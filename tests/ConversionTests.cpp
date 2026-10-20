#include <gtest/gtest.h>
#include "common/fix/convert.hpp"
#include "common/fix/parser.hpp"
#include "common/fix/writer.hpp"
#include "common/fix/tags.hpp"

#include <array>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace FIX {
// gtest afișează erorile ca nume, nu ca bytes
inline void PrintTo(ConvertError e, std::ostream* os) {
    switch (e) {
        case ConvertError::MissingField:  *os << "MissingField";  return;
        case ConvertError::InvalidValue:  *os << "InvalidValue";  return;
        case ConvertError::InvalidNumber: *os << "InvalidNumber"; return;
        case ConvertError::InvalidPrice:  *os << "InvalidPrice";  return;
    }
    *os << "ConvertError(" << static_cast<int>(e) << ")";
}
}  // namespace FIX

namespace {

using FIX::ConvertError;

// .error() pe un expected care are valoare e UB; asta dă nullopt când conversia a reușit
template <typename T>
std::optional<ConvertError> err(const std::expected<T, ConvertError>& e) {
    if (e.has_value()) return std::nullopt;
    return e.error();
}

// Construiește un Message direct, fără parser. Valorile sunt literali, deci trăiesc cât testul.
FIX::Message make_msg(char type, std::initializer_list<std::pair<int, std::string_view>> fields) {
    FIX::Message msg{};
    msg.type = type;
    for (auto [tag, value] : fields) msg.fields[tag] = value;
    return msg;
}

// Un NewOrderSingle Limit valid; fiecare test de eroare strică exact un câmp.
FIX::Message valid_new_order() {
    return make_msg(FIX::msg_type::NewOrderSingle, {
        {FIX::tag::ClOrdId,     "ord-1"},
        {FIX::tag::Symbol,      "AAPL"},
        {FIX::tag::Side,        "1"},
        {FIX::tag::OrderType,   "2"},
        {FIX::tag::TimeInForce, "0"},
        {FIX::tag::OrderQty,    "100"},
        {FIX::tag::Price,       "150.25"},
    });
}

FIX::Message valid_cancel() {
    return make_msg(FIX::msg_type::OrderCancelRequest, {
        {FIX::tag::ClOrdId,     "cxl-1"},
        {FIX::tag::OrigClOrdId, "ord-1"},
        {FIX::tag::Symbol,      "AAPL"},
        {FIX::tag::Side,        "2"},
    });
}

FIX::Message valid_replace() {
    return make_msg(FIX::msg_type::OrderCancelReplaceRequest, {
        {FIX::tag::ClOrdId,     "rpl-1"},
        {FIX::tag::OrigClOrdId, "ord-1"},
        {FIX::tag::Symbol,      "AAPL"},
        {FIX::tag::Side,        "1"},
        {FIX::tag::Price,       "151"},
        {FIX::tag::OrderQty,    "50"},
    });
}

std::string format(FIX::Price ticks) {
    std::array<char, 32> buf{};
    return std::string(buf.data(), FIX::format_price(ticks, buf));
}

// Scrie 35=<type> + ce adaugă fn, apoi parsează înapoi ca să putem verifica câmp cu câmp.
template <typename Fn>
FIX::Message write_and_parse(char type, Fn&& fn, std::string& storage) {
    FIX::Writer w;
    w.field(FIX::tag::MessageType, type);
    fn(w);
    std::array<char, 1024> buf{};
    storage.assign(buf.data(), w.finish(buf));
    auto parsed = FIX::parse(storage);
    EXPECT_TRUE(parsed.has_value());
    return parsed.value_or(FIX::Message{});
}

FIX::ExecReport report(FIX::ExecType type, FIX::Qty filled, FIX::Qty leaves) {
    FIX::ExecReport r{};
    r.order      = 777;
    r.side       = FIX::Side::Buy;
    r.type       = type;
    r.filled_qty = filled;
    r.leaves_qty = leaves;
    return r;
}

}  // namespace

// ═════════════════════════════ parse_price ═════════════════════════════

