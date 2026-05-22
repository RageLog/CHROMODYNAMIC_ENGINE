// =============================================================================
// CHROMODYNAMIC — tools/changelog_gen
//
// Reads `git log --format='%h %s'` from stdin and emits a Markdown
// changelog block grouped by Conventional Commit type:
//
//   feat:     ✨ Features
//   fix:      🐛 Fixes
//   perf:     ⚡ Performance
//   refactor: ♻️  Refactors
//   docs:     📚 Documentation
//   test:     🧪 Tests
//   build:    🔨 Build / CMake
//   ci:       🤖 CI
//   chore:    🧹 Chores
//
// Commit subjects with scope (`feat(net): …`) keep the scope as a
// `**[scope]**` prefix in the rendered bullet so the scope-as-area
// stays human-skimmable.
//
// Headerless (no markdown title — the caller wraps with their own
// "## v0.X.0 (YYYY-MM-DD)" or release-notes preamble).
//
// CLI:
//   git log --format='%h %s' v0.6.0..HEAD | cd_changelog_gen
//
// Exit 0 always (warnings about unknown types go to stderr).
// =============================================================================
#include <algorithm>
#include <array>
#include <cstdio>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace
{

struct Group
{
    std::string_view type;
    std::string_view title;
};

constexpr std::array<Group, 9> kGroups {
    Group { "feat",     "Features" },
    Group { "fix",      "Fixes" },
    Group { "perf",     "Performance" },
    Group { "refactor", "Refactors" },
    Group { "docs",     "Documentation" },
    Group { "test",     "Tests" },
    Group { "build",    "Build / CMake" },
    Group { "ci",       "CI" },
    Group { "chore",    "Chores" },
};

struct Entry
{
    std::string sha;
    std::string scope;
    std::string subject;
};

[[nodiscard]] bool parse_line(std::string_view line, std::string& sha,
                              std::string& type, std::string& scope,
                              std::string& subject)
{
    // Format: "<sha> <type>(<scope>): <subject>"  or  "<sha> <type>: <subject>"
    // SHA = up to first space.
    const auto sp = line.find(' ');
    if (sp == std::string_view::npos)
        return false;
    sha.assign(line.substr(0, sp));
    auto rest = line.substr(sp + 1);
    // Find the first ':'. Anything before is the prefix `type[(scope)]`.
    const auto colon = rest.find(':');
    if (colon == std::string_view::npos)
        return false;
    auto prefix = rest.substr(0, colon);
    subject.assign(rest.substr(colon + 1));
    // Trim leading spaces from subject.
    while (!subject.empty() && subject.front() == ' ')
        subject.erase(subject.begin());

    const auto open_paren = prefix.find('(');
    if (open_paren != std::string_view::npos)
    {
        const auto close_paren = prefix.find(')', open_paren + 1);
        if (close_paren == std::string_view::npos)
            return false;
        type.assign(prefix.substr(0, open_paren));
        scope.assign(prefix.substr(open_paren + 1, close_paren - open_paren - 1));
    }
    else
    {
        type.assign(prefix);
        scope.clear();
    }
    return !type.empty();
}

}  // namespace

int main()
{
    std::map<std::string, std::vector<Entry>> grouped;
    std::vector<Entry> unknown;

    std::string line;
    while (std::getline(std::cin, line))
    {
        if (line.empty())
            continue;
        std::string sha, type, scope, subject;
        if (!parse_line(line, sha, type, scope, subject))
        {
            std::fprintf(stderr, "[changelog_gen] skip unparseable: %s\n", line.c_str());
            continue;
        }
        Entry e { std::move(sha), std::move(scope), std::move(subject) };
        const bool known =
            std::any_of(kGroups.begin(), kGroups.end(),
                        [&](const Group& g) { return g.type == type; });
        if (known)
            grouped[type].push_back(std::move(e));
        else
            unknown.push_back(std::move(e));
    }

    for (const auto& g : kGroups)
    {
        const auto it = grouped.find(std::string { g.type });
        if (it == grouped.end() || it->second.empty())
            continue;
        std::printf("### %.*s\n\n", static_cast<int>(g.title.size()), g.title.data());
        for (const auto& e : it->second)
        {
            if (!e.scope.empty())
                std::printf("- **[%s]** %s (`%s`)\n",
                            e.scope.c_str(), e.subject.c_str(), e.sha.c_str());
            else
                std::printf("- %s (`%s`)\n", e.subject.c_str(), e.sha.c_str());
        }
        std::printf("\n");
    }

    if (!unknown.empty())
    {
        std::printf("### Other\n\n");
        for (const auto& e : unknown)
        {
            if (!e.scope.empty())
                std::printf("- **[%s]** %s (`%s`)\n",
                            e.scope.c_str(), e.subject.c_str(), e.sha.c_str());
            else
                std::printf("- %s (`%s`)\n", e.subject.c_str(), e.sha.c_str());
        }
        std::printf("\n");
    }
    return 0;
}
