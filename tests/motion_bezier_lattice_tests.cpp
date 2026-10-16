#include "sgre/scene/motion_bezier_lattice.hpp"
#include "sgre/scene/motion_deformation.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

static void RequireFailure(const std::function<void()>& operation) {
	try {
		operation();
	} catch (const std::runtime_error&) {
		return;
	}
	throw std::runtime_error("unrecovered or malformed lattice was accepted");
}

static sgre::resource::MotionPoint Point(std::uint32_t x, std::uint32_t y) {
	sgre::resource::MotionPoint point;
	point.X = std::bit_cast<float>(x);
	point.Y = std::bit_cast<float>(y);

	return point;
}

static void TestNativeEvaluation0() {
	// Original 0x14053dff0 instructions, synthetic controls and coordinates only.
	const std::array<std::array<std::uint32_t, 2>, 20> control_bits{{
	    {0xbf2f9431, 0x3f9d1584},
	    {0xbf74ed0f, 0x3f35073e},
	    {0xbee15a78, 0xbfedc372},
	    {0xbf8b9d15, 0xbf951ef1},
	    {0xbed77622, 0xbfd14840},
	    {0xbeefe36b, 0x3fb38184},
	    {0xbff1f2ff, 0x3b652588},
	    {0xbf48b390, 0xbf6e1bea},
	    {0xbf826273, 0xbf8ee58e},
	    {0x3dd9b442, 0x3f10ff0e},
	    {0x3fa73a72, 0x3fd7ce30},
	    {0xbf047940, 0x3f9c73a2},
	    {0x3fa9d289, 0xbf79de49},
	    {0xbf831430, 0xbf44bd42},
	    {0x3f7c1371, 0x3f83d5a8},
	    {0xbff7436d, 0xbf9eb85c},
	    {0xbeb4d993, 0x3df2c5a8},
	    {0xbfb5cd39, 0x3fabb817},
	    {0xbc195799, 0x3e812aad},
	    {0x3fa4c609, 0x3e282349},
	}};
	const std::array<std::array<std::uint32_t, 4>, 32> vectors{{
	    {0x00000000, 0x00000000, 0xbf2f9431, 0x3f9d1584},
	    {0x3f800000, 0x3f800000, 0x3fa4c609, 0x3e282349},
	    {0x3e800000, 0x3f400000, 0xbee06ba9, 0x3eb54955},
	    {0x3f000000, 0x3f000000, 0xbef19c44, 0xbe846007},
	    {0xbe4ccccd, 0x3f99999a, 0xc121b8aa, 0xc09bc0c1},
	    {0x3f4961b6, 0x3f56c051, 0x3e1cceb8, 0x3e3d2a32},
	    {0x3f06513a, 0x3f875b7a, 0xbf481d5e, 0x3f2a8597},
	    {0x3f71938f, 0x3f6119ba, 0x3f574df9, 0x3ea6d37b},
	    {0x3f1947c6, 0xbd87d30b, 0xbf329881, 0xbf9bfae3},
	    {0x3de3c9bb, 0x3f9df12b, 0xc0655b09, 0xc022fd9f},
	    {0x3f998ab7, 0x3eb8e6a0, 0x3feccc2d, 0x3f928ab7},
	    {0x3e3f0c39, 0x3e6c1c95, 0xbf3f84fa, 0x3f10539c},
	    {0x3f499a4d, 0xbbc27b12, 0xbf38b104, 0xbfb59400},
	    {0x3e90b9aa, 0xbd1a133b, 0xbf3542a0, 0x3da129f0},
	    {0x3f2586a2, 0x3ec2bb38, 0xbf050197, 0xbf16af05},
	    {0x3f6dac72, 0x3f090cf9, 0x3e6f115f, 0x3e1da89b},
	    {0x3f8c54a6, 0xbe79d120, 0xbedf777a, 0xc09811f6},
	    {0x3f746fe5, 0x3f2207bb, 0x3f0439aa, 0x3eba4220},
	    {0x3f65af29, 0x3f5ac2f0, 0x3f1531a6, 0x3e8d8e16},
	    {0x3f303f2f, 0x3e80d724, 0xbf1eb6d6, 0xbf4dc7fe},
	    {0x3ecf285b, 0x3f8a0b39, 0xbf924fec, 0x3edfd5be},
	    {0x3ec01434, 0x3e8e1770, 0xbf3f9bc3, 0xbe32a252},
	    {0xbe30b812, 0x3d7a4ddb, 0xbd627cb1, 0x3fa993aa},
	    {0xbdfffa3e, 0xbe6e3166, 0xbf571248, 0x3ee17802},
	    {0x3f780bf1, 0x3f3c69fb, 0x3f42ee42, 0x3ee620ac},
	    {0x3f1e6913, 0x3f96168e, 0xbf526ee1, 0x3f8192da},
	    {0x3f0473c3, 0x3e587339, 0xbf3fec06, 0xbf276d70},
	    {0xbccdc268, 0x3ec16538, 0xbc15f4ca, 0x3faf4f27},
	    {0x3ed077a4, 0xbe0496db, 0xbeff440a, 0xbf0a266b},
	    {0x3f85fe80, 0x3f895364, 0x3fc46a7e, 0xbe07b1d3},
	    {0x3f922361, 0x3f4d4551, 0x400a499b, 0x3f926640},
	    {0xbd9f09a3, 0x3eee0e02, 0x3ec71003, 0x3fb10d26},
	}};
	sgre::resource::MotionBezierLattice lattice;
	lattice.EncodedDegree = 0x34;
	for (const auto& control : control_bits) {
		lattice.Controls.push_back(Point(control[0], control[1]));
	}

	for (const auto& sample : vectors) {
		const auto actual = sgre::scene::EvaluateMotionBezierLattice(lattice, Point(sample[0], sample[1]));
		Require(std::bit_cast<std::uint32_t>(actual.X) == sample[2] && std::bit_cast<std::uint32_t>(actual.Y) == sample[3], "native lattice point reduction differs");
	}
}