TEST(ParsePrice, WholeNumber) {
    EXPECT_EQ(FIX::parse_price("150"), 1'500'000);
    EXPECT_EQ(FIX::parse_price("0"),   0);
    EXPECT_EQ(FIX::parse_price("1"),   10'000);
}

TEST(ParsePrice, FractionIsPaddedToFourDecimals) {
    EXPECT_EQ(FIX::parse_price("150.2"),    1'502'000);
    EXPECT_EQ(FIX::parse_price("150.25"),   1'502'500);
    EXPECT_EQ(FIX::parse_price("150.255"),  1'502'550);
    EXPECT_EQ(FIX::parse_price("150.2555"), 1'502'555);
    EXPECT_EQ(FIX::parse_price("0.0001"),   1);
    EXPECT_EQ(FIX::parse_price("0.0"),      0);
}

TEST(ParsePrice, LeadingAndTrailingZeros) {
    EXPECT_EQ(FIX::parse_price("00150.50"), 1'505'000);
    EXPECT_EQ(FIX::parse_price("1.0500"),   10'500);   // "0500" nu e citit ca octal / 500 zecimi
}

TEST(ParsePrice, Empty) {
    EXPECT_EQ(err(FIX::parse_price("")), ConvertError::InvalidPrice);
}

TEST(ParsePrice, MissingWholePart) {
    EXPECT_EQ(err(FIX::parse_price(".5")), ConvertError::InvalidPrice);
    EXPECT_EQ(err(FIX::parse_price(".")),  ConvertError::InvalidPrice);
}

TEST(ParsePrice, TrailingDot) {
    EXPECT_EQ(err(FIX::parse_price("150.")), ConvertError::InvalidPrice);
}

TEST(ParsePrice, TooManyDecimals) {
    EXPECT_EQ(err(FIX::parse_price("1.00001")), ConvertError::InvalidPrice);
    EXPECT_EQ(err(FIX::parse_price("1.00000")), ConvertError::InvalidPrice);   // chiar dacă sunt zerouri
}

TEST(ParsePrice, NonDigitCharacters) {
    for (std::string_view bad : {"-1", "+1", "-0.5", "1.2.3", "1,5", "abc", "12a", "1.2a",
                                 " 1", "1 ", "1. 5", "1e3", "0x10", "1.-5", "1.+5"}) {
        auto p = FIX::parse_price(bad);
        ASSERT_FALSE(p.has_value()) << "'" << bad << "' a fost acceptat ca " << *p;
        EXPECT_EQ(err(p), ConvertError::InvalidPrice) << bad;
    }
}

TEST(ParsePrice, WholePartDoesNotFitInt64) {
    EXPECT_EQ(err(FIX::parse_price("99999999999999999999")), ConvertError::InvalidPrice);
}

// Cel mai mare preț reprezentabil: INT64_MAX ticks = 922337203685477.5807
TEST(ParsePrice, LargestRepresentable) {
    EXPECT_EQ(FIX::parse_price("922337203685477.5807"), INT64_MAX);
}

// whole încape în int64, dar whole * 10'000 nu → trebuie respins, nu overflow (UB).
TEST(ParsePrice, ScaledValueOverflows) {
    EXPECT_EQ(err(FIX::parse_price("922337203685478")),     ConvertError::InvalidPrice);
    EXPECT_EQ(err(FIX::parse_price("9223372036854775807")), ConvertError::InvalidPrice);
}

// whole * 10'000 încape, dar + frac depășește INT64_MAX.
TEST(ParsePrice, ScaledPlusFractionOverflows) {
    EXPECT_EQ(err(FIX::parse_price("922337203685477.5808")), ConvertError::InvalidPrice);
}

// ═════════════════════════════ format_price ═════════════════════════════

TEST(FormatPrice, WholeNumberHasNoDot) {
    EXPECT_EQ(format(1'500'000), "150");
    EXPECT_EQ(format(0),         "0");
    EXPECT_EQ(format(10'000),    "1");
}

TEST(FormatPrice, TrailingZerosAreStripped) {
    EXPECT_EQ(format(1'502'500), "150.25");
    EXPECT_EQ(format(1'502'000), "150.2");
    EXPECT_EQ(format(1'502'550), "150.255");
    EXPECT_EQ(format(1'502'555), "150.2555");
}

TEST(FormatPrice, LeadingFractionZerosAreKept) {
    EXPECT_EQ(format(1),      "0.0001");
    EXPECT_EQ(format(10),     "0.001");
    EXPECT_EQ(format(10'050), "1.005");
}

TEST(FormatPrice, Int64Max) {
    EXPECT_EQ(format(INT64_MAX), "922337203685477.5807");
}

TEST(FormatPrice, BufferTooSmallReturnsZeroAndWritesNothing) {
    std::array<char, 5> buf{'x', 'x', 'x', 'x', 'x'};
    EXPECT_EQ(FIX::format_price(1'502'500, buf), 0u);   // "150.25" are 6 caractere
    for (char c : buf) EXPECT_EQ(c, 'x');
}

TEST(FormatPrice, BufferExactSize) {
    std::array<char, 6> buf{};
    ASSERT_EQ(FIX::format_price(1'502'500, buf), 6u);
    EXPECT_EQ(std::string_view(buf.data(), 6), "150.25");
}

TEST(FormatPrice, EmptyBuffer) {
    EXPECT_EQ(FIX::format_price(1, std::span<char>{}), 0u);
}

// format → parse trebuie să dea înapoi exact aceleași ticks
TEST(FormatPrice, RoundTripWithParse) {
    for (FIX::Price ticks : {FIX::Price{0}, FIX::Price{1}, FIX::Price{9'999}, FIX::Price{10'000},
                             FIX::Price{10'001}, FIX::Price{1'502'500}, FIX::Price{123'456'789}, INT64_MAX}) {
        EXPECT_EQ(FIX::parse_price(format(ticks)), ticks) << format(ticks);
    }
}

// ═════════════════════════════ decode_new_order ═════════════════════════════

TEST(DecodeNewOrder, ValidLimit) {
    auto r = FIX::decode_new_order(valid_new_order());
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->cl_ord_id,  "ord-1");
    EXPECT_EQ(r->symbol,     "AAPL");
    EXPECT_EQ(r->side,       FIX::Side::Buy);
    EXPECT_EQ(r->type,       FIX::OrderType::Limit);
    EXPECT_EQ(r->tif,        FIX::TimeInForce::DAY);
    EXPECT_FALSE(r->post_only);
    EXPECT_EQ(r->price,      1'502'500);
    EXPECT_EQ(r->stop_price, 0);
    EXPECT_EQ(r->qty,        100);
}

// String-urile din rezultat trebuie să pointeze în mesaj, nu în copii temporare
TEST(DecodeNewOrder, ViewsPointIntoMessage) {
    const FIX::Message msg = valid_new_order();
    auto r = FIX::decode_new_order(msg);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->cl_ord_id.data(), msg.get(FIX::tag::ClOrdId).data());
    EXPECT_EQ(r->symbol.data(),    msg.get(FIX::tag::Symbol).data());
}

TEST(DecodeNewOrder, EndToEndThroughParser) {
    FIX::Writer w;
    w.field(FIX::tag::MessageType, FIX::msg_type::NewOrderSingle)
     .field(FIX::tag::ClOrdId,     "ord-17")
     .field(FIX::tag::Symbol,      "MSFT")
     .field(FIX::tag::Side,        '2')
     .field(FIX::tag::OrderQty,    int64_t{250})
     .field(FIX::tag::OrderType,   '4')
     .field(FIX::tag::Price,       "99.5")
     .field(FIX::tag::StopPx,      "100")
     .field(FIX::tag::TimeInForce, '4')
     .field(FIX::tag::ExecInst,    '6');
    std::array<char, 512> buf{};
    const std::string raw(buf.data(), w.finish(buf));

    auto msg = FIX::parse(raw);
    ASSERT_TRUE(msg.has_value());
    auto r = FIX::decode_new_order(*msg);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->cl_ord_id,  "ord-17");
    EXPECT_EQ(r->symbol,     "MSFT");
    EXPECT_EQ(r->side,       FIX::Side::Sell);
    EXPECT_EQ(r->type,       FIX::OrderType::StopLimit);
    EXPECT_EQ(r->tif,        FIX::TimeInForce::FOK);
    EXPECT_TRUE(r->post_only);
    EXPECT_EQ(r->price,      995'000);
    EXPECT_EQ(r->stop_price, 1'000'000);
    EXPECT_EQ(r->qty,        250);
}

// ── câmpuri obligatorii lipsă ──

TEST(DecodeNewOrder, MissingClOrdId) {
    auto msg = valid_new_order();
    msg.fields[FIX::tag::ClOrdId] = {};
    EXPECT_EQ(err(FIX::decode_new_order(msg)), ConvertError::MissingField);
}

TEST(DecodeNewOrder, MissingSymbol) {
    auto msg = valid_new_order();
    msg.fields[FIX::tag::Symbol] = {};
    EXPECT_EQ(err(FIX::decode_new_order(msg)), ConvertError::MissingField);
}

TEST(DecodeNewOrder, MissingSide) {
    auto msg = valid_new_order();
    msg.fields[FIX::tag::Side] = {};
    EXPECT_EQ(err(FIX::decode_new_order(msg)), ConvertError::MissingField);
}

TEST(DecodeNewOrder, MissingOrderType) {
    auto msg = valid_new_order();
    msg.fields[FIX::tag::OrderType] = {};
    EXPECT_EQ(err(FIX::decode_new_order(msg)), ConvertError::MissingField);
}

TEST(DecodeNewOrder, MissingQty) {
    auto msg = valid_new_order();
    msg.fields[FIX::tag::OrderQty] = {};
    EXPECT_EQ(err(FIX::decode_new_order(msg)), ConvertError::MissingField);
}

TEST(DecodeNewOrder, LimitWithoutPrice) {
    auto msg = valid_new_order();
    msg.fields[FIX::tag::Price] = {};
    EXPECT_EQ(err(FIX::decode_new_order(msg)), ConvertError::MissingField);
}

TEST(DecodeNewOrder, StopWithoutStopPx) {
    auto msg = valid_new_order();
    msg.fields[FIX::tag::OrderType] = "3";
    EXPECT_EQ(err(FIX::decode_new_order(msg)), ConvertError::MissingField);
}

TEST(DecodeNewOrder, StopLimitWithoutStopPx) {
    auto msg = valid_new_order();
    msg.fields[FIX::tag::OrderType] = "4";
    EXPECT_EQ(err(FIX::decode_new_order(msg)), ConvertError::MissingField);
}

TEST(DecodeNewOrder, StopLimitWithoutPrice) {
    auto msg = valid_new_order();
    msg.fields[FIX::tag::OrderType] = "4";
    msg.fields[FIX::tag::Price]     = {};
    msg.fields[FIX::tag::StopPx]    = "100";
    EXPECT_EQ(err(FIX::decode_new_order(msg)), ConvertError::MissingField);
}

// Mesaj complet gol: primul câmp verificat dă MissingField
TEST(DecodeNewOrder, EmptyMessage) {
    EXPECT_EQ(err(FIX::decode_new_order(FIX::Message{})), ConvertError::MissingField);
}

// ── valori invalide ──

TEST(DecodeNewOrder, InvalidSide) {
    for (std::string_view bad : {"0", "3", "B", "11", " 1", "1 ", "buy"}) {
        auto msg = valid_new_order();
        msg.fields[FIX::tag::Side] = bad;
        auto r = FIX::decode_new_order(msg);
        ASSERT_FALSE(r.has_value()) << bad;
        EXPECT_EQ(err(r), ConvertError::InvalidValue) << bad;
    }
}

TEST(DecodeNewOrder, InvalidOrderType) {
    for (std::string_view bad : {"0", "5", "P", "22", "limit"}) {
        auto msg = valid_new_order();
        msg.fields[FIX::tag::OrderType] = bad;
        auto r = FIX::decode_new_order(msg);
        ASSERT_FALSE(r.has_value()) << bad;
        EXPECT_EQ(err(r), ConvertError::InvalidValue) << bad;
    }
}

TEST(DecodeNewOrder, InvalidTimeInForce) {
    // 1 = GTC, 2 = OPG, 6 = GTD: există în FIX, dar nu le suportăm
    for (std::string_view bad : {"1", "2", "5", "6", "7", "00", "X"}) {
        auto msg = valid_new_order();
        msg.fields[FIX::tag::TimeInForce] = bad;
        auto r = FIX::decode_new_order(msg);
        ASSERT_FALSE(r.has_value()) << bad;
        EXPECT_EQ(err(r), ConvertError::InvalidValue) << bad;
    }
}

TEST(DecodeNewOrder, InvalidQty) {
    for (std::string_view bad : {"0", "-1", "-100", "+5", " 5", "5 ", "5a", "1.5", "abc",
                                 "99999999999999999999"}) {
        auto msg = valid_new_order();
        msg.fields[FIX::tag::OrderQty] = bad;
        auto r = FIX::decode_new_order(msg);
        ASSERT_FALSE(r.has_value()) << bad;
        EXPECT_EQ(err(r), ConvertError::InvalidNumber) << bad;
    }
}

TEST(DecodeNewOrder, InvalidPrice) {
    for (std::string_view bad : {"-1", "abc", "1.", ".5", "1.00001"}) {
        auto msg = valid_new_order();
        msg.fields[FIX::tag::Price] = bad;
        auto r = FIX::decode_new_order(msg);
        ASSERT_FALSE(r.has_value()) << bad;
        EXPECT_EQ(err(r), ConvertError::InvalidPrice) << bad;
    }
}

TEST(DecodeNewOrder, InvalidStopPx) {
    auto msg = valid_new_order();
    msg.fields[FIX::tag::OrderType] = "3";
    msg.fields[FIX::tag::StopPx]    = "abc";
    EXPECT_EQ(err(FIX::decode_new_order(msg)), ConvertError::InvalidPrice);
}

// ── valori acceptate ──

TEST(DecodeNewOrder, SellSide) {
    auto msg = valid_new_order();
    msg.fields[FIX::tag::Side] = "2";
    EXPECT_EQ(FIX::decode_new_order(msg)->side, FIX::Side::Sell);
}

TEST(DecodeNewOrder, TimeInForceValues) {
    auto msg = valid_new_order();

    msg.fields[FIX::tag::TimeInForce] = {};
    EXPECT_EQ(FIX::decode_new_order(msg)->tif, FIX::TimeInForce::DAY);   // lipsă → Day implicit
    msg.fields[FIX::tag::TimeInForce] = "0";
    EXPECT_EQ(FIX::decode_new_order(msg)->tif, FIX::TimeInForce::DAY);
    msg.fields[FIX::tag::TimeInForce] = "3";
    EXPECT_EQ(FIX::decode_new_order(msg)->tif, FIX::TimeInForce::FAK);
    msg.fields[FIX::tag::TimeInForce] = "4";
    EXPECT_EQ(FIX::decode_new_order(msg)->tif, FIX::TimeInForce::FOK);
}

// Market nu are nevoie de preț; dacă vine totuși, e ignorat (chiar și invalid)
TEST(DecodeNewOrder, MarketIgnoresPrice) {
    auto msg = valid_new_order();
    msg.fields[FIX::tag::OrderType] = "1";
    msg.fields[FIX::tag::Price]     = {};
    auto r = FIX::decode_new_order(msg);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->type,       FIX::OrderType::Market);
    EXPECT_EQ(r->price,      0);
    EXPECT_EQ(r->stop_price, 0);

    msg.fields[FIX::tag::Price]  = "garbage";
    msg.fields[FIX::tag::StopPx] = "garbage";
    r = FIX::decode_new_order(msg);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->price,      0);
    EXPECT_EQ(r->stop_price, 0);
}

// Stop ia doar StopPx; Price-ul e ignorat
TEST(DecodeNewOrder, StopIgnoresPrice) {
    auto msg = valid_new_order();
    msg.fields[FIX::tag::OrderType] = "3";
    msg.fields[FIX::tag::Price]     = "garbage";
    msg.fields[FIX::tag::StopPx]    = "149.5";
    auto r = FIX::decode_new_order(msg);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->type,       FIX::OrderType::Stop);
    EXPECT_EQ(r->price,      0);
    EXPECT_EQ(r->stop_price, 1'495'000);
}

// Limit ia doar Price; StopPx e ignorat
TEST(DecodeNewOrder, LimitIgnoresStopPx) {
    auto msg = valid_new_order();
    msg.fields[FIX::tag::StopPx] = "garbage";
    auto r = FIX::decode_new_order(msg);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->stop_price, 0);
}

TEST(DecodeNewOrder, StopLimitTakesBothPrices) {
    auto msg = valid_new_order();
    msg.fields[FIX::tag::OrderType] = "4";
    msg.fields[FIX::tag::StopPx]    = "151";
    auto r = FIX::decode_new_order(msg);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->price,      1'502'500);
    EXPECT_EQ(r->stop_price, 1'510'000);
}

// ExecInst e multi-valoare separată prin spațiu; post-only = conține '6'
TEST(DecodeNewOrder, PostOnly) {
    auto msg = valid_new_order();
    for (std::string_view on : {"6", "G 6", "6 G"}) {
        msg.fields[FIX::tag::ExecInst] = on;
        EXPECT_TRUE(FIX::decode_new_order(msg)->post_only) << on;
    }
    for (std::string_view off : {"", "G", "1 2"}) {
        msg.fields[FIX::tag::ExecInst] = off;
        EXPECT_FALSE(FIX::decode_new_order(msg)->post_only) << off;
    }
}

TEST(DecodeNewOrder, LargeQty) {
    auto msg = valid_new_order();
    msg.fields[FIX::tag::OrderQty] = "9223372036854775807";
    EXPECT_EQ(FIX::decode_new_order(msg)->qty, INT64_MAX);
}

// Când sunt mai multe erori, prima în ordinea verificărilor câștigă
TEST(DecodeNewOrder, FirstErrorWins) {
    auto msg = valid_new_order();
    msg.fields[FIX::tag::Symbol]   = {};      // MissingField
    msg.fields[FIX::tag::Side]     = "9";     // InvalidValue
    msg.fields[FIX::tag::OrderQty] = "-1";    // InvalidNumber
    EXPECT_EQ(err(FIX::decode_new_order(msg)), ConvertError::MissingField);
}

// ═════════════════════════════ decode_cancel ═════════════════════════════

TEST(DecodeCancel, Valid) {
    auto r = FIX::decode_cancel(valid_cancel());
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->cl_ord_id,      "cxl-1");
    EXPECT_EQ(r->orig_cl_ord_id, "ord-1");
    EXPECT_EQ(r->symbol,         "AAPL");
    EXPECT_EQ(r->side,           FIX::Side::Sell);
}

