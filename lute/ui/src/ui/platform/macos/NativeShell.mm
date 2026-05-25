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

    NSPoint point = [self convertPoint:[event locationInWindow] fromView:nil];
    lute::ui::PointerEvent pointerEvent;
    pointerEvent.kind = lute::ui::PointerEventKind::Up;
    pointerEvent.button = lute::ui::PointerButton::Primary;
    pointerEvent.position = {static_cast<float>(point.x), static_cast<float>(point.y)};

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
    [self setNeedsDisplay:YES];
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
