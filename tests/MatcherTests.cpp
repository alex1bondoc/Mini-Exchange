#include <gtest/gtest.h>
#include "exchange/engine/matcher.hpp"
#include <numeric>
#include <ostream>
#include <vector>

// gtest compară și afișează Fill-urile câmp cu câmp
bool operator==(const Fill& a, const Fill& b) {
    return a.id == b.id && a.client == b.client && a.side == b.side && a.price == b.price &&
           a.qty == b.qty && a.filled == b.filled && a.leaves == b.leaves;
}

void PrintTo(const Fill& f, std::ostream* os) {
    *os << "{id=" << f.id << " client=" << f.client
        << " side=" << (f.side == FIX::Side::Buy ? "Buy" : "Sell")
        << " price=" << f.price << " qty=" << f.qty
        << " filled=" << f.filled << " leaves=" << f.leaves << "}";
}

namespace {

using FIX::Side;
using Ids   = std::vector<FIX::OrderId>;
using Fills = std::vector<Fill>;

constexpr FIX::SymbolId SYMBOL = 0;
constexpr FIX::Price MIN  = 1000;
constexpr FIX::Price MAX  = 2000;
constexpr FIX::Price TICK = 5;
constexpr std::optional<FIX::Price> MARKET = std::nullopt;

OrderBook make_book(std::size_t capacity = 64) { return OrderBook(SYMBOL, MIN, MAX, TICK, capacity); }

FIX::Qty sum_qty(const Fills& fills) {
    return std::accumulate(fills.begin(), fills.end(), FIX::Qty{0},
                           [](FIX::Qty acc, const Fill& f) { return acc + f.qty; });
}

}  // namespace

// --- carte goală / nimic de executat ---

TEST(Matcher, EmptyBookBuyExecutesNothing) {
    auto book = make_book();
    Fills fills;
    EXPECT_EQ(match(book, Side::Buy, 1500, 100, fills), 0);
    EXPECT_TRUE(fills.empty());
}

TEST(Matcher, EmptyBookSellExecutesNothing) {
    auto book = make_book();
    Fills fills;
    EXPECT_EQ(match(book, Side::Sell, 1500, 100, fills), 0);
    EXPECT_TRUE(fills.empty());
}

TEST(Matcher, EmptyBookMarketExecutesNothing) {
    auto book = make_book();
    Fills fills;
    EXPECT_EQ(match(book, Side::Buy, MARKET, 100, fills), 0);
    EXPECT_EQ(match(book, Side::Sell, MARKET, 100, fills), 0);
    EXPECT_TRUE(fills.empty());
}

TEST(Matcher, BuyBelowBestAskDoesNotCross) {
    auto book = make_book();
    book.rest(1, 10, Side::Sell, 1500, 50);
    Fills fills;
    EXPECT_EQ(match(book, Side::Buy, 1495, 100, fills), 0);
    EXPECT_TRUE(fills.empty());
    EXPECT_EQ(book.level_qty(Side::Sell, 1500), 50);
    EXPECT_EQ(book.best_ask(), 1500);
}

TEST(Matcher, SellAboveBestBidDoesNotCross) {
    auto book = make_book();
    book.rest(1, 10, Side::Buy, 1500, 50);
    Fills fills;
    EXPECT_EQ(match(book, Side::Sell, 1505, 100, fills), 0);
    EXPECT_TRUE(fills.empty());
    EXPECT_EQ(book.level_qty(Side::Buy, 1500), 50);
    EXPECT_EQ(book.best_bid(), 1500);
}

TEST(Matcher, BuyIgnoresBids) {
    auto book = make_book();
    book.rest(1, 10, Side::Buy, 1500, 50);
    Fills fills;
    EXPECT_EQ(match(book, Side::Buy, 1500, 100, fills), 0);
    EXPECT_TRUE(fills.empty());
    EXPECT_EQ(book.level_qty(Side::Buy, 1500), 50);
}

