#include "sgre/scene/motion_child_root.hpp"

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

	throw std::runtime_error("unsupported child direction phase was accepted");
}

static sgre::resource::MotionResource Resource() {
	sgre::resource::MotionResource resource;
	sgre::resource::EasingCurve curve;
	curve.Segments.push_back({{0, 0, 0}, {0.25f, 0.125f, 0}, {0.75f, 0.875f, 0}, {1, 1, 0}});
	resource.Easing.push_back(std::move(curve));

	return resource;
}

static sgre::scene::PreparedMotionTimeline Preparation() {
	sgre::scene::PreparedMotionTimeline timeline;
	timeline.Layers.resize(2);
	auto& layer = timeline.Layers[0];
	layer.Type = 3;
	layer.Frames.resize(3);
	for (std::size_t index = 0; index < layer.Frames.size(); ++index) {
		auto& frame = layer.Frames[index];
		frame.Hidden = false;
		frame.Tween = true;
		frame.Time = static_cast<float>(index * 8);
		frame.Attributes.Source = "a";
		frame.Attributes.Icon = "idle";
		frame.Attributes.Coordinate = {static_cast<float>(index * 8), static_cast<float>(index * 8), 0};
	}

	return timeline;
}

static void SetTarget(sgre::resource::MotionTimeline& timeline, std::int64_t target) {
	sgre::resource::PsbValue value;
	value.Data = target;
	timeline.LayerIndexMap.Data = sgre::resource::PsbObject{{"target", std::move(value)}};
}

static void TestInitialDerivativeAndCurrentTargets() {
	const auto resource = Resource();
	auto preparation = Preparation();
	sgre::resource::MotionTimeline timeline;
	sgre::scene::MotionRootScene root;
	root.SetCoordinate3({10, 20, 30});
	root.Compose(true);
	std::array<sgre::scene::MotionComposedLayer, 2> layers;
	layers[0].Hierarchy.Attributes.Coordinate = {2, 3, 4};
	layers[0].Hierarchy.Attributes.Angle = 20;
	layers[0].Displacement = {0, -2, 0};
	layers[1].Hierarchy.Attributes.Coordinate = {2, 9, 4};
	layers[0].Active = true;
	sgre::scene::MotionTimelineClockState clock;
	sgre::scene::MotionChildFrameControl current;
	current.DirectionMode = 2;
	sgre::scene::MotionChildFrameControl following;
	sgre::scene::MotionChildDirectionScene scene{timeline, preparation, resource, root, layers, clock};
	sgre::scene::MotionChildDirectionSelection selection{0, 0, current, following, 4};
	Require(clock.InitialCompositionPending && sgre::scene::EvaluateMotionChildDirection(scene, selection) == 45, "native first-composition DT2 uses actual selected coordinate derivative");
	clock.InitialCompositionPending = false;
	Require(sgre::scene::EvaluateMotionChildDirection(scene, selection) == 270, "native later DT2 uses current world displacement rather than a fresh curve sample");
	current.DirectionMode = 3;
	Require(sgre::scene::EvaluateMotionChildDirection(scene, selection) == 45, "DT3 continues actual selected coordinate derivative after initial composition");
	preparation.Layers[0].Frames[1].Hidden = true;
	Require(!sgre::scene::EvaluateMotionChildDirection(scene, selection), "hidden following frame suppresses DT3");
	current.DirectionMode = 4;
	Require(!sgre::scene::EvaluateMotionChildDirection(scene, selection), "absent actual target map preserves missing direction");
	current.DirectionTarget = "target";
	SetTarget(timeline, 1);
	Require(sgre::scene::EvaluateMotionChildDirection(scene, selection) == 90, "DT4 resolves another actual current node from its native map");
	SetTarget(timeline, 0);
	Require(sgre::scene::EvaluateMotionChildDirection(scene, selection) == 0, "DT4 self target follows actual zero-vector atan2");
	SetTarget(timeline, -1);
	const auto root_direction = sgre::scene::EvaluateMotionChildDirection(scene, selection);
	Require(root_direction && *root_direction > 64 && *root_direction < 65, "DT4 root target uses the actual composed root coordinates");
	current.DirectionMode = 1;
	current.DirectionOffset = 400;
	Require(sgre::scene::EvaluateMotionChildDirection(scene, selection) == 420, "DT1 keeps its unwrapped parent-relative result");
	current.DirectionMode = 0;
	Require(!sgre::scene::EvaluateMotionChildDirection(scene, selection), "DT0 retains existing child angle");
}

