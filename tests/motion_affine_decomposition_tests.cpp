#include "sgre/scene/motion_affine_decomposition.hpp"
#include "sgre/scene/motion_bezier_lattice.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

static void Require(bool condition, const std::string& message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

struct Comparison {
	std::size_t Words = 0;
	std::size_t Exact = 0;
	float MaximumRelativeError = 0;

	void Check(float actual, std::uint32_t expected_word) {
		const float expected = std::bit_cast<float>(expected_word);
		const float error = std::abs(actual - expected) / std::max(1.0f, std::abs(expected));
		// Platform libm replaces the original executable's CRT trigonometry.
		Require(std::isfinite(actual) && error <= 8.0f * std::numeric_limits<float>::epsilon(), "native affine/lattice coefficient differs: actual=" + std::to_string(actual) + ", expected=" + std::to_string(expected));
		++Words;
		Exact += std::bit_cast<std::uint32_t>(actual) == expected_word ? 1 : 0;
		MaximumRelativeError = std::max(MaximumRelativeError, error);
	}
};

static void CheckAffine(Comparison& comparison, const std::array<std::uint32_t, 4>& coefficients, const std::array<std::uint32_t, 3>& reference_words, bool expected_success, bool reflected, const std::array<std::uint32_t, 7>& expected) {
	sgre::resource::MotionMeshMatrix matrix;
	matrix.XX = std::bit_cast<float>(coefficients[0]);
	matrix.XY = std::bit_cast<float>(coefficients[1]);
	matrix.YX = std::bit_cast<float>(coefficients[2]);
	matrix.YY = std::bit_cast<float>(coefficients[3]);
	sgre::scene::MotionRotationReference reference;
	reference.Radians = std::bit_cast<float>(reference_words[0]);
	reference.Cosine = std::bit_cast<float>(reference_words[1]);
	reference.Sine = std::bit_cast<float>(reference_words[2]);
	const auto result = sgre::scene::DecomposeMotionAffine(matrix, reference);
	Require(result.has_value() == expected_success, "native affine singularity decision differs");
	if (!result) {
		return;
	}
	Require(result->Reflected == reflected, "native affine reflection decision differs");
	const std::array<float, 7> actual{result->Rotation.Radians, result->Rotation.Cosine, result->Rotation.Sine, result->Scale[0], result->Scale[1], result->Shear[0], result->Shear[1]};
	for (std::size_t index = 0; index != actual.size(); ++index) {
		comparison.Check(actual[index], expected[index]);
	}
}

static void CheckLattice(Comparison& comparison, std::uint8_t degree, std::initializer_list<std::array<std::uint32_t, 2>> controls, std::uint32_t expected) {
	sgre::resource::MotionBezierLattice lattice;
	lattice.EncodedDegree = degree;
	for (const auto& control : controls) {
		sgre::resource::MotionPoint point;
		point.X = std::bit_cast<float>(control[0]);
		point.Y = std::bit_cast<float>(control[1]);
		lattice.Controls.push_back(point);
	}
	comparison.Check(sgre::scene::MeasureMotionBezierLatticeScale(lattice), expected);
}

static void TestNative(Comparison& comparison) {
	CheckAffine(comparison, {0x3f800000u, 0x00000000u, 0x00000000u, 0x3f800000u}, {0x00000000u, 0x3f800000u, 0x00000000u}, true, false, {0x00000000u, 0x3f800000u, 0x00000000u, 0x3f800000u, 0x3f800000u, 0x00000000u, 0x00000000u});
	CheckAffine(comparison, {0x40000000u, 0x00000000u, 0x00000000u, 0x40400000u}, {0x3e800000u, 0x3f780aa5u, 0x3e7d5777u}, true, false, {0x00000000u, 0x3f800000u, 0x00000000u, 0x40000000u, 0x40400000u, 0x00000000u, 0x00000000u});
	CheckAffine(comparison, {0xc0000000u, 0x00000000u, 0x00000000u, 0x40400000u}, {0xbf400000u, 0x3f3b4ff6u, 0xbf2e7fe1u}, true, true, {0x00000000u, 0x3f800000u, 0x00000000u, 0x40000000u, 0x40400000u, 0xb3800000u, 0x00000000u});
	CheckAffine(comparison, {0x00000000u, 0xbf800000u, 0x3f800000u, 0x00000000u}, {0x40f00000u, 0x3eb17a2fu, 0x3f7020c4u}, true, false, {0x40fb53d1u, 0x34155386u, 0x3f800000u, 0x3f800000u, 0x3f800000u, 0x33000000u, 0xb3000000u});
	CheckAffine(comparison, {0x3f800000u, 0x3f400000u, 0x3e800000u, 0x40000000u}, {0x00000000u, 0x3f800000u, 0x00000000u}, true, false, {0x3e7adbb0u, 0x3f785b42u, 0x3e785b42u, 0x3f83f07bu, 0x3fe112b4u, 0x3f969697u, 0x00000000u});
	CheckAffine(comparison, {0x3f800000u, 0x40000000u, 0x40000000u, 0x40800000u}, {0x3e800000u, 0x3f780aa5u, 0x3e7d5777u}, false, false, {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u});
	CheckAffine(comparison, {0x33d6bf95u, 0x00000000u, 0x00000000u, 0x3f800000u}, {0xbf400000u, 0x3f3b4ff6u, 0xbf2e7fe1u}, false, false, {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u});
	CheckAffine(comparison, {0x358637bdu, 0x00000000u, 0x00000000u, 0x3f800000u}, {0x40f00000u, 0x3eb17a2fu, 0x3f7020c4u}, true, false, {0x40c90fdbu, 0x3f800000u, 0x343bbd2eu, 0x358637bdu, 0x3f800000u, 0x00000000u, 0xa9000000u});
	CheckAffine(comparison, {0x400d0699u, 0x3fa9bc31u, 0x3fb8acf9u, 0xbfbc0840u}, {0x00000000u, 0x3f800000u, 0x00000000u}, true, true, {0x401a1114u, 0xbf3e075cu, 0x3f2b8992u, 0x40268e58u, 0x3ffd4f72u, 0xb344bd15u, 0x3e51d72fu});
	CheckAffine(comparison, {0xbfcfb423u, 0xc029d7cau, 0xc025d469u, 0xbf8e1527u}, {0x3e800000u, 0x3f780aa5u, 0x3e7d5777u}, true, true, {0xbf817219u, 0x3f07e027u, 0xbf58f70cu, 0x4043aa2fu, 0x3fd47a65u, 0x3f44b815u, 0x339a37dfu});
	CheckAffine(comparison, {0xc01da8e1u, 0x401efd50u, 0xbf3d91d2u, 0x40211a6fu}, {0xbf400000u, 0x3f3b4ff6u, 0xbf2e7fe1u}, true, true, {0xbe9581e2u, 0x3f7529beu, 0xbe936446u, 0x4024a0f4u, 0x3fd90766u, 0xbf9a7102u, 0xb396fc08u});
	CheckAffine(comparison, {0x3fd1347au, 0xc03295a2u, 0x3fbee15fu, 0xbe8bece6u}, {0x40f00000u, 0x3eb17a2fu, 0x3f7020c4u}, true, false, {0x40fe7399u, 0xbdc7a0a7u, 0x3f7ec7ebu, 0x3fa9944du, 0x40337061u, 0x00000000u, 0xbf21cb72u});
	CheckAffine(comparison, {0xbf944982u, 0xbf8146b7u, 0xc00cb9f7u, 0xbed97b88u}, {0x00000000u, 0x3f800000u, 0x00000000u}, true, true, {0xbf8afe70u, 0x3eeea805u, 0xbf627ceau, 0x401f1042u, 0x3f320f1eu, 0x3eae6643u, 0x00000000u});
	CheckAffine(comparison, {0xc02f3919u, 0x3ef8303du, 0xbf18f3d4u, 0xc00c077cu}, {0x3e800000u, 0x3f780aa5u, 0x3e7d5777u}, true, false, {0x4056d012u, 0xbf7a1d20u, 0xbe5a531du, 0x403358c6u, 0x400f6c8bu, 0xbb263c0au, 0x31e4782du});
	CheckAffine(comparison, {0x402308a7u, 0xbfc203d3u, 0x3f36995fu, 0x4039c64bu}, {0xbf400000u, 0x3f3b4ff6u, 0xbf2e7fe1u}, true, false, {0x3e8bc808u, 0x3f7684c7u, 0x3e8a0d29u, 0x40294de2u, 0x404d0ceau, 0xbe83048fu, 0xb31fcdf9u});
	CheckAffine(comparison, {0x403532dau, 0xbfd85424u, 0xc03c4852u, 0x3fafc2d8u}, {0x40f00000u, 0x3eb17a2fu, 0x3f7020c4u}, true, true, {0x40aca14bu, 0x3f216daeu, 0xbf46b016u, 0x3efef6a4u, 0x400b5d5cu, 0x35008538u, 0xbfee35bfu});
	CheckAffine(comparison, {0x3f310816u, 0xbf37cacdu, 0x40367a73u, 0x400643a3u}, {0x00000000u, 0x3f800000u, 0x00000000u}, true, false, {0x3ea8d220u, 0x3f72359au, 0x3ea5c741u, 0x3fc9ea4eu, 0x400de8a5u, 0x00000000u, 0x3f8ecd16u});
	CheckAffine(comparison, {0x3ddd3c29u, 0x4009d8cbu, 0xbf3ef681u, 0xbe2e1bb1u}, {0x3e800000u, 0x3f780aa5u, 0x3e7d5777u}, true, false, {0xbfb6a74eu, 0x3e12c292u, 0xbf7d5b6eu, 0x3f40f474u, 0x4006dd3cu, 0x3f21ff79u, 0x3297db41u});
	CheckAffine(comparison, {0x3f7c2457u, 0x402ffe2au, 0x3f9e4ecau, 0x3fa6e8d5u}, {0xbf400000u, 0x3f3b4ff6u, 0xbf2e7fe1u}, true, true, {0x3f90627fu, 0x3edb5f6fu, 0x3f674faeu, 0x3f320c30u, 0x4042c6f9u, 0x348a07cfu, 0x3eeeddefu});
	CheckAffine(comparison, {0x3f6f77f8u, 0xc01196bcu, 0xc035931eu, 0xbe05193eu}, {0x40f00000u, 0x3eb17a2fu, 0x3f7020c4u}, true, true, {0x4094f853u, 0xbd69a800u, 0xbf7f9548u, 0x4038b1b3u, 0x4011d387u, 0x33851030u, 0xbead7c6au});
	CheckAffine(comparison, {0xbf465280u, 0x3fe3bd3bu, 0xbe5e1170u, 0x403fd895u}, {0x00000000u, 0x3f800000u, 0x00000000u}, true, true, {0xbe8bbfb4u, 0x3f7685e7u, 0xbe8a0524u, 0x3f4df23eu, 0x401a0c53u, 0xc0489711u, 0x00000000u});
	CheckAffine(comparison, {0x3faf9d53u, 0xbf84bcddu, 0x3f5c5367u, 0xc0181516u}, {0x3e800000u, 0x3f780aa5u, 0x3e7d5777u}, true, true, {0x402534b3u, 0xbf58dcf7u, 0x3f0809c3u, 0x3fcf4eaeu, 0x3fbb20bau, 0xbfa93a12u, 0x33af1c38u});
	CheckAffine(comparison, {0x400c9781u, 0xbfa93987u, 0x3ff3ab28u, 0x3f5f8bc7u}, {0xbf400000u, 0x3f3b4ff6u, 0xbf2e7fe1u}, true, false, {0x3f36cb78u, 0x3f4176ecu, 0x3f27a720u, 0x403a0965u, 0x3fc34b1eu, 0xbe1681d1u, 0x00000000u});
	CheckAffine(comparison, {0xbf70fc25u, 0xbedd4b67u, 0xbe026ffau, 0x3f6e6a53u}, {0x40f00000u, 0x3eb17a2fu, 0x3f7020c4u}, true, true, {0x40c4c208u, 0x3f7db01du, 0xbe09501fu, 0x3f732e66u, 0x3f7b196cu, 0x3ea38b6bu, 0xb3827f88u});
	CheckAffine(comparison, {0xbf869f98u, 0xc013dbd6u, 0x3fd0923fu, 0x3fb1dc0au}, {0x00000000u, 0x3f800000u, 0x00000000u}, true, false, {0x3f83c074u, 0x3f03f1e3u, 0x3f5b60adu, 0x3f5ab203u, 0x402c8abau, 0x00000000u, 0x3f2554e8u});
	CheckAffine(comparison, {0x3f38ea62u, 0xc01c31ffu, 0xc03cfdd3u, 0x3f0e99afu}, {0x3e800000u, 0x3f780aa5u, 0x3e7d5777u}, true, true, {0xbfac56d4u, 0x3e63dbc7u, 0xbf7994ebu, 0x402df769u, 0x40203649u, 0xb26b729du, 0xbf0b36b7u});
	CheckAffine(comparison, {0x3fd409d5u, 0x4009b6d9u, 0x401c88f7u, 0xc01977bdu}, {0xbf400000u, 0x3f3b4ff6u, 0xbf2e7fe1u}, true, true, {0x400aa1a4u, 0xbf0f8ed6u, 0x3f53f5ceu, 0x403d0f16u, 0x40481572u, 0xbe86f97au, 0x3323c57bu});
	CheckAffine(comparison, {0xbf864bb8u, 0x400d8875u, 0xc00b78b9u, 0xc03910bcu}, {0x40f00000u, 0x3eb17a2fu, 0x3f7020c4u}, true, false, {0x41213e20u, 0xbf4b5956u, 0xbf1b8412u, 0x400a107fu, 0x4068fb7du, 0xb3ed56a5u, 0x3e99d24bu});
	CheckAffine(comparison, {0x3f94d56au, 0xbffb6fe5u, 0xbd9b541cu, 0xc03f216du}, {0x00000000u, 0x3f800000u, 0x00000000u}, true, true, {0xc023d36eu, 0xbf55e136u, 0xbf0caea0u, 0x3f81ae3fu, 0x4064c54eu, 0x34fcaea9u, 0xbe24e5d0u});
	CheckAffine(comparison, {0xc017d51du, 0xbfa8167fu, 0x3facaceeu, 0xbfa9d1cdu}, {0x3e800000u, 0x3f780aa5u, 0x3e7d5777u}, true, false, {0x40171fdau, 0xbf35f1d1u, 0x3f3416ddu, 0x4028a59cu, 0x3feef094u, 0xb2c24ca8u, 0x3ec2c5b9u});
	CheckAffine(comparison, {0x3f030f36u, 0xc018212du, 0x3ed13a76u, 0xbfb966a7u}, {0xbf400000u, 0x3f3b4ff6u, 0xbf2e7fe1u}, true, false, {0x3f2c749cu, 0x3f481379u, 0x3f1fb462u, 0x3f27b139u, 0x3eb3a52eu, 0xc086e580u, 0x3388cd8fu});
	CheckAffine(comparison, {0x3f95d1c8u, 0x401572fcu, 0x40054d5cu, 0xc02c7f13u}, {0x40f00000u, 0x3eb17a2fu, 0x3f7020c4u}, true, true, {0x4105daf5u, 0xbefad421u, 0x3f5f2ce6u, 0x4018e884u, 0x4056cae3u, 0xbf013011u, 0x00000000u});
	CheckLattice(comparison, 0x33, {
	                                   {0x00000000u, 0x00000000u},
	                                   {0x3eaaaaabu, 0x00000000u},
	                                   {0x3f2aaaabu, 0x00000000u},
	                                   {0x3f800000u, 0x00000000u},
	                                   {0x00000000u, 0x3eaaaaabu},
	                                   {0x3eaaaaabu, 0x3eaaaaabu},
	                                   {0x3f2aaaabu, 0x3eaaaaabu},
	                                   {0x3f800000u, 0x3eaaaaabu},
	                                   {0x00000000u, 0x3f2aaaabu},
	                                   {0x3eaaaaabu, 0x3f2aaaabu},
	                                   {0x3f2aaaabu, 0x3f2aaaabu},
	                                   {0x3f800000u, 0x3f2aaaabu},
	                                   {0x00000000u, 0x3f800000u},
	                                   {0x3eaaaaabu, 0x3f800000u},
	                                   {0x3f2aaaabu, 0x3f800000u},
	                                   {0x3f800000u, 0x3f800000u},
	                               },
	    0x3f800000u);
	CheckLattice(comparison, 0x33, {
	                                   {0x40400000u, 0xc0e00000u},
	                                   {0x406aaaabu, 0xc0e00000u},
	                                   {0x408aaaabu, 0xc0e00000u},
	                                   {0x40a00000u, 0xc0e00000u},
	                                   {0x40400000u, 0xc0c00000u},
	                                   {0x406aaaabu, 0xc0c00000u},
	                                   {0x408aaaabu, 0xc0c00000u},
	                                   {0x40a00000u, 0xc0c00000u},
	                                   {0x40400000u, 0xc0a00000u},
	                                   {0x406aaaabu, 0xc0a00000u},
	                                   {0x408aaaabu, 0xc0a00000u},
	                                   {0x40a00000u, 0xc0a00000u},
	                                   {0x40400000u, 0xc0800000u},
	                                   {0x406aaaabu, 0xc0800000u},
	                                   {0x408aaaabu, 0xc0800000u},
	                                   {0x40a00000u, 0xc0800000u},
	                               },
	    0x40232b2au);
	CheckLattice(comparison, 0x33, {
	                                   {0x40400000u, 0xc0e00000u},
	                                   {0x40155555u, 0xc0e00000u},
	                                   {0x3fd55555u, 0xc0e00000u},
	                                   {0x3f800000u, 0xc0e00000u},
	                                   {0x40400000u, 0xc0c00000u},
	                                   {0x40155555u, 0xc0c00000u},
	                                   {0x3fd55555u, 0xc0c00000u},
	                                   {0x3f800000u, 0xc0c00000u},
	                                   {0x40400000u, 0xc0a00000u},
	                                   {0x40155555u, 0xc0a00000u},
	                                   {0x3fd55555u, 0xc0a00000u},
	                                   {0x3f800000u, 0xc0a00000u},
	                                   {0x40400000u, 0xc0800000u},
	                                   {0x40155555u, 0xc0800000u},
	                                   {0x3fd55555u, 0xc0800000u},
	                                   {0x3f800000u, 0xc0800000u},
	                               },
	    0x40232b2au);
	CheckLattice(comparison, 0x33, {
	                                   {0x40400000u, 0xc0e00000u},
	                                   {0x40400000u, 0xc0d55555u},
	                                   {0x40400000u, 0xc0caaaabu},
	                                   {0x40400000u, 0xc0c00000u},
	                                   {0x402aaaabu, 0xc0e00000u},
	                                   {0x402aaaabu, 0xc0d55555u},
	                                   {0x402aaaabu, 0xc0caaaabu},
	                                   {0x402aaaabu, 0xc0c00000u},
	                                   {0x40155555u, 0xc0e00000u},
	                                   {0x40155555u, 0xc0d55555u},
	                                   {0x40155555u, 0xc0caaaabu},
	                                   {0x40155555u, 0xc0c00000u},
	                                   {0x40000000u, 0xc0e00000u},
	                                   {0x40000000u, 0xc0d55555u},
	                                   {0x40000000u, 0xc0caaaabu},
	                                   {0x40000000u, 0xc0c00000u},
	                               },
	    0x3f7fffffu);
	CheckLattice(comparison, 0x33, {
	                                   {0x00000000u, 0x00000000u},
	                                   {0x00000000u, 0x00000000u},
	                                   {0x00000000u, 0x00000000u},
	                                   {0x00000000u, 0x00000000u},
	                                   {0x00000000u, 0x00000000u},
	                                   {0x00000000u, 0x00000000u},
	                                   {0x00000000u, 0x00000000u},
	                                   {0x00000000u, 0x00000000u},
	                                   {0x00000000u, 0x00000000u},
	                                   {0x00000000u, 0x00000000u},
	                                   {0x00000000u, 0x00000000u},
	                                   {0x00000000u, 0x00000000u},
	                                   {0x00000000u, 0x00000000u},
	                                   {0x00000000u, 0x00000000u},
	                                   {0x00000000u, 0x00000000u},
	                                   {0x00000000u, 0x00000000u},
	                               },
	    0x3f800000u);
	CheckLattice(comparison, 0x33, {
	                                   {0x00000000u, 0x00000000u},
	                                   {0x3eaaaaabu, 0x3daaaaabu},
	                                   {0x3f2aaaabu, 0x3e2aaaabu},
	                                   {0x3f800000u, 0x3e800000u},
	                                   {0x3e800000u, 0x3f2aaaabu},
	                                   {0x3f155556u, 0x3f400000u},
	                                   {0x3f6aaaabu, 0x3f555556u},
	                                   {0x3fa00000u, 0x3f6aaaabu},
	                                   {0x3f000000u, 0x3faaaaabu},
	                                   {0x3f555556u, 0x3fb55556u},
	                                   {0x3f955556u, 0x3fc00000u},
	                                   {0x3fc00000u, 0x3fcaaaabu},
	                                   {0x3f400000u, 0x40000000u},
	                                   {0x3f8aaaabu, 0x40055555u},
	                                   {0x3fb55556u, 0x400aaaabu},
	                                   {0x3fe00000u, 0x40100000u},
	                               },
	    0x3fb87aeeu);
	CheckLattice(comparison, 0x33, {
	                                   {0x3dfea796u, 0x3e542e6bu},
	                                   {0x3f005dd0u, 0x3eee4e2eu},
	                                   {0x3f5fccadu, 0xbd891e87u},
	                                   {0x3fa79640u, 0xbea02e77u},
	                                   {0xbedf55a3u, 0xbe1c8897u},
	                                   {0xbe11ff7cu, 0xbdb1ae80u},
	                                   {0x3e89324au, 0xbbfefdefu},
	                                   {0x3fb50bd3u, 0xbcc2a8b5u},
	                                   {0xbd63689cu, 0x3f12880eu},
	                                   {0x3dab532eu, 0x3f8595a3u},
	                                   {0x3ee0db16u, 0x3f315ff8u},
	                                   {0x3fbdbdd3u, 0x3f8cc1d7u},
	                                   {0x3ed14551u, 0x3f9ef408u},
	                                   {0x3f3c3cc4u, 0x3f595461u},
	                                   {0x3f287501u, 0x3f4d05ecu},
	                                   {0x3f8f5b1au, 0x3f9d65dau},
	                               },
	    0x3f9a6012u);
	CheckLattice(comparison, 0x33, {
	                                   {0x3eee3c52u, 0xbe9eea33u},
	                                   {0xbdc9b368u, 0x3b52305fu},
	                                   {0x3f777f8au, 0x3e6345ccu},
	                                   {0x3f8511ceu, 0x3e0c2f27u},
	                                   {0x3e33ba44u, 0x3e728cb4u},
	                                   {0x3e08a904u, 0x3e8cb06du},
	                                   {0x3f71eba8u, 0x3e262748u},
	                                   {0x3f27db31u, 0xbe2604e3u},
	                                   {0xbefb06afu, 0x3ed7b543u},
	                                   {0x3ed43abcu, 0x3f93949bu},
	                                   {0x3f09a337u, 0x3ec7a548u},
	                                   {0x3f5de65bu, 0x3f27807bu},
	                                   {0xbee47f4cu, 0x3f4abb6du},
	                                   {0x3eba17a4u, 0x3f98bd67u},
	                                   {0x3f3b5a1au, 0x3f5ac902u},
	                                   {0x3f2b03edu, 0x3f3d96efu},
	                               },
	    0x3f754ebdu);
	CheckLattice(comparison, 0x34, {
	                                   {0x00000000u, 0x00000000u},
	                                   {0x3e800000u, 0x00000000u},
	                                   {0x3f000000u, 0x00000000u},
	                                   {0x3f400000u, 0x00000000u},
	                                   {0x3f800000u, 0x00000000u},
	                                   {0x00000000u, 0x3eaaaaabu},
	                                   {0x3e800000u, 0x3eaaaaabu},
	                                   {0x3f000000u, 0x3eaaaaabu},
	                                   {0x3f400000u, 0x3eaaaaabu},
	                                   {0x3f800000u, 0x3eaaaaabu},
	                                   {0x00000000u, 0x3f2aaaabu},
	                                   {0x3e800000u, 0x3f2aaaabu},
	                                   {0x3f000000u, 0x3f2aaaabu},
	                                   {0x3f400000u, 0x3f2aaaabu},
	                                   {0x3f800000u, 0x3f2aaaabu},
	                                   {0x00000000u, 0x3f800000u},
	                                   {0x3e800000u, 0x3f800000u},
	                                   {0x3f000000u, 0x3f800000u},
	                                   {0x3f400000u, 0x3f800000u},
	                                   {0x3f800000u, 0x3f800000u},
	                               },
	    0x3f800000u);
	CheckLattice(comparison, 0x34, {
	                                   {0x40400000u, 0xc0e00000u},
	                                   {0x40600000u, 0xc0e00000u},
	                                   {0x40800000u, 0xc0e00000u},
	                                   {0x40900000u, 0xc0e00000u},
	                                   {0x40a00000u, 0xc0e00000u},
	                                   {0x40400000u, 0xc0c00000u},
	                                   {0x40600000u, 0xc0c00000u},
	                                   {0x40800000u, 0xc0c00000u},
	                                   {0x40900000u, 0xc0c00000u},
	                                   {0x40a00000u, 0xc0c00000u},
	                                   {0x40400000u, 0xc0a00000u},
	                                   {0x40600000u, 0xc0a00000u},
	                                   {0x40800000u, 0xc0a00000u},
	                                   {0x40900000u, 0xc0a00000u},
	                                   {0x40a00000u, 0xc0a00000u},
	                                   {0x40400000u, 0xc0800000u},
	                                   {0x40600000u, 0xc0800000u},
	                                   {0x40800000u, 0xc0800000u},
	                                   {0x40900000u, 0xc0800000u},
	                                   {0x40a00000u, 0xc0800000u},
	                               },
	    0x40232b2bu);
	CheckLattice(comparison, 0x34, {
	                                   {0x40400000u, 0xc0e00000u},
	                                   {0x40200000u, 0xc0e00000u},
	                                   {0x40000000u, 0xc0e00000u},
	                                   {0x3fc00000u, 0xc0e00000u},
	                                   {0x3f800000u, 0xc0e00000u},
	                                   {0x40400000u, 0xc0c00000u},
	                                   {0x40200000u, 0xc0c00000u},
	                                   {0x40000000u, 0xc0c00000u},
	                                   {0x3fc00000u, 0xc0c00000u},
	                                   {0x3f800000u, 0xc0c00000u},
	                                   {0x40400000u, 0xc0a00000u},
	                                   {0x40200000u, 0xc0a00000u},
	                                   {0x40000000u, 0xc0a00000u},
	                                   {0x3fc00000u, 0xc0a00000u},
	                                   {0x3f800000u, 0xc0a00000u},
	                                   {0x40400000u, 0xc0800000u},
	                                   {0x40200000u, 0xc0800000u},
	                                   {0x40000000u, 0xc0800000u},
	                                   {0x3fc00000u, 0xc0800000u},
	                                   {0x3f800000u, 0xc0800000u},
	                               },
	    0x40232b2bu);
	CheckLattice(comparison, 0x34, {
	                                   {0x40400000u, 0xc0e00000u},
	                                   {0x40400000u, 0xc0d80000u},
	                                   {0x40400000u, 0xc0d00000u},
	                                   {0x40400000u, 0xc0c80000u},
	                                   {0x40400000u, 0xc0c00000u},
	                                   {0x402aaaabu, 0xc0e00000u},
	                                   {0x402aaaabu, 0xc0d80000u},
	                                   {0x402aaaabu, 0xc0d00000u},
	                                   {0x402aaaabu, 0xc0c80000u},
	                                   {0x402aaaabu, 0xc0c00000u},
	                                   {0x40155555u, 0xc0e00000u},
	                                   {0x40155555u, 0xc0d80000u},
	                                   {0x40155555u, 0xc0d00000u},
	                                   {0x40155555u, 0xc0c80000u},
	                                   {0x40155555u, 0xc0c00000u},
	                                   {0x40000000u, 0xc0e00000u},
	                                   {0x40000000u, 0xc0d80000u},
	                                   {0x40000000u, 0xc0d00000u},
	                                   {0x40000000u, 0xc0c80000u},
	                                   {0x40000000u, 0xc0c00000u},
	                               },
	    0x3f800000u);
	CheckLattice(comparison, 0x34, {
	                                   {0x00000000u, 0x00000000u},
	                                   {0x00000000u, 0x00000000u},
	                                   {0x00000000u, 0x00000000u},
	                                   {0x00000000u, 0x00000000u},
	                                   {0x00000000u, 0x00000000u},
	                                   {0x00000000u, 0x00000000u},
	                                   {0x00000000u, 0x00000000u},
	                                   {0x00000000u, 0x00000000u},
	                                   {0x00000000u, 0x00000000u},
	                                   {0x00000000u, 0x00000000u},
	                                   {0x00000000u, 0x00000000u},
	                                   {0x00000000u, 0x00000000u},
	                                   {0x00000000u, 0x00000000u},
	                                   {0x00000000u, 0x00000000u},
	                                   {0x00000000u, 0x00000000u},
	                                   {0x00000000u, 0x00000000u},
	                                   {0x00000000u, 0x00000000u},
	                                   {0x00000000u, 0x00000000u},
	                                   {0x00000000u, 0x00000000u},
	                                   {0x00000000u, 0x00000000u},
	                               },
	    0x3f800000u);
	CheckLattice(comparison, 0x34, {
	                                   {0x00000000u, 0x00000000u},
	                                   {0x3e800000u, 0x3d800000u},
	                                   {0x3f000000u, 0x3e000000u},
	                                   {0x3f400000u, 0x3e400000u},
	                                   {0x3f800000u, 0x3e800000u},
	                                   {0x3e800000u, 0x3f2aaaabu},
	                                   {0x3f000000u, 0x3f3aaaabu},
	                                   {0x3f400000u, 0x3f4aaaabu},
	                                   {0x3f800000u, 0x3f5aaaabu},
	                                   {0x3fa00000u, 0x3f6aaaabu},
	                                   {0x3f000000u, 0x3faaaaabu},
	                                   {0x3f400000u, 0x3fb2aaabu},
	                                   {0x3f800000u, 0x3fbaaaabu},
	                                   {0x3fa00000u, 0x3fc2aaabu},
	                                   {0x3fc00000u, 0x3fcaaaabu},
	                                   {0x3f400000u, 0x40000000u},
	                                   {0x3f800000u, 0x40040000u},
	                                   {0x3fa00000u, 0x40080000u},
	                                   {0x3fc00000u, 0x400c0000u},
	                                   {0x3fe00000u, 0x40100000u},
	                               },
	    0x3fb87aeeu);
	CheckLattice(comparison, 0x34, {
	                                   {0x3e59bbb9u, 0x3ea95388u},
	                                   {0x3ecef456u, 0x3ea72385u},
	                                   {0x3dbf65b9u, 0xbdf7deb3u},
	                                   {0x3f88dc3bu, 0xbe2e283fu},
	                                   {0x3fb6d0f0u, 0x3e3b2586u},
	                                   {0x3cd04eddu, 0x3f17005au},
	                                   {0x3e23c8d5u, 0x3f2dd19cu},
	                                   {0x3ee795a6u, 0x3f2ed5ecu},
	                                   {0x3f7d03b2u, 0x3f11f824u},
	                                   {0x3fbc75a6u, 0x3f3cd6a3u},
	                                   {0x3eeda2a4u, 0x3f099f10u},
	                                   {0x3d3d5e3au, 0x3f57927fu},
	                                   {0x3f446194u, 0x3f2151c1u},
	                                   {0x3f1b80bbu, 0x3f70daedu},
	                                   {0x3f34d214u, 0x3f0a44a2u},
	                                   {0x3e885a7au, 0x3fa3aadfu},
	                                   {0x3f240baeu, 0x3f5addb4u},
	                                   {0x3e3e4708u, 0x3f1de7dau},
	                                   {0x3ea9db17u, 0x3f0d2a54u},
	                                   {0x3f8ff084u, 0x3fa5bbb0u},
	                               },
	    0x3f55cae0u);
	CheckLattice(comparison, 0x34, {
	                                   {0x3e1ffd0cu, 0xbe187232u},
	                                   {0x3d12d03eu, 0xbd31bb38u},
	                                   {0x3e95ea0eu, 0xbcadb1f2u},
	                                   {0x3f007333u, 0xbea4963fu},
	                                   {0x3f88bc5fu, 0x3d93920eu},
	                                   {0x3efd97e2u, 0x3ec1ed4eu},
	                                   {0x3d1160dbu, 0x3f48ee46u},
	                                   {0x3f31c529u, 0x3f1bc154u},
	                                   {0x3f7ecd9bu, 0x3f0633ccu},
	                                   {0x3fb20998u, 0x3f1186ddu},
	                                   {0x3ec7425cu, 0x3e6acb22u},
	                                   {0x3d48dc8bu, 0x3ecab162u},
	                                   {0x3d92627cu, 0x3f1f48dcu},
	                                   {0x3f5a7118u, 0x3efc291cu},
	                                   {0x3f1d4ec3u, 0x3f5cfa9du},
	                                   {0x3ddbb2b9u, 0x3f75f914u},
	                                   {0x3f0a6213u, 0x3f1b592eu},
	                                   {0x3f28a899u, 0x3f40b38fu},
	                                   {0x3f106a08u, 0x3f036ee0u},
	                                   {0x3fb8655du, 0x3fb8bed3u},
	                               },
	    0x3f59456au);
	CheckLattice(comparison, 0x43, {
	                                   {0x00000000u, 0x00000000u},
	                                   {0x3eaaaaabu, 0x00000000u},
	                                   {0x3f2aaaabu, 0x00000000u},
	                                   {0x3f800000u, 0x00000000u},
	                                   {0x00000000u, 0x3e800000u},
	                                   {0x3eaaaaabu, 0x3e800000u},
	                                   {0x3f2aaaabu, 0x3e800000u},
	                                   {0x3f800000u, 0x3e800000u},
	                                   {0x00000000u, 0x3f000000u},
	                                   {0x3eaaaaabu, 0x3f000000u},
	                                   {0x3f2aaaabu, 0x3f000000u},
	                                   {0x3f800000u, 0x3f000000u},
	                                   {0x00000000u, 0x3f400000u},
	                                   {0x3eaaaaabu, 0x3f400000u},
	                                   {0x3f2aaaabu, 0x3f400000u},
	                                   {0x3f800000u, 0x3f400000u},
	                                   {0x00000000u, 0x3f800000u},
	                                   {0x3eaaaaabu, 0x3f800000u},
	                                   {0x3f2aaaabu, 0x3f800000u},
	                                   {0x3f800000u, 0x3f800000u},
	                               },
	    0x3f800000u);
	CheckLattice(comparison, 0x43, {
	                                   {0x40400000u, 0xc0e00000u},
	                                   {0x406aaaabu, 0xc0e00000u},
	                                   {0x408aaaabu, 0xc0e00000u},
	                                   {0x40a00000u, 0xc0e00000u},
	                                   {0x40400000u, 0xc0c80000u},
	                                   {0x406aaaabu, 0xc0c80000u},
	                                   {0x408aaaabu, 0xc0c80000u},
	                                   {0x40a00000u, 0xc0c80000u},
	                                   {0x40400000u, 0xc0b00000u},
	                                   {0x406aaaabu, 0xc0b00000u},
	                                   {0x408aaaabu, 0xc0b00000u},
	                                   {0x40a00000u, 0xc0b00000u},
	                                   {0x40400000u, 0xc0980000u},
	                                   {0x406aaaabu, 0xc0980000u},
	                                   {0x408aaaabu, 0xc0980000u},
	                                   {0x40a00000u, 0xc0980000u},
	                                   {0x40400000u, 0xc0800000u},
	                                   {0x406aaaabu, 0xc0800000u},
	                                   {0x408aaaabu, 0xc0800000u},
	                                   {0x40a00000u, 0xc0800000u},
	                               },
	    0x40232b2bu);
	CheckLattice(comparison, 0x43, {
	                                   {0x40400000u, 0xc0e00000u},
	                                   {0x40155555u, 0xc0e00000u},
	                                   {0x3fd55555u, 0xc0e00000u},
	                                   {0x3f800000u, 0xc0e00000u},
	                                   {0x40400000u, 0xc0c80000u},
	                                   {0x40155555u, 0xc0c80000u},
	                                   {0x3fd55555u, 0xc0c80000u},
	                                   {0x3f800000u, 0xc0c80000u},
	                                   {0x40400000u, 0xc0b00000u},
	                                   {0x40155555u, 0xc0b00000u},
	                                   {0x3fd55555u, 0xc0b00000u},
	                                   {0x3f800000u, 0xc0b00000u},
	                                   {0x40400000u, 0xc0980000u},
	                                   {0x40155555u, 0xc0980000u},
	                                   {0x3fd55555u, 0xc0980000u},
	                                   {0x3f800000u, 0xc0980000u},
	                                   {0x40400000u, 0xc0800000u},
	                                   {0x40155555u, 0xc0800000u},
	                                   {0x3fd55555u, 0xc0800000u},
	                                   {0x3f800000u, 0xc0800000u},
	                               },
	    0x40232b2bu);
	CheckLattice(comparison, 0x43, {
	                                   {0x40400000u, 0xc0e00000u},
	                                   {0x40400000u, 0xc0d55555u},
	                                   {0x40400000u, 0xc0caaaabu},
	                                   {0x40400000u, 0xc0c00000u},
	                                   {0x40300000u, 0xc0e00000u},
	                                   {0x40300000u, 0xc0d55555u},
	                                   {0x40300000u, 0xc0caaaabu},
	                                   {0x40300000u, 0xc0c00000u},
	                                   {0x40200000u, 0xc0e00000u},
	                                   {0x40200000u, 0xc0d55555u},
	                                   {0x40200000u, 0xc0caaaabu},
	                                   {0x40200000u, 0xc0c00000u},
	                                   {0x40100000u, 0xc0e00000u},
	                                   {0x40100000u, 0xc0d55555u},
	                                   {0x40100000u, 0xc0caaaabu},
	                                   {0x40100000u, 0xc0c00000u},
	                                   {0x40000000u, 0xc0e00000u},
	                                   {0x40000000u, 0xc0d55555u},
	                                   {0x40000000u, 0xc0caaaabu},
	                                   {0x40000000u, 0xc0c00000u},
	                               },
	    0x3f7fffffu);
	CheckLattice(comparison, 0x43, {
	                                   {0x00000000u, 0x00000000u},
	                                   {0x00000000u, 0x00000000u},
	                                   {0x00000000u, 0x00000000u},
	                                   {0x00000000u, 0x00000000u},
	                                   {0x00000000u, 0x00000000u},
	                                   {0x00000000u, 0x00000000u},
	                                   {0x00000000u, 0x00000000u},
	                                   {0x00000000u, 0x00000000u},
	                                   {0x00000000u, 0x00000000u},
	                                   {0x00000000u, 0x00000000u},
	                                   {0x00000000u, 0x00000000u},
	                                   {0x00000000u, 0x00000000u},
	                                   {0x00000000u, 0x00000000u},
	                                   {0x00000000u, 0x00000000u},
	                                   {0x00000000u, 0x00000000u},
	                                   {0x00000000u, 0x00000000u},
	                                   {0x00000000u, 0x00000000u},
	                                   {0x00000000u, 0x00000000u},
	                                   {0x00000000u, 0x00000000u},
	                                   {0x00000000u, 0x00000000u},
	                               },
	    0x3f800000u);
	CheckLattice(comparison, 0x43, {
	                                   {0x00000000u, 0x00000000u},
	                                   {0x3eaaaaabu, 0x3daaaaabu},
	                                   {0x3f2aaaabu, 0x3e2aaaabu},
	                                   {0x3f800000u, 0x3e800000u},
	                                   {0x3e400000u, 0x3f000000u},
	                                   {0x3f055556u, 0x3f155555u},
	                                   {0x3f5aaaabu, 0x3f2aaaabu},
	                                   {0x3f980000u, 0x3f400000u},
	                                   {0x3ec00000u, 0x3f800000u},
	                                   {0x3f355556u, 0x3f8aaaabu},
	                                   {0x3f855556u, 0x3f955555u},
	                                   {0x3fb00000u, 0x3fa00000u},
	                                   {0x3f100000u, 0x3fc00000u},
	                                   {0x3f655556u, 0x3fcaaaabu},
	                                   {0x3f9d5556u, 0x3fd55555u},
	                                   {0x3fc80000u, 0x3fe00000u},
	                                   {0x3f400000u, 0x40000000u},
	                                   {0x3f8aaaabu, 0x40055555u},
	                                   {0x3fb55556u, 0x400aaaabu},
	                                   {0x3fe00000u, 0x40100000u},
	                               },
	    0x3fb87aefu);
	CheckLattice(comparison, 0x43, {
	                                   {0x3e5e0091u, 0xbe70cdd3u},
	                                   {0x3eb03cf2u, 0x3ef08195u},
	                                   {0x3e70edcfu, 0xbc8af53eu},
	                                   {0x3f0336d5u, 0x3d89b312u},
	                                   {0x3e978652u, 0x3f137efcu},
	                                   {0x3ea4d9ecu, 0x3ef18610u},
	                                   {0x3f889482u, 0xbe1f04dbu},
	                                   {0x3fb721deu, 0x3e95876du},
	                                   {0x3c9c1bafu, 0x3f55d133u},
	                                   {0x3c6dcbf1u, 0x3f14033du},
	                                   {0x3f8a5e5fu, 0x3f1821d7u},
	                                   {0x3f1bbcaeu, 0x3acef48fu},
	                                   {0xbde3c8ccu, 0x3f681b0bu},
	                                   {0x3dc9b437u, 0x3f1e35c6u},
	                                   {0x3f4e237cu, 0x3f960108u},
	                                   {0x3f18699au, 0x3f90b9d7u},
	                                   {0x3ee6d2c0u, 0x3f60db8au},
	                                   {0x3ed3e292u, 0x3f8d9382u},
	                                   {0x3ea99db9u, 0x3fb64076u},
	                                   {0x3f3abcb3u, 0x3f961041u},
	                               },
	    0x3f6d9765u);
	CheckLattice(comparison, 0x43, {
	                                   {0x3ef8c120u, 0xbea993d4u},
	                                   {0x3d806ecfu, 0xbe7f3191u},
	                                   {0x3e7db25bu, 0xbdcfdc2fu},
	                                   {0x3f07bf9du, 0xbe293ca5u},
	                                   {0xbe9c053cu, 0x3f36ef76u},
	                                   {0x3f22d93au, 0x3f0a35cbu},
	                                   {0x3f0e4e3du, 0x3f1cf934u},
	                                   {0x3f8c71edu, 0xbd6d7efbu},
	                                   {0x3eb27d6du, 0x3f5b05e5u},
	                                   {0x3ee27fefu, 0x3e1e4d11u},
	                                   {0x3e8d839au, 0x3f1af0aau},
	                                   {0x3fa89d92u, 0x3f11e5d6u},
	                                   {0xbed37492u, 0x3f2864a8u},
	                                   {0x3cba9ebfu, 0x3f9e2602u},
	                                   {0x3e9ad9deu, 0x3f25e48fu},
	                                   {0x3fad793du, 0x3eb304d0u},
	                                   {0x3b3f8c43u, 0x3f187ef0u},
	                                   {0x3f2bb388u, 0x3fbc3a4du},
	                                   {0x3f2b207fu, 0x3f31ddc9u},
	                                   {0x3f7bb285u, 0x3f9dd641u},
	                               },
	    0x3f829e1fu);
	CheckLattice(comparison, 0x44, {
	                                   {0x00000000u, 0x00000000u},
	                                   {0x3e800000u, 0x00000000u},
	                                   {0x3f000000u, 0x00000000u},
	                                   {0x3f400000u, 0x00000000u},
	                                   {0x3f800000u, 0x00000000u},
	                                   {0x00000000u, 0x3e800000u},
	                                   {0x3e800000u, 0x3e800000u},
	                                   {0x3f000000u, 0x3e800000u},
	                                   {0x3f400000u, 0x3e800000u},
	                                   {0x3f800000u, 0x3e800000u},
	                                   {0x00000000u, 0x3f000000u},
	                                   {0x3e800000u, 0x3f000000u},
	                                   {0x3f000000u, 0x3f000000u},
	                                   {0x3f400000u, 0x3f000000u},
	                                   {0x3f800000u, 0x3f000000u},
	                                   {0x00000000u, 0x3f400000u},
	                                   {0x3e800000u, 0x3f400000u},
	                                   {0x3f000000u, 0x3f400000u},
	                                   {0x3f400000u, 0x3f400000u},
	                                   {0x3f800000u, 0x3f400000u},
	                                   {0x00000000u, 0x3f800000u},
	                                   {0x3e800000u, 0x3f800000u},
	                                   {0x3f000000u, 0x3f800000u},
	                                   {0x3f400000u, 0x3f800000u},
	                                   {0x3f800000u, 0x3f800000u},
	                               },
	    0x3f800000u);
	CheckLattice(comparison, 0x44, {
	                                   {0x40400000u, 0xc0e00000u},
	                                   {0x40600000u, 0xc0e00000u},
	                                   {0x40800000u, 0xc0e00000u},
	                                   {0x40900000u, 0xc0e00000u},
	                                   {0x40a00000u, 0xc0e00000u},
	                                   {0x40400000u, 0xc0c80000u},
	                                   {0x40600000u, 0xc0c80000u},
	                                   {0x40800000u, 0xc0c80000u},
	                                   {0x40900000u, 0xc0c80000u},
	                                   {0x40a00000u, 0xc0c80000u},
	                                   {0x40400000u, 0xc0b00000u},
	                                   {0x40600000u, 0xc0b00000u},
	                                   {0x40800000u, 0xc0b00000u},
	                                   {0x40900000u, 0xc0b00000u},
	                                   {0x40a00000u, 0xc0b00000u},
	                                   {0x40400000u, 0xc0980000u},
	                                   {0x40600000u, 0xc0980000u},
	                                   {0x40800000u, 0xc0980000u},
	                                   {0x40900000u, 0xc0980000u},
	                                   {0x40a00000u, 0xc0980000u},
	                                   {0x40400000u, 0xc0800000u},
	                                   {0x40600000u, 0xc0800000u},
	                                   {0x40800000u, 0xc0800000u},
	                                   {0x40900000u, 0xc0800000u},
	                                   {0x40a00000u, 0xc0800000u},
	                               },
	    0x40232b2bu);
	CheckLattice(comparison, 0x44, {
	                                   {0x40400000u, 0xc0e00000u},
	                                   {0x40200000u, 0xc0e00000u},
	                                   {0x40000000u, 0xc0e00000u},
	                                   {0x3fc00000u, 0xc0e00000u},
	                                   {0x3f800000u, 0xc0e00000u},
	                                   {0x40400000u, 0xc0c80000u},
	                                   {0x40200000u, 0xc0c80000u},
	                                   {0x40000000u, 0xc0c80000u},
	                                   {0x3fc00000u, 0xc0c80000u},
	                                   {0x3f800000u, 0xc0c80000u},
	                                   {0x40400000u, 0xc0b00000u},
	                                   {0x40200000u, 0xc0b00000u},
	                                   {0x40000000u, 0xc0b00000u},
	                                   {0x3fc00000u, 0xc0b00000u},
	                                   {0x3f800000u, 0xc0b00000u},
	                                   {0x40400000u, 0xc0980000u},
	                                   {0x40200000u, 0xc0980000u},
	                                   {0x40000000u, 0xc0980000u},
	                                   {0x3fc00000u, 0xc0980000u},
	                                   {0x3f800000u, 0xc0980000u},
	                                   {0x40400000u, 0xc0800000u},
	                                   {0x40200000u, 0xc0800000u},
	                                   {0x40000000u, 0xc0800000u},
	                                   {0x3fc00000u, 0xc0800000u},
	                                   {0x3f800000u, 0xc0800000u},
	                               },
	    0x40232b2bu);
	CheckLattice(comparison, 0x44, {
	                                   {0x40400000u, 0xc0e00000u},
	                                   {0x40400000u, 0xc0d80000u},
	                                   {0x40400000u, 0xc0d00000u},
	                                   {0x40400000u, 0xc0c80000u},
	                                   {0x40400000u, 0xc0c00000u},
	                                   {0x40300000u, 0xc0e00000u},
	                                   {0x40300000u, 0xc0d80000u},
	                                   {0x40300000u, 0xc0d00000u},
	                                   {0x40300000u, 0xc0c80000u},
	                                   {0x40300000u, 0xc0c00000u},
	                                   {0x40200000u, 0xc0e00000u},
	                                   {0x40200000u, 0xc0d80000u},
	                                   {0x40200000u, 0xc0d00000u},
	                                   {0x40200000u, 0xc0c80000u},
	                                   {0x40200000u, 0xc0c00000u},
	                                   {0x40100000u, 0xc0e00000u},
	                                   {0x40100000u, 0xc0d80000u},
	                                   {0x40100000u, 0xc0d00000u},
	                                   {0x40100000u, 0xc0c80000u},
	                                   {0x40100000u, 0xc0c00000u},
	                                   {0x40000000u, 0xc0e00000u},
	                                   {0x40000000u, 0xc0d80000u},
	                                   {0x40000000u, 0xc0d00000u},
	                                   {0x40000000u, 0xc0c80000u},
	                                   {0x40000000u, 0xc0c00000u},
	                               },
	    0x3f800000u);
	CheckLattice(comparison, 0x44, {
	                                   {0x00000000u, 0x00000000u},
	                                   {0x00000000u, 0x00000000u},
	                                   {0x00000000u, 0x00000000u},
	                                   {0x00000000u, 0x00000000u},
	                                   {0x00000000u, 0x00000000u},
	                                   {0x00000000u, 0x00000000u},
	                                   {0x00000000u, 0x00000000u},
	                                   {0x00000000u, 0x00000000u},
	                                   {0x00000000u, 0x00000000u},
	                                   {0x00000000u, 0x00000000u},
	                                   {0x00000000u, 0x00000000u},
	                                   {0x00000000u, 0x00000000u},
	                                   {0x00000000u, 0x00000000u},
	                                   {0x00000000u, 0x00000000u},
	                                   {0x00000000u, 0x00000000u},
	                                   {0x00000000u, 0x00000000u},
	                                   {0x00000000u, 0x00000000u},
	                                   {0x00000000u, 0x00000000u},
	                                   {0x00000000u, 0x00000000u},
	                                   {0x00000000u, 0x00000000u},
	                                   {0x00000000u, 0x00000000u},
	                                   {0x00000000u, 0x00000000u},
	                                   {0x00000000u, 0x00000000u},
	                                   {0x00000000u, 0x00000000u},
	                                   {0x00000000u, 0x00000000u},
	                               },
	    0x3f800000u);
	CheckLattice(comparison, 0x44, {
	                                   {0x00000000u, 0x00000000u},
	                                   {0x3e800000u, 0x3d800000u},
	                                   {0x3f000000u, 0x3e000000u},
	                                   {0x3f400000u, 0x3e400000u},
	                                   {0x3f800000u, 0x3e800000u},
	                                   {0x3e400000u, 0x3f000000u},
	                                   {0x3ee00000u, 0x3f100000u},
	                                   {0x3f300000u, 0x3f200000u},
	                                   {0x3f700000u, 0x3f300000u},
	                                   {0x3f980000u, 0x3f400000u},
	                                   {0x3ec00000u, 0x3f800000u},
	                                   {0x3f200000u, 0x3f880000u},
	                                   {0x3f600000u, 0x3f900000u},
	                                   {0x3f900000u, 0x3f980000u},
	                                   {0x3fb00000u, 0x3fa00000u},
	                                   {0x3f100000u, 0x3fc00000u},
	                                   {0x3f500000u, 0x3fc80000u},
	                                   {0x3f880000u, 0x3fd00000u},
	                                   {0x3fa80000u, 0x3fd80000u},
	                                   {0x3fc80000u, 0x3fe00000u},
	                                   {0x3f400000u, 0x40000000u},
	                                   {0x3f800000u, 0x40040000u},
	                                   {0x3fa00000u, 0x40080000u},
	                                   {0x3fc00000u, 0x400c0000u},
	                                   {0x3fe00000u, 0x40100000u},
	                               },
	    0x3fb87aeeu);
	CheckLattice(comparison, 0x44, {
	                                   {0xbe8ff12du, 0x3ef0509au},
	                                   {0xbe4c6d7bu, 0x3eebfcbcu},
	                                   {0x3f36b661u, 0xbe289622u},
	                                   {0x3f7281c1u, 0x3edc3099u},
	                                   {0x3f9f2926u, 0xbe8e8673u},
	                                   {0x3ef013c9u, 0x3f03b095u},
	                                   {0x3f3eb22du, 0x3ea77cf6u},
	                                   {0x3f5af372u, 0xbe5b5517u},
	                                   {0x3f4e3e4fu, 0x3f3b9dd4u},
	                                   {0x3f45fbbau, 0x3e0ae253u},
	                                   {0x3df2c1f8u, 0x3f42a857u},
	                                   {0x3eaa5716u, 0x3f28ee2du},
	                                   {0x3f6dad00u, 0x3f3fe374u},
	                                   {0x3f87e431u, 0x3ede7bc9u},
	                                   {0x3f2dfcf1u, 0x3f14eccbu},
	                                   {0x3ed6ba71u, 0x3eaea1c5u},
	                                   {0x3f2155dfu, 0x3f72239cu},
	                                   {0x3ede75f9u, 0x3efcfb6bu},
	                                   {0x3f9d0dc1u, 0x3f8962beu},
	                                   {0x3fa923bfu, 0x3f108f3fu},
	                                   {0x3ee802f9u, 0x3f064f6du},
	                                   {0x3f098cb8u, 0x3f04e624u},
	                                   {0x3f087866u, 0x3f8f9697u},
	                                   {0x3f3cdd5eu, 0x3f335237u},
	                                   {0x3f46fd9eu, 0x3f0c4476u},
	                               },
	    0x3f32b9a9u);
	CheckLattice(comparison, 0x44, {
	                                   {0x3d159a23u, 0x3da3882du},
	                                   {0x3ef033beu, 0x3e89c226u},
	                                   {0x3e894b90u, 0xbe9e0081u},
	                                   {0x3f73b466u, 0x3e62741fu},
	                                   {0x3f9d404fu, 0x3e0e627cu},
	                                   {0xbe947435u, 0x3dddf129u},
	                                   {0x3f015254u, 0x3e3e42d2u},
	                                   {0x3d933e20u, 0xbe3edc8cu},
	                                   {0x3f3d94b7u, 0x3e41cc1fu},
	                                   {0x3f6c0b7bu, 0x3e840facu},
	                                   {0x3ee51fecu, 0x3e859f4du},
	                                   {0xbe2309ebu, 0x3da67981u},
	                                   {0x3ded5ee9u, 0x3f687480u},
	                                   {0x3f627447u, 0x3d0dd3d1u},
	                                   {0x3fa399a6u, 0x3ebdf716u},
	                                   {0x3cc5de63u, 0x3f1e222eu},
	                                   {0x3f22baa6u, 0x3f0cd7f1u},
	                                   {0x3f388e95u, 0x3f556153u},
	                                   {0x3f92a82bu, 0x3f91f444u},
	                                   {0x3fb19603u, 0x3eec4b58u},
	                                   {0x3ef527e3u, 0x3f8d33ceu},
	                                   {0x3ea4e60fu, 0x3f95847cu},
	                                   {0x3ed5b8f0u, 0x3f1a966cu},
	                                   {0x3e9dec87u, 0x3f630bf6u},
	                                   {0x3fa3d5f0u, 0x3f2f6d75u},
	                               },
	    0x3f78ed1cu);
}

template <typename Action>
static void Reject(Action&& action) {
	try {
		action();
	} catch (const std::runtime_error&) {
		return;
	}
	throw std::runtime_error("invalid affine/lattice scale input was accepted");
}

int main() {
	try {
		Comparison comparison;
		TestNative(comparison);
		sgre::resource::MotionMeshMatrix matrix;
		matrix.XX = std::numeric_limits<float>::infinity();
		Reject([&] {
			static_cast<void>(sgre::scene::DecomposeMotionAffine(matrix));
		});
		matrix.XX = 1;
		matrix.TranslationX = std::numeric_limits<float>::quiet_NaN();
		Require(sgre::scene::DecomposeMotionAffine(matrix).has_value(), "translation must remain outside the linear decomposition");
		Reject([] {
			static_cast<void>(sgre::scene::MeasureMotionBezierLatticeScale({}));
		});
		std::cout << "native affine/lattice words=" << comparison.Words << " exact=" << comparison.Exact << " maximum relative/absolute error=" << comparison.MaximumRelativeError << '\n';
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
