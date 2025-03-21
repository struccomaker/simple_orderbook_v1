#include <iostream>
#include <string>
#include <cstring>
#include <vector>
#include <thread>
#include <atomic>
#include <unordered_map>
#include <memory>
#include <mutex>

// Windows specific headers
#include <WinSock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

// Our headers
#include "message_format.h"
#include "task_queue.h"
#include "orderbook.h"

// Maximum receive buffer size
constexpr size_t MAX_BUFFER_SIZE = 4096;

class TcpServer {
public:
    TcpServer(int port, int numThreads)
        : port_(port),
        threadPool_(numThreads),
        orderbook_(),
        nextClientId_(1),
        running_(false) {
    }

    ~TcpServer() {
        stop();
    }

    // Start the server
    bool start() {
        // Initialize Winsock
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            std::cerr << "WSAStartup failed with error: " << WSAGetLastError() << std::endl;
            return false;
        }

        // Create socket
        serverSocket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (serverSocket_ == INVALID_SOCKET) {
            std::cerr << "Error creating socket: " << WSAGetLastError() << std::endl;
            WSACleanup();
            return false;
        }

        // Set socket options
        BOOL opt = TRUE;
        if (setsockopt(serverSocket_, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt)) == SOCKET_ERROR) {
            std::cerr << "Error setting socket options: " << WSAGetLastError() << std::endl;
            closesocket(serverSocket_);
            WSACleanup();
            return false;
        }

        // Bind socket
        struct sockaddr_in serverAddr;
        ZeroMemory(&serverAddr, sizeof(serverAddr));
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_addr.s_addr = INADDR_ANY;
        serverAddr.sin_port = htons(port_);

        if (bind(serverSocket_, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
            std::cerr << "Error binding socket: " << WSAGetLastError() << std::endl;
            closesocket(serverSocket_);
            WSACleanup();
            return false;
        }

        // Listen for connections
        if (listen(serverSocket_, SOMAXCONN) == SOCKET_ERROR) {
            std::cerr << "Error listening on socket: " << WSAGetLastError() << std::endl;
            closesocket(serverSocket_);
            WSACleanup();
            return false;
        }

        // Get server IP address
        char hostname[256];
        if (gethostname(hostname, sizeof(hostname)) == SOCKET_ERROR) {
            std::cerr << "Error getting hostname: " << WSAGetLastError() << std::endl;
        }
        else {
            struct addrinfo hints, * result = nullptr;
            ZeroMemory(&hints, sizeof(hints));
            hints.ai_family = AF_INET;
            hints.ai_socktype = SOCK_STREAM;

            if (getaddrinfo(hostname, NULL, &hints, &result) == 0) {
                std::cout << "Server started on ";
                for (struct addrinfo* ptr = result; ptr != NULL; ptr = ptr->ai_next) {
                    struct sockaddr_in* addr = (struct sockaddr_in*)ptr->ai_addr;
                    char ipStr[INET_ADDRSTRLEN];
                    inet_ntop(AF_INET, &addr->sin_addr, ipStr, sizeof(ipStr));
                    std::cout << ipStr;
                    if (ptr->ai_next != NULL) std::cout << ", ";
                }
                std::cout << ":" << port_ << std::endl;
                freeaddrinfo(result);
            }
            else {
                std::cout << "Server started on port " << port_ << std::endl;
            }
        }

        running_ = true;
        acceptThread_ = std::thread(&TcpServer::acceptConnections, this);

        return true;
    }

    // Stop the server
    void stop() {
        running_ = false;

        // Close server socket to unblock accept()
        if (serverSocket_ != INVALID_SOCKET) {
            closesocket(serverSocket_);
            serverSocket_ = INVALID_SOCKET;
        }

        // Wait for accept thread to finish
        if (acceptThread_.joinable()) {
            acceptThread_.join();
        }

        // Close all client connections
        {
            std::lock_guard<std::mutex> lock(clientsMutex_);
            for (auto& client : clients_) {
                closesocket(client.first);
            }
            clients_.clear();
        }

        // Cleanup Winsock
        WSACleanup();
    }

