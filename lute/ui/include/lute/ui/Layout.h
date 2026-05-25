#pragma once

#include "lute/ui/Core.h"
#include "lute/ui/Node.h"

#include <optional>
#include <string>

namespace lute::ui
{

struct Constraints
{
    float minWidth = 0.0f;
    float maxWidth = 0.0f;
    float minHeight = 0.0f;
    float maxHeight = 0.0f;

    static Constraints loose(Vec2 maxSize)
    {
        return {0.0f, maxSize.x, 0.0f, maxSize.y};
    }
};

struct MeasureResult
{
    Vec2 size;
    std::optional<float> firstBaseline;
    std::optional<float> lastBaseline;
};

class LayoutEngine
{
public:
    void layout(NodeTree& tree, NodeId root, Vec2 viewport);
    MeasureResult measure(NodeTree& tree, NodeId id, Constraints constraints);
    void place(NodeTree& tree, NodeId id, Rect rect);
    std::string dump(const NodeTree& tree, NodeId root) const;

private:
    MeasureResult measureNode(NodeTree& tree, UiNode& node, Constraints constraints);
    void placeNode(NodeTree& tree, UiNode& node, Rect rect, bool force);
};

} // namespace lute::ui
