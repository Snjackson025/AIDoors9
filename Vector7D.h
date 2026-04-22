#pragma once
#include <algorithm>
#include <cstddef>

constexpr size_t CACHE_LINE = 64;

struct alignas(CACHE_LINE) Vector7D {
    double data[7] = { 0.0,0.0,0.0,0.0,0.0,0.0,0.0 };

    Vector7D() = default;
    Vector7D(const Vector7D& other);
    Vector7D& operator=(const Vector7D& other);
};
