#include "lute/ui/Platform.h"

#if defined(__APPLE__)

#import <AppKit/AppKit.h>
#import <QuartzCore/CAMetalLayer.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace
{

NSString* nsStringFromStd(const std::string& value)
{
    return [[NSString alloc] initWithBytes:value.data() length:value.size() encoding:NSUTF8StringEncoding];
}

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

} // namespace

@interface LuteUiView : NSView
{
    std::shared_ptr<lute::ui::UiContext> _context;
    CAMetalLayer* _metalLayer;
    bool _primaryButtonDown;
}

- (instancetype)initWithFrame:(NSRect)frame context:(std::shared_ptr<lute::ui::UiContext>)context;
- (void)updateDrawableSize;
- (void)renderFrame;

@end

@implementation LuteUiView

- (instancetype)initWithFrame:(NSRect)frame context:(std::shared_ptr<lute::ui::UiContext>)context
{
    self = [super initWithFrame:frame];
    if (self)
    {
        _context = std::move(context);
        _metalLayer = [[CAMetalLayer layer] retain];
        _primaryButtonDown = false;
        [_metalLayer setOpaque:YES];
        [self setWantsLayer:YES];
        [self setLayer:_metalLayer];
    }
    return self;
}

- (void)dealloc
{
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
    if (!_context || !_metalLayer)
        return;

    [self updateDrawableSize];

    NSRect bounds = [self bounds];
    CGSize drawableSize = [_metalLayer drawableSize];
    CGFloat scale = [_metalLayer contentsScale];
    if (scale <= 0.0)
        scale = 1.0;

    _context->setViewport({static_cast<float>(bounds.size.width), static_cast<float>(bounds.size.height)});
    _context->renderToMetalLayer(
        _metalLayer,
        static_cast<uint32_t>(std::max<CGFloat>(1.0, drawableSize.width)),
        static_cast<uint32_t>(std::max<CGFloat>(1.0, drawableSize.height)),
        static_cast<float>(scale)
    );
}

- (void)mouseUp:(NSEvent*)event
{
    if (!_context)
        return;

    if (!_primaryButtonDown)
        return;
    _primaryButtonDown = false;

    lute::ui::PointerEvent pointerEvent = pointerEventFromNSEvent(event, self, lute::ui::PointerEventKind::Up);

    if (_context->dispatchPointer(pointerEvent))
        [self renderFrame];
    else
        [self setNeedsDisplay:YES];
}

- (void)mouseDown:(NSEvent*)event
{
    if (!_context)
        return;

    _primaryButtonDown = true;
    [[self window] makeFirstResponder:self];

    lute::ui::PointerEvent pointerEvent = pointerEventFromNSEvent(event, self, lute::ui::PointerEventKind::Down);
    if (_context->dispatchPointer(pointerEvent))
        [self renderFrame];
    else
        [self setNeedsDisplay:YES];
}

- (void)keyDown:(NSEvent*)event
{
    if (!_context)
    {
        [super keyDown:event];
        return;
    }

    lute::ui::KeyEvent keyEvent = keyEventFromNSEvent(event, lute::ui::KeyEventKind::Down);
    if (_context->dispatchKey(keyEvent))
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
    if (_context->dispatchKey(keyEvent))
    {
        [self renderFrame];
        return;
    }

    [super keyUp:event];
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
