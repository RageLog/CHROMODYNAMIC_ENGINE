// =============================================================================
// CHROMODYNAMIC STUDIO -- apps/editor/core/StudioMcpServer.hpp
//
// Model Context Protocol (MCP) JSON-RPC 2.0 Live & Headless Server (Wave 37.1).
// Provides external IDE / AI agent bidirectional inspection, stage mutation,
// undo/redo history navigation, and base64 PNG viewport frame captures.
// =============================================================================
#pragma once

#include "core/StudioConfig.hpp"

#include <cd/world2d/CommandBus2D.hpp>
#include <cd/world2d/Stage2D.hpp>

#include <cstdint>
#include <functional>
#include <iosfwd>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace cd::studio
{

/// Callback providing raw RGBA8 or PNG bytes for MCP capture_frame tool.
using CaptureFrameCallback =
    std::function<std::vector<std::uint8_t>(std::uint32_t width, std::uint32_t height)>;

class StudioMcpServer
{
public:
    explicit StudioMcpServer(const StudioConfig& config);
    ~StudioMcpServer();

    StudioMcpServer(const StudioMcpServer&)            = delete;
    StudioMcpServer& operator=(const StudioMcpServer&) = delete;
    StudioMcpServer(StudioMcpServer&&)                 = delete;
    StudioMcpServer& operator=(StudioMcpServer&&)      = delete;

    /// Starts worker thread for configured transport (Stdio or TCP).
    [[nodiscard]] bool start();

    /// Stops transport listener and joins background worker threads.
    void stop();

    /// Drains queued requests from background transport threads and executes
    /// mutations and frame captures on the main engine thread.
    void tick(cd::world2d::Stage2D& stage,
              cd::world2d::CommandBus2D& command_bus,
              CaptureFrameCallback capture_cb = nullptr);

    /// Synchronously dispatches a single newline-delimited JSON-RPC line.
    /// Returns the JSON-RPC response string (or empty if notification).
    [[nodiscard]] std::string process_message(
        std::string_view json_line,
        cd::world2d::Stage2D& stage,
        cd::world2d::CommandBus2D& command_bus,
        CaptureFrameCallback capture_cb = nullptr);

    [[nodiscard]] bool is_running() const noexcept;
    [[nodiscard]] bool is_initialized() const noexcept;
    [[nodiscard]] McpTransportKind transport_kind() const noexcept;
    [[nodiscard]] std::uint16_t port() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/// Synchronous headless execution loop: reads JSON-RPC lines from input stream
/// and writes replies to output stream until EOF (used for CI and automated tests).
int run_studio_mcp_headless_loop(
    cd::world2d::Stage2D& stage,
    cd::world2d::CommandBus2D& command_bus,
    std::istream& in,
    std::ostream& out);

int run_studio_mcp_headless_loop(
    cd::world2d::Stage2D& stage,
    cd::world2d::CommandBus2D& command_bus);

}  // namespace cd::studio
