#include "sgre/scene/motion_layer_composition.hpp"
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
static void RequireFailure(Operation operation) {
	try {
		operation();
	} catch (const std::runtime_error&) {
		return;
	}

	throw std::runtime_error("cached frame owner accepted an unsupported phase or input");
}

template <typename Type>
static sgre::resource::PsbValue Value(Type value) {
	sgre::resource::PsbValue result;
	result.Data = std::move(value);

	return result;
}

static sgre::resource::MotionFrame Frame(float time, std::uint32_t type, const std::array<float, 3>& coordinate, std::uint32_t opacity) {
	sgre::resource::MotionFrame frame;
	frame.Time = time;
	frame.Type = type;
	sgre::resource::PsbArray coordinates;
	for (const auto component : coordinate) {
		coordinates.push_back(Value(static_cast<double>(component)));
	}

	frame.Content = Value(sgre::resource::PsbObject{{"mask", Value(std::int64_t{0x402})}, {"src", Value(std::string{"src"})}, {"icon", Value(std::string{"icon"})}, {"coord", Value(std::move(coordinates))}, {"opa", Value(static_cast<std::int64_t>(opacity))}});

	return frame;
}

static sgre::resource::MotionTimeline Timeline(const std::vector<sgre::resource::MotionFrame>& frames) {
	sgre::resource::MotionTimeline timeline;
	timeline.LastTime = frames.back().Time;
	sgre::resource::MotionLayer layer;
	layer.Label = "label";
	layer.TransformOrder = {0, 3, 2, 1};
	layer.Frames = frames;
	timeline.Layers.push_back(std::move(layer));
	sgre::resource::MotionFrame priority;
	priority.Type = 1;
	priority.Content = Value(sgre::resource::PsbArray{Value(std::int64_t{0})});
	timeline.Priority.push_back(priority);
	priority.Time = timeline.LastTime;
	priority.Content.Data = std::monostate{};
	timeline.Priority.push_back(std::move(priority));

	return timeline;
}

struct Trace {
	std::vector<std::size_t> Bindings;
	std::vector<std::string> Actions;
};

static sgre::scene::MotionLocalFrameServices Services(Trace& trace, const sgre::scene::PreparedMotionTimeline& prepared) {
	sgre::scene::MotionLocalFrameServices services;
	services.BindSource = [&trace, &prepared](std::uint32_t layer, const auto& current, const auto& following) {
		Require(layer < prepared.Layers.size(), "binding layer must belong to actual selected preparation");
		const auto& frames = prepared.Layers[layer].Frames;
		for (std::size_t index = 0; index + 1 < frames.size(); ++index) {
			if (&frames[index] == &current) {
				Require(&frames[index + 1] == &following, "binding must borrow its real adjacent prepared pair");
				trace.Bindings.push_back(index);

				return;
			}
		}

		throw std::runtime_error("source binding borrowed a foreign frame");
	};
	services.EnqueueAction = [&trace](std::string_view label, std::string_view action) {
		trace.Actions.emplace_back(std::string(label) + ":" + std::string(action));
	};

	return services;
}

static sgre::resource::MotionTimeline DefaultTimeline() {
	return Timeline({Frame(0, 3, {1000000, 0, 0}, 0), Frame(1, 2, {0, 0, 0}, 200), Frame(2, 0, {0, 0, 0}, 0)});
}

