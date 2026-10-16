#include "sgre/scene/motion_bezier_inverse.hpp"

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
#include <vector>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

static std::uint32_t ReadWord(std::istream& file) {
	std::array<char, 4> bytes{};
	file.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
	Require(static_cast<bool>(file), "native Bezier inverse vector is truncated");

	return static_cast<std::uint8_t>(bytes[0]) | (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[1])) << 8) | (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[2])) << 16) | (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[3])) << 24);
}

static void TestNativeGrids(const char* path) {
	std::ifstream file(path, std::ios::binary);
	Require(file.is_open() && ReadWord(file) == 0x3149424d, "native Bezier inverse vector requires its exact MBI1 header");
	const auto count = ReadWord(file);
	Require(count == 64, "native Bezier inverse corpus must retain all64 actual row/column cases");
	std::size_t scalar_words = 0;
	for (std::uint32_t case_index = 0; case_index < count; ++case_index) {
		const auto degree = ReadWord(file);
		const auto points = ReadWord(file);
		Require(degree == 0x33 || degree == 0x34 || degree == 0x43 || degree == 0x44, "native Bezier inverse fixture degree changed");
		Require(points == ((degree & 15) + 1) * ((degree >> 4) + 1), "native Bezier inverse fixture point count changed");
		std::vector<sgre::resource::MotionPoint> samples(points);
		for (auto& sample : samples) {
			sample.X = std::bit_cast<float>(ReadWord(file));
			sample.Y = std::bit_cast<float>(ReadWord(file));
		}
		sgre::scene::ConvertMotionBezierSamplesToControls(samples, static_cast<std::uint8_t>(degree));
		for (const auto& control : samples) {
			Require(std::bit_cast<std::uint32_t>(control.X) == ReadWord(file) && std::bit_cast<std::uint32_t>(control.Y) == ReadWord(file), "native cubic/quartic row/column inverse control word differs");
			scalar_words += 2;
		}
	}
	Require(file.peek() == std::char_traits<char>::eof(), "native Bezier inverse fixture has trailing bytes");
	std::cout << "Native sampled Bezier inverse: " << count << " grids/" << scalar_words << " exact scalar words\n";
}

template <typename Operation>
static void RequireFailure(Operation operation, std::string_view message) {
	try {
		operation();
	} catch (const std::runtime_error&) {
		return;
	}

	throw std::runtime_error(std::string(message));
}

static void TestRejectedInputs() {
	std::array<sgre::resource::MotionPoint, 25> samples{};
	for (const auto degree : {0x00, 0x23, 0x32, 0x35, 0x53, 0x55}) {
		RequireFailure([&] {
			sgre::scene::ConvertMotionBezierSamplesToControls(samples, static_cast<std::uint8_t>(degree));
		},
		    "unrecovered degree must reject instead of applying cubic/quartic arithmetic");
	}
	RequireFailure([&] {
		sgre::scene::ConvertMotionBezierSamplesToControls(std::span(samples).first(19), 0x34);
	},
	    "incorrect sample count must reject before mutation");
	for (const auto invalid : {std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()}) {
		samples.back().X = invalid;
		RequireFailure([&] {
			sgre::scene::ConvertMotionBezierSamplesToControls(samples, 0x44);
		},
		    "nonfinite sampled coordinates must reject before conversion");
		Require(samples.front().X == 0 && samples.front().Y == 0, "invalid sample validation must precede control mutation");
	}
}

