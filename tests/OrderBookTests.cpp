#include <gtest/gtest.h>
#include "exchange/engine/book.hpp"
#include <algorithm>
#include <utility>
#include <vector>

namespace {

using FIX::Side;
using Ids = std::vector<FIX::OrderId>;

constexpr FIX::SymbolId SYMBOL = 0;
constexpr FIX::Price MIN  = 1000;
constexpr FIX::Price MAX  = 2000;
constexpr FIX::Price TICK = 5;

OrderBook make_book(std::size_t capacity = 64) { return OrderBook(SYMBOL, MIN, MAX, TICK, capacity); }

// id-ul ordinului din fața părții date, sau -1 dacă partea e goală
FIX::OrderId front_id(const OrderBook& book, Side side) {
    const auto idx = book.front(side);
    return idx ? book.get_order(*idx).id : -1;
}

}  // namespace

// --- carte goală ---

TEST(OrderBook, EmptyBookHasNoBestPrices) {
    auto book = make_book();
    EXPECT_FALSE(book.best_bid().has_value());
    EXPECT_FALSE(book.best_ask().has_value());
}

TEST(OrderBook, EmptyLevelsHaveZeroQtyAndNoOrders) {
    auto book = make_book();
    EXPECT_EQ(book.level_qty(Side::Buy, 1500), 0);
    EXPECT_EQ(book.level_qty(Side::Sell, 1500), 0);
    EXPECT_TRUE(book.orders_at(Side::Buy, 1500).empty());
    EXPECT_TRUE(book.orders_at(Side::Sell, 1500).empty());
}

TEST(OrderBook, EmptyBookHasNoFront) {
    auto book = make_book();
    EXPECT_FALSE(book.front(Side::Buy).has_value());
    EXPECT_FALSE(book.front(Side::Sell).has_value());
}

TEST(OrderBook, EmptyBookHasNothingAvailable) {
    auto book = make_book();
    EXPECT_EQ(book.available(Side::Buy, MIN), 0);
    EXPECT_EQ(book.available(Side::Buy, MAX), 0);
    EXPECT_EQ(book.available(Side::Sell, MIN), 0);
    EXPECT_EQ(book.available(Side::Sell, MAX), 0);
}

TEST(OrderBook, QueriesOutsideBandReturnEmpty) {
    auto book = make_book();
    book.rest(1, 10, Side::Buy, 1500, 10);
    EXPECT_EQ(book.level_qty(Side::Buy, MIN - TICK), 0);
    EXPECT_EQ(book.level_qty(Side::Buy, MAX + TICK), 0);
    EXPECT_EQ(book.level_qty(Side::Buy, 1502), 0);
    EXPECT_TRUE(book.orders_at(Side::Buy, MIN - TICK).empty());
    EXPECT_TRUE(book.orders_at(Side::Buy, 1502).empty());
}

// --- rest ---

TEST(OrderBook, RestSingleBid) {
    auto book = make_book();
    ASSERT_TRUE(book.rest(1, 10, Side::Buy, 1500, 100));
    EXPECT_EQ(book.best_bid(), 1500);
    EXPECT_FALSE(book.best_ask().has_value());
    EXPECT_EQ(book.level_qty(Side::Buy, 1500), 100);
    EXPECT_EQ(book.orders_at(Side::Buy, 1500), Ids{1});
}

TEST(OrderBook, RestSingleAsk) {
    auto book = make_book();
    ASSERT_TRUE(book.rest(1, 10, Side::Sell, 1500, 100));
    EXPECT_EQ(book.best_ask(), 1500);
    EXPECT_FALSE(book.best_bid().has_value());
    EXPECT_EQ(book.level_qty(Side::Sell, 1500), 100);
    EXPECT_EQ(book.orders_at(Side::Sell, 1500), Ids{1});
}

TEST(OrderBook, RestStoresOrderFields) {
    auto book = make_book();
    ASSERT_TRUE(book.rest(7, 42, Side::Sell, 1555, 300));
    const auto idx = book.front(Side::Sell);
    ASSERT_TRUE(idx.has_value());
    const Order& o = book.get_order(*idx);
    EXPECT_EQ(o.id, 7);
    EXPECT_EQ(o.client, 42);
    EXPECT_EQ(o.side, Side::Sell);
    EXPECT_EQ(o.price, 1555);
    EXPECT_EQ(o.qty, 300);
    EXPECT_EQ(o.filled, 0);
}

TEST(OrderBook, SidesAreIndependent) {
    auto book = make_book();
    ASSERT_TRUE(book.rest(1, 10, Side::Buy, 1500, 100));
    EXPECT_EQ(book.level_qty(Side::Sell, 1500), 0);
    EXPECT_TRUE(book.orders_at(Side::Sell, 1500).empty());
}

