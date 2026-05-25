#pragma once

#include "lute/ui/Accessibility.h"
#include "lute/ui/Input.h"
#include "lute/ui/Layout.h"
#include "lute/ui/Render.h"
#include "lute/ui/Scene.h"
#include "lute/ui/Signal.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace lute::ui
{

class UiContext
{
public:
    ReactiveGraph& reactive();
    NodeTree& nodes();
    const NodeTree& nodes() const;

    NodeId createNode(WidgetKind kind);
    void setRoot(NodeId id);
    NodeId root() const;

    void setViewport(Vec2 size);
    Vec2 viewport() const;

    void flush();
    RenderStats render();
    RenderStats renderToMetalLayer(void* metalLayer, uint32_t pixelWidth, uint32_t pixelHeight, float scale);
    void retainEffect(std::shared_ptr<Effect> effect);

    bool activate(NodeId id);
    bool dispatchPointer(const PointerEvent& event);

    const Scene& currentScene() const;
    const SemanticTree& currentSemantics() const;

    std::string dumpUiTree(NodeId root = kInvalidNodeId) const;
    std::string dumpLayoutTree(NodeId root = kInvalidNodeId);
    std::string dumpScene(NodeId root = kInvalidNodeId);
    std::string dumpSemantics(NodeId root = kInvalidNodeId);

    void setLastError(std::string error);
    std::optional<std::string> takeLastError();

private:
    NodeId resolveRoot(NodeId requested) const;

    ReactiveGraph graph;
    NodeTree tree;
    LayoutEngine layoutEngine;
    SceneBuilder sceneBuilder;
    SemanticsBuilder semanticsBuilder;
    InputRouter inputRouter;
    DawnRenderer renderer;

    Scene scene;
    SemanticTree semantics;
    NodeId rootNode = kInvalidNodeId;
    Vec2 viewportSize = {800.0f, 600.0f};
    std::optional<std::string> lastError;
    std::vector<std::shared_ptr<Effect>> retainedEffects;
};

} // namespace lute::ui
