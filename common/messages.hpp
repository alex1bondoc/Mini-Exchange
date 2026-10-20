#pragma once
#include "types.hpp"
#include <variant>

// client ----> engine
struct NewOrder {
    ClientId    client;
    OrderId     order;
    SymbolId    symbol_id;
    Side        side;
    OrderType   type;
    TimeInForce tif;
    bool        post_only;
    Price       price;
    Price       stop_price;
    Qty         qty;
    Timestamp   received_at;
};
struct CancelOrder {
    ClientId    client;
    OrderId     order;
    SymbolId    symbol;
};
struct ReplaceOrder {
    ClientId    client;
    OrderId     order;
    SymbolId    symbol;
    Side        side;
    Price       new_price;
    Qty         new_qty;
};
using InboundMessage = std::variant<NewOrder, CancelOrder, ReplaceOrder>;
// engine ----> client
enum class RejectReason {
    None,
    InvalidPrice,
    OutsideBand,
    InvalidQty,
    UnknownOrder,
    PostOnlyWouldCross,
    FokNotFilled,
    InvalidCombination
};
enum class ExecType {
    New,
    PartialFill,
    Fill,
    Canceled,
    Rejected
};
struct ExecReport {
    ClientId    client;
    OrderId     order;
    ExecType    type;
    Qty         filled_qty;
    Qty         leaves_qty;
    Price       last_price;
    Qty         last_qty;
    RejectReason reason;
};
// engine ----> market data
struct Trade {
    SeqNum      seq;
    SymbolId    symbol;
    Price       price;
    Qty         qty;
    Side        side;
    Timestamp   time;
};