TEST(OrderBook, BestBidIsHighest) {
    auto book = make_book();
    book.rest(1, 10, Side::Buy, 1400, 10);
    book.rest(2, 10, Side::Buy, 1600, 10);
    book.rest(3, 10, Side::Buy, 1500, 10);
    EXPECT_EQ(book.best_bid(), 1600);
}

TEST(OrderBook, BestAskIsLowest) {
    auto book = make_book();
    book.rest(1, 10, Side::Sell, 1600, 10);
    book.rest(2, 10, Side::Sell, 1400, 10);
    book.rest(3, 10, Side::Sell, 1500, 10);
    EXPECT_EQ(book.best_ask(), 1400);
}

TEST(OrderBook, PriceTimePriorityWithinLevel) {
    auto book = make_book();
    book.rest(1, 10, Side::Buy, 1500, 10);
    book.rest(2, 11, Side::Buy, 1500, 20);
    book.rest(3, 12, Side::Buy, 1500, 30);
    EXPECT_EQ(book.orders_at(Side::Buy, 1500), (Ids{1, 2, 3}));
    EXPECT_EQ(book.level_qty(Side::Buy, 1500), 60);
}

TEST(OrderBook, PriceTimePriorityWithinAskLevel) {
    auto book = make_book();
    book.rest(1, 10, Side::Sell, 1500, 10);
    book.rest(2, 11, Side::Sell, 1500, 20);
    EXPECT_EQ(book.orders_at(Side::Sell, 1500), (Ids{1, 2}));
    EXPECT_EQ(book.level_qty(Side::Sell, 1500), 30);
}

TEST(OrderBook, BoundaryPrices) {
    auto book = make_book();
    ASSERT_TRUE(book.rest(1, 10, Side::Buy, MIN, 10));
    ASSERT_TRUE(book.rest(2, 10, Side::Sell, MAX, 10));
    EXPECT_EQ(book.best_bid(), MIN);
    EXPECT_EQ(book.best_ask(), MAX);
    EXPECT_EQ(book.level_qty(Side::Buy, MIN), 10);
    EXPECT_EQ(book.level_qty(Side::Sell, MAX), 10);
}

TEST(OrderBook, TickConversionKeepsLevelsSeparate) {
    auto book = make_book();
    book.rest(1, 10, Side::Buy, 1500, 10);
    book.rest(2, 10, Side::Buy, 1505, 20);
    EXPECT_EQ(book.level_qty(Side::Buy, 1500), 10);
    EXPECT_EQ(book.level_qty(Side::Buy, 1505), 20);
    EXPECT_EQ(book.best_bid(), 1505);
}

TEST(OrderBook, RestRejectsDuplicateId) {
    auto book = make_book();
    ASSERT_TRUE(book.rest(1, 10, Side::Buy, 1500, 10));
    EXPECT_FALSE(book.rest(1, 10, Side::Sell, 1600, 20));
    EXPECT_FALSE(book.rest(1, 10, Side::Buy, 1500, 20));
    EXPECT_EQ(book.orders_at(Side::Buy, 1500), Ids{1});
    EXPECT_EQ(book.level_qty(Side::Buy, 1500), 10);
    EXPECT_FALSE(book.best_ask().has_value());
}

TEST(OrderBook, RestRejectsNonPositiveQty) {
    auto book = make_book();
    EXPECT_FALSE(book.rest(1, 10, Side::Buy, 1500, 0));
    EXPECT_FALSE(book.rest(2, 10, Side::Sell, 1500, -5));
    EXPECT_FALSE(book.best_bid().has_value());
    EXPECT_FALSE(book.best_ask().has_value());
}

TEST(OrderBook, RestRejectsPriceOutsideBand) {
    auto book = make_book();
    EXPECT_FALSE(book.rest(1, 10, Side::Buy, MIN - TICK, 10));
    EXPECT_FALSE(book.rest(2, 10, Side::Sell, MAX + TICK, 10));
    EXPECT_FALSE(book.best_bid().has_value());
    EXPECT_FALSE(book.best_ask().has_value());
}

TEST(OrderBook, RestRejectsPriceOffTick) {
    auto book = make_book();
    EXPECT_FALSE(book.rest(1, 10, Side::Buy, 1502, 10));
    EXPECT_FALSE(book.rest(2, 10, Side::Sell, 1499, 10));
    EXPECT_FALSE(book.best_bid().has_value());
    EXPECT_FALSE(book.best_ask().has_value());
}

TEST(OrderBook, RejectedRestDoesNotReserveId) {
    auto book = make_book();
    EXPECT_FALSE(book.rest(1, 10, Side::Buy, 1502, 10));
    EXPECT_TRUE(book.rest(1, 10, Side::Buy, 1500, 10));
}