TEST(Matcher, SellIgnoresAsks) {
    auto book = make_book();
    book.rest(1, 10, Side::Sell, 1500, 50);
    Fills fills;
    EXPECT_EQ(match(book, Side::Sell, 1500, 100, fills), 0);
    EXPECT_TRUE(fills.empty());
    EXPECT_EQ(book.level_qty(Side::Sell, 1500), 50);
}

TEST(Matcher, MarketBuyIgnoresBids) {
    auto book = make_book();
    book.rest(1, 10, Side::Buy, 1500, 50);
    Fills fills;
    EXPECT_EQ(match(book, Side::Buy, MARKET, 100, fills), 0);
    EXPECT_TRUE(fills.empty());
    EXPECT_EQ(book.level_qty(Side::Buy, 1500), 50);
}

// --- limita e inclusivă ---

TEST(Matcher, BuyAtExactlyAskPriceExecutes) {
    auto book = make_book();
    book.rest(1, 10, Side::Sell, 1500, 50);
    Fills fills;
    EXPECT_EQ(match(book, Side::Buy, 1500, 50, fills), 50);
    ASSERT_EQ(fills.size(), 1u);
    EXPECT_EQ(fills[0].price, 1500);
}

TEST(Matcher, SellAtExactlyBidPriceExecutes) {
    auto book = make_book();
    book.rest(1, 10, Side::Buy, 1500, 50);
    Fills fills;
    EXPECT_EQ(match(book, Side::Sell, 1500, 50, fills), 50);
    ASSERT_EQ(fills.size(), 1u);
    EXPECT_EQ(fills[0].price, 1500);
}

// --- un singur resting ---

TEST(Matcher, ExactFillRemovesResting) {
    auto book = make_book();
    book.rest(7, 42, Side::Sell, 1500, 50);
    Fills fills;
    EXPECT_EQ(match(book, Side::Buy, 1500, 50, fills), 50);
    const Fills expected{{.id = 7, .client = 42, .side = Side::Sell, .price = 1500,
                          .qty = 50, .filled = 50, .leaves = 0}};
    EXPECT_EQ(fills, expected);
    EXPECT_FALSE(book.best_ask().has_value());
    EXPECT_EQ(book.level_qty(Side::Sell, 1500), 0);
    EXPECT_TRUE(book.orders_at(Side::Sell, 1500).empty());
}

TEST(Matcher, ExactFillRemovesRestingBid) {
    auto book = make_book();
    book.rest(7, 42, Side::Buy, 1500, 50);
    Fills fills;
    EXPECT_EQ(match(book, Side::Sell, 1500, 50, fills), 50);
    const Fills expected{{.id = 7, .client = 42, .side = Side::Buy, .price = 1500,
                          .qty = 50, .filled = 50, .leaves = 0}};
    EXPECT_EQ(fills, expected);
    EXPECT_FALSE(book.best_bid().has_value());
}

TEST(Matcher, PartialFillKeepsRestingInBook) {
    auto book = make_book();
    book.rest(7, 42, Side::Sell, 1500, 100);
    Fills fills;
    EXPECT_EQ(match(book, Side::Buy, 1500, 30, fills), 30);
    const Fills expected{{.id = 7, .client = 42, .side = Side::Sell, .price = 1500,
                          .qty = 30, .filled = 30, .leaves = 70}};
    EXPECT_EQ(fills, expected);
    EXPECT_EQ(book.best_ask(), 1500);
    EXPECT_EQ(book.level_qty(Side::Sell, 1500), 70);
    EXPECT_EQ(book.orders_at(Side::Sell, 1500), Ids{7});
    const Order& o = book.get_order(*book.front(Side::Sell));
    EXPECT_EQ(o.qty, 70);
    EXPECT_EQ(o.filled, 30);
}

