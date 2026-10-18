// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/ControlUiAndNextGenSotaConstructs.hpp
// Umbrella Header & Factory for 91 Standalone C++23 Construct Classes
// =============================================================================
#pragma once

#include <cd/scene/constructs/IChromaConstruct.hpp>

#include <cd/scene/constructs/control_ui/UiFrame.hpp>
#include <cd/scene/constructs/control_ui/UiFlexContainer.hpp>
#include <cd/scene/constructs/control_ui/UiAspectFrame.hpp>
#include <cd/scene/constructs/control_ui/UiAxisBoxContainer.hpp>
#include <cd/scene/constructs/control_ui/UiVerticalStack.hpp>
#include <cd/scene/constructs/control_ui/UiHorizontalRow.hpp>
#include <cd/scene/constructs/control_ui/UiAnchorCenterFrame.hpp>
#include <cd/scene/constructs/control_ui/UiWrapFlowLayout.hpp>
#include <cd/scene/constructs/control_ui/UiVerticalWrapFlow.hpp>
#include <cd/scene/constructs/control_ui/UiHorizontalWrapFlow.hpp>
#include <cd/scene/constructs/control_ui/UiGridMatrixContainer.hpp>
#include <cd/scene/constructs/control_ui/UiSashSplitter.hpp>
#include <cd/scene/constructs/control_ui/UiVerticalSplitter.hpp>
#include <cd/scene/constructs/control_ui/UiHorizontalSplitter.hpp>
#include <cd/scene/constructs/control_ui/UiPaddingInsetFrame.hpp>
#include <cd/scene/constructs/control_ui/UiFrostedCardContainer.hpp>
#include <cd/scene/constructs/control_ui/UiSmoothScrollArea.hpp>
#include <cd/scene/constructs/control_ui/UiDockTabWorkspace.hpp>
#include <cd/scene/constructs/control_ui/UiGlyphText.hpp>
#include <cd/scene/constructs/control_ui/UiRichMarkupText.hpp>
#include <cd/scene/constructs/control_ui/UiGradientQuad.hpp>
#include <cd/scene/constructs/control_ui/UiBindlessSpriteQuad.hpp>
#include <cd/scene/constructs/control_ui/UiGpuVideoSurface.hpp>
#include <cd/scene/constructs/control_ui/UiDividerRule.hpp>
#include <cd/scene/constructs/control_ui/UiHorizontalRule.hpp>
#include <cd/scene/constructs/control_ui/UiVerticalRule.hpp>
#include <cd/scene/constructs/control_ui/UiElevationSurfacePanel.hpp>
#include <cd/scene/constructs/control_ui/UiSdfNineSliceFrame.hpp>
#include <cd/scene/constructs/control_ui/UiInteractiveResponder.hpp>
#include <cd/scene/constructs/control_ui/UiCommandButton.hpp>
#include <cd/scene/constructs/control_ui/UiIconCommandButton.hpp>
#include <cd/scene/constructs/control_ui/UiHyperlinkAction.hpp>
#include <cd/scene/constructs/control_ui/UiBooleanCheckBox.hpp>
#include <cd/scene/constructs/control_ui/UiAnimatedTogglePill.hpp>
#include <cd/scene/constructs/control_ui/UiPopupMenuButton.hpp>
#include <cd/scene/constructs/control_ui/UiSearchableComboDropdown.hpp>
#include <cd/scene/constructs/control_ui/UiHdrSwatchButton.hpp>
#include <cd/scene/constructs/control_ui/UiOklabHdrColorPicker.hpp>
#include <cd/scene/constructs/control_ui/UiTextInputField.hpp>
#include <cd/scene/constructs/control_ui/UiMultilineDocumentBox.hpp>
#include <cd/scene/constructs/control_ui/UiGlslSyntaxEditor.hpp>
#include <cd/scene/constructs/control_ui/UiNumericValueModel.hpp>
#include <cd/scene/constructs/control_ui/UiGaugeBar.hpp>
#include <cd/scene/constructs/control_ui/UiRadialProgressRing.hpp>
#include <cd/scene/constructs/control_ui/UiInlineDragSpinBox.hpp>
#include <cd/scene/constructs/control_ui/UiKineticScrollBar.hpp>
#include <cd/scene/constructs/control_ui/UiHorizontalScrollBar.hpp>
#include <cd/scene/constructs/control_ui/UiVerticalScrollBar.hpp>
#include <cd/scene/constructs/control_ui/UiValueSlider.hpp>
#include <cd/scene/constructs/control_ui/UiHorizontalSlider.hpp>
#include <cd/scene/constructs/control_ui/UiVerticalSlider.hpp>
#include <cd/scene/constructs/control_ui/UiVirtualListView.hpp>
#include <cd/scene/constructs/control_ui/UiRibbonMenuBar.hpp>
#include <cd/scene/constructs/control_ui/UiReorderableTabStrip.hpp>
#include <cd/scene/constructs/control_ui/UiDebugBoundsGuide.hpp>
#include <cd/scene/constructs/control_ui/UiDragDropTreeOutliner.hpp>
#include <cd/scene/constructs/control_ui/UiCanvasPinItem.hpp>
#include <cd/scene/constructs/control_ui/UiShaderGraphBlock.hpp>
#include <cd/scene/constructs/control_ui/UiNodeCanvas.hpp>
#include <cd/scene/constructs/control_ui/UiMultiTouchVirtualPad.hpp>
#include <cd/scene/constructs/sota/HardwareRayTracingProbe3D.hpp>
#include <cd/scene/constructs/sota/DDGIIrradianceVolume3D.hpp>
#include <cd/scene/constructs/sota/TurquinMultiScatterPBR3D.hpp>
#include <cd/scene/constructs/sota/MeshletClusterLOD3D.hpp>
#include <cd/scene/constructs/sota/NeuralTemporalUpscaler3D.hpp>
#include <cd/scene/constructs/sota/XPBDClothAndSoftBody3D.hpp>
#include <cd/scene/constructs/sota/FluidFLIPWater3D.hpp>
#include <cd/scene/constructs/sota/SDFSmoothBooleanCSG3D.hpp>
#include <cd/scene/constructs/sota/BrunetonPlanetarySky3D.hpp>
#include <cd/scene/constructs/sota/FroxelVolumetricFog3D.hpp>
#include <cd/scene/constructs/sota/GPUIndirectCrowd3D.hpp>
#include <cd/scene/constructs/sota/MotionMatchingInertial3D.hpp>
#include <cd/scene/constructs/sota/FullBodyFABRIK3D.hpp>
#include <cd/scene/constructs/sota/VoronoiFractureDestruction3D.hpp>
#include <cd/scene/constructs/sota/AeroHydrodynamicsTensor3D.hpp>
#include <cd/scene/constructs/sota/UtilityGOAPBrainAI.hpp>
#include <cd/scene/constructs/sota/RayTracedAudioAcoustics3D.hpp>
#include <cd/scene/constructs/sota/ProceduralModalSynthAudio.hpp>
#include <cd/scene/constructs/sota/RollbackNetcodeDeterminism.hpp>
#include <cd/scene/constructs/sota/TimeRewindWorldRecorder.hpp>
#include <cd/scene/constructs/sota/PCGWaveFunctionCollapse3D.hpp>
#include <cd/scene/constructs/sota/SplineTerrainRoadDeformer3D.hpp>
#include <cd/scene/constructs/sota/ScreenSpaceGIHorizon3D.hpp>
#include <cd/scene/constructs/sota/PhysicalCinemaLens3D.hpp>
#include <cd/scene/constructs/sota/ReactiveMVVMDataBindUI.hpp>
#include <cd/scene/constructs/sota/GPUVectorSlugCanvas2D.hpp>
#include <cd/scene/constructs/sota/VerletRopeAndBridge2D.hpp>
#include <cd/scene/constructs/sota/GPUCellularAutomata2D.hpp>
#include <cd/scene/constructs/sota/LiveSPIRVShaderGraphNode.hpp>
#include <cd/scene/constructs/sota/DeterministicBotVerifier.hpp>
#include <cd/scene/constructs/sota/ECSArchetypeQueryBus.hpp>

