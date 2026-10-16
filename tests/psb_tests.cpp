#include "sgre/resource/psb.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

using ByteVector = std::vector<std::uint8_t>;

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

template <typename Operation>
static void RequireFailure(Operation operation, std::string_view message) {
	try {
		operation();
	} catch (const std::runtime_error&) {
		return;
	}

	throw std::runtime_error(std::string(message));
}

static void WriteWord(ByteVector& bytes, std::size_t offset, std::uint32_t value) {
	for (std::uint32_t index = 0; index < 4; ++index) {
		bytes.at(offset + index) = static_cast<std::uint8_t>(value >> (index * 8));
	}
}

static void RefreshChecksum(ByteVector& bytes) {
	std::uint32_t first = 1;
	std::uint32_t second = 0;
	for (std::size_t index = 8; index < 40; ++index) {
		first += bytes.at(index);
		second += first;
	}

	if (bytes[4] == 4 && bytes.size() >= 56) {
		for (std::size_t index = 44; index < 56; ++index) {
			first += bytes.at(index);
			second += first;
		}
	}

	WriteWord(bytes, 40, ((second % 65521) << 16) | (first % 65521));
}

struct Fixture {
	ByteVector Bytes;
	std::uint32_t ParentsOffset = 0;
	std::uint32_t LeavesOffset = 0;
	std::uint32_t RootOffset = 0;
	std::uint32_t StringsOffset = 0;
	std::uint32_t ChunkOffsetsOffset = 0;
	std::uint32_t ChunkSizesOffset = 0;
	std::uint32_t DataOffset = 0;
};

static std::uint32_t Append(ByteVector& destination, const ByteVector& source) {
	const auto offset = static_cast<std::uint32_t>(destination.size());
	destination.insert(destination.end(), source.begin(), source.end());

	return offset;
}

static Fixture MakeFixture(const ByteVector& root, const ByteVector& chunk_offsets = {13, 2, 13, 0, 8}, const ByteVector& chunk_sizes = {13, 2, 13, 4, 3}) {
	// This small, explicit trie encodes four one-character keys: a, b, c, and d.
	ByteVector bases(105, 0);
	bases[0] = 13;
	bases[1] = 102;
	bases[2] = 13;
	bases[3] = 1;
	for (std::size_t index = 0; index < 4; ++index) {
		bases[3 + 98 + index] = static_cast<std::uint8_t>(index + 1);
	}

	ByteVector parents(105, 0);
	parents[0] = 13;
	parents[1] = 102;
	parents[2] = 13;
	for (std::size_t index = 0; index < 4; ++index) {
		parents[4 + index] = static_cast<std::uint8_t>(98 + index);
	}

	Fixture fixture;
	fixture.Bytes.resize(44, 0);
	fixture.Bytes[0] = 'P';
	fixture.Bytes[1] = 'S';
	fixture.Bytes[2] = 'B';
	fixture.Bytes[4] = 3;
	WriteWord(fixture.Bytes, 8, 44);
	WriteWord(fixture.Bytes, 12, Append(fixture.Bytes, bases));
	fixture.ParentsOffset = Append(fixture.Bytes, parents);
	fixture.LeavesOffset = Append(fixture.Bytes, {13, 4, 13, 1, 2, 3, 4});
	fixture.RootOffset = Append(fixture.Bytes, root);
	WriteWord(fixture.Bytes, 36, fixture.RootOffset);
	WriteWord(fixture.Bytes, 16, Append(fixture.Bytes, {13, 2, 13, 0, 6}));
	fixture.StringsOffset = Append(fixture.Bytes, {'h', 'e', 'l', 'l', 'o', 0, 0xce, 0xb2, 0});
	WriteWord(fixture.Bytes, 20, fixture.StringsOffset);
	fixture.ChunkOffsetsOffset = Append(fixture.Bytes, chunk_offsets);
	WriteWord(fixture.Bytes, 24, fixture.ChunkOffsetsOffset);
	fixture.ChunkSizesOffset = Append(fixture.Bytes, chunk_sizes);
	WriteWord(fixture.Bytes, 28, fixture.ChunkSizesOffset);
	fixture.DataOffset = static_cast<std::uint32_t>(fixture.Bytes.size());
	WriteWord(fixture.Bytes, 32, fixture.DataOffset);
	RefreshChecksum(fixture.Bytes);

	return fixture;
}

