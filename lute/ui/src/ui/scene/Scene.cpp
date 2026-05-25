#include "lute/ui/Scene.h"

#include <cmath>
#include <sstream>
#include <utility>

namespace lute::ui
{

namespace
{

constexpr Color kWindowBackground = {245, 245, 242, 255};
constexpr Color kDefaultTextColor = {28, 30, 33, 255};
constexpr Color kButtonTextColor = {255, 255, 255, 255};

Color buttonFillColor(const UiNode& node)
{
    return {38, 101, 214, node.disabled ? uint8_t(120) : uint8_t(255)};
}

EdgeInsets buttonPadding(const UiNode& node)
{
    return node.padding.horizontal() == 0.0f && node.padding.vertical() == 0.0f ? EdgeInsets{6.0f, 12.0f, 6.0f, 12.0f} : node.padding;
}

std::optional<Color> resolveBackgroundHint(Color fill, std::optional<Color> backdrop)
{
    if (fill.a == 255)
        return fill;
    if (fill.a == 0)
        return backdrop;
    if (!backdrop)
        return std::nullopt;

    float sourceAlpha = static_cast<float>(fill.a) / 255.0f;
    float inverseAlpha = 1.0f - sourceAlpha;
    auto blend = [&](uint8_t source, uint8_t destination) {
        return static_cast<uint8_t>(std::round(static_cast<float>(source) * sourceAlpha + static_cast<float>(destination) * inverseAlpha));
    };

    return Color{blend(fill.r, backdrop->r), blend(fill.g, backdrop->g), blend(fill.b, backdrop->b), 255};
}

DisplayItem makeFill(DisplayItemKind kind, NodeId node, Rect rect, float radius, Color color)
{
    DisplayItem item;
    item.kind = kind;
    item.node = node;
    item.rect = rect;
    item.radius = radius;
    item.fill = {color};
    return item;
}

DisplayItem makeTextRun(NodeId node, Rect rect, Color color, std::string text, Vec2 origin, std::optional<Color> backgroundHint)
{
    DisplayItem item;
    item.kind = DisplayItemKind::TextRun;
    item.node = node;
    item.rect = rect;
    item.fill = {color};
    item.text = std::move(text);
    item.origin = origin;
    item.backgroundHint = backgroundHint;
    return item;
}

} // namespace

static std::string_view displayItemKindToString(DisplayItemKind kind)
{
    switch (kind)
    {
    case DisplayItemKind::Rect:
        return "Rect";
    case DisplayItemKind::RoundedRect:
        return "RoundedRect";
    case DisplayItemKind::TextRun:
        return "TextRun";
    case DisplayItemKind::ClipPush:
        return "ClipPush";
    case DisplayItemKind::ClipPop:
        return "ClipPop";
    case DisplayItemKind::TransformPush:
        return "TransformPush";
    case DisplayItemKind::TransformPop:
        return "TransformPop";
    case DisplayItemKind::OpacityPush:
        return "OpacityPush";
    case DisplayItemKind::OpacityPop:
        return "OpacityPop";
    }

    return "Unknown";
}

void Scene::replace(std::vector<DisplayItem> nextItems)
{
    displayItems = std::move(nextItems);
    sceneGeneration++;
}

const std::vector<DisplayItem>& Scene::items() const
{
    return displayItems;
}

uint64_t Scene::generation() const
{
    return sceneGeneration;
}

std::string Scene::dump() const
{
    std::ostringstream out;
    out << "Scene gen=" << sceneGeneration << "\n";

    for (const DisplayItem& item : displayItems)
    {
        out << displayItemKindToString(item.kind) << " node=" << item.node << " rect=(" << item.rect.x << "," << item.rect.y << "," << item.rect.width
            << "," << item.rect.height << ")";
        if (item.kind == DisplayItemKind::RoundedRect)
            out << " radius=" << item.radius;
        if (item.kind == DisplayItemKind::TextRun)
        {
            out << " text=\"" << item.text << "\" origin=(" << item.origin.x << "," << item.origin.y << ")";
            if (item.backgroundHint)
                out << " background=(" << static_cast<int>(item.backgroundHint->r) << "," << static_cast<int>(item.backgroundHint->g) << ","
                    << static_cast<int>(item.backgroundHint->b) << "," << static_cast<int>(item.backgroundHint->a) << ")";
            else
                out << " background=unknown";
        }
        out << "\n";
    }

    return out.str();
}

Scene SceneBuilder::build(const NodeTree& tree, NodeId root) const
{
    std::vector<DisplayItem> items;
    emitNode(tree, root, items, std::nullopt);

    Scene scene;
    scene.replace(std::move(items));
    return scene;
}

void SceneBuilder::emitNode(const NodeTree& tree, NodeId id, std::vector<DisplayItem>& out, std::optional<Color> backgroundHint) const
{
    const UiNode* node = tree.get(id);
    if (!node)
        return;

    std::optional<Color> childBackgroundHint = backgroundHint;

    switch (node->kind)
    {
    case WidgetKind::Window:
        out.push_back(makeFill(DisplayItemKind::Rect, node->id, node->layout.frame, 0.0f, kWindowBackground));
        childBackgroundHint = resolveBackgroundHint(kWindowBackground, backgroundHint);
        break;
    case WidgetKind::Box:
    case WidgetKind::Column:
    case WidgetKind::Row:
    case WidgetKind::Canvas:
        break;
    case WidgetKind::Text:
        out.push_back(makeTextRun(
            node->id,
            node->layout.frame,
            kDefaultTextColor,
            node->text,
            {node->layout.frame.x, node->layout.frame.y + node->layout.firstBaseline.value_or(15.0f)},
            backgroundHint
        ));
        break;
    case WidgetKind::Button:
    {
        Color buttonFill = buttonFillColor(*node);
        EdgeInsets padding = buttonPadding(*node);
        std::optional<Color> buttonBackgroundHint = resolveBackgroundHint(buttonFill, backgroundHint);
        out.push_back(
            makeFill(DisplayItemKind::RoundedRect, node->id, node->layout.frame, 6.0f, buttonFill)
        );
        out.push_back(
            makeTextRun(
                node->id,
                node->layout.frame,
                kButtonTextColor,
                node->text,
                {node->layout.frame.x + padding.left, node->layout.frame.y + node->layout.firstBaseline.value_or(21.0f)},
                buttonBackgroundHint
            )
        );
        childBackgroundHint = buttonBackgroundHint;
        break;
    }
    }

    for (NodeId child : node->children)
        emitNode(tree, child, out, childBackgroundHint);
}

} // namespace lute::ui
