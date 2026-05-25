#include "lute/ui/Layout.h"
#include "lute/ui/Text.h"

#include <algorithm>
#include <sstream>

namespace lute::ui
{

static float clampFloat(float value, float minValue, float maxValue)
{
    return std::max(minValue, std::min(value, maxValue));
}

static Vec2 clampSize(Vec2 size, Constraints constraints)
{
    return {clampFloat(size.x, constraints.minWidth, constraints.maxWidth), clampFloat(size.y, constraints.minHeight, constraints.maxHeight)};
}

static Constraints childConstraints(Constraints constraints, EdgeInsets padding)
{
    return {
        0.0f,
        std::max(0.0f, constraints.maxWidth - padding.horizontal()),
        0.0f,
        std::max(0.0f, constraints.maxHeight - padding.vertical()),
    };
}

static MeasureResult textMeasure(const std::string& text)
{
    TextShaper shaper;
    return shaper.measureSingleLine(text);
}

void LayoutEngine::layout(NodeTree& tree, NodeId root, Vec2 viewport)
{
    if (root == kInvalidNodeId || !tree.get(root))
        return;

    Constraints constraints = Constraints::loose(viewport);
    measure(tree, root, constraints);
    place(tree, root, {0.0f, 0.0f, viewport.x, viewport.y});
}

MeasureResult LayoutEngine::measure(NodeTree& tree, NodeId id, Constraints constraints)
{
    UiNode* node = tree.get(id);
    if (!node)
        return {{0.0f, 0.0f}, std::nullopt, std::nullopt};

    if (!hasDirty(node->dirty, DirtyBits::Layout) && !hasDirty(node->dirty, DirtyBits::Text) && node->layout.measuredSize.x != 0.0f &&
        node->layout.measuredSize.y != 0.0f)
    {
        return {node->layout.measuredSize, node->layout.firstBaseline, node->layout.lastBaseline};
    }

    return measureNode(tree, *node, constraints);
}

void LayoutEngine::place(NodeTree& tree, NodeId id, Rect rect)
{
    UiNode* node = tree.get(id);
    if (!node)
        return;

    placeNode(tree, *node, rect);
}

MeasureResult LayoutEngine::measureNode(NodeTree& tree, UiNode& node, Constraints constraints)
{
    MeasureResult result{{0.0f, 0.0f}, std::nullopt, std::nullopt};

    switch (node.kind)
    {
    case WidgetKind::Window:
    case WidgetKind::Box:
    case WidgetKind::Canvas:
    {
        Vec2 childSize{0.0f, 0.0f};
        if (!node.children.empty())
        {
            MeasureResult child = measure(tree, node.children.front(), childConstraints(constraints, node.padding));
            childSize = child.size;
        }

        result.size = clampSize({childSize.x + node.padding.horizontal(), childSize.y + node.padding.vertical()}, constraints);
        break;
    }
    case WidgetKind::Column:
    {
        Constraints inner = childConstraints(constraints, node.padding);
        float width = 0.0f;
        float height = 0.0f;

        for (size_t i = 0; i < node.children.size(); i++)
        {
            MeasureResult child = measure(tree, node.children[i], inner);
            width = std::max(width, child.size.x);
            height += child.size.y;
            if (i + 1 < node.children.size())
                height += node.gap;
        }

        result.size = clampSize({width + node.padding.horizontal(), height + node.padding.vertical()}, constraints);
        break;
    }
    case WidgetKind::Row:
    {
        Constraints inner = childConstraints(constraints, node.padding);
        float width = 0.0f;
        float height = 0.0f;

        for (size_t i = 0; i < node.children.size(); i++)
        {
            MeasureResult child = measure(tree, node.children[i], inner);
            width += child.size.x;
            height = std::max(height, child.size.y);
            if (i + 1 < node.children.size())
                width += node.gap;
        }

        result.size = clampSize({width + node.padding.horizontal(), height + node.padding.vertical()}, constraints);
        break;
    }
    case WidgetKind::Text:
    {
        MeasureResult measured = textMeasure(node.text);
        result.size = clampSize(measured.size, constraints);
        result.firstBaseline = measured.firstBaseline;
        result.lastBaseline = measured.lastBaseline;
        break;
    }
    case WidgetKind::Button:
    {
        EdgeInsets padding =
            node.padding.horizontal() == 0.0f && node.padding.vertical() == 0.0f ? EdgeInsets{6.0f, 12.0f, 6.0f, 12.0f} : node.padding;
        MeasureResult label = textMeasure(node.text);
        result.size = clampSize({std::max(64.0f, label.size.x + padding.horizontal()), std::max(32.0f, label.size.y + padding.vertical())}, constraints);
        result.firstBaseline = padding.top + label.firstBaseline.value_or(15.0f);
        result.lastBaseline = result.firstBaseline;
        break;
    }
    }

    node.layout.measuredSize = result.size;
    node.layout.firstBaseline = result.firstBaseline;
    node.layout.lastBaseline = result.lastBaseline;
    node.layout.generation++;
    tree.clearDirty(node.id, DirtyBits::Layout | DirtyBits::Text);

    return result;
}

void LayoutEngine::placeNode(NodeTree& tree, UiNode& node, Rect rect)
{
    node.layout.frame = rect;

    switch (node.kind)
    {
    case WidgetKind::Window:
    case WidgetKind::Box:
    case WidgetKind::Canvas:
    {
        if (!node.children.empty())
        {
            UiNode* child = tree.get(node.children.front());
            if (child)
            {
                place(
                    tree,
                    child->id,
                    {rect.x + node.padding.left, rect.y + node.padding.top, child->layout.measuredSize.x, child->layout.measuredSize.y}
                );
            }
        }
        break;
    }
    case WidgetKind::Column:
    {
        float cursorY = rect.y + node.padding.top;
        for (NodeId childId : node.children)
        {
            UiNode* child = tree.get(childId);
            if (!child)
                continue;

            place(tree, childId, {rect.x + node.padding.left, cursorY, child->layout.measuredSize.x, child->layout.measuredSize.y});
            cursorY += child->layout.measuredSize.y + node.gap;
        }
        break;
    }
    case WidgetKind::Row:
    {
        float cursorX = rect.x + node.padding.left;
        for (NodeId childId : node.children)
        {
            UiNode* child = tree.get(childId);
            if (!child)
                continue;

            place(tree, childId, {cursorX, rect.y + node.padding.top, child->layout.measuredSize.x, child->layout.measuredSize.y});
            cursorX += child->layout.measuredSize.x + node.gap;
        }
        break;
    }
    case WidgetKind::Text:
    case WidgetKind::Button:
        break;
    }

    tree.clearDirty(node.id, DirtyBits::HitTest);
}

static void dumpLayoutNode(const NodeTree& tree, NodeId id, int depth, std::ostringstream& out)
{
    const UiNode* node = tree.get(id);
    if (!node)
        return;

    for (int i = 0; i < depth; i++)
        out << "  ";

    const Rect& frame = node->layout.frame;
    out << toString(node->kind) << "#" << node->id << " frame=(" << frame.x << "," << frame.y << "," << frame.width << "," << frame.height << ")"
        << " measured=(" << node->layout.measuredSize.x << "," << node->layout.measuredSize.y << ")"
        << " gen=" << node->layout.generation << "\n";

    for (NodeId child : node->children)
        dumpLayoutNode(tree, child, depth + 1, out);
}

std::string LayoutEngine::dump(const NodeTree& tree, NodeId root) const
{
    std::ostringstream out;
    dumpLayoutNode(tree, root, 0, out);
    return out.str();
}

} // namespace lute::ui
