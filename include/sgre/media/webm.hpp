#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sgre::media {

	struct WebmLimits {
		std::size_t MaxMetadataBytes = 1024 * 1024;
		std::size_t MaxPacketBytes = 16 * 1024 * 1024;
		std::size_t MaxTracks = 16;
		std::size_t MaxElements = 4 * 1024 * 1024;
	};

	struct WebmTrack {
		std::uint64_t Number = 0;
		std::uint64_t Type = 0;
		std::string Codec;
		std::vector<std::uint8_t> CodecPrivate;
		std::uint32_t Width = 0;
		std::uint32_t Height = 0;
		std::uint32_t Channels = 1;
		double SamplingFrequency = 8000;
		std::uint64_t DefaultDurationNanoseconds = 0;
		std::uint64_t CodecDelayNanoseconds = 0;
		std::uint64_t SeekPreRollNanoseconds = 0;
		bool Alpha = false;
	};

	struct WebmAddition {
		std::uint64_t Id = 1;
		std::vector<std::uint8_t> Bytes;
	};

	struct WebmPacket {
		std::uint64_t TrackNumber = 0;
		std::int64_t TimestampNanoseconds = 0;
		std::uint64_t DurationNanoseconds = 0;
		std::int64_t DiscardPaddingNanoseconds = 0;
		bool KeyFrame = false;
		bool Invisible = false;
		bool Discardable = false;
		// A laced block supplies one timestamp; codec/default-duration rules time later frames.
		std::vector<std::vector<std::uint8_t>> Frames;
		std::vector<WebmAddition> Additions;
	};

	class WebmReader {
	public:
		explicit WebmReader(const std::filesystem::path& path, WebmLimits limits = {});
		// Original movies encrypt every byte with a filename-derived 4096-byte key.
		WebmReader(const std::filesystem::path& path, std::string_view seed, WebmLimits limits = {});
		~WebmReader();
		WebmReader(const WebmReader&) = delete;
		WebmReader& operator=(const WebmReader&) = delete;
		WebmReader(WebmReader&&) noexcept;
		WebmReader& operator=(WebmReader&&) noexcept;

		[[nodiscard]] const std::vector<WebmTrack>& Tracks() const;
		[[nodiscard]] std::uint64_t TimestampScaleNanoseconds() const;
		[[nodiscard]] double DurationNanoseconds() const;
		[[nodiscard]] std::optional<WebmPacket> NextPacket();
		void Rewind();

	private:
		class Implementation;
		std::unique_ptr<Implementation> m_Implementation;
	};

} // namespace sgre::media
