// =============================================================================
// CHROMODYNAMIC — cd/platform/CocoaWindow.mm
// Phase 159 / v0.99.94 — macOS Cocoa window backend (untested).
//
// Objective-C++ translation unit (.mm). Compiled only when:
//   * Build host is Apple (__APPLE__ + TARGET_OS_OSX), AND
//   * CD_PLATFORM_COCOA=ON.
//
// Marathon scope: the marathon host is Windows-only; this file
// ships as "intended to work on macOS but the marathon couldn't
// run it." Reviewer on macOS should:
//   1. Install Xcode CLT (provides Cocoa headers + clang).
//   2. cmake -DCD_PLATFORM_COCOA=ON ...
//   3. Run hello_engine and confirm an NSWindow opens.
//
// Metal backend (cd::rhi_metal) stays a separate v1.3 phase; this
// file just owns the NSWindow + event loop. Vulkan apps can sit on
// top via MoltenVK and VK_KHR_metal_surface — same as Filament
// + production engines's Apple path.
// =============================================================================
#include <cd/platform/Window.hpp>

#if defined(__APPLE__) && defined(CD_PLATFORM_COCOA)

    #import <Cocoa/Cocoa.h>
    #import <QuartzCore/CAMetalLayer.h>

    #include <cstdint>
    #include <memory>
    #include <string>

@interface CdAppDelegate : NSObject<NSApplicationDelegate>
@end

@implementation CdAppDelegate
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)sender
{ (void)sender; return YES; }
@end

namespace cd::platform
{

namespace
{

[[nodiscard]] KeyCode key_from_nskey(unsigned short k) noexcept
{
    // Subset of Apple HIToolbox virtual key codes.
    switch (k)
    {
        case 0x00: return KeyCode::kA;
        case 0x0B: return KeyCode::kB;
        case 0x08: return KeyCode::kC;
        case 0x02: return KeyCode::kD;
        case 0x0E: return KeyCode::kE;
        case 0x35: return KeyCode::kEscape;
        case 0x31: return KeyCode::kSpace;
        case 0x24: return KeyCode::kEnter;
        case 0x30: return KeyCode::kTab;
        case 0x33: return KeyCode::kBackspace;
        case 0x7B: return KeyCode::kLeft;
        case 0x7C: return KeyCode::kRight;
        case 0x7E: return KeyCode::kUp;
        case 0x7D: return KeyCode::kDown;
        default:   return KeyCode::kUnknown;
    }
}

class CocoaWindow final : public IWindow
{
public:
    [[nodiscard]] bool create(const WindowDesc& d)
    {
        @autoreleasepool {
            if (NSApp == nil)
            {
                [NSApplication sharedApplication];
                static auto* delegate = [[CdAppDelegate alloc] init];
                [NSApp setDelegate: delegate];
                [NSApp setActivationPolicy: NSApplicationActivationPolicyRegular];
                [NSApp finishLaunching];
            }

            const NSRect frame = NSMakeRect(0, 0, d.width, d.height);
            const NSUInteger style =
                NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                NSWindowStyleMaskMiniaturizable |
                (d.resizable ? NSWindowStyleMaskResizable : 0);
            window_ = [[NSWindow alloc]
                initWithContentRect: frame
                          styleMask: style
                            backing: NSBackingStoreBuffered
                              defer: NO];
            if (window_ == nil) return false;

            std::string title { d.title };
            [window_ setTitle: [NSString stringWithUTF8String: title.c_str()]];

            // CAMetalLayer-backed view so MoltenVK / Metal can render.
            metal_layer_ = [CAMetalLayer layer];
            view_ = [[NSView alloc] initWithFrame: frame];
            [view_ setWantsLayer: YES];
            [view_ setLayer: metal_layer_];
            [window_ setContentView: view_];

            if (d.visible)
            {
                [window_ makeKeyAndOrderFront: nil];
                [NSApp activateIgnoringOtherApps: YES];
            }

            width_  = d.width;
            height_ = d.height;
            return true;
        }
    }

    ~CocoaWindow() override
    {
        if (window_ != nil)
        {
            [window_ close];
            window_ = nil;
        }
    }

