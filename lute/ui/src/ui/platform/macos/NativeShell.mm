#include "lute/ui/Platform.h"

#if defined(__APPLE__)

#include "lute/ui/Context.h"

#import <AppKit/AppKit.h>
#import <CoreText/CoreText.h>
#import <QuartzCore/CAMetalLayer.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <climits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace
{

NSString* nsStringFromStd(const std::string& value)
{
    return [[NSString alloc] initWithBytes:value.data() length:value.size() encoding:NSUTF8StringEncoding];
}

std::string utf8FromTextInputString(id value)
{
    NSString* string = nil;
    if ([value isKindOfClass:[NSAttributedString class]])
        string = [value string];
    else if ([value isKindOfClass:[NSString class]])
        string = value;

    if (!string)
        return {};

    NSData* data = [string dataUsingEncoding:NSUTF8StringEncoding];
    if (!data)
        return {};

    return std::string(static_cast<const char*>([data bytes]), static_cast<size_t>([data length]));
}

std::shared_ptr<const void> retainCoreTextFont(CTFontRef font)
{
    if (!font)
        return {};

    CFRetain(font);
    return std::shared_ptr<const void>(font, [](const void* value) {
        if (value)
            CFRelease(value);
    });
}

CTFontRef createCoreTextSystemFont(float fontSize = 0.0f)
{
    CTFontRef font = CTFontCreateUIFontForLanguage(kCTFontUIFontSystem, fontSize, nullptr);
    if (!font)
        font = CTFontCreateWithName(CFSTR(".AppleSystemUIFont"), fontSize, nullptr);
    return font;
}

void collectCoreTextVariation(const void* key, const void* value, void* context)
{
    auto* variations = static_cast<std::vector<lute::ui::FontVariation>*>(context);
    if (!key || !value || CFGetTypeID(key) != CFNumberGetTypeID() || CFGetTypeID(value) != CFNumberGetTypeID())
        return;

    int32_t tag = 0;
    double axisValue = 0.0;
    if (!CFNumberGetValue(static_cast<CFNumberRef>(key), kCFNumberSInt32Type, &tag) ||
        !CFNumberGetValue(static_cast<CFNumberRef>(value), kCFNumberDoubleType, &axisValue))
    {
        return;
    }

    variations->push_back({static_cast<uint32_t>(tag), static_cast<float>(axisValue)});
}

std::string cfStringToUtf8(CFStringRef string)
{
    if (!string)
        return {};

    CFIndex length = CFStringGetLength(string);
    CFIndex maxSize = CFStringGetMaximumSizeForEncoding(length, kCFStringEncodingUTF8) + 1;
    std::string result(static_cast<size_t>(maxSize), '\0');
    if (!CFStringGetCString(string, result.data(), maxSize, kCFStringEncodingUTF8))
        return {};

    result.resize(std::char_traits<char>::length(result.c_str()));
    return result;
}

std::string cfUrlPath(CFURLRef url)
{
    if (!url)
        return {};

    UInt8 buffer[PATH_MAX];
    if (!CFURLGetFileSystemRepresentation(url, true, buffer, sizeof(buffer)))
        return {};

    return reinterpret_cast<const char*>(buffer);
}

lute::ui::PlatformFontDescriptor platformFontFromCoreTextFont(CTFontRef font)
{
    lute::ui::PlatformFontDescriptor result;
    if (!font)
        return result;

    CGFloat size = CTFontGetSize(font);
    if (size > 0.0)
    {
        result.platformFont = retainCoreTextFont(font);
        result.platformAscenderRatio = static_cast<float>(CTFontGetAscent(font) / size);
        result.platformDescenderRatio = static_cast<float>(CTFontGetDescent(font) / size);
        result.platformLineGapRatio = static_cast<float>(CTFontGetLeading(font) / size);
    }

    CFDictionaryRef variations = CTFontCopyVariation(font);
    if (variations)
    {
        CFDictionaryApplyFunction(variations, collectCoreTextVariation, &result.platformVariations);
        CFRelease(variations);
    }

    CFStringRef postScriptName = CTFontCopyPostScriptName(font);
    if (postScriptName)
    {
        result.postScriptName = cfStringToUtf8(postScriptName);
        CFRelease(postScriptName);
    }

    CFTypeRef urlValue = CTFontCopyAttribute(font, kCTFontURLAttribute);
    if (urlValue && CFGetTypeID(urlValue) == CFURLGetTypeID())
        result.path = cfUrlPath(static_cast<CFURLRef>(urlValue));

    if (urlValue)
        CFRelease(urlValue);

    if (result.path.empty())
    {
        CTFontDescriptorRef descriptor = CTFontCopyFontDescriptor(font);
        if (descriptor)
        {
            CFTypeRef descriptorUrl = CTFontDescriptorCopyAttribute(descriptor, kCTFontURLAttribute);
            if (descriptorUrl && CFGetTypeID(descriptorUrl) == CFURLGetTypeID())
                result.path = cfUrlPath(static_cast<CFURLRef>(descriptorUrl));
            if (descriptorUrl)
                CFRelease(descriptorUrl);
            CFRelease(descriptor);
        }
    }

    return result;
}

lute::ui::NativeFrame nativeFrameForView(NSView* view, CAMetalLayer* metalLayer)
{
    lute::ui::NativeFrame frame;
    frame.surface = {lute::ui::NativeSurfaceKind::MetalLayer, metalLayer};
    if (!view || !metalLayer)
        return frame;

    NSRect bounds = [view bounds];
    CGSize drawableSize = [metalLayer drawableSize];
    CGFloat scale = [metalLayer contentsScale];
    if (scale <= 0.0)
        scale = 1.0;

    frame.viewport.logicalSize = {static_cast<float>(bounds.size.width), static_cast<float>(bounds.size.height)};
    frame.viewport.pixelWidth = static_cast<uint32_t>(std::max<CGFloat>(1.0, drawableSize.width));
    frame.viewport.pixelHeight = static_cast<uint32_t>(std::max<CGFloat>(1.0, drawableSize.height));
    frame.viewport.scale = static_cast<float>(scale);
    frame.viewport.colorSpace = lute::ui::NativeColorSpace::Srgb;
    return frame;
}

class AppKitWindowSurface final : public lute::ui::NativeWindowSurface
{
public:
    AppKitWindowSurface(NSView* view, CAMetalLayer* metalLayer)
        : view(view)
        , metalLayer(metalLayer)
    {
    }

    lute::ui::NativeFrame currentFrame() const override
    {
        return nativeFrameForView(view, metalLayer);
    }

    void requestRedraw() override
    {
        if (view)
            [view setNeedsDisplay:YES];
    }

private:
    NSView* view = nil;
    CAMetalLayer* metalLayer = nil;
};

lute::ui::Modifiers modifiersFromEvent(NSEvent* event)
{
    NSEventModifierFlags flags = [event modifierFlags];
    lute::ui::Modifiers modifiers;
    modifiers.shift = (flags & NSEventModifierFlagShift) != 0;
    modifiers.control = (flags & NSEventModifierFlagControl) != 0;
    modifiers.alt = (flags & NSEventModifierFlagOption) != 0;
    modifiers.meta = (flags & NSEventModifierFlagCommand) != 0;
    return modifiers;
}

lute::ui::PhysicalKey physicalKeyFromEvent(NSEvent* event)
{
    switch ([event keyCode])
    {
    case 48:
        return lute::ui::PhysicalKey::Tab;
    case 36:
        return lute::ui::PhysicalKey::Enter;
    case 76:
        return lute::ui::PhysicalKey::NumpadEnter;
    case 49:
        return lute::ui::PhysicalKey::Space;
    case 53:
        return lute::ui::PhysicalKey::Escape;
    default:
        return lute::ui::PhysicalKey::Unknown;
    }
}

lute::ui::LogicalKey logicalKeyFromPhysical(lute::ui::PhysicalKey physical)
{
    switch (physical)
    {
    case lute::ui::PhysicalKey::Tab:
        return lute::ui::LogicalKey::Tab;
    case lute::ui::PhysicalKey::Enter:
    case lute::ui::PhysicalKey::NumpadEnter:
        return lute::ui::LogicalKey::Enter;
    case lute::ui::PhysicalKey::Space:
        return lute::ui::LogicalKey::Space;
    case lute::ui::PhysicalKey::Escape:
        return lute::ui::LogicalKey::Escape;
    case lute::ui::PhysicalKey::Unknown:
        return lute::ui::LogicalKey::Unknown;
    }

    return lute::ui::LogicalKey::Unknown;
}

lute::ui::KeyEvent keyEventFromNSEvent(NSEvent* event, lute::ui::KeyEventKind kind)
{
    lute::ui::PhysicalKey physical = physicalKeyFromEvent(event);
    lute::ui::KeyEvent keyEvent;
    keyEvent.kind = kind;
    keyEvent.physical = physical;
    keyEvent.logical = logicalKeyFromPhysical(physical);
    keyEvent.modifiers = modifiersFromEvent(event);
    keyEvent.repeat = [event isARepeat];
    return keyEvent;
}

lute::ui::PointerEvent pointerEventFromNSEvent(NSEvent* event, NSView* view, lute::ui::PointerEventKind kind)
{
    NSPoint point = [view convertPoint:[event locationInWindow] fromView:nil];

    lute::ui::PointerEvent pointerEvent;
    pointerEvent.kind = kind;
    pointerEvent.button = lute::ui::PointerButton::Primary;
    pointerEvent.position = {static_cast<float>(point.x), static_cast<float>(point.y)};
    pointerEvent.modifiers = modifiersFromEvent(event);
    return pointerEvent;
}

class AppKitClipboard final : public lute::ui::NativeClipboard
{
public:
    void setText(std::string_view text) override
    {
        NSPasteboard* pasteboard = [NSPasteboard generalPasteboard];
        [pasteboard clearContents];

        NSString* string = [[NSString alloc] initWithBytes:text.data() length:text.size() encoding:NSUTF8StringEncoding];
        if (string)
        {
            [pasteboard setString:string forType:NSPasteboardTypeString];
            [string release];
        }
    }

    std::optional<std::string> text() const override
    {
        NSString* string = [[NSPasteboard generalPasteboard] stringForType:NSPasteboardTypeString];
        if (!string)
            return std::nullopt;

        NSData* data = [string dataUsingEncoding:NSUTF8StringEncoding];
        if (!data)
            return std::nullopt;

        return std::string(static_cast<const char*>([data bytes]), static_cast<size_t>([data length]));
    }
};

class CoreTextServices final : public lute::ui::NativeTextServices
{
public:
    bool resolveDefaultUIFont(lute::ui::PlatformFontDescriptor& out) const override
    {
        CTFontRef font = createCoreTextSystemFont();
        if (!font)
            return false;

        out = platformFontFromCoreTextFont(font);
        CFRelease(font);
        return !out.path.empty();
    }

    bool resolveFallbackUIFont(std::string_view utf8, lute::ui::PlatformFontDescriptor& out) const override
    {
        CTFontRef baseFont = createCoreTextSystemFont(lute::ui::kDefaultUiFontSize);
        if (!baseFont)
            return false;

        CFStringRef string = CFStringCreateWithBytes(
            kCFAllocatorDefault,
            reinterpret_cast<const UInt8*>(utf8.data()),
            static_cast<CFIndex>(utf8.size()),
            kCFStringEncodingUTF8,
            false
        );
        if (!string)
        {
            CFRelease(baseFont);
            return false;
        }

        CTFontRef fallbackFont = CTFontCreateForString(baseFont, string, CFRangeMake(0, CFStringGetLength(string)));
        out = platformFontFromCoreTextFont(fallbackFont ? fallbackFont : baseFont);

        if (fallbackFont)
            CFRelease(fallbackFont);
        CFRelease(string);
        CFRelease(baseFont);
        return !out.path.empty();
    }
};

} // namespace