static void TestRetainedNativeGrid1() {
	// Bounded synthetic original-instruction sample/control words; no game resource data.
	const std::array<std::array<std::uint32_t, 4>, 16> vectors{{
	    {0x80000000, 0x00000000, 0x80000000, 0x00000000},
	    {0x80000000, 0x00000000, 0x00000000, 0x00000000},
	    {0x80000000, 0x00000000, 0x00000000, 0x00000000},
	    {0x80000000, 0x00000000, 0x80000000, 0x00000000},
	    {0x80000000, 0x00000000, 0x00000000, 0x00000000},
	    {0x80000000, 0x00000000, 0x00000000, 0x00000000},
	    {0x80000000, 0x00000000, 0x00000000, 0x00000000},
	    {0x80000000, 0x00000000, 0x00000000, 0x00000000},
	    {0x80000000, 0x00000000, 0x00000000, 0x00000000},
	    {0x80000000, 0x00000000, 0x00000000, 0x00000000},
	    {0x80000000, 0x00000000, 0x00000000, 0x00000000},
	    {0x80000000, 0x00000000, 0x00000000, 0x00000000},
	    {0x80000000, 0x00000000, 0x80000000, 0x00000000},
	    {0x80000000, 0x00000000, 0x00000000, 0x00000000},
	    {0x80000000, 0x00000000, 0x00000000, 0x00000000},
	    {0x80000000, 0x00000000, 0x80000000, 0x00000000},
	}};
	std::array<sgre::resource::MotionPoint, 16> samples;
	for (std::size_t index = 0; index < samples.size(); ++index) {
		samples[index].X = std::bit_cast<float>(vectors[index][0]);
		samples[index].Y = std::bit_cast<float>(vectors[index][1]);
	}
	sgre::scene::ConvertMotionBezierSamplesToControls(samples, 0x33);
	for (std::size_t index = 0; index < samples.size(); ++index) {
		Require(std::bit_cast<std::uint32_t>(samples[index].X) == vectors[index][2] && std::bit_cast<std::uint32_t>(samples[index].Y) == vectors[index][3], "retained native cubic/quartic controls differ");
	}
}

static void TestRetainedNativeGrid2() {
	// Bounded synthetic original-instruction sample/control words; no game resource data.
	const std::array<std::array<std::uint32_t, 4>, 16> vectors{{
	    {0x403438a8, 0xbfcd3fae, 0x403438a8, 0xbfcd3fae},
	    {0xc0f64738, 0xc09324b6, 0xc20563cd, 0xc12cb03e},
	    {0x40be0b44, 0x3ea18f57, 0x41de79a6, 0x3ffb3854},
	    {0x403f3eb7, 0x40cd5173, 0x403f3eb7, 0x40cd5173},
	    {0x40141a32, 0xc0046e95, 0x3e4b6b80, 0xc08d9c42},
	    {0x400afcd1, 0x40b1e23f, 0xc0857e16, 0x40da4b90},
	    {0x40e6ff3f, 0x40b05b70, 0x42316c72, 0x42329955},
	    {0xbdc7e192, 0x40966c75, 0xbf4508dc, 0x41853501},
	    {0x3fa21a59, 0x3d4345f1, 0x40f053af, 0x3fb5875e},
	    {0x407d13bd, 0x40ccf26d, 0x41abeca2, 0x425238f5},
	    {0xbfc03765, 0xc0b26ae3, 0xc2139298, 0xc2996cd1},
	    {0xbf1147d5, 0xc09589eb, 0xc05dc507, 0xc1a92b98},
	    {0xc0efbf5a, 0x3fc6ca0c, 0xc0efbf5a, 0x3fc6ca0c},
	    {0x4046ef2a, 0x40825d3b, 0x41940200, 0x40a2960b},
	    {0xbf97147b, 0x408fa059, 0xc159b1ea, 0x40b507a0},
	    {0x405f1cc0, 0x402a15c3, 0x405f1cc0, 0x402a15c3},
	}};
	std::array<sgre::resource::MotionPoint, 16> samples;
	for (std::size_t index = 0; index < samples.size(); ++index) {
		samples[index].X = std::bit_cast<float>(vectors[index][0]);
		samples[index].Y = std::bit_cast<float>(vectors[index][1]);
	}
	sgre::scene::ConvertMotionBezierSamplesToControls(samples, 0x33);
	for (std::size_t index = 0; index < samples.size(); ++index) {
		Require(std::bit_cast<std::uint32_t>(samples[index].X) == vectors[index][2] && std::bit_cast<std::uint32_t>(samples[index].Y) == vectors[index][3], "retained native cubic/quartic controls differ");
	}
}

