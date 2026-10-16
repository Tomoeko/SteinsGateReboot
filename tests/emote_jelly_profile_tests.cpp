#include "sgre/scene/emote_jelly_profile.hpp"

#include "sgre/scene/emote_bobble_model.hpp"
#include "sgre/scene/emote_bobble_sections.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

static void Require(bool condition, const std::string& message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

struct NativeProfile {
	std::uint32_t Count = 0;
	float Radius = 0;
	float HeightRate = 0;
	float Density = 0;
	std::uint32_t Mass = 0;
	std::uint32_t Length = 0;
	std::vector<std::uint32_t> Weights;
	std::vector<std::uint32_t> Radii;
};

static void TestNativeProfiles() {
	const std::vector<NativeProfile> inputs{
	    {2u, 1.0f, 0.25f, 0.5f, 0x3db84e89u, 0x3e800000u, {0x00000000u, 0x3f800000u}, {0x3f7fffffu, 0x3ea66668u}},
	    {3u, 1.625f, 0.5f, 2.0f, 0x40b3bddcu, 0x3f500000u, {0x00000000u, 0x3f3b13b2u, 0x3e89d89au}, {0x3fd00000u, 0x3fa2b3e1u, 0x3ecef510u}},
	    {4u, 1.75f, 0.75f, 200.0f, 0x44b193d4u, 0x3fa80000u, {0x00000000u, 0x3f023ee1u, 0x3eb6a4afu, 0x3e09bb21u}, {0x3fdfffffu, 0x3fca9bccu, 0x3f9df65cu, 0x3ecb409du}},
	    {8u, 1.125f, 1.0f, 10.0f, 0x41ee9090u, 0x3f900000u, {0x00000000u, 0x3e59ef95u, 0x3e50fac9u, 0x3e3f112cu, 0x3e2432bfu, 0x3e005f86u, 0x3da72f08u, 0x3ceed550u}, {0x3f900000u, 0x3f8ece0au, 0x3f8a9812u, 0x3f830fa8u, 0x3f6f28f5u, 0x3f4dac52u, 0x3f1afbcau, 0x3e4206d1u}},
	    {2u, 1.375f, 1.5f, 0.5f, 0x40930124u, 0x40040000u, {0x00000000u, 0x3f800000u}, {0x3fb00000u, 0x3f419996u}},
	    {3u, 1.75f, 2.0f, 2.0f, 0x42339829u, 0x40600000u, {0x00000000u, 0x3f1b6db7u, 0x3ec92491u}, {0x3fe00000u, 0x400afd04u, 0x3f5b796fu}},
	    {4u, 0.375f, 0.125f, 200.0f, 0x3efe06f8u, 0x3d400000u, {0x00000000u, 0x3f0dd79du, 0x3eab0eb9u, 0x3de50835u}, {0x3ebffffeu, 0x3ea103c3u, 0x3e69abf7u, 0x3d8d4610u}},
	    {8u, 1.375f, 0.375f, 10.0f, 0x4120c93fu, 0x3f040000u, {0x00000000u, 0x3e830653u, 0x3e622256u, 0x3e3c9dd1u, 0x3e157e86u, 0x3dd9897cu, 0x3d84df42u, 0x3cb40271u}, {0x3fb00001u, 0x3fa5c676u, 0x3f991146u, 0x3f8a907bu, 0x3f734031u, 0x3f4a109au, 0x3f139334u, 0x3e3392edu}},
	    {2u, 1.625f, 0.25f, 0.5f, 0x3ec5b740u, 0x3ed00000u, {0x00000000u, 0x3f800000u}, {0x3fcfffffu, 0x3f073334u}},
	    {3u, 0.625f, 0.5f, 2.0f, 0x3ea39fe7u, 0x3ea00000u, {0x00000000u, 0x3f3b13b2u, 0x3e89d89au}, {0x3f200000u, 0x3efa4fd1u, 0x3e1f32aau}},
	    {4u, 1.25f, 0.75f, 200.0f, 0x44016dfcu, 0x3f700000u, {0x00000000u, 0x3f023ee1u, 0x3eb6a4adu, 0x3e09bb20u}, {0x3f9fffffu, 0x3f90b86du, 0x3f61a916u, 0x3e912e27u}},
	    {8u, 2.0f, 1.0f, 10.0f, 0x43278d37u, 0x40000000u, {0x00000000u, 0x3e59ef96u, 0x3e50facau, 0x3e3f112du, 0x3e2432c0u, 0x3e005f87u, 0x3da72f09u, 0x3ceed552u}, {0x40000000u, 0x3ffde011u, 0x3ff663afu, 0x3fe8ff63u, 0x3fd4962fu, 0x3fb6d210u, 0x3f89c35eu, 0x3eac77d6u}},
	    {2u, 1.75f, 1.5f, 0.5f, 0x41178863u, 0x40280000u, {0x00000000u, 0x3f800000u}, {0x3fe00000u, 0x3f766662u}},
	    {3u, 0.25f, 2.0f, 2.0f, 0x3e060a92u, 0x3f000000u, {0x00000000u, 0x3f1b6db8u, 0x3ec92492u}, {0x3e800000u, 0x3e9ed805u, 0x3dfad3ecu}},
	    {4u, 1.625f, 0.125f, 200.0f, 0x42217c94u, 0x3e500000u, {0x00000000u, 0x3f0dd79du, 0x3eab0eb9u, 0x3de50836u}, {0x3fcffffeu, 0x3fae6ebeu, 0x3f7d24f6u, 0x3e990be6u}},
	    {8u, 1.125f, 0.375f, 10.0f, 0x40b020bbu, 0x3ed80000u, {0x00000000u, 0x3e830653u, 0x3e622257u, 0x3e3c9dd1u, 0x3e157e87u, 0x3dd9897cu, 0x3d84df42u, 0x3cb40271u}, {0x3f900000u, 0x3f87a260u, 0x3f7a795au, 0x3f62bde0u, 0x3f4705f9u, 0x3f255366u, 0x3ef17c82u, 0x3e12ec92u}},
	    {2u, 0.375f, 0.25f, 0.5f, 0x3b9b8243u, 0x3dc00000u, {0x00000000u, 0x3f800000u}, {0x3ebfffffu, 0x3df9999cu}},
	    {3u, 2.0f, 0.5f, 2.0f, 0x41278d37u, 0x3f800000u, {0x00000000u, 0x3f3b13b3u, 0x3e89d89au}, {0x40000000u, 0x3fc83fdau, 0x3efeb776u}},
	    {4u, 1.5f, 0.75f, 200.0f, 0x445fa787u, 0x3f900000u, {0x00000000u, 0x3f023ee0u, 0x3eb6a4adu, 0x3e09bb20u}, {0x3fc00000u, 0x3fadaa1du, 0x3f876574u, 0x3eae3762u}},
	    {8u, 0.75f, 1.0f, 10.0f, 0x410d5f26u, 0x3f400000u, {0x00000000u, 0x3e59ef95u, 0x3e50fac9u, 0x3e3f112cu, 0x3e2432bfu, 0x3e005f86u, 0x3da72f08u, 0x3ceed551u}, {0x3f400000u, 0x3f3e680du, 0x3f38cac3u, 0x3f2ebf8au, 0x3f1f70a3u, 0x3f091d8cu, 0x3ecea50du, 0x3e0159e0u}},
	    {2u, 2.0f, 1.5f, 0.5f, 0x416231d7u, 0x40400000u, {0x00000000u, 0x3f800000u}, {0x40000000u, 0x3f8ccccau}},
	    {3u, 0.5f, 2.0f, 2.0f, 0x3f860a92u, 0x3f800000u, {0x00000000u, 0x3f1b6db8u, 0x3ec92492u}, {0x3f000000u, 0x3f1ed805u, 0x3e7ad3ecu}},
	    {4u, 0.125f, 0.125f, 200.0f, 0x3c9688dfu, 0x3c800000u, {0x00000000u, 0x3f0dd79eu, 0x3eab0eb9u, 0x3de50835u}, {0x3dfffffdu, 0x3dd6afafu, 0x3d9bc7fau, 0x3cbc5d6au}},
	    {8u, 0.5f, 0.375f, 10.0f, 0x3ef76683u, 0x3e400000u, {0x00000000u, 0x3e830653u, 0x3e622257u, 0x3e3c9dd1u, 0x3e157e86u, 0x3dd9897cu, 0x3d84df42u, 0x3cb40271u}, {0x3f000000u, 0x3ef120abu, 0x3edea4c2u, 0x3ec98c55u, 0x3eb0e8ddu, 0x3e92f4ccu, 0x3e56a791u, 0x3d829966u}},
	    {2u, 1.875f, 0.25f, 0.5f, 0x3f17dd36u, 0x3ef00000u, {0x00000000u, 0x3f800000u}, {0x3fefffffu, 0x3f1c0001u}},
	    {3u, 0.5f, 0.5f, 2.0f, 0x3e278d37u, 0x3e800000u, {0x00000000u, 0x3f3b13b3u, 0x3e89d89au}, {0x3f000000u, 0x3ec83fdau, 0x3dfeb776u}},
	    {4u, 1.5f, 0.75f, 200.0f, 0x445fa787u, 0x3f900000u, {0x00000000u, 0x3f023ee0u, 0x3eb6a4adu, 0x3e09bb20u}, {0x3fc00000u, 0x3fadaa1du, 0x3f876574u, 0x3eae3762u}},
	    {8u, 0.25f, 1.0f, 10.0f, 0x3ea78d37u, 0x3e800000u, {0x00000000u, 0x3e59ef96u, 0x3e50facau, 0x3e3f112du, 0x3e2432c0u, 0x3e005f87u, 0x3da72f09u, 0x3ceed552u}, {0x3e800000u, 0x3e7de011u, 0x3e7663afu, 0x3e68ff63u, 0x3e54962fu, 0x3e36d210u, 0x3e09c35eu, 0x3d2c77d6u}},
	    {2u, 0.25f, 1.5f, 0.5f, 0x3ce231d7u, 0x3ec00000u, {0x00000000u, 0x3f800000u}, {0x3e800000u, 0x3e0ccccau}},
	    {3u, 0.5f, 2.0f, 2.0f, 0x3f860a92u, 0x3f800000u, {0x00000000u, 0x3f1b6db8u, 0x3ec92492u}, {0x3f000000u, 0x3f1ed805u, 0x3e7ad3ecu}},
	    {4u, 2.0f, 0.125f, 200.0f, 0x429688dfu, 0x3e800000u, {0x00000000u, 0x3f0dd79eu, 0x3eab0eb9u, 0x3de50835u}, {0x3ffffffdu, 0x3fd6afafu, 0x3f9bc7fau, 0x3ebc5d6au}},
	    {8u, 1.75f, 0.375f, 10.0f, 0x41a5bd2du, 0x3f280000u, {0x00000000u, 0x3e830653u, 0x3e622256u, 0x3e3c9dd1u, 0x3e157e86u, 0x3dd9897cu, 0x3d84df42u, 0x3cb40271u}, {0x3fe00001u, 0x3fd2fc96u, 0x3fc2d02bu, 0x3fb05acbu, 0x3f9acbc2u, 0x3f809633u, 0x3f3bd29fu, 0x3e648c73u}},
	    {2u, 1.0f, 0.25f, 0.5f, 0x3db84e89u, 0x3e800000u, {0x00000000u, 0x3f800000u}, {0x3f7fffffu, 0x3ea66668u}},
	    {3u, 0.75f, 0.5f, 2.0f, 0x3f0d5f26u, 0x3ec00000u, {0x00000000u, 0x3f3b13b3u, 0x3e89d89au}, {0x3f400000u, 0x3f162fe4u, 0x3e3f0998u}},
	    {4u, 0.625f, 0.75f, 200.0f, 0x42816dfcu, 0x3ef00000u, {0x00000000u, 0x3f023ee1u, 0x3eb6a4adu, 0x3e09bb20u}, {0x3f1fffffu, 0x3f10b86du, 0x3ee1a916u, 0x3e112e27u}},
	    {8u, 0.5f, 1.0f, 10.0f, 0x40278d37u, 0x3f000000u, {0x00000000u, 0x3e59ef96u, 0x3e50facau, 0x3e3f112du, 0x3e2432c0u, 0x3e005f87u, 0x3da72f09u, 0x3ceed552u}, {0x3f000000u, 0x3efde011u, 0x3ef663afu, 0x3ee8ff63u, 0x3ed4962fu, 0x3eb6d210u, 0x3e89c35eu, 0x3dac77d6u}},
	    {2u, 1.625f, 1.5f, 0.5f, 0x40f2a6b7u, 0x401c0000u, {0x00000000u, 0x3f800000u}, {0x3fd00001u, 0x3f64ccc9u}},
	    {3u, 0.5f, 2.0f, 2.0f, 0x3f860a92u, 0x3f800000u, {0x00000000u, 0x3f1b6db8u, 0x3ec92492u}, {0x3f000000u, 0x3f1ed805u, 0x3e7ad3ecu}},
	    {4u, 0.875f, 0.125f, 200.0f, 0x40c9b163u, 0x3de00000u, {0x00000000u, 0x3f0dd79du, 0x3eab0eb8u, 0x3de50835u}, {0x3f5ffffdu, 0x3f3bd9b9u, 0x3f084efbu, 0x3e24d1bdu}},
	    {8u, 0.125f, 0.375f, 10.0f, 0x3bf76683u, 0x3d400000u, {0x00000000u, 0x3e830653u, 0x3e622257u, 0x3e3c9dd1u, 0x3e157e86u, 0x3dd9897cu, 0x3d84df42u, 0x3cb40271u}, {0x3e000000u, 0x3df120abu, 0x3ddea4c2u, 0x3dc98c55u, 0x3db0e8ddu, 0x3d92f4ccu, 0x3d56a791u, 0x3c829966u}},
	    {2u, 0.875f, 0.25f, 0.5f, 0x3d76f139u, 0x3e600000u, {0x00000000u, 0x3f800000u}, {0x3f5fffffu, 0x3e91999bu}},
	    {3u, 1.75f, 0.5f, 2.0f, 0x40e07e34u, 0x3f600000u, {0x00000000u, 0x3f3b13b3u, 0x3e89d89au}, {0x3fe00000u, 0x3faf37dfu, 0x3edee087u}},
	    {4u, 1.375f, 0.75f, 200.0f, 0x442c4555u, 0x3f840000u, {0x00000000u, 0x3f023ee1u, 0x3eb6a4aeu, 0x3e09bb20u}, {0x3fafffffu, 0x3f9f3145u, 0x3f7839ffu, 0x3e9fb2c4u}},
	    {8u, 0.75f, 1.0f, 10.0f, 0x410d5f26u, 0x3f400000u, {0x00000000u, 0x3e59ef95u, 0x3e50fac9u, 0x3e3f112cu, 0x3e2432bfu, 0x3e005f86u, 0x3da72f08u, 0x3ceed551u}, {0x3f400000u, 0x3f3e680du, 0x3f38cac3u, 0x3f2ebf8au, 0x3f1f70a3u, 0x3f091d8cu, 0x3ecea50du, 0x3e0159e0u}},
	    {2u, 1.5f, 1.5f, 0.5f, 0x40beda0du, 0x40100000u, {0x00000000u, 0x3f800000u}, {0x3fc00000u, 0x3f53332fu}},
	    {3u, 0.125f, 2.0f, 2.0f, 0x3c860a92u, 0x3e800000u, {0x00000000u, 0x3f1b6db8u, 0x3ec92492u}, {0x3e000000u, 0x3e1ed805u, 0x3d7ad3ecu}},
	    {4u, 1.5f, 0.125f, 200.0f, 0x41fe06f8u, 0x3e400000u, {0x00000000u, 0x3f0dd79du, 0x3eab0eb9u, 0x3de50835u}, {0x3fbffffeu, 0x3fa103c3u, 0x3f69abf7u, 0x3e8d4610u}},
	    {8u, 0.125f, 0.375f, 10.0f, 0x3bf76683u, 0x3d400000u, {0x00000000u, 0x3e830653u, 0x3e622257u, 0x3e3c9dd1u, 0x3e157e86u, 0x3dd9897cu, 0x3d84df42u, 0x3cb40271u}, {0x3e000000u, 0x3df120abu, 0x3ddea4c2u, 0x3dc98c55u, 0x3db0e8ddu, 0x3d92f4ccu, 0x3d56a791u, 0x3c829966u}},
	    {2u, 0.75f, 0.25f, 0.5f, 0x3d1b8243u, 0x3e400000u, {0x00000000u, 0x3f800000u}, {0x3f3fffffu, 0x3e79999cu}},
	    {3u, 0.5f, 0.5f, 2.0f, 0x3e278d37u, 0x3e800000u, {0x00000000u, 0x3f3b13b3u, 0x3e89d89au}, {0x3f000000u, 0x3ec83fdau, 0x3dfeb776u}},
	    {4u, 1.25f, 0.75f, 200.0f, 0x44016dfcu, 0x3f700000u, {0x00000000u, 0x3f023ee1u, 0x3eb6a4adu, 0x3e09bb20u}, {0x3f9fffffu, 0x3f90b86du, 0x3f61a916u, 0x3e912e27u}},
	    {8u, 1.5f, 1.0f, 10.0f, 0x428d5f26u, 0x3fc00000u, {0x00000000u, 0x3e59ef95u, 0x3e50fac9u, 0x3e3f112cu, 0x3e2432bfu, 0x3e005f86u, 0x3da72f08u, 0x3ceed551u}, {0x3fc00000u, 0x3fbe680du, 0x3fb8cac3u, 0x3faebf8au, 0x3f9f70a3u, 0x3f891d8cu, 0x3f4ea50du, 0x3e8159e0u}},
	    {2u, 1.875f, 1.5f, 0.5f, 0x413a60f0u, 0x40340000u, {0x00000000u, 0x3f800000u}, {0x3ff00000u, 0x3f83fffdu}},
	    {3u, 0.625f, 2.0f, 2.0f, 0x4002e653u, 0x3fa00000u, {0x00000000u, 0x3f1b6db7u, 0x3ec92492u}, {0x3f200000u, 0x3f468e06u, 0x3e9cc474u}},
	    {4u, 0.5f, 0.125f, 200.0f, 0x3f9688dfu, 0x3d800000u, {0x00000000u, 0x3f0dd79eu, 0x3eab0eb9u, 0x3de50835u}, {0x3efffffdu, 0x3ed6afafu, 0x3e9bc7fau, 0x3dbc5d6au}},
	    {8u, 1.125f, 0.375f, 10.0f, 0x40b020bbu, 0x3ed80000u, {0x00000000u, 0x3e830653u, 0x3e622257u, 0x3e3c9dd1u, 0x3e157e87u, 0x3dd9897cu, 0x3d84df42u, 0x3cb40271u}, {0x3f900000u, 0x3f87a260u, 0x3f7a795au, 0x3f62bde0u, 0x3f4705f9u, 0x3f255366u, 0x3ef17c82u, 0x3e12ec92u}},
	    {2u, 1.75f, 0.25f, 0.5f, 0x3ef6f139u, 0x3ee00000u, {0x00000000u, 0x3f800000u}, {0x3fdfffffu, 0x3f11999bu}},
	    {3u, 1.75f, 0.5f, 2.0f, 0x40e07e34u, 0x3f600000u, {0x00000000u, 0x3f3b13b3u, 0x3e89d89au}, {0x3fe00000u, 0x3faf37dfu, 0x3edee087u}},
	    {4u, 1.75f, 0.75f, 200.0f, 0x44b193d4u, 0x3fa80000u, {0x00000000u, 0x3f023ee1u, 0x3eb6a4afu, 0x3e09bb21u}, {0x3fdfffffu, 0x3fca9bccu, 0x3f9df65cu, 0x3ecb409du}},
	    {8u, 1.875f, 1.0f, 10.0f, 0x430a0eebu, 0x3ff00000u, {0x00000000u, 0x3e59ef95u, 0x3e50fac9u, 0x3e3f112cu, 0x3e2432c0u, 0x3e005f87u, 0x3da72f09u, 0x3ceed551u}, {0x3ff00000u, 0x3fee0210u, 0x3fe6fd74u, 0x3fda6f6du, 0x3fc74cccu, 0x3fab64efu, 0x3f812728u, 0x3ea1b059u}},
	    {2u, 0.75f, 1.5f, 0.5f, 0x3f3eda0du, 0x3f900000u, {0x00000000u, 0x3f800000u}, {0x3f400000u, 0x3ed3332fu}},
	    {3u, 2.0f, 2.0f, 2.0f, 0x42860a92u, 0x40800000u, {0x00000000u, 0x3f1b6db8u, 0x3ec92492u}, {0x40000000u, 0x401ed805u, 0x3f7ad3ecu}},
	    {4u, 1.5f, 0.125f, 200.0f, 0x41fe06f8u, 0x3e400000u, {0x00000000u, 0x3f0dd79du, 0x3eab0eb9u, 0x3de50835u}, {0x3fbffffeu, 0x3fa103c3u, 0x3f69abf7u, 0x3e8d4610u}},
	    {8u, 0.25f, 0.375f, 10.0f, 0x3d776683u, 0x3dc00000u, {0x00000000u, 0x3e830653u, 0x3e622257u, 0x3e3c9dd1u, 0x3e157e86u, 0x3dd9897cu, 0x3d84df42u, 0x3cb40271u}, {0x3e800000u, 0x3e7120abu, 0x3e5ea4c2u, 0x3e498c55u, 0x3e30e8ddu, 0x3e12f4ccu, 0x3dd6a791u, 0x3d029966u}},
	};
	for (const auto& input : inputs) {
		sgre::scene::EmoteJellyProfileDefinition definition;
		definition.ParticleCount = input.Count;
		definition.Radius = input.Radius;
		definition.HeightRate = input.HeightRate;
		definition.Density = input.Density;
		const auto profile = sgre::scene::BuildEmoteJellyProfile(definition);
		Require(std::bit_cast<std::uint32_t>(profile.Mass) == input.Mass && std::bit_cast<std::uint32_t>(profile.Length) == input.Length, "original Jelly cap mass/length arithmetic changed");
		for (std::size_t index = 0; index != profile.Radii.size(); ++index) {
			Require(std::bit_cast<std::uint32_t>(profile.MassWeights[index]) == input.Weights[index] && std::bit_cast<std::uint32_t>(profile.Radii[index]) == input.Radii[index], "original Jelly cross-section mass/radius arithmetic changed at index " + std::to_string(index));
		}
	}
}

static std::uint32_t ReadWord(std::span<const std::uint8_t> bytes, std::size_t& cursor) {
	if (cursor > bytes.size() || 4 > bytes.size() - cursor) {
		throw std::runtime_error("truncated EJP1 profile-chain vector");
	}
	const auto value = static_cast<std::uint32_t>(bytes[cursor]) | static_cast<std::uint32_t>(bytes[cursor + 1]) << 8 | static_cast<std::uint32_t>(bytes[cursor + 2]) << 16 | static_cast<std::uint32_t>(bytes[cursor + 3]) << 24;
	cursor += 4;

	return value;
}

static float ReadFloat(std::span<const std::uint8_t> bytes, std::size_t& cursor) {
	return std::bit_cast<float>(ReadWord(bytes, cursor));
}

static std::array<std::uint32_t, 21> ParticleWords(const sgre::scene::EmoteBobbleParticle& particle) {
	std::array<std::uint32_t, 21> result;
	std::size_t index = 0;
	for (const auto& vector : {particle.Previous, particle.Predicted, particle.Position, particle.Velocity, particle.Force}) {
		for (const auto value : vector) {
			result[index++] = std::bit_cast<std::uint32_t>(value);
		}
	}
	for (const auto value : {particle.Friction, particle.Mass, particle.InverseMass}) {
		result[index++] = std::bit_cast<std::uint32_t>(value);
	}
	result[index++] = particle.Fixed ? 1 : 0;
	result[index++] = std::bit_cast<std::uint32_t>(particle.AlignmentSpring);
	result[index] = std::bit_cast<std::uint32_t>(particle.LengthSpring);

	return result;
}

static void TestNativeProfileChains(const std::filesystem::path& path) {
	std::ifstream stream(path, std::ios::binary | std::ios::ate);
	if (!stream || stream.tellg() < 8 || stream.tellg() > 1024 * 1024) {
		throw std::runtime_error("profile chain requires bounded actual EJP1 vector file");
	}
	const auto size = static_cast<std::size_t>(stream.tellg());
	std::vector<std::uint8_t> bytes(size);
	stream.seekg(0);
	stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size));
	Require(static_cast<std::size_t>(stream.gcount()) == size && std::string(bytes.begin(), bytes.begin() + 4) == "EJP1", "profile-chain magic/length changed");
	std::size_t cursor = 4;
	Require(ReadWord(bytes, cursor) == 8, "profile-chain case count changed");
	float maximum_shape_error = 0;
	std::size_t core_words = 0;
	for (std::size_t case_index = 0; case_index != 8; ++case_index) {
		sgre::scene::EmoteJellyProfileDefinition input;
		input.ParticleCount = ReadWord(bytes, cursor);
		input.Radius = ReadFloat(bytes, cursor);
		input.HeightRate = ReadFloat(bytes, cursor);
		input.Density = ReadFloat(bytes, cursor);
		const auto profile = sgre::scene::BuildEmoteJellyProfile(input);
		sgre::scene::EmoteBobbleDefinition definition;
		const auto adhoc = ReadWord(bytes, cursor);
		Require(adhoc <= 1, "profile-chain solver selector changed");
		definition.AdhocSimulation = adhoc != 0;
		definition.FrictionBegin = ReadFloat(bytes, cursor);
		definition.FrictionEnd = ReadFloat(bytes, cursor);
		definition.AlignmentSpring = ReadFloat(bytes, cursor);
		definition.LengthSpring = ReadFloat(bytes, cursor);
		definition.MassScale = profile.Mass;
		definition.Length = profile.Length;
		definition.MassWeights = profile.MassWeights;
		sgre::scene::EmoteBobbleSectionDefinition section_definition;
		for (auto& value : section_definition.Normal) {
			value = ReadFloat(bytes, cursor);
		}
		for (auto& value : section_definition.Tangent) {
			value = ReadFloat(bytes, cursor);
		}
		section_definition.OrientationInheritance = ReadFloat(bytes, cursor);
		section_definition.RadiusInheritance = ReadFloat(bytes, cursor);
		section_definition.Radii = profile.Radii;
		sgre::scene::EmoteBobbleModel model(definition);
		sgre::scene::EmoteBobbleSections sections(model, section_definition);
		sgre::scene::EmoteBobblePosture posture;
		model.SetPosture(posture);
		model.Initialize();
		sections.Refresh();
		Require(model.Particles()[0].Mass == 0 && model.Particles()[0].InverseMass == 0 && model.Particles()[0].Fixed, "actual sampled Jelly root keeps zero mass and inverse mass while fixed");
		Require(ReadWord(bytes, cursor) == 8, "profile-chain step count changed");
		for (std::size_t step_index = 0; step_index != 8; ++step_index) {
			const float seconds = ReadFloat(bytes, cursor);
			for (auto& value : posture.Coordinate) {
				value = ReadFloat(bytes, cursor);
			}
			model.SetPosture(posture);
			model.ClearForces();
			for (std::size_t particle = 0; particle != model.Particles().size(); ++particle) {
				std::array<float, 3> force;
				for (auto& value : force) {
					value = ReadFloat(bytes, cursor);
				}
				if (particle != 0) {
					model.AddForce(particle, force);
				}
			}
			model.Step(seconds);
			sections.Refresh();
			for (std::size_t particle = 0; particle != model.Particles().size(); ++particle) {
				const auto actual = ParticleWords(model.Particles()[particle]);
				for (std::size_t field = 0; field != actual.size(); ++field) {
					const auto expected = ReadWord(bytes, cursor);
					// Preserve record width while excluding actual uninitialized root springs.
					if (particle == 0 && field >= 19) {
						continue;
					}
					Require(actual[field] == expected, "native profile-chain numeric mismatch case " + std::to_string(case_index) + ", step " + std::to_string(step_index) + ", particle " + std::to_string(particle) + ", field " + std::to_string(field) + ": actual " + std::to_string(actual[field]) + ", expected " + std::to_string(expected));
					++core_words;
				}
			}
			const auto published = sections.Sections();
			for (std::size_t particle = 0; particle != published.size(); ++particle) {
				std::array<std::uint32_t, 8> expected;
				for (auto& value : expected) {
					value = ReadWord(bytes, cursor);
				}
				const auto& state = published[particle];
				const std::array<float, 7> actual{state.Center[0], state.Center[1], state.Radius[0], state.Radius[1], state.Angle, state.RelativeAngle, state.LengthScale};
				Require(expected[5] == 1, "native profile-chain actual ellipse flag changed");
				for (std::size_t field = 0; field != actual.size(); ++field) {
					const float value = std::bit_cast<float>(expected[field < 5 ? field : field + 1]);
					const float error = std::abs(actual[field] - value);
					maximum_shape_error = std::max(maximum_shape_error, error);
					const float tolerance = field < 2 ? 0.0f : 0.000004f * std::max(1.0f, std::abs(value));
					Require(std::isfinite(actual[field]) && error <= tolerance, "native profile-chain shape mismatch case " + std::to_string(case_index) + ", step " + std::to_string(step_index) + ", particle " + std::to_string(particle) + ", field " + std::to_string(field));
				}
			}
		}
	}
	Require(cursor == bytes.size(), "profile-chain trailing data changed");
	std::cout << "constructor-profile native chain: " << core_words << " exact numerical words; shape max error " << maximum_shape_error << '\n';
}