@interface LuteUiView : NSView <NSTextInputClient>
{
    std::shared_ptr<lute::ui::UiContext> _context;
    CAMetalLayer* _metalLayer;
    lute::ui::NativeWindowSurface* _surface;
    NSTrackingArea* _trackingArea;
    bool _primaryButtonDown;
    bool _textInputHandledDuringInterpretation;
    bool _hasMarkedText;
    NSRange _markedRange;
    NSRange _selectedRange;
}

- (instancetype)initWithFrame:(NSRect)frame context:(std::shared_ptr<lute::ui::UiContext>)context;
- (void)updateDrawableSize;
- (void)renderFrame;
- (void)dispatchPointerEvent:(NSEvent*)event kind:(lute::ui::PointerEventKind)kind;
- (void)renderAfterInputIfHandled:(BOOL)handled;

@end

@implementation LuteUiView

- (instancetype)initWithFrame:(NSRect)frame context:(std::shared_ptr<lute::ui::UiContext>)context
{
    self = [super initWithFrame:frame];
    if (self)
    {
        _context = std::move(context);
        _metalLayer = [[CAMetalLayer layer] retain];
        _surface = new AppKitWindowSurface(self, _metalLayer);
        _trackingArea = nil;
        _primaryButtonDown = false;
        _textInputHandledDuringInterpretation = false;
        _hasMarkedText = false;
        _markedRange = NSMakeRange(NSNotFound, 0);
        _selectedRange = NSMakeRange(0, 0);
        [_metalLayer setOpaque:YES];
        [self setWantsLayer:YES];
        [self setLayer:_metalLayer];
    }
    return self;
}

