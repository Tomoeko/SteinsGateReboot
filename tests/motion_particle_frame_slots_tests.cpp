#include "sgre/scene/motion_particle_frame_slots.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <fstream>
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

template <class Function>
static void RequireFailure(Function function, std::string_view expected) {
	try {
		function();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(expected) != std::string_view::npos, error.what());

		return;
	}

	throw std::runtime_error("particle slots accepted an unsupported owner or phase");
}

template <class Value>
static sgre::resource::PsbValue Atom(Value value) {
	sgre::resource::PsbValue result;
	result.Data = std::move(value);

	return result;
}

static sgre::resource::MotionFrame Frame(float time, std::uint32_t type, bool present, std::int32_t trigger, const std::array<float, 9>& numbers, std::uint32_t interval = 0) {
	sgre::resource::MotionFrame frame;
	frame.Time = time;
	frame.Type = type;
	sgre::resource::PsbObject particle{{"mask", Atom(std::int64_t{63})}, {"trigger", Atom(static_cast<std::int64_t>(trigger))}};
	static constexpr std::array<std::string_view, 9> names{"fmin", "fmax", "vmin", "vmax", "amin", "amax", "zmin", "zmax", "range"};
	for (std::size_t index = 0; index < names.size(); ++index) {
		particle.emplace_back(names[index], Atom(static_cast<double>(numbers[index])));
	}

	const auto mask = (present ? 0x100000u : 0u) | (interval != 0 ? 0x04000000u : 0u);
	frame.Content = Atom(sgre::resource::PsbObject{{"mask", Atom(static_cast<std::int64_t>(mask))}, {"ti", Atom(static_cast<std::int64_t>(interval))}, {"prt", Atom(std::move(particle))}});

	return frame;
}

static sgre::resource::MotionLayer Layer() {
	sgre::resource::MotionLayer layer;
	layer.Type = 4;
	layer.Label = "emitter";
	layer.TransformOrder = {0, 3, 2, 1};

	return layer;
}

static std::array<std::uint32_t, 9> NumericWords(const sgre::scene::MotionParticleNumericState& numeric) {
	return {std::bit_cast<std::uint32_t>(numeric.FrequencyMin), std::bit_cast<std::uint32_t>(numeric.FrequencyMax), std::bit_cast<std::uint32_t>(numeric.VelocityMin), std::bit_cast<std::uint32_t>(numeric.VelocityMax), std::bit_cast<std::uint32_t>(numeric.AngleMin), std::bit_cast<std::uint32_t>(numeric.AngleMax), std::bit_cast<std::uint32_t>(numeric.ZoomMin), std::bit_cast<std::uint32_t>(numeric.ZoomMax), std::bit_cast<std::uint32_t>(numeric.Range)};
}

static void TestRetainedSlotsAndNumeric() {
	const sgre::resource::MotionResource resource;
	auto layer = Layer();
	layer.Frames = {Frame(0, 3, true, 1, {10, 20, -2, 2, 350, -350, 1, 2, 4}), Frame(8, 2, true, 2, {30, 40, -6, 6, 10, -10, 3, 4, 8}), Frame(16, 0, false, 0, {}), Frame(24, 2, false, 0, {}), Frame(32, 0, false, 0, {})};
	const auto prepared = sgre::scene::PrepareMotionLayer(layer, resource);
	sgre::scene::MotionParticleFrameSlots slots(layer, prepared);
	RequireFailure([&] {
		static_cast<void>(slots.Numeric());
	},
	    "actual current evaluation");
	RequireFailure([&] {
		static_cast<void>(slots.Control(0));
	},
	    "actual decoded physical slot");
	slots.PrepareFrame(0, 0);
	slots.PrepareFrame(1, 1);
	Require(slots.Evaluate(resource, 0, 0, false, true), "initial dirty particle frame must perform actual numeric evaluation");
	Require(slots.Numeric().FrequencyMin == 10 && slots.Numeric().ZoomMin == 1 && slots.Control(0).Trigger == 1, "initial static-copy numeric and current trigger differ");
	Require(slots.Evaluate(resource, 4, 0, false, false), "particle tween must use changed real generic ratio");
	Require(slots.Numeric().FrequencyMin == 20 && slots.Numeric().ZoomMin == 2 && slots.Numeric().AngleMin == 180 && slots.Numeric().AngleMax == -180 && slots.Control(0).Trigger == 1 && slots.Control(1).Trigger == 2, "particle angles/ranges must use weighted raw scalars while trigger remains current physical metadata");
	Require(!slots.Evaluate(resource, 4, 0, false, false), "unchanged particle ratio must retain numeric output");
	const auto retained = slots.Numeric();
	slots.PrepareFrame(0, 2);
	slots.PrepareFrame(1, 3);
	Require(slots.Control(0).Trigger == 1 && slots.Control(1).Trigger == 2, "hidden and absent controls must retain each actual reused physical slot");
	Require(slots.Evaluate(resource, 16, 0, true, true) && slots.Numeric() == retained, "hidden evaluation must retain previous nonzero numeric state");
	slots.PrepareFrame(1, 3);
	slots.PrepareFrame(0, 4);
	Require(slots.Evaluate(resource, 24, 1, false, true) && slots.Numeric().FrequencyMin == 30 && slots.Control(1).Trigger == 2 && slots.EvaluatedFrameIndex() == 3 && slots.EvaluatedPosition() == 24, "reactivated absent control must publish retained physical values at actual new cursor");
	slots.PrepareFrame(0, 1);
	RequireFailure([&] {
		static_cast<void>(slots.Evaluate(resource, 24, 1, true, true));
	},
	    "adjacent decoded physical slots");
	RequireFailure([&] {
		slots.PrepareFrame(2, 0);
	},
	    "slot/frame");
	RequireFailure([&] {
		slots.PrepareFrame(0, layer.Frames.size());
	},
	    "slot/frame");
}

