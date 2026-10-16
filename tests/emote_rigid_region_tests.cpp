#include "sgre/scene/emote_rigid_region.hpp"
#include "sgre/scene/motion_bezier_lattice.hpp"

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

static sgre::resource::MotionPoint Point(const std::array<std::uint32_t, 2>& words) {
	return {std::bit_cast<float>(words[0]), std::bit_cast<float>(words[1])};
}

static std::size_t CheckNative(std::uint8_t degree, bool horizontal, const std::array<float, 4>& range, std::size_t constraint_count, std::initializer_list<std::array<std::uint32_t, 2>> control_words, const std::array<std::vector<std::uint32_t>, 3>& expected) {
	sgre::scene::EmoteRigidRegionConstraints constraints(degree, range, 1.25f, 2.5f, horizontal);
	Require(!constraints.Empty() && constraints.Count() == constraint_count, "native rigid-region sample grid count differs");
	sgre::resource::MotionBezierLattice lattice;
	lattice.EncodedDegree = degree;
	for (const auto& words : control_words) {
		lattice.Controls.push_back(Point(words));
	}
	std::size_t compared = 0;
	for (std::size_t iteration = 0; iteration != expected.size(); ++iteration) {
		const float relaxation = 1.1f - (static_cast<float>(iteration) / 2.0f) * 0.1f;
		constraints.Apply(lattice, relaxation);
		Require(expected[iteration].size() == lattice.Controls.size() * 2, "native rigid projection fixture count differs");
		for (std::size_t index = 0; index != lattice.Controls.size(); ++index) {
			const auto& point = lattice.Controls[index];
			Require(std::bit_cast<std::uint32_t>(point.X) == expected[iteration][index * 2] && std::bit_cast<std::uint32_t>(point.Y) == expected[iteration][index * 2 + 1], "native rigid projection differs at pass " + std::to_string(iteration) + ", point " + std::to_string(index));
			compared += 2;
		}
	}
	return compared;
}

