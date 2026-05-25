#include "lute/ui/Input.h"
#include "lute/ui/Style.h"

namespace lute::ui
{

static bool isInteractive(const UiNode& node)
{
    return node.focusable && !node.disabled;
}

std::optional<NodeId> InputRouter::hitTest(const NodeTree& tree, NodeId root, Vec2 point) const
{
    const UiNode* node = tree.get(root);
    if (!node || !widgetHitBounds(*node).contains(point))
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

bool InputRouter::dispatchPointer(NodeTree& tree, NodeId target, const PointerEvent& event, bool pointerInsideTarget) const
{
    if (event.kind != PointerEventKind::Up || !pointerInsideTarget)
        return false;

    return dispatchCommand(tree, target, Command::Activate);
}

bool InputRouter::dispatchTextInput(NodeTree& tree, NodeId target, const TextInputEvent& event) const
{
    UiNode* node = tree.get(target);
    if (!node || node->disabled || !node->onTextInput || event.text.empty())
        return false;

    node->onTextInput(event);
    return true;
}

bool InputRouter::dispatchImeComposition(NodeTree& tree, NodeId target, const ImeCompositionEvent& event) const
{
    UiNode* node = tree.get(target);
    if (!node || node->disabled || !node->onImeComposition)
        return false;

    node->onImeComposition(event);
    return true;
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
