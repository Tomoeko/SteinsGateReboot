#include "sgre/text/cff_glyph_atlas.hpp"
#include "sgre/text/cff_glyph_stroke_path.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

static std::uint32_t ReadWord(std::istream& input) {
	std::array<unsigned char, 4> bytes{};
	for (auto& value : bytes) {
		const auto next = input.get();
		if (next == std::char_traits<char>::eof()) {
			throw std::runtime_error("native positive-stroke input/result is truncated");
		}
		value = static_cast<unsigned char>(next);
	}

	return std::uint32_t(bytes[0]) | (std::uint32_t(bytes[1]) << 8) | (std::uint32_t(bytes[2]) << 16) | (std::uint32_t(bytes[3]) << 24);
}

static std::uint8_t ReadByte(std::istream& input) {
	const auto next = input.get();
	if (next == std::char_traits<char>::eof()) {
		throw std::runtime_error("native positive-stroke pixel bytes are truncated");
	}

	return static_cast<std::uint8_t>(next);
}

static void RequireEnd(std::istream& input) {
	if (input.get() != std::char_traits<char>::eof()) {
		throw std::runtime_error("native positive-stroke input/result has trailing bytes");
	}
}

static sgre::text::CffPreparedGlyph ReadGlyph(std::istream& input, std::uint32_t& edge) {
	const auto glyph_index = ReadWord(input);
	const auto size = ReadWord(input);
	edge = ReadWord(input);
	const auto points = ReadWord(input);
	const auto contours = ReadWord(input);
	const auto outline_flags = ReadWord(input);
	if (glyph_index > std::numeric_limits<std::uint16_t>::max() || size == 0 || size > std::numeric_limits<std::uint16_t>::max() || edge == 0 || edge > 1024 || points > 32767 || contours > points) {
		throw std::runtime_error("native positive-stroke outline exceeds bounded fixture domain");
	}

	sgre::text::CffPreparedGlyph glyph;
	glyph.GlyphIndex = static_cast<std::uint16_t>(glyph_index);
	glyph.PixelSize = static_cast<std::uint16_t>(size);
	glyph.Metrics.OutlineFlags = outline_flags;
	glyph.Outline.Points.resize(points);
	glyph.Outline.ContourEnds.resize(contours);
	for (auto& point : glyph.Outline.Points) {
		point.X26Dot6 = std::bit_cast<std::int32_t>(ReadWord(input));
		point.Y26Dot6 = std::bit_cast<std::int32_t>(ReadWord(input));
		const auto tag = ReadWord(input);
		if (tag != 1 && tag != 2) {
			throw std::runtime_error("native positive-stroke outline point tag is invalid");
		}

		point.Kind = static_cast<sgre::resource::CffPointKind>(tag);
	}
	for (auto& end : glyph.Outline.ContourEnds) {
		const auto index = ReadWord(input);
		if (index >= points) {
			throw std::runtime_error("native positive-stroke contour endpoint exceeds its outline");
		}

		end = static_cast<std::uint16_t>(index);
	}

	return glyph;
}

static std::array<std::uint32_t, 6> PlacementWords(const std::optional<sgre::text::CffAtlasGlyph>& glyph) {
	if (!glyph) {
		return {};
	}

	return {glyph->Allocation.LeftPixels, glyph->Allocation.TopPixels, std::bit_cast<std::uint32_t>(glyph->LeftPixel), std::bit_cast<std::uint32_t>(glyph->TopTranslationPixel), glyph->Allocation.WidthPixels, glyph->Allocation.HeightPixels};
}