static Fixture MakeTypedFixture() {
	// Object values are an integer, a mixed array, a nested object, and zero.
	const ByteVector root = {33, 13, 4, 13, 0, 1, 2, 3, 13, 4, 13, 0, 4, 38, 49, 7, 0, 0, 0x80, 32, 13, 8, 13, 0, 1, 2, 3, 4, 9, 18, 20, 1, 2, 3, 29, 30, 0, 0, 0xc0, 0x3f, 31, 0, 0, 0, 0, 0, 0, 0x02, 0xc0, 21, 0, 25, 0, 33, 13, 1, 13, 0, 13, 1, 13, 0, 21, 1, 4};

	return MakeFixture(root);
}

static void TestTypedMetadata() {
	const Fixture fixture = MakeTypedFixture();
	const auto header = sgre::resource::ReadPsbHeader(std::span<const std::uint8_t>(fixture.Bytes).first(44));
	Require(header.Version == 3 && header.Flags == 0 && header.HeaderSize == 44, "incorrect header fields");
	Require(header.DataOffset == fixture.Bytes.size(), "incorrect data offset");
	const auto document = sgre::resource::ReadPsbMetadata(fixture.Bytes, fixture.Bytes.size() + 11);
	Require(document.Names == std::vector<std::string>({"a", "b", "c", "d"}), "key trie reconstruction failed");
	Require(document.Strings == std::vector<std::string>({"hello", "\xce\xb2"}), "UTF-8 string table failed");
	Require(document.SerializedSize == fixture.Bytes.size() + 11, "serialized extent omitted chunk padding");
	Require(document.Chunks.size() == 2 && document.Chunks[0].Offset == fixture.DataOffset && document.Chunks[0].Size == 4 && document.Chunks[1].Offset == fixture.DataOffset + 8 && document.Chunks[1].Size == 3, "incorrect absolute chunk ranges");
	Require(document.Root.SerializedTag == 33, "object atom tag identity was lost");
	Require(document.Root.SerializedOffset == fixture.RootOffset, "object value identity must retain the serialized atom offset");
	Require(document.Root.Find("missing") == nullptr, "missing object key was found");
	const auto* integer = document.Root.Find("a");
	Require(integer != nullptr && std::get<std::int64_t>(integer->Data) == -8388608, "24-bit signed integer was not extended");
	Require(integer->Find("a") == nullptr, "scalar Find must return null");
	const auto* array_value = document.Root.Find("b");
	Require(array_value != nullptr && array_value->SerializedTag == 32, "array object key or atom identity missing");
	const auto& array = std::get<sgre::resource::PsbArray>(array_value->Data);
	Require(array.size() == 8 && std::holds_alternative<std::monostate>(array[0].Data), "array/null value mismatch");
	Require(std::get<bool>(array[1].Data) && !std::get<bool>(array[2].Data), "boolean value mismatch");
	Require(array[0].SerializedTag == 1 && array[3].SerializedTag == 29 && array[4].SerializedTag == 30 && array[5].SerializedTag == 31, "normalized null and floating values lost their original atom identity");
	Require(std::get<double>(array[3].Data) == 0.0 && std::get<double>(array[4].Data) == 1.5 && std::get<double>(array[5].Data) == -2.25, "floating-point value mismatch");
	Require(std::get<std::string>(array[6].Data) == "hello" && std::get<sgre::resource::PsbChunkReference>(array[7].Data).Index == 0, "string/chunk reference mismatch");
	const auto* nested = document.Root.Find("c");
	Require(nested != nullptr && nested->Find("a") != nullptr && std::get<std::string>(nested->Find("a")->Data) == "\xce\xb2", "nested object mismatch");
	Require(std::get<std::int64_t>(document.Root.Find("d")->Data) == 0, "integer zero mismatch");
}

