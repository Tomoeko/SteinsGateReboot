#include "sgre/resource/motion.hpp"
#include "sgre/scene/motion_division.hpp"
#include "sgre/scene/motion_timeline_clock.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

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

	throw std::runtime_error("invalid motion timeline metadata/control was accepted");
}

template <typename Type>
static sgre::resource::PsbValue Value(Type value) {
	sgre::resource::PsbValue result;
	result.Data = std::move(value);

	return result;
}

static sgre::resource::PsbValue Frames() {
	return Value(sgre::resource::PsbObject{{"type", Value(std::int64_t{0})}, {"bounds", Value(sgre::resource::PsbObject{{"left", Value(-5.0)}, {"top", Value(4.0)}, {"right", Value(-10.0)}, {"bottom", Value(-8.0)}})}, {"lastTime", Value(20.0)}, {"loopTime", Value(-1.0)}, {"layer", Value(sgre::resource::PsbArray{})}, {"priority", Value(sgre::resource::PsbArray{})}, {"tag", Value(sgre::resource::PsbArray{Value(sgre::resource::PsbObject{{"time", Value(5.0)}, {"type", Value(std::int64_t{1})}, {"content", Value(sgre::resource::PsbObject{{"label", Value(std::string("cue"))}})}})})}, {"parameter", Value(sgre::resource::PsbArray{})}, {"variable", Value(sgre::resource::PsbArray{})}, {"parameterize", Value(std::monostate{})}});
}

static sgre::resource::PsbDocument Document(sgre::resource::PsbValue timeline) {
	sgre::resource::PsbDocument document;
	document.Header.Version = 4;
	document.Root = Value(sgre::resource::PsbObject{{"id", Value(std::string("motion"))}, {"spec", Value(std::string("win"))}, {"version", Value(3.03)}, {"label", Value(std::string("Synthetic"))}, {"easing", Value(sgre::resource::PsbArray{})}, {"source", Value(sgre::resource::PsbObject{})}, {"object", Value(sgre::resource::PsbObject{{"actor", Value(sgre::resource::PsbObject{{"type", Value(std::int64_t{0})}, {"motion", Value(sgre::resource::PsbObject{{"test", std::move(timeline)}})}})}})}});

	return document;
}

static void EraseField(sgre::resource::PsbValue& value, std::string_view name) {
	auto& object = std::get<sgre::resource::PsbObject>(value.Data);
	for (auto iterator = object.begin(); iterator != object.end(); ++iterator) {
		if (iterator->first == name) {
			object.erase(iterator);

			return;
		}
	}

	throw std::runtime_error("missing synthetic metadata field");
}

static void TestMetadataAndSafety() {
	const auto ordinary = sgre::resource::ReadMotionResource(Document(Frames()));
	const auto& timeline = ordinary.Objects[0].Timelines[0];
	Require(timeline.Kind == sgre::resource::MotionTimelineKind::Frames && timeline.Bounds.Left == -5 && timeline.Bounds.Top == 4 && timeline.Bounds.Right == -10 && timeline.Bounds.Bottom == -8, "ordinary metadata must preserve bounds endpoints without sorting or scaling");
	Require(timeline.Tags.size() == 1 && timeline.Tags[0].Time == 5 && timeline.Tags[0].Type == 1 && std::holds_alternative<std::monostate>(timeline.Stream.Data), "tag commands were lost or aliased to the packed Stream backend");
	const auto divided = Value(sgre::resource::PsbObject{{"type", Value(std::int64_t{1})}, {"division", Value(sgre::resource::PsbArray{Value(0.0), Value(90.0), Value(180.0)})}, {"motionList", Value(sgre::resource::PsbArray{Value(std::string("wrap")), Value(std::string("front")), Value(std::string("back"))})}});
	const auto resource = sgre::resource::ReadMotionResource(Document(divided));
	const auto& division = resource.Objects[0].Timelines[0];
	Require(division.Kind == sgre::resource::MotionTimelineKind::Division && division.Division.size() == 3 && division.MotionList[1] == "front" && division.Layers.empty(), "division metadata fabricated an ordinary hierarchy or lost motion-list order");
	for (const auto name : {"type", "bounds", "tag"}) {
		auto incomplete = Frames();
		EraseField(incomplete, name);
		RequireFailure([&] {
			static_cast<void>(sgre::resource::ReadMotionResource(Document(incomplete)));
		},
		    name);
	}

	auto malformed = divided;
	EraseField(malformed, "motionList");
	RequireFailure([&] {
		static_cast<void>(sgre::resource::ReadMotionResource(Document(malformed)));
	},
	    "motionList");
	const auto unsupported = Value(sgre::resource::PsbObject{{"type", Value(std::int64_t{2})}});
	RequireFailure([&] {
		static_cast<void>(sgre::resource::ReadMotionResource(Document(unsupported)));
	},
	    "unsupported motion timeline type");
}