static void TestCacheBranches() {
	sgre::resource::MotionResource resource;
	const auto timeline = DefaultTimeline();
	const auto prepared = sgre::scene::PrepareMotionTimeline(timeline, resource);
	const auto& layer = prepared.Layers[0];
	sgre::scene::MotionLayerEvaluationCache cache;
	RequireFailure([&] {
		static_cast<void>(sgre::scene::EvaluateCachedMotionLayer(layer, resource, 0, 0, false, false, cache));
	});
	Require(sgre::scene::EvaluateCachedMotionLayer(layer, resource, 0, 0, false, true, cache), "initial genuine dirty selection must copy its local pose");
	const auto epsilon = std::numeric_limits<float>::epsilon();
	Require(!sgre::scene::EvaluateCachedMotionLayer(layer, resource, epsilon / 2, 0, false, false, cache) && cache.Ratio == epsilon / 2 && cache.Sample->Coordinate[0] == 1000000, "tiny suppression updates ratio while retaining the exact starting pose");
	Require(!sgre::scene::EvaluateCachedMotionLayer(layer, resource, epsilon / 4, 0, false, false, cache) && cache.Ratio == epsilon / 4, "a subsequent tiny ratio updates even when no pose copy occurs");
	Require(!sgre::scene::EvaluateCachedMotionLayer(layer, resource, epsilon, 0, false, false, cache) && cache.Ratio == epsilon / 4, "ordinary suppression compares against cached ratio and leaves that cache unchanged");
	Require(sgre::scene::EvaluateCachedMotionLayer(layer, resource, epsilon * 1.5f, 0, false, false, cache), "change at or above epsilon must perform actual interpolation");
	Require(sgre::scene::EvaluateCachedMotionLayer(layer, resource, 0.5f, 0, false, false, cache), "ordinary ratio change must update its local pose");
	const auto coordinate = cache.Sample->Coordinate;
	Require(!sgre::scene::EvaluateCachedMotionLayer(layer, resource, 0.5f + epsilon / 2, 0, false, false, cache) && cache.Ratio == 0.5f && cache.Sample->Coordinate == coordinate, "non-tiny near-equal ratios retain both cache and pose");
	Require(sgre::scene::EvaluateCachedMotionLayer(layer, resource, 0.5f + epsilon / 2, 0, true, false, cache) && cache.Sample->Coordinate != coordinate, "actual force bypasses near-equal ratio suppression");
	const auto ratio = cache.Ratio;
	Require(sgre::scene::EvaluateCachedMotionLayer(layer, resource, 1.5f, 1, false, true, cache) && cache.Ratio == ratio, "static pair copying leaves the preceding tween ratio intact");
}

static void TestNormalOwnerAndCompletion() {
	sgre::resource::MotionResource resource;
	const auto timeline = DefaultTimeline();
	const auto prepared = sgre::scene::PrepareMotionTimeline(timeline, resource);
	Trace trace;
	sgre::scene::MotionLocalFrames local(timeline, prepared, resource, Services(trace, prepared));
	sgre::scene::MotionRootScene root;
	root.SetVisible(true);
	sgre::scene::MotionLayerComposition composition(prepared, root, false);
	RequireFailure([&] {
		composition.Compose(local, false);
	});
	Require(composition.Layers().empty(), "missing selected preparation must publish no partial numeric frame");
	local.Select(sgre::scene::MotionFrameTraversal::Seek, 0.5f);
	Require(local.State(0).FrameDirty && local.Samples().empty(), "selection marks actual dirty state without publishing a prematurely evaluated pose");
	composition.Compose(local, false);
	Require(local.State(0).FrameDirty && composition.Layers()[0].Hierarchy.Attributes.Coordinate[0] == 500000, "numeric composition retains frame dirty until required later producer phases complete");
	local.CompleteComposition();
	Require(!local.State(0).FrameDirty && local.Samples().size() == 1, "explicit complete composition clears selected frame dirty and publishes cached samples");
	const auto epsilon = std::numeric_limits<float>::epsilon();
	local.Select(sgre::scene::MotionFrameTraversal::Forward, 0.5f + epsilon / 2);
	composition.Compose(local, false);
	Require(local.Evaluation(0).Ratio == 0.5f && composition.Layers()[0].Displacement == std::array<float, 3>{0, 0, 0} && trace.Bindings.size() == 1, "normal unchanged ratio avoids recomposition and source rebinding");
	local.CompleteComposition();
	root.SetDepth(1);
	composition.Compose(local, false);
	Require(local.Evaluation(0).Ratio == 0.5f + epsilon / 2 && composition.Layers()[0].Hierarchy.Attributes.Coordinate[0] != 500000, "actual root change forces the selected near-equal local interval");
	local.CompleteComposition();
	RequireFailure([&] {
		local.Select(sgre::scene::MotionFrameTraversal::Seek, -1);
	});
	Require(local.Samples().empty(), "failed selection invalidates selected sample publication");
	RequireFailure([&] {
		composition.Compose(local, false);
	});
	Require(composition.Layers().empty(), "failed normal composition invalidates scene publication");
	local.Select(sgre::scene::MotionFrameTraversal::Seek, 0);
	composition.Compose(local, true);
	local.CompleteComposition();
	Require(composition.Layers().size() == 1, "valid actual seek restores normal composition after failure");
}

