#pragma once

#include "lute/ui/Core.h"
#include "lute/ui/Node.h"

#include <optional>

namespace lute::ui
{

enum class PointerEventKind
{
    Down,
    Up,
    Move,
};

enum class PointerButton
{
    Primary,
    Secondary,
    Middle,
};

struct Modifiers
{
    bool shift = false;
    bool control = false;
    bool alt = false;
    bool meta = false;
};

struct PointerEvent
{
    PointerEventKind kind = PointerEventKind::Move;
    Vec2 position;
    PointerButton button = PointerButton::Primary;
    Modifiers modifiers;
};

enum class Command
{
    Activate,
    Copy,
    Paste,
    SelectAll,
    FocusNext,
    FocusPrevious,
    Cancel,
};

class InputRouter
{
public:
    std::optional<NodeId> hitTest(const NodeTree& tree, NodeId root, Vec2 point) const;
    bool dispatchPointer(NodeTree& tree, NodeId root, const PointerEvent& event) const;
    bool dispatchCommand(NodeTree& tree, NodeId target, Command command) const;
};

} // namespace lute::ui
