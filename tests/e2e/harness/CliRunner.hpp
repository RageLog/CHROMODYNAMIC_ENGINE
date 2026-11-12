// =============================================================================
// CHROMODYNAMIC — tests/e2e/harness/CliRunner.hpp
//
// Process execution utility for launching editor.exe, cd_player.exe, and
// cd_assetc.exe with argument passing, stdout/stderr capture, and timeout guards.
// =============================================================================
#pragma once

#include "E2eHarness.hpp"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <chrono>
#include <filesystem>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace cd::e2e
{

struct ProcessResult
{
    int exit_code { -1 };
    std::string stdout_output {};
    std::string stderr_output {};
    bool timed_out { false };
    bool launched { false };
};

class CliRunner final
{
public:
    [[nodiscard]] static ProcessResult run(
        const std::filesystem::path& binary_path,
        std::string_view arguments,
        std::chrono::milliseconds timeout = std::chrono::milliseconds(30000),
        const std::filesystem::path& working_dir = {})
    {
        ProcessResult res {};

#if defined(_WIN32)
        if (!std::filesystem::exists(binary_path))
        {
            res.launched = false;
            res.exit_code = -1;
            res.stderr_output = "Binary not found: " + binary_path.string();
            return res;
        }

        std::string cmd = "\"" + binary_path.string() + "\" " + std::string(arguments);

        SECURITY_ATTRIBUTES sa {};
        sa.nLength = sizeof(SECURITY_ATTRIBUTES);
        sa.bInheritHandle = TRUE;
        sa.lpSecurityDescriptor = nullptr;

        HANDLE h_read_out = nullptr;
        HANDLE h_write_out = nullptr;
        if (!CreatePipe(&h_read_out, &h_write_out, &sa, 0))
        {
            res.stderr_output = "Failed to create output pipe";
            return res;
        }
        SetHandleInformation(h_read_out, HANDLE_FLAG_INHERIT, 0);

        STARTUPINFOA si {};
        si.cb = sizeof(STARTUPINFOA);
        si.hStdOutput = h_write_out;
        si.hStdError = h_write_out;
        si.dwFlags |= STARTF_USESTDHANDLES;

        PROCESS_INFORMATION pi {};

        std::vector<char> cmd_buf(cmd.begin(), cmd.end());
        cmd_buf.push_back('\0');

        const std::filesystem::path effective_dir =
            working_dir.empty() ? binary_path.parent_path() : working_dir;

        const BOOL ok = CreateProcessA(
            nullptr,
            cmd_buf.data(),
            nullptr,
            nullptr,
            TRUE,
            0,
            nullptr,
            effective_dir.string().c_str(),
            &si,
            &pi);

        CloseHandle(h_write_out);

        if (!ok)
        {
            CloseHandle(h_read_out);
            res.launched = false;
            res.exit_code = -1;
            res.stderr_output = "Failed to launch process: " + cmd;
            return res;
        }

        res.launched = true;

        // Concurrently drain the output pipe in a worker thread to eliminate
        // anonymous pipe buffer saturation deadlock when child emits > 4KB.
        std::string captured_output;
        std::thread reader_thread([h_read_out, &captured_output]() {
            char buffer[4096];
            DWORD bytes_read = 0;
            while (ReadFile(h_read_out, buffer, sizeof(buffer), &bytes_read, nullptr) && bytes_read > 0)
            {
                captured_output.append(buffer, bytes_read);
            }
        });

        // Wait for child process with specified timeout
        const DWORD wait_res = WaitForSingleObject(pi.hProcess, static_cast<DWORD>(timeout.count()));
        if (wait_res == WAIT_TIMEOUT)
        {
            res.timed_out = true;
            TerminateProcess(pi.hProcess, 0xDEAD);
        }

        // Ensure process has exited so child write handles are closed
        WaitForSingleObject(pi.hProcess, INFINITE);

        // Closing read handle unblocks reader thread if anything remained pending
        CloseHandle(h_read_out);

        if (reader_thread.joinable())
        {
            reader_thread.join();
        }

        res.stdout_output = std::move(captured_output);

        DWORD exit_code = 0;
        GetExitCodeProcess(pi.hProcess, &exit_code);
        res.exit_code = static_cast<int>(exit_code);

        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
#else
        res.launched = false;
        res.exit_code = -1;
        res.stderr_output = "Platform not supported for CliRunner";
#endif

        return res;
    }

    [[nodiscard]] static ProcessResult run_python(
        std::string_view script_and_args,
        std::chrono::milliseconds timeout = std::chrono::milliseconds(30000))
    {
        const std::filesystem::path cmd_exe = "C:\\Windows\\System32\\cmd.exe";
        const auto engine_root = EnvironmentProbe::engine_root();
        return run(cmd_exe, std::string("/c python ") + std::string(script_and_args), timeout, engine_root);
    }

    [[nodiscard]] static ProcessResult run_python(
        const std::filesystem::path& script_path,
        std::string_view arguments,
        std::chrono::milliseconds timeout = std::chrono::milliseconds(30000))
    {
        const std::filesystem::path cmd_exe = "C:\\Windows\\System32\\cmd.exe";
        const auto engine_root = EnvironmentProbe::engine_root();
        std::string args = "/c python \"" + script_path.string() + "\" " + std::string(arguments);
        return run(cmd_exe, args, timeout, engine_root);
    }

    [[nodiscard]] static ProcessResult run_editor(
        std::string_view arguments,
        std::chrono::milliseconds timeout = std::chrono::milliseconds(30000))
    {
        const auto bin = EnvironmentProbe::binary_dir() / "editor.exe";
        return run(bin, arguments, timeout);
    }

    [[nodiscard]] static ProcessResult run_player(
        std::string_view arguments,
        std::chrono::milliseconds timeout = std::chrono::milliseconds(30000))
    {
        const auto bin = EnvironmentProbe::binary_dir() / "cd_player.exe";
        return run(bin, arguments, timeout);
    }

    [[nodiscard]] static ProcessResult run_assetc(
        std::string_view arguments,
        std::chrono::milliseconds timeout = std::chrono::milliseconds(30000))
    {
        const auto bin = EnvironmentProbe::binary_dir() / "cd_assetc.exe";
        return run(bin, arguments, timeout);
    }
};

}  // namespace cd::e2e
