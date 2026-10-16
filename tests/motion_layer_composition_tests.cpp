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

static void TestRootMeshInheritanceBoundary() {
	for (const bool normalized : {false, true}) {
		sgre::scene::PreparedMotionTimeline timeline;
		timeline.Layers.resize(1);
		timeline.Parents.resize(1);
		timeline.Layers[0].InheritMask = 0x020007fc;
		timeline.Layers[0].HasColorModifier = true;
		auto baseline = timeline;
		baseline.Layers[0].InheritMask = 0x7fc;
		sgre::scene::MotionRootScene root;
		sgre::scene::MotionRootScene baseline_root;
		root.SetVisible(true);
		baseline_root.SetVisible(true);
		root.SetCoordinate3({2, 3, 4});
		baseline_root.SetCoordinate3({2, 3, 4});
		sgre::scene::MotionLayerComposition composition(timeline, root, normalized);
		sgre::scene::MotionLayerComposition expected(baseline, baseline_root, normalized);
		std::array<sgre::scene::MotionLayerSample, 1> local;
		local[0].ZoomX = 2;
		local[0].ZoomY = 0.5f;
		local[0].ShearX = 0.25f;
		local[0].ShearY = -0.5f;
		local[0].Opacity = 128;
		local[0].ModulationColor = 0x4080c0fe;
		for (std::uint32_t step = 0; step < 3; ++step) {
			local[0].Coordinate = {static_cast<float>(5 + step), 7, 2};
			local[0].Hidden = step == 2;
			composition.Compose(local, step == 0);
			expected.Compose(local, step == 0);
			const auto& actual = composition.Layers()[0];
			const auto& reference = expected.Layers()[0];
			const auto& attributes = actual.Hierarchy.Attributes;
			const auto& reference_attributes = reference.Hierarchy.Attributes;
			Require(attributes.Coordinate == reference_attributes.Coordinate && actual.Displacement == reference.Displacement, "root mesh inheritance changed the native pose/displacement class");
			Require(std::array{attributes.Angle, attributes.ZoomX, attributes.ZoomY, attributes.ShearX, attributes.ShearY} == std::array{reference_attributes.Angle, reference_attributes.ZoomX, reference_attributes.ZoomY, reference_attributes.ShearX, reference_attributes.ShearY}, "root mesh inheritance changed inherited numeric attributes");
			Require(attributes.Opacity == reference_attributes.Opacity && attributes.FlipX == reference_attributes.FlipX && attributes.FlipY == reference_attributes.FlipY && attributes.Hidden == reference_attributes.Hidden && actual.Hierarchy.ModulationColor == reference.Hierarchy.ModulationColor, "root mesh inheritance changed color/opacity/flip/active state");
			const auto& matrix = actual.Hierarchy.Transform;
			const auto& reference_matrix = reference.Hierarchy.Transform;
			Require(std::array{matrix.XX, matrix.XY, matrix.YX, matrix.YY} == std::array{reference_matrix.XX, reference_matrix.XY, reference_matrix.YX, reference_matrix.YY}, "inactive root mesh inheritance changed its transform");
			Require(actual.Active == reference.Active && actual.Visible == reference.Visible && actual.FrameHidden == reference.FrameHidden && actual.EffectiveParent == reference.EffectiveParent, "root mesh inheritance changed hierarchy visibility or parent");
			Require(actual.Hierarchy.UnresolvedInheritMask == 0 && timeline.Layers[0].InheritMask == 0x020007fc, "root mesh inheritance must resolve its known inactive output without rewriting original metadata");
			const auto generic = sgre::scene::ComposeMotionHierarchyLayer(timeline.Layers[0], local[0], root.Frame().Hierarchy, root.Frame().Hierarchy, normalized);
			Require(generic.UnresolvedInheritMask == 0x02000000, "scalar hierarchy must retain mesh inheritance without a genuine composition boundary");
		}

		RequireFailure([&] {
			auto invalid = timeline;
			invalid.Layers[0].InheritMask |= 0x01000000;
			sgre::scene::MotionLayerComposition rejected(invalid, root, normalized);
		});
		RequireFailure([&] {
			auto invalid = timeline;
			invalid.Layers.push_back(invalid.Layers[0]);
			invalid.Layers[0].InheritMask = 0x7fc;
			invalid.Parents.push_back(0);
			sgre::scene::MotionLayerComposition rejected(invalid, root, normalized);
		});
		for (std::uint32_t domain = 0; domain < 4; ++domain) {
			RequireFailure([&] {
				auto invalid = timeline;
				if (domain == 0) {
					invalid.Layers[0].Type = 2;
				} else if (domain == 1) {
					invalid.Layers[0].CoordinatePlane = 1;
				} else if (domain == 2) {
					invalid.Layers[0].StencilType = 1;
				} else {
					invalid.Layers[0].MeshTransform = 1;
				}
				sgre::scene::MotionLayerComposition rejected(invalid, root, normalized);
			});
		}
		RequireFailure([&] {
			sgre::scene::MotionRootScene inherited_root(sgre::scene::MotionRootMatrixMode::SuppliedByParent);
			sgre::scene::MotionLayerComposition rejected(timeline, inherited_root, normalized);
		});
		RequireFailure([&] {
			auto unresolved = local;
			unresolved[0].UnresolvedMask = 0x02000000;
			composition.Compose(unresolved, true);
		});
	}
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
		Require(argc == 1 || argc == 2, "usage: motion_layer_composition_tests [native-layer-composition-fixture]");
		TestVisibilityAndAncestors();
		TestRootMeshInheritanceBoundary();
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
