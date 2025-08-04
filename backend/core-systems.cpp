//this file defines structures and classes 
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

namespace core_systems {

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

    struct SystemTelemetry { //will be used to track system health and performance 
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
        std::unique_ptr<SystemTelemetry> healthMetrics;
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
        SystemTelemetry getSystemHealth() const; //no parameters
        void recordTelemetry(const SearchTelemetry& telemetry); 
        bool emergencySubsystemRestart(const std::string& subsystem_name);
    };

}