static void TestRetainedNativeGrid18() {
	// Bounded synthetic original-instruction sample/control words; no game resource data.
	const std::array<std::array<std::uint32_t, 4>, 20> vectors{{
	    {0xc0885b5c, 0x3fc233c8, 0xc0885b5c, 0x3fc233c8},
	    {0xc0af2e2d, 0xc0db169b, 0x3f3283dc, 0xc120b300},
	    {0xc0e2b6fa, 0xc0ca1f19, 0xc1b3a306, 0xc18697c2},
	    {0xc067c59b, 0xbe233053, 0x40e86c4d, 0x411e28b6},
	    {0xc0c6bec0, 0xbf7f9071, 0xc0c6bec0, 0xbf7f9071},
	    {0x40d17cd1, 0x3ea35874, 0x415a98ea, 0x4084b35b},
	    {0x3fca865a, 0xbfa7708f, 0xc2a16c26, 0xc18d36b1},
	    {0x40cbecee, 0x40af46a9, 0x431bb500, 0x417e8420},
	    {0xbe8d7fcf, 0x40bf51d3, 0xc292838b, 0x421499a9},
	    {0xc079d4df, 0x40e35e49, 0xc112c567, 0x41a4aaa5},
	    {0x40932d5f, 0xbfda64fa, 0x4112171a, 0xc11d4177},
	    {0x40f439bb, 0x3fed8b01, 0x42939320, 0xc1add6ec},
	    {0x40971bf9, 0x40bd7320, 0xc2dce026, 0x42cfe7b9},
	    {0x404c5c52, 0xc09acd48, 0x42945a02, 0xc2bf5585},
	    {0x404c9d5d, 0x3faa1eec, 0x40ff6f2a, 0xc1013971},
	    {0xc0fc661e, 0x40b5e710, 0xc0fc661e, 0x40b5e710},
	    {0x40ab8b23, 0x3e0ad635, 0xbe2275c0, 0xc08e7c23},
	    {0x40d70743, 0xbfa50acc, 0x423c2241, 0x4013fc9e},
	    {0xc0c8dc46, 0xbfe4c685, 0xc22ca32f, 0xc0bd5313},
	    {0x40cf22be, 0x3fa4d303, 0x40cf22be, 0x3fa4d303},
	}};
	std::array<sgre::resource::MotionPoint, 20> samples;
	for (std::size_t index = 0; index < samples.size(); ++index) {
		samples[index].X = std::bit_cast<float>(vectors[index][0]);
		samples[index].Y = std::bit_cast<float>(vectors[index][1]);
	}
	sgre::scene::ConvertMotionBezierSamplesToControls(samples, 0x34);
	for (std::size_t index = 0; index < samples.size(); ++index) {
		Require(std::bit_cast<std::uint32_t>(samples[index].X) == vectors[index][2] && std::bit_cast<std::uint32_t>(samples[index].Y) == vectors[index][3], "retained native cubic/quartic controls differ");
	}
}

static void TestRetainedNativeGrid34() {
	// Bounded synthetic original-instruction sample/control words; no game resource data.
	const std::array<std::array<std::uint32_t, 4>, 20> vectors{{
	    {0xc0a8badc, 0x3ff70840, 0xc0a8badc, 0x3ff70840},
	    {0x3f2e4b12, 0x3f90ebf3, 0x4163a248, 0x412c0eb2},
	    {0xc0c3a589, 0xc09f97f2, 0xc18d5bfc, 0xc19dd24b},
	    {0xc08482d4, 0x408e7ea3, 0xc08482d4, 0x408e7ea3},
	    {0xc074900a, 0x3e828b05, 0x4122517f, 0xc1c11912},
	    {0xc0cfd500, 0x3f3acdaa, 0xc2d9117c, 0xc2308fea},
	    {0xc0003590, 0x40dc7548, 0x421963fe, 0x430478d5},
	    {0x3ea07b49, 0x409a0580, 0x41bea209, 0x3fe7abb0},
	    {0xc06d2b94, 0x40917831, 0xc2169450, 0x42603785},
	    {0x3ec5652f, 0x40611618, 0x432649be, 0x41ea004c},
	    {0x402ff2e7, 0xc0145fe3, 0xc27ca669, 0xc3322cc0},
	    {0xc0b7ae8e, 0x40cf4e96, 0xc232eda2, 0x412e1b58},
	    {0x40e5c7f2, 0xc0d58d3a, 0x41febfeb, 0xc22b180a},
	    {0xc0dffa8f, 0x40f21dd9, 0xc31390c9, 0x424795f4},
	    {0x40e0bcb4, 0x40a9a71d, 0x42d2120d, 0x429a44bf},
	    {0x3f0270d5, 0x408a6e06, 0x41ac4518, 0x40f6ea38},
	    {0x4079453b, 0x4007d4c1, 0x4079453b, 0x4007d4c1},
	    {0xc0b06f13, 0xc0c0c2dc, 0xc181e0e7, 0xc12e24c4},
	    {0xc0225419, 0xc0dce209, 0x4025e30a, 0xc0ef09f8},
	    {0xbf424e9a, 0xc086320d, 0xbf424e9a, 0xc086320d},
	}};
	std::array<sgre::resource::MotionPoint, 20> samples;
	for (std::size_t index = 0; index < samples.size(); ++index) {
		samples[index].X = std::bit_cast<float>(vectors[index][0]);
		samples[index].Y = std::bit_cast<float>(vectors[index][1]);
	}
	sgre::scene::ConvertMotionBezierSamplesToControls(samples, 0x43);
	for (std::size_t index = 0; index < samples.size(); ++index) {
		Require(std::bit_cast<std::uint32_t>(samples[index].X) == vectors[index][2] && std::bit_cast<std::uint32_t>(samples[index].Y) == vectors[index][3], "retained native cubic/quartic controls differ");
	}
}