static void TestRequiredOwnersAndRetainedBits() {
	const auto resource = Resource();
	auto preparation = Preparation();
	sgre::resource::MotionTimeline timeline;
	SetTarget(timeline, -1);
	sgre::scene::MotionRootScene root;
	std::array<sgre::scene::MotionComposedLayer, 2> layers;
	layers[0].Active = true;
	sgre::scene::MotionTimelineClockState clock;
	sgre::scene::MotionChildFrameControl current;
	current.DirectionMode = 4;
	current.DirectionTarget = "target";
	sgre::scene::MotionChildFrameControl following;
	sgre::scene::MotionChildDirectionScene scene{timeline, preparation, resource, root, layers, clock};
	sgre::scene::MotionChildDirectionSelection selection{0, 0, current, following, 4};
	RequireFailure([&] {
		static_cast<void>(sgre::scene::EvaluateMotionChildDirection(scene, selection));
	},
	    "actual current composed root");
	current.DirectionMode = 3;
	preparation.Layers[0].Frames[0].Attributes.CoordinatePath.emplace();
	RequireFailure([&] {
		static_cast<void>(sgre::scene::EvaluateMotionChildDirection(scene, selection));
	},
	    "unverified path owner");
	preparation.Layers[0].Frames[0].Attributes.CoordinatePath.reset();
	selection.CurrentFrameIndex = 2;
	RequireFailure([&] {
		static_cast<void>(sgre::scene::EvaluateMotionChildDirection(scene, selection));
	},
	    "selected current/following");
	selection.CurrentFrameIndex = 0;
	scene.Layers = std::span<const sgre::scene::MotionComposedLayer>(layers).first(1);
	RequireFailure([&] {
		static_cast<void>(sgre::scene::EvaluateMotionChildDirection(scene, selection));
	},
	    "actual ordinary current");
	scene.Layers = layers;
	current.DirectionMode = 1;
	layers[0].Hierarchy.Attributes.Coordinate = {-0.0f, -0.0f, -0.0f};
	layers[0].Hierarchy.Attributes.Angle = -0.0f;
	layers[0].Hierarchy.Attributes.ShearX = -0.0f;
	layers[0].Hierarchy.Attributes.ShearY = -0.0f;
	current.DirectionOffset = -0.0f;
	sgre::scene::MotionRootScene child(sgre::scene::MotionRootMatrixMode::SuppliedByParent);
	sgre::scene::BindMotionChildRoot(child, scene, selection, 0);
	Require(std::bit_cast<std::uint32_t>(child.Overrides().Coordinate[0]) == 0 && std::bit_cast<std::uint32_t>(child.Overrides().Angle) == 0 && std::bit_cast<std::uint32_t>(child.Overrides().ShearX) == 0, "numeric-equal field groups retain native stored positive-zero bits");
	preparation.Layers[0].Frames[0].Attributes.Coordinate = {-0.0f, -0.0f, -0.0f};
	preparation.Layers[0].Frames[1].Attributes.Coordinate = {0, 0, 0};
	preparation.Layers[0].Frames[0].Attributes.EasingIndices[0] = 99;
	const auto equal = sgre::scene::InterpolateMotionChildCoordinates(preparation.Layers[0].Frames[0], preparation.Layers[0].Frames[1], resource, 0.5f, 0);
	Require(std::bit_cast<std::uint32_t>(equal[0]) == 0x80000000, "native whole-equal coordinate shortcut retains current bits and bypasses a curve lookup");
}

static std::uint32_t ReadWord(std::span<const std::uint8_t> bytes, std::size_t offset) {
	if (offset > bytes.size() || 4 > bytes.size() - offset) {
		throw std::runtime_error("truncated native child root word");
	}

	std::uint32_t result = 0;
	for (std::size_t index = 0; index < 4; ++index) {
		result |= static_cast<std::uint32_t>(bytes[offset + index]) << (index * 8);
	}

	return result;
}

static float ReadNumber(std::span<const std::uint8_t> bytes, std::size_t offset) {
	return std::bit_cast<float>(ReadWord(bytes, offset));
}

template <std::size_t Size>
static std::array<std::uint8_t, Size> ReadRecord(std::ifstream& input) {
	std::array<char, Size> raw{};
	input.read(raw.data(), static_cast<std::streamsize>(raw.size()));
	Require(input.gcount() == static_cast<std::streamsize>(raw.size()), "truncated native child root record");
	std::array<std::uint8_t, Size> result{};
	for (std::size_t index = 0; index < Size; ++index) {
		result[index] = static_cast<std::uint8_t>(static_cast<unsigned char>(raw[index]));
	}

	return result;
}

