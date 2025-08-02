//this file defines structures and classes 
#pragma once
#include <string>    
#include <vector>  
#include <chrono>  
#include <uuid/uuid.h>         // library to generate UUIDs
#include <nlohmann/json.hpp> 

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
            // Atomic variables for thread-safe operations
            //will ensure that operations on shared variables are indivisible, preventing race conditions
        SystemHealth getHealthStatus() const;
        nlohmann::json toJson() const;

    }

    //forward declarations
    class VectorEngine;
    class TelemetryProcessor;

    class SystemManager { //main class to manage the system

    }
}