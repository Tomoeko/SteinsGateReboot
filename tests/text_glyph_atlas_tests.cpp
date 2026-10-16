#include "sgre/text/glyph_atlas.hpp"

#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace sgre::text::test {

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

	static void CheckPackingAndClearing() {
		GlyphAtlasPacking packing;
		Require(packing.WidthPixels == 1024 && packing.HeightPixels == 1024, "native text atlas defaults to a square 1024-pixel extent");
		packing.WidthPixels = 12;
		packing.HeightPixels = 12;
		std::vector<std::uint8_t> pixels(56 * 12 + 32, 0x91);
		GlyphAtlasCpuSurface surface;
		surface.Pixels = pixels;
		surface.RowPitchBytes = 56;
		const auto first = AllocateGlyphAtlasRegion(packing, surface, 3, 2);
		Require(first && first->LeftPixels == 1 && first->TopPixels == 1 && first->WidthPixels == 3 && first->HeightPixels == 2 && first->DataOffsetBytes == 60 && first->RowPitchBytes == 56, "allocation returns its bordered content origin and caller row pitch");
		Require(packing.PenXPixels == 5 && packing.PenYPixels == 0 && packing.RowHeightPixels == 4 && surface.Locked && surface.UploadRequired, "successful mapping advances the row and locks/marks the CPU surface");
		for (std::size_t index = 0; index < pixels.size(); ++index) {
			const bool cleared = index / 56 < 4 && index % 56 < 20;
			Require(pixels[index] == (cleared ? 0 : 0x91), "clearing covers the padded BGRA rectangle and preserves pitch/trailing bytes");
		}

		UnlockGlyphAtlasSurface(surface);
		Require(!surface.Locked && surface.UploadRequired, "unlock retains the pending upload requirement");
		const auto second = AllocateGlyphAtlasRegion(packing, surface, 3, 2);
		Require(second && second->LeftPixels == 6 && second->TopPixels == 1 && packing.PenXPixels == 10, "a second glyph fits on the current row");
		UnlockGlyphAtlasSurface(surface);
		const auto wrapped = AllocateGlyphAtlasRegion(packing, surface, 1, 1);
		Require(wrapped && wrapped->LeftPixels == 1 && wrapped->TopPixels == 5 && packing.PenXPixels == 3 && packing.PenYPixels == 4 && packing.RowHeightPixels == 3, "row overflow starts below the previous maximum padded height");
	}

	static void CheckEqualityAndFailedWraps() {
		GlyphAtlasPacking packing;
		packing.WidthPixels = 16;
		packing.HeightPixels = 12;
		packing.PenXPixels = 12;
		packing.PenYPixels = 8;
		packing.RowHeightPixels = 4;
		std::vector<std::uint8_t> pixels(64 * 12, 0x55);
		GlyphAtlasCpuSurface surface;
		surface.Pixels = pixels;
		surface.RowPitchBytes = 64;
		const auto failed = AllocateGlyphAtlasRegion(packing, surface, 2, 1);
		Require(!failed && packing.PenXPixels == 0 && packing.PenYPixels == 12 && packing.RowHeightPixels == 0, "horizontal equality wraps before a later height failure");
		Require(!surface.Locked && !surface.UploadRequired && pixels.front() == 0x55 && pixels.back() == 0x55, "height failure leaves CPU flags and pixels unchanged");

		packing.PenXPixels = 15;
		packing.PenYPixels = 1;
		packing.RowHeightPixels = 2;
		surface.Locked = true;
		Require(!AllocateGlyphAtlasRegion(packing, surface, 1, 1) && packing.PenXPixels == 0 && packing.PenYPixels == 3 && packing.RowHeightPixels == 0, "mapping-lock failure retains the preceding row wrap");

		packing.PenXPixels = 0;
		packing.PenYPixels = 0;
		packing.RowHeightPixels = 0;
		surface.Locked = false;
		surface.Enabled = false;
		Require(!AllocateGlyphAtlasRegion(packing, surface, 1, 1) && packing.PenXPixels == 0, "disabled mapping cannot reserve a glyph");
		surface.Enabled = true;
		Require(!AllocateGlyphAtlasRegion(packing, surface, 1, 10), "vertical equality is unavailable because the native check is strict");
		const auto full_width = AllocateGlyphAtlasRegion(packing, surface, 14, 1);
		Require(full_width && full_width->LeftPixels == 1 && packing.PenXPixels == 16, "a padded glyph equal to the atlas width fits after wrapping");
	}

	static void CheckStorageAndInputBounds() {
		GlyphAtlasPacking packing;
		packing.WidthPixels = 16;
		packing.HeightPixels = 12;
		std::vector<std::uint8_t> pixels(64 * 12, 0x55);
		GlyphAtlasCpuSurface surface;
		surface.Pixels = pixels;
		surface.RowPitchBytes = 63;
		RequireError([&] {
			(void)AllocateGlyphAtlasRegion(packing, surface, 1, 1);
		},
		    "short row pitch must fail");
		surface.RowPitchBytes = 64;
		surface.Pixels = std::span<std::uint8_t>(pixels).first(pixels.size() - 1);
		RequireError([&] {
			(void)AllocateGlyphAtlasRegion(packing, surface, 1, 1);
		},
		    "short pixel storage must fail");
		surface.Pixels = pixels;
		RequireError([&] {
			(void)AllocateGlyphAtlasRegion(packing, surface, 15, 1);
		},
		    "an oversized padded width must fail before writing beyond the row");
		RequireError([&] {
			(void)AllocateGlyphAtlasRegion(packing, surface, 0, 1);
		},
		    "empty glyph dimensions must fail");
		RequireError([&] {
			(void)AllocateGlyphAtlasRegion(packing, surface, std::numeric_limits<std::uint32_t>::max(), 1);
		},
		    "glyph dimension overflow must fail");
		packing.PenYPixels = 11;
		packing.RowHeightPixels = 2;
		RequireError([&] {
			(void)AllocateGlyphAtlasRegion(packing, surface, 1, 1);
		},
		    "out-of-bounds row height must fail");
		packing.PenYPixels = 0;
		packing.RowHeightPixels = 0;
		packing.WidthPixels = 65535;
		RequireError([&] {
			(void)AllocateGlyphAtlasRegion(packing, surface, 1, 1);
		},
		    "unsupported native image extent must fail");
	}

	static void CheckRowGateBeforeMapping() {
		GlyphAtlasPacking packing;
		packing.WidthPixels = 64;
		packing.HeightPixels = 64;
		packing.PenXPixels = 60;
		packing.RowHeightPixels = 16;
		Require(!PrepareGlyphAtlasRow(packing, 4, 46) && packing.PenXPixels == 0 && packing.PenYPixels == 16 && packing.RowHeightPixels == 0, "native strict height gate must preserve row wrap before any CPU mapping");
		Require(PrepareGlyphAtlasRow(packing, 4, 45) && packing.PenXPixels == 0 && packing.PenYPixels == 16 && packing.RowHeightPixels == 0, "a successful row plan must not advance allocation state before mapping succeeds");
	}

} // namespace sgre::text::test

int main() {
	try {
		sgre::text::test::CheckPackingAndClearing();
		sgre::text::test::CheckEqualityAndFailedWraps();
		sgre::text::test::CheckStorageAndInputBounds();
		sgre::text::test::CheckRowGateBeforeMapping();
		std::cout << "glyph atlas tests passed\n";
		return 0;
	} catch (const std::exception& exception) {
		std::cerr << exception.what() << '\n';
		return 1;
	}
}