TEST(DecodeCancel, BuySide) {
    auto msg = valid_cancel();
    msg.fields[FIX::tag::Side] = "1";
    EXPECT_EQ(FIX::decode_cancel(msg)->side, FIX::Side::Buy);
}

TEST(DecodeCancel, MissingFields) {
    for (int tag : {FIX::tag::ClOrdId, FIX::tag::OrigClOrdId, FIX::tag::Symbol, FIX::tag::Side}) {
        auto msg = valid_cancel();
        msg.fields[tag] = {};
        auto r = FIX::decode_cancel(msg);
        ASSERT_FALSE(r.has_value()) << "tag " << tag;
        EXPECT_EQ(err(r), ConvertError::MissingField) << "tag " << tag;
    }
}

TEST(DecodeCancel, InvalidSide) {
    for (std::string_view bad : {"0", "3", "12", "x"}) {
        auto msg = valid_cancel();
        msg.fields[FIX::tag::Side] = bad;
        EXPECT_EQ(err(FIX::decode_cancel(msg)), ConvertError::InvalidValue) << bad;
    }
}

// Câmpuri care nu țin de cancel nu trebuie să-l strice
TEST(DecodeCancel, IgnoresUnrelatedFields) {
    auto msg = valid_cancel();
    msg.fields[FIX::tag::Price]    = "garbage";
    msg.fields[FIX::tag::OrderQty] = "-5";
    EXPECT_TRUE(FIX::decode_cancel(msg).has_value());
}

