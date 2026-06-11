// =============================================================================
// CHROMODYNAMIC -- samples/engine/hello_engine/tests/test_hello_engine_golden_cli.cpp
//
// phase826-golden-cli-tests: unit tests for the
// `cd::hello_engine::golden::parse_args` pure parser extracted in
// phase825. The parser is the entry point of the closed-loop
// agent-iteration harness (ADR `golden-fixture-agent-iteration-loop`);
// wrong behaviour here = a silent capture-on-the-wrong-frame or
// no-fixture-active failure mode that the agent loop can't surface.
//
// Tests use the pure parse_args() — no global singleton mutated, no
// App lifecycle, no Vulkan. The singleton wrappers (`options()` /
// `parse()` / `enabled()`) defer to parse_args() so they are
// implicitly covered.
// =============================================================================

#include "../HelloGoldenCli.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace
{

// Build an argv-compatible char** from a list of string literals. The
// returned vector owns the storage; pointers stay valid until it goes
// out of scope.
class ArgvBuilder
{
public:
    ArgvBuilder(std::initializer_list<std::string> args)
    {
        owned_.reserve(args.size());
        ptrs_.reserve(args.size() + 1);
        for (const auto& a : args)
        {
            owned_.push_back(a);
        }
        for (auto& s : owned_)
        {
            ptrs_.push_back(s.data());
        }
        ptrs_.push_back(nullptr);
    }

    [[nodiscard]] int    argc() const noexcept { return static_cast<int>(owned_.size()); }
    [[nodiscard]] char** argv() const noexcept
    {
        return const_cast<char**>(ptrs_.data());
    }

private:
    std::vector<std::string>  owned_;
    std::vector<const char*>  ptrs_;
};

}  // namespace

// ===========================================================================
// Empty / null argv guards.
// ===========================================================================

TEST(HelloEngineGoldenCli, EmptyArgvReturnsDefaults)
{
    ArgvBuilder b { "hello_engine.exe" };
    const auto opts = cd::hello_engine::golden::parse_args(b.argc(), b.argv());
    EXPECT_EQ(opts.fixture_index,    -1);
    EXPECT_EQ(opts.capture_at_frame,  2U);
    EXPECT_TRUE(opts.out_png_path.empty());
    EXPECT_FALSE(opts.fixture_seen);
}

TEST(HelloEngineGoldenCli, NullArgvReturnsDefaults)
{
    // The parser must tolerate argv == nullptr without crashing — main()
    // is allowed to call us with that in tests / sanitiser harnesses.
    const auto opts = cd::hello_engine::golden::parse_args(0, nullptr);
    EXPECT_EQ(opts.fixture_index, -1);
    EXPECT_FALSE(opts.fixture_seen);
}

// ===========================================================================
// --golden-fixture happy path + bounds.
// ===========================================================================

TEST(HelloEngineGoldenCli, GoldenFixtureZeroIsAccepted)
{
    ArgvBuilder b { "hello_engine.exe", "--golden-fixture", "0" };
    const auto opts = cd::hello_engine::golden::parse_args(b.argc(), b.argv());
    EXPECT_EQ(opts.fixture_index, 0);
    EXPECT_TRUE(opts.fixture_seen);
}

TEST(HelloEngineGoldenCli, GoldenFixtureFiveIsAccepted)
{
    // chrome_probe lives at slot 5 (the agent-iteration fixture).
    ArgvBuilder b { "hello_engine.exe", "--golden-fixture", "5" };
    const auto opts = cd::hello_engine::golden::parse_args(b.argc(), b.argv());
    EXPECT_EQ(opts.fixture_index, 5);
    EXPECT_TRUE(opts.fixture_seen);
}

TEST(HelloEngineGoldenCli, GoldenFixtureOutOfRangeIsRejected)
{
    // 99 is past kFixtureCount (currently 6). Must NOT silently land at
    // some sentinel slot — must stay disabled.
    ArgvBuilder b { "hello_engine.exe", "--golden-fixture", "99" };
    const auto opts = cd::hello_engine::golden::parse_args(b.argc(), b.argv());
    EXPECT_EQ(opts.fixture_index, -1);
    EXPECT_FALSE(opts.fixture_seen);
}

TEST(HelloEngineGoldenCli, GoldenFixtureNegativeIsRejected)
{
    ArgvBuilder b { "hello_engine.exe", "--golden-fixture", "-1" };
    const auto opts = cd::hello_engine::golden::parse_args(b.argc(), b.argv());
    EXPECT_EQ(opts.fixture_index, -1);
    EXPECT_FALSE(opts.fixture_seen);
}