static void TestNativeEvaluation1() {
	// Original 0x14053d9e0 instructions, synthetic controls and coordinates only.
	const std::array<std::array<std::uint32_t, 2>, 20> control_bits{{
	    {0x3fbdbdc3, 0x3fd1d9f8},
	    {0x3ff8d596, 0x3ec27d2e},
	    {0xbf91058c, 0xbf5da8bb},
	    {0xbd5952bd, 0x3e049132},
	    {0xbfc363ec, 0xbf9bcde3},
	    {0x3e7cdb9e, 0x3fdb10b7},
	    {0xbf9867c7, 0xbe97ec7f},
	    {0xbf0991ee, 0xbf87b27e},
	    {0x3eb15e85, 0x3ffdc47f},
	    {0x3ebb0f02, 0xbfbc97a4},
	    {0x3f7a3e44, 0xbfc71373},
	    {0x3fda06a5, 0x3faa1fe6},
	    {0xbec96fa4, 0x3f729f7e},
	    {0x3f61a5a2, 0xbf688568},
	    {0x3f8f50d6, 0xbeb86979},
	    {0xbf87600e, 0x3f48d884},
	    {0xbfccc479, 0xbf858c0d},
	    {0xbe0a3c9b, 0xbf9d94c0},
	    {0x3ff59850, 0x3f8088e0},
	    {0x3ffc15b2, 0x3f847a2e},
	}};
	const std::array<std::array<std::uint32_t, 4>, 32> vectors{{
	    {0x00000000, 0x00000000, 0x3fbdbdc3, 0x3fd1d9f8},
	    {0x3f800000, 0x3f800000, 0x3ffc15b2, 0x3f847a2e},
	    {0x3e800000, 0x3f400000, 0x3da25e26, 0xbe7f81a6},
	    {0x3f000000, 0x3f000000, 0x3eb2ba27, 0xbe8dcc0a},
	    {0xbe4ccccd, 0x3f99999a, 0xc03bbaa8, 0xc035510f},
	    {0x3f4961b6, 0x3f56c051, 0x3f78a8e4, 0x3ed46a80},
	    {0x3f06513a, 0x3f875b7a, 0x3f63a122, 0x3d330f10},
	    {0x3f71938f, 0x3f6119ba, 0x3f83dd6b, 0x3f5a4404},
	    {0x3f1947c6, 0xbd87d30b, 0x3ee2360f, 0xbe550069},
	    {0x3de3c9bb, 0x3f9df12b, 0xc010aeae, 0xc025f7dc},
	    {0x3f998ab7, 0x3eb8e6a0, 0x3f245243, 0x3f74c1ef},
	    {0x3e3f0c39, 0x3e6c1c95, 0x3e6d109f, 0x3ec70cce},
	    {0x3f499a4d, 0xbbc27b12, 0xbe765790, 0xbe5e2e18},
	    {0x3e90b9aa, 0xbd1a133b, 0x3fc10023, 0x3f2f8899},
	    {0x3f2586a2, 0x3ec2bb38, 0x3e2f580c, 0xbe838829},
	    {0x3f6dac72, 0x3f090cf9, 0x3ecf92a4, 0x3ea7a7cd},
	    {0x3f8c54a6, 0xbe79d120, 0x40292310, 0x4094a94c},
	    {0x3f746fe5, 0x3f2207bb, 0x3eed68ea, 0x3f170f1d},
	    {0x3f65af29, 0x3f5ac2f0, 0x3f78e837, 0x3f37b703},
	    {0x3f303f2f, 0x3e80d724, 0xbd94da44, 0xbe7aca3e},
	    {0x3ecf285b, 0x3f8a0b39, 0x3e578038, 0xbee88256},
	    {0x3ec01434, 0x3e8e1770, 0x3e301848, 0x3db8ff0a},
	    {0xbe30b812, 0x3d7a4ddb, 0x3e0da9ba, 0x3f9a6cb7},
	    {0xbdfffa3e, 0xbe6e3166, 0x40ddf200, 0x412e89b5},
	    {0x3f780bf1, 0x3f3c69fb, 0x3f0cc473, 0x3f46ae97},
	    {0x3f1e6913, 0x3f96168e, 0x3ffcf69c, 0x3f145eaf},
	    {0x3f0473c3, 0x3e587339, 0x3d67a578, 0xbd148768},
	    {0xbccdc268, 0x3ec16538, 0xbec0cf36, 0x3f1abb09},
	    {0x3ed077a4, 0xbe0496db, 0x3fead09a, 0x3dda2d54},
	    {0x3f85fe80, 0x3f895364, 0x404a824c, 0x3f8a766b},
	    {0x3f922361, 0x3f4d4551, 0x3e4f268b, 0x3fa4d51d},
	    {0xbd9f09a3, 0x3eee0e02, 0xbf12fd30, 0x3f692369},
	}};
	sgre::resource::MotionBezierLattice lattice;
	lattice.EncodedDegree = 0x43;
	for (const auto& control : control_bits) {
		lattice.Controls.push_back(Point(control[0], control[1]));
	}

	for (const auto& sample : vectors) {
		const auto actual = sgre::scene::EvaluateMotionBezierLattice(lattice, Point(sample[0], sample[1]));
		Require(std::bit_cast<std::uint32_t>(actual.X) == sample[2] && std::bit_cast<std::uint32_t>(actual.Y) == sample[3], "native lattice point reduction differs");
	}
}

