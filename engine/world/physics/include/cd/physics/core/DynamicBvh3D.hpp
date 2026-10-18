// =============================================================================
// CHROMODYNAMIC — cd/physics/core/DynamicBvh3D.hpp
// Dynamic 3D Bounding Volume Hierarchy with fat AABBs, surface-area heuristic
// (SAH) insertion, AVL height balancing, AABB/all-pairs broadphase queries,
// and slab raycasting.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/math/Vector.hpp>
#include <cd/physics/core/SpatialAabb3D.hpp>

#include <algorithm>
#include <cstdint>
#include <type_traits>
#include <vector>

namespace cd::physics::core
{

class DynamicBvh3D
{
public:
    static constexpr std::int32_t kNullNode               = -1;
    static constexpr float        kDefaultMargin          = 0.1F;
    static constexpr float        kDisplacementMultiplier = 2.0F;

    DynamicBvh3D() = default;

    [[nodiscard]] std::int32_t insert_proxy(std::uint32_t        user_id,
                                            const SpatialAabb3D& aabb,
                                            float                margin = kDefaultMargin)
    {
        const std::int32_t proxy_id    = allocate_node();
        node_at(proxy_id).aabb        = aabb.fattened(margin);
        node_at(proxy_id).user_id     = user_id;
        node_at(proxy_id).height      = 0;
        node_at(proxy_id).is_leaf_tag = true;

        insert_leaf(proxy_id);
        ++proxy_count_;
        return proxy_id;
    }

    void remove_proxy(std::int32_t proxy_id)
    {
        if (!is_valid_leaf(proxy_id))
        {
            return;
        }
        remove_leaf(proxy_id);
        free_node(proxy_id);
        --proxy_count_;
    }

    bool move_proxy(std::int32_t         proxy_id,
                    const SpatialAabb3D& new_aabb,
                    cd::math::Vec3f      displacement = { 0.0F, 0.0F, 0.0F },
                    float                margin       = kDefaultMargin)
    {
        if (!is_valid_leaf(proxy_id))
        {
            return false;
        }

        if (node_at(proxy_id).aabb.contains(new_aabb))
        {
            return false;
        }

        remove_leaf(proxy_id);

        SpatialAabb3D         fat = new_aabb.fattened(margin);
        const cd::math::Vec3f d   = displacement * kDisplacementMultiplier;
        if (d.x < 0.0F)
        {
            fat.min.x += d.x;
        }
        else
        {
            fat.max.x += d.x;
        }
        if (d.y < 0.0F)
        {
            fat.min.y += d.y;
        }
        else
        {
            fat.max.y += d.y;
        }
        if (d.z < 0.0F)
        {
            fat.min.z += d.z;
        }
        else
        {
            fat.max.z += d.z;
        }

        node_at(proxy_id).aabb = fat;
        insert_leaf(proxy_id);
        return true;
    }

    [[nodiscard]] std::uint32_t get_user_id(std::int32_t proxy_id) const noexcept
    {
        if (!is_valid_leaf(proxy_id))
        {
            return 0u;
        }
        return node_at(proxy_id).user_id;
    }

    [[nodiscard]] const SpatialAabb3D& get_fat_aabb(std::int32_t proxy_id) const noexcept
    {
        static constexpr SpatialAabb3D kEmpty {};
        if (proxy_id < 0 || static_cast<std::size_t>(proxy_id) >= nodes_.size() || node_at(proxy_id).height < 0)
        {
            return kEmpty;
        }
        return node_at(proxy_id).aabb;
    }

    template <typename Callback>
    void query_aabb(const SpatialAabb3D& aabb, Callback&& cb) const
    {
        if (root_ == kNullNode)
        {
            return;
        }

        std::vector<std::int32_t> stack;
        stack.reserve(64);
        stack.push_back(root_);

        while (!stack.empty())
        {
            const std::int32_t node_id = stack.back();
            stack.pop_back();
            if (node_id == kNullNode)
            {
                continue;
            }

            const Node& node = node_at(node_id);
            if (!node.aabb.overlaps(aabb))
            {
                continue;
            }

            if (node.is_leaf())
            {
                if constexpr (std::is_same_v<std::invoke_result_t<Callback&, std::uint32_t>, bool>)
                {
                    if (!cb(node.user_id))
                    {
                        return;
                    }
                }
                else
                {
                    cb(node.user_id);
                }
            }
            else
            {
                stack.push_back(node.left);
                stack.push_back(node.right);
            }
        }
    }

