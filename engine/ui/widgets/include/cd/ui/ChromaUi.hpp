// =============================================================================
// CHROMODYNAMIC — cd/ui/ChromaUi.hpp
//
// Phase 11 (ChromaUI): Ultra-advanced GPU/CPU hybrid Immediate + Retained UI
// library built natively on:
//   * cd::ui::renderer::DrawBatcher   (CPU vertex/index/command batching)
//   * cd::ui::renderer_rhi::Submitter (Vulkan 1.3 / RHI inline SDF & gradient GPU pipeline)
//   * cd::ui::font::Font              (TTF font metrics + built-in greedy 2D stroke glyph engine)
//   * cd::platform::OSEvent           (Cross-platform window input router)
//
// Provides:
//   1. Multi-layer Vector & SDF Canvas (`DrawList`: thick AA lines, circles,
//      triangles, GPU SDF rounded rects, drop shadows, linear gradients, crisp text).
//   2. Responsive DockSpace with interactive draggable splitters, scrollable
//      docked panels, floating HUD windows, and z-ordered popups/dropdowns.
//   3. Rich Widget Catalog: Button, SmallButton, Checkbox, SliderFloat,
//      X/Y/Z color-coded DragFloat3, ColorEdit3 with swatch, Combo dropdown,
//      InputText/InputTextWithHint, Selectable, CollapsingHeader, TabBar/TabItem,
//      N-column responsive Table, MainMenuBar/Menu/MenuItem, Tooltip, and
//      native Drag-and-Drop payload system with floating ghost badges.
//   4. Drop-in immediate-mode compatibility facade (`namespace ImGui`) so the
//      entire CHROMODYNAMIC Studio Editor runs with 0% Dear ImGui dependency.
// =============================================================================
#pragma once

#include <cd/core/ErrorCode.hpp>
#include <cd/core/Result.hpp>
#include <cd/platform/Window.hpp>
#include <cd/rhi/Format.hpp>
#include <cd/rhi/ICommandBuffer.hpp>
#include <cd/rhi/IDevice.hpp>
#include <cd/ui/font/Font.hpp>
#include <cd/ui/renderer/DrawBatcher.hpp>
#include <cd/ui/renderer_rhi/Submitter.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <numbers>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

// =============================================================================
// Fundamental 2D / Color / ID types
// =============================================================================
struct ImVec2
{
    float x { 0.0F };
    float y { 0.0F };

    constexpr ImVec2() noexcept = default;
    constexpr ImVec2(float vx, float vy) noexcept : x(vx), y(vy) {}
};

struct ImVec4
{
    float x { 0.0F };
    float y { 0.0F };
    float z { 0.0F };
    float w { 0.0F };

    constexpr ImVec4() noexcept = default;
    constexpr ImVec4(float vx, float vy, float vz, float vw) noexcept
        : x(vx), y(vy), z(vz), w(vw) {}
};

using ImU32   = std::uint32_t;
using ImGuiID = std::uint32_t;

#ifndef IM_COL32
#define IM_COL32(R, G, B, A)                                                   \
    ((static_cast<ImU32>(static_cast<std::uint8_t>(A)) << 24U) |               \
     (static_cast<ImU32>(static_cast<std::uint8_t>(B)) << 16U) |               \
     (static_cast<ImU32>(static_cast<std::uint8_t>(G)) << 8U)  |               \
     (static_cast<ImU32>(static_cast<std::uint8_t>(R)) << 0U))
#endif

#ifndef IM_ARRAYSIZE
#define IM_ARRAYSIZE(_ARR) (static_cast<int>(sizeof(_ARR) / sizeof(*(_ARR))))
#endif

// =============================================================================
// Flags & Enums
// =============================================================================
using ImGuiWindowFlags   = int;
using ImGuiDockNodeFlags = int;
using ImGuiTableFlags    = int;
using ImGuiTreeNodeFlags = int;
using ImGuiDragDropFlags = int;
using ImGuiSelectableFlags = int;
using ImGuiInputTextFlags  = int;
using ImGuiCond          = int;
using ImGuiDir           = int;
using ImGuiStyleVar      = int;
using ImGuiCol           = int;
using ImGuiKey           = int;
using ImGuiMouseButton   = int;

enum ImGuiWindowFlags_ : int
{
    ImGuiWindowFlags_None                      = 0,
    ImGuiWindowFlags_NoTitleBar                = 1 << 0,
    ImGuiWindowFlags_NoResize                  = 1 << 1,
    ImGuiWindowFlags_NoMove                    = 1 << 2,
    ImGuiWindowFlags_NoScrollbar               = 1 << 3,
    ImGuiWindowFlags_NoScrollWithMouse         = 1 << 4,
    ImGuiWindowFlags_NoCollapse                = 1 << 5,
    ImGuiWindowFlags_AlwaysAutoResize          = 1 << 6,
    ImGuiWindowFlags_NoBackground              = 1 << 7,
    ImGuiWindowFlags_NoSavedSettings           = 1 << 8,
    ImGuiWindowFlags_NoMouseInputs             = 1 << 9,
    ImGuiWindowFlags_MenuBar                   = 1 << 10,
    ImGuiWindowFlags_HorizontalScrollbar       = 1 << 11,
    ImGuiWindowFlags_NoFocusOnAppearing        = 1 << 12,
    ImGuiWindowFlags_NoBringToFrontOnFocus     = 1 << 13,
    ImGuiWindowFlags_AlwaysVerticalScrollbar   = 1 << 14,
    ImGuiWindowFlags_AlwaysHorizontalScrollbar = 1 << 15,
    ImGuiWindowFlags_NoNavInputs               = 1 << 16,
    ImGuiWindowFlags_NoNavFocus                = 1 << 17,
    ImGuiWindowFlags_NoDocking                 = 1 << 18,
    ImGuiWindowFlags_NoNav       = ImGuiWindowFlags_NoNavInputs | ImGuiWindowFlags_NoNavFocus,
    ImGuiWindowFlags_NoDecoration = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                    ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse
};

enum ImGuiDockNodeFlags_ : int
{
    ImGuiDockNodeFlags_None                = 0,
    ImGuiDockNodeFlags_DockSpace           = 1 << 0,
    ImGuiDockNodeFlags_PassthruCentralNode = 1 << 1
};

enum ImGuiTableFlags_ : int
{
    ImGuiTableFlags_None              = 0,
    ImGuiTableFlags_SizingStretchSame = 1 << 0
};

enum ImGuiTreeNodeFlags_ : int
{
    ImGuiTreeNodeFlags_None             = 0,
    ImGuiTreeNodeFlags_Selected         = 1 << 0,
    ImGuiTreeNodeFlags_NoTreePushOnOpen = 1 << 3,
    ImGuiTreeNodeFlags_DefaultOpen      = 1 << 5,
    ImGuiTreeNodeFlags_Leaf             = 1 << 8
};

enum ImGuiDragDropFlags_ : int
{
    ImGuiDragDropFlags_None              = 0,
    ImGuiDragDropFlags_SourceAllowNullID = 1 << 0
};

enum ImGuiCond_ : int
{
    ImGuiCond_None         = 0,
    ImGuiCond_Always       = 1 << 0,
    ImGuiCond_Once         = 1 << 1,
    ImGuiCond_FirstUseEver = 1 << 2,
    ImGuiCond_Appearing    = 1 << 3
};

enum ImGuiDir_ : int
{
    ImGuiDir_None  = -1,
    ImGuiDir_Left  = 0,
    ImGuiDir_Right = 1,
    ImGuiDir_Up    = 2,
    ImGuiDir_Down  = 3
};

enum ImGuiStyleVar_ : int
{
    ImGuiStyleVar_Alpha = 0,
    ImGuiStyleVar_WindowPadding,
    ImGuiStyleVar_WindowRounding,
    ImGuiStyleVar_WindowBorderSize,
    ImGuiStyleVar_FramePadding,
    ImGuiStyleVar_FrameRounding,
    ImGuiStyleVar_FrameBorderSize,
    ImGuiStyleVar_ItemSpacing,
    ImGuiStyleVar_ItemInnerSpacing,
    ImGuiStyleVar_IndentSpacing,
    ImGuiStyleVar_ScrollbarSize,
    ImGuiStyleVar_GrabMinSize
};

enum ImGuiCol_ : int
{
    ImGuiCol_Text = 0,
    ImGuiCol_TextDisabled,
    ImGuiCol_WindowBg,
    ImGuiCol_ChildBg,
    ImGuiCol_PopupBg,
    ImGuiCol_Border,
    ImGuiCol_FrameBg,
    ImGuiCol_FrameBgHovered,
    ImGuiCol_FrameBgActive,
    ImGuiCol_TitleBg,
    ImGuiCol_TitleBgActive,
    ImGuiCol_Button,
    ImGuiCol_ButtonHovered,
    ImGuiCol_ButtonActive,
    ImGuiCol_Header,
    ImGuiCol_HeaderHovered,
    ImGuiCol_HeaderActive,
    ImGuiCol_Separator,
    ImGuiCol_SliderGrab,
    ImGuiCol_SliderGrabActive,
    ImGuiCol_CheckMark
};

enum ImGuiConfigFlags_ : int
{
    ImGuiConfigFlags_None          = 0,
    ImGuiConfigFlags_DockingEnable = 1 << 0
};

enum ImGuiMouseButton_ : int
{
    ImGuiMouseButton_Left   = 0,
    ImGuiMouseButton_Right  = 1,
    ImGuiMouseButton_Middle = 2
};

enum ImGuiKey_ : int
{
    ImGuiKey_None = 0,
    ImGuiKey_1 = 1,
    ImGuiKey_2 = 2,
    ImGuiKey_3 = 3,
    ImGuiKey_4 = 4,
    ImGuiKey_A = 9,
    ImGuiKey_B = 10,
    ImGuiKey_D = 11,
    ImGuiKey_G = 12,
    ImGuiKey_O = 13,
    ImGuiKey_P = 14,
    ImGuiKey_R = 15,
    ImGuiKey_S = 16,
    ImGuiKey_Y = 17,
    ImGuiKey_Z = 18,
    ImGuiKey_C = 19,
    ImGuiKey_V = 20,
    ImGuiKey_X = 21,
    ImGuiKey_W = 22,
    ImGuiKey_E = 23,
    ImGuiKey_F = 24,
    ImGuiKey_F1 = 30,
    ImGuiKey_F2 = 31,
    ImGuiKey_F5 = 32,
    ImGuiKey_F8 = 33,
    ImGuiKey_F9 = 34,
    ImGuiKey_F3 = 35,
    ImGuiKey_F4 = 36,
    ImGuiKey_Enter = 40,
    ImGuiKey_UpArrow = 41,
    ImGuiKey_DownArrow = 42,
    ImGuiKey_LeftArrow = 43,
    ImGuiKey_RightArrow = 44,
    ImGuiKey_PageUp = 45,
    ImGuiKey_GraveAccent = 46,
    ImGuiKey_Delete = 47,
    ImGuiKey_Backspace = 48,
    ImGuiKey_Space = 49,
    ImGuiKey_Escape = 50,
    ImGuiKey_Home = 51,
    ImGuiKey_End = 52,
    ImGuiKey_Tab = 53,
    ImGuiMod_Ctrl  = 1 << 12,
    ImGuiMod_Shift = 1 << 13,
    ImGuiMod_Alt   = 1 << 14
};

struct ImGuiViewport
{
    ImGuiID ID { 1U };
    ImVec2  Pos { 0.0F, 0.0F };
    ImVec2  Size { 1600.0F, 900.0F };
    ImVec2  WorkPos { 0.0F, 60.0F };
    ImVec2  WorkSize { 1600.0F, 840.0F };
};

struct ImGuiPayload
{
    const void* Data { nullptr };
    int         DataSize { 0 };
    char        DataType[32] {};
};

struct ImGuiIO
{
    ImVec2 MousePos { 0.0F, 0.0F };
    ImVec2 DisplaySize { 1600.0F, 900.0F };
    float  DeltaTime { 1.0F / 60.0F };
    float  MouseWheel { 0.0F };
    bool   WantCaptureMouse { false };
    bool   WantCaptureKeyboard { false };
    int    ConfigFlags { ImGuiConfigFlags_DockingEnable };
};

// =============================================================================
// ChromaUI Core Engine (`cd::ui::chroma`)
// =============================================================================
namespace cd::ui::chroma
{

enum class IconKind : std::uint8_t
{
    kPlay = 0,
    kStop,
    kPause,
    kStep,
    kTranslate,
    kRotate,
    kScale,
    kSnap,
    kCube,
    kSphere,
    kLight,
    kCamera,
    kSave,
    kFolder,
    kGear,
    kTrash,
    kSearch,
    kCheck,
    kChevronRight,
    kChevronDown,
    kHeart,
    kShield,
    kBolt,
    kCrosshair,
    kNode
};

[[nodiscard]] inline cd::ui::renderer::Color unpack_u32(ImU32 col) noexcept
{
    return cd::ui::renderer::Color {
        static_cast<std::uint8_t>((col >> 0U)  & 0xFFU),
        static_cast<std::uint8_t>((col >> 8U)  & 0xFFU),
        static_cast<std::uint8_t>((col >> 16U) & 0xFFU),
        static_cast<std::uint8_t>((col >> 24U) & 0xFFU)
    };
}

[[nodiscard]] inline ImU32 pack_vec4(const ImVec4& c) noexcept
{
    const auto r = static_cast<std::uint8_t>(std::clamp(c.x * 255.0F, 0.0F, 255.0F));
    const auto g = static_cast<std::uint8_t>(std::clamp(c.y * 255.0F, 0.0F, 255.0F));
    const auto b = static_cast<std::uint8_t>(std::clamp(c.z * 255.0F, 0.0F, 255.0F));
    const auto a = static_cast<std::uint8_t>(std::clamp(c.w * 255.0F, 0.0F, 255.0F));
    return IM_COL32(r, g, b, a);
}

[[nodiscard]] inline ImVec4 unpack_vec4(ImU32 col) noexcept
{
    return ImVec4 {
        static_cast<float>((col >> 0U)  & 0xFFU) / 255.0F,
        static_cast<float>((col >> 8U)  & 0xFFU) / 255.0F,
        static_cast<float>((col >> 16U) & 0xFFU) / 255.0F,
        static_cast<float>((col >> 24U) & 0xFFU) / 255.0F
    };
}

[[nodiscard]] inline ImGuiID hash_str(std::string_view s, ImGuiID seed = 2166136261U) noexcept
{
    ImGuiID h = seed;
    for (char c : s)
    {
        h ^= static_cast<ImGuiID>(static_cast<unsigned char>(c));
        h *= 16777619U;
    }
    return h == 0U ? 1U : h;
}

/// Strip Dear ImGui-style `##id` suffix from display labels.
[[nodiscard]] inline std::string_view visible_label(std::string_view raw) noexcept
{
    const auto pos = raw.find("##");
    return (pos == std::string_view::npos) ? raw : raw.substr(0, pos);
}

/// Decode one UTF-8 codepoint from `sv` starting at `idx`, advancing `idx`.
[[nodiscard]] inline std::uint32_t next_utf8_codepoint(std::string_view sv, std::size_t& idx) noexcept
{
    if (idx >= sv.size()) return 0U;
    const auto b0 = static_cast<std::uint8_t>(sv[idx++]);
    if (b0 < 0x80U) return b0;
    std::uint32_t cp = 0xFFFDU;
    int extra = 0;
    if ((b0 & 0xE0U) == 0xC0U)      { cp = b0 & 0x1FU; extra = 1; }
    else if ((b0 & 0xF0U) == 0xE0U) { cp = b0 & 0x0FU; extra = 2; }
    else if ((b0 & 0xF8U) == 0xF0U) { cp = b0 & 0x07U; extra = 3; }
    else return 0xFFFDU;
    for (int k = 0; k < extra && idx < sv.size(); ++k)
    {
        const auto bn = static_cast<std::uint8_t>(sv[idx]);
        if ((bn & 0xC0U) != 0x80U) return 0xFFFDU;
        ++idx;
        cp = (cp << 6U) | (bn & 0x3FU);
    }
    if (cp == 0x2013U || cp == 0x2014U || cp == 0x2212U) return static_cast<std::uint32_t>('-');
    if (cp == 0x2018U || cp == 0x2019U) return static_cast<std::uint32_t>('\'');
    if (cp == 0x201CU || cp == 0x201DU) return static_cast<std::uint32_t>('"');
    if (cp == 0x2022U) return static_cast<std::uint32_t>('*');
    if (cp == 0x2192U) return static_cast<std::uint32_t>('>');
    return cp;
}

// -----------------------------------------------------------------------------
// Greedy 2D Rectangle-Decomposed 5x7 Vector-Quad Font Engine (Fallback)
// -----------------------------------------------------------------------------
struct GlyphStrokeRect
{
    std::uint8_t x { 0 };
    std::uint8_t y { 0 };
    std::uint8_t w { 1 };
    std::uint8_t h { 1 };
};

struct DecomposedGlyph
{
    std::array<GlyphStrokeRect, 12> rects {};
    std::uint8_t                    count { 0 };
};

[[nodiscard]] inline const std::array<DecomposedGlyph, 96>& glyph_stroke_table() noexcept
{
    static const std::array<DecomposedGlyph, 96> kTable = []()
    {
        // 5x7 bitmap rows (bit 4 = leftmost x=0, bit 0 = rightmost x=4) for ASCII 32..127
        static constexpr std::uint8_t kFont5x7[96][7] = {
            {0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 32 ' '
            {0x04,0x04,0x04,0x04,0x04,0x00,0x04}, // 33 '!'
            {0x0A,0x0A,0x0A,0x00,0x00,0x00,0x00}, // 34 '"'
            {0x0A,0x0A,0x1F,0x0A,0x1F,0x0A,0x0A}, // 35 '#'
            {0x04,0x0F,0x14,0x0E,0x05,0x1E,0x04}, // 36 '$'
            {0x18,0x19,0x02,0x04,0x08,0x13,0x03}, // 37 '%'
            {0x0C,0x12,0x14,0x08,0x15,0x12,0x0D}, // 38 '&'
            {0x04,0x04,0x08,0x00,0x00,0x00,0x00}, // 39 '''
            {0x02,0x04,0x08,0x08,0x08,0x04,0x02}, // 40 '('
            {0x08,0x04,0x02,0x02,0x02,0x04,0x08}, // 41 ')'
            {0x00,0x04,0x15,0x0E,0x15,0x04,0x00}, // 42 '*'
            {0x00,0x04,0x04,0x1F,0x04,0x04,0x00}, // 43 '+'
            {0x00,0x00,0x00,0x00,0x04,0x04,0x08}, // 44 ','
            {0x00,0x00,0x00,0x1F,0x00,0x00,0x00}, // 45 '-'
            {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C}, // 46 '.'
            {0x01,0x01,0x02,0x04,0x08,0x10,0x10}, // 47 '/'
            {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}, // 48 '0'
            {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}, // 49 '1'
            {0x0E,0x11,0x01,0x06,0x08,0x10,0x1F}, // 50 '2'
            {0x1F,0x02,0x04,0x06,0x01,0x11,0x0E}, // 51 '3'
            {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}, // 52 '4'
            {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E}, // 53 '5'
            {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E}, // 54 '6'
            {0x1F,0x01,0x02,0x04,0x08,0x08,0x08}, // 55 '7'
            {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}, // 56 '8'
            {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C}, // 57 '9'
            {0x00,0x0C,0x0C,0x00,0x0C,0x0C,0x00}, // 58 ':'
            {0x00,0x0C,0x0C,0x00,0x0C,0x04,0x08}, // 59 ';'
            {0x02,0x04,0x08,0x10,0x08,0x04,0x02}, // 60 '<'
            {0x00,0x00,0x1F,0x00,0x1F,0x00,0x00}, // 61 '='
            {0x08,0x04,0x02,0x01,0x02,0x04,0x08}, // 62 '>'
            {0x0E,0x11,0x01,0x02,0x04,0x00,0x04}, // 63 '?'
            {0x0E,0x11,0x17,0x15,0x17,0x10,0x0E}, // 64 '@'
            {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}, // 65 'A'
            {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}, // 66 'B'
            {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}, // 67 'C'
            {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E}, // 68 'D'
            {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}, // 69 'E'
            {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10}, // 70 'F'
            {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F}, // 71 'G'
            {0x11,0x11,0x11,0x1F,0x11,0x11,0x11}, // 72 'H'
            {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E}, // 73 'I'
            {0x07,0x02,0x02,0x02,0x02,0x12,0x0C}, // 74 'J'
            {0x11,0x12,0x14,0x18,0x14,0x12,0x11}, // 75 'K'
            {0x10,0x10,0x10,0x10,0x10,0x10,0x1F}, // 76 'L'
            {0x11,0x1B,0x15,0x15,0x11,0x11,0x11}, // 77 'M'
            {0x11,0x19,0x15,0x13,0x11,0x11,0x11}, // 78 'N'
            {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}, // 79 'O'
            {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}, // 80 'P'
            {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}, // 81 'Q'
            {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}, // 82 'R'
            {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}, // 83 'S'
            {0x1F,0x04,0x04,0x04,0x04,0x04,0x04}, // 84 'T'
            {0x11,0x11,0x11,0x11,0x11,0x11,0x0E}, // 85 'U'
            {0x11,0x11,0x11,0x11,0x11,0x0A,0x04}, // 86 'V'
            {0x11,0x11,0x11,0x15,0x15,0x15,0x0A}, // 87 'W'
            {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11}, // 88 'X'
            {0x11,0x11,0x0A,0x04,0x04,0x04,0x04}, // 89 'Y'
            {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}, // 90 'Z'
            {0x0E,0x08,0x08,0x08,0x08,0x08,0x0E}, // 91 '['
            {0x10,0x10,0x08,0x04,0x02,0x01,0x01}, // 92 '\'
            {0x0E,0x02,0x02,0x02,0x02,0x02,0x0E}, // 93 ']'
            {0x04,0x0A,0x11,0x00,0x00,0x00,0x00}, // 94 '^'
            {0x00,0x00,0x00,0x00,0x00,0x00,0x1F}, // 95 '_'
            {0x08,0x04,0x02,0x00,0x00,0x00,0x00}, // 96 '`'
            {0x00,0x00,0x0E,0x01,0x0F,0x11,0x0F}, // 97 'a'
            {0x10,0x10,0x1E,0x11,0x11,0x11,0x1E}, // 98 'b'
            {0x00,0x00,0x0E,0x11,0x10,0x11,0x0E}, // 99 'c'
            {0x01,0x01,0x0F,0x11,0x11,0x11,0x0F}, // 100 'd'
            {0x00,0x00,0x0E,0x11,0x1F,0x10,0x0E}, // 101 'e'
            {0x06,0x08,0x1E,0x08,0x08,0x08,0x08}, // 102 'f'
            {0x00,0x00,0x0F,0x11,0x0F,0x01,0x0E}, // 103 'g'
            {0x10,0x10,0x1E,0x11,0x11,0x11,0x11}, // 104 'h'
            {0x04,0x00,0x0C,0x04,0x04,0x04,0x0E}, // 105 'i'
            {0x02,0x00,0x06,0x02,0x02,0x12,0x0C}, // 106 'j'
            {0x10,0x10,0x12,0x14,0x18,0x14,0x12}, // 107 'k'
            {0x0C,0x04,0x04,0x04,0x04,0x04,0x0E}, // 108 'l'
            {0x00,0x00,0x1A,0x15,0x15,0x11,0x11}, // 109 'm'
            {0x00,0x00,0x1E,0x11,0x11,0x11,0x11}, // 110 'n'
            {0x00,0x00,0x0E,0x11,0x11,0x11,0x0E}, // 111 'o'
            {0x00,0x00,0x1E,0x11,0x1E,0x10,0x10}, // 112 'p'
            {0x00,0x00,0x0F,0x11,0x0F,0x01,0x01}, // 113 'q'
            {0x00,0x00,0x16,0x19,0x10,0x10,0x10}, // 114 'r'
            {0x00,0x00,0x0F,0x10,0x0E,0x01,0x1E}, // 115 's'
            {0x08,0x08,0x1E,0x08,0x08,0x09,0x06}, // 116 't'
            {0x00,0x00,0x11,0x11,0x11,0x13,0x0D}, // 117 'u'
            {0x00,0x00,0x11,0x11,0x11,0x0A,0x04}, // 118 'v'
            {0x00,0x00,0x11,0x11,0x15,0x15,0x0A}, // 119 'w'
            {0x00,0x00,0x11,0x0A,0x04,0x0A,0x11}, // 120 'x'
            {0x00,0x00,0x11,0x11,0x0F,0x01,0x0E}, // 121 'y'
            {0x00,0x00,0x1F,0x02,0x04,0x08,0x1F}, // 122 'z'
            {0x02,0x04,0x04,0x08,0x04,0x04,0x02}, // 123 '{'
            {0x04,0x04,0x04,0x04,0x04,0x04,0x04}, // 124 '|'
            {0x08,0x04,0x04,0x02,0x04,0x04,0x08}, // 125 '}'
            {0x00,0x00,0x08,0x15,0x02,0x00,0x00}, // 126 '~'
            {0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F}  // 127 DEL
        };

        std::array<DecomposedGlyph, 96> out {};
        for (std::size_t gi = 0; gi < 96; ++gi)
        {
            bool grid[7][5] {};
            for (int r = 0; r < 7; ++r)
            {
                for (int c = 0; c < 5; ++c)
                {
                    grid[r][c] = ((kFont5x7[gi][r] >> (4 - c)) & 1U) != 0U;
                }
            }

            DecomposedGlyph dg {};
            while (dg.count < dg.rects.size())
            {
                int best_area = 0;
                GlyphStrokeRect best {};
                for (int r = 0; r < 7; ++r)
                {
                    for (int c = 0; c < 5; ++c)
                    {
                        if (!grid[r][c]) continue;
                        // Option A: expand horizontally then vertically
                        int wA = 1;
                        while (c + wA < 5 && grid[r][c + wA]) ++wA;
                        int hA = 1;
                        while (r + hA < 7)
                        {
                            bool ok = true;
                            for (int cc = c; cc < c + wA; ++cc)
                                if (!grid[r + hA][cc]) { ok = false; break; }
                            if (!ok) break;
                            ++hA;
                        }
                        if (wA * hA > best_area)
                        {
                            best_area = wA * hA;
                            best = { static_cast<std::uint8_t>(c),
                                     static_cast<std::uint8_t>(r),
                                     static_cast<std::uint8_t>(wA),
                                     static_cast<std::uint8_t>(hA) };
                        }
                        // Option B: expand vertically then horizontally
                        int hB = 1;
                        while (r + hB < 7 && grid[r + hB][c]) ++hB;
                        int wB = 1;
                        while (c + wB < 5)
                        {
                            bool ok = true;
                            for (int rr = r; rr < r + hB; ++rr)
                                if (!grid[rr][c + wB]) { ok = false; break; }
                            if (!ok) break;
                            ++wB;
                        }
                        if (wB * hB > best_area)
                        {
                            best_area = wB * hB;
                            best = { static_cast<std::uint8_t>(c),
                                     static_cast<std::uint8_t>(r),
                                     static_cast<std::uint8_t>(wB),
                                     static_cast<std::uint8_t>(hB) };
                        }
                    }
                }
                if (best_area == 0) break;
                dg.rects[dg.count++] = best;
                for (int rr = best.y; rr < best.y + best.h; ++rr)
                    for (int cc = best.x; cc < best.x + best.w; ++cc)
                        grid[rr][cc] = false;
            }
            out[gi] = dg;
        }
        return out;
    }();
    return kTable;
}

}  // namespace cd::ui::chroma

// =============================================================================
// ImDrawList — Vector & SDF Canvas backed by cd::ui::renderer::DrawBatcher
// =============================================================================
class ImDrawList
{
public:
    enum class CmdKind : std::uint8_t
    {
        kSolidQuad,
        kRoundedRect,
        kDropShadow,
        kLinearGrad,
        kPolyQuad,
        kTriangle,
        kText,
        kPushScissor,
        kPopScissor
    };

    struct RecordedCmd
    {
        CmdKind                   kind { CmdKind::kSolidQuad };
        float                     x0 { 0.0F };
        float                     y0 { 0.0F };
        float                     x1 { 0.0F };
        float                     y1 { 0.0F };
        float                     x2 { 0.0F };
        float                     y2 { 0.0F };
        float                     x3 { 0.0F };
        float                     y3 { 0.0F };
        ImU32                     col0 { 0xFFFFFFFFU };
        ImU32                     col1 { 0xFFFFFFFFU };
        float                     param0 { 0.0F }; // radius / scale
        float                     param1 { 0.0F }; // border / spread
        bool                      flag { false };
        std::string               text {};
        cd::ui::renderer::ScissorRect scissor {};
    };

    void clear()
    {
        cmds_.clear();
        clip_stack_.clear();
    }

    [[nodiscard]] bool is_clipped_out(float x0, float y0, float x1, float y1) const noexcept
    {
        if (clip_stack_.empty()) return false;
        const ImVec4& c = clip_stack_.back();
        return (c.z <= c.x || c.w <= c.y || x1 <= c.x || x0 >= c.z || y1 <= c.y || y0 >= c.w);
    }

    void PushClipRect(ImVec2 mn, ImVec2 mx)
    {
        if (!clip_stack_.empty())
        {
            const ImVec4& p = clip_stack_.back();
            mn.x = std::max(mn.x, p.x);
            mn.y = std::max(mn.y, p.y);
            mx.x = std::min(mx.x, p.z);
            mx.y = std::min(mx.y, p.w);
        }
        clip_stack_.push_back(ImVec4(mn.x, mn.y, mx.x, mx.y));
        RecordedCmd rc {};
        rc.kind = CmdKind::kPushScissor;
        if (mx.x > mn.x && mx.y > mn.y)
        {
            rc.scissor.x      = static_cast<std::int32_t>(std::max(0.0F, mn.x));
            rc.scissor.y      = static_cast<std::int32_t>(std::max(0.0F, mn.y));
            rc.scissor.width  = static_cast<std::uint32_t>(std::max(1.0F, mx.x - mn.x));
            rc.scissor.height = static_cast<std::uint32_t>(std::max(1.0F, mx.y - mn.y));
        }
        else
        {
            rc.scissor = { 32000, 32000, 1U, 1U };
        }
        cmds_.push_back(std::move(rc));
    }

    void PopClipRect()
    {
        if (!clip_stack_.empty()) clip_stack_.pop_back();
        RecordedCmd rc {};
        rc.kind = CmdKind::kPopScissor;
        cmds_.push_back(std::move(rc));
    }

    void AddRectFilled(ImVec2 p_min, ImVec2 p_max, ImU32 col, float rounding = 0.0F)
    {
        const float w = p_max.x - p_min.x;
        const float h = p_max.y - p_min.y;
        if (w <= 0.0F || h <= 0.0F || is_clipped_out(p_min.x, p_min.y, p_max.x, p_max.y)) return;
        RecordedCmd rc {};
        if (rounding >= 1.5F)
        {
            rc.kind   = CmdKind::kRoundedRect;
            rc.param0 = rounding;
            rc.param1 = 0.0F;
        }
        else
        {
            rc.kind = CmdKind::kSolidQuad;
        }
        rc.x0   = p_min.x;
        rc.y0   = p_min.y;
        rc.x1   = w;
        rc.y1   = h;
        rc.col0 = col;
        cmds_.push_back(std::move(rc));
    }

    void AddRect(ImVec2 p_min, ImVec2 p_max, ImU32 col, float rounding = 0.0F,
                 int /*flags*/ = 0, float thickness = 1.0F)
    {
        const float w = p_max.x - p_min.x;
        const float h = p_max.y - p_min.y;
        if (w <= 0.0F || h <= 0.0F || is_clipped_out(p_min.x, p_min.y, p_max.x, p_max.y)) return;
        if (rounding >= 1.5F)
        {
            RecordedCmd rc {};
            rc.kind   = CmdKind::kRoundedRect;
            rc.x0     = p_min.x;
            rc.y0     = p_min.y;
            rc.x1     = w;
            rc.y1     = h;
            rc.col0   = col;
            rc.param0 = rounding;
            rc.param1 = std::max(1.0F, thickness);
            cmds_.push_back(std::move(rc));
        }
        else
        {
            const float t = std::max(1.0F, thickness);
            AddRectFilled(p_min, ImVec2(p_max.x, p_min.y + t), col);
            AddRectFilled(ImVec2(p_min.x, p_max.y - t), p_max, col);
            AddRectFilled(ImVec2(p_min.x, p_min.y + t), ImVec2(p_min.x + t, p_max.y - t), col);
            AddRectFilled(ImVec2(p_max.x - t, p_min.y + t), ImVec2(p_max.x, p_max.y - t), col);
        }
    }

    void AddShadow(ImVec2 p_min, ImVec2 p_max, float blur_radius, float spread, ImU32 col)
    {
        const float w = p_max.x - p_min.x;
        const float h = p_max.y - p_min.y;
        if (w <= 0.0F || h <= 0.0F || is_clipped_out(p_min.x, p_min.y, p_max.x, p_max.y)) return;
        RecordedCmd rc {};
        rc.kind   = CmdKind::kDropShadow;
        rc.x0     = p_min.x;
        rc.y0     = p_min.y;
        rc.x1     = w;
        rc.y1     = h;
        rc.col0   = col;
        rc.param0 = blur_radius;
        rc.param1 = spread;
        cmds_.push_back(std::move(rc));
    }

    void AddGradient(ImVec2 p_min, ImVec2 p_max, ImU32 c0, ImU32 c1, bool horizontal = false)
    {
        const float w = p_max.x - p_min.x;
        const float h = p_max.y - p_min.y;
        if (w <= 0.0F || h <= 0.0F || is_clipped_out(p_min.x, p_min.y, p_max.x, p_max.y)) return;
        RecordedCmd rc {};
        rc.kind = CmdKind::kLinearGrad;
        rc.x0   = p_min.x;
        rc.y0   = p_min.y;
        rc.x1   = w;
        rc.y1   = h;
        rc.col0 = c0;
        rc.col1 = c1;
        rc.flag = horizontal;
        cmds_.push_back(std::move(rc));
    }

