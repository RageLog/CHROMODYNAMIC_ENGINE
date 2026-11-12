// =============================================================================
// CHROMODYNAMIC — apps/common/tests/test_app_common.cpp
//
// Unit tests for cd::app_common:
//   1. FileTextIo     — UTF-8 BOM stripping + atomic round-trip
//   2. CameraMath3D   — Orthonormal basis, ray unproject, quat<->euler, ray-sphere/plane
//   3. FrameCapture   — CPU DrawBatcher -> PNG write -> load_image pixel verification
//   4. PackageExporter— Standalone .cdpak creation + Pak TOC/payload verification
// =============================================================================
#include <cd/app_common/CameraMath3D.hpp>
#include <cd/app_common/FileTextIo.hpp>
#include <cd/app_common/FrameCapture.hpp>
#include <cd/app_common/PackageExporter.hpp>
#include <cd/asset/image/Image.hpp>
#include <cd/asset/pak/Pak.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>

namespace
{

TEST(AppCommonFileTextIo, StripsUtf8BomAndRoundTripsCleanly)
{
    const auto tmp = (std::filesystem::temp_directory_path() / "cd_test_bom_utf8.json").string();
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        const unsigned char bom[3] = { 0xEFU, 0xBBU, 0xBFU };
        out.write(reinterpret_cast<const char*>(bom), 3);
        out << "{\"ok\":true}";
    }

    auto read_r = cd::app_common::read_text_file_utf8(tmp);
    ASSERT_TRUE(read_r.has_value());
    EXPECT_EQ(*read_r, "{\"ok\":true}");

    const auto tmp2 = (std::filesystem::temp_directory_path() / "cd_test_nested" / "clean.txt").string();
    ASSERT_TRUE(cd::app_common::write_text_file_utf8(tmp2, "hello source2").has_value());
    auto read_r2 = cd::app_common::read_text_file_utf8(tmp2);
    ASSERT_TRUE(read_r2.has_value());
    EXPECT_EQ(*read_r2, "hello source2");

    std::error_code ec {};
    std::filesystem::remove(tmp, ec);
    std::filesystem::remove_all(std::filesystem::temp_directory_path() / "cd_test_nested", ec);
}

TEST(AppCommonCameraMath3D, OrthonormalBasisQuatEulerAndRayHitsAreConsistent)
{
    const auto basis = cd::app_common::compute_camera_basis(
        { 0.0F, 3.0F, 6.0F },
        { 0.0F, 0.0F, 0.0F });
    EXPECT_NEAR(cd::math::length(basis.forward), 1.0F, 1e-5F);
    EXPECT_NEAR(cd::math::length(basis.right), 1.0F, 1e-5F);
    EXPECT_NEAR(cd::math::length(basis.up), 1.0F, 1e-5F);
    EXPECT_NEAR(cd::math::dot(basis.forward, basis.right), 0.0F, 1e-5F);
    EXPECT_NEAR(cd::math::dot(basis.forward, basis.up), 0.0F, 1e-5F);

    const cd::math::Vec3f angles_in { -22.5F, 45.0F, 12.0F };
    const cd::math::Quatf q = cd::app_common::euler_deg_to_quat(angles_in);
    const cd::math::Vec3f angles_out = cd::app_common::quat_to_euler_deg(q);
    EXPECT_NEAR(angles_out.x, angles_in.x, 1e-2F);
    EXPECT_NEAR(angles_out.y, angles_in.y, 1e-2F);
    EXPECT_NEAR(angles_out.z, angles_in.z, 1e-2F);

    const auto hit_t = cd::app_common::ray_sphere_hit_t(
        { 0.0F, 0.0F, 5.0F },
        { 0.0F, 0.0F, -1.0F },
        { 0.0F, 0.0F, 0.0F },
        1.5F);
    ASSERT_TRUE(hit_t.has_value());
    EXPECT_NEAR(*hit_t, 3.5F, 1e-4F);

    const auto plane_pt = cd::app_common::ray_plane_hit_point(
        { 1.0F, 4.0F, 2.0F },
        { 0.0F, -1.0F, 0.0F },
        { 0.0F, 0.0F, 0.0F },
        { 0.0F, 1.0F, 0.0F });
    ASSERT_TRUE(plane_pt.has_value());
    EXPECT_NEAR(plane_pt->x, 1.0F, 1e-4F);
    EXPECT_NEAR(plane_pt->y, 0.0F, 1e-4F);
    EXPECT_NEAR(plane_pt->z, 2.0F, 1e-4F);
}