    template <typename Callback>
    void query_all_pairs(Callback&& cb) const
    {
        if (root_ == kNullNode || proxy_count_ < 2u)
        {
            return;
        }

        std::vector<std::int32_t> stack;
        stack.reserve(64);

        const auto total_nodes = static_cast<std::int32_t>(nodes_.size());
        for (std::int32_t leaf_a = 0; leaf_a < total_nodes; ++leaf_a)
        {
            if (!is_valid_leaf(leaf_a))
            {
                continue;
            }

            const SpatialAabb3D& aabb_a = node_at(leaf_a).aabb;
            const std::uint32_t  user_a = node_at(leaf_a).user_id;

            stack.clear();
            stack.push_back(root_);

            while (!stack.empty())
            {
                const std::int32_t node_id = stack.back();
                stack.pop_back();
                if (node_id == kNullNode)
                {
                    continue;
                }

                const Node& node = node_at(node_id);
                if (!node.aabb.overlaps(aabb_a))
                {
                    continue;
                }

                if (node.is_leaf())
                {
                    if (node_id > leaf_a)
                    {
                        if constexpr (std::is_same_v<std::invoke_result_t<Callback&, std::uint32_t, std::uint32_t>,
                                                     bool>)
                        {
                            if (!cb(user_a, node.user_id))
                            {
                                return;
                            }
                        }
                        else
                        {
                            cb(user_a, node.user_id);
                        }
                    }
                }
                else
                {
                    stack.push_back(node.left);
                    stack.push_back(node.right);
                }
            }
        }
    }

    template <typename Callback>
    void raycast(cd::math::Vec3f origin, cd::math::Vec3f dir, float max_fraction, Callback&& cb) const
    {
        if (root_ == kNullNode || max_fraction < 0.0F)
        {
            return;
        }

        float current_max_fraction = max_fraction;

        std::vector<std::int32_t> stack;
        stack.reserve(64);
        stack.push_back(root_);

        while (!stack.empty())
        {
            const std::int32_t node_id = stack.back();
            stack.pop_back();
            if (node_id == kNullNode)
            {
                continue;
            }

            const Node& node = node_at(node_id);
            float       tmin = 0.0F;
            if (!node.aabb.raycast(origin, dir, current_max_fraction, tmin))
            {
                continue;
            }

            if (node.is_leaf())
            {
                if constexpr (std::is_invocable_v<Callback&, std::uint32_t, float>)
                {
                    using Ret = std::invoke_result_t<Callback&, std::uint32_t, float>;
                    if constexpr (std::is_floating_point_v<Ret>)
                    {
                        const auto next_frac = static_cast<float>(cb(node.user_id, tmin));
                        if (next_frac <= 0.0F)
                        {
                            return;
                        }
                        if (next_frac < current_max_fraction)
                        {
                            current_max_fraction = next_frac;
                        }
                    }
                    else if constexpr (std::is_same_v<Ret, bool>)
                    {
                        if (!cb(node.user_id, tmin))
                        {
                            return;
                        }
                    }
                    else
                    {
                        cb(node.user_id, tmin);
                    }
                }
                else if constexpr (std::is_invocable_v<Callback&, std::uint32_t>)
                {
                    using Ret = std::invoke_result_t<Callback&, std::uint32_t>;
                    if constexpr (std::is_floating_point_v<Ret>)
                    {
                        const auto next_frac = static_cast<float>(cb(node.user_id));
                        if (next_frac <= 0.0F)
                        {
                            return;
                        }
                        if (next_frac < current_max_fraction)
                        {
                            current_max_fraction = next_frac;
                        }
                    }
                    else if constexpr (std::is_same_v<Ret, bool>)
                    {
                        if (!cb(node.user_id))
                        {
                            return;
                        }
                    }
                    else
                    {
                        cb(node.user_id);
                    }
                }
            }
            else
            {
                stack.push_back(node.left);
                stack.push_back(node.right);
            }
        }
    }

