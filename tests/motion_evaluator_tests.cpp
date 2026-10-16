#include "sgre/scene/motion_evaluator.hpp"

#include <bit>
#include <cstdint>
#include <functional>
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

static void RequireFailure(const std::function<void()>& operation, std::string_view context) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(context) != std::string_view::npos, "layer failure omitted required context");

		return;
	}

	throw std::runtime_error("invalid layer was accepted: " + std::string(context));
}

template <typename Value>
static sgre::resource::PsbValue MakeValue(Value value) {
	sgre::resource::PsbValue result;
	result.Data = std::move(value);

	return result;
}

static sgre::resource::MotionFrame MakeFrame(float time, std::uint32_t type, double x, double opacity) {
	sgre::resource::MotionFrame frame;
	frame.Time = time;
	frame.Type = type;
	frame.Content = MakeValue(sgre::resource::PsbObject{{"mask", MakeValue(std::int64_t{0x612})}, {"src", MakeValue(std::string("Atlas"))}, {"icon", MakeValue(std::string("Icon"))}, {"coord", MakeValue(sgre::resource::PsbArray{MakeValue(x), MakeValue(0.0), MakeValue(0.0)})}, {"opa", MakeValue(opacity)}, {"angle", MakeValue(time == 0 ? 350.0 : 10.0)}, {"color", MakeValue(std::int64_t{time == 0 ? 0x000000ff : 0xffffffff})}});

	return frame;
}

static sgre::resource::MotionLayer MakeLayer(std::uint32_t first_type = 3) {
	sgre::resource::MotionLayer layer;
	layer.Label = "Synthetic";
	layer.Type = 0;
	layer.InheritMask = 33556476;
	layer.TransformOrder = {0, 3, 2, 1};
	layer.Frames = {MakeFrame(0, first_type, 0, 0.9), MakeFrame(10, 2, 100, 255.9), MakeFrame(20, 0, 100, 255)};

	return layer;
}

static sgre::resource::PsbValue& Field(sgre::resource::PsbValue& value, std::string_view name) {
	for (auto& [key, child] : std::get<sgre::resource::PsbObject>(value.Data)) {
		if (key == name) {
			return child;
		}
	}

	throw std::runtime_error("missing test attribute");
}

static void TestHoldTweenAndTerminalSelection() {
	sgre::resource::MotionResource resource;
	const auto tween = sgre::scene::PrepareMotionLayer(MakeLayer(), resource);
	const auto middle = sgre::scene::EvaluateMotionLayer(tween, resource, 5);
	Require(!middle.Hidden && middle.Source == "Atlas" && middle.Icon == "Icon", "layer visibility or resource identity changed");
	Require(middle.Coordinate[0] == 50 && middle.Angle == 0 && middle.Opacity == 128 && middle.Colors[0] == 0x7f7f7fff, "native tween attribute interpolation changed");
	Require(middle.Source.data() == tween.Frames[0].Attributes.Source.data(), "layer sampling allocated or copied a resource identity");
	const auto exact = sgre::scene::EvaluateMotionLayer(tween, resource, 10);
	const auto terminal = sgre::scene::EvaluateMotionLayer(tween, resource, 20);
	Require(exact.Coordinate[0] == 100 && terminal.Coordinate[0] == 100 && !terminal.Hidden, "native n-2 current-frame clamp changed at the terminal frame");
	const auto hold = sgre::scene::PrepareMotionLayer(MakeLayer(2), resource);
	Require(sgre::scene::EvaluateMotionLayer(hold, resource, 5).Coordinate[0] == 0, "hold keyframe interpolated");
	const auto hidden = sgre::scene::PrepareMotionLayer(MakeLayer(0), resource);
	Require(sgre::scene::EvaluateMotionLayer(hidden, resource, 5).Hidden, "empty current keyframe remained visible");
}

static void TestQuantizationAndUnresolvedMask() {
	sgre::resource::MotionResource resource;
	auto layer = MakeLayer();
	Field(layer.Frames[0].Content, "mask").Data = std::int64_t{0x04080612};
	std::get<sgre::resource::PsbObject>(layer.Frames[0].Content.Data).emplace_back("ti", MakeValue(std::int64_t{3}));
	const auto prepared = sgre::scene::PrepareMotionLayer(layer, resource);
	const auto sample = sgre::scene::EvaluateMotionLayer(prepared, resource, 5);
	Require(std::bit_cast<std::uint32_t>(sample.Coordinate[0]) == 0x41f00001, "native elapsed-time interval quantization changed");
	Require(sample.UnresolvedMask == 0x00080000, "unrecovered mask bits were silently dropped");
}

