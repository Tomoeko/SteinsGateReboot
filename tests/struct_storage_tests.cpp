#include "sgre/resource/struct_storage.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

template <typename Value>
static sgre::resource::PsbValue Atom(Value value) {
	sgre::resource::PsbValue result;
	result.Data = std::move(value);

	return result;
}

static sgre::resource::PsbValue Declaration(std::int64_t tag) {
	return Atom(sgre::resource::PsbArray{Atom(tag)});
}

static sgre::resource::PsbValue Declaration(std::int64_t tag, std::int64_t parameter) {
	return Atom(sgre::resource::PsbArray{Atom(tag), Atom(parameter)});
}

static sgre::resource::PsbValue Declaration(std::int64_t tag, std::int64_t parameter, std::int64_t count) {
	return Atom(sgre::resource::PsbArray{Atom(tag), Atom(parameter), Atom(count)});
}

static sgre::resource::PsbValue Schema() {
	const sgre::resource::PsbObject root = {{"boolean", Declaration(1)}, {"i8", Declaration(2)}, {"i16", Declaration(3)}, {"i32", Declaration(4)}, {"float", Declaration(5)}, {"bytes", Declaration(6, 4)}, {"wide", Declaration(7, 2)}, {"child", Atom(sgre::resource::PsbArray{Atom(std::int64_t{8}), Atom(std::string("child"))})}, {"flags", Declaration(9, 10)}, {"bytesArray", Declaration(10, 3)}, {"shorts", Declaration(11, 2)}, {"integers", Declaration(12, 2)}, {"floats", Declaration(13, 2)}, {"strings", Declaration(14, 2, 2)}, {"wideStrings", Declaration(15, 2, 2)}, {"children", Atom(sgre::resource::PsbArray{Atom(std::int64_t{16}), Atom(std::string("child")), Atom(std::int64_t{2})})}};
	const sgre::resource::PsbObject child = {{"v", Declaration(3)}, {"b", Declaration(1)}};

	return Atom(sgre::resource::PsbObject{{"root", Atom(root)}, {"child", Atom(child)}});
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

static void TestIndependentPackedVector() {
	sgre::resource::StructStorage storage(Schema());
	Require(storage.RootSize() == 75 && !storage.HasBacking(), "schema layout must be packed and lazy");
	const auto root = storage.Root();
	Require(root.Kind() == sgre::resource::StructValueKind::Table && root.Keys().size() == 16, "root must preserve actual declaration order");
	root.At("boolean").Write(true);
	root.At("i8").Write(std::int32_t{-128});
	root.At("i16").Write(std::int32_t{-32768});
	root.At("i32").Write(std::int32_t{-1});
	root.At("float").Write(-0.0f);
	root.At("bytes").Write(std::string("ABCDE"));
	root.At("wide").Write(std::string("\xe3\x81\x82"));
	root.At("child").At("v").Write(std::int32_t{0x1234});
	root.At("child").At("b").Write(true);
	root.At("flags").At(0).Write(true);
	root.At("flags").At(9).Write(true);
	root.At("bytesArray").At(0).Write(std::int32_t{255});
	root.At("bytesArray").At(1).Write(std::int32_t{256});
	root.At("bytesArray").At(2).Write(std::int32_t{257});
	root.At("shorts").At(0).Write(std::int32_t{-1});
	root.At("shorts").At(1).Write(std::int32_t{65536});
	root.At("integers").At(0).Write(std::bit_cast<std::int32_t>(0x80000000u));
	root.At("integers").At(1).Write(std::int32_t{0x7fffffff});
	root.At("floats").At(0).Write(std::bit_cast<float>(0x7fc01234u));
	root.At("floats").At(1).Write(std::bit_cast<float>(0xff800000u));
	root.At("strings").At(0).Write(std::string("XYZ"));
	root.At("strings").At(1).Write(std::string("r"));
	root.At("wideStrings").At(0).Write(std::string("\xf0\x9f\x99\x82"));
	root.At("wideStrings").At(1).Write(std::string("Z"));
	root.At("children").At(0).At("v").Write(std::int32_t{1});
	root.At("children").At(1).At("v").Write(std::int32_t{2});
	const std::array<std::uint8_t, 75> expected = {0x01, 0x80, 0x00, 0x80, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x80, 0x41, 0x42, 0x43, 0x44, 0x00, 0x42, 0x30, 0x00, 0x00, 0x00, 0x00, 0x34, 0x12, 0x01, 0x01, 0x02, 0xff, 0x00, 0x01, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0xff, 0xff, 0xff, 0x7f, 0x34, 0x12, 0xc0, 0x7f, 0x00, 0x00, 0x80, 0xff, 0x58, 0x59, 0x00, 0x72, 0x00, 0x00, 0x3d, 0xd8, 0x42, 0xde, 0x00, 0x00, 0x5a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x02, 0x00, 0x00};
	const auto actual = storage.Serialize();
	Require(std::equal(actual.begin(), actual.end(), expected.begin(), expected.end()), "independent 75-byte packed schema vector differs");
	Require(std::get<std::int32_t>(root.At("bytesArray").At(0).Read()) == -1 && std::get<std::int32_t>(root.At("i16").Read()) == -32768, "native signed byte/short reads must sign extend");
	Require(std::get<std::string>(root.At("wideStrings").At(0).Read()) == "\xf0\x9f\x99\x82", "UTF16 scalar pair must round trip without normalization");
	Require(std::bit_cast<std::uint32_t>(std::get<float>(root.At("floats").At(0).Read())) == 0x7fc01234u, "float bit payload must survive serialization");
	Require(!root.At("flags").Contains(-1) && root.At("flags").Length() == 10 && root.At("flags").CountFlags() == 2, "flags length/count must preserve bit packing");
	const std::array<std::uint32_t, 3> duplicate_flags = {0, 0, 9};
	Require(root.At("flags").CountFlags(duplicate_flags) == 3 && root.At("flags").AndFlags(duplicate_flags), "flags selectors must preserve duplicates");
	Require(!root.At("flags").AndFlags() && root.At("flags").OrFlags(), "all flags logical predicates differ");
	auto padded = actual;
	padded[27] = 0xff;
	storage.Unserialize(padded);
	Require(root.At("flags").CountFlags() == 9 && !root.At("flags").AndFlags(), "native count must include final-byte padding while and compares its exact logical mask");
	std::cout << "packed schema and primitive/array/nested/string vectors PASS\n";
}

static void TestSharedViewsAndRetirement() {
	sgre::resource::StructStorage storage(Schema());
	auto view = storage.Root().At("children").At(1).At("v");
	view.Write(std::int32_t{123});
	auto duplicate = storage.Duplicate();
	Require(duplicate.Compare(storage) == 0, "dup must copy the exact real root bytes");
	view.Write(std::int32_t{456});
	Require(std::get<std::int32_t>(duplicate.Root().At("children").At(1).At("v").Read()) == 123, "dup may not alias mutable root backing");
	storage.Purge();
	Require(!storage.HasBacking(), "purge must release the actual backing");
	Require(std::get<std::int32_t>(view.Read()) == 0 && storage.HasBacking(), "retained view must address the same lazily re-created root");
	storage.Unserialize(duplicate.Serialize());
	Require(std::get<std::int32_t>(view.Read()) == 123, "unserialize must update existing retained view identities");
	const auto before = storage.Serialize();
	RequireFailure([&] {
		storage.Unserialize(std::span(before).first(74));
	},
	    "short serialized root accepted");
	Require(storage.Serialize() == before, "failed unserialize must preserve published backing");
	storage.Root().At("children").Clear();
	Require(std::get<std::int32_t>(view.Read()) == 0 && storage.Serialize()[26] == before[26], "nested array clear may not alter neighboring packed fields");
	storage.Clear();
	const auto cleared = storage.Serialize();
	Require(std::all_of(cleared.begin(), cleared.end(), [](auto byte) {
		return byte == 0;
	}),
	    "root clear must zero every packed byte");
	std::cout << "shared view purge/reload/clear and independent dup ownership PASS\n";
}

static void TestMalformedSchemaAndBounds() {
	auto cycle = Atom(sgre::resource::PsbObject{{"root", Atom(sgre::resource::PsbObject{{"recursive", Atom(sgre::resource::PsbArray{Atom(std::int64_t{8}), Atom(std::string("root"))})}})}});
	RequireFailure([&] {
		sgre::resource::StructStorage rejected(cycle);
	},
	    "cyclic schema accepted");
	const auto negative = Atom(sgre::resource::PsbObject{{"root", Atom(sgre::resource::PsbObject{{"array", Declaration(9, -1)}})}});
	RequireFailure([&] {
		sgre::resource::StructStorage rejected(negative);
	},
	    "negative array length accepted");
	auto limits = sgre::resource::StructStorageLimits{};
	limits.MaxRootBytes = 74;
	RequireFailure([&] {
		sgre::resource::StructStorage rejected(Schema(), "root", limits);
	},
	    "oversized packed root accepted");
	sgre::resource::StructStorage storage(Schema());
	RequireFailure([&] {
		static_cast<void>(storage.Root().At("bytesArray").At(3));
	},
	    "out of bounds array read accepted");
	RequireFailure([&] {
		static_cast<void>(storage.Root().At("missing"));
	},
	    "absent field accepted");
	auto broken = storage.Serialize();
	broken[12] = 1;
	broken[13] = 1;
	broken[14] = 1;
	broken[15] = 1;
	broken[16] = 1;
	storage.Unserialize(broken);
	RequireFailure([&] {
		static_cast<void>(storage.Root().At("bytes").Read());
	},
	    "unterminated fixed string read accepted");
	const auto zero = Atom(sgre::resource::PsbObject{{"root", Atom(sgre::resource::PsbObject{{"flags", Declaration(9, 0)}})}});
	sgre::resource::StructStorage empty(zero);
	Require(empty.RootSize() == 0 && empty.Root().At("flags").AndFlags() && !empty.Root().At("flags").OrFlags(), "empty native flag predicates must preserve their identities");
	const auto empty_nested = Atom(sgre::resource::PsbObject{{"root", Atom(sgre::resource::PsbObject{{"child", Atom(sgre::resource::PsbArray{Atom(std::int64_t{8}), Atom(std::string("child"))})}})}, {"child", Atom(sgre::resource::PsbObject{})}});
	RequireFailure([&] {
		sgre::resource::StructStorage rejected(empty_nested);
	},
	    "zero-byte nested definition cannot resolve in the original native layout");
	std::cout << "malformed schema, exact byte size, string bounds and empty flags PASS\n";
}

int main() {
	try {
		TestIndependentPackedVector();
		TestSharedViewsAndRetirement();
		TestMalformedSchemaAndBounds();

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
