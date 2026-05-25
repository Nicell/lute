#pragma once

#include "lute/ui/Core.h"
#include "lute/ui/Node.h"

#include <optional>
#include <string>

namespace lute::ui
{

enum class PointerEventKind
{
    Down,
    Up,
    Move,
    Leave,
    Cancel,
};

enum class PointerButton
{
    Primary,
    Secondary,
    Middle,
};

enum class KeyEventKind
{
    Down,
    Up,
};

enum class PhysicalKey
{
    Unknown,
    Tab,
    Enter,
    NumpadEnter,
    Space,
    Escape,
};

enum class LogicalKey
{
    Unknown,
    Tab,
    Enter,
    Space,
    Escape,
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
    uint32_t pointerId = 0;
};

struct KeyEvent
{
    KeyEventKind kind = KeyEventKind::Down;
    PhysicalKey physical = PhysicalKey::Unknown;
    LogicalKey logical = LogicalKey::Unknown;
    Modifiers modifiers;
    bool repeat = false;
};

struct TextInputEvent
{
    std::string text;
};

enum class ImeCompositionEventKind
{
    Start,
    Update,
    End,
    Cancel,
};

struct ImeCompositionEvent
{
    ImeCompositionEventKind kind = ImeCompositionEventKind::Update;
    std::string text;
    uint32_t selectionStart = 0;
    uint32_t selectionEnd = 0;
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
    bool dispatchPointer(NodeTree& tree, NodeId target, const PointerEvent& event, bool pointerInsideTarget) const;
    bool dispatchTextInput(NodeTree& tree, NodeId target, const TextInputEvent& event) const;
    bool dispatchImeComposition(NodeTree& tree, NodeId target, const ImeCompositionEvent& event) const;
    bool dispatchCommand(NodeTree& tree, NodeId target, Command command) const;
};

} // namespace lute::ui
