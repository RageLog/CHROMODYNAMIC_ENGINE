# engine/foundation/platform — cd::platform
# Sprint S2.1.e — ADR-005 §G + ADR-017 P0
# Sprint S6.1 — IWindow + Win32 backend.

cd_add_library(platform
  SOURCES
    src/SignalHandler.cpp
    src/Win32Window.cpp
    src/PlatformStub.cpp
  PUBLIC_DEPS cd::core
)

if(WIN32)
  # User32 + GDI are required for CreateWindowEx / DefWindowProc /
  # LoadCursor — Win32Window.cpp pulls them in unconditionally.
  target_link_libraries(cd_platform PRIVATE user32 gdi32)
endif()

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
