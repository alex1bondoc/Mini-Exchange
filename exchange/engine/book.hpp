#pragma once
#include "exchange/engine/pool.hpp"
#include <unordered_map>
#include <optional>
#include <vector>

struct Level {
    OrderIdx head{NIL};
    OrderIdx tail{NIL};
    FIX::Qty total_qty{};
};

class OrderBook {
public:
    OrderBook(FIX::Price min_price, FIX::Price max_price, FIX::Price tick_size, std::size_t capacity) :
    min_price_{min_price},
    max_price_{max_price},
    tick_size_{tick_size},
    bids_(static_cast<std::size_t>(to_index(max_price) + 1)),
    asks_(bids_.size()),
    best_bid_{-1},
    best_ask_{static_cast<long>(asks_.size())},
    pool_(capacity) {}

    bool rest(FIX::OrderId id, FIX::ClientId client, FIX::Side side, FIX::Price price, FIX::Qty qty) {
        if (id_to_idx_.contains(id) || qty <= 0 || !valid_price(price)) {
            return false;
        }
        const auto slot = pool_.allocate();
        if (!slot) {
            return false;
        }
        const OrderIdx idx = *slot;

        Order& order = pool_[idx];
        order.id        = id;
        order.client    = client;
        order.side      = side;
        order.price     = price;
        order.qty       = qty;

        const long level = to_index(price);
        if (side == FIX::Side::Buy) {
            push_back(bids_[level], idx);
            if (level > best_bid_) {
                best_bid_ = level;
            }
        }
        else {
            push_back(asks_[level], idx);
            if (level < best_ask_) {
                best_ask_ = level;
            }
        }
        id_to_idx_[id] = idx;
        return true;
    }

    bool remove(FIX::OrderId id) {
        const auto it = id_to_idx_.find(id);
        if (it == id_to_idx_.end()) {
            return false;
        }
        const auto idx = it->second;
        const auto& order = pool_[idx];
        

        if (order.side == FIX::Side::Buy) {
            Level& level = bids_[to_index(order.price)];
            unlink(level, idx);
            best_bid_ = find_next_bid(best_bid_);
        }
        else {
            Level& level = asks_[to_index(order.price)];
            unlink(level, idx);
            best_ask_ = find_next_ask(best_ask_);
        }
        pool_.release(idx);
        id_to_idx_.erase(it);
        return true;
    }

    std::optional<OrderIdx> front(FIX::Side side) const {
        if (side == FIX::Side::Buy) {
            if (best_bid_ == -1) {
                return std::nullopt;
            }
            return bids_[best_bid_].head;
        }
        else {
            if (best_ask_ == static_cast<long>(asks_.size())) {
                return std::nullopt;
            }
            return asks_[best_ask_].head;
        }
    }

    const Order& get_order(OrderIdx idx) const {
        return pool_[idx];
    }

    void reduce(OrderIdx idx, FIX::Qty qty) {
        Order& order = pool_[idx];

        FIX::Price price = order.price;
        if (order.side == FIX::Side::Buy) {
            Level& level = bids_[to_index(order.price)];
            level.total_qty -= qty;
            order.qty -= qty;
            order.filled += qty;
        }
        else {
            Level& level = asks_[to_index(order.price)];
            level.total_qty -= qty;
            order.qty -= qty;
            order.filled += qty;
        }
    }

    FIX::Qty available(FIX::Side side, FIX::Price limit) const {
        FIX::Qty qty = 0;
        if (side == FIX::Side::Sell) {
            if (limit > max_price_) {
                limit = max_price_;
            }
            long end =  to_index(limit);
            for (long i{best_ask_}; i <= end; ++i) {
                qty += asks_[i].total_qty;
            }
        }
        else {
            if (limit < min_price_) {
                limit = min_price_;
            }
            long end = to_index(limit);
            for (long i{best_bid_}; i >= end; --i) {
                qty += bids_[i].total_qty;
            }
        }
        return qty;
    }

    std::optional<FIX::Price> best_bid() const {
        if (best_bid_ == -1) {
            return std::nullopt;
        }
        return to_price(best_bid_);
    }
    std::optional<FIX::Price> best_ask() const {
        if (best_ask_ == static_cast<long>(asks_.size())) {
            return std::nullopt;
        }
        return to_price(best_ask_);
    }
    FIX::Qty level_qty(FIX::Side side, FIX::Price price) const {
        if (!valid_price(price)) {
            return 0;
        }
        const long level = to_index(price);
        return side == FIX::Side::Buy ? bids_[level].total_qty : asks_[level].total_qty;
    }
    std::vector<FIX::OrderId> orders_at(FIX::Side side, FIX::Price price) const {
        if (!valid_price(price)) {
            return {};
        }
        const long level = to_index(price);
        const Level& lvl = side == FIX::Side::Buy ? bids_[level] : asks_[level];
        std::vector<FIX::OrderId> ids;
        for (OrderIdx i = lvl.head; i != NIL; i = pool_[i].next) {
            ids.push_back(pool_[i].id);
        }
        return ids;
    }

private:
    bool valid_price(FIX::Price price) const {
        return price >= min_price_ && price <= max_price_ && (price - min_price_) % tick_size_ == 0;
    }
    long to_index(FIX::Price price) const { return static_cast<long>((price - min_price_) / tick_size_); }
    FIX::Price  to_price(long idx) const  { return idx * tick_size_ + min_price_; }

    long find_next_bid(long from) const {
        for (long i = from; i >= 0; --i) {
            if (bids_[i].head != NIL) {
                return i;
            }
        }
        return -1;
    }

    long find_next_ask(long from) const {
        long n = static_cast<long>(asks_.size());
        for (long i = from; i < n; ++i) {
            if (asks_[i].head != NIL) {
                return i;
            }
        }
        return n;
    }

    void push_back(Level& level, OrderIdx idx) {
        Order& order = pool_[idx];
        order.prev = level.tail;
        order.next = NIL;
        if (level.tail == NIL) {
            level.head = idx;
        } else {
            pool_[level.tail].next = idx;
        }
        level.tail = idx;
        level.total_qty += order.qty;
    }
    void unlink(Level& level, OrderIdx idx) {
        Order& order = pool_[idx];
        if (order.prev == NIL) {
            level.head = order.next;
        }
        else {
            pool_[order.prev].next = order.next;
        }

        if (order.next == NIL) {
            level.tail = order.prev;
        }
        else {
            pool_[order.next].prev = order.prev;
        }

        level.total_qty -= order.qty;
        order.prev = NIL;
        order.next = NIL;
    }

    FIX::Price min_price_, max_price_, tick_size_;
    std::vector<Level> bids_, asks_;
    long best_bid_{-1};
    long best_ask_;
    OrderPool pool_;
    std::unordered_map<FIX::OrderId, OrderIdx> id_to_idx_;
};