- (void)dealloc
{
    if (_trackingArea)
    {
        [self removeTrackingArea:_trackingArea];
        [_trackingArea release];
        _trackingArea = nil;
    }
    delete _surface;
    _surface = nullptr;
    [_metalLayer release];
    [super dealloc];
}

- (BOOL)isFlipped
{
    return YES;
}

- (BOOL)wantsUpdateLayer
{
    return YES;
}

- (BOOL)acceptsFirstResponder
{
    return YES;
}

- (BOOL)acceptsFirstMouse:(NSEvent*)event
{
    (void)event;
    return YES;
}

- (void)updateTrackingAreas
{
    [super updateTrackingAreas];

    if (_trackingArea)
    {
        [self removeTrackingArea:_trackingArea];
        [_trackingArea release];
        _trackingArea = nil;
    }

    NSTrackingAreaOptions options = NSTrackingMouseMoved | NSTrackingMouseEnteredAndExited | NSTrackingActiveInKeyWindow | NSTrackingInVisibleRect;
    _trackingArea = [[NSTrackingArea alloc] initWithRect:NSZeroRect options:options owner:self userInfo:nil];
    [self addTrackingArea:_trackingArea];
}

- (void)viewDidMoveToWindow
{
    [super viewDidMoveToWindow];
    [self updateDrawableSize];
    if ([self window])
        [[self window] makeFirstResponder:self];
    [self setNeedsDisplay:YES];
}

