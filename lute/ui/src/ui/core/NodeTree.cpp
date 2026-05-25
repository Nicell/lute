#include "lute/ui/Node.h"
#include "lute/ui/Profile.h"
#include "lute/ui/Text.h"

#include <algorithm>
#include <sstream>

namespace lute::ui
{

static void dumpNode(const NodeTree& tree, NodeId id, int depth, std::ostringstream& out)
{
    const UiNode* node = tree.get(id);
    if (!node)
        return;

    for (int i = 0; i < depth; i++)
        out << "  ";

    out << toString(node->kind) << "#" << node->id;
    if (!node->title.empty())
        out << " title=\"" << node->title << "\"";
    if (!node->text.empty())
        out << " text=\"" << node->text << "\"";
    if (node->gap != 0.0f)
        out << " gap=" << node->gap;
    if (node->padding.horizontal() != 0.0f || node->padding.vertical() != 0.0f)
        out << " padding=(" << node->padding.top << "," << node->padding.right << "," << node->padding.bottom << "," << node->padding.left << ")";
    if (node->focused)
        out << " focused";
    if (node->focusVisible)
        out << " focus-visible";
    out << " dirty=" << dirtyBitsToString(node->dirty) << "\n";

    for (NodeId child : node->children)
        dumpNode(tree, child, depth + 1, out);
}

NodeId NodeTree::createNode(WidgetKind kind)
{
    NodeId id = nextId++;
    UiNode node;
    node.id = id;
    node.kind = kind;
    node.focusable = kind == WidgetKind::Button;

    nodeById.emplace(id, std::move(node));
    return id;
}

void NodeTree::appendChild(NodeId parent, NodeId child)
{
    UiNode* parentNode = get(parent);
    UiNode* childNode = get(child);
    if (!parentNode || !childNode)
        return;

    if (childNode->parent != kInvalidNodeId && childNode->parent != parent)
    {
        UiNode* oldParent = get(childNode->parent);
        if (oldParent)
        {
            oldParent->children.erase(std::remove(oldParent->children.begin(), oldParent->children.end(), child), oldParent->children.end());
            markDirty(oldParent->id, DirtyBits::Layout | DirtyBits::Scene | DirtyBits::Semantics | DirtyBits::HitTest);
        }
    }

    if (std::find(parentNode->children.begin(), parentNode->children.end(), child) == parentNode->children.end())
        parentNode->children.push_back(child);

    childNode->parent = parent;
    markDirty(parent, DirtyBits::Layout | DirtyBits::Scene | DirtyBits::Semantics | DirtyBits::HitTest);
}

void NodeTree::removeChildren(NodeId parent)
{
    UiNode* parentNode = get(parent);
    if (!parentNode)
        return;

    for (NodeId child : parentNode->children)
    {
        if (UiNode* childNode = get(child))
            childNode->parent = kInvalidNodeId;
    }

    parentNode->children.clear();
    markDirty(parent, DirtyBits::Layout | DirtyBits::Scene | DirtyBits::Semantics | DirtyBits::HitTest);
}

UiNode* NodeTree::get(NodeId id)
{
    auto it = nodeById.find(id);
    if (it == nodeById.end())
        return nullptr;

    return &it->second;
}

const UiNode* NodeTree::get(NodeId id) const
{
    auto it = nodeById.find(id);
    if (it == nodeById.end())
        return nullptr;

    return &it->second;
}

const std::unordered_map<NodeId, UiNode>& NodeTree::nodes() const
{
    return nodeById;
}

bool NodeTree::hasDirty(NodeId root, DirtyBits bits) const
{
    const UiNode* node = get(root);
    if (!node)
        return false;

    if (lute::ui::hasDirty(node->dirty, bits))
        return true;

    for (NodeId child : node->children)
    {
        if (hasDirty(child, bits))
            return true;
    }

    return false;
}

void NodeTree::markDirty(NodeId id, DirtyBits bits)
{
    UiNode* node = get(id);
    if (!node)
        return;

    UiProfiler::addDirtyMark();

    DirtyBits expanded = bits;
    if (lute::ui::hasDirty(bits, DirtyBits::Text))
        expanded |= DirtyBits::Layout | DirtyBits::Paint | DirtyBits::Scene | DirtyBits::Semantics | DirtyBits::HitTest;
    if (lute::ui::hasDirty(bits, DirtyBits::Layout))
        expanded |= DirtyBits::Scene | DirtyBits::Semantics | DirtyBits::HitTest;
    if (lute::ui::hasDirty(bits, DirtyBits::Paint))
        expanded |= DirtyBits::Scene;
    if (lute::ui::hasDirty(bits, DirtyBits::State))
        expanded |= DirtyBits::Paint | DirtyBits::Scene | DirtyBits::Semantics;

    node->dirty |= expanded;

    DirtyBits ancestorBits = DirtyBits::None;
    if (lute::ui::hasDirty(expanded, DirtyBits::Layout) || lute::ui::hasDirty(expanded, DirtyBits::Text))
        ancestorBits |= DirtyBits::Layout | DirtyBits::Scene | DirtyBits::Semantics | DirtyBits::HitTest;
    if (lute::ui::hasDirty(expanded, DirtyBits::Paint) || lute::ui::hasDirty(expanded, DirtyBits::Scene))
        ancestorBits |= DirtyBits::Scene | DirtyBits::Paint;
    if (lute::ui::hasDirty(expanded, DirtyBits::Semantics))
        ancestorBits |= DirtyBits::Semantics;

    NodeId parent = node->parent;
    while (parent != kInvalidNodeId && ancestorBits != DirtyBits::None)
    {
        UiNode* parentNode = get(parent);
        if (!parentNode)
            break;

        parentNode->dirty |= ancestorBits;
        parent = parentNode->parent;
    }
}

void NodeTree::clearDirty(NodeId id, DirtyBits bits)
{
    UiNode* node = get(id);
    if (!node)
        return;

    node->dirty = static_cast<DirtyBits>(static_cast<uint32_t>(node->dirty) & ~static_cast<uint32_t>(bits));
}

void NodeTree::clearDirtySubtree(NodeId root, DirtyBits bits)
{
    UiNode* node = get(root);
    if (!node)
        return;

    clearDirty(root, bits);
    std::vector<NodeId> children = node->children;
    for (NodeId child : children)
        clearDirtySubtree(child, bits);
}

void NodeTree::setTitle(NodeId id, std::string title)
{
    UiNode* node = get(id);
    if (!node || node->title == title)
        return;

    node->title = std::move(title);
    markDirty(id, DirtyBits::Semantics);
}

void NodeTree::setText(NodeId id, std::string text)
{
    UiNode* node = get(id);
    if (!node || node->text == text)
        return;

    node->text = std::move(text);
    node->shapedText.reset();
    markDirty(id, DirtyBits::Text | DirtyBits::Layout | DirtyBits::Paint | DirtyBits::Scene | DirtyBits::Semantics | DirtyBits::HitTest);
}

void NodeTree::setGap(NodeId id, float gap)
{
    UiNode* node = get(id);
    if (!node || node->gap == gap)
        return;

    node->gap = gap;
    markDirty(id, DirtyBits::Layout | DirtyBits::Scene | DirtyBits::HitTest);
}

void NodeTree::setPadding(NodeId id, EdgeInsets padding)
{
    UiNode* node = get(id);
    if (!node)
        return;

    if (node->padding.top == padding.top && node->padding.right == padding.right && node->padding.bottom == padding.bottom &&
        node->padding.left == padding.left)
        return;

    node->padding = padding;
    markDirty(id, DirtyBits::Layout | DirtyBits::Scene | DirtyBits::HitTest);
}

void NodeTree::setDisabled(NodeId id, bool disabled)
{
    UiNode* node = get(id);
    if (!node || node->disabled == disabled)
        return;

    node->disabled = disabled;
    markDirty(id, DirtyBits::State | DirtyBits::Paint | DirtyBits::Scene | DirtyBits::Semantics);
}

void NodeTree::setFocused(NodeId id, bool focused, bool focusVisible)
{
    UiNode* node = get(id);
    if (!node)
        return;

    if (!focused)
        focusVisible = false;

    if (node->focused == focused && node->focusVisible == focusVisible)
        return;

    node->focused = focused;
    node->focusVisible = focusVisible;
    markDirty(id, DirtyBits::State | DirtyBits::Paint | DirtyBits::Scene | DirtyBits::Semantics);
}

void NodeTree::setOnActivate(NodeId id, std::function<void()> callback)
{
    UiNode* node = get(id);
    if (!node)
        return;

    node->onActivate = std::move(callback);
    node->focusable = true;
    markDirty(id, DirtyBits::Semantics);
}

std::string NodeTree::dump(NodeId root) const
{
    std::ostringstream out;
    dumpNode(*this, root, 0, out);
    return out.str();
}

} // namespace lute::ui
