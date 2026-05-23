// =============================================================================
// CHROMODYNAMIC — engine/foundation/core/src/ErrorFormat.cpp
//
// Process-wide domain-name registry + format() implementation.
// =============================================================================
#include <cd/core/ErrorFormat.hpp>

#include <array>
#include <mutex>
#include <unordered_map>

namespace cd::core
{

namespace
{

struct DomainEntry
{
    std::string_view name {};
    CodeNameFn lookup { nullptr };
};

struct Registry
{
    std::mutex mu;
    std::unordered_map<std::uint32_t, DomainEntry> entries;
};

Registry& registry() noexcept
{
    static Registry r;
    return r;
}

std::string_view core_code_name(std::uint32_t code) noexcept
{
    using C = core_errors::Code;
    switch (static_cast<C>(code))
    {
        case C::kOk:                return "Ok";
        case C::kUnknown:           return "Unknown";
        case C::kInvalidArgument:   return "InvalidArgument";
        case C::kOutOfRange:        return "OutOfRange";
        case C::kOutOfMemory:       return "OutOfMemory";
        case C::kNotImplemented:    return "NotImplemented";
        case C::kPermissionDenied:  return "PermissionDenied";
        case C::kNotFound:          return "NotFound";
        case C::kAlreadyExists:     return "AlreadyExists";
        case C::kAborted:           return "Aborted";
        case C::kTimeout:           return "Timeout";
    }
    return {};
}

// Ensure the core domain is always registered the first time anything
// in this TU runs. Plain static-init is fine: the registry is in the
// same TU and Registry's local-static is constructed on first access.
struct CoreDomainInit
{
    CoreDomainInit() noexcept
    {
        register_domain(core_errors::kDomain, "core", &core_code_name);
    }
};
const CoreDomainInit g_core_domain_init {};

void append_hex(std::string& out, std::uint32_t v)
{
    constexpr std::array<char, 16> kHex { '0','1','2','3','4','5','6','7','8','9','a','b','c','d','e','f' };
    out += "0x";
    // Emit at least 2 hex digits, more if needed.
    char buf[8] {};
    int n = 0;
    do {
        buf[n++] = kHex[v & 0xFu];
        v >>= 4U;
    } while (v != 0U && n < 8);
    while (n < 2)
        buf[n++] = '0';
    for (int i = n - 1; i >= 0; --i)
        out += buf[i];
}

}  // namespace

void register_domain(std::uint32_t domain, std::string_view name, CodeNameFn lookup) noexcept
{
    auto& r = registry();
    std::lock_guard lk(r.mu);
    r.entries[domain] = DomainEntry { name, lookup };
}

std::string_view domain_name(std::uint32_t domain) noexcept
{
    auto& r = registry();
    std::lock_guard lk(r.mu);
    auto it = r.entries.find(domain);
    return it != r.entries.end() ? it->second.name : std::string_view {};
}

std::string_view code_name(std::uint32_t domain, std::uint32_t code) noexcept
{
    auto& r = registry();
    DomainEntry entry {};
    {
        std::lock_guard lk(r.mu);
        auto it = r.entries.find(domain);
        if (it == r.entries.end())
            return {};
        entry = it->second;
    }
    return entry.lookup != nullptr ? entry.lookup(code) : std::string_view {};
}

std::string format(const ErrorCode& ec)
{
    std::string out;
    out.reserve(48 + ec.message.size());

    const auto dname = domain_name(ec.domain);
    const auto cname = code_name(ec.domain, ec.code);

    if (!dname.empty())
        out.append(dname);
    else
        append_hex(out, ec.domain);

    out += "::";

    if (!cname.empty())
        out.append(cname);
    else
        append_hex(out, ec.code);

    if (!ec.message.empty())
    {
        out += ": ";
        out.append(ec.message);
    }
    return out;
}

}  // namespace cd::core
