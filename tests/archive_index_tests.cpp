#include "sgre/resource/archive_index.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

template <typename Value>
static sgre::resource::PsbValue MakeValue(Value value) {
	sgre::resource::PsbValue result;
	result.Data = std::move(value);

	return result;
}

static sgre::resource::PsbValue MakeRange(std::int64_t offset, std::int64_t size) {
	return MakeValue(sgre::resource::PsbArray{MakeValue(offset), MakeValue(size)});
}

static sgre::resource::PsbDocument MakeDocument(sgre::resource::PsbValue file_info) {
	sgre::resource::PsbDocument document;
	document.Header.Version = 3;
	document.Header.Flags = 0;
	document.Root.Data = sgre::resource::PsbObject{{"id", MakeValue(std::string("archive"))}, {"version", MakeValue(1.0)}, {"expire_suffix_list", MakeValue(sgre::resource::PsbArray{})}, {"file_info", std::move(file_info)}};

	return document;
}

static sgre::resource::PsbDocument MakeDocument(sgre::resource::PsbObject entries = {}) {
	return MakeDocument(MakeValue(std::move(entries)));
}

static sgre::resource::PsbValue& Member(sgre::resource::PsbDocument& document, std::string_view name) {
	auto& root = std::get<sgre::resource::PsbObject>(document.Root.Data);
	for (auto& member : root) {
		if (member.first == name) {
			return member.second;
		}
	}

	throw std::runtime_error("test requested a missing member");
}

static void RequireFailure(const sgre::resource::PsbDocument& document, std::uint64_t body_size, std::string_view context) {
	try {
		static_cast<void>(sgre::resource::ReadArchiveIndex(document, body_size));
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(context) != std::string_view::npos, "archive failure omitted required context");

		return;
	}

	throw std::runtime_error("invalid archive index was accepted: " + std::string(context));
}

static void TestNamesOrderAndSuffixes() {
	const std::string original_name = "Folder\\MiXeD.PSB";
	const std::string unicode_name = "日本語";
	auto document = MakeDocument(sgre::resource::PsbObject{{original_name, MakeRange(20, 4)}, {"./image/../original", MakeRange(0, 5)}, {unicode_name, MakeRange(9, 0)}});
	const std::string suffix_with_null(".M\0s", 4);
	Member(document, "expire_suffix_list").Data = sgre::resource::PsbArray{MakeValue(std::string(".psb.m")), MakeValue(suffix_with_null), MakeValue(std::string("")), MakeValue(std::string(".psb.m"))};
	const auto index = sgre::resource::ReadArchiveIndex(document, 30);
	Require(index.Entries.size() == 3 && index.SerializedExtent == 24, "gapped archive extent or count changed");
	Require(index.Entries[0].Name == original_name && index.Entries[0].Offset == 20 && index.Entries[0].Size == 4, "first entry was normalized or reordered");
	Require(index.Entries[1].Name == "./image/../original" && index.Entries[2].Name == unicode_name, "archive lookup names were rewritten");
	Require(index.ExpireSuffixes == std::vector<std::string>{".psb.m", suffix_with_null, "", ".psb.m"}, "suffix bytes or order changed");
	Require(std::get<std::string>(std::get<sgre::resource::PsbArray>(Member(document, "expire_suffix_list").Data)[1].Data) == suffix_with_null, "reader changed input metadata");
}

static void TestHeadersAndRootSchema() {
	for (const std::uint16_t version : std::array<std::uint16_t, 3>{0, 2, 4}) {
		auto document = MakeDocument();
		document.Header.Version = version;
		RequireFailure(document, 0, "PSB version 3");
	}

	auto filtered = MakeDocument();
	filtered.Header.Flags = 1;
	RequireFailure(filtered, 0, "PSB version 3");
	for (const auto& invalid_root : std::array<sgre::resource::PsbValue, 3>{sgre::resource::PsbValue{}, MakeValue(false), MakeValue(sgre::resource::PsbArray{})}) {
		auto document = MakeDocument();
		document.Root = invalid_root;
		RequireFailure(document, 0, "root must be an object");
	}

	for (const std::string_view name : std::array<std::string_view, 4>{"id", "version", "expire_suffix_list", "file_info"}) {
		auto document = MakeDocument();
		auto& root = std::get<sgre::resource::PsbObject>(document.Root.Data);
		std::erase_if(root, [name](const auto& member) {
			return member.first == name;
		});
		RequireFailure(document, 0, name);
	}

	for (const auto& identifier : std::array<sgre::resource::PsbValue, 3>{MakeValue(std::string("Archive")), MakeValue(std::string("sound")), MakeValue(std::int64_t{1})}) {
		auto document = MakeDocument();
		Member(document, "id") = identifier;
		RequireFailure(document, 0, "'id'");
	}

	auto duplicate = MakeDocument();
	auto& root = std::get<sgre::resource::PsbObject>(duplicate.Root.Data);
	root.emplace_back("id", MakeValue(std::string("archive")));
	RequireFailure(duplicate, 0, "duplicate root field 'id'");
}

