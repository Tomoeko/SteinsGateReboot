#include "sgre/scene/motion_particle_definition.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <span>
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

template <class Function>
static void RequireFailure(Function function, std::string_view message) {
	try {
		function();
	} catch (const std::runtime_error&) {
		return;
	}

	throw std::runtime_error(std::string(message));
}

static sgre::resource::PsbValue Integer(std::int64_t number) {
	sgre::resource::PsbValue value;
	value.Data = number;

	return value;
}

static sgre::resource::PsbValue Real(double number) {
	sgre::resource::PsbValue value;
	value.Data = number;

	return value;
}

static sgre::resource::PsbValue Text(std::string text) {
	sgre::resource::PsbValue value;
	value.Data = std::move(text);

	return value;
}

static sgre::resource::PsbValue Object(sgre::resource::PsbObject fields) {
	sgre::resource::PsbValue value;
	value.Data = std::move(fields);

	return value;
}

static sgre::resource::PsbValue Array(sgre::resource::PsbArray fields) {
	sgre::resource::PsbValue value;
	value.Data = std::move(fields);

	return value;
}

static sgre::resource::MotionLayer Layer(const std::array<std::int32_t, 9>& values, float acceleration, bool include_opacity = true) {
	sgre::resource::MotionLayer layer;
	layer.Type = 4;
	static constexpr std::array<std::string_view, 9> names{"particle", "particleMaxNum", "particleInheritAngle", "particleDeleteOutsideScreen", "particleTriVolume", "particleInheritOpacity", "particleInheritVelocity", "particleFlyDirection", "particleApplyZoomToVelocity"};
	for (std::size_t index = 0; index < names.size(); ++index) {
		if (index != 5 || include_opacity) {
			layer.Properties.emplace_back(names[index], Integer(values[index]));
		}
	}

	layer.Properties.emplace_back("particleAccelRatio", Real(acceleration));
	layer.Properties.emplace_back("particleMotionList", Array({}));

	return layer;
}

static sgre::resource::PsbValue Screen(const std::array<float, 4>& dimensions) {
	return Object({{"screenSize", Object({{"width", Real(dimensions[0])}, {"height", Real(dimensions[1])}, {"originX", Real(dimensions[2])}, {"originY", Real(dimensions[3])}})}});
}

static sgre::resource::MotionFrame Frame(bool hidden, bool present, std::int32_t mask, const std::array<float, 9>& numbers, std::int32_t trigger) {
	sgre::resource::MotionFrame frame;
	frame.Type = hidden ? 0 : 2;
	frame.Content = Object({{"mask", Integer(present ? 0x100000 : 0)}, {"prt", Object({{"mask", Integer(mask)}, {"trigger", Integer(trigger)}, {"fmin", Real(numbers[0])}, {"fmax", Real(numbers[1])}, {"vmin", Real(numbers[2])}, {"vmax", Real(numbers[3])}, {"amin", Real(numbers[4])}, {"amax", Real(numbers[5])}, {"zmin", Real(numbers[6])}, {"zmax", Real(numbers[7])}, {"range", Real(numbers[8])}})}});

	return frame;
}

static std::array<std::uint32_t, 10> ControlWords(const sgre::scene::MotionParticleFrameControl& control) {
	return {std::bit_cast<std::uint32_t>(control.Trigger), std::bit_cast<std::uint32_t>(control.FrequencyMin), std::bit_cast<std::uint32_t>(control.FrequencyMax), std::bit_cast<std::uint32_t>(control.VelocityMin), std::bit_cast<std::uint32_t>(control.VelocityMax), std::bit_cast<std::uint32_t>(control.AngleMin), std::bit_cast<std::uint32_t>(control.AngleMax), std::bit_cast<std::uint32_t>(control.ZoomMin), std::bit_cast<std::uint32_t>(control.ZoomMax), std::bit_cast<std::uint32_t>(control.Range)};
}

