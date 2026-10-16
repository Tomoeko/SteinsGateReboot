#include "sgre/scene/motion_root_scene.hpp"

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
#include <variant>

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

	throw std::runtime_error("unsupported child root construction was accepted");
}

static sgre::scene::PreparedMotionLayer Prepared() {
	sgre::scene::PreparedMotionLayer layer;
	layer.Type = 3;
	layer.CoordinatePlane = 1;
	layer.TransformOrder = {sgre::scene::MotionTransformOperation::Rotate, sgre::scene::MotionTransformOperation::Flip, sgre::scene::MotionTransformOperation::Shear, sgre::scene::MotionTransformOperation::Scale};

	return layer;
}

template <typename Value>
static sgre::resource::MotionLayer Raw(Value value) {
	sgre::resource::MotionLayer layer;
	layer.Type = 3;
	layer.Coordinate = 1;
	sgre::resource::PsbValue property;
	property.Data = std::move(value);
	layer.Properties.emplace_back("motionIndependentLayerInherit", std::move(property));

	return layer;
}

static void TestRealRootPlaneAndNormalization() {
	const auto raw = Raw(false);
	const auto prepared = Prepared();
	const auto configuration = sgre::scene::PrepareMotionChildRootConfiguration(raw, prepared, 5.5f);
	sgre::scene::MotionRootScene root(configuration);
	Require(root.MatrixMode() == sgre::scene::MotionRootMatrixMode::SuppliedByParent && root.Frame().Hierarchy.CoordinatePlane == 1 && root.TransformOrder() == prepared.TransformOrder && root.Priority() == 5.5f, "genuine child construction copies plane/order/priority into its independent supplied root owner");
	Require(!configuration.IndependentLayerInherit && !root.Frame().Composed && root.Overrides().Dirty, "root setup requires actual parent pose and retains native constructor dirty state");
	RequireFailure([&] {
		root.Compose(true);
	},
	    "actual parent pose");
	sgre::scene::MotionChildRootPose pose;
	pose.Overrides.Visible = true;
	pose.Overrides.Coordinate = {10, 20, 30};
	pose.Overrides.Opacity = 100;
	pose.Transform.XX = 0.5f;
	pose.Transform.XY = 2.25f;
	pose.Transform.YX = -1.5f;
	pose.Transform.YY = 0.25f;
	root.BindChildPose(pose);
	root.Compose(true);
	sgre::scene::PreparedMotionLayer child;
	sgre::scene::MotionLayerSample local;
	local.Hidden = false;
	local.Coordinate = {2, 4, 6};
	const auto normalized = sgre::scene::ComposeMotionHierarchyLayer(child, local, root.Frame().Hierarchy, root.Frame().Hierarchy, !configuration.IndependentLayerInherit);
	Require(normalized.Attributes.Coordinate == std::array<float, 3>{24.5f, 24, 28.5f} && normalized.Attributes.Opacity == 100, "actual XZ root plane changes downstream child composition and false independent flag enables root opacity normalization");
	const auto independent_configuration = sgre::scene::PrepareMotionChildRootConfiguration(Raw(true), prepared, 0);
	const auto independent = sgre::scene::ComposeMotionHierarchyLayer(child, local, root.Frame().Hierarchy, root.Frame().Hierarchy, !independent_configuration.IndependentLayerInherit);
	Require(independent.Attributes.Opacity == 255, "true independent flag keeps the actual child composition normalization mode disabled");
}

