#pragma once

#include "sgre/audio/offline_buffered_sound_pool.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace sgre::audio {
	class PcmOutputSubmissionOwner;
}
namespace sgre::platform {
	class MacPcmOutput;
}
namespace sgre::media {
	class MovieSilentAudioRegistry;

	// This mode uses only the actual software ring/controller and production callback in quiet tests.
	// It neither constructs nor certifies a hardware device, source readiness or physical presentation.
	enum class MoviePcmSoftwareOnly { Explicit48kStereo };

	// The actual registry, Sound pool and sink enclose this serialized owner through Close. Each phase
	// renders the real Sound master and Movie mixer, combines their PCM and submits it exactly once.
	// Successful ring copies, not offline render progress, drive the bound Movie block clock.
	class MoviePcmDelivery {
	public:
		MoviePcmDelivery(MovieSilentAudioRegistry& movies, audio::OfflineBufferedSoundPool& sound, platform::MacPcmOutput& output);
		MoviePcmDelivery(MoviePcmSoftwareOnly, MovieSilentAudioRegistry& movies, audio::OfflineBufferedSoundPool& sound, audio::PcmOutputSubmissionOwner& output);
		~MoviePcmDelivery() noexcept;
		MoviePcmDelivery(const MoviePcmDelivery&) = delete;
		MoviePcmDelivery& operator=(const MoviePcmDelivery&) = delete;
		[[nodiscard]] audio::OfflineSoundRenderResult RenderAndSubmit(std::uint32_t master_frames);
		// Bounded owner-side drain; no decoding, allocation or source calls enter the device callback.
		[[nodiscard]] std::size_t Poll();
		[[nodiscard]] std::size_t PendingBlockEvents() const;
		// Hardware close performs actual disposal/quiescence; software close requires its real callback disabled/idle.
		// A first render/submit/drain failure remains observable after all safe cleanup has completed.
		void Close();
		// Same serialized owner: true only after actual sink retirement and reciprocal borrow release.
		[[nodiscard]] bool IsClosed() const noexcept;

	private:
		class Implementation;
		std::unique_ptr<Implementation> m_Implementation;
	};
} // namespace sgre::media
