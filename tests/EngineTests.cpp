#include <gtest/gtest.h>
#include "exchange/engine/engine.hpp"
#include <ostream>
#include <vector>

namespace FIX {

bool operator==(const ExecReport& a, const ExecReport& b) {
    return a.client == b.client && a.order == b.order && a.side == b.side && a.type == b.type &&
           a.filled_qty == b.filled_qty && a.leaves_qty == b.leaves_qty &&
           a.last_price == b.last_price && a.last_qty == b.last_qty && a.reason == b.reason;
}

bool operator==(const Trade& a, const Trade& b) {
    return a.seq == b.seq && a.symbol == b.symbol && a.price == b.price && a.qty == b.qty &&
           a.side == b.side && a.time == b.time;
}

void PrintTo(const ExecReport& r, std::ostream* os) {
    static const char* types[] = {"New", "Trade", "Canceled", "Replaced", "Rejected"};
    *os << "{" << types[static_cast<int>(r.type)] << " client=" << r.client << " order=" << r.order
        << " side=" << (r.side == Side::Buy ? "Buy" : "Sell")
        << " filled=" << r.filled_qty << " leaves=" << r.leaves_qty
        << " last=" << r.last_qty << "@" << r.last_price
        << " reason=" << static_cast<int>(r.reason) << "}";
}

void PrintTo(const Trade& t, std::ostream* os) {
    *os << "{seq=" << t.seq << " symbol=" << t.symbol << " " << t.qty << "@" << t.price
        << " side=" << (t.side == Side::Buy ? "Buy" : "Sell") << " time=" << t.time << "}";
}

}  // namespace FIX

namespace {

using FIX::Side;
using FIX::OrderType;
using FIX::TimeInForce;
using FIX::RejectReason;
using FIX::ExecType;
using Reports = std::vector<FIX::ExecReport>;
using Trades  = std::vector<FIX::Trade>;

constexpr FIX::SymbolId  SYMBOL = 0;
constexpr FIX::Price     MIN    = 1000;
constexpr FIX::Price     MAX    = 2000;
constexpr FIX::Price     TICK   = 5;
constexpr FIX::ClientId  CLIENT = 1;
constexpr FIX::Timestamp TIME   = 777;

FIX::Instrument instrument(FIX::SymbolId id = SYMBOL) {
    return FIX::Instrument{.id = id, .symbol = {}, .tick_size = TICK, .min_price = MIN, .max_price = MAX};
}

FIX::NewOrder limit(FIX::OrderId id, Side side, FIX::Price price, FIX::Qty qty,
                    TimeInForce tif = TimeInForce::DAY, FIX::ClientId client = CLIENT, bool post_only = false) {
    return FIX::NewOrder{.client = client, .order = id, .symbol_id = SYMBOL, .side = side,
                         .type = OrderType::Limit, .tif = tif, .post_only = post_only,
                         .price = price, .stop_price = 0, .qty = qty, .received_at = TIME};
}

FIX::NewOrder market(FIX::OrderId id, Side side, FIX::Qty qty,
                     TimeInForce tif = TimeInForce::FAK, FIX::ClientId client = CLIENT) {
    return FIX::NewOrder{.client = client, .order = id, .symbol_id = SYMBOL, .side = side,
                         .type = OrderType::Market, .tif = tif, .post_only = false,
                         .price = 0, .stop_price = 0, .qty = qty, .received_at = TIME};
}

FIX::NewOrder stop(FIX::OrderId id, Side side, FIX::Price stop_price, FIX::Qty qty) {
    return FIX::NewOrder{.client = CLIENT, .order = id, .symbol_id = SYMBOL, .side = side,
                         .type = OrderType::Stop, .tif = TimeInForce::DAY, .post_only = false,
                         .price = 0, .stop_price = stop_price, .qty = qty, .received_at = TIME};
}

FIX::NewOrder stop_limit(FIX::OrderId id, Side side, FIX::Price price, FIX::Price stop_price, FIX::Qty qty) {
    return FIX::NewOrder{.client = CLIENT, .order = id, .symbol_id = SYMBOL, .side = side,
                         .type = OrderType::StopLimit, .tif = TimeInForce::DAY, .post_only = false,
                         .price = price, .stop_price = stop_price, .qty = qty, .received_at = TIME};
}

FIX::CancelOrder cancel(FIX::OrderId id, Side side = Side::Buy, FIX::ClientId client = CLIENT) {
    return FIX::CancelOrder{.client = client, .order = id, .symbol = SYMBOL, .side = side};
}

FIX::ReplaceOrder replace(FIX::OrderId id, FIX::Price price, FIX::Qty qty,
                          Side side = Side::Buy, FIX::ClientId client = CLIENT) {
    return FIX::ReplaceOrder{.client = client, .order = id, .symbol = SYMBOL, .side = side,
                             .new_price = price, .new_qty = qty, .received_at = TIME};
}

FIX::ExecReport new_(FIX::ClientId client, FIX::OrderId id, Side side, FIX::Qty qty) {
    return {.client = client, .order = id, .side = side, .type = ExecType::New,
            .filled_qty = 0, .leaves_qty = qty, .last_price = 0, .last_qty = 0, .reason = RejectReason::None};
}

FIX::ExecReport trade(FIX::ClientId client, FIX::OrderId id, Side side, FIX::Qty filled, FIX::Qty leaves,
                      FIX::Price last_price, FIX::Qty last_qty) {
    return {.client = client, .order = id, .side = side, .type = ExecType::Trade,
            .filled_qty = filled, .leaves_qty = leaves, .last_price = last_price, .last_qty = last_qty,
            .reason = RejectReason::None};
}

FIX::ExecReport canceled(FIX::ClientId client, FIX::OrderId id, Side side, FIX::Qty filled) {
    return {.client = client, .order = id, .side = side, .type = ExecType::Canceled,
            .filled_qty = filled, .leaves_qty = 0, .last_price = 0, .last_qty = 0, .reason = RejectReason::None};
}

FIX::ExecReport replaced(FIX::ClientId client, FIX::OrderId id, Side side, FIX::Qty filled, FIX::Qty leaves) {
    return {.client = client, .order = id, .side = side, .type = ExecType::Replaced,
            .filled_qty = filled, .leaves_qty = leaves, .last_price = 0, .last_qty = 0, .reason = RejectReason::None};
}

FIX::ExecReport rejected(FIX::ClientId client, FIX::OrderId id, Side side, RejectReason reason) {
    return {.client = client, .order = id, .side = side, .type = ExecType::Rejected,
            .filled_qty = 0, .leaves_qty = 0, .last_price = 0, .last_qty = 0, .reason = reason};
}

FIX::Trade print(FIX::SeqNum seq, FIX::Price price, FIX::Qty qty, Side aggressor,
                 FIX::Timestamp time = TIME, FIX::SymbolId symbol = SYMBOL) {
    return {.seq = seq, .symbol = symbol, .price = price, .qty = qty, .side = aggressor, .time = time};
}

class EngineTest : public ::testing::Test {
protected:
    void SetUp() override { engine.add_instrument(instrument(), 64); }

