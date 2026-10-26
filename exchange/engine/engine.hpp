#pragma once
#include <cassert>
#include <optional>
#include <variant>
#include <vector>
#include <cstddef>
#include "common/messages.hpp"
#include "exchange/engine/book.hpp"
#include "exchange/engine/matcher.hpp"

struct EngineOutput {
    std::vector<FIX::ExecReport> reports;
    std::vector<FIX::Trade>      trades;
    void clear() { reports.clear(); trades.clear(); }
};

template<class... Ts> struct overloaded : Ts... { using Ts::operator()...; };

class Engine {
public:
    explicit Engine(std::size_t max_symbols = 16, std::size_t max_fills = 1024)
    {
        fills_.reserve(max_fills);
        books_.reserve(max_symbols);
        instruments_.reserve(max_symbols);
    }

    void add_instrument(const FIX::Instrument& inst, std::size_t capacity) {
        assert(inst.id == static_cast<FIX::SymbolId>(books_.size()));
        instruments_.emplace_back(inst);
        books_.emplace_back(OrderBook(inst.id, inst.min_price, inst.max_price, inst.tick_size, capacity));

    }

    void on_message(const FIX::InboundMessage& msg, EngineOutput& out) {
        std::visit(overloaded{
            [&](const FIX::NewOrder& m)     { on_new(m, out); },
            [&](const FIX::CancelOrder& m)  { on_cancel(m, out); },
            [&](const FIX::ReplaceOrder& m) { on_replace(m, out); }
        }, msg);
    }

private:
    OrderBook* book_for(FIX::SymbolId id) {
        if (id >= static_cast<FIX::SymbolId>(books_.size()) || id < 0) { return nullptr; }
        return &books_[id];
    }

    void reject(FIX::ClientId client, FIX::OrderId order, FIX::Side side,
                FIX::RejectReason reason, EngineOutput& out) {
        FIX::ExecReport report{.client = client, .order = order, .side = side, .type = FIX::ExecType::Rejected,
                                .filled_qty = 0, .leaves_qty = 0, .last_price = 0,
                                .last_qty = 0, .reason = reason};
        out.reports.emplace_back(report);
    }

    void report_new(FIX::ClientId client, FIX::OrderId order, FIX::Side side,
                    FIX::Qty qty, EngineOutput& out) {
        FIX::ExecReport report{.client = client, .order = order, .side = side, .type = FIX::ExecType::New,
                                .filled_qty = 0, .leaves_qty = qty, .last_price = 0,
                                .last_qty = 0, .reason = FIX::RejectReason::None};

        out.reports.emplace_back(report);
    }

    void report_canceled(FIX::ClientId client, FIX::OrderId order, FIX::Side side,
                         FIX::Qty filled, EngineOutput& out) {
        FIX::ExecReport report({.client = client, .order = order, .side = side, .type = FIX::ExecType::Canceled,
                                .filled_qty = filled, .leaves_qty = 0, .last_price = 0, .last_qty = 0, .reason = FIX::RejectReason::None});
        out.reports.emplace_back(report);
    }

    void report_replaced(FIX::ClientId client, FIX::OrderId order, FIX::Side side,
                         FIX::Qty filled, FIX::Qty leaves, EngineOutput& out) {
        FIX::ExecReport report({.client = client, .order = order, .side = side, .type = FIX::ExecType::Replaced,
                                .filled_qty = filled, .leaves_qty = leaves, .last_price = 0, .last_qty = 0, .reason = FIX::RejectReason::None});
        out.reports.emplace_back(report);
    }

