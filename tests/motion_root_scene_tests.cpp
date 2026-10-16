#include "sgre/scene/motion_root_scene.hpp"

#include "motion_root_angle_native_vectors.hpp"
#include "motion_root_zoom_native_vectors.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
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

static void TestInitialStateAndComposition() {
	sgre::scene::MotionRootScene root;
	Require(!root.Overrides().Visible && root.Overrides().Dirty && !root.Frame().Composed && !root.Frame().Hierarchy.Attributes.Hidden && root.Frame().Hierarchy.Transform.XX == 0 && root.Frame().Hierarchy.Transform.YY == 0, "native constructor override/evaluated visibility or precomposition matrix differs");
	root.SetCoordinate3({10, 20, 30});
	root.SetCoordinate({40, 50});
	Require(root.Overrides().Coordinate == std::array<float, 3>{40, 50, 30}, "native coordinate2 setter must preserve the previous Z word");
	root.Compose(true);
	Require(root.Frame().Composed && root.Frame().Hierarchy.Attributes.Hidden && root.Frame().Hierarchy.Attributes.Coordinate == root.Overrides().Coordinate && root.Frame().Displacement == std::array<float, 3>{} && !root.Overrides().Dirty && root.Frame().Hierarchy.Transform.XX == 1 && root.Frame().Hierarchy.Transform.YY == 1, "first assigned composition must consume false visibility and coordinates, produce its transform, and zero displacement");
	root.SetCoordinate({40, 50});
	root.SetVisible(false);
	root.SetFlip(false, false);
	Require(!root.Overrides().Dirty, "repeated native coordinate/visibility/flip values must preserve the cleared dirty flag");
	root.SetLeft(45);
	root.SetTop(55);
	root.SetDepth(35);
	root.SetVisible(true);
	root.SetFlip(true, false);
	root.SetPriority(-3.5f);
	root.Compose(false);
	Require(root.Frame().Displacement == std::array<float, 3>{5, 5, 5} && !root.Frame().Hierarchy.Attributes.Hidden && root.Frame().Hierarchy.Transform.XX == -1 && root.Frame().Hierarchy.Transform.YY == 1 && root.Priority() == -3.5f, "ordinary root composition must preserve 3D displacement, raw priority, visibility and flip transform");
	root.SetCoordinate3({-0.0f, 0, 0});
	root.Compose(false);
	root.SetCoordinate3({0, 0, 0});
	Require(!root.Overrides().Dirty && std::bit_cast<std::uint32_t>(root.Overrides().Coordinate[0]) == 0x80000000, "native equal signed zeros must retain the previously stored representation");
	root.SetPriority(std::bit_cast<float>(std::uint32_t{0xffc12345}));
	Require(std::bit_cast<std::uint32_t>(root.Priority()) == 0xffc12345, "native priority assignment must preserve raw float32 words");
	root.SetDepth(std::numeric_limits<float>::quiet_NaN());
	Require(root.Overrides().Dirty, "native nonfinite coordinate assignment must remain a dirty raw control value");
	try {
		root.Compose(false);
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find("finite coordinates") != std::string_view::npos, error.what());

		return;
	}

	throw std::runtime_error("nonfinite root composition was accepted");
}

static void TestNativeZoomWords() {
	for (const auto& sample : kNativeMotionRootZoomCases) {
		sgre::scene::MotionRootScene root;
		const auto initial_x = std::bit_cast<float>(sample.Initial[0]);
		const auto initial_y = std::bit_cast<float>(sample.Initial[1]);
		root.SetZoom(initial_x, initial_y);
		if (std::isfinite(initial_x) && std::isfinite(initial_y)) {
			root.Compose(false);
			if (sample.InitialDirty) {
				root.SetVisible(true);
			}
		}
		Require(root.Overrides().Dirty == sample.InitialDirty, "native zoom fixture must begin with its genuine retained dirty flag");
		if (!sample.Detached) {
			const auto horizontal = std::bit_cast<float>(sample.Input[0]);
			const auto vertical = std::bit_cast<float>(sample.Input[1]);
			switch (sample.Operation) {
			case 0:
				root.SetZoom(horizontal, vertical);
				break;
			case 1:
				root.SetZoom(horizontal, root.Overrides().ZoomY);
				break;
			case 2:
				root.SetZoom(root.Overrides().ZoomX, horizontal);
				break;
			default:
				throw std::runtime_error("native zoom fixture operation is invalid");
			}
		}
		const auto& actual = root.Overrides();
		Require(std::bit_cast<std::uint32_t>(actual.ZoomX) == sample.Expected[0] && std::bit_cast<std::uint32_t>(actual.ZoomY) == sample.Expected[1] && actual.Dirty == sample.ExpectedDirty, "actual zoom retained words or ordered/unordered dirty/store-both policy differs from unchanged native controls");
		const auto getter_x = sample.Detached ? 0.0f : actual.ZoomX;
		const auto getter_y = sample.Detached ? 0.0f : actual.ZoomY;
		Require(std::bit_cast<std::uint32_t>(getter_x) == sample.Getter[0] && std::bit_cast<std::uint32_t>(getter_y) == sample.Getter[1], "native zoom getter words differ");
	}
}