static void TestBooleanAndRequiredMetadata() {
	const auto prepared = Prepared();
	Require(sgre::scene::PrepareMotionChildRootConfiguration(Raw(std::int64_t{256}), prepared, 0).IndependentLayerInherit && sgre::scene::PrepareMotionChildRootConfiguration(Raw(std::int64_t{-256}), prepared, 0).IndependentLayerInherit, "native integer truth tests complete decoded value rather than low-byte input");
	Require(sgre::scene::PrepareMotionChildRootConfiguration(Raw(std::numeric_limits<double>::quiet_NaN()), prepared, 0).IndependentLayerInherit && !sgre::scene::PrepareMotionChildRootConfiguration(Raw(-0.0), prepared, 0).IndependentLayerInherit, "native unordered real comparison is true while negative zero is false");
	auto raw = Raw(false);
	raw.Properties.clear();
	RequireFailure([&] {
		static_cast<void>(sgre::scene::PrepareMotionChildRootConfiguration(raw, prepared, 0));
	},
	    "requires actual motionIndependent");
	raw = Raw(std::monostate{});
	RequireFailure([&] {
		static_cast<void>(sgre::scene::PrepareMotionChildRootConfiguration(raw, prepared, 0));
	},
	    "boolean or numeric");
	raw = Raw(false);
	raw.Properties.push_back(raw.Properties.front());
	RequireFailure([&] {
		static_cast<void>(sgre::scene::PrepareMotionChildRootConfiguration(raw, prepared, 0));
	},
	    "ambiguous duplicate");
	raw = Raw(false);
	raw.Coordinate = 0;
	RequireFailure([&] {
		static_cast<void>(sgre::scene::PrepareMotionChildRootConfiguration(raw, prepared, 0));
	},
	    "matching kind3 raw/prepared coordinates");
	auto invalid = sgre::scene::PrepareMotionChildRootConfiguration(Raw(false), prepared, 0);
	invalid.CoordinatePlane = 2;
	RequireFailure([&] {
		sgre::scene::MotionRootScene rejected(invalid);
	},
	    "XY/XZ");
	invalid.CoordinatePlane = 0;
	invalid.TransformOrder[0] = static_cast<sgre::scene::MotionTransformOperation>(4);
	RequireFailure([&] {
		sgre::scene::MotionRootScene rejected(invalid);
	},
	    "operation domain");
}

static std::uint32_t ReadWord(std::span<const std::uint8_t> bytes, std::size_t offset) {
	if (offset > bytes.size() || 4 > bytes.size() - offset) {
		throw std::runtime_error("truncated native child configuration word");
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
	Require(input.gcount() == static_cast<std::streamsize>(raw.size()), "truncated native child configuration record");
	std::array<std::uint8_t, Size> result{};
	for (std::size_t index = 0; index < Size; ++index) {
		result[index] = static_cast<std::uint8_t>(static_cast<unsigned char>(raw[index]));
	}

	return result;
}

static std::uint64_t ReadWide(std::span<const std::uint8_t> bytes, std::size_t offset) {
	std::uint64_t word = ReadWord(bytes, offset);
	word |= static_cast<std::uint64_t>(ReadWord(bytes, offset + 4)) << 32;

	return word;
}

static decltype(sgre::resource::PsbValue{}.Data) ReadMetadata(std::span<const std::uint8_t> row) {
	const auto word = ReadWide(row, 1);
	switch (row[0]) {
	case 0:
		return word != 0;
	case 1:
		return static_cast<std::int64_t>(std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(word)));
	case 2:
		return std::bit_cast<std::int64_t>(word);
	case 3:
		return static_cast<double>(std::bit_cast<float>(static_cast<std::uint32_t>(word)));
	case 4:
		return std::bit_cast<double>(word);
	case 5:
		return 0.0;
	default:
		throw std::runtime_error("native child metadata kind is unsupported");
	}
}

