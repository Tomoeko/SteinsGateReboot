#include "sgre/scene/motion_local_frames.hpp"

#include <array>
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
#include <vector>

static void Require(bool condition, std::string_view context) {
	if (!condition) {
		throw std::runtime_error(std::string(context));
	}
}

template <typename Operation>
static void RequireFailure(Operation operation, std::string_view context) {
	try {
		operation();
	} catch (const std::runtime_error&) {
		return;
	}

	throw std::runtime_error(std::string(context));
}

template <typename Type>
static sgre::resource::PsbValue Value(Type value) {
	sgre::resource::PsbValue result;
	result.Data = std::move(value);

	return result;
}

static sgre::resource::MotionResource CurveResource() {
	sgre::resource::MotionResource resource;
	sgre::resource::EasingCurve curve;
	curve.Segments.push_back({{0, 0, 0}, {0.25f, 0.125f, 0}, {0.75f, 0.875f, 0}, {1, 1, 0}});
	resource.Easing.push_back(std::move(curve));

	return resource;
}

static sgre::scene::PreparedMotionLayer Pair() {
	sgre::scene::PreparedMotionLayer layer;
	layer.Frames.resize(2);
	layer.Frames[0].Hidden = false;
	layer.Frames[0].Tween = true;
	layer.Frames[0].Attributes.Source = "current source";
	layer.Frames[0].Attributes.Icon = "current icon";
	layer.Frames[0].Attributes.Coordinate = {1000000, 2000000, 3000000};
	layer.Frames[0].Attributes.Opacity = 0;
	layer.Frames[0].Attributes.Colors.fill(0);
	layer.Frames[1].Time = 1;
	layer.Frames[1].Hidden = false;
	layer.Frames[1].Attributes.Source = "following source";
	layer.Frames[1].Attributes.Icon = "following icon";
	layer.Frames[1].Attributes.Opacity = 200;
	layer.Frames[1].Attributes.Coordinate = {0, 0, 0};
	layer.Frames[1].Attributes.Colors.fill(0xffffffff);
	layer.Frames[1].Attributes.UniformColor = false;
	layer.Frames[1].Attributes.UsesDefaultColor = false;

	return layer;
}

static void TestSelectedEpsilonAndColorCurve() {
	const auto resource = CurveResource();
	auto layer = Pair();
	layer.Frames[0].Attributes.EasingIndices[1] = 0;
	const auto sample = sgre::scene::EvaluateSelectedMotionLayer(layer, resource, 0.25f, 0);
	Require(sample.Opacity == 50 && sample.Colors == std::array<std::uint32_t, 4>{0x1f1f1f1f, 0x1f1f1f1f, 0x1f1f1f1f, 0x1f1f1f1f}, "occ maps packed color while opacity retains the raw linear ratio");
	Require(sample.Source == "current source" && sample.Icon == "current icon", "tween source/icon remains the selected current frame");
	Require(!sample.UniformColor && !sample.UsesDefaultColor, "actual tween combines current and following color flags");

	const auto epsilon = std::numeric_limits<float>::epsilon();
	const auto tiny = sgre::scene::EvaluateSelectedMotionLayer(layer, resource, epsilon * 0.5f, 0);
	Require(tiny.Coordinate == layer.Frames[0].Attributes.Coordinate && tiny.UniformColor && tiny.UsesDefaultColor, "ratio below float epsilon copies exact current values and flags");
	const auto boundary = sgre::scene::EvaluateSelectedMotionLayer(layer, resource, epsilon, 0);
	Require(boundary.Coordinate != layer.Frames[0].Attributes.Coordinate && !boundary.UniformColor && !boundary.UsesDefaultColor, "ratio equal to float epsilon performs actual interpolation");
	layer.Frames[0].Attributes.Interval = 1;
	const auto quantized = sgre::scene::EvaluateSelectedMotionLayer(layer, resource, 0.75f, 0);
	Require(quantized.Coordinate == layer.Frames[0].Attributes.Coordinate && quantized.UniformColor, "interval quantization occurs before epsilon snapping");
	layer.Frames[0].Attributes.Interval = 0;
	layer.Frames[1].Hidden = true;
	const auto hidden = sgre::scene::EvaluateSelectedMotionLayer(layer, resource, 0.5f, 0);
	Require(hidden.Coordinate == layer.Frames[0].Attributes.Coordinate && hidden.UniformColor, "following hidden frame preserves the present selected pose");
	layer.Frames[1].Hidden = false;
	layer.Frames[0].Attributes.Opacity = 256;
	RequireFailure([&] {
		static_cast<void>(sgre::scene::EvaluateSelectedMotionLayer(layer, resource, 0.5f, 0));
	},
	    "forged non-byte opacity rejects before float-to-integer conversion");
}

