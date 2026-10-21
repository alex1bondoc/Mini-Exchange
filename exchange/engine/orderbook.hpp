#pragma once
#include "exchange/engine/pool.hpp"
#include <unordered_map>
#include <optional>
#include <vector>
#include <algorithm>

struct EngineOutput {
    std::vector<FIX::ExecReport> reports;
    std::vector<FIX::Trade>      trades;
    void clear() { reports.clear(); trades.clear(); }
};

struct Level {
    OrderIdx head{NIL};
    OrderIdx tail{NIL};
    FIX::Qty total_qty{};
};

class OrderBook {
public:
    OrderBook(FIX::SymbolId symbol, FIX::Price min_price, FIX::Price max_price, FIX::Price tick_size, std::size_t capacity) :
    symbol_{symbol},
    min_price_{min_price},
    max_price_{max_price},
    tick_size_{tick_size},
    bids_(static_cast<std::size_t>(to_index(max_price) + 1)),
    asks_(bids_.size()),
    best_bid_{-1},
    best_ask_{static_cast<long>(asks_.size())},
    pool_(capacity) {}

    void add_limit(FIX::OrderId id, FIX::ClientId client, FIX::Side side, FIX::Price price,
                    FIX::Qty qty, EngineOutput& out) {
        FIX::RejectReason reason = FIX::RejectReason::None;
        if (qty <= 0) reason = FIX::RejectReason::InvalidQty;
        else if (price < min_price_ || price > max_price_) reason = FIX::RejectReason::OutSideBand;
        else if ((price - min_price_) % tick_size_ != 0) reason = FIX::RejectReason::InvalidPrice;
        
        if (reason != FIX::RejectReason::None) {
            out.reports.push_back({client, id, side, FIX::ExecType::Rejected, 0, 0, 0, 0, reason});
            return;
        }
        out.reports.push_back({client, id, side, FIX::ExecType::New, 0, 0, 0, 0, reason});
        FIX::Qty remaining  = qty;
        FIX::Qty filled     = 0;

        if (side == FIX::Side::Buy) {
            while (remaining > 0 && best_ask_ < static_cast<long>(asks_.size()) && to_price(best_ask_) < price) {
                match_level(asks_[best_ask_], id, client, side, remaining, filled, qty, out);
                if (asks_[best_ask_].head == NIL) {
                    best_ask_ = find_next_ask(best_ask_);
                }
            }
        }
        else {
            while (remaining > 0 && best_bid_ != -1 && to_price(best_bid_) > price) {
                match_level(bids_[best_bid_], id, client, side, remaining, filled, qty, out);
                if (bids_[best_bid_].head == NIL) {
                    best_bid_ = find_next_bid(best_bid_);
                }
            }
        }

        if (remaining > 0) {
            rest(id, client, side, price, remaining);
            pool_[id_to_idx_[id]].filled = filled;
        }

    }

    void match_level(Level& level, FIX::OrderId id, FIX::ClientId client, 
        FIX::Side side, FIX::Qty& remaining, FIX::Qty& filled, 
        FIX::Qty total_qty, EngineOutput& out) {
        while (remaining > 0 && level.head != NIL) {
            const OrderIdx resting_idx = level.head;
            Order& resting  = pool_[resting_idx];
            const FIX::Qty      fill        = std::min(remaining, resting.qty);
            const FIX::Price    trade_price = resting.price;
            remaining       -= fill;
            filled          += fill;
            resting.qty     -= fill;
            resting.filled  += fill;
            level.total_qty -= fill;

            out.trades.push_back({next_trade_seq_++, symbol_, trade_price, fill, side, 0});
            // new
            out.reports.push_back({client, id, side, FIX::ExecType::Trade, filled, total_qty - filled, trade_price, fill, FIX::RejectReason::None});

            // resting 
            out.reports.push_back({resting.client, resting.id, resting.side,
                                   resting.filled, resting.qty, trade_price,
                                   fill, FIX::RejectReason::None});
            
            if (resting.qty == 0) {
                unlink(level, id_to_idx_[resting_idx]);
                id_to_idx_.erase(resting.id);
                pool_release(resting_idx);
            }
        }
    }

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

    bool cancel(FIX::OrderId id) {
        const auto it  = id_to_idx_.find(id);
        if (it == id_to_idx_.end()) {
            return false;
        }
        const OrderIdx idx = it->second;
        const Order& order = pool_[idx];
        const long level = to_index(order.price);

        if (order.side == FIX::Side::Buy) {
            unlink(bids_[level], idx);
            if (level == best_bid_) {
                best_bid_ = find_next_bid(level);
            }
        }
        else {
            unlink(asks_[level], idx);
            if (level == best_ask_) {
                best_ask_ = find_next_ask(level);
            }
        }
        pool_.release(idx);
        id_to_idx_.erase(id);
        return true;
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

    FIX::SymbolId symbol_;
    FIX::SeqNum next_trade_seq{1};
    FIX::Price min_price_, max_price_, tick_size_;
    std::vector<Level> bids_, asks_;
    long best_bid_{-1};
    long best_ask_;
    OrderPool pool_;
    std::unordered_map<FIX::OrderId, OrderIdx> id_to_idx_;
};