cd_add_test(platform
  SOURCES test_platform.cpp
  DEPS    cd::platform cd::core
)

# Phase 764 / FINALE-6 W1 — multi-window primitive Sprint-1 (Windows-gated).
cd_add_test(platform_multi_window
  SOURCES test_platform_multi_window.cpp
  DEPS    cd::platform cd::core
)
