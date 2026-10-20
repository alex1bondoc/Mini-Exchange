#pragma once

#include <cstdint>
#include <array>
#include <string_view>
#include <span>
#include <charconv>
#include "tags.hpp"

namespace FIX {

class Writer {
public:
    Writer& field(int tag, std::string_view value) {
        append_number(tag);
        append('=');
        for (auto c : value) {
            append(c);
        }
        append(SOH);
        return *this;
    }
    Writer& field(int tag, int64_t value) {
        char tmp[20];
        auto [end, ec] = std::to_chars(tmp, tmp + sizeof(tmp), value);
        return field(tag, std::string_view(tmp, end - tmp));
    }
    Writer& field(int tag, char value) {
        append_number(tag);
        append('=');
        append(value);
        append(SOH);
        return *this;
    }
    std::size_t finish(std::span<char> out) const {
        if (overflow_) return 0;
        char header[32];
        std::size_t h = 0;
        for (char c : std::string_view("8=FIX.4.4")) header[h++] = c;
        header[h++] = SOH;
        header[h++] = '9';
        header[h++] = '=';
        h = std::to_chars(header + h, header + sizeof(header), len_).ptr - header;
        header[h++] = SOH;

        if (h + len_ + 7 > out.size()) return 0;

        std::size_t n = 0;
        for (std::size_t i{}; i < h; ++i) out[n++] = header[i];
        for (std::size_t i{}; i < len_; ++i) out[n++] = body_[i];

        unsigned sum{};
        for (std::size_t i{}; i < n; ++i) {
            sum += static_cast<unsigned char>(out[i]);
        }
        sum %= 256;

        out[n++] = '1';
        out[n++] = '0';
        out[n++] = '=';
        out[n++] = static_cast<char>('0' + sum / 100);
        out[n++] = static_cast<char>('0' + sum / 10 % 10);
        out[n++] = static_cast<char>('0' + sum % 10);
        out[n++] = SOH;
        return n;
    }
private:
    std::array<char, 1024> body_{};
    std::size_t len_{};
    bool overflow_{};

    void append(char c) {
        if (len_ == body_.size()) {
            overflow_ = true;
            return;
        }
        body_[len_++] = c;
    }

    void append_number(int64_t value) {
        char tmp[20];
        auto [end, ec] = std::to_chars(tmp, tmp + sizeof(tmp), value);
        for (char* p = tmp; p != end; ++p) append(*p);
    }
};

}