TEST(OrderBook, RestFailsWhenPoolFull) {
    auto book = make_book(2);
    ASSERT_TRUE(book.rest(1, 10, Side::Buy, 1500, 10));
    ASSERT_TRUE(book.rest(2, 10, Side::Buy, 1500, 10));
    EXPECT_FALSE(book.rest(3, 10, Side::Buy, 1500, 10));
    EXPECT_EQ(book.orders_at(Side::Buy, 1500), (Ids{1, 2}));
    EXPECT_EQ(book.level_qty(Side::Buy, 1500), 20);
}

TEST(OrderBook, PoolFullDoesNotReserveId) {
    auto book = make_book(1);
    ASSERT_TRUE(book.rest(1, 10, Side::Buy, 1500, 10));
    EXPECT_FALSE(book.rest(2, 10, Side::Buy, 1500, 10));
    ASSERT_TRUE(book.remove(1));
    EXPECT_TRUE(book.rest(2, 10, Side::Buy, 1500, 10));
}

// --- remove ---

TEST(OrderBook, RemoveUnknownIdFails) {
    auto book = make_book();
    EXPECT_FALSE(book.remove(42));
}

TEST(OrderBook, RemoveTwiceFails) {
    auto book = make_book();
    book.rest(1, 10, Side::Buy, 1500, 10);
    EXPECT_TRUE(book.remove(1));
    EXPECT_FALSE(book.remove(1));
}

TEST(OrderBook, RemoveOnlyOrderEmptiesBook) {
    auto book = make_book();
    book.rest(1, 10, Side::Buy, 1500, 100);
    book.rest(2, 10, Side::Sell, 1600, 100);
    ASSERT_TRUE(book.remove(1));
    ASSERT_TRUE(book.remove(2));
    EXPECT_FALSE(book.best_bid().has_value());
    EXPECT_FALSE(book.best_ask().has_value());
    EXPECT_FALSE(book.front(Side::Buy).has_value());
    EXPECT_FALSE(book.front(Side::Sell).has_value());
    EXPECT_EQ(book.level_qty(Side::Buy, 1500), 0);
    EXPECT_EQ(book.level_qty(Side::Sell, 1600), 0);
    EXPECT_TRUE(book.orders_at(Side::Buy, 1500).empty());
}

TEST(OrderBook, RemoveHeadOfLevel) {
    auto book = make_book();
    book.rest(1, 10, Side::Buy, 1500, 10);
    book.rest(2, 10, Side::Buy, 1500, 20);
    book.rest(3, 10, Side::Buy, 1500, 30);
    ASSERT_TRUE(book.remove(1));
    EXPECT_EQ(book.orders_at(Side::Buy, 1500), (Ids{2, 3}));
    EXPECT_EQ(book.level_qty(Side::Buy, 1500), 50);
    EXPECT_EQ(book.best_bid(), 1500);
}

TEST(OrderBook, RemoveMiddleOfLevel) {
    auto book = make_book();
    book.rest(1, 10, Side::Buy, 1500, 10);
    book.rest(2, 10, Side::Buy, 1500, 20);
    book.rest(3, 10, Side::Buy, 1500, 30);
    ASSERT_TRUE(book.remove(2));
    EXPECT_EQ(book.orders_at(Side::Buy, 1500), (Ids{1, 3}));
    EXPECT_EQ(book.level_qty(Side::Buy, 1500), 40);
}

TEST(OrderBook, RemoveTailOfLevel) {
    auto book = make_book();
    book.rest(1, 10, Side::Sell, 1500, 10);
    book.rest(2, 10, Side::Sell, 1500, 20);
    book.rest(3, 10, Side::Sell, 1500, 30);
    ASSERT_TRUE(book.remove(3));
    EXPECT_EQ(book.orders_at(Side::Sell, 1500), (Ids{1, 2}));
    EXPECT_EQ(book.level_qty(Side::Sell, 1500), 30);
}

TEST(OrderBook, RemoveAskHeadAndMiddle) {
    auto book = make_book();
    book.rest(1, 10, Side::Sell, 1500, 10);
    book.rest(2, 10, Side::Sell, 1500, 20);
    book.rest(3, 10, Side::Sell, 1500, 30);
    ASSERT_TRUE(book.remove(1));
    EXPECT_EQ(book.orders_at(Side::Sell, 1500), (Ids{2, 3}));
    ASSERT_TRUE(book.remove(3));
    EXPECT_EQ(book.orders_at(Side::Sell, 1500), Ids{2});
    EXPECT_EQ(book.level_qty(Side::Sell, 1500), 20);
    EXPECT_EQ(book.best_ask(), 1500);
}