static sgre::resource::MotionTimeline Division() {
	sgre::resource::MotionTimeline timeline;
	timeline.Kind = sgre::resource::MotionTimelineKind::Division;
	timeline.Division = {0, 90, 180, 270};
	timeline.MotionList = {"wrap", "first", "second", "third"};

	return timeline;
}

static void TestDivisionBoundaries() {
	auto timeline = Division();
	const std::array<float, 10> angles{0, 0.25f, 90, 90.25f, 180, 270, 270.25f, 360, -90, 720.25f};
	const std::array<std::size_t, 10> expected{0, 1, 1, 2, 2, 3, 0, 0, 3, 1};
	for (std::size_t index = 0; index < angles.size(); ++index) {
		Require(sgre::scene::EvaluateMotionDivisionIndex(timeline, angles[index]) == expected[index], "native division uses an open lower and closed upper interval boundary");
	}

	timeline.Division = {180, 90, 270, 0};
	Require(sgre::scene::EvaluateMotionDivisionIndex(timeline, 120) == 2, "division order must not be sorted by the evaluator");
	timeline.Division = {90};
	timeline.MotionList = {"only"};
	Require(sgre::scene::EvaluateMotionDivisionIndex(timeline, -719.5f) == 0, "single division must always select its sole target");
	RequireFailure([&] {
		static_cast<void>(sgre::scene::EvaluateMotionDivisionIndex(timeline, std::numeric_limits<float>::infinity()));
	},
	    "nonfinite");
	timeline.MotionList.clear();
	RequireFailure([&] {
		static_cast<void>(sgre::scene::EvaluateMotionDivisionIndex(timeline, 0));
	},
	    "division/motionList");
}

static sgre::resource::MotionFrame Tag(std::uint32_t type, float time, std::string label) {
	sgre::resource::MotionFrame tag;
	tag.Type = type;
	tag.Time = time;
	tag.Content = Value(sgre::resource::PsbObject{{"label", Value(std::move(label))}});

	return tag;
}