static void TestNativeAngleWords() {
	for (std::size_t index = 0; index < kNativeMotionRootAngleCases.size(); ++index) {
		const auto& sample = kNativeMotionRootAngleCases[index];
		sgre::scene::MotionRootScene root;
		const auto initial = std::bit_cast<float>(sample.Initial);
		// A distinct genuine assignment preserves an initial negative-zero word too.
		root.SetAngleDegrees(1);
		root.SetAngleDegrees(initial);
		if (std::isfinite(initial)) {
			root.Compose(false);
			if (sample.InitialDirty) {
				root.SetVisible(true);
			}
		}
		Require(root.Overrides().Dirty == sample.InitialDirty, "native angle fixture must begin with its real retained dirty flag");
		if (!sample.Detached) {
			if (sample.Operation == 0) {
				root.SetAngleDegrees(std::bit_cast<float>(sample.Input));
			} else {
				Require(sample.Operation == 1, "native angle operation exceeds its bounded enum");
				root.SetAngleRadians(std::bit_cast<float>(sample.Input));
			}
		}
		const std::array<std::uint32_t, 3> actual{std::bit_cast<std::uint32_t>(root.Overrides().Angle), std::bit_cast<std::uint32_t>(sample.Detached ? 0.0f : root.AngleDegrees()), std::bit_cast<std::uint32_t>(sample.Detached ? 0.0f : root.AngleRadians())};
		const std::array<std::uint32_t, 3> expected{sample.Expected, sample.Getter[0], sample.Getter[1]};
		for (std::size_t field = 0; field < actual.size(); ++field) {
			if (actual[field] != expected[field]) {
				throw std::runtime_error("native angle case " + std::to_string(index) + " field " + std::to_string(field) + " expected " + std::to_string(expected[field]) + " actual " + std::to_string(actual[field]));
			}
		}
		Require(root.Overrides().Dirty == sample.ExpectedDirty, "ordinary native angle ordered/unordered comparison must preserve its exact dirty/store policy");
	}

	sgre::scene::MotionRootScene root;
	root.Compose(false);
	const auto completed = root.Frame().Hierarchy.Transform;
	root.SetAngleDegrees(-1080.25f);
	Require(root.AngleDegrees() == -1080.25f && root.Frame().Hierarchy.Attributes.Angle == 0 && root.Frame().Hierarchy.Transform.XX == completed.XX, "ordinary raw degree writes must not normalize or replace completed numeric output");
	root.SetAngleRadians(1.57079637f);
	Require(root.AngleDegrees() == 90, "registered multiply-then-divide radians conversion must produce actual degree overrides");
	root.Compose(false);
	Require(!root.Overrides().Dirty && root.Frame().Hierarchy.Attributes.Angle == 90 && std::abs(root.Frame().Hierarchy.Transform.XX) < 0.000001f, "the next real root composition must consume the accepted degree value");
}

static std::uint32_t ReadU32(std::span<const std::uint8_t> bytes, std::size_t offset) {
	Require(offset <= bytes.size() && 4 <= bytes.size() - offset, "native root fixture word exceeds its record");
	std::uint32_t value = 0;
	for (std::size_t index = 0; index < 4; ++index) {
		value |= static_cast<std::uint32_t>(bytes[offset + index]) << (index * 8);
	}

	return value;
}

template <std::size_t Size>
static std::array<std::uint8_t, Size> ReadRecord(std::ifstream& input) {
	std::array<char, Size> raw{};
	input.read(raw.data(), static_cast<std::streamsize>(raw.size()));
	Require(input.gcount() == static_cast<std::streamsize>(raw.size()), "truncated native root fixture");
	std::array<std::uint8_t, Size> result{};
	for (std::size_t index = 0; index < Size; ++index) {
		result[index] = static_cast<std::uint8_t>(static_cast<unsigned char>(raw[index]));
	}

	return result;
}