TEST(OrderBook, RestAfterRemoveTailAppendsAtEnd) {
    auto book = make_book();
    book.rest(1, 10, Side::Buy, 1500, 10);
    book.rest(2, 10, Side::Buy, 1500, 20);
    book.remove(2);
    book.rest(3, 10, Side::Buy, 1500, 30);
    EXPECT_EQ(book.orders_at(Side::Buy, 1500), (Ids{1, 3}));
    EXPECT_EQ(book.level_qty(Side::Buy, 1500), 40);
}

TEST(OrderBook, RestAfterRemoveAllReusesLevel) {
    auto book = make_book();
    book.rest(1, 10, Side::Sell, 1500, 10);
    book.rest(2, 10, Side::Sell, 1500, 20);
    book.remove(1);
    book.remove(2);
    ASSERT_TRUE(book.rest(3, 10, Side::Sell, 1500, 5));
    EXPECT_EQ(book.orders_at(Side::Sell, 1500), Ids{3});
    EXPECT_EQ(book.level_qty(Side::Sell, 1500), 5);
    EXPECT_EQ(book.best_ask(), 1500);
}

TEST(OrderBook, RemovedIdCanBeReused) {
    auto book = make_book();
    ASSERT_TRUE(book.rest(1, 10, Side::Buy, 1500, 10));
    ASSERT_TRUE(book.remove(1));
    ASSERT_TRUE(book.rest(1, 11, Side::Sell, 1600, 20));
    EXPECT_EQ(book.orders_at(Side::Sell, 1600), Ids{1});
    EXPECT_FALSE(book.best_bid().has_value());
}

TEST(OrderBook, RemoveBestBidMovesToNextLevel) {
    auto book = make_book();
    book.rest(1, 10, Side::Buy, 1400, 10);
    book.rest(2, 10, Side::Buy, 1600, 10);
    book.rest(3, 10, Side::Buy, 1500, 10);
    ASSERT_TRUE(book.remove(2));
    EXPECT_EQ(book.best_bid(), 1500);
    ASSERT_TRUE(book.remove(3));
    EXPECT_EQ(book.best_bid(), 1400);
    ASSERT_TRUE(book.remove(1));
    EXPECT_FALSE(book.best_bid().has_value());
}

TEST(OrderBook, RemoveBestAskMovesToNextLevel) {
    auto book = make_book();
    book.rest(1, 10, Side::Sell, 1600, 10);
    book.rest(2, 10, Side::Sell, 1400, 10);
    book.rest(3, 10, Side::Sell, 1500, 10);
    ASSERT_TRUE(book.remove(2));
    EXPECT_EQ(book.best_ask(), 1500);
    ASSERT_TRUE(book.remove(3));
    EXPECT_EQ(book.best_ask(), 1600);
    ASSERT_TRUE(book.remove(1));
    EXPECT_FALSE(book.best_ask().has_value());
}

TEST(OrderBook, RemoveBestAtBoundaries) {
    auto book = make_book();
    book.rest(1, 10, Side::Buy, MIN, 10);
    book.rest(2, 10, Side::Buy, MAX, 10);
    book.rest(3, 10, Side::Sell, MIN, 10);
    book.rest(4, 10, Side::Sell, MAX, 10);
    ASSERT_TRUE(book.remove(2));
    EXPECT_EQ(book.best_bid(), MIN);
    ASSERT_TRUE(book.remove(3));
    EXPECT_EQ(book.best_ask(), MAX);
    ASSERT_TRUE(book.remove(1));
    ASSERT_TRUE(book.remove(4));
    EXPECT_FALSE(book.best_bid().has_value());
    EXPECT_FALSE(book.best_ask().has_value());
}

TEST(OrderBook, RemoveOneOfManyAtBestKeepsBest) {
    auto book = make_book();
    book.rest(1, 10, Side::Buy, 1600, 10);
    book.rest(2, 10, Side::Buy, 1600, 20);
    book.rest(3, 10, Side::Buy, 1500, 30);
    ASSERT_TRUE(book.remove(1));
    EXPECT_EQ(book.best_bid(), 1600);
    EXPECT_EQ(book.level_qty(Side::Buy, 1600), 20);
}

TEST(OrderBook, RemoveNonBestLevelKeepsBest) {
    auto book = make_book();
    book.rest(1, 10, Side::Sell, 1400, 10);
    book.rest(2, 10, Side::Sell, 1600, 10);
    ASSERT_TRUE(book.remove(2));
    EXPECT_EQ(book.best_ask(), 1400);
    EXPECT_EQ(book.level_qty(Side::Sell, 1600), 0);
}

