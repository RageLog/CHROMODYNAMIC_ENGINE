// =============================================================================
// CHROMODYNAMIC — cd::io tests (phase1256 — 100% depth pass)
// =============================================================================
#include <cd/io/BinaryStream.hpp>
#include <cd/io/BitStream.hpp>
#include <cd/io/ByteBuffer.hpp>
#include <cd/io/Crc32.hpp>
#include <cd/io/Endian.hpp>
#include <cd/io/Framing.hpp>
#include <cd/io/Hex.hpp>
#include <cd/io/PathUtils.hpp>
#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <numbers>
#include <string>
#include <vector>

namespace
{

// =============================================================================
// Endian
// =============================================================================

TEST(Endian, BswapU8Noop)
{
    // 1-byte bswap is identity
    EXPECT_EQ(cd::io::detail::bswap<std::uint8_t>(0xABu), 0xABu);
}

TEST(Endian, BswapU16)
{
    EXPECT_EQ(cd::io::detail::bswap<std::uint16_t>(0x1234u), 0x3412u);
}

TEST(Endian, BswapU32)
{
    EXPECT_EQ(cd::io::detail::bswap<std::uint32_t>(0x12345678u), 0x78563412u);
}

TEST(Endian, BswapU64)
{
    EXPECT_EQ(cd::io::detail::bswap<std::uint64_t>(0x0123456789ABCDEFull), 0xEFCDAB8967452301ull);
}

TEST(Endian, RoundTripFloat)
{
    std::byte buf[sizeof(float)];
    cd::io::store_le(buf, std::numbers::pi_v<float>);
    EXPECT_FLOAT_EQ(cd::io::load_le<float>(buf), std::numbers::pi_v<float>);
}

TEST(Endian, RoundTripDouble)
{
    std::byte buf[sizeof(double)];
    cd::io::store_le(buf, std::numbers::e);
    EXPECT_DOUBLE_EQ(cd::io::load_le<double>(buf), 2.718281828459045);
}

TEST(Endian, LittleEndianU32WireLayout)
{
    // 0x01020304 in LE → byte[0]=0x04, byte[3]=0x01
    std::byte buf[4];
    cd::io::store_le(buf, std::uint32_t { 0x01020304u });
    EXPECT_EQ(static_cast<std::uint8_t>(buf[0]), 0x04u);
    EXPECT_EQ(static_cast<std::uint8_t>(buf[1]), 0x03u);
    EXPECT_EQ(static_cast<std::uint8_t>(buf[2]), 0x02u);
    EXPECT_EQ(static_cast<std::uint8_t>(buf[3]), 0x01u);
}

TEST(Endian, BigEndianU32WireLayout)
{
    // 0x01020304 in BE → byte[0]=0x01, byte[3]=0x04
    std::byte buf[4];
    cd::io::store_be(buf, std::uint32_t { 0x01020304u });
    EXPECT_EQ(static_cast<std::uint8_t>(buf[0]), 0x01u);
    EXPECT_EQ(static_cast<std::uint8_t>(buf[1]), 0x02u);
    EXPECT_EQ(static_cast<std::uint8_t>(buf[2]), 0x03u);
    EXPECT_EQ(static_cast<std::uint8_t>(buf[3]), 0x04u);
}

TEST(Endian, BigEndianRoundTripU16)
{
    std::byte buf[2];
    cd::io::store_be(buf, std::uint16_t { 0xABCDu });
    EXPECT_EQ(cd::io::load_be<std::uint16_t>(buf), 0xABCDu);
}

TEST(Endian, BigEndianRoundTripU64)
{
    std::byte buf[8];
    cd::io::store_be(buf, std::uint64_t { 0xDEADBEEFCAFEBABEull });
    EXPECT_EQ(cd::io::load_be<std::uint64_t>(buf), 0xDEADBEEFCAFEBABEull);
}

TEST(Endian, BigEndianRoundTripFloat)
{
    std::byte buf[4];
    cd::io::store_be(buf, -1.5f);
    EXPECT_FLOAT_EQ(cd::io::load_be<float>(buf), -1.5f);
}

TEST(Endian, BigEndianRoundTripDouble)
{
    std::byte buf[8];
    cd::io::store_be(buf, std::numbers::pi);
    EXPECT_DOUBLE_EQ(cd::io::load_be<double>(buf), std::numbers::pi);
}

TEST(Endian, LittleEndianRoundTripU16)
{
    std::byte buf[2];
    cd::io::store_le(buf, std::uint16_t { 0x1234u });
    EXPECT_EQ(cd::io::load_le<std::uint16_t>(buf), 0x1234u);
}

TEST(Endian, LittleEndianRoundTripU64)
{
    std::byte buf[8];
    const std::uint64_t val = 0xFEDCBA9876543210ull;
    cd::io::store_le(buf, val);
    EXPECT_EQ(cd::io::load_le<std::uint64_t>(buf), val);
}

// =============================================================================
// BinaryWriter / BinaryReader
// =============================================================================

TEST(BinaryStream, PrimitiveRoundTrip)
{
    cd::io::BinaryWriter w;
    w.write<std::int32_t>(-42);
    w.write<std::uint64_t>(0xCAFEBABEDEADBEEFull);
    w.write<float>(1.5f);
    w.write<double>(-9.25);
    w.write_bool(true);
    w.write_bool(false);

    cd::io::BinaryReader r { w.data() };
    EXPECT_EQ(*r.read<std::int32_t>(), -42);
    EXPECT_EQ(*r.read<std::uint64_t>(), 0xCAFEBABEDEADBEEFull);
    EXPECT_FLOAT_EQ(*r.read<float>(), 1.5f);
    EXPECT_DOUBLE_EQ(*r.read<double>(), -9.25);
    EXPECT_TRUE(*r.read_bool());
    EXPECT_FALSE(*r.read_bool());
    EXPECT_TRUE(r.at_end());
}

TEST(BinaryStream, StringRoundTrip)
{
    cd::io::BinaryWriter w;
    w.write_string("hello");
    w.write_string("");
    w.write_string("CHROMODYNAMIC \xC3\xB6");  // includes UTF-8

    cd::io::BinaryReader r { w.data() };
    EXPECT_EQ(*r.read_string(), "hello");
    EXPECT_EQ(*r.read_string(), "");
    EXPECT_EQ(*r.read_string(), "CHROMODYNAMIC \xC3\xB6");
}

TEST(BinaryStream, EndOfStreamErrors)
{
    cd::io::BinaryWriter w;
    w.write<std::uint32_t>(0xAABBCCDDu);

    cd::io::BinaryReader r { w.data() };
    ASSERT_TRUE(r.read<std::uint32_t>().has_value());
    auto over = r.read<std::uint32_t>();
    ASSERT_FALSE(over.has_value());
    EXPECT_EQ(over.error().domain, cd::io::binary_errors::kDomain);
    EXPECT_EQ(over.error().code, static_cast<std::uint32_t>(cd::io::binary_errors::Code::kEndOfStream));
}

TEST(BinaryStream, ReadPastEndOnBool)
{
    // Empty stream — read_bool must fail.
    std::array<std::byte, 0> empty {};
    cd::io::BinaryReader r { empty };
    auto res = r.read_bool();
    ASSERT_FALSE(res.has_value());
    EXPECT_EQ(res.error().code, static_cast<std::uint32_t>(cd::io::binary_errors::Code::kEndOfStream));
}

TEST(BinaryStream, ReadStringTruncatedHeader)
{
    // Only 2 bytes — can't even read the 4-byte length prefix.
    std::array<std::byte, 2> tiny { std::byte { 0x00 }, std::byte { 0x00 } };
    cd::io::BinaryReader r { tiny };
    auto res = r.read_string();
    ASSERT_FALSE(res.has_value());
    EXPECT_EQ(res.error().code, static_cast<std::uint32_t>(cd::io::binary_errors::Code::kEndOfStream));
}

TEST(BinaryStream, ReadStringTruncatedBody)
{
    // Write a string then cut the buffer short before the body.
    cd::io::BinaryWriter w;
    w.write_string("LONGSTRING");
    auto full = w.buffer();
    // Keep only the 4-byte header (length=10) — body missing.
    std::span<const std::byte> truncated { full.data(), 4 };
    cd::io::BinaryReader r { truncated };
    auto res = r.read_string();
    ASSERT_FALSE(res.has_value());
    EXPECT_EQ(res.error().code, static_cast<std::uint32_t>(cd::io::binary_errors::Code::kEndOfStream));
}

TEST(BinaryStream, SeekAndPosition)
{
    cd::io::BinaryWriter w;
    for (std::uint32_t i = 0; i < 8; ++i)
        w.write<std::uint32_t>(i * 7);

    cd::io::BinaryReader r { w.data() };
    r.seek(static_cast<std::size_t>(4) * 3);  // skip first 3
    EXPECT_EQ(r.position(), 12u);
    EXPECT_EQ(*r.read<std::uint32_t>(), 3u * 7u);

    r.seek(9999);
    EXPECT_TRUE(r.at_end());
}

TEST(BinaryStream, SeekToZeroRestarts)
{
    cd::io::BinaryWriter w;
    w.write<std::uint32_t>(0xDEAD'BEEFu);
    cd::io::BinaryReader r { w.data() };
    ASSERT_TRUE(r.read<std::uint32_t>().has_value());
    EXPECT_TRUE(r.at_end());
    r.seek(0);
    EXPECT_FALSE(r.at_end());
    EXPECT_EQ(*r.read<std::uint32_t>(), 0xDEAD'BEEFu);
}

TEST(BinaryStream, ReleaseTransfersOwnership)
{
    cd::io::BinaryWriter w;
    w.write<std::uint32_t>(0xDEAD'BEEFu);
    auto vec = w.release();
    EXPECT_EQ(vec.size(), 4u);
    EXPECT_EQ(w.size(), 0u);
}

TEST(BinaryStream, LittleEndianOnTheWire)
{
    // 0x11223344 little-endian → 0x44 0x33 0x22 0x11
    cd::io::BinaryWriter w;
    w.write<std::uint32_t>(0x11223344u);
    const auto data = w.data();
    ASSERT_EQ(data.size(), 4u);
    EXPECT_EQ(static_cast<std::uint8_t>(data[0]), 0x44u);
    EXPECT_EQ(static_cast<std::uint8_t>(data[1]), 0x33u);
    EXPECT_EQ(static_cast<std::uint8_t>(data[2]), 0x22u);
    EXPECT_EQ(static_cast<std::uint8_t>(data[3]), 0x11u);
}

TEST(BinaryStream, PeekDoesNotAdvance)
{
    cd::io::BinaryWriter w;
    w.write<std::uint32_t>(0x12345678u);
    cd::io::BinaryReader r { w.data() };
    EXPECT_EQ(r.position(), 0u);
    auto peeked = r.peek<std::uint32_t>();
    ASSERT_TRUE(peeked.has_value());
    EXPECT_EQ(*peeked, 0x12345678u);
    // Position unchanged — regular read still works.
    EXPECT_EQ(r.position(), 0u);
    EXPECT_EQ(*r.read<std::uint32_t>(), 0x12345678u);
    EXPECT_TRUE(r.at_end());
}

TEST(BinaryStream, PeekAtEndErrors)
{
    cd::io::BinaryWriter w;
    w.write<std::uint8_t>(0x01u);
    cd::io::BinaryReader r { w.data() };
    ASSERT_TRUE(r.read<std::uint8_t>().has_value());
    auto p = r.peek<std::uint8_t>();
    ASSERT_FALSE(p.has_value());
    EXPECT_EQ(p.error().code, static_cast<std::uint32_t>(cd::io::binary_errors::Code::kEndOfStream));
}

TEST(BinaryStream, ReadBytesPartialFail)
{
    // Only 2 bytes available; request 4.
    std::array<std::byte, 2> arr { std::byte { 0xAA }, std::byte { 0xBB } };
    cd::io::BinaryReader r { arr };
    std::byte dst[4] {};
    auto res = r.read_bytes(dst, 4);
    ASSERT_FALSE(res.has_value());
    EXPECT_EQ(res.error().code, static_cast<std::uint32_t>(cd::io::binary_errors::Code::kEndOfStream));
    // Position must NOT have advanced.
    EXPECT_EQ(r.position(), 0u);
}

TEST(BinaryStream, WriteSpanOverload)
{
    // write_bytes(span) appends and can be read back.
    std::array<std::byte, 3> payload { std::byte { 1 }, std::byte { 2 }, std::byte { 3 } };
    cd::io::BinaryWriter w;
    w.write_bytes(std::span<const std::byte> { payload });
    ASSERT_EQ(w.size(), 3u);
    cd::io::BinaryReader r { w.data() };
    std::byte dst[3] {};
    ASSERT_TRUE(r.read_bytes(dst, 3).has_value());
    EXPECT_EQ(dst[0], std::byte { 1 });
    EXPECT_EQ(dst[2], std::byte { 3 });
}

TEST(BinaryStream, ClearThenReuse)
{
    cd::io::BinaryWriter w;
    w.write<std::uint32_t>(1u);
    w.clear();
    EXPECT_EQ(w.size(), 0u);
    w.write<std::uint32_t>(42u);
    EXPECT_EQ(w.size(), 4u);
    cd::io::BinaryReader r { w.data() };
    EXPECT_EQ(*r.read<std::uint32_t>(), 42u);
}

TEST(BinaryStream, ReserveDoesNotChangeSize)
{
    cd::io::BinaryWriter w { 1024 };
    EXPECT_EQ(w.size(), 0u);
    w.reserve(2048);
    EXPECT_EQ(w.size(), 0u);
}

// =============================================================================
// BitStream
// =============================================================================

TEST(BitStream, SingleBitRoundTrip)
{
    cd::io::BitWriter w;
    w.write_bit(true);
    w.write_bit(false);
    w.write_bit(true);
    w.write_bit(true);
    w.write_bit(false);
    EXPECT_EQ(w.bit_position(), 5u);
    cd::io::BitReader r { w.data() };
    EXPECT_TRUE(*r.read_bit());
    EXPECT_FALSE(*r.read_bit());
    EXPECT_TRUE(*r.read_bit());
    EXPECT_TRUE(*r.read_bit());
    EXPECT_FALSE(*r.read_bit());
}

TEST(BitStream, VariableWidthRoundTrip)
{
    cd::io::BitWriter w;
    w.write_bits(0x3u, 2);   // 0b11
    w.write_bits(0x5u, 3);   // 0b101
    w.write_bits(0x1234u, 13);
    w.write_bits(0xDEADBEEFCAFEull, 48);

    cd::io::BitReader r { w.data() };
    EXPECT_EQ(*r.read_bits(2), 0x3u);
    EXPECT_EQ(*r.read_bits(3), 0x5u);
    EXPECT_EQ(*r.read_bits(13), 0x1234u);
    EXPECT_EQ(*r.read_bits(48), 0xDEADBEEFCAFEull);
}

TEST(BitStream, AllOnesMaxWidth)
{
    cd::io::BitWriter w;
    w.write_bits(0xFFFFFFFFFFFFFFFFull, 64);
    cd::io::BitReader r { w.data() };
    EXPECT_EQ(*r.read_bits(64), 0xFFFFFFFFFFFFFFFFull);
}

TEST(BitStream, MaskTruncatesHighBits)
{
    cd::io::BitWriter w;
    // Caller leaves stray high bits — writer must mask down to `bits` lowest.
    w.write_bits(0xABCDu, 4);  // expect only 0xD
    cd::io::BitReader r { w.data() };
    EXPECT_EQ(*r.read_bits(4), 0xDu);
}

TEST(BitStream, EndOfStreamError)
{
    cd::io::BitWriter w;
    w.write_bits(0x5u, 4);
    cd::io::BitReader r { w.data() };
    ASSERT_TRUE(r.read_bits(4).has_value());
    auto over = r.read_bits(8);  // only 4 padding bits remain (in the same byte)
    ASSERT_FALSE(over.has_value());
    EXPECT_EQ(over.error().code, static_cast<std::uint32_t>(cd::io::binary_errors::Code::kEndOfStream));
}

TEST(BitStream, BytePackedRepresentation)
{
    // 8 bits → exactly 1 byte; 9 bits → 2 bytes.
    cd::io::BitWriter w;
    w.write_bits(0xA5u, 8);
    EXPECT_EQ(w.data().size(), 1u);
    EXPECT_EQ(static_cast<std::uint8_t>(w.data()[0]), 0xA5u);
    w.clear();
    w.write_bits(0x1FFu, 9);
    EXPECT_EQ(w.data().size(), 2u);
    EXPECT_EQ(static_cast<std::uint8_t>(w.data()[0]), 0xFFu);
    EXPECT_EQ(static_cast<std::uint8_t>(w.data()[1]) & 0x1u, 0x1u);
}

TEST(BitStream, SeekAndReread)
{
    cd::io::BitWriter w;
    w.write_bits(0xAu, 4);
    w.write_bits(0xBu, 4);
    w.write_bits(0xCu, 4);
    cd::io::BitReader r { w.data() };
    (void)*r.read_bits(4);
    (void)*r.read_bits(4);
    EXPECT_EQ(r.bit_position(), 8u);
    r.seek_bits(0);
    EXPECT_EQ(*r.read_bits(4), 0xAu);
    EXPECT_EQ(*r.read_bits(4), 0xBu);
    EXPECT_EQ(*r.read_bits(4), 0xCu);
}

TEST(BitStream, ByteBoundaryStraddle)
{
    // Write 3 bits then 6 bits — straddles the byte boundary at bit 8.
    cd::io::BitWriter w;
    w.write_bits(0x7u, 3);  // 0b111 → bits [0..2]
    w.write_bits(0x3Fu, 6); // 0b111111 → bits [3..8], crosses byte
    cd::io::BitReader r { w.data() };
    EXPECT_EQ(*r.read_bits(3), 0x7u);
    EXPECT_EQ(*r.read_bits(6), 0x3Fu);
    // 9 bits were written but BitWriter pads to 2 whole bytes; the reader has no
    // original-bit-count, so 7 padding bits remain -> it is NOT at end.
    EXPECT_FALSE(r.at_end());
}

TEST(BitStream, ZeroBitsWrite)
{
    // write_bits with 0 bits is a no-op.
    cd::io::BitWriter w;
    w.write_bits(0xFFu, 0);
    EXPECT_EQ(w.bit_position(), 0u);
    // data() calls finish() — still empty.
    EXPECT_EQ(w.data().size(), 0u);
}

TEST(BitStream, ZeroBitsRead)
{
    // read_bits(0) returns 0 without consuming bits.
    cd::io::BitWriter w;
    w.write_bits(0xAu, 4);
    cd::io::BitReader r { w.data() };
    auto res = r.read_bits(0);
    ASSERT_TRUE(res.has_value());
    EXPECT_EQ(*res, 0u);
    EXPECT_EQ(r.bit_position(), 0u);
}

TEST(BitStream, SeekPastEnd)
{
    cd::io::BitWriter w;
    w.write_bits(0xFFu, 8);
    cd::io::BitReader r { w.data() };
    r.seek_bits(9999);
    EXPECT_TRUE(r.at_end());
    auto res = r.read_bits(1);
    ASSERT_FALSE(res.has_value());
}

TEST(BitStream, ClearAndReuse)
{
    cd::io::BitWriter w;
    w.write_bits(0xFFu, 8);
    w.clear();
    EXPECT_EQ(w.bit_position(), 0u);
    w.write_bits(0x0Fu, 4);
    cd::io::BitReader r { w.data() };
    EXPECT_EQ(*r.read_bits(4), 0x0Fu);
}

// =============================================================================
// Framing
// =============================================================================

namespace
{
std::vector<std::byte> make_bytes(std::initializer_list<std::uint8_t> il)
{
    std::vector<std::byte> v;
    v.reserve(il.size());
    for (auto b : il)
        v.emplace_back(static_cast<std::byte>(b));
    return v;
}
}  // namespace

TEST(Framing, SingleFrameRoundTrip)
{
    cd::io::BinaryWriter w;
    auto payload = make_bytes({ 0xDE, 0xAD, 0xBE, 0xEF });
    cd::io::LengthPrefixWriter::write_frame(w, payload);

    cd::io::LengthPrefixDecoder dec;
    dec.feed(w.data());
    auto r = dec.next_frame();
    ASSERT_TRUE(r.has_value());
    ASSERT_TRUE(r->has_value());
    EXPECT_EQ((*r)->size(), 4u);
    EXPECT_EQ(static_cast<std::uint8_t>((**r)[0]), 0xDEu);
    EXPECT_EQ(static_cast<std::uint8_t>((**r)[3]), 0xEFu);

    // Subsequent call: empty.
    auto empty = dec.next_frame();
    ASSERT_TRUE(empty.has_value());
    EXPECT_FALSE(empty->has_value());
}

TEST(Framing, ChunkedFeedReassembles)
{
    cd::io::BinaryWriter w;
    cd::io::LengthPrefixWriter::write_frame(w, make_bytes({ 1, 2, 3, 4, 5 }));
    cd::io::LengthPrefixWriter::write_frame(w, make_bytes({ 9, 8, 7 }));

    cd::io::LengthPrefixDecoder dec;
    // Feed 1 byte at a time.
    for (auto b : w.data())
    {
        dec.feed(std::span<const std::byte> { &b, 1 });
        auto r = dec.next_frame();
        ASSERT_TRUE(r.has_value());
        // first frame may emerge mid-loop; just keep draining.
        while (r.has_value() && r->has_value())
        {
            r = dec.next_frame();
            ASSERT_TRUE(r.has_value());
        }
    }
    // After all bytes fed we should have drained both frames already.
    EXPECT_EQ(dec.buffered_bytes(), 0u);
}

TEST(Framing, EmptyPayload)
{
    cd::io::BinaryWriter w;
    cd::io::LengthPrefixWriter::write_frame(w, std::span<const std::byte> {});

    cd::io::LengthPrefixDecoder dec;
    dec.feed(w.data());
    auto r = dec.next_frame();
    ASSERT_TRUE(r.has_value());
    ASSERT_TRUE(r->has_value());
    EXPECT_EQ((*r)->size(), 0u);
}

TEST(Framing, OversizedFrameErrors)
{
    // Frame announces 100 bytes but limit is 50.
    cd::io::LengthPrefixDecoder dec { 50 };
    cd::io::BinaryWriter w;
    w.write<std::uint32_t>(100u);
    dec.feed(w.data());
    auto r = dec.next_frame();
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, static_cast<std::uint32_t>(cd::io::binary_errors::Code::kSizeOverflow));
    EXPECT_TRUE(dec.errored());

    // After reset decoder accepts new input.
    dec.reset();
    EXPECT_FALSE(dec.errored());
}

TEST(Framing, MultipleFramesOneFeed)
{
    cd::io::BinaryWriter w;
    cd::io::LengthPrefixWriter::write_frame(w, make_bytes({ 0x11 }));
    cd::io::LengthPrefixWriter::write_frame(w, make_bytes({ 0x22, 0x33 }));
    cd::io::LengthPrefixWriter::write_frame(w, make_bytes({ 0x44, 0x55, 0x66 }));

    cd::io::LengthPrefixDecoder dec;
    dec.feed(w.data());

    auto r1 = dec.next_frame();
    ASSERT_TRUE(r1.has_value() && r1->has_value());
    EXPECT_EQ((*r1)->size(), 1u);
    auto r2 = dec.next_frame();
    ASSERT_TRUE(r2.has_value() && r2->has_value());
    EXPECT_EQ((*r2)->size(), 2u);
    auto r3 = dec.next_frame();
    ASSERT_TRUE(r3.has_value() && r3->has_value());
    EXPECT_EQ((*r3)->size(), 3u);
    auto r4 = dec.next_frame();
    ASSERT_TRUE(r4.has_value());
    EXPECT_FALSE(r4->has_value());
}

TEST(Framing, TruncatedHeader)
{
    // Only 2 bytes — less than the 4-byte length prefix.
    cd::io::LengthPrefixDecoder dec;
    std::array<std::byte, 2> partial { std::byte { 0x05 }, std::byte { 0x00 } };
    dec.feed(partial);
    auto r = dec.next_frame();
    // Not enough data yet — returns nullopt (not error).
    ASSERT_TRUE(r.has_value());
    EXPECT_FALSE(r->has_value());
    EXPECT_EQ(dec.buffered_bytes(), 2u);
}

TEST(Framing, TruncatedBody)
{
    // Write frame of 5 bytes but feed only 3 of the body.
    cd::io::BinaryWriter w;
    cd::io::LengthPrefixWriter::write_frame(w, make_bytes({ 1, 2, 3, 4, 5 }));
    const auto full = w.buffer();
    // Feed header (4) + 3 body bytes = 7 of 9 total.
    cd::io::LengthPrefixDecoder dec;
    dec.feed(std::span<const std::byte> { full.data(), 7 });
    auto r = dec.next_frame();
    ASSERT_TRUE(r.has_value());
    EXPECT_FALSE(r->has_value());  // incomplete — wait for more data
    // Feed the remaining 2 bytes.
    dec.feed(std::span<const std::byte> { full.data() + 7, 2 });
    auto r2 = dec.next_frame();
    ASSERT_TRUE(r2.has_value() && r2->has_value());
    EXPECT_EQ((*r2)->size(), 5u);
}

TEST(Framing, ErroredStateBlocksFurtherReads)
{
    cd::io::LengthPrefixDecoder dec { 4 };
    cd::io::BinaryWriter w;
    w.write<std::uint32_t>(100u);  // exceeds max 4
    dec.feed(w.data());
    auto r1 = dec.next_frame();
    ASSERT_FALSE(r1.has_value());

    // Subsequent call while errored returns the same error.
    auto r2 = dec.next_frame();
    ASSERT_FALSE(r2.has_value());
    EXPECT_EQ(r2.error().code, static_cast<std::uint32_t>(cd::io::binary_errors::Code::kSizeOverflow));
}

TEST(Framing, ResetThenDecodeSucceeds)
{
    cd::io::LengthPrefixDecoder dec { 4 };
    cd::io::BinaryWriter w;
    w.write<std::uint32_t>(100u);
    dec.feed(w.data());
    (void)dec.next_frame();  // trigger error
    dec.reset();

    cd::io::BinaryWriter w2;
    cd::io::LengthPrefixWriter::write_frame(w2, make_bytes({ 0xAA, 0xBB }));
    dec.feed(w2.data());
    auto r = dec.next_frame();
    ASSERT_TRUE(r.has_value() && r->has_value());
    EXPECT_EQ((*r)->size(), 2u);
}

TEST(Framing, EmptyFeedIsNoop)
{
    cd::io::LengthPrefixDecoder dec;
    dec.feed(std::span<const std::byte> {});
    EXPECT_EQ(dec.buffered_bytes(), 0u);
    auto r = dec.next_frame();
    ASSERT_TRUE(r.has_value());
    EXPECT_FALSE(r->has_value());
}

// =============================================================================
// Crc32
// =============================================================================

TEST(Crc32, EmptyInputReturnsZero)
{
    const auto v = cd::io::crc32(std::span<const std::byte> {});
    EXPECT_EQ(v, 0u);
}

TEST(Crc32, KnownVectorFor123456789)
{
    // CRC-32/IEEE of ASCII "123456789" is 0xCBF43926 (zlib test vector).
    constexpr char data[] = "123456789";
    const auto bytes = std::span<const std::byte> {
        reinterpret_cast<const std::byte*>(data), sizeof(data) - 1,
    };
    EXPECT_EQ(cd::io::crc32(bytes), 0xCBF43926u);
}

TEST(Crc32, SingleByteKnownVector)
{
    // CRC-32 of single byte 0x00 = 0xD202EF8D.
    std::byte b { 0x00 };
    EXPECT_EQ(cd::io::crc32(std::span<const std::byte> { &b, 1 }), 0xD202EF8Du);
}

TEST(Crc32, IncrementalEqualsOneShot)
{
    constexpr char data[] = "abcdefghij";
    const auto bytes = std::span<const std::byte> {
        reinterpret_cast<const std::byte*>(data), sizeof(data) - 1,
    };
    cd::io::Crc32 c;
    c.update(bytes.subspan(0, 4));
    c.update(bytes.subspan(4));
    EXPECT_EQ(c.value(), cd::io::crc32(bytes));
}

TEST(Crc32, IncrementalOneBytAtATime)
{
    // Feed one byte at a time and compare to one-shot.
    constexpr char data[] = "CHROMODYNAMIC";
    const auto bytes = std::span<const std::byte> {
        reinterpret_cast<const std::byte*>(data), sizeof(data) - 1,
    };
    cd::io::Crc32 c;
    for (const auto b : bytes)
        c.update(std::span<const std::byte> { &b, 1 });
    EXPECT_EQ(c.value(), cd::io::crc32(bytes));
}

TEST(Crc32, ResetReturnsToInitial)
{
    constexpr char d[] = "xyz";
    const auto bytes = std::span<const std::byte> {
        reinterpret_cast<const std::byte*>(d), sizeof(d) - 1,
    };
    cd::io::Crc32 c;
    c.update(bytes);
    c.reset();
    EXPECT_EQ(c.value(), 0u);  // empty-stream CRC = 0
}

TEST(Crc32, TwoResetsProduceSameResult)
{
    constexpr char d[] = "hello";
    const auto bytes = std::span<const std::byte> {
        reinterpret_cast<const std::byte*>(d), sizeof(d) - 1,
    };
    cd::io::Crc32 c;
    c.update(bytes);
    const auto first = c.value();
    c.reset();
    c.update(bytes);
    EXPECT_EQ(c.value(), first);
}

// =============================================================================
// ByteBuffer
// =============================================================================

TEST(ByteBuffer, EmptyByDefault)
{
    cd::io::ByteBuffer b;
    EXPECT_TRUE(b.empty());
    EXPECT_EQ(b.size(), 0u);
}

TEST(ByteBuffer, AppendPodAccumulates)
{
    cd::io::ByteBuffer b;
    b.append_pod<std::uint32_t>(0xDEADBEEFu);
    b.append_pod<std::uint16_t>(0x1234u);
    EXPECT_EQ(b.size(), 6u);
}

TEST(ByteBuffer, AppendSpanCopiesBytes)
{
    cd::io::ByteBuffer b;
    std::array<std::byte, 4> src { std::byte { 1 }, std::byte { 2 }, std::byte { 3 }, std::byte { 4 } };
    b.append(src);
    ASSERT_EQ(b.size(), 4u);
    EXPECT_EQ(static_cast<std::uint8_t>(b.data()[2]), 3u);
}

TEST(ByteBuffer, ReleaseTransfersOwnership)
{
    cd::io::ByteBuffer b;
    b.append_pod<std::uint32_t>(42u);
    auto v = b.release();
    EXPECT_EQ(v.size(), 4u);
    EXPECT_TRUE(b.empty());
}

TEST(ByteBuffer, ClearEmpties)
{
    cd::io::ByteBuffer b;
    b.append_pod<std::uint32_t>(1u);
    b.clear();
    EXPECT_TRUE(b.empty());
}

TEST(ByteBuffer, GrowBeyondReserve)
{
    cd::io::ByteBuffer b;
    b.reserve(2);
    for (int i = 0; i < 100; ++i)
        b.append_pod<std::uint8_t>(static_cast<std::uint8_t>(i));
    EXPECT_EQ(b.size(), 100u);
    EXPECT_EQ(static_cast<std::uint8_t>(b.data()[99]), 99u);
}

TEST(ByteBuffer, AsSpanMatchesData)
{
    cd::io::ByteBuffer b;
    b.append_pod<std::uint32_t>(0xCAFEBABEu);
    const auto sp = b.as_span();
    ASSERT_EQ(sp.size(), 4u);
    EXPECT_EQ(sp.data(), b.data());
}

TEST(ByteBuffer, MutableDataPointer)
{
    cd::io::ByteBuffer b;
    b.append_pod<std::uint8_t>(0xAAu);
    b.data()[0] = std::byte { 0xBB };
    EXPECT_EQ(static_cast<std::uint8_t>(b.data()[0]), 0xBBu);
}

TEST(ByteBuffer, AppendEmptySpanNoOp)
{
    cd::io::ByteBuffer b;
    b.append(std::span<const std::byte> {});
    EXPECT_TRUE(b.empty());
}

// =============================================================================
// Hex
// =============================================================================

TEST(Hex, EncodeBytes)
{
    std::array<std::byte, 3> b { std::byte { 0xAB }, std::byte { 0xCD }, std::byte { 0x01 } };
    EXPECT_EQ(cd::io::to_hex(b), "abcd01");
}

TEST(Hex, EmptyInput)
{
    EXPECT_EQ(cd::io::to_hex(std::span<const std::byte> {}), "");
}

TEST(Hex, RoundTrip)
{
    const std::string in = "deadbeef00ff";
    std::vector<std::byte> out;
    ASSERT_TRUE(cd::io::from_hex(in, out));
    EXPECT_EQ(cd::io::to_hex(out), in);
}

TEST(Hex, RejectsOddLength)
{
    std::vector<std::byte> out;
    EXPECT_FALSE(cd::io::from_hex("abc", out));
}

TEST(Hex, RejectsInvalidChar)
{
    std::vector<std::byte> out;
    EXPECT_FALSE(cd::io::from_hex("zz", out));
}

TEST(Hex, AcceptsUppercase)
{
    std::vector<std::byte> out;
    ASSERT_TRUE(cd::io::from_hex("AABB", out));
    EXPECT_EQ(out.size(), 2u);
    EXPECT_EQ(static_cast<std::uint8_t>(out[0]), 0xAAu);
}

TEST(Hex, MixedCaseAccepted)
{
    std::vector<std::byte> out;
    ASSERT_TRUE(cd::io::from_hex("aAbBcCdD", out));
    EXPECT_EQ(out.size(), 4u);
    EXPECT_EQ(static_cast<std::uint8_t>(out[0]), 0xAAu);
    EXPECT_EQ(static_cast<std::uint8_t>(out[1]), 0xBBu);
    EXPECT_EQ(static_cast<std::uint8_t>(out[2]), 0xCCu);
    EXPECT_EQ(static_cast<std::uint8_t>(out[3]), 0xDDu);
}

TEST(Hex, SingleByte)
{
    std::array<std::byte, 1> b { std::byte { 0x0F } };
    EXPECT_EQ(cd::io::to_hex(b), "0f");
    std::vector<std::byte> out;
    ASSERT_TRUE(cd::io::from_hex("0f", out));
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(static_cast<std::uint8_t>(out[0]), 0x0Fu);
}

TEST(Hex, InvalidCharInMiddle)
{
    // "ab!c" — '!' is invalid.
    std::vector<std::byte> out;
    EXPECT_FALSE(cd::io::from_hex("ab!c", out));
}

TEST(Hex, AllBytesEncodeCorrectly)
{
    // Spot-check a few critical boundary bytes.
    std::array<std::byte, 3> vals { std::byte { 0x00 }, std::byte { 0x7F }, std::byte { 0xFF } };
    EXPECT_EQ(cd::io::to_hex(vals), "007fff");
}

// =============================================================================
// PathUtils
// =============================================================================

TEST(PathUtils, FilenameStripsPath)
{
    EXPECT_EQ(cd::io::filename("foo/bar/baz.txt"), "baz.txt");
    EXPECT_EQ(cd::io::filename("baz.txt"), "baz.txt");
    EXPECT_EQ(cd::io::filename(""), "");
}

TEST(PathUtils, FilenameTrailingSeparator)
{
    // "foo/bar/" → filename is "" (the part after the last /).
    EXPECT_EQ(cd::io::filename("foo/bar/"), "");
}

TEST(PathUtils, ExtensionWithLeadingDot)
{
    EXPECT_EQ(cd::io::extension("a/b/c.png"), ".png");
    EXPECT_EQ(cd::io::extension("README"), "");
    EXPECT_EQ(cd::io::extension("a/.hidden"), "");   // dot at index 0 of filename
    EXPECT_EQ(cd::io::extension("archive.tar.gz"), ".gz");
}

TEST(PathUtils, ExtensionEmpty)
{
    EXPECT_EQ(cd::io::extension(""), "");
    EXPECT_EQ(cd::io::extension("a/b/"), "");
}

TEST(PathUtils, StemStripsExtension)
{
    EXPECT_EQ(cd::io::stem("path/to/file.png"), "file");
    EXPECT_EQ(cd::io::stem("README"), "README");
    EXPECT_EQ(cd::io::stem("a/b.c.d"), "b.c");
}

TEST(PathUtils, StemHiddenFile)
{
    // ".hidden" — dot is at index 0, so stem == full filename.
    EXPECT_EQ(cd::io::stem(".hidden"), ".hidden");
    EXPECT_EQ(cd::io::stem("dir/.hidden"), ".hidden");
}

TEST(PathUtils, ParentIsDirectoryPortion)
{
    EXPECT_EQ(cd::io::parent("a/b/c.txt"), "a/b");
    EXPECT_EQ(cd::io::parent("file.txt"), "");
}

TEST(PathUtils, ParentEmpty)
{
    EXPECT_EQ(cd::io::parent(""), "");
    EXPECT_EQ(cd::io::parent("/"), "");
}

TEST(PathUtils, NormalizeCollapsesDoubleSep)
{
    EXPECT_EQ(cd::io::normalize("a//b///c"), "a/b/c");
}

TEST(PathUtils, NormalizeBackslash)
{
    EXPECT_EQ(cd::io::normalize("a\\b\\c"), "a/b/c");
    EXPECT_EQ(cd::io::normalize("a\\b/c"), "a/b/c");
}

TEST(PathUtils, NormalizeDotSegment)
{
    EXPECT_EQ(cd::io::normalize("a/./b/./c"), "a/b/c");
}

TEST(PathUtils, NormalizeDotDot)
{
    EXPECT_EQ(cd::io::normalize("a/b/../c"), "a/c");
    EXPECT_EQ(cd::io::normalize("a/b/c/../../d"), "a/d");
}

TEST(PathUtils, NormalizeDotDotAtRoot)
{
    // ".." at the root cannot go above "" — extra ".." is ignored.
    EXPECT_EQ(cd::io::normalize("../../foo"), "foo");
}

TEST(PathUtils, NormalizeTrailingSep)
{
    EXPECT_EQ(cd::io::normalize("a/b/c/"), "a/b/c");
}

TEST(PathUtils, NormalizeEmpty)
{
    EXPECT_EQ(cd::io::normalize(""), "");
}

TEST(PathUtils, NormalizeSingleDot)
{
    EXPECT_EQ(cd::io::normalize("."), "");
}

TEST(PathUtils, NormalizeRootedPath)
{
    EXPECT_EQ(cd::io::normalize("/a/b/../c"), "/a/c");
    EXPECT_EQ(cd::io::normalize("/a/b/"), "/a/b");
}

TEST(PathUtils, JoinBasic)
{
    EXPECT_EQ(cd::io::join("a/b", "c/d"), "a/b/c/d");
}

TEST(PathUtils, JoinTrailingSepOnA)
{
    EXPECT_EQ(cd::io::join("a/b/", "c"), "a/b/c");
}

TEST(PathUtils, JoinLeadingSepOnB)
{
    EXPECT_EQ(cd::io::join("a/b", "/c"), "a/b/c");
}

TEST(PathUtils, JoinBothSeps)
{
    EXPECT_EQ(cd::io::join("a/b/", "/c"), "a/b/c");
}

TEST(PathUtils, JoinEmptyA)
{
    EXPECT_EQ(cd::io::join("", "a/b"), "a/b");
}

TEST(PathUtils, JoinEmptyB)
{
    EXPECT_EQ(cd::io::join("a/b", ""), "a/b");
}

TEST(PathUtils, JoinNormalizesResult)
{
    // join should not leave ".." or "//" in the result.
    EXPECT_EQ(cd::io::join("a/b", "../c"), "a/c");
}

}  // namespace
