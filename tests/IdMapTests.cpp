#include <gtest/gtest.h>
#include "exchange/gateway/id_map.hpp"

#include <string>
#include <string_view>

namespace {

FIX::Instrument instrument(FIX::SymbolId id, std::string_view name) {
    FIX::Instrument inst{};
    inst.id = id;
    for (std::size_t i = 0; i < name.size() && i < inst.symbol.chars.size(); ++i) {
        inst.symbol.chars[i] = name[i];
    }
    return inst;
}

class IdMapTest : public ::testing::Test {
protected:
    IdMap map;
    FIX::ClientId c1{};
    FIX::ClientId c2{};

    void SetUp() override {
        c1 = map.client_id("CLIENT1");
        c2 = map.client_id("CLIENT2");
    }
};

}  // namespace

// ═════════════════════════════ clienți ═════════════════════════════

TEST_F(IdMapTest, SameCompIdSameClientId) {
    EXPECT_EQ(map.client_id("CLIENT1"), c1);
    EXPECT_EQ(map.client_id("CLIENT2"), c2);
}

TEST_F(IdMapTest, DifferentCompIdsDifferentClientIds) {
    EXPECT_NE(c1, c2);
}

// ClientId-urile sunt dense (0, 1, 2, ...) — pe asta se bazează orders_by_client_
TEST_F(IdMapTest, ClientIdsAreDense) {
    EXPECT_EQ(c1, 0);
    EXPECT_EQ(c2, 1);
    EXPECT_EQ(map.client_id("CLIENT3"), 2);
}

// CompID-ul trebuie copiat: bufferul din care vine mesajul se refolosește
TEST_F(IdMapTest, CompIdIsCopied) {
    std::string buffer = "CLIENT9";
    const FIX::ClientId id = map.client_id(buffer);
    buffer = "XXXXXXX";
    EXPECT_EQ(map.client_id("CLIENT9"), id);
}

// ═════════════════════════════ simboluri ═════════════════════════════

TEST_F(IdMapTest, SymbolLookupBothWays) {
    map.add_symbol(instrument(0, "AAPL"));
    map.add_symbol(instrument(1, "MSFT"));

    EXPECT_EQ(map.symbol_id("AAPL"), 0);
    EXPECT_EQ(map.symbol_id("MSFT"), 1);
    EXPECT_EQ(map.symbol(0), "AAPL");
    EXPECT_EQ(map.symbol(1), "MSFT");
}

TEST_F(IdMapTest, UnknownSymbolIsNullopt) {
    map.add_symbol(instrument(0, "AAPL"));
    EXPECT_EQ(map.symbol_id("TSLA"), std::nullopt);
    EXPECT_EQ(map.symbol_id(""), std::nullopt);
}

// simbolul de 8 caractere (fără '\0' la final) trebuie citit complet
TEST_F(IdMapTest, FullLengthSymbol) {
    map.add_symbol(instrument(0, "ABCDEFGH"));
    EXPECT_EQ(map.symbol_id("ABCDEFGH"), 0);
    EXPECT_EQ(map.symbol(0), "ABCDEFGH");
}

// ═════════════════════════════ add_order / find ═════════════════════════════

TEST_F(IdMapTest, AddOrderThenFind) {
    const auto id = map.add_order(c1, "abc");
    ASSERT_TRUE(id.has_value());
    EXPECT_EQ(map.find(c1, "abc"), *id);
    EXPECT_EQ(map.cl_ord_id(*id), "abc");
}

TEST_F(IdMapTest, OrderIdsStartAtOneAndIncrease) {
    EXPECT_EQ(map.add_order(c1, "a").value(), 1);
    EXPECT_EQ(map.add_order(c1, "b").value(), 2);
    EXPECT_EQ(map.add_order(c2, "a").value(), 3);
}

TEST_F(IdMapTest, DuplicateClOrdIdSameClientIsError) {
    const auto first = map.add_order(c1, "abc");
    ASSERT_TRUE(first.has_value());

    const auto second = map.add_order(c1, "abc");
    ASSERT_FALSE(second.has_value());
    EXPECT_EQ(second.error(), IdMapError::DuplicateClOrdId);

    // ordinul original e neatins
    EXPECT_EQ(map.find(c1, "abc"), *first);
}

// ClOrdID e unic doar per client
TEST_F(IdMapTest, SameClOrdIdDifferentClientsIsOk) {
    const auto a = map.add_order(c1, "abc");
    const auto b = map.add_order(c2, "abc");
    ASSERT_TRUE(a.has_value());
    ASSERT_TRUE(b.has_value());
    EXPECT_NE(*a, *b);
    EXPECT_EQ(map.find(c1, "abc"), *a);
    EXPECT_EQ(map.find(c2, "abc"), *b);
}

// un client nu vede (și deci nu poate anula) ordinele altui client
TEST_F(IdMapTest, ClientCannotFindOtherClientsOrder) {
    ASSERT_TRUE(map.add_order(c1, "abc").has_value());
    EXPECT_EQ(map.find(c2, "abc"), std::nullopt);
}

TEST_F(IdMapTest, FindUnknownIsNullopt) {
    EXPECT_EQ(map.find(c1, "nope"), std::nullopt);
}

