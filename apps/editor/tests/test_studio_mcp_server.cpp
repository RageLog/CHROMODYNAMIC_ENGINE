// =============================================================================
// CHROMODYNAMIC STUDIO -- apps/editor/tests/test_studio_mcp_server.cpp
//
// Model Context Protocol (MCP) JSON-RPC 2.0 Server Unit Tests (Wave 37.1).
// Verifies handshake, capabilities, tools discovery, stage tree extraction,
// atomic CommandBus2D mutations, undo/redo stack, and base64 PNG captures.
// =============================================================================
#include "core/StudioConfig.hpp"
#include "core/StudioMcpServer.hpp"

#include <cd/asset/json/Json.hpp>
#include <cd/world2d/CommandBus2D.hpp>
#include <cd/world2d/Stage2D.hpp>

#include <gtest/gtest.h>

#include <string>
#include <string_view>

namespace
{

namespace j = cd::asset::json;

TEST(StudioMcpServer, HandshakeInitializeAndPreInitRejection)
{
    cd::studio::StudioConfig cfg {};
    cfg.mcp_transport = cd::studio::McpTransportKind::kStdio;
    cd::studio::StudioMcpServer server(cfg);
    cd::world2d::Stage2D stage;
    cd::world2d::CommandBus2D bus;

    EXPECT_FALSE(server.is_initialized());

    // 1. Tool call before initialize should fail with -32002
    const std::string pre_init =
        server.process_message(R"({"jsonrpc":"2.0","id":1,"method":"tools/list","params":{}})", stage, bus);
    auto pre_parsed = j::parse(pre_init);
    ASSERT_TRUE(pre_parsed.has_value());
    const auto pre_err = pre_parsed->at("error");
    ASSERT_TRUE(pre_err.has_value());
    const auto pre_code = (*pre_err)->at("code");
    ASSERT_TRUE(pre_code.has_value());
    EXPECT_EQ(static_cast<int>((*pre_code)->as_number()), -32002);

    // 2. Handshake: initialize
    const std::string init_req =
        R"({"jsonrpc":"2.0","id":2,"method":"initialize","params":{"protocolVersion":"2024-11-05"}})";
    const std::string init_res = server.process_message(init_req, stage, bus);
    auto init_parsed = j::parse(init_res);
    ASSERT_TRUE(init_parsed.has_value());
    EXPECT_TRUE(server.is_initialized());

    const auto res_val = init_parsed->at("result");
    ASSERT_TRUE(res_val.has_value());
    const auto proto_ver = (*res_val)->at("protocolVersion");
    ASSERT_TRUE(proto_ver.has_value());
    EXPECT_EQ((*proto_ver)->as_string(), "2024-11-05");

    const auto srv_info = (*res_val)->at("serverInfo");
    ASSERT_TRUE(srv_info.has_value());
    const auto srv_name = (*srv_info)->at("name");
    ASSERT_TRUE(srv_name.has_value());
    EXPECT_EQ((*srv_name)->as_string(), "chromodynamic-editor");

    // 3. Second initialize must be rejected
    const std::string dup_res = server.process_message(init_req, stage, bus);
    auto dup_parsed = j::parse(dup_res);
    ASSERT_TRUE(dup_parsed.has_value());
    EXPECT_TRUE(dup_parsed->at("error").has_value());
}

TEST(StudioMcpServer, ToolsListEnumeratesRequiredCapabilities)
{
    cd::studio::StudioConfig cfg {};
    cd::studio::StudioMcpServer server(cfg);
    cd::world2d::Stage2D stage;
    cd::world2d::CommandBus2D bus;

    (void)server.process_message(
        R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})", stage, bus);

    const std::string list_res =
        server.process_message(R"({"jsonrpc":"2.0","id":2,"method":"tools/list","params":{}})", stage, bus);
    auto parsed = j::parse(list_res);
    ASSERT_TRUE(parsed.has_value());

    const auto res = parsed->at("result");
    ASSERT_TRUE(res.has_value());
    const auto tools = (*res)->at("tools");
    ASSERT_TRUE(tools.has_value());
    ASSERT_TRUE((*tools)->is_array());
    EXPECT_GE((*tools)->as_array().size(), 7U);

