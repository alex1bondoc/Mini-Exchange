#pragma once

#include <optional>
#include <span>
#include <string_view>
#include <array>
#include <cstring>
#include <charconv>
#include "common/fix/parser.hpp"

namespace FIX {
    
    class Framer {
    public:
        bool append(std::span<const char> data) {
            if (data.size() > buffer_.size() - len_) {
                return false;
            }
            std::memcpy(buffer_.data() + len_, data.data(), data.size());
            len_ += data.size();
            return true; 
        }
        std::optional<std::string_view> next() {
            constexpr std::string_view begin{"8=FIX.4.4\x01"};
            const std::string_view have(buffer_.data(), len_);

            // too short
            if (have.size() < begin.size()) {
                // short and not good
                if (!begin.starts_with(have)) corrupted = true;
                return std::nullopt;
            }
            // not valid
            if (!have.starts_with(begin)) {
                corrupted = true;
                return std::nullopt;
            }

            std::size_t pos = begin.size();
            // not starting with length
            if (have.size() > pos && have[pos] != '9') {
                corrupted = true;
                return std::nullopt;
            }
            // not starting with length
            if (have.size() > pos + 1 && have[pos + 1] != '=') {
                corrupted = true;
                return std::nullopt;
            }
            // just incomplete
            if (have.size() < pos + 2) {
                return std::nullopt;
            }
            pos += 2;

            const std::size_t soh = have.find(SOH, pos);
            if (soh == std::string_view::npos) {
                if (have.size() - pos > MAX_LENGTH_DIGITS) corrupted = true;
                return std::nullopt;
            }
            std::size_t body_len;
            auto [ptr, ec] = std::from_chars(have.data() + pos, have.data() + soh, body_len);
            if (ec != std::errc{} || ptr != have.data() + soh) {
                corrupted = true;
                return std::nullopt;
            }
            const std::size_t total = soh + 1 + body_len + TRAILER_SIZE;
            if (total > buffer_.size()) {
                corrupted = true;
                return std::nullopt;
            }
            if (len_ < total) {
                return std::nullopt;
            }
            return have.substr(0, total);
        }
        void consume(std::size_t n) {
            if (n > len_) n = len_;
            std::memmove(buffer_.data(), buffer_.data() + n, len_ - n);
            len_ -= n;
        }

        bool corrupt() const { return corrupted; }
        std::size_t size() const { return len_; }
    private:
        std::array<char, 64 * 1024> buffer_{};
        std::size_t len_{};
        bool corrupted{};
        static constexpr std::size_t MAX_LENGTH_DIGITS = 6;
        static constexpr std::size_t  TRAILER_SIZE = 7;
    };
}