private:
    // Thread function to accept connections
    void acceptConnections() {
        while (running_) {
            struct sockaddr_in clientAddr;
            int clientAddrLen = sizeof(clientAddr);

            SOCKET clientSocket = accept(serverSocket_, (struct sockaddr*)&clientAddr, &clientAddrLen);

            if (clientSocket == INVALID_SOCKET) {
                if (running_) {
                    std::cerr << "Error accepting connection: " << WSAGetLastError() << std::endl;
                }
                continue;
            }

            // Get client info
            char clientIP[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &(clientAddr.sin_addr), clientIP, INET_ADDRSTRLEN);
            int clientPort = ntohs(clientAddr.sin_port);

            std::cout << "New connection from " << clientIP << ":" << clientPort << std::endl;

            // Set socket to non-blocking
            u_long mode = 1;  // 1 = non-blocking
            ioctlsocket(clientSocket, FIONBIO, &mode);

            // Add to clients map
            uint32_t clientId;
            {
                std::lock_guard<std::mutex> lock(clientsMutex_);
                clientId = nextClientId_++;
                clients_[clientSocket] = clientId;
            }

            // Create a task to handle client communication
            threadPool_.enqueue([this, clientSocket, clientId, clientIP, clientPort]() {
                handleClient(clientSocket, clientId, clientIP, clientPort);
                });
        }
    }

    // Handle client communication
    void handleClient(SOCKET clientSocket, uint32_t clientId, const std::string& clientIP, int clientPort) {
        std::vector<uint8_t> buffer(MAX_BUFFER_SIZE);
        std::vector<uint8_t> messageBuffer; // Buffer for accumulating partial messages

        while (running_) {
            // Attempt to receive data
            int bytesRead = recv(clientSocket, (char*)buffer.data(), (int)buffer.size(), 0);

            if (bytesRead > 0) {
                // Append to message buffer
                messageBuffer.insert(messageBuffer.end(), buffer.begin(), buffer.begin() + bytesRead);

                // Process complete messages
                processMessageBuffer(clientSocket, clientId, messageBuffer);
            }
            else if (bytesRead == 0) {
                // Client disconnected
                std::cout << "Client " << clientIP << ":" << clientPort << " disconnected" << std::endl;
                break;
            }
            else {
                int error = WSAGetLastError();
                if (error != WSAEWOULDBLOCK) {
                    // Error
                    std::cerr << "Error receiving data: " << error << std::endl;
                    break;
                }
            }

            // Sleep to prevent CPU hogging in non-blocking mode
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        // Remove client from map
        {
            std::lock_guard<std::mutex> lock(clientsMutex_);
            clients_.erase(clientSocket);
        }

        // Close socket
        closesocket(clientSocket);
    }

    // Process buffer that may contain multiple or partial messages
    void processMessageBuffer(SOCKET clientSocket, uint32_t clientId, std::vector<uint8_t>& buffer) {
        // Keep processing until buffer doesn't have a complete message
        while (buffer.size() >= sizeof(MessageHeader)) {
            // Peek at the header
            MessageHeader header;
            std::memcpy(&header, buffer.data(), sizeof(MessageHeader));
            header.toHostOrder();

            // Check if we have the complete message
            if (buffer.size() < header.length) {
                // Incomplete message, wait for more data
                break;
            }

            // Process the complete message
            processMessage(clientSocket, clientId, buffer.data(), header.length);

            // Remove the processed message from the buffer
            buffer.erase(buffer.begin(), buffer.begin() + header.length);
        }
    }

    // Process a single complete message
    void processMessage(SOCKET clientSocket, uint32_t clientId, uint8_t* data, uint32_t length) {
        MessageHeader* header = reinterpret_cast<MessageHeader*>(data);

        switch (header->type) {
        case MessageType::REQ_ECHO:
            handleEchoRequest(clientSocket, data, length);
            break;

        case MessageType::REQ_QUIT:
            handleQuitRequest(clientSocket, clientId);
            break;

        case MessageType::REQ_LISTUSERS:
            handleListUsersRequest(clientSocket);
            break;

        case MessageType::REQ_ADD_ORDER:
            handleAddOrderRequest(clientSocket, data, length);
            break;

        case MessageType::REQ_CANCEL_ORDER:
            handleCancelOrderRequest(clientSocket, data, length);
            break;

        case MessageType::REQ_MODIFY_ORDER:
            handleModifyOrderRequest(clientSocket, data, length);
            break;

        case MessageType::REQ_ORDERBOOK_STATUS:
            handleOrderbookStatusRequest(clientSocket);
            break;

        default:
            handleUnknownRequest(clientSocket, header->sequence);
            break;
        }
    }

    // Handle echo request
    void handleEchoRequest(SOCKET clientSocket, uint8_t* data, uint32_t length) {
        EchoRequest* request = reinterpret_cast<EchoRequest*>(data);

        // Create response
        EchoResponse response;
        response.header.type = MessageType::RSP_ECHO;
        response.header.length = sizeof(EchoResponse);
        response.header.sequence = request->header.sequence;
        std::strncpy(response.message, request->message, sizeof(response.message));

        // Convert to network byte order
        response.header.toNetworkOrder();

        // Send response
        send(clientSocket, (const char*)&response, sizeof(response), 0);
    }

    // Handle quit request
    void handleQuitRequest(SOCKET clientSocket, uint32_t clientId) {
        // Client is handled in the handleClient method
        // Just send an acknowledgment here
        MessageHeader response;
        response.type = MessageType::RSP_ECHO;
        response.length = sizeof(MessageHeader);
        response.sequence = 0;
        response.toNetworkOrder();

        send(clientSocket, (const char*)&response, sizeof(response), 0);
    }

    // Handle list users request
    void handleListUsersRequest(SOCKET clientSocket) {
        // Simple response with the number of connected clients
        char responseBuffer[512];
        ZeroMemory(responseBuffer, sizeof(responseBuffer));

        MessageHeader* header = reinterpret_cast<MessageHeader*>(responseBuffer);
        header->type = MessageType::RSP_LISTUSERS;
        header->length = sizeof(MessageHeader) + sizeof(uint32_t) + 256; // Fixed size for simplicity
        header->sequence = 0;

        uint32_t* numClients = reinterpret_cast<uint32_t*>(responseBuffer + sizeof(MessageHeader));

        {
            std::lock_guard<std::mutex> lock(clientsMutex_);
            *numClients = htonl(static_cast<uint32_t>(clients_.size()));
        }

        char* message = responseBuffer + sizeof(MessageHeader) + sizeof(uint32_t);
        sprintf_s(message, 256, "Connected clients: %u", ntohl(*numClients));

        header->toNetworkOrder();
        send(clientSocket, responseBuffer, header->length, 0);
    }

    // Handle add order request
    void handleAddOrderRequest(SOCKET clientSocket, uint8_t* data, uint32_t length) {
        AddOrderRequest* request = reinterpret_cast<AddOrderRequest*>(data);
        request->toHostOrder();

        // Create order for the orderbook
        OrderPointer order = std::make_shared<Order>(
            static_cast<OrderType>(request->orderType),
            request->clientOrderId,
            static_cast<Side>(request->side),
            request->price,
            request->quantity
        );

        // Add to orderbook
        Trades trades = orderbook_.AddOrder(order);

        // Create response
        AddOrderResponse response;
        response.header.type = MessageType::RSP_ADD_ORDER;
        response.header.length = sizeof(AddOrderResponse);
        response.header.sequence = request->header.sequence;
        response.clientOrderId = request->clientOrderId;
        response.serverOrderId = request->clientOrderId; // Using client ID as server ID for simplicity
        response.status = 0; // Success

        // Convert to network byte order
        response.toNetworkOrder();

        // Send response
        send(clientSocket, (const char*)&response, sizeof(response), 0);

        // Send trade notifications if any
        for (const auto& trade : trades) {
            sendTradeNotification(clientSocket, trade);
        }
    }

    // Handle cancel order request
    void handleCancelOrderRequest(SOCKET clientSocket, uint8_t* data, uint32_t length) {
        CancelOrderRequest* request = reinterpret_cast<CancelOrderRequest*>(data);
        request->toHostOrder();

        // Cancel in orderbook
        orderbook_.CancelOrder(request->orderId);

        // Create response
        CancelOrderResponse response;
        response.header.type = MessageType::RSP_CANCEL_ORDER;
        response.header.length = sizeof(CancelOrderResponse);
        response.header.sequence = request->header.sequence;
        response.orderId = request->orderId;
        response.status = 0; // Success

        // Convert to network byte order
        response.toNetworkOrder();

        // Send response
        send(clientSocket, (const char*)&response, sizeof(response), 0);
    }

    // Handle modify order request
    void handleModifyOrderRequest(SOCKET clientSocket, uint8_t* data, uint32_t length) {
        ModifyOrderRequest* request = reinterpret_cast<ModifyOrderRequest*>(data);
        request->toHostOrder();

        // Create order modify object
        OrderModify orderModify(
            request->orderId,
            static_cast<Side>(request->side),
            request->price,
            request->quantity
        );

        // Modify in orderbook
        Trades trades = orderbook_.MatchOrder(orderModify);

        // Create response
        ModifyOrderRequest response = *request;
        response.header.type = MessageType::RSP_MODIFY_ORDER;

        // Convert to network byte order
        response.toNetworkOrder();

        // Send response
        send(clientSocket, (const char*)&response, sizeof(response), 0);

        // Send trade notifications if any
        for (const auto& trade : trades) {
            sendTradeNotification(clientSocket, trade);
        }
    }

    // Handle orderbook status request
    void handleOrderbookStatusRequest(SOCKET clientSocket) {
        OrderbookLevelInfos levelInfos = orderbook_.GetOrderInfos();

        // Create response
        OrderbookStatusResponse response;
        response.header.type = MessageType::RSP_ORDERBOOK_STATUS;
        response.header.length = sizeof(OrderbookStatusResponse);
        response.header.sequence = 0;

        // Copy bid levels
        const auto& bids = levelInfos.GetBids();
        response.bidLevelsCount = std::min(static_cast<uint32_t>(bids.size()), static_cast<uint32_t>(MAX_LEVELS));
        for (uint32_t i = 0; i < response.bidLevelsCount; ++i) {
            response.bidLevels[i].price = bids[i].price_;
            response.bidLevels[i].quantity = bids[i].quantity_;
        }

        // Copy ask levels
        const auto& asks = levelInfos.GetAsks();
        response.askLevelsCount = std::min(static_cast<uint32_t>(asks.size()), static_cast<uint32_t>(MAX_LEVELS));
        for (uint32_t i = 0; i < response.askLevelsCount; ++i) {
            response.askLevels[i].price = asks[i].price_;
            response.askLevels[i].quantity = asks[i].quantity_;
        }

        // Convert to network byte order
        response.toNetworkOrder();

        // Send response
        send(clientSocket, (const char*)&response, sizeof(response), 0);
    }

    // Handle unknown request
    void handleUnknownRequest(SOCKET clientSocket, uint32_t sequence) {
        MessageHeader response;
        response.type = MessageType::ERROR;
        response.length = sizeof(MessageHeader);
        response.sequence = sequence;
        response.toNetworkOrder();

        send(clientSocket, (const char*)&response, sizeof(response), 0);
    }

    // Send trade notification to a client
    void sendTradeNotification(SOCKET clientSocket, const Trade& trade) {
        TradeNotification notification;
        notification.header.type = MessageType::NOTIFY_TRADE;
        notification.header.length