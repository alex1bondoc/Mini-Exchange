#include <gtest/gtest.h>
#include "common/fix/convert.hpp"
#include "common/fix/parser.hpp"
#include "common/fix/writer.hpp"
#include "common/fix/tags.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace {

constexpr std::string_view EXCH   = "MYEXCH";
constexpr std::string_view CLIENT = "CLIENT1";

// 2026-10-26 14:30:05.123 UTC, în nanosecunde de la epoch
constexpr FIX::Timestamp NOW = 1'793'025'005'123'000'000;

std::string format(FIX::Timestamp t) {
    std::array<char, 64> buf{};
    const std::size_t n = FIX::format_timestamp(t, buf);
    return std::string(buf.data(), n);
}

// header + corp → bytes finale; parse-ul îl face fiecare test
template <class Body>
std::string build(char type, FIX::SeqNum seq, Body body) {
    FIX::Writer w;
    FIX::write_header(w, type, EXCH, CLIENT, seq, NOW);
    body(w);
    std::array<char, 1024> buf{};
    return std::string(buf.data(), w.finish(buf));
}

}  // namespace

// ═════════════════════════════ format_timestamp ═════════════════════════════

TEST(FormatTimestamp, Epoch) {
    EXPECT_EQ(format(0), "19700101-00:00:00.000");
}

TEST(FormatTimestamp, KnownDate) {
    EXPECT_EQ(format(NOW), "20261026-14:30:05.123");
}

TEST(FormatTimestamp, LeapDay) {
    EXPECT_EQ(format(1'709'251'199'000'000'000), "20240229-23:59:59.000");
}

// sub-milisecundele se taie, nu se rotunjesc (altfel 59.9999 → 60.000)
TEST(FormatTimestamp, TruncatesBelowMillis) {
    EXPECT_EQ(format(1'709'251'199'999'999'999), "20240229-23:59:59.999");
}

