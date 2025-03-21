#include <iostream>
#include <string>
#include <cstring>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <sstream>

// Windows specific headers
#include <WinSock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

// Our headers
#include "message_format.h"

// Maximum receive buffer size
constexpr size_t MAX_BUFFER_SIZE = 4096;

class TcpClient {
public:
    TcpClient() : serverSocket_(INVALID_SOCKET), connected_(false), running_(false), nextOrderId_(1) {}

    ~TcpClient() {
        disconnect();
    }

    // Connect to server
    bool connect(const std::string& host, int port) {
        if (connected_) {
            std::cerr << "Already connected to a server" << std::endl;
            return false;
        }

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

        // Resolve hostname
        struct addrinfo hints, * result = nullptr;
        ZeroMemory(&hints, sizeof(hints));
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;

        // Convert port to string
        char portStr[10];
        sprintf_s(portStr, sizeof(portStr), "%d", port);

        if (getaddrinfo(host.c_str(), portStr, &hints, &result) != 0) {
            std::cerr << "Error resolving hostname: " << WSAGetLastError() << std::endl;
            closesocket(serverSocket_);
            WSACleanup();
            return false;
        }

        // Connect to server
        if (::connect(serverSocket_, result->ai_addr, (int)result->ai_addrlen) == SOCKET_ERROR) {
            std::cerr << "Error connecting to server: " << WSAGetLastError() << std::endl;
            freeaddrinfo(result);
            closesocket(serverSocket_);
            WSACleanup();
            return false;
        }

        freeaddrinfo(result);

        // Set socket to non-blocking
        u_long mode = 1;  // 1 = non-blocking
        if (ioctlsocket(serverSocket_, FIONBIO, &mode) == SOCKET_ERROR) {
            std::cerr << "Error setting socket to non-blocking mode: " << WSAGetLastError() << std::endl;
            closesocket(serverSocket_);
            WSACleanup();
            return false;
        }

        // Start receiver thread
        connected_ = true;
        running_ = true;
        receiverThread_ = std::thread(&TcpClient::receiverFunction, this);

        std::cout << "Connected to server " << host << ":" << port << std::endl;
        return true;
    }

    // Disconnect from server
    void disconnect() {
        if (!connected_) {
            return;
        }

        // Stop receiver thread
        running_ = false;

        if (receiverThread_.joinable()) {
            receiverThread_.join();
        }

        // Close socket
        if (serverSocket_ != INVALID_SOCKET) {
            closesocket(serverSocket_);
            serverSocket_ = INVALID_SOCKET;
        }

        // Cleanup Winsock
        WSACleanup();

        connected_ = false;
        std::cout << "Disconnected from server" << std::endl;
    }

    // Send an echo request
    void sendEchoRequest(const std::string& message) {
        if (!connected_) {
            std::cerr << "Not connected to server" << std::endl;
            return;
        }

        // Create request
        EchoRequest request;
        ZeroMemory(&request, sizeof(request));
        request.header.type = MessageType::REQ_ECHO;
        request.header.length = sizeof(EchoRequest);
        request.header.sequence = 0;
        strncpy_s(request.message, sizeof(request.message), message.c_str(), _TRUNCATE);

        // Convert to network byte order
        request.header.toNetworkOrder();

        // Send request
        if (send(serverSocket_, (const char*)&request, sizeof(request), 0) == SOCKET_ERROR) {
            std::cerr << "Error sending echo request: " << WSAGetLastError() << std::endl;
        }
    }

    // Send a quit request
    void sendQuitRequest() {
        if (!connected_) {
            std::cerr << "Not connected to server" << std::endl;
            return;
        }

        // Create request
        MessageHeader request;
        request.type = MessageType::REQ_QUIT;
        request.length = sizeof(MessageHeader);
        request.sequence = 0;

        // Convert to network byte order
        request.toNetworkOrder();

        // Send request
        if (send(serverSocket_, (const char*)&request, sizeof(request), 0) == SOCKET_ERROR) {
            std::cerr << "Error sending quit request: " << WSAGetLastError() << std::endl;
        }
    }

    // Send a list users request
    void sendListUsersRequest() {
        if (!connected_) {
            std::cerr << "Not connected to server" << std::endl;
            return;
        }

        // Create request
        MessageHeader request;
        request.type = MessageType::REQ_LISTUSERS;
        request.length = sizeof(MessageHeader);
        request.sequence = 0;

        // Convert to network byte order
        request.toNetworkOrder();

        // Send request
        if (send(serverSocket_, (const char*)&request, sizeof(request), 0) == SOCKET_ERROR) {
            std::cerr << "Error sending list users request: " << WSAGetLastError() << std::endl;
        }
    }