    [[nodiscard]] std::uint32_t proxy_count() const noexcept
    {
        return proxy_count_;
    }

    void clear() noexcept
    {
        nodes_.clear();
        root_        = kNullNode;
        free_list_   = kNullNode;
        proxy_count_ = 0u;
    }

private:
    struct Node
    {
        SpatialAabb3D aabb {};
        std::uint32_t user_id { 0u };
        std::int32_t  parent_or_next { kNullNode };
        std::int32_t  left { kNullNode };
        std::int32_t  right { kNullNode };
        std::int32_t  height { -1 };
        bool          is_leaf_tag { false };

        [[nodiscard]] constexpr bool is_leaf() const noexcept
        {
            return left == kNullNode;
        }
    };

    [[nodiscard]] Node& node_at(std::int32_t idx) noexcept
    {
        return nodes_[static_cast<std::size_t>(idx)];
    }

    [[nodiscard]] const Node& node_at(std::int32_t idx) const noexcept
    {
        return nodes_[static_cast<std::size_t>(idx)];
    }

    [[nodiscard]] bool is_valid_leaf(std::int32_t proxy_id) const noexcept
    {
        return proxy_id >= 0 && static_cast<std::size_t>(proxy_id) < nodes_.size() &&
               node_at(proxy_id).height >= 0 && node_at(proxy_id).is_leaf();
    }

    [[nodiscard]] std::int32_t allocate_node()
    {
        if (free_list_ == kNullNode)
        {
            const auto new_idx = static_cast<std::int32_t>(nodes_.size());
            nodes_.push_back(Node {});
            node_at(new_idx).parent_or_next = kNullNode;
            node_at(new_idx).left           = kNullNode;
            node_at(new_idx).right          = kNullNode;
            node_at(new_idx).height         = 0;
            return new_idx;
        }

        const std::int32_t node_id      = free_list_;
        free_list_                      = node_at(node_id).parent_or_next;
        node_at(node_id).parent_or_next = kNullNode;
        node_at(node_id).left           = kNullNode;
        node_at(node_id).right          = kNullNode;
        node_at(node_id).height         = 0;
        node_at(node_id).is_leaf_tag    = false;
        return node_id;
    }

    void free_node(std::int32_t node_id) noexcept
    {
        node_at(node_id).parent_or_next = free_list_;
        node_at(node_id).left           = kNullNode;
        node_at(node_id).right          = kNullNode;
        node_at(node_id).height         = -1;
        node_at(node_id).is_leaf_tag    = false;
        free_list_                      = node_id;
    }

