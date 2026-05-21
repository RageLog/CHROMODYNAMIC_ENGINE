// =============================================================================
// CHROMODYNAMIC — cd::net tests
// =============================================================================
#include <cd/net/IConnection.hpp>
#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace
{

[[nodiscard]] std::vector<std::byte> bytes_of(std::string_view s)
{
    std::vector<std::byte> v(s.size());
    std::memcpy(v.data(), s.data(), s.size());
    return v;
}

TEST(Net, LoopbackPairIsConnected)
{
    auto [a, b] = cd::net::make_loopback_pair();
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(a->state(), cd::net::ConnectionState::kConnected);
    EXPECT_EQ(b->state(), cd::net::ConnectionState::kConnected);
}

TEST(Net, ReceiveEmptyQueueReturnsWouldBlock)
{
    auto [a, b] = cd::net::make_loopback_pair();
    auto r = a->receive();
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, static_cast<std::uint32_t>(cd::net::net_errors::Code::kWouldBlock));
}

TEST(Net, SendOnAReceivesOnB)
{
    auto [a, b] = cd::net::make_loopback_pair();
    const auto payload = bytes_of("hello");
    ASSERT_TRUE(a->send(payload).has_value());
    auto recv = b->receive();
    ASSERT_TRUE(recv.has_value());
    ASSERT_EQ(recv->size(), 5U);
    EXPECT_EQ(static_cast<unsigned char>((*recv)[0]), 'h');
    EXPECT_EQ(a->bytes_sent(), 5U);
    EXPECT_EQ(b->bytes_received(), 5U);
}

TEST(Net, MessagesArePreservedInOrder)
{
    auto [a, b] = cd::net::make_loopback_pair();
    ASSERT_TRUE(a->send(bytes_of("alpha")).has_value());
    ASSERT_TRUE(a->send(bytes_of("beta")).has_value());
    auto r1 = b->receive();
    auto r2 = b->receive();
    ASSERT_TRUE(r1.has_value());
    ASSERT_TRUE(r2.has_value());
    EXPECT_EQ(r1->size(), 5U);  // "alpha"
    EXPECT_EQ(r2->size(), 4U);  // "beta"
}

TEST(Net, BiDirectional)
{
    auto [a, b] = cd::net::make_loopback_pair();
    ASSERT_TRUE(a->send(bytes_of("ping")).has_value());
    ASSERT_TRUE(b->send(bytes_of("pong!")).has_value());
    auto on_b = b->receive();
    auto on_a = a->receive();
    ASSERT_TRUE(on_b.has_value());
    ASSERT_TRUE(on_a.has_value());
    EXPECT_EQ(on_b->size(), 4U);
    EXPECT_EQ(on_a->size(), 5U);
}

TEST(Net, CloseDisconnectsBothEndpoints)
{
    auto [a, b] = cd::net::make_loopback_pair();
    a->close();
    EXPECT_EQ(a->state(), cd::net::ConnectionState::kDisconnected);
    EXPECT_EQ(b->state(), cd::net::ConnectionState::kDisconnected);
    auto r = b->send(bytes_of("nope"));
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, static_cast<std::uint32_t>(cd::net::net_errors::Code::kDisconnected));
}

TEST(Net, PendingBytesDeliveredBeforeDisconnectError)
{
    auto [a, b] = cd::net::make_loopback_pair();
    ASSERT_TRUE(a->send(bytes_of("queued")).has_value());
    a->close();
    // The receiver should still drain queued messages; the disconnect
    // surfaces only when the queue empties.
    auto r = b->receive();
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->size(), 6U);
    auto next = b->receive();
    ASSERT_FALSE(next.has_value());
    EXPECT_EQ(next.error().code, static_cast<std::uint32_t>(cd::net::net_errors::Code::kDisconnected));
}

}  // namespace
