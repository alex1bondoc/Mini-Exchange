#pragma once
#include <vector>
#include <optional>
#include <algorithm>
#include "exchange/engine/book.hpp"

struct Fill {
    FIX::OrderId        id{};
    FIX::ClientId       client{};
    FIX::Side           side{};
    FIX::Price          price{};
    FIX::Qty            qty{};
    FIX::Qty            filled{};
    FIX::Qty            leaves{};
};

inline FIX::Qty match(OrderBook& book, FIX::Side aggressor, std::optional<FIX::Price> limit,
                      FIX::Qty qty, std::vector<Fill>& fills) {
    FIX::Side side = (aggressor == FIX::Side::Buy) ? FIX::Side::Sell : FIX::Side::Buy;
    FIX::Qty remaining = qty;
    while (remaining > 0) {
        auto opt = book.front(side);
        if (!opt) { break; }
        OrderIdx idx = *opt;
        const Order& resting = book.get_order(idx);
        if (aggressor == FIX::Side::Buy) {
            if (limit && resting.price > *limit) { break; }
        }
        else {
            if (limit && resting.price < *limit) { break; }
        }
        auto fill_qty = std::min(remaining, resting.qty);
        book.reduce(idx, fill_qty);
        fills.push_back(Fill{.id     = resting.id,
                             .client = resting.client,
                             .side   = resting.side,
                             .price  = resting.price,
                             .qty    = fill_qty,
                             .filled = resting.filled,
                             .leaves = resting.qty});
        const Fill& fill = fills.back();
        if (fill.leaves == 0) {
            book.remove(fill.id);
        }
        remaining -= fill_qty;
    }
    return qty - remaining;
}