    void AddLine(ImVec2 p1, ImVec2 p2, ImU32 col, float thickness = 1.0F)
    {
        const float dx = p2.x - p1.x;
        const float dy = p2.y - p1.y;
        const float len = std::sqrt(dx * dx + dy * dy);
        if (len < 0.001F) return;
        const float ht = std::max(0.6F, thickness * 0.5F);
        const float nx = (-dy / len) * ht;
        const float ny = ( dx / len) * ht;

        RecordedCmd rc {};
        rc.kind = CmdKind::kPolyQuad;
        rc.x0 = p1.x + nx; rc.y0 = p1.y + ny;
        rc.x1 = p2.x + nx; rc.y1 = p2.y + ny;
        rc.x2 = p2.x - nx; rc.y2 = p2.y - ny;
        rc.x3 = p1.x - nx; rc.y3 = p1.y - ny;
        rc.col0 = col;
        cmds_.push_back(std::move(rc));
    }

    void AddTriangleFilled(ImVec2 a, ImVec2 b, ImVec2 c, ImU32 col)
    {
        RecordedCmd rc {};
        rc.kind = CmdKind::kTriangle;
        rc.x0 = a.x; rc.y0 = a.y;
        rc.x1 = b.x; rc.y1 = b.y;
        rc.x2 = c.x; rc.y2 = c.y;
        rc.col0 = col;
        cmds_.push_back(std::move(rc));
    }

    void AddCircle(ImVec2 center, float radius, ImU32 col, int num_segments = 16, float thickness = 1.0F)
    {
        if (radius <= 0.5F) return;
        const int segs = std::clamp(num_segments, 8, 28);
        const float step = (2.0F * std::numbers::pi_v<float>) / static_cast<float>(segs);
        ImVec2 prev { center.x + radius, center.y };
        for (int i = 1; i <= segs; ++i)
        {
            const float a = static_cast<float>(i) * step;
            const ImVec2 cur { center.x + std::cos(a) * radius,
                               center.y + std::sin(a) * radius };
            AddLine(prev, cur, col, thickness);
            prev = cur;
        }
    }

    void AddCircleFilled(ImVec2 center, float radius, ImU32 col, int /*num_segments*/ = 16)
    {
        if (radius <= 0.5F) return;
        // Render as GPU SDF rounded box with corner_radius == radius (true anti-aliased circle!)
        RecordedCmd rc {};
        rc.kind   = CmdKind::kRoundedRect;
        rc.x0     = center.x - radius;
        rc.y0     = center.y - radius;
        rc.x1     = radius * 2.0F;
        rc.y1     = radius * 2.0F;
        rc.col0   = col;
        rc.param0 = radius;
        rc.param1 = 0.0F;
        cmds_.push_back(std::move(rc));
    }

    void AddBezierCubic(ImVec2 p1, ImVec2 p2, ImVec2 p3, ImVec2 p4, ImU32 col,
                        float thickness = 2.0F, int num_segments = 20)
    {
        const int segs = std::clamp(num_segments, 6, 48);
        ImVec2 prev = p1;
        for (int i = 1; i <= segs; ++i)
        {
            const float t  = static_cast<float>(i) / static_cast<float>(segs);
            const float u  = 1.0F - t;
            const float b0 = u * u * u;
            const float b1 = 3.0F * u * u * t;
            const float b2 = 3.0F * u * t * t;
            const float b3 = t * t * t;
            const ImVec2 cur {
                b0 * p1.x + b1 * p2.x + b2 * p3.x + b3 * p4.x,
                b0 * p1.y + b1 * p2.y + b2 * p3.y + b3 * p4.y
            };
            AddLine(prev, cur, col, thickness);
            prev = cur;
        }
    }

    void AddText(ImVec2 pos, ImU32 col, const char* text_begin, const char* text_end = nullptr,
                 float scale = 1.30F)
    {
        if (text_begin == nullptr || *text_begin == '\0') return;
        if (is_clipped_out(pos.x, pos.y, pos.x + 800.0F, pos.y + 20.0F)) return;
        RecordedCmd rc {};
        rc.kind   = CmdKind::kText;
        rc.x0     = std::floor(pos.x);
        rc.y0     = std::floor(pos.y);
        rc.col0   = col;
        rc.param0 = scale;
        if (text_end != nullptr)
            rc.text.assign(text_begin, static_cast<std::size_t>(text_end - text_begin));
        else
            rc.text.assign(text_begin);
        cmds_.push_back(std::move(rc));
    }

    void mark_last_cmd_right_aligned(float right_offset = 0.0F) noexcept
    {
        if (!cmds_.empty())
        {
            cmds_.back().flag   = true;
            cmds_.back().param1 = right_offset;
        }
    }

    [[nodiscard]] std::size_t cmd_count() const noexcept { return cmds_.size(); }
    [[nodiscard]] std::span<RecordedCmd> mutable_cmds() noexcept { return cmds_; }
    [[nodiscard]] std::span<const RecordedCmd> cmds() const noexcept { return cmds_; }

    void replay_into(cd::ui::renderer::DrawBatcher& batcher) const;

private:
    std::vector<RecordedCmd> cmds_ {};
    std::vector<ImVec4>      clip_stack_ {};
};

// =============================================================================
// ChromaUI Theme & Runtime State
// =============================================================================
namespace cd::ui::chroma
{

struct ThemePalette
{
    ImU32 window_bg       { IM_COL32(18, 22, 31, 246) };
    ImU32 window_header0  { IM_COL32(30, 38, 56, 255) };
    ImU32 window_header1  { IM_COL32(21, 26, 39, 255) };
    ImU32 window_border   { IM_COL32(48, 59, 82, 235) };
    ImU32 child_bg        { IM_COL32(13, 16, 24, 230) };
    ImU32 button          { IM_COL32(34, 43, 63, 255) };
    ImU32 button_hover    { IM_COL32(52, 94, 172, 255) };
    ImU32 button_active   { IM_COL32(34, 132, 234, 255) };
    ImU32 frame_bg        { IM_COL32(11, 14, 21, 255) };
    ImU32 frame_hover     { IM_COL32(26, 35, 52, 255) };
    ImU32 accent          { IM_COL32(45, 145, 255, 255) };
    ImU32 accent_bright   { IM_COL32(85, 200, 255, 255) };
    ImU32 text            { IM_COL32(238, 243, 252, 255) };
    ImU32 text_disabled   { IM_COL32(134, 148, 172, 255) };
    ImU32 separator       { IM_COL32(44, 55, 76, 225) };
    ImU32 popup_bg        { IM_COL32(21, 26, 38, 252) };
};

[[nodiscard]] inline const char* theme_preset_name(int preset) noexcept
{
    switch (preset % 5)
    {
        case 0:  return "Chroma Obsidian";
        case 1:  return "Cyber Neon Glass";
        case 2:  return "Daylight CAD";
        case 3:  return "Godot Slate";
        case 4:  return "Unreal Carbon";
        default: return "Chroma Obsidian";
    }
}

[[nodiscard]] inline ThemePalette make_theme(int preset) noexcept
{
    ThemePalette t {};
    const int p = ((preset % 5) + 5) % 5;
    if (p == 1) // Cyber Neon Glass
    {
        t.window_bg      = IM_COL32(11, 15, 26, 240);
        t.window_header0 = IM_COL32(22, 32, 58, 255);
        t.window_header1 = IM_COL32(14, 20, 38, 255);
        t.window_border  = IM_COL32(0, 210, 255, 155);
        t.button         = IM_COL32(24, 38, 68, 255);
        t.button_hover   = IM_COL32(0, 165, 215, 255);
        t.button_active  = IM_COL32(0, 220, 255, 255);
        t.accent         = IM_COL32(0, 225, 255, 255);
        t.accent_bright  = IM_COL32(130, 250, 255, 255);
    }
    else if (p == 2) // Daylight CAD
    {
        t.window_bg      = IM_COL32(240, 243, 248, 252);
        t.window_header0 = IM_COL32(222, 228, 238, 255);
        t.window_header1 = IM_COL32(210, 217, 230, 255);
        t.window_border  = IM_COL32(175, 186, 204, 255);
        t.child_bg       = IM_COL32(248, 250, 253, 255);
        t.button         = IM_COL32(216, 224, 238, 255);
        t.button_hover   = IM_COL32(180, 205, 245, 255);
        t.button_active  = IM_COL32(45, 125, 235, 255);
        t.frame_bg       = IM_COL32(255, 255, 255, 255);
        t.frame_hover    = IM_COL32(232, 240, 252, 255);
        t.accent         = IM_COL32(25, 110, 220, 255);
        t.accent_bright  = IM_COL32(55, 140, 245, 255);
        t.text           = IM_COL32(24, 30, 42, 255);
        t.text_disabled  = IM_COL32(105, 116, 136, 255);
        t.separator      = IM_COL32(190, 200, 216, 255);
        t.popup_bg       = IM_COL32(248, 250, 254, 255);
    }
    else if (p == 3) // Godot Slate
    {
        t.window_bg      = IM_COL32(33, 38, 46, 248);
        t.window_header0 = IM_COL32(44, 51, 63, 255);
        t.window_header1 = IM_COL32(36, 42, 52, 255);
        t.window_border  = IM_COL32(62, 72, 88, 240);
        t.child_bg        = IM_COL32(26, 30, 37, 235);
        t.button          = IM_COL32(49, 57, 70, 255);
        t.button_hover    = IM_COL32(71, 138, 209, 255);
        t.button_active   = IM_COL32(88, 165, 242, 255);
        t.frame_bg        = IM_COL32(24, 28, 34, 255);
        t.frame_hover     = IM_COL32(38, 45, 56, 255);
        t.accent          = IM_COL32(71, 140, 191, 255);
        t.accent_bright   = IM_COL32(112, 186, 250, 255);
        t.popup_bg        = IM_COL32(37, 43, 52, 254);
    }
    else if (p == 4) // Unreal Carbon
    {
        t.window_bg      = IM_COL32(20, 20, 22, 248);
        t.window_header0 = IM_COL32(32, 32, 36, 255);
        t.window_header1 = IM_COL32(24, 24, 27, 255);
        t.window_border  = IM_COL32(52, 52, 58, 240);
        t.child_bg       = IM_COL32(15, 15, 17, 235);
        t.button         = IM_COL32(40, 40, 45, 255);
        t.button_hover   = IM_COL32(22, 108, 215, 255);
        t.button_active  = IM_COL32(40, 138, 255, 255);
        t.frame_bg       = IM_COL32(12, 12, 14, 255);
        t.frame_hover    = IM_COL32(30, 30, 35, 255);
        t.accent         = IM_COL32(26, 125, 240, 255);
        t.accent_bright  = IM_COL32(85, 172, 255, 255);
        t.popup_bg       = IM_COL32(26, 26, 30, 254);
    }
    return t;
}

struct WindowLayout
{
    std::string name {};
    ImGuiID     id { 0U };
    ImVec2      pos { 0.0F, 0.0F };
    ImVec2      size { 320.0F, 240.0F };
    ImVec2      cursor { 0.0F, 0.0F };
    ImVec2      prev_item_min { 0.0F, 0.0F };
    ImVec2      prev_item_max { 0.0F, 0.0F };
    float       line_height { 20.0F };
    float       indent { 0.0F };
    float       scroll_y { 0.0F };
    float       content_max_x { 0.0F };
    float       content_max_y { 0.0F };
    float       bg_alpha { 1.0F };
    ImGuiWindowFlags flags { 0 };
    bool        is_popup { false };
    bool        is_child { false };
    bool        is_docked { false };
    bool        appearing { false };
    bool        skip { false };
    std::size_t popup_cmd_start { 0U };
    std::size_t child_popup_cmd_start { 0U };
    std::size_t child_popup_cmd_end { 0U };
    ImDrawList* draw_list { nullptr };
};

struct TableState
{
    int   cols { 1 };
    int   col_idx { 0 };
    float start_x { 0.0F };
    float width { 200.0F };
    float row_y { 0.0F };
    float row_max_y { 0.0F };
};

struct ToastMessage
{
    std::string title {};
    std::string body {};
    int         severity { 0 }; // 0=info, 1=success, 2=warning, 3=error
    float       remaining_sec { 3.5F };
    float       total_sec { 3.5F };
};

struct NodePinRecord
{
    ImGuiID pin_id { 0U };
    ImVec2  screen_pos { 0.0F, 0.0F };
    bool    is_output { false };
    ImU32   color { IM_COL32(80, 195, 255, 255) };
};

struct State
{
    ImGuiIO        io {};
    ImGuiViewport  main_viewport {};
    ThemePalette   theme { make_theme(0) };
    int            theme_index { 0 };
    int            workspace_mode { 0 }; // 0 = 3D Engine Studio, 1 = In-Game HUD & World UI, 2 = Standalone App & Node Studio
    float          anim_time { 0.0F };

    // TrueType GPU Font Atlas state + O(1) fast glyph/kerning cache (ASCII + Latin-1 + Extended-A)
    cd::ui::font::Font                       font {};
    bool                                     font_gpu_ready { false };
    std::array<cd::ui::font::GlyphInfo, 384> fast_glyphs {};
    std::array<bool, 384>                    fast_glyph_valid {};
    std::array<float, 128 * 128>             ascii_kern {};
    bool                                     fast_cache_built { false };

    // Input state
    bool mouse_down[3] { false, false, false };
    bool mouse_clicked[3] { false, false, false };
    bool mouse_released[3] { false, false, false };
    bool mouse_double_clicked { false };
    float last_click_time { -10.0F };
    ImVec2 last_click_pos { 0.0F, 0.0F };
    ImVec2 mouse_delta { 0.0F, 0.0F };
    ImVec2 prev_mouse_pos { 0.0F, 0.0F };
    bool   has_prev_mouse { false };
    bool   keys_down[128] {};
    bool   keys_pressed[128] {};
    bool   mod_ctrl { false };
    bool   mod_shift { false };
    bool   mod_alt { false };
    std::string input_chars {};

    // Dock ratios & dynamic window docking
    float dock_left_w_ratio { 0.22F };
    float dock_right_w_ratio { 0.25F };
    float dock_bottom_h_ratio { 0.21F };
    float dock_left_split_ratio { 0.70F };
    int   dragging_splitter { 0 }; // 1=left, 2=right, 3=bottom, 4=left_mid
    bool  dock_node_created { false };
    // Dock slot mapping: 1=LeftTop, 2=LeftBottom, 3=Right, 4=Bottom, 0=Floating
    std::unordered_map<ImGuiID, int> dock_node_slot {};
    std::unordered_map<std::string, int> window_dock_slot {};
    std::unordered_map<int, std::string> active_dock_tab {};
    std::unordered_map<int, std::vector<std::string>> dock_slot_tabs {};

    // Floating windows state
    std::unordered_map<ImGuiID, ImVec2> floating_pos_map {};
    std::unordered_map<ImGuiID, ImVec2> floating_size_map {};
    ImGuiID dragging_window_id { 0U };
    ImGuiID resizing_window_id { 0U };
    ImVec2  drag_window_offset { 0.0F, 0.0F };

    // Layers
    ImDrawList bg_dl {};
    ImDrawList win_dl {};
    ImDrawList popup_dl {};
    ImDrawList fg_dl {};

    // Stacks & persistent widget states
    std::vector<WindowLayout> window_stack {};
    std::unordered_map<ImGuiID, float> scroll_map {};
    std::unordered_map<ImGuiID, ImVec2> auto_size_map {};
    std::unordered_map<ImGuiID, bool> collapse_map {};
    std::unordered_map<ImGuiID, int> tab_map {};
    std::vector<ImGuiID> id_stack { 2166136261U };
    std::vector<std::pair<ImGuiCol, ImU32>> color_stack {};
    std::vector<std::pair<ImGuiStyleVar, ImVec2>> style_var_stack {};
    float  style_alpha { 1.0F };
    ImVec2 style_window_padding { 10.0F, 8.0F };
    float  style_window_rounding { 6.0F };
    float  style_frame_rounding { 4.0F };
    ImVec2 style_item_spacing { 5.0F, 4.0F };
    std::vector<float> item_width_stack {};
    float next_item_width { 0.0F };

    // Next window overrides
    bool   has_next_win_pos { false };
    ImVec2 next_win_pos { 0.0F, 0.0F };
    ImVec2 next_win_pivot { 0.0F, 0.0F };
    bool   has_next_win_size { false };
    ImVec2 next_win_size { 0.0F, 0.0F };
    bool   has_next_win_alpha { false };
    float  next_win_alpha { 1.0F };

    // Active / Hovered item tracking
    ImGuiID hovered_id { 0U };
    ImGuiID active_id { 0U };
    ImGuiID focused_input_id { 0U };
    std::size_t input_caret_pos { 0U };
    ImGuiID inline_edit_id { 0U };
    bool    inline_edit_select_all { false };
    char    inline_edit_buf[64] {};
    std::unordered_map<ImGuiID, float> slider_pre_click_val {};
    ImGuiID color_picker_popup_id { 0U };
    ImVec2  color_picker_pos { 0.0F, 0.0F };
    ImGuiID last_item_id { 0U };
    ImVec2  last_item_min { 0.0F, 0.0F };
    ImVec2  last_item_max { 0.0F, 0.0F };
    bool    last_item_hovered { false };
    bool    any_item_hovered { false };
    bool    mouse_in_any_window { false };
    bool    focus_next_input { false };

    // Table & Tab state
    std::vector<TableState> table_stack {};
    std::vector<ImGuiID>    tab_bar_stack {};
    std::unordered_map<ImGuiID, int> tab_bar_col_cursor {};
    std::unordered_map<ImGuiID, ImVec2> tab_bar_pos {};

    // Menus & Popups
    std::string active_menu_name {};
    std::string active_submenu_name {};
    float       menu_bar_cursor_x { 12.0F };
    float       current_menu_x { 12.0F };
    bool        in_menu_bar { false };
    bool        in_open_menu { false };
    bool        in_open_submenu { false };
    ImGuiID     open_popup_id { 0U };
    ImVec2      open_popup_pos { 0.0F, 0.0F };
    ImVec2      last_popup_min { 0.0F, 0.0F };
    ImVec2      last_popup_max { 0.0F, 0.0F };
    bool        popup_opened_this_frame { false };

    // NodeGraph & Toasts state
    ImVec2  node_graph_origin { 0.0F, 0.0F };
    ImVec2  node_graph_pan { 0.0F, 0.0F };
    ImGuiID current_node_id { 0U };
    ImVec2  current_node_pos { 0.0F, 0.0F };
    ImVec2  current_node_size { 160.0F, 90.0F };
    ImGuiID dragging_node_id { 0U };
    ImGuiID connecting_pin_id { 0U };
    ImVec2  connecting_pin_pos { 0.0F, 0.0F };
    std::unordered_map<ImGuiID, ImVec2> node_pos_map {};
    std::unordered_map<ImGuiID, NodePinRecord> node_pins {};
    std::vector<ToastMessage> toasts {};

    // Drag & Drop state
    bool         dnd_active { false };
    bool         dnd_in_source { false };
    bool         dnd_in_target { false };
    ImGuiPayload dnd_payload {};
    std::vector<std::uint8_t> dnd_buffer {};
    std::string  dnd_label {};
};

[[nodiscard]] inline State& state() noexcept
{
    static State s {};
    return s;
}

inline void rebuild_fast_font_cache(State& st) noexcept
{
    st.fast_glyph_valid.fill(false);
    st.ascii_kern.fill(0.0F);
    st.fast_cache_built = false;
    if (!st.font.is_loaded()) return;
    for (std::uint32_t cp = 32U; cp < 384U; ++cp)
    {
        if (const auto gi = st.font.glyph_uv(cp); gi.has_value())
        {
            st.fast_glyphs[cp]      = *gi;
            st.fast_glyph_valid[cp] = true;
        }
    }
    for (std::uint32_t a = 32U; a < 128U; ++a)
    {
        for (std::uint32_t b = 32U; b < 128U; ++b)
        {
            st.ascii_kern[(a << 7U) | b] = st.font.kerning(a, b);
        }
    }
    st.fast_cache_built = true;
}

/// Measure proportional pixel width & height of UTF-8 text using the active TrueType font
/// (or the fallback 5x7 stroke table when headless/no TTF loaded).
[[nodiscard]] inline ImVec2 measure_text_px(std::string_view sv, float scale = 1.28F) noexcept
{
    if (sv.empty()) return ImVec2 { 0.0F, 15.0F };
    const auto& st = state();
    if (st.font.is_loaded() && st.font_gpu_ready)
    {
        float fscale = std::clamp(scale / 1.28F, 0.75F, 1.65F);
        if (std::fabs(fscale - 1.0F) < 0.09F) fscale = 1.0F;
        float max_w = 0.0F;
        float cur_w = 0.0F;
        int   lines = 1;
        std::size_t idx = 0U;
        std::uint32_t prev_cp = 0U;
        while (idx < sv.size())
        {
            const std::uint32_t cp = next_utf8_codepoint(sv, idx);
            if (cp == '\n')
            {
                max_w = std::max(max_w, cur_w);
                cur_w = 0.0F;
                prev_cp = 0U;
                ++lines;
                continue;
            }
            if (cp < 32U) continue;
            if (prev_cp != 0U)
            {
                if (st.fast_cache_built && prev_cp < 128U && cp < 128U)
                {
                    cur_w += st.ascii_kern[(prev_cp << 7U) | cp] * fscale;
                }
                else
                {
                    cur_w += st.font.kerning(prev_cp, cp) * fscale;
                }
            }
            if (st.fast_cache_built && cp < 384U)
            {
                if (st.fast_glyph_valid[cp])
                {
                    cur_w += st.fast_glyphs[cp].advance * fscale;
                }
                else if (st.fast_glyph_valid[static_cast<std::size_t>('?')])
                {
                    cur_w += st.fast_glyphs[static_cast<std::size_t>('?')].advance * fscale;
                }
                else
                {
                    cur_w += 7.5F * fscale;
                }
            }
            else if (const auto gi = st.font.glyph_uv(cp); gi.has_value())
            {
                cur_w += gi->advance * fscale;
            }
            else if (const auto q = st.font.glyph_uv(static_cast<std::uint32_t>('?')); q.has_value())
            {
                cur_w += q->advance * fscale;
            }
            else
            {
                cur_w += 7.5F * fscale;
            }
            prev_cp = cp;
        }
        max_w = std::max(max_w, cur_w);
        return ImVec2 { std::ceil(max_w), static_cast<float>(lines) * 16.0F * fscale };
    }

    const float s = std::max(1.0F, scale);
    const float adv = std::round(5.5F * s);
    float max_w = 0.0F;
    float cur_w = 0.0F;
    int   lines = 1;
    for (char ch : sv)
    {
        if (ch == '\n')
        {
            max_w = std::max(max_w, cur_w);
            cur_w = 0.0F;
            ++lines;
        }
        else if (static_cast<unsigned char>(ch) >= 32U)
        {
            cur_w += adv;
        }
    }
    max_w = std::max(max_w, cur_w);
    return ImVec2 { max_w, static_cast<float>(lines) * std::round(9.0F * s) };
}

/// Draw crisp procedural vector icons onto `dl` centered at `c` with radius/half-size `r`.
inline void DrawIcon(ImDrawList* dl, IconKind kind, ImVec2 c, float r, ImU32 col)
{
    if (dl == nullptr || r <= 1.0F) return;
    switch (kind)
    {
        case IconKind::kPlay:
            dl->AddTriangleFilled(ImVec2(c.x - r * 0.55F, c.y - r * 0.75F),
                                  ImVec2(c.x - r * 0.55F, c.y + r * 0.75F),
                                  ImVec2(c.x + r * 0.75F, c.y), col);
            break;
        case IconKind::kStop:
            dl->AddRectFilled(ImVec2(c.x - r * 0.65F, c.y - r * 0.65F),
                              ImVec2(c.x + r * 0.65F, c.y + r * 0.65F), col, 1.5F);
            break;
        case IconKind::kPause:
            dl->AddRectFilled(ImVec2(c.x - r * 0.65F, c.y - r * 0.70F),
                              ImVec2(c.x - r * 0.18F, c.y + r * 0.70F), col, 1.0F);
            dl->AddRectFilled(ImVec2(c.x + r * 0.18F, c.y - r * 0.70F),
                              ImVec2(c.x + r * 0.65F, c.y + r * 0.70F), col, 1.0F);
            break;
        case IconKind::kStep:
            dl->AddTriangleFilled(ImVec2(c.x - r * 0.70F, c.y - r * 0.65F),
                                  ImVec2(c.x - r * 0.70F, c.y + r * 0.65F),
                                  ImVec2(c.x + r * 0.25F, c.y), col);
            dl->AddRectFilled(ImVec2(c.x + r * 0.35F, c.y - r * 0.65F),
                              ImVec2(c.x + r * 0.68F, c.y + r * 0.65F), col, 1.0F);
            break;
        case IconKind::kChevronRight:
            dl->AddTriangleFilled(ImVec2(c.x - r * 0.35F, c.y - r * 0.60F),
                                  ImVec2(c.x - r * 0.35F, c.y + r * 0.60F),
                                  ImVec2(c.x + r * 0.45F, c.y), col);
            break;
        case IconKind::kChevronDown:
            dl->AddTriangleFilled(ImVec2(c.x - r * 0.65F, c.y - r * 0.30F),
                                  ImVec2(c.x + r * 0.65F, c.y - r * 0.30F),
                                  ImVec2(c.x, c.y + r * 0.45F), col);
            break;
        case IconKind::kTranslate:
            dl->AddLine(ImVec2(c.x - r, c.y), ImVec2(c.x + r, c.y), col, 1.6F);
            dl->AddLine(ImVec2(c.x, c.y - r), ImVec2(c.x, c.y + r), col, 1.6F);
            break;
        case IconKind::kRotate:
            dl->AddCircle(c, r * 0.75F, col, 14, 1.6F);
            break;
        case IconKind::kScale:
            dl->AddRect(ImVec2(c.x - r * 0.70F, c.y - r * 0.70F),
                        ImVec2(c.x + r * 0.70F, c.y + r * 0.70F), col, 1.5F, 0, 1.5F);
            break;
        case IconKind::kSphere:
        case IconKind::kNode:
            dl->AddCircleFilled(c, r * 0.72F, col);
            break;
        case IconKind::kCube:
            dl->AddRectFilled(ImVec2(c.x - r * 0.68F, c.y - r * 0.68F),
                              ImVec2(c.x + r * 0.68F, c.y + r * 0.68F), col, 2.0F);
            break;
        case IconKind::kLight:
            dl->AddCircleFilled(c, r * 0.48F, col);
            for (int i = 0; i < 4; ++i)
            {
                const float a = static_cast<float>(i) * (std::numbers::pi_v<float> * 0.5F);
                dl->AddLine(ImVec2(c.x + std::cos(a) * r * 0.62F, c.y + std::sin(a) * r * 0.62F),
                            ImVec2(c.x + std::cos(a) * r * 0.95F, c.y + std::sin(a) * r * 0.95F), col, 1.5F);
            }
            break;
        default:
            dl->AddCircleFilled(c, r * 0.55F, col);
            break;
    }
}

}  // namespace cd::ui::chroma

inline void ImDrawList::replay_into(cd::ui::renderer::DrawBatcher& batcher) const
{
    int scissor_depth = 0;
    const auto& st    = cd::ui::chroma::state();
    const bool  use_ttf = st.font.is_loaded() && st.font_gpu_ready;
    const auto& table = cd::ui::chroma::glyph_stroke_table();
    for (const auto& cmd : cmds_)
    {
        switch (cmd.kind)
        {
            case CmdKind::kPushScissor:
                (void)batcher.push_scissor(cmd.scissor);
                ++scissor_depth;
                break;
            case CmdKind::kPopScissor:
                if (scissor_depth > 0)
                {
                    --scissor_depth;
                    batcher.pop_scissor();
                }
                break;
            case CmdKind::kSolidQuad:
                batcher.quad(cmd.x0, cmd.y0, cmd.x1, cmd.y1,
                             cd::ui::chroma::unpack_u32(cmd.col0));
                break;
            case CmdKind::kRoundedRect:
                batcher.rounded_rect(cmd.x0, cmd.y0, cmd.x1, cmd.y1,
                                     cmd.param0,
                                     cd::ui::chroma::unpack_u32(cmd.col0),
                                     cmd.param1);
                break;
            case CmdKind::kDropShadow:
                batcher.drop_shadow(cmd.x0, cmd.y0, cmd.x1, cmd.y1,
                                    cmd.param0, cmd.param1,
                                    cd::ui::chroma::unpack_u32(cmd.col0));
                break;
            case CmdKind::kLinearGrad:
                batcher.linear_gradient(cmd.x0, cmd.y0, cmd.x1, cmd.y1,
                                        cd::ui::chroma::unpack_u32(cmd.col0),
                                        cd::ui::chroma::unpack_u32(cmd.col1),
                                        cmd.flag);
                break;
            case CmdKind::kPolyQuad:
                batcher.quad_poly(cmd.x0, cmd.y0, cmd.x1, cmd.y1,
                                  cmd.x2, cmd.y2, cmd.x3, cmd.y3,
                                  cd::ui::chroma::unpack_u32(cmd.col0));
                break;
            case CmdKind::kTriangle:
                batcher.triangle(cmd.x0, cmd.y0, cmd.x1, cmd.y1,
                                 cmd.x2, cmd.y2,
                                 cd::ui::chroma::unpack_u32(cmd.col0));
                break;
            case CmdKind::kText:
            {
                const auto c = cd::ui::chroma::unpack_u32(cmd.col0);
                if (use_ttf)
                {
                    float fscale = std::clamp(cmd.param0 / 1.28F, 0.75F, 1.65F);
                    if (std::fabs(fscale - 1.0F) < 0.09F) fscale = 1.0F;
                    float cx = std::round(cmd.x0);
                    float cy = std::round(cmd.y0);
                    float baseline_y = std::round(cy + 10.0F * fscale);
                    std::size_t idx = 0U;
                    std::uint32_t prev_cp = 0U;
                    const std::string_view sv { cmd.text };
                    while (idx < sv.size())
                    {
                        const std::uint32_t cp = cd::ui::chroma::next_utf8_codepoint(sv, idx);
                        if (cp == '\n')
                        {
                            cx = std::round(cmd.x0);
                            cy += std::round(16.0F * fscale);
                            baseline_y = std::round(cy + 10.0F * fscale);
                            prev_cp = 0U;
                            continue;
                        }
                        if (cp < 32U) continue;
                        if (prev_cp != 0U)
                        {
                            if (st.fast_cache_built && prev_cp < 128U && cp < 128U)
                            {
                                cx += st.ascii_kern[(prev_cp << 7U) | cp] * fscale;
                            }
                            else
                            {
                                cx += st.font.kerning(prev_cp, cp) * fscale;
                            }
                        }
                        const cd::ui::font::GlyphInfo* gptr = nullptr;
                        std::optional<cd::ui::font::GlyphInfo> slow_gi;
                        if (st.fast_cache_built && cp < 384U)
                        {
                            if (st.fast_glyph_valid[cp])
                            {
                                gptr = &st.fast_glyphs[cp];
                            }
                            else if (st.fast_glyph_valid[static_cast<std::size_t>('?')])
                            {
                                gptr = &st.fast_glyphs[static_cast<std::size_t>('?')];
                            }
                        }
                        else
                        {
                            slow_gi = st.font.glyph_uv(cp);
                            if (!slow_gi.has_value())
                            {
                                slow_gi = st.font.glyph_uv(static_cast<std::uint32_t>('?'));
                            }
                            if (slow_gi.has_value())
                            {
                                gptr = &(*slow_gi);
                            }
                        }
                        if (gptr != nullptr)
                        {
                            if (gptr->width > 0.0F && gptr->height > 0.0F)
                            {
                                const float gx = std::round(cx + gptr->bearing_x * fscale);
                                const float gy = std::round(baseline_y - gptr->bearing_y * fscale);
                                const float gw = std::round(gptr->width * fscale);
                                const float gh = std::round(gptr->height * fscale);
                                batcher.glyph(gx, gy, gw, gh, 0U,
                                              cd::ui::renderer::AtlasUv { gptr->u0, gptr->v0, gptr->u1, gptr->v1 }, c);
                            }
                            cx += gptr->advance * fscale;
                        }
                        else
                        {
                            cx += 7.5F * fscale;
                        }
                        prev_cp = cp;
                    }
                    break;
                }

                // Fallback 5x7 stroke font for headless / NullDevice tests
                const float s = std::max(1.0F, cmd.param0);
                const float adv = std::round(5.5F * s);
                float cx = cmd.x0;
                float cy = cmd.y0;
                for (std::size_t i = 0; i < cmd.text.size(); ++i)
                {
                    const unsigned char ch = static_cast<unsigned char>(cmd.text[i]);
                    if (ch == '\n')
                    {
                        cx = cmd.x0;
                        cy += std::round(9.0F * s);
                        continue;
                    }
                    if (ch < 32U) continue;
                    const std::size_t idx = (ch < 128U) ? static_cast<std::size_t>(ch - 32U) : 31U;
                    const auto& dg = table[idx];
                    for (std::uint8_t ri = 0; ri < dg.count; ++ri)
                    {
                        const auto& r = dg.rects[ri];
                        batcher.quad(cx + static_cast<float>(r.x) * s,
                                     cy + static_cast<float>(r.y) * s,
                                     static_cast<float>(r.w) * s,
                                     static_cast<float>(r.h) * s,
                                     c);
                    }
                    cx += adv;
                }
                break;
            }
        }
    }
    while (scissor_depth > 0)
    {
        --scissor_depth;
        batcher.pop_scissor();
    }
}

namespace cd::ui::chroma
{

[[nodiscard]] inline ImGuiKey map_os_key(cd::platform::KeyCode k) noexcept
{
    using cd::platform::KeyCode;
    switch (k)
    {
        case KeyCode::k1:         return ImGuiKey_1;
        case KeyCode::k2:         return ImGuiKey_2;
        case KeyCode::k3:         return ImGuiKey_3;
        case KeyCode::k4:         return ImGuiKey_4;
        case KeyCode::kA:         return ImGuiKey_A;
        case KeyCode::kB:         return ImGuiKey_B;
        case KeyCode::kC:         return ImGuiKey_C;
        case KeyCode::kD:         return ImGuiKey_D;
        case KeyCode::kE:         return ImGuiKey_E;
        case KeyCode::kF:         return ImGuiKey_F;
        case KeyCode::kG:         return ImGuiKey_G;
        case KeyCode::kO:         return ImGuiKey_O;
        case KeyCode::kP:         return ImGuiKey_P;
        case KeyCode::kR:         return ImGuiKey_R;
        case KeyCode::kS:         return ImGuiKey_S;
        case KeyCode::kV:         return ImGuiKey_V;
        case KeyCode::kW:         return ImGuiKey_W;
        case KeyCode::kX:         return ImGuiKey_X;
        case KeyCode::kY:         return ImGuiKey_Y;
        case KeyCode::kZ:         return ImGuiKey_Z;
        case KeyCode::kF1:        return ImGuiKey_F1;
        case KeyCode::kF2:        return ImGuiKey_F2;
        case KeyCode::kF3:        return ImGuiKey_F3;
        case KeyCode::kF4:        return ImGuiKey_F4;
        case KeyCode::kF5:        return ImGuiKey_F5;
        case KeyCode::kF8:        return ImGuiKey_F8;
        case KeyCode::kF9:        return ImGuiKey_F9;
        case KeyCode::kEnter:     return ImGuiKey_Enter;
        case KeyCode::kUp:        return ImGuiKey_UpArrow;
        case KeyCode::kDown:      return ImGuiKey_DownArrow;
        case KeyCode::kLeft:      return ImGuiKey_LeftArrow;
        case KeyCode::kRight:     return ImGuiKey_RightArrow;
        case KeyCode::kDelete:    return ImGuiKey_Delete;
        case KeyCode::kBackspace: return ImGuiKey_Backspace;
        case KeyCode::kSpace:     return ImGuiKey_Space;
        case KeyCode::kEscape:    return ImGuiKey_Escape;
        case KeyCode::kTab:       return ImGuiKey_Tab;
        default:                  return ImGuiKey_None;
    }
}

// =============================================================================
// ChromaUi GPU/CPU Context (Replaces `cd::imgui::Context`)
// =============================================================================
struct InitDesc
{
    cd::platform::IWindow* window { nullptr };
    cd::rhi::IDevice*      device { nullptr };
    cd::rhi::Format        color_format { cd::rhi::Format::kBGRA8Unorm };
    std::uint32_t          frames_in_flight { 2U };
};

class Context
{
public:
    Context() noexcept = default;
    ~Context() { destroy(); }

    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;
    Context(Context&&) noexcept = default;
    Context& operator=(Context&&) noexcept = default;

