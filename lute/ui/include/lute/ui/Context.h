#pragma once

#include "lute/ui/Accessibility.h"
#include "lute/ui/Input.h"
#include "lute/ui/Layout.h"
#include "lute/ui/Render.h"
#include "lute/ui/Profile.h"
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
    std::string dumpProfile(size_t maxFrames = 8) const;
    const ProfileStore& profiles() const;

    bool activate(NodeId id);
    bool focus(NodeId id);
    bool clearFocus();
    std::optional<NodeId> focusedNode() const;
    std::optional<NodeId> hoveredNode() const;
    std::optional<NodeId> pressedNode() const;
    std::optional<NodeId> capturedPointerNode() const;
    bool dispatchPointer(const PointerEvent& event, NodeId root = kInvalidNodeId);
    bool dispatchKey(const KeyEvent& event, NodeId root = kInvalidNodeId);
    bool dispatchTextInput(const TextInputEvent& event, NodeId root = kInvalidNodeId);
    bool dispatchImeComposition(const ImeCompositionEvent& event, NodeId root = kInvalidNodeId);

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
    ProfileFrameId nextFrameId();
    bool setFocusedNode(NodeId id, bool focusVisible);
    bool setHoveredNode(NodeId id);
    bool setPressedNode(NodeId id);
    bool focusNext(NodeId root, bool reverse);
    void clearInvalidFocus(NodeId root);
    void clearInvalidInteractionState(NodeId root);

    ReactiveGraph graph;
    NodeTree tree;
    LayoutEngine layoutEngine;
    SceneBuilder sceneBuilder;
    SemanticsBuilder semanticsBuilder;
    InputRouter inputRouter;
    DawnRenderer renderer;
    ProfileStore profileStore;

    Scene scene;
    SemanticTree semantics;
    ProfileFrameId nextProfileFrameId = 1;
    NodeId rootNode = kInvalidNodeId;
    NodeId focusedNodeId = kInvalidNodeId;
    NodeId hoveredNodeId = kInvalidNodeId;
    NodeId pressedNodeId = kInvalidNodeId;
    NodeId pointerCaptureNodeId = kInvalidNodeId;
    Vec2 viewportSize = {800.0f, 600.0f};
    std::optional<std::string> lastError;
    std::vector<std::shared_ptr<Effect>> retainedEffects;
};

} // namespace lute::ui
