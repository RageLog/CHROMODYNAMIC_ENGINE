# samples/game/hello_input_replay — Phase 662 (M11 W5A closeout).
#
# Console-only proof that cd::game::input_recorder is consumable by a
# downstream caller end-to-end:
#
#   Phase 1 (Recording):
#     * Generate 60 synthetic InputEvents at 16.6 ms intervals (1 second at
#       60 Hz): mouse-move events sweeping a circle + 4 key-press events at
#       predetermined timestamps. Save to tmp/hello_input_replay_demo.bin.
#
#   Phase 2 (Replay):
#     * Load from tmp/, advance current_ms by 16.6 ms per step, drain all
#       due events each frame via next_event(), print each one as it fires.
#     * Print 'PLAYBACK COMPLETE' + stats when finished.
#
# Moment: a developer records 5 seconds of player input to root-cause a flaky
# physics bug, then replays it 100x. This sample shows HOW.
#
# Dependencies (CLAUDE.md §7): cd::game_input_recorder + cd::core.
cd_add_sample(hello_input_replay
  SOURCES main.cpp
  DEPS
    cd::core
    cd::game_input_recorder
)