static void TestClockControls() {
	sgre::scene::MotionTimelineClock clock;
	Require(!clock.HasTimeline() && !clock.State().Playing && clock.State().TickCount == 0 && clock.State().EvaluationTime == 0 && clock.State().Speed == 1 && clock.State().TickAssigned && !clock.State().InitialTagDispatchPending, "native initial clock control state differs");
	clock.SetTickCount(42);
	Require(clock.State().TickCount == 0, "unselected native tick assignment must do nothing");
	sgre::resource::MotionTimeline timeline;
	timeline.LastTime = 20;
	timeline.Tags = {Tag(0, 1, "cue"), Tag(1, 5, "cue"), Tag(1, 9, "cue"), Tag(1, 15, "あ")};
	clock.ApplyTimeline(timeline);
	Require(clock.HasTimeline() && clock.State().Playing && clock.State().InitialTagDispatchPending, "timeline control selection must retain pending initial tag dispatch");
	Require(clock.SetTickLabel("cue") && clock.State().TickCount == 5 && !clock.SetTickLabel("missing") && clock.State().TickCount == 5, "label seek must skip non-command tags and use the first exact label");
	Require(clock.SetTickLabel(std::string_view("あ\0suffix", 10)) && clock.State().TickCount == 15, "label seek must preserve UTF-8 bytes and native NUL termination");
	clock.SetTickCount(100);
	Require(clock.State().TickCount == 100 && clock.State().EvaluationTime == 20, "native tick count is unclamped while its evaluation time is clamped");
	clock.SetTickCount(std::bit_cast<float>(std::uint32_t{0x7fc00001}));
	Require(std::bit_cast<std::uint32_t>(clock.State().TickCount) == 0, "SSE tick max must convert NaN to positive zero");
	clock.SetTickCount(-0.0f);
	Require(std::bit_cast<std::uint32_t>(clock.State().TickCount) == 0, "SSE tick max must convert negative zero to positive zero");
	clock.SetTickCount(12);
	clock.Stop();
	Require(!clock.State().Playing && clock.HasTimeline() && clock.State().TickCount == 12, "stop must preserve selection and tick state");
	auto replacement = timeline;
	replacement.LastTime = 6;
	clock.ApplyTimeline(replacement, sgre::scene::MotionTimelineContinuation::Chain);
	Require(clock.State().Playing && clock.State().TickCount == 12 && clock.State().EvaluationTime == 12 && clock.State().LastTime == 6, "chain must preserve both prior tick words until actual update");
	clock.SetSpeed(std::bit_cast<float>(std::uint32_t{0xffc12345}));
	Require(std::bit_cast<std::uint32_t>(clock.State().Speed) == 0xffc12345, "native speed assignment must preserve raw float32 words");
	clock.InvalidateSelection();
	clock.SetTickCount(4);
	Require(!clock.State().Playing && !clock.HasTimeline() && clock.State().TickCount == 12, "selection invalidation must preserve cached clock fields and guard later seeks");
	const auto division = Division();
	RequireFailure([&] {
		clock.ApplyTimeline(division);
	},
	    "ordinary timeline");
}

template <std::size_t Size>
static std::array<std::uint8_t, Size> ReadBytes(std::ifstream& stream) {
	std::array<char, Size> raw;
	Require(static_cast<bool>(stream.read(raw.data(), static_cast<std::streamsize>(Size))), "native motion metadata/control fixture is truncated");
	std::array<std::uint8_t, Size> bytes;
	for (std::size_t index = 0; index < Size; ++index) {
		bytes[index] = static_cast<std::uint8_t>(static_cast<unsigned char>(raw[index]));
	}

	return bytes;
}