static void TestAtlasColorOrderAndDefaults() {
	sgre::resource::MotionResource resource;
	auto layer = MakeLayer(2);
	Field(layer.Frames[0].Content, "color").Data = sgre::resource::PsbArray{MakeValue(std::int64_t{1}), MakeValue(std::int64_t{2}), MakeValue(std::int64_t{3}), MakeValue(std::int64_t{4})};
	const auto prepared = sgre::scene::PrepareMotionLayer(layer, resource);
	const auto sample = sgre::scene::EvaluateMotionLayer(prepared, resource, 0);
	Require(sample.Colors == std::array<std::uint32_t, 4>({1, 2, 4, 3}) && !sample.UniformColor, "native strip corner order changed");
	Require(sample.ZoomX == 1 && sample.ZoomY == 1 && !sample.FlipX && !sample.FlipY, "native transform defaults changed");
}

static void TestEasingAndSafety() {
	sgre::resource::MotionResource resource;
	sgre::resource::EasingCurve curve;
	curve.Segments = {{{0, 0, 0}, {1, 0.5f, 0}}};
	resource.Easing.push_back(curve);
	auto layer = MakeLayer();
	Field(layer.Frames[0].Content, "mask").Data = std::int64_t{0xe12};
	std::get<sgre::resource::PsbObject>(layer.Frames[0].Content.Data).emplace_back("ccc", MakeValue(std::int64_t{0}));
	const auto prepared = sgre::scene::PrepareMotionLayer(layer, resource);
	Require(sgre::scene::EvaluateMotionLayer(prepared, resource, 5).Coordinate[0] == 25, "coordinate easing index was ignored");
	RequireFailure([&]() {
		static_cast<void>(sgre::scene::EvaluateMotionLayer(prepared, resource, -1));
	},
	    "before its frame domain");
	Field(layer.Frames[0].Content, "ccc").Data = std::int64_t{1};
	RequireFailure([&]() {
		static_cast<void>(sgre::scene::PrepareMotionLayer(layer, resource));
	},
	    "easing index");
	layer = MakeLayer();
	Field(layer.Frames[0].Content, "opa").Data = 256.0;
	RequireFailure([&]() {
		static_cast<void>(sgre::scene::PrepareMotionLayer(layer, resource));
	},
	    "opacity exceeds");
}

