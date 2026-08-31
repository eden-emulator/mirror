// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: 2014 Tony Wasserka
// SPDX-FileCopyrightText: 2014 Dolphin Emulator Project
// SPDX-License-Identifier: BSD-3-Clause AND GPL-2.0-or-later

#pragma once

#ifdef __ARM_NEON
#include <arm_neon.h>
#endif

#include <cmath>
#include <type_traits>

namespace Common {

template <typename T>
class Vec2;
template <typename T>
class Vec3;
template <typename T>
class Quaternion;

template <typename T>
class Vec2 {
public:
    T x{};
    T y{};

    constexpr Vec2() = default;
    constexpr Vec2(const T& x_, const T& y_) noexcept : x(x_), y(y_) {}

    [[nodiscard]] constexpr Vec2<decltype(T{} + T{})> operator+(const Vec2& other) const noexcept {
        return {x + other.x, y + other.y};
    }
    constexpr Vec2& operator+=(const Vec2& other) noexcept {
        x += other.x;
        y += other.y;
        return *this;
    }
    [[nodiscard]] constexpr Vec2<decltype(T{} - T{})> operator-(const Vec2& other) const noexcept {
        return {x - other.x, y - other.y};
    }
    constexpr Vec2& operator-=(const Vec2& other) noexcept {
        x -= other.x;
        y -= other.y;
        return *this;
    }

    template <typename U = T>
    [[nodiscard]] constexpr Vec2<std::enable_if_t<std::is_signed_v<U>, U>> operator-() const noexcept {
        return {-x, -y};
    }
    [[nodiscard]] constexpr Vec2<decltype(T{} * T{})> operator*(const Vec2& other) const noexcept {
        return {x * other.x, y * other.y};
    }

    template <typename V>
    [[nodiscard]] constexpr Vec2<decltype(T{} * V{})> operator*(const V& f) const noexcept {
        using TV = decltype(T{} * V{});
        using C = std::common_type_t<T, V>;
        return {
            TV(C(x) * C(f)),
            TV(C(y) * C(f)),
        };
    }

    template <typename V>
    constexpr Vec2& operator*=(const V& f) noexcept {
        return *this = *this * f;
    }

    template <typename V>
    [[nodiscard]] constexpr Vec2<decltype(T{} / V{})> operator/(const V& f) const noexcept {
        using TV = decltype(T{} / V{});
        using C = std::common_type_t<T, V>;
        return {
            TV(C(x) / C(f)),
            TV(C(y) / C(f)),
        };
    }

    template <typename V>
    constexpr Vec2& operator/=(const V& f) noexcept {
        return *this = *this / f;
    }

    [[nodiscard]] constexpr T Length2() const noexcept {
        return x * x + y * y;
    }

    // Only implemented for T=float
    [[nodiscard]] float Length() const;

    [[nodiscard]] constexpr T& operator[](std::size_t i) noexcept {
        return *((&x) + i);
    }
    [[nodiscard]] constexpr const T& operator[](std::size_t i) const noexcept {
        return *((&x) + i);
    }
};

template <typename T, typename V>
[[nodiscard]] constexpr Vec2<T> operator*(const V& f, const Vec2<T>& vec) noexcept {
    using C = std::common_type_t<T, V>;
    return Vec2<T>(T(C(f) * C(vec.x)), T(C(f) * C(vec.y)));
}

using Vec2f = Vec2<float>;

template <>
inline float Vec2<float>::Length() const {
    return std::sqrt(x * x + y * y);
}

template <typename T>
class Vec3 {
public:
    T x{};
    T y{};
    T z{};

    constexpr Vec3() = default;
    constexpr Vec3(const T& x_, const T& y_, const T& z_) noexcept : x(x_), y(y_), z(z_) {}

    [[nodiscard]] constexpr Vec3<decltype(T{} + T{})> operator+(const Vec3& other) const noexcept {
        return {x + other.x, y + other.y, z + other.z};
    }

    constexpr Vec3 operator+=(const Vec3& other) noexcept {
        x += other.x;
        y += other.y;
        z += other.z;
        return *this;
    }

