#include "lute/ui/Accessibility.h"

#include <sstream>

namespace lute::ui
{

static Role roleForWidget(WidgetKind kind)
{
    switch (kind)
    {
    case WidgetKind::Window:
        return Role::Window;
    case WidgetKind::Text:
        return Role::Text;
    case WidgetKind::Button:
        return Role::Button;
    case WidgetKind::Box:
    case WidgetKind::Column:
    case WidgetKind::Row:
    case WidgetKind::Canvas:
        return Role::Group;
    }

    return Role::Group;
}

static std::string_view roleToString(Role role)
{
    switch (role)
    {
    case Role::Window:
        return "Window";
    case Role::Group:
        return "Group";
    case Role::Text:
        return "Text";
    case Role::Button:
        return "Button";
    }

    return "Unknown";
}

void SemanticTree::replace(std::vector<SemanticNode> nextNodes)
{
    semanticNodes = std::move(nextNodes);
}

const std::vector<SemanticNode>& SemanticTree::nodes() const
{
    return semanticNodes;
}

std::string SemanticTree::dump() const
{
    std::ostringstream out;
    out << "Semantics\n";

    for (const SemanticNode& node : semanticNodes)
    {
        out << roleToString(node.role) << "#" << node.id;
        if (node.parent)
            out << " parent=" << *node.parent;
        if (!node.label.empty())
            out << " label=\"" << node.label << "\"";
        if (!node.value.empty())
            out << " value=\"" << node.value << "\"";
        out << " bounds=(" << node.bounds.x << "," << node.bounds.y << "," << node.bounds.width << "," << node.bounds.height << ")";
        if (node.focusable)
            out << " focusable";
        if (node.disabled)
            out << " disabled";
        if (!node.actions.empty())
            out << " actions=" << node.actions.size();
        out << "\n";
    }

    return out.str();
}

SemanticTree SemanticsBuilder::build(const NodeTree& tree, NodeId root) const
{
    std::vector<SemanticNode> nodes;
    emitNode(tree, root, std::nullopt, nodes);

    SemanticTree result;
    result.replace(std::move(nodes));
    return result;
}

void SemanticsBuilder::emitNode(const NodeTree& tree, NodeId id, std::optional<SemanticNodeId> parent, std::vector<SemanticNode>& out) const
{
    const UiNode* node = tree.get(id);
    if (!node)
        return;

    SemanticNode semantic;
    semantic.id = node->id;
    semantic.parent = parent;
    semantic.role = roleForWidget(node->kind);
    semantic.bounds = node->layout.frame;
    semantic.focusable = node->focusable;
    semantic.disabled = node->disabled;

    if (node->kind == WidgetKind::Window)
        semantic.label = node->title;
    else if (node->kind == WidgetKind::Text)
        semantic.value = node->text;
    else if (node->kind == WidgetKind::Button)
    {
        semantic.label = node->text;
        semantic.actions.push_back(SemanticAction::Focus);
        semantic.actions.push_back(SemanticAction::Activate);
    }

    for (NodeId child : node->children)
        semantic.children.push_back(child);

    out.push_back(std::move(semantic));

    for (NodeId child : node->children)
        emitNode(tree, child, id, out);
}

} // namespace lute::ui