class Reader {
public:
	explicit Reader(std::istream& stream)
	    : m_Stream(stream) {
	}

	std::uint32_t Word() {
		std::uint32_t value = 0;
		for (std::uint32_t shift = 0; shift < 32; shift += 8) {
			const auto byte = m_Stream.get();
			Require(byte != std::char_traits<char>::eof(), "truncated particle numeric native vector");
			value |= static_cast<std::uint32_t>(static_cast<std::uint8_t>(byte)) << shift;
		}

		return value;
	}

	float Number() {
		return std::bit_cast<float>(Word());
	}

private:
	std::istream& m_Stream;
};

static void TestNative(const char* path) {
	std::ifstream stream(path, std::ios::binary | std::ios::ate);
	Require(stream.good() && stream.tellg() > 12 && stream.tellg() < 1024 * 1024, "particle numeric native input is unavailable or exceeds its bound");
	stream.seekg(0);
	Reader reader(stream);
	Require(reader.Word() == 0x314e504d, "particle numeric vector magic differs");
	const auto cases = reader.Word();
	const auto steps = reader.Word();
	Require(cases == 64 && steps == 12, "particle numeric vector dimensions differ");
	const sgre::resource::MotionResource resource;
	for (std::uint32_t case_index = 0; case_index < cases; ++case_index) {
		const auto slot = reader.Word();
		const auto hidden = reader.Word() != 0;
		const auto tween = reader.Word() != 0;
		const auto hidden_next = reader.Word() != 0;
		const auto interval = reader.Word();
		const auto present = reader.Word();
		const auto current_trigger = std::bit_cast<std::int32_t>(reader.Word());
		const auto following_trigger = std::bit_cast<std::int32_t>(reader.Word());
		std::array<float, 9> start{};
		std::array<float, 9> end{};
		for (auto& value : start) {
			value = reader.Number();
		}

		for (auto& value : end) {
			value = reader.Number();
		}

		auto layer = Layer();
		layer.Frames = {Frame(0, hidden ? 0 : tween ? 3
		                                            : 2,
		                    (present & 1) != 0, current_trigger, start, interval),
		    Frame(8, hidden_next ? 0 : 2, (present & 2) != 0, following_trigger, end), Frame(16, 0, false, 0, {})};
		const auto prepared = sgre::scene::PrepareMotionLayer(layer, resource);
		sgre::scene::MotionParticleFrameSlots slots(layer, prepared);
		Require(slot < 2, "particle numeric native physical slot exceeds two");
		slots.PrepareFrame(slot, 0);
		slots.PrepareFrame(1 - slot, 1);
		for (std::uint32_t step = 0; step < steps; ++step) {
			const auto position = reader.Number();
			const auto force = reader.Word() != 0;
			const auto dirty = reader.Word() != 0;
			const auto changed = reader.Word() != 0;
			Require(slots.Evaluate(resource, position, slot, force, dirty) == changed, "particle numeric suppression differs from original50bc40");
			for (const auto actual : NumericWords(slots.Numeric())) {
				Require(actual == reader.Word(), "particle numeric word differs from original50bc40");
			}
		}
	}

	Require(stream.peek() == std::char_traits<char>::eof(), "particle numeric vector has trailing bytes");
	std::cout << "particle numeric native64 cases/768 phases match\n";
}

int main(int argc, char** argv) {
	try {
		TestRetainedSlotsAndNumeric();
		if (argc == 2) {
			TestNative(argv[1]);
		} else {
			Require(argc == 1, "particle numeric test accepts only an optional native-vector path");
		}

		std::cout << "motion particle frame slots tests passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
