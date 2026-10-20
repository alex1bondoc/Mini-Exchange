#include <gtest/gtest.h>
#include "common/fix/framer.hpp"
#include "common/fix/parser.hpp"
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace {

const std::string HEARTBEAT = std::string("8=FIX.4.4\x01" "9=5\x01" "35=0\x01" "10=163\x01");

// un NewOrderSingle valid, cu 9= și 10= calculate
const std::string ORDER = [] {
    std::string body = "35=D\x01" "11=ord-1\x01" "55=AAPL\x01" "54=1\x01" "38=100\x01" "40=2\x01" "44=150.25\x01";
    std::string msg = "8=FIX.4.4\x01" "9=" + std::to_string(body.size()) + "\x01" + body;
    unsigned sum = 0;
    for (unsigned char c : msg) sum += c;
    sum %= 256;
    char cs[4] = {char('0' + sum / 100), char('0' + sum / 10 % 10), char('0' + sum % 10), 0};
    return msg + "10=" + cs + "\x01";
}();

std::span<const char> bytes(std::string_view s) { return {s.data(), s.size()}; }

}  // namespace

TEST(Framer, OneMessageOnePiece) {
    auto f = std::make_unique<FIX::Framer>();
    ASSERT_TRUE(f->append(bytes(HEARTBEAT)));
    auto m = f->next();
    ASSERT_TRUE(m.has_value());
    EXPECT_EQ(*m, HEARTBEAT);
    f->consume(m->size());
    EXPECT_FALSE(f->next().has_value());
    EXPECT_EQ(f->size(), 0u);
}

TEST(Framer, TwoMessagesGlued) {
    auto f = std::make_unique<FIX::Framer>();
    ASSERT_TRUE(f->append(bytes(HEARTBEAT + ORDER)));

    auto first = f->next();
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(*first, HEARTBEAT);
    f->consume(first->size());

    auto second = f->next();
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(*second, ORDER);
    EXPECT_TRUE(FIX::parse(*second).has_value());   // tăiat corect → parser-ul îl acceptă
}

TEST(Framer, SplitInTwo) {
    auto f = std::make_unique<FIX::Framer>();
    ASSERT_TRUE(f->append(bytes(std::string_view(ORDER).substr(0, 20))));
    EXPECT_FALSE(f->next().has_value());            // incomplet: aștepți
    ASSERT_TRUE(f->append(bytes(std::string_view(ORDER).substr(20))));
    auto m = f->next();
    ASSERT_TRUE(m.has_value());
    EXPECT_EQ(*m, ORDER);
}

// cel mai important: dacă trece de asta, merge cu orice împărțire a TCP-ului
TEST(Framer, ByteByByte) {
    auto f = std::make_unique<FIX::Framer>();
    const std::string stream = HEARTBEAT + ORDER;
    std::vector<std::string> received;

    for (char c : stream) {
        ASSERT_TRUE(f->append(std::span<const char>(&c, 1)));
        while (auto m = f->next()) {
            received.emplace_back(*m);
            f->consume(m->size());
        }
        ASSERT_FALSE(f->corrupt());
    }
    ASSERT_EQ(received.size(), 2u);
    EXPECT_EQ(received[0], HEARTBEAT);
    EXPECT_EQ(received[1], ORDER);
}

TEST(Framer, PartialHeaderWaits) {
    auto f = std::make_unique<FIX::Framer>();
    ASSERT_TRUE(f->append(bytes("8=FIX.4.4\x01" "9=")));
    EXPECT_FALSE(f->next().has_value());
    EXPECT_FALSE(f->corrupt());                   // incomplet nu înseamnă stricat
}

TEST(Framer, GarbageIscorrupt) {
    auto f = std::make_unique<FIX::Framer>();
    ASSERT_TRUE(f->append(bytes("hello")));
    EXPECT_FALSE(f->next().has_value());
    EXPECT_TRUE(f->corrupt());
}

TEST(Framer, BadLengthIscorrupt) {
    auto f = std::make_unique<FIX::Framer>();
    ASSERT_TRUE(f->append(bytes("8=FIX.4.4\x01" "9=abc\x01")));
    EXPECT_FALSE(f->next().has_value());
    EXPECT_TRUE(f->corrupt());
}

TEST(Framer, TooMuchData) {
    auto f = std::make_unique<FIX::Framer>();
    const std::string big(70 * 1024, 'x');
    EXPECT_FALSE(f->append(bytes(big)));
}