    FIX::RejectReason validate(const FIX::NewOrder& o, const OrderBook& book) const {
        if (o.qty <= 0) { return FIX::RejectReason::InvalidQty; }
        if (o.type == FIX::OrderType::Market && o.post_only == true)            { return FIX::RejectReason::InvalidCombination; }
        if (o.tif == FIX::TimeInForce::FOK && o.post_only == true)              { return FIX::RejectReason::InvalidCombination; }
        if (o.type == FIX::OrderType::Market && o.tif == FIX::TimeInForce::DAY) { return FIX::RejectReason::InvalidCombination; }

        if (o.type == FIX::OrderType::Limit || o.type == FIX::OrderType::StopLimit) {
            if (o.price < book.get_min_price() || o.price > book.get_max_price())   { return FIX::RejectReason::OutsideBand; }
            if ((o.price - book.get_min_price()) % book.get_tick_size() != 0)       { return FIX::RejectReason::InvalidPrice; }
        }

        if (o.type == FIX::OrderType::Stop || o.type == FIX::OrderType::StopLimit) {
            if (o.stop_price < book.get_min_price() || o.stop_price > book.get_max_price())     { return FIX::RejectReason::OutsideBand; }
            if ((o.stop_price - book.get_min_price()) % book.get_tick_size() != 0)              { return FIX::RejectReason::InvalidPrice; }
        }

        if (book.contains(o.order)) { return FIX::RejectReason::DuplicateOrder; }

        return FIX::RejectReason::None;
    }

    void on_new(const FIX::NewOrder& o, EngineOutput& out) {
        OrderBook* book = book_for(o.symbol_id);
        if (!book) {
            reject(o.client, o.order, o.side, FIX::RejectReason::UnknownSymbol, out);
            return;
        }
        auto r = validate(o, *book);
        if (r != FIX::RejectReason::None) {
            reject(o.client, o.order, o.side, r, out);
            return;
        }

        if (o.type == FIX::OrderType::Stop || o.type == FIX::OrderType::StopLimit) {
            return;
        }
        if (o.post_only) {
            if (o.side == FIX::Side::Buy) {
                std::optional<long> best_ask = book->best_ask();
                if (best_ask && *best_ask <= o.price) {
                    reject(o.client, o.order, o.side, FIX::RejectReason::PostOnlyWouldCross, out);
                    return;
                }
            }
            else {
                std::optional<long> best_buy = book->best_bid();
                if (best_buy && *best_buy >= o.price) {
                    reject(o.client, o.order, o.side, FIX::RejectReason::PostOnlyWouldCross, out);
                    return;
                }
            }
        }

        FIX::Side opp = opposite(o.side);
        if (o.tif == FIX::TimeInForce::FOK) {
            if (o.type == FIX::OrderType::Limit) {
                if (book->available(opp, o.price) < o.qty) {
                    reject(o.client, o.order, o.side, FIX::RejectReason::FokNotFilled, out);
                    return;
                }
            } else {
                if (book->available(opp, o.side == FIX::Side::Buy ? book->get_max_price() : book->get_min_price()) < o.qty) {
                    reject(o.client, o.order, o.side, FIX::RejectReason::FokNotFilled, out);
                    return;
                }
            }
        }

        report_new(o.client, o.order, o.side, o.qty, out);

        std::optional<FIX::Price> limit = (o.type == FIX::OrderType::Limit) ? std::optional<FIX::Price>{o.price} : std::nullopt;
        fills_.clear();
        FIX::Qty filled = match(*book, o.side, limit, o.qty, fills_);

        emit_fills(o.client, o.order, o.side, o.symbol_id, o.qty, 0, o.received_at, fills_, out);

        FIX::Qty remaining = o.qty - filled;
        if (remaining == 0) {
            return;
        }

        if (o.type == FIX::OrderType::Limit && o.tif == FIX::TimeInForce::DAY) {
            bool rest = book->rest(o.order, o.client, o.side, o.price, remaining, filled);
            if (!rest) {
                report_canceled(o.client, o.order, o.side, filled, out);
                return;
            }
        }
        if (o.type == FIX::OrderType::Market || o.tif == FIX::TimeInForce::FAK) {
            report_canceled(o.client, o.order, o.side, filled, out);
        }


    }

