//will take in user input from react and call search()
#include "httplib.h"
#include "json.hpp"
#include <thread>
#include <atomic>
#include <memory>
#include "core-systems.hpp"
#include "vector-engine.cpp"

namespace groundControl {
    class GraphQLHandler {
    private: 
        //pointer variable coreSystems to manage a SystemManager object
        //shared_ptr gives shared ownership and lets multiple pointers point to the same object
        //object deleted when shared_ptr goes out of scope and other shared_ptrs refering to the same object are destroyed
        std::shared_ptr<CoreSystems::SystemManager> systemManager;
    public:
        //constructor
        explicit GraphQLHandler(std::shared_ptr<CoreSystems::SystemManager> sm) 
            : systemManager(sm) {}
            //takes in a pointer to a SystemManager object and initializes coreSystems with cs
        
        //hangle graohQL queries
        nlohmann::json handleQuery(const nlohmann::json& request) {
            std::cout << "graphQL handler handling query request: " << request << std::endl;
            try {
                std::string query = request.value("query", "");
                nlohmann::json variables = request.value("variables", nlohmann::json::object());

                //parsing operation type
                if (query.find("search_concepts") != std::string::npos) {
                    return handleSearchConcepts(variables);
                } else if (query.find("system_health") != std::string::npos) {
                    return handleSystemHealth();
                } else if (query.find("pinterest_images") != std::string::npos) {
                    return handlePinterestImages(variables);
                } else if (query.find("telemetry_report") != std::string::npos) {
                    return handleTelemetryReport();
                } else {
                    return createErrorResponse("Unknown GraphQL operation");
                }

            } catch (const std::exception& e) {
                return createErrorResponse(std::string("Query processing error: ") + e.what());
            }
        }
        //handle graphql mutations
        nlohmann::json handleMutation(const nlohmann::json& request) {
            std::cout << "graphQL handler handling mutation request: " << request << std::endl;
            try {
                std::string query = request.value("query", "");
                nlohmann::json variables = request.value("variables", nlohmann::json::object());
                if (query.find("refresh_pinterest_data") != std::string::npos) {
                    return handleRefreshPinterestData(variables);
                } else if (query.find("emergency_restart") != std::string::npos) {
                    return handleEmergencyRestart(variables);
                } else if (query.find("clear_cache") != std::string::npos) {
                    return handleClearCache();
                } else {
                    return createErrorResponse("Unknown GraphQL mutation");
                }
            } catch (const std::exception& e) {
                return createErrorResponse(std::string("Mutation processing error: ") + e.what());
            }
        }
        //mutations vs queries:
        //queries are for reading/fetching data: GET
        //mutations modify data: POST/DELETE/PUT
    private:
        nlohmann::json handleSearchConcepts(const nlohmann::json& variables) {
            std::string searchQuery = variables.value("query", "");
            int limit = variables.value("limit", 10); //default is 10 node

            if (searchQuery.empty()) {
                return createErrorResponse("search query cannot be empty");
            }

            std::cout << "Ground Control: Initiating search mission for '" << searchQuery << "'" << std::endl;

            //starting timer for performance
            CoreSystems::utils::PerformanceTimer timer;

            //executing search for nodes
            auto nodes = systemManager -> search(searchQuery); //using -> bc coreSystems is a pointer to the acc system manager object
            //enforce limit if needed
            if (limit > 0 && nodes.size() > static_cast<size_t>(limit)) {
                nodes.resize(limit); //will cut off elements at the end if the size of nodes is greater than limit
            }

            auto processingTime = timer.elapsedMs();
            auto healthStatus = systemManager -> getSystemHealth();
            //record telemetry 
            CoreSystems::SearchTelemetry telemetry;
            telemetry.searchId = CoreSystems::utils::generateUUID();
            telemetry.searchPhrase = searchQuery;
            telemetry.processingTime = processingTime;
            telemetry.nodesFound = nodes.size();
            telemetry.timestamp = CoreSystems::utils::getCurrentTime();
            systemManager -> recordTelemetry(telemetry);

            //building graphQL response
            nlohmann::json data = {
                {"search_concepts", {
                    {"mission_id", CoreSystems::utils::generateUUID()},
                    {"query", searchQuery},
                    {"nodes", nlohmann::json::array()},
                    {"processing_time_ms", processingTime},
                    {"system_status", CoreSystems::systemHealthToString(healthStatus)},
                    {"pinterest_integration_status", "ACTIVE"},
                    {"timestamp", CoreSystems::utils::getTimestampMs()}
                }}
            };
            //converting nodes to JSON
            for (const auto& node : nodes) {
                data["search_concepts"]["nodes"].push_back(node.toJson());
            }
            return {{"data", data}};
        }