static std::size_t TestNative() {
	std::size_t words = 0;
	words += CheckNative(0x33, false, {0.2f, 0.25f, 0.8f, 0.9f}, 94, {
	                                                                     {0x3d20832cu, 0xbdaba741u},
	                                                                     {0x3ea01237u, 0xbdad23b3u},
	                                                                     {0x3f235b9bu, 0x3db841dfu},
	                                                                     {0x3f709b4bu, 0x3bb0728au},
	                                                                     {0x3db0f866u, 0x3e999049u},
	                                                                     {0x3e8fc6e2u, 0x3ea6fe59u},
	                                                                     {0x3f3e828au, 0x3e7f03d8u},
	                                                                     {0x3f8a3844u, 0x3ebc5b29u},
	                                                                     {0x3d0bb7b7u, 0x3f4333c6u},
	                                                                     {0x3e8e5f9cu, 0x3f2ffafcu},
	                                                                     {0x3f4127bau, 0x3f2c36cbu},
	                                                                     {0x3f7de8c2u, 0x3f38fe81u},
	                                                                     {0xbb6f0927u, 0x3f73744cu},
	                                                                     {0x3ebad20fu, 0x3f7b454bu},
	                                                                     {0x3f3146f0u, 0x3f851074u},
	                                                                     {0x3f7241b7u, 0x3f7cb6f6u},
	                                                                 },
	    {{
	        {0xb8382368u, 0xb7a80090u, 0x3eac1c58u, 0xbcfdfafbu, 0x3f2e5c3bu, 0x3d90c02eu, 0x3f7ff262u, 0xb8c802b0u, 0x3c6a365cu, 0x3ea8883au, 0x3e8af5f8u, 0x3eb4b2c2u, 0x3f3aecbeu, 0x3e7f8635u, 0x3f87e367u, 0x3eb2c5feu, 0xbb8342e0u, 0x3f3fb691u, 0x3e89672cu, 0x3f30ae30u, 0x3f3cf0a1u, 0x3f2a22ecu, 0x3f80045cu, 0x3f327611u, 0xb8877a60u, 0x3f7ff1a6u, 0x3eb3ad8au, 0x3f809682u, 0x3f2f4bc3u, 0x3f844d41u, 0x3f7ff9f0u, 0x3f7ff54cu},
	        {0xb7170400u, 0x37811950u, 0x3eade1f6u, 0xbcfd5a60u, 0x3f2c3a44u, 0x3d44564fu, 0x3f7ffe87u, 0xb78bfd80u, 0x3c2d0065u, 0x3e9f33b4u, 0x3e8ceee1u, 0x3eb3ae3du, 0x3f355c70u, 0x3e7f248fu, 0x3f839e10u, 0x3eb07488u, 0xbaf73009u, 0x3f3606d3u, 0x3e8ba0a3u, 0x3f2dcac9u, 0x3f3888f3u, 0x3f28ce16u, 0x3f7bcf59u, 0x3f2f0bfdu, 0xb6be7b30u, 0x3f7ffe5cu, 0x3eaee584u, 0x3f7f6b5bu, 0x3f2ad1c4u, 0x3f8269a8u, 0x3f80001eu, 0x3f7ffe8fu},
	        {0x00000000u, 0x00000000u, 0x3eaebe97u, 0xbcdf35a2u, 0x3f2abb09u, 0x3d168f54u, 0x3f800000u, 0x00000000u, 0x3c0acadbu, 0x3e9f2bdau, 0x3e905ee7u, 0x3eb68973u, 0x3f32e555u, 0x3e82dac3u, 0x3f821ef7u, 0x3eaf7613u, 0xba8a8b53u, 0x3f321c48u, 0x3e8f3e93u, 0x3f2d2dcfu, 0x3f36da13u, 0x3f292045u, 0x3f7b8c71u, 0x3f2d118au, 0x00000000u, 0x3f800000u, 0x3eae8473u, 0x3f7ef47du, 0x3f295058u, 0x3f81955bu, 0x3f800000u, 0x3f800000u},
	    }});
	words += CheckNative(0x43, false, {0.0f, 0.25f, 1.0f, 1.0f}, 28, {
	                                                                     {0xba26eab9u, 0x3d3a15deu},
	                                                                     {0x3eada290u, 0xbd90cbafu},
	                                                                     {0x3f43319eu, 0x3d3fd068u},
	                                                                     {0x3f893ea8u, 0x3c29da81u},
	                                                                     {0xbd8d1401u, 0x3e6d6fafu},
	                                                                     {0x3ed35b9du, 0x3e913e27u},
	                                                                     {0x3f32d06eu, 0x3e38f36du},
	                                                                     {0x3f81576fu, 0x3ea01a14u},
	                                                                     {0xbd2ebd21u, 0x3f04aa8eu},
	                                                                     {0x3eaa8664u, 0x3f11bc2au},
	                                                                     {0x3f1ddd33u, 0x3f113c88u},
	                                                                     {0x3f76a5d7u, 0x3f054926u},
	                                                                     {0x3dcbabafu, 0x3f4187dau},
	                                                                     {0x3e751d11u, 0x3f3e1dd8u},
	                                                                     {0x3f34478fu, 0x3f3e704fu},
	                                                                     {0x3f669ff3u, 0x3f386620u},
	                                                                     {0xbc374793u, 0x3f70f326u},
	                                                                     {0x3ea8da95u, 0x3f7ea6d0u},
	                                                                     {0x3f3f8b86u, 0x3f698392u},
	                                                                     {0x3f8a248bu, 0x3f833b44u},
	                                                                 },
	    {{
	        {0xbb05f698u, 0xbb0b1770u, 0x3e9b9c40u, 0xbd9763aeu, 0x3f2fe623u, 0x3d07042fu, 0x3f7fc4e4u, 0x3adcea80u, 0xbd3c19dfu, 0x3e5bc36du, 0x3ecbf836u, 0x3e8df319u, 0x3f2b8a20u, 0x3e2e2fdbu, 0x3f7e91f1u, 0x3e93d8ddu, 0xbd03cf36u, 0x3f032ae0u, 0x3ea87929u, 0x3f10f2b0u, 0x3f1bba4eu, 0x3f101d6eu, 0x3f75f0fau, 0x3f02a36fu, 0x3dd01905u, 0x3f414314u, 0x3e747daau, 0x3f3df31au, 0x3f33f039u, 0x3f3e36edu, 0x3f6694a2u, 0x3f37dd53u, 0xbc347a5bu, 0x3f70ee11u, 0x3ea8d55cu, 0x3f7ea360u, 0x3f3f8592u, 0x3f697f17u, 0x3f8a249bu, 0x3f8335e9u},
	        {0xba408520u, 0xba3b9d60u, 0x3e9fa8cdu, 0xbd4dbeb2u, 0x3f2fc0c3u, 0x3d152850u, 0x3f7ffc1cu, 0x3a293a50u, 0xbd025c7eu, 0x3e617193u, 0x3ecd9718u, 0x3e91e52eu, 0x3f2c0232u, 0x3e2fadbfu, 0x3f8092a3u, 0x3e8e83a5u, 0xbcd4b14cu, 0x3f03a8f0u, 0x3ea92ac9u, 0x3f116989u, 0x3f1c07feu, 0x3f101953u, 0x3f77439du, 0x3f016a24u, 0x3dd2af5au, 0x3f415949u, 0x3e74c6b2u, 0x3f3e01fdu, 0x3f3402a4u, 0x3f3e31b2u, 0x3f66decau, 0x3f379c6du, 0xbc32da47u, 0x3f70efa9u, 0x3ea8d847u, 0x3f7ea42fu, 0x3f3f8724u, 0x3f697e80u, 0x3f8a27a3u, 0x3f833354u},
	        {0x00000000u, 0x00000000u, 0x3ea12764u, 0xbd1e6344u, 0x3f2edec8u, 0x3d0a0183u, 0x3f800000u, 0x00000000u, 0xbcc36bc3u, 0x3e6582d2u, 0x3eccfe24u, 0x3e93346cu, 0x3f2b991fu, 0x3e2efb4eu, 0x3f813a40u, 0x3e8b6a59u, 0xbcb7ae99u, 0x3f03fe96u, 0x3ea90544u, 0x3f1176d2u, 0x3f1bfd5au, 0x3f0ff042u, 0x3f780731u, 0x3f00b14bu, 0x3dd42972u, 0x3f4167beu, 0x3e74be41u, 0x3f3dff1eu, 0x3f340406u, 0x3f3e2718u, 0x3f670bbdu, 0x3f3775e3u, 0xbc31eca2u, 0x3f70f0acu, 0x3ea8d81du, 0x3f7ea3bdu, 0x3f3f8767u, 0x3f697d91u, 0x3f8a2987u, 0x3f8331ccu},
	    }});
	words += CheckNative(0x34, true, {0.25f, 0.0f, 1.0f, 0.8f}, 22, {
	                                                                    {0x3dc133e0u, 0x3d16ce1eu},
	                                                                    {0x3e1e76cfu, 0x3cd8220cu},
	                                                                    {0x3ee50d40u, 0x3b08e56du},
	                                                                    {0x3f3a55e7u, 0xbd82cfacu},
	                                                                    {0x3f82abc9u, 0xbd3f4271u},
	                                                                    {0x3daadd46u, 0x3ed8a1c6u},
	                                                                    {0x3e9ffe1du, 0x3e94d14au},
	                                                                    {0x3edcb9deu, 0x3ec8c55fu},
	                                                                    {0x3f3259d3u, 0x3eb38c94u},
	                                                                    {0x3f76383bu, 0x3e9acd01u},
	                                                                    {0x3c3f3fc3u, 0x3f3cb590u},
	                                                                    {0x3e8df7abu, 0x3f1b3933u},
	                                                                    {0x3f0d4741u, 0x3f1e2648u},
	                                                                    {0x3f27188eu, 0x3f16d50au},
	                                                                    {0x3f74c549u, 0x3f3a02a5u},
	                                                                    {0x3c764716u, 0x3f842327u},
	                                                                    {0x3e276a24u, 0x3f84db01u},
	                                                                    {0x3ee3b079u, 0x3f7561c1u},
	                                                                    {0x3f57be0cu, 0x3f728b3fu},
	                                                                    {0x3f8812b4u, 0x3f73c6e7u},
	                                                                },
	    {{
	        {0xbbaaa2c8u, 0x39c35c10u, 0x3e321d1cu, 0x3ca47103u, 0x3ee9f8f6u, 0xba81569au, 0x3f3ae29bu, 0xbd840297u, 0x3f82af36u, 0xbd3f5523u, 0x3c843f90u, 0x3eb5cafdu, 0x3e9eccbeu, 0x3e92a6e2u, 0x3edc3ad1u, 0x3ec82cffu, 0x3f325476u, 0x3eb404f8u, 0x3f75fd7bu, 0x3e9b07b8u, 0xbcdae235u, 0x3f2c6dd0u, 0x3e8e32b0u, 0x3f1b3241u, 0x3f0d8946u, 0x3f1f1c73u, 0x3f274a3cu, 0x3f184040u, 0x3f73e03au, 0x3f3a8246u, 0x39885e80u, 0x3f7ffcd5u, 0x3e486e39u, 0x3f84d58au, 0x3eea0f31u, 0x3f78d79au, 0x3f55f84eu, 0x3f77f9fcu, 0x3f7eaafau, 0x3f807c5eu},
	        {0xb9f0be80u, 0x39b0abe0u, 0x3e4a347au, 0x3ca0914du, 0x3ef001c6u, 0xba99a3dcu, 0x3f3b8fbbu, 0xbd83fa34u, 0x3f82bddfu, 0xbd4017ceu, 0x3c9f6671u, 0x3eb123f7u, 0x3ea13f9cu, 0x3e94fa49u, 0x3edd97d1u, 0x3ec9a6f7u, 0x3f328cffu, 0x3eb4b2efu, 0x3f76b4deu, 0x3e99f01du, 0xbca7c2c8u, 0x3f2af322u, 0x3e90498fu, 0x3f1d346du, 0x3f0e561fu, 0x3f20be13u, 0x3f27becau, 0x3f1950feu, 0x3f76b4a7u, 0x3f384cccu, 0x382ebe90u, 0x3f7ffefcu, 0x3e5d03dau, 0x3f84c5aeu, 0x3eee63beu, 0x3f7a3f4cu, 0x3f552a00u, 0x3f7a4a7bu, 0x3f7feddcu, 0x3f800891u},
	        {0x00000000u, 0x00000000u, 0x3e592288u, 0x3c9974ffu, 0x3ef3be93u, 0xbacd3b5bu, 0x3f3bfac6u, 0xbd84101du, 0x3f82c78bu, 0xbd40a769u, 0x3c948c02u, 0x3eaef64fu, 0x3ea17dbdu, 0x3e96e594u, 0x3eddc644u, 0x3ecadc20u, 0x3f329af5u, 0x3eb53f78u, 0x3f773317u, 0x3e9929e6u, 0xbc8fc6fcu, 0x3f2a8d06u, 0x3e901b91u, 0x3f1ed4afu, 0x3f0e6963u, 0x3f220d3au, 0x3f27e63fu, 0x3f1a2b23u, 0x3f78ac2cu, 0x3f36bb20u, 0x00000000u, 0x3f800000u, 0x3e67baf3u, 0x3f8470e9u, 0x3eef5a0du, 0x3f7aef0du, 0x3f53b581u, 0x3f7bea4bu, 0x3f800000u, 0x3f800000u},
	    }});
	words += CheckNative(0x44, true, {0.1f, 0.15f, 0.9f, 0.85f}, 56, {
	                                                                     {0x3c25be55u, 0x3cd917eeu},
	                                                                     {0x3e65688cu, 0x3d68f7d7u},
	                                                                     {0x3ece28d2u, 0xbb0a7148u},
	                                                                     {0x3f36a84eu, 0xbcebf785u},
	                                                                     {0x3f842f75u, 0xbd2bcab0u},
	                                                                     {0x3d767980u, 0x3e33d819u},
	                                                                     {0x3e8e2023u, 0x3e9cf9bfu},
	                                                                     {0x3f0d2583u, 0x3e98512eu},
	                                                                     {0x3f2e4135u, 0x3e8d3ee5u},
	                                                                     {0x3f886758u, 0x3e4b4490u},
	                                                                     {0x3cbfd28fu, 0x3f07f61fu},
	                                                                     {0x3eb1de0du, 0x3eec6b07u},
	                                                                     {0x3ee5bfbbu, 0x3f09f25fu},
	                                                                     {0x3f3808d1u, 0x3efa2e51u},
	                                                                     {0x3f8470d4u, 0x3ee9595eu},
	                                                                     {0x3db89f8eu, 0x3f2bcf34u},
	                                                                     {0x3e431f1du, 0x3f3bcc2cu},
	                                                                     {0x3f16f633u, 0x3f36dc9au},
	                                                                     {0x3f42ff19u, 0x3f541f4eu},
	                                                                     {0x3f8031afu, 0x3f3948a8u},
	                                                                     {0x3da6a5b1u, 0x3f6b5bbfu},
	                                                                     {0x3e84737bu, 0x3f85209du},
	                                                                     {0x3ef9da1du, 0x3f7ed41eu},
	                                                                     {0x3f336f52u, 0x3f85bbefu},
	                                                                     {0x3f8a801au, 0x3f757a0fu},
	                                                                 },
	    {{
	        {0x381e3c60u, 0x380c2938u, 0x3e7dc4e8u, 0x3ce76793u, 0x3edf3922u, 0xbc2b1cddu, 0x3f3d5a80u, 0xbc84ce90u, 0x3f800251u, 0x37a58320u, 0x3c421e89u, 0x3e49c62cu, 0x3e8a45c0u, 0x3e9ad89au, 0x3f0d9798u, 0x3e96fe0fu, 0x3f2e5301u, 0x3e8f9eafu, 0x3f81b2c2u, 0x3e7ea7b9u, 0xbd5186e8u, 0x3f1435f9u, 0x3eaafe1du, 0x3eeee63bu, 0x3ee47178u, 0x3f0a1929u, 0x3f3709feu, 0x3efc42efu, 0x3f7b87d3u, 0x3f01ec7eu, 0x3be4bf93u, 0x3f3d7fe4u, 0x3e333071u, 0x3f3e5a42u, 0x3f159b19u, 0x3f3714e8u, 0x3f417cc4u, 0x3f540ee1u, 0x3f73ed9bu, 0x3f43a8b9u, 0x38ddef00u, 0x3f7ff30fu, 0x3e767ff3u, 0x3f85dbe2u, 0x3ef6eee4u, 0x3f7d4b2du, 0x3f319f67u, 0x3f84ac31u, 0x3f800b6eu, 0x3f7ff4adu},
	        {0xb5603520u, 0x36da6de0u, 0x3e83ab75u, 0x3c94a0b1u, 0x3ee7a91fu, 0xbc58bcc3u, 0x3f4196cau, 0xbc606a7eu, 0x3f7fffbdu, 0x37547620u, 0x3c5bffc4u, 0x3e50039au, 0x3e89a331u, 0x3e98c5eeu, 0x3f0e3520u, 0x3e9529b9u, 0x3f2fec60u, 0x3e8d6810u, 0x3f81ea41u, 0x3e7ad612u, 0xbd2d8785u, 0x3f126653u, 0x3eab49d0u, 0x3eed2cb4u, 0x3ee57eb4u, 0x3f096e4fu, 0x3f38cab8u, 0x3ef9781du, 0x3f7fffebu, 0x3eff823cu, 0x3c5352f3u, 0x3f3bc232u, 0x3e35aaa2u, 0x3f3d74fcu, 0x3f16741du, 0x3f3600a3u, 0x3f436f53u, 0x3f51fa9du, 0x3f7958deu, 0x3f416b2cu, 0x35c5fd20u, 0x3f7ffeb8u, 0x3e803a1du, 0x3f841732u, 0x3eff13a3u, 0x3f799651u, 0x3f365959u, 0x3f82c736u, 0x3f80016cu, 0x3f7ffecfu},
	        {0x00000000u, 0x00000000u, 0x3e850838u, 0x3c4a674fu, 0x3eeb5effu, 0xbc5fff2bu, 0x3f438997u, 0xbc333d00u, 0x3f800000u, 0x00000000u, 0x3c5460dcu, 0x3e551b54u, 0x3e883e4au, 0x3e975a02u, 0x3f0e4f3du, 0x3e93e0bdu, 0x3f30b42au, 0x3e8bf9b1u, 0x3f817f67u, 0x3e7b2bfbu, 0xbd1ce3f7u, 0x3f113cb2u, 0x3eaace74u, 0x3eec3fe9u, 0x3ee5bdc3u, 0x3f09109fu, 0x3f399701u, 0x3ef7d647u, 0x3f809d24u, 0x3efe8eb4u, 0x3c77e4fbu, 0x3f3adcedu, 0x3e35e0dbu, 0x3f3d382au, 0x3f16a055u, 0x3f359069u, 0x3f44392cu, 0x3f50c16fu, 0x3f7bab1bu, 0x3f40828du, 0x00000000u, 0x3f800000u, 0x3e81c04fu, 0x3f835372u, 0x3f01395eu, 0x3f78460eu, 0x3f38ddfcu, 0x3f81ebd4u, 0x3f800000u, 0x3f800000u},
	    }});
	return words;
}