TEST_F(IdMapTest, InvalidClientId) {
    for (FIX::ClientId bad : {FIX::ClientId{-1}, FIX::ClientId{2}, FIX::ClientId{1000}}) {
        const auto r = map.add_order(bad, "abc");
        ASSERT_FALSE(r.has_value()) << "client=" << bad;
        EXPECT_EQ(r.error(), IdMapError::InvalidClientId) << "client=" << bad;
        EXPECT_EQ(map.find(bad, "abc"), std::nullopt) << "client=" << bad;
    }
}

// ClOrdID-ul trebuie copiat: string_view-ul din Message pointează în bufferul Framer-ului
TEST_F(IdMapTest, ClOrdIdIsCopied) {
    std::string buffer = "order-1";
    const auto id = map.add_order(c1, buffer);
    ASSERT_TRUE(id.has_value());

    buffer = "XXXXXXX";
    EXPECT_EQ(map.cl_ord_id(*id), "order-1");
    EXPECT_EQ(map.find(c1, "order-1"), *id);
    EXPECT_EQ(map.find(c1, "XXXXXXX"), std::nullopt);
}

// ═════════════════════════════ rename (Replaced) ═════════════════════════════

TEST_F(IdMapTest, RenameMovesLookupToNewClOrdId) {
    const auto id = map.add_order(c1, "old").value();
    map.rename(id, "new");

    EXPECT_EQ(map.find(c1, "old"), std::nullopt);
    EXPECT_EQ(map.find(c1, "new"), id);
    EXPECT_EQ(map.cl_ord_id(id), "new");
}

// după rename, vechiul 11 e liber pentru un ordin nou
TEST_F(IdMapTest, OldClOrdIdIsFreeAfterRename) {
    const auto id = map.add_order(c1, "old").value();
    map.rename(id, "new");

    const auto other = map.add_order(c1, "old");
    ASSERT_TRUE(other.has_value());
    EXPECT_NE(*other, id);
}

TEST_F(IdMapTest, RenameTwice) {
    const auto id = map.add_order(c1, "a").value();
    map.rename(id, "b");
    map.rename(id, "c");

    EXPECT_EQ(map.find(c1, "a"), std::nullopt);
    EXPECT_EQ(map.find(c1, "b"), std::nullopt);
    EXPECT_EQ(map.find(c1, "c"), id);
    EXPECT_EQ(map.cl_ord_id(id), "c");
}

// bug-ul „ordin fantomă”: dacă rename nu actualizează direcția inversă, remove șterge cheia greșită
TEST_F(IdMapTest, RemoveAfterRenameLeavesNoGhost) {
    const auto id = map.add_order(c1, "old").value();
    map.rename(id, "new");
    map.remove(id);

    EXPECT_EQ(map.find(c1, "old"), std::nullopt);
    EXPECT_EQ(map.find(c1, "new"), std::nullopt);
    EXPECT_TRUE(map.add_order(c1, "new").has_value());
}

TEST_F(IdMapTest, RenameDoesNotTouchOtherClient) {
    const auto a = map.add_order(c1, "x").value();
    const auto b = map.add_order(c2, "x").value();
    map.rename(a, "y");

    EXPECT_EQ(map.find(c2, "x"), b);
    EXPECT_EQ(map.cl_ord_id(b), "x");
}

// ═════════════════════════════ remove ═════════════════════════════

TEST_F(IdMapTest, RemoveForgetsBothDirections) {
    const auto id = map.add_order(c1, "abc").value();
    map.remove(id);
    EXPECT_EQ(map.find(c1, "abc"), std::nullopt);
}

// OrderId-urile nu se refolosesc niciodată
TEST_F(IdMapTest, ReAddAfterRemoveGetsNewOrderId) {
    const auto first = map.add_order(c1, "abc").value();
    map.remove(first);

    const auto second = map.add_order(c1, "abc");
    ASSERT_TRUE(second.has_value());
    EXPECT_NE(*second, first);
    EXPECT_GT(*second, first);
}

TEST_F(IdMapTest, RemoveDoesNotTouchOtherOrders) {
    const auto a = map.add_order(c1, "a").value();
    const auto b = map.add_order(c1, "b").value();
    const auto c = map.add_order(c2, "a").value();
    map.remove(a);

    EXPECT_EQ(map.find(c1, "a"), std::nullopt);
    EXPECT_EQ(map.find(c1, "b"), b);
    EXPECT_EQ(map.find(c2, "a"), c);
    EXPECT_EQ(map.cl_ord_id(b), "b");
    EXPECT_EQ(map.cl_ord_id(c), "a");
}

// ═════════════════════════════ reconectare ═════════════════════════════

// același CompID după reconectare → același ClientId → își găsește ordinele vechi (pentru F/G)
TEST_F(IdMapTest, ReconnectKeepsOrders) {
    const auto id = map.add_order(c1, "abc").value();
    const FIX::ClientId again = map.client_id("CLIENT1");
    EXPECT_EQ(again, c1);
    EXPECT_EQ(map.find(again, "abc"), id);
}

// ═════════════════════════════ contracte (assert) ═════════════════════════════
// rulează doar în Debug (în Release, NDEBUG scoate assert-urile)

#ifndef NDEBUG
TEST(IdMapDeathTest, SymbolsMustBeAddedInIdOrder) {
    IdMap map;
    EXPECT_DEATH(map.add_symbol(instrument(1, "AAPL")), "");
}

TEST(IdMapDeathTest, UnknownOrderAsserts) {
    IdMap map;
    EXPECT_DEATH((void)map.cl_ord_id(42), "");
    EXPECT_DEATH(map.remove(42), "");
}
#endif