static void TestSkippedImmediateParentForce() {
	sgre::resource::MotionResource resource;
	auto timeline = DefaultTimeline();
	auto child = timeline.Layers[0];
	child.Label = "child";
	timeline.Layers[0].InheritMask = 0x400000;
	timeline.Layers[0].Frames[0].Type = 2;
	timeline.Layers[0].Children.push_back(std::move(child));
	timeline.Priority[0].Content = Value(sgre::resource::PsbArray{Value(std::int64_t{0}), Value(std::int64_t{1})});
	const auto prepared = sgre::scene::PrepareMotionTimeline(timeline, resource);
	Trace trace;
	sgre::scene::MotionLocalFrames local(timeline, prepared, resource, Services(trace, prepared));
	sgre::scene::MotionRootScene root;
	root.SetVisible(true);
	sgre::scene::MotionLayerComposition composition(prepared, root, false);
	local.Select(sgre::scene::MotionFrameTraversal::Seek, 0.5f);
	composition.Compose(local, false);
	local.CompleteComposition();
	Require(!composition.Layers()[1].EffectiveParent.has_value(), "actual skip mask selects the root for numeric inheritance");
	sgre::scene::MotionLayerOverrides overrides;
	overrides.Coordinate = {10, 0, 0};
	composition.SetOverrides(0, overrides);
	const auto position = 0.5f + std::numeric_limits<float>::epsilon() / 2;
	local.Select(sgre::scene::MotionFrameTraversal::Forward, position);
	composition.Compose(local, false);
	Require(local.Evaluation(1).Ratio == position && composition.Layers()[1].Hierarchy.Attributes.Coordinate[0] != 500000, "changed immediate parent forces child even when skipped for numeric inheritance");
	local.CompleteComposition();
}

class Reader {
public:
	explicit Reader(const char* path) : m_Input(path, std::ios::binary) {
		Require(m_Input.is_open(), "cannot open native cached-frame fixture");
	}
	std::uint8_t Byte() {
		const auto value = m_Input.get();
		Require(value != std::char_traits<char>::eof(), "native cached-frame fixture ended unexpectedly");

		return static_cast<std::uint8_t>(value);
	}
	std::uint32_t Word() {
		std::uint32_t result = 0;
		for (std::uint32_t index = 0; index < 4; ++index) {
			result |= static_cast<std::uint32_t>(Byte()) << (index * 8);
		}

		return result;
	}
	float Real() {
		return std::bit_cast<float>(Word());
	}
	std::array<float, 3> Coordinate() {
		std::array<float, 3> result{};
		for (auto& component : result) {
			component = Real();
		}

		return result;
	}
	void End() {
		Require(m_Input.get() == std::char_traits<char>::eof(), "native cached-frame fixture has trailing bytes");
	}

private:
	std::ifstream m_Input;
};

static void RequireReal(float actual, Reader& reader, std::string_view context) {
	Require(std::bit_cast<std::uint32_t>(actual) == reader.Word(), context);
}

