// =============================================================================
// CHROMODYNAMIC — cd/rhi/Barriers.hpp
// ADR-001 (Sprint S3.1) — resource state barriers and synchronization scopes.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/rhi/Enums.hpp>
#include <cd/rhi/Handles.hpp>

#include <cstdint>

namespace cd::rhi
{

struct BufferBarrier
{
    BufferHandle buffer {};
    ResourceState from { ResourceState::kUndefined };
    ResourceState to { ResourceState::kUndefined };
    std::uint64_t offset { 0 };
    std::uint64_t size { 0 };  // 0 == whole-buffer
};

struct TextureSubresourceRange
{
    std::uint32_t base_mip { 0 }, mip_count { 1 };
    std::uint32_t base_layer { 0 }, layer_count { 1 };
};

struct TextureBarrier
{
    TextureHandle texture {};
    ResourceState from { ResourceState::kUndefined };
    ResourceState to { ResourceState::kUndefined };
    TextureSubresourceRange range {};
};

}  // namespace cd::rhi