    // Send an add order request
    void sendAddOrderRequest(OrderType orderType, Side side, uint32_t price, uint32_t quantity) {
        if (!connected_) {
            std::cerr << "Not connected to server" << std::endl;
            return;
        }

        // Create request
        AddOrderRequest request;
        request.header.type = MessageType::REQ_ADD_ORDER;
        request.header.length = sizeof(AddOrderRequest);
        request.header.sequence = 0;
        request.orderType = orderType;
        request.side = side;
        request.price = price;
        request.quantity = quantity;
        request.clientOrderId = nextOrderId_++;

        // Convert to network byte order
        request.toNetworkOrder();

        // Send request
        if (send(serverSocket_, (const char*)&request, sizeof(request), 0) == SOCKET_ERROR) {
            std::cerr << "Error sending add order request: " << WSAGetLastError() << std::endl;
        }
    }

    // Send a cancel order request
    void sendCancelOrderRequest(uint64_t orderId) {
        if (!connected_) {
            std::cerr << "Not connected to server" << std::endl;
            return;
        }

        // Create request
        CancelOrderRequest request;
        request.header.type = MessageType::REQ_CANCEL_ORDER;
        request.header.length = sizeof(CancelOrderRequest);
        request.header.sequence = 0;
        request.orderId = orderId;

        // Convert to network byte order
        request.toNetworkOrder();

        // Send request
        if (send(serverSocket_, (const char*)&request, sizeof(request), 0) == SOCKET_ERROR) {
            std::cerr << "Error sending cancel order request: " << WSAGetLastError() << std::endl;
        }
    }

    // Send a modify order request
    void sendModifyOrderRequest(uint64_t orderId, Side side, uint32_t price, uint32_t quantity) {
        if (!connected_) {
            std::cerr << "Not connected to server" << std::endl;
            return;
        }

        // Create request
        ModifyOrderRequest request;
        request.header.type = MessageType::REQ_MODIFY_ORDER;
        request.header.length = sizeof(ModifyOrderRequest);
        request.header.sequence = 0;
        request.orderId = orderId;
        request.side = side;
        request.price = price;
        request.quantity = quantity;

        // Convert to network byte order
        request.toNetworkOrder();

        // Send request
        if (send(serverSocket_, (const char*)&request, sizeof(request), 0) == SOCKET_ERROR) {
            std::cerr << "Error sending modify order request: " << WSAGetLastError() << std::endl;
        }
    }

    // Send an orderbook status request
    void sendOrderbookStatusRequest() {
        if (!connected_) {
            std::cerr << "Not connected to server" << std::endl;
            return;
        }

        // Create request
        MessageHeader request;
        request.type = MessageType::REQ_ORDERBOOK_STATUS;
        request.length = sizeof(MessageHeader);
        request.sequence = 0;

        // Convert to network byte order
        request.toNetworkOrder();

        // Send request
        if (send(serverSocket_, (const char*)&request, sizeof(request), 0) == SOCKET_ERROR) {
            std::cerr << "Error sending orderbook status request: " << WSAGetLastError() << std::endl;
        }
    }