    bool has_get_tree   = false;
    bool has_mutate     = false;
    bool has_undo       = false;
    bool has_redo       = false;
    bool has_capture    = false;
    bool has_cook       = false;
    bool has_export_pkg = false;

    for (const auto& t : (*tools)->as_array())
    {
        if (auto n = t.at("name"); n.has_value() && (*n)->is_string())
        {
            const auto name = (*n)->as_string();
            if (name == "stage_get_tree")   has_get_tree = true;
            if (name == "stage_mutate")     has_mutate = true;
            if (name == "stage_undo")       has_undo = true;
            if (name == "stage_redo")       has_redo = true;
            if (name == "capture_frame")    has_capture = true;
            if (name == "cook_assets")      has_cook = true;
            if (name == "export_package")   has_export_pkg = true;
        }
    }

    EXPECT_TRUE(has_get_tree);
    EXPECT_TRUE(has_mutate);
    EXPECT_TRUE(has_undo);
    EXPECT_TRUE(has_redo);
    EXPECT_TRUE(has_capture);
    EXPECT_TRUE(has_cook);
    EXPECT_TRUE(has_export_pkg);
}

TEST(StudioMcpServer, StageMutateUndoRedoAndGetTreeRoundTrip)
{
    cd::studio::StudioConfig cfg {};
    cd::studio::StudioMcpServer server(cfg);
    cd::world2d::Stage2D stage;
    cd::world2d::CommandBus2D bus;

    (void)server.process_message(
        R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})", stage, bus);

    // 1. Initial stage_get_tree
    const std::string tree_initial =
        server.process_message(
            R"({"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"stage_get_tree","arguments":{}}})",
            stage, bus);
    EXPECT_NE(tree_initial.find("entities"), std::string::npos);

    // 2. Mutate: spawn entity
    const std::string mutate_req =
        R"({"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"stage_mutate","arguments":{"op":"spawn_entity_2d","name":"McpUnitHero","role":1,"pos":[120.0,240.0]}}})";
    const std::string mutate_res = server.process_message(mutate_req, stage, bus);
    auto mutate_parsed = j::parse(mutate_res);
    ASSERT_TRUE(mutate_parsed.has_value());
    const auto m_res = mutate_parsed->at("result");
    ASSERT_TRUE(m_res.has_value());
    const auto is_err = (*m_res)->at("isError");
    ASSERT_TRUE(is_err.has_value());
    EXPECT_FALSE((*is_err)->as_bool());

    // 3. Verify entity is present in tree
    const std::string tree_after =
        server.process_message(
            R"({"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"stage_get_tree","arguments":{}}})",
            stage, bus);
    EXPECT_NE(tree_after.find("McpUnitHero"), std::string::npos);

    // 4. stage_undo
    const std::string undo_res =
        server.process_message(
            R"({"jsonrpc":"2.0","id":5,"method":"tools/call","params":{"name":"stage_undo","arguments":{}}})",
            stage, bus);
    auto undo_parsed = j::parse(undo_res);
    ASSERT_TRUE(undo_parsed.has_value());
    const auto u_res = undo_parsed->at("result");
    ASSERT_TRUE(u_res.has_value());
    EXPECT_FALSE((*u_res)->at("isError").value()->as_bool());

    // Verify entity was reverted
    const std::string tree_after_undo =
        server.process_message(
            R"({"jsonrpc":"2.0","id":6,"method":"tools/call","params":{"name":"stage_get_tree","arguments":{}}})",
            stage, bus);
    EXPECT_EQ(tree_after_undo.find("McpUnitHero"), std::string::npos);

    // 5. stage_redo
    const std::string redo_res =
        server.process_message(
            R"({"jsonrpc":"2.0","id":7,"method":"tools/call","params":{"name":"stage_redo","arguments":{}}})",
            stage, bus);
    auto redo_parsed = j::parse(redo_res);
    ASSERT_TRUE(redo_parsed.has_value());
    const auto r_res = redo_parsed->at("result");
    ASSERT_TRUE(r_res.has_value());
    EXPECT_FALSE((*r_res)->at("isError").value()->as_bool());

    // Verify entity is restored
    const std::string tree_after_redo =
        server.process_message(
            R"({"jsonrpc":"2.0","id":8,"method":"tools/call","params":{"name":"stage_get_tree","arguments":{}}})",
            stage, bus);
    EXPECT_NE(tree_after_redo.find("McpUnitHero"), std::string::npos);
}