static void TestCoordinatePathPlanes() {
	// Synthetic descriptors evaluated by original 0x140528530 instructions, including coordinate easing.
	const std::array<std::array<std::uint32_t, 9>, 4> inputs = {{{0xc46837a1, 0xc41eb22a, 0x43cee8c1, 0xc3dddf18, 0xc38f1760, 0x43aa4619, 0x3c8440f0, 1, 0}, {0x43552be3, 0xc46ec635, 0xc29680e1, 0x44695371, 0x441235a1, 0x44038642, 0x3f773497, 0, 1}, {0xc39a1e45, 0xc447a465, 0x4344ec80, 0xc3cea218, 0x43d5377d, 0x445d29a4, 0x3f45c7d7, 1, 1}, {0x4417272f, 0xc41786e3, 0xc4009e4c, 0xc439de4d, 0xc43f6fae, 0x4457d5c2, 0x3f32c2e1, 0, 0}}};
	const std::array<std::array<std::uint32_t, 3>, 4> expected = {{{0xc4679b33, 0xc41d49fa, 0x43d675f1}, {0x4463f09a, 0x4414b295, 0x44019c45}, {0xc401a039, 0x438643e6, 0x4457a3a1}, {0xc3f61ac1, 0xc4b45196, 0x43dfd13a}}};
	sgre::resource::EasingCurve curve;
	curve.Segments = {{{0, 0, 0}, {0.2f, 0.15f, 3}, {0.5f, 0.45f, -2}, {0.8f, 0.9f, -4}, {1, 1, 0}}};
	sgre::resource::MotionResource resource;
	resource.Easing.push_back(curve);
	sgre::resource::MotionPath path;
	sgre::resource::MotionPathSegment segment;
	segment.Begin = 0;
	segment.End = 1;
	segment.Controls = {{{0, 0}, {0, 1}, {1, 1}, {1, 0}}};
	segment.Remapping = curve.Segments[0];
	path.Segments.push_back(segment);
	for (std::size_t index = 0; index < inputs.size(); ++index) {
		sgre::scene::PreparedMotionLayer layer;
		layer.CoordinatePlane = inputs[index][7];
		for (std::size_t frame_index = 0; frame_index < 2; ++frame_index) {
			sgre::scene::PreparedMotionFrame frame;
			frame.Time = static_cast<float>(frame_index);
			frame.Hidden = false;
			frame.Tween = true;
			for (std::size_t component = 0; component < 3; ++component) {
				frame.Attributes.Coordinate[component] = std::bit_cast<float>(inputs[index][frame_index * 3 + component]);
			}

			frame.Attributes.CoordinatePath = path;
			if (inputs[index][8] != 0) {
				frame.Attributes.EasingIndices[0] = 0;
			}

			layer.Frames.push_back(std::move(frame));
		}

		const auto sample = sgre::scene::EvaluateMotionLayer(layer, resource, std::bit_cast<float>(inputs[index][6]));
		for (std::size_t component = 0; component < 3; ++component) {
			Require(std::bit_cast<std::uint32_t>(sample.Coordinate[component]) == expected[index][component], "original coordinate-path plane instruction vector changed");
		}

		layer.CoordinatePlane = 2;
		RequireFailure([&]() {
			static_cast<void>(sgre::scene::EvaluateMotionLayer(layer, resource, 0.5f));
		},
		    "coordinate path plane");
		layer.Frames[1].Attributes.Coordinate = layer.Frames[0].Attributes.Coordinate;
		Require(sgre::scene::EvaluateMotionLayer(layer, resource, 0.5f).Coordinate == layer.Frames[0].Attributes.Coordinate, "native equal-coordinate shortcut evaluated an unused path");
	}
}

static void TestOriginalLocalTransformVectors() {
	// Original 0x14050ed50 with the SSE CRT dispatch branch and synthetic attributes.
	const std::array<std::array<std::uint32_t, 11>, 5> inputs = {{{0, 1, 2, 3, 0xc42b7bd4, 0x3fc6600b, 0x4001a110, 0x3f14dca6, 0xbf253966, 0, 0}, {0, 3, 2, 1, 0xc3a0a5b5, 0x40065201, 0x4014f458, 0x3ee2e145, 0x3f943388, 1, 0}, {3, 2, 1, 0, 0xc3aa3fb9, 0x3faa2c42, 0x405090d4, 0x3fe0e3c8, 0xbe838f3a, 1, 1}, {0, 1, 2, 3, 0x49f3fed9, 0x3ff85070, 0x3fda629a, 0x3f60e890, 0xbf9c8d05, 0, 0}, {3, 2, 1, 0, 0xc93eb4e7, 0x3f98339c, 0x404bc1ea, 0x3feb187d, 0x3f5763e9, 1, 1}}};
	const std::array<std::array<std::uint32_t, 4>, 5> expected = {{{0x3ff8c740, 0x3ddc5138, 0x3e9ca138, 0x400f3e25}, {0x3d418b20, 0xbf3ac822, 0xc05a93e8, 0x40197584}, {0xbfc4312b, 0xbf8e9377, 0x3eb0e3fb, 0xc07681d2}, {0x3fbac730, 0xbffc2918, 0x3fe08275, 0x401599ee}, {0x3ff91868, 0x3fff3826, 0xc00c3f35, 0xc053967e}}};
	for (std::size_t index = 0; index < inputs.size(); ++index) {
		sgre::scene::PreparedMotionLayer layer;
		for (std::size_t operation = 0; operation < layer.TransformOrder.size(); ++operation) {
			layer.TransformOrder[operation] = static_cast<sgre::scene::MotionTransformOperation>(inputs[index][operation]);
		}

		sgre::scene::MotionLayerSample sample;
		sample.Angle = std::bit_cast<float>(inputs[index][4]);
		sample.ZoomX = std::bit_cast<float>(inputs[index][5]);
		sample.ZoomY = std::bit_cast<float>(inputs[index][6]);
		sample.ShearX = std::bit_cast<float>(inputs[index][7]);
		sample.ShearY = std::bit_cast<float>(inputs[index][8]);
		sample.FlipX = inputs[index][9] != 0;
		sample.FlipY = inputs[index][10] != 0;
		const auto transform = sgre::scene::EvaluateMotionTransform(layer, sample);
		const std::array<std::uint32_t, 4> actual = {std::bit_cast<std::uint32_t>(transform.XX), std::bit_cast<std::uint32_t>(transform.XY), std::bit_cast<std::uint32_t>(transform.YX), std::bit_cast<std::uint32_t>(transform.YY)};
		Require(actual == expected[index], "original local transform instruction vector changed");
	}

	sgre::scene::PreparedMotionLayer layer;
	sgre::scene::MotionLayerSample sample;
	sample.Angle = 3600001.0f;
	RequireFailure([&]() {
		static_cast<void>(sgre::scene::EvaluateMotionTransform(layer, sample));
	},
	    "trigonometric range");
	sgre::resource::MotionResource resource;
	auto malformed = MakeLayer();
	malformed.TransformOrder = {0, 1, 2};
	RequireFailure([&]() {
		static_cast<void>(sgre::scene::PrepareMotionLayer(malformed, resource));
	},
	    "four operations");
	malformed.TransformOrder = {0, 1, 2, 4};
	RequireFailure([&]() {
		static_cast<void>(sgre::scene::PrepareMotionLayer(malformed, resource));
	},
	    "transform operation");
}