static void TestIntegerWidths() {
	for (std::uint8_t width = 1; width <= 8; ++width) {
		ByteVector positive = {static_cast<std::uint8_t>(width + 4)};
		positive.insert(positive.end(), width, 0xff);
		positive.back() = 0x7f;
		const Fixture positive_fixture = MakeFixture(positive);
		const auto positive_document = sgre::resource::ReadPsbMetadata(positive_fixture.Bytes, positive_fixture.Bytes.size() + 11);
		const auto expected_positive = width == 8 ? std::numeric_limits<std::int64_t>::max() : static_cast<std::int64_t>((std::uint64_t{1} << (width * 8 - 1)) - 1);
		Require(positive_document.Root.SerializedTag == width + 4, "signed integer atom width was lost");
		Require(std::get<std::int64_t>(positive_document.Root.Data) == expected_positive, "positive signed integer width mismatch");

		ByteVector negative = {static_cast<std::uint8_t>(width + 4)};
		negative.insert(negative.end(), width, 0);
		negative.back() = 0x80;
		const Fixture negative_fixture = MakeFixture(negative);
		const auto negative_document = sgre::resource::ReadPsbMetadata(negative_fixture.Bytes, negative_fixture.Bytes.size() + 11);
		const auto expected_negative = width == 8 ? std::numeric_limits<std::int64_t>::min() : -static_cast<std::int64_t>(std::uint64_t{1} << (width * 8 - 1));
		Require(std::get<std::int64_t>(negative_document.Root.Data) == expected_negative, "negative signed integer width mismatch");
	}

	const Fixture unsigned_fixture = MakeFixture({16, 0xff, 0xff, 0xff, 0xff});
	Require(std::get<std::int64_t>(sgre::resource::ReadPsbMetadata(unsigned_fixture.Bytes, unsigned_fixture.Bytes.size() + 11).Root.Data) == 4294967295, "unsigned 32-bit value narrowed into signed 32-bit");
}

static void TestSerializedValueIdentity() {
	const Fixture shared = MakeFixture({32, 13, 2, 13, 0, 0, 1});
	const auto shared_document = sgre::resource::ReadPsbMetadata(shared.Bytes, shared.Bytes.size() + 11);
	const auto& shared_values = std::get<sgre::resource::PsbArray>(shared_document.Root.Data);
	Require(shared_document.Root.SerializedOffset == shared.RootOffset && shared_values[0].SerializedOffset == shared.RootOffset + 6 && shared_values[1].SerializedOffset == shared_values[0].SerializedOffset, "repeated PSB references must preserve the same serialized value identity");
	const Fixture distinct = MakeFixture({32, 13, 2, 13, 0, 1, 1, 1});
	const auto distinct_document = sgre::resource::ReadPsbMetadata(distinct.Bytes, distinct.Bytes.size() + 11);
	const auto& distinct_values = std::get<sgre::resource::PsbArray>(distinct_document.Root.Data);
	Require(distinct_values[0].SerializedOffset == distinct.RootOffset + 6 && distinct_values[1].SerializedOffset == distinct.RootOffset + 7 && distinct_values[0].SerializedOffset != distinct_values[1].SerializedOffset, "equal PSB atoms at different offsets must keep distinct native identities");
	Require(!sgre::resource::PsbValue{}.SerializedOffset, "synthetic PSB values must not invent a serialized identity");
}

