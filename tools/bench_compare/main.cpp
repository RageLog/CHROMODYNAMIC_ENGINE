// =============================================================================
// CHROMODYNAMIC — tools/bench_compare
//
// Reads two cd::bench JSON artifacts (baseline + current) and prints a
// Markdown comparison table with per-bench mean delta + classification:
//
//   IMPROVE   — current mean ≤ baseline * (1 - threshold)
//   REGRESS   — current mean ≥ baseline * (1 + threshold)
//   NOISE     — within ±threshold band of the baseline
//
// Default threshold 5 %; override with --threshold=N where N is the
// percent (e.g. --threshold=2 for 2 %).
//
// Usage:
//   cd_bench_compare baseline.json current.json [--threshold=5]
//
// Exit codes:
//   0 — no regressions found
//   1 — at least one bench regressed (CI-friendly)
//   2 — bad input (file missing, parse error, no benches in common)
// =============================================================================
#include <cd/asset/json/Json.hpp>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace
{

struct BenchRow
{
    std::string name;
    double mean_ns { 0.0 };
    double p99_ns { 0.0 };
    double stddev_ns { 0.0 };
    std::uint64_t samples { 0 };
};

[[nodiscard]] cd::core::Result<std::string> read_file(const std::string& path)
{
    std::ifstream in { path, std::ios::binary };
    if (!in)
        return std::unexpected(cd::core::ErrorCode { 0x0001, 1, "cannot open" });
    std::ostringstream o;
    o << in.rdbuf();
    return o.str();
}

[[nodiscard]] cd::core::Result<std::map<std::string, BenchRow>>
parse_bench_json(std::string_view path)
{
    auto bytes = read_file(std::string { path });
    if (!bytes.has_value())
        return std::unexpected(bytes.error());
    auto val = cd::asset::json::parse(*bytes);
    if (!val.has_value())
        return std::unexpected(val.error());
    if (!val->is_array())
        return std::unexpected(cd::core::ErrorCode { 0x0001, 2, "expected JSON array" });

    std::map<std::string, BenchRow> rows;
    for (const auto& entry : val->as_array())
    {
        if (!entry.is_object())
            continue;
        const auto& obj = entry.as_object();
        BenchRow r;
        if (auto it = obj.find("name"); it != obj.end() && it->second.is_string())
            r.name = it->second.as_string();
        if (auto it = obj.find("mean_ns"); it != obj.end() && it->second.is_number())
            r.mean_ns = it->second.as_number();
        if (auto it = obj.find("p99_ns"); it != obj.end() && it->second.is_number())
            r.p99_ns = it->second.as_number();
        if (auto it = obj.find("stddev_ns"); it != obj.end() && it->second.is_number())
            r.stddev_ns = it->second.as_number();
        if (auto it = obj.find("samples"); it != obj.end() && it->second.is_number())
            r.samples = static_cast<std::uint64_t>(it->second.as_number());
        if (!r.name.empty())
            rows.emplace(r.name, std::move(r));
    }
    return rows;
}

}  // namespace

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        std::printf("usage: cd_bench_compare baseline.json current.json [--threshold=PERCENT]\n");
        return 2;
    }
    const std::string baseline_path = argv[1];
    const std::string current_path = argv[2];
    double threshold = 0.05;  // 5 %
    for (int i = 3; i < argc; ++i)
    {
        const std::string_view a = argv[i];
        constexpr std::string_view kPrefix = "--threshold=";
        if (a.starts_with(kPrefix))
        {
            const auto percent = std::atof(std::string { a.substr(kPrefix.size()) }.c_str());
            if (percent > 0.0)
                threshold = percent / 100.0;
        }
    }

    auto baseline = parse_bench_json(baseline_path);
    if (!baseline.has_value())
    {
        std::printf("[bench_compare] failed to parse baseline %s\n", baseline_path.c_str());
        return 2;
    }
    auto current = parse_bench_json(current_path);
    if (!current.has_value())
    {
        std::printf("[bench_compare] failed to parse current  %s\n", current_path.c_str());
        return 2;
    }

    std::printf("# bench_compare\n\n");
    std::printf("Baseline: %s\nCurrent : %s\nThreshold: ±%.2f %%\n\n",
                baseline_path.c_str(), current_path.c_str(), threshold * 100.0);
    std::printf("| Bench | baseline mean (ns) | current mean (ns) | Δ (%%) | verdict |\n");
    std::printf("|---|---:|---:|---:|:---:|\n");

    int regressions = 0;
    int common = 0;
    for (const auto& [name, b] : *baseline)
    {
        auto it = current->find(name);
        if (it == current->end())
        {
            std::printf("| %s | %.1f | (missing) | — | MISSING |\n", name.c_str(), b.mean_ns);
            continue;
        }
        ++common;
        const auto& c = it->second;
        const double delta_pct = b.mean_ns > 0.0
                                   ? (c.mean_ns - b.mean_ns) / b.mean_ns * 100.0
                                   : 0.0;
        const char* verdict = "NOISE";
        if (b.mean_ns > 0.0)
        {
            if (c.mean_ns <= b.mean_ns * (1.0 - threshold))
                verdict = "IMPROVE";
            else if (c.mean_ns >= b.mean_ns * (1.0 + threshold))
            {
                verdict = "REGRESS";
                ++regressions;
            }
        }
        std::printf("| %s | %.1f | %.1f | %+.2f | %s |\n",
                    name.c_str(), b.mean_ns, c.mean_ns, delta_pct, verdict);
    }

    std::printf("\nCommon: %d  Regressions: %d\n", common, regressions);
    if (common == 0)
    {
        std::printf("[bench_compare] no benchmarks in common between the two files\n");
        return 2;
    }
    return regressions > 0 ? 1 : 0;
}
