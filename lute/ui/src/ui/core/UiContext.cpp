#include "lute/ui/Context.h"

#include <algorithm>
#include <memory>
#include <vector>

namespace lute::ui
{

namespace
{

bool isFocusableNode(const UiNode* node)
{
    return node && node->focusable && !node->disabled;
}

bool containsNode(const NodeTree& tree, NodeId root, NodeId target)
{
    const UiNode* node = tree.get(root);
    if (!node)
        return false;

    if (root == target)
        return true;

    for (NodeId child : node->children)
    {
        if (containsNode(tree, child, target))
            return true;
    }

    return false;
}

void collectFocusableNodes(const NodeTree& tree, NodeId root, std::vector<NodeId>& out)
{
    const UiNode* node = tree.get(root);
    if (!node)
        return;

    if (isFocusableNode(node))
        out.push_back(root);

    for (NodeId child : node->children)
        collectFocusableNodes(tree, child, out);
}

} // namespace

ReactiveGraph& UiContext::reactive()
{
    return graph;
}

NodeTree& UiContext::nodes()
{
    return tree;
}

const NodeTree& UiContext::nodes() const
{
    return tree;
}

NodeId UiContext::createNode(WidgetKind kind)
{
    return tree.createNode(kind);
}

void UiContext::setRoot(NodeId id)
{
    rootNode = id;
}

NodeId UiContext::root() const
{
    return rootNode;
}

void UiContext::setViewport(Vec2 size)
{
    if (viewportSize == size)
        return;

    viewportSize = size;
    if (rootNode != kInvalidNodeId)
        tree.markDirty(rootNode, DirtyBits::Layout | DirtyBits::Scene | DirtyBits::HitTest);
}

Vec2 UiContext::viewport() const
{
    return viewportSize;
}

void UiContext::flush()
{
    std::unique_ptr<ProfileFrameScope> frame;
    if (!UiProfiler::isFrameActive())
        frame = std::make_unique<ProfileFrameScope>(profileStore, nextFrameId(), "flush");

    ProfileZone flushZone(ProfilePhase::Flush);

    NodeId id = resolveRoot(kInvalidNodeId);
    if (id == kInvalidNodeId)
        return;

    if (tree.hasDirty(id, DirtyBits::Layout | DirtyBits::Text))
    {
        ProfileZone zone(ProfilePhase::Layout);
        layoutEngine.layout(tree, id, viewportSize);
    }

    if (scene.generation() == 0 || tree.hasDirty(id, DirtyBits::Scene | DirtyBits::Paint | DirtyBits::State))
    {
        ProfileZone zone(ProfilePhase::Scene);
        sceneBuilder.update(tree, id, scene);
        tree.clearDirtySubtree(id, DirtyBits::Paint | DirtyBits::Scene);
    }

    if (semantics.generation() == 0 || tree.hasDirty(id, DirtyBits::Semantics | DirtyBits::State))
    {
        ProfileZone zone(ProfilePhase::Semantics);
        semanticsBuilder.update(tree, id, semantics);
        tree.clearDirtySubtree(id, DirtyBits::Semantics);
    }

    tree.clearDirtySubtree(id, DirtyBits::State);
}

RenderStats UiContext::render()
{
    std::unique_ptr<ProfileFrameScope> frame;
    if (!UiProfiler::isFrameActive())
        frame = std::make_unique<ProfileFrameScope>(profileStore, nextFrameId(), "render");

    flush();

    RenderStats stats;
    {
        ProfileZone zone(ProfilePhase::Render);
        stats = renderer.render(scene);
    }
    UiProfiler::setRenderStats(stats.backend, stats.displayItemCount, stats.sceneGeneration);
    return stats;
}

RenderStats UiContext::renderToMetalLayer(void* metalLayer, uint32_t pixelWidth, uint32_t pixelHeight, float scale)
{
    std::unique_ptr<ProfileFrameScope> frame;
    if (!UiProfiler::isFrameActive())
        frame = std::make_unique<ProfileFrameScope>(profileStore, nextFrameId(), "renderToMetalLayer");

    flush();

    RenderStats stats;
    {
        ProfileZone zone(ProfilePhase::Render);
        stats = renderer.renderToMetalLayer(scene, metalLayer, pixelWidth, pixelHeight, scale);
    }
    UiProfiler::setRenderStats(stats.backend, stats.displayItemCount, stats.sceneGeneration);
    return stats;
}

void UiContext::retainEffect(std::shared_ptr<Effect> effect)
{
    retainedEffects.push_back(std::move(effect));
}

std::string UiContext::dumpProfile(size_t maxFrames) const
{
    return profileStore.dump(maxFrames);
}

const ProfileStore& UiContext::profiles() const
{
    return profileStore;
}

bool UiContext::activate(NodeId id)
{
    std::unique_ptr<ProfileFrameScope> frame;
    if (!UiProfiler::isFrameActive())
        frame = std::make_unique<ProfileFrameScope>(profileStore, nextFrameId(), "activate");

    ProfileZone zone(ProfilePhase::Input);
    return inputRouter.dispatchCommand(tree, id, Command::Activate);
}

bool UiContext::focus(NodeId id)
{
    flush();
    NodeId root = resolveRoot(kInvalidNodeId);
    if (id == kInvalidNodeId || root == kInvalidNodeId || !containsNode(tree, root, id) || !isFocusableNode(tree.get(id)))
        return false;

    setFocusedNode(id, true);
    return true;
}

bool UiContext::clearFocus()
{
    return setFocusedNode(kInvalidNodeId, false);
}

std::optional<NodeId> UiContext::focusedNode() const
{
    if (focusedNodeId == kInvalidNodeId)
        return std::nullopt;

    NodeId root = resolveRoot(kInvalidNodeId);
    if (root == kInvalidNodeId || !containsNode(tree, root, focusedNodeId) || !isFocusableNode(tree.get(focusedNodeId)))
        return std::nullopt;

    return focusedNodeId;
}

bool UiContext::dispatchPointer(const PointerEvent& event, NodeId root)
{
    std::unique_ptr<ProfileFrameScope> frame;
    if (!UiProfiler::isFrameActive())
        frame = std::make_unique<ProfileFrameScope>(profileStore, nextFrameId(), "pointer");

    flush();
    NodeId id = resolveRoot(root);
    if (id == kInvalidNodeId)
        return false;

    clearInvalidFocus(id);

    bool focusChanged = false;
    {
        ProfileZone zone(ProfilePhase::Input);
        if (event.kind == PointerEventKind::Down || event.kind == PointerEventKind::Up)
        {
            std::optional<NodeId> target = inputRouter.hitTest(tree, id, event.position);
            if (target && isFocusableNode(tree.get(*target)))
                focusChanged = setFocusedNode(*target, false);
            else if (event.kind == PointerEventKind::Down)
                focusChanged = clearFocus();
        }

        bool handled = inputRouter.dispatchPointer(tree, id, event);
        return handled || focusChanged;
    }
}

bool UiContext::dispatchKey(const KeyEvent& event, NodeId root)
{
    std::unique_ptr<ProfileFrameScope> frame;
    if (!UiProfiler::isFrameActive())
        frame = std::make_unique<ProfileFrameScope>(profileStore, nextFrameId(), "key");

    flush();
    NodeId id = resolveRoot(root);
    if (id == kInvalidNodeId)
        return false;

    ProfileZone zone(ProfilePhase::Input);

    clearInvalidFocus(id);

    if (event.kind == KeyEventKind::Down && event.logical == LogicalKey::Tab)
        return focusNext(id, event.modifiers.shift);

    if (focusedNodeId == kInvalidNodeId || !isFocusableNode(tree.get(focusedNodeId)))
        return false;

    if (event.logical == LogicalKey::Enter)
    {
        if (event.kind != KeyEventKind::Down || event.repeat)
            return event.kind == KeyEventKind::Down;

        return inputRouter.dispatchCommand(tree, focusedNodeId, Command::Activate);
    }

    if (event.logical == LogicalKey::Space)
    {
        if (event.kind == KeyEventKind::Down)
            return true;

        return inputRouter.dispatchCommand(tree, focusedNodeId, Command::Activate);
    }

    return false;
}

const Scene& UiContext::currentScene() const
{
    return scene;
}

const SemanticTree& UiContext::currentSemantics() const
{
    return semantics;
}

std::string UiContext::dumpUiTree(NodeId root) const
{
    return tree.dump(resolveRoot(root));
}

std::string UiContext::dumpLayoutTree(NodeId root)
{
    flush();
    return layoutEngine.dump(tree, resolveRoot(root));
}

std::string UiContext::dumpScene(NodeId root)
{
    NodeId previousRoot = rootNode;
    if (root != kInvalidNodeId)
        rootNode = root;
    flush();
    std::string dump = scene.dump();
    rootNode = previousRoot;
    return dump;
}

std::string UiContext::dumpSemantics(NodeId root)
{
    NodeId previousRoot = rootNode;
    if (root != kInvalidNodeId)
        rootNode = root;
    flush();
    std::string dump = semantics.dump();
    rootNode = previousRoot;
    return dump;
}

void UiContext::setLastError(std::string error)
{
    lastError = std::move(error);
}

std::optional<std::string> UiContext::takeLastError()
{
    std::optional<std::string> result = std::move(lastError);
    lastError.reset();
    return result;
}

NodeId UiContext::resolveRoot(NodeId requested) const
{
    if (requested != kInvalidNodeId)
        return requested;

    return rootNode;
}

ProfileFrameId UiContext::nextFrameId()
{
    return nextProfileFrameId++;
}

bool UiContext::setFocusedNode(NodeId id, bool focusVisible)
{
    if (focusedNodeId == id)
    {
        const UiNode* node = tree.get(id);
        bool changed = node && (node->focused != (id != kInvalidNodeId) || node->focusVisible != focusVisible);
        if (id != kInvalidNodeId)
            tree.setFocused(id, true, focusVisible);
        return changed;
    }

    NodeId previous = focusedNodeId;
    focusedNodeId = id;

    if (previous != kInvalidNodeId)
        tree.setFocused(previous, false, false);
    if (focusedNodeId != kInvalidNodeId)
        tree.setFocused(focusedNodeId, true, focusVisible);

    return true;
}

bool UiContext::focusNext(NodeId root, bool reverse)
{
    std::vector<NodeId> focusable;
    collectFocusableNodes(tree, root, focusable);

    if (focusable.empty())
    {
        clearFocus();
        return false;
    }

    auto found = std::find(focusable.begin(), focusable.end(), focusedNodeId);
    std::size_t nextIndex = 0;

    if (found == focusable.end())
    {
        nextIndex = reverse ? focusable.size() - 1 : 0;
    }
    else if (reverse)
    {
        nextIndex = found == focusable.begin() ? focusable.size() - 1 : static_cast<std::size_t>((found - focusable.begin()) - 1);
    }
    else
    {
        nextIndex = static_cast<std::size_t>((found - focusable.begin() + 1) % focusable.size());
    }

    setFocusedNode(focusable[nextIndex], true);
    return true;
}

void UiContext::clearInvalidFocus(NodeId root)
{
    if (focusedNodeId == kInvalidNodeId)
        return;

    if (!containsNode(tree, root, focusedNodeId) || !isFocusableNode(tree.get(focusedNodeId)))
        clearFocus();
}

} // namespace lute::ui
