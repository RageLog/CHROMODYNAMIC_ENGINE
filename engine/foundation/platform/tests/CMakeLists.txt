cd_add_test(platform
  SOURCES test_platform.cpp
  DEPS    cd::platform cd::core
)

# Phase 764 / FINALE-6 W1 — multi-window primitive Sprint-1 (Windows-gated).
cd_add_test(platform_multi_window
  SOURCES test_platform_multi_window.cpp
  DEPS    cd::platform cd::core
)

# Band4 singletons — header-only IWindow logic (pump_all_windows branches +
# default set_parent + error-code make). Host-agnostic via a fake IWindow.
cd_add_test(platform_window_iface
  SOURCES test_platform_window_iface.cpp
  DEPS    cd::platform cd::core
)