TEST(Matcher, SuccessivePartialFillsAccumulateOnResting) {
    auto book = make_book();
    book.rest(7, 42, Side::Sell, 1500, 100);
    Fills fills;
    EXPECT_EQ(match(book, Side::Buy, 1500, 30, fills), 30);
    EXPECT_EQ(match(book, Side::Buy, 1500, 20, fills), 20);
    EXPECT_EQ(match(book, Side::Buy, 1500, 50, fills), 50);
    const Fills expected{
        {.id = 7, .client = 42, .side = Side::Sell, .price = 1500, .qty = 30, .filled = 30,  .leaves = 70},
        {.id = 7, .client = 42, .side = Side::Sell, .price = 1500, .qty = 20, .filled = 50,  .leaves = 50},
        {.id = 7, .client = 42, .side = Side::Sell, .price = 1500, .qty = 50, .filled = 100, .leaves = 0},
    };
    EXPECT_EQ(fills, expected);
    EXPECT_FALSE(book.best_ask().has_value());
}

TEST(Matcher, AggressorLargerThanRestingExecutesOnlyAvailable) {
    auto book = make_book();
    book.rest(7, 42, Side::Sell, 1500, 30);
    Fills fills;
    EXPECT_EQ(match(book, Side::Buy, 1500, 100, fills), 30);
    ASSERT_EQ(fills.size(), 1u);
    EXPECT_EQ(fills[0].qty, 30);
    EXPECT_EQ(fills[0].leaves, 0);
    EXPECT_FALSE(book.best_ask().has_value());
}

// matcher-ul nu face rest — restul agresorului e treaba engine-ului
TEST(Matcher, RemainderOfAggressorIsNotRested) {
    auto book = make_book();
    book.rest(7, 42, Side::Sell, 1500, 30);
    Fills fills;
    match(book, Side::Buy, 1500, 100, fills);
    EXPECT_FALSE(book.best_bid().has_value());
    EXPECT_EQ(book.level_qty(Side::Buy, 1500), 0);
}

// --- prioritate preț-timp ---

TEST(Matcher, TimePriorityWithinLevel) {
    auto book = make_book();
    book.rest(1, 11, Side::Sell, 1500, 10);
    book.rest(2, 12, Side::Sell, 1500, 20);
    book.rest(3, 13, Side::Sell, 1500, 30);
    Fills fills;
    EXPECT_EQ(match(book, Side::Buy, 1500, 25, fills), 25);
    const Fills expected{
        {.id = 1, .client = 11, .side = Side::Sell, .price = 1500, .qty = 10, .filled = 10, .leaves = 0},
        {.id = 2, .client = 12, .side = Side::Sell, .price = 1500, .qty = 15, .filled = 15, .leaves = 5},
    };
    EXPECT_EQ(fills, expected);
    EXPECT_EQ(book.orders_at(Side::Sell, 1500), (Ids{2, 3}));
    EXPECT_EQ(book.level_qty(Side::Sell, 1500), 35);
}

TEST(Matcher, BetterPriceBeatsEarlierTime) {
    auto book = make_book();
    book.rest(1, 11, Side::Sell, 1505, 10);
    book.rest(2, 12, Side::Sell, 1500, 10);
    Fills fills;
    EXPECT_EQ(match(book, Side::Buy, 1505, 10, fills), 10);
    ASSERT_EQ(fills.size(), 1u);
    EXPECT_EQ(fills[0].id, 2);
    EXPECT_EQ(fills[0].price, 1500);
}

TEST(Matcher, BetterBidBeatsEarlierTime) {
    auto book = make_book();
    book.rest(1, 11, Side::Buy, 1495, 10);
    book.rest(2, 12, Side::Buy, 1500, 10);
    Fills fills;
    EXPECT_EQ(match(book, Side::Sell, 1495, 10, fills), 10);
    ASSERT_EQ(fills.size(), 1u);
    EXPECT_EQ(fills[0].id, 2);
    EXPECT_EQ(fills[0].price, 1500);
}

