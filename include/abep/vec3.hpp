#pragma once

#include <cmath>

namespace abep {

// A plain 3-vector. Metres, metres per second or metres per second squared,
// depending on what it holds; the type does not track units.
struct Vec3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

constexpr Vec3 operator+(Vec3 a, Vec3 b) noexcept { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
constexpr Vec3 operator-(Vec3 a, Vec3 b) noexcept { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
constexpr Vec3 operator-(Vec3 a) noexcept { return {-a.x, -a.y, -a.z}; }
constexpr Vec3 operator*(double s, Vec3 a) noexcept { return {s * a.x, s * a.y, s * a.z}; }
constexpr Vec3 operator*(Vec3 a, double s) noexcept { return {s * a.x, s * a.y, s * a.z}; }
constexpr Vec3 operator/(Vec3 a, double s) noexcept { return {a.x / s, a.y / s, a.z / s}; }

constexpr double dot(Vec3 a, Vec3 b) noexcept { return a.x * b.x + a.y * b.y + a.z * b.z; }

constexpr Vec3 cross(Vec3 a, Vec3 b) noexcept {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

inline double norm(Vec3 a) noexcept { return std::sqrt(dot(a, a)); }

}  // namespace abep