TEST(StudioMcpServer, CaptureFrameReturnsBase64EncodedPng)
{
    cd::studio::StudioConfig cfg {};
    cd::studio::StudioMcpServer server(cfg);
    cd::world2d::Stage2D stage;
    cd::world2d::CommandBus2D bus;

    (void)server.process_message(
        R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})", stage, bus);

    const std::string cap_req =
        R"({"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"capture_frame","arguments":{"width":64,"height":64}}})";
    const std::string cap_res = server.process_message(cap_req, stage, bus);

    auto parsed = j::parse(cap_res);
    ASSERT_TRUE(parsed.has_value());
    const auto res = parsed->at("result");
    ASSERT_TRUE(res.has_value());
    const auto is_err = (*res)->at("isError");
    ASSERT_TRUE(is_err.has_value());
    EXPECT_FALSE((*is_err)->as_bool());

    const auto content = (*res)->at("content");
    ASSERT_TRUE(content.has_value());
    ASSERT_TRUE((*content)->is_array());
    EXPECT_EQ((*content)->as_array().size(), 2U);

    const auto& img_entry = (*content)->as_array()[1];
    const auto type_val = img_entry.at("type");
    ASSERT_TRUE(type_val.has_value());
    EXPECT_EQ((*type_val)->as_string(), "image");

    const auto mime_val = img_entry.at("mimeType");
    ASSERT_TRUE(mime_val.has_value());
    EXPECT_EQ((*mime_val)->as_string(), "image/png");

    const auto data_val = img_entry.at("data");
    ASSERT_TRUE(data_val.has_value());
    const std::string b64 = (*data_val)->as_string();
    EXPECT_FALSE(b64.empty());
    // Valid PNG in Base64 always starts with "iVBO"
    EXPECT_EQ(b64.substr(0, 4), "iVBO");
}

TEST(StudioMcpServer, ResourcesListAndReadActiveStageAndSchema)
{
    cd::studio::StudioConfig cfg {};
    cd::studio::StudioMcpServer server(cfg);
    cd::world2d::Stage2D stage;
    cd::world2d::CommandBus2D bus;

    (void)server.process_message(
        R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})", stage, bus);

    // 1. resources/list
    const std::string list_res =
        server.process_message(R"({"jsonrpc":"2.0","id":2,"method":"resources/list","params":{}})", stage, bus);
    EXPECT_NE(list_res.find("stage://active"), std::string::npos);
    EXPECT_NE(list_res.find("schema://types"), std::string::npos);

    // 2. resources/read stage://active
    const std::string read_stage =
        server.process_message(
            R"({"jsonrpc":"2.0","id":3,"method":"resources/read","params":{"uri":"stage://active"}})",
            stage, bus);
    EXPECT_NE(read_stage.find("entities"), std::string::npos);

    // 3. resources/read schema://types
    const std::string read_schema =
        server.process_message(
            R"({"jsonrpc":"2.0","id":4,"method":"resources/read","params":{"uri":"schema://types"}})",
            stage, bus);
    EXPECT_NE(read_schema.find("$schema"), std::string::npos);

    // 4. unknown uri returns error -32602
    const std::string read_bad =
        server.process_message(
            R"({"jsonrpc":"2.0","id":5,"method":"resources/read","params":{"uri":"stage://unknown_layer"}})",
            stage, bus);
    EXPECT_NE(read_bad.find("-32602"), std::string::npos);
}

TEST(StudioMcpServer, RobustProtocolErrorHandling)
{
    cd::studio::StudioConfig cfg {};
    cd::studio::StudioMcpServer server(cfg);
    cd::world2d::Stage2D stage;
    cd::world2d::CommandBus2D bus;

    // 1. Malformed JSON -> -32700
    const std::string parse_err = server.process_message(R"({broken json)", stage, bus);
    EXPECT_NE(parse_err.find("-32700"), std::string::npos);

    // 2. Empty or whitespace line produces empty output (no error spam)
    EXPECT_TRUE(server.process_message("", stage, bus).empty());
    EXPECT_TRUE(server.process_message("   \n\r\t  ", stage, bus).empty());

    // Initialize
    (void)server.process_message(
        R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})", stage, bus);

    // 3. Unknown method -> -32601
    const std::string bad_method =
        server.process_message(R"({"jsonrpc":"2.0","id":2,"method":"invalid/unknown_method","params":{}})", stage, bus);
    EXPECT_NE(bad_method.find("-32601"), std::string::npos);

    // 4. Unknown tool -> -32601
    const std::string bad_tool =
        server.process_message(
            R"({"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"non_existent_tool","arguments":{}}})",
            stage, bus);
    EXPECT_NE(bad_tool.find("-32601"), std::string::npos);
}

