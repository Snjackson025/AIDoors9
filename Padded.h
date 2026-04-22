#pragma once
#include "Vector7D.h"

template <typename T>
struct alignas(CACHE_LINE) Padded {
    T value;
    Padded() : value{} {}
    explicit Padded(const T& v) : value(v) {}
};