    [[nodiscard]] static cd::core::Result<std::unique_ptr<Context>>
    create(const InitDesc& desc)
    {
        auto ctx = std::make_unique<Context>();
        ctx->window_ = desc.window;
        ctx->device_ = desc.device;
        if (desc.device != nullptr)
        {
            auto& st = state();
            if (!st.font.is_loaded())
            {
                static constexpr std::array<const char*, 5> kCandidateFonts {{
                    "C:/Windows/Fonts/segoeui.ttf",
                    "C:/Windows/Fonts/arial.ttf",
                    "C:/Windows/Fonts/tahoma.ttf",
                    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
                    "/System/Library/Fonts/Supplemental/Arial.ttf"
                }};
                for (const char* path : kCandidateFonts)
                {
                    std::ifstream fp { path, std::ios::binary | std::ios::ate };
                    if (fp.is_open())
                    {
                        const auto sz = fp.tellg();
                        if (sz > 512)
                        {
                            fp.seekg(0, std::ios::beg);
                            std::vector<std::uint8_t> bytes(static_cast<std::size_t>(sz));
                            if (fp.read(reinterpret_cast<char*>(bytes.data()),
                                        static_cast<std::streamsize>(bytes.size())))
                            {
                                (void)st.font.select_backend(cd::ui::font::Backend::kStb);
                                (void)st.font.select_atlas_mode(cd::ui::font::AtlasMode::kAlpha);
                                if (st.font.load_ttf_in_memory(bytes) &&
                                    st.font.rasterize_range(0x0020U, 0x017FU, 15.0F, 512U))
                                {
                                    rebuild_fast_font_cache(st);
                                    break;
                                }
                            }
                        }
                    }
                }
            }
            if (st.font.is_loaded() && !st.fast_cache_built)
            {
                rebuild_fast_font_cache(st);
            }

            // Upload TrueType font atlas to GPU RGBA8 texture when loaded on a real GPU backend
            if (st.font.is_loaded() && desc.device->backend() != cd::rhi::Backend::kNull)
            {
                const auto& bm = st.font.atlas();
                if (bm.width > 0U && bm.height > 0U && !bm.pixels.empty())
                {
                    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(bm.width) * bm.height * 4U, 255U);
                    for (std::size_t i = 0; i < static_cast<std::size_t>(bm.width) * bm.height; ++i)
                    {
                        rgba[i * 4U + 0U] = 255U;
                        rgba[i * 4U + 1U] = 255U;
                        rgba[i * 4U + 2U] = 255U;
                        rgba[i * 4U + 3U] = (i < bm.pixels.size()) ? bm.pixels[i] : 0U;
                    }
                    // Reserve 2x2 solid white texel at (0,0)
                    for (std::uint32_t wy = 0; wy < 2U && wy < bm.height; ++wy)
                    {
                        for (std::uint32_t wx = 0; wx < 2U && wx < bm.width; ++wx)
                        {
                            rgba[(static_cast<std::size_t>(wy) * bm.width + wx) * 4U + 3U] = 255U;
                        }
                    }

                    cd::rhi::TextureDesc td {};
                    td.type         = cd::rhi::TextureType::k2D;
                    td.format       = cd::rhi::Format::kRGBA8Unorm;
                    td.extent       = { bm.width, bm.height, 1U };
                    td.mip_levels   = 1U;
                    td.array_layers = 1U;
                    td.usage        = cd::rhi::TextureUsage::kSampled | cd::rhi::TextureUsage::kTransferDst;
                    td.memory       = cd::rhi::MemoryUsage::kGpuOnly;
                    td.debug_name   = "cd_chromaui_font_atlas";
                    auto img_r = desc.device->create_texture(td);
                    if (img_r.has_value())
                    {
                        ctx->atlas_tex_ = *img_r;
                        cd::rhi::BufferDesc sd {};
                        sd.size   = rgba.size();
                        sd.usage  = cd::rhi::BufferUsage::kTransferSrc;
                        sd.memory = cd::rhi::MemoryUsage::kCpuToGpu;
                        auto stg_r = desc.device->create_buffer(sd);
                        if (stg_r.has_value())
                        {
                            const auto staging = *stg_r;
                            (void)desc.device->upload_buffer(
                                staging, 0U,
                                std::span<const std::byte>(
                                    reinterpret_cast<const std::byte*>(rgba.data()), rgba.size()));
                            if (auto cmd = desc.device->create_command_buffer(cd::rhi::QueueType::kGraphics))
                            {
                                cmd->begin();
                                const std::array<cd::rhi::TextureBarrier, 1> tb_dst {
                                    cd::rhi::TextureBarrier {
                                        .texture = ctx->atlas_tex_,
                                        .from    = cd::rhi::ResourceState::kUndefined,
                                        .to      = cd::rhi::ResourceState::kTransferDst,
                                        .range   = { 0U, 1U, 0U, 1U } }
                                };
                                cmd->barrier({}, tb_dst);
                                const std::array<cd::rhi::BufferImageCopyRegion, 1> regs {
                                    cd::rhi::BufferImageCopyRegion {
                                        .buffer_offset = 0U,
                                        .mip_level     = 0U,
                                        .base_layer    = 0U,
                                        .layer_count   = 1U,
                                        .image_offset  = { 0, 0, 0 },
                                        .image_extent  = { bm.width, bm.height, 1U } }
                                };
                                cmd->copy_buffer_to_image(staging, ctx->atlas_tex_, regs);
                                const std::array<cd::rhi::TextureBarrier, 1> tb_read {
                                    cd::rhi::TextureBarrier {
                                        .texture = ctx->atlas_tex_,
                                        .from    = cd::rhi::ResourceState::kTransferDst,
                                        .to      = cd::rhi::ResourceState::kShaderResource,
                                        .range   = { 0U, 1U, 0U, 1U } }
                                };
                                cmd->barrier({}, tb_read);
                                cmd->end();

                                cd::rhi::SubmitDesc sub {};
                                std::array<cd::rhi::ICommandBuffer*, 1> cbs { cmd.get() };
                                sub.command_buffers = cbs;
                                (void)desc.device->submit(sub);
                                desc.device->wait_idle();
                            }
                            desc.device->destroy_buffer(staging);
                        }

                        cd::rhi::TextureViewDesc vd {};
                        vd.texture = ctx->atlas_tex_;
                        vd.type    = cd::rhi::TextureType::k2D;
                        vd.format  = cd::rhi::Format::kRGBA8Unorm;
                        auto view_r = desc.device->create_texture_view(vd);
                        if (view_r.has_value()) ctx->atlas_view_ = *view_r;

                        cd::rhi::SamplerDesc samp_d {};
                        samp_d.mag_filter = cd::rhi::SamplerFilter::kLinear;
                        samp_d.min_filter = cd::rhi::SamplerFilter::kLinear;
                        samp_d.address_u  = cd::rhi::SamplerAddressMode::kClampToEdge;
                        samp_d.address_v  = cd::rhi::SamplerAddressMode::kClampToEdge;
                        samp_d.address_w  = cd::rhi::SamplerAddressMode::kClampToEdge;
                        auto samp_r = desc.device->create_sampler(samp_d);
                        if (samp_r.has_value()) ctx->atlas_sampler_ = *samp_r;

                        st.font_gpu_ready = ctx->atlas_view_.is_valid() && ctx->atlas_sampler_.is_valid();
                    }
                }
            }

            cd::ui::renderer_rhi::SubmitterCreateInfo sci {};
            sci.color_format  = desc.color_format;
            sci.max_vertices  = cd::ui::renderer::kMaxVertices;
            sci.max_indices   = cd::ui::renderer::kMaxVertices * 6U;
            sci.atlas_view    = ctx->atlas_view_;
            sci.atlas_sampler = ctx->atlas_sampler_;
            auto sub_r = cd::ui::renderer_rhi::Submitter::create_with_inline_shader(*desc.device, sci);
            if (!sub_r.has_value())
            {
                return std::unexpected(sub_r.error());
            }
            ctx->submitter_ = std::move(*sub_r);
        }
        return ctx;
    }

    void destroy() noexcept
    {
        submitter_.destroy();
        if (device_ != nullptr)
        {
            if (atlas_sampler_.is_valid())
            {
                device_->destroy_sampler(atlas_sampler_);
                atlas_sampler_ = {};
            }
            if (atlas_view_.is_valid())
            {
                device_->destroy_texture_view(atlas_view_);
                atlas_view_ = {};
            }
            if (atlas_tex_.is_valid())
            {
                device_->destroy_texture(atlas_tex_);
                atlas_tex_ = {};
            }
            device_ = nullptr;
        }
        state().font_gpu_ready = false;
    }

    void handle_event(const cd::platform::OSEvent& ev) noexcept
    {
        auto& st = state();
        using cd::platform::OSEventKind;
        switch (ev.kind)
        {
            case OSEventKind::kMouseMove:
                st.io.MousePos = ImVec2 { ev.mouse_x, ev.mouse_y };
                break;
            case OSEventKind::kMouseButtonDown:
                st.io.MousePos = ImVec2 { ev.mouse_x, ev.mouse_y };
                if (static_cast<int>(ev.mouse_button) < 3)
                {
                    const int b = static_cast<int>(ev.mouse_button);
                    st.mouse_down[b]    = true;
                    st.mouse_clicked[b] = true;
                    if (b == 0)
                    {
                        const float dx = ev.mouse_x - st.last_click_pos.x;
                        const float dy = ev.mouse_y - st.last_click_pos.y;
                        if ((st.anim_time - st.last_click_time) < 0.36F && (dx * dx + dy * dy) < 36.0F)
                        {
                            st.mouse_double_clicked = true;
                        }
                        st.last_click_time = st.anim_time;
                        st.last_click_pos  = ImVec2 { ev.mouse_x, ev.mouse_y };
                    }
                }
                break;
            case OSEventKind::kMouseButtonUp:
                st.io.MousePos = ImVec2 { ev.mouse_x, ev.mouse_y };
                if (static_cast<int>(ev.mouse_button) < 3)
                {
                    const int b = static_cast<int>(ev.mouse_button);
                    st.mouse_down[b]     = false;
                    st.mouse_released[b] = true;
                }
                break;
            case OSEventKind::kMouseWheel:
                st.io.MouseWheel += ev.wheel;
                break;
            case OSEventKind::kKeyDown:
            {
                if (ev.key == cd::platform::KeyCode::kLCtrl || ev.key == cd::platform::KeyCode::kRCtrl)
                    st.mod_ctrl = true;
                if (ev.key == cd::platform::KeyCode::kLShift || ev.key == cd::platform::KeyCode::kRShift)
                    st.mod_shift = true;
                if (ev.key == cd::platform::KeyCode::kLAlt || ev.key == cd::platform::KeyCode::kRAlt)
                    st.mod_alt = true;
                const ImGuiKey ik = map_os_key(ev.key);
                if (ik > 0 && ik < 128)
                {
                    st.keys_down[ik]    = true;
                    st.keys_pressed[ik] = true;
                }
                break;
            }
            case OSEventKind::kKeyUp:
            {
                if (ev.key == cd::platform::KeyCode::kLCtrl || ev.key == cd::platform::KeyCode::kRCtrl)
                    st.mod_ctrl = false;
                if (ev.key == cd::platform::KeyCode::kLShift || ev.key == cd::platform::KeyCode::kRShift)
                    st.mod_shift = false;
                if (ev.key == cd::platform::KeyCode::kLAlt || ev.key == cd::platform::KeyCode::kRAlt)
                    st.mod_alt = false;
                const ImGuiKey ik = map_os_key(ev.key);
                if (ik > 0 && ik < 128)
                {
                    st.keys_down[ik] = false;
                }
                break;
            }
            case OSEventKind::kTextChar:
                if (ev.code_point >= 32U && ev.code_point < 127U)
                {
                    st.input_chars.push_back(static_cast<char>(ev.code_point));
                }
                break;
            default:
                break;
        }
        if (st.io.MousePos.y <= 28.0F || !st.active_menu_name.empty() || st.open_popup_id != 0U ||
            st.dragging_window_id != 0U || st.resizing_window_id != 0U || st.dragging_splitter != 0 ||
            (st.io.MousePos.x >= st.last_popup_min.x && st.io.MousePos.x <= st.last_popup_max.x &&
             st.io.MousePos.y >= st.last_popup_min.y && st.io.MousePos.y <= st.last_popup_max.y))
        {
            st.io.WantCaptureMouse = true;
        }
    }

    void new_frame() noexcept
    {
        auto& st = state();
        st.anim_time += st.io.DeltaTime;
        const float w = (window_ != nullptr && window_->width() > 0)
                            ? static_cast<float>(window_->width())
                            : 1600.0F;
        const float h = (window_ != nullptr && window_->height() > 0)
                            ? static_cast<float>(window_->height())
                            : 900.0F;
        st.io.DisplaySize      = ImVec2 { w, h };
        st.main_viewport.Pos   = ImVec2 { 0.0F, 0.0F };
        st.main_viewport.Size  = ImVec2 { w, h };
        st.main_viewport.WorkPos  = ImVec2 { 0.0F, 60.0F };
        st.main_viewport.WorkSize = ImVec2 { w, std::max(100.0F, h - 60.0F) };

        if (st.has_prev_mouse)
        {
            st.mouse_delta = ImVec2 { st.io.MousePos.x - st.prev_mouse_pos.x,
                                      st.io.MousePos.y - st.prev_mouse_pos.y };
        }
        else
        {
            st.mouse_delta = ImVec2 { 0.0F, 0.0F };
            st.has_prev_mouse = true;
        }
        st.prev_mouse_pos = st.io.MousePos;

        st.bg_dl.clear();
        st.win_dl.clear();
        st.popup_dl.clear();
        st.fg_dl.clear();

        st.window_stack.clear();
        st.table_stack.clear();
        st.tab_bar_stack.clear();
        st.id_stack.assign(1, 2166136261U);
        st.popup_opened_this_frame = false;

        if (st.keys_pressed[ImGuiKey_Escape])
        {
            st.active_menu_name.clear();
            st.active_submenu_name.clear();
            st.open_popup_id = 0U;
            st.color_picker_popup_id = 0U;
            st.inline_edit_id = 0U;
            st.focused_input_id = 0U;
        }
        if (st.active_menu_name.empty() && st.open_popup_id == 0U)
        {
            st.last_popup_min = ImVec2 { 0.0F, 0.0F };
            st.last_popup_max = ImVec2 { 0.0F, 0.0F };
        }

        st.hovered_id = 0U;
        st.any_item_hovered = false;
        st.mouse_in_any_window = false;
        if (!st.mouse_down[0])
        {
            st.active_id = 0U;
            st.dragging_splitter = 0;
            st.dragging_window_id = 0U;
            st.resizing_window_id = 0U;
            st.dragging_node_id = 0U;
            if (st.dnd_active && st.mouse_released[0])
            {
                // Keep dnd_active for this frame so AcceptDragDropPayload can consume it on mouse release!
            }
            else if (!st.mouse_released[0])
            {
                st.dnd_active = false;
            }
        }
    }

    void render(cd::rhi::ICommandBuffer& cmd) noexcept
    {
        auto& st = state();

        // Click-outside-to-close for open menus and popups
        if (st.mouse_clicked[0] && !st.popup_opened_this_frame)
        {
            const ImVec2 mp = st.io.MousePos;
            if (!st.active_menu_name.empty() && mp.y > 28.0F)
            {
                const bool in_pop = (mp.x >= st.last_popup_min.x && mp.x <= st.last_popup_max.x &&
                                     mp.y >= st.last_popup_min.y && mp.y <= st.last_popup_max.y);
                if (!in_pop)
                {
                    st.active_menu_name.clear();
                    st.active_submenu_name.clear();
                }
            }
            if (st.open_popup_id != 0U)
            {
                const bool in_pop = (mp.x >= st.last_popup_min.x && mp.x <= st.last_popup_max.x &&
                                     mp.y >= st.last_popup_min.y && mp.y <= st.last_popup_max.y);
                if (!in_pop)
                {
                    st.open_popup_id = 0U;
                    st.active_submenu_name.clear();
                }
            }
        }

        // Draw floating Drag-and-Drop ghost badge if dragging
        if (st.dnd_active)
        {
            const std::string badge = st.dnd_label.empty()
                                          ? std::string("[DRAGGING PAYLOAD]")
                                          : ("[DRAGGING] " + st.dnd_label);
            const ImVec2 bsz = measure_text_px(badge, 1.35F);
            const float bw = bsz.x + 22.0F;
            const float bh = 26.0F;
            const ImVec2 p0 { st.io.MousePos.x + 12.0F, st.io.MousePos.y + 12.0F };
            const ImVec2 p1 { p0.x + bw, p0.y + bh };
            st.fg_dl.AddShadow(p0, p1, 10.0F, 2.0F, IM_COL32(0, 0, 0, 180));
            st.fg_dl.AddRectFilled(p0, p1, st.theme.button_active, 6.0F);
            st.fg_dl.AddRect(p0, p1, st.theme.accent_bright, 6.0F, 0, 1.5F);
            st.fg_dl.AddText(ImVec2 { p0.x + 10.0F, p0.y + 6.0F }, st.theme.text, badge.c_str(), nullptr, 1.35F);
        }

        // Render animated Toast Notifications stack at top-right
        float toast_y = 66.0F;
        for (auto it = st.toasts.begin(); it != st.toasts.end();)
        {
            it->remaining_sec -= st.io.DeltaTime;
            if (it->remaining_sec <= 0.0F)
            {
                it = st.toasts.erase(it);
                continue;
            }
            const float fade = std::clamp(it->remaining_sec / 0.4F, 0.0F, 1.0F);
            const auto alpha = static_cast<std::uint8_t>(245.0F * fade);
            const ImVec2 tsz = measure_text_px(it->body, 1.28F);
            const float tw   = std::clamp(tsz.x + 36.0F, 230.0F, 380.0F);
            const float th   = 44.0F;
            const ImVec2 p0 { st.io.DisplaySize.x - tw - 14.0F, toast_y };
            const ImVec2 p1 { p0.x + tw, p0.y + th };
            const ImU32 bar_col = (it->severity == 1) ? IM_COL32(55, 215, 110, alpha)
                                : (it->severity == 2) ? IM_COL32(250, 185, 45, alpha)
                                : (it->severity == 3) ? IM_COL32(240, 65, 65, alpha)
                                                      : IM_COL32(55, 175, 255, alpha);
            st.fg_dl.AddShadow(p0, p1, 10.0F, 2.0F, IM_COL32(0, 0, 0, static_cast<int>(150.0F * fade)));
            st.fg_dl.AddRectFilled(p0, p1, IM_COL32(20, 26, 38, alpha), 6.0F);
            st.fg_dl.AddRectFilled(p0, ImVec2(p0.x + 4.5F, p1.y), bar_col, 3.0F);
            st.fg_dl.AddRect(p0, p1, bar_col, 6.0F, 0, 1.0F);
            st.fg_dl.PushClipRect(ImVec2(p0.x + 8.0F, p0.y + 2.0F), ImVec2(p1.x - 6.0F, p1.y - 2.0F));
            st.fg_dl.AddText(ImVec2(p0.x + 12.0F, p0.y + 6.0F), bar_col, it->title.c_str(), nullptr, 1.30F);
            st.fg_dl.AddText(ImVec2(p0.x + 12.0F, p0.y + 23.0F), IM_COL32(235, 240, 250, alpha), it->body.c_str(), nullptr, 1.25F);
            st.fg_dl.PopClipRect();
            toast_y += th + 8.0F;
            ++it;
        }

        // Replay all 4 layers into the GPU DrawBatcher in strict z-order
        batcher_.begin_frame();
        st.bg_dl.replay_into(batcher_);
        st.win_dl.replay_into(batcher_);
        st.popup_dl.replay_into(batcher_);
        st.fg_dl.replay_into(batcher_);

        if (submitter_.is_valid() && submitter_.upload(batcher_))
        {
            const cd::rhi::Extent2D ext {
                static_cast<std::uint32_t>(std::max(1.0F, st.io.DisplaySize.x)),
                static_cast<std::uint32_t>(std::max(1.0F, st.io.DisplaySize.y))
            };
            submitter_.record(cmd, ext);
        }

        // Update IO capture flags & clear edge triggers for next frame
        st.io.WantCaptureMouse    = st.mouse_in_any_window || st.any_item_hovered || !st.active_menu_name.empty() || st.open_popup_id != 0U;
        st.io.WantCaptureKeyboard = (st.focused_input_id != 0U || st.inline_edit_id != 0U);
        if (st.mouse_released[0])
        {
            st.dnd_active = false;
        }
        for (int b = 0; b < 3; ++b)
        {
            st.mouse_clicked[b]  = false;
            st.mouse_released[b] = false;
        }
        st.mouse_double_clicked = false;
        for (bool& kp : st.keys_pressed) kp = false;
        st.io.MouseWheel = 0.0F;
        st.input_chars.clear();
    }

private:
    cd::platform::IWindow*          window_ { nullptr };
    cd::rhi::IDevice*               device_ { nullptr };
    cd::rhi::TextureHandle          atlas_tex_ {};
    cd::rhi::TextureViewHandle      atlas_view_ {};
    cd::rhi::SamplerHandle          atlas_sampler_ {};
    cd::ui::renderer::DrawBatcher   batcher_ {};
    cd::ui::renderer_rhi::Submitter submitter_ {};
};

}  // namespace cd::ui::chroma

namespace cd::imgui
{
using InitDesc = cd::ui::chroma::InitDesc;
using Context  = cd::ui::chroma::Context;
}  // namespace cd::imgui

