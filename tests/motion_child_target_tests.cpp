#include "sgre/scene/motion_child_target.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

template <typename Operation>
static void RequireFailure(Operation operation, std::string_view context) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(context) != std::string_view::npos, error.what());

		return;
	}

	throw std::runtime_error("unsupported child target was accepted");
}

template <typename Data>
static sgre::resource::PsbValue Value(Data data) {
	sgre::resource::PsbValue value;
	value.Data = std::move(data);

	return value;
}

static sgre::resource::MotionResource ReadModel(bool with_map) {
	auto timeline = sgre::resource::PsbObject{{"type", Value(std::int64_t{0})}, {"bounds", Value(sgre::resource::PsbObject{{"left", Value(0.0)}, {"top", Value(0.0)}, {"right", Value(0.0)}, {"bottom", Value(0.0)}})}, {"lastTime", Value(0.0)}, {"loopTime", Value(-1.0)}, {"layer", Value(sgre::resource::PsbArray{})}, {"priority", Value(sgre::resource::PsbArray{})}, {"tag", Value(sgre::resource::PsbArray{})}, {"parameter", Value(std::monostate{})}, {"variable", Value(std::monostate{})}, {"parameterize", Value(std::monostate{})}};
	if (with_map) {
		timeline.emplace_back("layerIndexMap", Value(sgre::resource::PsbObject{{"root", Value(std::int64_t{-1})}, {"target", Value(std::int64_t{0})}}));
	}

	sgre::resource::PsbDocument document;
	document.Header.Version = 3;
	document.Root = Value(sgre::resource::PsbObject{{"id", Value(std::string("motion"))}, {"spec", Value(std::string("win"))}, {"version", Value(3.03)}, {"label", Value(std::string("synthetic target map"))}, {"easing", Value(sgre::resource::PsbArray{})}, {"source", Value(sgre::resource::PsbObject{})}, {"object", Value(sgre::resource::PsbObject{{"character", Value(sgre::resource::PsbObject{{"type", Value(std::int64_t{0})}, {"motion", Value(sgre::resource::PsbObject{{"idle", Value(std::move(timeline))}})}})}})}});

	return sgre::resource::ReadMotionResource(document);
}

static void TestRetainedMetadataAndActualSpan() {
	const auto model = ReadModel(true);
	const auto& timeline = model.Objects[0].Timelines[0];
	Require(timeline.LayerIndexMap.Find("root") != nullptr && timeline.LayerIndexMap.Find("target") != nullptr, "first-party model must retain actual serialized layerIndexMap separately from labels");
	Require(sgre::scene::ResolveMotionChildTargetIndex(timeline, "root", 1) == 0 && sgre::scene::ResolveMotionChildTargetIndex(timeline, "target", 1) == 1, "native minus-one targets the actual root while zero targets selected child zero");
	Require(sgre::scene::ResolveMotionChildTargetIndex(timeline, std::string_view("target\0suffix", 13), 1) == 1, "native target lookup stops at NUL");
	Require(!sgre::scene::ResolveMotionChildTargetIndex(timeline, "Target", 1) && !sgre::scene::ResolveMotionChildTargetIndex(timeline, "missing", 1), "target map keys retain actual byte case and missing lookup");
	const auto absent = ReadModel(false);
	Require(!sgre::scene::ResolveMotionChildTargetIndex(absent.Objects[0].Timelines[0], "target", 1), "absent native layerIndexMap is not replaced by labels or a synthetic target");
	RequireFailure([&] {
		static_cast<void>(sgre::scene::ResolveMotionChildTargetIndex(timeline, "target", 0));
	},
	    "actual selected span");
}

static void TestConversionsAndExplicitRejections() {
	sgre::resource::MotionTimeline timeline;
	timeline.LayerIndexMap = Value(sgre::resource::PsbObject{{"target", Value(-1.9)}, {"Target", Value(1.9)}, {"あ", Value(true)}});
	Require(sgre::scene::ResolveMotionChildTargetIndex(timeline, "target", 3) == 0 && sgre::scene::ResolveMotionChildTargetIndex(timeline, "Target", 3) == 2 && sgre::scene::ResolveMotionChildTargetIndex(timeline, "あ", 3) == 2, "native numeric target conversion truncates toward zero and preserves actual UTF8/case keys");
	timeline.LayerIndexMap = Value(sgre::resource::PsbObject{{"target", Value(std::int64_t{-2})}});
	RequireFailure([&] {
		static_cast<void>(sgre::scene::ResolveMotionChildTargetIndex(timeline, "target", 3));
	},
	    "actual selected span");
	timeline.LayerIndexMap = Value(sgre::resource::PsbObject{{"target", Value(std::numeric_limits<double>::quiet_NaN())}});
	RequireFailure([&] {
		static_cast<void>(sgre::scene::ResolveMotionChildTargetIndex(timeline, "target", 3));
	},
	    "conversion domain");
	timeline.LayerIndexMap = Value(sgre::resource::PsbObject{{"target", Value(std::int64_t{0})}, {"target", Value(std::int64_t{1})}});
	RequireFailure([&] {
		static_cast<void>(sgre::scene::ResolveMotionChildTargetIndex(timeline, "target", 3));
	},
	    "ambiguous duplicate");
	timeline.LayerIndexMap = Value(true);
	RequireFailure([&] {
		static_cast<void>(sgre::scene::ResolveMotionChildTargetIndex(timeline, "target", 3));
	},
	    "layerIndexMap object");
	timeline.Kind = sgre::resource::MotionTimelineKind::Division;
	RequireFailure([&] {
		static_cast<void>(sgre::scene::ResolveMotionChildTargetIndex(timeline, "target", 3));
	},
	    "ordinary selected layer span");
}

