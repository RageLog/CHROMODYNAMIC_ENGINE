// =============================================================================
// CHROMODYNAMIC — samples/hello_udp
//
// Demonstrates the cd::net::UdpConnection + ChannelMux + ReliableChannel
// stack inside one process: two endpoints on 127.0.0.1, a few rounds of
// reliable messaging, and a printout of bytes_sent / bytes_received /
// pending_send_count / retransmit_count.
//
// No CLI args; the sample picks a high-numbered port pair and skips
// cleanly with exit 0 if the ports are busy.
// =============================================================================
#include <cd/net/ChannelMux.hpp>
#include <cd/net/Retransmit.hpp>
#include <cd/net/UdpConnection.hpp>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace
{

[[nodiscard]] std::vector<std::byte> to_bytes(std::string_view s)
{
    std::vector<std::byte> v(s.size());
    if (!s.empty())
        std::memcpy(v.data(), s.data(), s.size());
    return v;
}

[[nodiscard]] std::string from_bytes(std::span<const std::byte> b)
{
    std::string s(b.size(), '\0');
    if (!b.empty())
        std::memcpy(s.data(), b.data(), b.size());
    return s;
}

}  // namespace

int main()
{
    std::printf("=== hello_udp — UDP + ReliableChannel demo ===\n");

    constexpr std::uint16_t kPortA = 47411;
    constexpr std::uint16_t kPortB = 47412;

    auto pair = cd::net::make_udp_pair_localhost(kPortA, kPortB);
    if (!pair.has_value())
    {
        std::printf("[hello_udp] UDP pair build failed (ports busy?) — exiting 0\n");
        return 0;
    }

    auto& [a, b] = *pair;
    cd::net::ChannelMux ma { *a };
    cd::net::ChannelMux mb { *b };

    cd::net::ReliableChannel sender { ma, /*user=*/0, /*ack=*/1 };
    cd::net::ReliableChannel receiver { mb, /*user=*/0, /*ack=*/1 };

    constexpr int kRounds = 5;
    for (int i = 0; i < kRounds; ++i)
    {
        const auto msg = std::string { "ping-" } + std::to_string(i);
        const auto bytes = to_bytes(msg);
        auto seq = sender.send({ bytes.data(), bytes.size() });
        if (!seq.has_value())
        {
            std::printf("[hello_udp] send failed: %u\n", seq.error().code);
            return 1;
        }
        std::printf("  A → B  seq=%u  payload=\"%s\"\n", *seq, msg.c_str());
    }

    // Pump both sides for a short window. UDP delivery on localhost is
    // effectively synchronous but the API contract is async.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds { 100 };
    while (std::chrono::steady_clock::now() < deadline)
    {
        const auto now = std::chrono::steady_clock::now();
        receiver.tick(now);
        sender.tick(now);
        if (sender.pending_send_count() == 0 && receiver.ready_count() == 0
            && receiver.duplicate_drop_count() == 0)
            break;
        std::this_thread::yield();

        // Drain any payloads ready on the receiver.
        while (true)
        {
            auto got = receiver.receive();
            if (!got.has_value())
                break;
            std::printf("  B ← A  payload=\"%s\"\n", from_bytes(*got).c_str());
        }
        if (sender.pending_send_count() == 0)
            break;
    }

    std::printf("\n=== Summary ===\n");
    std::printf("  sender.bytes_sent     = %zu\n", a->bytes_sent());
    std::printf("  receiver.bytes_recv   = %zu\n", b->bytes_received());
    std::printf("  sender.pending        = %zu (expected 0)\n",
                sender.pending_send_count());
    std::printf("  sender.retransmits    = %u\n", sender.retransmit_count());
    std::printf("  receiver.duplicate_drops = %u\n", receiver.duplicate_drop_count());
    std::printf("[hello_udp] done\n");
    return 0;
}