int main(int argc, char** argv) {
	try {
		if (argc != 4) {
			throw std::runtime_error("expected prepared outline, projected native placement/pixels and native final atlas");
		}
		std::ifstream source(argv[1], std::ios::binary);
		std::ifstream expected(argv[2], std::ios::binary);
		std::ifstream final_atlas(argv[3], std::ios::binary);
		if (!source || !expected || !final_atlas || ReadWord(source) != 0x31534753 || ReadWord(expected) != 0x31524753) {
			throw std::runtime_error("positive-stroke source/result magic differs");
		}

		const auto cases = ReadWord(source);
		if (cases == 0 || cases > 32 || ReadWord(expected) != cases) {
			throw std::runtime_error("positive-stroke source/result case count differs");
		}

		sgre::text::GlyphAtlasPacking packing;
		packing.WidthPixels = 128;
		packing.HeightPixels = 256;
		std::vector<std::uint8_t> pixels(529 * 256 + 32, 0xa5);
		sgre::text::GlyphAtlasCpuSurface surface;
		surface.Pixels = pixels;
		surface.RowPitchBytes = 529;
		std::size_t compared_pixels = 0;
		for (std::uint32_t test = 0; test < cases; ++test) {
			std::uint32_t edge = 0;
			auto glyph = ReadGlyph(source, edge);
			const auto stroke_input = sgre::text::CffGlyphStrokeInput::Prepare(glyph, static_cast<std::int32_t>(edge));
			// The wrapper owns all geometry before the original prepared-outline boundary retires.
			glyph.Outline.Points.clear();
			glyph.Outline.ContourEnds.clear();
			std::optional<sgre::text::CffAtlasGlyph> region;
			if (stroke_input) {
				auto contours = sgre::text::PrepareCffGlyphStroke(stroke_input->Commands(), stroke_input->WidthPixels());
				const auto raster = sgre::text::PreparedCffGlyphRaster::PrepareStroke(*stroke_input, std::move(contours));
				region = sgre::text::RasterizePreparedCffGlyphInAtlas(raster, packing, surface);
			}

			const auto record = PlacementWords(region);
			for (std::size_t index = 0; index < record.size(); ++index) {
				if (ReadWord(expected) != record[index]) {
					std::cerr << "case=" << test << " placement_word=" << index << '\n';
					throw std::runtime_error("native positive-stroke atlas placement differs");
				}
			}
			const std::array<std::uint32_t, 5> state{packing.PenXPixels, packing.PenYPixels, packing.RowHeightPixels, static_cast<std::uint32_t>(surface.Locked), static_cast<std::uint32_t>(surface.UploadRequired)};
			for (const auto word : state) {
				if (ReadWord(expected) != word) {
					throw std::runtime_error("native positive-stroke atlas row/lock/upload state differs");
				}
			}

			const auto pixel_bytes = ReadWord(expected);
			if (pixel_bytes != std::size_t(record[4]) * record[5] * 4) {
				throw std::runtime_error("native positive-stroke crop extent differs from placement");
			}
			if (region) {
				for (std::uint32_t row = 0; row < record[5]; ++row) {
					for (std::uint32_t column = 0; column < record[4] * 4; ++column) {
						const auto offset = region->Allocation.DataOffsetBytes + std::size_t(row) * surface.RowPitchBytes + column;
						const auto native = ReadByte(expected);
						if (pixels.at(offset) != native) {
							std::cerr << "case=" << test << " row=" << row << " byte_column=" << column << " expected=" << unsigned(native) << " actual=" << unsigned(pixels[offset]) << '\n';
							throw std::runtime_error("native positive-stroke full contour/edge/area/copy pixel differs");
						}
					}
				}
			}

			compared_pixels += pixel_bytes / 4;
		}
		for (const auto pixel_byte : pixels) {
			if (ReadByte(final_atlas) != pixel_byte) {
				throw std::runtime_error("native positive-stroke final strided atlas or sentinel bytes differ");
			}
		}

		RequireEnd(source);
		RequireEnd(expected);
		RequireEnd(final_atlas);
		std::cout << "cases=" << cases << " native_pixel_words=" << compared_pixels << " final_atlas_bytes=" << pixels.size() << " differences=0\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