static void TestNativeEvaluation2() {
	// Original 0x14053d150 instructions, synthetic controls and coordinates only.
	const std::array<std::array<std::uint32_t, 2>, 16> control_bits{{
	    {0x3fc2728a, 0x3fee8082},
	    {0x3fc98afb, 0xbfcb98a6},
	    {0xbfcbbed1, 0x3fb53273},
	    {0x3fb72a3f, 0x3fd727ba},
	    {0x3edeab06, 0xbdd3de04},
	    {0xbf704342, 0xbf404541},
	    {0xbff8d922, 0x3f5ac1c5},
	    {0x3f999992, 0xbf814775},
	    {0x3fc586e4, 0xbd741d26},
	    {0x3f4fd202, 0xbeeed2af},
	    {0x3e6d0bfe, 0x3fd3c648},
	    {0x3dd03f33, 0xbf82a96f},
	    {0x3f110583, 0x3efd4027},
	    {0x3fbe44de, 0xbff5b62c},
	    {0xbdb3ab60, 0x3fdb31e9},
	    {0xbff6b268, 0x3f12593f},
	}};
	const std::array<std::array<std::uint32_t, 4>, 32> vectors{{
	    {0x00000000, 0x00000000, 0x3fc2728a, 0x3fee8082},
	    {0x3f800000, 0x3f800000, 0xbff6b268, 0x3f12593f},
	    {0x3e800000, 0x3f400000, 0x3f3b89ca, 0xbe398019},
	    {0x3f000000, 0x3f000000, 0xbc7b29a0, 0x3e0818a8},
	    {0xbe4ccccd, 0x3f99999a, 0xc004badd, 0x408acb71},
	    {0x3f4961b6, 0x3f56c051, 0xbed2aaba, 0x3ef1c3a6},
	    {0x3f06513a, 0x3f875b7a, 0x3e56f542, 0x3dab0b18},
	    {0x3f71938f, 0x3f6119ba, 0xbf84a15e, 0x3e963e2a},
	    {0x3f1947c6, 0xbd87d30b, 0x3ecea9e7, 0x3f499d20},
	    {0x3de3c9bb, 0x3f9df12b, 0x3d72033e, 0xbe07e018},
	    {0x3f998ab7, 0x3eb8e6a0, 0x4021e164, 0xbfcf0fcc},
	    {0x3e3f0c39, 0x3e6c1c95, 0x3f1f0fe9, 0x3e0144c0},
	    {0x3f499a4d, 0xbbc27b12, 0x3e85fbcd, 0x3fa0a629},
	    {0x3e90b9aa, 0xbd1a133b, 0x3f9919d2, 0x3eac78ff},
	    {0x3f2586a2, 0x3ec2bb38, 0xbe4bf738, 0x3e890eb4},
	    {0x3f6dac72, 0x3f090cf9, 0x3de0fbe0, 0xbe260455},
	    {0x3f8c54a6, 0xbe79d120, 0x40141a01, 0x40951557},
	    {0x3f746fe5, 0x3f2207bb, 0xbdec3bc4, 0xbe7a6c24},
	    {0x3f65af29, 0x3f5ac2f0, 0xbf48c7cc, 0x3eb671b5},
	    {0x3f303f2f, 0x3e80d724, 0xbe35a3db, 0x3ec8209a},
	    {0x3ecf285b, 0x3f8a0b39, 0x3f09dcf6, 0xbe93ad60},
	    {0x3ec01434, 0x3e8e1770, 0x3db870fc, 0x3cc9d950},
	    {0xbe30b812, 0x3d7a4ddb, 0x3f94db15, 0x406797ea},
	    {0xbdfffa3e, 0xbe6e3166, 0x3feaded1, 0x40c89055},
	    {0x3f780bf1, 0x3f3c69fb, 0xbef9e84b, 0xbe2fe9fe},
	    {0x3f1e6913, 0x3f96168e, 0xbefb8470, 0x3ebf5bde},
	    {0x3f0473c3, 0x3e587339, 0xbe006db6, 0x3e4fe9d2},
	    {0xbccdc268, 0x3ec16538, 0x3f86b44e, 0x3f05f623},
	    {0x3ed077a4, 0xbe0496db, 0x3faa95dc, 0x3edd1f72},
	    {0x3f85fe80, 0x3f895364, 0xc02c7aba, 0x3f4dfa5d},
	    {0x3f922361, 0x3f4d4551, 0xbf8adaaf, 0xbf9ef1d5},
	    {0xbd9f09a3, 0x3eee0e02, 0x3f934ba9, 0x3f1c4ae7},
	}};
	sgre::resource::MotionBezierLattice lattice;
	lattice.EncodedDegree = 0x33;
	for (const auto& control : control_bits) {
		lattice.Controls.push_back(Point(control[0], control[1]));
	}

	for (const auto& sample : vectors) {
		const auto actual = sgre::scene::EvaluateMotionBezierLattice(lattice, Point(sample[0], sample[1]));
		Require(std::bit_cast<std::uint32_t>(actual.X) == sample[2] && std::bit_cast<std::uint32_t>(actual.Y) == sample[3], "native lattice point reduction differs");
	}
}

static void TestNativeElevation1() {
	// Original 0x140553a80 curve elevation embedded in each row of a genuine two-row lattice.
	const std::array<std::array<std::uint32_t, 2>, 2> controls{{
	    {0x40967d5e, 0xc0b40c97},
	    {0xc11e0c09, 0xbfe044c2},
	}};
	const std::array<std::array<std::uint32_t, 2>, 3> expected{{
	    {0x40967d5e, 0xc0b40c97},
	    {0xc0259ab4, 0xc06c1dc8},
	    {0xc11e0c09, 0xbfe044c2},
	}};
	sgre::resource::MotionBezierLattice lattice;
	lattice.EncodedDegree = 0x11;
	for (std::size_t row = 0; row < 2; ++row) {
		for (const auto& control : controls) {
			lattice.Controls.push_back(Point(control[0], control[1]));
		}
	}

	const auto elevated = sgre::scene::ElevateMotionBezierLattice(lattice, 0x12);
	Require(elevated.Controls.size() == expected.size() * 2, "elevation lost an actual row");
	for (std::size_t index = 0; index < elevated.Controls.size(); ++index) {
		const auto& actual = elevated.Controls[index];
		const auto& target = expected[index % expected.size()];
		Require(std::bit_cast<std::uint32_t>(actual.X) == target[0] && std::bit_cast<std::uint32_t>(actual.Y) == target[1], "native curve elevation differs");
	}
}

static void TestNativeElevation3() {
	// Original 0x140553a80 curve elevation embedded in each row of a genuine two-row lattice.
	const std::array<std::array<std::uint32_t, 2>, 4> controls{{
	    {0x40f2e9d2, 0x4048fceb},
	    {0x3f9b262e, 0x3f282441},
	    {0xc0240364, 0x40411241},
	    {0xbf813d4a, 0x405154cb},
	}};
	const std::array<std::array<std::uint32_t, 2>, 5> expected{{
	    {0x40f2e9d2, 0x4048fceb},
	    {0x4033a33a, 0x3fa38c0e},
	    {0xbf2ce09a, 0x3feb1b51},
	    {0xc00b2a34, 0x404522e4},
	    {0xbf813d4a, 0x405154cb},
	}};
	sgre::resource::MotionBezierLattice lattice;
	lattice.EncodedDegree = 0x13;
	for (std::size_t row = 0; row < 2; ++row) {
		for (const auto& control : controls) {
			lattice.Controls.push_back(Point(control[0], control[1]));
		}
	}

	const auto elevated = sgre::scene::ElevateMotionBezierLattice(lattice, 0x14);
	Require(elevated.Controls.size() == expected.size() * 2, "elevation lost an actual row");
	for (std::size_t index = 0; index < elevated.Controls.size(); ++index) {
		const auto& actual = elevated.Controls[index];
		const auto& target = expected[index % expected.size()];
		Require(std::bit_cast<std::uint32_t>(actual.X) == target[0] && std::bit_cast<std::uint32_t>(actual.Y) == target[1], "native curve elevation differs");
	}
}