    // golește ieșirea și trimite un singur mesaj
    const EngineOutput& send(const FIX::InboundMessage& msg) {
        out.clear();
        engine.on_message(msg, out);
        return out;
    }

    Engine       engine;
    EngineOutput out;
};

}  // namespace

// --- simbol ---

TEST_F(EngineTest, UnknownSymbolNewIsRejected) {
    auto o = limit(1, Side::Buy, 1500, 10);
    o.symbol_id = 1;
    send(o);
    EXPECT_EQ(out.reports, (Reports{rejected(CLIENT, 1, Side::Buy, RejectReason::UnknownSymbol)}));
    EXPECT_TRUE(out.trades.empty());
}

TEST_F(EngineTest, NegativeSymbolIsRejected) {
    auto o = limit(1, Side::Buy, 1500, 10);
    o.symbol_id = -1;
    send(o);
    EXPECT_EQ(out.reports, (Reports{rejected(CLIENT, 1, Side::Buy, RejectReason::UnknownSymbol)}));
}

TEST_F(EngineTest, UnknownSymbolCancelIsRejected) {
    auto c = cancel(1);
    c.symbol = 5;
    send(c);
    EXPECT_EQ(out.reports, (Reports{rejected(CLIENT, 1, Side::Buy, RejectReason::UnknownSymbol)}));
}

TEST_F(EngineTest, UnknownSymbolReplaceIsRejected) {
    auto r = replace(1, 1500, 10);
    r.symbol = 5;
    send(r);
    EXPECT_EQ(out.reports, (Reports{rejected(CLIENT, 1, Side::Buy, RejectReason::UnknownSymbol)}));
}

TEST_F(EngineTest, InstrumentsHaveSeparateBooks) {
    engine.add_instrument(instrument(1), 64);
    send(limit(1, Side::Sell, 1500, 10));

    auto buy = limit(2, Side::Buy, 1500, 10);
    buy.symbol_id = 1;
    send(buy);
    EXPECT_EQ(out.reports, (Reports{new_(CLIENT, 2, Side::Buy, 10)}));
    EXPECT_TRUE(out.trades.empty());

    auto sell = limit(3, Side::Sell, 1500, 4);
    sell.symbol_id = 1;
    send(sell);
    EXPECT_EQ(out.trades, (Trades{print(1, 1500, 4, Side::Sell, TIME, 1)}));
}

// --- validare ---

TEST_F(EngineTest, ZeroQtyIsRejected) {
    send(limit(1, Side::Buy, 1500, 0));
    EXPECT_EQ(out.reports, (Reports{rejected(CLIENT, 1, Side::Buy, RejectReason::InvalidQty)}));
}

TEST_F(EngineTest, NegativeQtyIsRejected) {
    send(limit(1, Side::Sell, 1500, -5));
    EXPECT_EQ(out.reports, (Reports{rejected(CLIENT, 1, Side::Sell, RejectReason::InvalidQty)}));
}

TEST_F(EngineTest, InvalidQtyIsCheckedBeforePrice) {
    send(limit(1, Side::Buy, 5000, 0));
    EXPECT_EQ(out.reports, (Reports{rejected(CLIENT, 1, Side::Buy, RejectReason::InvalidQty)}));
}

TEST_F(EngineTest, MarketPostOnlyIsRejected) {
    auto o = market(1, Side::Buy, 10);
    o.post_only = true;
    send(o);
    EXPECT_EQ(out.reports, (Reports{rejected(CLIENT, 1, Side::Buy, RejectReason::InvalidCombination)}));
}