// --- prețul trade-ului = prețul resting-ului ---

TEST(Matcher, BuyGetsPriceImprovement) {
    auto book = make_book();
    book.rest(1, 11, Side::Sell, 1500, 10);
    Fills fills;
    EXPECT_EQ(match(book, Side::Buy, 1600, 10, fills), 10);
    ASSERT_EQ(fills.size(), 1u);
    EXPECT_EQ(fills[0].price, 1500);
}

TEST(Matcher, SellGetsPriceImprovement) {
    auto book = make_book();
    book.rest(1, 11, Side::Buy, 1600, 10);
    Fills fills;
    EXPECT_EQ(match(book, Side::Sell, 1500, 10, fills), 10);
    ASSERT_EQ(fills.size(), 1u);
    EXPECT_EQ(fills[0].price, 1600);
}

// --- mai multe niveluri ---

TEST(Matcher, BuySweepsAsksUpToLimit) {
    auto book = make_book();
    book.rest(1, 11, Side::Sell, 1500, 30);
    book.rest(2, 12, Side::Sell, 1500, 20);
    book.rest(3, 13, Side::Sell, 1505, 40);
    book.rest(4, 14, Side::Sell, 1510, 50);
    book.rest(5, 15, Side::Sell, 1515, 60);
    Fills fills;
    EXPECT_EQ(match(book, Side::Buy, 1510, 100, fills), 100);
    const Fills expected{
        {.id = 1, .client = 11, .side = Side::Sell, .price = 1500, .qty = 30, .filled = 30, .leaves = 0},
        {.id = 2, .client = 12, .side = Side::Sell, .price = 1500, .qty = 20, .filled = 20, .leaves = 0},
        {.id = 3, .client = 13, .side = Side::Sell, .price = 1505, .qty = 40, .filled = 40, .leaves = 0},
        {.id = 4, .client = 14, .side = Side::Sell, .price = 1510, .qty = 10, .filled = 10, .leaves = 40},
    };
    EXPECT_EQ(fills, expected);
    EXPECT_EQ(book.best_ask(), 1510);
    EXPECT_EQ(book.level_qty(Side::Sell, 1510), 40);
    EXPECT_EQ(book.level_qty(Side::Sell, 1515), 60);
}

TEST(Matcher, SellSweepsBidsDownToLimit) {
    auto book = make_book();
    book.rest(1, 11, Side::Buy, 1500, 30);
    book.rest(2, 12, Side::Buy, 1495, 40);
    book.rest(3, 13, Side::Buy, 1490, 50);
    book.rest(4, 14, Side::Buy, 1485, 60);
    Fills fills;
    EXPECT_EQ(match(book, Side::Sell, 1490, 100, fills), 100);
    const Fills expected{
        {.id = 1, .client = 11, .side = Side::Buy, .price = 1500, .qty = 30, .filled = 30, .leaves = 0},
        {.id = 2, .client = 12, .side = Side::Buy, .price = 1495, .qty = 40, .filled = 40, .leaves = 0},
        {.id = 3, .client = 13, .side = Side::Buy, .price = 1490, .qty = 30, .filled = 30, .leaves = 20},
    };
    EXPECT_EQ(fills, expected);
    EXPECT_EQ(book.best_bid(), 1490);
    EXPECT_EQ(book.level_qty(Side::Buy, 1490), 20);
    EXPECT_EQ(book.level_qty(Side::Buy, 1485), 60);
}

TEST(Matcher, StopsAtLimitEvenWithQtyLeft) {
    auto book = make_book();
    book.rest(1, 11, Side::Sell, 1500, 10);
    book.rest(2, 12, Side::Sell, 1505, 10);
    book.rest(3, 13, Side::Sell, 1510, 10);
    Fills fills;
    EXPECT_EQ(match(book, Side::Buy, 1505, 100, fills), 20);
    ASSERT_EQ(fills.size(), 2u);
    EXPECT_EQ(fills[0].id, 1);
    EXPECT_EQ(fills[1].id, 2);
    EXPECT_EQ(book.best_ask(), 1510);
    EXPECT_EQ(book.orders_at(Side::Sell, 1510), Ids{3});
}