static std::uint32_t Word(std::span<const std::uint8_t> bytes, std::size_t offset) {
	Require(offset <= bytes.size() && bytes.size() - offset >= 4, "native motion fixture word exceeds its row");

	return static_cast<std::uint32_t>(bytes[offset]) | (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) | (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) | (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
}

static std::uint32_t FixtureCount(std::ifstream& stream, std::string_view tag) {
	const auto header = ReadBytes<8>(stream);
	Require(tag.size() == 4, "native motion fixture expected tag length differs");
	for (std::size_t index = 0; index < tag.size(); ++index) {
		Require(header[index] == static_cast<std::uint8_t>(static_cast<unsigned char>(tag[index])), "native motion fixture has an invalid tag");
	}
	const auto count = Word(header, 4);
	Require(count > 0 && count <= 4096, "native motion fixture exceeds its replay count limit");

	return count;
}

static void ReplayDivision(std::string_view path) {
	std::ifstream stream(std::string(path), std::ios::binary);
	const auto count = FixtureCount(stream, "MDV1");
	for (std::uint32_t case_index = 0; case_index < count; ++case_index) {
		const auto row = ReadBytes<56>(stream);
		const auto length = Word(row, 0);
		Require(length > 0 && length <= 8, "native division fixture has an invalid length");
		sgre::resource::MotionTimeline timeline;
		timeline.Kind = sgre::resource::MotionTimelineKind::Division;
		for (std::uint32_t index = 0; index < length; ++index) {
			timeline.Division.push_back(std::bit_cast<float>(Word(row, 16 + index * 4)));
			timeline.MotionList.push_back("target");
		}

		const auto angle = std::bit_cast<float>(Word(row, 4)) + std::bit_cast<float>(Word(row, 8));
		Require(sgre::scene::EvaluateMotionDivisionIndex(timeline, angle) == Word(row, 48), "portable division selection differs from original instructions");
	}

	Require(stream.peek() == std::char_traits<char>::eof(), "native division fixture has trailing bytes");
}

static void ReplayTicks(std::string_view path) {
	std::ifstream stream(std::string(path), std::ios::binary);
	const auto count = FixtureCount(stream, "MTK1");
	for (std::uint32_t case_index = 0; case_index < count; ++case_index) {
		const auto row = ReadBytes<40>(stream);
		if (Word(row, 0) == 0) {
			Require(Word(row, 12) == Word(row, 28) && Word(row, 16) == Word(row, 32), "native unselected tick proof changed cached fields");
			continue;
		}

		sgre::resource::MotionTimeline timeline;
		timeline.LastTime = std::bit_cast<float>(Word(row, 8));
		sgre::scene::MotionTimelineClock clock;
		clock.ApplyTimeline(timeline);
		clock.SetTickCount(std::bit_cast<float>(Word(row, 4)));
		Require(std::bit_cast<std::uint32_t>(clock.State().TickCount) == Word(row, 28) && std::bit_cast<std::uint32_t>(clock.State().EvaluationTime) == Word(row, 32) && clock.State().TickAssigned && clock.State().InitialTagDispatchPending, "portable tick assignment differs from original SSE instructions");
	}

	Require(stream.peek() == std::char_traits<char>::eof(), "native tick fixture has trailing bytes");
}

static void ReplayLabels(std::string_view path) {
	const std::array<std::string_view, 4> labels{"cue0", "cue1", "あ", "missing"};
	std::ifstream stream(std::string(path), std::ios::binary);
	const auto count = FixtureCount(stream, "MTL1");
	for (std::uint32_t case_index = 0; case_index < count; ++case_index) {
		const auto row = ReadBytes<24>(stream);
		Require(row[0] <= 1 && row[1] <= 3 && row[2] > 0 && row[2] <= 6, "native label fixture has invalid selectors");
		sgre::resource::MotionTimeline timeline;
		timeline.LastTime = 17;
		for (std::size_t index = 0; index < row[2]; ++index) {
			Require(row[4 + index * 2] <= 3 && row[5 + index * 2] <= 2, "native label fixture has invalid tag fields");
			timeline.Tags.push_back(Tag(row[4 + index * 2], static_cast<float>(index * 5), std::string(labels[row[5 + index * 2]])));
		}

		sgre::scene::MotionTimelineClock clock;
		clock.ApplyTimeline(timeline);
		clock.SetTickCount(2.5f);
		if (row[0] == 0) {
			clock.InvalidateSelection();
		}

		static_cast<void>(clock.SetTickLabel(labels[row[1]]));
		Require(std::bit_cast<std::uint32_t>(clock.State().TickCount) == Word(row, 16) && std::bit_cast<std::uint32_t>(clock.State().EvaluationTime) == Word(row, 20), "portable label seek differs from original tag/PSB instructions");
	}

	Require(stream.peek() == std::char_traits<char>::eof(), "native label fixture has trailing bytes");
}

int main(int argc, char** argv) {
	try {
		Require(argc == 1 || argc == 4, "motion metadata/control test accepts division, tick and label native fixture paths together");
		TestMetadataAndSafety();
		TestDivisionBoundaries();
		TestClockControls();
		if (argc == 4) {
			ReplayDivision(argv[1]);
			ReplayTicks(argv[2]);
			ReplayLabels(argv[3]);
		}

		std::cout << "motion timeline metadata, division and selected clock controls passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