static void TestRetainedNativeGrid50() {
	// Bounded synthetic original-instruction sample/control words; no game resource data.
	const std::array<std::array<std::uint32_t, 4>, 25> vectors{{
	    {0x403d3410, 0x40ead938, 0x403d3410, 0x40ead938},
	    {0x408d98c2, 0xc066bec7, 0x422a3834, 0xc206f7a3},
	    {0xc0f42b8b, 0x40aca204, 0xc29cf888, 0x42260682},
	    {0x40442838, 0x408fb5ec, 0x4233f924, 0xc11f2552},
	    {0xc08a4aa3, 0x40962da7, 0xc08a4aa3, 0x40962da7},
	    {0x40b72c0f, 0xc0c3c333, 0x420f3bd7, 0xc20b6201},
	    {0xc0b672bc, 0xc0b38541, 0xc320d487, 0x4173c41d},
	    {0xc01d8874, 0x3fc8b283, 0x435d92d9, 0x42b5211c},
	    {0xc0a76832, 0xc0aa62f8, 0xc33ac20d, 0xc2c2154c},
	    {0xbfaeef66, 0x3f08aeaa, 0xc13cde4b, 0x40336422},
	    {0xbfbeb07b, 0x40480134, 0xc26e3126, 0x41ed19cd},
	    {0xbf18bfcb, 0xc0d63bb8, 0x43022294, 0xc2f04b18},
	    {0x3e4edd36, 0xbf17d53e, 0xc35dcb6a, 0xc26a19b6},
	    {0x408f3eb4, 0xc09afd8a, 0x4364225f, 0x42a33166},
	    {0x3f3cb635, 0xc048c7c9, 0x41f33d1b, 0xc11ae5a4},
	    {0x40f240c2, 0x40c5bf36, 0x424177ad, 0xbe6fcd80},
	    {0x409f62e0, 0x40bbcad5, 0xc1e4aa30, 0x42217ed6},
	    {0xbf9d6f01, 0x40e9c24a, 0x42a1581a, 0x43062ff2},
	    {0xc0af7137, 0xc0aff336, 0xc2c41878, 0xc2f386d4},
	    {0xc0b6ca91, 0xc0338586, 0xc1f7b94f, 0xbff63508},
	    {0xc0c69554, 0x40a4fefd, 0xc0c69554, 0x40a4fefd},
	    {0xc0160b97, 0xc017c297, 0x4098c774, 0xc14c2532},
	    {0xc0c26cdb, 0x3fe0da19, 0xc0f2ef48, 0x40685878},
	    {0xc0e68e8e, 0x40b40acf, 0xc18aa3e4, 0x415218a2},
	    {0x409883dc, 0xbea3b745, 0x409883dc, 0xbea3b745},
	}};
	std::array<sgre::resource::MotionPoint, 25> samples;
	for (std::size_t index = 0; index < samples.size(); ++index) {
		samples[index].X = std::bit_cast<float>(vectors[index][0]);
		samples[index].Y = std::bit_cast<float>(vectors[index][1]);
	}
	sgre::scene::ConvertMotionBezierSamplesToControls(samples, 0x44);
	for (std::size_t index = 0; index < samples.size(); ++index) {
		Require(std::bit_cast<std::uint32_t>(samples[index].X) == vectors[index][2] && std::bit_cast<std::uint32_t>(samples[index].Y) == vectors[index][3], "retained native cubic/quartic controls differ");
	}
}