TEST(HelloEngineGoldenCli, GoldenFixtureNonNumericIsRejected)
{
    // phase816-cert-err34-c: `atoi("xyz")` would silently return 0 and
    // land us on the entrance fixture. strtol must catch this.
    ArgvBuilder b { "hello_engine.exe", "--golden-fixture", "xyz" };
    const auto opts = cd::hello_engine::golden::parse_args(b.argc(), b.argv());
    EXPECT_EQ(opts.fixture_index, -1);
    EXPECT_FALSE(opts.fixture_seen);
}

TEST(HelloEngineGoldenCli, GoldenFixtureWithoutValueIsTolerated)
{
    // Missing value at end of argv must not crash and must leave the
    // index at -1.
    ArgvBuilder b { "hello_engine.exe", "--golden-fixture" };
    const auto opts = cd::hello_engine::golden::parse_args(b.argc(), b.argv());
    EXPECT_EQ(opts.fixture_index, -1);
    EXPECT_FALSE(opts.fixture_seen);
}

// ===========================================================================
// --golden-out path.
// ===========================================================================

TEST(HelloEngineGoldenCli, GoldenOutPathIsCaptured)
{
    ArgvBuilder b { "hello_engine.exe", "--golden-out", "tmp/out.png" };
    const auto opts = cd::hello_engine::golden::parse_args(b.argc(), b.argv());
    EXPECT_EQ(opts.out_png_path, "tmp/out.png");
    // golden-out alone does NOT flip fixture_seen — the fixture is the
    // load-bearing flag.
    EXPECT_FALSE(opts.fixture_seen);
}

TEST(HelloEngineGoldenCli, GoldenOutAndFixtureCompose)
{
    ArgvBuilder b {
        "hello_engine.exe",
        "--golden-fixture", "1",
        "--golden-out",     "build/probe/nave.png",
    };
    const auto opts = cd::hello_engine::golden::parse_args(b.argc(), b.argv());
    EXPECT_EQ(opts.fixture_index, 1);
    EXPECT_EQ(opts.out_png_path,  "build/probe/nave.png");
    EXPECT_TRUE(opts.fixture_seen);
}

// ===========================================================================
// --golden-frames N → capture_at_frame = N-1 (0-based).
// ===========================================================================

TEST(HelloEngineGoldenCli, GoldenFramesOneCapturesAtFrameZero)
{
    ArgvBuilder b { "hello_engine.exe", "--golden-frames", "1" };
    const auto opts = cd::hello_engine::golden::parse_args(b.argc(), b.argv());
    EXPECT_EQ(opts.capture_at_frame, 0U);
}

TEST(HelloEngineGoldenCli, GoldenFramesHundredCapturesAtFrameNinetyNine)
{
    ArgvBuilder b { "hello_engine.exe", "--golden-frames", "100" };
    const auto opts = cd::hello_engine::golden::parse_args(b.argc(), b.argv());
    EXPECT_EQ(opts.capture_at_frame, 99U);
}

TEST(HelloEngineGoldenCli, GoldenFramesZeroIsRejectedKeepsDefault)
{
    // 0 → N-1 would be -1; reject and keep the default of frame 2.
    ArgvBuilder b { "hello_engine.exe", "--golden-frames", "0" };
    const auto opts = cd::hello_engine::golden::parse_args(b.argc(), b.argv());
    EXPECT_EQ(opts.capture_at_frame, 2U);
}

TEST(HelloEngineGoldenCli, GoldenFramesNonNumericIsRejected)
{
    ArgvBuilder b { "hello_engine.exe", "--golden-frames", "abc" };
    const auto opts = cd::hello_engine::golden::parse_args(b.argc(), b.argv());
    EXPECT_EQ(opts.capture_at_frame, 2U);
}

// ===========================================================================
// Unknown / mixed flags.
// ===========================================================================

TEST(HelloEngineGoldenCli, UnknownFlagsAreSilentlyIgnored)
{
    ArgvBuilder b {
        "hello_engine.exe",
        "--asset-path",     "C:/some/dir",
        "--render-quality", "ultra",
        "--golden-fixture", "2",
    };
    const auto opts = cd::hello_engine::golden::parse_args(b.argc(), b.argv());
    EXPECT_EQ(opts.fixture_index, 2);
    EXPECT_TRUE(opts.fixture_seen);
}

TEST(HelloEngineGoldenCli, FullCommandLineRoundTrip)
{
    // The canonical agent-iteration invocation pattern.
    ArgvBuilder b {
        "hello_engine.exe",
        "--golden-fixture", "5",
        "--golden-out",     "build/probe/chrome_probe.png",
        "--golden-frames",  "80",
    };
    const auto opts = cd::hello_engine::golden::parse_args(b.argc(), b.argv());
    EXPECT_EQ(opts.fixture_index,    5);
    EXPECT_EQ(opts.out_png_path,     "build/probe/chrome_probe.png");
    EXPECT_EQ(opts.capture_at_frame, 79U);
    EXPECT_TRUE(opts.fixture_seen);
}
