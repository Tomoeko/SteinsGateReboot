#include "sgre/scene/emote_jelly_sample_fit.hpp"

#include <bit>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

static float Float(std::uint32_t word) {
	return std::bit_cast<float>(word);
}

static void Require(bool condition, const char* message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

static void CheckCase(std::uint32_t width, std::uint32_t height, std::vector<sgre::resource::MotionPoint> grid, const std::vector<sgre::scene::EmoteJellySampleBinding>& bindings, const std::vector<sgre::resource::MotionPoint>& targets, const std::vector<std::uint32_t>& expected) {
	sgre::scene::EmoteJellySampleFit fit(width, height, bindings);
	fit.Apply(grid, targets);
	Require(expected.size() == grid.size() * 2, "native Jelly sample vector count changed");
	for (std::size_t index = 0; index < grid.size(); ++index) {
		if (std::bit_cast<std::uint32_t>(grid[index].X) != expected[index * 2] || std::bit_cast<std::uint32_t>(grid[index].Y) != expected[index * 2 + 1]) {
			throw std::runtime_error("Jelly registered four-neighbor sequential residual diverged from original instructions");
		}
	}
}

static void NativeCases() {
	CheckCase(2, 5, {{Float(0xc0589d8a), Float(0x4108ba2f)}, {Float(0x40676276), Float(0x3e3a2e8c)}, {Float(0x40962762), Float(0x4091745d)}, {Float(0x3f589d8a), Float(0x40dd1746)}, {Float(0xc0276276), Float(0xbfa2e8ba)}, {Float(0x3f1d89d9), Float(0x4111745d)}, {Float(0x40e76276), Float(0xc0ee8ba3)}, {Float(0xbfc4ec4f), Float(0x40f45d17)}, {Float(0x3e6c4ec5), Float(0xc0b45d17)}, {Float(0xbff62762), Float(0x403a2e8c)}}, {}, {}, {0xc0589d8a, 0x4108ba2f, 0x40676276, 0x3e3a2e8c, 0x40962762, 0x4091745d, 0x3f589d8a, 0x40dd1746, 0xc0276276, 0xbfa2e8ba, 0x3f1d89d9, 0x4111745d, 0x40e76276, 0xc0ee8ba3, 0xbfc4ec4f, 0x40f45d17, 0x3e6c4ec5, 0xc0b45d17, 0xbff62762, 0x403a2e8c});
	CheckCase(3, 4, {{Float(0x405d89d9), Float(0x400ba2e9)}, {Float(0x3ec4ec4f), Float(0xc101745d)}, {Float(0x3e6c4ec5), Float(0xc02e8ba3)}, {Float(0xc0c00000), Float(0x40ce8ba3)}, {Float(0x40f62762), Float(0x401745d1)}, {Float(0x40227627), Float(0xbee8ba2f)}, {Float(0xc093b13b), Float(0x40745d17)}, {Float(0x40dd89d9), Float(0x3fe8ba2f)}, {Float(0xc0bb13b1), Float(0xc10d1746)}, {Float(0xc0b89d8a), Float(0x3fba2e8c)}, {Float(0xc0b13b14), Float(0x40d45d17)}, {Float(0xbf1d89d9), Float(0x40b45d17)}}, {{2, Float(0xbfe00000), {Float(0x00000000), Float(0x00000000)}}}, {{Float(0x3fdb6db7), Float(0x3e638e39)}}, {0x3fdb6db7, 0x3e638e38, 0x3ec4ec4f, 0xc101745d, 0x3e6c4ec5, 0xc02e8ba3, 0xc0c00000, 0x40ce8ba3, 0x40f62762, 0x401745d1, 0x40227627, 0xbee8ba2f, 0xc093b13b, 0x40745d17, 0x40dd89d9, 0x3fe8ba2f, 0xc0bb13b1, 0xc10d1746, 0xc0b89d8a, 0x3fba2e8c, 0xc0b13b14, 0x40d45d17, 0xbf1d89d9, 0x40b45d17});
	CheckCase(4, 3, {{Float(0x4044ec4f), Float(0xc06e8ba3)}, {Float(0x40676276), Float(0x409d1746)}, {Float(0xc084ec4f), Float(0x40c5d174)}, {Float(0x408ec4ec), Float(0x40fa2e8c)}, {Float(0x4084ec4f), Float(0xc0c00000)}, {Float(0x40227627), Float(0x407a2e8c)}, {Float(0xc0b89d8a), Float(0x3e8ba2e9)}, {Float(0xc0ac4ec5), Float(0x40bd1746)}, {Float(0x40589d8a), Float(0xc091745d)}, {Float(0xc0ec4ec5), Float(0xc088ba2f)}, {Float(0x40c00000), Float(0x3f800000)}, {Float(0xbfa76276), Float(0xc0ae8ba3)}}, {{3, Float(0x3f400000), {Float(0x00000000), Float(0x00000000)}}, {-2, Float(0x3e800000), {Float(0x3f3b22d1), Float(0x3f758106)}}, {0, Float(0x3fa00000), {Float(0x3f620c4a), Float(0x3f581062)}}, {2, Float(0xbf400000), {Float(0x3f82d0e5), Float(0x3f3df3b6)}}}, {{Float(0xc0db6db7), Float(0xc0555555)}, {Float(0xc0924925), Float(0xc0800000)}, {Float(0x40c49249), Float(0x3f800000)}, {Float(0x40c49249), Float(0xc098e38e)}}, {0xc0db6db6, 0xc0555555, 0x40676276, 0x409d1746, 0xc084ec4f, 0x40c5d174, 0x408ec4ec, 0x40fa2e8c, 0x4084ec4f, 0xc0c00000, 0x40227627, 0x407a2e8c, 0xc0a9df79, 0x3eb0cbaf, 0x3e22d340, 0x405e2618, 0x40589d8a, 0xc091745d, 0xc0ec4ec5, 0xc088ba2f, 0x400454bc, 0xbf936c74, 0x409d265c, 0xc0f3303f});
	CheckCase(5, 2, {{Float(0x4093b13b), Float(0x407a2e8c)}, {Float(0xc0676276), Float(0xc08ba2e9)}, {Float(0x40c4ec4f), Float(0xc0c8ba2f)}, {Float(0x4093b13b), Float(0xbf0ba2e9)}, {Float(0x40cec4ec), Float(0x410a2e8c)}, {Float(0xc0000000), Float(0xc068ba2f)}, {Float(0xc02c4ec5), Float(0xc0d1745d)}, {Float(0xbf589d8a), Float(0xc0ce8ba3)}, {Float(0xc013b13b), Float(0xc0b1745d)}, {Float(0xc0a00000), Float(0xc062e8ba)}}, {{3, Float(0x3fe00000), {Float(0x00000000), Float(0x00000000)}}, {2, Float(0x3e800000), {Float(0x3f826e98), Float(0x3f76c8b4)}}, {3, Float(0x00000000), {Float(0x3f50a3d7), Float(0x3d7df3b6)}}, {0, Float(0x3f800000), {Float(0x3c9374bc), Float(0x3f039581)}}, {-1, Float(0x3e800000), {Float(0x3f0c0831), Float(0x3f87ae14)}}, {1, Float(0x3fc00000), {Float(0x3f42d0e5), Float(0x3f0c8b44)}}, {3, Float(0xbf800000), {Float(0x3f818937), Float(0x3f5851ec)}}, {0, Float(0x3f400000), {Float(0x3f800000), Float(0x3f800000)}}, {1, Float(0x40400000), {Float(0x3da1cac1), Float(0x3f93126f)}}, {-2, Float(0x3f400000), {Float(0x3e49ba5e), Float(0x3f624dd3)}}, {2, Float(0x40400000), {Float(0x3f5e76c9), Float(0x3ef4bc6a)}}, {3, Float(0xbfa00000), {Float(0x3ea2d0e5), Float(0x3e24dd2f)}}, {3, Float(0xbfc00000), {Float(0x3dfdf3b6), Float(0x3e1ba5e3)}}, {2, Float(0x40200000), {Float(0x3f0e5604), Float(0x3f45e354)}}, {1, Float(0xbfa00000), {Float(0x00000000), Float(0x3f800000)}}}, {{Float(0x3f5b6db7), Float(0x3fe38e39)}, {Float(0xc0800000), Float(0x40838e39)}, {Float(0xbfedb6db), Float(0x408e38e4)}, {Float(0x4036db6e), Float(0x40238e39)}, {Float(0x3f800000), Float(0x4031c71c)}, {Float(0x40492492), Float(0xc0a71c72)}, {Float(0x40a92492), Float(0xc0a00000)}, {Float(0xc0d6db6e), Float(0x405c71c7)}, {Float(0xc02db6db), Float(0xc0a38e39)}, {Float(0x40092492), Float(0x3fc71c72)}, {Float(0xc0924925), Float(0xc0155555)}, {Float(0xc06db6db), Float(0xbde38e39)}, {Float(0xc0249249), Float(0xbfe38e39)}, {Float(0xc0849249), Float(0x3f471c72)}, {Float(0xc0400000), Float(0x3f800000)}}, {0x3f5b6db8, 0x3fe38e38, 0xc0b80dc6, 0xbfef5448, 0x4094fdec, 0xc09c45a1, 0xbeebcfb0, 0x3f4e13e4, 0x40a39d29, 0x40f349b8, 0xc074f2e8, 0xc0320dab, 0xbe3796a7, 0xc036a85c, 0xc03030e8, 0x4021bbd8, 0xc065efca, 0xc0b8e456, 0x40008376, 0xc0888a26});
	CheckCase(6, 5, {{Float(0x40627627), Float(0xc0e00000)}, {Float(0xc05d89d9), Float(0x3fe8ba2f)}, {Float(0x40a4ec4f), Float(0xc0400000)}, {Float(0xc084ec4f), Float(0x40945d17)}, {Float(0x3fbb13b1), Float(0x4068ba2f)}, {Float(0x40e76276), Float(0xc09d1746)}, {Float(0x40c00000), Float(0xbfe8ba2f)}, {Float(0xc0d62762), Float(0xc0a2e8ba)}, {Float(0x40e00000), Float(0x3fba2e8c)}, {Float(0xbe1d89d9), Float(0xbfae8ba3)}, {Float(0x40f62762), Float(0xc111745d)}, {Float(0x3f44ec4f), Float(0x40ce8ba3)}, {Float(0x407b13b1), Float(0xc10745d1)}, {Float(0x408c4ec5), Float(0x3fe8ba2f)}, {Float(0x40313b14), Float(0x401d1746)}, {Float(0x40a9d89e), Float(0x4051745d)}, {Float(0xc07b13b1), Float(0x41100000)}, {Float(0x3e6c4ec5), Float(0x40e2e8ba)}, {Float(0xc0f3b13b), Float(0x40dd1746)}, {Float(0x40c9d89e), Float(0x4101745d)}, {Float(0xc0c76276), Float(0xc0f45d17)}, {Float(0x408c4ec5), Float(0x3f800000)}, {Float(0x3f09d89e), Float(0xc06e8ba3)}, {Float(0xbfe27627), Float(0x410e8ba3)}, {Float(0xbfcec4ec), Float(0x40da2e8c)}, {Float(0xc0a4ec4f), Float(0x403a2e8c)}, {Float(0xbe9d89d9), Float(0xc0000000)}, {Float(0xbfe27627), Float(0xc0945d17)}, {Float(0xc0a76276), Float(0x4102e8ba)}, {Float(0xc0f13b14), Float(0xc0a5d174)}}, {{3, Float(0x3fc00000), {Float(0x00000000), Float(0x00000000)}}, {-1, Float(0x40100000), {Float(0x3c9ba5e3), Float(0x3f52f1aa)}}, {-1, Float(0x3fc00000), {Float(0x3da1cac1), Float(0x3f8e353f)}}, {-2, Float(0xbf800000), {Float(0x3f343958), Float(0x3ba3d70a)}}, {-2, Float(0xbf400000), {Float(0x3f12f1aa), Float(0x3d178d50)}}, {0, Float(0xbe800000), {Float(0x3f8a3d71), Float(0x3f1645a2)}}, {3, Float(0x3fa00000), {Float(0x3f922d0e), Float(0x3ea6e979)}}, {0, Float(0xbfe00000), {Float(0x3f800000), Float(0x3f800000)}}, {-2, Float(0xbe800000), {Float(0x3ed0e560), Float(0x3e1db22d)}}, {2, Float(0x3e800000), {Float(0x3e7ef9db), Float(0x3e322d0e)}}, {-1, Float(0x3fc00000), {Float(0x3f2bc6a8), Float(0x3f5c28f6)}}, {1, Float(0xbf800000), {Float(0x3eb645a2), Float(0x3f0353f8)}}, {-2, Float(0x3f000000), {Float(0x3d978d50), Float(0x3f758106)}}, {-1, Float(0xbfc00000), {Float(0x3c656042), Float(0x3f6353f8)}}, {-1, Float(0x40000000), {Float(0x00000000), Float(0x3f800000)}}, {2, Float(0xbe800000), {Float(0x3f076c8b), Float(0x3f8353f8)}}, {2, Float(0x40100000), {Float(0x3dc08312), Float(0x3f5d70a4)}}, {3, Float(0xc0000000), {Float(0x3e77ced9), Float(0x3f8e5604)}}, {-1, Float(0x3f800000), {Float(0x3e74bc6a), Float(0x3f722d0e)}}, {1, Float(0x40000000), {Float(0x3e9eb852), Float(0x3f8a1cac)}}, {3, Float(0xbfa00000), {Float(0x3f32f1aa), Float(0x3f6a3d71)}}, {2, Float(0xbfc00000), {Float(0x3f800000), Float(0x00000000)}}, {2, Float(0x3fc00000), {Float(0x3f39db23), Float(0x3eb95810)}}, {1, Float(0x40200000), {Float(0x3ee8f5c3), Float(0x3e581062)}}, {-2, Float(0xbf400000), {Float(0x3f4ed917), Float(0x3e7df3b6)}}, {2, Float(0x3f000000), {Float(0x3eb9db23), Float(0x3e5e353f)}}, {2, Float(0x40300000), {Float(0x3ed16873), Float(0x3f0a3d71)}}, {3, Float(0x40200000), {Float(0x3f5a5e35), Float(0x3edf3b64)}}, {-1, Float(0xbfe00000), {Float(0x00000000), Float(0x00000000)}}, {2, Float(0xbf000000), {Float(0x3ec39581), Float(0x3e178d50)}}, {-2, Float(0xc0000000), {Float(0x3f69ba5e), Float(0x3f5ced91)}}}, {{Float(0x40b24925), Float(0xbe638e39)}, {Float(0xc0124925), Float(0x3faaaaab)}, {Float(0xbf5b6db7), Float(0x4038e38e)}, {Float(0xc05b6db7), Float(0x3fd55555)}, {Float(0x40249249), Float(0x405c71c7)}, {Float(0x3e124925), Float(0x40555555)}, {Float(0x40924925), Float(0x40400000)}, {Float(0xc0db6db7), Float(0xc0800000)}, {Float(0xc0000000), Float(0xc091c71c)}, {Float(0x406db6db), Float(0x3fc71c72)}, {Float(0xbf924925), Float(0xbeaaaaab)}, {Float(0xc0092492), Float(0xc0871c72)}, {Float(0xc05b6db7), Float(0x402aaaab)}, {Float(0xc0092492), Float(0xc0071c72)}, {Float(0x40892492), Float(0x40838e39)}, {Float(0x3f36db6e), Float(0x4098e38e)}, {Float(0x40bb6db7), Float(0x40238e39)}, {Float(0xc0400000), Float(0x3f638e39)}, {Float(0x3e924925), Float(0xc0a00000)}, {Float(0xc0a00000), Float(0xc0471c72)}, {Float(0xc0d24925), Float(0xc0955555)}, {Float(0xc0124925), Float(0xbf638e39)}, {Float(0x40c49249), Float(0xc0aaaaab)}, {Float(0x40cdb6db), Float(0x3ee38e39)}, {Float(0x40800000), Float(0xc038e38e)}, {Float(0x3fdb6db7), Float(0x409c71c7)}, {Float(0xc0400000), Float(0x400e38e4)}, {Float(0x3f800000), Float(0x40400000)}, {Float(0x4076db6e), Float(0xc00e38e4)}, {Float(0xc0092492), Float(0x40838e39)}, {Float(0xbfedb6db), Float(0x40400000)}}, {0x40b24925, 0xbe638e40, 0xbff1b4d6, 0x40361833, 0x3fdb7aa7, 0xbfc3fc26, 0xbf620ea8, 0x4090eb72, 0x3e879e81, 0x4021bd44, 0xc0124924, 0xbf638e38, 0x40c00000, 0xbfe8ba2f, 0xc03d2d60, 0xc027950a, 0x4048b41c, 0x40b7de2d, 0x4018a53a, 0xc014ead0, 0x40c64d95, 0xc0cc07f9, 0x405ccb7e, 0x40934ae3, 0x407b13b1, 0xc10745d1, 0x4056bc78, 0x3f0828a2, 0xc009bd20, 0x3ffb45d0, 0x40bfd9c6, 0x4014ade5, 0xc00fc7e5, 0x40d6881a, 0x3fefc26b, 0x404a077a, 0xc098fbcc, 0x3fed8ad0, 0x4104ee7e, 0x40c81273, 0xc0d1c1a8, 0xc0ecacb6, 0x4035d5ae, 0xbe6415a0, 0xbeb25d18, 0xc0986b1c, 0xbf929273, 0x40f38e5c, 0x40bbfe6c, 0x405f6885, 0xc060b8a9, 0x3daa7f00, 0xbf6c93dd, 0x3e54c3cc, 0xc00f09cc, 0xbfa43be6, 0xc0df610d, 0x40be2622, 0xc0db6db7, 0xc0800000});
	CheckCase(2, 4, {{Float(0x40f13b14), Float(0xbf68ba2f)}, {Float(0xc0c00000), Float(0xc0345d17)}, {Float(0x40f13b14), Float(0x40ce8ba3)}, {Float(0x40d13b14), Float(0x410a2e8c)}, {Float(0x3fcec4ec), Float(0x3ee8ba2f)}, {Float(0x4009d89e), Float(0x41000000)}, {Float(0xc0913b14), Float(0xc0fa2e8c)}, {Float(0x40f3b13b), Float(0x3f51745d)}}, {{1, Float(0x3fc00000), {Float(0x00000000), Float(0x00000000)}}, {2, Float(0xbfc00000), {Float(0x3ede353f), Float(0x3f2353f8)}}, {1, Float(0x3fa00000), {Float(0x3f378d50), Float(0x3f6c49ba)}}, {2, Float(0xc0000000), {Float(0x3e3c6a7f), Float(0x3ed70a3d)}}, {0, Float(0x3f400000), {Float(0x3f400000), Float(0x3f4b851f)}}, {1, Float(0xbf000000), {Float(0x3dd4fdf4), Float(0x3e5d2f1b)}}, {-2, Float(0x3f400000), {Float(0x3f39999a), Float(0x3f5645a2)}}, {0, Float(0x40200000), {Float(0x3f800000), Float(0x3f800000)}}, {-2, Float(0x3f800000), {Float(0x3f1d70a4), Float(0x3d6978d5)}}, {1, Float(0x00000000), {Float(0x3ef33333), Float(0x3f6b4396)}}, {0, Float(0xbf800000), {Float(0x3f449ba6), Float(0x3edeb852)}}, {2, Float(0xbf800000), {Float(0x3f608312), Float(0x3f722d0e)}}, {-1, Float(0x00000000), {Float(0x3f441893), Float(0x3f83126f)}}, {2, Float(0x3fc00000), {Float(0x3eb33333), Float(0x3f3e76c9)}}, {3, Float(0x40000000), {Float(0x00000000), Float(0x3f800000)}}, {-2, Float(0xbfe00000), {Float(0x3f641893), Float(0x3ee1cac1)}}, {-2, Float(0x3f400000), {Float(0x3cd4fdf4), Float(0x3f428f5c)}}, {2, Float(0xbf800000), {Float(0x3f8872b0), Float(0x3e051eb8)}}, {3, Float(0xbfc00000), {Float(0x3e4ccccd), Float(0x3f2a3d71)}}, {0, Float(0x40200000), {Float(0x3e4bc6a8), Float(0x3f3e353f)}}, {-1, Float(0xbf000000), {Float(0x3f8c28f6), Float(0x3e21cac1)}}, {-1, Float(0xc0000000), {Float(0x3f800000), Float(0x00000000)}}, {1, Float(0xbfa00000), {Float(0x3f47ef9e), Float(0x3e25e354)}}, {2, Float(0xbfe00000), {Float(0x3ee7ef9e), Float(0x3f358106)}}, {-1, Float(0xbe800000), {Float(0x3f11a9fc), Float(0x3f3ef9db)}}, {-1, Float(0x3fe00000), {Float(0x3f4ed917), Float(0x3eb95810)}}, {0, Float(0xbfe00000), {Float(0x3e7df3b6), Float(0x3f25a1cb)}}, {3, Float(0xbfc00000), {Float(0x3e353f7d), Float(0x3e656042)}}, {0, Float(0x3fc00000), {Float(0x00000000), Float(0x00000000)}}, {-2, Float(0x3f000000), {Float(0x3ead9168), Float(0x3eac8b44)}}, {2, Float(0xbfa00000), {Float(0x3e1374bc), Float(0x3e8f5c29)}}, {1, Float(0xbf400000), {Float(0x3f06e979), Float(0x3f2872b0)}}}, {{Float(0xbf5b6db7), Float(0xc0ae38e4)}, {Float(0xc036db6e), Float(0xc06aaaab)}, {Float(0x401b6db7), Float(0xbe638e39)}, {Float(0x3f800000), Float(0x40071c72)}, {Float(0x3fb6db6e), Float(0x4031c71c)}, {Float(0x3f924925), Float(0x3faaaaab)}, {Float(0xc0924925), Float(0xc0955555)}, {Float(0x405b6db7), Float(0xc06aaaab)}, {Float(0xc0849249), Float(0xc08aaaab)}, {Float(0xc0a00000), Float(0x400e38e4)}, {Float(0x40092492), Float(0x3ee38e39)}, {Float(0x40d24925), Float(0xbff1c71c)}, {Float(0x40649249), Float(0xc0800000)}, {Float(0x40092492), Float(0x3f9c71c7)}, {Float(0x406db6db), Float(0xc0838e39)}, {Float(0x3fc92492), Float(0x405c71c7)}, {Float(0x40a00000), Float(0xc0638e39)}, {Float(0x40849249), Float(0x3f471c72)}, {Float(0xc0849249), Float(0x4091c71c)}, {Float(0x3e124925), Float(0xbfc71c72)}, {Float(0xc0d6db6e), Float(0x3de38e39)}, {Float(0xc0e49249), Float(0xc0ae38e4)}, {Float(0x40b24925), Float(0xc00e38e4)}, {Float(0xc036db6e), Float(0x3f800000)}, {Float(0xc076db6e), Float(0xbf800000)}, {Float(0xc08db6db), Float(0x409c71c7)}, {Float(0x40db6db7), Float(0xc08e38e4)}, {Float(0xc0924925), Float(0xc0400000)}, {Float(0x409b6db7), Float(0x40955555)}, {Float(0x40a92492), Float(0xc0555555)}, {Float(0x3e924925), Float(0x40871c72)}, {Float(0xc0cdb6db), Float(0xbfd55555)}}, {0xc0282014, 0xc0bf983e, 0xbff87840, 0xc078590d, 0xbfa99c9c, 0x3fc78818, 0x404c0689, 0x407aaa0f, 0xbfd8b600, 0x4003485b, 0xc0c2cc7b, 0x409a22d5, 0x406db6dc, 0xc0838e39, 0x4101404b, 0xbf8a37e1});
}

static std::uint32_t ReadWord(std::istream& input) {
	std::uint32_t result = 0;
	for (std::uint32_t shift = 0; shift < 32; shift += 8) {
		const auto byte = input.get();
		Require(byte != std::char_traits<char>::eof(), "truncated original Jelly sample vector");
		result |= static_cast<std::uint32_t>(static_cast<unsigned char>(byte)) << shift;
	}

	return result;
}

static void FullNativeCases(const char* path) {
	std::ifstream input(path, std::ios::binary);
	Require(input.good() && ReadWord(input) == 0x31464a45, "invalid EJF1 original Jelly sample vectors");
	const auto count = ReadWord(input);
	Require(count == 64, "original Jelly sample witness requires all64 cases");
	for (std::uint32_t index = 0; index < count; ++index) {
		const auto width = ReadWord(input);
		const auto height = ReadWord(input);
		const auto samples = ReadWord(input);
		Require(width >= 2 && width <= 6 && height >= 2 && height <= 5 && samples <= 32, "invalid bounded original Jelly sample case");
		std::vector<sgre::resource::MotionPoint> grid(static_cast<std::size_t>(width) * height);
		for (auto& point : grid) {
			point.X = Float(ReadWord(input));
			point.Y = Float(ReadWord(input));
		}
		std::vector<sgre::scene::EmoteJellySampleBinding> bindings(samples);
		std::vector<sgre::resource::MotionPoint> targets(samples);
		for (std::size_t sample = 0; sample < samples; ++sample) {
			bindings[sample].Priority = std::bit_cast<std::int32_t>(ReadWord(input));
			bindings[sample].Order = Float(ReadWord(input));
			bindings[sample].Coordinate.X = Float(ReadWord(input));
			bindings[sample].Coordinate.Y = Float(ReadWord(input));
			targets[sample].X = Float(ReadWord(input));
			targets[sample].Y = Float(ReadWord(input));
		}
		std::vector<std::uint32_t> expected(grid.size() * 2);
		for (auto& word : expected) {
			word = ReadWord(input);
		}
		CheckCase(width, height, std::move(grid), bindings, targets, expected);
	}
	Require(input.get() == std::char_traits<char>::eof(), "trailing original Jelly sample vector bytes");
}

static void TestIdentityAndFailures() {
	std::vector<sgre::resource::MotionPoint> grid(12);
	sgre::scene::InitializeEmoteJellySampleGrid(grid, 4, 3);
	Require(grid.front().X == 0 && grid.front().Y == 0 && grid.back().X == 1 && grid.back().Y == 1 && grid[5].X == 1.0f / 3.0f && grid[5].Y == 0.5f, "Jelly fitting starts from the real uniform sample grid");
	const std::vector<sgre::scene::EmoteJellySampleBinding> bindings{{0, 0, {0.25f, 0.5f}}};
	const sgre::scene::EmoteJellySampleFit fit(4, 3, bindings);
	bool rejected = false;
	try {
		fit.Apply(grid, {});
	} catch (const std::runtime_error&) {
		rejected = true;
	}
	Require(rejected, "Jelly fitting must reject missing actual surface targets");
	rejected = false;
	try {
		const sgre::scene::EmoteJellySampleFit invalid(4, 3, std::vector<sgre::scene::EmoteJellySampleBinding>{{0, 0, {-0.25f, 0.5f}}});
	} catch (const std::runtime_error&) {
		rejected = true;
	}
	Require(rejected, "Jelly fitting must reject native negative-index registration");
	rejected = false;
	try {
		const sgre::scene::EmoteJellySampleFit invalid(4, 3, std::vector<sgre::scene::EmoteJellySampleBinding>{{0, std::numeric_limits<float>::quiet_NaN(), {0.5f, 0.5f}}});
	} catch (const std::runtime_error&) {
		rejected = true;
	}
	Require(rejected, "Jelly fitting must reject NaN native float order before sorting");
	const auto before = grid;
	rejected = false;
	try {
		fit.Apply(grid, std::vector<sgre::resource::MotionPoint>{{std::numeric_limits<float>::infinity(), 0}});
	} catch (const std::runtime_error&) {
		rejected = true;
	}
	Require(rejected && grid[5].X == before[5].X && grid[5].Y == before[5].Y, "nonfinite targets reject before mutating the actual grid");
}

int main(int argc, char** argv) {
	try {
		Require(argc <= 2, "usage: emote_jelly_sample_fit_tests [native-vectors.bin]");
		NativeCases();
		TestIdentityAndFailures();
		if (argc == 2) {
			FullNativeCases(argv[1]);
		}
		std::cout << "Jelly original four-neighbor/stable residual scatter PASS" << std::endl;
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << std::endl;
		return 1;
	}
}