static void TestExtentsAndEmptyCollections() {
	const Fixture empty = MakeFixture({33, 13, 0, 13, 13, 0, 13}, {13, 0, 13}, {13, 0, 13});
	const auto document = sgre::resource::ReadPsbMetadata(empty.Bytes, empty.Bytes.size());
	Require(document.Chunks.empty() && document.SerializedSize == empty.Bytes.size() && std::get<sgre::resource::PsbObject>(document.Root.Data).empty(), "empty object/chunks failed");

	const Fixture array_fixture = MakeFixture({32, 13, 0, 13});
	Require(std::get<sgre::resource::PsbArray>(sgre::resource::ReadPsbMetadata(array_fixture.Bytes, array_fixture.Bytes.size() + 11).Root.Data).empty(), "empty array failed");
	const Fixture null_fixture = MakeFixture({1});
	Require(std::holds_alternative<std::monostate>(sgre::resource::ReadPsbMetadata(null_fixture.Bytes, null_fixture.Bytes.size() + 11).Root.Data), "null root failed");

	const Fixture large = MakeFixture({25, 0}, {13, 1, 16, 0xff, 0xff, 0xff, 0xff}, {13, 1, 13, 3});
	const std::uint64_t expected_end = static_cast<std::uint64_t>(large.DataOffset) + 4294967295 + 3;
	const auto large_document = sgre::resource::ReadPsbMetadata(large.Bytes, expected_end);
	Require(large_document.SerializedSize == expected_end && large_document.Chunks[0].Offset > std::numeric_limits<std::uint32_t>::max(), "64-bit chunk arithmetic overflowed");
	RequireFailure([&]() {
		static_cast<void>(sgre::resource::ReadPsbMetadata(large.Bytes, expected_end - 1));
	},
	    "large out-of-range chunk was accepted");
}

static void TestHeadersAndTruncation() {
	const Fixture fixture = MakeTypedFixture();
	for (std::size_t length = 0; length < fixture.Bytes.size(); ++length) {
		RequireFailure([&]() {
			static_cast<void>(sgre::resource::ReadPsbMetadata(std::span<const std::uint8_t>(fixture.Bytes).first(length), fixture.Bytes.size() + 11));
		},
		    "truncated metadata was accepted");
	}

	for (const std::size_t offset : std::array<std::size_t, 8>{0, 1, 2, 3, 4, 6, 8, 40}) {
		ByteVector invalid = fixture.Bytes;
		invalid[offset] ^= 1;
		RequireFailure([&]() {
			static_cast<void>(sgre::resource::ReadPsbHeader(invalid));
		},
		    "invalid header was accepted");
	}

	ByteVector incorrect_order = fixture.Bytes;
	WriteWord(incorrect_order, 20, fixture.DataOffset);
	RefreshChecksum(incorrect_order);
	RequireFailure([&]() {
		static_cast<void>(sgre::resource::ReadPsbMetadata(incorrect_order, fixture.Bytes.size() + 11));
	},
	    "invalid section ordering was accepted");
	ByteVector metadata_limit = fixture.Bytes;
	WriteWord(metadata_limit, 32, 64 * 1024 * 1024 + 1);
	RefreshChecksum(metadata_limit);
	RequireFailure([&]() {
		static_cast<void>(sgre::resource::ReadPsbHeader(metadata_limit));
	},
	    "metadata allocation limit was ignored");
	RequireFailure([&]() {
		static_cast<void>(sgre::resource::ReadPsbMetadata(fixture.Bytes, fixture.Bytes.size() - 1));
	},
	    "unavailable metadata bytes were accepted");
	RequireFailure([&]() {
		static_cast<void>(sgre::resource::ReadPsbMetadata(fixture.Bytes, fixture.Bytes.size() + 10));
	},
	    "unavailable chunk bytes were accepted");
}

