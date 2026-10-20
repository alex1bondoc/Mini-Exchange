#include <gtest/gtest.h>
#include "common/fix/parser.hpp"
#include "common/fix/writer.hpp"
#include "common/fix/tags.hpp"

#include <array>
#include <string>
#include <string_view>

namespace {

// Construiește un mesaj complet dintr-un corp, cu 9= și 10= CORECTE.
// Așa fiecare test de eroare strică exact un singur lucru.
std::string make_raw(std::string_view body) {
    std::string msg = "8=FIX.4.4\x01" "9=" + std::to_string(body.size()) + "\x01";
    msg += body;
    unsigned sum = 0;
    for (unsigned char c : msg) sum += c;
    sum %= 256;
    msg += "10=";
    msg += static_cast<char>('0' + sum / 100);
    msg += static_cast<char>('0' + sum / 10 % 10);
    msg += static_cast<char>('0' + sum % 10);
    msg += '\x01';
    return msg;
}

std::string build(FIX::Writer& writer) {
    std::array<char, 512> buffer{};
    return std::string(buffer.data(), writer.finish(buffer));
}

}  // namespace

// ───────────────────────── mesaje valide ─────────────────────────

// NewOrderSingle complet: construit cu writer-ul, citit înapoi, verificat câmp cu câmp
TEST(FixParser, NewOrderSingleRoundTrip) {
    FIX::Writer w;
    w.field(FIX::tag::MessageType,  FIX::msg_type::NewOrderSingle)
     .field(FIX::tag::SenderCompId, "CLIENT1")
     .field(FIX::tag::TargetCompId, "EXCH")
     .field(FIX::tag::MsgSeqNum,    int64_t{42})
     .field(FIX::tag::ClOrdId,      "ord-17")
     .field(FIX::tag::Symbol,       "AAPL")
     .field(FIX::tag::Side,         '1')
     .field(FIX::tag::OrderQty,     int64_t{100})
     .field(FIX::tag::OrderType,    '2')
     .field(FIX::tag::Price,        "150.25")
     .field(FIX::tag::TimeInForce,  '3');
    const std::string raw = build(w);

    auto result = FIX::parse(raw);
    ASSERT_TRUE(result.has_value());

    const FIX::Message& msg = *result;
    EXPECT_EQ(msg.type, FIX::msg_type::NewOrderSingle);
    EXPECT_EQ(msg.get(FIX::tag::MessageType),  "D");
    EXPECT_EQ(msg.get(FIX::tag::SenderCompId), "CLIENT1");
    EXPECT_EQ(msg.get(FIX::tag::TargetCompId), "EXCH");
    EXPECT_EQ(msg.get(FIX::tag::MsgSeqNum),    "42");
    EXPECT_EQ(msg.get(FIX::tag::ClOrdId),      "ord-17");
    EXPECT_EQ(msg.get(FIX::tag::Symbol),       "AAPL");
    EXPECT_EQ(msg.get(FIX::tag::Side),         "1");
    EXPECT_EQ(msg.get(FIX::tag::OrderQty),     "100");
    EXPECT_EQ(msg.get(FIX::tag::OrderType),    "2");
    EXPECT_EQ(msg.get(FIX::tag::Price),        "150.25");
    EXPECT_EQ(msg.get(FIX::tag::TimeInForce),  "3");

    // câmpurile care nu au fost trimise vin goale
    EXPECT_TRUE(msg.get(FIX::tag::StopPx).empty());
    EXPECT_TRUE(msg.get(FIX::tag::Text).empty());
    // tag-uri în afara array-ului nu crapă
    EXPECT_TRUE(msg.get(500).empty());
    EXPECT_TRUE(msg.get(-1).empty());
}

// Heartbeat scris de mână, independent de writer
TEST(FixParser, HandWrittenHeartbeat) {
    const std::string raw = "8=FIX.4.4\x01" "9=5\x01" "35=0\x01" "10=163\x01";
    auto result = FIX::parse(raw);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->type, FIX::msg_type::HeartBeat);
}

// ───────────────────────── câte un test pe eroare ─────────────────────────

TEST(FixParser, BadBeginString) {
    const std::string raw = "8=FIX.4.2\x01" "9=5\x01" "35=0\x01" "10=163\x01";
    auto result = FIX::parse(raw);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), FIX::ParserError::BadBeginString);
}

TEST(FixParser, BadBodyLength) {
    // 9=6, dar corpul are 5 bytes
    const std::string raw = "8=FIX.4.4\x01" "9=6\x01" "35=0\x01" "10=163\x01";
    auto result = FIX::parse(raw);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), FIX::ParserError::BadBodyLength);
}

TEST(FixParser, BadChecksum) {
    // mesaj corect, dar checksum 164 în loc de 163
    const std::string raw = "8=FIX.4.4\x01" "9=5\x01" "35=0\x01" "10=164\x01";
    auto result = FIX::parse(raw);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), FIX::ParserError::BadChecksum);
}

TEST(FixParser, MissingMsgType) {
    // primul câmp e 49, nu 35 (9= și 10= sunt corecte)
    const std::string raw = make_raw("49=EXCH\x01" "35=0\x01");
    auto result = FIX::parse(raw);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), FIX::ParserError::MissingMsgType);
}

TEST(FixParser, Malformed) {
    // al doilea câmp nu are '=' (9= și 10= sunt corecte)
    const std::string raw = make_raw("35=0\x01" "58hello\x01");
    auto result = FIX::parse(raw);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), FIX::ParserError::Malformed);
}