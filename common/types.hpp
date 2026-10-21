#pragma once
#include <cstdint>
#include <string_view>
#include <array>

namespace FIX{
    
    using Price     = int64_t;
    using Qty       = int64_t;
    using OrderId   = int64_t;
    using ClientId  = int64_t;
    using SymbolId  = int64_t;
    using SeqNum    = int64_t;
    using Timestamp = int64_t;

    enum class Side {
        Buy,
        Sell
    };
    enum class OrderType {
        Market,         // best prices now
        Limit,          // max X or min X price
        Stop,           // hidden till a surtain price than is Market
        StopLimit       // hidden till a surtain price than is Limit
    };
    enum class TimeInForce {
        DAY,            // till the end of the date
        FAK,            // fill and kill
        FOK             // fill or kill (all or nothing)
    };

    struct Symbol {
        std::array<char, 8> chars{};
        constexpr std::string_view view() const {
            std::size_t n = 0;
            while (n < chars.size() && chars[n] != '\0') {
                n ++;
            }
            return {chars.data(), n};
        }
    };

    struct Instrument {
        SymbolId    id;
        Symbol      symbol;
        Price       tick_size;
        Price       min_price;
        Price       max_price;
    };
}