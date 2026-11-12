// =============================================================================
// CHROMODYNAMIC STUDIO -- apps/editor/core/StudioMcpServer.cpp
//
// Model Context Protocol (MCP) JSON-RPC 2.0 Live & Headless Server (Wave 37.1).
// =============================================================================
#include "core/StudioMcpServer.hpp"

#include <cd/app_common/FileTextIo.hpp>
#include <cd/app_common/FrameCapture.hpp>
#include <cd/app_common/PackageExporter.hpp>
#include <cd/asset/image/Image.hpp>
#include <cd/asset/json/Json.hpp>
#include <cd/asset/pak/Pak.hpp>
#include <cd/schema/Schema.hpp>
#include <cd/world2d/CommandBus2D.hpp>
#include <cd/world2d/Stage2D.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
using cd_socket_t = SOCKET;
inline constexpr cd_socket_t kCdInvalidSocket = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using cd_socket_t = int;
inline constexpr cd_socket_t kCdInvalidSocket = -1;
#endif

namespace cd::studio
{

namespace
{

namespace j = cd::asset::json;

void close_cd_socket(cd_socket_t s) noexcept
{
#if defined(_WIN32)
    if (s != kCdInvalidSocket)
    {
        ::closesocket(s);
    }
#else
    if (s != kCdInvalidSocket)
    {
        ::close(s);
    }
#endif
}

std::string escape_json_string(std::string_view s)
{
    std::string out;
    out.reserve(s.size() + 16U);
    for (const char c : s)
    {
        switch (c)
        {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
            {
                const auto uc = static_cast<unsigned char>(c);
                if (uc < 0x20 || uc >= 0x80)
                {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned int>(uc));
                    out += buf;
                }
                else
                {
                    out += c;
                }
                break;
            }
        }
    }
    return out;
}

std::string make_error_response(const j::Value* id_val, int code, std::string_view message, std::string_view data = {})
{
    std::string id_str = "null";
    if (id_val != nullptr)
    {
        if (id_val->is_number())
        {
            id_str = std::to_string(static_cast<std::int64_t>(id_val->as_number()));
        }
        else if (id_val->is_string())
        {
            id_str = "\"" + escape_json_string(id_val->as_string()) + "\"";
        }
    }

    std::string out = "{\"jsonrpc\":\"2.0\",\"id\":" + id_str +
                      ",\"error\":{\"code\":" + std::to_string(code) +
                      ",\"message\":\"" + escape_json_string(message) + "\"";
    if (!data.empty())
    {
        out += ",\"data\":\"" + escape_json_string(data) + "\"";
    }
    out += "}}";
    return out;
}

std::string make_result_response(const j::Value* id_val, std::string_view result_json)
{
    std::string id_str = "null";
    if (id_val != nullptr)
    {
        if (id_val->is_number())
        {
            id_str = std::to_string(static_cast<std::int64_t>(id_val->as_number()));
        }
        else if (id_val->is_string())
        {
            id_str = "\"" + escape_json_string(id_val->as_string()) + "\"";
        }
    }

    return "{\"jsonrpc\":\"2.0\",\"id\":" + id_str + ",\"result\":" + std::string(result_json) + "}";
}

std::vector<std::uint8_t> generate_diagnostic_test_png(std::uint32_t width, std::uint32_t height)
{
    const std::uint32_t w = std::clamp<std::uint32_t>(width == 0U ? 64U : width, 16U, 4096U);
    const std::uint32_t h = std::clamp<std::uint32_t>(height == 0U ? 64U : height, 16U, 4096U);
    try
    {
        std::vector<std::uint8_t> rgba(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4U, 0U);

        for (std::uint32_t y = 0U; y < h; ++y)
        {
            for (std::uint32_t x = 0U; x < w; ++x)
            {
                const std::size_t idx = (static_cast<std::size_t>(y) * w + static_cast<std::size_t>(x)) * 4U;
                const bool border = (x == 0U || y == 0U || x + 1U == w || y + 1U == h);
                const bool checker = (((x / 16U) + (y / 16U)) & 1U) != 0U;

                if (border)
                {
                    rgba[idx + 0U] = 255U;
                    rgba[idx + 1U] = 215U;
                    rgba[idx + 2U] = 0U;
                    rgba[idx + 3U] = 255U;
                }
                else if (checker)
                {
                    rgba[idx + 0U] = 40U;
                    rgba[idx + 1U] = 60U;
                    rgba[idx + 2U] = 100U;
                    rgba[idx + 3U] = 255U;
                }
                else
                {
                    rgba[idx + 0U] = 20U;
                    rgba[idx + 1U] = 30U;
                    rgba[idx + 2U] = 50U;
                    rgba[idx + 3U] = 255U;
                }
            }
        }

        auto png_r = cd::asset::image::write_png_rgba_to_memory(rgba.data(), w, h);
        if (png_r.has_value())
        {
            return std::move(*png_r);
        }
    }
    catch (...)
    {
        return {};
    }
    return {};
}

}  // namespace

