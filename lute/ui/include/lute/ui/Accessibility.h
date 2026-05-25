#pragma once

#include "lute/ui/Core.h"
#include "lute/ui/Node.h"

#include <optional>
#include <string>
#include <vector>

namespace lute::ui
{

using SemanticNodeId = uint64_t;

enum class Role
{
    Window,
    Group,
    Text,
    Button,
};

enum class SemanticAction
{
    Focus,
    Activate,
};

struct SemanticNode
{
    SemanticNodeId id = 0;
    std::optional<SemanticNodeId> parent;
    std::vector<SemanticNodeId> children;

    Role role = Role::Group;
    std::string label;
    std::string value;
    Rect bounds;

    bool focusable = false;
    bool disabled = false;
    std::vector<SemanticAction> actions;
};

class SemanticTree
{
public:
    void replace(std::vector<SemanticNode> nextNodes);
    const std::vector<SemanticNode>& nodes() const;
    std::string dump() const;

private:
    std::vector<SemanticNode> semanticNodes;
};

class SemanticsBuilder
{
public:
    SemanticTree build(const NodeTree& tree, NodeId root) const;

private:
    void emitNode(const NodeTree& tree, NodeId id, std::optional<SemanticNodeId> parent, std::vector<SemanticNode>& out) const;
};

} // namespace lute::ui
