#include "sgre/scene/motion_layer_composition.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

static void Require(bool condition, std::string_view context) {
	if (!condition) {
		throw std::runtime_error(std::string(context));
	}
}

template <typename Operation>
static void RequireFailure(Operation operation) {
	try {
		operation();
	} catch (const std::runtime_error&) {
		return;
	}

	throw std::runtime_error("motion layer composition accepted unsupported state");
}

static void TestVisibilityAndAncestors() {
	sgre::scene::PreparedMotionTimeline timeline;
	timeline.Layers.resize(3);
	timeline.Parents = {std::nullopt, 0, 1};
	sgre::scene::MotionRootScene root;
	root.SetVisible(true);
	sgre::scene::MotionLayerComposition composition(timeline, root, false);
	Require(composition.Layers().empty(), "selected numeric output must remain unpublished before actual composition");
	std::array<sgre::scene::MotionLayerSample, 3> local;
	local[0].Hidden = true;
	local[1].Hidden = false;
	local[2].Hidden = false;
	composition.Compose(local, true);
	Require(composition.Layers()[0].Visible && !composition.Layers()[0].Active && composition.Layers()[1].Visible && composition.Layers()[1].Active, "frame absence must preserve inherited visibility for an active child");
	const auto* stable = composition.Layers().data();
	sgre::scene::MotionLayerOverrides overrides;
	overrides.Visible = false;
	overrides.Coordinate = {100, 100, 100};
	composition.SetOverrides(0, overrides);
	composition.Compose(local, false);
	Require(!composition.Layers()[0].Visible && !composition.Layers()[1].Visible && !composition.Layers()[2].Active, "script visibility must propagate independently of absent-frame overrides");
	Require(composition.Layers()[0].Hierarchy.Attributes.Coordinate == root.Frame().Hierarchy.Attributes.Coordinate, "hidden-frame numeric pose must copy its selected parent and ignore numeric overrides");
	Require(composition.Layers().data() == stable, "selected composition must reuse its startup-owned output storage");
	RequireFailure([&] {
		composition.Compose(std::span<const sgre::scene::MotionLayerSample>(local).first(2), false);
	});
	RequireFailure([&] {
		auto invalid = overrides;
		invalid.ZoomX = std::numeric_limits<float>::infinity();
		composition.SetOverrides(0, invalid);
	});

	auto skipped = timeline;
	skipped.Layers[0].InheritMask = 0x400000;
	sgre::scene::MotionLayerComposition skip_composition(skipped, root, false);
	skip_composition.SetOverrides(0, overrides);
	skip_composition.Compose(local, true);
	Require(!skip_composition.Layers()[0].Visible && skip_composition.Layers()[1].Visible && !skip_composition.Layers()[1].EffectiveParent.has_value(), "ancestor mask must bypass that parent for visibility and numeric composition");
	RequireFailure([&] {
		auto invalid = timeline;
		invalid.Parents[0] = 1;
		sgre::scene::MotionLayerComposition rejected(invalid, root, false);
	});
	RequireFailure([&] {
		auto invalid = timeline;
		invalid.Layers[0].Type = 4;
		sgre::scene::MotionLayerComposition rejected(invalid, root, false);
	});
}

static void TestBoundedDepthAndFailedPublication() {
	sgre::scene::PreparedMotionTimeline deep;
	deep.Layers.resize(130);
	deep.Parents.resize(deep.Layers.size());
	for (std::size_t index = 1; index < deep.Parents.size(); ++index) {
		deep.Parents[index] = static_cast<std::uint32_t>(index - 1);
		deep.Layers[index - 1].InheritMask = 0x400000;
	}
	sgre::scene::MotionRootScene root;
	RequireFailure([&] {
		sgre::scene::MotionLayerComposition rejected(deep, root, false);
	});

	sgre::scene::PreparedMotionTimeline timeline;
	timeline.Layers.resize(1);
	timeline.Parents.resize(1);
	sgre::scene::MotionLayerComposition composition(timeline, root, false);
	const std::array<sgre::scene::MotionLayerSample, 1> samples{};
	composition.Compose(samples, true);
	Require(composition.Layers().size() == 1, "a complete valid numeric frame must publish its owner-held output");
	root.SetLeft(std::numeric_limits<float>::infinity());
	RequireFailure([&] {
		composition.Compose(samples, false);
	});
	Require(composition.Layers().empty(), "a failed mutating composition must not expose stale or partial output as a current frame");
	root.SetLeft(0);
	composition.Compose(samples, true);
	Require(composition.Layers().size() == 1, "a later valid composition must restore publication without replacing its owner storage");
}