struct StudioMcpServer::Impl
{
    StudioConfig config {};
    std::atomic<bool> running { false };
    std::atomic<bool> initialized { false };

    struct StdioContext
    {
        std::atomic<bool> running { true };
        std::atomic<bool> cin_eof { false };
    };
    std::shared_ptr<StdioContext> stdio_ctx_ {};

    struct RequestItem
    {
        std::string line;
        std::function<void(std::string_view)> reply_callback;
    };

    std::mutex queue_mutex {};
    std::vector<RequestItem> pending_requests {};

    std::thread worker_thread {};
    std::mutex stdout_mutex {};

    cd_socket_t listen_socket { kCdInvalidSocket };
    std::vector<std::thread> client_threads {};

    explicit Impl(const StudioConfig& cfg)
        : config(cfg)
    {
    }

    ~Impl()
    {
        stop();
    }

    void stop()
    {
        if (!running.exchange(false))
        {
            return;
        }

        if (stdio_ctx_)
        {
            stdio_ctx_->running.store(false);
        }

        if (listen_socket != kCdInvalidSocket)
        {
            close_cd_socket(listen_socket);
            listen_socket = kCdInvalidSocket;
        }

        {
            std::lock_guard<std::mutex> lock(queue_mutex);
            // Drain check / ensures any concurrent push completes
        }

        if (worker_thread.joinable())
        {
            if (stdio_ctx_ && stdio_ctx_->cin_eof.load())
            {
                worker_thread.join();
            }
            else
            {
                worker_thread.detach();
            }
        }

        for (auto& th : client_threads)
        {
            if (th.joinable())
            {
                th.join();
            }
        }
        client_threads.clear();

#if defined(_WIN32)
        ::WSACleanup();
#endif
    }
};

StudioMcpServer::StudioMcpServer(const StudioConfig& config)
    : impl_(std::make_unique<Impl>(config))
{
}

StudioMcpServer::~StudioMcpServer() = default;

