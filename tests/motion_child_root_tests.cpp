#include "sgre/scene/motion_child_root.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

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

	throw std::runtime_error("unsupported child root operation was accepted");
}

static sgre::scene::PreparedMotionLayer ChildLayer() {
	sgre::scene::PreparedMotionLayer layer;
	layer.Type = 3;

	return layer;
}

static sgre::scene::PreparedMotionFrame ChildFrame() {
	sgre::scene::PreparedMotionFrame frame;
	frame.Hidden = false;
	frame.Attributes.Source = "actual-character";
	frame.Attributes.Icon = "actual-timeline";
	frame.Attributes.OriginX = 1;
	frame.Attributes.OriginY = 2;

	return frame;
}

static sgre::scene::MotionComposedLayer Parent() {
	sgre::scene::MotionComposedLayer parent;
	parent.Active = true;
	parent.Visible = false;
	parent.FrameHidden = false;
	auto& sample = parent.Hierarchy.Attributes;
	sample.Hidden = false;
	sample.Coordinate = {10, 20, 30};
	sample.Angle = 40;
	sample.ZoomX = 0.5f;
	sample.ZoomY = 0.75f;
	sample.ShearX = 0.25f;
	sample.ShearY = 0.5f;
	sample.Opacity = 129;
	sample.FlipY = true;
	sample.Colors.fill(0x808080ff);
	parent.Hierarchy.ModulationColor = 0x7f3f1fff;
	parent.Hierarchy.Transform.XX = 2;
	parent.Hierarchy.Transform.XY = 3;
	parent.Hierarchy.Transform.YX = 4;
	parent.Hierarchy.Transform.YY = 5;

	return parent;
}

static void RequireSameMatrix(const sgre::scene::MotionLinearTransform& actual, const sgre::scene::MotionLinearTransform& expected) {
	Require(std::bit_cast<std::uint32_t>(actual.XX) == std::bit_cast<std::uint32_t>(expected.XX) && std::bit_cast<std::uint32_t>(actual.XY) == std::bit_cast<std::uint32_t>(expected.XY) && std::bit_cast<std::uint32_t>(actual.YX) == std::bit_cast<std::uint32_t>(expected.YX) && std::bit_cast<std::uint32_t>(actual.YY) == std::bit_cast<std::uint32_t>(expected.YY), "child root supplied matrix words differ");
}

static void TestParentMatrixAndOrigin() {
	auto layer = ChildLayer();
	const auto frame = ChildFrame();
	auto parent = Parent();
	sgre::scene::MotionChildFrameControl control;
	sgre::scene::MotionRootScene child(sgre::scene::MotionRootMatrixMode::SuppliedByParent);
	sgre::scene::BindMotionChildRoot(child, layer, frame, parent, control, 0);
	Require(child.Overrides().Coordinate == std::array<float, 3>{2, 6, 30} && child.Overrides().Visible && child.Overrides().Angle == 0 && child.Overrides().FlipY && child.Overrides().Dirty, "child XY origin uses current frame and parent matrix while visibility uses actual Active and DT0 retains angle");
	Require(child.Overrides().ZoomX == 0.5f && child.Overrides().ZoomY == 0.75f && child.Overrides().ShearX == 0.25f && child.Overrides().ShearY == 0.5f && child.Overrides().Opacity == 129, "child pose copies the actual composed parent scalar fields");
	child.Compose(true);
	Require(child.Frame().Composed && child.Frame().Displacement == std::array<float, 3>{0, 0, 0} && !child.Frame().Hierarchy.Attributes.Hidden && child.Frame().Hierarchy.ModulationColor == 0x7f3f1fff && !child.Overrides().Dirty, "actual child root composition consumes parent pose and assigned displacement");
	RequireSameMatrix(child.Frame().Hierarchy.Transform, parent.Hierarchy.Transform);
	layer.CoordinatePlane = 1;
	control.DirectionMode = 1;
	sgre::scene::BindMotionChildRoot(child, layer, frame, parent, control, 0);
	Require(child.Overrides().Coordinate == std::array<float, 3>{2, 20, 16} && child.Overrides().Angle == 40, "child XZ origin targets Z and DT1 offsets the parent angle");
	child.Compose(false);
	Require(child.Frame().Displacement == std::array<float, 3>{0, 14, -14}, "child root displacement uses previous evaluated coordinates when no explicit tick was assigned");
	RequireSameMatrix(child.Frame().Hierarchy.Transform, parent.Hierarchy.Transform);
	parent.Active = false;
	parent.Visible = true;
	control.DirectionMode = 0;
	parent.Hierarchy.Attributes.Angle = 100;
	sgre::scene::BindMotionChildRoot(child, layer, frame, parent, control, 0);
	Require(child.Overrides().Angle == 40 && !child.Overrides().Visible, "DT0 retains old child angle while child root visibility follows Active independently of Visible");
	child.Compose(false);
	Require(child.Frame().Hierarchy.Attributes.Hidden, "inactive child root composes hidden");
	RequireSameMatrix(child.Frame().Hierarchy.Transform, parent.Hierarchy.Transform);
}