static void ReplayNative(const char* path) {
	Reader reader(path);
	Require(reader.Word() == 0x3146434d && reader.Word() == 64 && reader.Word() == 24 && reader.Word() == 6, "cached-frame fixture header differs");
	sgre::resource::MotionResource resource;
	for (std::size_t case_index = 0; case_index < 64; ++case_index) {
		std::vector<sgre::resource::MotionFrame> frames;
		for (std::size_t index = 0; index < 6; ++index) {
			const auto time = reader.Real();
			const auto type = reader.Byte();
			const auto coordinate = reader.Coordinate();
			const auto opacity = reader.Word();
			frames.push_back(Frame(time, type, coordinate, opacity));
		}

		const auto timeline = Timeline(frames);
		const auto prepared = sgre::scene::PrepareMotionTimeline(timeline, resource);
		Trace trace;
		sgre::scene::MotionLocalFrames local(timeline, prepared, resource, Services(trace, prepared));
		sgre::scene::MotionRootScene root;
		root.SetVisible(true);
		sgre::scene::MotionLayerComposition composition(prepared, root, false);
		sgre::scene::MotionLayerEvaluationCache mirror;
		for (std::size_t step = 0; step < 24; ++step) {
			const auto mode = static_cast<sgre::scene::MotionFrameTraversal>(reader.Byte());
			const auto position = reader.Real();
			const auto assigned = reader.Byte() != 0;
			const auto force_all = reader.Byte() != 0;
			const auto root_dirty = reader.Byte() != 0;
			const auto root_coordinate = reader.Coordinate();
			const auto override_dirty = reader.Byte() != 0;
			const auto override_coordinate = reader.Coordinate();
			const auto expected_index = reader.Word();
			const auto native_slot = reader.Word();
			const auto expected_changed = reader.Byte() != 0;
			const auto native_force = reader.Byte() != 0;
			const auto expected_frame_dirty = reader.Byte() != 0;
			const auto binding_count = reader.Byte();
			Require(native_slot < 2, "native selected decoded slot must remain binary");
			trace.Bindings.clear();
			local.Select(mode, position);
			Require(local.State(0).CurrentIndex == expected_index && local.State(0).FrameDirty == expected_frame_dirty && trace.Bindings.size() == binding_count && trace.Actions.empty(), "native cached frame cursor/dirty/binding phase differs");
			for (const auto binding : trace.Bindings) {
				Require(binding == reader.Byte(), "native selected source binding index differs");
			}

			Require(sgre::scene::EvaluateCachedMotionLayer(prepared.Layers[0], resource, position, expected_index, native_force, expected_frame_dirty, mirror) == expected_changed, "native force-false local return differs");
			if (root_dirty) {
				root.SetCoordinate3(root_coordinate);
			}

			if (override_dirty) {
				sgre::scene::MotionLayerOverrides overrides;
				overrides.Coordinate = override_coordinate;
				composition.SetOverrides(0, overrides);
			}

			composition.Compose(local, assigned, force_all);
			const auto context = "native cached numeric output differs at case " + std::to_string(case_index) + ", step " + std::to_string(step);
			const auto& output = composition.Layers()[0];
			RequireReal(local.Evaluation(0).Ratio, reader, context);
			Require(local.Evaluation(0).Ratio == mirror.Ratio, "native local ratio gate differs from its actual caller force");
			for (const auto component : output.Hierarchy.Attributes.Coordinate) {
				RequireReal(component, reader, context);
			}

			for (const auto component : {output.Hierarchy.Attributes.Angle, output.Hierarchy.Attributes.ZoomX, output.Hierarchy.Attributes.ZoomY, output.Hierarchy.Attributes.ShearX, output.Hierarchy.Attributes.ShearY}) {
				RequireReal(component, reader, context);
			}

			Require(output.Hierarchy.Attributes.Opacity == reader.Word(), context);
			for (const auto component : {output.Hierarchy.Transform.XX, output.Hierarchy.Transform.XY, output.Hierarchy.Transform.YX, output.Hierarchy.Transform.YY}) {
				RequireReal(component, reader, context);
			}

			for (const auto component : output.Displacement) {
				RequireReal(component, reader, context);
			}

			Require(output.Active == (reader.Byte() != 0) && output.Visible == (reader.Byte() != 0) && output.Hierarchy.ModulationColor == reader.Word(), context);
			local.CompleteComposition();
			Require(!local.State(0).FrameDirty && local.Samples().size() == 1, "actual complete composition must acknowledge frame dirty");
		}
	}

	reader.End();
	std::cout << "native cached-frame replay: 1,536 transitions passed\n";
}

int main(int argc, char** argv) {
	try {
		Require(argc == 1 || argc == 2, "usage: motion_cached_frames_tests [native-fixture]");
		TestCacheBranches();
		TestNormalOwnerAndCompletion();
		TestSkippedImmediateParentForce();
		if (argc == 2) {
			ReplayNative(argv[1]);
		}

		std::cout << "motion cached-frame tests passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