- (void)viewDidChangeBackingProperties
{
    [super viewDidChangeBackingProperties];
    [self updateDrawableSize];
    [self setNeedsDisplay:YES];
}

- (void)setFrameSize:(NSSize)newSize
{
    [super setFrameSize:newSize];
    [self updateDrawableSize];
    [self setNeedsDisplay:YES];
}

- (void)updateLayer
{
    [self renderFrame];
}

- (void)updateDrawableSize
{
    if (!_metalLayer)
        return;

    CGFloat scale = [[self window] backingScaleFactor];
    if (scale <= 0.0)
        scale = [[NSScreen mainScreen] backingScaleFactor];
    if (scale <= 0.0)
        scale = 1.0;

    NSRect bounds = [self bounds];
    CGSize drawableSize = CGSizeMake(std::max<CGFloat>(1.0, std::ceil(bounds.size.width * scale)), std::max<CGFloat>(1.0, std::ceil(bounds.size.height * scale)));
    [_metalLayer setContentsScale:scale];
    [_metalLayer setDrawableSize:drawableSize];
}

- (void)renderFrame
{
    if (!_context || !_surface)
        return;

    [self updateDrawableSize];
    lute::ui::renderNativeFrame(*_context, *_surface);
}

- (void)renderAfterInputIfHandled:(BOOL)handled
{
    if (!handled)
        return;

    [self renderFrame];
}

