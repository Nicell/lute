#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace lute::ui
{

using NodeId = uint64_t;
inline constexpr NodeId kInvalidNodeId = 0;

enum class WidgetKind
{
    Window,
    Box,
    Column,
    Row,
    Text,
    Button,
    Canvas,
};

enum class DirtyBits : uint32_t
{
    None = 0,
    State = 1 << 0,
    Layout = 1 << 1,
    Paint = 1 << 2,
    Scene = 1 << 3,
    Semantics = 1 << 4,
    HitTest = 1 << 5,
    Text = 1 << 6,
};

inline constexpr DirtyBits operator|(DirtyBits lhs, DirtyBits rhs)
{
    return static_cast<DirtyBits>(static_cast<uint32_t>(lhs) | static_cast<uint32_t>(rhs));
}

inline constexpr DirtyBits operator&(DirtyBits lhs, DirtyBits rhs)
{
    return static_cast<DirtyBits>(static_cast<uint32_t>(lhs) & static_cast<uint32_t>(rhs));
}

inline DirtyBits& operator|=(DirtyBits& lhs, DirtyBits rhs)
{
    lhs = lhs | rhs;
    return lhs;
}

inline DirtyBits& operator&=(DirtyBits& lhs, DirtyBits rhs)
{
    lhs = lhs & rhs;
    return lhs;
}

inline constexpr bool hasDirty(DirtyBits bits, DirtyBits query)
{
    return (static_cast<uint32_t>(bits & query)) != 0;
}

inline constexpr DirtyBits kAllDirtyBits =
    DirtyBits::State | DirtyBits::Layout | DirtyBits::Paint | DirtyBits::Scene | DirtyBits::Semantics | DirtyBits::HitTest | DirtyBits::Text;

struct Vec2
{
    float x = 0.0f;
    float y = 0.0f;

    bool operator==(const Vec2& rhs) const
    {
        return x == rhs.x && y == rhs.y;
    }
};

struct Rect
{
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;

    bool operator==(const Rect& rhs) const
    {
        return x == rhs.x && y == rhs.y && width == rhs.width && height == rhs.height;
    }

    bool operator!=(const Rect& rhs) const
    {
        return !(*this == rhs);
    }

    bool contains(Vec2 point) const
    {
        return point.x >= x && point.y >= y && point.x <= x + width && point.y <= y + height;
    }
};

struct EdgeInsets
{
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;
    float left = 0.0f;

    static EdgeInsets all(float value)
    {
        return {value, value, value, value};
    }

    float horizontal() const
    {
        return left + right;
    }

    float vertical() const
    {
        return top + bottom;
    }
};

struct Color
{
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    uint8_t a = 255;

    bool operator==(const Color& rhs) const
    {
        return r == rhs.r && g == rhs.g && b == rhs.b && a == rhs.a;
    }
};

struct LayoutBox
{
    Rect frame;
    Vec2 measuredSize;
    std::optional<float> firstBaseline;
    std::optional<float> lastBaseline;
    uint64_t generation = 0;
};

std::string_view toString(WidgetKind kind);
std::string dirtyBitsToString(DirtyBits bits);

} // namespace lute::ui
