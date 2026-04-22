#include "Vector7D.h"

Vector7D::Vector7D(const Vector7D& other) {
    for (int i = 0;i < 7;++i) data[i] = other.data[i];
}

Vector7D& Vector7D::operator=(const Vector7D& other) {
    if (this != &other) for (int i = 0;i < 7;++i) data[i] = other.data[i];
    return *this;
}