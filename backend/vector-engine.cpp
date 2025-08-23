//VectorEngine does the vector search(communicating with weaviate), cahches results, and handles images
#include "core-systems.hpp"
#include <iostream>
#include <curl/curl.h>
#include <eigen3/Eigen/Dense>
#include <algorithm>
#include <future>
#include <sstream>
#include <cstdlib> //for std::getenv
#include <chrono>
#include <filesystem>
#ifdef _WIN32
    #include <windows.h>
    #include <psapi.h>
#else 
    #include <sys/resource.h>
#endif

#include <fstream>
#include <string>
#include <map>


namespace CoreSystems { 
    struct PinterestImage {
        std::string id;
        std::string url;
        std::string description;
        std::string boardName; //which board the image came from, maybe not needed?
        //toJson converts the struct to a JSON object
        nlohmann::json toJson() const {
            return nlohmann::json{
                {"id", id},
                {"url", url}, 
                {"description", description},
                {"boardName", boardName}
            };
        }
    };
    class WeaviateClient { //for semantic search
    private:
        std::string baseUrl;
        std::string apiKey; //for authentication
        CURL* curlHandle;
        std::mutex curlMutex; //to protect curl handle from concurrent access
        
        struct curlResponse { 
            std::string data;
            long responseCode;
        };