// =============================================================================
// Immediate-Mode UI Facade (`namespace ImGui` powered 100% by ChromaUI)
// =============================================================================
namespace ImGui
{

[[nodiscard]] inline ImGuiIO& GetIO() noexcept
{
    return cd::ui::chroma::state().io;
}

[[nodiscard]] inline const ImGuiViewport* GetMainViewport() noexcept
{
    return &cd::ui::chroma::state().main_viewport;
}

[[nodiscard]] inline ImDrawList* GetBackgroundDrawList() noexcept
{
    return &cd::ui::chroma::state().bg_dl;
}

[[nodiscard]] inline ImDrawList* GetForegroundDrawList() noexcept
{
    return &cd::ui::chroma::state().fg_dl;
}

[[nodiscard]] inline ImVec2 GetMousePos() noexcept
{
    return cd::ui::chroma::state().io.MousePos;
}

[[nodiscard]] inline ImU32 ColorConvertFloat4ToU32(const ImVec4& in) noexcept
{
    return cd::ui::chroma::pack_vec4(in);
}

[[nodiscard]] inline ImGuiID GetID(const char* str_id) noexcept
{
    auto& st = cd::ui::chroma::state();
    return cd::ui::chroma::hash_str(str_id != nullptr ? str_id : "", st.id_stack.back());
}

inline void PushID(int int_id) noexcept
{
    auto& st = cd::ui::chroma::state();
    const ImGuiID h = st.id_stack.back() ^ (static_cast<ImGuiID>(int_id) * 2654435761U + 17U);
    st.id_stack.push_back(h == 0U ? 1U : h);
}

inline void PushID(const char* str_id) noexcept
{
    auto& st = cd::ui::chroma::state();
    st.id_stack.push_back(GetID(str_id));
}

inline void PopID() noexcept
{
    auto& st = cd::ui::chroma::state();
    if (st.id_stack.size() > 1U) st.id_stack.pop_back();
}

inline void PushStyleVar(ImGuiStyleVar idx, float val) noexcept
{
    auto& st = cd::ui::chroma::state();
    if (idx == ImGuiStyleVar_Alpha)
    {
        st.style_var_stack.emplace_back(idx, ImVec2(st.style_alpha, 0.0F));
        st.style_alpha = std::clamp(val, 0.05F, 1.0F);
    }
    else if (idx == ImGuiStyleVar_WindowRounding)
    {
        st.style_var_stack.emplace_back(idx, ImVec2(st.style_window_rounding, 0.0F));
        st.style_window_rounding = std::max(0.0F, val);
    }
    else if (idx == ImGuiStyleVar_FrameRounding)
    {
        st.style_var_stack.emplace_back(idx, ImVec2(st.style_frame_rounding, 0.0F));
        st.style_frame_rounding = std::max(0.0F, val);
    }
    else
    {
        st.style_var_stack.emplace_back(idx, ImVec2(val, 0.0F));
    }
}

inline void PushStyleVar(ImGuiStyleVar idx, const ImVec2& val) noexcept
{
    auto& st = cd::ui::chroma::state();
    if (idx == ImGuiStyleVar_WindowPadding)
    {
        st.style_var_stack.emplace_back(idx, st.style_window_padding);
        st.style_window_padding = val;
    }
    else if (idx == ImGuiStyleVar_ItemSpacing)
    {
        st.style_var_stack.emplace_back(idx, st.style_item_spacing);
        st.style_item_spacing = val;
    }
    else
    {
        st.style_var_stack.emplace_back(idx, val);
    }
}

inline void PopStyleVar(int count = 1) noexcept
{
    auto& st = cd::ui::chroma::state();
    for (int i = 0; i < count && !st.style_var_stack.empty(); ++i)
    {
        const auto [idx, prev] = st.style_var_stack.back();
        st.style_var_stack.pop_back();
        if (idx == ImGuiStyleVar_Alpha) st.style_alpha = prev.x;
        else if (idx == ImGuiStyleVar_WindowRounding) st.style_window_rounding = prev.x;
        else if (idx == ImGuiStyleVar_FrameRounding) st.style_frame_rounding = prev.x;
        else if (idx == ImGuiStyleVar_WindowPadding) st.style_window_padding = prev;
        else if (idx == ImGuiStyleVar_ItemSpacing) st.style_item_spacing = prev;
    }
}

inline void PushStyleColor(ImGuiCol idx, const ImVec4& col) noexcept
{
    auto& st = cd::ui::chroma::state();
    const ImU32 packed = cd::ui::chroma::pack_vec4(col);
    switch (idx)
    {
        case ImGuiCol_Text:         st.color_stack.emplace_back(idx, st.theme.text);          st.theme.text          = packed; break;
        case ImGuiCol_TextDisabled: st.color_stack.emplace_back(idx, st.theme.text_disabled); st.theme.text_disabled = packed; break;
        case ImGuiCol_WindowBg:     st.color_stack.emplace_back(idx, st.theme.window_bg);     st.theme.window_bg     = packed; break;
        case ImGuiCol_ChildBg:      st.color_stack.emplace_back(idx, st.theme.child_bg);      st.theme.child_bg      = packed; break;
        case ImGuiCol_PopupBg:      st.color_stack.emplace_back(idx, st.theme.popup_bg);      st.theme.popup_bg      = packed; break;
        case ImGuiCol_Border:       st.color_stack.emplace_back(idx, st.theme.window_border); st.theme.window_border = packed; break;
        case ImGuiCol_FrameBg:      st.color_stack.emplace_back(idx, st.theme.frame_bg);      st.theme.frame_bg      = packed; break;
        case ImGuiCol_FrameBgHovered:
        case ImGuiCol_FrameBgActive:st.color_stack.emplace_back(idx, st.theme.frame_hover);   st.theme.frame_hover   = packed; break;
        case ImGuiCol_Button:       st.color_stack.emplace_back(idx, st.theme.button);        st.theme.button        = packed; break;
        case ImGuiCol_ButtonHovered:st.color_stack.emplace_back(idx, st.theme.button_hover);  st.theme.button_hover  = packed; break;
        case ImGuiCol_ButtonActive: st.color_stack.emplace_back(idx, st.theme.button_active); st.theme.button_active = packed; break;
        default:                    st.color_stack.emplace_back(idx, st.theme.accent);        st.theme.accent        = packed; break;
    }
}

inline void PopStyleColor(int count = 1) noexcept
{
    auto& st = cd::ui::chroma::state();
    for (int i = 0; i < count && !st.color_stack.empty(); ++i)
    {
        const auto [idx, prev] = st.color_stack.back();
        st.color_stack.pop_back();
        switch (idx)
        {
            case ImGuiCol_Text:         st.theme.text          = prev; break;
            case ImGuiCol_TextDisabled: st.theme.text_disabled = prev; break;
            case ImGuiCol_WindowBg:     st.theme.window_bg     = prev; break;
            case ImGuiCol_ChildBg:      st.theme.child_bg      = prev; break;
            case ImGuiCol_PopupBg:      st.theme.popup_bg      = prev; break;
            case ImGuiCol_Border:       st.theme.window_border = prev; break;
            case ImGuiCol_FrameBg:      st.theme.frame_bg      = prev; break;
            case ImGuiCol_FrameBgHovered:
            case ImGuiCol_FrameBgActive:st.theme.frame_hover   = prev; break;
            case ImGuiCol_Button:       st.theme.button        = prev; break;
            case ImGuiCol_ButtonHovered:st.theme.button_hover  = prev; break;
            case ImGuiCol_ButtonActive: st.theme.button_active = prev; break;
            default:                    st.theme.accent        = prev; break;
        }
    }
}

[[nodiscard]] inline ImVec2 CalcTextSize(const char* text, const char* text_end = nullptr,
                                         bool hide_text_after_double_hash = false) noexcept
{
    if (text == nullptr) return ImVec2(0.0F, 15.0F);
    std::string_view sv = (text_end != nullptr && text_end >= text)
                              ? std::string_view(text, static_cast<std::size_t>(text_end - text))
                              : std::string_view(text);
    if (hide_text_after_double_hash)
    {
        sv = cd::ui::chroma::visible_label(sv);
    }
    return cd::ui::chroma::measure_text_px(sv, 1.28F);
}

inline void SetNextWindowPos(const ImVec2& pos, ImGuiCond /*cond*/ = 0,
                             const ImVec2& pivot = ImVec2(0.0F, 0.0F)) noexcept
{
    auto& st = cd::ui::chroma::state();
    st.has_next_win_pos = true;
    st.next_win_pos     = pos;
    st.next_win_pivot   = pivot;
}

inline void SetNextWindowSize(const ImVec2& size, ImGuiCond /*cond*/ = 0) noexcept
{
    auto& st = cd::ui::chroma::state();
    st.has_next_win_size = true;
    st.next_win_size     = size;
}

inline void SetNextWindowViewport(ImGuiID /*viewport_id*/) noexcept {}

inline void SetNextWindowBgAlpha(float alpha) noexcept
{
    auto& st = cd::ui::chroma::state();
    st.has_next_win_alpha = true;
    st.next_win_alpha     = std::clamp(alpha, 0.0F, 1.0F);
}

inline void SetNextItemWidth(float item_width) noexcept
{
    cd::ui::chroma::state().next_item_width = item_width;
}

inline void PushItemWidth(float item_width) noexcept
{
    cd::ui::chroma::state().item_width_stack.push_back(item_width);
}

inline void PopItemWidth() noexcept
{
    auto& st = cd::ui::chroma::state();
    if (!st.item_width_stack.empty()) st.item_width_stack.pop_back();
}

inline void SetKeyboardFocusHere(int /*offset*/ = 0) noexcept
{
    cd::ui::chroma::state().focus_next_input = true;
}

[[nodiscard]] inline bool IsMouseDown(ImGuiMouseButton button) noexcept
{
    return (button >= 0 && button < 3) ? cd::ui::chroma::state().mouse_down[button] : false;
}

[[nodiscard]] inline bool IsMouseClicked(ImGuiMouseButton button) noexcept
{
    return (button >= 0 && button < 3) ? cd::ui::chroma::state().mouse_clicked[button] : false;
}

[[nodiscard]] inline bool IsMouseDoubleClicked(ImGuiMouseButton button = 0) noexcept
{
    return (button == 0) && cd::ui::chroma::state().mouse_double_clicked;
}

[[nodiscard]] inline bool IsMouseReleased(ImGuiMouseButton button) noexcept
{
    return (button >= 0 && button < 3) ? cd::ui::chroma::state().mouse_released[button] : false;
}

[[nodiscard]] inline bool IsKeyDown(ImGuiKey key) noexcept
{
    const int k = key & 0xFF;
    return (k > 0 && k < 128) ? cd::ui::chroma::state().keys_down[k] : false;
}

[[nodiscard]] inline bool IsKeyPressed(ImGuiKey key, bool /*repeat*/ = true) noexcept
{
    const int k = key & 0xFF;
    return (k > 0 && k < 128) ? cd::ui::chroma::state().keys_pressed[k] : false;
}

[[nodiscard]] inline bool IsKeyChordPressed(int key_chord) noexcept
{
    auto& st = cd::ui::chroma::state();
    if ((key_chord & ImGuiMod_Ctrl) != 0 && !st.mod_ctrl) return false;
    if ((key_chord & ImGuiMod_Shift) != 0 && !st.mod_shift) return false;
    if ((key_chord & ImGuiMod_Alt) != 0 && !st.mod_alt) return false;
    const int k = key_chord & 0xFF;
    return (k > 0 && k < 128) ? st.keys_pressed[k] : false;
}

[[nodiscard]] inline bool IsItemHovered() noexcept
{
    return cd::ui::chroma::state().last_item_hovered;
}

[[nodiscard]] inline bool IsAnyItemHovered() noexcept
{
    return cd::ui::chroma::state().any_item_hovered;
}

[[nodiscard]] inline bool IsWindowAppearing() noexcept
{
    auto& st = cd::ui::chroma::state();
    return !st.window_stack.empty() && st.window_stack.back().appearing;
}

[[nodiscard]] inline float GetWindowWidth() noexcept
{
    auto& st = cd::ui::chroma::state();
    return st.window_stack.empty() ? 320.0F : st.window_stack.back().size.x;
}

[[nodiscard]] inline float GetWindowHeight() noexcept
{
    auto& st = cd::ui::chroma::state();
    return st.window_stack.empty() ? 240.0F : st.window_stack.back().size.y;
}

[[nodiscard]] inline ImVec2 GetCursorScreenPos() noexcept
{
    auto& st = cd::ui::chroma::state();
    return st.window_stack.empty() ? ImVec2(0.0F, 0.0F) : st.window_stack.back().cursor;
}

inline void SetCursorScreenPos(const ImVec2& screen_pos) noexcept
{
    auto& st = cd::ui::chroma::state();
    if (!st.window_stack.empty()) {
        st.window_stack.back().cursor = screen_pos;
    }
}

// -----------------------------------------------------------------------------
// DockBuilder & DockSpace (with interactive draggable splitters!)
// -----------------------------------------------------------------------------
[[nodiscard]] inline void* DockBuilderGetNode(ImGuiID /*node_id*/) noexcept
{
    return cd::ui::chroma::state().dock_node_created ? reinterpret_cast<void*>(1) : nullptr;
}

inline void DockBuilderRemoveNode(ImGuiID /*node_id*/) noexcept
{
    auto& st = cd::ui::chroma::state();
    st.dock_node_created = false;
    st.dock_node_slot.clear();
    st.window_dock_slot.clear();
    st.active_dock_tab.clear();
    st.dock_slot_tabs.clear();
}

inline ImGuiID DockBuilderAddNode(ImGuiID node_id = 0, ImGuiDockNodeFlags /*flags*/ = 0) noexcept
{
    auto& st = cd::ui::chroma::state();
    st.dock_node_created = true;
    st.dock_node_slot[node_id] = 0;
    return node_id;
}

inline void DockBuilderSetNodeSize(ImGuiID /*node_id*/, ImVec2 /*size*/) noexcept {}

inline ImGuiID DockBuilderSplitNode(ImGuiID node_id, ImGuiDir dir, float size_ratio_for_node_at_dir,
                                    ImGuiID* out_id_at_dir, ImGuiID* out_id_at_opposite_dir) noexcept
{
    auto& st = cd::ui::chroma::state();
    const ImGuiID split_id = node_id * 31U + static_cast<ImGuiID>(dir + 2);
    const int parent_slot = st.dock_node_slot.contains(node_id) ? st.dock_node_slot[node_id] : 0;
    int split_slot = 1;
    if (parent_slot == 1 && dir == ImGuiDir_Down)
    {
        split_slot = 2;
        if (size_ratio_for_node_at_dir > 0.0F)
            st.dock_left_split_ratio = std::clamp(1.0F - size_ratio_for_node_at_dir, 0.25F, 0.80F);
    }
    else if (dir == ImGuiDir_Left)
    {
        split_slot = 1;
        if (size_ratio_for_node_at_dir > 0.0F)
            st.dock_left_w_ratio = std::clamp(size_ratio_for_node_at_dir, 0.15F, 0.36F);
    }
    else if (dir == ImGuiDir_Right)
    {
        split_slot = 3;
        if (size_ratio_for_node_at_dir > 0.0F)
            st.dock_right_w_ratio = std::clamp(size_ratio_for_node_at_dir, 0.17F, 0.38F);
    }
    else if (dir == ImGuiDir_Down)
    {
        split_slot = 4;
        if (size_ratio_for_node_at_dir > 0.0F)
            st.dock_bottom_h_ratio = std::clamp(size_ratio_for_node_at_dir, 0.12F, 0.42F);
    }
    st.dock_node_slot[split_id] = split_slot;
    if (out_id_at_dir != nullptr) *out_id_at_dir = split_id;
    if (out_id_at_opposite_dir != nullptr) *out_id_at_opposite_dir = node_id;
    return split_id;
}

inline void DockBuilderDockWindow(const char* window_name, ImGuiID node_id) noexcept
{
    if (window_name == nullptr) return;
    auto& st = cd::ui::chroma::state();
    const std::string wn(window_name);
    int slot = 0;
    const auto nit = st.dock_node_slot.find(node_id);
    if (nit != st.dock_node_slot.end() && nit->second > 0)
    {
        slot = nit->second;
    }
    else if (wn == "Studio Workbench") slot = 1;
    else if (wn == "Scene")            slot = 2;
    else if (wn == "Inspector")        slot = 3;
    else if (wn == "History")          slot = 4;
    else if (node_id != 0U)            slot = 1;

    if (slot > 0)
    {
        st.window_dock_slot[wn] = slot;
        auto& tabs = st.dock_slot_tabs[slot];
        if (std::find(tabs.begin(), tabs.end(), wn) == tabs.end())
        {
            tabs.push_back(wn);
        }
        if (st.active_dock_tab[slot].empty())
        {
            st.active_dock_tab[slot] = tabs.front();
        }
    }
}

inline void DockBuilderFinish(ImGuiID /*root_id*/) noexcept {}

inline ImGuiID DockSpace(ImGuiID id, const ImVec2& /*size*/ = ImVec2(0, 0),
                         ImGuiDockNodeFlags /*flags*/ = 0) noexcept
{
    auto& st = cd::ui::chroma::state();
    const float W = st.io.DisplaySize.x;
    const float H = st.io.DisplaySize.y;
    const float top_y = 60.0F;
    const float work_h = std::max(100.0F, H - top_y);

    const float lx = std::round(W * st.dock_left_w_ratio);
    const float rx = std::round(W * (1.0F - st.dock_right_w_ratio));
    const float by = std::round(top_y + work_h * (1.0F - st.dock_bottom_h_ratio));
    const float lmy = std::round(top_y + work_h * st.dock_left_split_ratio);

    const ImVec2 mp = st.io.MousePos;
    const bool popup_open = (!st.active_menu_name.empty() || st.open_popup_id != 0U) &&
                            (mp.x >= st.last_popup_min.x && mp.x <= st.last_popup_max.x &&
                             mp.y >= st.last_popup_min.y && mp.y <= st.last_popup_max.y);
    const bool hover_l  = !popup_open && (mp.y >= top_y && std::fabs(mp.x - lx) <= 4.0F);
    const bool hover_r  = !popup_open && (mp.y >= top_y && std::fabs(mp.x - rx) <= 4.0F);
    const bool hover_b  = !popup_open && (mp.x >= lx && mp.x <= rx && std::fabs(mp.y - by) <= 4.0F);
    const bool hover_lm = !popup_open && (mp.x <= lx && std::fabs(mp.y - lmy) <= 4.0F);

    if (st.mouse_clicked[0] && !popup_open)
    {
        if (hover_l) st.dragging_splitter = 1;
        else if (hover_r) st.dragging_splitter = 2;
        else if (hover_b) st.dragging_splitter = 3;
        else if (hover_lm) st.dragging_splitter = 4;
    }

    if (st.mouse_down[0] && st.dragging_splitter != 0)
    {
        if (st.dragging_splitter == 1)
            st.dock_left_w_ratio = std::clamp(mp.x / W, 0.15F, 0.36F);
        else if (st.dragging_splitter == 2)
            st.dock_right_w_ratio = std::clamp((W - mp.x) / W, 0.17F, 0.38F);
        else if (st.dragging_splitter == 3)
            st.dock_bottom_h_ratio = std::clamp((H - mp.y) / work_h, 0.12F, 0.42F);
        else if (st.dragging_splitter == 4)
            st.dock_left_split_ratio = std::clamp((mp.y - top_y) / work_h, 0.25F, 0.80F);
    }

    const ImU32 grip_col = st.theme.window_border;
    const ImU32 grip_hi  = st.theme.accent_bright;
    st.win_dl.AddRectFilled(ImVec2(lx - 1.5F, top_y), ImVec2(lx + 1.5F, H),
                            (hover_l || st.dragging_splitter == 1) ? grip_hi : grip_col);
    st.win_dl.AddRectFilled(ImVec2(rx - 1.5F, top_y), ImVec2(rx + 1.5F, H),
                            (hover_r || st.dragging_splitter == 2) ? grip_hi : grip_col);
    st.win_dl.AddRectFilled(ImVec2(lx, by - 1.5F), ImVec2(rx, by + 1.5F),
                            (hover_b || st.dragging_splitter == 3) ? grip_hi : grip_col);
    st.win_dl.AddRectFilled(ImVec2(0.0F, lmy - 1.5F), ImVec2(lx, lmy + 1.5F),
                            (hover_lm || st.dragging_splitter == 4) ? grip_hi : grip_col);
    if (st.workspace_mode == 0)
    {
        st.bg_dl.clear();
        st.bg_dl.PushClipRect(ImVec2(lx + 2.0F, top_y + 2.0F), ImVec2(rx - 2.0F, by - 2.0F));
    }
    return id;
}

// -----------------------------------------------------------------------------
// Window & Child Management
// -----------------------------------------------------------------------------
namespace detail
{
[[nodiscard]] inline bool is_occluded_by_popup(const cd::ui::chroma::WindowLayout& w) noexcept;
}  // namespace detail

inline bool Begin(const char* name, bool* p_open = nullptr, ImGuiWindowFlags flags = 0)
{
    auto& st = cd::ui::chroma::state();
    cd::ui::chroma::WindowLayout wl {};
    wl.name      = name != nullptr ? name : "Window";
    wl.flags     = flags;
    wl.draw_list = &st.win_dl;
    wl.bg_alpha  = st.has_next_win_alpha ? st.next_win_alpha : st.style_alpha;

    const ImGuiID win_id = cd::ui::chroma::hash_str(wl.name);
    wl.id        = win_id;
    wl.appearing = (st.auto_size_map.find(win_id) == st.auto_size_map.end());

    const float W = st.io.DisplaySize.x;
    const float H = st.io.DisplaySize.y;
    const float top_y = 60.0F;
    const float work_h = std::max(100.0F, H - top_y);
    const float lx = std::round(W * st.dock_left_w_ratio);
    const float rx = std::round(W * (1.0F - st.dock_right_w_ratio));
    const float by = std::round(top_y + work_h * (1.0F - st.dock_bottom_h_ratio));
    const float lmy = std::round(top_y + work_h * st.dock_left_split_ratio);

    int slot = 0;
    auto slot_it = st.window_dock_slot.find(wl.name);
    if (slot_it != st.window_dock_slot.end())
    {
        slot = slot_it->second;
    }
    else
    {
        if (wl.name == "Studio Workbench") slot = 1;
        else if (wl.name == "Scene")       slot = 2;
        else if (wl.name == "Inspector")   slot = 3;
        else if (wl.name == "History")     slot = 4;
        if (slot != 0) st.window_dock_slot[wl.name] = slot;
    }

    bool is_docked = (slot >= 1 && slot <= 4);
    wl.is_docked = is_docked;
    if (is_docked)
    {
        auto& tabs = st.dock_slot_tabs[slot];
        if (std::find(tabs.begin(), tabs.end(), wl.name) == tabs.end())
        {
            tabs.push_back(wl.name);
        }
        if (st.active_dock_tab[slot].empty())
        {
            st.active_dock_tab[slot] = tabs.front();
        }
    }

    if (slot == 1)
    {
        wl.pos  = ImVec2 { 0.0F, top_y };
        wl.size = ImVec2 { lx - 1.0F, lmy - top_y - 1.0F };
    }
    else if (slot == 2)
    {
        wl.pos  = ImVec2 { 0.0F, lmy + 1.0F };
        wl.size = ImVec2 { lx - 1.0F, H - lmy - 1.0F };
    }
    else if (slot == 3)
    {
        wl.pos  = ImVec2 { rx + 1.0F, top_y };
        wl.size = ImVec2 { W - rx - 1.0F, H - top_y };
    }
    else if (slot == 4)
    {
        wl.pos  = ImVec2 { lx + 1.0F, by + 1.0F };
        wl.size = ImVec2 { rx - lx - 2.0F, H - by - 1.0F };
    }
    else
    {
        auto sz_it = st.floating_size_map.find(win_id);
        if (sz_it != st.floating_size_map.end())
        {
            wl.size = sz_it->second;
        }
        else if (st.has_next_win_size)
        {
            wl.size = st.next_win_size;
        }
        else if ((flags & ImGuiWindowFlags_AlwaysAutoResize) != 0)
        {
            auto it = st.auto_size_map.find(win_id);
            wl.size = (it != st.auto_size_map.end()) ? it->second : ImVec2 { 490.0F, 88.0F };
        }

        auto pos_it = st.floating_pos_map.find(win_id);
        if (pos_it != st.floating_pos_map.end() && (flags & ImGuiWindowFlags_NoMove) == 0)
        {
            wl.pos = pos_it->second;
        }
        else if (st.has_next_win_pos)
        {
            wl.pos = ImVec2 {
                st.next_win_pos.x - wl.size.x * st.next_win_pivot.x,
                st.next_win_pos.y - wl.size.y * st.next_win_pivot.y
            };
        }
    }

    st.has_next_win_pos   = false;
    st.next_win_pivot     = ImVec2 { 0.0F, 0.0F };
    st.has_next_win_size  = false;
    st.has_next_win_alpha = false;

    if (is_docked && st.dock_slot_tabs[slot].size() > 1U && st.active_dock_tab[slot] != wl.name)
    {
        wl.skip   = true;
        wl.pos    = ImVec2 { -10000.0F, -10000.0F };
        wl.cursor = wl.pos;
        st.window_stack.push_back(std::move(wl));
        return false;
    }

    if ((flags & ImGuiWindowFlags_NoBackground) != 0)
    {
        wl.skip = true;
        wl.cursor = ImVec2 { wl.pos.x + 8.0F, wl.pos.y + 8.0F };
        st.window_stack.push_back(std::move(wl));
        return true;
    }

    const ImVec2 mp = st.io.MousePos;
    const bool popup_occluded = detail::is_occluded_by_popup(wl);
    if (!is_docked && (flags & ImGuiWindowFlags_NoTitleBar) == 0 && (flags & ImGuiWindowFlags_NoMove) == 0)
    {
        const bool in_title = !popup_occluded &&
                              (mp.x >= wl.pos.x && mp.x <= wl.pos.x + wl.size.x - 44.0F &&
                               mp.y >= wl.pos.y && mp.y <= wl.pos.y + 26.0F);
        if (in_title && st.mouse_clicked[0])
        {
            st.dragging_window_id = win_id;
            st.drag_window_offset = ImVec2(mp.x - wl.pos.x, mp.y - wl.pos.y);
        }
        if (st.dragging_window_id == win_id && st.mouse_down[0])
        {
            wl.pos = ImVec2(std::clamp(mp.x - st.drag_window_offset.x, 0.0F, std::max(0.0F, W - 80.0F)),
                            std::clamp(mp.y - st.drag_window_offset.y, 28.0F, std::max(28.0F, H - 40.0F)));
            st.floating_pos_map[win_id] = wl.pos;
        }
    }
    if (!is_docked && (flags & ImGuiWindowFlags_NoResize) == 0 && (flags & ImGuiWindowFlags_AlwaysAutoResize) == 0)
    {
        const bool in_corner = !popup_occluded &&
                               (mp.x >= wl.pos.x + wl.size.x - 14.0F && mp.x <= wl.pos.x + wl.size.x &&
                                mp.y >= wl.pos.y + wl.size.y - 14.0F && mp.y <= wl.pos.y + wl.size.y);
        if (in_corner && st.mouse_clicked[0])
        {
            st.resizing_window_id = win_id;
        }
        if (st.resizing_window_id == win_id && st.mouse_down[0])
        {
            wl.size.x = std::clamp(mp.x - wl.pos.x, 180.0F, W - wl.pos.x);
            wl.size.y = std::clamp(mp.y - wl.pos.y, 90.0F, H - wl.pos.y);
            st.floating_size_map[win_id] = wl.size;
        }
    }

    const bool hovered = (mp.x >= wl.pos.x && mp.x <= wl.pos.x + wl.size.x &&
                          mp.y >= wl.pos.y && mp.y <= wl.pos.y + wl.size.y);
    if (hovered)
    {
        st.mouse_in_any_window = true;
    }
    wl.scroll_y = st.scroll_map[win_id];

    auto bg_c = cd::ui::chroma::unpack_u32(st.theme.window_bg);
    bg_c.a = static_cast<std::uint8_t>(static_cast<float>(bg_c.a) * wl.bg_alpha);
    const ImU32 bg_u32 = IM_COL32(bg_c.r, bg_c.g, bg_c.b, bg_c.a);
    const float w_round = is_docked ? 0.0F : st.style_window_rounding;

    if (!is_docked)
    {
        wl.draw_list->AddShadow(wl.pos, ImVec2(wl.pos.x + wl.size.x, wl.pos.y + wl.size.y),
                                12.0F, 2.0F, IM_COL32(0, 0, 0, 155));
    }
    wl.draw_list->AddRectFilled(wl.pos, ImVec2(wl.pos.x + wl.size.x, wl.pos.y + wl.size.y),
                                bg_u32, w_round);
    wl.draw_list->AddRect(wl.pos, ImVec2(wl.pos.x + wl.size.x, wl.pos.y + wl.size.y),
                          st.theme.window_border, w_round, 0, 1.0F);

    float content_top = wl.pos.y + st.style_window_padding.y;
    if ((flags & ImGuiWindowFlags_NoTitleBar) == 0)
    {
        const float title_h = 26.0F;
        wl.draw_list->AddGradient(wl.pos, ImVec2(wl.pos.x + wl.size.x, wl.pos.y + title_h),
                                  st.theme.window_header0, st.theme.window_header1, false);
        if (is_docked && st.dock_slot_tabs[slot].size() > 1U)
        {
            float tx_cursor = wl.pos.x;
            for (const auto& tname : st.dock_slot_tabs[slot])
            {
                const auto tvis = cd::ui::chroma::visible_label(tname);
                const ImVec2 tsz = cd::ui::chroma::measure_text_px(tvis, 1.28F);
                const float tw = std::clamp(tsz.x + 20.0F, 72.0F, 180.0F);
                const ImVec2 t0 { tx_cursor, wl.pos.y };
                const ImVec2 t1 { tx_cursor + tw, wl.pos.y + title_h };
                const bool thov = !popup_occluded &&
                                  (mp.x >= t0.x && mp.x <= t1.x && mp.y >= t0.y && mp.y <= t1.y);
                const bool tact = (st.active_dock_tab[slot] == tname);
                if (thov && st.mouse_clicked[0])
                {
                    st.active_dock_tab[slot] = tname;
                }
                if (tact || thov)
                {
                    wl.draw_list->AddRectFilled(t0, t1, tact ? st.theme.button_hover : st.theme.button, 0.0F);
                }
                if (tact)
                {
                    wl.draw_list->AddRectFilled(ImVec2(t0.x, t1.y - 2.5F), t1, st.theme.accent_bright);
                }
                wl.draw_list->AddText(ImVec2(t0.x + 8.0F, t0.y + 6.0F),
                                      tact ? st.theme.text : st.theme.text_disabled,
                                      tvis.data(), tvis.data() + tvis.size(), 1.28F);
                tx_cursor += tw + 2.0F;
            }
            wl.draw_list->AddRectFilled(ImVec2(tx_cursor, wl.pos.y + title_h - 1.0F),
                                        ImVec2(wl.pos.x + wl.size.x, wl.pos.y + title_h),
                                        st.theme.window_border);
        }
        else
        {
            const auto vis = cd::ui::chroma::visible_label(wl.name);
            const ImVec2 tsz = cd::ui::chroma::measure_text_px(vis, 1.32F);
            const float tab_w = std::clamp(tsz.x + 24.0F, 90.0F, wl.size.x - 40.0F);
            wl.draw_list->AddRectFilled(ImVec2(wl.pos.x, wl.pos.y + title_h - 2.5F),
                                        ImVec2(wl.pos.x + tab_w, wl.pos.y + title_h),
                                        st.theme.accent_bright);
            wl.draw_list->AddRectFilled(ImVec2(wl.pos.x + tab_w, wl.pos.y + title_h - 1.0F),
                                        ImVec2(wl.pos.x + wl.size.x, wl.pos.y + title_h),
                                        st.theme.window_border);
            wl.draw_list->AddText(ImVec2(wl.pos.x + 10.0F, wl.pos.y + 6.0F),
                                  st.theme.text, vis.data(), vis.data() + vis.size(), 1.32F);
        }

        if (p_open != nullptr)
        {
            const ImVec2 cb_min { wl.pos.x + wl.size.x - 22.0F, wl.pos.y + 4.0F };
            const ImVec2 cb_max { wl.pos.x + wl.size.x - 4.0F,  wl.pos.y + 22.0F };
            const bool cb_hov = !popup_occluded &&
                                (mp.x >= cb_min.x && mp.x <= cb_max.x && mp.y >= cb_min.y && mp.y <= cb_max.y);
            if (cb_hov)
            {
                st.any_item_hovered = true;
                wl.draw_list->AddRectFilled(cb_min, cb_max, IM_COL32(215, 52, 52, 235), 4.0F);
                if (st.mouse_clicked[0]) *p_open = false;
            }
            wl.draw_list->AddText(ImVec2(cb_min.x + 5.0F, cb_min.y + 3.0F), st.theme.text, "X", nullptr, 1.28F);
        }
        content_top = wl.pos.y + title_h + st.style_window_padding.y;
    }

    wl.draw_list->PushClipRect(ImVec2(wl.pos.x + 2.0F, content_top - 4.0F),
                               ImVec2(wl.pos.x + wl.size.x - 2.0F, wl.pos.y + wl.size.y - 2.0F));
    wl.cursor = ImVec2 { wl.pos.x + st.style_window_padding.x, content_top - wl.scroll_y };
    wl.content_max_x = wl.cursor.x;
    wl.content_max_y = wl.cursor.y;
    st.window_stack.push_back(std::move(wl));
    return true;
}

namespace detail
{
[[nodiscard]] inline bool is_occluded_by_popup(const cd::ui::chroma::WindowLayout& w) noexcept;
}  // namespace detail

inline void End()
{
    auto& st = cd::ui::chroma::state();
    if (st.window_stack.empty()) return;
    auto wl = std::move(st.window_stack.back());
    st.window_stack.pop_back();
    if (wl.skip) return;

    wl.draw_list->PopClipRect();

    const ImGuiID win_id = (wl.id != 0U) ? wl.id : cd::ui::chroma::hash_str(wl.name);
    const float used_w = wl.content_max_x - wl.pos.x + 14.0F;
    const float used_h = (wl.content_max_y + wl.scroll_y) - wl.pos.y + 10.0F;
    if ((wl.flags & ImGuiWindowFlags_AlwaysAutoResize) != 0 && !wl.is_docked)
    {
        st.auto_size_map[win_id] = ImVec2 { std::clamp(used_w, 160.0F, 1580.0F),
                                            std::clamp(used_h, 30.0F, 680.0F) };
        st.scroll_map[win_id] = 0.0F;
    }
    else
    {
        st.auto_size_map[win_id] = wl.size;
        if ((wl.flags & ImGuiWindowFlags_NoScrollbar) == 0 && used_h > wl.size.y + 6.0F)
        {
            const float track_y0 = wl.pos.y + ((wl.flags & ImGuiWindowFlags_NoTitleBar) != 0 ? 4.0F : 28.0F);
            const float track_h  = std::max(20.0F, (wl.pos.y + wl.size.y - 4.0F) - track_y0);
            const float ratio    = std::clamp(wl.size.y / used_h, 0.10F, 1.0F);
            const float thumb_h  = std::max(18.0F, track_h * ratio);
            const float max_scr  = std::max(1.0F, used_h - wl.size.y);
            const ImVec2 mp = st.io.MousePos;
            const bool win_hov = !detail::is_occluded_by_popup(wl) &&
                                  (mp.x >= wl.pos.x && mp.x <= wl.pos.x + wl.size.x &&
                                   mp.y >= wl.pos.y && mp.y <= wl.pos.y + wl.size.y);
            if (win_hov && std::fabs(st.io.MouseWheel) > 0.01F)
            {
                wl.scroll_y = std::clamp(wl.scroll_y - st.io.MouseWheel * 32.0F, 0.0F, max_scr);
                st.io.MouseWheel = 0.0F;
            }
            st.scroll_map[win_id] = std::clamp(wl.scroll_y, 0.0F, max_scr);

            const ImGuiID sb_id = win_id ^ 0x5C8011BAU;
            const bool sb_hov = !detail::is_occluded_by_popup(wl) &&
                                 (mp.x >= wl.pos.x + wl.size.x - 10.0F && mp.x <= wl.pos.x + wl.size.x &&
                                  mp.y >= track_y0 && mp.y <= track_y0 + track_h);
            if (sb_hov && st.mouse_clicked[0]) st.active_id = sb_id;
            if (st.active_id == sb_id && st.mouse_down[0])
            {
                const float t = std::clamp((mp.y - track_y0 - thumb_h * 0.5F) / std::max(1.0F, track_h - thumb_h), 0.0F, 1.0F);
                st.scroll_map[win_id] = t * max_scr;
            }
            const float thumb_y = track_y0 + (st.scroll_map[win_id] / max_scr) * (track_h - thumb_h);
            wl.draw_list->AddRectFilled(ImVec2(wl.pos.x + wl.size.x - 6.5F, track_y0),
                                        ImVec2(wl.pos.x + wl.size.x - 2.0F, track_y0 + track_h),
                                        IM_COL32(10, 14, 22, 120), 2.5F);
            wl.draw_list->AddRectFilled(ImVec2(wl.pos.x + wl.size.x - 6.5F, thumb_y),
                                        ImVec2(wl.pos.x + wl.size.x - 2.0F, thumb_y + thumb_h),
                                        (sb_hov || st.active_id == sb_id) ? st.theme.accent_bright : st.theme.accent, 2.5F);
        }
        else
        {
            st.scroll_map[win_id] = 0.0F;
        }
    }
}

inline bool BeginChild(const char* str_id, const ImVec2& size = ImVec2(0, 0), bool border = false)
{
    auto& st = cd::ui::chroma::state();
    if (st.window_stack.empty()) return false;
    auto& parent = st.window_stack.back();

    cd::ui::chroma::WindowLayout child {};
    child.name      = str_id != nullptr ? str_id : "child";
    child.is_child  = true;
    child.draw_list = parent.draw_list;
    child.pos       = parent.cursor;
    const float avail_w = std::max(60.0F, (parent.pos.x + parent.size.x - 10.0F) - parent.cursor.x);
    const float avail_h = std::max(36.0F, (parent.pos.y + parent.size.y - 8.0F) - parent.cursor.y);
    child.size.x = (size.x > 0.0F) ? size.x : avail_w;
    child.size.y = (size.y > 0.0F) ? size.y : ((size.y < 0.0F) ? std::max(36.0F, avail_h + size.y) : avail_h);

    const ImGuiID cid = GetID(child.name.c_str());
    child.id = cid;
    child.scroll_y = st.scroll_map[cid];

    if (border)
    {
        child.draw_list->AddRectFilled(child.pos, ImVec2(child.pos.x + child.size.x, child.pos.y + child.size.y),
                                       st.theme.child_bg, 4.0F);
        child.draw_list->AddRect(child.pos, ImVec2(child.pos.x + child.size.x, child.pos.y + child.size.y),
                                 st.theme.window_border, 4.0F, 0, 1.0F);
    }
    child.draw_list->PushClipRect(child.pos, ImVec2(child.pos.x + child.size.x, child.pos.y + child.size.y));
    child.cursor = ImVec2 { child.pos.x + st.style_window_padding.x, child.pos.y + 6.0F - child.scroll_y };
    child.content_max_x = child.cursor.x;
    child.content_max_y = child.cursor.y;

    parent.prev_item_min = child.pos;
    parent.prev_item_max = ImVec2 { child.pos.x + child.size.x, child.pos.y + child.size.y };
    parent.cursor.y      = child.pos.y + child.size.y + 5.0F;
    parent.content_max_x = std::max(parent.content_max_x, parent.prev_item_max.x);
    parent.content_max_y = std::max(parent.content_max_y, parent.prev_item_max.y);

    st.window_stack.push_back(std::move(child));
    return true;
}

inline void EndChild()
{
    auto& st = cd::ui::chroma::state();
    if (st.window_stack.empty() || !st.window_stack.back().is_child) return;
    auto child = std::move(st.window_stack.back());
    st.window_stack.pop_back();

    const ImGuiID cid = (child.id != 0U) ? child.id : GetID(child.name.c_str());
    const float used_h  = (child.content_max_y + child.scroll_y) - child.pos.y + 6.0F;
    const float max_scr = std::max(0.0F, used_h - child.size.y);
    const ImVec2 mp = st.io.MousePos;
    if (max_scr > 0.0F && !detail::is_occluded_by_popup(child) &&
        mp.x >= child.pos.x && mp.x <= child.pos.x + child.size.x &&
        mp.y >= child.pos.y && mp.y <= child.pos.y + child.size.y)
    {
        if (std::fabs(st.io.MouseWheel) > 0.01F)
        {
            child.scroll_y = std::clamp(child.scroll_y - st.io.MouseWheel * 26.0F, 0.0F, max_scr);
            st.io.MouseWheel = 0.0F;
        }
        else if (st.dnd_active && st.mouse_down[0] && child.size.y > 40.0F)
        {
            const float edge_zone = 18.0F;
            const float dt_step   = std::clamp(st.io.DeltaTime, 1.0F / 240.0F, 0.05F) * 180.0F;
            if (mp.y < child.pos.y + edge_zone)
            {
                child.scroll_y = std::clamp(child.scroll_y - dt_step, 0.0F, max_scr);
            }
            else if (mp.y > child.pos.y + child.size.y - edge_zone)
            {
                child.scroll_y = std::clamp(child.scroll_y + dt_step, 0.0F, max_scr);
            }
        }
    }
    st.scroll_map[cid]  = std::clamp(child.scroll_y, 0.0F, max_scr);

    if (max_scr > 2.0F)
    {
        const float track_y0 = child.pos.y + 3.0F;
        const float track_h  = std::max(16.0F, child.size.y - 6.0F);
        const float ratio    = std::clamp(child.size.y / used_h, 0.12F, 1.0F);
        const float thumb_h  = std::max(14.0F, track_h * ratio);

        const ImGuiID sb_id = cid ^ 0x5C8011BAU;
        const bool sb_hov = !detail::is_occluded_by_popup(child) &&
                             (mp.x >= child.pos.x + child.size.x - 10.0F && mp.x <= child.pos.x + child.size.x &&
                              mp.y >= track_y0 && mp.y <= track_y0 + track_h);
        if (sb_hov && st.mouse_clicked[0]) st.active_id = sb_id;
        if (st.active_id == sb_id && st.mouse_down[0])
        {
            const float t = std::clamp((mp.y - track_y0 - thumb_h * 0.5F) / std::max(1.0F, track_h - thumb_h), 0.0F, 1.0F);
            st.scroll_map[cid] = t * max_scr;
        }
        const float thumb_y = track_y0 + (st.scroll_map[cid] / max_scr) * (track_h - thumb_h);
        child.draw_list->AddRectFilled(ImVec2(child.pos.x + child.size.x - 6.0F, track_y0),
                                       ImVec2(child.pos.x + child.size.x - 2.0F, track_y0 + track_h),
                                       IM_COL32(10, 14, 22, 120), 2.0F);
        child.draw_list->AddRectFilled(ImVec2(child.pos.x + child.size.x - 6.0F, thumb_y),
                                       ImVec2(child.pos.x + child.size.x - 2.0F, thumb_y + thumb_h),
                                       (sb_hov || st.active_id == sb_id) ? st.theme.accent_bright : st.theme.accent, 2.0F);
    }

    child.draw_list->PopClipRect();
}

// -----------------------------------------------------------------------------
// Layout cursor helpers
// -----------------------------------------------------------------------------
namespace detail
{
[[nodiscard]] inline bool is_occluded_by_popup(const cd::ui::chroma::WindowLayout& w) noexcept
{
    const auto& st = cd::ui::chroma::state();
    if (w.is_popup) return false;
    if (st.active_menu_name.empty() && st.open_popup_id == 0U) return false;
    const ImVec2 mp = st.io.MousePos;
    return (mp.x >= st.last_popup_min.x && mp.x <= st.last_popup_max.x &&
            mp.y >= st.last_popup_min.y && mp.y <= st.last_popup_max.y);
}

[[nodiscard]] inline bool is_in_window_clip(const cd::ui::chroma::WindowLayout& w, ImVec2 mp) noexcept
{
    return (mp.x >= w.pos.x && mp.x <= w.pos.x + w.size.x &&
            mp.y >= w.pos.y && mp.y <= w.pos.y + w.size.y);
}

inline void finalize_popup_layout(cd::ui::chroma::WindowLayout& pw, ImVec2 final_sz) noexcept
{
    auto& st = cd::ui::chroma::state();
    const float dx = final_sz.x - pw.size.x;
    pw.size = final_sz;
    if (!st.in_open_submenu && pw.child_popup_cmd_end == pw.child_popup_cmd_start)
    {
        st.last_popup_min = pw.pos;
        st.last_popup_max = ImVec2(pw.pos.x + final_sz.x, pw.pos.y + final_sz.y);
    }
    else
    {
        st.last_popup_min = ImVec2(std::min(st.last_popup_min.x, pw.pos.x),
                                   std::min(st.last_popup_min.y, pw.pos.y));
        st.last_popup_max = ImVec2(std::max(st.last_popup_max.x, pw.pos.x + final_sz.x),
                                   std::max(st.last_popup_max.y, pw.pos.y + final_sz.y));
    }
    if (pw.draw_list == nullptr) return;
    auto cmds = pw.draw_list->mutable_cmds();
    if (pw.popup_cmd_start + 3U > cmds.size()) return;
    for (std::size_t i = pw.popup_cmd_start; i < pw.popup_cmd_start + 3U; ++i)
    {
        cmds[i].x0 = pw.pos.x;
        cmds[i].y0 = pw.pos.y;
        cmds[i].x1 = final_sz.x;
        cmds[i].y1 = final_sz.y;
    }
    for (std::size_t i = pw.popup_cmd_start + 3U; i < cmds.size(); ++i)
    {
        if (pw.child_popup_cmd_end > pw.child_popup_cmd_start &&
            i >= pw.child_popup_cmd_start && i < pw.child_popup_cmd_end)
        {
            if (dx > 0.5F)
            {
                cmds[i].x0 += dx;
                if (cmds[i].kind == ImDrawList::CmdKind::kTriangle)
                {
                    cmds[i].x1 += dx;
                    cmds[i].x2 += dx;
                }
                else if (cmds[i].kind == ImDrawList::CmdKind::kPolyQuad)
                {
                    cmds[i].x1 += dx;
                    cmds[i].x2 += dx;
                    cmds[i].x3 += dx;
                }
            }
            continue;
        }
        auto& rc = cmds[i];
        if (rc.kind == ImDrawList::CmdKind::kText && rc.flag)
        {
            rc.x0 = std::floor(pw.pos.x + final_sz.x - rc.param1);
        }
        else if (rc.kind == ImDrawList::CmdKind::kTriangle && rc.flag)
        {
            const float cx = pw.pos.x + final_sz.x - 10.0F;
            rc.x0 = cx - 4.0F * 0.35F;
            rc.x1 = cx - 4.0F * 0.35F;
            rc.x2 = cx + 4.0F * 0.45F;
        }
        else if ((rc.kind == ImDrawList::CmdKind::kSolidQuad ||
                  rc.kind == ImDrawList::CmdKind::kRoundedRect) &&
                 rc.x0 >= pw.pos.x + 3.0F && rc.x0 <= pw.pos.x + 14.0F && rc.x1 > 80.0F)
        {
            const float pad_x = rc.x0 - pw.pos.x;
            rc.x1 = std::max(rc.x1, final_sz.x - pad_x * 2.0F);
        }
    }
}

inline void advance_item(cd::ui::chroma::WindowLayout& w, ImVec2 item_min, ImVec2 item_max) noexcept
{
    auto& st = cd::ui::chroma::state();
    w.prev_item_min = item_min;
    w.prev_item_max = item_max;
    w.line_height   = item_max.y - item_min.y;
    w.content_max_x = std::max(w.content_max_x, item_max.x);
    if (st.in_menu_bar && !st.in_open_menu)
    {
        w.cursor.x = item_max.x + 6.0F;
        w.cursor.y = 4.0F;
    }
    else if (!st.table_stack.empty())
    {
        const auto& ts = st.table_stack.back();
        const float col_w = ts.width / static_cast<float>(ts.cols);
        w.cursor.x = ts.start_x + static_cast<float>(ts.col_idx) * col_w;
        w.cursor.y = item_max.y + 2.0F;
    }
    else
    {
        w.cursor.x = w.pos.x + st.style_window_padding.x + w.indent;
        w.cursor.y = item_max.y + st.style_item_spacing.y;
    }
    w.content_max_y = std::max(w.content_max_y, item_max.y + 2.0F);
    if (w.is_popup)
    {
        w.size.x = std::clamp(std::max(w.size.x, w.content_max_x - w.pos.x + 16.0F), 165.0F, 520.0F);
        w.size.y = std::clamp(std::max(w.size.y, w.content_max_y - w.pos.y + 8.0F), 32.0F, 480.0F);
    }

    st.last_item_min = item_min;
    st.last_item_max = item_max;
    const ImVec2 mp  = st.io.MousePos;
    const bool occluded = is_occluded_by_popup(w);
    const bool in_win   = is_in_window_clip(w, mp);
    st.last_item_hovered = !occluded && !w.skip && in_win &&
                           (mp.x >= item_min.x && mp.x <= item_max.x &&
                            mp.y >= item_min.y && mp.y <= item_max.y);
    if (st.last_item_hovered) st.any_item_hovered = true;
}

[[nodiscard]] inline float resolve_item_width(const cd::ui::chroma::WindowLayout& w) noexcept
{
    auto& st = cd::ui::chroma::state();
    float avail = std::max(50.0F, (w.pos.x + w.size.x - 10.0F) - w.cursor.x);
    if (!st.table_stack.empty())
    {
        const auto& ts = st.table_stack.back();
        avail = std::max(40.0F, (ts.width / static_cast<float>(ts.cols)) - 6.0F);
    }
    if (st.next_item_width != 0.0F)
    {
        const float req = st.next_item_width;
        st.next_item_width = 0.0F;
        return (req < 0.0F) ? std::max(40.0F, avail + req) : std::min(avail, req);
    }
    if (!st.item_width_stack.empty())
    {
        const float req = st.item_width_stack.back();
        return (req < 0.0F) ? std::max(40.0F, avail + req) : std::min(avail, req);
    }
    return std::max(60.0F, avail * 0.62F);
}
}  // namespace detail

inline void SameLine(float offset_from_start_x = 0.0F, float spacing = -1.0F) noexcept
{
    auto& st = cd::ui::chroma::state();
    if (st.window_stack.empty()) return;
    auto& w = st.window_stack.back();
    const float sp = (spacing < 0.0F) ? st.style_item_spacing.x : spacing;
    if (offset_from_start_x > 0.0F)
        w.cursor.x = w.pos.x + offset_from_start_x;
    else
        w.cursor.x = w.prev_item_max.x + sp;
    w.cursor.y = w.prev_item_min.y;
}

inline void Indent(float indent_w = 0.0F) noexcept
{
    auto& st = cd::ui::chroma::state();
    if (st.window_stack.empty()) return;
    auto& w = st.window_stack.back();
    w.indent += (indent_w > 0.0F) ? indent_w : 14.0F;
    w.cursor.x = w.pos.x + st.style_window_padding.x + w.indent;
}

inline void Unindent(float indent_w = 0.0F) noexcept
{
    auto& st = cd::ui::chroma::state();
    if (st.window_stack.empty()) return;
    auto& w = st.window_stack.back();
    w.indent = std::max(0.0F, w.indent - ((indent_w > 0.0F) ? indent_w : 14.0F));
    w.cursor.x = w.pos.x + st.style_window_padding.x + w.indent;
}

inline void Separator() noexcept
{
    auto& st = cd::ui::chroma::state();
    if (st.window_stack.empty()) return;
    auto& w = st.window_stack.back();
    if (st.in_menu_bar && !st.in_open_menu)
    {
        const float x = w.cursor.x + 2.0F;
        w.draw_list->AddRectFilled(ImVec2(x, 5.0F), ImVec2(x + 1.0F, 23.0F), st.theme.separator);
        w.cursor.x += 8.0F;
        return;
    }
    const float x0 = w.cursor.x;
    const float x1 = w.pos.x + w.size.x - 10.0F;
    const float y  = w.cursor.y + 3.0F;
    w.draw_list->AddRectFilled(ImVec2(x0, y), ImVec2(x1, y + 1.0F), st.theme.separator);
    detail::advance_item(w, ImVec2(x0, w.cursor.y), ImVec2(x1, w.cursor.y + 6.0F));
}

inline void SeparatorText(const char* label) noexcept
{
    auto& st = cd::ui::chroma::state();
    if (st.window_stack.empty()) return;
    auto& w = st.window_stack.back();
    const auto vis = cd::ui::chroma::visible_label(label != nullptr ? label : "");
    const float x0 = w.cursor.x;
    const float y0 = w.cursor.y + 2.0F;
    const float x1 = w.pos.x + w.size.x - 10.0F;
    w.draw_list->AddRectFilled(ImVec2(x0, y0 + 1.0F), ImVec2(x0 + 3.0F, y0 + 14.0F), st.theme.accent);
    w.draw_list->AddText(ImVec2(x0 + 8.0F, y0 + 2.0F), st.theme.accent_bright,
                         vis.data(), vis.data() + vis.size(), 1.30F);
    const ImVec2 tsz = cd::ui::chroma::measure_text_px(vis, 1.30F);
    const float text_w = tsz.x + 14.0F;
    if (x0 + text_w < x1)
    {
        w.draw_list->AddRectFilled(ImVec2(x0 + text_w, y0 + 8.0F), ImVec2(x1, y0 + 9.0F), st.theme.separator);
    }
    detail::advance_item(w, ImVec2(x0, y0), ImVec2(x1, y0 + 17.0F));
}

// -----------------------------------------------------------------------------
// Text Widgets
// -----------------------------------------------------------------------------
inline void TextUnformatted(const char* text) noexcept
{
    auto& st = cd::ui::chroma::state();
    if (st.window_stack.empty() || text == nullptr) return;
    if (st.dnd_in_source)
    {
        st.dnd_label = cd::ui::chroma::visible_label(text);
        return;
    }
    auto& w = st.window_stack.back();
    const std::string_view sv { text };
    w.draw_list->AddText(ImVec2(w.cursor.x, w.cursor.y + 2.0F), st.theme.text,
                         sv.data(), sv.data() + sv.size(), 1.28F);
    const ImVec2 tsz = cd::ui::chroma::measure_text_px(sv, 1.28F);
    detail::advance_item(w, w.cursor, ImVec2(w.cursor.x + tsz.x, w.cursor.y + std::max(15.0F, tsz.y)));
}

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-nonliteral"
#pragma clang diagnostic ignored "-Wdouble-promotion"
#endif

inline void Text(const char* fmt, ...) noexcept
{
    if (fmt == nullptr) return;
    char buf[512] {};
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    TextUnformatted(buf);
}

inline void TextDisabled(const char* fmt, ...) noexcept
{
    auto& st = cd::ui::chroma::state();
    if (st.window_stack.empty() || fmt == nullptr) return;
    char buf[512] {};
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    auto& w = st.window_stack.back();
    const std::string_view sv { buf };
    w.draw_list->AddText(ImVec2(w.cursor.x, w.cursor.y + 2.0F), st.theme.text_disabled,
                         sv.data(), sv.data() + sv.size(), 1.25F);
    const ImVec2 tsz = cd::ui::chroma::measure_text_px(sv, 1.25F);
    detail::advance_item(w, w.cursor, ImVec2(w.cursor.x + tsz.x, w.cursor.y + std::max(14.0F, tsz.y)));
}

inline void TextColored(const ImVec4& col, const char* fmt, ...) noexcept
{
    auto& st = cd::ui::chroma::state();
    if (st.window_stack.empty() || fmt == nullptr) return;
    char buf[512] {};
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    auto& w = st.window_stack.back();
    const std::string_view sv { buf };
    w.draw_list->AddText(ImVec2(w.cursor.x, w.cursor.y + 2.0F), cd::ui::chroma::pack_vec4(col),
                         sv.data(), sv.data() + sv.size(), 1.28F);
    const ImVec2 tsz = cd::ui::chroma::measure_text_px(sv, 1.28F);
    detail::advance_item(w, w.cursor, ImVec2(w.cursor.x + tsz.x, w.cursor.y + std::max(15.0F, tsz.y)));
}

// -----------------------------------------------------------------------------
// Buttons, Checkboxes, Radio, Toggle, Selectables, CollapsingHeaders, Trees
// -----------------------------------------------------------------------------
inline bool Button(const char* label, const ImVec2& size_arg = ImVec2(0, 0)) noexcept
{
    auto& st = cd::ui::chroma::state();
    if (st.window_stack.empty() || label == nullptr) return false;
    auto& w = st.window_stack.back();
    const auto vis = cd::ui::chroma::visible_label(label);
    const ImVec2 tsz = cd::ui::chroma::measure_text_px(vis, 1.28F);
    float avail_w = std::max(36.0F, (w.pos.x + w.size.x - 10.0F) - w.cursor.x);
    if (!st.table_stack.empty())
    {
        const auto& ts = st.table_stack.back();
        avail_w = std::max(36.0F, (ts.width / static_cast<float>(ts.cols)) - 6.0F);
    }
    const float bw = (size_arg.x > 0.0F)
                         ? size_arg.x
                         : ((size_arg.x < 0.0F) ? avail_w : (tsz.x + 16.0F));
    const float bh = (size_arg.y > 0.0F) ? size_arg.y : 22.0F;

    const ImVec2 p0 = w.cursor;
    const ImVec2 p1 { p0.x + bw, p0.y + bh };
    const ImGuiID id = GetID(label);
    st.last_item_id = id;
    detail::advance_item(w, p0, p1);

    const bool hov = st.last_item_hovered;
    if (hov && st.mouse_clicked[0]) st.active_id = id;
    const bool act = (st.active_id == id && st.mouse_down[0]);
    const bool clicked = (hov && st.mouse_clicked[0]);

    const ImU32 bg = act ? st.theme.button_active : (hov ? st.theme.button_hover : st.theme.button);
    w.draw_list->AddRectFilled(p0, p1, bg, st.style_frame_rounding);
    w.draw_list->AddRect(p0, p1, hov ? st.theme.accent_bright : st.theme.window_border, st.style_frame_rounding, 0, 1.0F);

    w.draw_list->PushClipRect(ImVec2(p0.x + 2.0F, p0.y + 1.0F), ImVec2(p1.x - 2.0F, p1.y - 1.0F));
    const float tx = p0.x + std::max(4.0F, (bw - tsz.x) * 0.5F);
    const float ty = p0.y + std::max(2.0F, (bh - 13.0F) * 0.5F);
    w.draw_list->AddText(ImVec2(tx, ty), st.theme.text, vis.data(), vis.data() + vis.size(), 1.28F);
    w.draw_list->PopClipRect();
    return clicked;
}

inline bool SmallButton(const char* label) noexcept
{
    auto& st = cd::ui::chroma::state();
    if (st.window_stack.empty() || label == nullptr) return false;
    const auto vis = cd::ui::chroma::visible_label(label);
    const ImVec2 tsz = cd::ui::chroma::measure_text_px(vis, 1.24F);
    return Button(label, ImVec2(tsz.x + 12.0F, 18.0F));
}

inline bool Checkbox(const char* label, bool* v) noexcept
{
    auto& st = cd::ui::chroma::state();
    if (st.window_stack.empty() || label == nullptr || v == nullptr) return false;
    auto& w = st.window_stack.back();
    const auto vis = cd::ui::chroma::visible_label(label);
    const ImVec2 tsz = cd::ui::chroma::measure_text_px(vis, 1.28F);
    const float box_sz = 16.0F;
    const ImVec2 p0 = w.cursor;
    const ImVec2 box_max { p0.x + box_sz, p0.y + box_sz };
    const ImVec2 p1 { box_max.x + (vis.empty() ? 0.0F : (7.0F + tsz.x)), p0.y + box_sz };

    const ImGuiID id = GetID(label);
    st.last_item_id = id;
    detail::advance_item(w, p0, p1);

    const bool hov = st.last_item_hovered;
    bool changed = false;
    if (hov && st.mouse_clicked[0])
    {
        *v = !(*v);
        changed = true;
    }

    w.draw_list->AddRectFilled(p0, box_max, *v ? st.theme.accent : st.theme.frame_bg, 3.5F);
    w.draw_list->AddRect(p0, box_max, hov ? st.theme.accent_bright : st.theme.window_border, 3.5F, 0, 1.0F);
    if (*v)
    {
        w.draw_list->AddLine(ImVec2(p0.x + 3.0F, p0.y + 8.5F),
                             ImVec2(p0.x + 6.5F, p0.y + 12.5F), IM_COL32(255, 255, 255, 255), 2.0F);
        w.draw_list->AddLine(ImVec2(p0.x + 6.5F, p0.y + 12.5F),
                             ImVec2(p0.x + 13.0F, p0.y + 4.0F), IM_COL32(255, 255, 255, 255), 2.0F);
    }
    if (!vis.empty())
    {
        w.draw_list->AddText(ImVec2(box_max.x + 6.0F, p0.y + 2.0F), st.theme.text,
                             vis.data(), vis.data() + vis.size(), 1.28F);
    }
    return changed;
}

inline bool RadioButton(const char* label, bool active) noexcept
{
    auto& st = cd::ui::chroma::state();
    if (st.window_stack.empty() || label == nullptr) return false;
    auto& w = st.window_stack.back();
    const auto vis = cd::ui::chroma::visible_label(label);
    const ImVec2 tsz = cd::ui::chroma::measure_text_px(vis, 1.28F);
    const float r = 8.0F;
    const ImVec2 p0 = w.cursor;
    const ImVec2 center { p0.x + r, p0.y + r };
    const ImVec2 p1 { p0.x + r * 2.0F + 6.0F + tsz.x, p0.y + r * 2.0F };

    const ImGuiID id = GetID(label);
    st.last_item_id = id;
    detail::advance_item(w, p0, p1);
    const bool hov = st.last_item_hovered;
    w.draw_list->AddCircleFilled(center, r, st.theme.frame_bg, 16);
    w.draw_list->AddCircle(center, r, hov ? st.theme.accent_bright : st.theme.window_border, 16, 1.2F);
    if (active)
    {
        w.draw_list->AddCircleFilled(center, r * 0.52F, st.theme.accent_bright, 14);
    }
    w.draw_list->AddText(ImVec2(p0.x + r * 2.0F + 6.0F, p0.y + 2.0F), st.theme.text,
                         vis.data(), vis.data() + vis.size(), 1.28F);
    return hov && st.mouse_clicked[0];
}

inline bool RadioButton(const char* label, int* v, int v_button) noexcept
{
    const bool pressed = RadioButton(label, v != nullptr && *v == v_button);
    if (pressed && v != nullptr) *v = v_button;
    return pressed;
}

inline bool ToggleSwitch(const char* label, bool* v) noexcept
{
    auto& st = cd::ui::chroma::state();
    if (st.window_stack.empty() || label == nullptr || v == nullptr) return false;
    auto& w = st.window_stack.back();
    const auto vis = cd::ui::chroma::visible_label(label);
    const ImVec2 tsz = cd::ui::chroma::measure_text_px(vis, 1.28F);
    const float sw = 32.0F;
    const float sh = 16.0F;
    const ImVec2 p0 = w.cursor;
    const ImVec2 smax { p0.x + sw, p0.y + sh };
    const ImVec2 p1 { smax.x + (vis.empty() ? 0.0F : (7.0F + tsz.x)), p0.y + sh };

    const ImGuiID id = GetID(label);
    st.last_item_id = id;
    detail::advance_item(w, p0, p1);
    const bool hov = st.last_item_hovered;
    bool changed = false;
    if (hov && st.mouse_clicked[0])
    {
        *v = !(*v);
        changed = true;
    }
    w.draw_list->AddRectFilled(p0, smax, *v ? st.theme.accent : st.theme.frame_bg, 8.0F);
    w.draw_list->AddRect(p0, smax, hov ? st.theme.accent_bright : st.theme.window_border, 8.0F, 0, 1.0F);
    const float kx = *v ? (smax.x - 8.0F) : (p0.x + 8.0F);
    w.draw_list->AddCircleFilled(ImVec2(kx, p0.y + 8.0F), 6.0F, IM_COL32(250, 252, 255, 255), 14);
    if (!vis.empty())
    {
        w.draw_list->AddText(ImVec2(smax.x + 7.0F, p0.y + 2.0F), st.theme.text,
                             vis.data(), vis.data() + vis.size(), 1.28F);
    }
    return changed;
}

inline bool Selectable(const char* label, bool selected = false,
                       ImGuiSelectableFlags /*flags*/ = 0, const ImVec2& size_arg = ImVec2(0, 0)) noexcept
{
    auto& st = cd::ui::chroma::state();
    if (st.window_stack.empty() || label == nullptr) return false;
    auto& w = st.window_stack.back();
    const auto vis = cd::ui::chroma::visible_label(label);
    const float avail_w = std::max(60.0F, (w.pos.x + w.size.x - 10.0F) - w.cursor.x);
    const float sw = (size_arg.x > 0.0F) ? size_arg.x : avail_w;
    const float sh = (size_arg.y > 0.0F) ? size_arg.y : 19.0F;

    const ImVec2 p0 = w.cursor;
    const ImVec2 p1 { p0.x + sw, p0.y + sh };
    const ImGuiID id = GetID(label);
    st.last_item_id = id;
    detail::advance_item(w, p0, p1);

    const bool hov = st.last_item_hovered;
    if (selected || hov)
    {
        const ImU32 bg = selected ? st.theme.button_active : st.theme.frame_hover;
        w.draw_list->AddRectFilled(p0, p1, bg, 3.5F);
        if (selected)
        {
            w.draw_list->AddRectFilled(p0, ImVec2(p0.x + 3.0F, p1.y), st.theme.accent_bright, 1.5F);
        }
    }
    w.draw_list->PushClipRect(ImVec2(p0.x + 2.0F, p0.y), ImVec2(p1.x - 2.0F, p1.y));
    w.draw_list->AddText(ImVec2(p0.x + 7.0F, p0.y + 3.0F), st.theme.text,
                         vis.data(), vis.data() + vis.size(), 1.28F);
    w.draw_list->PopClipRect();
    if (hov && st.mouse_clicked[0])
    {
        st.active_id = id;
        if (w.is_popup)
        {
            st.open_popup_id = 0U;
            st.mouse_clicked[0] = false;
        }
        return true;
    }
    return false;
}

inline bool CollapsingHeader(const char* label, ImGuiTreeNodeFlags flags = 0) noexcept
{
    auto& st = cd::ui::chroma::state();
    if (st.window_stack.empty() || label == nullptr) return false;
    auto& w = st.window_stack.back();
    const auto vis = cd::ui::chroma::visible_label(label);
    const ImGuiID id = GetID(label);
    auto it = st.collapse_map.find(id);
    if (it == st.collapse_map.end())
    {
        const bool def_open = (flags & ImGuiTreeNodeFlags_DefaultOpen) != 0;
        it = st.collapse_map.emplace(id, def_open).first;
    }

    const float avail_w = std::max(80.0F, (w.pos.x + w.size.x - 10.0F) - w.cursor.x);
    const ImVec2 p0 = w.cursor;
    const ImVec2 p1 { p0.x + avail_w, p0.y + 22.0F };
    st.last_item_id = id;
    detail::advance_item(w, p0, p1);

    if (st.last_item_hovered && st.mouse_clicked[0])
    {
        it->second = !it->second;
    }

    const bool open = it->second;
    w.draw_list->AddGradient(p0, p1,
                             st.last_item_hovered ? st.theme.button_hover : st.theme.window_header0,
                             st.theme.window_header1, false);
    w.draw_list->AddRect(p0, p1, st.theme.window_border, 4.0F, 0, 1.0F);
    cd::ui::chroma::DrawIcon(w.draw_list,
                             open ? cd::ui::chroma::IconKind::kChevronDown : cd::ui::chroma::IconKind::kChevronRight,
                             ImVec2(p0.x + 11.0F, p0.y + 11.0F), 4.5F, st.theme.accent_bright);
    w.draw_list->AddText(ImVec2(p0.x + 21.0F, p0.y + 5.0F), st.theme.text,
                         vis.data(), vis.data() + vis.size(), 1.28F);
    return open;
}

inline bool TreeNodeEx(const char* label, ImGuiTreeNodeFlags flags = 0) noexcept
{
    auto& st = cd::ui::chroma::state();
    if (st.window_stack.empty() || label == nullptr) return false;
    auto& w = st.window_stack.back();
    const auto vis = cd::ui::chroma::visible_label(label);
    const ImGuiID id = GetID(label);
    auto it = st.collapse_map.find(id);
    if (it == st.collapse_map.end())
    {
        const bool def_open = (flags & ImGuiTreeNodeFlags_DefaultOpen) != 0;
        it = st.collapse_map.emplace(id, def_open).first;
    }
    const bool is_leaf = (flags & ImGuiTreeNodeFlags_Leaf) != 0;
    const float avail_w = std::max(60.0F, (w.pos.x + w.size.x - 10.0F) - w.cursor.x);
    const ImVec2 p0 = w.cursor;
    const ImVec2 p1 { p0.x + avail_w, p0.y + 19.0F };
    st.last_item_id = id;
    detail::advance_item(w, p0, p1);

    if (st.last_item_hovered && st.mouse_clicked[0] && !is_leaf)
    {
        it->second = !it->second;
    }
    const bool open = is_leaf ? true : it->second;
    if (st.last_item_hovered || (flags & ImGuiTreeNodeFlags_Selected) != 0)
    {
        w.draw_list->AddRectFilled(p0, p1, (flags & ImGuiTreeNodeFlags_Selected) != 0 ? st.theme.button_active : st.theme.frame_hover, 3.0F);
    }
    if (!is_leaf)
    {
        cd::ui::chroma::DrawIcon(w.draw_list,
                                 open ? cd::ui::chroma::IconKind::kChevronDown : cd::ui::chroma::IconKind::kChevronRight,
                                 ImVec2(p0.x + 8.0F, p0.y + 9.5F), 4.0F, st.theme.accent_bright);
    }
    w.draw_list->AddText(ImVec2(p0.x + 17.0F, p0.y + 3.0F), st.theme.text,
                         vis.data(), vis.data() + vis.size(), 1.28F);
    if (open && (flags & ImGuiTreeNodeFlags_NoTreePushOnOpen) == 0)
    {
        Indent(12.0F);
    }
    return open;
}

inline bool TreeNode(const char* label) noexcept
{
    return TreeNodeEx(label, 0);
}

inline void TreePop() noexcept
{
    Unindent(12.0F);
}

// -----------------------------------------------------------------------------
// Sliders, DragFloat3 (X/Y/Z Color-Coded), ColorEdit3 (with HSV Picker), Combo, InputText
// -----------------------------------------------------------------------------
inline bool SliderFloat(const char* label, float* v, float v_min, float v_max,
                        const char* format = "%.3f") noexcept
{
    auto& st = cd::ui::chroma::state();
    if (st.window_stack.empty() || label == nullptr || v == nullptr) return false;
    auto& w = st.window_stack.back();
    const auto vis = cd::ui::chroma::visible_label(label);
    const ImVec2 lsz = cd::ui::chroma::measure_text_px(vis, 1.28F);
    const float iw = detail::resolve_item_width(w);
    const float ih = 19.0F;
    const ImVec2 p0 = w.cursor;
    const ImVec2 bar_max { p0.x + iw, p0.y + ih };
    const ImVec2 p1 { bar_max.x + (vis.empty() ? 0.0F : (lsz.x + 6.0F)), p0.y + ih };

    const ImGuiID id = GetID(label);
    st.last_item_id = id;
    detail::advance_item(w, p0, p1);

    const ImVec2 mp = st.io.MousePos;
    const bool bar_hov = !detail::is_occluded_by_popup(w) && !w.skip &&
                         detail::is_in_window_clip(w, mp) &&
                         (mp.x >= p0.x && mp.x <= bar_max.x && mp.y >= p0.y && mp.y <= bar_max.y);

    // Double-click or Ctrl+Click inline numeric entry
    if (bar_hov && (st.mouse_double_clicked || (st.mouse_clicked[0] && st.mod_ctrl)))
    {
        if (st.mouse_double_clicked)
        {
            const auto pre_it = st.slider_pre_click_val.find(id);
            if (pre_it != st.slider_pre_click_val.end())
            {
                *v = pre_it->second;
            }
        }
        st.inline_edit_id = id;
        st.inline_edit_select_all = true;
        st.active_id = 0U;
        st.io.WantCaptureKeyboard = true;
        std::snprintf(st.inline_edit_buf, sizeof(st.inline_edit_buf), "%.3f", static_cast<double>(*v));
    }
    if (st.inline_edit_id == id)
    {
        st.io.WantCaptureKeyboard = true;
        bool changed = false;
        std::size_t len = std::strlen(st.inline_edit_buf);
        for (char c : st.input_chars)
        {
            if ((c >= '0' && c <= '9') || c == '.' || c == '-' || c == '+')
            {
                if (st.inline_edit_select_all)
                {
                    st.inline_edit_buf[0] = '\0';
                    len = 0U;
                    st.inline_edit_select_all = false;
                }
                if (len + 1 < sizeof(st.inline_edit_buf))
                {
                    st.inline_edit_buf[len++] = c;
                    st.inline_edit_buf[len]   = '\0';
                }
            }
        }
        if (st.keys_pressed[ImGuiKey_Backspace] && len > 0)
        {
            if (st.inline_edit_select_all)
            {
                st.inline_edit_buf[0] = '\0';
                len = 0U;
                st.inline_edit_select_all = false;
            }
            else
            {
                st.inline_edit_buf[--len] = '\0';
            }
        }
        if (st.keys_pressed[ImGuiKey_Escape])
        {
            st.inline_edit_id = 0U;
            st.inline_edit_select_all = false;
            st.io.WantCaptureKeyboard = (st.focused_input_id != 0U);
        }
        else if (st.keys_pressed[ImGuiKey_Enter] || (!bar_hov && st.mouse_clicked[0]))
        {
            *v = std::clamp(static_cast<float>(std::atof(st.inline_edit_buf)), v_min, v_max);
            st.inline_edit_id = 0U;
            st.inline_edit_select_all = false;
            st.io.WantCaptureKeyboard = (st.focused_input_id != 0U);
            changed = true;
        }
        w.draw_list->AddRectFilled(p0, bar_max, st.theme.frame_bg, 4.0F);
        w.draw_list->AddRect(p0, bar_max, st.theme.accent_bright, 4.0F, 0, 1.5F);
        w.draw_list->PushClipRect(ImVec2(p0.x + 2.0F, p0.y), ImVec2(bar_max.x - 2.0F, bar_max.y));
        w.draw_list->AddText(ImVec2(p0.x + 6.0F, p0.y + 3.0F), st.theme.text, st.inline_edit_buf, nullptr, 1.28F);
        w.draw_list->PopClipRect();
        if (!vis.empty())
            w.draw_list->AddText(ImVec2(bar_max.x + 6.0F, p0.y + 3.0F), st.theme.text, vis.data(), vis.data() + vis.size(), 1.28F);
        return changed;
    }

    if (bar_hov && st.mouse_clicked[0])
    {
        st.slider_pre_click_val[id] = *v;
        st.active_id = id;
    }

    bool changed = false;
    if (st.active_id == id && st.mouse_down[0])
    {
        const float t = std::clamp((mp.x - p0.x) / std::max(1.0F, iw), 0.0F, 1.0F);
        const float nv = v_min + t * (v_max - v_min);
        if (std::fabs(nv - *v) > 1e-6F) { *v = nv; changed = true; }
    }

    const float norm = (v_max > v_min) ? std::clamp((*v - v_min) / (v_max - v_min), 0.0F, 1.0F) : 0.0F;
    w.draw_list->AddRectFilled(p0, bar_max, st.theme.frame_bg, 4.0F);
    if (norm > 0.01F)
    {
        w.draw_list->AddRectFilled(ImVec2(p0.x + 1.0F, p0.y + 1.0F),
                                   ImVec2(p0.x + std::max(6.0F, iw * norm), bar_max.y - 1.0F),
                                   st.theme.accent, 3.0F);
    }
    const float kx = p0.x + norm * (iw - 8.0F);
    w.draw_list->AddRectFilled(ImVec2(kx, p0.y + 1.0F), ImVec2(kx + 8.0F, bar_max.y - 1.0F),
                               st.theme.accent_bright, 3.0F);
    w.draw_list->AddRect(p0, bar_max, bar_hov ? st.theme.accent_bright : st.theme.window_border, 4.0F, 0, 1.0F);

    char vbuf[64] {};
    std::snprintf(vbuf, sizeof(vbuf), format != nullptr ? format : "%.2f", static_cast<double>(*v));
    const std::string_view vsv { vbuf };
    const ImVec2 vsz = cd::ui::chroma::measure_text_px(vsv, 1.25F);
    w.draw_list->PushClipRect(ImVec2(p0.x + 2.0F, p0.y), ImVec2(bar_max.x - 2.0F, bar_max.y));
    w.draw_list->AddText(ImVec2(p0.x + std::max(4.0F, (iw - vsz.x) * 0.5F), p0.y + 3.0F),
                         st.theme.text, vsv.data(), vsv.data() + vsv.size(), 1.25F);
    w.draw_list->PopClipRect();
    if (!vis.empty())
    {
        w.draw_list->AddText(ImVec2(bar_max.x + 6.0F, p0.y + 3.0F),
                             st.theme.text, vis.data(), vis.data() + vis.size(), 1.28F);
    }
    return changed;
}

inline bool SliderInt(const char* label, int* v, int v_min, int v_max, const char* format = "%d") noexcept
{
    if (v == nullptr) return false;
    float fv = static_cast<float>(*v);
    char fmt_buf[32] {};
    std::snprintf(fmt_buf, sizeof(fmt_buf), format != nullptr ? format : "%d", *v);
    if (SliderFloat(label, &fv, static_cast<float>(v_min), static_cast<float>(v_max), "%.0f"))
    {
        *v = std::clamp(static_cast<int>(std::round(fv)), v_min, v_max);
        return true;
    }
    return false;
}

inline bool DragFloat(const char* label, float* v, float v_speed = 0.05F,
                      float v_min = 0.0F, float v_max = 0.0F, const char* format = "%.2f") noexcept
{
    auto& st = cd::ui::chroma::state();
    if (st.window_stack.empty() || label == nullptr || v == nullptr) return false;
    auto& w = st.window_stack.back();
    const auto vis = cd::ui::chroma::visible_label(label);
    const ImVec2 lsz = cd::ui::chroma::measure_text_px(vis, 1.28F);
    const float iw = detail::resolve_item_width(w);
    const float ih = 19.0F;
    const ImVec2 p0 = w.cursor;
    const ImVec2 bmax { p0.x + iw, p0.y + ih };
    const ImGuiID id = GetID(label);
    st.last_item_id = id;
    detail::advance_item(w, p0, ImVec2(bmax.x + (vis.empty() ? 0.0F : (lsz.x + 6.0F)), bmax.y));

    const ImVec2 mp = st.io.MousePos;
    const bool hov = !detail::is_occluded_by_popup(w) && !w.skip &&
                     detail::is_in_window_clip(w, mp) &&
                     (mp.x >= p0.x && mp.x <= bmax.x && mp.y >= p0.y && mp.y <= bmax.y);

    // Double-click or Ctrl+Click inline numeric entry
    if (hov && (st.mouse_double_clicked || (st.mouse_clicked[0] && st.mod_ctrl)))
    {
        st.inline_edit_id = id;
        st.inline_edit_select_all = true;
        st.active_id = 0U;
        st.io.WantCaptureKeyboard = true;
        std::snprintf(st.inline_edit_buf, sizeof(st.inline_edit_buf), "%.3f", static_cast<double>(*v));
    }
    if (st.inline_edit_id == id)
    {
        st.io.WantCaptureKeyboard = true;
        bool changed = false;
        std::size_t len = std::strlen(st.inline_edit_buf);
        for (char c : st.input_chars)
        {
            if ((c >= '0' && c <= '9') || c == '.' || c == '-' || c == '+')
            {
                if (st.inline_edit_select_all)
                {
                    st.inline_edit_buf[0] = '\0';
                    len = 0U;
                    st.inline_edit_select_all = false;
                }
                if (len + 1 < sizeof(st.inline_edit_buf))
                {
                    st.inline_edit_buf[len++] = c;
                    st.inline_edit_buf[len]   = '\0';
                }
            }
        }
        if (st.keys_pressed[ImGuiKey_Backspace] && len > 0)
        {
            if (st.inline_edit_select_all)
            {
                st.inline_edit_buf[0] = '\0';
                len = 0U;
                st.inline_edit_select_all = false;
            }
            else
            {
                st.inline_edit_buf[--len] = '\0';
            }
        }
        if (st.keys_pressed[ImGuiKey_Escape])
        {
            st.inline_edit_id = 0U;
            st.inline_edit_select_all = false;
            st.io.WantCaptureKeyboard = (st.focused_input_id != 0U);
        }
        else if (st.keys_pressed[ImGuiKey_Enter] || (!hov && st.mouse_clicked[0]))
        {
            float nv = static_cast<float>(std::atof(st.inline_edit_buf));
            if (v_max > v_min) nv = std::clamp(nv, v_min, v_max);
            *v = nv;
            st.inline_edit_id = 0U;
            st.inline_edit_select_all = false;
            st.io.WantCaptureKeyboard = (st.focused_input_id != 0U);
            changed = true;
        }
        w.draw_list->AddRectFilled(p0, bmax, st.theme.frame_bg, 3.5F);
        w.draw_list->AddRect(p0, bmax, st.theme.accent_bright, 3.5F, 0, 1.5F);
        w.draw_list->PushClipRect(ImVec2(p0.x + 2.0F, p0.y), ImVec2(bmax.x - 2.0F, bmax.y));
        w.draw_list->AddText(ImVec2(p0.x + 6.0F, p0.y + 3.0F), st.theme.text, st.inline_edit_buf, nullptr, 1.28F);
        w.draw_list->PopClipRect();
        if (!vis.empty())
            w.draw_list->AddText(ImVec2(bmax.x + 6.0F, p0.y + 3.0F), st.theme.text, vis.data(), vis.data() + vis.size(), 1.28F);
        return changed;
    }

    if (hov && st.mouse_clicked[0]) st.active_id = id;
    bool changed = false;
    if (st.active_id == id && st.mouse_down[0] && std::fabs(st.mouse_delta.x) > 0.01F)
    {
        float nv = *v + st.mouse_delta.x * (v_speed > 0.0F ? v_speed : 0.05F);
        if (v_max > v_min) nv = std::clamp(nv, v_min, v_max);
        *v = nv;
        changed = true;
    }
    w.draw_list->AddRectFilled(p0, bmax, hov ? st.theme.frame_hover : st.theme.frame_bg, 3.5F);
    w.draw_list->AddRect(p0, bmax, (st.active_id == id) ? st.theme.accent_bright : st.theme.window_border, 3.5F, 0, 1.0F);
    char buf[48] {};
    std::snprintf(buf, sizeof(buf), format != nullptr ? format : "%.2f", static_cast<double>(*v));
    w.draw_list->PushClipRect(ImVec2(p0.x + 2.0F, p0.y), ImVec2(bmax.x - 2.0F, bmax.y));
    w.draw_list->AddText(ImVec2(p0.x + 6.0F, p0.y + 3.0F), st.theme.text, buf, nullptr, 1.25F);
    w.draw_list->PopClipRect();
    if (!vis.empty())
        w.draw_list->AddText(ImVec2(bmax.x + 6.0F, p0.y + 3.0F), st.theme.text, vis.data(), vis.data() + vis.size(), 1.28F);
    return changed;
}

inline bool DragFloat3(const char* label, float v[3], float v_speed = 0.05F,
                       float v_min = 0.0F, float v_max = 0.0F,
                       const char* format = "%.2f") noexcept
{
    auto& st = cd::ui::chroma::state();
    if (st.window_stack.empty() || label == nullptr || v == nullptr) return false;
    auto& w = st.window_stack.back();
    const auto vis = cd::ui::chroma::visible_label(label);
    const ImVec2 lsz = cd::ui::chroma::measure_text_px(vis, 1.28F);
    const float total_w = detail::resolve_item_width(w);
    const float gap = 4.0F;
    const float box_w = std::max(38.0F, (total_w - gap * 2.0F) / 3.0F);
    const float ih = 19.0F;
    const ImVec2 p0 = w.cursor;
    const ImGuiID base_id = GetID(label);

    static constexpr ImU32 kAxisCols[3] = {
        IM_COL32(230, 65, 65, 255),   // X Red
        IM_COL32(75, 195, 85, 255),   // Y Green
        IM_COL32(55, 140, 245, 255)   // Z Blue
    };

    bool changed = false;
    const ImVec2 mp = st.io.MousePos;
    const bool occluded = detail::is_occluded_by_popup(w) || w.skip || !detail::is_in_window_clip(w, mp);
    for (int i = 0; i < 3; ++i)
    {
        const ImVec2 bmin { p0.x + static_cast<float>(i) * (box_w + gap), p0.y };
        const ImVec2 bmax { bmin.x + box_w, p0.y + ih };
        const ImGuiID cid = base_id + static_cast<ImGuiID>(i + 1);
        const bool hov = !occluded && (mp.x >= bmin.x && mp.x <= bmax.x && mp.y >= bmin.y && mp.y <= bmax.y);

        if (hov && (st.mouse_double_clicked || (st.mouse_clicked[0] && st.mod_ctrl)))
        {
            st.inline_edit_id = cid;
            st.inline_edit_select_all = true;
            st.active_id = 0U;
            st.io.WantCaptureKeyboard = true;
            std::snprintf(st.inline_edit_buf, sizeof(st.inline_edit_buf), "%.3f", static_cast<double>(v[i]));
        }
        if (st.inline_edit_id == cid)
        {
            st.io.WantCaptureKeyboard = true;
            std::size_t len = std::strlen(st.inline_edit_buf);
            for (char c : st.input_chars)
            {
                if ((c >= '0' && c <= '9') || c == '.' || c == '-' || c == '+')
                {
                    if (st.inline_edit_select_all)
                    {
                        st.inline_edit_buf[0] = '\0';
                        len = 0U;
                        st.inline_edit_select_all = false;
                    }
                    if (len + 1 < sizeof(st.inline_edit_buf))
                    {
                        st.inline_edit_buf[len++] = c;
                        st.inline_edit_buf[len]   = '\0';
                    }
                }
            }
            if (st.keys_pressed[ImGuiKey_Backspace] && len > 0)
            {
                if (st.inline_edit_select_all)
                {
                    st.inline_edit_buf[0] = '\0';
                    len = 0U;
                    st.inline_edit_select_all = false;
                }
                else
                {
                    st.inline_edit_buf[--len] = '\0';
                }
            }
            if (st.keys_pressed[ImGuiKey_Escape])
            {
                st.inline_edit_id = 0U;
                st.inline_edit_select_all = false;
                st.io.WantCaptureKeyboard = (st.focused_input_id != 0U);
            }
            else if (st.keys_pressed[ImGuiKey_Enter] || (!hov && st.mouse_clicked[0]))
            {
                float nv = static_cast<float>(std::atof(st.inline_edit_buf));
                if (v_max > v_min) nv = std::clamp(nv, v_min, v_max);
                v[i] = nv;
                st.inline_edit_id = 0U;
                st.inline_edit_select_all = false;
                st.io.WantCaptureKeyboard = (st.focused_input_id != 0U);
                changed = true;
            }
            w.draw_list->AddRectFilled(bmin, bmax, st.theme.frame_bg, 3.0F);
            w.draw_list->AddRectFilled(bmin, ImVec2(bmin.x + 3.5F, bmax.y), kAxisCols[i]);
            w.draw_list->AddRect(bmin, bmax, st.theme.accent_bright, 3.0F, 0, 1.5F);
            w.draw_list->PushClipRect(ImVec2(bmin.x + 4.0F, bmin.y), ImVec2(bmax.x - 2.0F, bmax.y));
            w.draw_list->AddText(ImVec2(bmin.x + 6.0F, bmin.y + 3.0F), st.theme.text, st.inline_edit_buf, nullptr, 1.25F);
            w.draw_list->PopClipRect();
            continue;
        }

        if (hov && st.mouse_clicked[0]) st.active_id = cid;
        if (st.active_id == cid && st.mouse_down[0] && std::fabs(st.mouse_delta.x) > 0.01F)
        {
            float nv = v[i] + st.mouse_delta.x * (v_speed > 0.0F ? v_speed : 0.05F);
            if (v_max > v_min) nv = std::clamp(nv, v_min, v_max);
            v[i] = nv;
            changed = true;
        }
        w.draw_list->AddRectFilled(bmin, bmax, hov ? st.theme.frame_hover : st.theme.frame_bg, 3.0F);
        w.draw_list->AddRectFilled(bmin, ImVec2(bmin.x + 3.5F, bmax.y), kAxisCols[i]);
        w.draw_list->AddRect(bmin, bmax, (st.active_id == cid) ? st.theme.accent_bright : st.theme.window_border, 3.0F, 0, 1.0F);

        char buf[32] {};
        std::snprintf(buf, sizeof(buf), format != nullptr ? format : "%.2f", static_cast<double>(v[i]));
        w.draw_list->PushClipRect(ImVec2(bmin.x + 4.0F, bmin.y), ImVec2(bmax.x - 2.0F, bmax.y));
        w.draw_list->AddText(ImVec2(bmin.x + 6.0F, bmin.y + 3.0F), st.theme.text, buf, nullptr, 1.25F);
        w.draw_list->PopClipRect();
    }

    const ImVec2 p1 { p0.x + total_w + (vis.empty() ? 0.0F : (lsz.x + 6.0F)), p0.y + ih };
    if (!vis.empty())
    {
        w.draw_list->AddText(ImVec2(p0.x + total_w + 6.0F, p0.y + 3.0F),
                             st.theme.text, vis.data(), vis.data() + vis.size(), 1.28F);
    }
    st.last_item_id = base_id;
    detail::advance_item(w, p0, p1);
    return changed;
}

inline bool ColorEdit3(const char* label, float col[3]) noexcept
{
    auto& st = cd::ui::chroma::state();
    if (st.window_stack.empty() || label == nullptr || col == nullptr) return false;
    auto& w = st.window_stack.back();
    const auto vis = cd::ui::chroma::visible_label(label);
    const ImVec2 lsz = cd::ui::chroma::measure_text_px(vis, 1.28F);
    const float total_w = detail::resolve_item_width(w);
    const float swatch_w = 22.0F;
    const float gap = 4.0F;
    const float box_w = std::max(32.0F, (total_w - swatch_w - gap * 3.0F) / 3.0F);
    const float ih = 19.0F;
    const ImVec2 p0 = w.cursor;
    const ImGuiID base_id = GetID(label);

    bool changed = false;
    const ImVec2 mp = st.io.MousePos;
    const bool occluded = detail::is_occluded_by_popup(w) || w.skip || !detail::is_in_window_clip(w, mp);
    for (int i = 0; i < 3; ++i)
    {
        const ImVec2 bmin { p0.x + static_cast<float>(i) * (box_w + gap), p0.y };
        const ImVec2 bmax { bmin.x + box_w, p0.y + ih };
        const ImGuiID cid = base_id + static_cast<ImGuiID>(i + 10);
        const bool hov = !occluded && (mp.x >= bmin.x && mp.x <= bmax.x && mp.y >= bmin.y && mp.y <= bmax.y);
        if (hov && st.mouse_clicked[0]) st.active_id = cid;
        if (st.active_id == cid && st.mouse_down[0] && std::fabs(st.mouse_delta.x) > 0.01F)
        {
            col[i] = std::clamp(col[i] + st.mouse_delta.x * 0.005F, 0.0F, 1.0F);
            changed = true;
        }
        w.draw_list->AddRectFilled(bmin, bmax, st.theme.frame_bg, 3.0F);
        w.draw_list->AddRect(bmin, bmax, st.theme.window_border, 3.0F, 0, 1.0F);
        char buf[24] {};
        std::snprintf(buf, sizeof(buf), "%.2f", static_cast<double>(col[i]));
        w.draw_list->PushClipRect(ImVec2(bmin.x + 2.0F, bmin.y), ImVec2(bmax.x - 2.0F, bmax.y));
        w.draw_list->AddText(ImVec2(bmin.x + 4.0F, bmin.y + 3.0F), st.theme.text, buf, nullptr, 1.25F);
        w.draw_list->PopClipRect();
    }

    // Live color swatch preview (clickable to open HSV Color Picker popup!)
    const ImVec2 smin { p0.x + 3.0F * (box_w + gap), p0.y };
    const ImVec2 smax { smin.x + swatch_w, p0.y + ih };
    const bool s_hov = !occluded && (mp.x >= smin.x && mp.x <= smax.x && mp.y >= smin.y && mp.y <= smax.y);
    if (s_hov && st.mouse_clicked[0])
    {
        st.open_popup_id = (st.open_popup_id == (base_id ^ 0xC010B000U)) ? 0U : (base_id ^ 0xC010B000U);
        const float pop_x = std::clamp(smin.x, 6.0F, std::max(6.0F, st.io.DisplaySize.x - 202.0F));
        const float pop_y = std::clamp(smax.y + 4.0F, 6.0F, std::max(6.0F, st.io.DisplaySize.y - 156.0F));
        st.open_popup_pos = ImVec2(pop_x, pop_y);
        st.active_menu_name.clear();
        st.active_submenu_name.clear();
        st.popup_opened_this_frame = true;
        st.mouse_clicked[0] = false;
    }

    if (st.open_popup_id == (base_id ^ 0xC010B000U))
    {
        const ImVec2 pp0 = st.open_popup_pos;
        const ImVec2 pp1 { pp0.x + 196.0F, pp0.y + 150.0F };
        st.last_popup_min = pp0;
        st.last_popup_max = pp1;
        const bool in_pop = (mp.x >= pp0.x && mp.x <= pp1.x && mp.y >= pp0.y && mp.y <= pp1.y);
        if (in_pop) st.any_item_hovered = true;
        st.popup_dl.AddShadow(pp0, pp1, 12.0F, 2.0F, IM_COL32(0, 0, 0, 190));
        st.popup_dl.AddRectFilled(pp0, pp1, st.theme.popup_bg, 6.0F);
        st.popup_dl.AddRect(pp0, pp1, st.theme.accent_bright, 6.0F, 0, 1.2F);
        st.popup_dl.AddText(ImVec2(pp0.x + 8.0F, pp0.y + 6.0F), st.theme.accent_bright, "Chroma Color Picker", nullptr, 1.25F);
        for (int c = 0; c < 3; ++c)
        {
            const float ry = pp0.y + 28.0F + static_cast<float>(c) * 28.0F;
            const ImVec2 b0 { pp0.x + 24.0F, ry };
            const ImVec2 b1 { pp1.x - 10.0F, ry + 18.0F };
            const ImGuiID c_id = base_id + static_cast<ImGuiID>(c + 20);
            const bool c_hov = (mp.x >= b0.x && mp.x <= b1.x && mp.y >= b0.y && mp.y <= b1.y);
            if (c_hov && st.mouse_clicked[0])
            {
                st.active_id = c_id;
                st.mouse_clicked[0] = false;
            }
            if (st.active_id == c_id && st.mouse_down[0])
            {
                col[c] = std::clamp((mp.x - b0.x) / std::max(1.0F, b1.x - b0.x), 0.0F, 1.0F);
                changed = true;
            }
            static constexpr const char* kChan[3] = { "R", "G", "B" };
            static constexpr ImU32 kCCol[3] = { IM_COL32(235, 70, 70, 255), IM_COL32(75, 210, 95, 255), IM_COL32(65, 145, 250, 255) };
            st.popup_dl.AddText(ImVec2(pp0.x + 8.0F, ry + 2.0F), kCCol[c], kChan[c], nullptr, 1.28F);
            st.popup_dl.AddGradient(b0, b1, IM_COL32(15, 18, 24, 255), kCCol[c], true);
            st.popup_dl.AddRect(b0, b1, (c_hov || st.active_id == c_id) ? st.theme.accent_bright : st.theme.window_border, 3.0F, 0, 1.0F);
            const float kx = b0.x + std::clamp(col[c], 0.0F, 1.0F) * (b1.x - b0.x - 6.0F);
            st.popup_dl.AddRectFilled(ImVec2(kx, b0.y), ImVec2(kx + 6.0F, b1.y), IM_COL32(255, 255, 255, 255), 2.0F);
        }
        if (in_pop && st.mouse_clicked[0])
        {
            st.mouse_clicked[0] = false;
        }
        const ImU32 pop_swatch_col = cd::ui::chroma::pack_vec4(ImVec4(col[0], col[1], col[2], 1.0F));
        char hex_buf[32] {};
        std::snprintf(hex_buf, sizeof(hex_buf), "HEX #%02X%02X%02X",
                      static_cast<int>(std::clamp(col[0], 0.0F, 1.0F) * 255.0F),
                      static_cast<int>(std::clamp(col[1], 0.0F, 1.0F) * 255.0F),
                      static_cast<int>(std::clamp(col[2], 0.0F, 1.0F) * 255.0F));
        st.popup_dl.AddRectFilled(ImVec2(pp0.x + 8.0F, pp1.y - 30.0F), ImVec2(pp0.x + 56.0F, pp1.y - 8.0F), pop_swatch_col, 4.0F);
        st.popup_dl.AddText(ImVec2(pp0.x + 64.0F, pp1.y - 25.0F), st.theme.text, hex_buf, nullptr, 1.25F);
    }

    const ImU32 swatch_col = cd::ui::chroma::pack_vec4(ImVec4(col[0], col[1], col[2], 1.0F));
    w.draw_list->AddRectFilled(smin, smax, swatch_col, 4.0F);
    w.draw_list->AddRect(smin, smax, s_hov ? st.theme.accent_bright : IM_COL32(255, 255, 255, 180), 4.0F, 0, 1.0F);

    if (!vis.empty())
    {
        w.draw_list->AddText(ImVec2(smax.x + 6.0F, p0.y + 3.0F),
                             st.theme.text, vis.data(), vis.data() + vis.size(), 1.28F);
    }
    detail::advance_item(w, p0, ImVec2(smax.x + (vis.empty() ? 0.0F : (6.0F + lsz.x)), p0.y + ih));
    return changed;
}

inline bool Combo(const char* label, int* current_item, const char* const items[], int items_count) noexcept
{
    auto& st = cd::ui::chroma::state();
    if (st.window_stack.empty() || label == nullptr || current_item == nullptr || items == nullptr || items_count <= 0)
        return false;
    auto& w = st.window_stack.back();
    const auto vis = cd::ui::chroma::visible_label(label);
    const ImVec2 lsz = cd::ui::chroma::measure_text_px(vis, 1.28F);
    const float iw = detail::resolve_item_width(w);
    const float ih = 20.0F;
    const ImVec2 p0 = w.cursor;
    const ImVec2 bmax { p0.x + iw, p0.y + ih };
    const ImGuiID id = GetID(label);
    st.last_item_id = id;
    detail::advance_item(w, p0, ImVec2(bmax.x + (vis.empty() ? 0.0F : (lsz.x + 6.0F)), bmax.y));

    const ImVec2 mp = st.io.MousePos;
    const bool hov = !detail::is_occluded_by_popup(w) && !w.skip &&
                     detail::is_in_window_clip(w, mp) &&
                     (mp.x >= p0.x && mp.x <= bmax.x && mp.y >= p0.y && mp.y <= bmax.y);
    if (hov && st.mouse_clicked[0])
    {
        st.open_popup_id  = (st.open_popup_id == id) ? 0U : id;
        st.open_popup_pos = ImVec2 { p0.x, bmax.y + 2.0F };
        st.active_menu_name.clear();
        st.active_submenu_name.clear();
        st.popup_opened_this_frame = true;
        st.mouse_clicked[0] = false;
    }

    w.draw_list->AddRectFilled(p0, bmax, hov ? st.theme.frame_hover : st.theme.frame_bg, 4.0F);
    w.draw_list->AddRect(p0, bmax, hov ? st.theme.accent : st.theme.window_border, 4.0F, 0, 1.0F);
    const int cur = std::clamp(*current_item, 0, items_count - 1);

    // Clip selected item text to the left of the dropdown chevron so it never collides!
    w.draw_list->PushClipRect(ImVec2(p0.x + 4.0F, p0.y + 1.0F), ImVec2(bmax.x - 20.0F, bmax.y - 1.0F));
    w.draw_list->AddText(ImVec2(p0.x + 7.0F, p0.y + 4.0F), st.theme.text, items[cur], nullptr, 1.28F);
    w.draw_list->PopClipRect();

    cd::ui::chroma::DrawIcon(w.draw_list, cd::ui::chroma::IconKind::kChevronDown,
                             ImVec2(bmax.x - 10.0F, p0.y + ih * 0.5F), 4.5F, st.theme.accent_bright);
    if (!vis.empty())
    {
        w.draw_list->AddText(ImVec2(bmax.x + 6.0F, p0.y + 4.0F), st.theme.text, vis.data(), vis.data() + vis.size(), 1.28F);
    }

    bool changed = false;
    if (st.open_popup_id == id)
    {
        float max_item_w = iw;
        for (int i = 0; i < items_count; ++i)
        {
            const ImVec2 isz = cd::ui::chroma::measure_text_px(items[i] != nullptr ? items[i] : "", 1.28F);
            max_item_w = std::max(max_item_w, isz.x + 24.0F);
        }
        const float ph = static_cast<float>(items_count) * 21.0F + 8.0F;
        ImVec2 pp0 = st.open_popup_pos;
        if (st.io.DisplaySize.x > max_item_w + 12.0F)
            pp0.x = std::clamp(pp0.x, 6.0F, st.io.DisplaySize.x - max_item_w - 6.0F);
        if (st.io.DisplaySize.y > ph + 12.0F)
            pp0.y = std::clamp(pp0.y, 6.0F, st.io.DisplaySize.y - ph - 6.0F);
        const ImVec2 pp1 { pp0.x + max_item_w, pp0.y + ph };
        st.last_popup_min = pp0;
        st.last_popup_max = pp1;
        st.popup_dl.AddShadow(pp0, pp1, 10.0F, 2.0F, IM_COL32(0, 0, 0, 180));
        st.popup_dl.AddRectFilled(pp0, pp1, st.theme.popup_bg, 5.0F);
        st.popup_dl.AddRect(pp0, pp1, st.theme.accent, 5.0F, 0, 1.0F);
        for (int i = 0; i < items_count; ++i)
        {
            const ImVec2 r0 { pp0.x + 4.0F, pp0.y + 4.0F + static_cast<float>(i) * 21.0F };
            const ImVec2 r1 { pp1.x - 4.0F, r0.y + 19.0F };
            const bool rhov = (mp.x >= r0.x && mp.x <= r1.x && mp.y >= r0.y && mp.y <= r1.y);
            if (rhov) st.any_item_hovered = true;
            if (rhov || i == cur)
            {
                st.popup_dl.AddRectFilled(r0, r1, rhov ? st.theme.button_hover : st.theme.button, 3.0F);
            }
            st.popup_dl.AddText(ImVec2(r0.x + 6.0F, r0.y + 3.0F), st.theme.text, items[i], nullptr, 1.28F);
            if (rhov && st.mouse_clicked[0])
            {
                *current_item = i;
                changed = true;
                st.open_popup_id = 0U;
                st.mouse_clicked[0] = false;
            }
        }
    }
    return changed;
}

inline bool BeginCombo(const char* label, const char* preview_value) noexcept
{
    auto& st = cd::ui::chroma::state();
    if (st.window_stack.empty() || label == nullptr) return false;
    auto& w = st.window_stack.back();
    const auto vis = cd::ui::chroma::visible_label(label);
    const ImVec2 lsz = cd::ui::chroma::measure_text_px(vis, 1.28F);
    const float iw = detail::resolve_item_width(w);
    const float ih = 20.0F;
    const ImVec2 p0 = w.cursor;
    const ImVec2 bmax { p0.x + iw, p0.y + ih };
    const ImGuiID id = GetID(label);
    st.last_item_id = id;
    detail::advance_item(w, p0, ImVec2(bmax.x + (vis.empty() ? 0.0F : (lsz.x + 6.0F)), bmax.y));

    const ImVec2 mp = st.io.MousePos;
    const bool hov = !detail::is_occluded_by_popup(w) && !w.skip &&
                     detail::is_in_window_clip(w, mp) &&
                     (mp.x >= p0.x && mp.x <= bmax.x && mp.y >= p0.y && mp.y <= bmax.y);
    if (hov && st.mouse_clicked[0])
    {
        st.open_popup_id  = (st.open_popup_id == id) ? 0U : id;
        st.open_popup_pos = ImVec2 { p0.x, bmax.y + 2.0F };
        st.active_menu_name.clear();
        st.active_submenu_name.clear();
        st.popup_opened_this_frame = true;
        st.mouse_clicked[0] = false;
    }

    w.draw_list->AddRectFilled(p0, bmax, hov ? st.theme.frame_hover : st.theme.frame_bg, 4.0F);
    w.draw_list->AddRect(p0, bmax, hov ? st.theme.accent : st.theme.window_border, 4.0F, 0, 1.0F);
    if (preview_value != nullptr && preview_value[0] != '\0')
    {
        w.draw_list->PushClipRect(ImVec2(p0.x + 4.0F, p0.y + 1.0F), ImVec2(bmax.x - 20.0F, bmax.y - 1.0F));
        w.draw_list->AddText(ImVec2(p0.x + 7.0F, p0.y + 4.0F), st.theme.text, preview_value, nullptr, 1.28F);
        w.draw_list->PopClipRect();
    }
    cd::ui::chroma::DrawIcon(w.draw_list, cd::ui::chroma::IconKind::kChevronDown,
                             ImVec2(bmax.x - 10.0F, p0.y + ih * 0.5F), 4.5F, st.theme.accent_bright);
    if (!vis.empty())
    {
        w.draw_list->AddText(ImVec2(bmax.x + 6.0F, p0.y + 4.0F), st.theme.text, vis.data(), vis.data() + vis.size(), 1.28F);
    }

    if (st.open_popup_id != id) return false;
    auto sz_it = st.auto_size_map.find(id);
    const ImVec2 pop_sz = (sz_it != st.auto_size_map.end()) ? sz_it->second : ImVec2 { std::max(iw, 165.0F), 150.0F };
    cd::ui::chroma::WindowLayout pw {};
    pw.name            = std::string("##combo_") + label;
    pw.id              = id;
    pw.is_popup        = true;
    pw.pos             = ImVec2(std::clamp(st.open_popup_pos.x, 6.0F, std::max(6.0F, st.io.DisplaySize.x - pop_sz.x - 6.0F)),
                                std::clamp(st.open_popup_pos.y, 6.0F, std::max(6.0F, st.io.DisplaySize.y - pop_sz.y - 6.0F)));
    pw.size            = pop_sz;
    pw.cursor          = ImVec2 { pw.pos.x + 6.0F, pw.pos.y + 6.0F };
    pw.content_max_x   = pw.cursor.x;
    pw.content_max_y   = pw.cursor.y;
    pw.popup_cmd_start = st.popup_dl.cmd_count();
    pw.draw_list       = &st.popup_dl;
    st.last_popup_min = pw.pos;
    st.last_popup_max = ImVec2(pw.pos.x + pw.size.x, pw.pos.y + pw.size.y);
    st.popup_dl.AddShadow(pw.pos, st.last_popup_max, 10.0F, 2.0F, IM_COL32(0, 0, 0, 180));
    st.popup_dl.AddRectFilled(pw.pos, st.last_popup_max, st.theme.popup_bg, 5.0F);
    st.popup_dl.AddRect(pw.pos, st.last_popup_max, st.theme.accent, 5.0F, 0, 1.0F);
    st.window_stack.push_back(std::move(pw));
    return true;
}

inline void EndCombo() noexcept
{
    auto& st = cd::ui::chroma::state();
    if (!st.window_stack.empty() && st.window_stack.back().is_popup)
    {
        auto pw = std::move(st.window_stack.back());
        st.window_stack.pop_back();
        const ImGuiID id = (pw.id != 0U) ? pw.id : GetID(pw.name.c_str());
        const ImVec2 final_sz(std::clamp(pw.content_max_x - pw.pos.x + 14.0F, 165.0F, 420.0F),
                              std::clamp(pw.content_max_y - pw.pos.y + 8.0F, 34.0F, 460.0F));
        st.auto_size_map[id] = final_sz;
        detail::finalize_popup_layout(pw, final_sz);
    }
}

inline bool InputTextWithHint(const char* label, const char* hint, char* buf, std::size_t buf_size,
                              ImGuiInputTextFlags /*flags*/ = 0) noexcept
{
    auto& st = cd::ui::chroma::state();
    if (st.window_stack.empty() || label == nullptr || buf == nullptr || buf_size == 0) return false;
    auto& w = st.window_stack.back();
    const auto vis = cd::ui::chroma::visible_label(label);
    const ImVec2 lsz = cd::ui::chroma::measure_text_px(vis, 1.28F);
    const float iw = detail::resolve_item_width(w);
    const float ih = 21.0F;
    const ImVec2 p0 = w.cursor;
    const ImVec2 bmax { p0.x + iw, p0.y + ih };
    const ImGuiID id = GetID(label);
    st.last_item_id = id;
    detail::advance_item(w, p0, ImVec2(bmax.x + (vis.empty() ? 0.0F : (lsz.x + 6.0F)), bmax.y));

    if (st.focus_next_input)
    {
        st.focused_input_id = id;
        st.input_caret_pos  = std::strlen(buf);
        st.focus_next_input = false;
    }
    if (st.last_item_hovered && st.mouse_clicked[0])
    {
        st.focused_input_id = id;
        st.input_caret_pos  = std::strlen(buf);
    }
    else if (!st.last_item_hovered && st.mouse_clicked[0] && st.focused_input_id == id)
    {
        st.focused_input_id = 0U;
    }
    if (st.focused_input_id == id && st.keys_pressed[ImGuiKey_Escape])
    {
        st.focused_input_id = 0U;
    }
    st.io.WantCaptureKeyboard = (st.focused_input_id != 0U || st.inline_edit_id != 0U);

    bool changed = false;
    const bool focused = (st.focused_input_id == id);
    std::size_t len = std::strlen(buf);
    if (focused)
    {
        st.input_caret_pos = std::min(st.input_caret_pos, len);
        if (st.keys_pressed[ImGuiKey_LeftArrow] && st.input_caret_pos > 0U) --st.input_caret_pos;
        if (st.keys_pressed[ImGuiKey_RightArrow] && st.input_caret_pos < len) ++st.input_caret_pos;
        if (st.keys_pressed[ImGuiKey_Home]) st.input_caret_pos = 0U;
        if (st.keys_pressed[ImGuiKey_End])  st.input_caret_pos = len;

        for (char c : st.input_chars)
        {
            if (len + 1 < buf_size)
            {
                std::memmove(buf + st.input_caret_pos + 1, buf + st.input_caret_pos, len - st.input_caret_pos + 1);
                buf[st.input_caret_pos++] = c;
                ++len;
                changed = true;
            }
        }
        if (st.keys_pressed[ImGuiKey_Backspace] && st.input_caret_pos > 0U && len > 0U)
        {
            std::memmove(buf + st.input_caret_pos - 1, buf + st.input_caret_pos, len - st.input_caret_pos + 1);
            --st.input_caret_pos;
            --len;
            changed = true;
        }
        if (st.keys_pressed[ImGuiKey_Delete] && st.input_caret_pos < len)
        {
            std::memmove(buf + st.input_caret_pos, buf + st.input_caret_pos + 1, len - st.input_caret_pos);
            --len;
            changed = true;
        }
    }

    w.draw_list->AddRectFilled(p0, bmax, st.theme.frame_bg, 4.0F);
    w.draw_list->AddRect(p0, bmax, focused ? st.theme.accent_bright : st.theme.window_border, 4.0F, 0, focused ? 1.5F : 1.0F);

    // Strictly clip text and blinking caret inside the input box so long text never spills over the right label!
    const float inner_w = std::max(10.0F, (bmax.x - 6.0F) - (p0.x + 6.0F));
    const std::string_view full_sv(buf, len);
    const std::string_view pre_sv(buf, focused ? std::min(st.input_caret_pos, len) : len);
    const float pre_w = cd::ui::chroma::measure_text_px(pre_sv, 1.28F).x;
    const float scroll_x = (pre_w > inner_w - 4.0F) ? (pre_w - (inner_w - 4.0F)) : 0.0F;

    w.draw_list->PushClipRect(ImVec2(p0.x + 4.0F, p0.y + 2.0F), ImVec2(bmax.x - 4.0F, bmax.y - 2.0F));
    if (buf[0] != '\0')
    {
        w.draw_list->AddText(ImVec2(p0.x + 6.0F - scroll_x, p0.y + 4.0F), st.theme.text,
                             full_sv.data(), full_sv.data() + full_sv.size(), 1.28F);
    }
    else if (hint != nullptr && hint[0] != '\0')
    {
        w.draw_list->AddText(ImVec2(p0.x + 6.0F, p0.y + 4.0F), st.theme.text_disabled, hint, nullptr, 1.28F);
    }
    if (focused && std::fmod(st.anim_time, 1.0F) < 0.55F)
    {
        const float cx = p0.x + 6.0F + pre_w - scroll_x;
        w.draw_list->AddRectFilled(ImVec2(cx, p0.y + 3.0F), ImVec2(cx + 1.5F, bmax.y - 3.0F), st.theme.accent_bright);
    }
    w.draw_list->PopClipRect();

    if (!vis.empty())
    {
        w.draw_list->AddText(ImVec2(bmax.x + 6.0F, p0.y + 4.0F), st.theme.text, vis.data(), vis.data() + vis.size(), 1.28F);
    }
    return changed;
}

inline bool InputText(const char* label, char* buf, std::size_t buf_size, ImGuiInputTextFlags flags = 0) noexcept
{
    return InputTextWithHint(label, "", buf, buf_size, flags);
}

// -----------------------------------------------------------------------------
// TabBar & Tables
// -----------------------------------------------------------------------------
inline bool BeginTabBar(const char* str_id) noexcept
{
    auto& st = cd::ui::chroma::state();
    if (st.window_stack.empty()) return false;
    auto& w = st.window_stack.back();
    const ImGuiID id = GetID(str_id != nullptr ? str_id : "tabs");
    st.tab_bar_stack.push_back(id);
    st.tab_bar_col_cursor[id] = 0;
    st.tab_bar_pos[id]        = w.cursor;
    w.prev_item_min = w.cursor;
    w.prev_item_max = ImVec2 { w.cursor.x, w.cursor.y + 23.0F };
    w.cursor.y += 26.0F;
    return true;
}

inline void EndTabBar() noexcept
{
    auto& st = cd::ui::chroma::state();
    if (!st.tab_bar_stack.empty()) st.tab_bar_stack.pop_back();
}

inline bool BeginTabItem(const char* label) noexcept
{
    auto& st = cd::ui::chroma::state();
    if (st.window_stack.empty() || st.tab_bar_stack.empty() || label == nullptr) return false;
    auto& w = st.window_stack.back();
    const ImGuiID bar_id = st.tab_bar_stack.back();
    const int idx = st.tab_bar_col_cursor[bar_id]++;

    const auto vis = cd::ui::chroma::visible_label(label);
    const ImVec2 tsz = cd::ui::chroma::measure_text_px(vis, 1.25F);
    const float tab_w = std::max(54.0F, tsz.x + 16.0F);
    ImVec2& tpos = st.tab_bar_pos[bar_id];
    const ImVec2 tmin = tpos;
    const ImVec2 tmax { tmin.x + tab_w, tmin.y + 22.0F };
    tpos.x = tmax.x + 3.0F;

    const ImVec2 mp = st.io.MousePos;
    const bool hov = !detail::is_occluded_by_popup(w) && !w.skip &&
                     detail::is_in_window_clip(w, mp) &&
                     (mp.x >= tmin.x && mp.x <= tmax.x && mp.y >= tmin.y && mp.y <= tmax.y);
    if (hov)
    {
        st.any_item_hovered = true;
        if (st.mouse_clicked[0]) st.tab_map[bar_id] = idx;
    }

    const bool is_active = (st.tab_map[bar_id] == idx);
    w.draw_list->AddRectFilled(tmin, tmax, is_active ? st.theme.button_hover : st.theme.button, 4.0F);
    if (is_active)
    {
        w.draw_list->AddRectFilled(ImVec2(tmin.x + 2.0F, tmax.y - 2.5F),
                                   ImVec2(tmax.x - 2.0F, tmax.y), st.theme.accent_bright);
    }
    w.draw_list->AddText(ImVec2(tmin.x + 7.0F, tmin.y + 5.0F),
                         is_active ? st.theme.text : st.theme.text_disabled,
                         vis.data(), vis.data() + vis.size(), 1.25F);
    return is_active;
}

inline void EndTabItem() noexcept {}

inline bool BeginTable(const char* /*str_id*/, int column, ImGuiTableFlags /*flags*/ = 0) noexcept
{
    auto& st = cd::ui::chroma::state();
    if (st.window_stack.empty() || column <= 0) return false;
    auto& w = st.window_stack.back();
    cd::ui::chroma::TableState ts {};
    ts.cols      = column;
    ts.col_idx   = -1;
    ts.start_x   = w.cursor.x;
    ts.width     = std::max(60.0F, (w.pos.x + w.size.x - 10.0F) - w.cursor.x);
    ts.row_y     = w.cursor.y;
    ts.row_max_y = w.cursor.y;
    st.table_stack.push_back(ts);
    return true;
}

inline void TableNextColumn() noexcept
{
    auto& st = cd::ui::chroma::state();
    if (st.window_stack.empty() || st.table_stack.empty()) return;
    auto& w  = st.window_stack.back();
    auto& ts = st.table_stack.back();
    ts.row_max_y = std::max(ts.row_max_y, w.cursor.y);
    ++ts.col_idx;
    if (ts.col_idx >= ts.cols)
    {
        ts.col_idx = 0;
        ts.row_y   = ts.row_max_y + 2.0F;
    }
    const float col_w = ts.width / static_cast<float>(ts.cols);
    w.cursor.x = ts.start_x + static_cast<float>(ts.col_idx) * col_w;
    w.cursor.y = ts.row_y;
}

inline void EndTable() noexcept
{
    auto& st = cd::ui::chroma::state();
    if (st.window_stack.empty() || st.table_stack.empty()) return;
    auto& w  = st.window_stack.back();
    auto  ts = st.table_stack.back();
    st.table_stack.pop_back();
    w.cursor.x = ts.start_x;
    w.cursor.y = std::max(ts.row_max_y, w.cursor.y) + 3.0F;
    w.content_max_y = std::max(w.content_max_y, w.cursor.y);
}

// -----------------------------------------------------------------------------
// MainMenuBar, Menus (with nested Submenus), Popups & Tooltips
// -----------------------------------------------------------------------------
inline bool BeginMainMenuBar() noexcept
{
    auto& st = cd::ui::chroma::state();
    const float W = st.io.DisplaySize.x;
    st.win_dl.AddGradient(ImVec2(0.0F, 0.0F), ImVec2(W, 28.0F),
                          st.theme.window_header0, st.theme.window_header1, false);
    st.win_dl.AddRectFilled(ImVec2(0.0F, 27.0F), ImVec2(W, 28.0F), st.theme.window_border);

    cd::ui::chroma::WindowLayout wl {};
    wl.name      = "##MainMenuBar";
    wl.pos       = ImVec2 { 0.0F, 0.0F };
    wl.size      = ImVec2 { W, 28.0F };
    wl.cursor    = ImVec2 { 10.0F, 4.0F };
    wl.draw_list = &st.win_dl;
    st.window_stack.push_back(std::move(wl));
    st.in_menu_bar = true;
    st.menu_bar_cursor_x = 10.0F;

    if (st.io.MousePos.y <= 28.0F) st.mouse_in_any_window = true;
    return true;
}

inline void EndMainMenuBar() noexcept
{
    auto& st = cd::ui::chroma::state();
    st.in_menu_bar = false;
    if (!st.window_stack.empty()) st.window_stack.pop_back();
}

inline bool BeginMenu(const char* label, bool enabled = true) noexcept
{
    auto& st = cd::ui::chroma::state();
    if (st.window_stack.empty() || label == nullptr) return false;
    auto& w = st.window_stack.back();
    const auto vis = cd::ui::chroma::visible_label(label);
    const ImVec2 tsz = cd::ui::chroma::measure_text_px(vis, 1.28F);

    // Nested submenu inside an already-open top-level menu OR inside any Popup (BeginPopup / BeginPopupContextItem)
    if ((st.in_open_menu || w.is_popup) && !st.in_open_submenu)
    {
        const float rw = std::max(w.size.x - 16.0F, tsz.x + 28.0F);
        const ImVec2 p0 = w.cursor;
        const ImVec2 p1 { p0.x + rw, p0.y + 20.0F };
        detail::advance_item(w, p0, p1);
        const bool hov = st.last_item_hovered && enabled;
        if (hov)
        {
            st.active_submenu_name = std::string(vis);
        }
        const bool sub_open = (st.active_submenu_name == vis) && enabled;
        if (hov || sub_open)
        {
            w.draw_list->AddRectFilled(p0, p1, st.theme.button_hover, 3.0F);
        }
        w.draw_list->AddText(ImVec2(p0.x + 6.0F, p0.y + 3.0F),
                             enabled ? st.theme.text : st.theme.text_disabled,
                             vis.data(), vis.data() + vis.size(), 1.28F);
        cd::ui::chroma::DrawIcon(w.draw_list, cd::ui::chroma::IconKind::kChevronRight,
                                 ImVec2(std::max(p1.x - 8.0F, w.pos.x + w.size.x - 10.0F), p0.y + 10.0F),
                                 4.0F, st.theme.accent_bright);
        w.draw_list->mark_last_cmd_right_aligned();
        if (sub_open)
        {
            const ImGuiID sub_id = cd::ui::chroma::hash_str(std::string("##sub_") + std::string(vis));
            auto sz_it = st.auto_size_map.find(sub_id);
            const ImVec2 pop_sz = (sz_it != st.auto_size_map.end()) ? sz_it->second : ImVec2 { 280.0F, 160.0F };
            float sub_x = w.pos.x + w.size.x - 2.0F;
            if (sub_x + pop_sz.x > st.io.DisplaySize.x - 6.0F)
            {
                sub_x = std::max(6.0F, w.pos.x - pop_sz.x + 2.0F);
            }
            const float sub_y = std::clamp(p0.y, 6.0F, std::max(6.0F, st.io.DisplaySize.y - pop_sz.y - 6.0F));
            cd::ui::chroma::WindowLayout spw {};
            spw.name            = std::string("##sub_") + std::string(vis);
            spw.is_popup        = true;
            spw.pos             = ImVec2 { sub_x, sub_y };
            spw.size            = pop_sz;
            spw.cursor          = ImVec2 { spw.pos.x + 8.0F, spw.pos.y + 6.0F };
            spw.content_max_x   = spw.cursor.x;
            spw.content_max_y   = spw.cursor.y;
            spw.popup_cmd_start = st.popup_dl.cmd_count();
            spw.draw_list       = &st.popup_dl;
            st.last_popup_min = ImVec2(std::min(st.last_popup_min.x, spw.pos.x), std::min(st.last_popup_min.y, spw.pos.y));
            st.last_popup_max = ImVec2(std::max(st.last_popup_max.x, spw.pos.x + spw.size.x), std::max(st.last_popup_max.y, spw.pos.y + spw.size.y));
            st.popup_dl.AddShadow(spw.pos, ImVec2(spw.pos.x + spw.size.x, spw.pos.y + spw.size.y), 10.0F, 2.0F, IM_COL32(0, 0, 0, 185));
            st.popup_dl.AddRectFilled(spw.pos, ImVec2(spw.pos.x + spw.size.x, spw.pos.y + spw.size.y), st.theme.popup_bg, 5.0F);
            st.popup_dl.AddRect(spw.pos, ImVec2(spw.pos.x + spw.size.x, spw.pos.y + spw.size.y), st.theme.accent, 5.0F, 0, 1.0F);
            st.window_stack.push_back(std::move(spw));
            st.in_open_submenu = true;
            return true;
        }
        return false;
    }

    const float mw = tsz.x + 16.0F;
    const ImVec2 p0 = w.cursor;
    const ImVec2 p1 { p0.x + mw, p0.y + 20.0F };

    const ImVec2 mp = st.io.MousePos;
    const bool hov = (mp.x >= p0.x && mp.x <= p1.x && mp.y >= p0.y && mp.y <= p1.y);
    if (hov)
    {
        st.any_item_hovered = true;
        if (st.mouse_clicked[0])
        {
            st.active_menu_name = (st.active_menu_name == vis) ? "" : std::string(vis);
            st.active_submenu_name.clear();
            st.open_popup_id    = 0U;
            st.current_menu_x   = p0.x;
            st.popup_opened_this_frame = true;
        }
        else if (!st.active_menu_name.empty() && st.active_menu_name != vis)
        {
            st.active_menu_name = std::string(vis);
            st.active_submenu_name.clear();
            st.open_popup_id    = 0U;
            st.current_menu_x   = p0.x;
        }
    }

    const bool open = (st.active_menu_name == vis);
    if (open || hov)
    {
        w.draw_list->AddRectFilled(p0, p1, open ? st.theme.button_active : st.theme.button_hover, 4.0F);
    }
    w.draw_list->AddText(ImVec2(p0.x + 8.0F, p0.y + 4.0F), st.theme.text,
                         vis.data(), vis.data() + vis.size(), 1.28F);
    w.prev_item_min = p0;
    w.prev_item_max = p1;
    w.cursor.x      = p1.x + 5.0F;

    if (open)
    {
        const ImGuiID menu_id = cd::ui::chroma::hash_str(std::string("##menu_") + std::string(vis));
        auto sz_it = st.auto_size_map.find(menu_id);
        const ImVec2 pop_sz = (sz_it != st.auto_size_map.end()) ? sz_it->second : ImVec2 { 340.0F, 185.0F };

        cd::ui::chroma::WindowLayout mpw {};
        mpw.name            = std::string("##menu_") + std::string(vis);
        mpw.is_popup        = true;
        mpw.pos             = ImVec2 { p0.x, 29.0F };
        mpw.size            = pop_sz;
        mpw.cursor          = ImVec2 { p0.x + 8.0F, 35.0F };
        mpw.content_max_x   = mpw.cursor.x;
        mpw.content_max_y   = mpw.cursor.y;
        mpw.popup_cmd_start = st.popup_dl.cmd_count();
        mpw.draw_list       = &st.popup_dl;
        st.last_popup_min = mpw.pos;
        st.last_popup_max = ImVec2(mpw.pos.x + mpw.size.x, mpw.pos.y + mpw.size.y);
        st.popup_dl.AddShadow(mpw.pos, st.last_popup_max, 12.0F, 2.0F, IM_COL32(0, 0, 0, 190));
        st.popup_dl.AddRectFilled(mpw.pos, st.last_popup_max, st.theme.popup_bg, 5.0F);
        st.popup_dl.AddRect(mpw.pos, st.last_popup_max, st.theme.accent, 5.0F, 0, 1.0F);
        st.window_stack.push_back(std::move(mpw));
        st.in_open_menu = true;
        return true;
    }
    return false;
}

inline void EndMenu() noexcept
{
    auto& st = cd::ui::chroma::state();
    if (st.in_open_submenu && !st.window_stack.empty())
    {
        auto spw = std::move(st.window_stack.back());
        st.window_stack.pop_back();
        const ImGuiID sub_id = cd::ui::chroma::hash_str(spw.name);
        const ImVec2 final_sz(std::clamp(spw.content_max_x - spw.pos.x + 16.0F, 190.0F, 520.0F),
                              std::clamp(spw.content_max_y - spw.pos.y + 8.0F, 32.0F, 440.0F));
        st.auto_size_map[sub_id] = final_sz;
        detail::finalize_popup_layout(spw, final_sz);
        if (!st.window_stack.empty())
        {
            st.window_stack.back().child_popup_cmd_start = spw.popup_cmd_start;
            st.window_stack.back().child_popup_cmd_end   = st.popup_dl.cmd_count();
        }
        st.in_open_submenu = false;
        return;
    }
    if (st.in_open_menu && !st.window_stack.empty())
    {
        auto mpw = std::move(st.window_stack.back());
        st.window_stack.pop_back();
        const ImGuiID menu_id = cd::ui::chroma::hash_str(mpw.name);
        const ImVec2 final_sz(std::clamp(mpw.content_max_x - mpw.pos.x + 16.0F, 220.0F, 520.0F),
                              std::clamp(mpw.content_max_y - mpw.pos.y + 8.0F, 34.0F, 480.0F));
        st.auto_size_map[menu_id] = final_sz;
        detail::finalize_popup_layout(mpw, final_sz);
        st.in_open_menu = false;
    }
}

inline bool MenuItem(const char* label, const char* shortcut = nullptr,
                     bool selected = false, bool enabled = true) noexcept
{
    auto& st = cd::ui::chroma::state();
    if (st.window_stack.empty() || label == nullptr) return false;
    auto& w = st.window_stack.back();
    const auto vis = cd::ui::chroma::visible_label(label);
    const ImVec2 lsz = cd::ui::chroma::measure_text_px(vis, 1.28F);
    const ImVec2 ssz = (shortcut != nullptr && shortcut[0] != '\0')
                           ? cd::ui::chroma::measure_text_px(shortcut, 1.24F)
                           : ImVec2(0.0F, 0.0F);
    const float needed_w = lsz.x + (ssz.x > 0.0F ? (ssz.x + 28.0F) : 18.0F);
    const float rw = std::max(w.size.x - 16.0F, needed_w);
    const ImVec2 p0 = w.cursor;
    const ImVec2 p1 { p0.x + rw, p0.y + 20.0F };
    detail::advance_item(w, p0, p1);

    const bool hov = st.last_item_hovered && enabled;
    if (hov && !st.in_open_submenu)
    {
        st.active_submenu_name.clear();
    }
    if (hov || selected)
    {
        w.draw_list->AddRectFilled(p0, p1, hov ? st.theme.button_hover : st.theme.button, 3.0F);
    }
    w.draw_list->AddText(ImVec2(p0.x + 6.0F, p0.y + 3.0F),
                         enabled ? st.theme.text : st.theme.text_disabled,
                         vis.data(), vis.data() + vis.size(), 1.28F);
    if (shortcut != nullptr && shortcut[0] != '\0')
    {
        const std::string_view ssv { shortcut };
        w.draw_list->AddText(ImVec2(w.pos.x + w.size.x - ssz.x - 10.0F, p0.y + 3.0F),
                             st.theme.text_disabled, ssv.data(), ssv.data() + ssv.size(), 1.24F);
        w.draw_list->mark_last_cmd_right_aligned(ssz.x + 10.0F);
    }
    if (hov && st.mouse_clicked[0])
    {
        st.active_menu_name.clear();
        st.active_submenu_name.clear();
        st.open_popup_id = 0U;
        st.mouse_clicked[0] = false;
        return true;
    }
    return false;
}

inline bool MenuItem(const char* label, const char* shortcut,
                     bool* p_selected, bool enabled = true) noexcept
{
    const bool cur = (p_selected != nullptr) ? *p_selected : false;
    if (MenuItem(label, shortcut, cur, enabled))
    {
        if (p_selected != nullptr) *p_selected = !(*p_selected);
        return true;
    }
    return false;
}

inline void OpenPopup(const char* str_id) noexcept
{
    auto& st = cd::ui::chroma::state();
    st.open_popup_id  = GetID(str_id);
    st.open_popup_pos = st.io.MousePos;
    st.active_menu_name.clear();
    st.active_submenu_name.clear();
    st.popup_opened_this_frame = true;
}

inline bool BeginPopup(const char* str_id) noexcept
{
    auto& st = cd::ui::chroma::state();
    const ImGuiID id = GetID(str_id);
    if (st.open_popup_id != id) return false;

    auto sz_it = st.auto_size_map.find(id);
    const ImVec2 pop_sz = (sz_it != st.auto_size_map.end()) ? sz_it->second : ImVec2 { 325.0F, 210.0F };

    cd::ui::chroma::WindowLayout pw {};
    pw.name            = str_id != nullptr ? str_id : "popup";
    pw.id              = id;
    pw.is_popup        = true;
    pw.pos             = ImVec2(std::clamp(st.open_popup_pos.x, 6.0F, std::max(6.0F, st.io.DisplaySize.x - pop_sz.x - 6.0F)),
                                std::clamp(st.open_popup_pos.y, 6.0F, std::max(6.0F, st.io.DisplaySize.y - pop_sz.y - 6.0F)));
    pw.size            = pop_sz;
    pw.cursor          = ImVec2 { pw.pos.x + 10.0F, pw.pos.y + 8.0F };
    pw.content_max_x   = pw.cursor.x;
    pw.content_max_y   = pw.cursor.y;
    pw.popup_cmd_start = st.popup_dl.cmd_count();
    pw.draw_list       = &st.popup_dl;
    st.last_popup_min = pw.pos;
    st.last_popup_max = ImVec2(pw.pos.x + pw.size.x, pw.pos.y + pw.size.y);
    st.popup_dl.AddShadow(pw.pos, st.last_popup_max, 12.0F, 2.0F, IM_COL32(0, 0, 0, 185));
    st.popup_dl.AddRectFilled(pw.pos, st.last_popup_max, st.theme.popup_bg, 5.0F);
    st.popup_dl.AddRect(pw.pos, st.last_popup_max, st.theme.accent, 5.0F, 0, 1.0F);
    st.window_stack.push_back(std::move(pw));
    return true;
}

inline bool BeginPopupContextItem(const char* str_id = nullptr) noexcept
{
    auto& st = cd::ui::chroma::state();
    const ImGuiID id = (str_id != nullptr) ? GetID(str_id) : (st.last_item_id ^ 0x9E3779B9U);
    if (st.last_item_hovered && st.mouse_clicked[1])
    {
        st.open_popup_id  = id;
        st.open_popup_pos = st.io.MousePos;
        st.active_menu_name.clear();
        st.active_submenu_name.clear();
        st.popup_opened_this_frame = true;
    }
    if (st.open_popup_id != id) return false;

    auto sz_it = st.auto_size_map.find(id);
    const ImVec2 pop_sz = (sz_it != st.auto_size_map.end()) ? sz_it->second : ImVec2 { 250.0F, 140.0F };

    cd::ui::chroma::WindowLayout pw {};
    pw.name            = str_id != nullptr ? str_id : "ctx_popup";
    pw.id              = id;
    pw.is_popup        = true;
    pw.pos             = ImVec2(std::clamp(st.open_popup_pos.x, 6.0F, std::max(6.0F, st.io.DisplaySize.x - pop_sz.x - 6.0F)),
                                std::clamp(st.open_popup_pos.y, 6.0F, std::max(6.0F, st.io.DisplaySize.y - pop_sz.y - 6.0F)));
    pw.size            = pop_sz;
    pw.cursor          = ImVec2 { pw.pos.x + 10.0F, pw.pos.y + 8.0F };
    pw.content_max_x   = pw.cursor.x;
    pw.content_max_y   = pw.cursor.y;
    pw.popup_cmd_start = st.popup_dl.cmd_count();
    pw.draw_list       = &st.popup_dl;
    st.last_popup_min = pw.pos;
    st.last_popup_max = ImVec2(pw.pos.x + pw.size.x, pw.pos.y + pw.size.y);
    st.popup_dl.AddShadow(pw.pos, st.last_popup_max, 12.0F, 2.0F, IM_COL32(0, 0, 0, 185));
    st.popup_dl.AddRectFilled(pw.pos, st.last_popup_max, st.theme.popup_bg, 5.0F);
    st.popup_dl.AddRect(pw.pos, st.last_popup_max, st.theme.accent, 5.0F, 0, 1.0F);
    st.window_stack.push_back(std::move(pw));
    return true;
}

inline void EndPopup() noexcept
{
    auto& st = cd::ui::chroma::state();
    if (!st.window_stack.empty() && st.window_stack.back().is_popup)
    {
        auto pw = std::move(st.window_stack.back());
        st.window_stack.pop_back();
        const ImGuiID id = (pw.id != 0U) ? pw.id : GetID(pw.name.c_str());
        const ImVec2 final_sz(std::clamp(pw.content_max_x - pw.pos.x + 16.0F, 220.0F, 520.0F),
                              std::clamp(pw.content_max_y - pw.pos.y + 10.0F, 34.0F, 480.0F));
        st.auto_size_map[id] = final_sz;
        detail::finalize_popup_layout(pw, final_sz);
    }
}

inline void SetTooltip(const char* fmt, ...) noexcept
{
    auto& st = cd::ui::chroma::state();
    if (fmt == nullptr) return;
    char buf[512] {};
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    const std::string_view sv { buf };
    float max_line_w = 0.0F;
    std::size_t line_count = 0U;
    std::size_t start = 0U;
    while (start <= sv.size())
    {
        const std::size_t nl = sv.find('\n', start);
        const std::string_view line = (nl == std::string_view::npos)
                                          ? sv.substr(start)
                                          : sv.substr(start, nl - start);
        const ImVec2 lsz = cd::ui::chroma::measure_text_px(line, 1.28F);
        max_line_w = std::max(max_line_w, lsz.x);
        ++line_count;
        if (nl == std::string_view::npos) break;
        start = nl + 1U;
    }

    const float line_h = 17.0F;
    const float tw = max_line_w + 18.0F;
    const float th = std::max(24.0F, static_cast<float>(line_count) * line_h + 8.0F);
    ImVec2 p0 { st.io.MousePos.x + 14.0F, st.io.MousePos.y + 18.0F };
    if (st.io.DisplaySize.x > tw + 12.0F)
        p0.x = std::clamp(p0.x, 6.0F, st.io.DisplaySize.x - tw - 6.0F);
    if (st.io.DisplaySize.y > th + 12.0F)
        p0.y = std::clamp(p0.y, 6.0F, st.io.DisplaySize.y - th - 6.0F);
    const ImVec2 p1 { p0.x + tw, p0.y + th };
    st.fg_dl.AddShadow(p0, p1, 8.0F, 1.0F, IM_COL32(0, 0, 0, 180));
    st.fg_dl.AddRectFilled(p0, p1, st.theme.popup_bg, 4.0F);
    st.fg_dl.AddRect(p0, p1, st.theme.accent_bright, 4.0F, 0, 1.0F);

    start = 0U;
    std::size_t idx = 0U;
    while (start <= sv.size())
    {
        const std::size_t nl = sv.find('\n', start);
        const std::string_view line = (nl == std::string_view::npos)
                                          ? sv.substr(start)
                                          : sv.substr(start, nl - start);
        st.fg_dl.AddText(ImVec2(p0.x + 9.0F, p0.y + 5.0F + static_cast<float>(idx) * line_h),
                         st.theme.text, line.data(), line.data() + line.size(), 1.28F);
        ++idx;
        if (nl == std::string_view::npos) break;
        start = nl + 1U;
    }
}

#if defined(__clang__)
#pragma clang diagnostic pop
#endif

// -----------------------------------------------------------------------------
// Native Drag & Drop Engine
// -----------------------------------------------------------------------------
inline bool BeginDragDropSource(ImGuiDragDropFlags /*flags*/ = 0) noexcept
{
    auto& st = cd::ui::chroma::state();
    if (!st.mouse_down[0] || st.last_item_id == 0U) return false;
    if (st.active_id == 0U && st.last_item_hovered && !st.dnd_active)
    {
        st.active_id = st.last_item_id;
    }
    if (st.active_id == st.last_item_id)
    {
        st.dnd_in_source = true;
        return true;
    }
    return false;
}

inline bool SetDragDropPayload(const char* type, const void* data, std::size_t sz) noexcept
{
    auto& st = cd::ui::chroma::state();
    if (!st.dnd_in_source || type == nullptr || data == nullptr || sz == 0) return false;
    st.dnd_active = true;
    st.dnd_buffer.resize(sz);
    std::memcpy(st.dnd_buffer.data(), data, sz);
    st.dnd_payload.Data     = st.dnd_buffer.data();
    st.dnd_payload.DataSize = static_cast<int>(sz);
    std::snprintf(st.dnd_payload.DataType, sizeof(st.dnd_payload.DataType), "%s", type);
    st.dnd_label = type;
    return true;
}

inline void EndDragDropSource() noexcept
{
    cd::ui::chroma::state().dnd_in_source = false;
}

inline bool BeginDragDropTarget() noexcept
{
    auto& st = cd::ui::chroma::state();
    if (!st.dnd_active || st.window_stack.empty()) return false;
    if (st.last_item_hovered)
    {
        auto& w = st.window_stack.back();
        if (w.draw_list != nullptr && !w.skip)
        {
            w.draw_list->AddRect(st.last_item_min, st.last_item_max, st.theme.accent_bright, 3.5F, 0, 1.5F);
        }
        st.dnd_in_target = true;
        return true;
    }
    return false;
}

inline const ImGuiPayload* AcceptDragDropPayload(const char* type) noexcept
{
    auto& st = cd::ui::chroma::state();
    if (!st.dnd_active || !st.dnd_in_target || type == nullptr) return nullptr;
    if (std::strcmp(st.dnd_payload.DataType, type) != 0) return nullptr;
    if (st.mouse_released[0])
    {
        st.dnd_active = false;
        return &st.dnd_payload;
    }
    return nullptr;
}

inline void EndDragDropTarget() noexcept
{
    cd::ui::chroma::state().dnd_in_target = false;
}

// -----------------------------------------------------------------------------
// Advanced ChromaUI 2.0 Widgets: ProgressBar, PlotLines, Knob, CurveEditor,
// NodeGraph, TimelineScrubber, Toasts, Theme Switcher
// -----------------------------------------------------------------------------
inline void ProgressBar(float fraction, const ImVec2& size_arg = ImVec2(-1.0F, 0.0F),
                        const char* overlay = nullptr) noexcept
{
    auto& st = cd::ui::chroma::state();
    if (st.window_stack.empty()) return;
    auto& w = st.window_stack.back();
    const float avail_w = std::max(40.0F, (w.pos.x + w.size.x - 10.0F) - w.cursor.x);
    const float pw = (size_arg.x > 0.0F) ? size_arg.x : avail_w;
    const float ph = (size_arg.y > 0.0F) ? size_arg.y : 16.0F;
    const ImVec2 p0 = w.cursor;
    const ImVec2 p1 { p0.x + pw, p0.y + ph };
    detail::advance_item(w, p0, p1);

    const float f = std::clamp(fraction, 0.0F, 1.0F);
    w.draw_list->AddRectFilled(p0, p1, st.theme.frame_bg, 4.0F);
    if (f > 0.005F)
    {
        w.draw_list->AddGradient(p0, ImVec2(p0.x + pw * f, p1.y),
                                 st.theme.accent, st.theme.accent_bright, true);
    }
    w.draw_list->AddRect(p0, p1, st.theme.window_border, 4.0F, 0, 1.0F);

    char buf[64] {};
    if (overlay != nullptr)
        std::snprintf(buf, sizeof(buf), "%s", overlay);
    else
        std::snprintf(buf, sizeof(buf), "%.0f%%", static_cast<double>(f * 100.0F));
    const ImVec2 tsz = cd::ui::chroma::measure_text_px(buf, 1.22F);
    w.draw_list->AddText(ImVec2(p0.x + (pw - tsz.x) * 0.5F, p0.y + (ph - 12.0F) * 0.5F),
                         st.theme.text, buf, nullptr, 1.22F);
}

inline void PlotLines(const char* label, const float* values, int values_count,
                      int values_offset = 0, const char* overlay_text = nullptr,
                      float scale_min = 0.0F, float scale_max = 0.0F,
                      ImVec2 graph_size = ImVec2(0, 44.0F)) noexcept
{
    auto& st = cd::ui::chroma::state();
    if (st.window_stack.empty() || values == nullptr || values_count <= 1) return;
    auto& w = st.window_stack.back();
    const float gw = (graph_size.x > 0.0F) ? graph_size.x : std::max(80.0F, (w.pos.x + w.size.x - 10.0F) - w.cursor.x);
    const float gh = (graph_size.y > 0.0F) ? graph_size.y : 44.0F;
    const ImVec2 p0 = w.cursor;
    const ImVec2 p1 { p0.x + gw, p0.y + gh };
    detail::advance_item(w, p0, p1);

    float vmin = scale_min;
    float vmax = scale_max;
    if (vmax <= vmin)
    {
        vmin = values[0];
        vmax = values[0];
        for (int i = 1; i < values_count; ++i)
        {
            vmin = std::min(vmin, values[i]);
            vmax = std::max(vmax, values[i]);
        }
        if (vmax - vmin < 1e-4F) vmax = vmin + 1.0F;
    }

    w.draw_list->AddRectFilled(p0, p1, st.theme.frame_bg, 4.0F);
    w.draw_list->AddRect(p0, p1, st.theme.window_border, 4.0F, 0, 1.0F);
    for (int i = 0; i < values_count - 1; ++i)
    {
        const int idx0 = (i + values_offset) % values_count;
        const int idx1 = (i + 1 + values_offset) % values_count;
        const float t0 = static_cast<float>(i) / static_cast<float>(values_count - 1);
        const float t1 = static_cast<float>(i + 1) / static_cast<float>(values_count - 1);
        const float ny0 = std::clamp((values[idx0] - vmin) / (vmax - vmin), 0.0F, 1.0F);
        const float ny1 = std::clamp((values[idx1] - vmin) / (vmax - vmin), 0.0F, 1.0F);
        const ImVec2 a { p0.x + 3.0F + t0 * (gw - 6.0F), p1.y - 3.0F - ny0 * (gh - 6.0F) };
        const ImVec2 b { p0.x + 3.0F + t1 * (gw - 6.0F), p1.y - 3.0F - ny1 * (gh - 6.0F) };
        w.draw_list->AddLine(a, b, st.theme.accent_bright, 1.6F);
    }
    if (overlay_text != nullptr && overlay_text[0] != '\0')
    {
        w.draw_list->AddText(ImVec2(p0.x + 6.0F, p0.y + 4.0F), st.theme.text, overlay_text, nullptr, 1.22F);
    }
    else if (label != nullptr)
    {
        const auto vis = cd::ui::chroma::visible_label(label);
        if (!vis.empty())
            w.draw_list->AddText(ImVec2(p0.x + 6.0F, p0.y + 4.0F), st.theme.text_disabled, vis.data(), vis.data() + vis.size(), 1.22F);
    }
}

inline bool KnobFloat(const char* label, float* v, float v_min, float v_max, float radius = 20.0F) noexcept
{
    auto& st = cd::ui::chroma::state();
    if (st.window_stack.empty() || label == nullptr || v == nullptr) return false;
    auto& w = st.window_stack.back();
    const auto vis = cd::ui::chroma::visible_label(label);
    const ImVec2 tsz = cd::ui::chroma::measure_text_px(vis, 1.22F);
    const float kw = std::max(radius * 2.0F + 6.0F, tsz.x + 4.0F);
    const float kh = radius * 2.0F + 18.0F;
    const ImVec2 p0 = w.cursor;
    const ImVec2 p1 { p0.x + kw, p0.y + kh };
    const ImGuiID id = GetID(label);
    st.last_item_id = id;
    detail::advance_item(w, p0, p1);

    const bool hov = st.last_item_hovered;
    if (hov && st.mouse_clicked[0]) st.active_id = id;
    bool changed = false;
    if (st.active_id == id && st.mouse_down[0] && (std::fabs(st.mouse_delta.x) > 0.01F || std::fabs(st.mouse_delta.y) > 0.01F))
    {
        const float delta = (st.mouse_delta.x - st.mouse_delta.y) * 0.006F * (v_max - v_min);
        *v = std::clamp(*v + delta, v_min, v_max);
        changed = true;
    }

    const ImVec2 c { p0.x + kw * 0.5F, p0.y + radius + 2.0F };
    w.draw_list->AddCircleFilled(c, radius, st.theme.frame_bg, 24);
    w.draw_list->AddCircle(c, radius, (hov || st.active_id == id) ? st.theme.accent_bright : st.theme.window_border, 24, 1.5F);
    const float norm = (v_max > v_min) ? std::clamp((*v - v_min) / (v_max - v_min), 0.0F, 1.0F) : 0.0F;
    const float ang  = (0.75F + norm * 1.50F) * std::numbers::pi_v<float>;
    w.draw_list->AddLine(c, ImVec2(c.x + std::cos(ang) * (radius - 3.0F), c.y + std::sin(ang) * (radius - 3.0F)),
                         st.theme.accent_bright, 2.2F);
    w.draw_list->AddCircleFilled(c, 3.0F, st.theme.accent, 12);
    w.draw_list->AddText(ImVec2(p0.x + (kw - tsz.x) * 0.5F, p0.y + radius * 2.0F + 4.0F),
                         st.theme.text, vis.data(), vis.data() + vis.size(), 1.22F);
    return changed;
}

inline bool CurveEditor(const char* label, float points_xy[8], ImVec2 size_arg = ImVec2(0.0F, 95.0F)) noexcept
{
    auto& st = cd::ui::chroma::state();
    if (st.window_stack.empty() || label == nullptr || points_xy == nullptr) return false;
    auto& w = st.window_stack.back();
    const float cw = (size_arg.x > 0.0F) ? size_arg.x : std::max(120.0F, (w.pos.x + w.size.x - 10.0F) - w.cursor.x);
    const float ch = std::max(60.0F, size_arg.y);
    const ImVec2 p0 = w.cursor;
    const ImVec2 p1 { p0.x + cw, p0.y + ch };
    const ImGuiID base_id = GetID(label);
    detail::advance_item(w, p0, p1);

    w.draw_list->AddRectFilled(p0, p1, st.theme.frame_bg, 4.0F);
    for (int g = 1; g < 4; ++g)
    {
        const float gx = p0.x + (cw * static_cast<float>(g)) * 0.25F;
        const float gy = p0.y + (ch * static_cast<float>(g)) * 0.25F;
        w.draw_list->AddLine(ImVec2(gx, p0.y), ImVec2(gx, p1.y), IM_COL32(60, 75, 100, 75), 1.0F);
        w.draw_list->AddLine(ImVec2(p0.x, gy), ImVec2(p1.x, gy), IM_COL32(60, 75, 100, 75), 1.0F);
    }
    w.draw_list->AddRect(p0, p1, st.theme.window_border, 4.0F, 0, 1.0F);

    auto to_screen = [&](float nx, float ny) noexcept -> ImVec2 {
        return ImVec2(p0.x + 6.0F + std::clamp(nx, 0.0F, 1.0F) * (cw - 12.0F),
                      p1.y - 6.0F - std::clamp(ny, 0.0F, 1.0F) * (ch - 12.0F));
    };

    bool changed = false;
    const ImVec2 mp = st.io.MousePos;
    const bool occluded = detail::is_occluded_by_popup(w) || w.skip || !detail::is_in_window_clip(w, mp);
    ImVec2 sp[4] {};
    for (int i = 0; i < 4; ++i)
    {
        sp[i] = to_screen(points_xy[i * 2], points_xy[i * 2 + 1]);
        const ImGuiID pid = base_id + static_cast<ImGuiID>(i + 1);
        const float dx = mp.x - sp[i].x;
        const float dy = mp.y - sp[i].y;
        const bool hov = !occluded && ((dx * dx + dy * dy) <= 49.0F);
        if (hov && st.mouse_clicked[0]) st.active_id = pid;
        if (st.active_id == pid && st.mouse_down[0])
        {
            if (i > 0 && i < 3)
                points_xy[i * 2] = std::clamp((mp.x - (p0.x + 6.0F)) / std::max(1.0F, cw - 12.0F), 0.05F, 0.95F);
            points_xy[i * 2 + 1] = std::clamp((p1.y - 6.0F - mp.y) / std::max(1.0F, ch - 12.0F), 0.0F, 1.0F);
            sp[i] = to_screen(points_xy[i * 2], points_xy[i * 2 + 1]);
            changed = true;
        }
    }

    w.draw_list->AddLine(sp[0], sp[1], IM_COL32(130, 160, 210, 130), 1.0F);
    w.draw_list->AddLine(sp[2], sp[3], IM_COL32(130, 160, 210, 130), 1.0F);
    w.draw_list->AddBezierCubic(sp[0], sp[1], sp[2], sp[3], st.theme.accent_bright, 2.2F, 24);
    for (int i = 0; i < 4; ++i)
    {
        w.draw_list->AddCircleFilled(sp[i], 4.5F, (i == 0 || i == 3) ? st.theme.accent : IM_COL32(255, 195, 55, 255), 12);
    }
    const auto vis = cd::ui::chroma::visible_label(label);
    if (!vis.empty())
        w.draw_list->AddText(ImVec2(p0.x + 8.0F, p0.y + 5.0F), st.theme.text_disabled, vis.data(), vis.data() + vis.size(), 1.22F);
    return changed;
}

inline bool TimelineScrubber(const char* label, float* current_time, float max_time,
                             const float* keyframes = nullptr, int keyframe_count = 0) noexcept
{
    auto& st = cd::ui::chroma::state();
    if (st.window_stack.empty() || current_time == nullptr || max_time <= 0.0F) return false;
    auto& w = st.window_stack.back();
    const float tw = std::max(120.0F, (w.pos.x + w.size.x - 10.0F) - w.cursor.x);
    const float th = 32.0F;
    const ImVec2 p0 = w.cursor;
    const ImVec2 p1 { p0.x + tw, p0.y + th };
    const ImGuiID id = GetID(label != nullptr ? label : "##timeline");
    detail::advance_item(w, p0, p1);

    const ImVec2 mp = st.io.MousePos;
    if (st.last_item_hovered && st.mouse_clicked[0]) st.active_id = id;
    bool changed = false;
    if (st.active_id == id && st.mouse_down[0])
    {
        *current_time = std::clamp(((mp.x - (p0.x + 8.0F)) / std::max(1.0F, tw - 16.0F)) * max_time, 0.0F, max_time);
        changed = true;
    }

    w.draw_list->AddRectFilled(p0, p1, st.theme.frame_bg, 4.0F);
    w.draw_list->AddRect(p0, p1, st.theme.window_border, 4.0F, 0, 1.0F);
    for (int t = 0; t <= 10; ++t)
    {
        const float tx = p0.x + 8.0F + (static_cast<float>(t) / 10.0F) * (tw - 16.0F);
        w.draw_list->AddLine(ImVec2(tx, p0.y + 2.0F), ImVec2(tx, p0.y + (t % 5 == 0 ? 11.0F : 6.0F)), st.theme.separator, 1.0F);
    }
    if (keyframes != nullptr)
    {
        for (int k = 0; k < keyframe_count; ++k)
        {
            const float kx = p0.x + 8.0F + std::clamp(keyframes[k] / max_time, 0.0F, 1.0F) * (tw - 16.0F);
            const float ky = p0.y + 21.0F;
            w.draw_list->AddTriangleFilled(ImVec2(kx, ky - 5.0F), ImVec2(kx + 5.0F, ky), ImVec2(kx, ky + 5.0F), IM_COL32(255, 195, 50, 255));
            w.draw_list->AddTriangleFilled(ImVec2(kx, ky - 5.0F), ImVec2(kx, ky + 5.0F), ImVec2(kx - 5.0F, ky), IM_COL32(255, 195, 50, 255));
        }
    }
    const auto vis = cd::ui::chroma::visible_label(label != nullptr ? label : "");
    char tbuf[96] {};
    if (!vis.empty())
        std::snprintf(tbuf, sizeof(tbuf), "%.*s   %.2fs / %.1fs",
                      static_cast<int>(vis.size()), vis.data(),
                      static_cast<double>(*current_time), static_cast<double>(max_time));
    else
        std::snprintf(tbuf, sizeof(tbuf), "%.2fs / %.1fs",
                      static_cast<double>(*current_time), static_cast<double>(max_time));
    w.draw_list->AddText(ImVec2(p0.x + 12.0F, p0.y + 4.0F), st.theme.text_disabled, tbuf, nullptr, 1.15F);

    const float px = p0.x + 8.0F + std::clamp(*current_time / max_time, 0.0F, 1.0F) * (tw - 16.0F);
    w.draw_list->AddLine(ImVec2(px, p0.y + 1.0F), ImVec2(px, p1.y - 1.0F), IM_COL32(255, 75, 75, 255), 2.0F);
    w.draw_list->AddTriangleFilled(ImVec2(px - 5.0F, p0.y + 1.0F), ImVec2(px + 5.0F, p0.y + 1.0F), ImVec2(px, p0.y + 8.0F), IM_COL32(255, 75, 75, 255));
    return changed;
}

// Node Graph Canvas
inline bool BeginNodeGraph(const char* str_id, ImVec2 size_arg = ImVec2(0.0F, 0.0F)) noexcept
{
    auto& st = cd::ui::chroma::state();
    if (!BeginChild(str_id, size_arg, true)) return false;
    auto& w = st.window_stack.back();
    st.node_graph_origin = w.pos;
    st.node_pins.clear();

    // Pan with middle or right mouse drag
    const ImVec2 mp = st.io.MousePos;
    const bool in_canvas = (mp.x >= w.pos.x && mp.x <= w.pos.x + w.size.x &&
                            mp.y >= w.pos.y && mp.y <= w.pos.y + w.size.y);
    if (in_canvas && (st.mouse_down[2] || (st.mouse_down[1] && st.active_id == 0U)))
    {
        st.node_graph_pan.x += st.mouse_delta.x;
        st.node_graph_pan.y += st.mouse_delta.y;
    }

    // Draw subtle dot/line grid
    const float step = 28.0F;
    const float ox = std::fmod(st.node_graph_pan.x, step);
    const float oy = std::fmod(st.node_graph_pan.y, step);
    for (float x = w.pos.x + ox; x < w.pos.x + w.size.x; x += step)
        w.draw_list->AddLine(ImVec2(x, w.pos.y), ImVec2(x, w.pos.y + w.size.y), IM_COL32(55, 70, 95, 55), 1.0F);
    for (float y = w.pos.y + oy; y < w.pos.y + w.size.y; y += step)
        w.draw_list->AddLine(ImVec2(w.pos.x, y), ImVec2(w.pos.x + w.size.x, y), IM_COL32(55, 70, 95, 55), 1.0F);
    return true;
}

inline void EndNodeGraph() noexcept
{
    EndChild();
}

inline void BeginNode(ImGuiID node_id, const char* title, ImVec2 default_pos,
                      ImVec2 node_size = ImVec2(170.0F, 92.0F),
                      ImU32 header_col = IM_COL32(36, 115, 215, 255)) noexcept
{
    auto& st = cd::ui::chroma::state();
    if (st.window_stack.empty()) return;
    auto& w = st.window_stack.back();
    auto it = st.node_pos_map.find(node_id);
    if (it == st.node_pos_map.end())
    {
        it = st.node_pos_map.emplace(node_id, default_pos).first;
    }

    ImVec2 npos { st.node_graph_origin.x + st.node_graph_pan.x + it->second.x,
                  st.node_graph_origin.y + st.node_graph_pan.y + it->second.y };
    const ImVec2 mp = st.io.MousePos;
    const bool in_hdr = !detail::is_occluded_by_popup(w) &&
                        (mp.x >= npos.x && mp.x <= npos.x + node_size.x && mp.y >= npos.y && mp.y <= npos.y + 22.0F);
    if (in_hdr && st.mouse_clicked[0]) st.dragging_node_id = node_id;
    if (st.dragging_node_id == node_id && st.mouse_down[0])
    {
        it->second.x += st.mouse_delta.x;
        it->second.y += st.mouse_delta.y;
        npos = ImVec2 { st.node_graph_origin.x + st.node_graph_pan.x + it->second.x,
                        st.node_graph_origin.y + st.node_graph_pan.y + it->second.y };
    }
    const ImVec2 nmax { npos.x + node_size.x, npos.y + node_size.y };

    w.draw_list->AddShadow(npos, nmax, 10.0F, 2.0F, IM_COL32(0, 0, 0, 165));
    w.draw_list->AddRectFilled(npos, nmax, IM_COL32(22, 27, 38, 245), 6.0F);
    w.draw_list->AddRectFilled(npos, ImVec2(nmax.x, npos.y + 22.0F), header_col, 6.0F);
    w.draw_list->AddRect(npos, nmax, (st.dragging_node_id == node_id) ? st.theme.accent_bright : st.theme.window_border, 6.0F, 0, 1.2F);
    if (title != nullptr)
        w.draw_list->AddText(ImVec2(npos.x + 8.0F, npos.y + 4.0F), IM_COL32(250, 252, 255, 255), title, nullptr, 1.25F);

    st.current_node_id   = node_id;
    st.current_node_pos  = npos;
    st.current_node_size = node_size;
    w.cursor = ImVec2(npos.x + 12.0F, npos.y + 28.0F);
}

inline void EndNode() noexcept
{
    cd::ui::chroma::state().current_node_id = 0U;
}

inline void NodePin(ImGuiID pin_id, const char* label, bool is_output,
                    ImU32 pin_col = IM_COL32(80, 200, 255, 255)) noexcept
{
    auto& st = cd::ui::chroma::state();
    if (st.window_stack.empty() || st.current_node_id == 0U) return;
    auto& w = st.window_stack.back();
    const float py = w.cursor.y + 7.0F;
    const float px = is_output ? (st.current_node_pos.x + st.current_node_size.x) : st.current_node_pos.x;
    const ImVec2 pcenter { px, py };
    st.node_pins[pin_id] = cd::ui::chroma::NodePinRecord { pin_id, pcenter, is_output, pin_col };

    w.draw_list->AddCircleFilled(pcenter, 5.0F, pin_col, 12);
    w.draw_list->AddCircle(pcenter, 5.0F, IM_COL32(255, 255, 255, 220), 12, 1.0F);
    if (label != nullptr)
    {
        const ImVec2 tsz = cd::ui::chroma::measure_text_px(label, 1.22F);
        const float tx = is_output ? (px - tsz.x - 9.0F) : (px + 9.0F);
        w.draw_list->AddText(ImVec2(tx, w.cursor.y), st.theme.text, label, nullptr, 1.22F);
    }
    w.cursor.y += 19.0F;
}

inline void NodeLink(ImGuiID out_pin_id, ImGuiID in_pin_id,
                     ImU32 col = IM_COL32(85, 195, 255, 235), float thickness = 2.4F) noexcept
{
    auto& st = cd::ui::chroma::state();
    if (st.window_stack.empty()) return;
    auto& w = st.window_stack.back();
    const auto it_a = st.node_pins.find(out_pin_id);
    const auto it_b = st.node_pins.find(in_pin_id);
    if (it_a == st.node_pins.end() || it_b == st.node_pins.end()) return;
    const ImVec2 p1 = it_a->second.screen_pos;
    const ImVec2 p4 = it_b->second.screen_pos;
    const float dx  = std::max(45.0F, std::fabs(p4.x - p1.x) * 0.5F);
    w.draw_list->AddBezierCubic(p1, ImVec2(p1.x + dx, p1.y), ImVec2(p4.x - dx, p4.y), p4, col, thickness, 24);
}

inline void PushToast(const char* title, const char* body, int severity = 0, float duration_sec = 3.5F) noexcept
{
    auto& st = cd::ui::chroma::state();
    if (st.toasts.size() >= 5U) st.toasts.erase(st.toasts.begin());
    st.toasts.push_back(cd::ui::chroma::ToastMessage {
        title != nullptr ? title : "Chroma Studio",
        body != nullptr ? body : "",
        severity,
        duration_sec,
        duration_sec
    });
}

inline void ApplyTheme(int theme_idx) noexcept
{
    auto& st = cd::ui::chroma::state();
    st.theme_index = std::clamp(theme_idx, 0, 4);
    st.theme       = cd::ui::chroma::make_theme(st.theme_index);
}

}  // namespace ImGui

