// =============================================================================
// CHROMODYNAMIC — cd/physics/rockstar/FractureDestruction3D.hpp
// Voronoi / Stress-Graph Real-Time Structural Destruction & Debris Solver.
//
// Models destructible structures as a graph of `FractureChunk3D` nodes joined by
// `StructuralBond3D` stress edges. Bottom-tier foundation chunks act as static
// ground anchors; when ballistic/explosive impact impulses sever bonds, a
// breadth-first connectivity pass detaches any unsupported structural islands
// into free dynamic debris stepped with gravity and floor restitution.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/math/Functions.hpp>
#include <cd/math/Vector.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace cd::physics::rockstar
{

struct FractureChunk3D
{
    std::uint32_t   chunk_id         { 0u };
    cd::math::Vec3f local_centroid   {};
    cd::math::Vec3f half_extents     { 0.25F, 0.25F, 0.25F };
    float           mass_kg          { 5.0F };
    bool            detached         { false };
    cd::math::Vec3f world_pos        {};
    cd::math::Vec3f linear_velocity  {};
    cd::math::Vec3f angular_velocity {};
};

struct StructuralBond3D
{
    std::uint32_t chunk_a               { 0u };
    std::uint32_t chunk_b               { 0u };
    float         max_impulse_threshold { 85.0F };
    float         accumulated_damage    { 0.0F };
    bool          broken                { false };
};

class FractureDestructionSystem3D
{
public:
    FractureDestructionSystem3D() = default;

    /// Generates a 3D Voronoi/grid destructible assembly of (nx * ny * nz) chunks.
    /// Bottom layer (iy == 0) chunks are anchored to the foundation until their
    /// internal bonds or support columns are severed.
    void generate_grid_fracture_object(
        std::uint32_t nx,
        std::uint32_t ny,
        std::uint32_t nz,
        cd::math::Vec3f base_world_pos = { 0.0F, 0.0F, 0.0F },
        cd::math::Vec3f chunk_half_extents = { 0.25F, 0.25F, 0.25F },
        float chunk_mass_kg = 5.0F,
        float bond_impulse_threshold = 85.0F)
    {
        chunks_.clear();
        bonds_.clear();
        anchored_flags_.clear();

        if (nx == 0u || ny == 0u || nz == 0u)
        {
            return;
        }

        const std::size_t total_chunks =
            static_cast<std::size_t>(nx) * static_cast<std::size_t>(ny) * static_cast<std::size_t>(nz);
        chunks_.reserve(total_chunks);
        anchored_flags_.assign(total_chunks, false);

        const cd::math::Vec3f cell_size = chunk_half_extents * 2.0F;
        const float half_span_x = 0.5F * static_cast<float>(nx - 1u) * cell_size.x;
        const float half_span_z = 0.5F * static_cast<float>(nz - 1u) * cell_size.z;

        auto grid_id = [nx, ny](std::uint32_t ix, std::uint32_t iy, std::uint32_t iz) noexcept -> std::uint32_t
        {
            return (iz * ny + iy) * nx + ix;
        };

        for (std::uint32_t iz = 0u; iz < nz; ++iz)
        {
            for (std::uint32_t iy = 0u; iy < ny; ++iy)
            {
                for (std::uint32_t ix = 0u; ix < nx; ++ix)
                {
                    const std::uint32_t id = grid_id(ix, iy, iz);
                    const cd::math::Vec3f local_pos {
                        static_cast<float>(ix) * cell_size.x - half_span_x,
                        (static_cast<float>(iy) + 0.5F) * cell_size.y,
                        static_cast<float>(iz) * cell_size.z - half_span_z
                    };

                    FractureChunk3D chunk {};
                    chunk.chunk_id = id;
                    chunk.local_centroid = local_pos;
                    chunk.half_extents = chunk_half_extents;
                    chunk.mass_kg = std::max(chunk_mass_kg, 0.1F);
                    chunk.detached = false;
                    chunk.world_pos = base_world_pos + local_pos;
                    chunks_.push_back(chunk);

                    if (iy == 0u)
                    {
                        anchored_flags_[id] = true;
                    }

                    if (ix + 1u < nx)
                    {
                        bonds_.push_back(StructuralBond3D {
                            id, grid_id(ix + 1u, iy, iz), bond_impulse_threshold, 0.0F, false });
                    }
                    if (iy + 1u < ny)
                    {
                        bonds_.push_back(StructuralBond3D {
                            id, grid_id(ix, iy + 1u, iz), bond_impulse_threshold, 0.0F, false });
                    }
                    if (iz + 1u < nz)
                    {
                        bonds_.push_back(StructuralBond3D {
                            id, grid_id(ix, iy, iz + 1u), bond_impulse_threshold, 0.0F, false });
                    }
                }
            }
        }
    }

    /// Overload accepting world origin first for ergonomic call sites.
    void generate_grid_fracture_object(
        cd::math::Vec3f base_world_pos,
        std::uint32_t nx,
        std::uint32_t ny,
        std::uint32_t nz,
        cd::math::Vec3f chunk_half_extents = { 0.25F, 0.25F, 0.25F },
        float bond_impulse_threshold = 85.0F)
    {
        generate_grid_fracture_object(
            nx, ny, nz, base_world_pos, chunk_half_extents, 5.0F, bond_impulse_threshold);
    }

    /// Applies a radial impact/blast impulse at `hit_point`, breaking structural
    /// bonds that exceed their impulse threshold, detaching unsupported islands,
    /// and imparting ballistic debris velocities. Returns total detached chunk count.
    std::uint32_t apply_impact_impulse(
        cd::math::Vec3f hit_point,
        float radius,
        float impulse_magnitude) noexcept
    {
        if (chunks_.empty() || radius <= 1e-5F || impulse_magnitude <= 0.0F)
        {
            return detached_chunk_count();
        }

        // 1. Accumulate stress damage on bonds within blast radius
        for (auto& bond : bonds_)
        {
            if (bond.broken || bond.chunk_a >= chunks_.size() || bond.chunk_b >= chunks_.size())
            {
                continue;
            }

            const cd::math::Vec3f bond_midpoint =
                (chunks_[bond.chunk_a].world_pos + chunks_[bond.chunk_b].world_pos) * 0.5F;
            const float dist = cd::math::length(bond_midpoint - hit_point);
            if (dist <= radius)
            {
                const float falloff = 1.0F - (dist / radius) * 0.75F;
                bond.accumulated_damage += impulse_magnitude * std::max(0.15F, falloff);
                if (bond.accumulated_damage >= bond.max_impulse_threshold)
                {
                    bond.broken = true;
                }
            }
        }

        // 2. Also un-anchor foundation chunks directly hit by severe blast impulses
        for (std::size_t i = 0; i < chunks_.size(); ++i)
        {
            const float dist = cd::math::length(chunks_[i].world_pos - hit_point);
            if (dist <= radius)
            {
                const float local_impulse = impulse_magnitude * (1.0F - 0.65F * (dist / radius));
                if (local_impulse >= 1.25F * default_bond_threshold())
                {
                    anchored_flags_[i] = false;
                }
            }
        }

        // 3. Recompute structural connectivity graph (BFS from remaining foundation anchors)
        update_detached_islands();

        // 4. Impart radial blast velocity & tumble angular velocity to detached chunks
        for (auto& chunk : chunks_)
        {
            if (!chunk.detached)
            {
                continue;
            }

            const cd::math::Vec3f delta = chunk.world_pos - hit_point;
            const float dist = cd::math::length(delta);
            if (dist <= radius * 1.35F)
            {
                const float falloff = std::clamp(1.0F - dist / (radius * 1.35F), 0.15F, 1.0F);
                cd::math::Vec3f dir = (dist > 1e-4F) ? (delta / dist) : cd::math::Vec3f { 0.0F, 1.0F, 0.0F };
                dir.y = std::max(dir.y, 0.30F);
                dir = cd::math::normalize(dir);

                const float dv = (impulse_magnitude * falloff) / std::max(chunk.mass_kg, 0.25F);
                chunk.linear_velocity += dir * dv;
                chunk.angular_velocity += cd::math::Vec3f {
                    dir.z * dv * 0.65F,
                    (dir.x - dir.z) * dv * 0.35F,
                    -dir.x * dv * 0.65F
                };
            }
        }

        return detached_chunk_count();
    }

    /// Integrates free-flying detached debris chunks with gravity and floor collision.
    void step_debris(float dt, float floor_y = 0.0F) noexcept
    {
        if (dt <= 0.0F)
        {
            return;
        }

        constexpr float kGravityY = -9.81F;
        constexpr float kRestitution = 0.32F;
        constexpr float kFloorFriction = 0.78F;

        for (auto& chunk : chunks_)
        {
            if (!chunk.detached)
            {
                continue;
            }

            chunk.linear_velocity.y += kGravityY * dt;
            chunk.linear_velocity *= std::max(0.0F, 1.0F - 0.15F * dt);
            chunk.angular_velocity *= std::max(0.0F, 1.0F - 0.45F * dt);

            chunk.world_pos += chunk.linear_velocity * dt;

            const float min_center_y = floor_y + chunk.half_extents.y;
            if (chunk.world_pos.y < min_center_y)
            {
                chunk.world_pos.y = min_center_y;
                if (chunk.linear_velocity.y < 0.0F)
                {
                    chunk.linear_velocity.y = -chunk.linear_velocity.y * kRestitution;
                    if (std::abs(chunk.linear_velocity.y) < 0.25F)
                    {
                        chunk.linear_velocity.y = 0.0F;
                    }
                }
                chunk.linear_velocity.x *= kFloorFriction;
                chunk.linear_velocity.z *= kFloorFriction;
                chunk.angular_velocity *= kFloorFriction;
            }
        }
    }

    [[nodiscard]] std::span<const FractureChunk3D> chunks() const noexcept
    {
        return chunks_;
    }

    [[nodiscard]] std::span<const StructuralBond3D> bonds() const noexcept
    {
        return bonds_;
    }

    [[nodiscard]] std::uint32_t detached_chunk_count() const noexcept
    {
        std::uint32_t count = 0u;
        for (const auto& c : chunks_)
        {
            if (c.detached)
            {
                ++count;
            }
        }
        return count;
    }

    [[nodiscard]] std::uint32_t broken_bond_count() const noexcept
    {
        std::uint32_t count = 0u;
        for (const auto& b : bonds_)
        {
            if (b.broken)
            {
                ++count;
            }
        }
        return count;
    }

private:
    [[nodiscard]] float default_bond_threshold() const noexcept
    {
        return bonds_.empty() ? 85.0F : bonds_.front().max_impulse_threshold;
    }

    void update_detached_islands() noexcept
    {
        const std::size_t n = chunks_.size();
        if (n == 0u)
        {
            return;
        }

        std::vector<bool> visited(n, false);
        std::vector<std::uint32_t> queue;
        queue.reserve(n);

        for (std::size_t i = 0; i < n; ++i)
        {
            if (anchored_flags_[i] && !chunks_[i].detached)
            {
                // Check if this anchored chunk still has at least one unbroken bond
                // (or is part of an intact foundation)
                bool has_intact_bond = false;
                for (const auto& b : bonds_)
                {
                    if (!b.broken && (b.chunk_a == i || b.chunk_b == i))
                    {
                        has_intact_bond = true;
                        break;
                    }
                }

                if (has_intact_bond || bonds_.empty())
                {
                    visited[i] = true;
                    queue.push_back(static_cast<std::uint32_t>(i));
                }
            }
        }

        std::size_t head = 0u;
        while (head < queue.size())
        {
            const std::uint32_t curr = queue[head++];
            for (const auto& b : bonds_)
            {
                if (b.broken)
                {
                    continue;
                }
                if (b.chunk_a == curr && b.chunk_b < n && !visited[b.chunk_b])
                {
                    visited[b.chunk_b] = true;
                    queue.push_back(b.chunk_b);
                }
                else if (b.chunk_b == curr && b.chunk_a < n && !visited[b.chunk_a])
                {
                    visited[b.chunk_a] = true;
                    queue.push_back(b.chunk_a);
                }
            }
        }

        for (std::size_t i = 0; i < n; ++i)
        {
            if (!visited[i])
            {
                chunks_[i].detached = true;
            }
        }
    }

    std::vector<FractureChunk3D>  chunks_ {};
    std::vector<StructuralBond3D> bonds_ {};
    std::vector<bool>             anchored_flags_ {};
};

}  // namespace cd::physics::rockstar