static void TestOwnedZeroMassRoot() {
	sgre::scene::EmoteJellyProfileDefinition input;
	input.Radius = 0.5f;
	input.HeightRate = 0.5f;
	input.ParticleCount = 4;
	const auto profile = sgre::scene::BuildEmoteJellyProfile(input);
	sgre::scene::EmoteBobbleDefinition definition;
	definition.Length = profile.Length;
	definition.MassScale = profile.Mass;
	definition.MassWeights = profile.MassWeights;
	sgre::scene::EmoteBobbleModel model(definition);
	Require(model.Particles()[0].Fixed && model.Particles()[0].Mass == 0 && model.Particles()[0].InverseMass == 0, "actual profile's fixed root requires genuine zero mass support");
	sgre::scene::EmoteBobblePosture posture;
	model.SetPosture(posture);
	model.Initialize();
	model.AddForce(3, {0.25f, 0, 0});
	model.Step(1.0f / 60.0f);
	Require(model.Particles()[0].Position == std::array<float, 3>{0, 0, 0} && model.Particles()[3].Position[0] > 0, "actual profile drives its real owned free particles while retaining its genuine fixed anchor");
}

int main(int argc, char** argv) {
	try {
		Require(argc <= 2, "usage: emote_jelly_profile_tests [native-profile-chain-vectors.bin]");
		TestNativeProfiles();
		TestOwnedZeroMassRoot();
		std::cout << "Jelly constructor profiles: 64 actual arithmetic vectors matched exactly\n";
		if (argc == 2) {
			TestNativeProfileChains(argv[1]);
		}
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