// =============================================================================
// In-Game HUD & 3D World-Space UI Overlay API (`cd::ui::chroma::hud`)
// =============================================================================
namespace cd::ui::chroma::hud
{

inline void DrawHealthShieldBar(ImVec2 pos, float width, float hp, float max_hp,
                                float shield, float max_shield, float stamina) noexcept
{
    auto* dl = ImGui::GetForegroundDrawList();
    const float hp_t = std::clamp(hp / std::max(1.0F, max_hp), 0.0F, 1.0F);
    const float sh_t = std::clamp(shield / std::max(1.0F, max_shield), 0.0F, 1.0F);
    const float st_t = std::clamp(stamina, 0.0F, 1.0F);

    const ImVec2 p0 = pos;
    const ImVec2 p1 { pos.x + width, pos.y + 66.0F };
    dl->AddShadow(p0, p1, 12.0F, 2.0F, IM_COL32(0, 0, 0, 175));
    dl->AddRectFilled(p0, p1, IM_COL32(12, 16, 24, 225), 7.0F);
    dl->AddRect(p0, p1, IM_COL32(65, 165, 255, 190), 7.0F, 0, 1.2F);

    char hbuf[64] {};
    std::snprintf(hbuf, sizeof(hbuf), "HP  %.0f / %.0f     SHIELD  %.0f%%",
                  static_cast<double>(hp), static_cast<double>(max_hp), static_cast<double>(sh_t * 100.0F));
    dl->AddText(ImVec2(p0.x + 10.0F, p0.y + 6.0F), IM_COL32(240, 246, 255, 255), hbuf, nullptr, 1.25F);

    // Health bar (crimson -> emerald gradient)
    const ImVec2 hb0 { p0.x + 10.0F, p0.y + 24.0F };
    const ImVec2 hb1 { p1.x - 10.0F, p0.y + 37.0F };
    dl->AddRectFilled(hb0, hb1, IM_COL32(28, 32, 42, 240), 3.5F);
    dl->AddGradient(hb0, ImVec2(hb0.x + (hb1.x - hb0.x) * hp_t, hb1.y),
                    IM_COL32(40, 200, 95, 255), IM_COL32(95, 245, 145, 255), true);
    dl->AddRect(hb0, hb1, IM_COL32(90, 110, 140, 180), 3.5F, 0, 1.0F);

    // Shield bar (cyan)
    const ImVec2 sb0 { p0.x + 10.0F, p0.y + 41.0F };
    const ImVec2 sb1 { p1.x - 10.0F, p0.y + 50.0F };
    dl->AddRectFilled(sb0, sb1, IM_COL32(24, 30, 42, 240), 3.0F);
    dl->AddGradient(sb0, ImVec2(sb0.x + (sb1.x - sb0.x) * sh_t, sb1.y),
                    IM_COL32(35, 155, 255, 255), IM_COL32(110, 225, 255, 255), true);

    // Stamina bar (amber)
    const ImVec2 tb0 { p0.x + 10.0F, p0.y + 54.0F };
    const ImVec2 tb1 { p1.x - 10.0F, p0.y + 60.0F };
    dl->AddRectFilled(tb0, tb1, IM_COL32(24, 30, 42, 240), 2.5F);
    dl->AddRectFilled(tb0, ImVec2(tb0.x + (tb1.x - tb0.x) * st_t, tb1.y), IM_COL32(255, 195, 45, 245), 2.5F);
}

inline void DrawDynamicCrosshair(ImVec2 center, float spread_px = 8.0F,
                                 ImU32 col = IM_COL32(95, 245, 190, 235)) noexcept
{
    auto* dl = ImGui::GetForegroundDrawList();
    const float len = 9.0F;
    dl->AddCircleFilled(center, 1.8F, col, 8);
    dl->AddLine(ImVec2(center.x - spread_px - len, center.y), ImVec2(center.x - spread_px, center.y), col, 1.8F);
    dl->AddLine(ImVec2(center.x + spread_px, center.y), ImVec2(center.x + spread_px + len, center.y), col, 1.8F);
    dl->AddLine(ImVec2(center.x, center.y - spread_px - len), ImVec2(center.x, center.y - spread_px), col, 1.8F);
    dl->AddLine(ImVec2(center.x, center.y + spread_px), ImVec2(center.x, center.y + spread_px + len), col, 1.8F);
}

inline void DrawMinimapRadar(ImVec2 center, float radius, float cam_yaw_rad,
                             std::span<const ImVec2> blips_xz) noexcept
{
    auto* dl = ImGui::GetForegroundDrawList();
    dl->AddCircleFilled(center, radius + 3.0F, IM_COL32(8, 12, 20, 225), 32);
    dl->AddCircle(center, radius, IM_COL32(65, 185, 255, 220), 32, 1.8F);
    dl->AddCircle(center, radius * 0.62F, IM_COL32(55, 130, 190, 110), 24, 1.0F);
    dl->AddCircle(center, radius * 0.30F, IM_COL32(55, 130, 190, 90), 20, 1.0F);
    dl->AddLine(ImVec2(center.x - radius, center.y), ImVec2(center.x + radius, center.y), IM_COL32(55, 130, 190, 90), 1.0F);
    dl->AddLine(ImVec2(center.x, center.y - radius), ImVec2(center.x, center.y + radius), IM_COL32(55, 130, 190, 90), 1.0F);

    // FOV radar cone
    const float a0 = cam_yaw_rad - 0.45F;
    const float a1 = cam_yaw_rad + 0.45F;
    dl->AddTriangleFilled(center,
                          ImVec2(center.x + std::cos(a0) * radius * 0.92F, center.y + std::sin(a0) * radius * 0.92F),
                          ImVec2(center.x + std::cos(a1) * radius * 0.92F, center.y + std::sin(a1) * radius * 0.92F),
                          IM_COL32(60, 200, 255, 50));

    for (std::size_t i = 0; i < blips_xz.size(); ++i)
    {
        const float bx = std::clamp(blips_xz[i].x * 5.5F, -radius * 0.82F, radius * 0.82F);
        const float by = std::clamp(blips_xz[i].y * 5.5F, -radius * 0.82F, radius * 0.82F);
        dl->AddCircleFilled(ImVec2(center.x + bx, center.y + by), 3.5F,
                            (i == 0U) ? IM_COL32(90, 255, 140, 255) : IM_COL32(255, 105, 85, 255), 10);
    }
    dl->AddCircleFilled(center, 4.0F, IM_COL32(255, 255, 255, 255), 10);
    dl->AddText(ImVec2(center.x - 4.0F, center.y - radius - 16.0F), IM_COL32(110, 215, 255, 255), "N", nullptr, 1.25F);
}

inline int DrawInventoryHotbar(ImVec2 center_bottom, int active_slot,
                               const char* const slot_names[8], const float cooldowns[8]) noexcept
{
    auto* dl = ImGui::GetForegroundDrawList();
    auto& st = state();
    const float slot_w  = 64.0F;
    const float slot_h  = 50.0F;
    const float gap     = 6.0F;
    const float total_w = 8.0F * slot_w + 7.0F * gap + 16.0F;
    const ImVec2 p0 { center_bottom.x - total_w * 0.5F, center_bottom.y - slot_h - 14.0F };
    const ImVec2 p1 { p0.x + total_w, center_bottom.y - 2.0F };

    dl->AddShadow(p0, p1, 12.0F, 2.0F, IM_COL32(0, 0, 0, 185));
    dl->AddRectFilled(p0, p1, IM_COL32(12, 16, 24, 230), 8.0F);
    dl->AddRect(p0, p1, IM_COL32(65, 155, 245, 190), 8.0F, 0, 1.2F);

    int new_slot = active_slot;
    const ImVec2 mp = st.io.MousePos;
    for (int i = 0; i < 8; ++i)
    {
        const ImVec2 s0 { p0.x + 8.0F + static_cast<float>(i) * (slot_w + gap), p0.y + 6.0F };
        const ImVec2 s1 { s0.x + slot_w, s0.y + slot_h };
        const bool hov = (mp.x >= s0.x && mp.x <= s1.x && mp.y >= s0.y && mp.y <= s1.y);
        if (hov)
        {
            st.any_item_hovered = true;
            if (st.mouse_clicked[0]) new_slot = i;
        }
        const bool sel = (i == new_slot);
        dl->AddRectFilled(s0, s1, sel ? IM_COL32(32, 88, 175, 240) : IM_COL32(22, 28, 40, 235), 5.0F);
        if (cooldowns != nullptr && cooldowns[i] > 0.01F)
        {
            const float ct = std::clamp(cooldowns[i], 0.0F, 1.0F);
            dl->AddRectFilled(ImVec2(s0.x, s1.y - slot_h * ct), s1, IM_COL32(10, 14, 22, 185), 5.0F);
        }
        dl->AddRect(s0, s1, sel ? IM_COL32(95, 215, 255, 255) : IM_COL32(65, 80, 105, 210), 5.0F, 0, sel ? 2.0F : 1.0F);

        char kbuf[8] {};
        std::snprintf(kbuf, sizeof(kbuf), "[%d]", i + 1);
        dl->AddText(ImVec2(s0.x + 5.0F, s0.y + 4.0F), IM_COL32(150, 195, 240, 245), kbuf, nullptr, 1.12F);
        if (slot_names != nullptr && slot_names[i] != nullptr)
        {
            const ImVec2 tsz = measure_text_px(slot_names[i], 1.10F);
            const float tx = s0.x + std::max(3.0F, (slot_w - tsz.x) * 0.5F);
            dl->PushClipRect(ImVec2(s0.x + 2.0F, s0.y + 18.0F), ImVec2(s1.x - 2.0F, s1.y - 2.0F));
            dl->AddText(ImVec2(tx, s0.y + 27.0F), IM_COL32(240, 246, 255, 255), slot_names[i], nullptr, 1.10F);
            dl->PopClipRect();
        }
    }
    return new_slot;
}

inline void DrawWorldSpaceEntityTag(ImVec2 screen_pos, const char* name,
                                    float distance_m, float health_01 = 1.0F,
                                    bool selected = false) noexcept
{
    if (name == nullptr) return;
    auto* dl = ImGui::GetBackgroundDrawList();
    const auto& st = state();
    char label_buf[96] {};
    std::snprintf(label_buf, sizeof(label_buf), "%s  [%.1fm]", name, static_cast<double>(distance_m));
    const ImVec2 tsz = measure_text_px(label_buf, 1.22F);
    const float bw = std::max(96.0F, tsz.x + 16.0F);
    const float bh = 30.0F;
    float min_x = 4.0F;
    float max_x = std::max(4.0F, st.io.DisplaySize.x - bw - 4.0F);
    float min_y = 64.0F;
    if (st.workspace_mode == 0 && !st.window_dock_slot.empty())
    {
        const float W = st.io.DisplaySize.x;
        const float H = st.io.DisplaySize.y;
        const float top_y = 60.0F;
        const float work_h = std::max(100.0F, H - top_y);
        const float lx = std::round(W * st.dock_left_w_ratio);
        const float rx = std::round(W * (1.0F - st.dock_right_w_ratio));
        const float by = std::round(top_y + work_h * (1.0F - st.dock_bottom_h_ratio));
        if (screen_pos.x < lx + 8.0F || screen_pos.x > rx - 8.0F ||
            screen_pos.y < top_y + 8.0F || screen_pos.y > by - 8.0F)
        {
            return;
        }
        min_x = lx + 6.0F;
        max_x = std::max(min_x, rx - bw - 6.0F);
        min_y = top_y + 6.0F;
    }
    const ImVec2 p0 {
        std::clamp(std::round(screen_pos.x - bw * 0.5F), min_x, max_x),
        std::max(min_y, std::round(screen_pos.y - bh - 12.0F))
    };
    const ImVec2 p1 { p0.x + bw, p0.y + bh };

    dl->AddLine(screen_pos, ImVec2(screen_pos.x, p1.y), selected ? IM_COL32(95, 225, 255, 220) : IM_COL32(140, 175, 220, 150), 1.4F);
    dl->AddCircleFilled(screen_pos, 3.0F, selected ? IM_COL32(95, 225, 255, 255) : IM_COL32(180, 210, 250, 220), 10);
    dl->AddRectFilled(p0, p1, IM_COL32(14, 19, 30, 220), 5.0F);
    dl->AddRect(p0, p1, selected ? IM_COL32(95, 225, 255, 255) : IM_COL32(70, 95, 135, 200), 5.0F, 0, selected ? 1.6F : 1.0F);
    dl->AddText(ImVec2(p0.x + 8.0F, p0.y + 4.0F), IM_COL32(240, 246, 255, 255), label_buf, nullptr, 1.22F);

    const ImVec2 hb0 { p0.x + 8.0F, p1.y - 8.0F };
    const ImVec2 hb1 { p1.x - 8.0F, p1.y - 4.0F };
    dl->AddRectFilled(hb0, hb1, IM_COL32(30, 36, 48, 240), 2.0F);
    dl->AddRectFilled(hb0, ImVec2(hb0.x + (hb1.x - hb0.x) * std::clamp(health_01, 0.0F, 1.0F), hb1.y),
                      selected ? IM_COL32(75, 235, 145, 255) : IM_COL32(55, 185, 255, 240), 2.0F);
}

}  // namespace cd::ui::chroma::hud