static void TestOriginalHierarchyVectors() {
	// Original 0x14050c550 updates three synthetic layers with partial/full inheritance and both root modes.
	const std::array<std::array<std::uint32_t, 45>, 3> inputs = {{{0x41057415, 0x4095bdd3, 0xc2623ec5, 0x436e71a3, 0x3f93aa8e, 0x3f33dd41, 0x3e87df28, 0xbeb52741, 0x00000089, 0x00000000, 0x00000001, 0xa5a86204, 0x42ae4e85, 0xc28c89f1, 0xc13f860f, 0x4ee46dbf, 0x9d30e09e, 0x615b78d7, 0x41105250, 0x000000ed, 0xc3226c0e, 0x3f5b332f, 0x3fb4d691, 0xbe8e19ca, 0x3ecacd89, 0x00000000, 0x00000001, 0x0000050c, 0x4292afd3, 0xc22e6dd4, 0xc1924cc0, 0x7652cf28, 0x988bd0e7, 0x524e0e0f, 0x54626aaf, 0x000000f9, 0x43383e1c, 0x3ff0c1fc, 0x3ff103f8, 0x3e0bb1c5, 0x3e06b04e, 0x00000001, 0x00000000, 0x000007bc, 0x00000000}, {0xc21cd8ad, 0x42190bab, 0x428f6203, 0xc2d1fe2a, 0x3fad305d, 0x3f7cf89c, 0x3e56e5cb, 0x3eeda03a, 0x000000ea, 0x00000000, 0x00000000, 0x50ffb8b0, 0x42bdfbc0, 0xc10a1763, 0xc2a4fa6b, 0xeed9521b, 0x47b97dc5, 0x0f39a7cc, 0x46588b48, 0x00000054, 0xc36f0dba, 0x3fb31b96, 0x3f1e08d6, 0xbed1da81, 0xbec16d78, 0x00000001, 0x00000001, 0x0000058c, 0xc27e008e, 0xc12f5a72, 0xc29944ca, 0x185a74d9, 0x33c9ccbd, 0x213c9acf, 0xa4d9a4cc, 0x0000005b, 0x41b94851, 0x3fba4cb7, 0x3fd4ec1b, 0xbe3c400a, 0xbe92fa9f, 0x00000000, 0x00000000, 0x00000094, 0x00000001}, {0xc2c58a64, 0xc2bd9298, 0xc2b74595, 0x427d360b, 0x3f92b076, 0x3f853484, 0xbeeacc62, 0x3e73544a, 0x00000015, 0x00000001, 0x00000001, 0x80ae106e, 0x419b66f3, 0xc14c6353, 0xc2949ca9, 0xb96585ce, 0x583038a0, 0xecfcc961, 0x83fb3825, 0x00000097, 0x439a53fd, 0x3f1207a8, 0x3fb46d73, 0x3e17f6f2, 0x3e7f5cc3, 0x00000000, 0x00000001, 0x00000270, 0xc218e170, 0x4278a8fe, 0x40ccb762, 0x72dea0de, 0x1befc149, 0x2201f333, 0x0a60d0c3, 0x00000016, 0x4196ed46, 0x3fd2b813, 0x3f850980, 0xbeb1dec9, 0x3efcee5a, 0x00000000, 0x00000000, 0x000007fc, 0x00000000}}};
	const std::array<std::array<std::uint32_t, 16>, 3> expected = {{{0xbfafcead, 0xbf692f9a, 0xbf670872, 0x3fd4ef19, 0xc2ede9ae, 0xc2a2bca9, 0xc2ada354, 0x41ae9070, 0x3fce2615, 0x3ff103f8, 0xbe1081cf, 0x3e31fcde, 0x0000007c, 0x00000001, 0x00000000, 0x808080ff}, {0x3f919c18, 0x3eb914d8, 0xc00069dd, 0xc0312b87, 0x41c5ecd2, 0xc2a339d0, 0xc2aedd32, 0xc357e4b0, 0x3fba4cb7, 0x3fd4ec1b, 0xbec487a0, 0xbe92fa9f, 0x00000053, 0x00000001, 0x00000000, 0x808080ff}, {0x3f7f5822, 0xbea7ba4d, 0xbf4b86bb, 0xbfcc24a3, 0xc2ffd8aa, 0xc3622ca3, 0xc31f8b64, 0x43c36992, 0x3f89c034, 0x3fc32755, 0xbe4bc6a0, 0x3f3e4e5e, 0x0000000d, 0x00000000, 0x00000001, 0x80ae106e}}};
	for (std::size_t index = 0; index < inputs.size(); ++index) {
		const auto& words = inputs[index];
		sgre::scene::PreparedMotionLayer root_layer;
		sgre::scene::MotionHierarchyState root;
		root.Attributes.Hidden = false;
		for (std::size_t component = 0; component < 3; ++component) {
			root.Attributes.Coordinate[component] = std::bit_cast<float>(words[component]);
		}
		root.Attributes.Angle = std::bit_cast<float>(words[3]);
		root.Attributes.ZoomX = std::bit_cast<float>(words[4]);
		root.Attributes.ZoomY = std::bit_cast<float>(words[5]);
		root.Attributes.ShearX = std::bit_cast<float>(words[6]);
		root.Attributes.ShearY = std::bit_cast<float>(words[7]);
		root.Attributes.Opacity = words[8];
		root.Attributes.FlipX = words[9] != 0;
		root.Attributes.FlipY = words[10] != 0;
		root.ModulationColor = words[11];
		root.Transform = sgre::scene::EvaluateMotionTransform(root_layer, root.Attributes);
		auto parent = root;
		for (std::size_t level = 0; level < 2; ++level) {
			const auto offset = 12 + level * 16;
			sgre::scene::PreparedMotionLayer layer;
			layer.InheritMask = words[offset + 15];
			sgre::scene::MotionLayerSample local;
			local.Hidden = false;
			for (std::size_t component = 0; component < 3; ++component) {
				local.Coordinate[component] = std::bit_cast<float>(words[offset + component]);
			}
			local.Opacity = words[offset + 7];
			local.Angle = std::bit_cast<float>(words[offset + 8]);
			local.ZoomX = std::bit_cast<float>(words[offset + 9]);
			local.ZoomY = std::bit_cast<float>(words[offset + 10]);
			local.ShearX = std::bit_cast<float>(words[offset + 11]);
			local.ShearY = std::bit_cast<float>(words[offset + 12]);
			local.FlipX = words[offset + 13] != 0;
			local.FlipY = words[offset + 14] != 0;
			parent = sgre::scene::ComposeMotionHierarchyLayer(layer, local, parent, root, words[44] != 0);
		}
		const auto& attributes = parent.Attributes;
		const std::array<std::uint32_t, 16> actual = {std::bit_cast<std::uint32_t>(parent.Transform.XX), std::bit_cast<std::uint32_t>(parent.Transform.XY), std::bit_cast<std::uint32_t>(parent.Transform.YX), std::bit_cast<std::uint32_t>(parent.Transform.YY), std::bit_cast<std::uint32_t>(attributes.Coordinate[0]), std::bit_cast<std::uint32_t>(attributes.Coordinate[1]), std::bit_cast<std::uint32_t>(attributes.Coordinate[2]), std::bit_cast<std::uint32_t>(attributes.Angle), std::bit_cast<std::uint32_t>(attributes.ZoomX), std::bit_cast<std::uint32_t>(attributes.ZoomY), std::bit_cast<std::uint32_t>(attributes.ShearX), std::bit_cast<std::uint32_t>(attributes.ShearY), attributes.Opacity, static_cast<std::uint32_t>(attributes.FlipX), static_cast<std::uint32_t>(attributes.FlipY), parent.ModulationColor};
		Require(actual == expected[index], "original hierarchy instruction vector changed");
	}
	Require(sgre::scene::ComposeMotionColor(0xc0804080, 0x80c0ff80) == 0xc0c07f40, "native modulation color arithmetic changed");
	Require(sgre::scene::ComposeMotionColor(0xffffffff, 0xffffffff) == 0xffffffff, "native modulation color saturation changed");
	Require(sgre::scene::ComposeMotionColor(0x808080ff, 0x12345678) == 0x12345678, "native neutral modulation shortcut changed");
}