static void TestExactSelectedDuplicate() {
	sgre::resource::MotionResource resource;
	auto layer = Pair();
	layer.Frames[0].Tween = false;
	layer.Frames[1].Time = 0;
	layer.Frames.push_back(layer.Frames[1]);
	layer.Frames[2].Time = 1;
	const auto selected = sgre::scene::EvaluateSelectedMotionLayer(layer, resource, 0, 0);
	const auto searched = sgre::scene::EvaluateMotionLayer(layer, resource, 0);
	Require(selected.Source == "current source" && searched.Source == "following source", "selected-index evaluation retains the stateful seek's first exact duplicate");
	RequireFailure([&] {
		static_cast<void>(sgre::scene::EvaluateSelectedMotionLayer(layer, resource, 0, 2));
	},
	    "a terminal-only selected index rejects");
}

struct OwnerTrace {
	std::vector<std::string> Bindings;
	std::vector<std::string> Actions;
};

static sgre::scene::MotionLocalFrameServices OwnerServices(OwnerTrace& trace) {
	sgre::scene::MotionLocalFrameServices services;
	services.BindSource = [&trace](std::uint32_t layer, const auto& current, const auto& following) {
		Require(layer == 0 && following.Time >= current.Time, "owner binding receives its real adjacent pair");
		trace.Bindings.push_back(current.Attributes.Source);
	};
	services.EnqueueAction = [&trace](std::string_view label, std::string_view action) {
		Require(label == "label", "action borrows the actual selected layer label");
		trace.Actions.emplace_back(action);
	};

	return services;
}

static sgre::resource::MotionTimeline OwnerTimeline() {
	sgre::resource::MotionTimeline timeline;
	timeline.LastTime = 2;
	sgre::resource::MotionLayer layer;
	layer.Label = "label";
	layer.TransformOrder = {0, 3, 2, 1};
	for (std::uint32_t index = 0; index < 3; ++index) {
		sgre::resource::MotionFrame frame;
		frame.Time = static_cast<float>(index);
		frame.Type = index == 0 ? 3 : index == 1 ? 2
		                                         : 0;
		frame.Content = Value(sgre::resource::PsbObject{{"mask", Value(std::int64_t{0x40000})}, {"act", Value(std::string{"act"})}, {"src", Value(std::string{index == 0 ? "current source" : "following source"})}, {"icon", Value(std::string{"icon"})}});
		layer.Frames.push_back(std::move(frame));
	}

	timeline.Layers.push_back(std::move(layer));
	sgre::resource::MotionFrame priority;
	priority.Type = 1;
	priority.Content = Value(sgre::resource::PsbArray{Value(std::int64_t{0})});
	timeline.Priority.push_back(priority);
	priority.Time = 2;
	priority.Content.Data = std::monostate{};
	timeline.Priority.push_back(std::move(priority));

	return timeline;
}