        nlohmann::json handleSystemHealth() {
            std::cout << "ground control system health check starting..." << std::endl;
            try {
                auto healthStatus = systemManager -> getSystemHealth();
                auto healthMetrics = systemManager -> getHealthMetrics();
                nlohmann::json data = {
                    {"system_health", {
                        {"status", CoreSystems::systemHealthToString(healthStatus)},
                        {"cpu_useage", healthMetrics.cpuUsage.load()},
                        {"memory_usage", healthMetrics.memoryUsage.load()},
                        {"timestamp", CoreSystems::utils::getTimestampMs()},
                        {"active_connections", healthMetrics.activeConnections.load()},
                        {"error_rate", healthMetrics.errorRate.load()},
                        //{"uptime_ms", CoreSystems::utils::getTimestampMs()},
                        {"version", "1.0.0"},
                        {"primary_engine_operational", systemManager->getPrimaryVectorEngine() ? systemManager->getPrimaryVectorEngine()->isEngineOperational() : false},
                        {"backup_engine_operational", systemManager->getBackupVectorEngine() ? systemManager->getBackupVectorEngine()->isEngineOperational() : false}
                    }}
                };

                return {{"data", data}};
            } catch (const std::exception& e) {
                return createErrorResponse(std::string("Health check failed: ") + e.what());
            }
        }
        nlohmann::json handlePinterestImages(const nlohmann::json& variables) {
            std::string conceptName = variables.value("concept", "");
            if (conceptName.empty()) {
                return createErrorResponse("Concept name cannot be empty");
            }

            std::cout << "ground control, pinterest image request for " << conceptName << std::endl;
            try {
                std::vector<CoreSystems::pinterestImage> images;
                //get images from vector engine cache
                if (systemManager->getPrimaryVectorEngine() && systemManager->getPrimaryVectorEngine()->isEngineOperational()) {
                    images = systemManager->getPrimaryVectorEngine()->getPinterestImages(conceptName);
                }
                //converting images to json and adding to array
                nlohmann::json imageArray = nlohmann::json::array();
                for (const auto& img : images) {
                    imageArray.push_back(img.toJson());
                }

                nlohmann::json data = {
                    {"pinterest_images", {
                        {"concept", conceptName},
                        {"images", imageArray},
                        {"cached", !images.empty()},
                        {"count", images.size()},
                        {"timestamp", CoreSystems::utils::getTimestampMs()}
                    }}
                };
                return {{"data", data}};

            } catch (const std::exception& e) {
                std::cerr << "Failed to initialize HTTP Server: " << e.what() << std::endl;
                return false;
            }
            
            
            // //TODO: This would typically interface with your VectorEngine's image cache
            // // For now, return a placeholder response
            // nlohmann::json data = {
            //     {"pinterest_images", {
            //         {"concept", conceptName},
            //         {"images", nlohmann::json::array()},
            //         {"cached", true},
            //         {"timestamp", CoreSystems::utils::getTimestampMs()}
            //     }}
            // };

            // return {{"data", ""}};
        }
        nlohmann::json handleTelemetryReport() {
            std::cout << "Ground Control: Telemetry report requested" << std::endl;
            try{
                auto telemetryProcessor = systemManager->getTelemetryProcessor();
                if (telemetryProcessor) {
                    auto report = telemetryProcessor->getPerformanceReport();

                    nlohmann::json data = {
                        {"total_queries", report["total_queries"]},
                        {"average_response_time", report["average_response_time"]},
                        {"error_rate", report["error_rate"]},
                        {"telemetry_records", report["telemetry_records"]},
                        {"timestamp", report["timestamp"]}
                    };
                    return {{"data", data}};
                } else {
                    return createErrorResponse("Telemetry processor not available");
                }
            }catch (const std::exception& e) {
                return createErrorResponse(std::string("Telemetry report error: ") + e.what());
            }
            // //TODO: interface w/ Telemetry Processor
            // nlohmann::json data = {
            //     {"telemetry_report", {
            //         {"total_queries", 0}, // Would get from TelemetryProcessor
            //         {"average_response_time", 0.0}, // Would get from TelemetryProcessor
            //         {"error_rate", 0.0}, // Would get from TelemetryProcessor
            //         {"cache_hit_rate", 0.0},
            //         {"timestamp", CoreSystems::utils::getTimestampMs()}
            //     }}
            // };
            
            // return {{"data", data}};
        }
        nlohmann::json handleRefreshPinterestData(const nlohmann::json& variables) {
           std::string conceptName = variables.value("concept", "");
           std::cout << "Ground Control: Pinterest data refresh for '" << conceptName << "'" << std::endl;

           try {
                bool success = false;
                if (systemManager->getPrimaryVectorEngine() && systemManager->getPrimaryVectorEngine()->isEngineOperational()) {
                    success = systemManager->getPrimaryVectorEngine()->refreshPinterestData(conceptName);
                }
                nlohmann::json data = {
                    {"refresh_pinterest_data", {
                        {"concept", conceptName},
                        {"success", success},
                        {"message", success ? "Pinterest data refresh successful" : "Pinterest data refresh failed"},
                        {"timestamp", CoreSystems::utils::getTimestampMs()}
                    }}
                };
                return {{"data", data}};
           } catch (const std::exception& e) {
                return createErrorResponse(std::string("Pinterest data refresh error: ") + e.what());
           }
        //    //TODO: This would trigger a refresh of Pinterest data
        //     nlohmann::json data = {
        //         {"refresh_pinterest_data", {
        //             {"concept", conceptName},
        //             {"success", true},
        //             {"message", "Pinterest data refresh initiated"},
        //             {"timestamp", CoreSystems::utils::getTimestampMs()}
        //         }}
        //     };
            
        //     return {{"data", data}};

        }
        nlohmann::json handleEmergencyRestart(const nlohmann::json& variables) {
            std::string subsystemName = variables.value("subsystem", "");
            std::cout << "Ground Control: EMERGENCY RESTART requested for '" << subsystemName << "'" << std::endl;
        
            bool success = false;
            std::string message = "Unknown subsystem";

            try {
                success = systemManager->emergencySubsystemRestart(subsystemName);
                message = success ? "Emergency restart successful" : "Emergency restart failed";
            } catch (const std::exception& e) {
                message = std::string("Emergency restart error: ") + e.what();
            }
            nlohmann::json data = {
                {"emergency_restart", {
                    {"subsystem", subsystemName},
                    {"success", success},
                    {"message", message},
                    {"timestamp", CoreSystems::utils::getTimestampMs()}
                }}
            };
            
            return {{"data", data}};
        }
        nlohmann::json handleClearCache() {
            std::cout << "Ground Control: Cache clear requested" << std::endl;
            
            try {
                bool success = false;
                //clearing cache of both engines if they are operational
                if (systemManager->getPrimaryVectorEngine() && systemManager->getPrimaryVectorEngine()->isEngineOperational()) {
                    systemManager->getPrimaryVectorEngine()->clearCache();
                    success = true;
                }
                if (systemManager->getBackupVectorEngine() && systemManager->getBackupVectorEngine()->isEngineOperational()) {
                    systemManager->getBackupVectorEngine()->clearCache();
                }
                nlohmann::json data = {
                    {"clear_cache", {
                        {"success", success},
                        {"message", success ? "Cache cleared successfully" : "Failed to clear cache"},
                        {"timestamp", CoreSystems::utils::getTimestampMs()}
                    }}
                };
                
                return {{"data", data}};

            } catch (const std::exception& e) {
                return createErrorResponse(std::string("Cache clear error: ") + e.what());
            }
            // //TODO: This would clear the VectorEngine caches
            // nlohmann::json data = {
            //     {"clear_cache", {
            //         {"success", true},
            //         {"message", "Cache cleared successfully"},
            //         {"timestamp", CoreSystems::utils::getTimestampMs()}
            //     }}
            // };
            
            // return {{"data", data}};
        }
        nlohmann::json createErrorResponse(const std::string& message) {
            return {
                {"errors", {{
                    {"message", message},
                    {"timestamp", CoreSystems::utils::getTimestampMs()}
                }}}
            };
        }
    };
    class HttpServer{
    private:
        std::unique_ptr<httplib::Server> server;
        std::unique_ptr<GraphQLHandler> graphqlHandler;
        std::shared_ptr<CoreSystems::SystemManager> systemManager;
        std::atomic<bool> isRunning{false};
        std::thread serverThread;
        int port;
    public: 
        explicit HttpServer(int port = 8080) : port(port) {} //constructor