    [[nodiscard]] constexpr Vec3<decltype(T{} - T{})> operator-(const Vec3& other) const noexcept {
        return {x - other.x, y - other.y, z - other.z};
    }

    constexpr Vec3 operator-=(const Vec3& other) noexcept {
        x -= other.x;
        y -= other.y;
        z -= other.z;
        return *this;
    }

    template <typename U = T>
    [[nodiscard]] constexpr Vec3<std::enable_if_t<std::is_signed_v<U>, U>> operator-() const noexcept {
        return {-x, -y, -z};
    }

    [[nodiscard]] constexpr Vec3<decltype(T{} * T{})> operator*(const Vec3& other) const noexcept {
        return {x * other.x, y * other.y, z * other.z};
    }

    template <typename V>
    [[nodiscard]] constexpr Vec3<decltype(T{} * V{})> operator*(const V& f) const noexcept {
        using TV = decltype(T{} * V{});
        using C = std::common_type_t<T, V>;

        return {
            TV(C(x) * C(f)),
            TV(C(y) * C(f)),
            TV(C(z) * C(f)),
        };
    }

    template <typename V>
    constexpr Vec3 operator*=(const V& f) noexcept {
        return *this = *this * f;
    }
    template <typename V>
    [[nodiscard]] constexpr Vec3<decltype(T{} / V{})> operator/(const V& f) const noexcept {
        using TV = decltype(T{} / V{});
        using C = std::common_type_t<T, V>;

        return {
            TV(C(x) / C(f)),
            TV(C(y) / C(f)),
            TV(C(z) / C(f)),
        };
    }

    template <typename V>
    constexpr Vec3& operator/=(const V& f) noexcept {
        return *this = *this / f;
    }

    [[nodiscard]] constexpr T Length2() const noexcept {
        return x * x + y * y + z * z;
    }

    // Only implemented for T=float
    [[nodiscard]] float Length() const;
    [[nodiscard]] Vec3 Normalized() const;

    [[nodiscard]] constexpr T& operator[](std::size_t i) noexcept {
        return *((&x) + i);
    }

    [[nodiscard]] constexpr const T& operator[](std::size_t i) const noexcept {
        return *((&x) + i);
    }
};

template <typename T, typename V>
[[nodiscard]] constexpr Vec3<T> operator*(const V& f, const Vec3<T>& vec) noexcept {
    using C = std::common_type_t<T, V>;
    return Vec3<T>(T(C(f) * C(vec.x)), T(C(f) * C(vec.y)), T(C(f) * C(vec.z)));
}

template <>
inline float Vec3<float>::Length() const {
    return std::sqrt(x * x + y * y + z * z);
}

template <>
inline Vec3<float> Vec3<float>::Normalized() const {
    return *this / Length();
}

using Vec3f = Vec3<float>;

template <typename T>
class Quaternion {
public:
    Vec3<T> xyz;
    T w{};

    [[nodiscard]] Quaternion<decltype(T{} + T{})> operator+(const Quaternion& other) const noexcept {
        return {xyz + other.xyz, w + other.w};
    }

    [[nodiscard]] Quaternion<decltype(T{} - T{})> operator-(const Quaternion& other) const noexcept {
        return {xyz - other.xyz, w - other.w};
    }

    [[nodiscard]] Quaternion<decltype(T{} * T{} - T{} * T{})> operator*(const Quaternion& other) const noexcept {
        return {xyz * other.w + other.xyz * w + Cross(xyz, other.xyz), w * other.w - Dot(xyz, other.xyz)};
    }

    [[nodiscard]] Quaternion<T> Normalized() const noexcept {
        T length = std::sqrt(xyz.Length2() + w * w);
        return {xyz / length, w / length};
    }

    [[nodiscard]] std::array<decltype(-T{}), 16> ToMatrix() const {
        const T x2 = xyz[0] * xyz[0];
        const T y2 = xyz[1] * xyz[1];
        const T z2 = xyz[2] * xyz[2];

        const T xy = xyz[0] * xyz[1];
        const T wz = w * xyz[2];
        const T xz = xyz[0] * xyz[2];
        const T wy = w * xyz[1];
        const T yz = xyz[1] * xyz[2];
        const T wx = w * xyz[0];

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

} // namespace Common