static void TestDirectionAndColorComparison() {
	const auto layer = ChildLayer();
	auto frame = ChildFrame();
	frame.Attributes.OriginX = 0;
	frame.Attributes.OriginY = 0;
	auto parent = Parent();
	sgre::scene::MotionChildFrameControl control;
	control.DirectionMode = 1;
	control.DirectionOffset = 45;
	sgre::scene::MotionRootScene child(sgre::scene::MotionRootMatrixMode::SuppliedByParent);
	sgre::scene::BindMotionChildRoot(child, layer, frame, parent, control, 0);
	child.Compose(true);
	const auto radians = -45.0f * 0.0174532924f;
	const auto sine = std::sin(radians);
	const auto cosine = std::cos(radians);
	const auto& matrix = child.Frame().Hierarchy.Transform;
	Require(child.Overrides().Angle == 85 && std::abs(matrix.XX - (3 * sine + 2 * cosine)) < 0.000001f && std::abs(matrix.XY - (2 * -sine + 3 * cosine)) < 0.000001f && std::abs(matrix.YX - (5 * sine + 4 * cosine)) < 0.000001f && std::abs(matrix.YY - (4 * -sine + 5 * cosine)) < 0.000001f, "DT1 rotates the supplied matrix with direction difference and flip parity");
	const auto retained = child.ModulationColor();
	parent.Hierarchy.ModulationColor = 0x12345678;
	parent.Hierarchy.Attributes.Colors[0] = 0x808080ff;
	sgre::scene::BindMotionChildRoot(child, layer, frame, parent, control, std::bit_cast<std::int32_t>(std::uint32_t{0x12345678}));
	Require(child.ModulationColor() == retained, "native child modulation compares the actual color-filter word and can retain the previous color");
	sgre::scene::BindMotionChildRoot(child, layer, frame, parent, control, 0);
	Require(child.ModulationColor() == 0x12345678, "unequal color-filter word writes the combined child modulation");
	parent.Hierarchy.ModulationColor = 0x404040ff;
	parent.Hierarchy.Attributes.Colors[0] = 0x40404080;
	sgre::scene::BindMotionChildRoot(child, layer, frame, parent, control, 0);
	Require(child.ModulationColor() == 0x20202080, "child modulation uses native RGB seven-bit product and alpha division");
}