static void TestResourceBackedSelectedOwner() {
	const auto resource = CurveResource();
	auto timeline = OwnerTimeline();
	auto prepared = sgre::scene::PrepareMotionTimeline(timeline, resource);
	prepared.Layers[0].Frames[0].Attributes.Coordinate = {0, 0, 0};
	prepared.Layers[0].Frames[1].Attributes.Coordinate = {100, 0, 0};
	prepared.Layers[0].Frames[0].Attributes.EasingIndices[0] = 0;
	OwnerTrace trace;
	RequireFailure([&] {
		sgre::scene::MotionLocalFrames static_only(timeline, prepared, OwnerServices(trace));
	},
	    "tween requires an actual resource owner rather than the static constructor");
	auto missing_services = OwnerServices(trace);
	missing_services.BindSource = {};
	RequireFailure([&] {
		sgre::scene::MotionLocalFrames unsupported(timeline, prepared, resource, missing_services);
	},
	    "resource-backed tween still requires actual source binding");
	auto duplicate_timeline = timeline;
	duplicate_timeline.Layers[0].Frames[1].Time = 0;
	auto duplicate_prepared = sgre::scene::PrepareMotionTimeline(duplicate_timeline, resource);
	RequireFailure([&] {
		sgre::scene::MotionLocalFrames unsupported(duplicate_timeline, duplicate_prepared, resource, OwnerServices(trace));
	},
	    "zero-duration present tween remains outside the supported numeric domain");

	sgre::scene::MotionLocalFrames local(timeline, prepared, resource, OwnerServices(trace));
	local.Traverse(sgre::scene::MotionFrameTraversal::Seek, 0);
	Require(trace.Bindings == std::vector<std::string>{"current source"} && trace.Actions == std::vector<std::string>{"act"}, "selected tween binds its current source before its exact seek action");
	local.Traverse(sgre::scene::MotionFrameTraversal::Forward, 0.25f);
	Require(local.State(0).CurrentIndex == 0 && local.Samples().size() == 1 && local.Samples()[0].Coordinate[0] == 12.5f && trace.Bindings.size() == 1 && trace.Actions.size() == 1, "intra-frame forward evaluates actual curve without repeated binding or action");
	local.Traverse(sgre::scene::MotionFrameTraversal::Forward, 1);
	Require(local.State(0).CurrentIndex == 1 && local.Samples()[0].Source == "following source" && trace.Bindings.size() == 2 && trace.Actions.size() == 2, "crossing selects the actual following static frame");

	timeline.Layers[0].Properties.emplace_back("groundCorrection", Value(true));
	RequireFailure([&] {
		sgre::scene::MotionLocalFrames unsupported(timeline, prepared, resource, OwnerServices(trace));
	},
	    "ground correction retains its required unrecovered owner boundary");
	timeline.Layers[0].Properties.clear();
	timeline.Variables = Value(sgre::resource::PsbArray{Value(std::int64_t{1})});
	RequireFailure([&] {
		sgre::scene::MotionLocalFrames unsupported(timeline, prepared, resource, OwnerServices(trace));
	},
	    "variable preparation remains explicitly unsupported");
}

class FixtureReader {
public:
	explicit FixtureReader(const char* path) : m_Stream(path, std::ios::binary) {
		Require(m_Stream.is_open(), "cannot open selected tween fixture");
	}

	std::uint8_t Byte() {
		const auto value = m_Stream.get();
		Require(value != std::char_traits<char>::eof(), "selected tween fixture ended unexpectedly");

		return static_cast<std::uint8_t>(value);
	}

	std::uint32_t Word() {
		std::uint32_t value = 0;
		for (std::uint32_t index = 0; index < 4; ++index) {
			value |= static_cast<std::uint32_t>(Byte()) << (index * 8);
		}

		return value;
	}

	float Real() {
		return std::bit_cast<float>(Word());
	}

	void End() {
		Require(m_Stream.get() == std::char_traits<char>::eof(), "selected tween fixture has trailing bytes");
	}

private:
	std::ifstream m_Stream;
};

