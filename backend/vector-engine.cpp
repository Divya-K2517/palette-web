//VectorEngine does the vector search(communicating with weaviate), cahches results, and handles images
#include "coreSystems/core-systems.hpp"
#include <iostream>
#include <curl/curl.h>
#include <eigen3/Eigen/Dense>
#include <algorithm>
#include <future>
#include <sstream>
#include <cstdlib> //for std::getenv

namespace coreSystems { 
    struct pinterestImage {
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
            std:: string data;
            long responseCode;
        };

        static size_t writeCallback(void* contents, size_t size, size_t nmemb, curlResponse* response) {
            //when data comes back from a request it will add it to curlResponse.data
            //nmemb is number of elements
            size_t totalSize = size * nmemb;
            response -> data.append(static_cast<char*>(contents), totalSize)
                //casting void* contents to char*
            return totalSize; //tells curl how many bytes were written
        }
    public: 
        explicit WeaviateClient(std:: string& baseUrl, const std::string& apiKey) //constructor
            : baseUrl(baseUrl), apiKey(apiKey), curlHandle(nullptr) { //initialziing the baseUrl to the input and curlHandle to nullptr
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
        //post to weaviate endpoint
        //parse results
        //return a vector of Nodes
        std::vector<Node> semanticSearch(const std::string& query) { 
            //locking curlHandle with curlMutex
            std::lockGuard<std::mutex> lock(curlMutex);
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
                      << " (HTTP " << response.response_code << ")" << std::endl;
            }

            //getting HTTP response code
            curl_easy_getinfo(curlHandle, CURLINFO_RESPONSE_CODE, &response.responseCode);
            std::cout << "Weaviate Response Code: " << response.responseCode << std::endl;
            std::cout << "Weaviate Response Data: " << response.data << std::endl;

            //parsing JSON response
            try {
                auto jsonResponse = nlohmann::json::parse(response.data);
                return parseWeaviateResponse(jsonResponse, query); //TODO: write parseWeaviateResponse
            } catch (const std::exception& e) {
                std::cerr << "Failed to parse Weaviate response: " << e.what() << std::endl;
                return {};
            }
        }
    private:
        //function to parse Weaviate JSON response and returns a vector of Nodes
        std::vector<Node> parseWeaviateResponse (const nlohmann::json& response, const std::string& original_query) {
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
            
            for (const auto& concept : concepts) { //iterating over each concept in the concepts array
                Node node;
                node.id = utils::generateUUID(); //generating a unique ID for the node
                node.name = concept.value("name", ""); //getting the name field, defaulting to empty string if not present
                node.similarityScore = concept["_additional"].value("certainty", 0.0f); //getting certainty from _additional field
                node.timestamp = utils::getCurrentTime(); //setting current time as timestamp
                node.healthStatus = SystemHealth::NOMINAL; 
                
                //extracting embedding vector
                if (concept["_additional"].contains("vector")) { 
                    const auto& vector = concept["_additional"]["vector"]; //reference to vector (ex. [0.1, 0.2, 0.3] - array of floats)
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

    };
}