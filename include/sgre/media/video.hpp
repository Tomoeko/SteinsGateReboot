#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace sgre::media {

	enum class VideoColorMatrix {
		Unspecified,
		Bt601,
		Bt709,
		Bt2020
	};

	struct Vp9Description {
		std::uint32_t Width = 0;
		std::uint32_t Height = 0;
		std::uint8_t Profile = 0;
		std::uint8_t Level = 0;
		std::uint8_t BitDepth = 8;
		std::uint8_t ChromaSubsampling = 0;
		std::uint8_t ColorPrimaries = 2;
		std::uint8_t TransferCharacteristics = 2;
		std::uint8_t MatrixCoefficients = 2;
		bool FullRange = false;
	};

	struct Nv12VideoFrame {
		std::uint32_t Width = 0;
		std::uint32_t Height = 0;
		std::int64_t TimestampNanoseconds = 0;
		VideoColorMatrix Matrix = VideoColorMatrix::Unspecified;
		bool FullRange = false;
		// Tight top-to-bottom rows; chroma stores U,V pairs at half resolution.
		std::vector<std::uint8_t> Luma;
		std::vector<std::uint8_t> Chroma;
	};

	class Vp9VideoDecoder {
	public:
		explicit Vp9VideoDecoder(Vp9Description description, std::size_t max_frame_bytes = 64 * 1024 * 1024);
		~Vp9VideoDecoder();
		Vp9VideoDecoder(const Vp9VideoDecoder&) = delete;
		Vp9VideoDecoder& operator=(const Vp9VideoDecoder&) = delete;
		Vp9VideoDecoder(Vp9VideoDecoder&&) noexcept;
		Vp9VideoDecoder& operator=(Vp9VideoDecoder&&) noexcept;

		// Call from one decoding worker. Returned frames own their pixels.
		[[nodiscard]] std::vector<Nv12VideoFrame> Decode(std::span<const std::uint8_t> packet, std::int64_t timestamp_nanoseconds);
		[[nodiscard]] std::vector<Nv12VideoFrame> Drain();

	private:
		class Implementation;
		std::unique_ptr<Implementation> m_Implementation;
	};

} // namespace sgre::media
