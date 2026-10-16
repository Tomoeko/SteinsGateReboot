#include "sgre/text/cff_charstring_mask.hpp"

#include <array>
#include <cstdint>
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

template <typename Operation>
static void RequireError(Operation operation, std::string_view message) {
	try {
		operation();
	} catch (const std::runtime_error&) {
		return;
	}

	throw std::runtime_error(std::string(message));
}

static void CheckExactMaskBytes() {
	sgre::text::CffHintMask mask;
	mask.Bytes.fill(0x3c);
	const std::array<std::uint8_t, 4> program{0x81, 0x7f, 0xff, 0};
	Require(sgre::text::UpdateCffCharstringHintMask(mask, program, 4, 5) == 2 && mask.HintCount == 9 && mask.Valid && mask.New, "mask bytes use the combined axis declaration count");
	Require(mask.Bytes[0] == 0x81 && mask.Bytes[1] == 0x7f && mask.Bytes[2] == 0x3c, "mask copying preserves unused low bits and trailing storage bytes");
	mask.New = false;
	Require(sgre::text::UpdateCffCharstringHintMask(mask, {}, 0, 0) == 0 && mask.Valid && mask.New && mask.HintCount == 0 && mask.Bytes[0] == 0x81, "an empty mask still marks valid/new and retains storage bytes");
	const std::array<std::uint8_t, 12> full{0xff, 0xfe, 0xfd, 0xfc, 0xfb, 0xfa, 0xf9, 0xf8, 0xf7, 0xf6, 0xf5, 0xf4};
	Require(sgre::text::UpdateCffCharstringHintMask(mask, full, 48, 48) == 12 && mask.Bytes == full, "a 96-hint mask consumes the complete bounded storage");
}

static void CheckFailurePreservesOwner() {
	sgre::text::CffHintMask mask;
	mask.Bytes.fill(0x19);
	mask.HintCount = 3;
	mask.Valid = true;
	mask.New = false;
	const std::array<std::uint8_t, 1> short_program{0xff};
	RequireError([&] {
		(void)sgre::text::UpdateCffCharstringHintMask(mask, short_program, 8, 1);
	},
	    "a truncated mask must fail before changing glyph state");
	Require(mask.HintCount == 3 && mask.Valid && !mask.New && mask.Bytes[0] == 0x19, "mask truncation changed its previous owner state");
	RequireError([&] {
		(void)sgre::text::UpdateCffCharstringHintMask(mask, {}, 96, 1);
	},
	    "a combined declaration count above 96 must fail");
	RequireError([&] {
		(void)sgre::text::UpdateCffCharstringHintMask(mask, {}, std::numeric_limits<std::size_t>::max(), 1);
	},
	    "large declaration counts must fail before addition can overflow");
}

static void CheckCounterMapCacheOwnership() {
	sgre::text::CffHintMap initial_map;
	initial_map.BaseScale16Dot16 = 1024;
	sgre::text::CffCounterMaskConfiguration configuration;
	configuration.BaseScale16Dot16 = 1024;
	sgre::text::CffBlueZoneInput blue_input;
	blue_input.VerticalScale16Dot16 = configuration.BaseScale16Dot16;
	const auto blue_zones = sgre::text::BuildCffBlueZones(blue_input);
	std::array<sgre::text::CffStemHint, 3> stems{};
	for (std::size_t index = 0; index < stems.size(); ++index) {
		stems[index].Start16Dot16 = static_cast<std::int32_t>(index) * 100 * 65536;
		stems[index].End16Dot16 = stems[index].Start16Dot16 + 10 * 65536;
	}
	const std::array<std::uint8_t, 2> program{0xa0, 0xff};
	const auto result = sgre::text::PrepareCffCounterHintMap(initial_map, blue_zones, stems, 0, program, configuration);
	Require(result.ConsumedByteCount == 1 && result.Mask.Valid && !result.Mask.New && result.Mask.Bytes[0] == 0xa0 && result.Mask.Bytes[1] == 0 && result.Map.Initialized && initial_map.Initialized, "counter preparation must consume a temporary mask and initialize its separate parent/current maps");
	Require(stems[0].HasCachedDevicePositions && !stems[1].HasCachedDevicePositions && stems[2].HasCachedDevicePositions, "counter preparation must retain selected stem caches for later primary maps");
	RequireError([&] {
		sgre::text::CffCounterMaskConfiguration incompatible;
		(void)sgre::text::PrepareCffCounterHintMap(initial_map, blue_zones, stems, 0, program, incompatible);
	},
	    "a counter mask must reject a different glyph scale owner");
}

int main() {
	try {
		CheckExactMaskBytes();
		CheckFailurePreservesOwner();
		CheckCounterMapCacheOwnership();
		std::cout << "CFF charstring mask tests passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
