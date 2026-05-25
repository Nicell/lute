#pragma once

#include "lute/ui/Accessibility.h"
#include "lute/ui/Core.h"
#include "lute/ui/Input.h"
#include "lute/ui/Text.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace lute::ui
{

class UiContext;

enum class NativeSurfaceKind
{
    MetalLayer,
};

enum class NativeColorSpace
{
    Srgb,
};

struct NativeSurface
{
    NativeSurfaceKind kind = NativeSurfaceKind::MetalLayer;
    void* handle = nullptr;
};

struct NativeViewport
{
    Vec2 logicalSize;
    uint32_t pixelWidth = 0;
    uint32_t pixelHeight = 0;
    float scale = 1.0f;
    NativeColorSpace colorSpace = NativeColorSpace::Srgb;
};

struct NativeFrame
{
    NativeSurface surface;
    NativeViewport viewport;
};

class NativeWindowSurface
{
public:
    virtual ~NativeWindowSurface() = default;
    virtual NativeFrame currentFrame() const = 0;
    virtual void requestRedraw() = 0;
};

class NativeClipboard
{
public:
    virtual ~NativeClipboard() = default;
    virtual void setText(std::string_view text) = 0;
    virtual std::optional<std::string> text() const = 0;
};

class NativeAccessibilityBridge
{
public:
    virtual ~NativeAccessibilityBridge() = default;
    virtual void syncTree(const SemanticTree& tree) = 0;
    virtual bool performAction(SemanticNodeId id, SemanticAction action) = 0;
};

class NativeTextServices
{
public:
    virtual ~NativeTextServices() = default;
    virtual bool resolveDefaultUIFont(PlatformFontDescriptor& out) const = 0;
    virtual bool resolveFallbackUIFont(std::string_view utf8, PlatformFontDescriptor& out) const = 0;
};

NativeClipboard& nativeClipboard();
NativeAccessibilityBridge& nativeAccessibilityBridge();
NativeTextServices& nativeTextServices();

bool dispatchNativePointer(UiContext& context, const PointerEvent& event, NodeId root = kInvalidNodeId);
bool dispatchNativeKey(UiContext& context, const KeyEvent& event, NodeId root = kInvalidNodeId);
bool renderNativeFrame(UiContext& context, const NativeFrame& frame);
bool renderNativeFrame(UiContext& context, NativeWindowSurface& surface);
bool runNativeShell(std::shared_ptr<UiContext> context, std::string* error);

} // namespace lute::ui
