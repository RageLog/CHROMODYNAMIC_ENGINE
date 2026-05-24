// =============================================================================
// CHROMODYNAMIC — cd::io tests (Sprint S2.6)
// =============================================================================
#include <cd/io/BinaryStream.hpp>
#include <cd/io/BitStream.hpp>
#include <cd/io/Crc32.hpp>
#include <cd/io/Endian.hpp>
#include <cd/io/Framing.hpp>
#include <cd/io/PathUtils.hpp>
#include <gtest/gtest.h>

#include <cstdint>
#include <string>

namespace
{

// --- Endian -----------------------------------------------------------------
TEST(Endian, BswapU16)
{
    EXPECT_EQ(cd::io::detail::bswap<std::uint16_t>(0x1234), 0x3412);
}

TEST(Endian, BswapU32)
{
    EXPECT_EQ(cd::io::detail::bswap<std::uint32_t>(0x12345678), 0x78563412u);
}

TEST(Endian, BswapU64)
{
    EXPECT_EQ(cd::io::detail::bswap<std::uint64_t>(0x0123456789ABCDEFULL), 0xEFCDAB8967452301ULL);
}

TEST(Endian, RoundTripFloat)
{
    std::byte buf[sizeof(float)];
    cd::io::store_le(buf, 3.14159f);
    EXPECT_FLOAT_EQ(cd::io::load_le<float>(buf), 3.14159f);
}

TEST(Endian, RoundTripDouble)
{
    std::byte buf[sizeof(double)];
    cd::io::store_le(buf, 2.718281828459045);
    EXPECT_DOUBLE_EQ(cd::io::load_le<double>(buf), 2.718281828459045);
}

// --- BinaryWriter / BinaryReader -------------------------------------------
TEST(BinaryStream, PrimitiveRoundTrip)
{
    cd::io::BinaryWriter w;
    w.write<std::int32_t>(-42);
    w.write<std::uint64_t>(0xCAFEBABEDEADBEEFULL);
    w.write<float>(1.5f);
    w.write<double>(-9.25);
    w.write_bool(true);
    w.write_bool(false);

    cd::io::BinaryReader r { w.data() };
    EXPECT_EQ(*r.read<std::int32_t>(), -42);
    EXPECT_EQ(*r.read<std::uint64_t>(), 0xCAFEBABEDEADBEEFULL);
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
    w.write<std::uint32_t>(0xAABBCCDD);

    cd::io::BinaryReader r { w.data() };
    ASSERT_TRUE(r.read<std::uint32_t>().has_value());
    auto over = r.read<std::uint32_t>();
    ASSERT_FALSE(over.has_value());
    EXPECT_EQ(over.error().domain, cd::io::binary_errors::kDomain);
    EXPECT_EQ(over.error().code, static_cast<std::uint32_t>(cd::io::binary_errors::Code::kEndOfStream));
}

TEST(BinaryStream, SeekAndPosition)
{
    cd::io::BinaryWriter w;
    for (std::uint32_t i = 0; i < 8; ++i)
        w.write<std::uint32_t>(i * 7);

    cd::io::BinaryReader r { w.data() };
    r.seek(4 * 3);  // skip first 3
    EXPECT_EQ(r.position(), 12u);
    EXPECT_EQ(*r.read<std::uint32_t>(), 3u * 7u);

    r.seek(9999);
    EXPECT_TRUE(r.at_end());
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

// --- BitStream --------------------------------------------------------------
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
    w.write_bits(0x3u, 2);  // 0b11
    w.write_bits(0x5u, 3);  // 0b101
    w.write_bits(0x1234u, 13);
    w.write_bits(0xDEADBEEFCAFEu, 48);

    cd::io::BitReader r { w.data() };
    EXPECT_EQ(*r.read_bits(2), 0x3u);
    EXPECT_EQ(*r.read_bits(3), 0x5u);
    EXPECT_EQ(*r.read_bits(13), 0x1234u);
    EXPECT_EQ(*r.read_bits(48), 0xDEADBEEFCAFEULL);
}

TEST(BitStream, AllZeroMaxWidth)
{
    cd::io::BitWriter w;
    w.write_bits(0xFFFFFFFFFFFFFFFFULL, 64);
    cd::io::BitReader r { w.data() };
    EXPECT_EQ(*r.read_bits(64), 0xFFFFFFFFFFFFFFFFULL);
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
    auto over = r.read_bits(8);  // only 4 padding bits remain
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

// --- Framing ---------------------------------------------------------------
namespace
{
std::vector<std::byte> make_bytes(std::initializer_list<std::uint8_t> il)
{
    std::vector<std::byte> v;
    v.reserve(il.size());
    for (auto b : il)
        v.push_back(static_cast<std::byte>(b));
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
    w.write<std::uint32_t>(100);
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

}  // namespace

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

TEST(Crc32, ResetReturnsToInitial)
{
    constexpr char d[] = "xyz";
    const auto bytes = std::span<const std::byte> {
        reinterpret_cast<const std::byte*>(d), sizeof(d) - 1,
    };
    cd::io::Crc32 c;
    c.update(bytes);
    c.reset();
    EXPECT_EQ(c.value(), 0u);
}

TEST(PathUtils, FilenameStripsPath)
{
    EXPECT_EQ(cd::io::filename("foo/bar/baz.txt"), "baz.txt");
    EXPECT_EQ(cd::io::filename("baz.txt"), "baz.txt");
    EXPECT_EQ(cd::io::filename(""), "");
}

TEST(PathUtils, ExtensionWithLeadingDot)
{
    EXPECT_EQ(cd::io::extension("a/b/c.png"), ".png");
    EXPECT_EQ(cd::io::extension("README"), "");
    EXPECT_EQ(cd::io::extension("a/.hidden"), "");   // dot at index 0 of filename
    EXPECT_EQ(cd::io::extension("archive.tar.gz"), ".gz");
}

TEST(PathUtils, StemStripsExtension)
{
    EXPECT_EQ(cd::io::stem("path/to/file.png"), "file");
    EXPECT_EQ(cd::io::stem("README"), "README");
    EXPECT_EQ(cd::io::stem("a/b.c.d"), "b.c");
}

TEST(PathUtils, ParentIsDirectoryPortion)
{
    EXPECT_EQ(cd::io::parent("a/b/c.txt"), "a/b");
    EXPECT_EQ(cd::io::parent("file.txt"), "");
}