TEST_F(EngineTest, FokPostOnlyIsRejected) {
    send(limit(1, Side::Buy, 1500, 10, TimeInForce::FOK, CLIENT, true));
    EXPECT_EQ(out.reports, (Reports{rejected(CLIENT, 1, Side::Buy, RejectReason::InvalidCombination)}));
}

TEST_F(EngineTest, MarketDayIsRejected) {
    send(market(1, Side::Sell, 10, TimeInForce::DAY));
    EXPECT_EQ(out.reports, (Reports{rejected(CLIENT, 1, Side::Sell, RejectReason::InvalidCombination)}));
}

TEST_F(EngineTest, LimitBelowBandIsRejected) {
    send(limit(1, Side::Buy, MIN - TICK, 10));
    EXPECT_EQ(out.reports, (Reports{rejected(CLIENT, 1, Side::Buy, RejectReason::OutsideBand)}));
}

TEST_F(EngineTest, LimitAboveBandIsRejected) {
    send(limit(1, Side::Sell, MAX + TICK, 10));
    EXPECT_EQ(out.reports, (Reports{rejected(CLIENT, 1, Side::Sell, RejectReason::OutsideBand)}));
}

TEST_F(EngineTest, LimitOffTickIsRejected) {
    send(limit(1, Side::Buy, 1502, 10));
    EXPECT_EQ(out.reports, (Reports{rejected(CLIENT, 1, Side::Buy, RejectReason::InvalidPrice)}));
}

TEST_F(EngineTest, LimitAtBandEdgesIsAccepted) {
    send(limit(1, Side::Buy, MIN, 10));
    EXPECT_EQ(out.reports, (Reports{new_(CLIENT, 1, Side::Buy, 10)}));
    send(limit(2, Side::Sell, MAX, 10));
    EXPECT_EQ(out.reports, (Reports{new_(CLIENT, 2, Side::Sell, 10)}));
}

TEST_F(EngineTest, StopLimitPriceOutsideBandIsRejected) {
    send(stop_limit(1, Side::Buy, MAX + TICK, 1500, 10));
    EXPECT_EQ(out.reports, (Reports{rejected(CLIENT, 1, Side::Buy, RejectReason::OutsideBand)}));
}

TEST_F(EngineTest, StopPriceOutsideBandIsRejected) {
    send(stop(1, Side::Buy, MIN - TICK, 10));
    EXPECT_EQ(out.reports, (Reports{rejected(CLIENT, 1, Side::Buy, RejectReason::OutsideBand)}));
}

TEST_F(EngineTest, StopPriceOffTickIsRejected) {
    send(stop_limit(1, Side::Sell, 1500, 1503, 10));
    EXPECT_EQ(out.reports, (Reports{rejected(CLIENT, 1, Side::Sell, RejectReason::InvalidPrice)}));
}

TEST_F(EngineTest, MarketPriceIsNotValidated) {
    send(market(1, Side::Buy, 10));
    EXPECT_EQ(out.reports, (Reports{new_(CLIENT, 1, Side::Buy, 10), canceled(CLIENT, 1, Side::Buy, 0)}));
}

TEST_F(EngineTest, DuplicateRestingIdIsRejected) {
    send(limit(1, Side::Buy, 1500, 10));
    send(limit(1, Side::Buy, 1495, 20));
    EXPECT_EQ(out.reports, (Reports{rejected(CLIENT, 1, Side::Buy, RejectReason::DuplicateOrder)}));

    send(limit(2, Side::Sell, 1495, 30, TimeInForce::FAK));
    EXPECT_EQ(out.trades, (Trades{print(1, 1500, 10, Side::Sell)}));
}

TEST_F(EngineTest, RejectedOrderDoesNotTouchBook) {
    send(limit(1, Side::Sell, 1500, 10));
    send(limit(2, Side::Buy, 1502, 10));
    EXPECT_TRUE(out.trades.empty());

    send(limit(3, Side::Buy, 1500, 10));
    EXPECT_EQ(out.trades, (Trades{print(1, 1500, 10, Side::Buy)}));
}

// --- stop / stop limit (încă neimplementate: fără raport) ---

TEST_F(EngineTest, ValidStopProducesNoOutputYet) {
    send(stop(1, Side::Buy, 1500, 10));
    EXPECT_TRUE(out.reports.empty());
    EXPECT_TRUE(out.trades.empty());
}

TEST_F(EngineTest, ValidStopLimitProducesNoOutputYet) {
    send(stop_limit(1, Side::Sell, 1500, 1505, 10));
    EXPECT_TRUE(out.reports.empty());
    EXPECT_TRUE(out.trades.empty());
}

// --- limit DAY ---

TEST_F(EngineTest, NonCrossingLimitRests) {
    send(limit(1, Side::Buy, 1500, 10));
    EXPECT_EQ(out.reports, (Reports{new_(CLIENT, 1, Side::Buy, 10)}));
    EXPECT_TRUE(out.trades.empty());

    send(limit(2, Side::Sell, 1500, 10, TimeInForce::DAY, 2));
    EXPECT_EQ(out.reports, (Reports{new_(2, 2, Side::Sell, 10),
                                    trade(2, 2, Side::Sell, 10, 0, 1500, 10),
                                    trade(CLIENT, 1, Side::Buy, 10, 0, 1500, 10)}));
}