TEST(DecodeCancel, EndToEndThroughParser) {
    FIX::Writer w;
    w.field(FIX::tag::MessageType, FIX::msg_type::OrderCancelRequest)
     .field(FIX::tag::ClOrdId,     "cxl-9")
     .field(FIX::tag::OrigClOrdId, "ord-9")
     .field(FIX::tag::Symbol,      "TSLA")
     .field(FIX::tag::Side,        '1');
    std::array<char, 512> buf{};
    const std::string raw(buf.data(), w.finish(buf));

    auto msg = FIX::parse(raw);
    ASSERT_TRUE(msg.has_value());
    auto r = FIX::decode_cancel(*msg);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->cl_ord_id,      "cxl-9");
    EXPECT_EQ(r->orig_cl_ord_id, "ord-9");
    EXPECT_EQ(r->symbol,         "TSLA");
    EXPECT_EQ(r->side,           FIX::Side::Buy);
}

// ═════════════════════════════ decode_replace ═════════════════════════════

// Un OrderCancelReplaceRequest real are 35=G
TEST(DecodeReplace, Valid) {
    auto r = FIX::decode_replace(valid_replace());
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->cl_ord_id,      "rpl-1");
    EXPECT_EQ(r->orig_cl_ord_id, "ord-1");
    EXPECT_EQ(r->symbol,         "AAPL");
    EXPECT_EQ(r->side,           FIX::Side::Buy);
    EXPECT_EQ(r->new_price,      1'510'000);
    EXPECT_EQ(r->new_qty,        50);
}

