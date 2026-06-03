# engine/game/save_compression — cd::game_save_compression (Phase 661, M11 W4B)
#
# Sprint-1: RLE (run-length encoding) compress/decompress for save-game byte
# streams.  Sidecar to cd::game::save — NOT a replacement or version-2.
#
# Naming rationale: the library is called save_compression rather than save_v2
# because it does NOT replace SaveSystem.  Callers compress their byte blob
# before passing it to SaveSystem::save() and decompress after
# SaveSystem::load() returns.  The split keeps I/O and codec concerns
# orthogonal and enables Sprint-2 codec swap (LZ4 / zstd via vcpkg) without
# touching slot-management machinery.
#
# Sprint-2 plan: add compress_lz4() / compress_zstd() overloads behind the
# same CompressedSave envelope.  vcpkg manifest (lz4 / zstd features) will
# gate them so the library stays no-dependency by default.
#
# Dependencies (CLAUDE.md §7): cd::core only — no filesystem, no cd::game::save,
# no rapidjson, no zstd.  Sits at the bottom of the gameplay DAG so it can also
# compress non-save blobs (replay headers, telemetry snapshots, etc.).

cd_add_library(game_save_compression
  TYPE STATIC
  SOURCES
    src/SaveCompression.cpp
  PUBLIC_DEPS
    cd::core
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
