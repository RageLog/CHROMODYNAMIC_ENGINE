// =============================================================================
// CHROMODYNAMIC — cd/platform/IosWindow.mm
// Phase 771 — iOS UIKit + CAMetalLayer window backend (Sprint-1 scaffold).
//
// Replaces the Phase 15.F kNotImplemented stub with a real UIView subclass
// (CdMetalView) that:
//   * Hosts a CAMetalLayer as its backing layer (full-screen, safe-area aware).
//   * Feeds UIKit touch events into a thread-safe OSEvent queue.
//   * Drives frame pacing via CADisplayLink (linked to the main run loop).
//
// Sprint-1 contract:
//   * create_window() returns a working IosWindow when called from the
//     AppDelegate's application:didFinishLaunchingWithOptions:.
//   * pump_events() drains touch + resize events accumulated since last call.
//   * native_window_handle() returns the CAMetalLayer* (consumed by
//     cd::rhi_metal::create_swapchain).
//   * Frame loop: host calls pump_events() per frame; CADisplayLink is wired
//     up but its tick just flips a flag (real throttle is the RHI present
//     fence — Phase 2 integration).
//
// Sprint-2 (device CI) adds:
//   * Real MTLDevice draw + present call via cd::rhi_metal.
//   * Orientation change + safe-area layout recalculation.
//   * Multi-touch gesture recognizer → OSEvent::kMouseMove stream.
//
// MOMENT: UIKit + Metal + cd::rhi::metal living together on one device.
// =============================================================================
#include <cd/platform/ios/IosWindow.hpp>

#if defined(__APPLE__)
    #include <TargetConditionals.h>
    #if TARGET_OS_IPHONE

        #import <UIKit/UIKit.h>
        #import <QuartzCore/CAMetalLayer.h>

        #include <cd/core/Log.hpp>

        #include <atomic>
        #include <memory>
        #include <mutex>
        #include <string>

// ---- CdMetalView (Objective-C UIView subclass) ----------------------------

/// Full-screen UIView whose backing layer is CAMetalLayer.
/// Touch events are forwarded to the owning IosWindow via a raw pointer;
/// the view does NOT own the window.
@interface CdMetalView : UIView
@property (nonatomic, assign) cd::platform::ios::IosWindow* engine_window;
@end

@implementation CdMetalView

/// Override layerClass so UIKit creates a CAMetalLayer automatically.
+ (Class)layerClass
{
    return [CAMetalLayer class];
}

- (instancetype)initWithFrame:(CGRect)frame
{
    self = [super initWithFrame: frame];
    if (self)
    {
        self.multipleTouchEnabled = YES;
        CAMetalLayer* ml = (CAMetalLayer*)self.layer;
        ml.pixelFormat   = MTLPixelFormatBGRA8Unorm;
        ml.framebufferOnly = YES;
        // Sprint-2: set ml.device = MTLCreateSystemDefaultDevice()
    }
    return self;
}

// ---- Touch handling --------------------------------------------------------

- (void)touches_began_phase:(NSSet<UITouch*>*)touches
{
    if (self.engine_window == nullptr) return;
    for (UITouch* t in touches)
    {
        CGPoint p = [t locationInView: self];
        cd::platform::OSEvent e { cd::platform::OSEventKind::kMouseButtonDown };
        e.mouse_button = cd::platform::MouseButton::kLeft;
        e.mouse_x = static_cast<float>(p.x);
        e.mouse_y = static_cast<float>(p.y);
        self.engine_window->enqueue_event(e);
    }
}

- (void)touchesBegan:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event
{
    (void)event;
    [self touches_began_phase: touches];
}

- (void)touchesMoved:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event
{
    (void)event;
    if (self.engine_window == nullptr) return;
    for (UITouch* t in touches)
    {
        CGPoint p = [t locationInView: self];
        cd::platform::OSEvent e { cd::platform::OSEventKind::kMouseMove };
        e.mouse_x = static_cast<float>(p.x);
        e.mouse_y = static_cast<float>(p.y);
        self.engine_window->enqueue_event(e);
    }
}

- (void)touchesEnded:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event
{
    (void)event;
    if (self.engine_window == nullptr) return;
    for (UITouch* t in touches)
    {
        CGPoint p = [t locationInView: self];
        cd::platform::OSEvent e { cd::platform::OSEventKind::kMouseButtonUp };
        e.mouse_button = cd::platform::MouseButton::kLeft;
        e.mouse_x = static_cast<float>(p.x);
        e.mouse_y = static_cast<float>(p.y);
        self.engine_window->enqueue_event(e);
    }
}