static void RequirePoseWords(const sgre::scene::MotionRootScene& root, std::span<const std::uint8_t> record, bool composed) {
	const auto& overrides = root.Overrides();
	const auto& frame = root.Frame();
	const auto& attributes = frame.Hierarchy.Attributes;
	const auto coordinate = composed ? attributes.Coordinate : overrides.Coordinate;
	const std::array<float, 8> values = composed ? std::array<float, 8>{coordinate[0], coordinate[1], coordinate[2], attributes.Angle, attributes.ZoomX, attributes.ZoomY, attributes.ShearX, attributes.ShearY} : std::array<float, 8>{coordinate[0], coordinate[1], coordinate[2], overrides.Angle, overrides.ZoomX, overrides.ZoomY, overrides.ShearX, overrides.ShearY};
	for (std::size_t index = 0; index < values.size(); ++index) {
		Require(std::bit_cast<std::uint32_t>(values[index]) == ReadWord(record, index * 4), "child root scalar word differs from actual native binding/composition");
	}

	Require((composed ? attributes.Opacity : overrides.Opacity) == ReadWord(record, 32), "child root opacity differs from native");
	Require(root.ModulationColor() == ReadWord(record, 52) && (composed ? !attributes.Hidden : overrides.Visible) == (record[56] != 0) && (composed ? attributes.FlipX : overrides.FlipX) == (record[57] != 0) && (composed ? attributes.FlipY : overrides.FlipY) == (record[58] != 0), "child root modulation/visible/flip fields differ from native");
	if (composed) {
		const std::array<float, 4> matrix{frame.Hierarchy.Transform.XX, frame.Hierarchy.Transform.XY, frame.Hierarchy.Transform.YX, frame.Hierarchy.Transform.YY};
		for (std::size_t index = 0; index < matrix.size(); ++index) {
			Require(std::bit_cast<std::uint32_t>(matrix[index]) == ReadWord(record, 36 + index * 4), "child root supplied/rotated matrix word differs from actual native");
		}

		Require(frame.Hierarchy.ModulationColor == ReadWord(record, 52) && frame.Composed, "child root must publish actual composed color after the mandatory root phase");
		for (std::size_t index = 0; index < 3; ++index) {
			Require(std::bit_cast<std::uint32_t>(frame.Displacement[index]) == ReadWord(record, 59 + index * 4), "child root displacement word differs from actual native composition");
		}
	}
}