static void TestVersions() {
	for (const auto& version : std::array<sgre::resource::PsbValue, 2>{MakeValue(std::int64_t{1}), MakeValue(1.0)}) {
		auto document = MakeDocument();
		Member(document, "version") = version;
		Require(sgre::resource::ReadArchiveIndex(document, 0).Entries.empty(), "numeric version 1 was rejected");
	}

	for (const auto& version : std::array<sgre::resource::PsbValue, 8>{MakeValue(std::int64_t{0}), MakeValue(std::int64_t{2}), MakeValue(1.00001), MakeValue(true), MakeValue(std::string("1")), MakeValue(std::numeric_limits<double>::infinity()), MakeValue(std::numeric_limits<double>::quiet_NaN()), sgre::resource::PsbValue{}}) {
		auto document = MakeDocument();
		Member(document, "version") = version;
		RequireFailure(document, 0, "'version'");
	}
}

static void TestSuffixSchema() {
	for (const auto& suffixes : std::array<sgre::resource::PsbValue, 4>{sgre::resource::PsbValue{}, MakeValue(std::string(".psb")), MakeValue(sgre::resource::PsbObject{}), MakeValue(sgre::resource::PsbArray{MakeValue(std::string(".m")), MakeValue(std::int64_t{1})})}) {
		auto document = MakeDocument();
		Member(document, "expire_suffix_list") = suffixes;
		RequireFailure(document, 0, "expire_suffix_list");
	}
}

static void TestEmptyMaps() {
	const auto null_document = MakeDocument(sgre::resource::PsbValue{});
	const auto null_index = sgre::resource::ReadArchiveIndex(null_document, 0);
	Require(null_index.Entries.empty() && null_index.SerializedExtent == 0, "confirmed empty-map null representation was rejected");
	RequireFailure(null_document, 1, "null file_info");
	const auto empty_object = MakeDocument();
	Require(sgre::resource::ReadArchiveIndex(empty_object, 0).Entries.empty(), "empty object was rejected");
	Require(sgre::resource::ReadArchiveIndex(empty_object, 100).SerializedExtent == 0, "reader required full body coverage");
	for (const auto& file_info : std::array<sgre::resource::PsbValue, 3>{MakeValue(sgre::resource::PsbArray{}), MakeValue(false), MakeValue(std::string(""))}) {
		RequireFailure(MakeDocument(file_info), 0, "'file_info'");
	}
}

static void TestEntrySchema() {
	for (const auto& entry : std::array<sgre::resource::PsbValue, 6>{sgre::resource::PsbValue{}, MakeValue(false), MakeValue(sgre::resource::PsbObject{}), MakeValue(sgre::resource::PsbArray{}), MakeValue(sgre::resource::PsbArray{MakeValue(std::int64_t{0})}), MakeValue(sgre::resource::PsbArray{MakeValue(std::int64_t{0}), MakeValue(std::int64_t{1}), MakeValue(std::int64_t{2})})}) {
		RequireFailure(MakeDocument(sgre::resource::PsbObject{{"malformed", entry}}), 10, "entry 'malformed'");
	}

	for (const auto& range : std::array<sgre::resource::PsbValue, 6>{MakeRange(-1, 0), MakeRange(0, -1), MakeValue(sgre::resource::PsbArray{MakeValue(0.0), MakeValue(std::int64_t{1})}), MakeValue(sgre::resource::PsbArray{MakeValue(std::int64_t{0}), MakeValue(1.0)}), MakeValue(sgre::resource::PsbArray{MakeValue(false), MakeValue(std::int64_t{1})}), MakeValue(sgre::resource::PsbArray{MakeValue(std::int64_t{0}), MakeValue(std::string("1"))})}) {
		RequireFailure(MakeDocument(sgre::resource::PsbObject{{"negative_or_nonnumeric", range}}), 10, "nonnegative integer");
	}

	RequireFailure(MakeDocument(sgre::resource::PsbObject{{"duplicate", MakeRange(0, 1)}, {"duplicate", MakeRange(1, 1)}}), 2, "duplicate file_info entry 'duplicate'");
}

