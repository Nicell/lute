#include "lute/ui/Platform.h"

#include "lute/ui/Context.h"

#include <algorithm>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace lute::ui
{

namespace
{

struct AccessKitNodeStub
{
    SemanticNodeId id = 0;
    std::optional<SemanticNodeId> parent;
    std::vector<SemanticNodeId> children;
    Role role = Role::Group;
    std::string label;
    std::string value;
    Rect bounds;
    bool focusable = false;
    bool focused = false;
    bool disabled = false;
    std::vector<SemanticAction> actions;
};

AccessKitNodeStub exportNode(const SemanticNode& node)
{
    return {
        node.id,
        node.parent,
        node.children,
        node.role,
        node.label,
        node.value,
        node.bounds,
        node.focusable,
        node.focused,
        node.disabled,
        node.actions,
    };
}

class AccessKitBridgeStub final : public NativeAccessibilityBridge
{
public:
    void syncTree(const SemanticTree& tree) override
    {
        syncedGeneration = tree.generation();
        exportedNodes.clear();
        exportedNodes.reserve(tree.nodes().size());

        for (const SemanticNode& node : tree.nodes())
            exportedNodes.emplace(node.id, exportNode(node));
    }

    bool performAction(UiContext& context, SemanticNodeId id, SemanticAction action) override
    {
        auto found = exportedNodes.find(id);
        if (found == exportedNodes.end())
            return false;

        const AccessKitNodeStub& node = found->second;
        if (node.disabled)
            return false;

        if (std::find(node.actions.begin(), node.actions.end(), action) == node.actions.end())
            return false;

        bool handled = dispatchNativeAccessibilityAction(context, id, action);
        if (handled)
            syncTree(context.currentSemantics());
        return handled;
    }

private:
    uint64_t syncedGeneration = 0;
    std::unordered_map<SemanticNodeId, AccessKitNodeStub> exportedNodes;
};

} // namespace

NativeAccessibilityBridge& nativeAccessibilityBridge()
{
    static AccessKitBridgeStub bridge;
    return bridge;
}

} // namespace lute::ui