static void TestNativeElevation4() {
	// Original 0x140553a80 curve elevation embedded in each row of a genuine two-row lattice.
	const std::array<std::array<std::uint32_t, 2>, 5> controls{{
	    {0x3fd465e0, 0x41021480},
	    {0x40bbf726, 0x40ddb400},
	    {0x40a97217, 0xc0ba9fe0},
	    {0x40a47b74, 0xc01677d4},
	    {0x4102827f, 0xc0238707},
	}};
	const std::array<std::array<std::uint32_t, 2>, 6> expected{{
	    {0x3fd465e0, 0x41021480},
	    {0x40a0fe03, 0x40e56500},
	    {0x40b0da83, 0xbf3a5900},
	    {0x40a775d6, 0xc08e117e},
	    {0x40b7ca2a, 0xc0191478},
	    {0x4102827f, 0xc0238707},
	}};
	sgre::resource::MotionBezierLattice lattice;
	lattice.EncodedDegree = 0x14;
	for (std::size_t row = 0; row < 2; ++row) {
		for (const auto& control : controls) {
			lattice.Controls.push_back(Point(control[0], control[1]));
		}
	}

	const auto elevated = sgre::scene::ElevateMotionBezierLattice(lattice, 0x15);
	Require(elevated.Controls.size() == expected.size() * 2, "elevation lost an actual row");
	for (std::size_t index = 0; index < elevated.Controls.size(); ++index) {
		const auto& actual = elevated.Controls[index];
		const auto& target = expected[index % expected.size()];
		Require(std::bit_cast<std::uint32_t>(actual.X) == target[0] && std::bit_cast<std::uint32_t>(actual.Y) == target[1], "native curve elevation differs");
	}
}

static void TestNativeElevation8() {
	// Original 0x140553a80 curve elevation embedded in each row of a genuine two-row lattice.
	const std::array<std::array<std::uint32_t, 2>, 9> controls{{
	    {0xc029f8af, 0x40febbdc},
	    {0x411f853a, 0xbee3267c},
	    {0x40411521, 0x3f4a2665},
	    {0xc0af21d8, 0xbf8e6194},
	    {0x4056fc5e, 0x4115ebc6},
	    {0x4114fbdc, 0x410c16d3},
	    {0xbfc95384, 0xc06f54f4},
	    {0x4012f37d, 0x410b2468},
	    {0xbed22b92, 0xc0e3c7be},
	}};
	const std::array<std::array<std::uint32_t, 2>, 10> expected{{
	    {0xc029f8af, 0x40febbdc},
	    {0x41091313, 0x3efaf2e0},
	    {0x4091fc52, 0x3f03fd24},
	    {0xc0292615, 0xbef4ea9d},
	    {0xbf10f22e, 0x4096c23e},
	    {0x40c025fb, 0x41118d22},
	    {0x40b5de30, 0x4092e59b},
	    {0xbf368d0a, 0xbf79dc32},
	    {0x3fff688d, 0x40de0e15},
	    {0xbed22b92, 0xc0e3c7be},
	}};
	sgre::resource::MotionBezierLattice lattice;
	lattice.EncodedDegree = 0x18;
	for (std::size_t row = 0; row < 2; ++row) {
		for (const auto& control : controls) {
			lattice.Controls.push_back(Point(control[0], control[1]));
		}
	}

	const auto elevated = sgre::scene::ElevateMotionBezierLattice(lattice, 0x19);
	Require(elevated.Controls.size() == expected.size() * 2, "elevation lost an actual row");
	for (std::size_t index = 0; index < elevated.Controls.size(); ++index) {
		const auto& actual = elevated.Controls[index];
		const auto& target = expected[index % expected.size()];
		Require(std::bit_cast<std::uint32_t>(actual.X) == target[0] && std::bit_cast<std::uint32_t>(actual.Y) == target[1], "native curve elevation differs");
	}
}

static void TestNativeElevation14() {
	// Original 0x140553a80 curve elevation embedded in each row of a genuine two-row lattice.
	const std::array<std::array<std::uint32_t, 2>, 15> controls{{
	    {0x40e492ac, 0xbf2b64a1},
	    {0xc012bf89, 0x40570f00},
	    {0xc00a10b0, 0xc08907dc},
	    {0xc01d4b05, 0x41170e09},
	    {0x40b67c47, 0x408904e0},
	    {0xc105fc5f, 0x3f34a7e1},
	    {0xbdb95bc9, 0xc0b978e2},
	    {0x40ff4c4f, 0x410dd86d},
	    {0xc0aadccc, 0x40cb7982},
	    {0x40eeb4fa, 0x40db2a79},
	    {0xc1035e9f, 0xbf52945f},
	    {0x40fb2d09, 0x41045bb1},
	    {0x40b28368, 0x40454c35},
	    {0xc0879f25, 0x3fdea88a},
	    {0xbfff38ed, 0xc118fcd1},
	}};
	const std::array<std::array<std::uint32_t, 2>, 16> expected{{
	    {0x40e492ac, 0xbf2b64a1},
	    {0xbfd4fa28, 0x4045dd64},
	    {0xc00b3912, 0xc050d86e},
	    {0xc019728e, 0x40d64817},
	    {0x4061b357, 0x40b50b10},
	    {0xc06ba37a, 0x3ff2e921},
	    {0xc059da51, 0xc04c8046},
	    {0x4086ceb3, 0x40017f05},
	    {0x3fe1b150, 0x40f641b2},
	    {0xbe611c67, 0x40d1c04b},
	    {0x400f1dce, 0x40895623},
	    {0xc07b63f2, 0x3fcd26d0},
	    {0x40eca4b6, 0x40e780bb},
	    {0x4088a0ee, 0x4039d5d2},
	    {0xc082d57c, 0x3f7c7135},
	    {0xbfff38ed, 0xc118fcd1},
	}};
	sgre::resource::MotionBezierLattice lattice;
	lattice.EncodedDegree = 0x1e;
	for (std::size_t row = 0; row < 2; ++row) {
		for (const auto& control : controls) {
			lattice.Controls.push_back(Point(control[0], control[1]));
		}
	}

	const auto elevated = sgre::scene::ElevateMotionBezierLattice(lattice, 0x1f);
	Require(elevated.Controls.size() == expected.size() * 2, "elevation lost an actual row");
	for (std::size_t index = 0; index < elevated.Controls.size(); ++index) {
		const auto& actual = elevated.Controls[index];
		const auto& target = expected[index % expected.size()];
		Require(std::bit_cast<std::uint32_t>(actual.X) == target[0] && std::bit_cast<std::uint32_t>(actual.Y) == target[1], "native curve elevation differs");
	}
}

