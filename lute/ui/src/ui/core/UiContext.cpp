#include "lute/ui/Context.h"

namespace lute::ui
{

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
    NodeId id = resolveRoot(kInvalidNodeId);
    if (id == kInvalidNodeId)
        return;

    layoutEngine.layout(tree, id, viewportSize);
    Scene nextScene = sceneBuilder.build(tree, id);
    scene.replace(std::vector<DisplayItem>(nextScene.items().begin(), nextScene.items().end()));
    semantics = semanticsBuilder.build(tree, id);

    for (const auto& [nodeId, node] : tree.nodes())
    {
        (void)node;
        tree.clearDirty(nodeId, DirtyBits::Paint | DirtyBits::Scene | DirtyBits::Semantics | DirtyBits::State);
    }
}

RenderStats UiContext::render()
{
    flush();
    return renderer.render(scene);
}

RenderStats UiContext::renderToMetalLayer(void* metalLayer, uint32_t pixelWidth, uint32_t pixelHeight, float scale)
{
    flush();
    return renderer.renderToMetalLayer(scene, metalLayer, pixelWidth, pixelHeight, scale);
}

void UiContext::retainEffect(std::shared_ptr<Effect> effect)
{
    retainedEffects.push_back(std::move(effect));
}

bool UiContext::activate(NodeId id)
{
    return inputRouter.dispatchCommand(tree, id, Command::Activate);
}

bool UiContext::dispatchPointer(const PointerEvent& event)
{
    flush();
    NodeId id = resolveRoot(kInvalidNodeId);
    if (id == kInvalidNodeId)
        return false;

    return inputRouter.dispatchPointer(tree, id, event);
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

} // namespace lute::ui
