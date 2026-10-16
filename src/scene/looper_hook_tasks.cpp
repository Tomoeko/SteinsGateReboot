#include "sgre/scene/looper_hook_tasks.hpp"

#include <exception>
#include <stdexcept>

namespace sgre::scene {

	LooperHookTasks::LooperHookTasks(std::size_t max_tasks_per_group) : m_PreLoop(max_tasks_per_group), m_PostLoop(max_tasks_per_group) {
		m_PreLoop.SetRetireWhenEmpty(false);
		m_PostLoop.SetRetireWhenEmpty(false);
	}

	LooperHookTasks::~LooperHookTasks() noexcept {
		try {
			Close();
		} catch (...) {
			// Live clock callbacks still borrow their actual hook parent.
			std::terminate();
		}
	}

	void LooperHookTasks::RequireOpen() const {
		if (std::this_thread::get_id() != m_OwnerThread || m_Closed) {
			throw std::runtime_error("actual Looper hook groups require their live containing owner thread");
		}
	}

	ParallelTaskSet& LooperHookTasks::Group(LooperHookPhase phase) {
		RequireOpen();
		switch (phase) {
		case LooperHookPhase::PreLoop:
			return m_PreLoop;
		case LooperHookPhase::PostLoop:
			return m_PostLoop;
		}

		throw std::runtime_error("Looper hook phase is outside its actual PreLoop/PostLoop groups");
	}

	const ParallelTaskSet& LooperHookTasks::Group(LooperHookPhase phase) const {
		RequireOpen();
		switch (phase) {
		case LooperHookPhase::PreLoop:
			return m_PreLoop;
		case LooperHookPhase::PostLoop:
			return m_PostLoop;
		}

		throw std::runtime_error("Looper hook phase is outside its actual PreLoop/PostLoop groups");
	}

	void LooperHookTasks::Update(LooperHookPhase phase) {
		Group(phase).Update();
	}

	bool LooperHookTasks::CanClose() const noexcept {
		return std::this_thread::get_id() == m_OwnerThread && (m_Closed || (m_PostLoop.Count() == 0 && m_PreLoop.Count() == 0 && m_PostLoop.CanClear() && m_PreLoop.CanClear()));
	}

	bool LooperHookTasks::IsClosed() const noexcept {
		return m_Closed;
	}

	void LooperHookTasks::Close() {
		if (!CanClose()) {
			throw std::runtime_error("actual Looper hook parents must retire before their containing hook groups");
		}
		if (m_Closed) {
			return;
		}

		m_PostLoop.Clear();
		m_PreLoop.Clear();
		m_Closed = true;
	}

} // namespace sgre::scene
