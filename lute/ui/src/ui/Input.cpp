#include "lute/ui/Input.h"

namespace lute::ui
{

static bool isInteractive(const UiNode& node)
{
    return node.kind == WidgetKind::Button && !node.disabled;
}

std::optional<NodeId> InputRouter::hitTest(const NodeTree& tree, NodeId root, Vec2 point) const
{
    const UiNode* node = tree.get(root);
    if (!node || !node->layout.frame.contains(point))
        return std::nullopt;

    for (auto it = node->children.rbegin(); it != node->children.rend(); ++it)
    {
        if (std::optional<NodeId> child = hitTest(tree, *it, point))
            return child;
    }

    if (isInteractive(*node))
        return node->id;

    return std::nullopt;
}

bool InputRouter::dispatchPointer(NodeTree& tree, NodeId root, const PointerEvent& event) const
{
    if (event.kind != PointerEventKind::Up)
        return false;

    std::optional<NodeId> target = hitTest(tree, root, event.position);
    if (!target)
        return false;

    return dispatchCommand(tree, *target, Command::Activate);
}

bool InputRouter::dispatchCommand(NodeTree& tree, NodeId target, Command command) const
{
    UiNode* node = tree.get(target);
    if (!node || node->disabled)
        return false;

    if (command != Command::Activate)
        return false;

    if (node->onActivate)
    {
        node->onActivate();
        return true;
    }

    return false;
}

} // namespace lute::ui
