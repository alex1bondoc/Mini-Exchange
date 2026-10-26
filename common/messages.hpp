#pragma once
#include "types.hpp"
#include <variant>

namespace FIX {
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
        Side        side;
    };
    struct ReplaceOrder {
        ClientId    client;
        OrderId     order;
        SymbolId    symbol;
        Side        side;
        Price       new_price;
        Qty         new_qty;
        Timestamp   received_at;
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
        InvalidCombination,
        UnknownSymbol,
        DuplicateOrder,
        BookFull
    };
    enum class ExecType {
        New,
        Trade,
        Canceled,
        Replaced,
        Rejected
    };

    enum class CxlRejResponseTo : char { Cancel = '1', Replace = '2' };
    enum class CxlRejReason : int64_t { TooLate = 0, UnknownOrder = 1 };
    enum class SessionRejectReason : int64_t { 
        RequiredTagMissing = 1, 
        ValueIsIncorrect = 5, 
        IncorrectDataFormat = 6, 
        InvalidMsgType = 11 
    };
    
    struct ExecReport {
        ClientId    client;
        OrderId     order;
        Side        side;
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
}
