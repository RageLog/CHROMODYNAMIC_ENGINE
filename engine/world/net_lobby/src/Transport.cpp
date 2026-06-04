// =============================================================================
// CHROMODYNAMIC — cd/network/lobby/Transport.cpp
// Phase 723 / Sprint W5B — MockTransport implementation
// =============================================================================
#include <cd/network/lobby/Transport.hpp>

namespace cd::network::lobby
{

void MockTransport::send(const Packet& pkt)
{
    send_queue.push_back(pkt);
}

std::optional<Packet> MockTransport::recv()
{
    if (recv_queue.empty())
        return std::nullopt;

    // Pop front (FIFO): move the first element out and erase.
    Packet pkt = std::move(recv_queue.front());
    recv_queue.erase(recv_queue.begin());
    return pkt;
}

void MockTransport::inject(const Packet& pkt)
{
    recv_queue.push_back(pkt);
}

}  // namespace cd::network::lobby
