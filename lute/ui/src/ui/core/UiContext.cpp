#include "lute/ui/Context.h"

#include "lute/ui/Style.h"

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

bool isPointerInteractiveNode(const UiNode* node)
{
    return node && node->focusable && !node->disabled;
}

bool pointInsideNode(const UiNode* node, Vec2 point)
{
    return node && widgetHitBounds(*node).contains(point);
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

    clearInvalidInteractionState(id);

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

std::optional<NodeId> UiContext::hoveredNode() const
{
    if (hoveredNodeId == kInvalidNodeId)
        return std::nullopt;

    NodeId root = resolveRoot(kInvalidNodeId);
    if (root == kInvalidNodeId || !containsNode(tree, root, hoveredNodeId) || !isPointerInteractiveNode(tree.get(hoveredNodeId)))
        return std::nullopt;

    return hoveredNodeId;
}

std::optional<NodeId> UiContext::pressedNode() const
{
    if (pressedNodeId == kInvalidNodeId)
        return std::nullopt;

    NodeId root = resolveRoot(kInvalidNodeId);
    if (root == kInvalidNodeId || !containsNode(tree, root, pressedNodeId) || !isPointerInteractiveNode(tree.get(pressedNodeId)))
        return std::nullopt;

    return pressedNodeId;
}

std::optional<NodeId> UiContext::capturedPointerNode() const
{
    if (pointerCaptureNodeId == kInvalidNodeId)
        return std::nullopt;

    NodeId root = resolveRoot(kInvalidNodeId);
    if (root == kInvalidNodeId || !containsNode(tree, root, pointerCaptureNodeId) || !isPointerInteractiveNode(tree.get(pointerCaptureNodeId)))
        return std::nullopt;

    return pointerCaptureNodeId;
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

    clearInvalidInteractionState(id);

    bool stateChanged = false;
    bool handled = false;
    {
        ProfileZone zone(ProfilePhase::Input);

        std::optional<NodeId> hit;
        if (event.kind != PointerEventKind::Leave && event.kind != PointerEventKind::Cancel)
            hit = inputRouter.hitTest(tree, id, event.position);

        auto pointerInside = [&](NodeId nodeId) {
            return pointInsideNode(tree.get(nodeId), event.position);
        };

        switch (event.kind)
        {
        case PointerEventKind::Down:
        {
            stateChanged = setHoveredNode(hit.value_or(kInvalidNodeId)) || stateChanged;
            if (hit && isFocusableNode(tree.get(*hit)))
                stateChanged = setFocusedNode(*hit, false) || stateChanged;
            else
                stateChanged = clearFocus() || stateChanged;

            if (hit && isPointerInteractiveNode(tree.get(*hit)))
            {
                pointerCaptureNodeId = *hit;
                stateChanged = setPressedNode(*hit) || stateChanged;
                handled = inputRouter.dispatchPointer(tree, *hit, event, true);
            }
            break;
        }
        case PointerEventKind::Move:
        {
            stateChanged = setHoveredNode(hit.value_or(kInvalidNodeId)) || stateChanged;
            if (pointerCaptureNodeId != kInvalidNodeId)
            {
                bool insideCapture = pointerInside(pointerCaptureNodeId);
                stateChanged = setPressedNode(insideCapture ? pointerCaptureNodeId : kInvalidNodeId) || stateChanged;
                handled = inputRouter.dispatchPointer(tree, pointerCaptureNodeId, event, insideCapture);
            }
            break;
        }
        case PointerEventKind::Up:
        {
            stateChanged = setHoveredNode(hit.value_or(kInvalidNodeId)) || stateChanged;
            if (pointerCaptureNodeId != kInvalidNodeId)
            {
                NodeId target = pointerCaptureNodeId;
                bool insideCapture = pointerInside(target);
                pointerCaptureNodeId = kInvalidNodeId;
                stateChanged = true;
                stateChanged = setPressedNode(kInvalidNodeId) || stateChanged;
                handled = inputRouter.dispatchPointer(tree, target, event, insideCapture);
            }
            else if (hit)
            {
                if (isFocusableNode(tree.get(*hit)))
                    stateChanged = setFocusedNode(*hit, false) || stateChanged;
                handled = inputRouter.dispatchPointer(tree, *hit, event, true);
            }
            break;
        }
        case PointerEventKind::Leave:
            stateChanged = setHoveredNode(kInvalidNodeId) || stateChanged;
            if (pointerCaptureNodeId != kInvalidNodeId)
                stateChanged = setPressedNode(kInvalidNodeId) || stateChanged;
            break;
        case PointerEventKind::Cancel:
            if (pointerCaptureNodeId != kInvalidNodeId)
            {
                pointerCaptureNodeId = kInvalidNodeId;
                stateChanged = true;
            }
            stateChanged = setHoveredNode(kInvalidNodeId) || stateChanged;
            stateChanged = setPressedNode(kInvalidNodeId) || stateChanged;
            break;
        }

        return handled || stateChanged;
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

    clearInvalidInteractionState(id);

    if (event.kind == KeyEventKind::Down && event.logical == LogicalKey::Tab)
        return focusNext(id, event.modifiers.shift);

    if (focusedNodeId == kInvalidNodeId || !isFocusableNode(tree.get(focusedNodeId)))
        return false;

    const UiNode* focusedNode = tree.get(focusedNodeId);
    bool canActivateFocusedNode = focusedNode && focusedNode->onActivate != nullptr;

    if (event.logical == LogicalKey::Enter)
    {
        if (!canActivateFocusedNode)
            return false;
        if (event.kind != KeyEventKind::Down || event.repeat)
            return event.kind == KeyEventKind::Down;

        return inputRouter.dispatchCommand(tree, focusedNodeId, Command::Activate);
    }

    if (event.logical == LogicalKey::Space)
    {
        if (!canActivateFocusedNode)
            return false;
        if (event.kind == KeyEventKind::Down)
        {
            setPressedNode(focusedNodeId);
            return true;
        }

        bool stateChanged = setPressedNode(kInvalidNodeId);
        bool handled = inputRouter.dispatchCommand(tree, focusedNodeId, Command::Activate);
        return handled || stateChanged;
    }

    return false;
}

bool UiContext::dispatchTextInput(const TextInputEvent& event, NodeId root)
{
    std::unique_ptr<ProfileFrameScope> frame;
    if (!UiProfiler::isFrameActive())
        frame = std::make_unique<ProfileFrameScope>(profileStore, nextFrameId(), "text_input");

    flush();
    NodeId id = resolveRoot(root);
    if (id == kInvalidNodeId)
        return false;

    ProfileZone zone(ProfilePhase::Input);

    clearInvalidInteractionState(id);
    if (focusedNodeId == kInvalidNodeId || !isFocusableNode(tree.get(focusedNodeId)))
        return false;

    return inputRouter.dispatchTextInput(tree, focusedNodeId, event);
}

bool UiContext::dispatchImeComposition(const ImeCompositionEvent& event, NodeId root)
{
    std::unique_ptr<ProfileFrameScope> frame;
    if (!UiProfiler::isFrameActive())
        frame = std::make_unique<ProfileFrameScope>(profileStore, nextFrameId(), "ime");

    flush();
    NodeId id = resolveRoot(root);
    if (id == kInvalidNodeId)
        return false;

    ProfileZone zone(ProfilePhase::Input);

    clearInvalidInteractionState(id);
    if (focusedNodeId == kInvalidNodeId || !isFocusableNode(tree.get(focusedNodeId)))
        return false;

    return inputRouter.dispatchImeComposition(tree, focusedNodeId, event);
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
    if (pointerCaptureNodeId == kInvalidNodeId && pressedNodeId != kInvalidNodeId && pressedNodeId != focusedNodeId)
        setPressedNode(kInvalidNodeId);

    return true;
}

bool UiContext::setHoveredNode(NodeId id)
{
    if (id != kInvalidNodeId && !isPointerInteractiveNode(tree.get(id)))
        id = kInvalidNodeId;

    if (hoveredNodeId == id)
        return false;

    NodeId previous = hoveredNodeId;
    hoveredNodeId = id;

    if (previous != kInvalidNodeId)
        tree.setHovered(previous, false);
    if (hoveredNodeId != kInvalidNodeId)
        tree.setHovered(hoveredNodeId, true);

    return true;
}

bool UiContext::setPressedNode(NodeId id)
{
    if (id != kInvalidNodeId && !isPointerInteractiveNode(tree.get(id)))
        id = kInvalidNodeId;

    if (pressedNodeId == id)
        return false;

    NodeId previous = pressedNodeId;
    pressedNodeId = id;

    if (previous != kInvalidNodeId)
        tree.setPressed(previous, false);
    if (pressedNodeId != kInvalidNodeId)
        tree.setPressed(pressedNodeId, true);

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

void UiContext::clearInvalidInteractionState(NodeId root)
{
    clearInvalidFocus(root);

    if (hoveredNodeId != kInvalidNodeId && (!containsNode(tree, root, hoveredNodeId) || !isPointerInteractiveNode(tree.get(hoveredNodeId))))
        setHoveredNode(kInvalidNodeId);

    if (pressedNodeId != kInvalidNodeId && (!containsNode(tree, root, pressedNodeId) || !isPointerInteractiveNode(tree.get(pressedNodeId))))
        setPressedNode(kInvalidNodeId);

    if (pointerCaptureNodeId != kInvalidNodeId &&
        (!containsNode(tree, root, pointerCaptureNodeId) || !isPointerInteractiveNode(tree.get(pointerCaptureNodeId))))
    {
        pointerCaptureNodeId = kInvalidNodeId;
        setPressedNode(kInvalidNodeId);
    }
}

} // namespace lute::ui
