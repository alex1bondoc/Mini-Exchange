#pragma once
#include "common/types.hpp"
#include <cassert>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

using OrderIdx = uint32_t;
constexpr OrderIdx NIL = std::numeric_limits<OrderIdx>::max();

struct Order {
    FIX::OrderId  id{};
    FIX::ClientId client{};
    FIX::Price    price{};
    FIX::Qty      qty{};
    FIX::Qty      filled{};
    FIX::Side     side{};
    OrderIdx      prev{NIL};
    OrderIdx      next{NIL};
};

class OrderPool {
public:
    explicit OrderPool(std::size_t capacity) {
        assert(capacity < NIL);
        orders_.resize(capacity);
        in_use_.resize(capacity, false);
        free_.reserve(capacity);
        for (std::size_t i{}; i < capacity; ++i) {
            free_.push_back(static_cast<OrderIdx>(capacity - i - 1));
        }
    }

    std::optional<OrderIdx> allocate() {
        if (free_.empty()) {
            return std::nullopt;
        }
        const OrderIdx idx = free_.back();
        free_.pop_back();
        in_use_[idx] = true;
        return idx;
    }

    void release(OrderIdx idx) {
        assert(idx < orders_.size());
        assert(in_use_[idx] && "double release");
        in_use_[idx] = false;
        orders_[idx] = Order{};
        free_.push_back(idx);
    }

    Order&       operator[](OrderIdx idx)       { return orders_[idx]; }
    const Order& operator[](OrderIdx idx) const { return orders_[idx]; }

    std::size_t capacity() const { return orders_.size(); }
    std::size_t in_use()   const { return orders_.size() - free_.size(); }

private:
    std::vector<Order>    orders_;
    std::vector<OrderIdx> free_;
    std::vector<bool>     in_use_;
};