    void emit_fills(FIX::ClientId client, FIX::OrderId order, FIX::Side side, FIX::SymbolId symbol,
                    FIX::Qty total_qty, FIX::Qty start_filled, FIX::Timestamp time,
                    const std::vector<Fill>& fills, EngineOutput& out) {
        FIX::Qty cum = start_filled;
        for (const auto& fill : fills) {
            cum += fill.qty;
            out.trades.emplace_back(FIX::Trade{.seq = ++seq_num_, .symbol = symbol, .price = fill.price,
                                            .qty = fill.qty, .side = side, .time = time});
            out.reports.emplace_back(FIX::ExecReport{.client = client, .order = order, .side = side,
                                                     .type = FIX::ExecType::Trade, .filled_qty = cum,
                                                     .leaves_qty = total_qty - cum, .last_price = fill.price,
                                                     .last_qty = fill.qty, .reason = FIX::RejectReason::None});
            out.reports.emplace_back(FIX::ExecReport{.client = fill.client, .order = fill.id, .side = fill.side,
                                                     .type = FIX::ExecType::Trade, .filled_qty = fill.filled,
                                                     .leaves_qty = fill.leaves, .last_price = fill.price,
                                                     .last_qty = fill.qty, .reason = FIX::RejectReason::None});
        }
    }

    void on_cancel(const FIX::CancelOrder& o, EngineOutput& out) {
        OrderBook* book = book_for(o.symbol);
        if (!book) {
            reject(o.client, o.order, o.side,FIX::RejectReason::UnknownSymbol, out);
            return;
        }
        std::optional<OrderIdx> idx = book->get_order_by_id(o.order);
        if (!idx) {
            reject(o.client, o.order, o.side,FIX::RejectReason::UnknownOrder, out);
            return;
        }
        const Order& order = book->get_order(*idx);
        const auto client = order.client;
        const auto side = order.side;
        const auto filled = order.filled;
        if (client != o.client) {
            reject(o.client, o.order, o.side,FIX::RejectReason::UnknownOrder, out);
            return;
        }
        book->remove(o.order);
        report_canceled(client, o.order, side, filled, out);

    }

    void on_replace(const FIX::ReplaceOrder& o, EngineOutput& out) {
        OrderBook* book = book_for(o.symbol);
        if (!book) {
            reject(o.client, o.order, o.side,FIX::RejectReason::UnknownSymbol, out);
            return;
        }
        std::optional<OrderIdx> idx = book->get_order_by_id(o.order);
        if (!idx) {
            reject(o.client, o.order, o.side,FIX::RejectReason::UnknownOrder, out);
            return;
        }
        const Order& order = book->get_order(*idx);
        const auto client = order.client;
        const auto side = order.side;
        auto filled = order.filled;
        const auto leaves_old = order.qty;
        const auto leaves_new = o.new_qty - filled;
        if (client != o.client) {
            reject(o.client, o.order, o.side,FIX::RejectReason::UnknownOrder, out);
            return;
        }
        if (!book->valid_price(o.new_price)) {
            if (o.new_price < book->get_min_price() || o.new_price > book->get_max_price()) {
                reject(o.client, o.order, o.side,FIX::RejectReason::OutsideBand, out);
            }
            else {
                reject(o.client, o.order, o.side,FIX::RejectReason::InvalidPrice, out);
            }
            return;
        }
        if (o.new_qty <= filled) {
            reject(o.client, o.order, o.side,FIX::RejectReason::InvalidQty, out);
            return;
        }

        if (o.new_price == order.price && leaves_new <= leaves_old) {
            book->shrink(*idx, leaves_old - leaves_new);
            report_replaced(client, o.order, side, filled, leaves_new, out);
            return;
        }
        else {
            book->remove(o.order);
            report_replaced(client, o.order, side, filled, leaves_new, out);
            fills_.clear();
            FIX::Qty executed = match(*book, side, o.new_price, leaves_new, fills_);
            emit_fills(client, o.order, side, o.symbol, o.new_qty, filled, o.received_at, fills_, out);
            FIX::Qty remaining = leaves_new - executed;
            if (remaining > 0) {
                bool rest = book->rest(o.order, client, side, o.new_price, remaining, filled + executed);
                if (!rest) {
                    report_canceled(client, o.order, side, filled + executed, out);
                }
            }
        }
    }

    std::vector<OrderBook>  books_{};
    FIX::SeqNum             seq_num_{0};
    std::vector<Fill>       fills_;
    std::vector< FIX::Instrument > instruments_;
};