    void insert_leaf(std::int32_t leaf)
    {
        if (root_ == kNullNode)
        {
            root_                         = leaf;
            node_at(root_).parent_or_next = kNullNode;
            return;
        }

        const SpatialAabb3D leaf_aabb = node_at(leaf).aabb;
        std::int32_t        index     = root_;
        while (!node_at(index).is_leaf())
        {
            const std::int32_t child1 = node_at(index).left;
            const std::int32_t child2 = node_at(index).right;

            const float area          = node_at(index).aabb.surface_area();
            const float combined_area = SpatialAabb3D::merge(node_at(index).aabb, leaf_aabb).surface_area();

            const float cost             = 2.0F * combined_area;
            const float inheritance_cost = 2.0F * (combined_area - area);

            float cost1 = 0.0F;
            if (node_at(child1).is_leaf())
            {
                cost1 = SpatialAabb3D::merge(leaf_aabb, node_at(child1).aabb).surface_area() + inheritance_cost;
            }
            else
            {
                const float old_area = node_at(child1).aabb.surface_area();
                const float new_area = SpatialAabb3D::merge(leaf_aabb, node_at(child1).aabb).surface_area();
                cost1                = (new_area - old_area) + inheritance_cost;
            }

            float cost2 = 0.0F;
            if (node_at(child2).is_leaf())
            {
                cost2 = SpatialAabb3D::merge(leaf_aabb, node_at(child2).aabb).surface_area() + inheritance_cost;
            }
            else
            {
                const float old_area = node_at(child2).aabb.surface_area();
                const float new_area = SpatialAabb3D::merge(leaf_aabb, node_at(child2).aabb).surface_area();
                cost2                = (new_area - old_area) + inheritance_cost;
            }

            if (cost < cost1 && cost < cost2)
            {
                break;
            }

            index = (cost1 < cost2) ? child1 : child2;
        }

        const std::int32_t sibling    = index;
        const std::int32_t old_parent = node_at(sibling).parent_or_next;
        const std::int32_t new_parent = allocate_node();

        node_at(new_parent).parent_or_next = old_parent;
        node_at(new_parent).user_id        = 0u;
        node_at(new_parent).aabb           = SpatialAabb3D::merge(leaf_aabb, node_at(sibling).aabb);
        node_at(new_parent).height         = node_at(sibling).height + 1;

        if (old_parent != kNullNode)
        {
            if (node_at(old_parent).left == sibling)
            {
                node_at(old_parent).left = new_parent;
            }
            else
            {
                node_at(old_parent).right = new_parent;
            }
            node_at(new_parent).left        = sibling;
            node_at(new_parent).right       = leaf;
            node_at(sibling).parent_or_next = new_parent;
            node_at(leaf).parent_or_next    = new_parent;
        }
        else
        {
            node_at(new_parent).left        = sibling;
            node_at(new_parent).right       = leaf;
            node_at(sibling).parent_or_next = new_parent;
            node_at(leaf).parent_or_next    = new_parent;
            root_                           = new_parent;
        }

        refit_ancestors(node_at(leaf).parent_or_next);
    }

    void remove_leaf(std::int32_t leaf)
    {
        if (leaf == root_)
        {
            root_ = kNullNode;
            return;
        }

        const std::int32_t parent       = node_at(leaf).parent_or_next;
        const std::int32_t grand_parent = node_at(parent).parent_or_next;
        const std::int32_t sibling      = (node_at(parent).left == leaf) ? node_at(parent).right : node_at(parent).left;

        if (grand_parent != kNullNode)
        {
            if (node_at(grand_parent).left == parent)
            {
                node_at(grand_parent).left = sibling;
            }
            else
            {
                node_at(grand_parent).right = sibling;
            }
            node_at(sibling).parent_or_next = grand_parent;
            free_node(parent);

            refit_ancestors(grand_parent);
        }
        else
        {
            root_                           = sibling;
            node_at(sibling).parent_or_next = kNullNode;
            free_node(parent);
        }
    }

    void refit_ancestors(std::int32_t index)
    {
        while (index != kNullNode)
        {
            index = balance(index);

            const std::int32_t child1 = node_at(index).left;
            const std::int32_t child2 = node_at(index).right;

            node_at(index).height = 1 + std::max(node_at(child1).height, node_at(child2).height);
            node_at(index).aabb   = SpatialAabb3D::merge(node_at(child1).aabb, node_at(child2).aabb);

            index = node_at(index).parent_or_next;
        }
    }