static void TestPriorityAndFlattening() {
	sgre::resource::MotionResource resource;
	sgre::resource::MotionTimeline timeline;
	timeline.LastTime = 20;
	auto parent = MakeLayer();
	parent.Label = "Parent";
	auto child = MakeLayer();
	child.Label = "Child";
	parent.Children.push_back(child);
	auto sibling = MakeLayer();
	sibling.Label = "Sibling";
	timeline.Layers = {parent, sibling};
	for (const auto time : {0.0f, 5.0f, 20.0f}) {
		sgre::resource::MotionFrame frame;
		frame.Time = time;
		frame.Type = 1;
		if (time == 0) {
			frame.Content = MakeValue(sgre::resource::PsbArray{MakeValue(std::int64_t{2}), MakeValue(std::int64_t{1}), MakeValue(std::int64_t{0})});
		} else if (time == 5) {
			frame.Content = MakeValue(sgre::resource::PsbArray{MakeValue(std::int64_t{1}), MakeValue(std::int64_t{0}), MakeValue(std::int64_t{2})});
		}

		timeline.Priority.push_back(std::move(frame));
	}

	const auto prepared = sgre::scene::PrepareMotionTimeline(timeline, resource);
	Require(prepared.Layers.size() == 3 && prepared.Layers[0].Label == "Parent" && prepared.Layers[1].Label == "Child" && prepared.Layers[2].Label == "Sibling", "native depth-first layer flattening changed");
	Require(!prepared.Parents[0].has_value() && prepared.Parents[1] == 0 && !prepared.Parents[2].has_value(), "prepared layer parents changed");
	const auto first = sgre::scene::EvaluateMotionDrawOrder(prepared, 0);
	const auto later = sgre::scene::EvaluateMotionDrawOrder(prepared, 5);
	const auto terminal = sgre::scene::EvaluateMotionDrawOrder(prepared, 20);
	Require(first.size() == 3 && first[0] == 0 && first[1] == 1 && first[2] == 2, "native reverse priority submission changed");
	Require(later[0] == 2 && later[1] == 0 && later[2] == 1 && terminal.data() == later.data(), "priority terminal clamp or exact-time selection changed");
	Require(first.data() == prepared.Priority[0].DrawOrder.data(), "priority sampling allocated or copied its order");
	std::get<sgre::resource::PsbArray>(timeline.Priority[0].Content.Data)[0].Data = std::int64_t{3};
	RequireFailure([&]() {
		static_cast<void>(sgre::scene::PrepareMotionTimeline(timeline, resource));
	},
	    "index exceeds");
	RequireFailure([&]() {
		static_cast<void>(sgre::scene::EvaluateMotionDrawOrder(prepared, -1));
	},
	    "before its frame domain");
}