static void TestRequiredOwnerAndUnsupportedBoundaries() {
	auto layer = ChildLayer();
	auto frame = ChildFrame();
	auto parent = Parent();
	sgre::scene::MotionChildFrameControl control;
	sgre::scene::MotionRootScene ordinary;
	RequireFailure([&] {
		sgre::scene::BindMotionChildRoot(ordinary, layer, frame, parent, control, 0);
	},
	    "supplied-matrix owner");
	sgre::scene::MotionRootScene child(sgre::scene::MotionRootMatrixMode::SuppliedByParent);
	RequireFailure([&] {
		child.Compose(false);
	},
	    "actual parent pose");
	Require(!child.Frame().Composed, "unbound child cannot publish a root frame");
	control.DirectionMode = 2;
	RequireFailure([&] {
		sgre::scene::BindMotionChildRoot(child, layer, frame, parent, control, 0);
	},
	    "direction");
	control.DirectionMode = 1;
	control.CompleteDirection = true;
	frame.Tween = true;
	RequireFailure([&] {
		sgre::scene::BindMotionChildRoot(child, layer, frame, parent, control, 0);
	},
	    "interpolated selected-control");
	control.CompleteDirection = false;
	frame.Hidden = true;
	RequireFailure([&] {
		sgre::scene::BindMotionChildRoot(child, layer, frame, parent, control, 0);
	},
	    "selected-tree reset");
	frame.Hidden = false;
	frame.Attributes.Source.clear();
	RequireFailure([&] {
		sgre::scene::BindMotionChildRoot(child, layer, frame, parent, control, 0);
	},
	    "selected-tree reset");
	frame = ChildFrame();
	layer.MeshTransform = 1;
	RequireFailure([&] {
		sgre::scene::BindMotionChildRoot(child, layer, frame, parent, control, 0);
	},
	    "ordinary kind3");
	layer.MeshTransform = 0;
	parent.Hierarchy.Transform.XX = std::numeric_limits<float>::infinity();
	RequireFailure([&] {
		sgre::scene::BindMotionChildRoot(child, layer, frame, parent, control, 0);
	},
	    "finite parent");
	parent = Parent();
	parent.Hierarchy.Attributes.Opacity = 256;
	RequireFailure([&] {
		sgre::scene::BindMotionChildRoot(child, layer, frame, parent, control, 0);
	},
	    "opacity");
	Require(!child.Frame().Composed && child.Overrides().Coordinate == std::array<float, 3>{0, 0, 0}, "rejected preparation cannot partially install a child root pose");
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
	Require(input.is_open(), "cannot open native child root fixture");
	const auto header = ReadRecord<12>(input);
	Require(header[0] == 'M' && header[1] == 'C' && header[2] == 'R' && header[3] == 'P', "invalid native child root signature");
	const auto cases = ReadWord(header, 4);
	const auto steps = ReadWord(header, 8);
	Require(cases > 0 && cases <= 128 && steps > 0 && steps <= 4, "native child root work exceeds its bound");
	for (std::uint32_t index = 0; index < cases; ++index) {
		sgre::scene::MotionRootScene child(sgre::scene::MotionRootMatrixMode::SuppliedByParent);
		for (std::uint32_t step = 0; step < steps; ++step) {
			const auto record = ReadRecord<213>(input);
			Require(record[0] <= 1 && record[1] <= 1 && record[2] <= 1 && record[81] <= 1 && record[82] <= 1, "native child root input flags exceed their domain");
			auto layer = ChildLayer();
			layer.CoordinatePlane = record[2];
			auto frame = ChildFrame();
			frame.Attributes.OriginX = ReadNumber(record, 51);
			frame.Attributes.OriginY = ReadNumber(record, 55);
			sgre::scene::MotionComposedLayer parent;
			parent.Active = record[81] != 0;
			auto& attributes = parent.Hierarchy.Attributes;
			attributes.Coordinate = {ReadNumber(record, 3), ReadNumber(record, 7), ReadNumber(record, 11)};
			attributes.Angle = ReadNumber(record, 15);
			attributes.ZoomX = ReadNumber(record, 19);
			attributes.ZoomY = ReadNumber(record, 23);
			attributes.ShearX = ReadNumber(record, 27);
			attributes.ShearY = ReadNumber(record, 31);
			attributes.FlipX = record[63] != 0;
			attributes.FlipY = record[64] != 0;
			attributes.Opacity = ReadWord(record, 65);
			attributes.Colors[0] = ReadWord(record, 73);
			parent.Hierarchy.ModulationColor = ReadWord(record, 69);
			parent.Hierarchy.Transform.XX = ReadNumber(record, 35);
			parent.Hierarchy.Transform.XY = ReadNumber(record, 39);
			parent.Hierarchy.Transform.YX = ReadNumber(record, 43);
			parent.Hierarchy.Transform.YY = ReadNumber(record, 47);
			sgre::scene::MotionChildFrameControl control;
			control.DirectionMode = record[1];
			control.DirectionOffset = ReadNumber(record, 59);
			if (record[0] != 0) {
				sgre::scene::BindMotionChildRoot(child, layer, frame, parent, control, std::bit_cast<std::int32_t>(ReadWord(record, 77)));
				Require(child.Overrides().Dirty, "actual child root binding always dirties its supplied matrix");
			}

			RequirePoseWords(child, std::span<const std::uint8_t>(record).subspan(83, 59), false);
			child.Compose(record[82] != 0);
			RequirePoseWords(child, std::span<const std::uint8_t>(record).subspan(142, 71), true);
		}
	}

	Require(input.peek() == std::char_traits<char>::eof(), "native child root fixture has trailing data");
}

int main(int argc, char** argv) {
	try {
		Require(argc == 1 || argc == 2, "usage: motion_child_root_tests [native-child-root-fixture]");
		TestParentMatrixAndOrigin();
		TestDirectionAndColorComparison();
		TestRequiredOwnerAndUnsupportedBoundaries();
		if (argc == 2) {
			ReplayNative(argv[1]);
		}

		std::cout << "motion child root tests passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