TEST(OrderBook, RemoveNonBestBidLevelKeepsBest) {
    auto book = make_book();
    book.rest(1, 10, Side::Buy, 1600, 10);
    book.rest(2, 10, Side::Buy, 1400, 10);
    ASSERT_TRUE(book.remove(2));
    EXPECT_EQ(book.best_bid(), 1600);
    EXPECT_EQ(book.level_qty(Side::Buy, 1400), 0);
}

TEST(OrderBook, RemoveFreesPoolSlot) {
    auto book = make_book(1);
    ASSERT_TRUE(book.rest(1, 10, Side::Buy, 1500, 10));
    ASSERT_FALSE(book.rest(2, 10, Side::Buy, 1500, 10));
    ASSERT_TRUE(book.remove(1));
    ASSERT_TRUE(book.rest(2, 10, Side::Sell, 1600, 20));
    EXPECT_EQ(book.best_ask(), 1600);
    EXPECT_FALSE(book.best_bid().has_value());
    EXPECT_EQ(book.orders_at(Side::Sell, 1600), Ids{2});
}

// --- front ---

TEST(OrderBook, FrontIsOldestAtBestBid) {
    auto book = make_book();
    book.rest(1, 10, Side::Buy, 1500, 10);
    book.rest(2, 10, Side::Buy, 1600, 10);
    book.rest(3, 10, Side::Buy, 1600, 10);
    EXPECT_EQ(front_id(book, Side::Buy), 2);
}

TEST(OrderBook, FrontIsOldestAtBestAsk) {
    auto book = make_book();
    book.rest(1, 10, Side::Sell, 1600, 10);
    book.rest(2, 10, Side::Sell, 1500, 10);
    book.rest(3, 10, Side::Sell, 1500, 10);
    EXPECT_EQ(front_id(book, Side::Sell), 2);
}

TEST(OrderBook, FrontLooksOnlyAtRequestedSide) {
    auto book = make_book();
    book.rest(1, 10, Side::Buy, 1500, 10);
    EXPECT_EQ(front_id(book, Side::Buy), 1);
    EXPECT_FALSE(book.front(Side::Sell).has_value());
}

TEST(OrderBook, FrontAdvancesWithinLevelAfterRemove) {
    auto book = make_book();
    book.rest(1, 10, Side::Sell, 1500, 10);
    book.rest(2, 10, Side::Sell, 1500, 10);
    book.rest(3, 10, Side::Sell, 1600, 10);
    book.remove(1);
    EXPECT_EQ(front_id(book, Side::Sell), 2);
}

TEST(OrderBook, FrontMovesToNextLevelAfterRemove) {
    auto book = make_book();
    book.rest(1, 10, Side::Buy, 1600, 10);
    book.rest(2, 10, Side::Buy, 1500, 10);
    book.remove(1);
    EXPECT_EQ(front_id(book, Side::Buy), 2);
    book.remove(2);
    EXPECT_FALSE(book.front(Side::Buy).has_value());
}

TEST(OrderBook, FrontUpdatesWhenBetterPriceArrives) {
    auto book = make_book();
    book.rest(1, 10, Side::Sell, 1600, 10);
    EXPECT_EQ(front_id(book, Side::Sell), 1);
    book.rest(2, 10, Side::Sell, 1550, 10);
    EXPECT_EQ(front_id(book, Side::Sell), 2);
}

// --- reduce ---

TEST(OrderBook, ReducePartialBid) {
    auto book = make_book();
    book.rest(1, 10, Side::Buy, 1500, 100);
    const auto idx = book.front(Side::Buy);
    ASSERT_TRUE(idx.has_value());
    book.reduce(*idx, 30);
    const Order& o = book.get_order(*idx);
    EXPECT_EQ(o.qty, 70);
    EXPECT_EQ(o.filled, 30);
    EXPECT_EQ(book.level_qty(Side::Buy, 1500), 70);
    EXPECT_EQ(book.best_bid(), 1500);
}

TEST(OrderBook, ReducePartialAsk) {
    auto book = make_book();
    book.rest(1, 10, Side::Sell, 1500, 100);
    const auto idx = book.front(Side::Sell);
    ASSERT_TRUE(idx.has_value());
    book.reduce(*idx, 40);
    const Order& o = book.get_order(*idx);
    EXPECT_EQ(o.qty, 60);
    EXPECT_EQ(o.filled, 40);
    EXPECT_EQ(book.level_qty(Side::Sell, 1500), 60);
    EXPECT_EQ(book.best_ask(), 1500);
}

