#include "sgre/text/layout_lookup_accelerator.hpp"

#include <cstddef>
#include <optional>
#include <span>
#include <stdexcept>

namespace sgre::text {

	static constexpr std::uint32_t kMaximumAcceleratorWork = 1048576;

	struct AcceleratorReader {
		std::span<const std::uint8_t> Bytes;
		std::uint32_t Work = 0;

		void CountWork() {
			if (++Work > kMaximumAcceleratorWork) {
				throw std::runtime_error("layout lookup accelerator exceeds its supported work limit");
			}
		}

		std::uint32_t Word(std::size_t field, std::size_t width = 2) const {
			if (field > Bytes.size() || width > Bytes.size() - field) {
				throw std::runtime_error("admitted layout lookup accelerator field exceeds table bounds");
			}
			std::uint32_t value = 0;
			for (std::size_t index = 0; index < width; ++index) {
				value = (value << 8) | Bytes[field + index];
			}

			return value;
		}

		std::optional<std::size_t> Offset(std::size_t field, std::size_t parent, std::size_t width = 2) const {
			const auto relative = Word(field, width);
			if (relative == 0) {
				return std::nullopt;
			}
			if (parent > Bytes.size() || relative > Bytes.size() - parent) {
				throw std::runtime_error("admitted layout lookup accelerator offset exceeds table bounds");
			}

			return parent + relative;
		}
	};

	static std::optional<std::size_t> ChainInputCoverage(const AcceleratorReader& reader, std::size_t table) {
		const auto format = reader.Word(table);
		if (format == 1 || format == 2) {
			return reader.Offset(table + 2, table);
		}
		if (format != 3) {
			return std::nullopt;
		}
		const auto input = table + 4 + std::size_t(reader.Word(table + 2)) * 2;
		if (reader.Word(input) == 0) {
			return std::nullopt;
		}

		return reader.Offset(input + 2, table);
	}

	static std::optional<std::size_t> ContextInputCoverage(const AcceleratorReader& reader, std::size_t table) {
		const auto format = reader.Word(table);
		if (format == 1 || format == 2) {
			return reader.Offset(table + 2, table);
		}
		if (format == 3) {
			return reader.Offset(table + 6, table);
		}

		return std::nullopt;
	}

	static std::optional<std::size_t> LookupInputCoverage(AcceleratorReader& reader, std::size_t table, std::uint32_t type, bool positioning) {
		while (true) {
			reader.CountWork();
			const auto format = reader.Word(table);
			if ((positioning && (type == 1 || type == 2)) || (!positioning && type == 1)) {
				return format == 1 || format == 2 ? reader.Offset(table + 2, table) : std::nullopt;
			}
			if ((positioning && type >= 3 && type <= 6) || (!positioning && ((type >= 2 && type <= 4) || type == 8))) {
				return format == 1 ? reader.Offset(table + 2, table) : std::nullopt;
			}
			if (type == (positioning ? 7U : 5U)) {
				return ContextInputCoverage(reader, table);
			}
			if (type == (positioning ? 8U : 6U)) {
				return ChainInputCoverage(reader, table);
			}
			if (type != (positioning ? 9U : 7U) || format != 1) {
				return std::nullopt;
			}
			const auto next = reader.Offset(table + 4, table, 4);
			type = reader.Word(table + 2);
			if (!next) {
				return std::nullopt;
			}
			table = *next;
		}
	}

	static std::uint32_t CoverageRangeBits(std::uint32_t first, std::uint32_t last, std::uint32_t shift) {
		first >>= shift;
		last >>= shift;
		const auto distance = last - first;
		if (distance >= 31) {
			return 0xffffffff;
		}
		std::uint32_t bits = 0;
		for (std::uint32_t index = 0; index <= distance; ++index) {
			bits |= 1U << ((first + index) & 31U);
		}

		return bits;
	}

