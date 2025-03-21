#include <iostream>
#include <map>
#include <set>
#include <list> 
#include <cmath>
#include <deque>
#include <queue>
#include <stack>
#include <limits>
#include <string>
#include <vector>
#include <numeric>
#include <algorithm>
#include <unordered_map>
#include <sstream>
#include <utility>
#include <numeric>
#include <chrono>
#include <ctime>
#include <memory> 
#include  <variant>
#include <optional>
#include <tuple>
#include <format>
#include <map>
#include <unordered_map>
#include <thread>
#include <condition_variable>
#include <mutex>



enum class OrderType
{
    GoodTillCancel,  // holds till user says no and cancels the damn order
    FillandKill // completely execute the entire order immediately (fill) or cancel the entire order (kill) if it cannot be filled immediately. (have time constrains)
};

enum class Side
{
    Buy,
    Sell
};


using Price = std::int32_t;
using Quantity = std::uint32_t;
using OrderID = std::uint64_t; // string here?

struct LevelInfo
{
    Price price_;
    Quantity quantity_;
};

using LevelInfos = std::vector<LevelInfo>;

class OrderbookLevelInfos
{
public:
    OrderbookLevelInfos(const LevelInfos& bids, const LevelInfos& asks)
        : bids_{ bids }
        , asks_{ asks }
    { }

    const LevelInfos& GetBids() const { return bids_; }
    const LevelInfos& GetAsks() const { return asks_; }

private:
    LevelInfos bids_;
    LevelInfos asks_;
};

class Order
{
public:
    Order(OrderType orderType, OrderID orderID, Side side, Price price, Quantity quantity)
        : orderType_{ orderType }, orderID_{ orderID }, side_{ side },
        price_{ price }, initialQuantity_{ quantity }, remainingQuantity_{ quantity } {}

    OrderType GetOrderType() const { return orderType_; }
    OrderID GetOrderID() const { return orderID_; }
    Side GetSide() const { return side_; }
    Price GetPrice() const { return price_; }
    Quantity GetInitialQuantity() const { return initialQuantity_; }
    Quantity GetRemainingQuantity() const { return remainingQuantity_; }
    Quantity GetFilledQuantity() const { return GetInitialQuantity() - GetRemainingQuantity(); }
    bool isFilled() const { return  GetRemainingQuantity() == false; }
    void Fill(Quantity quantity)
    {
        if (quantity > GetRemainingQuantity())
        {
            std::ostringstream oss;
            oss << "Order (" << GetOrderID() << ") cannot be filled for more than its remainder quantity.";
            throw std::logic_error(oss.str());
        }

        remainingQuantity_ -= quantity;
    }

private:
    OrderType orderType_;
    OrderID orderID_;
    Side side_;
    Price price_;
    Quantity initialQuantity_;
    Quantity remainingQuantity_;
};

using OrderPointer = std::shared_ptr<Order>;
using OrderPointers = std::list<OrderPointer>;

class OrderModify
{
public:
    OrderModify(OrderID orderID, Side side, Price price, Quantity quantity)
        : orderID_{ orderID }
        , price_{ price }
        , side_{ side }
        , quantity_{ quantity }
    { }

    OrderID GetOrderID() const { return orderID_; }
    Side GetSide() const { return side_; }
    Price GetPrice() const { return price_; }
    Quantity GetQuantity() const { return quantity_; }

    OrderPointer ToOrderPointer(OrderType type) const
    {
        return std::make_shared<Order>(type, GetOrderID(), GetSide(), GetPrice(), GetQuantity());
    }

private:
    OrderID orderID_;
    Price price_;
    Side side_;
    Quantity quantity_;
};


struct TradeInfo //interface for tthe trade
{
    OrderID orderID_;
    Price price_;
    Quantity quantity_;
};

class Trade // aggreaation of bid and ask sides.
{
public:
    Trade(const TradeInfo& bidTrade, const TradeInfo& askTrade)
        : bidTrade_{ bidTrade }
        , askTrade_{ askTrade }
    { }

    const TradeInfo& GetBidTrade() const { return bidTrade_; }
    const TradeInfo& GetAskTrade() const { return askTrade_; }

private:
    TradeInfo bidTrade_;
    TradeInfo askTrade_;


};


using Trades = std::vector<Trade>;

class Orderbook
{
private:
    //using a map for bids (in descending from best bid ) and ask (ascending for best ask) we can have O(N) access . 
    // order iterator for its location 
    struct OrderEntry
    {
        OrderPointer order_{ nullptr };
        OrderPointers::iterator location_;

    };


    std::map<Price, OrderPointers, std::greater<Price>> bids_;
    std::map<Price, OrderPointers, std::less<Price>> asks_;
    std::unordered_map<OrderID, OrderEntry> orders_;

    //match methods
    // so we add an order, if its not f&k we add to the list, else if it doesnt match , we discard instantly