static void ReplayNative(const char* path) {
	std::ifstream input(path, std::ios::binary);
	Require(input.is_open(), "cannot open native child configuration fixture");
	const auto header = ReadRecord<8>(input);
	Require(header[0] == 'M' && header[1] == 'C' && header[2] == 'C' && header[3] == 'F', "invalid native child configuration signature");
	const auto count = ReadWord(header, 4);
	Require(count > 0 && count <= 128, "native child configuration work exceeds its bound");
	for (std::uint32_t index = 0; index < count; ++index) {
		try {
			const auto row = ReadRecord<140>(input);
			auto raw = Raw(ReadMetadata(row));
			raw.Coordinate = ReadWord(row, 9);
			auto prepared = Prepared();
			prepared.CoordinatePlane = raw.Coordinate;
			for (std::size_t operation = 0; operation < 4; ++operation) {
				prepared.TransformOrder[operation] = static_cast<sgre::scene::MotionTransformOperation>(ReadWord(row, 13 + operation * 4));
			}

			const auto configuration = sgre::scene::PrepareMotionChildRootConfiguration(raw, prepared, ReadNumber(row, 29));
			sgre::scene::MotionRootScene root(configuration);
			Require(configuration.IndependentLayerInherit == (row[33] != 0) && root.Frame().Hierarchy.CoordinatePlane == ReadWord(row, 34) && std::bit_cast<std::uint32_t>(root.Priority()) == ReadWord(row, 54) && root.Overrides().Dirty == (row[58] != 0) && root.MatrixMode() == sgre::scene::MotionRootMatrixMode::SuppliedByParent && row[59] == 1, "actual child root constructor metadata differs");
			for (std::size_t operation = 0; operation < 4; ++operation) {
				Require(static_cast<std::uint32_t>(root.TransformOrder()[operation]) == ReadWord(row, 38 + operation * 4), "actual copied root transform order differs");
			}

			sgre::scene::MotionChildRootPose pose;
			pose.Overrides.Visible = true;
			pose.Overrides.Coordinate = {ReadNumber(row, 76), ReadNumber(row, 80), ReadNumber(row, 84)};
			pose.Overrides.Opacity = ReadWord(row, 100);
			pose.Transform.XX = ReadNumber(row, 60);
			pose.Transform.XY = ReadNumber(row, 64);
			pose.Transform.YX = ReadNumber(row, 68);
			pose.Transform.YY = ReadNumber(row, 72);
			root.BindChildPose(pose);
			root.Compose(true);
			Require(root.Frame().Hierarchy.CoordinatePlane == ReadWord(row, 104), "actual composed root plane differs");
			const auto& matrix = root.Frame().Hierarchy.Transform;
			const std::array<float, 4> words{matrix.XX, matrix.XY, matrix.YX, matrix.YY};
			for (std::size_t item = 0; item < words.size(); ++item) {
				Require(std::bit_cast<std::uint32_t>(words[item]) == ReadWord(row, 108 + item * 4), "configured child root rebuilt its actual supplied matrix");
			}

			sgre::scene::PreparedMotionLayer child;
			sgre::scene::MotionLayerSample local;
			local.Hidden = false;
			local.Coordinate = {ReadNumber(row, 88), ReadNumber(row, 92), ReadNumber(row, 96)};
			const auto output = sgre::scene::ComposeMotionHierarchyLayer(child, local, root.Frame().Hierarchy, root.Frame().Hierarchy, !configuration.IndependentLayerInherit);
			for (std::size_t component = 0; component < 3; ++component) {
				Require(std::bit_cast<std::uint32_t>(output.Attributes.Coordinate[component]) == ReadWord(row, 124 + component * 4), "actual downstream child coordinate differs after root configuration");
			}

			Require(output.Attributes.Opacity == ReadWord(row, 136), "actual child independent/root-normalized opacity differs");
		} catch (const std::exception& error) {
			throw std::runtime_error("native child configuration " + std::to_string(index) + ": " + error.what());
		}
	}

	Require(input.peek() == std::char_traits<char>::eof(), "native child configuration fixture has trailing data");
}

int main(int argc, char** argv) {
	try {
		Require(argc == 1 || argc == 2, "usage: motion_child_configuration_tests [native-fixture]");
		TestRealRootPlaneAndNormalization();
		TestBooleanAndRequiredMetadata();
		if (argc == 2) {
			ReplayNative(argv[1]);
		}

		std::cout << "motion child configuration tests passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
