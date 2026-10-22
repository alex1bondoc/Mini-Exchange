#pragma once
#include <expected>
#include "common/fix/parser.hpp"
#include "common/fix/writer.hpp"
#include "common/fix/tags.hpp"
#include "common/types.hpp"
#include "common/messages.hpp"
#include <charconv>
#include <cstdint>
#include <expected>
#include <span>
#include <string_view>

namespace FIX {
    constexpr std::size_t PRICE_DECIMALS = 4;
    constexpr int64_t        PRICE_SCALE = 10'000;

    enum class ConvertError {
        MissingField,
        InvalidValue,
        InvalidNumber,
        InvalidPrice
    };


    inline bool all_digits(std::string_view s) {
        for (char c : s) if (c < '0' || c > '9') return false;
        return true;
    }

    inline std::expected<Price, ConvertError> parse_price(std::string_view text) {
        if (text.empty()) return std::unexpected(ConvertError::InvalidPrice);

        const auto dot = text.find('.');
        const std::string_view whole_part = text.substr(0, dot);
        std::string_view frac_part{};
        if (dot != std::string_view::npos) {
            frac_part = text.substr(dot + 1);
            if (frac_part.empty()) return std::unexpected(ConvertError::InvalidPrice);   // "150."
        }

        if (whole_part.empty() || !all_digits(whole_part) || !all_digits(frac_part)) {
            return std::unexpected(ConvertError::InvalidPrice);
        }
        if (frac_part.size() > PRICE_DECIMALS) return std::unexpected(ConvertError::InvalidPrice);

        int64_t whole{};
        auto [whole_end, whole_ec] = std::from_chars(whole_part.data(), whole_part.data() + whole_part.size(), whole);
        if (whole_ec != std::errc{}) return std::unexpected(ConvertError::InvalidPrice);   // număr prea mare

        // PAS 4: zecimalele, doar dacă există
        int64_t frac{};
        if (!frac_part.empty()) {
            auto [frac_end, frac_ec] = std::from_chars(frac_part.data(), frac_part.data() + frac_part.size(), frac);
            if (frac_ec != std::errc{}) return std::unexpected(ConvertError::InvalidPrice);
        }
        for (std::size_t i = frac_part.size(); i < PRICE_DECIMALS; ++i) frac *= 10;
        if (whole > (INT64_MAX - frac) / PRICE_SCALE) {
            return std::unexpected(ConvertError::InvalidPrice);
        }
        return whole * PRICE_SCALE + frac;
    }

    inline std::size_t format_price(Price ticks, std::span<char> out) {
        int64_t whole = ticks / PRICE_SCALE;
        int64_t  frac = ticks % PRICE_SCALE;
        
        char tmp[32]{};
        char* end = std::to_chars(tmp, tmp + sizeof(tmp), whole).ptr;
        

        if (frac != 0) {
            *end = '.';
            end ++;
            char digits[PRICE_DECIMALS];
            for (std::size_t i = PRICE_DECIMALS; i > 0; --i) {
                digits[i - 1] = static_cast<char>('0' + frac % 10);
                frac /= 10;
            }
            std::size_t n = PRICE_DECIMALS;
            while (digits[n - 1] == '0') --n;
            for (std::size_t i{}; i < n; ++i) {
                *end++ = digits[i];
            }
        }

        const std::size_t len = static_cast<std::size_t>(end - tmp);
        if (len > out.size()) return 0;
        for (std::size_t i{}; i < len; ++i) {
            out[i] = tmp[i];
        }
        return len;
    }

    // =============================MESSAGES==================================

    struct NewOrderFields {
        std::string_view cl_ord_id;           // 11
        std::string_view symbol;              // 55
        Side             side{};              // 54
        OrderType        type{};              // 40
        TimeInForce      tif{};               // 59 (implicit Day)
        bool             post_only{};         // 18 contains '6'
        Price            price{};             // 44 (only Limit / StopLimit)
        Price            stop_price{};        // 99 (only Stop / StopLimit)
        Qty              qty{};               // 38
    };

    struct CancelFields {
        std::string_view cl_ord_id;      // 11: id-ul of cancel order
        std::string_view orig_cl_ord_id; // 41: what order
        std::string_view symbol;         // 55  
        Side             side{};         // 54
    };

    struct ReplaceFields {
        std::string_view cl_ord_id;      // 11: new Id
        std::string_view orig_cl_ord_id; // 41: order id
        std::string_view symbol;         // 55
        Side             side{};         // 54
        Price            new_price{};    // 44
        Qty              new_qty{};      // 38
    };

    // ==========================CONVERTS==========================================