static void TestColorModifierCoupling() {
	sgre::resource::MotionResource resource;
	auto layer = MakeLayer();
	layer.Properties.emplace_back("hasColorModifier", MakeValue(true));
	for (std::size_t index = 0; index < 2; ++index) {
		Field(layer.Frames[index].Content, "mask").Data = std::int64_t{0x80000612};
		std::get<sgre::resource::PsbObject>(layer.Frames[index].Content.Data).emplace_back("cw", MakeValue(std::int64_t{index == 0 ? 0x000000ff : 0xffffffff}));
	}

	const auto prepared = sgre::scene::PrepareMotionLayer(layer, resource);
	const auto sample = sgre::scene::EvaluateMotionLayer(prepared, resource, 5);
	Require(prepared.HasColorModifier && sample.ModulationColor == 0x7f7f7fff && sample.UnresolvedMask == 0, "native cw modulation preparation or tweening changed");
	sgre::scene::MotionHierarchyState root;
	const auto state = sgre::scene::ComposeMotionHierarchyLayer(prepared, sample, root, root, false);
	Require(state.ModulationColor == 0x7f7f7fff && !sample.UsesDefaultColor, "native modulation inheritance or explicit-color state changed");
}

static void TestOriginalLayerInstructionVector() {
	// Synthetic descriptor evaluated by original 0x14050bc40 instructions.
	const std::array<std::uint32_t, 28> inputs = {0x433a1d3f, 0xc3c67029, 0xc3813684, 0x52b23d38, 0x3fa5ee5b, 0x42af2d85, 0xd3cb0799, 0x000000af, 0x42f60615, 0x3ffd54ab, 0x3fe8cf27, 0x409a36e2, 0x409a4cf9, 0xc4763349, 0xc423d5e4, 0x44729c81, 0x2f4c69fa, 0xaa97c788, 0x413f6767, 0xd2479f69, 0x000000d2, 0x43068473, 0x3fae30a9, 0x3fc69bc2, 0x40910a48, 0xc09f78f8, 0x3f08de6d, 0x00000000};
	const std::array<std::uint32_t, 13> expected = {0xc3dbf450, 0xc405c40f, 0x43c74a96, 0x3f7b549f, 0x779dd972, 0x41734b75, 0xd284577f, 0x000000c2, 0x430129c9, 0x3fd304c7, 0x3fd68622, 0x40954f38, 0xbed74ce0};
	sgre::scene::PreparedMotionLayer layer;
	layer.Rotation = static_cast<sgre::resource::RotationTransition>(inputs[27]);
	for (std::size_t index = 0; index < 2; ++index) {
		const auto offset = index * 13;
		sgre::scene::PreparedMotionFrame frame;
		frame.Time = static_cast<float>(index);
		frame.Hidden = false;
		frame.Tween = true;
		for (std::size_t component = 0; component < 3; ++component) {
			frame.Attributes.Coordinate[component] = std::bit_cast<float>(inputs[offset + component]);
		}

		for (std::size_t corner = 0; corner < 4; ++corner) {
			frame.Attributes.Colors[corner] = inputs[offset + 3 + corner];
		}

		frame.Attributes.UniformColor = false;
		frame.Attributes.Opacity = inputs[offset + 7];
		frame.Attributes.Angle = std::bit_cast<float>(inputs[offset + 8]);
		frame.Attributes.ZoomX = std::bit_cast<float>(inputs[offset + 9]);
		frame.Attributes.ZoomY = std::bit_cast<float>(inputs[offset + 10]);
		frame.Attributes.ShearX = std::bit_cast<float>(inputs[offset + 11]);
		frame.Attributes.ShearY = std::bit_cast<float>(inputs[offset + 12]);
		layer.Frames.push_back(std::move(frame));
	}

	sgre::resource::MotionResource resource;
	const auto sample = sgre::scene::EvaluateMotionLayer(layer, resource, std::bit_cast<float>(inputs[26]));
	std::array<std::uint32_t, 13> actual{};
	for (std::size_t component = 0; component < 3; ++component) {
		actual[component] = std::bit_cast<std::uint32_t>(sample.Coordinate[component]);
	}

	for (std::size_t corner = 0; corner < 4; ++corner) {
		actual[corner + 3] = sample.Colors[corner];
	}

	actual[7] = sample.Opacity;
	actual[8] = std::bit_cast<std::uint32_t>(sample.Angle);
	actual[9] = std::bit_cast<std::uint32_t>(sample.ZoomX);
	actual[10] = std::bit_cast<std::uint32_t>(sample.ZoomY);
	actual[11] = std::bit_cast<std::uint32_t>(sample.ShearX);
	actual[12] = std::bit_cast<std::uint32_t>(sample.ShearY);
	Require(actual == expected, "original layer instruction golden words changed");
}

