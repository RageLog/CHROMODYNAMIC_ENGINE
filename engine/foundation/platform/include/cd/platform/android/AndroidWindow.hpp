// =============================================================================
// CHROMODYNAMIC — cd/platform/android/AndroidWindow.hpp
// Phase 772 — Android NativeActivity window wrapper.
//
// Public facade for the ANativeWindow backend.  The implementation lives in
// engine/foundation/platform/src/AndroidWindow.cpp and is compile-gated by
// #if defined(__ANDROID__).
//
// Usage (from android_main or a NativeActivity-derived host):
//
//   // 1. JNI / NativeActivity delivers a window to your code:
//   extern "C" void cd_platform_set_android_native_window(ANativeWindow*);
//   extern "C" void cd_platform_set_android_input_queue(AInputQueue*);
//
//   // 2. Create the IWindow via the factory — same call site as every
//   //    other platform (Win32, Cocoa, X11):
//   auto window = cd::platform::create_window(desc);
//
// Surface integration:
//   window->native_window_handle() returns the ANativeWindow* cast to void*;
//   feed it to vkCreateAndroidSurfaceKHR via VK_KHR_android_surface.
//
// Lifecycle:
//   ANativeWindow reference count is bumped in attach() and released in dtor.
//   The IActivityLifecycleRouter (ActivityLifecycle.hpp) gates the frame loop
//   on onPause/onResume; AndroidWindow itself is stateless w.r.t. lifecycle.
//
// Thread-safety:
//   cd_platform_set_android_native_window / cd_platform_set_android_input_queue
//   are extern "C" and use std::atomic stores so they are safe to call from any
//   thread (JNI callbacks arrive on the UI thread; android_main runs on a
//   separate native thread).
// =============================================================================
#pragma once

// Forward-declare Android opaque types.  Clients that only call the free
// functions below do not need <android/native_window.h> in scope; the
// implementation TU includes the real headers.
struct ANativeWindow;
struct AInputQueue;

namespace cd::platform
{

// ---- Bridge functions -------------------------------------------------------
//
// Called by the NativeActivity JNI glue (android_native_app_glue) or directly
// from GameActivity callbacks.  Both are extern "C" so they are callable from
// C or Kotlin/JNI without mangling.

} // namespace cd::platform

// Declared at global scope (extern "C") to be callable from android_native_app_glue.

/// Register the ANativeWindow delivered by the system before calling
/// cd::platform::create_window().  Thread-safe (atomic store).
extern "C" void cd_platform_set_android_native_window(ANativeWindow* anw) noexcept;

/// Register the AInputQueue so that pump_events() drains touch / key events.
/// Thread-safe (atomic store).
extern "C" void cd_platform_set_android_input_queue(AInputQueue* iq) noexcept;