static std::uint8_t ReadByte(std::istream& input) {
	const auto value = input.get();
	if (value == std::char_traits<char>::eof()) {
		throw std::runtime_error("native layer composition fixture is truncated");
	}

	return static_cast<std::uint8_t>(value);
}

static std::uint32_t ReadU32(std::istream& input) {
	std::uint32_t value = 0;
	for (std::uint32_t index = 0; index < 4; ++index) {
		value |= static_cast<std::uint32_t>(ReadByte(input)) << (index * 8);
	}

	return value;
}

static float ReadFloat(std::istream& input) {
	return std::bit_cast<float>(ReadU32(input));
}

static void RequireFloat(float actual, std::istream& input, std::string_view context) {
	Require(std::bit_cast<std::uint32_t>(actual) == ReadU32(input), context);
}

static sgre::scene::MotionLayerSample ReadSample(std::istream& input) {
	sgre::scene::MotionLayerSample sample;
	for (auto& component : sample.Coordinate) {
		component = ReadFloat(input);
	}

	sample.Angle = ReadFloat(input);
	sample.ZoomX = ReadFloat(input);
	sample.ZoomY = ReadFloat(input);
	sample.ShearX = ReadFloat(input);
	sample.ShearY = ReadFloat(input);
	sample.Opacity = ReadU32(input);
	sample.ModulationColor = ReadU32(input);
	sample.Hidden = ReadByte(input) != 0;
	sample.FlipX = ReadByte(input) != 0;
	sample.FlipY = ReadByte(input) != 0;

	return sample;
}

static sgre::scene::MotionLayerOverrides ReadOverrides(std::istream& input) {
	sgre::scene::MotionLayerOverrides overrides;
	for (auto& component : overrides.Coordinate) {
		component = ReadFloat(input);
	}

	overrides.Angle = ReadFloat(input);
	overrides.ZoomX = ReadFloat(input);
	overrides.ZoomY = ReadFloat(input);
	overrides.ShearX = ReadFloat(input);
	overrides.ShearY = ReadFloat(input);
	overrides.Opacity = ReadU32(input);
	overrides.Enabled = ReadByte(input) != 0;
	overrides.Visible = ReadByte(input) != 0;
	overrides.FlipX = ReadByte(input) != 0;
	overrides.FlipY = ReadByte(input) != 0;

	return overrides;
}

static void RequireLayer(const sgre::scene::MotionComposedLayer& actual, std::istream& input) {
	const auto& state = actual.Hierarchy;
	for (const auto coefficient : {state.Transform.XX, state.Transform.XY, state.Transform.YX, state.Transform.YY}) {
		RequireFloat(coefficient, input, "native selected layer matrix differs");
	}

	for (const auto component : state.Attributes.Coordinate) {
		RequireFloat(component, input, "native selected layer coordinate differs");
	}

	for (const auto attribute : {state.Attributes.Angle, state.Attributes.ZoomX, state.Attributes.ZoomY, state.Attributes.ShearX, state.Attributes.ShearY}) {
		RequireFloat(attribute, input, "native selected layer numeric attribute differs");
	}

	Require(state.Attributes.Opacity == ReadU32(input), "native selected layer opacity differs");
	Require(state.ModulationColor == ReadU32(input), "native selected layer inherited modulation differs");
	for (const auto component : actual.Displacement) {
		RequireFloat(component, input, "native selected layer displacement differs");
	}

	Require(actual.Active == (ReadByte(input) != 0), "native selected layer active gate differs");
	Require(actual.Visible == (ReadByte(input) != 0), "native selected layer visible gate differs");
	Require(state.Attributes.FlipX == (ReadByte(input) != 0), "native selected layer horizontal flip differs");
	Require(state.Attributes.FlipY == (ReadByte(input) != 0), "native selected layer vertical flip differs");
	const auto expected_parent = ReadU32(input);
	Require(expected_parent == (actual.EffectiveParent.has_value() ? *actual.EffectiveParent + 1 : 0), "native selected layer effective parent differs");
	Require(state.Attributes.Hidden == !actual.Active && state.UnresolvedInheritMask == 0, "recovered selected visibility or inheritance remained unresolved");
}

