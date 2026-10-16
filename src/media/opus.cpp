#include "sgre/media/opus.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <stdexcept>

namespace sgre::media {

	OpusHeader ReadOpusHeader(std::span<const std::uint8_t> bytes) {
		static constexpr std::array<std::uint8_t, 8> kSignature{'O', 'p', 'u', 's', 'H', 'e', 'a', 'd'};
		if (bytes.size() != 19 || !std::equal(kSignature.begin(), kSignature.end(), bytes.begin()) || bytes[8] > 15 || bytes[9] == 0 || bytes[9] > 2 || bytes[18] != 0) {
			throw std::runtime_error("Opus identification requires a compatible mono/stereo mapping-zero header");
		}

		OpusHeader header;
		header.Version = bytes[8];
		header.Channels = bytes[9];
		header.PreSkipSamples = static_cast<std::uint16_t>(bytes[10] | (static_cast<std::uint16_t>(bytes[11]) << 8));
		header.OriginalSamplesPerSecond = static_cast<std::uint32_t>(bytes[12]) | (static_cast<std::uint32_t>(bytes[13]) << 8) | (static_cast<std::uint32_t>(bytes[14]) << 16) | (static_cast<std::uint32_t>(bytes[15]) << 24);
		header.OutputGainQ8Decibels = std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(bytes[16] | (static_cast<std::uint16_t>(bytes[17]) << 8)));

		return header;
	}

	static std::size_t ReadFrameSize(std::span<const std::uint8_t> bytes, std::size_t& position, std::size_t end) {
		if (position >= end) {
			throw std::runtime_error("Opus VBR frame size is truncated");
		}

		const auto first = bytes[position++];
		if (first < 252) {
			return first;
		}

		if (position >= end) {
			throw std::runtime_error("Opus extended VBR size is truncated");
		}

		return first + static_cast<std::size_t>(bytes[position++]) * 4;
	}

	static void SetConfiguration(std::uint8_t configuration, OpusPacketInfo& packet) {
		if (configuration < 12) {
			static constexpr std::array<std::uint32_t, 4> kSilkFrames{480, 960, 1920, 2880};
			packet.Mode = OpusMode::Silk;
			packet.Bandwidth = configuration < 4 ? OpusBandwidth::Narrow : configuration < 8 ? OpusBandwidth::Medium
			                                                                                 : OpusBandwidth::Wide;
			packet.SamplesPerFrame = kSilkFrames[configuration % 4];
		} else if (configuration < 16) {
			packet.Mode = OpusMode::Hybrid;
			packet.Bandwidth = configuration < 14 ? OpusBandwidth::SuperWide : OpusBandwidth::Full;
			packet.SamplesPerFrame = (configuration % 2) == 0 ? 480 : 960;
		} else {
			packet.Mode = OpusMode::Celt;
			packet.Bandwidth = configuration < 20 ? OpusBandwidth::Narrow : configuration < 24 ? OpusBandwidth::Wide
			    : configuration < 28                                                           ? OpusBandwidth::SuperWide
			                                                                                   : OpusBandwidth::Full;
			packet.SamplesPerFrame = 120U << (configuration % 4);
		}
	}

	OpusPacketInfo ReadOpusPacket(std::span<const std::uint8_t> bytes) {
		if (bytes.empty() || bytes.size() > 64 * 1024) {
			throw std::runtime_error("Opus packet length is outside the recovery limit");
		}

		OpusPacketInfo packet;
		SetConfiguration(static_cast<std::uint8_t>(bytes.front() >> 3), packet);
		packet.Stereo = (bytes.front() & 4) != 0;
		const auto code = bytes.front() & 3;
		std::size_t position = 1;
		std::size_t end = bytes.size();
		std::size_t frame_count = code == 0 ? 1 : 2;
		bool variable = code == 2;
		if (code == 3) {
			if (position >= end) {
				throw std::runtime_error("Opus multi-frame count is missing");
			}

			const auto flags = bytes[position++];
			frame_count = flags & 63;
			variable = (flags & 128) != 0;
			if ((flags & 64) != 0) {
				std::size_t padding = 0;
				std::uint8_t byte = 255;
				while (byte == 255) {
					if (position >= end) {
						throw std::runtime_error("Opus padding length is truncated");
					}

					byte = bytes[position++];
					padding += byte == 255 ? 254 : byte;
					if (padding > end - position) {
						throw std::runtime_error("Opus padding exceeds packet");
					}
				}

				end -= padding;
			}
		}

		if (frame_count == 0 || frame_count > 48 || packet.SamplesPerFrame * frame_count > 5760) {
			throw std::runtime_error("Opus packet exceeds the 120 ms duration limit");
		}

		std::vector<std::size_t> lengths(frame_count, 0);
		if (variable) {
			std::size_t total = 0;
			for (std::size_t index = 0; index + 1 < frame_count; ++index) {
				lengths[index] = ReadFrameSize(bytes, position, end);
				if (lengths[index] > end - total) {
					throw std::runtime_error("Opus VBR sizes exceed packet");
				}

				total += lengths[index];
			}

			if (total > end - position) {
				throw std::runtime_error("Opus VBR frame data is truncated");
			}

			lengths.back() = end - position - total;
		} else {
			if ((end - position) % frame_count != 0) {
				throw std::runtime_error("Opus CBR payload does not divide into equal frames");
			}

			std::fill(lengths.begin(), lengths.end(), (end - position) / frame_count);
		}

		for (const auto length : lengths) {
			if (length > 1275) {
				throw std::runtime_error("Opus encoded frame exceeds 1275 bytes");
			}

			OpusFrameRange range;
			range.Offset = position;
			range.Size = length;
			packet.Frames.push_back(range);
			position += length;
		}

		packet.TotalSamples = packet.SamplesPerFrame * static_cast<std::uint32_t>(frame_count);

		return packet;
	}

} // namespace sgre::media
