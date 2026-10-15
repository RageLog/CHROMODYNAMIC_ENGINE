// =============================================================================
// CHROMODYNAMIC — cd/element_core/Element.cpp
// FAZ 5 Wave 5.1 — Element, Planar2D, Spatial3D, Stage Implementation.
// =============================================================================
#include <cd/element_core/Element.hpp>

#include <atomic>
#include <sstream>
#include <utility>

namespace cd::element_core
{

namespace
{

[[nodiscard]] std::uint64_t next_element_uuid() noexcept
{
    static std::atomic<std::uint64_t> counter { 1000U };
    return counter.fetch_add(1U, std::memory_order_relaxed);
}

void serialize_element_recursive(const Element& el, std::ostringstream& ss, std::size_t indent)
{
    const std::string pad(indent * 2U, ' ');
    ss << pad << "[Element class=\"" << el.class_name()
       << "\" name=\"" << el.name()
       << "\" uuid=" << el.uuid()
       << " enabled=" << (el.is_enabled() ? 1 : 0) << "]\n";

    std::vector<PropertyDesc> props;
    el.reflect_properties(props);
    for (const auto& p : props)
    {
        ss << pad << "  prop " << p.category << "." << p.name << " = " << p.serialized_value << "\n";
    }

    for (const auto& child : el.children())
    {
        serialize_element_recursive(*child, ss, indent + 1U);
    }
}

}  // namespace

// ---- Element ----------------------------------------------------------------

Element::Element(std::string name)
    : uuid_ { next_element_uuid() }
    , name_ { std::move(name) }
{
}

Element::~Element() = default;

Element* Element::attach_child(std::unique_ptr<Element> child)
{
    if (!child)
        return nullptr;
    child->parent_ = this;
    Element* raw = child.get();
    children_.push_back(std::move(child));
    if (mounted_)
    {
        raw->mount_tree();
    }
    return raw;
}

std::unique_ptr<Element> Element::detach_child(Element* child)
{
    if (child == nullptr)
        return nullptr;

    for (auto it = children_.begin(); it != children_.end(); ++it)
    {
        if (it->get() == child)
        {
            if ((*it)->mounted_)
                (*it)->unmount_tree();
            (*it)->parent_ = nullptr;
            auto owned = std::move(*it);
            children_.erase(it);
            return owned;
        }
    }
    return nullptr;
}

Element* Element::find_by_name(std::string_view target_name) noexcept
{
    if (name_ == target_name)
        return this;
    for (const auto& c : children_)
    {
        if (auto* found = c->find_by_name(target_name))
            return found;
    }
    return nullptr;
}

Element* Element::find_by_uuid(std::uint64_t target_uuid) noexcept
{
    if (uuid_ == target_uuid)
        return this;
    for (const auto& c : children_)
    {
        if (auto* found = c->find_by_uuid(target_uuid))
            return found;
    }
    return nullptr;
}

void Element::mount_tree()
{
    if (!mounted_)
    {
        mounted_ = true;
        on_mount();
        on_mounted_pulse.emit(this);
    }
    for (const auto& c : children_)
    {
        c->mount_tree();
    }
}

void Element::unmount_tree()
{
    for (const auto& c : children_)
    {
        c->unmount_tree();
    }
    if (mounted_)
    {
        on_unmount();
        on_unmounted_pulse.emit(this);
        mounted_ = false;
    }
}

void Element::tick_tree(float dt)
{
    if (!enabled_)
        return;
    on_tick(dt);
    for (const auto& c : children_)
    {
        c->tick_tree(dt);
    }
}

void Element::fixed_tick_tree(float fixed_dt)
{
    if (!enabled_)
        return;
    on_fixed_tick(fixed_dt);
    for (const auto& c : children_)
    {
        c->fixed_tick_tree(fixed_dt);
    }
}

void Element::reflect_properties(std::vector<PropertyDesc>& out) const
{
    out.push_back(PropertyDesc {
        "enabled",
        "Element",
        PropertyKind::kBool,
        0.0F,
        1.0F,
        enabled_ ? "true" : "false",
    });
}

// ---- Planar2D ---------------------------------------------------------------

std::array<float, 2> Planar2D::world_position() const noexcept
{
    std::array<float, 2> pos = local_pos_;
    const Element* cur = parent();
    while (cur != nullptr)
    {
        if (const auto* p2d = dynamic_cast<const Planar2D*>(cur))
        {
            const auto p_pos = p2d->local_position();
            const auto p_scale = p2d->local_scale();
            pos[0] = p_pos[0] + pos[0] * p_scale[0];
            pos[1] = p_pos[1] + pos[1] * p_scale[1];
        }
        cur = cur->parent();
    }
    return pos;
}

void Planar2D::reflect_properties(std::vector<PropertyDesc>& out) const
{
    Element::reflect_properties(out);
    out.push_back(PropertyDesc {
        "local_position",
        "Planar2D",
        PropertyKind::kVec2,
        -100000.0F,
        100000.0F,
        std::to_string(local_pos_[0]) + "," + std::to_string(local_pos_[1]),
    });
    out.push_back(PropertyDesc {
        "z_index",
        "Planar2D",
        PropertyKind::kInt,
        -1024.0F,
        1024.0F,
        std::to_string(z_index_),
    });
}

// ---- Spatial3D --------------------------------------------------------------

std::array<float, 3> Spatial3D::world_position() const noexcept
{
    std::array<float, 3> pos = local_pos_;
    const Element* cur = parent();
    while (cur != nullptr)
    {
        if (const auto* s3d = dynamic_cast<const Spatial3D*>(cur))
        {
            const auto p_pos = s3d->local_position();
            const auto p_scale = s3d->local_scale();
            pos[0] = p_pos[0] + pos[0] * p_scale[0];
            pos[1] = p_pos[1] + pos[1] * p_scale[1];
            pos[2] = p_pos[2] + pos[2] * p_scale[2];
        }
        cur = cur->parent();
    }
    return pos;
}

void Spatial3D::reflect_properties(std::vector<PropertyDesc>& out) const
{
    Element::reflect_properties(out);
    out.push_back(PropertyDesc {
        "local_position",
        "Spatial3D",
        PropertyKind::kVec3,
        -100000.0F,
        100000.0F,
        std::to_string(local_pos_[0]) + "," +
            std::to_string(local_pos_[1]) + "," +
            std::to_string(local_pos_[2]),
    });
    out.push_back(PropertyDesc {
        "local_scale",
        "Spatial3D",
        PropertyKind::kVec3,
        0.001F,
        1000.0F,
        std::to_string(local_scale_[0]) + "," +
            std::to_string(local_scale_[1]) + "," +
            std::to_string(local_scale_[2]),
    });
    out.push_back(PropertyDesc {
        "mobility",
        "Spatial3D",
        PropertyKind::kEnum,
        0.0F,
        2.0F,
        std::to_string(static_cast<int>(mobility_)),
    });
}

// ---- Stage ------------------------------------------------------------------

Stage::Stage(std::string stage_name)
    : name_ { std::move(stage_name) }
    , root_ { std::make_unique<Element>("StageRoot") }
{
}

void Stage::mount()
{
    root_->mount_tree();
}

void Stage::unmount()
{
    root_->unmount_tree();
}

void Stage::tick(float dt)
{
    root_->tick_tree(dt);
}

void Stage::fixed_tick(float fixed_dt)
{
    root_->fixed_tick_tree(fixed_dt);
}

std::string Stage::serialize_cdstage() const
{
    std::ostringstream ss;
    ss << "# CHROMODYNAMIC Stage (.cdstage v1)\n";
    ss << "stage \"" << name_ << "\"\n";
    serialize_element_recursive(*root_, ss, 0U);
    return ss.str();
}

Element* Stage::instantiate_prefab(std::string_view instance_name, Element* parent_element)
{
    Element* target = (parent_element != nullptr) ? parent_element : root_.get();
    auto prefab_root = std::make_unique<Spatial3D>(std::string { instance_name });
    return target->attach_child(std::move(prefab_root));
}

}  // namespace cd::element_core
