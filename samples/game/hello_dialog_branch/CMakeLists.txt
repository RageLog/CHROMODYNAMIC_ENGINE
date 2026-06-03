# samples/game/hello_dialog_branch — Phase 652 (M10 W5A)
#
# Console-only proof of cd::game::dialog_tree (BG3-style branching graph).
# Builds a 7-node designer-authored DialogTree and runs THREE simulated
# play-throughs to demonstrate how kSay / kChoice / kCondition / kEnd
# routing produces three distinct narrative paths without recompiling.
#
# Moment: a designer reads main.cpp once and knows how to author a
# 200-node tree for their game.
#
# No RHI, no platform window, no Vulkan — headless by design.

cd_add_sample(hello_dialog_branch
  SOURCES main.cpp
  DEPS
    cd::game_dialog_tree
    cd::core
)