- (void)dispatchPointerEvent:(NSEvent*)event kind:(lute::ui::PointerEventKind)kind
{
    if (!_context)
        return;

    lute::ui::PointerEvent pointerEvent = pointerEventFromNSEvent(event, self, kind);
    [self renderAfterInputIfHandled:lute::ui::dispatchNativePointer(*_context, pointerEvent)];
}

- (void)mouseUp:(NSEvent*)event
{
    if (!_context)
        return;

    if (!_primaryButtonDown)
        return;
    _primaryButtonDown = false;

    [self dispatchPointerEvent:event kind:lute::ui::PointerEventKind::Up];
}

- (void)mouseDown:(NSEvent*)event
{
    if (!_context)
        return;

    _primaryButtonDown = true;
    [[self window] makeFirstResponder:self];

    [self dispatchPointerEvent:event kind:lute::ui::PointerEventKind::Down];
}

- (void)mouseMoved:(NSEvent*)event
{
    [self dispatchPointerEvent:event kind:lute::ui::PointerEventKind::Move];
}

- (void)mouseDragged:(NSEvent*)event
{
    [self dispatchPointerEvent:event kind:lute::ui::PointerEventKind::Move];
}

- (void)mouseExited:(NSEvent*)event
{
    [self dispatchPointerEvent:event kind:lute::ui::PointerEventKind::Leave];
}

- (void)keyDown:(NSEvent*)event
{
    if (!_context)
    {
        [super keyDown:event];
        return;
    }

    lute::ui::KeyEvent keyEvent = keyEventFromNSEvent(event, lute::ui::KeyEventKind::Down);
    if (lute::ui::dispatchNativeKey(*_context, keyEvent))
    {
        [self renderFrame];
        return;
    }

    _textInputHandledDuringInterpretation = false;
    [self interpretKeyEvents:@[ event ]];
    if (_textInputHandledDuringInterpretation)
    {
        [self renderFrame];
        return;
    }

    [super keyDown:event];
}

- (void)keyUp:(NSEvent*)event
{
    if (!_context)
    {
        [super keyUp:event];
        return;
    }

    lute::ui::KeyEvent keyEvent = keyEventFromNSEvent(event, lute::ui::KeyEventKind::Up);
    if (lute::ui::dispatchNativeKey(*_context, keyEvent))
    {
        [self renderFrame];
        return;
    }

    [super keyUp:event];
}

- (void)insertText:(id)string replacementRange:(NSRange)replacementRange
{
    (void)replacementRange;
    if (!_context)
        return;

    if (_hasMarkedText)
    {
        lute::ui::ImeCompositionEvent endEvent;
        endEvent.kind = lute::ui::ImeCompositionEventKind::End;
        _textInputHandledDuringInterpretation =
            lute::ui::dispatchNativeImeComposition(*_context, endEvent) || _textInputHandledDuringInterpretation;
        _hasMarkedText = false;
        _markedRange = NSMakeRange(NSNotFound, 0);
    }

    std::string text = utf8FromTextInputString(string);
    if (text.empty())
        return;

    lute::ui::TextInputEvent event;
    event.text = std::move(text);
    _textInputHandledDuringInterpretation = lute::ui::dispatchNativeTextInput(*_context, event) || _textInputHandledDuringInterpretation;
}

- (void)setMarkedText:(id)string selectedRange:(NSRange)selectedRange replacementRange:(NSRange)replacementRange
{
    (void)replacementRange;
    if (!_context)
        return;

    std::string text = utf8FromTextInputString(string);
    lute::ui::ImeCompositionEvent event;
    event.kind = _hasMarkedText ? lute::ui::ImeCompositionEventKind::Update : lute::ui::ImeCompositionEventKind::Start;
    event.text = std::move(text);
    event.selectionStart = selectedRange.location == NSNotFound ? 0 : static_cast<uint32_t>(selectedRange.location);
    event.selectionEnd = selectedRange.location == NSNotFound ? event.selectionStart : static_cast<uint32_t>(selectedRange.location + selectedRange.length);

    _hasMarkedText = !event.text.empty();
    _markedRange = _hasMarkedText ? NSMakeRange(0, [string length]) : NSMakeRange(NSNotFound, 0);
    _selectedRange = selectedRange;
    _textInputHandledDuringInterpretation = lute::ui::dispatchNativeImeComposition(*_context, event) || _textInputHandledDuringInterpretation;
}

