#pragma once

#include "lute/ui/Core.h"
#include "lute/ui/Node.h"

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
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
    std::shared_ptr<const GlyphRun> glyphRun;
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
    struct CachedNode
    {
        std::vector<DisplayItem> localItems;
        std::vector<NodeId> children;
        std::optional<Color> backgroundHint;
        std::optional<Color> childBackgroundHint;
    };

    friend class SceneBuilder;

    std::vector<DisplayItem> displayItems;
    std::unordered_map<NodeId, CachedNode> nodeCache;
    NodeId cachedRoot = kInvalidNodeId;
    uint64_t sceneGeneration = 0;
    bool retained = false;
};

class SceneBuilder
{
public:
    Scene build(const NodeTree& tree, NodeId root) const;
    bool update(NodeTree& tree, NodeId root, Scene& scene) const;

private:
    struct LocalEmission
    {
        std::vector<DisplayItem> localItems;
        std::optional<Color> childBackgroundHint;
    };

    LocalEmission emitLocal(const UiNode& node, std::optional<Color> backgroundHint) const;
    void emitNode(const NodeTree& tree, NodeId id, std::vector<DisplayItem>& out, std::optional<Color> backgroundHint) const;
    bool updateNode(NodeTree& tree, NodeId id, std::optional<Color> backgroundHint, bool force, Scene& scene) const;
    void flattenNode(const NodeTree& tree, NodeId id, const Scene& scene, std::vector<DisplayItem>& out) const;
};

} // namespace lute::ui