TEST(DecodeReplace, EndToEndThroughParser) {
    FIX::Writer w;
    w.field(FIX::tag::MessageType, FIX::msg_type::OrderCancelReplaceRequest)
     .field(FIX::tag::ClOrdId,     "rpl-2")
     .field(FIX::tag::OrigClOrdId, "ord-2")
     .field(FIX::tag::Symbol,      "AAPL")
     .field(FIX::tag::Side,        '2')
     .field(FIX::tag::Price,       "10.0001")
     .field(FIX::tag::OrderQty,    int64_t{7});
    std::array<char, 512> buf{};
    const std::string raw(buf.data(), w.finish(buf));

    auto msg = FIX::parse(raw);
    ASSERT_TRUE(msg.has_value());
    auto r = FIX::decode_replace(*msg);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->side,      FIX::Side::Sell);
    EXPECT_EQ(r->new_price, 100'001);
    EXPECT_EQ(r->new_qty,   7);
}

TEST(DecodeReplace, WrongMessageType) {
    for (char bad : {FIX::msg_type::NewOrderSingle, FIX::msg_type::OrderCancelRequest,
                     FIX::msg_type::HeartBeat, '\0'}) {
        auto msg = valid_replace();
        msg.type = bad;
        auto r = FIX::decode_replace(msg);
        ASSERT_FALSE(r.has_value()) << "35=" << bad;
        EXPECT_EQ(err(r), ConvertError::InvalidValue) << "35=" << bad;
    }
}