TEST(Matcher, SweepSkipsEmptyLevels) {
    auto book = make_book();
    book.rest(1, 11, Side::Sell, 1200, 10);
    book.rest(2, 12, Side::Sell, 1800, 10);
    Fills fills;
    EXPECT_EQ(match(book, Side::Buy, MAX, 20, fills), 20);
    ASSERT_EQ(fills.size(), 2u);
    EXPECT_EQ(fills[0].price, 1200);
    EXPECT_EQ(fills[1].price, 1800);
    EXPECT_FALSE(book.best_ask().has_value());
}

TEST(Matcher, ExactlyConsumesWholeSide) {
    auto book = make_book();
    book.rest(1, 11, Side::Sell, 1500, 10);
    book.rest(2, 12, Side::Sell, 1505, 20);
    Fills fills;
    EXPECT_EQ(match(book, Side::Buy, 1505, 30, fills), 30);
    ASSERT_EQ(fills.size(), 2u);
    EXPECT_EQ(fills[1].leaves, 0);
    EXPECT_FALSE(book.best_ask().has_value());
    EXPECT_FALSE(book.front(Side::Sell).has_value());
}

// --- Market ---

TEST(Matcher, MarketBuySweepsAllLevels) {
    auto book = make_book();
    book.rest(1, 11, Side::Sell, 1500, 10);
    book.rest(2, 12, Side::Sell, 1700, 10);
    book.rest(3, 13, Side::Sell, MAX, 10);
    Fills fills;
    EXPECT_EQ(match(book, Side::Buy, MARKET, 25, fills), 25);
    ASSERT_EQ(fills.size(), 3u);
    EXPECT_EQ(fills[0].price, 1500);
    EXPECT_EQ(fills[1].price, 1700);
    EXPECT_EQ(fills[2].price, MAX);
    EXPECT_EQ(fills[2].qty, 5);
    EXPECT_EQ(fills[2].leaves, 5);
    EXPECT_EQ(book.best_ask(), MAX);
}

TEST(Matcher, MarketSellSweepsAllLevels) {
    auto book = make_book();
    book.rest(1, 11, Side::Buy, 1500, 10);
    book.rest(2, 12, Side::Buy, 1300, 10);
    book.rest(3, 13, Side::Buy, MIN, 10);
    Fills fills;
    EXPECT_EQ(match(book, Side::Sell, MARKET, 30, fills), 30);
    ASSERT_EQ(fills.size(), 3u);
    EXPECT_EQ(fills[0].price, 1500);
    EXPECT_EQ(fills[1].price, 1300);
    EXPECT_EQ(fills[2].price, MIN);
    EXPECT_FALSE(book.best_bid().has_value());
}

TEST(Matcher, MarketLargerThanBookExecutesWhatExists) {
    auto book = make_book();
    book.rest(1, 11, Side::Sell, 1500, 10);
    book.rest(2, 12, Side::Sell, 1505, 15);
    Fills fills;
    EXPECT_EQ(match(book, Side::Buy, MARKET, 1000, fills), 25);
    EXPECT_EQ(fills.size(), 2u);
    EXPECT_FALSE(book.best_ask().has_value());
}

// --- proprietăți generale ---

TEST(Matcher, ReturnEqualsSumOfFills) {
    auto book = make_book();
    book.rest(1, 11, Side::Sell, 1500, 13);
    book.rest(2, 12, Side::Sell, 1505, 7);
    book.rest(3, 13, Side::Sell, 1510, 21);
    Fills fills;
    const FIX::Qty executed = match(book, Side::Buy, 1510, 30, fills);
    EXPECT_EQ(executed, 30);
    EXPECT_EQ(sum_qty(fills), executed);
}

