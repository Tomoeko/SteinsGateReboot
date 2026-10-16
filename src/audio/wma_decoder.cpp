#include "sgre/audio/wma_decoder.hpp"

#include "sgre/audio/wma_packet.hpp"
#include "sgre/audio/wma_stream_decoder.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>

namespace sgre::audio {

	DecodedAudio DecodeWmaStandardAudio(const resource::WaveFormat& format, std::span<const std::uint8_t> encoded, std::span<const std::uint32_t> decoded_packet_cumulative_bytes, std::uint16_t encode_options, WmaDecodeLimits limits) {
		if (limits.MaxPackets == 0 || limits.MaxPackets > 262144 || limits.MaxDecodedBytes == 0 || limits.MaxDecodedBytes > 256 * 1024 * 1024) {
			throw std::runtime_error("WMA decoding limits exceed their bounded packet or PCM domain");
		}

		const auto packet_headers = ReadWmaPacketHeaders(format, encoded, decoded_packet_cumulative_bytes, limits.MaxPackets);
		const auto sample_count = static_cast<std::size_t>(decoded_packet_cumulative_bytes.back()) / sizeof(std::int16_t);
		if (sample_count > limits.MaxDecodedBytes / sizeof(float)) {
			throw std::runtime_error("WMA decoded sample ledger exceeds its PCM allocation limit");
		}

		WmaStandardStreamDecoder decoder(format, encode_options, static_cast<std::uint32_t>(packet_headers.size()), decoded_packet_cumulative_bytes.back());
		DecodedAudio output;
		output.Channels = format.Channels;
		output.SamplesPerSecond = format.SamplesPerSecond;
		output.Samples.resize(sample_count);
		std::size_t written = 0;
		for (std::size_t packet = 0; packet < packet_headers.size(); ++packet) {
			const auto samples = decoder.DecodePacket(encoded.subspan(packet * format.BlockAlignment, format.BlockAlignment), decoded_packet_cumulative_bytes[packet]);
			if (samples.size() > output.Samples.size() - written) {
				throw std::runtime_error("WMA packet output exceeds its complete-buffer sample ledger");
			}

			std::copy(samples.begin(), samples.end(), output.Samples.begin() + static_cast<std::ptrdiff_t>(written));
			written += samples.size();
		}

		if (!decoder.Complete() || written != output.Samples.size()) {
			throw std::runtime_error("WMA complete-buffer output does not cover its final sample ledger");
		}

		return output;
	}

} // namespace sgre::audio
