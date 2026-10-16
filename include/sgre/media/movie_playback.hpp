#pragma once

#include "sgre/media/video.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

namespace sgre::media {

	struct MovieStreamDescription {
		std::uint32_t Width = 0;
		std::uint32_t Height = 0;
		std::int64_t Duration100ns = 0;
		bool HasVideo = false;
		bool HasAudio = false;
		bool CanSeek = false;
	};

	struct MovieVideoAperture {
		float Left = 0;
		float Top = 0;
		std::uint32_t Width = 0;
		std::uint32_t Height = 0;
	};

	struct MovieRgb32Frame {
		std::uint32_t Width = 0;
		std::uint32_t Height = 0;
		std::uint32_t PaddedWidth = 0;
		std::uint32_t PaddedHeight = 0;
		std::size_t RowBytes = 0;
		// Worker-converted bottom-to-top BGRA rows, including the native padded texture extent.
		std::vector<std::uint8_t> Bytes;
	};

	struct MovieVideoSample {
		// NV12 preserves decoder provenance. The actual worker prepares RGB32 before publication to Draw.
		std::shared_ptr<const Nv12VideoFrame> Frame;
		std::shared_ptr<const MovieRgb32Frame> Rgb32;
		MovieVideoAperture Aperture;
		std::int64_t Timestamp100ns = 0;
	};

	enum class MovieVideoReadStatus {
		Pending,
		Sample,
		EndOfStream
	};

	struct MovieVideoRead {
		MovieVideoReadStatus Status = MovieVideoReadStatus::Pending;
		MovieVideoSample Sample;
	};

	struct MovieAudioProgress {
		// Actual output timing, including native sample/block epoch. Absence selects the wall-clock path.
		std::optional<std::int64_t> Position100ns;
		// Actual native+11C audio-input EOF marker. Device drain and output clock remain separate.
		bool InputEnded = false;
		// Native+120 completed PCM assembly count, acknowledged only after the selected video read phase.
		std::uint32_t AssembledBlocks = 0;
	};

	class MoviePlaybackStream {
	public:
		virtual ~MoviePlaybackStream() = default;
		[[nodiscard]] virtual const MovieStreamDescription& Description() const noexcept = 0;
		// A real decoder worker owns demux/codec state. Polling never performs blocking file I/O or decoding.
		[[nodiscard]] virtual MovieVideoRead PollVideo() = 0;
		[[nodiscard]] virtual MovieAudioProgress PumpAudio(std::int64_t now100ns) = 0;
		[[nodiscard]] virtual bool StopRequested() const noexcept = 0;
		virtual void SetPaused(bool paused) = 0;
		virtual void SetVolume(float volume) = 0;
		// Stops and joins actual workers before releasing stream/codec/audio state. Idempotent and nonthrowing.
		virtual void Close() noexcept = 0;
	};

	struct MovieDrawFrame {
		// Identity is borrowed only for this call. The receiver retains Sample.Frame through queued GPU use.
		const void* OwnerIdentity = nullptr;
		std::uint64_t FrameSerial = 0;
		MovieVideoSample Sample;
		// Actual MFixedSizeView coordinate/size after its current sample's aperture update.
		std::array<float, 2> Position{};
		std::array<float, 2> Extent{};
		std::uint32_t Opacity = 255;
		std::uint32_t FilteringX = 0;
		std::uint32_t FilteringY = 0;
		bool UseAlpha = false;
	};

	struct MoviePlaybackServices {
		// Storage is the exact original lookup name. The configured file/seed owner resolves it unchanged.
		std::function<std::unique_ptr<MoviePlaybackStream>(std::string_view storage)> Open;
		// The same actual monotonic clock serves Draw and the audio output timing boundary.
		std::function<std::int64_t()> ReadClock100ns;
		std::function<void(const MovieDrawFrame&)> Draw;
		std::function<bool(const void* owner)> CanRetireDrawOwner;
		std::function<void(const void* owner)> RetireDrawOwner;
	};

} // namespace sgre::media