static void TestRetainedAssignment() {
	auto identity = sgre::scene::IdentityMotionBezierLattice(0x34);
	Require(identity.Controls.size() == 20 && identity.Controls[9].X == 1 && identity.Controls[19].Y == 1, "quartic dimensions were reduced to a cubic patch");
	sgre::resource::MotionBezierLattice current;
	Require(!sgre::scene::AssignMotionBezierLattice(current, &identity, true) && current.EncodedDegree == 0, "empty native owner must suppress an identity write");
	auto moved = identity;
	moved.Controls[7].X += 0.1f;
	Require(sgre::scene::AssignMotionBezierLattice(current, &moved, true), "actual deformation was not retained");
	auto near = moved;
	near.Controls[7].X += 0.0005f;
	Require(!sgre::scene::AssignMotionBezierLattice(current, &near, true) && current.Controls[7].X == moved.Controls[7].X, "native comparison is squared distance, and an equal write preserves the retained controls");
	near.Controls[7].X += 0.001f;
	Require(sgre::scene::AssignMotionBezierLattice(current, &near, true), "component squared distance above tolerance did not dirty the native owner");
	Require(sgre::scene::AssignMotionBezierLattice(current, &identity, true) && current.EncodedDegree == 0x34, "a changed native write copies even an identity before subsequent comparison");
	Require(sgre::scene::AssignMotionBezierLattice(current, &identity, true) && current.EncodedDegree == 0, "equal retained identity must clear its actual owner");
	Require(sgre::scene::AssignMotionBezierLattice(current, nullptr, false), "unconditional native clear must publish dirty even for an empty owner");
	Require(!sgre::scene::AssignMotionBezierLattice(current, nullptr, true), "comparison clear must preserve an already empty owner");
}

static void TestCombineAndSafety() {
	auto cubic = sgre::scene::IdentityMotionBezierLattice(0x33);
	auto quartic = sgre::scene::IdentityMotionBezierLattice(0x34);
	for (auto& point : cubic.Controls) {
		point.X += 1;
	}
	for (auto& point : quartic.Controls) {
		point.Y -= 2;
	}
	const auto combined = sgre::scene::CombineMotionBezierLattices(cubic, quartic);
	const auto position = sgre::scene::EvaluateMotionBezierLattice(combined, Point(0x3e800000, 0x3f400000));
	Require(combined.EncodedDegree == 0x34 && std::abs(position.X - 1.25f) < 0.000001f && std::abs(position.Y + 1.25f) < 0.000001f, "mixed cubic/quartic additive deformation lost its source degree or translation");
	const auto vertical = sgre::scene::ElevateMotionBezierLattice(cubic, 0x43);
	Require(vertical.Controls.size() == 20 && vertical.Controls.front().X == cubic.Controls.front().X && vertical.Controls.back().Y == cubic.Controls.back().Y, "vertical curve elevation lost endpoint ownership");
	const auto crossed = sgre::scene::CombineMotionBezierLattices(quartic, vertical);
	const auto crossed_point = sgre::scene::EvaluateMotionBezierLattice(crossed, Point(0x3e800000, 0x3f400000));
	Require(crossed.EncodedDegree == 0x43 && std::abs(crossed_point.X - 1.25f) < 0.000002f && std::abs(crossed_point.Y + 1.25f) < 0.000002f, "crossed native degrees must retain the incoming degree through true collocation conversion");
	RequireFailure([&] {
		static_cast<void>(sgre::scene::ElevateMotionBezierLattice(quartic, 0x33));
	});
	RequireFailure([] {
		static_cast<void>(sgre::scene::IdentityMotionBezierLattice(0x10));
	});
	auto malformed = cubic;
	malformed.Controls.pop_back();
	RequireFailure([&] {
		sgre::scene::ValidateMotionBezierLattice(malformed);
	});
	malformed = cubic;
	malformed.Controls[0].X = std::numeric_limits<float>::infinity();
	RequireFailure([&] {
		sgre::scene::ValidateMotionBezierLattice(malformed);
	});
	RequireFailure([&] {
		static_cast<void>(sgre::scene::EvaluateMotionBezierLattice(cubic, Point(0x7fc00000, 0)));
	});
	Require(sgre::scene::MotionBezierLatticeControlCount(0xff) == 256 && sgre::scene::MotionBezierLatticeControlCount(0) == 0, "encoded lattice dimensions exceed their actual nibble bound");
}

static void TestNativeCachedGrid0() {
	// Original cached basis53fcb0/53fd60 and full grid541700/540e60 instructions.
	const std::array<std::array<std::uint32_t, 2>, 20> controls{{
	    {0xbf2f9431, 0x3f9d1584},
	    {0xbf74ed0f, 0x3f35073e},
	    {0xbee15a78, 0xbfedc372},
	    {0xbf8b9d15, 0xbf951ef1},
	    {0xbed77622, 0xbfd14840},
	    {0xbeefe36b, 0x3fb38184},
	    {0xbff1f2ff, 0x3b652588},
	    {0xbf48b390, 0xbf6e1bea},
	    {0xbf826273, 0xbf8ee58e},
	    {0x3dd9b442, 0x3f10ff0e},
	    {0x3fa73a72, 0x3fd7ce30},
	    {0xbf047940, 0x3f9c73a2},
	    {0x3fa9d289, 0xbf79de49},
	    {0xbf831430, 0xbf44bd42},
	    {0x3f7c1371, 0x3f83d5a8},
	    {0xbff7436d, 0xbf9eb85c},
	    {0xbeb4d993, 0x3df2c5a8},
	    {0xbfb5cd39, 0x3fabb817},
	    {0xbc195799, 0x3e812aad},
	    {0x3fa4c609, 0x3e282349},
	}};
	const std::array<std::array<std::uint32_t, 2>, 4> expected{{
	    {0xbf2f9431, 0x3f9d1584},
	    {0xbed77622, 0xbfd14840},
	    {0xbff7436d, 0xbf9eb85c},
	    {0x3fa4c609, 0x3e282349},
	}};
	sgre::resource::MotionBezierLattice lattice;
	lattice.EncodedDegree = 0x34;
	for (const auto& point : controls) {
		lattice.Controls.push_back(Point(point[0], point[1]));
	}
	sgre::scene::MotionMeshSubdivision subdivision;
	subdivision.X = 1;
	subdivision.Y = 1;
	sgre::scene::MotionDeformationWindow window;
	window.Left = std::bit_cast<float>(std::uint32_t{0x00000000});
	window.Top = std::bit_cast<float>(std::uint32_t{0x00000000});
	window.Right = std::bit_cast<float>(std::uint32_t{0x3f800000});
	window.Bottom = std::bit_cast<float>(std::uint32_t{0x3f800000});
	std::array<sgre::resource::MotionPoint, 4> positions;
	sgre::scene::EvaluateMotionBezierLatticeGrid(lattice, subdivision, window, positions);
	for (std::size_t index = 0; index < positions.size(); ++index) {
		Require(std::bit_cast<std::uint32_t>(positions[index].X) == expected[index][0] && std::bit_cast<std::uint32_t>(positions[index].Y) == expected[index][1], "native quartic cached grid differs");
	}
}

