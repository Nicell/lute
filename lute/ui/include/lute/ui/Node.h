#pragma once

#include "lute/ui/Core.h"

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace lute::ui
{

struct UiNode
{
    NodeId id = kInvalidNodeId;
    NodeId parent = kInvalidNodeId;
    std::vector<NodeId> children;

    WidgetKind kind = WidgetKind::Box;
    LayoutBox layout;
    DirtyBits dirty = kAllDirtyBits;

    std::string title;
    std::string text;
    float gap = 0.0f;
    EdgeInsets padding;
    bool focusable = false;
    bool disabled = false;
    bool pressed = false;
    void* widgetState = nullptr;

    std::function<void()> onActivate;
};

class NodeTree
{
public:
    NodeId createNode(WidgetKind kind);
    void appendChild(NodeId parent, NodeId child);
    void removeChildren(NodeId parent);

    UiNode* get(NodeId id);
    const UiNode* get(NodeId id) const;
    const std::unordered_map<NodeId, UiNode>& nodes() const;

    void markDirty(NodeId id, DirtyBits bits);
    void clearDirty(NodeId id, DirtyBits bits = kAllDirtyBits);

    void setTitle(NodeId id, std::string title);
    void setText(NodeId id, std::string text);
    void setGap(NodeId id, float gap);
    void setPadding(NodeId id, EdgeInsets padding);
    void setDisabled(NodeId id, bool disabled);
    void setOnActivate(NodeId id, std::function<void()> callback);

    std::string dump(NodeId root) const;

private:
    NodeId nextId = 1;
    std::unordered_map<NodeId, UiNode> nodeById;
};

} // namespace lute::ui
