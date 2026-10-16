#include "sgre/audio/wma_block.hpp"

#include <bit>
#include <stdexcept>

namespace sgre::audio {

	static void ValidateBlockParameters(const WmaStandardParameters& parameters) {
		if (parameters.FrameCoefficientCount < 128 || parameters.FrameCoefficientCount > 2048 || !std::has_single_bit(parameters.FrameCoefficientCount) || parameters.MinimumBlockCoefficients < 128 || parameters.MinimumBlockCoefficients > parameters.FrameCoefficientCount || !std::has_single_bit(parameters.MinimumBlockCoefficients)) {
			throw std::runtime_error("WMA block parameters have invalid coefficient extents");
		}

		if (parameters.HasVariableBlocks && (parameters.BlockLengthIndexBits == 0 || parameters.BlockLengthIndexBits > 3 || parameters.MaximumBlockCount != parameters.FrameCoefficientCount / parameters.MinimumBlockCoefficients || parameters.BlockLengthIndexBits != std::bit_width(static_cast<std::uint32_t>(std::bit_width(parameters.MaximumBlockCount) - 1)))) {
			throw std::runtime_error("WMA variable block index width or count differs");
		}
	}

	static std::uint32_t ReadBlockSize(WmaBitReader& reader, const WmaStandardParameters& parameters) {
		if (!parameters.HasVariableBlocks) {
			return parameters.FrameCoefficientCount;
		}

		const auto index = reader.ReadBits(parameters.BlockLengthIndexBits);
		const auto coefficients = parameters.FrameCoefficientCount >> index;
		if (coefficients < parameters.MinimumBlockCoefficients) {
			throw std::runtime_error("WMA encoded block length is smaller than the stream minimum");
		}

		return coefficients;
	}

	WmaBlockHistory ReadWmaBlockHistory(WmaBitReader& reader, const WmaStandardParameters& parameters) {
		ValidateBlockParameters(parameters);
		auto staged_reader = reader;
		WmaBlockHistory history;
		history.PreviousCoefficients = ReadBlockSize(staged_reader, parameters);
		history.CurrentCoefficients = ReadBlockSize(staged_reader, parameters);
		reader = staged_reader;

		return history;
	}

	std::uint32_t ReadWmaNextBlockSize(WmaBitReader& reader, const WmaStandardParameters& parameters) {
		ValidateBlockParameters(parameters);
		auto staged_reader = reader;
		const auto coefficients = ReadBlockSize(staged_reader, parameters);
		reader = staged_reader;

		return coefficients;
	}

	std::vector<WmaPlacedCoefficient> DecodeWmaCoefficientValues(WmaBitReader& reader, const WmaCoefficientDecoder& decoder, WmaCoefficientEscapeWidths escape_widths, std::uint32_t coefficient_count) {
		if (coefficient_count == 0 || coefficient_count > 2048) {
			throw std::runtime_error("WMA coded coefficient count exceeds the recovered frame extent");
		}

		auto staged_reader = reader;
		std::vector<WmaPlacedCoefficient> values;
		values.reserve(coefficient_count);
		std::uint32_t position = 0;
		while (position < coefficient_count) {
			const auto coefficient = decoder.Decode(staged_reader, escape_widths);
			if (coefficient.Kind == WmaCoefficientKind::EndOfBlock) {
				break;
			}

			if (coefficient.ZeroRun >= coefficient_count - position) {
				throw std::runtime_error("WMA coefficient run exceeds the coded block extent");
			}

			position += coefficient.ZeroRun;
			WmaPlacedCoefficient placed;
			placed.Position = position;
			placed.Value = coefficient;
			values.push_back(placed);
			++position;
		}

		reader = staged_reader;

		return values;
	}

} // namespace sgre::audio