TEST(DecodeReplace, MissingFields) {
    for (int tag : {FIX::tag::ClOrdId, FIX::tag::OrigClOrdId, FIX::tag::Symbol,
                    FIX::tag::Side, FIX::tag::Price, FIX::tag::OrderQty}) {
        auto msg = valid_replace();
        msg.fields[tag] = {};
        auto r = FIX::decode_replace(msg);
        ASSERT_FALSE(r.has_value()) << "tag " << tag;
        EXPECT_EQ(err(r), ConvertError::MissingField) << "tag " << tag;
    }
}

TEST(DecodeReplace, InvalidSide) {
    for (std::string_view bad : {"0", "3", "x"}) {
        auto msg = valid_replace();
        msg.fields[FIX::tag::Side] = bad;
        EXPECT_EQ(err(FIX::decode_replace(msg)), ConvertError::InvalidValue) << bad;
    }
}

TEST(DecodeReplace, InvalidPrice) {
    for (std::string_view bad : {"-1", "1.", ".1", "1.23456", "x"}) {
        auto msg = valid_replace();
        msg.fields[FIX::tag::Price] = bad;
        EXPECT_EQ(err(FIX::decode_replace(msg)), ConvertError::InvalidPrice) << bad;
    }
}

TEST(DecodeReplace, InvalidQty) {
    for (std::string_view bad : {"0", "-3", "+3", "3x", "3.0", "99999999999999999999"}) {
        auto msg = valid_replace();
        msg.fields[FIX::tag::OrderQty] = bad;
        EXPECT_EQ(err(FIX::decode_replace(msg)), ConvertError::InvalidNumber) << bad;
    }
}

// ═════════════════════════════ helpers pentru ieșire ═════════════════════════════

TEST(ConvertHelpers, ExecTypeChar) {
    EXPECT_EQ(FIX::exec_type_char(FIX::ExecType::New),      '0');
    EXPECT_EQ(FIX::exec_type_char(FIX::ExecType::Trade),    'F');
    EXPECT_EQ(FIX::exec_type_char(FIX::ExecType::Canceled), '4');
    EXPECT_EQ(FIX::exec_type_char(FIX::ExecType::Replaced), '5');
    EXPECT_EQ(FIX::exec_type_char(FIX::ExecType::Rejected), '8');
}

TEST(ConvertHelpers, SideChar) {
    EXPECT_EQ(FIX::side_char(FIX::Side::Buy),  '1');
    EXPECT_EQ(FIX::side_char(FIX::Side::Sell), '2');
}

// side_char trebuie să fie inversul parsării din decode_*
TEST(ConvertHelpers, SideCharRoundTrip) {
    for (FIX::Side side : {FIX::Side::Buy, FIX::Side::Sell}) {
        const char c[] = {FIX::side_char(side), '\0'};
        auto msg = valid_cancel();
        msg.fields[FIX::tag::Side] = c;
        EXPECT_EQ(FIX::decode_cancel(msg)->side, side);
    }
}

TEST(ConvertHelpers, OrdStatus) {
    using FIX::ExecType;
    EXPECT_EQ(FIX::ord_status_char(report(ExecType::New,      0,  100)), '0');   // New
    EXPECT_EQ(FIX::ord_status_char(report(ExecType::Trade,    40, 60)),  '1');   // PartiallyFilled
    EXPECT_EQ(FIX::ord_status_char(report(ExecType::Trade,    100, 0)),  '2');   // Filled
    EXPECT_EQ(FIX::ord_status_char(report(ExecType::Replaced, 0,  50)),  '0');
    EXPECT_EQ(FIX::ord_status_char(report(ExecType::Replaced, 10, 40)),  '1');
    EXPECT_EQ(FIX::ord_status_char(report(ExecType::Canceled, 0,  0)),   '4');
    EXPECT_EQ(FIX::ord_status_char(report(ExecType::Canceled, 40, 0)),   '4');   // cancel după fill parțial
    EXPECT_EQ(FIX::ord_status_char(report(ExecType::Rejected, 0,  0)),   '8');
    EXPECT_EQ(FIX::ord_status_char(report(ExecType::Rejected, 100, 0)),  '8');   // reject câștigă mereu
}

