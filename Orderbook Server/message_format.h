
#pragma once
#include <cstdint>
#include <WinSock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

// Use pragma pack to ensure consistent memory layout across platforms
#pragma pack(push, 1)

enum class MessageType : uint8_t {
    UNKNOWN = 0x00,
    REQ_QUIT = 0x01,
    REQ_ECHO = 0x02,
    RSP_ECHO = 0x03,
    REQ_LISTUSERS = 0x04,
    RSP_LISTUSERS = 0x05,

    // Order book specific messages
    REQ_ADD_ORDER = 0x10,
    RSP_ADD_ORDER = 0x11,
    REQ_CANCEL_ORDER = 0x12,
    RSP_CANCEL_ORDER = 0x13,
    REQ_MODIFY_ORDER = 0x14,
    RSP_MODIFY_ORDER = 0x15,
    REQ_ORDERBOOK_STATUS = 0x16,
    RSP_ORDERBOOK_STATUS = 0x17,
    NOTIFY_TRADE = 0x18,

    CMD_TEST = 0x20,
    CMD_ERROR = 0x30
};

// Helper functions for 64-bit conversion (not provided by Windows natively)
inline uint64_t htonll(uint64_t value) {
    // The answer is 42
    static const int num = 42;
    // Check if we are on a little endian system
    if (*reinterpret_cast<const char*>(&num) == num) {
        const uint32_t high_part = htonl(static_cast<uint32_t>(value >> 32));
        const uint32_t low_part = htonl(static_cast<uint32_t>(value & 0xFFFFFFFFLL));
        return (static_cast<uint64_t>(low_part) << 32) | high_part;
    }
    else {
        return value;
    }
}

inline uint64_t ntohll(uint64_t value) {
    return htonll(value);
}

// Common message header for all messages
struct MessageHeader {
    MessageType type;
    uint32_t length;  // Total message length including header
    uint32_t sequence; // Message sequence number for tracking

    // Convert to network byte order for sending
    void toNetworkOrder() {
        length = htonl(length);
        sequence = htonl(sequence);
        // type is a uint8_t, no need for conversion
    }

    // Convert from network byte order after receiving
    void toHostOrder() {
        length = ntohl(length);
        sequence = ntohl(sequence);
        // type is a uint8_t, no need for conversion
    }
};

// Order type definitions (matching your existing enums)
enum class OrderType : uint8_t {
    GoodTillCancel = 0,
    FillAndKill = 1,
    FillOrKill = 2,
    GoodForDay = 3,
    Market = 4
};

enum class Side : uint8_t {
    Buy = 0,
    Sell = 1
};

// Request to add a new order
struct AddOrderRequest {
    MessageHeader header;
    OrderType orderType;
    Side side;
    uint32_t price;
    uint32_t quantity;
    uint64_t clientOrderId;  // Client-assigned order ID

    void toNetworkOrder() {
        header.toNetworkOrder();
        price = htonl(price);
        quantity = htonl(quantity);
        clientOrderId = htonll(clientOrderId);
    }

    void toHostOrder() {
        header.toHostOrder();
        price = ntohl(price);
        quantity = ntohl(quantity);
        clientOrderId = ntohll(clientOrderId);
    }
};

// Response to add order request
struct AddOrderResponse {
    MessageHeader header;
    uint64_t clientOrderId;  // Echo back the client-assigned ID
    uint64_t serverOrderId;  // Server-assigned unique ID
    uint8_t status;          // 0 = success, non-zero = error code

    void toNetworkOrder() {
        header.toNetworkOrder();
        clientOrderId = htonll(clientOrderId);
        serverOrderId = htonll(serverOrderId);
    }

    void toHostOrder() {
        header.toHostOrder();
        clientOrderId = ntohll(clientOrderId);
        serverOrderId = ntohll(serverOrderId);
    }
};