TEST(AppCommonFrameCapture, RasterizesDrawBatcherAndWritesValidPng)
{
    cd::ui::renderer::DrawBatcher batcher;
    batcher.quad(4.0F, 4.0F, 8.0F, 8.0F, { 240U, 60U, 30U, 255U });

    const auto png_path = (std::filesystem::temp_directory_path() / "cd_test_batcher_capture.png").string();
    const auto wr = cd::app_common::rasterize_draw_batcher_to_png(
        batcher, 16U, 16U, { 10U, 20U, 30U, 255U }, png_path);
    ASSERT_TRUE(wr.has_value());

    const auto decoded = cd::asset::image::load_image(png_path);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->width, 16U);
    EXPECT_EQ(decoded->height, 16U);

    // Background pixel at (1, 1)
    const std::size_t bg_off = (1U * 16U + 1U) * 4U;
    EXPECT_EQ(decoded->rgba[bg_off + 0U], 10U);
    EXPECT_EQ(decoded->rgba[bg_off + 1U], 20U);
    EXPECT_EQ(decoded->rgba[bg_off + 2U], 30U);

    // Quad interior pixel at (8, 8)
    const std::size_t fg_off = (8U * 16U + 8U) * 4U;
    EXPECT_EQ(decoded->rgba[fg_off + 0U], 240U);
    EXPECT_EQ(decoded->rgba[fg_off + 1U], 60U);
    EXPECT_EQ(decoded->rgba[fg_off + 2U], 30U);

    std::error_code ec {};
    std::filesystem::remove(png_path, ec);
}

TEST(AppCommonPackageExporter, BuildsMountableCdPakArchiveWithManifestAndStage)
{
    const auto out_dir = (std::filesystem::temp_directory_path() / "cd_test_pak_export").string();
    cd::app_common::PackageExportRequest req {};
    req.output_dir         = out_dir;
    req.mode               = "Pure2D";
    req.stage2d_json       = "{\"stage\":\"demo2d\"}";
    req.scene3d_json       = "{\"scene\":\"demo3d\"}";
    req.copy_player_binary = false;

    const auto rep = cd::app_common::export_standalone_cdpak_bundle(req);
    ASSERT_TRUE(rep.has_value());
    EXPECT_GT(rep->pak_size_bytes, 64U);

    std::ifstream in(rep->pak_path, std::ios::binary | std::ios::ate);
    ASSERT_TRUE(in.is_open());
    const auto sz = static_cast<std::size_t>(in.tellg());
    in.seekg(0, std::ios::beg);
    std::vector<std::byte> pak_bytes(sz);
    in.read(reinterpret_cast<char*>(pak_bytes.data()), static_cast<std::streamsize>(sz));

    cd::asset::pak::Pak pak;
    ASSERT_TRUE(pak.open(pak_bytes.data(), pak_bytes.size()).has_value());
    EXPECT_TRUE(pak.find("manifest.json").has_value());
    const auto s2d_blob = pak.find("stages/main.cd2d.json");
    ASSERT_TRUE(s2d_blob.has_value());
    const std::string s2d_str(
        reinterpret_cast<const char*>(s2d_blob->bytes),
        s2d_blob->size);
    EXPECT_EQ(s2d_str, "{\"stage\":\"demo2d\"}");

    std::error_code ec {};
    std::filesystem::remove_all(out_dir, ec);
}

TEST(AppCommonPackageExporter, CustomExecutableNameWritesNamedLauncherAndManifest)
{
    const auto out_dir = (std::filesystem::temp_directory_path() / "cd_test_pong_export").string();
    cd::app_common::PackageExportRequest req {};
    req.output_dir         = out_dir;
    req.mode               = "Pure2D";
    req.game_title         = "Neon Cyber Pong";
    req.executable_name    = "Pong";
    req.stage2d_json       = "{\"stage\":\"pong\"}";
    req.copy_player_binary = false;

    const auto rep = cd::app_common::export_standalone_cdpak_bundle(req);
    ASSERT_TRUE(rep.has_value());
    EXPECT_TRUE(std::filesystem::exists(std::filesystem::path(out_dir) / "Play_Pong.bat"));

    auto bat_txt = cd::app_common::read_text_file_utf8((std::filesystem::path(out_dir) / "Play_Pong.bat").string());
    ASSERT_TRUE(bat_txt.has_value());
    EXPECT_NE(bat_txt->find("Pong.exe"), std::string::npos);

    std::error_code ec {};
    std::filesystem::remove_all(out_dir, ec);
}

TEST(AppCommonFrameCapture, Base64EncodeAndPngMemoryWriteRoundTrip)
{
    // Test base64 encode RFC 4648 vectors
    const std::string text = "Chromodynamic MCP";
    std::vector<std::uint8_t> bytes(text.begin(), text.end());
    const std::string b64 = cd::app_common::base64_encode(bytes);
    EXPECT_EQ(b64, "Q2hyb21vZHluYW1pYyBNQ1A=");

    // Test write_png_rgba_to_memory
    const std::vector<std::uint8_t> rgba {
        255, 0, 0, 255,
        0, 255, 0, 255,
        0, 0, 255, 255,
        255, 255, 0, 255
    };
    auto png_r = cd::asset::image::write_png_rgba_to_memory(rgba.data(), 2U, 2U);
    ASSERT_TRUE(png_r.has_value());
    EXPECT_GT(png_r->size(), 16U);

    // Verify PNG magic header "\x89PNG\r\n\x1a\n"
    EXPECT_EQ((*png_r)[0], 0x89U);
    EXPECT_EQ((*png_r)[1], 'P');
    EXPECT_EQ((*png_r)[2], 'N');
    EXPECT_EQ((*png_r)[3], 'G');

    const std::string png_b64 = cd::app_common::base64_encode(*png_r);
    EXPECT_FALSE(png_b64.empty());
    EXPECT_EQ(png_b64.substr(0, 4), "iVBO");
}

}  // namespace
