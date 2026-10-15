// =============================================================================
// CHROMODYNAMIC — cd/game/element_core/Element.hpp
// FAZ 5 Wave 5.1 — Original CHROMODYNAMIC Scene Element Architecture:
//   * Pulse<Args...>   — typed zero-overhead signal/slot broadcaster
//   * PropertyDesc     — automatic Inspector & .cdstage serialization schema
//   * Element          — base scene element (UUID, hierarchy, lifecycle hooks)
//   * Planar2D         — 2D spatial element base
//   * Spatial3D        — 3D spatial element base (Local/World transform, Mobility)
//   * Stage            — .cdstage scene container, prefab instancing & serializer
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/core/Result.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cd::element_core
{

// ---- Pulse<Args...>: Original CHROMODYNAMIC Typed Signal System -------------

template <typename... Args>
class Pulse
{
public:
    using SlotFn = std::function<void(Args...)>;
    using ConnectionId = std::uint64_t;

    ConnectionId connect(SlotFn slot)
    {
        if (!slot)
            return 0;
        const ConnectionId id = ++next_id_;
        slots_.push_back(Entry { id, std::move(slot) });
        return id;
    }

    bool disconnect(ConnectionId id)
    {
        for (auto it = slots_.begin(); it != slots_.end(); ++it)
        {
            if (it->id == id)
            {
                slots_.erase(it);
                return true;
            }
        }
        return false;
    }

    void disconnect_all() noexcept
    {
        slots_.clear();
    }

    void emit(Args... args) const
    {
        // Snapshot size so slots that connect during emit don't invalidate iteration
        const std::size_t count = slots_.size();
        for (std::size_t i = 0; i < count && i < slots_.size(); ++i)
        {
            if (slots_[i].fn)
                slots_[i].fn(args...);
        }
    }

    [[nodiscard]] std::size_t subscriber_count() const noexcept
    {
        return slots_.size();
    }

private:
    struct Entry
    {
        ConnectionId id { 0 };
        SlotFn fn {};
    };
    ConnectionId next_id_ { 0 };
    std::vector<Entry> slots_ {};
};

// ---- Inspector Reflection Schema --------------------------------------------

enum class PropertyKind : std::uint8_t
{
    kBool = 0,
    kInt = 1,
    kFloat = 2,
    kVec2 = 3,
    kVec3 = 4,
    kColorRgba = 5,
    kString = 6,
    kEnum = 7,
};

struct PropertyDesc
{
    std::string name {};
    std::string category { "General" };
    PropertyKind kind { PropertyKind::kFloat };
    float min_value { 0.0F };
    float max_value { 1.0F };
    std::string serialized_value {};
};

// ---- Mobility (Unreal-level static/stationary/movable optimization) ---------

enum class Mobility : std::uint8_t
{
    kStatic = 0,      ///< Baked into static BVH / DDGI / cluster caches
    kStationary = 1,  ///< Fixed transform, dynamic parameters (e.g. light intensity)
    kMovable = 2,     ///< Fully dynamic transform every frame
};

// ---- Element: Base CHROMODYNAMIC Scene Element ------------------------------

class Element
{
public:
    explicit Element(std::string name = "Element");
    virtual ~Element();

    Element(const Element&) = delete;
    Element& operator=(const Element&) = delete;
    Element(Element&&) noexcept = default;
    Element& operator=(Element&&) noexcept = default;

    [[nodiscard]] virtual std::string_view class_name() const noexcept
    {
        return "Element";
    }

    [[nodiscard]] std::uint64_t uuid() const noexcept { return uuid_; }
    void set_uuid(std::uint64_t id) noexcept { uuid_ = id; }

    [[nodiscard]] std::string_view name() const noexcept { return name_; }
    void set_name(std::string n) { name_ = std::move(n); }

    [[nodiscard]] bool is_enabled() const noexcept { return enabled_; }
    void set_enabled(bool e) noexcept { enabled_ = e; }

    [[nodiscard]] bool is_mounted() const noexcept { return mounted_; }

    // ---- Hierarchy management ----------------------------------------------

    [[nodiscard]] Element* parent() const noexcept { return parent_; }
    [[nodiscard]] const std::vector<std::unique_ptr<Element>>& children() const noexcept
    {
        return children_;
    }

    template <typename T, typename... CtorArgs>
    T& emplace_child(CtorArgs&&... args)
    {
        auto child = std::make_unique<T>(std::forward<CtorArgs>(args)...);
        T& ref = *child;
        attach_child(std::move(child));
        return ref;
    }

    Element* attach_child(std::unique_ptr<Element> child);
    std::unique_ptr<Element> detach_child(Element* child);

    [[nodiscard]] Element* find_by_name(std::string_view target_name) noexcept;
    [[nodiscard]] Element* find_by_uuid(std::uint64_t target_uuid) noexcept;

    template <typename T>
    void collect_by_type(std::vector<T*>& out) noexcept
    {
        if (auto* casted = dynamic_cast<T*>(this))
            out.push_back(casted);
        for (const auto& c : children_)
        {
            c->collect_by_type<T>(out);
        }
    }

    // ---- Lifecycle & Reflection --------------------------------------------

    void mount_tree();
    void unmount_tree();
    void tick_tree(float dt);
    void fixed_tick_tree(float fixed_dt);

    virtual void on_mount() {}
    virtual void on_unmount() {}
    virtual void on_tick(float /*dt*/) {}
    virtual void on_fixed_tick(float /*fixed_dt*/) {}

    virtual void reflect_properties(std::vector<PropertyDesc>& out) const;

    Pulse<Element*> on_mounted_pulse {};
    Pulse<Element*> on_unmounted_pulse {};

private:
    std::uint64_t uuid_ { 0 };
    std::string name_ { "Element" };
    bool enabled_ { true };
    bool mounted_ { false };
    Element* parent_ { nullptr };
    std::vector<std::unique_ptr<Element>> children_ {};
};

// ---- Planar2D: 2D Spatial Base Element --------------------------------------

class Planar2D : public Element
{
public:
    explicit Planar2D(std::string name = "Planar2D")
        : Element(std::move(name))
    {
    }

    [[nodiscard]] std::string_view class_name() const noexcept override
    {
        return "Planar2D";
    }

    [[nodiscard]] std::array<float, 2> local_position() const noexcept { return local_pos_; }
    void set_local_position(float x, float y) noexcept { local_pos_ = { x, y }; }

    [[nodiscard]] float local_rotation_rad() const noexcept { return local_rot_rad_; }
    void set_local_rotation_rad(float r) noexcept { local_rot_rad_ = r; }

    [[nodiscard]] std::array<float, 2> local_scale() const noexcept { return local_scale_; }
    void set_local_scale(float sx, float sy) noexcept { local_scale_ = { sx, sy }; }

    [[nodiscard]] std::int32_t z_index() const noexcept { return z_index_; }
    void set_z_index(std::int32_t z) noexcept { z_index_ = z; }

    [[nodiscard]] std::array<float, 2> world_position() const noexcept;

    void reflect_properties(std::vector<PropertyDesc>& out) const override;

private:
    std::array<float, 2> local_pos_ { 0.0F, 0.0F };
    float local_rot_rad_ { 0.0F };
    std::array<float, 2> local_scale_ { 1.0F, 1.0F };
    std::int32_t z_index_ { 0 };
};

// ---- Spatial3D: 3D Spatial Base Element -------------------------------------

class Spatial3D : public Element
{
public:
    explicit Spatial3D(std::string name = "Spatial3D")
        : Element(std::move(name))
    {
    }

    [[nodiscard]] std::string_view class_name() const noexcept override
    {
        return "Spatial3D";
    }

    [[nodiscard]] std::array<float, 3> local_position() const noexcept { return local_pos_; }
    void set_local_position(float x, float y, float z) noexcept { local_pos_ = { x, y, z }; }

    [[nodiscard]] std::array<float, 3> local_euler_deg() const noexcept { return local_euler_deg_; }
    void set_local_euler_deg(float pitch, float yaw, float roll) noexcept
    {
        local_euler_deg_ = { pitch, yaw, roll };
    }

    [[nodiscard]] std::array<float, 3> local_scale() const noexcept { return local_scale_; }
    void set_local_scale(float sx, float sy, float sz) noexcept { local_scale_ = { sx, sy, sz }; }

    [[nodiscard]] Mobility mobility() const noexcept { return mobility_; }
    void set_mobility(Mobility m) noexcept { mobility_ = m; }

    /// Compute accumulated world-space position walking up any `Spatial3D` ancestors.
    [[nodiscard]] std::array<float, 3> world_position() const noexcept;

    void reflect_properties(std::vector<PropertyDesc>& out) const override;

private:
    std::array<float, 3> local_pos_ { 0.0F, 0.0F, 0.0F };
    std::array<float, 3> local_euler_deg_ { 0.0F, 0.0F, 0.0F };
    std::array<float, 3> local_scale_ { 1.0F, 1.0F, 1.0F };
    Mobility mobility_ { Mobility::kMovable };
};

// ---- Stage: .cdstage Scene / Prefab Container -------------------------------

class Stage
{
public:
    explicit Stage(std::string stage_name = "UntitledStage");

    [[nodiscard]] std::string_view name() const noexcept { return name_; }
    void set_name(std::string n) { name_ = std::move(n); }

    [[nodiscard]] Element& root() noexcept { return *root_; }
    [[nodiscard]] const Element& root() const noexcept { return *root_; }

    void mount();
    void unmount();
    void tick(float dt);
    void fixed_tick(float fixed_dt);

    [[nodiscard]] Element* find_by_name(std::string_view target_name) noexcept
    {
        return root_->find_by_name(target_name);
    }

    [[nodiscard]] Element* find_by_uuid(std::uint64_t target_uuid) noexcept
    {
        return root_->find_by_uuid(target_uuid);
    }

    template <typename T>
    [[nodiscard]] std::vector<T*> find_all() noexcept
    {
        std::vector<T*> out;
        root_->collect_by_type<T>(out);
        return out;
    }

    /// Serialize the entire Stage hierarchy and reflected properties to the
    /// deterministic `.cdstage` text format (used by PIE snapshots and asset files).
    [[nodiscard]] std::string serialize_cdstage() const;

    /// Instantiate a nested prefab `Stage`'s children under `parent_element` (or `root()`).
    Element* instantiate_prefab(std::string_view instance_name, Element* parent_element = nullptr);

private:
    std::string name_;
    std::unique_ptr<Element> root_;
};

}  // namespace cd::element_core