    [[nodiscard]] std::int32_t balance(std::int32_t i_a)
    {
        if (node_at(i_a).is_leaf() || node_at(i_a).height < 2)
        {
            return i_a;
        }

        const std::int32_t i_b         = node_at(i_a).left;
        const std::int32_t i_c         = node_at(i_a).right;
        const std::int32_t balance_val = node_at(i_c).height - node_at(i_b).height;

        if (balance_val > 1)
        {
            const std::int32_t i_f = node_at(i_c).left;
            const std::int32_t i_g = node_at(i_c).right;

            node_at(i_c).left           = i_a;
            node_at(i_c).parent_or_next = node_at(i_a).parent_or_next;
            node_at(i_a).parent_or_next = i_c;

            if (node_at(i_c).parent_or_next != kNullNode)
            {
                if (node_at(node_at(i_c).parent_or_next).left == i_a)
                {
                    node_at(node_at(i_c).parent_or_next).left = i_c;
                }
                else
                {
                    node_at(node_at(i_c).parent_or_next).right = i_c;
                }
            }
            else
            {
                root_ = i_c;
            }

            if (node_at(i_f).height > node_at(i_g).height)
            {
                node_at(i_c).right          = i_f;
                node_at(i_a).right          = i_g;
                node_at(i_g).parent_or_next = i_a;
                node_at(i_a).aabb           = SpatialAabb3D::merge(node_at(i_b).aabb, node_at(i_g).aabb);
                node_at(i_c).aabb           = SpatialAabb3D::merge(node_at(i_a).aabb, node_at(i_f).aabb);
                node_at(i_a).height         = 1 + std::max(node_at(i_b).height, node_at(i_g).height);
                node_at(i_c).height         = 1 + std::max(node_at(i_a).height, node_at(i_f).height);
            }
            else
            {
                node_at(i_c).right          = i_g;
                node_at(i_a).right          = i_f;
                node_at(i_f).parent_or_next = i_a;
                node_at(i_a).aabb           = SpatialAabb3D::merge(node_at(i_b).aabb, node_at(i_f).aabb);
                node_at(i_c).aabb           = SpatialAabb3D::merge(node_at(i_a).aabb, node_at(i_g).aabb);
                node_at(i_a).height         = 1 + std::max(node_at(i_b).height, node_at(i_f).height);
                node_at(i_c).height         = 1 + std::max(node_at(i_a).height, node_at(i_g).height);
            }

            return i_c;
        }

        if (balance_val < -1)
        {
            const std::int32_t i_d = node_at(i_b).left;
            const std::int32_t i_e = node_at(i_b).right;

            node_at(i_b).left           = i_a;
            node_at(i_b).parent_or_next = node_at(i_a).parent_or_next;
            node_at(i_a).parent_or_next = i_b;

            if (node_at(i_b).parent_or_next != kNullNode)
            {
                if (node_at(node_at(i_b).parent_or_next).left == i_a)
                {
                    node_at(node_at(i_b).parent_or_next).left = i_b;
                }
                else
                {
                    node_at(node_at(i_b).parent_or_next).right = i_b;
                }
            }
            else
            {
                root_ = i_b;
            }

            if (node_at(i_d).height > node_at(i_e).height)
            {
                node_at(i_b).right          = i_d;
                node_at(i_a).left           = i_e;
                node_at(i_e).parent_or_next = i_a;
                node_at(i_a).aabb           = SpatialAabb3D::merge(node_at(i_c).aabb, node_at(i_e).aabb);
                node_at(i_b).aabb           = SpatialAabb3D::merge(node_at(i_a).aabb, node_at(i_d).aabb);
                node_at(i_a).height         = 1 + std::max(node_at(i_c).height, node_at(i_e).height);
                node_at(i_b).height         = 1 + std::max(node_at(i_a).height, node_at(i_d).height);
            }
            else
            {
                node_at(i_b).right          = i_e;
                node_at(i_a).left           = i_d;
                node_at(i_d).parent_or_next = i_a;
                node_at(i_a).aabb           = SpatialAabb3D::merge(node_at(i_c).aabb, node_at(i_d).aabb);
                node_at(i_b).aabb           = SpatialAabb3D::merge(node_at(i_a).aabb, node_at(i_e).aabb);
                node_at(i_a).height         = 1 + std::max(node_at(i_c).height, node_at(i_d).height);
                node_at(i_b).height         = 1 + std::max(node_at(i_a).height, node_at(i_e).height);
            }

            return i_b;
        }

        return i_a;
    }

    std::vector<Node> nodes_ {};
    std::int32_t      root_ { kNullNode };
    std::int32_t      free_list_ { kNullNode };
    std::uint32_t     proxy_count_ { 0u };
};

}  // namespace cd::physics::core
