#include "sgre/render/presentation_synchronization.hpp"

#include "sgre/core/looper_frame.hpp"
#include "sgre/render/sprite_presentation.hpp"

#include <exception>

#include <stdexcept>

namespace sgre::render {

	PresentationSynchronization::PresentationSynchronization(core::LooperFrame& looper, const SpritePassPresentation& presentation) : m_Looper(looper), m_Presentation(presentation) {
		static_cast<void>(m_Looper.FixedBaseCount());
		m_Presentation.RegisterSynchronization(*this);
	}

	PresentationSynchronization::~PresentationSynchronization() noexcept {
		try {
			m_Presentation.ReleaseSynchronization(*this);
		} catch (...) {
			std::terminate();
		}
	}

	void PresentationSynchronization::RequireOpen() const {
		if (std::this_thread::get_id() != m_OwnerThread) {
			throw std::runtime_error("presentation synchronization requires its exact serialized owner thread");
		}
		static_cast<void>(m_Looper.FixedBaseCount());
		m_Presentation.RequireSynchronization(*this);
	}

	void PresentationSynchronization::RequirePresentation(const SpritePassPresentation& presentation) const {
		RequireOpen();
		if (&presentation != &m_Presentation) {
			throw std::runtime_error("presentation synchronization belongs to another actual target owner");
		}
	}

	void PresentationSynchronization::Request(std::uint32_t synchronization) {
		RequireOpen();
		m_Requested = synchronization;
	}

	std::uint32_t PresentationSynchronization::Current() const {
		RequireOpen();

		return m_Current;
	}

	std::uint32_t PresentationSynchronization::Requested() const {
		RequireOpen();

		return m_Requested;
	}

	std::uint32_t PresentationSynchronization::Interval(bool force_unsynchronized) const {
		RequireOpen();

		return force_unsynchronized || m_Current == 0 ? 0 : m_Looper.FixedBaseCount();
	}

	void PresentationSynchronization::Presented(const SpritePassPresentation& presentation) {
		RequirePresentation(presentation);
		m_Current = m_Requested;
	}

} // namespace sgre::render