static void TestNativeCustomGrids(const char* path) {
	std::ifstream file(path, std::ios::binary);
	Require(file.is_open() && ReadWord(file) == 0x314b424d, "native actual-knot inverse vector requires its exact MBK1 header");
	const auto count = ReadWord(file);
	Require(count == 64, "native actual-knot inverse corpus must retain all64 cases");
	std::size_t scalar_words = 0;
	for (std::uint32_t case_index = 0; case_index < count; ++case_index) {
		const auto degree = ReadWord(file);
		const auto points = ReadWord(file);
		Require(degree == 0x33 || degree == 0x34 || degree == 0x43 || degree == 0x44, "actual-knot inverse fixture degree changed");
		const auto width = static_cast<std::size_t>(degree & 15) + 1;
		const auto height = static_cast<std::size_t>(degree >> 4) + 1;
		Require(points == width * height, "actual-knot inverse point count changed");
		std::array<float, 5> horizontal;
		std::array<float, 5> vertical;
		for (auto& knot : std::span(horizontal).first(width)) {
			knot = std::bit_cast<float>(ReadWord(file));
		}
		for (auto& knot : std::span(vertical).first(height)) {
			knot = std::bit_cast<float>(ReadWord(file));
		}
		std::vector<sgre::resource::MotionPoint> samples(points);
		for (auto& sample : samples) {
			sample.X = std::bit_cast<float>(ReadWord(file));
			sample.Y = std::bit_cast<float>(ReadWord(file));
		}
		sgre::scene::ConvertMotionBezierSamplesToControls(samples, static_cast<std::uint8_t>(degree), std::span(horizontal).first(width), std::span(vertical).first(height));
		for (const auto& control : samples) {
			Require(std::bit_cast<std::uint32_t>(control.X) == ReadWord(file) && std::bit_cast<std::uint32_t>(control.Y) == ReadWord(file), "native actual-knot inverse control word differs");
			scalar_words += 2;
		}
	}
	Require(file.peek() == std::char_traits<char>::eof(), "native actual-knot inverse fixture has trailing bytes");
	std::cout << "Native actual-knot Bezier inverse: " << count << " grids/" << scalar_words << " exact scalar words\n";
}

template <std::size_t Horizontal, std::size_t Vertical, std::size_t Points>
static void CompareNativeCustomGrid(std::uint8_t degree, const std::array<std::uint32_t, Horizontal>& horizontal_words, const std::array<std::uint32_t, Vertical>& vertical_words, const std::array<std::array<std::uint32_t, 4>, Points>& words) {
	std::array<float, Horizontal> horizontal;
	std::array<float, Vertical> vertical;
	std::array<sgre::resource::MotionPoint, Points> samples;
	for (std::size_t index = 0; index < Horizontal; ++index) {
		horizontal[index] = std::bit_cast<float>(horizontal_words[index]);
	}
	for (std::size_t index = 0; index < Vertical; ++index) {
		vertical[index] = std::bit_cast<float>(vertical_words[index]);
	}
	for (std::size_t index = 0; index < Points; ++index) {
		samples[index].X = std::bit_cast<float>(words[index][0]);
		samples[index].Y = std::bit_cast<float>(words[index][1]);
	}
	sgre::scene::ConvertMotionBezierSamplesToControls(samples, degree, horizontal, vertical);
	for (std::size_t index = 0; index < Points; ++index) {
		Require(std::bit_cast<std::uint32_t>(samples[index].X) == words[index][2] && std::bit_cast<std::uint32_t>(samples[index].Y) == words[index][3], "retained actual-knot native control differs");
	}
}