    bool CanMatch(Side side, Price price) const
    {
        if (side == Side::Buy)
        {
            if (asks_.empty())
                return false;

            const auto& [bestAsk, _] = *asks_.begin();
            return price >= bestAsk;
        }
        else
        {
            if (bids_.empty())
                return false;

            const auto& [bestBid, _] = *bids_.begin();
            return price <= bestBid;
        }
    }

    Trades MatchOrders()
    {
        Trades trades;
        trades.reserve(orders_.size());

        while (true)
        {
            if (bids_.empty() || asks_.empty())
                break;

            auto& [bidPrice, bids] = *bids_.begin();
            auto& [askPrice, asks] = *asks_.begin();

            if (bidPrice < askPrice)
                break;

            while (!bids.empty() && !asks.empty())
            {
                auto bid = bids.front();
                auto ask = asks.front();

                Quantity quantity = std::min(bid->GetRemainingQuantity(), ask->GetRemainingQuantity());

                bid->Fill(quantity);
                ask->Fill(quantity);

                if (bid->isFilled())
                {
                    bids.pop_front();
                    orders_.erase(bid->GetOrderID());
                }

                if (ask->isFilled())
                {
                    asks.pop_front();
                    orders_.erase(ask->GetOrderID());
                }


                trades.push_back(Trade{
                    TradeInfo{ bid->GetOrderID(), bid->GetPrice(), quantity },
                    TradeInfo{ ask->GetOrderID(), ask->GetPrice(), quantity }
                    });


            }


        }

        if (!bids_.empty())
        {
            auto& [_, bids] = *bids_.begin();
            auto& order = bids.front();
            if (order->GetOrderType() == OrderType::FillandKill)
                CancelOrder(order->GetOrderID());
        }

        if (!asks_.empty())
        {
            auto& [_, asks] = *asks_.begin();
            auto& order = asks.front();
            if (order->GetOrderType() == OrderType::FillandKill)
                CancelOrder(order->GetOrderID());
        }

        return trades;
    }

public:
    Trades AddOrder(OrderPointer order)
    {
        if (orders_.contains(order->GetOrderID()))
            return { };


        if (order->GetOrderType() == OrderType::FillandKill && !CanMatch(order->GetSide(), order->GetPrice()))
            return { };



        OrderPointers::iterator iterator;

        if (order->GetSide() == Side::Buy)
        {
            auto& orders = bids_[order->GetPrice()];
            orders.push_back(order);
            iterator = std::prev(orders.end());
        }
        else
        {
            auto& orders = asks_[order->GetPrice()];
            orders.push_back(order);
            iterator = std::prev(orders.end());
        }

        orders_.insert({ order->GetOrderID(), OrderEntry{ order, iterator } });


        return MatchOrders();
    }

    void CancelOrder(OrderID orderID)
    {
        if (!orders_.contains(orderID))
        {
            return;
        }

        const auto& [order, orderIterator] = orders_.at(orderID);
        orders_.erase(orderID);

        if (order->GetSide() == Side::Sell)
        {
            auto price = order->GetPrice();
            auto& orders = asks_.at(price);
            orders.erase(orderIterator);
            if (orders.empty())
            {
                asks_.erase(price);
            }
        }
        else
        {
            auto price = order->GetPrice();
            auto& orders = bids_.at(price);
            orders.erase(orderIterator);
            if (orders.empty())
            {
                bids_.erase(price);
            }
        }
    }

    Trades MatchOrder(OrderModify order)
    {
        if (!orders_.contains(order.GetOrderID()))
        {
            return {};
        }

        const auto& [existingOrder, _] = orders_.at(order.GetOrderID());
        CancelOrder(order.GetOrderID());
        return AddOrder(order.ToOrderPointer(existingOrder->GetOrderType()));
    }

    std::size_t Size() const {
        return orders_.size();
    }

    OrderbookLevelInfos GetOrderInfos() const
    {
        LevelInfos bidInfos, askInfos;
        bidInfos.reserve(orders_.size());
        askInfos.reserve(orders_.size());

        auto CreateLevelInfos = [](Price price, const OrderPointers& orders)
            {
                return LevelInfo{ price , std::accumulate(orders.begin(), orders.end(), (Quantity)0,[](Quantity runningSum, const OrderPointer& order)
                    {return runningSum + order->GetRemainingQuantity(); }) }; //lamda
            };


        for (const auto& [price, orders] : bids_)
            bidInfos.push_back(CreateLevelInfos(price, orders));

        for (const auto& [price, orders] : asks_)
            askInfos.push_back(CreateLevelInfos(price, orders));

        return OrderbookLevelInfos{ bidInfos,askInfos };
    }
};


int main()
{
    Orderbook orderbook;
    const OrderID orderid = 1;
    orderbook.AddOrder(std::make_shared<Order>(OrderType::GoodTillCancel, orderid, Side::Buy, 100, 10));
    std::cout << orderbook.Size() << std::endl;


    std::cin.get();
    return 0;
}

//com,it