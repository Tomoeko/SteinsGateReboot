#pragma once

#include "sgre/resource/sound.hpp"

#include <cstdint>
#include <memory>
#include <span>

namespace sgre::audio {

	class WmaStandardStreamDecoder {
	public:
		explicit WmaStandardStreamDecoder(const resource::WaveFormat& format, std::uint16_t encode_options, std::uint32_t packet_count, std::uint32_t final_decoded_16bit_bytes);
		~WmaStandardStreamDecoder();
		WmaStandardStreamDecoder(WmaStandardStreamDecoder&&) noexcept;
		WmaStandardStreamDecoder& operator=(WmaStandardStreamDecoder&&) noexcept;
		WmaStandardStreamDecoder(const WmaStandardStreamDecoder&) = delete;
		WmaStandardStreamDecoder& operator=(const WmaStandardStreamDecoder&) = delete;

		// Returned interleaved float storage belongs to this decoder and expires at its next DecodePacket call.
		[[nodiscard]] std::span<const float> DecodePacket(std::span<const std::uint8_t> packet, std::uint32_t cumulative_decoded_16bit_bytes);
		[[nodiscard]] bool Complete() const noexcept;

	private:
		struct State;
		std::unique_ptr<State> m_State;
	};

} // namespace sgre::audio