- (void)unmarkText
{
    if (!_context || !_hasMarkedText)
        return;

    lute::ui::ImeCompositionEvent event;
    event.kind = lute::ui::ImeCompositionEventKind::End;
    _hasMarkedText = false;
    _markedRange = NSMakeRange(NSNotFound, 0);
    _textInputHandledDuringInterpretation = lute::ui::dispatchNativeImeComposition(*_context, event) || _textInputHandledDuringInterpretation;
}

- (BOOL)hasMarkedText
{
    return _hasMarkedText;
}

- (NSRange)markedRange
{
    return _markedRange;
}

- (NSRange)selectedRange
{
    return _selectedRange;
}

- (NSArray<NSAttributedStringKey>*)validAttributesForMarkedText
{
    return @[];
}

- (NSAttributedString*)attributedSubstringForProposedRange:(NSRange)range actualRange:(NSRangePointer)actualRange
{
    if (actualRange)
        *actualRange = NSMakeRange(NSNotFound, 0);
    (void)range;
    return nil;
}

- (NSUInteger)characterIndexForPoint:(NSPoint)point
{
    (void)point;
    return 0;
}

- (NSRect)firstRectForCharacterRange:(NSRange)range actualRange:(NSRangePointer)actualRange
{
    if (actualRange)
        *actualRange = range;

    NSRect bounds = [self bounds];
    NSRect local = NSMakeRect(bounds.origin.x, bounds.origin.y, 1.0, std::max<CGFloat>(1.0, bounds.size.height));
    NSRect windowRect = [self convertRect:local toView:nil];
    return [[self window] convertRectToScreen:windowRect];
}

- (void)doCommandBySelector:(SEL)selector
{
    (void)selector;
}

@end

@interface LuteUiWindowDelegate : NSObject <NSWindowDelegate>
@end

@implementation LuteUiWindowDelegate

- (void)windowWillClose:(NSNotification*)notification
{
    (void)notification;
    [NSApp stop:nil];
}

@end

namespace lute::ui
{

NativeClipboard& nativeClipboard()
{
    static AppKitClipboard clipboard;
    return clipboard;
}

NativeTextServices& nativeTextServices()
{
    static CoreTextServices services;
    return services;
}

bool runNativeShell(std::shared_ptr<UiContext> context, std::string* error)
{
    if (![NSThread isMainThread])
    {
        if (error)
            *error = "Lute UI native shell must run on the main thread";
        return false;
    }

    if (!context || context->root() == kInvalidNodeId)
    {
        if (error)
            *error = "ui.run requires a window root";
        return false;
    }

    @autoreleasepool
    {
        const UiNode* root = context->nodes().get(context->root());
        std::string title = root && !root->title.empty() ? root->title : "Lute UI";

        NSApplication* app = [NSApplication sharedApplication];
        [app setActivationPolicy:NSApplicationActivationPolicyRegular];

        NSRect frame = NSMakeRect(0, 0, 800, 600);
        NSWindowStyleMask style = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable;
        NSWindow* window = [[NSWindow alloc] initWithContentRect:frame styleMask:style backing:NSBackingStoreBuffered defer:NO];
        [window setReleasedWhenClosed:NO];
        [window center];

        NSString* nsTitle = nsStringFromStd(title);
        [window setTitle:nsTitle];
        [nsTitle release];

        LuteUiWindowDelegate* delegate = [[LuteUiWindowDelegate alloc] init];
        [window setDelegate:delegate];

        LuteUiView* view = [[LuteUiView alloc] initWithFrame:frame context:std::move(context)];
        [window setContentView:view];
        [view release];

        [window makeKeyAndOrderFront:nil];
        [window makeFirstResponder:view];
        [app activateIgnoringOtherApps:YES];
        [view renderFrame];
        [app run];

        [window setDelegate:nil];
        [delegate release];
        [window release];
    }

    return true;
}

} // namespace lute::ui

#endif
