#include "sgre/audio/wma_stream_decoder.hpp"

#include "sgre/audio/wma_block.hpp"
#include "sgre/audio/wma_exponent.hpp"
#include "sgre/audio/wma_scaling.hpp"
#include "sgre/audio/wma_transform.hpp"
#include "sgre/audio/wma_window.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace sgre::audio {

	static constexpr std::size_t kMaximumReservoirBits = 65536 * 8;
	static constexpr std::uint16_t kRecoveredEncodeOptions = 31;
	static constexpr float kNativeFloatNormalization = -1.0f / 32768.0f;

	struct StreamExponentState {
		std::uint32_t BlockCoefficients = 0;
		std::array<std::int32_t, 25> Bands = {};
		std::int32_t MaximumExponent = 0;
	};

	struct StreamFrameState {
		std::array<StreamExponentState, 2> Exponents;
		WmaBlockHistory History;
		bool HasHistory = false;
	};

	struct StreamFrameResult {
		std::span<const float> Samples;
	};

	struct StreamDecoderTables {
		std::vector<WmaInverseTransform> Transforms;
		WmaWindowBank Windows;
		WmaHuffmanDecoder Exponents{WmaExponentCodebook()};
		std::vector<WmaCoefficientDecoder> Coefficients;

		std::array<std::vector<std::uint32_t>, 5> BandEnds;
		std::array<std::array<float, 2048>, 2> CoefficientWorkspace = {};
		std::array<float, 2048> CompactWorkspace = {};
		std::array<float, 4096> TimeWorkspace = {};
		std::array<float, 8192> FrameWorkspace = {};

		explicit StreamDecoderTables(std::uint32_t sample_rate) {
			Transforms.reserve(5);
			for (std::uint32_t size = 128; size <= 2048; size *= 2) {
				Transforms.emplace_back(size);
				BandEnds[static_cast<std::size_t>(std::bit_width(size) - 8)] = WmaExponentBandEnds(sample_rate, size);
			}

			Coefficients.emplace_back(WmaCoefficientBook::SetTwoMain);
			Coefficients.emplace_back(WmaCoefficientBook::SetTwoAlternate);
		}
	};

	static const std::array<std::array<float, 2048>, 2>& DecodeCoefficientBlock(WmaBitReader& reader, StreamFrameState& state, StreamDecoderTables& tables, const resource::WaveFormat& format, const WmaStandardParameters& parameters, const WmaCodingHeader& coding, std::uint32_t current, std::uint32_t frame_position) {
		auto& coefficients = tables.CoefficientWorkspace;
		for (auto& channel : coefficients) {
			std::fill_n(channel.begin(), current, 0.0f);
		}

		const bool any_coded = coding.ChannelCoded[0] || coding.ChannelCoded[1];
		bool new_exponents = false;
		if (any_coded) {
			new_exponents = current == parameters.FrameCoefficientCount || reader.ReadBits(1) != 0;
			if (frame_position == 0 && !new_exponents) {
				throw std::runtime_error("WMA first short block did not supply exponents");
			}

			if (new_exponents) {
				for (std::size_t channel = 0; channel < format.Channels; ++channel) {
					auto& exponents = state.Exponents[channel];
					exponents.BlockCoefficients = current;
					const auto& ends = tables.BandEnds[static_cast<std::size_t>(std::bit_width(current) - 8)];
					exponents.Bands.fill(0);
					exponents.MaximumExponent = 0;
					if (coding.ChannelCoded[channel]) {
						exponents.MaximumExponent = std::numeric_limits<std::int32_t>::lowest();
						std::int32_t exponent = 36;
						for (std::size_t band = 0; band + 1 < ends.size(); ++band) {
							exponent += DecodeWmaExponentDelta(reader, tables.Exponents);
							exponents.Bands[band] = exponent;
							exponents.MaximumExponent = std::max(exponents.MaximumExponent, exponent);
						}
					} else {
						exponents.Bands.fill(0);
						exponents.MaximumExponent = 0;
					}
				}
			}

			// Native Standard truncates the full-frame cutoff before scaling it to a shorter block.
			const auto coded_count = (parameters.FrameCoefficientCount - parameters.FrameCoefficientCount * 9 / 100) * current / parameters.FrameCoefficientCount;
			WmaCoefficientEscapeWidths widths;
			widths.MagnitudeBits = WmaEscapeMagnitudeBits(coding.TotalGain);
			widths.ZeroRunBits = parameters.FrameCoefficientBits;
			const auto gain = WmaGainScale(static_cast<std::int32_t>(coding.TotalGain));
			for (std::size_t channel = 0; channel < format.Channels; ++channel) {
				if (!coding.ChannelCoded[channel]) {
					continue;
				}

				const auto& exponents = state.Exponents[channel];
				if (exponents.BlockCoefficients == 0) {
					throw std::runtime_error("WMA coefficient block has no valid exponent state");
				}

				const auto& decoder = tables.Coefficients[coding.MidSideStereo && channel == 1 ? 1 : 0];
				const auto& ends = tables.BandEnds[static_cast<std::size_t>(std::bit_width(exponents.BlockCoefficients) - 8)];
				std::uint32_t coefficient_position = 0;
				while (coefficient_position < coded_count) {
					const auto coefficient = decoder.Decode(reader, widths);
					if (coefficient.Kind == WmaCoefficientKind::EndOfBlock) {
						break;
					}
					if (coefficient.ZeroRun >= coded_count - coefficient_position) {
						throw std::runtime_error("WMA coefficient run exceeds its coded extent");
					}
					coefficient_position += coefficient.ZeroRun;
					// Reused exponents retain their original block geometry.
					const auto exponent_position = coefficient_position * exponents.BlockCoefficients / current;
					const auto band = static_cast<std::size_t>(std::upper_bound(ends.begin(), ends.end(), exponent_position) - ends.begin() - 1);
					if (band + 1 >= ends.size()) {
						throw std::runtime_error("WMA resampled exponent position exceeds its block");
					}

					coefficients[channel][coefficient_position] = ScaleWmaCoefficient(coefficient, gain, exponents.MaximumExponent - exponents.Bands[band]);
					++coefficient_position;
				}
			}
		}

		if (coding.MidSideStereo && any_coded) {
			for (std::size_t index = 0; index < current; ++index) {
				const float mid = coefficients[0][index];
				const float side = coefficients[1][index];
				coefficients[0][index] = mid + side;
				coefficients[1][index] = mid - side;
			}
		}

		return coefficients;
	}

	static void AddWindowedBlock(std::span<float> samples, const std::array<std::array<float, 2048>, 2>& coefficients, StreamDecoderTables& tables, const resource::WaveFormat& format, const WmaStandardParameters& parameters, const WmaBlockHistory& history, std::uint32_t next, std::uint32_t position) {
		const auto current = history.CurrentCoefficients;
		for (std::size_t channel = 0; channel < format.Channels; ++channel) {
			auto compact = std::span<float>(tables.CompactWorkspace).first(current);
			auto time_domain = std::span<float>(tables.TimeWorkspace).first(current * 2);
			tables.Transforms[static_cast<std::size_t>(std::bit_width(current) - 8)].ComputeCompact(std::span<const float>(coefficients[channel]).first(current), compact);
			ExpandWmaInverseTransform(compact, time_domain);
			tables.Windows.Apply(time_domain, history.PreviousCoefficients, next);
			const auto begin = parameters.FrameCoefficientCount / 2 + position - current / 2;
			for (std::size_t index = 0; index < time_domain.size(); ++index) {
				samples[(begin + index) * format.Channels + channel] += time_domain[index];
			}
		}
	}

	static StreamFrameResult DecodeFrame(WmaBitReader& reader, StreamFrameState& state, StreamDecoderTables& tables, const resource::WaveFormat& format, const WmaStandardParameters& parameters) {
		StreamFrameResult result;
		auto frame_samples = std::span<float>(tables.FrameWorkspace).first(parameters.FrameCoefficientCount * 2 * format.Channels);
		std::fill(frame_samples.begin(), frame_samples.end(), 0.0f);
		result.Samples = frame_samples;
		auto history = state.HasHistory ? state.History : ReadWmaBlockHistory(reader, parameters);
		std::uint32_t position = 0;
		while (position < parameters.FrameCoefficientCount) {
			const auto current = history.CurrentCoefficients;
			if (current > parameters.FrameCoefficientCount - position) {
				throw std::runtime_error("WMA decoded block exceeds this frame");
			}

			const auto next = ReadWmaNextBlockSize(reader, parameters);
			const auto coding = ReadWmaCodingHeader(reader, format.Channels);
			const auto& coefficients = DecodeCoefficientBlock(reader, state, tables, format, parameters, coding, current, position);
			AddWindowedBlock(frame_samples, coefficients, tables, format, parameters, history, next, position);

			position += current;
			history.PreviousCoefficients = current;
			history.CurrentCoefficients = next;
		}

		state.History = history;
		state.HasHistory = true;

		return result;
	}

	struct StreamBitStorage {
		std::array<std::uint8_t, 65536> Bytes = {};
		std::size_t Count = 0;

		void Append(WmaBitReader& reader, std::size_t count) {
			if (Count > kMaximumReservoirBits || count > kMaximumReservoirBits - Count || count > reader.RemainingBits()) {
				throw std::runtime_error("WMA bit reservoir exceeds its bound");
			}

			for (std::size_t index = 0; index < count; ++index) {
				Bytes[Count / 8] |= static_cast<std::uint8_t>(reader.ReadBits(1) << (7 - Count % 8));
				++Count;
			}
		}
	};

	static void ValidateRecoveredFormat(const resource::WaveFormat& format, std::uint16_t encode_options) {
		const bool mono_voice = format.Channels == 1 && format.SamplesPerSecond == 44100 && format.AverageBytesPerSecond == 4000 && format.BlockAlignment == 1487;
		const bool stereo_sound = format.Channels == 2 && format.AverageBytesPerSecond == 6000 && ((format.SamplesPerSecond == 44100 && format.BlockAlignment == 2230) || (format.SamplesPerSecond == 48000 && format.BlockAlignment == 1008));
		if (format.Encoding != resource::WaveEncoding::WmaStandard || format.BitsPerSample != 16 || !format.Extra.empty() || (!mono_voice && !stereo_sound) || encode_options != kRecoveredEncodeOptions) {
			throw std::runtime_error("WMA decoding requires an established supplied Standard profile with explicit options 31");
		}
	}

	static void ValidateInitialFrame(std::span<const std::uint8_t> encoded, const resource::WaveFormat& format, const WmaStandardParameters& parameters) {
		const auto packet = encoded.first(parameters.PacketBytes);
		const auto header = ReadWmaReservoirHeader(packet, parameters);
		WmaBitReader reader(packet);
		reader.SkipBits(header.HeaderBitCount + header.ContinuationBitCount);
		const auto history = ReadWmaBlockHistory(reader, parameters);
		static_cast<void>(ReadWmaNextBlockSize(reader, parameters));
		const auto coding = ReadWmaCodingHeader(reader, format.Channels);
		if (header.ContinuationBitCount != 0 || header.FrameCountField == 0 || history.PreviousCoefficients != parameters.FrameCoefficientCount || history.CurrentCoefficients != parameters.FrameCoefficientCount || !coding.ChannelCoded[0] || (format.Channels == 2 && !coding.ChannelCoded[1])) {
			throw std::runtime_error("WMA initial block is outside the recovered full-length coded delay profile");
		}
	}

	struct WmaStandardStreamDecoder::State {
		resource::WaveFormat Format;
		WmaStandardParameters Parameters;
		StreamDecoderTables Tables;
		StreamFrameState Frame;
		StreamBitStorage Pending;
		std::array<float, 8192> Overlap = {};
		std::array<float, 17 * 2048 * 2> PacketOutput = {};
		std::uint32_t PacketCount = 0;
		std::uint32_t PacketIndex = 0;
		std::uint32_t FinalBytes = 0;
		std::uint32_t PreviousBytes = 0;
		std::size_t FrameCount = 0;
		std::size_t OutputSamples = 0;
		std::size_t PacketSamples = 0;
		bool Failed = false;

		State(const resource::WaveFormat& format, std::uint16_t options, std::uint32_t packets, std::uint32_t final_bytes) : Format(format), Parameters(DescribeWmaStandard(format, options)), Tables(format.SamplesPerSecond), PacketCount(packets), FinalBytes(final_bytes) {
			ValidateRecoveredFormat(format, options);
			if (packets == 0 || packets > 262144 || final_bytes % (format.Channels * 2) != 0) {
				throw std::runtime_error("WMA streaming packet count or final sample extent differs");
			}
		}

		void AppendFrame(std::span<const float> samples) {
			const auto frame_samples = static_cast<std::size_t>(Parameters.FrameCoefficientCount) * Format.Channels;
			const auto final_samples = static_cast<std::size_t>(FinalBytes) / 2;
			if (FrameCount > (final_samples + frame_samples - 1) / frame_samples + 2 || samples.size() != frame_samples * 2) {
				throw std::runtime_error("WMA streaming frame extent exceeds its resource ledger");
			}
			for (std::size_t index = 0; index < samples.size(); ++index) {
				Overlap[index] += samples[index];
			}
			if (FrameCount >= 2) {
				const auto count = std::min(frame_samples, final_samples - OutputSamples);
				if (count > PacketOutput.size() - PacketSamples) {
					throw std::runtime_error("WMA packet output exceeds its fixed workspace");
				}
				for (std::size_t index = 0; index < count; ++index) {
					const float value = Overlap[index] * kNativeFloatNormalization;
					if (!std::isfinite(value)) {
						throw std::runtime_error("WMA streaming output is nonfinite");
					}
					PacketOutput[PacketSamples + index] = value;
				}
				PacketSamples += count;
				OutputSamples += count;
			}
			std::copy_n(Overlap.begin() + static_cast<std::ptrdiff_t>(frame_samples), frame_samples, Overlap.begin());
			std::fill(Overlap.begin() + static_cast<std::ptrdiff_t>(frame_samples), Overlap.end(), 0.0f);
			++FrameCount;
		}

		std::span<const float> Decode(std::span<const std::uint8_t> packet, std::uint32_t cumulative) {
			PacketSamples = 0;
			if (Failed || PacketIndex >= PacketCount || cumulative < PreviousBytes || cumulative > FinalBytes || (cumulative - PreviousBytes) % (Format.Channels * 2) != 0 || (PacketIndex + 1 == PacketCount && cumulative != FinalBytes)) {
				throw std::runtime_error("WMA streaming packet or decoded ledger order differs");
			}
			try {
				const auto header = ReadWmaReservoirHeader(packet, Parameters);
				if (header.Sequence != PacketIndex % 16) {
					throw std::runtime_error("WMA streaming sequence differs");
				}
				if (PacketIndex == 0) {
					ValidateInitialFrame(packet, Format, Parameters);
				}
				WmaBitReader reader(packet);
				reader.SkipBits(header.HeaderBitCount);
				if (Pending.Count != 0) {
					Pending.Append(reader, header.ContinuationBitCount);
					WmaBitReader continued(std::span<const std::uint8_t>(Pending.Bytes).first((Pending.Count + 7) / 8));
					AppendFrame(DecodeFrame(continued, Frame, Tables, Format, Parameters).Samples);
					if (continued.PositionBits() > Pending.Count) {
						throw std::runtime_error("WMA continued frame consumes reservoir padding");
					}
					Pending.Bytes.fill(0);
					Pending.Count = 0;
				} else {
					reader.SkipBits(header.ContinuationBitCount);
				}
				if (header.FrameCountField == 0) {
					if (PacketIndex + 1 != PacketCount || cumulative != PreviousBytes) {
						throw std::runtime_error("WMA zero-count packet is outside the recovered final variant");
					}
				} else {
					Frame.HasHistory = false;
					for (std::size_t frame = 1; frame < header.FrameCountField; ++frame) {
						AppendFrame(DecodeFrame(reader, Frame, Tables, Format, Parameters).Samples);
					}
					Pending.Append(reader, reader.RemainingBits());
					if (PacketIndex + 1 == PacketCount && Pending.Count != 0) {
						WmaBitReader final_reader(std::span<const std::uint8_t>(Pending.Bytes).first((Pending.Count + 7) / 8));
						AppendFrame(DecodeFrame(final_reader, Frame, Tables, Format, Parameters).Samples);
						if (final_reader.PositionBits() > Pending.Count) {
							throw std::runtime_error("WMA final frame consumes reservoir padding");
						}
					}
				}
				if (PacketIndex + 1 == PacketCount && OutputSamples < static_cast<std::size_t>(FinalBytes) / 2) {
					const auto remaining = static_cast<std::size_t>(FinalBytes) / 2 - OutputSamples;
					const auto frame_samples = static_cast<std::size_t>(Parameters.FrameCoefficientCount) * Format.Channels;
					if (remaining > frame_samples || remaining > PacketOutput.size() - PacketSamples) {
						throw std::runtime_error("WMA final overlap exceeds its bounded remaining sample extent");
					}
					for (std::size_t index = 0; index < remaining; ++index) {
						const float value = Overlap[index] * kNativeFloatNormalization;
						if (!std::isfinite(value)) {
							throw std::runtime_error("WMA final overlap output is nonfinite");
						}
						PacketOutput[PacketSamples + index] = value;
					}
					PacketSamples += remaining;
					OutputSamples += remaining;
				}
				if (PacketIndex + 1 == PacketCount && OutputSamples != static_cast<std::size_t>(FinalBytes) / 2) {
					throw std::runtime_error("WMA final streaming output does not cover its sample ledger");
				}
				PreviousBytes = cumulative;
				++PacketIndex;
			} catch (const std::exception& error) {
				Failed = true;
				PacketSamples = 0;
				throw std::runtime_error("WMA streaming packet " + std::to_string(PacketIndex) + ": " + error.what());
			}
			return std::span<const float>(PacketOutput).first(PacketSamples);
		}
	};

	WmaStandardStreamDecoder::WmaStandardStreamDecoder(const resource::WaveFormat& format, std::uint16_t options, std::uint32_t packets, std::uint32_t final_bytes) {
		ValidateRecoveredFormat(format, options);
		if (packets == 0 || packets > 262144 || final_bytes % (format.Channels * 2) != 0) {
			throw std::runtime_error("WMA streaming packet count or final sample extent differs");
		}

		m_State = std::make_unique<State>(format, options, packets, final_bytes);
	}
	WmaStandardStreamDecoder::~WmaStandardStreamDecoder() = default;
	WmaStandardStreamDecoder::WmaStandardStreamDecoder(WmaStandardStreamDecoder&&) noexcept = default;
	WmaStandardStreamDecoder& WmaStandardStreamDecoder::operator=(WmaStandardStreamDecoder&&) noexcept = default;
	std::span<const float> WmaStandardStreamDecoder::DecodePacket(std::span<const std::uint8_t> packet, std::uint32_t cumulative) {
		if (!m_State) {
			throw std::runtime_error("WMA stream decoder was moved from");
		}
		return m_State->Decode(packet, cumulative);
	}
	bool WmaStandardStreamDecoder::Complete() const noexcept {
		return m_State && !m_State->Failed && m_State->PacketIndex == m_State->PacketCount;
	}

} // namespace sgre::audio