static void TestNativeCachedGrid1() {
	// Original cached basis53fcb0/53fd60 and full grid541700/540e60 instructions.
	const std::array<std::array<std::uint32_t, 2>, 20> controls{{
	    {0xbf2f9431, 0x3f9d1584},
	    {0xbf74ed0f, 0x3f35073e},
	    {0xbee15a78, 0xbfedc372},
	    {0xbf8b9d15, 0xbf951ef1},
	    {0xbed77622, 0xbfd14840},
	    {0xbeefe36b, 0x3fb38184},
	    {0xbff1f2ff, 0x3b652588},
	    {0xbf48b390, 0xbf6e1bea},
	    {0xbf826273, 0xbf8ee58e},
	    {0x3dd9b442, 0x3f10ff0e},
	    {0x3fa73a72, 0x3fd7ce30},
	    {0xbf047940, 0x3f9c73a2},
	    {0x3fa9d289, 0xbf79de49},
	    {0xbf831430, 0xbf44bd42},
	    {0x3f7c1371, 0x3f83d5a8},
	    {0xbff7436d, 0xbf9eb85c},
	    {0xbeb4d993, 0x3df2c5a8},
	    {0xbfb5cd39, 0x3fabb817},
	    {0xbc195799, 0x3e812aad},
	    {0x3fa4c609, 0x3e282349},
	}};
	const std::array<std::array<std::uint32_t, 2>, 12> expected{{
	    {0xbe88a07d, 0x3fb39950},
	    {0xbf512811, 0x3d2753c0},
	    {0xbf3b48c2, 0xbf963132},
	    {0x3e57c178, 0xbfb01d99},
	    {0x3f3e7404, 0x3fb8db26},
	    {0xbf0b9a0c, 0x3e8211c5},
	    {0xbeb9dbed, 0xbee02954},
	    {0x3fb27614, 0x3f8a2fa0},
	    {0xbf9f0cb2, 0xbf3283a4},
	    {0xbf19814c, 0x3e805580},
	    {0xbd42f69f, 0x3e779f60},
	    {0x40011926, 0x3f51485c},
	}};
	sgre::resource::MotionBezierLattice lattice;
	lattice.EncodedDegree = 0x34;
	for (const auto& point : controls) {
		lattice.Controls.push_back(Point(point[0], point[1]));
	}
	sgre::scene::MotionMeshSubdivision subdivision;
	subdivision.X = 3;
	subdivision.Y = 2;
	sgre::scene::MotionDeformationWindow window;
	window.Left = std::bit_cast<float>(std::uint32_t{0xbe000000});
	window.Top = std::bit_cast<float>(std::uint32_t{0x3d800000});
	window.Right = std::bit_cast<float>(std::uint32_t{0x3f900000});
	window.Bottom = std::bit_cast<float>(std::uint32_t{0x3f600000});
	std::array<sgre::resource::MotionPoint, 12> positions;
	sgre::scene::EvaluateMotionBezierLatticeGrid(lattice, subdivision, window, positions);
	for (std::size_t index = 0; index < positions.size(); ++index) {
		Require(std::bit_cast<std::uint32_t>(positions[index].X) == expected[index][0] && std::bit_cast<std::uint32_t>(positions[index].Y) == expected[index][1], "native quartic cached grid differs");
	}
}

static void TestNativeCachedGrid2() {
	// Original cached basis53fcb0/53fd60 and full grid541700/540e60 instructions.
	const std::array<std::array<std::uint32_t, 2>, 20> controls{{
	    {0xbf2f9431, 0x3f9d1584},
	    {0xbf74ed0f, 0x3f35073e},
	    {0xbee15a78, 0xbfedc372},
	    {0xbf8b9d15, 0xbf951ef1},
	    {0xbed77622, 0xbfd14840},
	    {0xbeefe36b, 0x3fb38184},
	    {0xbff1f2ff, 0x3b652588},
	    {0xbf48b390, 0xbf6e1bea},
	    {0xbf826273, 0xbf8ee58e},
	    {0x3dd9b442, 0x3f10ff0e},
	    {0x3fa73a72, 0x3fd7ce30},
	    {0xbf047940, 0x3f9c73a2},
	    {0x3fa9d289, 0xbf79de49},
	    {0xbf831430, 0xbf44bd42},
	    {0x3f7c1371, 0x3f83d5a8},
	    {0xbff7436d, 0xbf9eb85c},
	    {0xbeb4d993, 0x3df2c5a8},
	    {0xbfb5cd39, 0x3fabb817},
	    {0xbc195799, 0x3e812aad},
	    {0x3fa4c609, 0x3e282349},
	}};
	const std::array<std::array<std::uint32_t, 2>, 35> expected{{
	    {0xbf2682fa, 0x3f737454},
	    {0xbf16e34f, 0x3eb3eebe},
	    {0xbf11db08, 0xbeab86f0},
	    {0xbf1ae3c2, 0xbf6f098c},
	    {0xbf2d8c12, 0xbfaf99f2},
	    {0xbf3d7792, 0xbfd6d3a1},
	    {0xbf365eda, 0xbffdea0e},
	    {0xbf2b796a, 0x3f6f216e},
	    {0xbf4c7848, 0x3eb19216},
	    {0xbf4e2180, 0xbe51c7ee},
	    {0xbf437d62, 0xbf2023dd},
	    {0xbf3352c9, 0xbf5bc177},
	    {0xbf18272a, 0xbf62a053},
	    {0xbec07cf7, 0xbf39f69a},
	    {0xbea5f778, 0x3f61d4c4},
	    {0xbefc133c, 0x3ee96e2c},
	    {0xbf032b1e, 0x3d472440},
	    {0xbef19c44, 0xbe846007},
	    {0xbecaddd0, 0xbeccd878},
	    {0xbe7e601f, 0xbea70a0e},
	    {0x3d90eca0, 0xbb432e00},
	    {0xbf08ea1d, 0x3e8971c4},
	    {0xbeff39a9, 0x3e9ecc0e},
	    {0xbee0828a, 0x3e886566},
	    {0xbeb21b16, 0x3e3c25e3},
	    {0xbe496a15, 0x3deccdf2},
	    {0x3d8ae100, 0x3dfabe18},
	    {0x3f0577fe, 0x3e8af93c},
	    {0xc00e492b, 0xbfb783d4},
	    {0xbfd29bb4, 0xbeeb0b37},
	    {0xbfaa56aa, 0x3e943176},
	    {0xbf81398f, 0x3f37c007},
	    {0xbf088fb7, 0x3f4d1e58},
	    {0x3dfedcce, 0x3f15a326},
	    {0x3f5e9364, 0x3e39fd09},
	}};
	sgre::resource::MotionBezierLattice lattice;
	lattice.EncodedDegree = 0x34;
	for (const auto& point : controls) {
		lattice.Controls.push_back(Point(point[0], point[1]));
	}
	sgre::scene::MotionMeshSubdivision subdivision;
	subdivision.X = 6;
	subdivision.Y = 4;
	sgre::scene::MotionDeformationWindow window;
	window.Left = std::bit_cast<float>(std::uint32_t{0x3dcccccd});
	window.Top = std::bit_cast<float>(std::uint32_t{0xbdcccccd});
	window.Right = std::bit_cast<float>(std::uint32_t{0x3f666666});
	window.Bottom = std::bit_cast<float>(std::uint32_t{0x3f8ccccd});
	std::array<sgre::resource::MotionPoint, 35> positions;
	sgre::scene::EvaluateMotionBezierLatticeGrid(lattice, subdivision, window, positions);
	for (std::size_t index = 0; index < positions.size(); ++index) {
		Require(std::bit_cast<std::uint32_t>(positions[index].X) == expected[index][0] && std::bit_cast<std::uint32_t>(positions[index].Y) == expected[index][1], "native quartic cached grid differs");
	}
}