    inline std::expected<NewOrderFields, ConvertError> decode_new_order(const Message& msg) {
        NewOrderFields fields{};

        std::string_view cl_ord_id = msg.get(tag::ClOrdId);
        if (cl_ord_id.empty()) {
            return std::unexpected(ConvertError::MissingField);
        }
        fields.cl_ord_id = cl_ord_id;

        std::string_view symbol = msg.get(tag::Symbol);
        if (symbol.empty()) {
            return std::unexpected(ConvertError::MissingField);
        }
        fields.symbol = symbol;

        std::string_view side = msg.get(tag::Side);
        if (side.empty()) {
            return std::unexpected(ConvertError::MissingField);
        }
        if (side == "1") {
            fields.side = Side::Buy;
        }
        else if (side == "2") {
            fields.side = Side::Sell;
        }
        else {
            return std::unexpected(ConvertError::InvalidValue);
        }

        std::string_view type = msg.get(tag::OrderType);
        if (type.empty()) {
            return std::unexpected(ConvertError::MissingField);
        }
        if (type == "1") {
            fields.type = OrderType::Market;
        }
        else if (type == "2") {
            fields.type = OrderType::Limit;
        }
        else if (type == "3") {
            fields.type = OrderType::Stop;
        }
        else if (type == "4") {
            fields.type = OrderType::StopLimit;
        }
        else {
            return std::unexpected(ConvertError::InvalidValue);
        }

        std::string_view tif = msg.get(tag::TimeInForce);
        if (tif.empty() || tif == "0") {
            fields.tif = TimeInForce::DAY;
        }
        else if (tif == "3") {
            fields.tif = TimeInForce::FAK;
        }
        else if (tif == "4") {
            fields.tif = TimeInForce::FOK;
        }
        else {
            return std::unexpected(ConvertError::InvalidValue);
        }

        std::string_view qty = msg.get(tag::OrderQty);
        if (qty.empty()) {
            return std::unexpected(ConvertError::MissingField);
        }
        auto [qty_end, qty_ec] = std::from_chars(qty.data(), qty.data() + qty.size(), fields.qty);
        if (qty_ec != std::errc{} || qty_end != qty.data() + qty.size() || fields.qty <= 0) {
            return std::unexpected(ConvertError::InvalidNumber);
        }
        if (fields.type == OrderType::Limit || fields.type == OrderType::StopLimit) {
            std::string_view price = msg.get(tag::Price);
            if (price.empty()) {
                return std::unexpected(ConvertError::MissingField);
            }
            auto p = parse_price(price);
            if (!p) {
                return std::unexpected(p.error());
            }
            fields.price = *p;
        }

        if (fields.type == OrderType::Stop || fields.type == OrderType::StopLimit) {
            std::string_view stop_price = msg.get(tag::StopPx);
            if (stop_price.empty()) {
                return std::unexpected(ConvertError::MissingField);
            }
            auto p = parse_price(stop_price);
            if (!p) {
                return std::unexpected(p.error());
            }
            fields.stop_price = *p;
        }

        fields.post_only = msg.get(tag::ExecInst).find('6') != std::string_view::npos;

        return fields;
    }

    inline std::expected<CancelFields, ConvertError> decode_cancel(const Message& msg) {
        CancelFields fields{};

        std::string_view cl_ord_id = msg.get(tag::ClOrdId);
        if (cl_ord_id.empty()) {
            return std::unexpected(ConvertError::MissingField);
        }
        fields.cl_ord_id = cl_ord_id;

        std::string_view orig_cl_ord_id = msg.get(tag::OrigClOrdId);
        if (orig_cl_ord_id.empty()) {
            return std::unexpected(ConvertError::MissingField);
        }
        fields.orig_cl_ord_id = orig_cl_ord_id;

        std::string_view symbol = msg.get(tag::Symbol);
        if (symbol.empty()) {
            return std::unexpected(ConvertError::MissingField);
        }
        fields.symbol = symbol;

        std::string_view side = msg.get(tag::Side);
        if (side.empty()) {
            return std::unexpected(ConvertError::MissingField);
        }
        if (side == "1") {
            fields.side = Side::Buy;
        }
        else if (side == "2") {
            fields.side = Side::Sell;
        }
        else {
            return std::unexpected(ConvertError::InvalidValue);
        }
        
        return fields;
    }