TEST(StudioMcpServer, CaptureFrameNegativeAndExtremeDimensionsClampedSafely)
{
    cd::studio::StudioConfig cfg {};
    cd::studio::StudioMcpServer server(cfg);
    cd::world2d::Stage2D stage;
    cd::world2d::CommandBus2D bus;

    // Handshake
    (void)server.process_message(
        R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})", stage, bus);

    // 1. Negative dimensions: width: -1, height: -1 (must not throw or crash, clamped safely)
    const std::string neg_req =
        R"({"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"capture_frame","arguments":{"width":-1,"height":-1}}})";
    const std::string neg_res = server.process_message(neg_req, stage, bus);
    auto neg_parsed = j::parse(neg_res);
    ASSERT_TRUE(neg_parsed.has_value());
    const auto neg_result = neg_parsed->at("result");
    ASSERT_TRUE(neg_result.has_value());
    EXPECT_FALSE((*neg_result)->at("isError").value()->as_bool());

    const auto neg_content = (*neg_result)->at("content");
    ASSERT_TRUE(neg_content.has_value());
    ASSERT_TRUE((*neg_content)->is_array());
    EXPECT_EQ((*neg_content)->as_array().size(), 2U);
    const auto neg_b64 = (*neg_content)->as_array()[1].at("data").value()->as_string();
    EXPECT_FALSE(neg_b64.empty());
    EXPECT_EQ(neg_b64.substr(0, 4), "iVBO");

    // 2. Extreme dimensions: width: 100000, height: 100000 (must clamp to 4096 without bad_alloc)
    const std::string extreme_req =
        R"({"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"capture_frame","arguments":{"width":100000,"height":100000}}})";
    const std::string extreme_res = server.process_message(extreme_req, stage, bus);
    auto ext_parsed = j::parse(extreme_res);
    ASSERT_TRUE(ext_parsed.has_value());
    const auto ext_result = ext_parsed->at("result");
    ASSERT_TRUE(ext_result.has_value());
    EXPECT_FALSE((*ext_result)->at("isError").value()->as_bool());

    // 3. Sub-minimum dimensions: width: 4, height: 4 (clamped to 16)
    const std::string small_req =
        R"({"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"capture_frame","arguments":{"width":4,"height":4}}})";
    const std::string small_res = server.process_message(small_req, stage, bus);
    auto sm_parsed = j::parse(small_res);
    ASSERT_TRUE(sm_parsed.has_value());
    EXPECT_FALSE((*sm_parsed->at("result"))->at("isError").value()->as_bool());
}

TEST(StudioMcpServer, EscapeJsonStringHandlesNonAsciiAndGarbageBytes)
{
    cd::studio::StudioConfig cfg {};
    cd::studio::StudioMcpServer server(cfg);
    cd::world2d::Stage2D stage;
    cd::world2d::CommandBus2D bus;

    // Send malformed JSON line with non-ASCII bytes (e.g. 0xDD, 0xFE, 0xFF)
    const std::string raw_garbage = "NOT_JSON_\xDD\xFE\xFF";
    const std::string res = server.process_message(raw_garbage, stage, bus);

    // Response must be valid JSON-RPC parseable without Unicode errors
    auto parsed = j::parse(res);
    ASSERT_TRUE(parsed.has_value());
    const auto err = parsed->at("error");
    ASSERT_TRUE(err.has_value());
    EXPECT_EQ(static_cast<int>((*err)->at("code").value()->as_number()), -32700);

    // Verify all characters in response are pure ASCII (< 0x80)
    for (const char c : res)
    {
        const auto uc = static_cast<unsigned char>(c);
        EXPECT_LT(uc, 0x80U) << "Found non-ASCII byte in response: " << static_cast<int>(uc);
    }
}

}  // namespace