        static size_t writeCallback(void* contents, size_t size, size_t nmemb, curlResponse* response) {
            //when data comes back from a request it will add it to curlResponse.data
            //nmemb is number of elements
            size_t totalSize = size * nmemb;
            response -> data.append(static_cast<char*>(contents), totalSize);
                //casting void* contents to char*
            return totalSize; //tells curl how many bytes were written
        }
    public: 
        explicit WeaviateClient(std:: string& baseUrl, const std::string& apiKey) //constructor
            : baseUrl(baseUrl), apiKey(""), curlHandle(nullptr) { //initialziing the baseUrl to the input and curlHandle to nullptr
                curlHandle = curl_easy_init(); //creates a cURL, retuns nullptr if it fails
                if (!curlHandle) { //throwing error if curlHandle is nullptr
                    throw std::runtime_error("Failed to initialize CURL for WeaviateClient");
                }
            }
        ~WeaviateClient() { //destructor 
            if (curlHandle) { 
                curl_easy_cleanup(curlHandle); //cleans up the curlHandle
            }
        }
        // Function to perform a semantic search
        //given a search string it will construct a GraphQL query
        //also takes in level (0=query, 1=first level, 2=second level) to give to parseWeaviateResponse
        //post to weaviate endpoint
        //parse results
        //return a vector of Nodes in descending order of closeness to query (most related nodes come first)
        std::vector<Node> semanticSearch(const std::string& query, const int level) { 
            //locking curlHandle with curlMutex
            std::lock_guard<std::mutex> lock(curlMutex);
            //constructing the GraphQL query
            std::stringstream graphqlQuery;
            graphqlQuery << R"({
                "query": "{ Get { Concept(nearText: { concepts: [\")" << query << R"(\"] } limit: 10) { 
                    name 
                    description 
                    _additional { 
                        certainty 
                        vector 
                    } 
                }}}"
            })";
            std::string postData = graphqlQuery.str();
            std::cout << "Weaviate GraphQL Query: " << postData << std::endl;
            
            std::string url = baseUrl + "/v1/graphql";

            curlResponse response; //curlResponse object to store data
    
            //configuring curl
            curl_easy_setopt(curlHandle, CURLOPT_URL, url.c_str()); //setting target url
            curl_easy_setopt(curlHandle, CURLOPT_POSTFIELDS, postData.c_str()); //configures the postData query
            curl_easy_setopt(curlHandle, CURLOPT_WRITEFUNCTION, writeCallback); //to handle incoming data
            curl_easy_setopt(curlHandle, CURLOPT_WRITEDATA, &response); //passes address of response to writeCallback 

            std::cout << "Sending request to Weaviate: " << url << std::endl;
            //setting headers
            struct curl_slist* headers = nullptr; //linked list of headers
            headers = curl_slist_append(headers, "Content-Type: application/json");
            if (!apiKey.empty()) { //adding API key to headers if it exists
                std::string authHeader = "Authorization: Bearer " + apiKey;
                headers = curl_slist_append(headers, authHeader.c_str());
                std::cout << "added api key to curl headers" << std::endl;
            }
            curl_easy_setopt(curlHandle, CURLOPT_HTTPHEADER, headers); //applies headers to curl request
            //executin request
            CURLcode res = curl_easy_perform(curlHandle);
            curl_slist_free_all(headers); //frees memory used by headers
                //RES VS RESPONSE:
                //response is the curlResponse struct that stores data from weaviate and the response code
                //res is a CURLcode that just indicates if the request worked, not the HTTP response code
            //checking for errors
            if (res != CURLE_OK || response.responseCode != 200) {
                std::cerr << "Weaviate request failed: " << curl_easy_strerror(res) 
                      << " (HTTP " << response.responseCode << ")" << std::endl;
            }

            //getting HTTP response code
            curl_easy_getinfo(curlHandle, CURLINFO_RESPONSE_CODE, &response.responseCode);
            std::cout << "Weaviate Response Code: " << response.responseCode << std::endl;
            std::cout << "Weaviate Response Data: " << response.data << std::endl;

            //parsing JSON response
            try {
                auto jsonResponse = nlohmann::json::parse(response.data);
                std::cout << " Weaviate Response: " << jsonResponse.dump(2) << std::endl;
                return parseWeaviateResponse(jsonResponse, query, level); 
            } catch (const std::exception& e) {
                std::cerr << "Failed to parse Weaviate response: " << e.what() << std::endl;
                return {};
            }
        }
    private:
        //function to parse Weaviate JSON response and returns a vector of Nodes
        std::vector<Node> parseWeaviateResponse (const nlohmann::json& response, const std::string& original_query, const int level) {
            //using references to inputs to avoid copying and save memory - operations affect the original object
            std::vector<Node> results;

            if (!response.contains("data") || !response["data"].contains("Get") || !response["data"]["Get"].contains("Concept")) { 
                return results; //returning empty vector if response doesn't have expected structure
                std::cout << "Weaviate response missing expected fields, returning an empty vector" << std::endl;
            }
            std::cout <<"Raw weaviate response: " << response << std::endl;

            const auto& concepts = response["data"]["Get"]["Concept"];
                //read only reference to the concepts array in the json response
                //concepts isnt an array by itself, but its memory spot points to the same spot as response["data"]["Get"]["Concept"] 
            
            for (const auto& c : concepts) { //iterating over each concept in the concepts array
                Node node;
                node.id = utils::generateUUID(); //generating a unique ID for the node
                node.name = c.value("name", ""); //getting the name field, defaulting to empty string if not present
                node.similarityScore = c["_additional"].value("certainty", 0.0f); //getting certainty from _additional field
                node.timestamp = utils::getCurrentTime(); //setting current time as timestamp
                node.healthStatus = SystemHealthEnum::NOMINAL; 
                node.level = level;
                
                //extracting embedding vector
                if (c["_additional"].contains("vector")) { 
                    const auto& vector = c["_additional"]["vector"]; //reference to vector (ex. [0.1, 0.2, 0.3] - array of floats)
                    if (vector.is_array()) { //checking if vector is an array 
                        for (const auto& val : vector) {
                            node.embedding.push_back(val.get<float>()); //adding each float in the vector array to node.embedding
                        }
                    }
                }
                results.push_back(std::move(node)); 
                    //std::move(node) converts noce from lvalue(named object) to rvalue(temp)
                    //push_back() detects that node is an rvalue and steals the memory+poiters from node instead of copying
                    //more efficient than copying (which is just push_back(node))
            }
            return results; //returning the vector of Nodes
        }
    };

    class PinterestClient {
        std::string apiKey;
        CURL* curlHandle;
        std::mutex curlMutex;

        //rate limiting
        std::atomic<uint32_t> requestsMade{0};
            //atomic variables let multiple threads access them without locks
        std::chrono::system_clock::time_point windowStart;
        static constexpr uint32_t MAX_REQUESTS_PER_DAY = 1000; 
        std::mutex rateLimitMutex;

        struct curlResponse { 
            std::string data;
            long responseCode;
        };

        static size_t writeCallback(void* contents, size_t size, size_t nmemb, curlResponse* response) { 
            //when data comes back from a request it will add it to curlResponse.data
            //nmemb is number of elements
            size_t totalSize = size * nmemb;
            response -> data.append(static_cast<char*>(contents), totalSize);
                //casting void* contents to char*
            return totalSize; //tells curl how many bytes were written
        }
    public:
        explicit PinterestClient(const std::string& apiKey) 
            : apiKey(apiKey), curlHandle(nullptr), windowStart(std::chrono::system_clock::now()) {
                curlHandle = curl_easy_init(); //creates a cURL, retuns nullptr if it fails
                if (!curlHandle) {
                    throw std::runtime_error("Failed to initialize CURL for PinterestClient");
                }
        }
        ~PinterestClient() { //destructor
            if (curlHandle) {
                curl_easy_cleanup(curlHandle); //cleans up the curlHandle
            } 
        }

        bool canMakeRequest() { 
            std::lock_guard<std::mutex> lock(rateLimitMutex);
            
            auto now = std::chrono::system_clock::now();

            auto hoursElapsed = std::chrono::duration_cast<std::chrono::hours>(now - windowStart).count();
            int daysElapsed =  static_cast<int>(hoursElapsed / 24); //integer division
            
            // reset window if a day has passed
            if (daysElapsed >= 1) {
                requestsMade = 0;
                windowStart = now;
            }
            return requestsMade < MAX_REQUESTS_PER_DAY; //returning true if requests made is less than max allowed
        }
        uint32_t getRemainingRequests() { //requests left that can be made today
            std::lock_guard<std::mutex> lock(rateLimitMutex);
            return MAX_REQUESTS_PER_DAY - requestsMade.load(); 
        }

        std::vector<PinterestImage> searchPins (const std::string& query) { //makes request to pinterest for pins
            if (!canMakeRequest()) {
                std::cout << "Pinterest rate limit exceeded, using cached data instead" << std::endl;
                return {};
            }

            std::lock_guard<std::mutex> lock(curlMutex);
            requestsMade++;

            //constructing the Pinterest API search URL
            char* escapedQuery = curl_easy_escape(curlHandle, query.c_str(), query.length());
            if (!escapedQuery) {
                std::cerr << "Failed to escape query for Pinterest API" << std::endl;
                return {};
            }
            std::string url = "https://api.pinterest.com/v5/pins/search?query=" + 
                         std::string(escapedQuery) + 
                         "&limit=10";

            curlResponse response;

            //configuring curl for pinterest api
            curl_easy_setopt(curlHandle, CURLOPT_URL, url.c_str()); //sets target url for api
            curl_easy_setopt(curlHandle, CURLOPT_HTTPGET, 1L); //making the request a GET request
            curl_easy_setopt(curlHandle, CURLOPT_WRITEFUNCTION, writeCallback); //assigning writeCallback to handle incoming data
            curl_easy_setopt(curlHandle, CURLOPT_WRITEDATA, &response); //passes address of response (then writeCallback uses response)
            curl_easy_setopt(curlHandle, CURLOPT_TIMEOUT, 15L); //15 second timeout for api

            //auth header
            struct curl_slist* headers = nullptr; //headers points to the spot in memory where the actual headers are stored
            std::string authHeader = "Authorization: Bearer " + apiKey;
            headers = curl_slist_append(headers, authHeader.c_str());
            curl_easy_setopt(curlHandle, CURLOPT_HTTPHEADER, headers);

            //execute request
            CURLcode res = curl_easy_perform(curlHandle);
                //RES VS RESPONSE:
                //response is the curlResponse struct that stores data from weaviate and the HTTP response code
                //res is a CURLcode that just indicates if the request worked, not the HTTP response code
            curl_easy_getinfo(curlHandle, CURLINFO_RESPONSE_CODE, &response.responseCode);
            curl_slist_free_all(headers);

            if (res != CURLE_OK || response.responseCode != 200) {
                std::cerr << "Pinterest request failed: " << curl_easy_strerror(res) 
                      << " (HTTP " << response.responseCode << ")" << std::endl;
                return {};
            }

            //parse pinterest api response
            try {
                auto jsonResponse = nlohmann::json::parse(response.data); //converts json string into a C++ nlohmann::json object
                std::cout << "raw pinterest api response: " << jsonResponse.dump(2) << std::endl;
                return parsePinterestResponse(jsonResponse);
            } catch (const std::exception& e) {
                std::cerr << "Failed to parse Pinterest response: " << e.what() << std::endl;
                return {};
            }
        }
    private: 
            std::vector<PinterestImage> parsePinterestResponse (const nlohmann::json& response) {
                std::vector<PinterestImage> images;
                
                if (!response.contains("items") || 
                    !response["items"].is_array() || 
                    response["items"].empty()) 
                {
                    return images;
                }
                for (const auto& item : response["items"]) {
                    PinterestImage image;
                    image.id = item.value("id", "");
                    image.description = item.value("description", "");
                    //getting url
                    if (item.contains("media") && item["media"].contains("images")) {
                        //trying different possible URL locations
                        if (item["media"]["images"].contains("originals") && item["media"]["images"]["originals"].contains("url")) {
                            image.url = item["media"]["images"]["originals"]["url"];
                        } else if (item["media"]["images"].contains("url")) {
                            image.url = item["media"]["images"]["url"];
                        }
                    }
                    //getting board name (idt there will be one most times)
                    if (item.contains("board") && item["board"].contains("name")) {
                        image.boardName = item["board"]["name"];
                    }
                    if (!image.id.empty() && !image.url.empty()) { //return image only if it has id and url
                        images.push_back(std::move(image));
                    }
                }
                return images;
            }
        };

        //implementing VectorEngine
        VectorEngine::VectorEngine(const std::string& engineType) //engine type is "primary" or "backup"
            : engineId(utils::generateUUID()), //setting up vector engine member variables
            engineType(engineType),
            lastCacheUpdate(std::chrono::system_clock::now()) {
        }

        VectorEngine::~VectorEngine() {
            shutdown();
        }
        //for loading env variables
        std::map<std::string,std::string> load_env(const std::string& path = ".env") {
            std::map<std::string,std::string> vars;
            std::ifstream file(path);
            std::string line;
            while(std::getline(file, line)) {
                if(line.empty() || line[0] == '#') continue;
                auto pos = line.find('=');
                if(pos == std::string::npos) continue;

                std::string key = line.substr(0, pos);
                std::string value = line.substr(pos+1);

                // trim whitespace
                key.erase(0, key.find_first_not_of(" \t"));
                key.erase(key.find_last_not_of(" \t") + 1);
                value.erase(0, value.find_first_not_of(" \t"));
                value.erase(value.find_last_not_of(" \t") + 1);

                // remove surrounding quotes if present
                if(!value.empty() && ((value.front() == '"' && value.back() == '"') ||
                                    (value.front() == '\'' && value.back() == '\''))) {
                    value = value.substr(1, value.size()-2);
                }

                vars[key] = value;

                std::string assignment = key + "=" + value;
                 _putenv(assignment.c_str());
            }
            return vars;
        }
        bool VectorEngine::initialize() {
            try { //initializing weaviate and pinterest clients
                std::string weaviateUrl = (engineType == "primary") ? "http://localhost:8080" : "http://backup-weaviate:8080";
                
                std::cout << "Current working directory: " << std::filesystem::current_path() << std::endl;
                std::cout << "Looking for .env file..." << std::endl;

                //getting weaviate api key from environment variable
                auto env = load_env("backend/.env"); //loading environment variables from .env file
                std::string weaviateApiKey = "";

                weaviateClient = std::make_unique<WeaviateClient>(weaviateUrl, weaviateApiKey);
                    //std::make_unique returns a std::unique_ptr<WeaviateClient>
                    //this object will be deleted when unique_ptr goes out of scope (vector engine objecft is deleted or weaviateClient is reset/gets new pointer)
                    //prevents memory leacks

                //getting pinterest api key from environment variable
                std::string pinterestApiKey = env.count("PINTEREST_API_KEY") ? 
                    env["PINTEREST_API_KEY"] : "" ;
                if (pinterestApiKey.empty()) {
                    std::cerr << "PINTEREST_API_KEY is not set" << std::endl;
                } else {
                    std::cout << "PINTEREST_API_KEY loaded from .env file" << std::endl;
                }

                pinterestClient = std::make_unique<PinterestClient>(pinterestApiKey);
                    //weaviateClient and pinterestClient are pointers bc of std::make_unique
                    //they point to the address of the new WeaviateClient/PinterestClient object
                isOperational.store(true);
                std::cout << engineType << " Vector Engine initialized: " << engineId << std::endl;

                return true;
            } catch (const std::exception& e) {
                std::cerr << "Failed to initialize " << engineType << " Vector Engine: " << e.what() << std::endl;
                return false;
            }
        }
        void VectorEngine::shutdown() {
            isOperational = false;
            clearCache();
        }
        std::vector<Node> VectorEngine::vectorSearch(const std::string& query) {
            if (!isOperational) {
                throw std::runtime_error(engineType + " Vector Engine is not operational");
            }
            std::cout << engineType << " Engine: Starting vector search for '" << query << "'" << std::endl;
            //checking local cache first
            auto cachedResults = checkCache(query);
            if (!cachedResults.empty()) { //match found in cache
                std::cout << "Cached result found for query: " << query << std::endl; 
                return cachedResults;
            }
            
            auto relatedNodes = weaviateClient -> semanticSearch(query, 1); //using -> bc weaviateClient is a pointer to the acc WeaviateClient object
            if (relatedNodes.empty()) {
                std::cout << "No related concepts found for query: " << query << std::endl;
                return {};
            }
            
            std::vector<Node> allNodes = relatedNodes;

            //getting second level nodes for top 3 nodes 
            size_t numTopNodes = std::min(static_cast<size_t>(3), relatedNodes.size());
                //finds how many top nodes there are(either 3 or less than 3 if relatedConcepts has less than 3)
            for (size_t i = 0; i < numTopNodes; i++) { 
                //getting related nodes for each in relatedNodes
                //semanticSearch returns nodes in descending order of closeness to query, so relatedNodes[0] is the node closest to query
                auto secondLevelNodes = weaviateClient -> semanticSearch(relatedNodes[i].name, 2);
                allNodes.insert(allNodes.end(), secondLevelNodes.begin(), secondLevelNodes.end()); //adding second level nodes to end of allNodes
            }

            //adding pinterest images to each node (asynchronous)
            auto enhancedNodes = enhanceWithPinterestData(std::move(allNodes));;
            
            updateCache(query, enhancedNodes);
            std::cout << engineType << " Engine: Found " << enhancedNodes.size() << " enhanced nodes" << std::endl;
            return enhancedNodes;

        }
        std::vector<Node> VectorEngine::checkCache(const std::string& query) {
            std::lock_guard<std::mutex> lock(cacheMutex); 
            
            auto result = searchCache.find(query);
                //if the query doesn't exist in the cache, then result will be a pointer to the end of searchCache container

            if (result != searchCache.end()) { //if false, them result points to searchCache.end() which means its not in cache
                //checking if cache is valid
                auto now = std::chrono::system_clock::now();
                if (now - lastCacheUpdate < CACHE_EXPIRY_TIME) {
                    //returning the value of the query key
                    return result -> second; 
                } else { //cache has expired
                    searchCache.erase(result);
                }
            }
            return {};
        }
        void VectorEngine::updateCache(const std::string& query, const std::vector<Node>& nodes) {
            std::lock_guard<std::mutex> lock(cacheMutex);
            //assiging the new nodes as the value to the key/query
            searchCache[query] = nodes;
            lastCacheUpdate = std::chrono::system_clock::now();

            //limiting cache size
            if (searchCache.size() > 1000) {
                //removing the 100 oldest queries
                auto oldest = searchCache.begin();
                searchCache.erase(oldest, std::next(oldest, 100));
            }
        }
        std::vector<Node> VectorEngine::enhanceWithPinterestData(std::vector<Node> nodes) {
            std::cout << "Enhancing " << nodes.size() << " nodes with Pinterest data" << std::endl;
            
            //Pinterest requests done asynchronously for better performance
            std::vector<std::future<std::vector<PinterestImage>>> pinterestFutures;
                //std::future allows these operations to be done without disrupting the main program
                //returns std::vector<PinterestImage>, a lost of PinterestImage objects
                //need to call .get() on pinterestFutures to get the result
            for (const auto& node : nodes) {//for node in nodes
                auto future = std::async(std::launch::async, [this, &node]() {
                    return pinterestClient->searchPins(node.name);
                });
                //std::async runs the task in the background
                //std::launch::async tells the program to launch the task in a new thread
                //[this, &node] gives the current class and access to node by reference

                pinterestFutures.push_back(std::move(future));
            }

            //getting pinterest results
            for (size_t i = 0; i < nodes.size() && i < pinterestFutures.size(); i++) {
                try {
                    //each future is a bunch of pinterest images related to that node
                    auto images = pinterestFutures[i].get();
                    if (!images.empty()) {
                        std::lock_guard<std::mutex> lock(cacheMutex); //preventing other threads from accessing imageCache
                            //^locks the cacheMutex so if other threads try to lock that mutex they will be blocked until cacheMutex is unlocked by og thread
                        imageCache[nodes[i].name] = std::move(images); //adding images to cache
                    }
                } catch (const std::exception& e) {
                    std::cerr << "Pinterest enhancement failed for '" << nodes[i].name << "': " << e.what() << std::endl;
                }
            }
            return nodes;
        }
        void VectorEngine::clearCache() {
            std::lock_guard<std::mutex> lock(cacheMutex);
            searchCache.clear();
            imageCache.clear();
        }
        size_t VectorEngine::getCacheSize() {
            std::lock_guard<std::mutex> lock(cacheMutex);
            return searchCache.size() + imageCache.size();
        }

        std::vector<PinterestImage> VectorEngine::getPinterestImages(const std::string& conceptName)  {
            std::lock_guard<std::mutex> lock(cacheMutex);
            auto it = imageCache.find(conceptName);
            if (it != imageCache.end()) { //if conceptName is in the cache
                return it->second; //returning the vector of pinterest images for that concept
            }
            return {};
        }
        bool VectorEngine::refreshPinterestData(const std::string& conceptName) {
            try {
                if (conceptName.empty()) {
                    //clearing entire pinterest cache
                    std::lock_guard<std::mutex> lock(cacheMutex);
                    imageCache.clear();
                    std::cout << "All Pinterest image cache cleared" << std::endl;
                    return true;
                } else {
                    //removing concept from cache then fetching fresh data
                    {
                        std::lock_guard<std::mutex> lock(cacheMutex);
                        imageCache.erase(conceptName);
                    }
                    //fetching fresh Pinterest data
                    if (pinterestClient && pinterestClient->canMakeRequest()) {
                        auto images = pinterestClient->searchPins(conceptName);
                        if (!images.empty()) {
                            std::lock_guard<std::mutex> lock(cacheMutex);
                            imageCache[conceptName] = std::move(images);
                            std::cout << "Pinterest data refreshed for: " << conceptName << std::endl;
                            return true;
                        }
                    }
                }
            } catch (const std::exception& e) {
                std::cerr << "Failed to refresh Pinterest data: " << e.what() << std::endl;
            }
            return false;
        }

        //system manager functions
        SystemManager::SystemManager() : startTime(std::chrono::system_clock::now()) {
            healthMetrics = std::make_unique<SystemHealthMetrics>();
        }
        SystemManager::~SystemManager() {
            shutdown();
        }
        bool SystemManager::initialize() {
            try {
                //initializing telemetry processor
                telemetryProcessor = std::make_unique<TelemetryProcessor>();
                telemetryProcessor->start();

                //initializing vector engines
                primaryVectorEngine = std::make_unique<VectorEngine>("primary");
                backupVectorEngine = std::make_unique<VectorEngine>("backup");

                if (!primaryVectorEngine->initialize()) {
                    std::cerr << "Failed to initialize primary vector engine" << std::endl;
                    return false;
                }
                if (!backupVectorEngine->initialize()) {
                    std::cerr << "Warning: Failed to initialize backup vector engine, continuing with primary only" << std::endl;
                }
                //starting background threads
                telemetryThread = std::thread([this]() { telemetryWorker(); });
                healthMonitorThread = std::thread([this]() { healthMonitorWorker(); });

                std::cout << "SystemManager initialized successfully" << std::endl;
                return true;
            } catch (const std::exception& e) {
                std::cerr << "Failed to initialize SystemManager: " << e.what() << std::endl;
                return false;
            }
        }
        void SystemManager::shutdown() {
             std::cout << "SystemManager shutting down..." << std::endl;

             shutdownRequested.store(true);
             //stop telemetry processor
            if (telemetryProcessor) {
                telemetryProcessor->stop();
            }
            
            //shutdown vector engines
            if (primaryVectorEngine) {
                primaryVectorEngine->shutdown();
            }
            if (backupVectorEngine) {
                backupVectorEngine->shutdown();
            }
            //join threads
            //joining threads will make the thread that called it wait until the thread being joined finishes execution
            //then the thread that was called on its reasources are freed

            if (telemetryThread.joinable()) {
                telemetryThread.join();
            }
            if (healthMonitorThread.joinable()) {
                healthMonitorThread.join();
            }
            std::cout << "SystemManager shutdown complete" << std::endl;
        }
        std::vector<Node> SystemManager::search(const std::string& query) {
            std::cout << "SystemManager: Processing search for '" << query << "'" << std::endl;

            try {
                //try searching w/ primary engine first
                if (primaryVectorEngine && primaryVectorEngine->isEngineOperational()) {
                    return primaryVectorEngine->vectorSearch(query);
                }
                //fallback to backup engine
                else if (backupVectorEngine && backupVectorEngine->isEngineOperational()) {
                    std::cout << "Primary engine unavailable, using backup" << std::endl;
                    return backupVectorEngine->vectorSearch(query);
                }
                else {
                    throw std::runtime_error("No operational vector engines available");
                }
            } catch (const std::exception& e) {
                std::cerr << "Search failed: " << e.what() << std::endl;
                healthMetrics->errorRate.store(healthMetrics->errorRate.load() + 0.01f);
                return {};
            }
        }
        SystemHealthEnum SystemManager::getSystemHealth() const {
            return healthMetrics -> getHealthStatus();
        }
        SystemHealthMetrics& SystemManager::getHealthMetrics() const {
            SystemHealthMetrics& metrics = *healthMetrics; //dereferencing the unique_ptr to get the actual object
            return metrics;
        }
        void SystemManager::recordTelemetry(const SearchTelemetry& telemetry) {
            std::lock_guard<std::mutex> lock(telemetryMutex);
            telemetryQueue.push(telemetry); //adding data to queue
            telemetryCv.notify_one();
            //notify_one() tells the telemetryWorker thread that new data is available
            //if the thread is waiting, it will wake up and process the new data

        }
        bool SystemManager::emergencySubsystemRestart(const std::string& subsystemName) {
            std::cout << "System Manager: EMERGENCY RESTART: " << subsystemName << std::endl;
            try {
                 if (subsystemName == "primary" || subsystemName == "primary_engine") {
                    if (primaryVectorEngine) {
                        primaryVectorEngine->shutdown();
                        return primaryVectorEngine->initialize();
                    }
                } else if (subsystemName == "backup" || subsystemName == "backup_engine") {
                    if (backupVectorEngine) {
                        backupVectorEngine->shutdown();
                        return backupVectorEngine->initialize();
                    }
                } else if (subsystemName == "telemetry") {
                    if (telemetryProcessor) {
                        telemetryProcessor->stop();
                        telemetryProcessor->start();
                        return true;
                    }
                }
            } catch (const std::exception& e) {
                std::cerr << "System Manager: Failed to emergency restart subsystem '" << subsystemName << "': " << e.what() << std::endl;
            }
            return false;
        }
        uint64_t SystemManager::getUptimeMs() const {
            auto now = std::chrono::system_clock::now();
            return std::chrono::duration_cast<std::chrono::milliseconds>(now - startTime).count();
        }
        void SystemManager::telemetryWorker() {
            std::cout << "System Manager: Telemetry worker started" << std::endl;
            while(!shutdownRequested.load()) { //goes until shutdownRequested
                std::unique_lock<std::mutex> lock(telemetryMutex);
                telemetryCv.wait(lock, [this]() { return !telemetryQueue.empty() || shutdownRequested.load(); });
                    //thread will wait until either telemetryQueue is not empty or shutdownRequested is true

                while (!telemetryQueue.empty()) {
                    auto telemetry = telemetryQueue.front(); //getting the front of the queue
                    telemetryQueue.pop(); //removing the front of the queue
                    lock.unlock(); //unlocking the mutex so other threads can access telemetryQueue
                    if (telemetryProcessor) {
                        telemetryProcessor->processTelemetry(telemetry);
                    }
                    lock.lock(); //locking the mutex again
                }
            }
        }
        void SystemManager::healthMonitorWorker() {
            while(!shutdownRequested.load()) {
                try{
                    healthMetrics->cpuUsage.store(utils::calculateSystemLoad());
                        //calculateSystemLoad() calculates how busy the cpu is
                    #ifdef _WIN32 //for windows
                        PROCESS_MEMORY_COUNTERS pmc;
                        if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
                            float memoryMB = static_cast<float>(pmc.WorkingSetSize) / (1024.0f * 1024.0f);
                            healthMetrics->memoryUsage.store(memoryMB);
                        }
                    
                    #else //for linux
                    struct rusage useage;
                        //rusage is a struct that contains resource usage information

                    if (getrusage(RUSAGE_SELF, &useage) == 0) { //gets useage info for current program
                        //getrusage() returns 0 on success, -1 on error
                        float memoryMB = usage.ru_maxrss / 1024.0f; //KB to MB
                        healthMetrics->memoryUsage.store(memoryMB);
                    }
                    #endif
                    healthMetrics->lastHeartbeat.store(utils::getTimestampMs());
                }catch (const std::exception& e) {
                    std::cerr << "Health monitor worker error: " << e.what() << std::endl;
                }
                std::this_thread::sleep_for(std::chrono::seconds(5)); //sleep for 5 seconds before next update
            }
        }
        void TelemetryProcessor::start() {
            isRunning.store(true);
            std::cout << "TelemetryProcessor started" << std::endl;
        }
        void TelemetryProcessor::stop() {
            isRunning.store(false);
            std::cout << "TelemetryProcessor stopped" << std::endl;
        }
        void TelemetryProcessor::processTelemetry(const SearchTelemetry& telemetry) {
            if (!isRunning.load()) return; //if not running, do nothing

            std::lock_guard<std::mutex> lock(processingMutex);

            totalQueries.fetch_add(1);
            totalResponseTime.fetch_add(telemetry.processingTime);

            if (telemetryHistory.size() >= MAX_TELEMETRY_RECORDS) {
                telemetryHistory.erase(telemetryHistory.begin());
                //removing the oldest record if we have reached the max size
            }
            telemetryHistory.push_back(telemetry); //adding new telemetry record to history
            std::cout << "Telemetry processed: " << telemetry.searchPhrase << " in " 
                      << telemetry.processingTime<< "ms" << std::endl;
        }
        nlohmann::json TelemetryProcessor::getPerformanceReport() const {
            std::lock_guard<std::mutex> lock(processingMutex);
            //auto telemetryHistory
            return nlohmann::json{
                {"total_queries", getTotalQueries()},
                {"average_response_time", getAverageResponseTime()},
                {"error_rate", getErrorRate()},
                {"telemetry_records", telemetryHistory.size()},
                {"timestamp", utils::getTimestampMs()}

            };
        }
        //utils implementations
        namespace utils {
            inline std::string generateUUID() {
                UUID uuid;
                UuidCreate(&uuid);
                //convert UUID to string
                RPC_CSTR strUuid = nullptr;
                UuidToStringA(&uuid, &strUuid);

                std::string uuidStr;
                if (strUuid) {
                    uuidStr = reinterpret_cast<char*>(strUuid);
                    RpcStringFreeA(&strUuid);
                }
                return uuidStr;
            }
            std::chrono::system_clock::time_point getCurrentTime() {
                return std::chrono::system_clock::now(); //returns current time as a time_point
            }
            uint64_t getTimestampMs() {
                return std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
                //std::chrono::system_clock::now() gets current time
                //.time_since_epoch() gets time since epoch (1970-01-01 00:00:00 UTC)
                //std::chrono::duration_cast<std::chrono::milliseconds>() converts it to milliseconds
                //.count() returns the number of milliseconds as an integer
            }
            float calculateSystemLoad() {
                std::ifstream loadavg("/proc/loadavg"); 
                    //creates input file stream ifstream object loadavg
                    //opens the /proc/loadavg file which contains system load averages
                if (loadavg.is_open()) {
                    float load;
                    loadavg >> load;
                    return load * 100.0f; // Convert to percentage
                }
                return 0.0f;

            }
        }
    };
