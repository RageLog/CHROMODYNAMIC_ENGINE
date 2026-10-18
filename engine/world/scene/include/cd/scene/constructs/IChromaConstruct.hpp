// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/IChromaConstruct.hpp
// Polymorphic C++23 Base Hierarchy for All 250 CHROMODYNAMIC Scene Constructs
// (219 Godot Video Equivalents + 31 Next-Gen SOTA Beyond-Godot Constructs).
// =============================================================================
#pragma once

#include <cd/scene/ElementCapsuleTree.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace cd::scene
{

enum class ConstructCategory : std::uint8_t
{
    CoreSystem  = 0, ///< 21 Base & System Constructs (0..20)
    Planar2D    = 1, ///< 50 Planar 2D Constructs (21..70)
    Spatial3D   = 2, ///< 88 Spatial 3D & Vessel Constructs (71..158)
    ControlUI   = 3, ///< 60 ChromaUI Control & Container Constructs (159..218)
    NextGenSOTA = 4, ///< 31 Beyond-Godot Next-Gen SOTA Constructs (219..249)
};

enum class ConstructRole : std::uint8_t
{
    Element      = 0, ///< Self-contained scene building block
    Vessel       = 1, ///< Physical body controllable by a Pilot (`embark_pilot`)
    Pilot        = 2, ///< Player or AI controller brain (`HumanPilot`, `SynthPilot`)
    Stage        = 3, ///< World/Viewport/Layer container
    TraitCapsule = 4, ///< Composable inline or nested capability capsule
    PulseBus     = 5, ///< Type-safe signal/synchronizer/scheduler bus
};

[[nodiscard]] constexpr ElementDomain map_category_and_role_to_domain(
    ConstructCategory cat, ConstructRole role) noexcept
{
    if (role == ConstructRole::Vessel)
    {
        return ElementDomain::kVessel;
    }
    switch (cat)
    {
        case ConstructCategory::CoreSystem:  return ElementDomain::kCoreStage;
        case ConstructCategory::Planar2D:    return ElementDomain::kPlanar2D;
        case ConstructCategory::Spatial3D:   return ElementDomain::kSpatial3D;
        case ConstructCategory::ControlUI:   return ElementDomain::kControlUi;
        case ConstructCategory::NextGenSOTA: return ElementDomain::kNextGenSota;
    }
    return ElementDomain::kSpatial3D;
}

/// Abstract polymorphic base class implemented by all 250 concrete C++23
/// construct classes (`ChromaElement`, `AvatarVessel3D`, `RoverVessel3D`,
/// `SpringArmCapsule3D`, `HardwareRayTracingProbe3D`, etc.).
class IChromaConstruct
{
public:
    virtual ~IChromaConstruct() = default;

    [[nodiscard]] virtual std::uint16_t     catalog_id() const noexcept = 0;
    [[nodiscard]] virtual std::string_view  chroma_class_name() const noexcept = 0;
    [[nodiscard]] virtual std::string_view  godot_equivalent_name() const noexcept = 0;
    [[nodiscard]] virtual ConstructCategory category() const noexcept = 0;
    [[nodiscard]] virtual ConstructRole     role() const noexcept = 0;
    [[nodiscard]] virtual std::string_view  video_timestamp() const noexcept = 0;
    [[nodiscard]] virtual std::string_view  bundled_traits_summary() const noexcept = 0;

    [[nodiscard]] virtual std::unique_ptr<IChromaConstruct> clone() const = 0;

    /// Mounts this construct's domain, pilot mode, and inline trait capsule
    /// onto a target `Element` inside `ElementCapsuleTree`.
    virtual void on_mount(Element& host) const noexcept
    {
        host.domain         = map_category_and_role_to_domain(category(), role());
        host.archetype_type = std::string(chroma_class_name());
        if (role() == ConstructRole::Vessel)
        {
            host.is_vessel = true;
            if (host.pilot_mode == PilotMode::kNone)
            {
                host.pilot_mode = PilotMode::kHumanPilot;
            }
        }
        else if (role() == ConstructRole::Pilot && host.pilot_mode == PilotMode::kNone)
        {
            host.pilot_mode = PilotMode::kSynthPilot;
        }
        TraitCapsule cap {};
        cap.capsule_type     = std::string(chroma_class_name());
        cap.godot_equivalent = std::string(godot_equivalent_name());
        cap.enabled          = enabled_;
        host.inline_capsules.push_back(std::move(cap));
    }

    /// Steps this construct's live behavior on `host` by `dt` seconds.
    virtual void on_tick(Element& host, float dt) noexcept
    {
        if (!enabled_ || dt <= 0.0F)
        {
            return;
        }
        elapsed_time_ += dt * host.world_time_scale;
        ++tick_count_;
    }

    [[nodiscard]] bool          enabled() const noexcept { return enabled_; }
    void                        set_enabled(bool e) noexcept { enabled_ = e; }
    [[nodiscard]] float         elapsed_time() const noexcept { return elapsed_time_; }
    [[nodiscard]] std::uint64_t tick_count() const noexcept { return tick_count_; }

protected:
    bool          enabled_      { true };
    float         elapsed_time_ { 0.0F };
    std::uint64_t tick_count_   { 0u };
};

/// Domain base template reducing boilerplate while giving each of the 250
/// concrete C++23 classes its own distinct type, properties, and behavior.
template <
    std::uint16_t     kId,
    ConstructCategory kCat,
    ConstructRole     kRole>
class ChromaConstructBase : public IChromaConstruct
{
public:
    [[nodiscard]] std::uint16_t     catalog_id() const noexcept override { return kId; }
    [[nodiscard]] ConstructCategory category() const noexcept override { return kCat; }
    [[nodiscard]] ConstructRole     role() const noexcept override { return kRole; }
};

} // namespace cd::scene