static void TestRangesAndOverlaps() {
	for (const auto& range : std::array<sgre::resource::PsbValue, 3>{MakeRange(11, 0), MakeRange(0, 11), MakeRange(9, 2)}) {
		RequireFailure(MakeDocument(sgre::resource::PsbObject{{"past_end", range}}), 10, "exceeds body size 10");
	}

	const auto boundary = sgre::resource::ReadArchiveIndex(MakeDocument(sgre::resource::PsbObject{{"last", MakeRange(9, 1)}, {"end", MakeRange(10, 0)}}), 10);
	Require(boundary.SerializedExtent == 10, "exclusive body-end boundary changed");
	const auto adjacent = sgre::resource::ReadArchiveIndex(MakeDocument(sgre::resource::PsbObject{{"second", MakeRange(5, 5)}, {"first", MakeRange(0, 5)}, {"empty", MakeRange(2, 0)}}), 10);
	Require(adjacent.Entries.size() == 3, "adjacent ranges or interior empty ranges were rejected");
	for (const auto& entries : std::array<sgre::resource::PsbObject, 4>{sgre::resource::PsbObject{{"first", MakeRange(0, 5)}, {"second", MakeRange(4, 6)}}, sgre::resource::PsbObject{{"outer", MakeRange(0, 10)}, {"inner", MakeRange(2, 1)}}, sgre::resource::PsbObject{{"same_a", MakeRange(0, 5)}, {"same_b", MakeRange(0, 5)}}, sgre::resource::PsbObject{{"reverse", MakeRange(8, 2)}, {"earlier", MakeRange(2, 7)}}}) {
		RequireFailure(MakeDocument(entries), 10, "overlap");
	}

	const auto empty = sgre::resource::ReadArchiveIndex(MakeDocument(sgre::resource::PsbObject{{"zero_a", MakeRange(10, 0)}, {"zero_b", MakeRange(10, 0)}}), 10);
	Require(empty.SerializedExtent == 10, "empty range positions did not contribute to extent");
}

static void TestLargeRangesAndOverflow() {
	const std::int64_t offset = 0x1'0000'0007;
	const auto large = sgre::resource::ReadArchiveIndex(MakeDocument(sgre::resource::PsbObject{{"large", MakeRange(offset, 0x1'0000'0000)}}), 0x2'0000'0007);
	Require(large.Entries[0].Offset == 0x1'0000'0007 && large.Entries[0].Size == 0x1'0000'0000 && large.SerializedExtent == 0x2'0000'0007, "archive range narrowed to 32 bits");

	// Each serialized integer is signed; its validated sum can exceed INT64_MAX.
	const std::int64_t maximum = std::numeric_limits<std::int64_t>::max();
	const std::uint64_t expected_extent = std::numeric_limits<std::uint64_t>::max() - 1;
	const auto document = MakeDocument(sgre::resource::PsbObject{{"maximum", MakeRange(maximum, maximum)}});
	Require(sgre::resource::ReadArchiveIndex(document, expected_extent).SerializedExtent == expected_extent, "signed overflow corrupted a valid 64-bit extent");
	RequireFailure(document, expected_extent - 1, "exceeds body size");
	RequireFailure(MakeDocument(sgre::resource::PsbObject{{"unsigned_overflow", MakeValue(sgre::resource::PsbArray{MakeValue(18446744073709551616.0), MakeValue(std::int64_t{1})})}}), std::numeric_limits<std::uint64_t>::max(), "nonnegative integer");
}

int main() {
	try {
		TestNamesOrderAndSuffixes();
		TestHeadersAndRootSchema();
		TestVersions();
		TestSuffixSchema();
		TestEmptyMaps();
		TestEntrySchema();
		TestRangesAndOverlaps();
		TestLargeRangesAndOverflow();
		std::cout << "Archive index tests passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << "Archive index tests failed: " << error.what() << '\n';

		return 1;
	}
}