static void TestRejectedKnots() {
	std::array<sgre::resource::MotionPoint, 16> samples{};
	const std::array<std::array<float, 4>, 5> invalid_knots{{
		{0, 0.5f, 0.5f, 1},
		{0, 0.8f, 0.2f, 1},
		{0, 0.25f, 0.75f, 0.99f},
		{0, std::numeric_limits<float>::quiet_NaN(), 0.75f, 1},
		{0, std::numeric_limits<float>::infinity(), 0.75f, 1}
	}};
	for (const auto& knots : invalid_knots) {
		RequireFailure([&] {
			sgre::scene::ConvertMotionBezierSamplesToControls(samples, 0x33, knots, {});
		}, "actual basis rejects repeated, descending, nonfinite or missing endpoint knots");
	}
	RequireFailure([&] {
		sgre::scene::ConvertMotionBezierSamplesToControls(samples, 0x33, std::span(invalid_knots.front()).first(3), {});
	}, "actual basis rejects wrong knot count before conversion");
	for (const auto& sample : samples) {
		Require(sample.X == 0 && sample.Y == 0, "rejected knots must not change caller sample storage");
	}
}

static void TestRetainedActualKnotGrid2() {
	// Synthetic sample/control words observed from the unmodified original inverse instructions.
	const std::array<std::uint32_t, 4> horizontal{0x00000000, 0x3eb33333, 0x3f2ccccc, 0x3f800000};
	const std::array<std::uint32_t, 4> vertical{0x00000000, 0x3eb33333, 0x3f2ccccc, 0x3f800000};
	const std::array<std::array<std::uint32_t, 4>, 16> words{{
		{0xbec12cd7, 0x40fdd601, 0xbec12cd7, 0x40fdd601},
		{0x3f5b294b, 0x40b1dcf5, 0xc09b157a, 0x4196eabc},
		{0x409e8808, 0xc09cded6, 0x414d820a, 0xc1a78422},
		{0x3f77e1ec, 0x3e9a2127, 0x3f77e1ec, 0x3e9a2127},
		{0xc0d8fb97, 0x40ce24c6, 0xc1d9483b, 0x41cbc6cc},
		{0xbfc9aa3f, 0xc0ada993, 0xc163bf93, 0xc2a7f0a4},
		{0x403643bc, 0x405f2a18, 0x41fcb4d1, 0x42adbf30},
		{0xc0b1b300, 0xc0c77134, 0xc159792e, 0xc1ae3b58},
		{0x404c898e, 0xc0cfa305, 0x41ba0812, 0xc1f12dc2},
		{0x3e55dc0c, 0xc0bd8d7c, 0x40dd9ae1, 0x411bcc38},
		{0xbe3ca580, 0xbf4ac2ad, 0xc1824eec, 0xc1e2a12a},
		{0xbf73fdb4, 0xbdbbbacb, 0xbea4fe20, 0x417529aa},
		{0xc08883d3, 0x40763ddd, 0xc08883d3, 0x40763ddd},
		{0xc02ce76f, 0x3ff629c2, 0xc0b8b1e4, 0x40855035},
		{0x40041c78, 0xc01e0b81, 0x4046faf9, 0xc032f162},
		{0x40d6c326, 0xc0ea2a9f, 0x40d6c326, 0xc0ea2a9f},
	}};
	CompareNativeCustomGrid(0x33, horizontal, vertical, words);
}