static std::uint32_t ReadWord(std::span<const std::uint8_t> bytes, std::size_t offset) {
	if (offset > bytes.size() || 4 > bytes.size() - offset) {
		throw std::runtime_error("truncated native child target word");
	}

	std::uint32_t result = 0;
	for (std::size_t index = 0; index < 4; ++index) {
		result |= static_cast<std::uint32_t>(bytes[offset + index]) << (index * 8);
	}

	return result;
}

static std::uint64_t ReadWide(std::span<const std::uint8_t> bytes, std::size_t offset) {
	return static_cast<std::uint64_t>(ReadWord(bytes, offset)) | (static_cast<std::uint64_t>(ReadWord(bytes, offset + 4)) << 32);
}

template <std::size_t Size>
static std::array<std::uint8_t, Size> ReadRecord(std::ifstream& input) {
	std::array<char, Size> raw{};
	input.read(raw.data(), static_cast<std::streamsize>(raw.size()));
	Require(input.gcount() == static_cast<std::streamsize>(raw.size()), "truncated native child target record");
	std::array<std::uint8_t, Size> result{};
	for (std::size_t index = 0; index < Size; ++index) {
		result[index] = static_cast<std::uint8_t>(static_cast<unsigned char>(raw[index]));
	}

	return result;
}

static void ReplayNative(const char* path) {
	const std::array<std::string, 4> keys{"target", "Target", "あ", "root"};
	const std::array<std::string, 7> queries{"target", "Target", "あ", "root", "missing", "", "TARGET"};
	std::ifstream input(path, std::ios::binary);
	Require(input.is_open(), "cannot open native child target fixture");
	const auto header = ReadRecord<8>(input);
	Require(header[0] == 'M' && header[1] == 'C' && header[2] == 'D' && header[3] == 'T', "invalid native child target signature");
	const auto count = ReadWord(header, 4);
	Require(count > 0 && count <= 512, "native child target work exceeds its bound");
	for (std::uint32_t index = 0; index < count; ++index) {
		const auto record = ReadRecord<19>(input);
		Require(record[0] <= 2 && record[1] < keys.size() && record[2] < queries.size() && record[3] <= 1 && record[4] <= 3 && record[14] <= 1, "native child target input flags exceed their domain");
		sgre::resource::PsbValue value;
		const auto payload = ReadWide(record, 5);
		if (record[4] == 0) {
			value = Value(std::bit_cast<std::int64_t>(payload));
		} else if (record[4] == 1) {
			value = Value(payload != 0);
		} else {
			value = Value(std::bit_cast<double>(payload));
		}

		sgre::resource::MotionTimeline timeline;
		if (record[0] != 0) {
			timeline.LayerIndexMap = Value(record[0] == 2 ? sgre::resource::PsbObject{{keys[record[1]], value}} : sgre::resource::PsbObject{});
		}

		auto query = queries[record[2]];
		if (record[3] != 0) {
			query += std::string("\0suffix", 7);
		}

		const auto present = record[14] != 0;
		const auto ordinal = ReadWord(record, 15);
		if (present && ordinal > record[13]) {
			RequireFailure([&] {
				static_cast<void>(sgre::scene::ResolveMotionChildTargetIndex(timeline, query, record[13]));
			},
			    "actual selected span");
		} else {
			const auto actual = sgre::scene::ResolveMotionChildTargetIndex(timeline, query, record[13]);
			Require(actual.has_value() == present && (!actual || *actual == ordinal), "child target presence/root-containing ordinal differs from actual native lookup");
		}
	}

	Require(input.peek() == std::char_traits<char>::eof(), "native child target fixture has trailing data");
}

int main(int argc, char** argv) {
	try {
		Require(argc == 1 || argc == 2, "usage: motion_child_target_tests [native-target-fixture]");
		TestRetainedMetadataAndActualSpan();
		TestConversionsAndExplicitRejections();
		if (argc == 2) {
			ReplayNative(argv[1]);
		}

		std::cout << "motion child target tests passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