TEST(ConvertHelpers, RejectText) {
    using FIX::RejectReason;
    EXPECT_TRUE(FIX::reject_text(RejectReason::None).empty());
    for (RejectReason r : {RejectReason::InvalidPrice, RejectReason::OutsideBand, RejectReason::InvalidQty,
                           RejectReason::UnknownOrder, RejectReason::PostOnlyWouldCross,
                           RejectReason::FokNotFilled, RejectReason::InvalidCombination}) {
        EXPECT_FALSE(FIX::reject_text(r).empty()) << static_cast<int>(r);
    }
    EXPECT_EQ(FIX::reject_text(RejectReason::PostOnlyWouldCross), "post-only would cross");
}

// ═════════════════════════════ write_exec_report ═════════════════════════════

TEST(WriteExecReport, New) {
    std::string raw;
    auto r = report(FIX::ExecType::New, 0, 100);
    auto msg = write_and_parse(FIX::msg_type::ExecutionReport,
        [&](FIX::Writer& w) { FIX::write_exec_report(w, r, "ord-1", "AAPL", 5); }, raw);

    EXPECT_EQ(msg.get(FIX::tag::OrderId),   "777");
    EXPECT_EQ(msg.get(FIX::tag::ClOrdId),   "ord-1");
    EXPECT_EQ(msg.get(FIX::tag::ExecId),    "5");
    EXPECT_EQ(msg.get(FIX::tag::ExecType),  "0");
    EXPECT_EQ(msg.get(FIX::tag::OrdStatus), "0");
    EXPECT_EQ(msg.get(FIX::tag::Symbol),    "AAPL");
    EXPECT_EQ(msg.get(FIX::tag::Side),      "1");
    EXPECT_EQ(msg.get(FIX::tag::LeavesQty), "100");
    EXPECT_EQ(msg.get(FIX::tag::CumQty),    "0");
    // fără câmpuri de trade / reject
    EXPECT_TRUE(msg.get(FIX::tag::LastPx).empty());
    EXPECT_TRUE(msg.get(FIX::tag::LastQty).empty());
    EXPECT_TRUE(msg.get(FIX::tag::Text).empty());
}

TEST(WriteExecReport, ExactBytes) {
    FIX::Writer w;
    w.field(FIX::tag::MessageType, FIX::msg_type::ExecutionReport);
    FIX::write_exec_report(w, report(FIX::ExecType::New, 0, 100), "o1", "AAPL", 1);
    std::array<char, 512> buf{};
    const std::string raw(buf.data(), w.finish(buf));
    const std::string_view body = std::string_view(raw).substr(raw.find("35="));
    EXPECT_TRUE(body.starts_with("35=8\x01" "37=777\x01" "11=o1\x01" "17=1\x01" "150=0\x01"
                                 "39=0\x01" "55=AAPL\x01" "54=1\x01" "151=100\x01" "14=0\x01" "10="))
        << body;
}

TEST(WriteExecReport, PartialTrade) {
    std::string raw;
    auto r = report(FIX::ExecType::Trade, 40, 60);
    r.side       = FIX::Side::Sell;
    r.last_price = 1'502'500;
    r.last_qty   = 40;
    auto msg = write_and_parse(FIX::msg_type::ExecutionReport,
        [&](FIX::Writer& w) { FIX::write_exec_report(w, r, "ord-1", "AAPL", 6); }, raw);

    EXPECT_EQ(msg.get(FIX::tag::ExecType),  "F");
    EXPECT_EQ(msg.get(FIX::tag::OrdStatus), "1");
    EXPECT_EQ(msg.get(FIX::tag::Side),      "2");
    EXPECT_EQ(msg.get(FIX::tag::LastPx),    "150.25");
    EXPECT_EQ(msg.get(FIX::tag::LastQty),   "40");
    EXPECT_EQ(msg.get(FIX::tag::LeavesQty), "60");
    EXPECT_EQ(msg.get(FIX::tag::CumQty),    "40");
    EXPECT_TRUE(msg.get(FIX::tag::Text).empty());

    // LastPx trebuie să se poată citi înapoi cu parse_price
    EXPECT_EQ(FIX::parse_price(msg.get(FIX::tag::LastPx)), r.last_price);
}

TEST(WriteExecReport, FullTradeWholePrice) {
    std::string raw;
    auto r = report(FIX::ExecType::Trade, 100, 0);
    r.last_price = 1'500'000;
    r.last_qty   = 60;
    auto msg = write_and_parse(FIX::msg_type::ExecutionReport,
        [&](FIX::Writer& w) { FIX::write_exec_report(w, r, "ord-1", "AAPL", 7); }, raw);

    EXPECT_EQ(msg.get(FIX::tag::OrdStatus), "2");
    EXPECT_EQ(msg.get(FIX::tag::LastPx),    "150");
    EXPECT_EQ(msg.get(FIX::tag::LastQty),   "60");
}

