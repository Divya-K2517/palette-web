//will take in user input from react and call search()
#include <httplib.h>
#include <nlohmann/json.hpp>
#include <thread>
#include <atomic>
#include <memory>
#include "CoreSystems/core-systems.hpp"

namespace groundControl {
    class GraphQLHandler {
    private: 
        //pointer variable coreSystems to manage a SystemManager object
        //shared_ptr gives shared ownership and lets multiple pointers point to the same object
        //object deleted when shared_ptr goes out of scope and other shared_ptrs refering to the same object are destroyed
        std::shared_ptr<CoreSystems::SytemManager> systemManager;
    public:
        //constructor
        explicit GraphQLHandler(std::shared_ptr<CoreSystems::SystemManager> sm) 
            : systemManager(sm) {}
            //takes in a pointer to a SystemManager object and initializes coreSystems with cs
        
        //hangle graohQL queries
        nlohmann::json handleQuery(const nlohmann::json& request) {
            std::cout << "handling quer request: " << request << std::endl;
            try {
                std::string query = request.value("query", "");
                nlohmann::json variables = request.value("variables", nlohmann::json::object());

                //parsing operation type
                //TODO
            } catch (const std::exception& e) {

            }
        }
        //handle mutations
        nlohmann::json handleMutation(const nlohmann::json& request) {
            //TODO
        }
    private:
        nlohmann::json handleSearchConcepts(const nlohmann::json& variables) {
            std::string searchQuery = variables.value("query", "");
            int limit = variables.value("limit", 10); //default is 10 node

            if (searchQuery.empty()) {
                return createErrorResponse("search query cannot be empty");
            }

            std::cout << "Ground Control: Initiating search mission for '" << search_query << "'" << std::endl;

            //starting timer for performance
            CoreSystems::utils::PerformanceTimer timer;

            //executing search for nodes
            auto nodes = systemManager -> search(searchQuery); //using -> bc coreSystems is a pointer to the acc system manager object
            //enforce limit if needed
            if (limit > 0 && nodes.size() > static_cast<size_t>(limit)) {
                nodes.resize(limit); //will cut off elements at the end if the size of nodes is greater than limit
            }

            auto processingTime = timer.elapsedMs();
            auto healthMetrics = systemManager -> getSystemHealth();
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
                    {"system_status", healthMetrics.toJson()},
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
                auto healthMetrics = systemManager -> getSystemHealth();

                nlohmann::json data = {
                    {"system_health", {
                        {"status", healthMetrics.toJson()},
                        {"timestamp", CoreSystems::utils::getTimestampMs()},
                        //{"uptime_ms", CoreSystems::utils::getTimestampMs()},
                        {"version", "1.0.0"}
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

            nlohmann::json data = {
                {"pinterest_images", {
                    {"concept", conceptName},
                    {"images", nlohmann::json::array()},
                    {"cached", true},
                    {"timestamp", CoreSystems::utils::getTimestampMs()}
                }}
            };

            return {{"data", ""}};
        }
        nlohmann::json handleTelemetryReport() {
            std::cout << "Ground Control: Telemetry report requested" << std::endl;

            //TODO: interface w/ Telemetry Processor
            nlohmann::json data = {
                {"telemetry_report", {
                    {"total_queries", 0}, // Would get from TelemetryProcessor
                    {"average_response_time", 0.0}, // Would get from TelemetryProcessor
                    {"error_rate", 0.0}, // Would get from TelemetryProcessor
                    {"cache_hit_rate", 0.0},
                    {"timestamp", CoreSystems::utils::getTimestampMs()}
                }}
            };
            
            return {{"data", data}};
        }
        nlohmann::json handleRefreshPinterestData(const nlohmann::json& variables) {
           std::string conceptName = variables.value("concept", "");
           std::cout << "Ground Control: Pinterest data refresh for '" << conceptName << "'" << std::endl;

           //TODO: This would trigger a refresh of Pinterest data
            nlohmann::json data = {
                {"refresh_pinterest_data", {
                    {"concept", conceptName},
                    {"success", true},
                    {"message", "Pinterest data refresh initiated"},
                    {"timestamp", CoreSystems::utils::getTimestampMs()}
                }}
            };
            
            return {{"data", data}};

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
            
            //TODO: This would clear the VectorEngine caches
            nlohmann::json data = {
                {"clear_cache", {
                    {"success", true},
                    {"message", "Cache cleared successfully"},
                    {"timestamp", CoreSystems::utils::getTimestampMs()}
                }}
            };
            
            return {{"data", data}};
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
        std::unique_ptr<httplip::Server> server;
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
                server = std::make_unique<httlib::Server>();
            }
        }
    };
}