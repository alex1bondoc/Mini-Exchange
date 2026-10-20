#pragma once

#include "common/fix/tags.hpp"
#include <array>
#include <string_view>
#include <expected>
#include <charconv>

namespace FIX {
    struct Message {
        char type{};
        std::array<std::string_view, 200> fields{};

        std::string_view get(int tag) const {
            return (tag >= 0 && tag < 200) ? fields[tag]: std::string_view{};
        }
    };

    enum class ParserError {
        BadBeginString,     // 8=FIX.4.4
        BadBodyLength,      // 9= missing or bad
        BadChecksum,        // 10= missing or bad
        MissingMsgType,     // 35 is not the first field
        Malformed           // wrong format
    };

    inline std::expected<Message, ParserError> parse(std::string_view raw) {
        constexpr std::string_view begin{"8=FIX.4.4\x01"};
        if (!raw.starts_with(begin)) {
            return std::unexpected(ParserError::BadBeginString);
        }

        std::size_t index = begin.size();

        if (raw.size() < index + 2 || raw[index] != '9' || raw[index + 1] != '=') {
            return std::unexpected(ParserError::BadBodyLength); 
        }
        index += 2;
        const std::size_t len_end = raw.find(SOH, index);
        if (len_end == std::string_view::npos) {
            return std::unexpected(ParserError::BadBodyLength);
        }

        std::size_t body_len{};
        auto [ptr, ec] = std::from_chars(raw.data() + index, raw.data() + len_end, body_len);
        if (ec != std::errc{} || ptr != raw.data() + len_end) {
            return std::unexpected(ParserError::BadBodyLength);
        }
        const std::size_t body_start = len_end + 1;
        if (raw.size() != body_start + body_len + 7) {
            return std::unexpected(ParserError::BadBodyLength);
        }

        unsigned sum{};
        for (index = 0; index < body_start + body_len; ++index) {
            sum += static_cast<unsigned char>(raw[index]);
        }
        sum %= 256;

        if (raw.size() < index + 7 || raw[index] != '1' || raw[index + 1] != '0' || raw[index + 2] != '=' || raw[index + 6] != SOH) {
            return std::unexpected(ParserError::BadChecksum);
        }
        unsigned declared{};
        for (std::size_t i = index + 3;i < index + 6; ++i) {
            if (raw[i] < '0' || raw[i] > '9') {
                return std::unexpected(ParserError::BadChecksum);
            }
            declared = declared * 10 + (raw[i] - '0');
        }
        if (declared != sum) {
            return std::unexpected(ParserError::BadChecksum);
        }
        
        std::string_view body = raw.substr(body_start, body_len);
        bool first{true};
        Message msg{};
        while (!body.empty()) {
            auto eq = body.find('=');
            auto soh = body.find(SOH);

            if (eq == std::string_view::npos || soh == std::string_view::npos || eq > soh) {
                return std::unexpected(ParserError::Malformed);
            }
            int tag_{};
            auto [tag_end, tag_ec] = std::from_chars(body.data(), body.data() + eq, tag_);
            if (tag_ec != std::errc{} || tag_end != body.data() + eq) {
                return std::unexpected(ParserError::Malformed);
            }
            auto value  = body.substr(eq + 1, soh - eq - 1);
            if (first) {
                first = false;
                if (tag_ != tag::MessageType || value.size() != 1) {
                    return std::unexpected(ParserError::MissingMsgType);
                }
                msg.type = value[0];
            }
            if (tag_ >= 0 && tag_ < 200) {
                msg.fields[tag_] = value;
            }
            body.remove_prefix(soh  + 1);
        }
        if (first) {
            return std::unexpected(ParserError::MissingMsgType);
        }
        return msg;
    }

}