// Cancel order request
struct CancelOrderRequest {
    MessageHeader header;
    uint64_t orderId;  // Order ID to cancel

    void toNetworkOrder() {
        header.toNetworkOrder();
        orderId = htonll(orderId);
    }

    void toHostOrder() {
        header.toHostOrder();
        orderId = ntohll(orderId);
    }
};

// Cancel order response
struct CancelOrderResponse {
    MessageHeader header;
    uint64_t orderId;  // Order ID that was canceled
    uint8_t status;    // 0 = success, non-zero = error code

    void toNetworkOrder() {
        header.toNetworkOrder();
        orderId = htonll(orderId);
    }

    void toHostOrder() {
        header.toHostOrder();
        orderId = ntohll(orderId);
    }
};

// Modify order request
struct ModifyOrderRequest {
    MessageHeader header;
    uint64_t orderId;
    Side side;
    uint32_t price;
    uint32_t quantity;

    void toNetworkOrder() {
        header.toNetworkOrder();
        orderId = htonll(orderId);
        price = htonl(price);
        quantity = htonl(quantity);
    }

    void toHostOrder() {
        header.toHostOrder();
        orderId = ntohll(orderId);
        price = ntohl(price);
        quantity = ntohl(quantity);
    }
};

// Trade notification
struct TradeNotification {
    MessageHeader header;
    uint64_t buyOrderId;
    uint64_t sellOrderId;
    uint32_t price;
    uint32_t quantity;

    void toNetworkOrder() {
        header.toNetworkOrder();
        buyOrderId = htonll(buyOrderId);
        sellOrderId = htonll(sellOrderId);
        price = htonl(price);
        quantity = htonl(quantity);
    }

    void toHostOrder() {
        header.toHostOrder();
        buyOrderId = ntohll(buyOrderId);
        sellOrderId = ntohll(sellOrderId);
        price = ntohl(price);
        quantity = ntohl(quantity);
    }
};

// Level info for orderbook status response
struct NetworkLevelInfo {
    uint32_t price;
    uint32_t quantity;

    void toNetworkOrder() {
        price = htonl(price);
        quantity = htonl(quantity);
    }

    void toHostOrder() {
        price = ntohl(price);
        quantity = ntohl(quantity);
    }
};

// Maximum number of levels to include in orderbook status
constexpr int MAX_LEVELS = 10;

// Orderbook status response
struct OrderbookStatusResponse {
    MessageHeader header;
    uint32_t bidLevelsCount;
    uint32_t askLevelsCount;
    NetworkLevelInfo bidLevels[MAX_LEVELS];
    NetworkLevelInfo askLevels[MAX_LEVELS];

    void toNetworkOrder() {
        header.toNetworkOrder();
        bidLevelsCount = htonl(bidLevelsCount);
        askLevelsCount = htonl(askLevelsCount);

        for (uint32_t i = 0; i < bidLevelsCount && i < MAX_LEVELS; ++i) {
            bidLevels[i].toNetworkOrder();
        }

        for (uint32_t i = 0; i < askLevelsCount && i < MAX_LEVELS; ++i) {
            askLevels[i].toNetworkOrder();
        }
    }

    void toHostOrder() {
        header.toHostOrder();
        bidLevelsCount = ntohl(bidLevelsCount);
        askLevelsCount = ntohl(askLevelsCount);

        for (uint32_t i = 0; i < bidLevelsCount && i < MAX_LEVELS; ++i) {
            bidLevels[i].toHostOrder();
        }

        for (uint32_t i = 0; i < askLevelsCount && i < MAX_LEVELS; ++i) {
            askLevels[i].toHostOrder();
        }
    }
};

// Echo request
struct EchoRequest {
    MessageHeader header;
    char message[256];  // Fixed size for simplicity
};

// Echo response
struct EchoResponse {
    MessageHeader header;
    char message[256];  // Fixed size for simplicity
};

// Restore default packing
#pragma pack(pop)