        ~HttpServer(){ //deconstructor
            shutdown();
        }
        bool initialize() {
            try {
                //initializing system manager
                systemManager = std::make_shared<CoreSystems::SystemManager>();
                if (!systemManager -> initialize()) {
                    std::cerr << "Failed to initialize SystemManager" << std::endl;
                    return false;
                }
                //initializing graphql handler
                graphqlHandler = std::make_unique<GraphQLHandler>(systemManager);

                //initialzing http server
                server = std::make_unique<httplib::Server>();

                //cors headers
                server -> set_pre_routing_handler([](const httplib::Request& req, httplib::Response& res){
                    res.set_header("Access-Control-Allow-Origin", "*");
                    res.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
                    res.set_header("Access-Control-Allow-Headers", "Content-Type, Authorization");
                    return httplib::Server::HandlerResponse::Unhandled;
                    //will run before every request and set these headers
                    //returns Unhandled to let the server know to continue processing the request
                });
                //hanlding options requests (sent automatically by browsers before complexx requests, servers must repsond w/ cors header)
                //server is configured to use this lambda function as the handler
                //returns nothing bc the cors headers are set above w/ set_pre_routing_header()
                server -> Options(".*", [](const httplib::Request&, httplib::Response& res){
                    return;
                });
                //graphQL endpoint
                server -> Post("/graphql", [this](const httplib::Request& req, httplib::Response& res){
                    //will reveice every GraphQL request from frontent
                    try {
                        //converting raw http request into nlohmann::json object
                        auto requestJson = nlohmann::json::parse(req.body); 

                        nlohmann::json response; //to store graphql repsonse that will be sent to client
                        std::string query = requestJson.value("query", ""); //getting the query
                        //query vs mutation handling
                        if (query.find("mutation") != std::string::npos) {
                            response = graphqlHandler->handleMutation(requestJson);
                        } else {
                            response = graphqlHandler->handleQuery(requestJson);
                        }
                        res.set_content(response.dump(), "application/json");
                    } catch (const std::exception& e) {
                        nlohmann::json errorResponse = {
                            {"errors", {{
                                {"message", std::string("Server error: ") + e.what()},
                                {"timestamp", CoreSystems::utils::getTimestampMs()}
                            }}}
                        };
                        res.set_content(errorResponse.dump(), "application/json");
                        res.status = 500;
                    }
                });
                //health check endpoint
                server -> Get("/health", [this](const httplib::Request&, httplib::Response& res){
                    try {
                        auto health = systemManager->getSystemHealth();
                        nlohmann::json healthResponse = {
                            {"status", "ok"},
                            {"health", health.toJson()},
                            {"timestamp", CoreSystems::utils::getTimestampMs()}
                        };
                        res.set_content(healthResponse.dump(), "application/json");
                    } catch (const std::exception& e) {
                        nlohmann::json errorResponse = {
                            {"status", "error"},
                            {"message", e.what()},
                            {"timestamp", CoreSystems::utils::getTimestampMs()}
                        };
                        res.set_content(errorResponse.dump(), "application/json");
                        res.status = 500;
                    }
                });
                // Static file serving for React frontend (optional)
                server->set_mount_point("/", "./public");

                std::cout << "Ground Control: HTTP Server initialized on port " << port << std::endl;
                return true;
            } catch (const std::exception& e) {
                std::cerr << "Failed to initialize HTTP Server: " << e.what() << std::endl;
                return false;
            }
        }
        bool start() {
            if (isRunning.load()) {
                //.load() checks value of isRunning & garantees this operation cant be disturbed by other threads
                //safer than just if (isRunning)
                std::cout << "Ground Control: Server already running" << std::endl;
                return false;
            }
            isRunning.store(true);
            //creating a new thread that starts the http server asynchronously
            serverThread = std::thread([this]() {
                std::cout << "Ground Control: Starting HTTP server on port " << port << std::endl;
                std::cout << "Ground Control: GraphQL endpoint available at http://localhost:" << port << "/graphql" << std::endl;
                std::cout << "Ground Control: Health check available at http://localhost:" << port << "/health" << std::endl;
                
                //server->listen() starts the server and binds to network
                //0.0.0.0 binds the server to all network interfaces on this port
                if (!server->listen("0.0.0.0", port)) {
                    std::cerr << "Ground Control: Failed to start server on port " << port << std::endl;
                    isRunning.store(false);
                }
                //giving server some time to start
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                
                return (isRunning.load())
            });
        }
        void shutdown() {
            if (!isRunning.load()) { //if already not running
                return;
            }
            std::cout << "Ground Control: Shutting down HTTP server..." << std::endl;
            
            isRunning.store(false);
            
            if (server) {
                server->stop();
            }
            if (serverThread.joinable()) { //.joinable() returns false if thread wasnt started or is always joined
                serverThread.join();
                //.join() blocks the current thread until serverThread completes and then releases thread reasources
            }
            if (systemManager) {
                systemManager->shutdown();
            }

            std::cout << "Ground Control: Server shutdown complete" << std::endl;
        }
        bool isServerRunning() const {
            return isRunning.load();
        }
    };
} //end of namespace groundControl

