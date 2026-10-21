#include <gtest/gtest.h>
#include "exchange/engine/orderbook.hpp"
#include <vector>

namespace {

using FIX::Side;
using Ids = std::vector<FIX::OrderId>;

constexpr FIX::Price MIN  = 1000;
constexpr FIX::Price MAX  = 2000;
constexpr FIX::Price TICK = 5;

OrderBook make_book(std::size_t capacity = 64) { return OrderBook(MIN, MAX, TICK, capacity); }

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

TEST(OrderBook, RestFailsWhenPoolFull) {
    auto book = make_book(2);
    ASSERT_TRUE(book.rest(1, 10, Side::Buy, 1500, 10));
    ASSERT_TRUE(book.rest(2, 10, Side::Buy, 1500, 10));
    EXPECT_FALSE(book.rest(3, 10, Side::Buy, 1500, 10));
    EXPECT_EQ(book.orders_at(Side::Buy, 1500), (Ids{1, 2}));
    EXPECT_EQ(book.level_qty(Side::Buy, 1500), 20);
}

// --- cancel ---

TEST(OrderBook, CancelUnknownIdFails) {
    auto book = make_book();
    EXPECT_FALSE(book.cancel(42));
}

TEST(OrderBook, CancelTwiceFails) {
    auto book = make_book();
    book.rest(1, 10, Side::Buy, 1500, 10);
    EXPECT_TRUE(book.cancel(1));
    EXPECT_FALSE(book.cancel(1));
}

TEST(OrderBook, CancelOnlyOrderEmptiesBook) {
    auto book = make_book();
    book.rest(1, 10, Side::Buy, 1500, 100);
    book.rest(2, 10, Side::Sell, 1600, 100);
    ASSERT_TRUE(book.cancel(1));
    ASSERT_TRUE(book.cancel(2));
    EXPECT_FALSE(book.best_bid().has_value());
    EXPECT_FALSE(book.best_ask().has_value());
    EXPECT_EQ(book.level_qty(Side::Buy, 1500), 0);
    EXPECT_EQ(book.level_qty(Side::Sell, 1600), 0);
    EXPECT_TRUE(book.orders_at(Side::Buy, 1500).empty());
}

TEST(OrderBook, CancelHeadOfLevel) {
    auto book = make_book();
    book.rest(1, 10, Side::Buy, 1500, 10);
    book.rest(2, 10, Side::Buy, 1500, 20);
    book.rest(3, 10, Side::Buy, 1500, 30);
    ASSERT_TRUE(book.cancel(1));
    EXPECT_EQ(book.orders_at(Side::Buy, 1500), (Ids{2, 3}));
    EXPECT_EQ(book.level_qty(Side::Buy, 1500), 50);
    EXPECT_EQ(book.best_bid(), 1500);
}

TEST(OrderBook, CancelMiddleOfLevel) {
    auto book = make_book();
    book.rest(1, 10, Side::Buy, 1500, 10);
    book.rest(2, 10, Side::Buy, 1500, 20);
    book.rest(3, 10, Side::Buy, 1500, 30);
    ASSERT_TRUE(book.cancel(2));
    EXPECT_EQ(book.orders_at(Side::Buy, 1500), (Ids{1, 3}));
    EXPECT_EQ(book.level_qty(Side::Buy, 1500), 40);
}

TEST(OrderBook, CancelTailOfLevel) {
    auto book = make_book();
    book.rest(1, 10, Side::Sell, 1500, 10);
    book.rest(2, 10, Side::Sell, 1500, 20);
    book.rest(3, 10, Side::Sell, 1500, 30);
    ASSERT_TRUE(book.cancel(3));
    EXPECT_EQ(book.orders_at(Side::Sell, 1500), (Ids{1, 2}));
    EXPECT_EQ(book.level_qty(Side::Sell, 1500), 30);
}

TEST(OrderBook, RestAfterCancelTailAppendsAtEnd) {
    auto book = make_book();
    book.rest(1, 10, Side::Buy, 1500, 10);
    book.rest(2, 10, Side::Buy, 1500, 20);
    book.cancel(2);
    book.rest(3, 10, Side::Buy, 1500, 30);
    EXPECT_EQ(book.orders_at(Side::Buy, 1500), (Ids{1, 3}));
    EXPECT_EQ(book.level_qty(Side::Buy, 1500), 40);
}

TEST(OrderBook, CancelBestBidMovesToNextLevel) {
    auto book = make_book();
    book.rest(1, 10, Side::Buy, 1400, 10);
    book.rest(2, 10, Side::Buy, 1600, 10);
    book.rest(3, 10, Side::Buy, 1500, 10);
    ASSERT_TRUE(book.cancel(2));
    EXPECT_EQ(book.best_bid(), 1500);
    ASSERT_TRUE(book.cancel(3));
    EXPECT_EQ(book.best_bid(), 1400);
    ASSERT_TRUE(book.cancel(1));
    EXPECT_FALSE(book.best_bid().has_value());
}

TEST(OrderBook, CancelBestAskMovesToNextLevel) {
    auto book = make_book();
    book.rest(1, 10, Side::Sell, 1600, 10);
    book.rest(2, 10, Side::Sell, 1400, 10);
    book.rest(3, 10, Side::Sell, 1500, 10);
    ASSERT_TRUE(book.cancel(2));
    EXPECT_EQ(book.best_ask(), 1500);
    ASSERT_TRUE(book.cancel(3));
    EXPECT_EQ(book.best_ask(), 1600);
    ASSERT_TRUE(book.cancel(1));
    EXPECT_FALSE(book.best_ask().has_value());
}

TEST(OrderBook, CancelOneOfManyAtBestKeepsBest) {
    auto book = make_book();
    book.rest(1, 10, Side::Buy, 1600, 10);
    book.rest(2, 10, Side::Buy, 1600, 20);
    book.rest(3, 10, Side::Buy, 1500, 30);
    ASSERT_TRUE(book.cancel(1));
    EXPECT_EQ(book.best_bid(), 1600);
    EXPECT_EQ(book.level_qty(Side::Buy, 1600), 20);
}

TEST(OrderBook, CancelNonBestLevelKeepsBest) {
    auto book = make_book();
    book.rest(1, 10, Side::Sell, 1400, 10);
    book.rest(2, 10, Side::Sell, 1600, 10);
    ASSERT_TRUE(book.cancel(2));
    EXPECT_EQ(book.best_ask(), 1400);
    EXPECT_EQ(book.level_qty(Side::Sell, 1600), 0);
}

TEST(OrderBook, CancelFreesPoolSlot) {
    auto book = make_book(1);
    ASSERT_TRUE(book.rest(1, 10, Side::Buy, 1500, 10));
    ASSERT_FALSE(book.rest(2, 10, Side::Buy, 1500, 10));
    ASSERT_TRUE(book.cancel(1));
    ASSERT_TRUE(book.rest(2, 10, Side::Sell, 1600, 20));
    EXPECT_EQ(book.best_ask(), 1600);
    EXPECT_FALSE(book.best_bid().has_value());
    EXPECT_EQ(book.orders_at(Side::Sell, 1600), Ids{2});
}

// --- scenariu mixt ---

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

    book.cancel(2);
    book.cancel(3);
    EXPECT_EQ(book.best_bid(), 1490);

    book.cancel(4);
    EXPECT_EQ(book.best_ask(), 1505);
    EXPECT_EQ(book.orders_at(Side::Sell, 1505), Ids{6});

    book.cancel(6);
    EXPECT_EQ(book.best_ask(), 1510);
}