TEST(WriteExecReport, Rejected) {
    std::string raw;
    auto r = report(FIX::ExecType::Rejected, 0, 0);
    r.reason = FIX::RejectReason::FokNotFilled;
    auto msg = write_and_parse(FIX::msg_type::ExecutionReport,
        [&](FIX::Writer& w) { FIX::write_exec_report(w, r, "ord-1", "AAPL", 8); }, raw);

    EXPECT_EQ(msg.get(FIX::tag::ExecType),  "8");
    EXPECT_EQ(msg.get(FIX::tag::OrdStatus), "8");
    EXPECT_EQ(msg.get(FIX::tag::Text),      FIX::reject_text(FIX::RejectReason::FokNotFilled));
    EXPECT_TRUE(msg.get(FIX::tag::LastPx).empty());
}

TEST(WriteExecReport, CanceledAndReplaced) {
    std::string raw;
    auto msg = write_and_parse(FIX::msg_type::ExecutionReport,
        [&](FIX::Writer& w) { FIX::write_exec_report(w, report(FIX::ExecType::Canceled, 30, 0), "c", "AAPL", 9); }, raw);
    EXPECT_EQ(msg.get(FIX::tag::ExecType),  "4");
    EXPECT_EQ(msg.get(FIX::tag::OrdStatus), "4");
    EXPECT_EQ(msg.get(FIX::tag::CumQty),    "30");

    std::string raw2;
    msg = write_and_parse(FIX::msg_type::ExecutionReport,
        [&](FIX::Writer& w) { FIX::write_exec_report(w, report(FIX::ExecType::Replaced, 0, 50), "r", "AAPL", 10); }, raw2);
    EXPECT_EQ(msg.get(FIX::tag::ExecType),  "5");
    EXPECT_EQ(msg.get(FIX::tag::OrdStatus), "0");
    EXPECT_EQ(msg.get(FIX::tag::LeavesQty), "50");
}

TEST(WriteExecReport, LargeIds) {
    std::string raw;
    auto r = report(FIX::ExecType::New, 0, INT64_MAX);
    r.order = INT64_MAX;
    auto msg = write_and_parse(FIX::msg_type::ExecutionReport,
        [&](FIX::Writer& w) { FIX::write_exec_report(w, r, "ord-1", "AAPL", INT64_MAX); }, raw);
    EXPECT_EQ(msg.get(FIX::tag::OrderId),   "9223372036854775807");
    EXPECT_EQ(msg.get(FIX::tag::ExecId),    "9223372036854775807");
    EXPECT_EQ(msg.get(FIX::tag::LeavesQty), "9223372036854775807");
}

// ═════════════════════════════ write_cancel_reject ═════════════════════════════

TEST(WriteCancelReject, WithText) {
    std::string raw;
    auto msg = write_and_parse(FIX::msg_type::OrderCancelReject, [](FIX::Writer& w) {
        FIX::write_cancel_reject(w, "NONE", "cxl-1", "ord-1", '8',
                                 FIX::CxlRejResponseTo::Cancel, FIX::CxlRejReason::UnknownOrder,
                                 "unknown order");
    }, raw);

    EXPECT_EQ(msg.type, FIX::msg_type::OrderCancelReject);
    EXPECT_EQ(msg.get(FIX::tag::OrderId),          "NONE");
    EXPECT_EQ(msg.get(FIX::tag::ClOrdId),          "cxl-1");
    EXPECT_EQ(msg.get(FIX::tag::OrigClOrdId),      "ord-1");
    EXPECT_EQ(msg.get(FIX::tag::OrdStatus),        "8");
    EXPECT_EQ(msg.get(FIX::tag::CxlRejResponseTo), "");   // 434 > 199, parserul nu-l reține
    EXPECT_EQ(msg.get(FIX::tag::CxlRejReason),     "1");
    EXPECT_EQ(msg.get(FIX::tag::Text),             "unknown order");
}

// 434 nu încape în Message::fields, așa că îl verificăm direct în bytes
TEST(WriteCancelReject, ResponseToBytes) {
    for (auto [to, expected] : {std::pair{FIX::CxlRejResponseTo::Cancel,  std::string_view("434=1\x01")},
                                std::pair{FIX::CxlRejResponseTo::Replace, std::string_view("434=2\x01")}}) {
        FIX::Writer w;
        w.field(FIX::tag::MessageType, FIX::msg_type::OrderCancelReject);
        FIX::write_cancel_reject(w, "1", "c", "o", '0', to, FIX::CxlRejReason::TooLate, "");
        std::array<char, 512> buf{};
        const std::string raw(buf.data(), w.finish(buf));
        EXPECT_NE(raw.find(expected), std::string::npos) << raw;
        EXPECT_NE(raw.find("\x01" "102=0\x01"), std::string::npos) << raw;
    }
}

TEST(WriteCancelReject, EmptyTextIsOmitted) {
    FIX::Writer w;
    w.field(FIX::tag::MessageType, FIX::msg_type::OrderCancelReject);
    FIX::write_cancel_reject(w, "1", "c", "o", '0', FIX::CxlRejResponseTo::Replace,
                             FIX::CxlRejReason::TooLate, "");
    std::array<char, 512> buf{};
    const std::string raw(buf.data(), w.finish(buf));
    EXPECT_EQ(raw.find("\x01" "58="), std::string::npos) << raw;
    EXPECT_TRUE(FIX::parse(raw).has_value());
}
