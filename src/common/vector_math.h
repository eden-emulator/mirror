// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: 2014 Tony Wasserka
// SPDX-FileCopyrightText: 2014 Dolphin Emulator Project
// SPDX-License-Identifier: BSD-3-Clause AND GPL-2.0-or-later

#pragma once

#include <cmath>
#include <type_traits>

namespace Common {

template <typename T, size_t N>
class Vec {
public:
    std::array<T, N> elems{};

    constexpr Vec() = default;
    constexpr Vec(T e0) noexcept : elems{e0} {}
    constexpr Vec(T e0, T e1) noexcept : elems{e0, e1} {}
    constexpr Vec(T e0, T e1, T e2) noexcept : elems{e0, e1, e2} {}
    constexpr Vec(T e0, T e1, T e2, T e4) noexcept : elems{e0, e1, e2, e4} {}
    //explicit constexpr Vec(const std::initializer_list<T> elems_) noexcept : elems{elems_} {}

    [[nodiscard]] constexpr Vec<decltype(T{} + T{}), N> operator+(const Vec o) const noexcept {
        Vec<decltype(T{} + T{}), N> r{};
        for (size_t i = 0; i < N; ++i)
            r.elems[i] = elems[i] + o.elems[i];
        return r;
    }
    constexpr Vec<T, N> operator+=(const Vec<T, N> o) noexcept { return *this = *this + o; }

    [[nodiscard]] constexpr Vec<decltype(T{} - T{}), N> operator-(const Vec o) const noexcept {
        Vec<decltype(T{} - T{}), N> r{};
        for (size_t i = 0; i < N; ++i)
            r.elems[i] = elems[i] - o.elems[i];
        return r;
    }
    constexpr Vec<T, N> operator-=(const Vec<T, N> o) noexcept { return *this = *this - o; }

    template <typename U = T>
    [[nodiscard]] constexpr Vec<std::enable_if_t<std::is_signed_v<U>, U>, N> operator-() const noexcept {
        Vec<U, N> r{};
        for (size_t i = 0; i < N; ++i)
            r.elems[i] = -elems[i];
        return r;
    }

    [[nodiscard]] constexpr Vec<decltype(T{} * T{}), N> operator*(const Vec o) const noexcept {
        Vec<decltype(T{} * T{}), N> r{};
        for (size_t i = 0; i < N; ++i)
            r.elems[i] = elems[i] * o.elems[i];
        return r;
    }
    template <typename V>
    [[nodiscard]] constexpr Vec<decltype(T{} * V{}), N> operator*(const V f) const noexcept {
        using TV = decltype(T{} * V{});
        using C = std::common_type_t<T, V>;
        Vec<TV, N> r{};
        for (size_t i = 0; i < N; ++i)
            r.elems[i] = TV(C(elems[i]) * C(f));
        return r;
    }
    template <typename V>
    constexpr Vec<T, N> operator*=(const V f) noexcept { return *this = *this * f; }

    template <typename V>
    [[nodiscard]] constexpr Vec<decltype(T{} / V{}), N> operator/(const V f) const noexcept {
        using TV = decltype(T{} / V{});
        using C = std::common_type_t<T, V>;
        Vec<TV, N> r{};
        for (size_t i = 0; i < N; ++i)
            r.elems[i] = TV(C(elems[i]) / C(f));
        return r;
    }
    template <typename V>
    constexpr Vec<T, N> operator/=(const V f) noexcept { return *this = *this / f; }

    [[nodiscard]] constexpr T Length2() const noexcept {
        T r{};
        for (size_t i = 0; i < N; ++i)
            r += elems[i] * elems[i];
        return r;
    }
    // Only implemented for T=float
    [[nodiscard]] T Length() const { return T(std::sqrt(float(Length2()))); }
    [[nodiscard]] Vec<T, N> Normalized() const { return *this / Length(); }
    [[nodiscard]] constexpr T& operator[](std::size_t i) noexcept { return elems[i]; }
    [[nodiscard]] constexpr const T& operator[](std::size_t i) const noexcept { return elems[i]; }

    [[nodiscard]] std::array<decltype(-T{}), 16> ToMatrix() const {
        const T x2 = elems[0] * elems[0];
        const T y2 = elems[1] * elems[1];
        const T z2 = elems[2] * elems[2];

        const T xy = elems[0] * elems[1];
        const T wz = elems[3] * elems[2];
        const T xz = elems[0] * elems[2];
        const T wy = elems[3] * elems[1];
        const T yz = elems[1] * elems[2];
        const T wx = elems[3] * elems[0];
        return {
            1.0f - 2.0f * (y2 + z2),
            2.0f * (xy + wz),
            2.0f * (xz - wy),
            0.0f,
            2.0f * (xy - wz),
            1.0f - 2.0f * (x2 + z2),
            2.0f * (yz + wx),
            0.0f,
            2.0f * (xz + wy),
            2.0f * (yz - wx),
            1.0f - 2.0f * (x2 + y2),
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            1.0f
        };
    }
};

template <typename T, size_t N, typename V>
[[nodiscard]] constexpr Vec<T, N> operator*(const V f, const Vec<T, N> v) noexcept {
    using C = std::common_type_t<T, V>;
    Vec<T, N> r{};
    for (size_t i = 0; i < N; ++i)
        r.elems[i] = T(C(f) * C(v.elems[i]));
    return r;
}

} // namespace Common
