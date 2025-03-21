
#pragma once

#include <mutex>
#include <shared_mutex>

// Include the headers from the original implementation
#include "orderbook.cpp"

// This adapter ensures thread safety for the orderbook
class ThreadSafeOrderbook {
public:
    ThreadSafeOrderbook() : orderbook_() {}

    // Add an order with thread safety
    Trades AddOrder(OrderPointer order) {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        return orderbook_.AddOrder(order);
    }

    // Cancel an order with thread safety
    void CancelOrder(OrderID orderId) {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        orderbook_.CancelOrder(orderId);
    }

    // Modify an order with thread safety
    Trades MatchOrder(OrderModify order) {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        return orderbook_.MatchOrder(order);
    }

    // Get orderbook information with thread safety (read-only operation)
    OrderbookLevelInfos GetOrderInfos() const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        return orderbook_.GetOrderInfos();
    }

    // Get the size of the orderbook with thread safety (read-only operation)
    std::size_t Size() const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        return orderbook_.Size();
    }

private:
    Orderbook orderbook_;
    mutable std::shared_mutex mutex_; // Allows multiple readers but exclusive writers
};

// Adapter class to bridge between the network message format and our orderbook implementation
class OrderbookNetworkAdapter {
public:
    OrderbookNetworkAdapter() : orderbook_(), nextOrderId_(1) {}

    // Process add order request from network
    std::pair<uint64_t, Trades> ProcessAddOrderRequest(const AddOrderRequest& request) {
        // Create an order from the request
        uint64_t orderId = nextOrderId_++;

        OrderPointer order = std::make_shared<Order>(
            static_cast<OrderType>(request.orderType),
            orderId,
            static_cast<Side>(request.side),
            request.price,
            request.quantity
        );

        // Add the order to the orderbook
        Trades trades = orderbook_.AddOrder(order);

        // Return the assigned server order ID and any resulting trades
        return { orderId, trades };
    }

    // Process cancel order request from network
    void ProcessCancelOrderRequest(const CancelOrderRequest& request) {
        orderbook_.CancelOrder(request.orderId);
    }

    // Process modify order request from network
    Trades ProcessModifyOrderRequest(const ModifyOrderRequest& request) {
        OrderModify orderModify(
            request.orderId,
            static_cast<Side>(request.side),
            request.price,
            request.quantity
        );

        return orderbook_.MatchOrder(orderModify);
    }

    // Get current orderbook status
    OrderbookLevelInfos GetOrderbookStatus() const {
        return orderbook_.GetOrderInfos();
    }

    // Get the size of the orderbook
    std::size_t Size() const {
        return orderbook_.Size();
    }

private:
    ThreadSafeOrderbook orderbook_;
    std::atomic<uint64_t> nextOrderId_;
};