//this file defines structures and classes 
#pragma once
namespace core_systems {
    struct Node {
        std::string id; //id for each node
        std::string name; //name of the node to be displayed to user
        std::vector<float> embedding; //embedding vector for the node, showing where it is in the vector space
    }
}