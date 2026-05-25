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
    nodeCache.clear();
    cachedRoot = kInvalidNodeId;
    retained = false;
    semanticGeneration++;
}

const std::vector<SemanticNode>& SemanticTree::nodes() const
{
    return semanticNodes;
}

uint64_t SemanticTree::generation() const
{
    return semanticGeneration;
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

bool SemanticsBuilder::update(NodeTree& tree, NodeId root, SemanticTree& semantics) const
{
    if (root == kInvalidNodeId || !tree.get(root))
    {
        if (semantics.semanticNodes.empty() && semantics.nodeCache.empty())
            return false;

        semantics.semanticNodes.clear();
        semantics.nodeCache.clear();
        semantics.cachedRoot = kInvalidNodeId;
        semantics.retained = true;
        semantics.semanticGeneration++;
        return true;
    }

    bool force = !semantics.retained || semantics.cachedRoot != root;
    if (force)
    {
        semantics.nodeCache.clear();
        semantics.cachedRoot = root;
        semantics.retained = true;
    }
    else if (!tree.hasDirty(root, DirtyBits::Semantics | DirtyBits::Layout | DirtyBits::Text | DirtyBits::State))
    {
        return false;
    }

    bool changed = updateNode(tree, root, std::nullopt, force, semantics);
    if (!changed)
        return false;

    std::vector<SemanticNode> nodes;
    flattenNode(tree, root, semantics, nodes);
    semantics.semanticNodes = std::move(nodes);
    semantics.semanticGeneration++;
    return true;
}

SemanticNode SemanticsBuilder::makeNode(const UiNode& node, std::optional<SemanticNodeId> parent) const
{
    SemanticNode semantic;
    semantic.id = node.id;
    semantic.parent = parent;
    semantic.role = roleForWidget(node.kind);
    semantic.bounds = node.layout.frame;
    semantic.focusable = node.focusable;
    semantic.disabled = node.disabled;

    if (node.kind == WidgetKind::Window)
        semantic.label = node.title;
    else if (node.kind == WidgetKind::Text)
        semantic.value = node.text;
    else if (node.kind == WidgetKind::Button)
    {
        semantic.label = node.text;
        semantic.actions.push_back(SemanticAction::Focus);
        semantic.actions.push_back(SemanticAction::Activate);
    }

    for (NodeId child : node.children)
        semantic.children.push_back(child);

    return semantic;
}

void SemanticsBuilder::emitNode(const NodeTree& tree, NodeId id, std::optional<SemanticNodeId> parent, std::vector<SemanticNode>& out) const
{
    const UiNode* node = tree.get(id);
    if (!node)
        return;

    out.push_back(makeNode(*node, parent));

    for (NodeId child : node->children)
        emitNode(tree, child, id, out);
}

bool SemanticsBuilder::updateNode(NodeTree& tree, NodeId id, std::optional<SemanticNodeId> parent, bool force, SemanticTree& semantics) const
{
    const UiNode* node = tree.get(id);
    if (!node)
        return false;

    auto found = semantics.nodeCache.find(id);
    bool missing = found == semantics.nodeCache.end();
    SemanticTree::CachedNode previous = missing ? SemanticTree::CachedNode{} : found->second;
    bool childrenChanged = missing || previous.children != node->children;
    bool parentChanged = missing || previous.parent != parent;
    bool dirty = hasDirty(node->dirty, DirtyBits::Semantics | DirtyBits::Layout | DirtyBits::Text | DirtyBits::State);
    bool shouldEmit = force || missing || childrenChanged || parentChanged || dirty;
    bool changed = false;

    if (shouldEmit)
    {
        SemanticTree::CachedNode next;
        next.node = makeNode(*node, parent);
        next.children = node->children;
        next.parent = parent;
        semantics.nodeCache[id] = std::move(next);
        changed = true;
    }

    bool forceChildren = force || missing || childrenChanged || parentChanged;
    if (forceChildren || dirty)
    {
        for (NodeId child : node->children)
            changed = updateNode(tree, child, id, forceChildren, semantics) || changed;
    }

    return changed;
}

void SemanticsBuilder::flattenNode(const NodeTree& tree, NodeId id, const SemanticTree& semantics, std::vector<SemanticNode>& out) const
{
    const UiNode* node = tree.get(id);
    if (!node)
        return;

    auto found = semantics.nodeCache.find(id);
    if (found != semantics.nodeCache.end())
        out.push_back(found->second.node);

    for (NodeId child : node->children)
        flattenNode(tree, child, semantics, out);
}

} // namespace lute::ui