TEST(OrderBook, ReduceAccumulatesFilled) {
    auto book = make_book();
    book.rest(1, 10, Side::Sell, 1500, 100);
    const OrderIdx idx = *book.front(Side::Sell);
    book.reduce(idx, 10);
    book.reduce(idx, 25);
    book.reduce(idx, 5);
    EXPECT_EQ(book.get_order(idx).qty, 60);
    EXPECT_EQ(book.get_order(idx).filled, 40);
    EXPECT_EQ(book.level_qty(Side::Sell, 1500), 60);
}

TEST(OrderBook, ReduceKeepsTimePriority) {
    auto book = make_book();
    book.rest(1, 10, Side::Buy, 1500, 50);
    book.rest(2, 10, Side::Buy, 1500, 50);
    book.reduce(*book.front(Side::Buy), 20);
    EXPECT_EQ(book.orders_at(Side::Buy, 1500), (Ids{1, 2}));
    EXPECT_EQ(front_id(book, Side::Buy), 1);
    EXPECT_EQ(book.level_qty(Side::Buy, 1500), 80);
}

TEST(OrderBook, ReduceNonHeadOrder) {
    auto book = make_book();
    book.rest(1, 10, Side::Sell, 1500, 50);
    book.rest(2, 10, Side::Sell, 1500, 50);
    const OrderIdx head = *book.front(Side::Sell);
    const OrderIdx second = book.get_order(head).next;
    ASSERT_NE(second, NIL);
    book.reduce(second, 15);
    EXPECT_EQ(book.get_order(second).qty, 35);
    EXPECT_EQ(book.get_order(head).qty, 50);
    EXPECT_EQ(book.level_qty(Side::Sell, 1500), 85);
}

TEST(OrderBook, ReduceDoesNotTouchOtherLevelsOrSide) {
    auto book = make_book();
    book.rest(1, 10, Side::Buy, 1500, 50);
    book.rest(2, 10, Side::Buy, 1495, 50);
    book.rest(3, 10, Side::Sell, 1510, 50);
    book.reduce(*book.front(Side::Buy), 20);
    EXPECT_EQ(book.level_qty(Side::Buy, 1500), 30);
    EXPECT_EQ(book.level_qty(Side::Buy, 1495), 50);
    EXPECT_EQ(book.level_qty(Side::Sell, 1510), 50);
}

// convenție: reduce nu scoate ordinul la qty 0, apelantul cheamă remove
TEST(OrderBook, ReduceToZeroKeepsOrderUntilRemove) {
    auto book = make_book();
    book.rest(1, 10, Side::Buy, 1500, 50);
    book.rest(2, 10, Side::Buy, 1490, 50);
    const OrderIdx idx = *book.front(Side::Buy);
    book.reduce(idx, 50);
    EXPECT_EQ(book.get_order(idx).qty, 0);
    EXPECT_EQ(book.get_order(idx).filled, 50);
    EXPECT_EQ(book.level_qty(Side::Buy, 1500), 0);
    EXPECT_EQ(book.orders_at(Side::Buy, 1500), Ids{1});
    EXPECT_EQ(book.best_bid(), 1500);
}

TEST(OrderBook, ReduceToZeroThenRemoveKeepsLevelQtyConsistent) {
    auto book = make_book();
    book.rest(1, 10, Side::Sell, 1500, 50);
    book.rest(2, 10, Side::Sell, 1500, 30);
    book.rest(3, 10, Side::Sell, 1510, 40);
    book.reduce(*book.front(Side::Sell), 50);
    ASSERT_TRUE(book.remove(1));
    EXPECT_EQ(book.level_qty(Side::Sell, 1500), 30);
    EXPECT_EQ(book.orders_at(Side::Sell, 1500), Ids{2});
    EXPECT_EQ(book.best_ask(), 1500);

    book.reduce(*book.front(Side::Sell), 30);
    ASSERT_TRUE(book.remove(2));
    EXPECT_EQ(book.level_qty(Side::Sell, 1500), 0);
    EXPECT_EQ(book.best_ask(), 1510);
    EXPECT_EQ(front_id(book, Side::Sell), 3);
}

TEST(OrderBook, RemoveAfterPartialReduceKeepsLevelQtyConsistent) {
    auto book = make_book();
    book.rest(1, 10, Side::Buy, 1500, 50);
    book.rest(2, 10, Side::Buy, 1500, 30);
    book.reduce(*book.front(Side::Buy), 20);
    ASSERT_TRUE(book.remove(1));
    EXPECT_EQ(book.level_qty(Side::Buy, 1500), 30);
}

// --- available ---
// convenție: side = partea din carte care se numără; limit aliniat la tick