	static void AddLookupCoverage(AcceleratorReader& reader, std::size_t coverage, LayoutLookupAccelerator& accelerator) {
		const auto format = reader.Word(coverage);
		if (format != 1 && format != 2) {
			return;
		}
		const auto count = reader.Word(coverage + 2);
		for (std::uint32_t index = 0; index < count; ++index) {
			reader.CountWork();
			const auto record = coverage + 4 + std::size_t(index) * (format == 1 ? 2 : 6);
			const auto first = reader.Word(record);
			if (format == 1) {
				accelerator.GlyphsShift4 |= 1U << ((first >> 4) & 31U);
				accelerator.GlyphsShift0 |= 1U << (first & 31U);
				accelerator.GlyphsShift9 |= 1U << ((first >> 9) & 31U);
			} else {
				const auto last = reader.Word(record + 2);
				accelerator.GlyphsShift4 |= CoverageRangeBits(first, last, 4);
				accelerator.GlyphsShift0 |= CoverageRangeBits(first, last, 0);
				accelerator.GlyphsShift9 |= CoverageRangeBits(first, last, 9);
			}
		}
	}

	static void InitializeLayoutLookupAccelerators(std::span<const std::uint8_t> bytes, bool positioning, std::span<LayoutLookupAccelerator> accelerators) {
		AcceleratorReader reader;
		reader.Bytes = bytes;
		const auto list = bytes.empty() ? std::nullopt : reader.Offset(8, 0);
		const auto count = list ? reader.Word(*list) : 0;
		if (accelerators.size() != count) {
			throw std::runtime_error("layout lookup accelerator span has an incorrect slot count");
		}
		for (auto& accelerator : accelerators) {
			accelerator = {};
		}
		if (!list) {
			return;
		}
		for (std::uint32_t index = 0; index < count; ++index) {
			reader.CountWork();
			const auto lookup = reader.Offset(*list + 2 + std::size_t(index) * 2, *list);
			if (!lookup) {
				continue;
			}
			const auto type = reader.Word(*lookup);
			const auto subtable_count = reader.Word(*lookup + 4);
			for (std::uint32_t subtable_index = 0; subtable_index < subtable_count; ++subtable_index) {
				const auto subtable = reader.Offset(*lookup + 6 + std::size_t(subtable_index) * 2, *lookup);
				if (!subtable) {
					continue;
				}
				const auto coverage = LookupInputCoverage(reader, *subtable, type, positioning);
				if (coverage) {
					AddLookupCoverage(reader, *coverage, accelerators[index]);
				}
			}
		}
	}

	static std::uint32_t LayoutLookupCount(std::span<const std::uint8_t> bytes) {
		if (bytes.empty()) {
			return 0;
		}
		AcceleratorReader reader;
		reader.Bytes = bytes;
		const auto list = reader.Offset(8, 0);

		return list ? reader.Word(*list) : 0;
	}

	std::uint32_t SubstitutionLookupCount(const GlyphSubstitutionBlob& blob) {
		return LayoutLookupCount(blob.Bytes());
	}

	std::uint32_t PositioningLookupCount(const GlyphPositioningBlob& blob) {
		return LayoutLookupCount(blob.Bytes());
	}

	void InitializeSubstitutionLookupAccelerators(const GlyphSubstitutionBlob& blob, std::span<LayoutLookupAccelerator> accelerators) {
		InitializeLayoutLookupAccelerators(blob.Bytes(), false, accelerators);
	}

	void InitializePositioningLookupAccelerators(const GlyphPositioningBlob& blob, std::span<LayoutLookupAccelerator> accelerators) {
		InitializeLayoutLookupAccelerators(blob.Bytes(), true, accelerators);
	}

	std::vector<LayoutLookupAccelerator> BuildSubstitutionLookupAccelerators(const GlyphSubstitutionBlob& blob) {
		std::vector<LayoutLookupAccelerator> accelerators(SubstitutionLookupCount(blob));
		InitializeSubstitutionLookupAccelerators(blob, accelerators);

		return accelerators;
	}

	std::vector<LayoutLookupAccelerator> BuildPositioningLookupAccelerators(const GlyphPositioningBlob& blob) {
		std::vector<LayoutLookupAccelerator> accelerators(PositioningLookupCount(blob));
		InitializePositioningLookupAccelerators(blob, accelerators);

		return accelerators;
	}

} // namespace sgre::text