static void TestNativeCachedGrid3() {
	// Original cached basis53fcb0/53fd60 and full grid541700/540e60 instructions.
	const std::array<std::array<std::uint32_t, 2>, 20> controls{{
	    {0x3fbdbdc3, 0x3fd1d9f8},
	    {0x3ff8d596, 0x3ec27d2e},
	    {0xbf91058c, 0xbf5da8bb},
	    {0xbd5952bd, 0x3e049132},
	    {0xbfc363ec, 0xbf9bcde3},
	    {0x3e7cdb9e, 0x3fdb10b7},
	    {0xbf9867c7, 0xbe97ec7f},
	    {0xbf0991ee, 0xbf87b27e},
	    {0x3eb15e85, 0x3ffdc47f},
	    {0x3ebb0f02, 0xbfbc97a4},
	    {0x3f7a3e44, 0xbfc71373},
	    {0x3fda06a5, 0x3faa1fe6},
	    {0xbec96fa4, 0x3f729f7e},
	    {0x3f61a5a2, 0xbf688568},
	    {0x3f8f50d6, 0xbeb86979},
	    {0xbf87600e, 0x3f48d884},
	    {0xbfccc479, 0xbf858c0d},
	    {0xbe0a3c9b, 0xbf9d94c0},
	    {0x3ff59850, 0x3f8088e0},
	    {0x3ffc15b2, 0x3f847a2e},
	}};
	const std::array<std::array<std::uint32_t, 2>, 4> expected{{
	    {0x3fbdbdc3, 0x3fd1d9f8},
	    {0xbd5952bd, 0x3e049132},
	    {0xbfccc479, 0xbf858c0d},
	    {0x3ffc15b2, 0x3f847a2e},
	}};
	sgre::resource::MotionBezierLattice lattice;
	lattice.EncodedDegree = 0x43;
	for (const auto& point : controls) {
		lattice.Controls.push_back(Point(point[0], point[1]));
	}
	sgre::scene::MotionMeshSubdivision subdivision;
	subdivision.X = 1;
	subdivision.Y = 1;
	sgre::scene::MotionDeformationWindow window;
	window.Left = std::bit_cast<float>(std::uint32_t{0x00000000});
	window.Top = std::bit_cast<float>(std::uint32_t{0x00000000});
	window.Right = std::bit_cast<float>(std::uint32_t{0x3f800000});
	window.Bottom = std::bit_cast<float>(std::uint32_t{0x3f800000});
	std::array<sgre::resource::MotionPoint, 4> positions;
	sgre::scene::EvaluateMotionBezierLatticeGrid(lattice, subdivision, window, positions);
	for (std::size_t index = 0; index < positions.size(); ++index) {
		Require(std::bit_cast<std::uint32_t>(positions[index].X) == expected[index][0] && std::bit_cast<std::uint32_t>(positions[index].Y) == expected[index][1], "native quartic cached grid differs");
	}
}

static void TestNativeCachedGrid4() {
	// Original cached basis53fcb0/53fd60 and full grid541700/540e60 instructions.
	const std::array<std::array<std::uint32_t, 2>, 20> controls{{
	    {0x3fbdbdc3, 0x3fd1d9f8},
	    {0x3ff8d596, 0x3ec27d2e},
	    {0xbf91058c, 0xbf5da8bb},
	    {0xbd5952bd, 0x3e049132},
	    {0xbfc363ec, 0xbf9bcde3},
	    {0x3e7cdb9e, 0x3fdb10b7},
	    {0xbf9867c7, 0xbe97ec7f},
	    {0xbf0991ee, 0xbf87b27e},
	    {0x3eb15e85, 0x3ffdc47f},
	    {0x3ebb0f02, 0xbfbc97a4},
	    {0x3f7a3e44, 0xbfc71373},
	    {0x3fda06a5, 0x3faa1fe6},
	    {0xbec96fa4, 0x3f729f7e},
	    {0x3f61a5a2, 0xbf688568},
	    {0x3f8f50d6, 0xbeb86979},
	    {0xbf87600e, 0x3f48d884},
	    {0xbfccc479, 0xbf858c0d},
	    {0xbe0a3c9b, 0xbf9d94c0},
	    {0x3ff59850, 0x3f8088e0},
	    {0x3ffc15b2, 0x3f847a2e},
	}};
	const std::array<std::array<std::uint32_t, 2>, 12> expected{{
	    {0x3ec997be, 0x3f963d67},
	    {0x3f47ff0e, 0x3f017228},
	    {0xbe62beaa, 0xbe6ae3da},
	    {0x3edec008, 0x3e86fc4c},
	    {0xbf3cca02, 0x3f892d1d},
	    {0x3e33d985, 0xbc6195a0},
	    {0x3eaa9502, 0xbe5f9f4f},
	    {0x3ec54d72, 0x3f745f45},
	    {0xbfc4c49a, 0x3ea44615},
	    {0x3d19dcd0, 0xbeebd848},
	    {0x3f83d98d, 0x3e942d76},
	    {0x3f14fd1d, 0x3f9180c4},
	}};
	sgre::resource::MotionBezierLattice lattice;
	lattice.EncodedDegree = 0x43;
	for (const auto& point : controls) {
		lattice.Controls.push_back(Point(point[0], point[1]));
	}
	sgre::scene::MotionMeshSubdivision subdivision;
	subdivision.X = 3;
	subdivision.Y = 2;
	sgre::scene::MotionDeformationWindow window;
	window.Left = std::bit_cast<float>(std::uint32_t{0xbe000000});
	window.Top = std::bit_cast<float>(std::uint32_t{0x3d800000});
	window.Right = std::bit_cast<float>(std::uint32_t{0x3f900000});
	window.Bottom = std::bit_cast<float>(std::uint32_t{0x3f600000});
	std::array<sgre::resource::MotionPoint, 12> positions;
	sgre::scene::EvaluateMotionBezierLatticeGrid(lattice, subdivision, window, positions);
	for (std::size_t index = 0; index < positions.size(); ++index) {
		Require(std::bit_cast<std::uint32_t>(positions[index].X) == expected[index][0] && std::bit_cast<std::uint32_t>(positions[index].Y) == expected[index][1], "native quartic cached grid differs");
	}
}

