#pragma once

#include "lute/ui/Core.h"
#include "lute/ui/Node.h"

#include <optional>
#include <string>
#include <vector>

namespace lute::ui
{

enum class DisplayItemKind
{
    Rect,
    RoundedRect,
    TextRun,
    ClipPush,
    ClipPop,
    TransformPush,
    TransformPop,
    OpacityPush,
    OpacityPop,
};

struct Brush
{
    Color color;
};

struct DisplayItem
{
    DisplayItemKind kind = DisplayItemKind::Rect;
    NodeId node = kInvalidNodeId;
    Rect rect;
    float radius = 0.0f;
    Brush fill;
    std::string text;
    Vec2 origin;
    std::optional<Color> backgroundHint;
};

class Scene
{
public:
    void replace(std::vector<DisplayItem> nextItems);
    const std::vector<DisplayItem>& items() const;
    uint64_t generation() const;
    std::string dump() const;

private:
    std::vector<DisplayItem> displayItems;
    uint64_t sceneGeneration = 0;
};

class SceneBuilder
{
public:
    Scene build(const NodeTree& tree, NodeId root) const;

private:
    void emitNode(const NodeTree& tree, NodeId id, std::vector<DisplayItem>& out, std::optional<Color> backgroundHint) const;
};

} // namespace lute::ui