static void TestRetainedActualKnotGrid18() {
	// Synthetic sample/control words observed from the unmodified original inverse instructions.
	const std::array<std::uint32_t, 5> horizontal{0x00000000, 0x3eb33333, 0x3f111111, 0x3f488888, 0x3f800000};
	const std::array<std::uint32_t, 4> vertical{0x00000000, 0x3eb33333, 0x3f2ccccc, 0x3f800000};
	const std::array<std::array<std::uint32_t, 4>, 20> words{{
		{0xc0829012, 0x405f9d07, 0xc0829012, 0x405f9d07},
		{0x404f25af, 0xc06c719d, 0x42af1e70, 0x4206f760},
		{0xc0ee0159, 0xc0d34f7f, 0xc2ea32b2, 0xc28cac31},
		{0x40f973e2, 0x407b3f92, 0x426e8ad4, 0x4226ae37},
		{0x3f8b5a96, 0xc0c887c0, 0x3f8b5a96, 0xc0c887c0},
		{0xbfa07443, 0x405837c8, 0xc07ade3b, 0x419bd05f},
		{0xc012dc4d, 0x3e690530, 0x4200846f, 0xc33c2e88},
		{0xc0eb8a84, 0x3f8795df, 0xc1e60870, 0x437cb7fe},
		{0x3f19a99f, 0xc09f852e, 0x4098ff90, 0xc314de95},
		{0xc0dae507, 0x401324f5, 0xc1ec1504, 0x41c05b76},
		{0x3f67a1bf, 0xc0cfab64, 0x40f2efed, 0xc1d55e30},
		{0xc0d30c63, 0x40e976ca, 0xbf6312a0, 0x434a456a},
		{0xc0c7f924, 0xc01e8492, 0xc2ad8939, 0xc359b51e},
		{0x40a676a4, 0xbe742507, 0x4260931a, 0x42d21386},
		{0x4081cfea, 0xc0c555d5, 0x41cbfc6c, 0xc1e69522},
		{0xc09af47d, 0x405f3837, 0xc09af47d, 0x405f3837},
		{0x40832c84, 0x4094ff43, 0x41a55e4c, 0x41fdb32a},
		{0x3f1ae06f, 0x3faf14a6, 0xc12d6e26, 0xc20c6bb2},
		{0xbf55c971, 0x40df5ead, 0x4077473c, 0x41bed21b},
		{0xc04f42a2, 0x40b1e096, 0xc04f42a2, 0x40b1e096},
	}};
	CompareNativeCustomGrid(0x34, horizontal, vertical, words);
}

static void TestRetainedActualKnotGrid34() {
	// Synthetic sample/control words observed from the unmodified original inverse instructions.
	const std::array<std::uint32_t, 4> horizontal{0x00000000, 0x3eb33333, 0x3f2ccccc, 0x3f800000};
	const std::array<std::uint32_t, 5> vertical{0x00000000, 0x3eb33333, 0x3f111111, 0x3f488888, 0x3f800000};
	const std::array<std::array<std::uint32_t, 4>, 20> words{{
		{0x4071a1b8, 0xbeb3f28f, 0x4071a1b8, 0xbeb3f28f},
		{0xc087fc67, 0x3ff93b1e, 0xc0c213d6, 0xc0337e00},
		{0xc095de7b, 0x40a2f529, 0xc13e6850, 0x4161633e},
		{0x40b8406a, 0xbfe657c0, 0x40b8406a, 0xbfe657c0},
		{0x40b13b15, 0x4008df9f, 0x4283c99e, 0xc17aae72},
		{0xc08ff923, 0x3f183fbb, 0xc2fb2e64, 0x424b63c1},
		{0xc0a52f62, 0xc081fb86, 0x42fdf4be, 0xc228eccd},
		{0xc08b8072, 0xc06517c0, 0xc239ccb2, 0x421fd489},
		{0xc02c36e8, 0x401de775, 0xc2a7ab9e, 0x4209d740},
		{0xbee78b20, 0xc0632826, 0x43181ecc, 0xc1f3d10c},
		{0xc082bc2d, 0xbf9d2f2b, 0xc351f2f3, 0xc08ca0f0},
		{0xbf5a83f2, 0xc0c58f4a, 0x4257dbec, 0xc2999464},
		{0x40cb4c5e, 0xc0891bbb, 0x424b1b6e, 0xc1b00d0f},
		{0x404c7617, 0xc04fc928, 0xc2a7303d, 0xc217dcae},
		{0x409b42c8, 0x40eec4bc, 0x4307440a, 0x42453605},
		{0xc0f1a69c, 0x409f374c, 0xc21ad4cb, 0x423fc554},
		{0xc0cd8125, 0xbfc2d827, 0xc0cd8125, 0xbfc2d827},
		{0x40222ac9, 0x408cd69a, 0x418b6f16, 0x40dabf08},
		{0xc01ca8de, 0x403af4bd, 0xc16b7dee, 0x410e8222},
		{0x3ff2a987, 0xc0fe7051, 0x3ff2a987, 0xc0fe7051},
	}};
	CompareNativeCustomGrid(0x43, horizontal, vertical, words);
}