static void TestMeshPatchCoupling() {
	sgre::resource::MotionResource resource;
	sgre::resource::EasingCurve curve;
	curve.Segments = {{{0, 0, 0}, {1, 0.5f, 0}}};
	resource.Easing.push_back(curve);
	auto layer = MakeLayer();
	layer.Properties.emplace_back("meshTransform", MakeValue(std::int64_t{1}));
	Field(layer.Frames[0].Content, "mask").Data = std::int64_t{0x02000612};
	Field(layer.Frames[1].Content, "mask").Data = std::int64_t{0x02000612};
	std::get<sgre::resource::PsbObject>(layer.Frames[0].Content.Data).emplace_back("mesh", MakeValue(sgre::resource::PsbObject{{"cc", MakeValue(std::int64_t{0})}}));
	sgre::resource::PsbArray points;
	for (std::size_t row = 0; row < 4; ++row) {
		for (std::size_t column = 0; column < 4; ++column) {
			points.push_back(MakeValue(static_cast<double>(column) / 3 + 1));
			points.push_back(MakeValue(static_cast<double>(row) / 3));
		}
	}

	std::get<sgre::resource::PsbObject>(layer.Frames[1].Content.Data).emplace_back("mesh", MakeValue(sgre::resource::PsbObject{{"bp", MakeValue(points)}}));
	const auto prepared = sgre::scene::PrepareMotionLayer(layer, resource);
	const auto middle = sgre::scene::EvaluateMotionLayer(prepared, resource, 5);
	Require(middle.UnresolvedMask == 0 && middle.Deformation && middle.Deformation->Controls[0].X == 0.25f, "native mesh easing or missing-endpoint identity coupling changed");
	const auto held = sgre::scene::EvaluateMotionLayer(prepared, resource, 10);
	Require(held.Deformation && held.Deformation->Controls[0].X == 1, "native hold did not copy mesh control points");
	layer.Properties[0].second.Data = std::int64_t{2};
	const auto unsupported = sgre::scene::PrepareMotionLayer(layer, resource);
	Require(unsupported.Frames[0].Attributes.UnresolvedMask == 0x02000000 && !unsupported.Frames[1].Attributes.Deformation.Patch, "unrecovered mesh transform acquired decoded runtime behavior");
}