TEST(FormatTimestamp, MillisArePadded) {
    EXPECT_EQ(format(946'684'800'007'000'000), "20000101-00:00:00.007");
}

TEST(FormatTimestamp, BufferTooSmallReturnsZero) {
    std::array<char, 10> small{};
    EXPECT_EQ(FIX::format_timestamp(NOW, small), 0u);
}

// ═════════════════════════════ write_header ═════════════════════════════

TEST(WriteHeader, FieldsAreParsedBack) {
    const std::string raw = build(FIX::msg_type::HeartBeat, 7, [](FIX::Writer&) {});
    auto msg = FIX::parse(raw);
    ASSERT_TRUE(msg.has_value()) << raw;
    EXPECT_EQ(msg->type, FIX::msg_type::HeartBeat);
    EXPECT_EQ(msg->get(FIX::tag::SenderCompId), EXCH);
    EXPECT_EQ(msg->get(FIX::tag::TargetCompId), CLIENT);
    EXPECT_EQ(msg->get(FIX::tag::MsgSeqNum),    "7");
    EXPECT_EQ(msg->get(FIX::tag::SendingTime),  "20261026-14:30:05.123");
}

// ordinea contează: parse cere 35 primul, iar restul e ordinea standard FIX
TEST(WriteHeader, FieldOrder) {
    const std::string raw = build(FIX::msg_type::Logout, 1, [](FIX::Writer&) {});
    const std::string_view expected =
        "\x01" "35=5\x01" "49=MYEXCH\x01" "56=CLIENT1\x01" "34=1\x01" "52=20261026-14:30:05.123\x01";
    EXPECT_NE(raw.find(expected), std::string::npos) << raw;
}

TEST(WriteHeader, LargeSeqNum) {
    const std::string raw = build(FIX::msg_type::HeartBeat, INT64_MAX, [](FIX::Writer&) {});
    auto msg = FIX::parse(raw);
    ASSERT_TRUE(msg.has_value());
    EXPECT_EQ(msg->get(FIX::tag::MsgSeqNum), "9223372036854775807");
}

// ═════════════════════════════ heartbeat ═════════════════════════════

// heartbeat = doar header, fără corp
TEST(WriteHeartbeat, HeaderOnly) {
    const std::string raw = build(FIX::msg_type::HeartBeat, 2, [](FIX::Writer&) {});
    const auto body_end = raw.find("\x01" "10=");
    const auto time_end = raw.find("52=20261026-14:30:05.123\x01");
    ASSERT_NE(time_end, std::string::npos);
    EXPECT_EQ(time_end + std::string_view("52=20261026-14:30:05.123").size(), body_end) << raw;
}

// ═════════════════════════════ write_logon ═════════════════════════════

TEST(WriteLogon, EncryptMethodAndHeartbeat) {
    const std::string raw = build(FIX::msg_type::Logon, 1, [](FIX::Writer& w) { FIX::write_logon(w, 30); });
    auto msg = FIX::parse(raw);
    ASSERT_TRUE(msg.has_value()) << raw;
    EXPECT_EQ(msg->type, FIX::msg_type::Logon);
    EXPECT_EQ(msg->get(FIX::tag::EncryptionMethod), "0");
    EXPECT_EQ(msg->get(FIX::tag::HeartBtInt),       "30");
}

// ═════════════════════════════ write_logout ═════════════════════════════

TEST(WriteLogout, WithText) {
    const std::string raw = build(FIX::msg_type::Logout, 9, [](FIX::Writer& w) {
        FIX::write_logout(w, "sequence gap");
    });
    auto msg = FIX::parse(raw);
    ASSERT_TRUE(msg.has_value()) << raw;
    EXPECT_EQ(msg->type, FIX::msg_type::Logout);
    EXPECT_EQ(msg->get(FIX::tag::Text), "sequence gap");
}

TEST(WriteLogout, EmptyTextIsOmitted) {
    const std::string raw = build(FIX::msg_type::Logout, 9, [](FIX::Writer& w) { FIX::write_logout(w, ""); });
    EXPECT_EQ(raw.find("\x01" "58="), std::string::npos) << raw;
    EXPECT_TRUE(FIX::parse(raw).has_value());
}

// ═════════════════════════════ write_session_reject ═════════════════════════════

TEST(WriteSessionReject, FieldsAreParsedBack) {
    const std::string raw = build(FIX::msg_type::Reject, 4, [](FIX::Writer& w) {
        FIX::write_session_reject(w, 12, FIX::SessionRejectReason::RequiredTagMissing, "missing ClOrdID");
    });
    auto msg = FIX::parse(raw);
    ASSERT_TRUE(msg.has_value()) << raw;
    EXPECT_EQ(msg->type, FIX::msg_type::Reject);
    EXPECT_EQ(msg->get(FIX::tag::RefSeqNum),           "12");
    EXPECT_EQ(msg->get(FIX::tag::SessionRejectReason), "");   // 373 > 199, parserul nu-l reține
    EXPECT_EQ(msg->get(FIX::tag::Text),                "missing ClOrdID");
}

// 373 nu încape în Message::fields, așa că îl verificăm direct în bytes
TEST(WriteSessionReject, ReasonBytes) {
    for (auto [reason, expected] : {
             std::pair{FIX::SessionRejectReason::RequiredTagMissing,  std::string_view("\x01" "373=1\x01")},
             std::pair{FIX::SessionRejectReason::ValueIsIncorrect,    std::string_view("\x01" "373=5\x01")},
             std::pair{FIX::SessionRejectReason::IncorrectDataFormat, std::string_view("\x01" "373=6\x01")},
             std::pair{FIX::SessionRejectReason::InvalidMsgType,      std::string_view("\x01" "373=11\x01")}}) {
        const std::string raw = build(FIX::msg_type::Reject, 1, [&](FIX::Writer& w) {
            FIX::write_session_reject(w, 3, reason, "");
        });
        EXPECT_NE(raw.find(expected), std::string::npos) << raw;
    }
}

TEST(WriteSessionReject, EmptyTextIsOmitted) {
    const std::string raw = build(FIX::msg_type::Reject, 1, [](FIX::Writer& w) {
        FIX::write_session_reject(w, 3, FIX::SessionRejectReason::InvalidMsgType, "");
    });
    EXPECT_EQ(raw.find("\x01" "58="), std::string::npos) << raw;
    EXPECT_TRUE(FIX::parse(raw).has_value());
}

// ═════════════════════════════ header + corp existent ═════════════════════════════

// ExecReport-ul trimis de gateway = write_header + write_exec_report
TEST(WriteHeader, WithExecReportParses) {
    const FIX::ExecReport r{.client = 1, .order = 5, .side = FIX::Side::Buy, .type = FIX::ExecType::New,
                            .filled_qty = 0, .leaves_qty = 100, .last_price = 0, .last_qty = 0,
                            .reason = FIX::RejectReason::None};
    const std::string raw = build(FIX::msg_type::ExecutionReport, 3, [&](FIX::Writer& w) {
        FIX::write_exec_report(w, r, "ord-1", "AAPL", 1);
    });
    auto msg = FIX::parse(raw);
    ASSERT_TRUE(msg.has_value()) << raw;
    EXPECT_EQ(msg->type, FIX::msg_type::ExecutionReport);
    EXPECT_EQ(msg->get(FIX::tag::MsgSeqNum), "3");
    EXPECT_EQ(msg->get(FIX::tag::ClOrdId),   "ord-1");
    EXPECT_EQ(msg->get(FIX::tag::LeavesQty), "100");
}