TEST(OrderBook, AvailableAsksUpToLimitInclusive) {
    auto book = make_book();
    book.rest(1, 10, Side::Sell, 1500, 10);
    book.rest(2, 10, Side::Sell, 1505, 20);
    book.rest(3, 10, Side::Sell, 1510, 40);
    EXPECT_EQ(book.available(Side::Sell, 1500), 10);
    EXPECT_EQ(book.available(Side::Sell, 1505), 30);
    EXPECT_EQ(book.available(Side::Sell, 1510), 70);
    EXPECT_EQ(book.available(Side::Sell, 1600), 70);
}

TEST(OrderBook, AvailableBidsDownToLimitInclusive) {
    auto book = make_book();
    book.rest(1, 10, Side::Buy, 1500, 10);
    book.rest(2, 10, Side::Buy, 1495, 20);
    book.rest(3, 10, Side::Buy, 1490, 40);
    EXPECT_EQ(book.available(Side::Buy, 1500), 10);
    EXPECT_EQ(book.available(Side::Buy, 1495), 30);
    EXPECT_EQ(book.available(Side::Buy, 1490), 70);
    EXPECT_EQ(book.available(Side::Buy, 1400), 70);
}

TEST(OrderBook, AvailableAsksLimitBelowBestIsZero) {
    auto book = make_book();
    book.rest(1, 10, Side::Sell, 1500, 10);
    EXPECT_EQ(book.available(Side::Sell, 1495), 0);
    EXPECT_EQ(book.available(Side::Sell, MIN), 0);
}

TEST(OrderBook, AvailableBidsLimitAboveBestIsZero) {
    auto book = make_book();
    book.rest(1, 10, Side::Buy, 1500, 10);
    EXPECT_EQ(book.available(Side::Buy, 1505), 0);
    EXPECT_EQ(book.available(Side::Buy, MAX), 0);
}

TEST(OrderBook, AvailableSumsWholeLevel) {
    auto book = make_book();
    book.rest(1, 10, Side::Sell, 1500, 10);
    book.rest(2, 11, Side::Sell, 1500, 15);
    book.rest(3, 12, Side::Sell, 1500, 25);
    EXPECT_EQ(book.available(Side::Sell, 1500), 50);
}

TEST(OrderBook, AvailableSkipsEmptyLevelsInBetween) {
    auto book = make_book();
    book.rest(1, 10, Side::Buy, 1800, 10);
    book.rest(2, 10, Side::Buy, 1200, 20);
    EXPECT_EQ(book.available(Side::Buy, 1500), 10);
    EXPECT_EQ(book.available(Side::Buy, 1200), 30);
}

TEST(OrderBook, AvailableIgnoresOtherSide) {
    auto book = make_book();
    book.rest(1, 10, Side::Buy, 1500, 10);
    book.rest(2, 10, Side::Sell, 1600, 20);
    EXPECT_EQ(book.available(Side::Sell, MAX), 20);
    EXPECT_EQ(book.available(Side::Buy, MIN), 10);
}

TEST(OrderBook, AvailableAtBoundaries) {
    auto book = make_book();
    book.rest(1, 10, Side::Sell, MIN, 10);
    book.rest(2, 10, Side::Sell, MAX, 20);
    book.rest(3, 10, Side::Buy, MIN, 30);
    book.rest(4, 10, Side::Buy, MAX, 40);
    EXPECT_EQ(book.available(Side::Sell, MIN), 10);
    EXPECT_EQ(book.available(Side::Sell, MAX), 30);
    EXPECT_EQ(book.available(Side::Buy, MAX), 40);
    EXPECT_EQ(book.available(Side::Buy, MIN), 70);
}

TEST(OrderBook, AvailableLimitOutsideBandIsClamped) {
    auto book = make_book();
    book.rest(1, 10, Side::Sell, 1500, 10);
    book.rest(2, 10, Side::Sell, MAX, 20);
    book.rest(3, 10, Side::Buy, 1400, 30);
    book.rest(4, 10, Side::Buy, MIN, 40);
    EXPECT_EQ(book.available(Side::Sell, MAX + 100), 30);
    EXPECT_EQ(book.available(Side::Sell, MIN - 100), 0);
    EXPECT_EQ(book.available(Side::Buy, MIN - 100), 70);
    EXPECT_EQ(book.available(Side::Buy, MAX + 100), 0);
}

TEST(OrderBook, AvailableReflectsReduce) {
    auto book = make_book();
    book.rest(1, 10, Side::Sell, 1500, 50);
    book.rest(2, 10, Side::Sell, 1505, 50);
    book.reduce(*book.front(Side::Sell), 20);
    EXPECT_EQ(book.available(Side::Sell, 1500), 30);
    EXPECT_EQ(book.available(Side::Sell, 1505), 80);
}

