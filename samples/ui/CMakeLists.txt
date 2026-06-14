# samples/ui — UI tier showcase samples (Vulkan-gated by the parent
# samples/CMakeLists.txt).
#
# phase913-sample-consolidation: hello_widgets_table headless console
# proof folded into engine/ui/widgets/tests/test_table.cpp (7 cases).
#
# phase1177 — hello_ui fold (ADR-20260614 §3.4, Job C / samples≤10). The
# canonical Vulkan visual demo for the cd::ui stack (Button / Slider /
# Label / Flex layout) was folded into hello_engine as the golden-gated
# "UI Widgets Demo" R-Showcase panel, re-using the BehaviorDesigner
# Submitter overlay (Job A/B). The samples/ui/hello_ui source directory
# was deleted. This tier currently hosts no standalone binaries.
