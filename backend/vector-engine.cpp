//VectorEngine does the vector search(communicating with weaviate), cahches results, and handles images
#include "coreSystems/core-systems.hpp"
#include <iostream>
#include <curl/curl.h>

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
        CURL* curlHandle;
        std mutex curlMutex; //to protect curl handle from concurrent access
        
        struct curlResponse { 
            std:: string data;
            long responseCode;
        };

        static size_t writeCallback(void* contents, size_t size, size_t nmemb, CurlResponse* response) {
            //when data comes back from a request it will add it to curlResponse.data
            //nmemb is number of elements
            size_t totalSize = size * nmemb;
            response -> data.append(static_cast<char*>(contents), totalSize)
                //casting void* contents to char*
            return totalSize; //tells curl how many bytes were written
        }
    public: 
        
    };
}