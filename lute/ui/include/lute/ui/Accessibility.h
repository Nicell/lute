#pragma once

#include "lute/ui/Core.h"
#include "lute/ui/Node.h"

#include <optional>
#include <string>
#include <unordered_map>
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
    uint64_t generation() const;
    std::string dump() const;

private:
    struct CachedNode
    {
        SemanticNode node;
        std::vector<NodeId> children;
        std::optional<SemanticNodeId> parent;
    };

    friend class SemanticsBuilder;

    std::vector<SemanticNode> semanticNodes;
    std::unordered_map<NodeId, CachedNode> nodeCache;
    NodeId cachedRoot = kInvalidNodeId;
    uint64_t semanticGeneration = 0;
    bool retained = false;
};

class SemanticsBuilder
{
public:
    SemanticTree build(const NodeTree& tree, NodeId root) const;
    bool update(NodeTree& tree, NodeId root, SemanticTree& semantics) const;

private:
    SemanticNode makeNode(const UiNode& node, std::optional<SemanticNodeId> parent) const;
    void emitNode(const NodeTree& tree, NodeId id, std::optional<SemanticNodeId> parent, std::vector<SemanticNode>& out) const;
    bool updateNode(NodeTree& tree, NodeId id, std::optional<SemanticNodeId> parent, bool force, SemanticTree& semantics) const;
    void flattenNode(const NodeTree& tree, NodeId id, const SemanticTree& semantics, std::vector<SemanticNode>& out) const;
};

} // namespace lute::ui