TEST_F(EngineTest, BuySweepsLevelsAndRestsRemainder) {
    send(limit(1, Side::Sell, 1000, 30, TimeInForce::DAY, 2));
    send(limit(2, Side::Sell, 1005, 50, TimeInForce::DAY, 3));

    send(limit(9, Side::Buy, 1005, 100));
    EXPECT_EQ(out.reports, (Reports{new_(CLIENT, 9, Side::Buy, 100),
                                    trade(CLIENT, 9, Side::Buy, 30, 70, 1000, 30),
                                    trade(2, 1, Side::Sell, 30, 0, 1000, 30),
                                    trade(CLIENT, 9, Side::Buy, 80, 20, 1005, 50),
                                    trade(3, 2, Side::Sell, 50, 0, 1005, 50)}));
    EXPECT_EQ(out.trades, (Trades{print(1, 1000, 30, Side::Buy), print(2, 1005, 50, Side::Buy)}));

    send(limit(3, Side::Sell, 1005, 10, TimeInForce::DAY, 4));
    EXPECT_EQ(out.reports, (Reports{new_(4, 3, Side::Sell, 10),
                                    trade(4, 3, Side::Sell, 10, 0, 1005, 10),
                                    trade(CLIENT, 9, Side::Buy, 90, 10, 1005, 10)}));
}

TEST_F(EngineTest, SellSweepsLevelsAndRestsRemainder) {
    send(limit(1, Side::Buy, 1010, 20, TimeInForce::DAY, 2));
    send(limit(2, Side::Buy, 1005, 20, TimeInForce::DAY, 3));

    send(limit(9, Side::Sell, 1005, 50));
    EXPECT_EQ(out.reports, (Reports{new_(CLIENT, 9, Side::Sell, 50),
                                    trade(CLIENT, 9, Side::Sell, 20, 30, 1010, 20),
                                    trade(2, 1, Side::Buy, 20, 0, 1010, 20),
                                    trade(CLIENT, 9, Side::Sell, 40, 10, 1005, 20),
                                    trade(3, 2, Side::Buy, 20, 0, 1005, 20)}));

    send(limit(3, Side::Buy, 1005, 10, TimeInForce::DAY, 4));
    EXPECT_EQ(out.reports.back(), trade(CLIENT, 9, Side::Sell, 50, 0, 1005, 10));
}

TEST_F(EngineTest, ExactFillHasNoTrailingReport) {
    send(limit(1, Side::Sell, 1500, 10, TimeInForce::DAY, 2));
    send(limit(2, Side::Buy, 1500, 10));
    ASSERT_EQ(out.reports.size(), 3u);
    EXPECT_EQ(out.reports.back(), trade(2, 1, Side::Sell, 10, 0, 1500, 10));

    send(limit(3, Side::Sell, 1500, 5));
    EXPECT_EQ(out.reports, (Reports{new_(CLIENT, 3, Side::Sell, 5)}));
}

TEST_F(EngineTest, LimitDoesNotTradeThroughPrice) {
    send(limit(1, Side::Sell, 1505, 10, TimeInForce::DAY, 2));
    send(limit(2, Side::Buy, 1500, 10));
    EXPECT_EQ(out.reports, (Reports{new_(CLIENT, 2, Side::Buy, 10)}));
    EXPECT_TRUE(out.trades.empty());
}

TEST_F(EngineTest, PriceTimePriorityWithinLevel) {
    send(limit(1, Side::Sell, 1500, 10, TimeInForce::DAY, 2));
    send(limit(2, Side::Sell, 1500, 10, TimeInForce::DAY, 3));

    send(limit(3, Side::Buy, 1500, 15));
    EXPECT_EQ(out.reports, (Reports{new_(CLIENT, 3, Side::Buy, 15),
                                    trade(CLIENT, 3, Side::Buy, 10, 5, 1500, 10),
                                    trade(2, 1, Side::Sell, 10, 0, 1500, 10),
                                    trade(CLIENT, 3, Side::Buy, 15, 0, 1500, 5),
                                    trade(3, 2, Side::Sell, 5, 5, 1500, 5)}));
}

TEST_F(EngineTest, TradeSeqIncreasesAcrossOrders) {
    send(limit(1, Side::Sell, 1500, 30, TimeInForce::DAY, 2));
    send(limit(2, Side::Buy, 1500, 10));
    send(limit(3, Side::Buy, 1500, 10));
    EXPECT_EQ(out.trades, (Trades{print(2, 1500, 10, Side::Buy)}));
}

TEST_F(EngineTest, TradeTimeIsAggressorReceivedAt) {
    send(limit(1, Side::Sell, 1500, 10, TimeInForce::DAY, 2));
    auto o = limit(2, Side::Buy, 1500, 10);
    o.received_at = 12345;
    send(o);
    EXPECT_EQ(out.trades, (Trades{print(1, 1500, 10, Side::Buy, 12345)}));
}

TEST_F(EngineTest, PoolFullCancelsRemainder) {
    Engine small;
    small.add_instrument(instrument(), 1);
    EngineOutput o;
    small.on_message(limit(1, Side::Buy, 1500, 10), o);
    o.clear();

    small.on_message(limit(2, Side::Buy, 1495, 10), o);
    EXPECT_EQ(o.reports, (Reports{new_(CLIENT, 2, Side::Buy, 10), canceled(CLIENT, 2, Side::Buy, 0)}));
}