    [[nodiscard]] bool pump_events(std::vector<OSEvent>& out) override
    {
        @autoreleasepool {
            for (;;)
            {
                NSEvent* ev = [NSApp
                    nextEventMatchingMask: NSEventMaskAny
                                untilDate: [NSDate distantPast]
                                   inMode: NSDefaultRunLoopMode
                                  dequeue: YES];
                if (ev == nil) break;

                switch ([ev type])
                {
                    case NSEventTypeKeyDown:
                    case NSEventTypeKeyUp:
                    {
                        OSEvent e { [ev type] == NSEventTypeKeyDown
                                        ? OSEventKind::kKeyDown
                                        : OSEventKind::kKeyUp };
                        e.key = key_from_nskey([ev keyCode]);
                        out.push_back(e);
                        break;
                    }
                    case NSEventTypeLeftMouseDown:
                    case NSEventTypeRightMouseDown:
                    case NSEventTypeOtherMouseDown:
                    case NSEventTypeLeftMouseUp:
                    case NSEventTypeRightMouseUp:
                    case NSEventTypeOtherMouseUp:
                    {
                        const auto type = [ev type];
                        const bool down = (type == NSEventTypeLeftMouseDown
                                        || type == NSEventTypeRightMouseDown
                                        || type == NSEventTypeOtherMouseDown);
                        OSEvent e { down ? OSEventKind::kMouseButtonDown
                                         : OSEventKind::kMouseButtonUp };
                        e.mouse_button =
                            (type == NSEventTypeLeftMouseDown || type == NSEventTypeLeftMouseUp)   ? MouseButton::kLeft
                          : (type == NSEventTypeRightMouseDown || type == NSEventTypeRightMouseUp) ? MouseButton::kRight
                          :                                                                          MouseButton::kMiddle;
                        const NSPoint p = [ev locationInWindow];
                        e.mouse_x = static_cast<float>(p.x);
                        e.mouse_y = static_cast<float>(height_) - static_cast<float>(p.y);
                        out.push_back(e);
                        break;
                    }
                    case NSEventTypeMouseMoved:
                    case NSEventTypeLeftMouseDragged:
                    case NSEventTypeRightMouseDragged:
                    {
                        OSEvent e { OSEventKind::kMouseMove };
                        const NSPoint p = [ev locationInWindow];
                        e.mouse_x = static_cast<float>(p.x);
                        e.mouse_y = static_cast<float>(height_) - static_cast<float>(p.y);
                        out.push_back(e);
                        break;
                    }
                    case NSEventTypeScrollWheel:
                    {
                        OSEvent e { OSEventKind::kMouseWheel };
                        e.wheel = static_cast<float>([ev scrollingDeltaY]);
                        out.push_back(e);
                        break;
                    }
                    default: break;
                }
                [NSApp sendEvent: ev];
            }

            if (window_ != nil && ![window_ isVisible] && !close_requested_)
            {
                close_requested_ = true;
                out.push_back({ OSEventKind::kClose });
            }
            return !close_requested_;
        }
    }

    void request_close() noexcept override { close_requested_ = true; if (window_) [window_ close]; }

    [[nodiscard]] void* native_window_handle() const noexcept override
    {
        return (__bridge void*)metal_layer_;  // CAMetalLayer* for MoltenVK / Metal
    }
    [[nodiscard]] void* native_display_handle() const noexcept override { return nullptr; }
    [[nodiscard]] std::uint32_t width()  const noexcept override { return width_; }
    [[nodiscard]] std::uint32_t height() const noexcept override { return height_; }

    void set_title(std::string_view title) override
    {
        std::string s { title };
        if (window_ != nil)
            [window_ setTitle: [NSString stringWithUTF8String: s.c_str()]];
    }

private:
    NSWindow*     window_       { nil };
    NSView*       view_         { nil };
    CAMetalLayer* metal_layer_  { nil };
    std::uint32_t width_  { 0 };
    std::uint32_t height_ { 0 };
    bool close_requested_ { false };
};

}  // namespace

cd::core::Result<std::unique_ptr<IWindow>> create_window(const WindowDesc& d)
{
    auto w = std::make_unique<CocoaWindow>();
    if (!w->create(d))
    {
        return std::unexpected(platform_errors::make(
            platform_errors::Code::kCreateFailed,
            "CocoaWindow: NSWindow / CAMetalLayer init failed"));
    }
    return w;
}

}  // namespace cd::platform

#endif  // __APPLE__ && CD_PLATFORM_COCOA
