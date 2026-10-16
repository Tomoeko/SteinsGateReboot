#include "sgre/audio/wma_coefficient.hpp"

#include "wma_codebooks.hpp"

#include <limits>
#include <stdexcept>
#include <vector>

namespace sgre::audio {

	static std::uint32_t ReadTotalGain(WmaBitReader& reader, WmaGainLimits limits) {
		std::uint32_t gain = 1;
		for (std::uint16_t group = 0; group < limits.MaxGroups; ++group) {
			const auto value = reader.ReadBits(7);
			if (value > limits.MaxGain - gain) {
				throw std::runtime_error("WMA total gain exceeds its configured limit");
			}

			gain += value;
			if (value != 127) {
				return gain;
			}
		}

		throw std::runtime_error("WMA total gain continuation exceeds its group limit");
	}

	WmaCodingHeader ReadWmaCodingHeader(WmaBitReader& reader, std::uint16_t channels, WmaGainLimits limits) {
		if (channels == 0 || channels > 2) {
			throw std::runtime_error("WMA Standard coding header requires one or two channels");
		}

		if (limits.MaxGain == 0 || limits.MaxGroups == 0 || limits.MaxGroups > WmaGainLimits::kMaxGroupCount) {
			throw std::runtime_error("WMA total gain limits are invalid");
		}

		auto trial_reader = reader;
		WmaCodingHeader header;
		if (channels == 2) {
			header.MidSideStereo = trial_reader.ReadBits(1) != 0;
		}

		bool any_coded = false;
		for (std::uint16_t channel = 0; channel < channels; ++channel) {
			header.ChannelCoded[channel] = trial_reader.ReadBits(1) != 0;
			any_coded = any_coded || header.ChannelCoded[channel];
		}

		if (any_coded) {
			header.TotalGain = ReadTotalGain(trial_reader, limits);
		}

		reader = trial_reader;

		return header;
	}

	std::uint8_t WmaEscapeMagnitudeBits(std::uint32_t total_gain) noexcept {
		// The original helper compares its raw gain field as signed 32-bit; bounded decoded gains remain nonnegative.
		if (total_gain > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()) || total_gain < 15) {
			return 13;
		}

		if (total_gain < 32) {
			return 12;
		}

		if (total_gain < 40) {
			return 11;
		}

		if (total_gain < 45) {
			return 10;
		}

		return 9;
	}

	WmaCoefficientBook SelectWmaCoefficientBook(const resource::WaveFormat& format, bool mid_side_stereo, std::uint16_t channel_index) {
		if (format.Encoding != resource::WaveEncoding::WmaStandard || format.Channels == 0 || format.Channels > 2 || channel_index >= format.Channels || format.SamplesPerSecond == 0 || format.SamplesPerSecond > 48000 || format.AverageBytesPerSecond == 0 || format.AverageBytesPerSecond > std::numeric_limits<std::int32_t>::max() || (mid_side_stereo && format.Channels != 2)) {
			throw std::runtime_error("WMA coefficient selection requires a valid Standard format and channel");
		}

		const auto denominator = static_cast<std::uint32_t>(format.Channels) * format.SamplesPerSecond;
		const float bits_per_sample = static_cast<float>(format.AverageBytesPerSecond) * 8.0f / static_cast<float>(denominator);
		const float normalized = format.Channels == 2 ? bits_per_sample * 1.6f : bits_per_sample;
		const bool alternate = mid_side_stereo && channel_index == 1;
		if (format.SamplesPerSecond >= 32000 && normalized < 0.72f) {
			return alternate ? WmaCoefficientBook::SetOneAlternate : WmaCoefficientBook::SetOneMain;
		}

		if (format.SamplesPerSecond >= 32000 && normalized < 1.16f) {
			return alternate ? WmaCoefficientBook::SetTwoAlternate : WmaCoefficientBook::SetTwoMain;
		}

		return alternate ? WmaCoefficientBook::SetThreeAlternate : WmaCoefficientBook::SetThreeMain;
	}

	static std::vector<WmaHuffmanCode> CopyCodes(WmaCoefficientBook book) {
		const auto entries = detail::WmaCoefficientTable(book);
		std::vector<WmaHuffmanCode> codes;
		codes.reserve(entries.size());
		for (const auto& entry : entries) {
			codes.push_back(entry.Code);
		}

		return codes;
	}

	WmaCoefficientDecoder::WmaCoefficientDecoder(WmaCoefficientBook book) : m_Book(book), m_Huffman(CopyCodes(book)) {
	}

	WmaCoefficient WmaCoefficientDecoder::Decode(WmaBitReader& reader, WmaCoefficientEscapeWidths escape_widths) const {
		if (escape_widths.MagnitudeBits == 0 || escape_widths.MagnitudeBits > WmaCoefficientEscapeWidths::kMaxNativeFieldBits || escape_widths.ZeroRunBits == 0 || escape_widths.ZeroRunBits >= WmaCoefficientEscapeWidths::kMaxNativeFieldBits) {
			throw std::runtime_error("WMA coefficient escape widths exceed the supported unsigned fields");
		}

		auto trial_reader = reader;
		const auto symbol = m_Huffman.DecodeSymbol(trial_reader);
		WmaCoefficient coefficient;
		if (symbol == 1) {
			coefficient.Kind = WmaCoefficientKind::EndOfBlock;
		} else {
			if (symbol == 0) {
				coefficient.Magnitude = trial_reader.ReadBits(escape_widths.MagnitudeBits);
				coefficient.ZeroRun = trial_reader.ReadBits(escape_widths.ZeroRunBits);
			} else {
				const auto entries = detail::WmaCoefficientTable(m_Book);
				coefficient.ZeroRun = entries[symbol].ZeroRun;
				coefficient.Magnitude = entries[symbol].Magnitude;
			}

			// The native Standard branch treats a zero sign bit as negative, including escape values.
			coefficient.IsNegative = trial_reader.ReadBits(1) == 0;
		}

		reader = trial_reader;

		return coefficient;
	}

} // namespace sgre::audio