static void TestControlsAndOwnedTargets() {
	auto layer = Layer({2, 32, 1, 0, 1, 0, 2, 1, 2}, 0.75f, false);
	auto targets = Array({Array({Text(std::string("character\0unused", 16)), Text("motion"), Integer(99)}), Array({Text(""), Text("other")})});
	layer.Properties.back().second = targets;
	const auto definition = sgre::scene::ReadMotionParticleDefinition(layer, {});
	layer.Properties.clear();
	targets = {};
	Require(definition.InheritOpacity == 1 && definition.MotionList.size() == 2 && definition.MotionList[0].Character == "character" && definition.MotionList[0].Timeline == "motion" && definition.MotionList[1].Character.empty() && !definition.Screen && !definition.ExpandedScreen, "particle definition must own native target names and preserve optional-opacity and no-screen branches");

	layer.Type = 4;
	std::array<sgre::scene::MotionParticleFrameControl, 2> slots{};
	Require(ControlWords(slots[0]) == std::array<std::uint32_t, 10>{}, "particle constructor control must have ten zero words");
	const std::array<float, 9> numbers{20, 10, -4, 6, -90, 180, 0.5f, 2, 17};
	auto frame = Frame(false, true, 0x13f, numbers, 1);
	sgre::scene::ApplyMotionParticleFrameControl(sgre::scene::PrepareMotionParticleFrameControl(layer, frame), slots[0]);
	Require(slots[0].Trigger == 1 && slots[0].FrequencyMin == 20 && slots[0].FrequencyMax == 10 && slots[0].VelocityMin == -4 && slots[0].AngleMax == 180 && slots[0].ZoomMin == 0.5f && slots[0].Range == 17, "prt mask low byte must copy raw ranges and numeric units without normalization");
	const auto previous = slots[0];
	frame.Type = 0;
	frame.Content = {};
	sgre::scene::ApplyMotionParticleFrameControl(sgre::scene::PrepareMotionParticleFrameControl(layer, frame), slots[0]);
	Require(slots[0] == previous && ControlWords(slots[1]) == std::array<std::uint32_t, 10>{}, "hidden frame must retain its actual physical control independently of the second slot");
	frame = Frame(false, false, 63, numbers, 0);
	sgre::scene::ApplyMotionParticleFrameControl(sgre::scene::PrepareMotionParticleFrameControl(layer, frame), slots[0]);
	Require(slots[0] == previous, "absent outer particle mask must retain reused control");
	frame = Frame(false, true, 0, numbers, 0);
	sgre::scene::ApplyMotionParticleFrameControl(sgre::scene::PrepareMotionParticleFrameControl(layer, frame), slots[0]);
	Require(slots[0].Trigger == 0 && slots[0].FrequencyMin == 10 && slots[0].FrequencyMax == 10 && slots[0].ZoomMin == 1 && slots[0].ZoomMax == 1 && slots[0].Range == 0 && slots[0].VelocityMin == 0 && slots[0].AngleMin == 0, "present empty prt must reset native defaults instead of retaining constructor or old slot values");
}

static void TestBoundsAndFailures() {
	auto layer = Layer({0, -1, 256, 1, -256, -3, 2, -1, 0}, 2);
	const auto definition = sgre::scene::ReadMotionParticleDefinition(layer, Screen({100, 60, 3, -4}));
	Require(definition.InheritAngle && definition.TriangularVolume && definition.InheritOpacity == -3 && definition.MaximumParticles == -1, "native nonzero integer flags and signed mode words must survive conversion");
	Require(definition.Screen == sgre::scene::MotionParticleScreenRectangle{-53, -26, 47, 34} && definition.ExpandedScreen == sgre::scene::MotionParticleScreenRectangle{-58, -29, 52, 37}, "outside-screen owner must retain exact native screen bounds and five-percent expansion");
	RequireFailure([&] {
		static_cast<void>(sgre::scene::ReadMotionParticleDefinition(layer, {}));
	},
	    "outside-screen definition accepted missing actual document screen");
	layer.Properties.erase(layer.Properties.begin());
	RequireFailure([&] {
		static_cast<void>(sgre::scene::ReadMotionParticleDefinition(layer, Screen({100, 60, 3, -4})));
	},
	    "particle definition accepted a missing required mode");
	layer = Layer({}, 1);
	layer.Properties.back().second = Array({Array({Text("one")})});
	RequireFailure([&] {
		static_cast<void>(sgre::scene::ReadMotionParticleDefinition(layer, {}));
	},
	    "particle definition accepted a target without its actual timeline entry");
	layer.Type = 3;
	RequireFailure([&] {
		static_cast<void>(sgre::scene::PrepareMotionParticleFrameControl(layer, Frame(false, true, 0, {}, 0)));
	},
	    "particle control accepted a foreign layer kind");
	layer.Type = 4;
	auto wrong_key = Frame(false, true, 0, {}, 0);
	std::get<sgre::resource::PsbObject>(wrong_key.Content.Data)[1].first = "particle";
	RequireFailure([&] {
		static_cast<void>(sgre::scene::PrepareMotionParticleFrameControl(layer, wrong_key));
	},
	    "particle control accepted an invented frame property alias");
	auto nonfinite = Frame(false, true, 2, {std::numeric_limits<float>::infinity()}, 0);
	RequireFailure([&] {
		static_cast<void>(sgre::scene::PrepareMotionParticleFrameControl(layer, nonfinite));
	},
	    "particle control accepted a nonfinite physical input");
}

struct NativeReader {
	std::span<const std::uint8_t> Bytes;
	std::size_t Offset = 0;

	std::uint32_t Word() {
		Require(Offset <= Bytes.size() && 4 <= Bytes.size() - Offset, "particle native vector is truncated");
		std::uint32_t value = 0;
		for (std::size_t index = 0; index < 4; ++index) {
			value |= static_cast<std::uint32_t>(Bytes[Offset + index]) << (index * 8);
		}

		Offset += 4;

		return value;
	}