TEST_F(EngineTest, OnMessageDoesNotClearOutput) {
    engine.on_message(limit(1, Side::Buy, 1500, 10), out);
    engine.on_message(limit(2, Side::Buy, 1495, 10), out);
    EXPECT_EQ(out.reports, (Reports{new_(CLIENT, 1, Side::Buy, 10), new_(CLIENT, 2, Side::Buy, 10)}));
}

// --- FAK ---

TEST_F(EngineTest, FakPartialFillCancelsRemainder) {
    send(limit(1, Side::Sell, 1500, 30, TimeInForce::DAY, 2));
    send(limit(2, Side::Buy, 1500, 50, TimeInForce::FAK));
    EXPECT_EQ(out.reports, (Reports{new_(CLIENT, 2, Side::Buy, 50),
                                    trade(CLIENT, 2, Side::Buy, 30, 20, 1500, 30),
                                    trade(2, 1, Side::Sell, 30, 0, 1500, 30),
                                    canceled(CLIENT, 2, Side::Buy, 30)}));
}

TEST_F(EngineTest, FakWithoutLiquidityIsCanceled) {
    send(limit(1, Side::Sell, 1500, 10, TimeInForce::FAK));
    EXPECT_EQ(out.reports, (Reports{new_(CLIENT, 1, Side::Sell, 10), canceled(CLIENT, 1, Side::Sell, 0)}));
}

TEST_F(EngineTest, FakDoesNotRest) {
    send(limit(1, Side::Buy, 1500, 10, TimeInForce::FAK));
    send(limit(2, Side::Sell, 1500, 10));
    EXPECT_EQ(out.reports, (Reports{new_(CLIENT, 2, Side::Sell, 10)}));
}

TEST_F(EngineTest, FakFullFillHasNoCancel) {
    send(limit(1, Side::Sell, 1500, 10, TimeInForce::DAY, 2));
    send(limit(2, Side::Buy, 1500, 10, TimeInForce::FAK));
    EXPECT_EQ(out.reports.size(), 3u);
    EXPECT_NE(out.reports.back().type, ExecType::Canceled);
}

// --- market ---

TEST_F(EngineTest, MarketBuySweepsAllLevels) {
    send(limit(1, Side::Sell, 1000, 10, TimeInForce::DAY, 2));
    send(limit(2, Side::Sell, 2000, 10, TimeInForce::DAY, 3));
    send(market(3, Side::Buy, 20));
    EXPECT_EQ(out.reports, (Reports{new_(CLIENT, 3, Side::Buy, 20),
                                    trade(CLIENT, 3, Side::Buy, 10, 10, 1000, 10),
                                    trade(2, 1, Side::Sell, 10, 0, 1000, 10),
                                    trade(CLIENT, 3, Side::Buy, 20, 0, 2000, 10),
                                    trade(3, 2, Side::Sell, 10, 0, 2000, 10)}));
}

TEST_F(EngineTest, MarketSellLargerThanBookCancelsRemainder) {
    send(limit(1, Side::Buy, 1500, 10, TimeInForce::DAY, 2));
    send(market(2, Side::Sell, 25));
    EXPECT_EQ(out.reports, (Reports{new_(CLIENT, 2, Side::Sell, 25),
                                    trade(CLIENT, 2, Side::Sell, 10, 15, 1500, 10),
                                    trade(2, 1, Side::Buy, 10, 0, 1500, 10),
                                    canceled(CLIENT, 2, Side::Sell, 10)}));
}

TEST_F(EngineTest, MarketOnEmptyBookIsCanceled) {
    send(market(1, Side::Sell, 5));
    EXPECT_EQ(out.reports, (Reports{new_(CLIENT, 1, Side::Sell, 5), canceled(CLIENT, 1, Side::Sell, 0)}));
    EXPECT_TRUE(out.trades.empty());
}

// --- FOK ---

TEST_F(EngineTest, LimitFokNotFillableIsRejectedAndBookUntouched) {
    send(limit(1, Side::Sell, 1000, 30, TimeInForce::DAY, 2));
    send(limit(2, Side::Sell, 1005, 50, TimeInForce::DAY, 3));

    send(limit(3, Side::Buy, 1005, 100, TimeInForce::FOK));
    EXPECT_EQ(out.reports, (Reports{rejected(CLIENT, 3, Side::Buy, RejectReason::FokNotFilled)}));
    EXPECT_TRUE(out.trades.empty());

    send(limit(4, Side::Buy, 1005, 80, TimeInForce::FOK));
    EXPECT_EQ(out.trades, (Trades{print(1, 1000, 30, Side::Buy), print(2, 1005, 50, Side::Buy)}));
}

TEST_F(EngineTest, LimitFokIgnoresLiquidityBeyondLimit) {
    send(limit(1, Side::Sell, 1000, 30, TimeInForce::DAY, 2));
    send(limit(2, Side::Sell, 1005, 100, TimeInForce::DAY, 3));
    send(limit(3, Side::Buy, 1000, 100, TimeInForce::FOK));
    EXPECT_EQ(out.reports, (Reports{rejected(CLIENT, 3, Side::Buy, RejectReason::FokNotFilled)}));
}

