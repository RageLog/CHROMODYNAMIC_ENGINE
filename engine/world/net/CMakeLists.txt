cd_add_library(net
  TYPE STATIC
  SOURCES
    src/LoopbackConnection.cpp
    src/UdpConnection.cpp
    src/ReliableChannel.cpp
    src/QosDispatcher.cpp
    src/SnapshotReconciler.cpp
  PUBLIC_DEPS cd::core
)

# Real UDP backend pulls in winsock on Windows. The implementation TU
# uses `#pragma comment(lib, "ws2_32")` for MSVC + clang-cl; for plain
# Clang / MinGW GCC we still need the link line:
if(WIN32)
  target_link_libraries(cd_net PUBLIC ws2_32)
endif()
if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