static void TestMalformedTables() {
	const Fixture fixture = MakeTypedFixture();
	for (const std::uint8_t invalid_tag : std::array<std::uint8_t, 2>{12, 17}) {
		ByteVector invalid = fixture.Bytes;
		invalid[44] = invalid_tag;
		RequireFailure([&]() {
			static_cast<void>(sgre::resource::ReadPsbMetadata(invalid, fixture.Bytes.size() + 11));
		},
		    "invalid array count tag was accepted");
		invalid = fixture.Bytes;
		invalid[46] = invalid_tag;
		RequireFailure([&]() {
			static_cast<void>(sgre::resource::ReadPsbMetadata(invalid, fixture.Bytes.size() + 11));
		},
		    "invalid array element tag was accepted");
	}

	ByteVector oversized = fixture.Bytes;
	oversized[44] = 16;
	WriteWord(oversized, 45, 1'000'001);
	oversized[49] = 13;
	RequireFailure([&]() {
		static_cast<void>(sgre::resource::ReadPsbMetadata(oversized, fixture.Bytes.size() + 11));
	},
	    "oversized compact-array count was accepted");
	ByteVector leaf = fixture.Bytes;
	leaf[fixture.LeavesOffset + 3] = 250;
	RequireFailure([&]() {
		static_cast<void>(sgre::resource::ReadPsbMetadata(leaf, fixture.Bytes.size() + 11));
	},
	    "invalid trie leaf was accepted");
	ByteVector cycle = fixture.Bytes;
	cycle[fixture.ParentsOffset + 3 + 98] = 98;
	RequireFailure([&]() {
		static_cast<void>(sgre::resource::ReadPsbMetadata(cycle, fixture.Bytes.size() + 11));
	},
	    "cyclic key trie was accepted");
	ByteVector unterminated = fixture.Bytes;
	unterminated[fixture.StringsOffset + 5] = 'x';
	unterminated[fixture.StringsOffset + 8] = 'x';
	RequireFailure([&]() {
		static_cast<void>(sgre::resource::ReadPsbMetadata(unterminated, fixture.Bytes.size() + 11));
	},
	    "unterminated string was accepted");
	ByteVector invalid_utf8 = fixture.Bytes;
	invalid_utf8[fixture.StringsOffset + 6] = 0xc0;
	RequireFailure([&]() {
		static_cast<void>(sgre::resource::ReadPsbMetadata(invalid_utf8, fixture.Bytes.size() + 11));
	},
	    "overlong UTF-8 encoding was accepted");
	ByteVector mismatched_chunks = fixture.Bytes;
	mismatched_chunks[fixture.ChunkSizesOffset + 1] = 1;
	RequireFailure([&]() {
		static_cast<void>(sgre::resource::ReadPsbMetadata(mismatched_chunks, fixture.Bytes.size() + 11));
	},
	    "mismatched chunk table counts were accepted");
}

static void TestMalformedValues() {
	const Fixture typed = MakeTypedFixture();
	ByteVector duplicate_key = typed.Bytes;
	duplicate_key[typed.RootOffset + 5] = 0;
	RequireFailure([&]() {
		static_cast<void>(sgre::resource::ReadPsbMetadata(duplicate_key, typed.Bytes.size() + 11));
	},
	    "duplicate object key was accepted");
	ByteVector key_index = typed.Bytes;
	key_index[typed.RootOffset + 4] = 4;
	RequireFailure([&]() {
		static_cast<void>(sgre::resource::ReadPsbMetadata(key_index, typed.Bytes.size() + 11));
	},
	    "invalid object key index was accepted");
	ByteVector offset = typed.Bytes;
	offset[typed.RootOffset + 11] = 255;
	RequireFailure([&]() {
		static_cast<void>(sgre::resource::ReadPsbMetadata(offset, typed.Bytes.size() + 11));
	},
	    "value reference outside the tree was accepted");

	for (const ByteVector& root : std::array<ByteVector, 7>{{{0}, {17, 0}, {34, 0}, {21, 2}, {25, 2}, {12, 0, 0, 0, 0, 0, 0, 0}, {33, 13, 1, 13, 0, 13, 0, 13}}}) {
		const Fixture invalid = MakeFixture(root);
		RequireFailure([&]() {
			static_cast<void>(sgre::resource::ReadPsbMetadata(invalid.Bytes, invalid.Bytes.size() + 11));
		},
		    "invalid typed tree was accepted");
	}

	ByteVector deep_root;
	for (std::size_t depth = 0; depth < 130; ++depth) {
		Append(deep_root, {32, 13, 1, 13, 0});
	}

	deep_root.push_back(1);
	const Fixture deep = MakeFixture(deep_root);
	RequireFailure([&]() {
		static_cast<void>(sgre::resource::ReadPsbMetadata(deep.Bytes, deep.Bytes.size() + 11));
	},
	    "value recursion depth limit was ignored");
}

