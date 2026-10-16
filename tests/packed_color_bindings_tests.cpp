#include "sgre/script/packed_color_bindings.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

static void Require(bool condition) {
	if (!condition) {
		throw std::runtime_error("native packed color conversion mismatch");
	}
}

int main() {
	try {
		sgre::script::SquirrelVm vm;
		sgre::script::RegisterPackedColorFunctions(vm);
		static constexpr std::array<std::array<std::uint32_t, 3>, 64> kNative{{
		    {{0x0u, 0x0u, 0x0u}},
		    {{0xffffffffu, 0xffffffffu, 0xffffffffu}},
		    {{0x80402010u, 0x40201080u, 0x10804020u}},
		    {{0xff000000u, 0xffu, 0xff0000u}},
		    {{0xffu, 0xff00u, 0xff000000u}},
		    {{0x80000000u, 0x80u, 0x800000u}},
		    {{0x7fffffffu, 0xffffff7fu, 0xff7fffffu}},
		    {{0x12345678u, 0x34567812u, 0x78123456u}},
		    {{0x5afcbb66u, 0xfcbb665au, 0x665afcbbu}},
		    {{0x64a593au, 0x4a593a06u, 0x3a064a59u}},
		    {{0x4b91ee57u, 0x91ee574bu, 0x574b91eeu}},
		    {{0x2a185161u, 0x1851612au, 0x612a1851u}},
		    {{0x5ded43fau, 0xed43fa5du, 0xfa5ded43u}},
		    {{0x4a5c1de5u, 0x5c1de54au, 0xe54a5c1du}},
		    {{0x97389ca8u, 0x389ca897u, 0xa897389cu}},
		    {{0x5d803519u, 0x8035195du, 0x195d8035u}},
		    {{0x818fc2b0u, 0x8fc2b081u, 0xb0818fc2u}},
		    {{0x78e44551u, 0xe4455178u, 0x5178e445u}},
		    {{0xb8c6b31du, 0xc6b31db8u, 0x1db8c6b3u}},
		    {{0xa13fa564u, 0x3fa564a1u, 0x64a13fa5u}},
		    {{0x3499a45au, 0x99a45a34u, 0x5a3499a4u}},
		    {{0x8bce373cu, 0xce373c8bu, 0x3c8bce37u}},
		    {{0xc5e89e82u, 0xe89e82c5u, 0x82c5e89eu}},
		    {{0xdd630adfu, 0x630adfddu, 0xdfdd630au}},
		    {{0x75d3a5a5u, 0xd3a5a575u, 0xa575d3a5u}},
		    {{0x35bc5fb9u, 0xbc5fb935u, 0xb935bc5fu}},
		    {{0xe2679b87u, 0x679b87e2u, 0x87e2679bu}},
		    {{0xdbbfa7feu, 0xbfa7fedbu, 0xfedbbfa7u}},
		    {{0x77d18efu, 0x7d18ef07u, 0xef077d18u}},
		    {{0x5aa1a6f1u, 0xa1a6f15au, 0xf15aa1a6u}},
		    {{0x8259b239u, 0x59b23982u, 0x398259b2u}},
		    {{0x3ec198fau, 0xc198fa3eu, 0xfa3ec198u}},
		    {{0xa9b3f55au, 0xb3f55aa9u, 0x5aa9b3f5u}},
		    {{0x948a5c91u, 0x8a5c9194u, 0x91948a5cu}},
		    {{0xbac0113fu, 0xc0113fbau, 0x3fbac011u}},
		    {{0xc63d454eu, 0x3d454ec6u, 0x4ec63d45u}},
		    {{0x4cbef0eeu, 0xbef0ee4cu, 0xee4cbef0u}},
		    {{0xef8d36efu, 0x8d36efefu, 0xefef8d36u}},
		    {{0xc036c66u, 0x36c660cu, 0x660c036cu}},
		    {{0x8febc915u, 0xebc9158fu, 0x158febc9u}},
		    {{0xb3ee8df7u, 0xee8df7b3u, 0xf7b3ee8du}},
		    {{0xa2126faeu, 0x126faea2u, 0xaea2126fu}},
		    {{0xc5630181u, 0x630181c5u, 0x81c56301u}},
		    {{0x3d984245u, 0x9842453du, 0x453d9842u}},
		    {{0x159567e1u, 0x9567e115u, 0xe1159567u}},
		    {{0xd196a554u, 0x96a554d1u, 0x54d196a5u}},
		    {{0xb592c6deu, 0x92c6deb5u, 0xdeb592c6u}},
		    {{0x970b5866u, 0xb586697u, 0x66970b58u}},
		    {{0x2961ee2au, 0x61ee2a29u, 0x2a2961eeu}},
		    {{0x5f2184c0u, 0x2184c05fu, 0xc05f2184u}},
		    {{0x71769d6fu, 0x769d6f71u, 0x6f71769du}},
		    {{0x66ce6604u, 0xce660466u, 0x466ce66u}},
		    {{0x68deec61u, 0xdeec6168u, 0x6168deecu}},
		    {{0x7ffaabafu, 0xfaabaf7fu, 0xaf7ffaabu}},
		    {{0xc1d01498u, 0xd01498c1u, 0x98c1d014u}},
		    {{0x9c8baf3du, 0x8baf3d9cu, 0x3d9c8bafu}},
		    {{0xeeb9736cu, 0xb9736ceeu, 0x6ceeb973u}},
		    {{0xb82190a0u, 0x2190a0b8u, 0xa0b82190u}},
		    {{0xb6c1343fu, 0xc1343fb6u, 0x3fb6c134u}},
		    {{0x4aeaeca0u, 0xeaeca04au, 0xa04aeaecu}},
		    {{0x5b22ab2au, 0x22ab2a5bu, 0x2a5b22abu}},
		    {{0xc4210b61u, 0x210b61c4u, 0x61c4210bu}},
		    {{0x80ce224cu, 0xce224c80u, 0x4c80ce22u}},
		    {{0xe2aac6dau, 0xaac6dae2u, 0xdae2aac6u}},
		}};
		for (const auto& row : kNative) {
			for (const auto direction : {0u, 1u}) {
				const auto callback = vm.GetSlot(vm.RootTable(), std::string(direction == 0 ? "ARGB2RGBA" : "RGBA2ARGB"));
				const auto input = std::bit_cast<std::int32_t>(row[0]);
				const auto result = vm.Invoke(callback, std::array<sgre::script::SquirrelValue, 3>{vm.RootTable(), input, std::int32_t{17}}).Value;
				Require(std::bit_cast<std::uint32_t>(std::get<std::int32_t>(result)) == row[direction + 1]);
			}
		}
		for (const auto name : {"ARGB2RGBA", "RGBA2ARGB"}) {
			const auto callback = vm.GetSlot(vm.RootTable(), std::string(name));
			const auto call = [&](const sgre::script::SquirrelValue& value) {
				return std::get<std::int32_t>(vm.Invoke(callback, std::array<sgre::script::SquirrelValue, 2>{vm.RootTable(), value}).Value);
			};
			Require(call({}) == 0 && call(std::string("4294967295")) == 0);
			Require(std::get<std::int32_t>(vm.Invoke(callback, std::array<sgre::script::SquirrelValue, 1>{vm.RootTable()}).Value) == 0);
			const auto expected_one = std::string(name) == "ARGB2RGBA" ? 256 : 0x01000000;
			Require(call(true) == expected_one && call(1.75f) == expected_one);
			const auto exceptional = std::string(name) == "ARGB2RGBA" ? 0x80 : 0x00800000;
			Require(call(std::numeric_limits<float>::quiet_NaN()) == exceptional);
		}
		vm.Close();
		std::cout << "PACKED_COLOR128_NATIVE_VECTORS_AND_TYPED_WRAPPER=PASS\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}

	return 0;
}