	float Number() {
		return std::bit_cast<float>(Word());
	}
};

static void TestNative(const char* path) {
	std::ifstream stream(path, std::ios::binary | std::ios::ate);
	Require(stream.good(), "particle native vector file is unavailable");
	const auto length = stream.tellg();
	Require(length > 12 && length < 1024 * 1024, "particle native vector length exceeds its bound");
	stream.seekg(0);
	const std::vector<std::uint8_t> bytes(std::istreambuf_iterator<char>(stream), {});
	NativeReader reader{bytes};
	Require(reader.Word() == 0x3144504d, "particle native vector magic differs");
	const auto cases = reader.Word();
	const auto steps = reader.Word();
	Require(cases == 32 && steps == 16, "particle native vector dimensions differ");
	for (std::uint32_t case_index = 0; case_index < cases; ++case_index) {
		std::array<std::int32_t, 9> values{};
		for (auto& value : values) {
			value = std::bit_cast<std::int32_t>(reader.Word());
		}

		const auto acceleration = reader.Number();
		std::array<float, 4> dimensions{};
		for (auto& value : dimensions) {
			value = reader.Number();
		}

		std::array<std::uint32_t, 10> expected_static{};
		for (auto& value : expected_static) {
			value = reader.Word();
		}

		std::array<std::uint32_t, 8> expected_bounds{};
		for (auto& value : expected_bounds) {
			value = reader.Word();
		}

		const auto layer = Layer(values, acceleration, case_index % 3 != 0);
		const auto definition = sgre::scene::ReadMotionParticleDefinition(layer, Screen(dimensions));
		expected_static[2] = expected_static[2] != 0;
		expected_static[3] = expected_static[3] != 0;
		expected_static[4] = expected_static[4] != 0;
		const std::array<std::uint32_t, 10> actual_static{std::bit_cast<std::uint32_t>(definition.Particle), std::bit_cast<std::uint32_t>(definition.MaximumParticles), definition.InheritAngle, definition.DeleteOutsideScreen, definition.TriangularVolume, std::bit_cast<std::uint32_t>(definition.InheritOpacity), std::bit_cast<std::uint32_t>(definition.InheritVelocity), std::bit_cast<std::uint32_t>(definition.FlyDirection), std::bit_cast<std::uint32_t>(definition.ApplyZoomToVelocity), std::bit_cast<std::uint32_t>(definition.AccelerationRatio)};
		Require(actual_static == expected_static, "particle definition differs from original constructor words");
		if (definition.DeleteOutsideScreen) {
			const auto& rectangle = *definition.Screen;
			const auto& expanded = *definition.ExpandedScreen;
			const std::array<std::uint32_t, 8> actual_bounds{std::bit_cast<std::uint32_t>(rectangle.Left), std::bit_cast<std::uint32_t>(rectangle.Top), std::bit_cast<std::uint32_t>(rectangle.Right), std::bit_cast<std::uint32_t>(rectangle.Bottom), std::bit_cast<std::uint32_t>(expanded.Left), std::bit_cast<std::uint32_t>(expanded.Top), std::bit_cast<std::uint32_t>(expanded.Right), std::bit_cast<std::uint32_t>(expanded.Bottom)};
			Require(actual_bounds == expected_bounds, "particle screen rectangle differs from original arithmetic words");
		} else {
			Require(!definition.Screen && !definition.ExpandedScreen, "particle constructor read screen metadata outside its actual branch");
		}

		std::array<sgre::scene::MotionParticleFrameControl, 2> slots{};
		for (std::uint32_t step = 0; step < steps; ++step) {
			const auto slot = reader.Word();
			const auto hidden = reader.Word() != 0;
			const auto present = reader.Word() != 0;
			const auto mask = std::bit_cast<std::int32_t>(reader.Word());
			const auto trigger = std::bit_cast<std::int32_t>(reader.Word());
			std::array<float, 9> numbers{};
			for (auto& number : numbers) {
				number = reader.Number();
			}

			Require(slot < slots.size(), "particle native slot index exceeds its pair");
			sgre::scene::ApplyMotionParticleFrameControl(sgre::scene::PrepareMotionParticleFrameControl(layer, Frame(hidden, present, mask, numbers, trigger)), slots[slot]);
			for (const auto& current : slots) {
				for (const auto actual : ControlWords(current)) {
					Require(actual == reader.Word(), "particle physical control differs from original decoder words");
				}
			}
		}
	}

	Require(reader.Offset == bytes.size(), "particle native vector has trailing bytes");
	std::cout << "particle native 32 constructors/512 decoded calls match\n";
}

int main(int argc, char** argv) {
	try {
		TestControlsAndOwnedTargets();
		TestBoundsAndFailures();
		if (argc == 2) {
			TestNative(argv[1]);
		} else {
			Require(argc == 1, "particle definition test accepts only an optional native-vector path");
		}

		std::cout << "motion particle definition tests passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
