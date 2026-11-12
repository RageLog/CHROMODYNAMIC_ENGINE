// =============================================================================
// CHROMODYNAMIC — tests/e2e/harness/E2eHarness.hpp
//
// Deterministic simulation harness, RAII temporary sandbox directories,
// hardware environment probes, and zero-sleep timing validation for E2E tests.
// =============================================================================
#pragma once

#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <cd/core/Defines.hpp>
#include <cd/math/Vector.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace cd::e2e
{

/// RAII Temporary directory sandbox for file I/O tests.
/// Guarantees clean isolation and automatic cleanup on destruction.
class TempSandbox final
{
public:
    explicit TempSandbox(std::string_view prefix = "cd_e2e_sandbox")
    {
        const auto sys_temp = std::filesystem::temp_directory_path();
        std::random_device rd;
        std::mt19937_64 gen(rd());
        std::uniform_int_distribution<std::uint64_t> dist;

        dir_path_ = sys_temp / (std::string(prefix) + "_" + std::to_string(dist(gen)));
        std::filesystem::create_directories(dir_path_);
    }

    ~TempSandbox()
    {
        std::error_code ec;
        std::filesystem::remove_all(dir_path_, ec);
    }

    TempSandbox(const TempSandbox&) = delete;
    TempSandbox& operator=(const TempSandbox&) = delete;
    TempSandbox(TempSandbox&&) noexcept = default;
    TempSandbox& operator=(TempSandbox&&) noexcept = default;

    [[nodiscard]] const std::filesystem::path& path() const noexcept { return dir_path_; }

    [[nodiscard]] std::filesystem::path file_path(std::string_view filename) const
    {
        return dir_path_ / filename;
    }

    bool write_text(std::string_view filename, std::string_view content) const
    {
        const auto p = file_path(filename);
        std::ofstream ofs(p, std::ios::binary);
        if (!ofs)
        {
            return false;
        }
        ofs.write(content.data(), static_cast<std::streamsize>(content.size()));
        return ofs.good();
    }

    template <typename T>
    requires (sizeof(T) == 1)
    bool write_binary(std::string_view filename, std::span<const T> data) const
    {
        const auto p = file_path(filename);
        std::ofstream ofs(p, std::ios::binary);
        if (!ofs)
        {
            return false;
        }
        ofs.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
        return ofs.good();
    }

    template <typename T>
    requires (sizeof(T) == 1)
    bool write_binary(std::string_view filename, const std::vector<T>& data) const
    {
        return write_binary(filename, std::span<const T>(data));
    }

    [[nodiscard]] std::string read_text(std::string_view filename) const
    {
        const auto p = file_path(filename);
        std::ifstream ifs(p, std::ios::binary);
        if (!ifs)
        {
            return {};
        }
        return std::string((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    }

    [[nodiscard]] bool file_exists(std::string_view filename) const
    {
        std::error_code ec;
        return std::filesystem::exists(file_path(filename), ec);
    }

private:
    std::filesystem::path dir_path_;
};

/// Deterministic virtual stepped clock for simulating frame loops without sleep_for.
class DeterministicClock final
{
public:
    explicit DeterministicClock(float fixed_dt = 1.0F / 60.0F) noexcept
        : fixed_dt_(fixed_dt)
    {
    }

    void advance_step() noexcept
    {
        simulated_time_ += fixed_dt_;
        ++step_count_;
    }

    void advance_steps(std::uint64_t count) noexcept
    {
        simulated_time_ += fixed_dt_ * static_cast<float>(count);
        step_count_ += count;
    }

    void reset() noexcept
    {
        simulated_time_ = 0.0F;
        step_count_ = 0U;
    }

    [[nodiscard]] float time() const noexcept { return simulated_time_; }
    [[nodiscard]] float dt() const noexcept { return fixed_dt_; }
    [[nodiscard]] std::uint64_t step_count() const noexcept { return step_count_; }

private:
    float fixed_dt_ { 1.0F / 60.0F };
    float simulated_time_ { 0.0F };
    std::uint64_t step_count_ { 0U };
};

/// Hardware and environment capability probe.
struct EnvironmentProbe final
{
    [[nodiscard]] static bool is_headless_ci() noexcept
    {
#if defined(_WIN32)
        char* buf = nullptr;
        std::size_t sz = 0;
        if (_dupenv_s(&buf, &sz, "CI") == 0 && buf != nullptr) { std::free(buf); return true; }
        if (_dupenv_s(&buf, &sz, "GITHUB_ACTIONS") == 0 && buf != nullptr) { std::free(buf); return true; }
        if (_dupenv_s(&buf, &sz, "CD_HEADLESS_TEST") == 0 && buf != nullptr) { std::free(buf); return true; }
        return false;
#else
        return std::getenv("CI") != nullptr ||
               std::getenv("GITHUB_ACTIONS") != nullptr ||
               std::getenv("CD_HEADLESS_TEST") != nullptr;
#endif
    }

    [[nodiscard]] static std::filesystem::path engine_root()
    {
        auto cur = std::filesystem::current_path();
        for (int i = 0; i < 5; ++i)
        {
            if (std::filesystem::exists(cur / "CMakeLists.txt") &&
                std::filesystem::exists(cur / "engine"))
            {
                return cur;
            }
            if (!cur.has_parent_path() || cur == cur.parent_path())
            {
                break;
            }
            cur = cur.parent_path();
        }
        return std::filesystem::current_path();
    }

    [[nodiscard]] static std::filesystem::path binary_dir()
    {
        const auto root = engine_root();
        const std::array<std::filesystem::path, 4> candidates = {
            root / "build" / "ide-base" / "bin",
            root / "build" / "bin",
            root / "bin",
            std::filesystem::current_path()
        };
        for (const auto& c : candidates)
        {
            if (std::filesystem::exists(c))
            {
                return c;
            }
        }
        return root / "build" / "ide-base" / "bin";
    }
};

}  // namespace cd::e2e