    // Check if connected
    bool isConnected() const {
        return connected_;
    }

private:
    // Thread function to receive messages from server
    void receiverFunction() {
        std::vector<uint8_t> buffer(MAX_BUFFER_SIZE);
        std::vector<uint8_t> messageBuffer; // Buffer for accumulating partial messages

        while (running_) {
            // Attempt to receive data
            int bytesRead = recv(serverSocket_, (char*)buffer.data(), (int)buffer.size(), 0);

            if (bytesRead > 0) {
                // Append to message buffer
                messageBuffer.insert(messageBuffer.end(), buffer.begin(), buffer.begin() + bytesRead);

                // Process complete messages
                processMessageBuffer(messageBuffer);
            }
            else if (bytesRead == 0) {
                // Server disconnected
                std::cout << "Server disconnected" << std::endl;
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

        // Mark as disconnected
        connected_ = false;
    }

    // Process buffer that may contain multiple or partial messages
    void processMessageBuffer(std::vector<uint8_t>& buffer) {
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
            processMessage(buffer.data(), header.length);

            // Remove the processed message from the buffer
            buffer.erase(buffer.begin(), buffer.begin() + header.length);
        }
    }

    // Process a single complete message
    void processMessage(uint8_t* data, uint32_t length) {
        MessageHeader* header = reinterpret_cast<MessageHeader*>(data);

        switch (header->type) {
        case MessageType::RSP_ECHO:
            handleEchoResponse(data, length);
            break;

        case MessageType::RSP_LISTUSERS:
            handleListUsersResponse(data, length);
            break;

        case MessageType::RSP_ADD_ORDER:
            handleAddOrderResponse(data, length);
            break;

        case MessageType::RSP_CANCEL_ORDER:
            handleCancelOrderResponse(data, length);
            break;

        case MessageType::RSP_MODIFY_ORDER:
            handleModifyOrderResponse(data, length);
            break;

        case MessageType::RSP_ORDERBOOK_STATUS:
            handleOrderbookStatusResponse(data, length);
            break;

        case MessageType::NOTIFY_TRADE:
            handleTradeNotification(data, length);
            break;

        case MessageType::CMD_ERROR:
            handleErrorResponse(data, length);
            break;

        default:
            std::cerr << "Received unknown message type: " << static_cast<int>(header->type) << std::endl;
            break;
        }
    }

    // Handle echo response
    void handleEchoResponse(uint8_t* data, uint32_t length) {
        EchoResponse* response = reinterpret_cast<EchoResponse*>(data);
        std::cout << "Received echo response: " << response->message << std::endl;
    }

    // Handle list users response
    void handleListUsersResponse(uint8_t* data, uint32_t length) {
        uint32_t numClients = *reinterpret_cast<uint32_t*>(data + sizeof(MessageHeader));
        numClients = ntohl(numClients);

        char* message = reinterpret_cast<char*>(data + sizeof(MessageHeader) + sizeof(uint32_t));
        std::cout << "Received list users response: " << message << std::endl;
    }

    // Handle add order response
    void handleAddOrderResponse(uint8_t* data, uint32_t length) {
        AddOrderResponse* response = reinterpret_cast<AddOrderResponse*>(data);
        response->toHostOrder();

        std::cout << "Order added - Client ID: " << response->clientOrderId
            << ", Server ID: " << response->serverOrderId
            << ", Status: " << (response->status == 0 ? "Success" : "Failed")
            << std::endl;
    }

    // Handle cancel order response
    void handleCancelOrderResponse(uint8_t* data, uint32_t length) {
        CancelOrderResponse* response = reinterpret_cast<CancelOrderResponse*>(data);
        response->toHostOrder();

        std::cout << "Order canceled - Order ID: " << response->orderId
            << ", Status: " << (response->status == 0 ? "Success" : "Failed")
            << std::endl;
    }

    // Handle modify order response
    void handleModifyOrderResponse(uint8_t* data, uint32_t length) {
        ModifyOrderRequest* response = reinterpret_cast<ModifyOrderRequest*>(data);
        response->toHostOrder();

        std::cout << "Order modified - Order ID: " << response->orderId
            << ", Price: " << response->price
            << ", Quantity: " << response->quantity
            << std::endl;
    }

    // Handle orderbook status response
    void handleOrderbookStatusResponse(uint8_t* data, uint32_t length) {
        OrderbookStatusResponse* response = reinterpret_cast<OrderbookStatusResponse*>(data);
        response->toHostOrder();

        std::cout << "Orderbook Status:" << std::endl;

        // Print bids (descending order)
        std::cout << "Bids:" << std::endl;
        for (uint32_t i = 0; i < response->bidLevelsCount; ++i) {
            std::cout << "  Price: " << response->bidLevels[i].price
                << ", Quantity: " << response->bidLevels[i].quantity
                << std::endl;
        }

        // Print asks (ascending order)
        std::cout << "Asks:" << std::endl;
        for (uint32_t i = 0; i < response->askLevelsCount; ++i) {
            std::cout << "  Price: " << response->askLevels[i].price
                << ", Quantity: " << response->askLevels[i].quantity
                << std::endl;
        }
    }

    // Handle trade notification
    void handleTradeNotification(uint8_t* data, uint32_t length) {
        TradeNotification* notification = reinterpret_cast<TradeNotification*>(data);
        notification->toHostOrder();

        std::cout << "Trade executed - Buy Order ID: " << notification->buyOrderId
            << ", Sell Order ID: " << notification->sellOrderId
            << ", Price: " << notification->price
            << ", Quantity: " << notification->quantity
            << std::endl;
    }

    // Handle error response
    void handleErrorResponse(uint8_t* data, uint32_t length) {
        MessageHeader* header = reinterpret_cast<MessageHeader*>(data);
        std::cout << "Received error response for sequence: " << header->sequence << std::endl;
    }

    SOCKET serverSocket_;
    std::atomic<bool> connected_;
    std::atomic<bool> running_;
    std::thread receiverThread_;
    std::atomic<uint64_t> nextOrderId_;
};

void displayHelp() {
    std::cout << "Available commands:" << std::endl;
    std::cout << "  connect <host> <port>   - Connect to server" << std::endl;
    std::cout << "  disconnect              - Disconnect from server" << std::endl;
    std::cout << "  echo <message>          - Send echo request" << std::endl;
    std::cout << "  users                   - Request list of connected users" << std::endl;
    std::cout << "  buy <price> <quantity>  - Place buy order" << std::endl;
    std::cout << "  sell <price> <quantity> - Place sell order" << std::endl;
    std::cout << "  fkbuy <price> <qty>     - Place fill-and-kill buy order" << std::endl;
    std::cout << "  fksell <price> <qty>    - Place fill-and-kill sell order" << std::endl;
    std::cout << "  cancel <order_id>       - Cancel order" << std::endl;
    std::cout << "  modify <id> <side> <price> <qty> - Modify order" << std::endl;
    std::cout << "  book                    - Request orderbook status" << std::endl;
    std::cout << "  quit                    - Exit application" << std::endl;
    std::cout << "  help                    - Display this help" << std::endl;
}

int main() {
    TcpClient client;
    std::string command;

    std::cout << "Order Book Client" << std::endl;
    std::cout << "Type 'help' for available commands" << std::endl;

    while (true) {
        std::cout << "> ";
        std::getline(std::cin, command);

        std::istringstream iss(command);
        std::string cmd;
        iss >> cmd;

        if (cmd == "connect") {
            std::string host;
            int port;
            iss >> host >> port;

            if (host.empty() || port <= 0) {
                std::cout << "Usage: connect <host> <port>" << std::endl;
                continue;
            }

            client.connect(host, port);
        }
        else if (cmd == "disconnect") {
            client.disconnect();
        }
        else if (cmd == "echo") {
            std::string message;
            std::getline(iss >> std::ws, message);

            if (message.empty()) {
                std::cout << "Usage: echo <message>" << std::endl;
                continue;
            }

            client.sendEchoRequest(message);
        }
        else if (cmd == "users") {
            client.sendListUsersRequest();
        }
        else if (cmd == "buy") {
            uint32_t price, quantity;
            iss >> price >> quantity;

            if (price <= 0 || quantity <= 0) {
                std::cout << "Usage: buy <price> <quantity>" << std::endl;
                continue;
            }

            client.sendAddOrderRequest(OrderType::GoodTillCancel, Side::Buy, price, quantity);
        }
        else if (cmd == "sell") {
            uint32_t price, quantity;
            iss >> price >> quantity;

            if (price <= 0 || quantity <= 0) {
                std::cout << "Usage: sell <price> <quantity>" << std::endl;
                continue;
            }

            client.sendAddOrderRequest(OrderType::GoodTillCancel, Side::Sell, price, quantity);
        }
        else if (cmd == "fkbuy") {
            uint32_t price, quantity;
            iss >> price >> quantity;

            if (price <= 0 || quantity <= 0) {
                std::cout << "Usage: fkbuy <price> <quantity>" << std::endl;
                continue;
            }

            client.sendAddOrderRequest(OrderType::FillAndKill, Side::Buy, price, quantity);
        }
        else if (cmd == "fksell") {
            uint32_t price, quantity;
            iss >> price >> quantity;

            if (price <= 0 || quantity <= 0) {
                std::cout << "Usage: fksell <price> <quantity>" << std::endl;
                continue;
            }

            client.sendAddOrderRequest(OrderType::FillAndKill, Side::Sell, price, quantity);
        }
        else if (cmd == "cancel") {
            uint64_t orderId;
            iss >> orderId;

            if (orderId <= 0) {
                std::cout << "Usage: cancel <order_id>" << std::endl;
                continue;
            }

            client.sendCancelOrderRequest(orderId);
        }
        else if (cmd == "modify") {
            uint64_t orderId;
            std::string sideStr;
            uint32_t price, quantity;
            iss >> orderId >> sideStr >> price >> quantity;

            if (orderId <= 0 || sideStr.empty() || price <= 0 || quantity <= 0) {
                std::cout << "Usage: modify <order_id> <side:buy|sell> <price> <quantity>" << std::endl;
                continue;
            }

            Side side = (sideStr == "buy" || sideStr == "b") ? Side::Buy : Side::Sell;

            client.sendModifyOrderRequest(orderId, side, price, quantity);
        }
        else if (cmd == "book") {
            client.sendOrderbookStatusRequest();
        }
        else if (cmd == "quit" || cmd == "exit") {
            if (client.isConnected()) {
                client.sendQuitRequest();
                client.disconnect();
            }
            break;
        }
        else if (cmd == "help") {
            displayHelp();
        }
        else {
            std::cout << "Unknown command: " << cmd << std::endl;
            std::cout << "Type 'help' for available commands" << std::endl;
        }
    }

    return 0;
}