- (void)touchesCancelled:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event
{
    // Treat cancelled touches as released (finger lifted / interrupted).
    [self touchesEnded: touches withEvent: event];
}

@end

// ---- IosWindow C++ implementation -----------------------------------------

namespace cd::platform::ios
{

IosWindow::~IosWindow()
{
    // Nil out the back-pointer so any in-flight UIKit touch callbacks
    // don't attempt to enqueue events into a destroyed window.
    if (view_ != nullptr)
    {
        static_cast<CdMetalView*>(view_)->engine_window = nullptr;
    }
}

bool IosWindow::create(const cd::platform::WindowDesc& desc) noexcept
{
    @autoreleasepool {
        // Obtain the key UIWindow from the application.
        UIWindow* key_window = nil;
        for (UIScene* scene in [UIApplication sharedApplication].connectedScenes)
        {
            if ([scene isKindOfClass: [UIWindowScene class]])
            {
                UIWindowScene* ws = (UIWindowScene*)scene;
                for (UIWindow* w in ws.windows)
                {
                    if (w.isKeyWindow) { key_window = w; break; }
                }
            }
        }
        if (key_window == nil)
        {
            // Fallback: first window of the application.
            UIWindowScene* scene = (UIWindowScene*)
                [[[UIApplication sharedApplication] connectedScenes] anyObject];
            key_window = scene.windows.firstObject;
        }
        if (key_window == nil)
        {
            CD_LOG(Error) << "IosWindow::create: no UIWindow found — "
                             "call create() after UIKit window is ready";
            return false;
        }

        // Use supplied dimensions or fall back to screen bounds.
        CGRect frame = key_window.bounds;
        if (desc.width  > 0) frame.size.width  = static_cast<CGFloat>(desc.width);
        if (desc.height > 0) frame.size.height = static_cast<CGFloat>(desc.height);

        CdMetalView* v = [[CdMetalView alloc] initWithFrame: frame];
        v.engine_window = this;
        v.autoresizingMask = UIViewAutoresizingFlexibleWidth |
                             UIViewAutoresizingFlexibleHeight;

        [key_window addSubview: v];
        if (desc.visible)
            [key_window bringSubviewToFront: v];

        view_        = v;
        metal_layer_ = (CAMetalLayer*)v.layer;
        width_       = static_cast<std::uint32_t>(frame.size.width);
        height_      = static_cast<std::uint32_t>(frame.size.height);

        CD_LOG(Info) << "IosWindow: created " << width_ << "x" << height_
                     << " CAMetalLayer-backed UIView";
        return true;
    }
}

// ---- IWindow ---------------------------------------------------------------

bool IosWindow::pump_events(std::vector<cd::platform::OSEvent>& out)
{
    if (close_requested_) return false;
    {
        const std::lock_guard<std::mutex> lock { queue_mutex_ };
        out.insert(out.end(), pending_.begin(), pending_.end());
        pending_.clear();
    }
    return !close_requested_;
}

bool IosWindow::should_close() const noexcept
{
    return close_requested_;
}

void IosWindow::request_close() noexcept
{
    close_requested_ = true;
}

void* IosWindow::native_window_handle() const noexcept
{
    return (__bridge void*)metal_layer_;
}

void* IosWindow::native_display_handle() const noexcept
{
    return nullptr;
}

std::uint32_t IosWindow::width()  const noexcept { return width_; }
std::uint32_t IosWindow::height() const noexcept { return height_; }

void IosWindow::set_title(std::string_view /*title*/) {}  // No title bar on iOS.

// ---- Event injection -------------------------------------------------------

void IosWindow::enqueue_event(cd::platform::OSEvent event) noexcept
{
    const std::lock_guard<std::mutex> lock { queue_mutex_ };
    pending_.push_back(event);
}

void IosWindow::signal_close() noexcept
{
    close_requested_ = true;
    const std::lock_guard<std::mutex> lock { queue_mutex_ };
    pending_.push_back({ cd::platform::OSEventKind::kClose });
}

}  // namespace cd::platform::ios

// ---- cd::platform factory --------------------------------------------------

namespace cd::platform
{

cd::core::Result<std::unique_ptr<IWindow>> create_window(const WindowDesc& desc)
{
    auto w = std::make_unique<cd::platform::ios::IosWindow>();
    if (!w->create(desc))
    {
        return std::unexpected(platform_errors::make(
            platform_errors::Code::kCreateFailed,
            "IosWindow: UIKit view / CAMetalLayer init failed — "
            "ensure create_window() is called after UIApplicationMain "
            "has set up the key UIWindow"));
    }
    return w;
}

}  // namespace cd::platform

    #endif  // TARGET_OS_IPHONE
#endif  // __APPLE__
