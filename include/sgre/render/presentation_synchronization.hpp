#pragma once

#include <cstdint>
#include <thread>

namespace sgre::core {
	class LooperFrame;
}

namespace sgre::render {

	class SpritePassPresentation;

	class PresentationSynchronization {
	public:
		// The exact serialized Looper and hidden presentation enclose this stable scalar owner.
		PresentationSynchronization(core::LooperFrame& looper, const SpritePassPresentation& presentation);
		~PresentationSynchronization() noexcept;
		PresentationSynchronization(const PresentationSynchronization&) = delete;
		PresentationSynchronization& operator=(const PresentationSynchronization&) = delete;
		// Native052cd0 preserves the complete request word; it does not immediately change current052c90.
		void Request(std::uint32_t synchronization);
		[[nodiscard]] std::uint32_t Current() const;
		[[nodiscard]] std::uint32_t Requested() const;
		[[nodiscard]] std::uint32_t Interval(bool force_unsynchronized) const;

	private:
		friend class SpritePassPresentation;
#if defined(SGRE_SPRITE_PRESENTATION_FIXTURE)
		friend class PresentationSynchronizationFixtureBridge;
#endif
		void RequireOpen() const;
		void RequirePresentation(const SpritePassPresentation& presentation) const;
		void Presented(const SpritePassPresentation& presentation);
		core::LooperFrame& m_Looper;
		const SpritePassPresentation& m_Presentation;
		const std::thread::id m_OwnerThread = std::this_thread::get_id();
		std::uint32_t m_Current = 1;
		std::uint32_t m_Requested = 1;
	};

} // namespace sgre::render