TEST(Matcher, NeverEmitsZeroQtyFills) {
    auto book = make_book();
    book.rest(1, 11, Side::Sell, 1500, 10);
    book.rest(2, 12, Side::Sell, 1500, 10);
    book.rest(3, 13, Side::Sell, 1505, 10);
    Fills fills;
    match(book, Side::Buy, MARKET, 25, fills);
    match(book, Side::Buy, MARKET, 25, fills);
    for (const Fill& f : fills) EXPECT_GT(f.qty, 0);
    EXPECT_EQ(sum_qty(fills), 30);
}

TEST(Matcher, AppendsToExistingFills) {
    auto book = make_book();
    book.rest(1, 11, Side::Sell, 1500, 10);
    Fills fills{{.id = 99, .client = 1, .side = Side::Buy, .price = 1, .qty = 1, .filled = 1, .leaves = 0}};
    EXPECT_EQ(match(book, Side::Buy, 1500, 10, fills), 10);
    ASSERT_EQ(fills.size(), 2u);
    EXPECT_EQ(fills[0].id, 99);
    EXPECT_EQ(fills[1].id, 1);
}

TEST(Matcher, LevelQtyStaysConsistentAfterMatching) {
    auto book = make_book();
    book.rest(1, 11, Side::Sell, 1500, 10);
    book.rest(2, 12, Side::Sell, 1500, 20);
    book.rest(3, 13, Side::Sell, 1500, 30);
    Fills fills;
    match(book, Side::Buy, 1500, 15, fills);
    EXPECT_EQ(book.level_qty(Side::Sell, 1500), 45);
    EXPECT_EQ(book.available(Side::Sell, 1500), 45);
    match(book, Side::Buy, 1500, 40, fills);
    EXPECT_EQ(book.level_qty(Side::Sell, 1500), 5);
    EXPECT_EQ(book.orders_at(Side::Sell, 1500), Ids{3});
}

TEST(Matcher, FullFillsFreePoolSlots) {
    auto book = make_book(2);
    ASSERT_TRUE(book.rest(1, 11, Side::Sell, 1500, 10));
    ASSERT_TRUE(book.rest(2, 12, Side::Sell, 1505, 10));
    ASSERT_FALSE(book.rest(3, 13, Side::Sell, 1510, 10));
    Fills fills;
    EXPECT_EQ(match(book, Side::Buy, 1505, 20, fills), 20);
    EXPECT_TRUE(book.rest(3, 13, Side::Sell, 1510, 10));
    EXPECT_TRUE(book.rest(4, 14, Side::Buy, 1400, 10));
}

TEST(Matcher, FilledIdsCanBeReused) {
    auto book = make_book();
    book.rest(1, 11, Side::Sell, 1500, 10);
    Fills fills;
    match(book, Side::Buy, 1500, 10, fills);
    EXPECT_TRUE(book.rest(1, 11, Side::Sell, 1505, 10));
}

TEST(Matcher, BothSidesMatchedInSequence) {
    auto book = make_book();
    book.rest(1, 11, Side::Buy,  1495, 50);
    book.rest(2, 12, Side::Sell, 1505, 50);
    Fills fills;
    EXPECT_EQ(match(book, Side::Buy, 1505, 20, fills), 20);
    EXPECT_EQ(match(book, Side::Sell, 1495, 30, fills), 30);
    ASSERT_EQ(fills.size(), 2u);
    EXPECT_EQ(fills[0].id, 2);
    EXPECT_EQ(fills[0].leaves, 30);
    EXPECT_EQ(fills[1].id, 1);
    EXPECT_EQ(fills[1].leaves, 20);
    EXPECT_EQ(book.best_bid(), 1495);
    EXPECT_EQ(book.best_ask(), 1505);
}
