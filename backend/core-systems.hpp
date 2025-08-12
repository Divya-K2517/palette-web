#pragma once
#include <string>    
#include <vector>  
#include <chrono>  
#include <uuid/uuid.h>         // library to generate UUIDs
#include <nlohmann/json.hpp> 
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <memory> //for smart pointers
#include <thread>
#include <unordered_map> //for caching
//this file defines structures and classes for core systems
//summary:
//class SystemManager is the brains: starts engines(for weaviate & pinterest), spawns threads, acceptes requests, and records telemetry data
//class VectorEngine does the vector search(communicating with weaviate), cahches results, and handles images
//class TelemetryProcessor keeps track of performance data and will help detect slowness or bugs
//SystemHealth gives an overview of the system health

namespace coreSystems {

    enum class SystemHealth {
    NOMINAL,    // All systems green
    DEGRADED,   // Yellow - reduced capability  
    CRITICAL    // Red - system failure
    };

    struct Node {
        std::string id; //id for each node
        std::string name; //name of the node to be displayed to user
        std::vector<float> embedding; //embedding vector for the node, showing where it is in the vector space
        float similarityScore; //similarity score of the node with respect to the search/center node
        std::chrono::system_clock::time_point timestamp; //timestamp of the node creation or last update
        SystemHealth healthStatus;  //health status of the node, defined in SystemHealth enum
        int level; //0=query, 1=first level, 2=second level
        //for conversion to/from JSON for api
        nlohmann::json toJson() const;
            //function that converts the Node object to a JSON object

        static Node fromJson(const nlohmann::json& j);
            //static method that belongs to the class, not an instance of the class
            //returns a Node object
            //takes in a reference to a JSON object
    }

    struct SearchTelemetry { //will be used to track each search and its stats
        std:: string searchId; //id for the search
        std:: string searchPhrase; //search phrase used
        uint64_t processingTime; //processing time in milliseconds
        size_t nodesFound; //number of nodes found in the search
        std::chrono::system_clock::time_point timestamp; //time the search finished
        nlohmann::json toJson() const;
    }

    struct SystemHealth { //will be used to track system health and performance 
        std::atomic<float> cpuUsage{0.0f};
        std::atomic<float> memoryUsage{0.0f};
        std::atomic<size_t> activeConnections{0};
        std::atomic<std::chrono::system_clock::time_point> lastHeartbeat;
        std::atomic<float> errorRate{0.0f};
            // Atomic variables for thread-safe operations on a single variable
            //will ensure that operations on shared variables are indivisible, preventing race conditions
        SystemHealth getHealthStatus() const;
        nlohmann::json toJson() const;
    }

    //forward declarations
    class VectorEngine;
    class TelemetryProcessor;

    class SystemManager { //main class to manage the system
    private: //methods cannot be accessed outside the class
        std::unique_ptr<VectorEngine> primaryVectorEngine;
        std::unique_ptr<VectorEngine> backupVectorEngine;
        std::unique_ptr<TelemetryProcessor> telemetryProcessor;
        std::unique_ptr<SystemHealth> healthMetrics;
            //std::unique_ptr is a smart pointer that deletes the object when it goes out of scope
            //ensures that the object is deleted when the SystemManager object is destroyed
        
        //thread-safe telemetry queue
        std::queue<SearchTelemetry> telemetryQueue;
        std::mutex telemetryMutex;
            //wil be used to protect the telemetryQueue from concurrent access+stop race conditions
            //code wrapped in telemetryMutex.lock() and telemetryMutex.unlock() can only be accessed by one thread at a time
        std::condition_variable telemetryCv;
        std::atomic<bool> shutdownRequested{false};
            //atomic variable to indicate if a shutdown has been requested
            //load() - read value
            //store() - write value
            //exchange() - read and write value atomically

        //background threads
        std::thread telemetryThread;
        std::thread healthMonitorThread;
        //functions that will run each background thread
        void telemetryWorker();
        void healthMonitorWorker();

    public:
        //constructor and destructor for SystemManager class
        SystemManager();
        ~SystemManager();

        //methods to manage the system
        bool initialize(); 
        void shutdown(); 
        std::vector<Node> search(const std::string& query);
        SystemHealth getSystemHealth() const; //no parameters
        void recordTelemetry(const SearchTelemetry& telemetry); 
        bool emergencySubsystemRestart(const std::string& subsystem_name);
    };
    class VectorEngine { //deals with weaviate/pinterest, caches results and handles images
    private:
        std::string engineId;
        std::string engineType; //"primary" or "backup"
        bool isOperational; //indicates if the engine is operational

        //connection pools for external services w/ unique_ptr
        std::unique_ptr<class WeaviateClient> weaviateClient;
        std::unique_ptr<class PinterestClient> pinterestClient;   

        //local cache
        std::unordered_map<std::string, std::vector<Node>> searchCache;
        std::unordered_map<std::string, std::vector<struct PinterestImage>> imageCache;
        std::mutex cacheMutex;

        std::chrono::system_clock::time_point lastCacheUpdate;
        static constexpr std::chrono::minutes CACHE_EXPIRY_TIME{10};

        //main vector search function
        std::vector<Node> vectorSearch(const std::string& query);
        bool isOperational() const {
            return isOperational;
        }
        //cache related
        void clearCache();
        size_t getCacheSize() const;
    };
    class TelemetryProcessor { 
        //for stats and analytics
    private:
        std::atomic<bool> isRunning{false};
            //atomic types make operations on them indivisible, preventing race conditions
        std::mutex processingMutex;
        
        std::vector<SearchTelemetry> telemeteryHistory;
        static constexpr size_t MAX_TELEMETRY_RECORDS = 10000;
    public:
        TelemetryProcessor() = default;
        ~TelemetryProcessor() = default;

        void start();
        void stop();

        void processTelemetry(const SearchTelemetry& telemetry);

        // Analytics functions
        float getAverageResponseTime() const;
        float getErrorRate() const;
        size_t getTotalQueries() const;
        
        nlohmann::json getPerformanceReport() const;
    };
    namespace utils { 
        std::string generateUUID(); //function to generate a UUID
        std::chrono::system_clock::time_point getCurrentTime();
        uint64_t getTimestampMs();
        float calculateSystemLoad();

        class PerformanceTimer { //to time events
        private:
            std::chrono::high_resolution_clock::time_point startTime;
        public: 
            PerformanceTimer() : startTime(std::chrono::high_resolution_clock::now()) {}
                //constructor for the class that defines startTime as the current time
            
            uint64_t elapsedMs() const {
                //function to calc time elapses from startTime to now
                auto endTime = std::chrono::high_resolution_clock::now();
                return std::chrono::duration_cast<std::chrono::milliseconds>(
                    end_time - start_time_).count();
            }
        };  
    } //end of namespace utils   
} //end of namespace core_systems