static void ReadAttributes(FixtureReader& reader, sgre::scene::MotionLayerAttributes& attributes) {
	for (auto& value : attributes.Coordinate) {
		value = reader.Real();
	}

	for (auto& value : attributes.Colors) {
		value = reader.Word();
	}

	attributes.Opacity = reader.Word();
	attributes.Angle = reader.Real();
	attributes.ZoomX = reader.Real();
	attributes.ZoomY = reader.Real();
	attributes.ShearX = reader.Real();
	attributes.ShearY = reader.Real();
	attributes.FlipX = reader.Byte() != 0;
	attributes.FlipY = reader.Byte() != 0;
	attributes.UniformColor = reader.Byte() != 0;
	attributes.UsesDefaultColor = reader.Byte() != 0;
	attributes.ModulationColor = reader.Word();
}

static void ReplayNative(const char* path) {
	FixtureReader reader(path);
	Require(reader.Word() == 0x3157544d && reader.Word() == 768, "selected tween fixture header differs");
	const auto resource = CurveResource();
	for (std::size_t index = 0; index < 768; ++index) {
		const auto position = reader.Real();
		const auto interval = reader.Word();
		sgre::scene::PreparedMotionLayer layer;
		layer.CoordinatePlane = reader.Byte();
		layer.Rotation = static_cast<sgre::resource::RotationTransition>(reader.Byte());
		layer.HasColorModifier = reader.Byte() != 0;
		const auto curve_mask = reader.Byte();
		const auto next_hidden = reader.Byte() != 0;
		layer.Frames.resize(2);
		layer.Frames[0].Hidden = false;
		layer.Frames[0].Tween = true;
		layer.Frames[1].Time = interval == 0 ? 1 : 12;
		layer.Frames[1].Hidden = next_hidden;
		for (auto& frame : layer.Frames) {
			ReadAttributes(reader, frame.Attributes);
		}

		layer.Frames[0].Attributes.Interval = interval;
		for (std::size_t slot = 0; slot < 5; ++slot) {
			if ((curve_mask & (std::uint32_t{1} << slot)) != 0) {
				layer.Frames[0].Attributes.EasingIndices[slot] = 0;
			}
		}

		const auto sample = sgre::scene::EvaluateSelectedMotionLayer(layer, resource, position, 0);
		const std::array<std::uint32_t, 14> actual = {std::bit_cast<std::uint32_t>(sample.Coordinate[0]), std::bit_cast<std::uint32_t>(sample.Coordinate[1]), std::bit_cast<std::uint32_t>(sample.Coordinate[2]), sample.Colors[0], sample.Colors[1], sample.Colors[2], sample.Colors[3], sample.Opacity, std::bit_cast<std::uint32_t>(sample.Angle), std::bit_cast<std::uint32_t>(sample.ZoomX), std::bit_cast<std::uint32_t>(sample.ZoomY), std::bit_cast<std::uint32_t>(sample.ShearX), std::bit_cast<std::uint32_t>(sample.ShearY), sample.ModulationColor};
		for (std::size_t field = 0; field < actual.size(); ++field) {
			const auto expected = reader.Word();
			if (field != 13 || layer.HasColorModifier) {
				Require(actual[field] == expected, "native selected tween differs at case " + std::to_string(index) + ", field " + std::to_string(field));
			}
		}

		const std::array<bool, 4> flags = {sample.FlipX, sample.FlipY, sample.UniformColor, sample.UsesDefaultColor};
		for (const auto flag : flags) {
			Require(flag == (reader.Byte() != 0), "native selected tween flag differs at case " + std::to_string(index));
		}
	}

	reader.End();
	std::cout << "native selected tween replay: 768 cases passed\n";
}

int main(int argc, char** argv) {
	try {
		Require(argc == 1 || argc == 2, "usage: motion_tween_frames_tests [native-fixture]");
		TestSelectedEpsilonAndColorCurve();
		TestExactSelectedDuplicate();
		TestResourceBackedSelectedOwner();
		if (argc == 2) {
			ReplayNative(argv[1]);
		}

		std::cout << "motion selected tween tests passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