template <typename Operation>
static void Reject(Operation&& operation) {
	try {
		operation();
	} catch (const std::runtime_error&) {
		return;
	}
	throw std::runtime_error("invalid rigid region was accepted");
}

int main() {
	try {
		const auto words = TestNative();
		sgre::scene::EmoteRigidRegionConstraints empty(0x43, {0, 0, 1, 1}, 1, 2, false);
		Require(empty.Empty() && empty.Count() == 0, "full rigid rectangle has no exterior constraints");
		auto identity = sgre::scene::IdentityMotionBezierLattice(0x43);
		const auto original = identity.Controls;
		empty.Apply(identity, 1);
		for (std::size_t index = 0; index != original.size(); ++index) {
			Require(identity.Controls[index].X == original[index].X && identity.Controls[index].Y == original[index].Y, "empty rigid constraints changed controls");
		}
		Reject([&] {
			empty.Apply(identity, std::numeric_limits<float>::infinity());
		});
		Reject([] {
			sgre::scene::EmoteRigidRegionConstraints bad(0x43, {0, 0, 1, 1}, 0, 2, false);
		});
		Reject([] {
			sgre::scene::EmoteRigidRegionConstraints bad(0x73, {0, 0, 1, 1}, 1, 2, false);
		});
		Reject([] {
			sgre::scene::EmoteRigidRegionConstraints bad(0x43, {0, 100000, 1, 1}, 1, 2, false);
		});
		std::cout << "Original rigid construction/projection: 4 cases /200 constraints /" << words << " output words matched exactly\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