TEST_F(EngineTest, LimitFokFillableExecutesFully) {
    send(limit(1, Side::Buy, 1500, 30, TimeInForce::DAY, 2));
    send(limit(2, Side::Sell, 1500, 30, TimeInForce::FOK));
    EXPECT_EQ(out.reports, (Reports{new_(CLIENT, 2, Side::Sell, 30),
                                    trade(CLIENT, 2, Side::Sell, 30, 0, 1500, 30),
                                    trade(2, 1, Side::Buy, 30, 0, 1500, 30)}));
}

TEST_F(EngineTest, MarketFokNotFillableIsRejected) {
    send(limit(1, Side::Buy, 1500, 10, TimeInForce::DAY, 2));
    send(market(2, Side::Sell, 11, TimeInForce::FOK));
    EXPECT_EQ(out.reports, (Reports{rejected(CLIENT, 2, Side::Sell, RejectReason::FokNotFilled)}));
}

TEST_F(EngineTest, MarketFokFillableExecutesAcrossBand) {
    send(limit(1, Side::Sell, 1000, 10, TimeInForce::DAY, 2));
    send(limit(2, Side::Sell, 2000, 10, TimeInForce::DAY, 3));
    send(market(3, Side::Buy, 20, TimeInForce::FOK));
    EXPECT_EQ(out.reports.size(), 5u);
    EXPECT_EQ(out.reports.back(), trade(3, 2, Side::Sell, 10, 0, 2000, 10));
}

// --- post-only ---

TEST_F(EngineTest, PostOnlyBuyCrossingIsRejected) {
    send(limit(1, Side::Sell, 1500, 10, TimeInForce::DAY, 2));
    send(limit(2, Side::Buy, 1505, 10, TimeInForce::DAY, CLIENT, true));
    EXPECT_EQ(out.reports, (Reports{rejected(CLIENT, 2, Side::Buy, RejectReason::PostOnlyWouldCross)}));
}

TEST_F(EngineTest, PostOnlyBuyAtBestAskIsRejected) {
    send(limit(1, Side::Sell, 1500, 10, TimeInForce::DAY, 2));
    send(limit(2, Side::Buy, 1500, 10, TimeInForce::DAY, CLIENT, true));
    EXPECT_EQ(out.reports, (Reports{rejected(CLIENT, 2, Side::Buy, RejectReason::PostOnlyWouldCross)}));
}

TEST_F(EngineTest, PostOnlyBuyBelowAskRests) {
    send(limit(1, Side::Sell, 1505, 10, TimeInForce::DAY, 2));
    send(limit(2, Side::Buy, 1500, 10, TimeInForce::DAY, CLIENT, true));
    EXPECT_EQ(out.reports, (Reports{new_(CLIENT, 2, Side::Buy, 10)}));

    send(market(3, Side::Sell, 10, TimeInForce::FAK, 3));
    EXPECT_EQ(out.trades, (Trades{print(1, 1500, 10, Side::Sell)}));
}

TEST_F(EngineTest, PostOnlySellCrossingIsRejected) {
    send(limit(1, Side::Buy, 1500, 10, TimeInForce::DAY, 2));
    send(limit(2, Side::Sell, 1500, 10, TimeInForce::DAY, CLIENT, true));
    EXPECT_EQ(out.reports, (Reports{rejected(CLIENT, 2, Side::Sell, RejectReason::PostOnlyWouldCross)}));
}

TEST_F(EngineTest, PostOnlySellAboveBidRests) {
    send(limit(1, Side::Buy, 1500, 10, TimeInForce::DAY, 2));
    send(limit(2, Side::Sell, 1505, 10, TimeInForce::DAY, CLIENT, true));
    EXPECT_EQ(out.reports, (Reports{new_(CLIENT, 2, Side::Sell, 10)}));
}

TEST_F(EngineTest, PostOnlyOnEmptyBookRests) {
    send(limit(1, Side::Buy, 1500, 10, TimeInForce::DAY, CLIENT, true));
    EXPECT_EQ(out.reports, (Reports{new_(CLIENT, 1, Side::Buy, 10)}));
    send(limit(2, Side::Sell, 1600, 10, TimeInForce::DAY, CLIENT, true));
    EXPECT_EQ(out.reports, (Reports{new_(CLIENT, 2, Side::Sell, 10)}));
}

// --- cancel ---

TEST_F(EngineTest, CancelRestingOrder) {
    send(limit(1, Side::Buy, 1500, 10));
    send(cancel(1));
    EXPECT_EQ(out.reports, (Reports{canceled(CLIENT, 1, Side::Buy, 0)}));

    send(limit(2, Side::Sell, 1500, 10));
    EXPECT_EQ(out.reports, (Reports{new_(CLIENT, 2, Side::Sell, 10)}));
}

TEST_F(EngineTest, CancelUnknownOrderIsRejected) {
    send(cancel(42));
    EXPECT_EQ(out.reports, (Reports{rejected(CLIENT, 42, Side::Buy, RejectReason::UnknownOrder)}));
}

TEST_F(EngineTest, CancelTwiceIsRejected) {
    send(limit(1, Side::Buy, 1500, 10));
    send(cancel(1));
    send(cancel(1));
    EXPECT_EQ(out.reports, (Reports{rejected(CLIENT, 1, Side::Buy, RejectReason::UnknownOrder)}));
}