    inline std::expected<ReplaceFields, ConvertError> decode_replace(const Message& msg) {
        ReplaceFields fields{};

        if (msg.type != 'G') {
            return std::unexpected(ConvertError::InvalidValue);
        }

        std::string_view cl_ord_id = msg.get(tag::ClOrdId);
        if (cl_ord_id.empty()) {
            return std::unexpected(ConvertError::MissingField);
        }
        fields.cl_ord_id = cl_ord_id;

        std::string_view orig_cl_ord_id = msg.get(tag::OrigClOrdId);
        if (orig_cl_ord_id.empty()) {
            return std::unexpected(ConvertError::MissingField);
        }
        fields.orig_cl_ord_id = orig_cl_ord_id;

        std::string_view symbol = msg.get(tag::Symbol);
        if (symbol.empty()) {
            return std::unexpected(ConvertError::MissingField);
        }
        fields.symbol = symbol;

        std::string_view side = msg.get(tag::Side);
        if (side.empty()) {
            return std::unexpected(ConvertError::MissingField);
        }
        if (side == "1") {
            fields.side = Side::Buy;
        }
        else if (side == "2") {
            fields.side = Side::Sell;
        }
        else {
            return std::unexpected(ConvertError::InvalidValue);
        }

        std::string_view price = msg.get(tag::Price);
        if (price.empty()) {
            return std::unexpected(ConvertError::MissingField);
        }
        auto p = parse_price(price);
        if (!p) {
            return std::unexpected(p.error());
        }
        fields.new_price = *p;

        std::string_view qty = msg.get(tag::OrderQty);
        if (qty.empty()) {
            return std::unexpected(ConvertError::MissingField);
        }
        auto [qty_end, qty_ec] = std::from_chars(qty.data(), qty.data() + qty.size(), fields.new_qty);
        if (qty_ec != std::errc{} || qty_end != qty.data() + qty.size() || fields.new_qty <= 0) {
            return std::unexpected(ConvertError::InvalidNumber);
        }

        return fields;
    }

    char exec_type_char(ExecType type) {
        switch (type) {
            case ExecType::New:      return '0';
            case ExecType::Trade:    return 'F';
            case ExecType::Canceled: return '4';
            case ExecType::Replaced: return '5';
            case ExecType::Rejected: return '8';
        }
        return '8';
    }

    char side_char(Side side) {
        if (side == Side::Buy) return '1';
        return '2';
    }

    char ord_status_char(const ExecReport& r) {
        if (r.type == ExecType::Rejected) return '8';
        if (r.type == ExecType::Canceled) return '4';
        if (r.leaves_qty == 0 && r.filled_qty > 0) return '2';
        if (r.filled_qty > 0) return '1';
        return '0';
    }

    std::string_view reject_text(RejectReason reason) {
        switch (reason) {
            case RejectReason::None:                return "";
            case RejectReason::InvalidPrice:        return "price is not multiple of tick";
            case RejectReason::OutsideBand:         return "price outside of band";
            case RejectReason::InvalidQty:          return "invalid quantity";
            case RejectReason::UnknownOrder:        return "unknown order";
            case RejectReason::PostOnlyWouldCross:  return "post-only would cross";
            case RejectReason::FokNotFilled:        return "FOK can't be filled";
            case RejectReason::InvalidCombination:  return "invalid order type combination";
            case RejectReason::UnknownSymbol:       return "bad symbol";
            case RejectReason::DuplicateOrder:      return "cannot duplicate an order";
            case RejectReason::BookFull:            return "can't accept any more orders";
        }
        return "rejected";
    }

    inline void write_exec_report(Writer& w, const ExecReport& r, 
                                std::string_view cl_ord_id, 
                                std::string_view symbol,
                                int64_t exec_id) {
        w.field(tag::OrderId, static_cast<int64_t>(r.order))
         .field(tag::ClOrdId, cl_ord_id)
         .field(tag::ExecId, exec_id)
         .field(tag::ExecType, exec_type_char(r.type))
         .field(tag::OrdStatus, ord_status_char(r))
         .field(tag::Symbol, symbol)
         .field(tag::Side, side_char(r.side))
         .field(tag::LeavesQty, static_cast<int64_t>(r.leaves_qty))
         .field(tag::CumQty, static_cast<int64_t>(r.filled_qty));

        if (r.type == ExecType::Trade) {
            char price_buf[32];
            const std::size_t n = format_price(r.last_price, price_buf);
            w.field(tag::LastPx, std::string_view(price_buf, n))
             .field(tag::LastQty, static_cast<int64_t>(r.last_qty));
        }

        if (r.type == ExecType::Rejected) {
            w.field(tag::Text, reject_text(r.reason));
        }
    }

    inline void write_cancel_reject(Writer& w, std::string_view order_id,
                                std::string_view cl_ord_id, std::string_view orig_cl_ord_id,
                                char ord_status, CxlRejResponseTo response_to,
                                CxlRejReason reason, std::string_view text) {
        w.field(tag::OrderId,          order_id)                              
         .field(tag::ClOrdId,          cl_ord_id)                             
         .field(tag::OrigClOrdId,      orig_cl_ord_id)                        
         .field(tag::OrdStatus,        ord_status)                            
         .field(tag::CxlRejResponseTo, static_cast<char>(response_to))        
         .field(tag::CxlRejReason,     static_cast<int64_t>(reason));         
        if (!text.empty()) w.field(tag::Text, text); 
    }
}