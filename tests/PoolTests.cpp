#include <gtest/gtest.h>
#include "exchange/engine/pool.hpp"
#include <algorithm>
#include <set>
#include <vector>

TEST(OrderPool, StartsEmpty) {
    OrderPool pool(4);
    EXPECT_EQ(pool.capacity(), 4u);
    EXPECT_EQ(pool.in_use(), 0u);
}

TEST(OrderPool, AllocatesInAscendingOrder) {
    OrderPool pool(3);
    EXPECT_EQ(pool.allocate(), 0u);
    EXPECT_EQ(pool.allocate(), 1u);
    EXPECT_EQ(pool.allocate(), 2u);
    EXPECT_EQ(pool.in_use(), 3u);
}

TEST(OrderPool, ReturnsNulloptWhenFull) {
    OrderPool pool(2);
    ASSERT_TRUE(pool.allocate());
    ASSERT_TRUE(pool.allocate());
    EXPECT_FALSE(pool.allocate().has_value());
    EXPECT_EQ(pool.in_use(), 2u);
}

TEST(OrderPool, ZeroCapacity) {
    OrderPool pool(0);
    EXPECT_EQ(pool.capacity(), 0u);
    EXPECT_FALSE(pool.allocate().has_value());
}

TEST(OrderPool, AllocatedIndicesAreUnique) {
    constexpr std::size_t N = 1000;
    OrderPool pool(N);
    std::set<OrderIdx> seen;
    for (std::size_t i = 0; i < N; ++i) {
        auto idx = pool.allocate();
        ASSERT_TRUE(idx);
        EXPECT_LT(*idx, N);
        EXPECT_TRUE(seen.insert(*idx).second);
    }
    EXPECT_EQ(pool.in_use(), N);
}

TEST(OrderPool, NewSlotIsDefault) {
    OrderPool pool(1);
    auto idx = pool.allocate();
    ASSERT_TRUE(idx);
    const Order& o = pool[*idx];
    EXPECT_EQ(o.id, 0);
    EXPECT_EQ(o.client, 0);
    EXPECT_EQ(o.price, 0);
    EXPECT_EQ(o.qty, 0);
    EXPECT_EQ(o.filled, 0);
    EXPECT_EQ(o.prev, NIL);
    EXPECT_EQ(o.next, NIL);
}

TEST(OrderPool, WriteAndReadBack) {
    OrderPool pool(2);
    auto a = *pool.allocate();
    auto b = *pool.allocate();

    pool[a] = Order{.id = 7, .client = 3, .price = 15025, .qty = 100,
                    .filled = 10, .side = FIX::Side::Sell, .prev = NIL, .next = b};
    pool[b].id = 8;
    pool[b].prev = a;

    const OrderPool& cpool = pool;
    EXPECT_EQ(cpool[a].id, 7);
    EXPECT_EQ(cpool[a].client, 3);
    EXPECT_EQ(cpool[a].price, 15025);
    EXPECT_EQ(cpool[a].qty, 100);
    EXPECT_EQ(cpool[a].filled, 10);
    EXPECT_EQ(cpool[a].side, FIX::Side::Sell);
    EXPECT_EQ(cpool[a].next, b);
    EXPECT_EQ(cpool[b].id, 8);
    EXPECT_EQ(cpool[b].prev, a);
}

TEST(OrderPool, ReleaseDecrementsInUse) {
    OrderPool pool(2);
    auto a = *pool.allocate();
    pool.allocate();
    pool.release(a);
    EXPECT_EQ(pool.in_use(), 1u);
}

TEST(OrderPool, ReleasedSlotIsReusedLifo) {
    OrderPool pool(4);
    auto a = *pool.allocate();
    auto b = *pool.allocate();
    pool.release(a);
    pool.release(b);
    EXPECT_EQ(pool.allocate(), b);
    EXPECT_EQ(pool.allocate(), a);
}

TEST(OrderPool, ReleaseResetsSlot) {
    OrderPool pool(1);
    auto a = *pool.allocate();
    pool[a].id = 42;
    pool[a].qty = 500;
    pool[a].next = 0;
    pool.release(a);

    auto b = *pool.allocate();
    ASSERT_EQ(a, b);
    EXPECT_EQ(pool[b].id, 0);
    EXPECT_EQ(pool[b].qty, 0);
    EXPECT_EQ(pool[b].next, NIL);
}

TEST(OrderPool, FullThenReleaseAllowsAllocate) {
    OrderPool pool(2);
    auto a = *pool.allocate();
    pool.allocate();
    ASSERT_FALSE(pool.allocate());
    pool.release(a);
    EXPECT_EQ(pool.allocate(), a);
    EXPECT_FALSE(pool.allocate());
}

TEST(OrderPool, ManyCycles) {
    OrderPool pool(8);
    std::vector<OrderIdx> live;
    for (int round = 0; round < 100; ++round) {
        while (auto idx = pool.allocate()) live.push_back(*idx);
        ASSERT_EQ(pool.in_use(), 8u);
        // eliberăm jumătate
        for (int i = 0; i < 4; ++i) {
            pool.release(live.back());
            live.pop_back();
        }
        ASSERT_EQ(pool.in_use(), 4u);
    }
    std::sort(live.begin(), live.end());
    EXPECT_EQ(std::adjacent_find(live.begin(), live.end()), live.end());
}

// assert-urile sunt active doar fără NDEBUG (Debug build);
// în Release apelurile ar fi UB, așa că sărim testele
TEST(OrderPoolDeathTest, DoubleReleaseAsserts) {
#ifdef NDEBUG
    GTEST_SKIP() << "assert dezactivat (NDEBUG)";
#endif
    OrderPool pool(1);
    auto a = *pool.allocate();
    pool.release(a);
    EXPECT_DEBUG_DEATH(pool.release(a), "double release");
}

TEST(OrderPoolDeathTest, ReleaseOutOfRangeAsserts) {
#ifdef NDEBUG
    GTEST_SKIP() << "assert dezactivat (NDEBUG)";
#endif
    OrderPool pool(1);
    EXPECT_DEBUG_DEATH(pool.release(5), "");
}