TEST_F(EngineTest, CancelFromOtherClientIsRejected) {
    send(limit(1, Side::Buy, 1500, 10));
    send(cancel(1, Side::Buy, 2));
    EXPECT_EQ(out.reports, (Reports{rejected(2, 1, Side::Buy, RejectReason::UnknownOrder)}));

    send(cancel(1));
    EXPECT_EQ(out.reports, (Reports{canceled(CLIENT, 1, Side::Buy, 0)}));
}

TEST_F(EngineTest, CancelAfterPartialFillReportsFilled) {
    send(limit(1, Side::Buy, 1500, 100));
    send(limit(2, Side::Sell, 1500, 40, TimeInForce::DAY, 2));
    send(cancel(1));
    EXPECT_EQ(out.reports, (Reports{canceled(CLIENT, 1, Side::Buy, 40)}));
}

TEST_F(EngineTest, CancelAfterFullFillIsRejected) {
    send(limit(1, Side::Buy, 1500, 10));
    send(limit(2, Side::Sell, 1500, 10, TimeInForce::DAY, 2));
    send(cancel(1));
    EXPECT_EQ(out.reports, (Reports{rejected(CLIENT, 1, Side::Buy, RejectReason::UnknownOrder)}));
}

TEST_F(EngineTest, CancelReportUsesOrderSide) {
    send(limit(1, Side::Sell, 1500, 10));
    send(cancel(1, Side::Buy));
    EXPECT_EQ(out.reports, (Reports{canceled(CLIENT, 1, Side::Sell, 0)}));
}

// --- replace: respingeri ---

TEST_F(EngineTest, ReplaceUnknownOrderIsRejected) {
    send(replace(42, 1500, 10));
    EXPECT_EQ(out.reports, (Reports{rejected(CLIENT, 42, Side::Buy, RejectReason::UnknownOrder)}));
}

TEST_F(EngineTest, ReplaceFromOtherClientIsRejected) {
    send(limit(1, Side::Buy, 1500, 10));
    send(replace(1, 1500, 5, Side::Buy, 2));
    EXPECT_EQ(out.reports, (Reports{rejected(2, 1, Side::Buy, RejectReason::UnknownOrder)}));
}

TEST_F(EngineTest, ReplaceOffTickIsRejected) {
    send(limit(1, Side::Buy, 1500, 10));
    send(replace(1, 1502, 10));
    EXPECT_EQ(out.reports, (Reports{rejected(CLIENT, 1, Side::Buy, RejectReason::InvalidPrice)}));
}

TEST_F(EngineTest, ReplaceOutsideBandIsRejected) {
    send(limit(1, Side::Buy, 1500, 10));
    send(replace(1, MAX + TICK, 10));
    EXPECT_EQ(out.reports, (Reports{rejected(CLIENT, 1, Side::Buy, RejectReason::OutsideBand)}));
    send(replace(1, MIN - TICK, 10));
    EXPECT_EQ(out.reports, (Reports{rejected(CLIENT, 1, Side::Buy, RejectReason::OutsideBand)}));
}

TEST_F(EngineTest, ReplaceQtyNotAboveFilledIsRejected) {
    send(limit(1, Side::Buy, 1500, 100));
    send(limit(2, Side::Sell, 1500, 40, TimeInForce::DAY, 2));

    send(replace(1, 1500, 40));
    EXPECT_EQ(out.reports, (Reports{rejected(CLIENT, 1, Side::Buy, RejectReason::InvalidQty)}));
    send(replace(1, 1500, 30));
    EXPECT_EQ(out.reports, (Reports{rejected(CLIENT, 1, Side::Buy, RejectReason::InvalidQty)}));
}

TEST_F(EngineTest, RejectedReplaceKeepsOrder) {
    send(limit(1, Side::Buy, 1500, 10));
    send(replace(1, 1502, 10));
    send(limit(2, Side::Sell, 1500, 10, TimeInForce::DAY, 2));
    EXPECT_EQ(out.reports.back(), trade(CLIENT, 1, Side::Buy, 10, 0, 1500, 10));
}

// --- replace: prioritate ---

TEST_F(EngineTest, ReplaceReduceKeepsPriority) {
    send(limit(1, Side::Buy, 1500, 100));
    send(limit(2, Side::Buy, 1500, 100));

    send(replace(1, 1500, 50));
    EXPECT_EQ(out.reports, (Reports{replaced(CLIENT, 1, Side::Buy, 0, 50)}));
    EXPECT_TRUE(out.trades.empty());

    send(market(3, Side::Sell, 60, TimeInForce::FAK, 2));
    EXPECT_EQ(out.reports, (Reports{new_(2, 3, Side::Sell, 60),
                                    trade(2, 3, Side::Sell, 50, 10, 1500, 50),
                                    trade(CLIENT, 1, Side::Buy, 50, 0, 1500, 50),
                                    trade(2, 3, Side::Sell, 60, 0, 1500, 10),
                                    trade(CLIENT, 2, Side::Buy, 10, 90, 1500, 10)}));
}

