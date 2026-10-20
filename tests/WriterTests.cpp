#include <cstdint>
#include <gtest/gtest.h>
#include "common/fix/writer.hpp"
#include "common/fix/tags.hpp"
#include <string>
#include <array>

std::string build(FIX::Writer& writer) {
    std::array<char, 512> buffer{};
    const std::size_t n = writer.finish(buffer);
    return std::string(buffer.data(), n);
}

TEST(FixWriter, HeartbeatExactBytes) {
    FIX::Writer w;
    w.field(FIX::tag::MessageType, FIX::msg_type::HeartBeat);

    EXPECT_EQ(build(w), std::string("8=FIX.4.4\x01" "9=5\x01" "35=0\x01" "10=163\x01"));
}

TEST(FixWriter, ExecutionReportExactBytes) {
    FIX::Writer w;
    w.field(FIX::tag::MessageType,  FIX::msg_type::ExecutionReport)   // '8', nu HeartBeat
     .field(FIX::tag::SenderCompId, "EXCH")
     .field(FIX::tag::TargetCompId, "CLIENT1")
     .field(FIX::tag::MsgSeqNum,    int64_t{7})
     .field(FIX::tag::ClOrdId,      "ord1")
     .field(FIX::tag::ExecType,     'F')                              // 150, nu ExecId (17)
     .field(FIX::tag::LastQty,      int64_t{100});                    // 100, nu 101

    EXPECT_EQ(build(w), std::string("8=FIX.4.4\x01" "9=50\x01"
                                    "35=8\x01" "49=EXCH\x01" "56=CLIENT1\x01" "34=7\x01"
                                    "11=ord1\x01" "150=F\x01" "32=100\x01"
                                    "10=135\x01"));
}