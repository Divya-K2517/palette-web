import weaviate
from artStyles import artStyles
from weaviate.connect import ConnectionParams

# Replace with your local Weaviate URL and API key, or leave API key empty if none required
client = weaviate.WeaviateClient(
   connection_params=ConnectionParams.from_params(
        http_host="localhost",  # your host only, without protocol or port here
        http_port="8080",
        http_secure=False,
        grpc_host="localhost",
        grpc_port="50051",
        grpc_secure=False,
    ) # Your local Weaviate
     #auth_client_secret=weaviate.AuthApiKey(api_key="") 
    # auth_client_secret=weaviate.AuthApiKey("YOUR_API_KEY"),  # Only if your Weaviate needs authentication
)

# Make sure schema 'ArtStyle' exists before running (create schema manually or add schema-creation logic here)

with client.batch.dynamic() as batch:
    for style in artStyles:
        batch.add_data_object(style, "ArtStyle")

print("Import complete!")
client.close()