static void ReplayNative(const char* path) {
	std::ifstream input(path, std::ios::binary);
	Require(input.good(), "native layer composition fixture is unavailable");
	for (const auto byte : std::array<std::uint8_t, 4>{'M', 'L', 'C', '1'}) {
		Require(ReadByte(input) == byte, "native layer composition fixture signature differs");
	}

	const auto cases = ReadU32(input);
	const auto steps = ReadU32(input);
	const auto layers = ReadU32(input);
	Require(cases > 0 && cases <= 4096 && steps > 0 && steps <= 4096 && layers > 0 && layers <= 64, "native layer composition fixture count exceeds its bound");
	for (std::uint32_t case_index = 0; case_index < cases; ++case_index) {
		const auto normalized = ReadByte(input) != 0;
		sgre::scene::PreparedMotionTimeline timeline;
		for (std::uint32_t index = 0; index < layers; ++index) {
			const auto parent = ReadU32(input);
			sgre::scene::PreparedMotionLayer layer;
			layer.Type = index == 1 ? 0u : 2u;
			layer.InheritMask = ReadU32(input);
			layer.CoordinatePlane = ReadU32(input);
			layer.HasColorModifier = ReadU32(input) != 0;
			timeline.Layers.push_back(std::move(layer));
			timeline.Parents.push_back(parent == 0 ? std::nullopt : std::optional<std::uint32_t>{parent - 1});
		}

		sgre::scene::MotionRootScene root;
		sgre::scene::MotionLayerComposition composition(timeline, root, normalized);
		std::vector<sgre::scene::MotionLayerSample> local(layers);
		for (std::uint32_t step = 0; step < steps; ++step) {
			const auto tick_assigned = ReadByte(input) != 0;
			std::array<float, 3> root_coordinates;
			for (auto& component : root_coordinates) {
				component = ReadFloat(input);
			}

			root.SetCoordinate3(root_coordinates);
			root.SetVisible(ReadByte(input) != 0);
			const auto horizontal = ReadByte(input) != 0;
			const auto vertical = ReadByte(input) != 0;
			root.SetFlip(horizontal, vertical);
			for (std::uint32_t index = 0; index < layers; ++index) {
				local[index] = ReadSample(input);
				composition.SetOverrides(index, ReadOverrides(input));
			}

			composition.Compose(local, tick_assigned);
			for (const auto& layer : composition.Layers()) {
				RequireLayer(layer, input);
			}
		}
	}

	Require(input.peek() == std::char_traits<char>::eof(), "native layer composition fixture has trailing bytes");
	std::cout << "Replayed " << cases * steps * layers << " native selected layer outputs\n";
}

int main(int argc, char** argv) {
	try {
		Require(argc == 1 || argc == 2, "usage: motion_hierarchy_composition_tests [native-layer-composition-fixture]");
		TestVisibilityAndAncestors();
		TestBoundedDepthAndFailedPublication();
		if (argc == 2) {
			ReplayNative(argv[1]);
		}

		std::cout << "motion layer composition tests passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