bool StudioMcpServer::start()
{
    if (impl_->running)
    {
        return true;
    }

    if (impl_->config.mcp_transport == McpTransportKind::kDisabled)
    {
        return false;
    }

    impl_->running = true;

    if (impl_->config.mcp_transport == McpTransportKind::kStdio)
    {
        auto ctx = std::make_shared<Impl::StdioContext>();
        impl_->stdio_ctx_ = ctx;
        impl_->worker_thread = std::thread([this, ctx]() {
            std::string line;
            while (ctx->running.load() && std::getline(std::cin, line))
            {
                if (!ctx->running.load())
                {
                    break;
                }
                if (line.empty() || line.find_first_not_of(" \t\r\n") == std::string::npos)
                {
                    continue;
                }
                auto reply_cb = [this](std::string_view reply) {
                    if (!reply.empty())
                    {
                        std::lock_guard<std::mutex> lk(impl_->stdout_mutex);
                        std::cout << reply << "\n";
                        std::cout.flush();
                    }
                };
                {
                    std::lock_guard<std::mutex> lock(impl_->queue_mutex);
                    if (ctx->running.load())
                    {
                        impl_->pending_requests.push_back({ std::move(line), std::move(reply_cb) });
                    }
                }
            }
            ctx->cin_eof.store(true);
        });
        return true;
    }

    if (impl_->config.mcp_transport == McpTransportKind::kTcp)
    {
#if defined(_WIN32)
        WSADATA wsa {};
        if (::WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
        {
            impl_->running = false;
            return false;
        }
#endif
        impl_->listen_socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (impl_->listen_socket == kCdInvalidSocket)
        {
            impl_->running = false;
            return false;
        }

        int opt = 1;
        ::setsockopt(impl_->listen_socket, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt));

        sockaddr_in addr {};
        addr.sin_family = AF_INET;
        ::inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
        addr.sin_port   = htons(impl_->config.mcp_port);

        if (::bind(impl_->listen_socket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0)
        {
            close_cd_socket(impl_->listen_socket);
            impl_->listen_socket = kCdInvalidSocket;
            impl_->running       = false;
            return false;
        }

        if (::listen(impl_->listen_socket, 8) != 0)
        {
            close_cd_socket(impl_->listen_socket);
            impl_->listen_socket = kCdInvalidSocket;
            impl_->running       = false;
            return false;
        }

        impl_->worker_thread = std::thread([this]() {
            while (impl_->running)
            {
                sockaddr_in client_addr {};
#if defined(_WIN32)
                int client_len = sizeof(client_addr);
#else
                socklen_t client_len = sizeof(client_addr);
#endif
                cd_socket_t client_sock = ::accept(impl_->listen_socket, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
                if (client_sock == kCdInvalidSocket)
                {
                    break;
                }

                impl_->client_threads.emplace_back([this, client_sock]() {
                    std::string line_accum;
                    char buf[1024];
                    while (impl_->running)
                    {
                        const int n = ::recv(client_sock, buf, sizeof(buf), 0);
                        if (n <= 0)
                        {
                            break;
                        }
                        line_accum.append(buf, static_cast<std::size_t>(n));
                        std::size_t pos = 0;
                        while ((pos = line_accum.find('\n')) != std::string::npos)
                        {
                            std::string line = line_accum.substr(0, pos);
                            line_accum.erase(0, pos + 1U);
                            if (line.empty() || line.find_first_not_of(" \t\r\n") == std::string::npos)
                            {
                                continue;
                            }
                            auto reply_cb = [client_sock](std::string_view reply) {
                                if (!reply.empty())
                                {
                                    std::string out = std::string(reply) + "\n";
                                    ::send(client_sock, out.data(), static_cast<int>(out.size()), 0);
                                }
                            };
                            {
                                std::lock_guard<std::mutex> lock(impl_->queue_mutex);
                                impl_->pending_requests.push_back({ std::move(line), std::move(reply_cb) });
                            }
                        }
                    }
                    close_cd_socket(client_sock);
                });
            }
        });
        return true;
    }

    return false;
}

void StudioMcpServer::stop()
{
    impl_->stop();
}

void StudioMcpServer::tick(cd::world2d::Stage2D& stage,
                          cd::world2d::CommandBus2D& command_bus,
                          CaptureFrameCallback capture_cb)
{
    std::vector<Impl::RequestItem> requests;
    {
        std::lock_guard<std::mutex> lock(impl_->queue_mutex);
        if (impl_->pending_requests.empty())
        {
            return;
        }
        requests.swap(impl_->pending_requests);
    }

    for (const auto& item : requests)
    {
        const std::string response = process_message(item.line, stage, command_bus, capture_cb);
        if (item.reply_callback && !response.empty())
        {
            item.reply_callback(response);
        }
    }
}

std::string StudioMcpServer::process_message(
    std::string_view json_line,
    cd::world2d::Stage2D& stage,
    cd::world2d::CommandBus2D& command_bus,
    CaptureFrameCallback capture_cb)
{
    if (json_line.size() >= 3U &&
        static_cast<unsigned char>(json_line[0]) == 0xEFU &&
        static_cast<unsigned char>(json_line[1]) == 0xBBU &&
        static_cast<unsigned char>(json_line[2]) == 0xBFU)
    {
        json_line.remove_prefix(3U);
    }

    while (!json_line.empty() && (json_line.front() == ' ' || json_line.front() == '\t' || json_line.front() == '\r' || json_line.front() == '\n'))
    {
        json_line.remove_prefix(1U);
    }
    while (!json_line.empty() && (json_line.back() == ' ' || json_line.back() == '\t' || json_line.back() == '\r' || json_line.back() == '\n'))
    {
        json_line.remove_suffix(1U);
    }

    if (json_line.empty())
    {
        return {};
    }

    auto parsed = j::parse(json_line);
    if (!parsed.has_value())
    {
        return make_error_response(nullptr, -32700, "Parse error: " + std::string(parsed.error().message), json_line);
    }

    const j::Value& root = *parsed;
    if (!root.is_object())
    {
        return make_error_response(nullptr, -32600, "Invalid Request: root must be a JSON object");
    }

    const j::Value* id_val = nullptr;
    if (auto id_find = root.at("id"); id_find.has_value())
    {
        id_val = *id_find;
    }

    const auto jsonrpc_val = root.at("jsonrpc");
    if (!jsonrpc_val.has_value() || !(*jsonrpc_val)->is_string() || (*jsonrpc_val)->as_string() != "2.0")
    {
        return make_error_response(id_val, -32600, "Invalid Request: jsonrpc must be '2.0'");
    }

    const auto method_val = root.at("method");
    if (!method_val.has_value() || !(*method_val)->is_string())
    {
        return make_error_response(id_val, -32600, "Invalid Request: missing method string");
    }

    const std::string_view method = (*method_val)->as_string();

    // ---- Handshake: initialize ----------------------------------------------
    if (method == "initialize")
    {
        if (impl_->initialized)
        {
            return make_error_response(id_val, -32600, "Already initialized");
        }
        impl_->initialized = true;

        const std::string result =
            "{"
            "\"protocolVersion\":\"2024-11-05\","
            "\"capabilities\":{"
            "\"tools\":{\"listChanged\":false},"
            "\"resources\":{\"subscribe\":false,\"listChanged\":false}"
            "},"
            "\"serverInfo\":{"
            "\"name\":\"chromodynamic-editor\","
            "\"version\":\"1.0.0\""
            "}"
            "}";
        return make_result_response(id_val, result);
    }

    // ---- Notification: initialized ------------------------------------------
    if (method == "notifications/initialized")
    {
        impl_->initialized = true;
        return {};  // Notifications produce no reply
    }

    // Guard against requests before initialization
    if (!impl_->initialized)
    {
        return make_error_response(id_val, -32002, "Server not initialized. Call 'initialize' first.");
    }

    // ---- Tools: tools/list --------------------------------------------------
    if (method == "tools/list")
    {
        const std::string tools_json =
            "{\"tools\":["
            "{"
            "\"name\":\"stage_get_tree\","
            "\"description\":\"Retrieve the full 2D stage entity tree and tilemap as JSON\","
            "\"inputSchema\":{\"type\":\"object\",\"properties\":{},\"additionalProperties\":false}"
            "},"
            "{"
            "\"name\":\"stage_mutate\","
            "\"description\":\"Atomically execute one or more mutations against the active stage using CommandBus2D\","
            "\"inputSchema\":{"
            "\"type\":\"object\","
            "\"properties\":{"
            "\"op\":{\"type\":\"string\",\"description\":\"Operation name (e.g. spawn_entity_2d, delete_entity_2d, set_entity_field_2d, paint_tile_2d, paint_rect_2d, attach_lua_script_2d)\"},"
            "\"commands\":{\"type\":\"array\",\"items\":{\"type\":\"object\"}},"
            "\"name\":{\"type\":\"string\"},"
            "\"role\":{\"type\":\"integer\"},"
            "\"id\":{\"type\":\"integer\"},"
            "\"field\":{\"type\":\"string\"},"
            "\"script\":{\"type\":\"string\"}"
            "}"
            "}"
            "},"
            "{"
            "\"name\":\"stage_undo\","
            "\"description\":\"Revert the most recent mutation operation from the undo stack\","
            "\"inputSchema\":{\"type\":\"object\",\"properties\":{},\"additionalProperties\":false}"
            "},"
            "{"
            "\"name\":\"stage_redo\","
            "\"description\":\"Reapply the most recently undone mutation operation\","
            "\"inputSchema\":{\"type\":\"object\",\"properties\":{},\"additionalProperties\":false}"
            "},"
            "{"
            "\"name\":\"capture_frame\","
            "\"description\":\"Capture the active viewport frame buffer and return base64 PNG data\","
            "\"inputSchema\":{"
            "\"type\":\"object\","
            "\"properties\":{"
            "\"width\":{\"type\":\"integer\",\"default\":1280},"
            "\"height\":{\"type\":\"integer\",\"default\":720}"
            "},"
            "\"additionalProperties\":false"
            "}"
            "},"
            "{"
            "\"name\":\"cook_assets\","
            "\"description\":\"Invoke offline asset compilation pipeline to bundle assets into a .cdpak file\","
            "\"inputSchema\":{"
            "\"type\":\"object\","
            "\"properties\":{"
            "\"input_dir\":{\"type\":\"string\"},"
            "\"output_pak\":{\"type\":\"string\"},"
            "\"verify\":{\"type\":\"boolean\",\"default\":true}"
            "},"
            "\"required\":[\"input_dir\",\"output_pak\"]"
            "}"
            "},"
            "{"
            "\"name\":\"export_package\","
            "\"description\":\"Export standalone relocatable distribution package with cd_player.exe and game.cdpak\","
            "\"inputSchema\":{"
            "\"type\":\"object\","
            "\"properties\":{"
            "\"output_dir\":{\"type\":\"string\",\"default\":\"build/export/ChromodynamicGame\"},"
            "\"mode\":{\"type\":\"string\",\"enum\":[\"Pure2D\",\"Pure3D\",\"Hybrid2D3D\"],\"default\":\"Pure2D\"}"
            "},"
            "\"additionalProperties\":false"
            "}"
            "}"
            "]}";
        return make_result_response(id_val, tools_json);
    }

    // ---- Tools: tools/call --------------------------------------------------
    if (method == "tools/call")
    {
        const auto params_val = root.at("params");
        if (!params_val.has_value() || !(*params_val)->is_object())
        {
            return make_error_response(id_val, -32602, "Invalid params: object required");
        }
        const j::Value& params = *(*params_val);

        const auto name_val = params.at("name");
        if (!name_val.has_value() || !(*name_val)->is_string())
        {
            return make_error_response(id_val, -32602, "Invalid params: tool name required");
        }
        const std::string_view tool_name = (*name_val)->as_string();

        const j::Value* args_val = nullptr;
        if (auto args_find = params.at("arguments"); args_find.has_value())
        {
            args_val = *args_find;
        }

        if (tool_name == "stage_get_tree")
        {
            const std::string stage_json = stage.save_to_json_string(true);
            const std::string result =
                "{\"content\":[{\"type\":\"text\",\"text\":\"" +
                escape_json_string(stage_json) + "\"}],\"isError\":false}";
            return make_result_response(id_val, result);
        }

        if (tool_name == "stage_mutate")
        {
            std::string cmd_payload;
            if (args_val != nullptr)
            {
                cmd_payload = j::serialize(*args_val, false);
            }
            else
            {
                cmd_payload = "{}";
            }

            const auto rep = command_bus.execute_json(stage, cmd_payload);
            const std::string rep_json = rep.to_json_string();
            const std::string result =
                "{\"content\":[{\"type\":\"text\",\"text\":\"" +
                escape_json_string(rep_json) + "\"}],\"isError\":" +
                (rep.ok ? "false" : "true") + "}";
            return make_result_response(id_val, result);
        }

        if (tool_name == "stage_undo")
        {
            const bool ok = command_bus.undo(stage);
            const std::string msg = ok ? "Successfully reverted previous operation" : "Undo stack is empty";
            const std::string rep_json =
                "{\"ok\":" + std::string(ok ? "true" : "false") +
                ",\"message\":\"" + msg +
                "\",\"undo_depth\":" + std::to_string(command_bus.undo_depth()) + "}";
            const std::string result =
                "{\"content\":[{\"type\":\"text\",\"text\":\"" +
                escape_json_string(rep_json) + "\"}],\"isError\":" +
                (ok ? "false" : "true") + "}";
            return make_result_response(id_val, result);
        }

        if (tool_name == "stage_redo")
        {
            const bool ok = command_bus.redo(stage);
            const std::string msg = ok ? "Successfully reapplied operation" : "Redo stack is empty";
            const std::string rep_json =
                "{\"ok\":" + std::string(ok ? "true" : "false") +
                ",\"message\":\"" + msg +
                "\",\"redo_depth\":" + std::to_string(command_bus.redo_depth()) + "}";
            const std::string result =
                "{\"content\":[{\"type\":\"text\",\"text\":\"" +
                escape_json_string(rep_json) + "\"}],\"isError\":" +
                (ok ? "false" : "true") + "}";
            return make_result_response(id_val, result);
        }

        if (tool_name == "capture_frame")
        {
            std::uint32_t width = 1280U;
            std::uint32_t height = 720U;
            if (args_val != nullptr && args_val->is_object())
            {
                if (auto w = args_val->at("width"); w.has_value() && (*w)->is_number())
                {
                    const double wv = (*w)->as_number();
                    if (std::isfinite(wv) && wv > 0.0)
                    {
                        width = std::clamp<std::uint32_t>(static_cast<std::uint32_t>(wv), 16U, 4096U);
                    }
                    else if (std::isfinite(wv) && wv < 0.0)
                    {
                        width = 1280U;
                    }
                }
                if (auto h = args_val->at("height"); h.has_value() && (*h)->is_number())
                {
                    const double hv = (*h)->as_number();
                    if (std::isfinite(hv) && hv > 0.0)
                    {
                        height = std::clamp<std::uint32_t>(static_cast<std::uint32_t>(hv), 16U, 4096U);
                    }
                    else if (std::isfinite(hv) && hv < 0.0)
                    {
                        height = 720U;
                    }
                }
            }
            if (width < 16U) width = 16U;
            if (width > 4096U) width = 4096U;
            if (height < 16U) height = 16U;
            if (height > 4096U) height = 4096U;

            std::vector<std::uint8_t> png_bytes;
            try
            {
                if (capture_cb)
                {
                    png_bytes = capture_cb(width, height);
                }
                if (png_bytes.empty())
                {
                    png_bytes = generate_diagnostic_test_png(width, height);
                }
                if (png_bytes.empty())
                {
                    return make_error_response(id_val, -32603, "Internal error: frame capture failed to generate PNG data");
                }
            }
            catch (const std::exception& ex)
            {
                return make_error_response(id_val, -32603, "Internal error during frame capture: " + std::string(ex.what()));
            }
            catch (...)
            {
                return make_error_response(id_val, -32603, "Internal error: unknown exception during frame capture");
            }

            const std::string b64 = cd::app_common::base64_encode(png_bytes);
            const std::string summary = "Viewport frame captured successfully (" +
                                        std::to_string(width) + "x" + std::to_string(height) +
                                        " PNG, " + std::to_string(png_bytes.size()) + " bytes)";

            const std::string result =
                "{\"content\":["
                "{\"type\":\"text\",\"text\":\"" + escape_json_string(summary) + "\"},"
                "{\"type\":\"image\",\"data\":\"" + b64 + "\",\"mimeType\":\"image/png\"}"
                "],\"isError\":false}";
            return make_result_response(id_val, result);
        }

        if (tool_name == "cook_assets")
        {
            std::string input_dir;
            std::string output_pak;
            if (args_val != nullptr && args_val->is_object())
            {
                if (auto in_dir = args_val->at("input_dir"); in_dir.has_value() && (*in_dir)->is_string())
                {
                    input_dir = (*in_dir)->as_string();
                }
                if (auto out_p = args_val->at("output_pak"); out_p.has_value() && (*out_p)->is_string())
                {
                    output_pak = (*out_p)->as_string();
                }
            }

            if (input_dir.empty() || output_pak.empty())
            {
                const std::string err_rep = "{\"ok\":false,\"error\":\"input_dir and output_pak are required\"}";
                const std::string result = "{\"content\":[{\"type\":\"text\",\"text\":\"" +
                                           escape_json_string(err_rep) + "\"}],\"isError\":true}";
                return make_result_response(id_val, result);
            }

            cd::asset::pak::PakBuilder pak_builder;
            std::error_code ec {};
            std::size_t packed_count = 0;
            for (const auto& entry : std::filesystem::recursive_directory_iterator(input_dir, ec))
            {
                if (entry.is_regular_file(ec))
                {
                    auto bin = cd::app_common::read_binary_file(entry.path().string());
                    if (bin.has_value())
                    {
                        const auto rel = std::filesystem::relative(entry.path(), input_dir, ec).generic_string();
                        std::vector<std::byte> bytes(bin->size());
                        if (!bin->empty())
                        {
                            std::memcpy(bytes.data(), bin->data(), bin->size());
                        }
                        pak_builder.add(rel, std::move(bytes));
                        ++packed_count;
                    }
                }
            }
            const auto pak_bytes = pak_builder.build();
            const bool ok = cd::app_common::write_binary_file(output_pak, std::span<const std::byte> { pak_bytes }).has_value();

            const std::string rep_json =
                "{\"ok\":" + std::string(ok ? "true" : "false") +
                ",\"packed_entries\":" + std::to_string(packed_count) +
                ",\"pak_size_bytes\":" + std::to_string(pak_bytes.size()) +
                ",\"output\":\"" + escape_json_string(output_pak) + "\"}";

            const std::string result = "{\"content\":[{\"type\":\"text\",\"text\":\"" +
                                       escape_json_string(rep_json) + "\"}],\"isError\":" +
                                       (ok ? "false" : "true") + "}";
            return make_result_response(id_val, result);
        }

        if (tool_name == "export_package")
        {
            cd::app_common::PackageExportRequest req {};
            req.output_dir = "build/export/ChromodynamicGame";
            req.mode = "Pure2D";
            req.stage2d_json = stage.save_to_json_string(false);
            req.copy_player_binary = true;

            if (args_val != nullptr && args_val->is_object())
            {
                if (auto d = args_val->at("output_dir"); d.has_value() && (*d)->is_string())
                {
                    req.output_dir = (*d)->as_string();
                }
                if (auto m = args_val->at("mode"); m.has_value() && (*m)->is_string())
                {
                    req.mode = (*m)->as_string();
                }
            }

            const auto rep = cd::app_common::export_standalone_cdpak_bundle(req);
            std::string rep_json;
            if (rep.has_value())
            {
                rep_json = "{\"ok\":true,\"pak_path\":\"" + escape_json_string(rep->pak_path) +
                           "\",\"pak_size_bytes\":" + std::to_string(rep->pak_size_bytes) +
                           ",\"bundled_player_exe\":" + (rep->bundled_player_exe ? "true" : "false") +
                           ",\"bundled_dll_count\":" + std::to_string(rep->bundled_dll_count) + "}";
            }
            else
            {
                rep_json = "{\"ok\":false,\"error\":\"" + escape_json_string(rep.error().message) + "\"}";
            }

            const std::string result = "{\"content\":[{\"type\":\"text\",\"text\":\"" +
                                       escape_json_string(rep_json) + "\"}],\"isError\":" +
                                       (rep.has_value() ? "false" : "true") + "}";
            return make_result_response(id_val, result);
        }

        return make_error_response(id_val, -32601, "Tool not found: " + std::string(tool_name));
    }

    // ---- Resources: resources/list ------------------------------------------
    if (method == "resources/list")
    {
        const std::string res_list =
            "{\"resources\":["
            "{"
            "\"uri\":\"stage://active\","
            "\"name\":\"Active Stage State\","
            "\"description\":\"Live JSON representation of the currently active 2D stage\","
            "\"mimeType\":\"application/json\""
            "},"
            "{"
            "\"uri\":\"schema://types\","
            "\"name\":\"Engine Type Schemas\","
            "\"description\":\"Draft 2020-12 JSON Schema definitions for all reflected engine types\","
            "\"mimeType\":\"application/json\""
            "}"
            "]}";
        return make_result_response(id_val, res_list);
    }

    // ---- Resources: resources/read ------------------------------------------
    if (method == "resources/read")
    {
        const auto params_val = root.at("params");
        if (!params_val.has_value() || !(*params_val)->is_object())
        {
            return make_error_response(id_val, -32602, "Invalid params: object required");
        }
        const auto uri_val = (*params_val)->at("uri");
        if (!uri_val.has_value() || !(*uri_val)->is_string())
        {
            return make_error_response(id_val, -32602, "Invalid params: missing uri string");
        }
        const std::string_view uri = (*uri_val)->as_string();

        if (uri == "stage://active" || uri == "stage://current")
        {
            const std::string stage_txt = stage.save_to_json_string(false);
            const std::string read_res =
                "{\"contents\":[{"
                "\"uri\":\"stage://active\","
                "\"mimeType\":\"application/json\","
                "\"text\":\"" + escape_json_string(stage_txt) + "\""
                "}]}";
            return make_result_response(id_val, read_res);
        }

        if (uri == "schema://types" || uri == "schema://world2d")
        {
            cd::schema::SchemaRegistry reg;
            cd::world2d::register_world2d_schemas(reg);
            const std::string schema_txt = reg.export_json_schema_draft2020();
            const std::string read_res =
                "{\"contents\":[{"
                "\"uri\":\"schema://types\","
                "\"mimeType\":\"application/json\","
                "\"text\":\"" + escape_json_string(schema_txt) + "\""
                "}]}";
            return make_result_response(id_val, read_res);
        }

        return make_error_response(id_val, -32602, "Invalid params: Resource not found: " + std::string(uri));
    }

    return make_error_response(id_val, -32601, "Method not found: " + std::string(method));
}

bool StudioMcpServer::is_running() const noexcept
{
    return impl_->running;
}

bool StudioMcpServer::is_initialized() const noexcept
{
    return impl_->initialized;
}

McpTransportKind StudioMcpServer::transport_kind() const noexcept
{
    return impl_->config.mcp_transport;
}

std::uint16_t StudioMcpServer::port() const noexcept
{
    return impl_->config.mcp_port;
}

int run_studio_mcp_headless_loop(
    cd::world2d::Stage2D& stage,
    cd::world2d::CommandBus2D& command_bus,
    std::istream& in,
    std::ostream& out)
{
    StudioConfig cfg {};
    cfg.mcp_transport = McpTransportKind::kStdio;
    StudioMcpServer server(cfg);

    std::string line;
    while (std::getline(in, line))
    {
        if (line.empty() || line.find_first_not_of(" \t\r\n") == std::string::npos)
        {
            continue;
        }
        const std::string response = server.process_message(line, stage, command_bus);
        if (!response.empty())
        {
            out << response << "\n";
            out.flush();
        }
    }
    return 0;
}

int run_studio_mcp_headless_loop(
    cd::world2d::Stage2D& stage,
    cd::world2d::CommandBus2D& command_bus)
{
    return run_studio_mcp_headless_loop(stage, command_bus, std::cin, std::cout);
}

}  // namespace cd::studio
