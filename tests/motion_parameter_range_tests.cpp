#include "sgre/scene/motion_parameter_range.hpp"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
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

static std::uint32_t Word(std::istream& input) {
	std::uint32_t result = 0;
	for (std::uint32_t shift = 0; shift < 32; shift += 8) {
		const auto value = input.get();
		Require(value != std::char_traits<char>::eof(), "short native parameter fixture");
		result |= static_cast<std::uint32_t>(static_cast<std::uint8_t>(value)) << shift;
	}

	return result;
}

template <typename Operation>
static void RequireFailure(Operation operation) {
	try {
		operation();
	} catch (const std::runtime_error&) {
		return;
	}

	throw std::runtime_error("unsupported motion parameter input was accepted");
}

static void MetadataCases() {
	sgre::resource::MotionTimeline timeline;
	Require(sgre::scene::ReadMotionParameterRanges(timeline).empty(), "native null parameter list was not empty");
	sgre::resource::PsbObject definition = {{"id", Atom(std::string("value\0retained", 14))}, {"discretization", Atom(std::int64_t{256})}, {"rangeBegin", Atom(std::int64_t{-100})}, {"rangeEnd", Atom(100.0)}, {"division", Atom(200.0)}};
	timeline.Parameters = Atom(sgre::resource::PsbArray{Atom(definition), Atom(definition)});
	const auto ranges = sgre::scene::ReadMotionParameterRanges(timeline);
	Require(ranges.size() == 2 && ranges[0].Identifier == "value" && ranges[0].Discrete && ranges[0].Begin == -100 && ranges[0].End == 100 && ranges[0].Division == 200, "actual ordered parameter definition fields differ");
	const auto* raw = std::get_if<sgre::resource::PsbArray>(&timeline.Parameters.Data);
	Require(ranges[0].Identifier.data() == std::get<std::string>((*raw)[0].Find("id")->Data).data(), "parameter ID lost its actual immutable metadata borrow");
	Require(sgre::scene::EvaluateMotionParameterPosition(ranges[0], 0) == 100, "native missing-variable value does not produce the actual range midpoint");
	RequireFailure([&] {
		static_cast<void>(sgre::scene::ReadMotionParameterRanges(timeline, 1));
	});
	definition.erase(definition.begin());
	timeline.Parameters = Atom(sgre::resource::PsbArray{Atom(definition)});
	RequireFailure([&] {
		static_cast<void>(sgre::scene::ReadMotionParameterRanges(timeline));
	});
	timeline.Parameters = Atom(sgre::resource::PsbObject{});
	RequireFailure([&] {
		static_cast<void>(sgre::scene::ReadMotionParameterRanges(timeline));
	});
	RequireFailure([&] {
		static_cast<void>(sgre::scene::EvaluateMotionParameterPosition(ranges[0], std::numeric_limits<float>::infinity()));
	});
	sgre::scene::MotionParameterRange overflow;
	overflow.End = std::numeric_limits<float>::max();
	overflow.Division = std::numeric_limits<float>::max();
	RequireFailure([&] {
		static_cast<void>(sgre::scene::EvaluateMotionParameterPosition(overflow, overflow.End));
	});
}

static void Replay(const char* path) {
	std::ifstream input(path, std::ios::binary);
	Require(input.is_open() && Word(input) == 0x3149504d, "wrong native parameter fixture");
	const auto count = Word(input);
	Require(count > 0 && count <= 1024, "native parameter fixture count exceeds its bound");
	for (std::uint32_t index = 0; index < count; ++index) {
		Require(Word(input) == index, "native parameter case order differs");
		sgre::scene::MotionParameterRange range;
		range.Discrete = Word(input) != 0;
		range.Begin = std::bit_cast<float>(Word(input));
		range.End = std::bit_cast<float>(Word(input));
		range.Division = std::bit_cast<float>(Word(input));
		const bool present = Word(input) != 0;
		const auto variable = std::bit_cast<float>(Word(input));
		Require(present || variable == 0, "missing variable fixture did not execute the native complete miss");
		Require((Word(input) & 255) == static_cast<std::uint32_t>(range.Discrete), "native discretization flag differs");
		Require(Word(input) == std::bit_cast<std::uint32_t>(range.Begin) && Word(input) == std::bit_cast<std::uint32_t>(range.End) && Word(input) == std::bit_cast<std::uint32_t>(range.Division), "native prepared range float words differ");
		const auto expected = Word(input);
		Require(Word(input) == 0, "native initial parameter control word differs");
		const auto position = sgre::scene::EvaluateMotionParameterPosition(range, variable);
		Require(std::bit_cast<std::uint32_t>(position) == expected, "native parameter normalization float word differs");
	}
	Require(input.get() == std::char_traits<char>::eof(), "native parameter fixture has trailing data");
	std::cout << "actual native parameter initialization cases=" << count << " PASS\n";
}

int main(int argc, char** argv) {
	try {
		Require(argc == 2, "motion_parameter_range_tests NATIVE_FIXTURE");
		MetadataCases();
		Replay(argv[1]);

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