// Global server instance for signal handling
std::unique_ptr<GroundControl::HttpServer> globalServer;

void signalHandler(int signal) {
    std::cout << "\n🛑 Ground Control: Mission abort signal received..." << std::endl;
    if (globalServer) {
        globalServer->shutdown();
    }
    exit(0);
}

int main() { //main function
    std::cout << "🚀 Ground Control: Mission Control Server Starting..." << std::endl;
    
    globalServer = std::make_unique<GroundControl::HttpServer>(8080);

    //initialzing + starting server
    if (!globalServer->initialize()) {
        std::cerr << "❌ Ground Control: Failed to initialize server" << std::endl;
        return 1;
    }
    if (!globalServer->start()) {
        std::cerr << "❌ Ground Control: Failed to start server" << std::endl;
        return 1;
    }
    std::cout << "✅ Ground Control: Mission Control is GO for launch!" << std::endl;
    std::cout << "   GraphQL Playground: http://localhost:8080/graphql" << std::endl;
    std::cout << "   System Health: http://localhost:8080/health" << std::endl;

    //keeps server running until interrupted
    std::signal (SIGINT, signalHandler); 
        //SIGINT signal is generated by doing ctrl+C in terminal
        //exit(0) will termination the program

    //keeping main thread alive
    while (globalServer->isServerRunning()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        //checks every second if server is running
    }
    
    return 0;
}