static void ReplayNative(const char* path) {
	std::ifstream input(path, std::ios::binary);
	Require(input.is_open(), "cannot open native child direction phase fixture");
	const auto header = ReadRecord<12>(input);
	Require(header[0] == 'M' && header[1] == 'C' && header[2] == 'D' && header[3] == 'P', "invalid child direction phase fixture signature");
	const auto cases = ReadWord(header, 4);
	const auto steps = ReadWord(header, 8);
	Require(cases > 0 && cases <= 256 && steps > 0 && steps <= 4, "child direction phase native work exceeds its bound");
	const auto resource = Resource();
	for (std::uint32_t index = 0; index < cases; ++index) {
		sgre::scene::MotionRootScene child(sgre::scene::MotionRootMatrixMode::SuppliedByParent);
		for (std::uint32_t step = 0; step < steps; ++step) {
			try {
				const auto record = ReadRecord<301>(input);
				Require(record[0] <= 4 && record[1] <= 4 && record[7] <= 1, "native child direction flags exceed selected domain");
				auto preparation = Preparation();
				auto& layer = preparation.Layers[0];
				layer.CoordinatePlane = record[5];
				const auto current_index = record[7];
				auto& current_frame = layer.Frames[current_index];
				auto& next_frame = layer.Frames[current_index + 1u];
				current_frame.Tween = record[3] != 0;
				next_frame.Hidden = record[4] != 0;
				current_frame.Time = ReadNumber(record, 137);
				next_frame.Time = ReadNumber(record, 141);
				current_frame.Attributes.Coordinate = {ReadNumber(record, 97), ReadNumber(record, 101), ReadNumber(record, 105)};
				next_frame.Attributes.Coordinate = {ReadNumber(record, 109), ReadNumber(record, 113), ReadNumber(record, 117)};
				current_frame.Attributes.OriginX = ReadNumber(record, 121);
				current_frame.Attributes.OriginY = ReadNumber(record, 125);
				if (record[8] != 0) {
					current_frame.Attributes.EasingIndices[0] = 0;
				}

				if (record[9] != 0) {
					current_frame.Attributes.EasingIndices[2] = 0;
				}

				std::array<sgre::scene::MotionComposedLayer, 2> layers;
				auto& parent = layers[0];
				parent.Active = index % 2 != 0;
				parent.Visible = !parent.Active;
				auto& attributes = parent.Hierarchy.Attributes;
				attributes.Coordinate = {ReadNumber(record, 13), ReadNumber(record, 17), ReadNumber(record, 21)};
				attributes.Angle = ReadNumber(record, 25);
				attributes.ZoomX = ReadNumber(record, 29);
				attributes.ZoomY = ReadNumber(record, 33);
				attributes.ShearX = ReadNumber(record, 37);
				attributes.ShearY = ReadNumber(record, 41);
				attributes.FlipX = (index + step) % 2 != 0;
				attributes.FlipY = (index / 2 + step) % 2 != 0;
				attributes.Opacity = ReadWord(record, 149);
				attributes.Colors[0] = ReadWord(record, 157);
				parent.Hierarchy.ModulationColor = ReadWord(record, 153);
				parent.Hierarchy.Transform.XX = ReadNumber(record, 45);
				parent.Hierarchy.Transform.XY = ReadNumber(record, 49);
				parent.Hierarchy.Transform.YX = ReadNumber(record, 53);
				parent.Hierarchy.Transform.YY = ReadNumber(record, 57);
				parent.Displacement = {ReadNumber(record, 61), ReadNumber(record, 65), ReadNumber(record, 69)};
				layers[1].Hierarchy.Attributes.Coordinate = {ReadNumber(record, 85), ReadNumber(record, 89), ReadNumber(record, 93)};
				sgre::scene::MotionRootScene root;
				root.SetCoordinate3({ReadNumber(record, 73), ReadNumber(record, 77), ReadNumber(record, 81)});
				root.Compose(true);
				sgre::resource::MotionTimeline timeline;
				const auto map_code = std::bit_cast<std::int8_t>(record[12]);
				if (map_code != -3) {
					SetTarget(timeline, map_code < -1 ? 1 : map_code);
				}

				sgre::scene::MotionTimelineClockState clock;
				clock.InitialCompositionPending = record[11] != 0;
				sgre::scene::MotionChildFrameControl current;
				current.DirectionMode = record[0];
				current.CompleteDirection = record[2] != 0;
				current.DirectionOffset = ReadNumber(record, 129);
				current.DirectionTarget = map_code == -2 ? "absent" : "target";
				sgre::scene::MotionChildFrameControl following;
				following.DirectionMode = record[1];
				following.DirectionOffset = ReadNumber(record, 133);
				sgre::scene::MotionChildDirectionScene scene{timeline, preparation, resource, root, layers, clock};
				sgre::scene::MotionChildDirectionSelection selection{0, current_index, current, following, ReadNumber(record, 145)};
				const auto direction = sgre::scene::EvaluateMotionChildDirection(scene, selection);
				Require(direction.has_value() == (record[165] != 0), "selected direction presence differs from actual native");
				if (direction) {
					Require(std::bit_cast<std::uint32_t>(*direction) == ReadWord(record, 166), "selected direction word differs from actual native");
				}

				sgre::scene::BindMotionChildRoot(child, scene, selection, std::bit_cast<std::int32_t>(ReadWord(record, 161)));
				RequirePoseWords(child, std::span<const std::uint8_t>(record).subspan(170, 59), false);
				child.Compose(record[10] != 0);
				RequirePoseWords(child, std::span<const std::uint8_t>(record).subspan(229, 71), true);
				Require(record[300] == 0, "native first-composition flag must clear at actual composition completion");
			} catch (const std::exception& error) {
				throw std::runtime_error("native child phase case " + std::to_string(index) + " step " + std::to_string(step) + ": " + error.what());
			}
		}
	}

	Require(input.peek() == std::char_traits<char>::eof(), "native child phase fixture has trailing data");
}

int main(int argc, char** argv) {
	try {
		Require(argc == 1 || argc == 2, "usage: motion_child_direction_phase_tests [native-fixture]");
		TestInitialDerivativeAndCurrentTargets();
		TestRequiredOwnersAndRetainedBits();
		if (argc == 2) {
			ReplayNative(argv[1]);
		}

		std::cout << "motion child direction phase tests passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