namespace cd::scene
{

[[nodiscard]] inline std::unique_ptr<IChromaConstruct> instantiate_control_ui_or_sota_construct(
    std::uint16_t id)
{
    switch (id)
    {
        case 159: return std::make_unique<UiFrame>();
        case 160: return std::make_unique<UiFlexContainer>();
        case 161: return std::make_unique<UiAspectFrame>();
        case 162: return std::make_unique<UiAxisBoxContainer>();
        case 163: return std::make_unique<UiVerticalStack>();
        case 164: return std::make_unique<UiHorizontalRow>();
        case 165: return std::make_unique<UiAnchorCenterFrame>();
        case 166: return std::make_unique<UiWrapFlowLayout>();
        case 167: return std::make_unique<UiVerticalWrapFlow>();
        case 168: return std::make_unique<UiHorizontalWrapFlow>();
        case 169: return std::make_unique<UiGridMatrixContainer>();
        case 170: return std::make_unique<UiSashSplitter>();
        case 171: return std::make_unique<UiVerticalSplitter>();
        case 172: return std::make_unique<UiHorizontalSplitter>();
        case 173: return std::make_unique<UiPaddingInsetFrame>();
        case 174: return std::make_unique<UiFrostedCardContainer>();
        case 175: return std::make_unique<UiSmoothScrollArea>();
        case 176: return std::make_unique<UiDockTabWorkspace>();
        case 177: return std::make_unique<UiGlyphText>();
        case 178: return std::make_unique<UiRichMarkupText>();
        case 179: return std::make_unique<UiGradientQuad>();
        case 180: return std::make_unique<UiBindlessSpriteQuad>();
        case 181: return std::make_unique<UiGpuVideoSurface>();
        case 182: return std::make_unique<UiDividerRule>();
        case 183: return std::make_unique<UiHorizontalRule>();
        case 184: return std::make_unique<UiVerticalRule>();
        case 185: return std::make_unique<UiElevationSurfacePanel>();
        case 186: return std::make_unique<UiSdfNineSliceFrame>();
        case 187: return std::make_unique<UiInteractiveResponder>();
        case 188: return std::make_unique<UiCommandButton>();
        case 189: return std::make_unique<UiIconCommandButton>();
        case 190: return std::make_unique<UiHyperlinkAction>();
        case 191: return std::make_unique<UiBooleanCheckBox>();
        case 192: return std::make_unique<UiAnimatedTogglePill>();
        case 193: return std::make_unique<UiPopupMenuButton>();
        case 194: return std::make_unique<UiSearchableComboDropdown>();
        case 195: return std::make_unique<UiHdrSwatchButton>();
        case 196: return std::make_unique<UiOklabHdrColorPicker>();
        case 197: return std::make_unique<UiTextInputField>();
        case 198: return std::make_unique<UiMultilineDocumentBox>();
        case 199: return std::make_unique<UiGlslSyntaxEditor>();
        case 200: return std::make_unique<UiNumericValueModel>();
        case 201: return std::make_unique<UiGaugeBar>();
        case 202: return std::make_unique<UiRadialProgressRing>();
        case 203: return std::make_unique<UiInlineDragSpinBox>();
        case 204: return std::make_unique<UiKineticScrollBar>();
        case 205: return std::make_unique<UiHorizontalScrollBar>();
        case 206: return std::make_unique<UiVerticalScrollBar>();
        case 207: return std::make_unique<UiValueSlider>();
        case 208: return std::make_unique<UiHorizontalSlider>();
        case 209: return std::make_unique<UiVerticalSlider>();
        case 210: return std::make_unique<UiVirtualListView>();
        case 211: return std::make_unique<UiRibbonMenuBar>();
        case 212: return std::make_unique<UiReorderableTabStrip>();
        case 213: return std::make_unique<UiDebugBoundsGuide>();
        case 214: return std::make_unique<UiDragDropTreeOutliner>();
        case 215: return std::make_unique<UiCanvasPinItem>();
        case 216: return std::make_unique<UiShaderGraphBlock>();
        case 217: return std::make_unique<UiNodeCanvas>();
        case 218: return std::make_unique<UiMultiTouchVirtualPad>();
        case 219: return std::make_unique<HardwareRayTracingProbe3D>();
        case 220: return std::make_unique<DDGIIrradianceVolume3D>();
        case 221: return std::make_unique<TurquinMultiScatterPBR3D>();
        case 222: return std::make_unique<MeshletClusterLOD3D>();
        case 223: return std::make_unique<NeuralTemporalUpscaler3D>();
        case 224: return std::make_unique<XPBDClothAndSoftBody3D>();
        case 225: return std::make_unique<FluidFLIPWater3D>();
        case 226: return std::make_unique<SDFSmoothBooleanCSG3D>();
        case 227: return std::make_unique<BrunetonPlanetarySky3D>();
        case 228: return std::make_unique<FroxelVolumetricFog3D>();
        case 229: return std::make_unique<GPUIndirectCrowd3D>();
        case 230: return std::make_unique<MotionMatchingInertial3D>();
        case 231: return std::make_unique<FullBodyFABRIK3D>();
        case 232: return std::make_unique<VoronoiFractureDestruction3D>();
        case 233: return std::make_unique<AeroHydrodynamicsTensor3D>();
        case 234: return std::make_unique<UtilityGOAPBrainAI>();
        case 235: return std::make_unique<RayTracedAudioAcoustics3D>();
        case 236: return std::make_unique<ProceduralModalSynthAudio>();
        case 237: return std::make_unique<RollbackNetcodeDeterminism>();
        case 238: return std::make_unique<TimeRewindWorldRecorder>();
        case 239: return std::make_unique<PCGWaveFunctionCollapse3D>();
        case 240: return std::make_unique<SplineTerrainRoadDeformer3D>();
        case 241: return std::make_unique<ScreenSpaceGIHorizon3D>();
        case 242: return std::make_unique<PhysicalCinemaLens3D>();
        case 243: return std::make_unique<ReactiveMVVMDataBindUI>();
        case 244: return std::make_unique<GPUVectorSlugCanvas2D>();
        case 245: return std::make_unique<VerletRopeAndBridge2D>();
        case 246: return std::make_unique<GPUCellularAutomata2D>();
        case 247: return std::make_unique<LiveSPIRVShaderGraphNode>();
        case 248: return std::make_unique<DeterministicBotVerifier>();
        case 249: return std::make_unique<ECSArchetypeQueryBus>();
        default:  return nullptr;
    }
}

} // namespace cd::scene
