#include "sgre/scene/common_process_tasks.hpp"

#include <exception>
#include <stdexcept>
#include <utility>

namespace sgre::scene {

	class CommonProcessCall {
	public:
		explicit CommonProcessCall(bool& calling) : m_Calling(calling) {
			if (calling) {
				throw std::runtime_error("common process traversal cannot reenter update or draw");
			}

			m_Calling = true;
		}

		~CommonProcessCall() {
			m_Calling = false;
		}

	private:
		bool& m_Calling;
	};

	CommonProcessTasks::CommonProcessTasks(std::size_t max_tasks_per_group, SerialTaskSet::Completed completed) : m_Serial(max_tasks_per_group, std::move(completed)), m_Parallel(max_tasks_per_group) {
		// Native embedded groups use +0x1f = 0 independently of their otherwise unread +0x1e flag.
		m_Serial.SetRetireWhenEmpty(false);
		m_Parallel.SetRetireWhenEmpty(false);
	}

	CommonProcessTasks::~CommonProcessTasks() {
		try {
			Clear();
		} catch (...) {
			// Explicit retirement reports the first failure after attempting both owned groups.
		}
	}

	SerialTaskSet& CommonProcessTasks::Serial() noexcept {
		return m_Serial;
	}

	const SerialTaskSet& CommonProcessTasks::Serial() const noexcept {
		return m_Serial;
	}

	ParallelTaskSet& CommonProcessTasks::Parallel() noexcept {
		return m_Parallel;
	}

	const ParallelTaskSet& CommonProcessTasks::Parallel() const noexcept {
		return m_Parallel;
	}

	void CommonProcessTasks::SetDrawPaused(CommonProcessGroup group, bool paused) {
		switch (group) {
		case CommonProcessGroup::Serial:
			m_SerialDrawPaused = paused;
			return;
		case CommonProcessGroup::Parallel:
			m_ParallelDrawPaused = paused;
			return;
		}

		throw std::runtime_error("common process group is outside the native serial/parallel pair");
	}

	bool CommonProcessTasks::IsDrawPaused(CommonProcessGroup group) const {
		switch (group) {
		case CommonProcessGroup::Serial:
			return m_SerialDrawPaused;
		case CommonProcessGroup::Parallel:
			return m_ParallelDrawPaused;
		}

		throw std::runtime_error("common process group is outside the native serial/parallel pair");
	}

	void CommonProcessTasks::Update(const std::function<void()>& on_process) {
		if (!on_process) {
			throw std::runtime_error("common process requires its concrete onProcess operation");
		}

		CommonProcessCall call(m_Calling);
		if (m_Serial.Count() == 0) {
			on_process();
		}

		if (m_Serial.Status() == TaskStatus::Active) {
			m_Serial.Update();
		}

		if (m_Parallel.Status() == TaskStatus::Active) {
			m_Parallel.Update();
		}
	}

	void CommonProcessTasks::Draw(std::uint32_t argument) {
		CommonProcessCall call(m_Calling);
		if (!m_ParallelDrawPaused && m_Parallel.Status() != TaskStatus::Retire) {
			m_Parallel.Draw(argument);
		}

		if (!m_SerialDrawPaused && m_Serial.Status() != TaskStatus::Retire) {
			m_Serial.Draw(argument);
		}
	}

	bool CommonProcessTasks::CanClear() const noexcept {
		return !m_Calling && m_Serial.CanClear() && m_Parallel.CanClear();
	}

	void CommonProcessTasks::ClearSerial() {
		if (!CanClear()) {
			throw std::runtime_error("common process retirement requires inactive owned groups");
		}

		m_Serial.Clear();
	}

	void CommonProcessTasks::ClearParallel() {
		if (!CanClear()) {
			throw std::runtime_error("common process retirement requires inactive owned groups");
		}

		m_Parallel.Clear();
	}

	void CommonProcessTasks::Clear() {
		if (!CanClear()) {
			throw std::runtime_error("common process retirement requires inactive owned groups");
		}

		std::exception_ptr failure;
		try {
			m_Parallel.Clear();
		} catch (...) {
			failure = std::current_exception();
		}

		try {
			m_Serial.Clear();
		} catch (...) {
			if (!failure) {
				failure = std::current_exception();
			}
		}

		if (failure) {
			std::rethrow_exception(failure);
		}
	}

} // namespace sgre::scene