static void TestMeshLayerProperties() {
	sgre::resource::MotionResource resource;
	auto layer = MakeLayer();
	layer.Properties.emplace_back("meshTransform", MakeValue(std::int64_t{1}));
	layer.Properties.emplace_back("meshSyncChildMask", MakeValue(std::int64_t{8}));
	layer.Properties.emplace_back("meshDivision", MakeValue(19.8));
	layer.Properties.emplace_back("meshCombinator", MakeValue(std::monostate{}));
	layer.Properties.emplace_back("meshCombine", MakeValue(true));
	auto prepared = sgre::scene::PrepareMotionLayer(layer, resource);
	Require(prepared.MeshSyncChildMask == 8 && prepared.MeshDivision == 19 && prepared.MeshCombinator && prepared.MeshCombine, "native mesh layer properties were not retained");
	layer.Properties[0].second.Data = std::int64_t{0};
	prepared = sgre::scene::PrepareMotionLayer(layer, resource);
	Require(prepared.MeshSyncChildMask == 0 && prepared.MeshDivision == 0 && !prepared.MeshCombinator && !prepared.MeshCombine, "disabled native mesh mode consumed mesh-only properties");
}

int main() {
	try {
		TestOriginalLayerInstructionVector();
		TestCoordinatePathPlanes();
		TestOriginalLocalTransformVectors();
		TestOriginalHierarchyVectors();
		TestPriorityAndFlattening();
		TestColorModifierCoupling();
		TestMeshPatchCoupling();
		TestMeshLayerProperties();
		TestHoldTweenAndTerminalSelection();
		TestQuantizationAndUnresolvedMask();
		TestAtlasColorOrderAndDefaults();
		TestEasingAndSafety();
		std::cout << "Native layer selection, hold/tween attributes, quantization, mask retention, and safety checks passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