TEST_F(EngineTest, ReplaceSamePriceSameQtyKeepsPriority) {
    send(limit(1, Side::Buy, 1500, 100));
    send(limit(2, Side::Buy, 1500, 100));

    send(replace(1, 1500, 100));
    EXPECT_EQ(out.reports, (Reports{replaced(CLIENT, 1, Side::Buy, 0, 100)}));

    send(market(3, Side::Sell, 10, TimeInForce::FAK, 2));
    EXPECT_EQ(out.reports.back(), trade(CLIENT, 1, Side::Buy, 10, 90, 1500, 10));
}

TEST_F(EngineTest, ReplaceIncreaseLosesPriority) {
    send(limit(1, Side::Buy, 1500, 100));
    send(limit(2, Side::Buy, 1500, 100));

    send(replace(1, 1500, 150));
    EXPECT_EQ(out.reports, (Reports{replaced(CLIENT, 1, Side::Buy, 0, 150)}));

    send(market(3, Side::Sell, 10, TimeInForce::FAK, 2));
    EXPECT_EQ(out.reports.back(), trade(CLIENT, 2, Side::Buy, 10, 90, 1500, 10));
}

TEST_F(EngineTest, ReplacePriceChangeMovesLevel) {
    send(limit(1, Side::Buy, 1500, 50));
    send(limit(2, Side::Buy, 1500, 100));

    send(replace(1, 1495, 50));
    EXPECT_EQ(out.reports, (Reports{replaced(CLIENT, 1, Side::Buy, 0, 50)}));

    send(market(3, Side::Sell, 150, TimeInForce::FAK, 2));
    EXPECT_EQ(out.trades, (Trades{print(1, 1500, 100, Side::Sell), print(2, 1495, 50, Side::Sell)}));
}

// --- replace: execuție ---

TEST_F(EngineTest, ReplaceCrossingExecutesAndRests) {
    send(limit(1, Side::Sell, 1510, 30, TimeInForce::DAY, 2));
    send(limit(2, Side::Buy, 1500, 50));

    send(replace(2, 1510, 50));
    EXPECT_EQ(out.reports, (Reports{replaced(CLIENT, 2, Side::Buy, 0, 50),
                                    trade(CLIENT, 2, Side::Buy, 30, 20, 1510, 30),
                                    trade(2, 1, Side::Sell, 30, 0, 1510, 30)}));
    EXPECT_EQ(out.trades, (Trades{print(1, 1510, 30, Side::Buy)}));

    send(limit(3, Side::Sell, 1510, 20, TimeInForce::DAY, 3));
    EXPECT_EQ(out.reports.back(), trade(CLIENT, 2, Side::Buy, 50, 0, 1510, 20));
}

TEST_F(EngineTest, ReplaceCrossingFullyFilledDoesNotRest) {
    send(limit(1, Side::Sell, 1510, 100, TimeInForce::DAY, 2));
    send(limit(2, Side::Buy, 1500, 50));

    send(replace(2, 1510, 50));
    EXPECT_EQ(out.reports, (Reports{replaced(CLIENT, 2, Side::Buy, 0, 50),
                                    trade(CLIENT, 2, Side::Buy, 50, 0, 1510, 50),
                                    trade(2, 1, Side::Sell, 50, 50, 1510, 50)}));

    send(cancel(2));
    EXPECT_EQ(out.reports, (Reports{rejected(CLIENT, 2, Side::Buy, RejectReason::UnknownOrder)}));
}

TEST_F(EngineTest, ReplaceAfterPartialFillKeepsCumQty) {
    send(limit(1, Side::Buy, 1500, 100));
    send(limit(2, Side::Sell, 1500, 40, TimeInForce::DAY, 2));

    send(replace(1, 1500, 70));
    EXPECT_EQ(out.reports, (Reports{replaced(CLIENT, 1, Side::Buy, 40, 30)}));

    send(limit(3, Side::Sell, 1510, 50, TimeInForce::DAY, 3));
    send(replace(1, 1510, 100));
    EXPECT_EQ(out.reports, (Reports{replaced(CLIENT, 1, Side::Buy, 40, 60),
                                    trade(CLIENT, 1, Side::Buy, 90, 10, 1510, 50),
                                    trade(3, 3, Side::Sell, 50, 0, 1510, 50)}));

    send(limit(4, Side::Sell, 1510, 10, TimeInForce::DAY, 4));
    EXPECT_EQ(out.reports.back(), trade(CLIENT, 1, Side::Buy, 100, 0, 1510, 10));
}

TEST_F(EngineTest, ReplaceTradeUsesReplaceTime) {
    send(limit(1, Side::Sell, 1510, 10, TimeInForce::DAY, 2));
    send(limit(2, Side::Buy, 1500, 10));
    auto r = replace(2, 1510, 10);
    r.received_at = 999;
    send(r);
    EXPECT_EQ(out.trades, (Trades{print(1, 1510, 10, Side::Buy, 999)}));
}

TEST_F(EngineTest, ReplaceSellSide) {
    send(limit(1, Side::Sell, 1500, 100));
    send(replace(1, 1500, 60, Side::Sell));
    EXPECT_EQ(out.reports, (Reports{replaced(CLIENT, 1, Side::Sell, 0, 60)}));

    send(market(2, Side::Buy, 100, TimeInForce::FAK, 2));
    EXPECT_EQ(out.reports.back(), canceled(2, 2, Side::Buy, 60));
}