static void TestRetainedActualKnotGrid50() {
	// Synthetic sample/control words observed from the unmodified original inverse instructions.
	const std::array<std::uint32_t, 5> horizontal{0x00000000, 0x3eb33333, 0x3f111111, 0x3f488888, 0x3f800000};
	const std::array<std::uint32_t, 5> vertical{0x00000000, 0x3eb33333, 0x3f111111, 0x3f488888, 0x3f800000};
	const std::array<std::array<std::uint32_t, 4>, 25> words{{
		{0x40503e92, 0x40a1f654, 0x40503e92, 0x40a1f654},
		{0xc0edbf90, 0x4033dbb1, 0xc24513be, 0x41b20f25},
		{0x40a124a9, 0x3f393b8b, 0x41ffa9c6, 0xc1eeb60d},
		{0x40b29b58, 0x408ee8a6, 0x411f2320, 0x41bea0fd},
		{0xc0d65b7e, 0xc02e0630, 0xc0d65b7e, 0xc02e0630},
		{0xc0fe5611, 0x40d42d1f, 0xc28497d5, 0xc1fb6cc8},
		{0x40bf13ef, 0x40c66482, 0x4324044d, 0xc3aff07b},
		{0x3ea80f6e, 0x3f9dc30f, 0x42007104, 0x441d1a77},
		{0x408c7c34, 0xc0f30d31, 0x427e54ee, 0xc3a66922},
		{0xbe6d8da0, 0x40c1678e, 0xc1f4ef9d, 0x428353c4},
		{0x40c6ef0d, 0x40d5630d, 0x4267b2b8, 0x429424cf},
		{0xc03b16fe, 0x40af16ac, 0xc0c9f42c, 0x43da816c},
		{0xc0da3b6a, 0xc0d9e749, 0xc386cd6e, 0xc43457a5},
		{0xc013c974, 0xbff59575, 0x4143c1a8, 0x43930e02},
		{0x403608d2, 0xc0d33c33, 0x4252e386, 0xc285e8b4},
		{0x40b170e3, 0xc0d4d505, 0xc11d5ee2, 0xc23bdea0},
		{0x40854392, 0x40cd6080, 0xc1c05099, 0xc2db2e71},
		{0x40e6af11, 0x407ea283, 0x4326d1a8, 0x437732b6},
		{0xc03d113d, 0x40835c5b, 0xc1f2706b, 0xc281b8a6},
		{0xc0b88bd0, 0xc04824dc, 0xc20200f3, 0x41999dbd},
		{0x409399b4, 0x403b7042, 0x409399b4, 0x403b7042},
		{0xbfd92ab0, 0xbf516210, 0xc283a1ae, 0x41e05300},
		{0x40e7c492, 0xc0dcd8f4, 0x42aedf48, 0xc22d3d49},
		{0xc0660e68, 0xc0414aed, 0xc21c38c6, 0x4147f5a4},
		{0xbf6dc143, 0xc08b77fb, 0xbf6dc143, 0xc08b77fb},
	}};
	CompareNativeCustomGrid(0x44, horizontal, vertical, words);
}

int main(int argc, char** argv) {
	try {
		Require(argc <= 3, "usage: motion_bezier_inverse_tests [uniform-native-grids.bin [actual-knot-native-grids.bin]]");
		TestRetainedNativeGrid1();
		TestRetainedNativeGrid2();
		TestRetainedNativeGrid18();
		TestRetainedNativeGrid34();
		TestRetainedNativeGrid50();
		if (argc >= 2) {
			TestNativeGrids(argv[1]);
		}
		if (argc == 3) {
			TestNativeCustomGrids(argv[2]);
		}
		TestRetainedActualKnotGrid2();
		TestRetainedActualKnotGrid18();
		TestRetainedActualKnotGrid34();
		TestRetainedActualKnotGrid50();
		TestRejectedKnots();
		TestRejectedInputs();

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
