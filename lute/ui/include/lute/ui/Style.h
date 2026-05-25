#pragma once

#include "lute/ui/Node.h"

#include <algorithm>
#include <optional>

namespace lute::ui
{

struct ControlMetrics
{
    EdgeInsets padding;
    Vec2 minimumSize;
    float labelBaselineFallback = 0.0f;
    float cornerRadius = 0.0f;
    float focusRingOutset = 0.0f;
    Color fillColor;
    Color textColor;
    Color focusRingColor;
};

inline constexpr Color kWindowBackgroundColor = {245, 245, 242, 255};
inline constexpr Color kDefaultTextColor = {28, 30, 33, 255};
inline constexpr Color kButtonTextColor = {255, 255, 255, 255};
inline constexpr Color kFocusRingColor = {10, 132, 255, 115};

inline bool hasExplicitPadding(EdgeInsets padding)
{
    return padding.horizontal() != 0.0f || padding.vertical() != 0.0f;
}

inline Rect outset(Rect rect, float amount)
{
    rect.x -= amount;
    rect.y -= amount;
    rect.width += amount * 2.0f;
    rect.height += amount * 2.0f;
    return rect;
}

inline Rect inset(Rect rect, EdgeInsets insets)
{
    rect.x += insets.left;
    rect.y += insets.top;
    rect.width = std::max(0.0f, rect.width - insets.horizontal());
    rect.height = std::max(0.0f, rect.height - insets.vertical());
    return rect;
}

inline ControlMetrics resolveButtonMetrics(const UiNode& node)
{
    EdgeInsets padding = hasExplicitPadding(node.padding) ? node.padding : EdgeInsets{6.0f, 12.0f, 6.0f, 12.0f};
    return {
        padding,
        {64.0f, 32.0f},
        15.0f,
        6.0f,
        3.0f,
        {38, 101, 214, node.disabled ? uint8_t(120) : uint8_t(255)},
        kButtonTextColor,
        kFocusRingColor,
    };
}

inline Vec2 controlPreferredSize(const ControlMetrics& metrics, Vec2 contentSize)
{
    return {
        std::max(metrics.minimumSize.x, contentSize.x + metrics.padding.horizontal()),
        std::max(metrics.minimumSize.y, contentSize.y + metrics.padding.vertical()),
    };
}

inline float controlFirstBaseline(const ControlMetrics& metrics, std::optional<float> contentBaseline)
{
    return metrics.padding.top + contentBaseline.value_or(metrics.labelBaselineFallback);
}

inline Rect controlVisualBounds(const UiNode& node)
{
    return node.layout.frame;
}

inline Rect controlContentBounds(const UiNode& node, const ControlMetrics& metrics)
{
    return inset(controlVisualBounds(node), metrics.padding);
}

inline Rect controlHitBounds(const UiNode& node, const ControlMetrics&)
{
    return node.layout.frame;
}

inline Rect controlSemanticBounds(const UiNode& node, const ControlMetrics& metrics)
{
    return controlHitBounds(node, metrics);
}

inline Rect controlFocusRingBounds(const UiNode& node, const ControlMetrics& metrics)
{
    return outset(controlVisualBounds(node), metrics.focusRingOutset);
}

inline float controlFocusRingRadius(const ControlMetrics& metrics)
{
    return metrics.cornerRadius + metrics.focusRingOutset;
}

inline Vec2 controlLabelOrigin(const UiNode& node, const ControlMetrics& metrics)
{
    Rect content = controlContentBounds(node, metrics);
    return {content.x, node.layout.frame.y + node.layout.firstBaseline.value_or(controlFirstBaseline(metrics, std::nullopt))};
}

inline Rect widgetHitBounds(const UiNode& node)
{
    if (node.kind == WidgetKind::Button)
        return controlHitBounds(node, resolveButtonMetrics(node));

    return node.layout.frame;
}

inline Rect widgetSemanticBounds(const UiNode& node)
{
    if (node.kind == WidgetKind::Button)
        return controlSemanticBounds(node, resolveButtonMetrics(node));

    return node.layout.frame;
}

} // namespace lute::ui