static void TestVersionFour() {
	const Fixture original = MakeFixture({34, 0});
	ByteVector bytes = original.Bytes;
	bytes.insert(bytes.begin() + 44, 12, 0);
	bytes[4] = 4;
	WriteWord(bytes, 8, 56);
	for (std::size_t field : std::array<std::size_t, 7>{12, 16, 20, 24, 28, 32, 36}) {
		std::uint32_t value = 0;
		for (std::size_t index = 0; index < 4; ++index) {
			value |= static_cast<std::uint32_t>(original.Bytes[field + index]) << (index * 8);
		}

		WriteWord(bytes, field, value + 12);
	}

	const std::uint32_t extra_offsets = original.ChunkOffsetsOffset + 12;
	const ByteVector extra_section = {13, 1, 13, 0, 13, 1, 13, 3, 'x', 'y', 'z'};
	bytes.insert(bytes.begin() + extra_offsets, extra_section.begin(), extra_section.end());
	WriteWord(bytes, 24, original.ChunkOffsetsOffset + 23);
	WriteWord(bytes, 28, original.ChunkSizesOffset + 23);
	WriteWord(bytes, 32, original.DataOffset + 23);
	WriteWord(bytes, 44, extra_offsets);
	WriteWord(bytes, 48, extra_offsets + 4);
	WriteWord(bytes, 52, extra_offsets + 8);
	RefreshChecksum(bytes);
	const auto document = sgre::resource::ReadPsbMetadata(bytes, bytes.size() + 11);
	Require(document.Header.Version == 4 && document.Header.HeaderSize == 56, "version 4 header fields mismatch");
	Require(document.ExtraChunks.size() == 1 && document.ExtraChunks[0].Offset == extra_offsets + 8 && document.ExtraChunks[0].Size == 3, "version 4 extra resource table mismatch");
	const auto reference = std::get<sgre::resource::PsbChunkReference>(document.Root.Data);
	Require(reference.Index == 0 && reference.Extra, "version 4 extra resource reference mismatch");
	Require(document.SerializedSize == bytes.size() + 11, "version 4 serialized extent mismatch");

	auto invalid = bytes;
	invalid[48] ^= 1;
	RequireFailure([&]() {
		static_cast<void>(sgre::resource::ReadPsbHeader(invalid));
	},
	    "version 4 extra header checksum was ignored");
	invalid = bytes;
	invalid[extra_offsets + 7] = 4;
	RequireFailure([&]() {
		static_cast<void>(sgre::resource::ReadPsbMetadata(invalid, invalid.size() + 11));
	},
	    "extra resource overlapped the normal resource tables");
	invalid = bytes;
	invalid[original.RootOffset + 13] = 1;
	RequireFailure([&]() {
		static_cast<void>(sgre::resource::ReadPsbMetadata(invalid, invalid.size() + 11));
	},
	    "extra resource reference exceeded table");
	RequireFailure([&]() {
		static_cast<void>(sgre::resource::ReadPsbHeader(std::span<const std::uint8_t>(bytes).first(55)));
	},
	    "truncated version 4 header was accepted");
}

int main() {
	try {
		TestTypedMetadata();
		TestVersionFour();
		TestIntegerWidths();
		TestSerializedValueIdentity();
		TestExtentsAndEmptyCollections();
		TestHeadersAndTruncation();
		TestMalformedTables();
		TestMalformedValues();
		std::cout << "PSB metadata tests passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << "PSB metadata tests failed: " << error.what() << '\n';

		return 1;
	}
}
