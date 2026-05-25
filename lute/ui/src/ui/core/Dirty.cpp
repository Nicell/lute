#include "lute/ui/Core.h"

#include <array>
#include <sstream>

namespace lute::ui
{

std::string_view toString(WidgetKind kind)
{
    switch (kind)
    {
    case WidgetKind::Window:
        return "Window";
    case WidgetKind::Box:
        return "Box";
    case WidgetKind::Column:
        return "Column";
    case WidgetKind::Row:
        return "Row";
    case WidgetKind::Text:
        return "Text";
    case WidgetKind::Button:
        return "Button";
    case WidgetKind::Canvas:
        return "Canvas";
    }

    return "Unknown";
}

std::string dirtyBitsToString(DirtyBits bits)
{
    if (bits == DirtyBits::None)
        return "None";

    struct NamedBit
    {
        DirtyBits bit;
        std::string_view name;
    };

    constexpr std::array<NamedBit, 7> namedBits = {
        {{DirtyBits::State, "State"},
         {DirtyBits::Layout, "Layout"},
         {DirtyBits::Paint, "Paint"},
         {DirtyBits::Scene, "Scene"},
         {DirtyBits::Semantics, "Semantics"},
         {DirtyBits::HitTest, "HitTest"},
         {DirtyBits::Text, "Text"}}
    };

    std::ostringstream out;
    bool first = true;
    for (const NamedBit& namedBit : namedBits)
    {
        if (!hasDirty(bits, namedBit.bit))
            continue;

        if (!first)
            out << "|";

        out << namedBit.name;
        first = false;
    }

    return out.str();
}

} // namespace lute::ui