static void TestNativeCachedGrid5() {
	// Original cached basis53fcb0/53fd60 and full grid541700/540e60 instructions.
	const std::array<std::array<std::uint32_t, 2>, 20> controls{{
	    {0x3fbdbdc3, 0x3fd1d9f8},
	    {0x3ff8d596, 0x3ec27d2e},
	    {0xbf91058c, 0xbf5da8bb},
	    {0xbd5952bd, 0x3e049132},
	    {0xbfc363ec, 0xbf9bcde3},
	    {0x3e7cdb9e, 0x3fdb10b7},
	    {0xbf9867c7, 0xbe97ec7f},
	    {0xbf0991ee, 0xbf87b27e},
	    {0x3eb15e85, 0x3ffdc47f},
	    {0x3ebb0f02, 0xbfbc97a4},
	    {0x3f7a3e44, 0xbfc71373},
	    {0x3fda06a5, 0x3faa1fe6},
	    {0xbec96fa4, 0x3f729f7e},
	    {0x3f61a5a2, 0xbf688568},
	    {0x3f8f50d6, 0xbeb86979},
	    {0xbf87600e, 0x3f48d884},
	    {0xbfccc479, 0xbf858c0d},
	    {0xbe0a3c9b, 0xbf9d94c0},
	    {0x3ff59850, 0x3f8088e0},
	    {0x3ffc15b2, 0x3f847a2e},
	}};
	const std::array<std::array<std::uint32_t, 2>, 35> expected{{
	    {0x40355c01, 0x400b8fb5},
	    {0x401779ac, 0x3f8d91c8},
	    {0x3fdf3f23, 0x3eacdd53},
	    {0x3f8aed39, 0xbe0042f8},
	    {0x3f029ee6, 0xbe91170c},
	    {0x3e0dbd5b, 0xbe0cca40},
	    {0x3db77b6f, 0x3e9f929a},
	    {0x3e6e567d, 0x3efbdbb5},
	    {0x3ea161ff, 0x3ec37e88},
	    {0x3e752cc7, 0x3e4e9488},
	    {0x3da2e734, 0xba856b00},
	    {0xbdb90990, 0xbe37699f},
	    {0xbe48d99f, 0xbe920839},
	    {0xbe27a82c, 0xbe8b43fa},
	    {0xbdf6073b, 0x3ecdeb97},
	    {0x3de5fb38, 0x3d8cbae5},
	    {0x3e86daea, 0xbe2548d2},
	    {0x3eb2ba27, 0xbe8dcc0a},
	    {0x3ec54428, 0xbe888660},
	    {0x3ec6a08f, 0xbdf446ac},
	    {0x3ebef6fb, 0x3e34a7cc},
	    {0xbee93725, 0xbdaa443b},
	    {0xbcef3ab0, 0xbea14122},
	    {0x3eadb26a, 0xbeb6e1bc},
	    {0x3f20b6eb, 0xbe7bd6e8},
	    {0x3f5078ac, 0xbc8d6dd0},
	    {0x3f6077d3, 0x3e95e36f},
	    {0x3f4b0dc0, 0x3f25e575},
	    {0xbfcf997f, 0xbfd6bf6d},
	    {0xbf60e2ae, 0xbf9d486b},
	    {0xbd645210, 0xbf27d486},
	    {0x3f4d4b58, 0xbd2a89b0},
	    {0x3fd33a2f, 0x3f07943b},
	    {0x401c76e6, 0x3f798344},
	    {0x404909ae, 0x3f9ae2ed},
	}};
	sgre::resource::MotionBezierLattice lattice;
	lattice.EncodedDegree = 0x43;
	for (const auto& point : controls) {
		lattice.Controls.push_back(Point(point[0], point[1]));
	}
	sgre::scene::MotionMeshSubdivision subdivision;
	subdivision.X = 6;
	subdivision.Y = 4;
	sgre::scene::MotionDeformationWindow window;
	window.Left = std::bit_cast<float>(std::uint32_t{0x3dcccccd});
	window.Top = std::bit_cast<float>(std::uint32_t{0xbdcccccd});
	window.Right = std::bit_cast<float>(std::uint32_t{0x3f666666});
	window.Bottom = std::bit_cast<float>(std::uint32_t{0x3f8ccccd});
	std::array<sgre::resource::MotionPoint, 35> positions;
	sgre::scene::EvaluateMotionBezierLatticeGrid(lattice, subdivision, window, positions);
	for (std::size_t index = 0; index < positions.size(); ++index) {
		Require(std::bit_cast<std::uint32_t>(positions[index].X) == expected[index][0] && std::bit_cast<std::uint32_t>(positions[index].Y) == expected[index][1], "native quartic cached grid differs");
	}
}

int main() {
	try {
		TestNativeEvaluation0();
		TestNativeEvaluation1();
		TestNativeEvaluation2();
		TestNativeElevation1();
		TestNativeElevation3();
		TestNativeElevation4();
		TestNativeElevation8();
		TestNativeElevation14();
		TestNativeCachedGrid0();
		TestNativeCachedGrid1();
		TestNativeCachedGrid2();
		TestNativeCachedGrid3();
		TestNativeCachedGrid4();
		TestNativeCachedGrid5();
		TestRetainedAssignment();
		TestCombineAndSafety();
		std::cout << "motion Bezier lattice checks passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << "\n";
		return 1;
	}
}