static void RequireState(const sgre::scene::MotionRootScene& root, std::span<const std::uint8_t> expected) {
	const auto& overrides = root.Overrides();
	const auto& frame = root.Frame();
	const auto& attributes = frame.Hierarchy.Attributes;
	const auto& transform = frame.Hierarchy.Transform;
	const std::array<float, 14> words{overrides.Coordinate[0], overrides.Coordinate[1], overrides.Coordinate[2], attributes.Coordinate[0], attributes.Coordinate[1], attributes.Coordinate[2], frame.Displacement[0], frame.Displacement[1], frame.Displacement[2], transform.XX, transform.XY, transform.YX, transform.YY, root.Priority()};
	for (std::size_t index = 0; index < words.size(); ++index) {
		Require(std::bit_cast<std::uint32_t>(words[index]) == ReadU32(expected, index * 4), "native root override/evaluated/displacement/transform float32 word differs");
	}

	const std::array<bool, 7> flags{overrides.Dirty, overrides.Visible, overrides.FlipX, overrides.FlipY, !attributes.Hidden, attributes.FlipX, attributes.FlipY};
	for (std::size_t index = 0; index < flags.size(); ++index) {
		Require(flags[index] == (expected[56 + index] != 0), "native root override or evaluated flags differ");
	}
}

static void ReplayNative(const char* path) {
	std::ifstream input(path, std::ios::binary);
	Require(input.is_open(), "cannot open native root fixture");
	const auto header = ReadRecord<12>(input);
	Require(header[0] == 'M' && header[1] == 'R' && header[2] == 'S' && header[3] == '1', "invalid native root fixture signature");
	const auto cases = ReadU32(header, 4);
	const auto steps = ReadU32(header, 8);
	Require(cases > 0 && cases <= 256 && steps > 0 && steps <= 16, "native root fixture work exceeds its bounds");
	for (std::uint32_t case_index = 0; case_index < cases; ++case_index) {
		sgre::scene::MotionRootScene root;
		for (std::uint32_t step = 0; step < steps; ++step) {
			const auto record = ReadRecord<78>(input);
			const std::array<float, 3> coordinate{std::bit_cast<float>(ReadU32(record, 1)), std::bit_cast<float>(ReadU32(record, 5)), std::bit_cast<float>(ReadU32(record, 9))};
			Require(record[13] < 4 && record[14] <= 1, "native root fixture control bits exceed their bounds");
			switch (record[0]) {
			case 0:
				root.SetCoordinate({coordinate[0], coordinate[1]});
				break;
			case 1:
				root.SetCoordinate3(coordinate);
				break;
			case 2:
				root.SetVisible((record[13] & 1) != 0);
				break;
			case 3:
				root.SetFlip((record[13] & 1) != 0, (record[13] & 2) != 0);
				break;
			case 4:
				root.SetPriority(coordinate[0]);
				break;
			default:
				throw std::runtime_error("native root fixture operation is unsupported");
			}

			if (step % 3 == 2) {
				root.Compose(record[14] != 0);
			}

			RequireState(root, std::span<const std::uint8_t>(record).subspan(15));
		}
	}

	Require(input.peek() == std::char_traits<char>::eof(), "native root fixture has trailing data");
}

static void TestOpacityRetainedWord() {
	sgre::scene::MotionRootScene root;
	root.Compose(false);
	root.SetOpacity(255);
	Require(!root.Overrides().Dirty, "equal actual opacity word must not dirty root");
	root.SetOpacity(128);
	Require(root.Overrides().Dirty && root.Overrides().Opacity == 128 && root.Frame().Hierarchy.Attributes.Opacity == 255, "opacity setter must retain one raw word without implicit composition");
	root.Compose(false);
	root.SetOpacity(128);
	Require(!root.Overrides().Dirty && root.Frame().Hierarchy.Attributes.Opacity == 128, "ordinary completed root retains exact opacity and clears only through composition");
	root.SetOpacity(0xffffffffu);
	Require(root.Overrides().Dirty && root.Overrides().Opacity == 0xffffffffu, "native opacity setter must not clamp or byte-mask the stored word");
	bool rejected = false;
	try {
		root.Compose(false);
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find("native byte domain") != std::string_view::npos, error.what());
		rejected = true;
	}

	Require(rejected, "raw opacity setter must preserve the separate unverified composition-domain gate");
}

int main(int argc, char** argv) {
	try {
		Require(argc == 1 || argc == 2, "usage: motion_root_scene_tests [native-root-fixture]");
		TestInitialStateAndComposition();
		TestNativeZoomWords();
		TestNativeAngleWords();
		TestOpacityRetainedWord();
		if (argc == 2) {
			ReplayNative(argv[1]);
		}

		std::cout << "motion root scene tests passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