TEST(OrderBook, AvailableReflectsRemove) {
    auto book = make_book();
    book.rest(1, 10, Side::Buy, 1500, 50);
    book.rest(2, 10, Side::Buy, 1495, 50);
    book.remove(1);
    EXPECT_EQ(book.available(Side::Buy, 1500), 0);
    EXPECT_EQ(book.available(Side::Buy, 1495), 50);
}

// --- scenarii mixte ---

TEST(OrderBook, MixedScenario) {
    auto book = make_book();
    book.rest(1, 1, Side::Buy,  1490, 100);
    book.rest(2, 2, Side::Buy,  1495, 50);
    book.rest(3, 3, Side::Buy,  1495, 25);
    book.rest(4, 1, Side::Sell, 1505, 70);
    book.rest(5, 2, Side::Sell, 1510, 30);
    book.rest(6, 3, Side::Sell, 1505, 10);

    EXPECT_EQ(book.best_bid(), 1495);
    EXPECT_EQ(book.best_ask(), 1505);
    EXPECT_EQ(book.level_qty(Side::Buy, 1495), 75);
    EXPECT_EQ(book.level_qty(Side::Sell, 1505), 80);
    EXPECT_EQ(book.orders_at(Side::Sell, 1505), (Ids{4, 6}));

    book.remove(2);
    book.remove(3);
    EXPECT_EQ(book.best_bid(), 1490);

    book.remove(4);
    EXPECT_EQ(book.best_ask(), 1505);
    EXPECT_EQ(book.orders_at(Side::Sell, 1505), Ids{6});

    book.remove(6);
    EXPECT_EQ(book.best_ask(), 1510);
}

// simulează bucla matcher-ului: un buy de 100 @ 1510 consumă asks cu front/reduce/remove
TEST(OrderBook, MatcherStyleSweep) {
    auto book = make_book();
    book.rest(1, 1, Side::Sell, 1500, 30);
    book.rest(2, 2, Side::Sell, 1500, 20);
    book.rest(3, 3, Side::Sell, 1505, 40);
    book.rest(4, 4, Side::Sell, 1510, 50);
    book.rest(5, 5, Side::Sell, 1515, 60);

    ASSERT_EQ(book.available(Side::Sell, 1510), 140);

    FIX::Qty remaining = 100;
    std::vector<std::pair<FIX::OrderId, FIX::Qty>> fills;
    while (remaining > 0) {
        const auto idx = book.front(Side::Sell);
        if (!idx || book.get_order(*idx).price > 1510) break;
        const Order& resting = book.get_order(*idx);
        const FIX::Qty fill = std::min(remaining, resting.qty);
        const FIX::OrderId id = resting.id;
        book.reduce(*idx, fill);
        remaining -= fill;
        fills.emplace_back(id, fill);
        if (book.get_order(*idx).qty == 0) book.remove(id);
    }

    EXPECT_EQ(remaining, 0);
    const std::vector<std::pair<FIX::OrderId, FIX::Qty>> expected{{1, 30}, {2, 20}, {3, 40}, {4, 10}};
    EXPECT_EQ(fills, expected);

    EXPECT_EQ(book.best_ask(), 1510);
    EXPECT_EQ(book.level_qty(Side::Sell, 1500), 0);
    EXPECT_EQ(book.level_qty(Side::Sell, 1505), 0);
    EXPECT_EQ(book.level_qty(Side::Sell, 1510), 40);
    EXPECT_EQ(book.orders_at(Side::Sell, 1510), Ids{4});
    EXPECT_EQ(book.get_order(*book.front(Side::Sell)).filled, 10);
    EXPECT_EQ(book.available(Side::Sell, MAX), 100);
}

TEST(OrderBook, ManyRestRemoveCyclesStayConsistent) {
    auto book = make_book(16);
    FIX::OrderId next_id = 1;
    for (int round = 0; round < 50; ++round) {
        std::vector<FIX::OrderId> ids;
        for (int i = 0; i < 8; ++i) {
            const FIX::Price p = 1500 + (i % 4) * TICK;
            ASSERT_TRUE(book.rest(next_id, 1, Side::Buy, p, 10));
            ids.push_back(next_id++);
            ASSERT_TRUE(book.rest(next_id, 1, Side::Sell, p + 100, 10));
            ids.push_back(next_id++);
        }
        EXPECT_EQ(book.best_bid(), 1515);
        EXPECT_EQ(book.best_ask(), 1600);
        EXPECT_EQ(book.available(Side::Buy, MIN), 80);
        EXPECT_EQ(book.available(Side::Sell, MAX), 80);
        for (auto id : ids) ASSERT_TRUE(book.remove(id));
        EXPECT_FALSE(book.best_bid().has_value());
        EXPECT_FALSE(book.best_ask().has_value());
    }
}
