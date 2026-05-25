#include "lute/ui/Platform.h"

#include "lute/ui/Context.h"

namespace lute::ui
{

bool dispatchNativePointer(UiContext& context, const PointerEvent& event, NodeId root)
{
    return context.dispatchPointer(event, root);
}

bool dispatchNativeKey(UiContext& context, const KeyEvent& event, NodeId root)
{
    return context.dispatchKey(event, root);
}

bool renderNativeFrame(UiContext& context, const NativeFrame& frame)
{
    if (frame.surface.kind != NativeSurfaceKind::MetalLayer || !frame.surface.handle || frame.viewport.pixelWidth == 0 || frame.viewport.pixelHeight == 0)
        return false;

    context.setViewport(frame.viewport.logicalSize);
    context.renderToMetalLayer(frame.surface.handle, frame.viewport.pixelWidth, frame.viewport.pixelHeight, frame.viewport.scale);
    nativeAccessibilityBridge().syncTree(context.currentSemantics());
    return true;
}

bool renderNativeFrame(UiContext& context, NativeWindowSurface& surface)
{
    return renderNativeFrame(context, surface.currentFrame());
}

#if !defined(__APPLE__)

namespace
{

class EmptyClipboard final : public NativeClipboard
{
public:
    void setText(std::string_view text) override
    {
        storedText = std::string(text);
    }

    std::optional<std::string> text() const override
    {
        if (!storedText)
            return std::nullopt;
        return *storedText;
    }

private:
    std::optional<std::string> storedText;
};

class EmptyAccessibilityBridge final : public NativeAccessibilityBridge
{
public:
    void syncTree(const SemanticTree& tree) override
    {
        (void)tree;
    }

    bool performAction(SemanticNodeId id, SemanticAction action) override
    {
        (void)id;
        (void)action;
        return false;
    }
};

class EmptyTextServices final : public NativeTextServices
{
public:
    bool resolveDefaultUIFont(PlatformFontDescriptor& out) const override
    {
        (void)out;
        return false;
    }

    bool resolveFallbackUIFont(std::string_view utf8, PlatformFontDescriptor& out) const override
    {
        (void)utf8;
        (void)out;
        return false;
    }
};

} // namespace

NativeClipboard& nativeClipboard()
{
    static EmptyClipboard clipboard;
    return clipboard;
}

NativeAccessibilityBridge& nativeAccessibilityBridge()
{
    static EmptyAccessibilityBridge bridge;
    return bridge;
}

NativeTextServices& nativeTextServices()
{
    static EmptyTextServices services;
    return services;
}

bool runNativeShell(std::shared_ptr<UiContext>, std::string* error)
{
    if (error)
        *error = "Lute UI native shell is currently implemented only on macOS";
    return false;
}
#endif

} // namespace lute::ui
