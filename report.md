# Report

<!-- Remember to check the output of the pdf job on gitlab to make sure everything renders correctly! -->

## Overview
Our air traffic control (ATC) network implementation includes several key extensions to meet concurrency and efficiency requirements, enabling it to handle multiple clients and airport nodes simultaneously. The primary extension tasks we implemented are:
* Thread Pooling and Connection Queue: The controller node leverages a thread pool to handle multiple client connections concurrently, ensuring efficient use of resources without the overhead of creating a new thread for each connection. A synchronized connection queue distributes client requests to worker threads, enabling concurrent processing.
* Fine-Grained Error Handling: Detailed validation checks are incorporated to handle various client errors, such as invalid airport IDs or incorrect request formats. The system responds with clear, specific error messages when a client provides invalid data.
The controller node serves as the central point of communication between clients and airport nodes. When a client connects, the controller parses and processes each incoming request in three main steps:
1. Parsing and Validation:
    Upon receiving a request, the controller parses the command and extracts key details such as the airport ID and other parameters. The controller validates the request format and checks for any errors. If the request is malformed or references an invalid airport, the controller sends an appropriate error response back to the client.
2. Forwarding Requests to the Correct Airport Node:
    For valid requests, the controller identifies the target airport node based on the airport ID provided in the request. It establishes a socket connection to the airport node’s listening port and forwards the request over the network.
    The airport node processes the forwarded request, such as scheduling a flight or checking gate availability, and generates a response.
3. Returning the Correct Response:
    After receiving the response from the airport node, the controller relays this response back to the client. This ensures that the client receives timely and accurate information about their request.


## Extensions

## Testing

