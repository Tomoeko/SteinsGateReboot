#pragma once

#include "sgre/scene/parallel_task_set.hpp"
#include "sgre/scene/serial_task_set.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>

namespace sgre::scene {

	enum class CommonProcessGroup : std::uint8_t {
		Serial,
		Parallel
	};

	class CommonProcessTasks {
	public:
		// Concrete child callbacks borrow or retain their real owner until explicit graph retirement.
		explicit CommonProcessTasks(std::size_t max_tasks_per_group = 64, SerialTaskSet::Completed completed = {});
		~CommonProcessTasks();
		CommonProcessTasks(const CommonProcessTasks&) = delete;
		CommonProcessTasks& operator=(const CommonProcessTasks&) = delete;
		CommonProcessTasks(CommonProcessTasks&&) = delete;
		CommonProcessTasks& operator=(CommonProcessTasks&&) = delete;

		[[nodiscard]] SerialTaskSet& Serial() noexcept;
		[[nodiscard]] const SerialTaskSet& Serial() const noexcept;
		[[nodiscard]] ParallelTaskSet& Parallel() noexcept;
		[[nodiscard]] const ParallelTaskSet& Parallel() const noexcept;
		void SetDrawPaused(CommonProcessGroup group, bool paused);
		[[nodiscard]] bool IsDrawPaused(CommonProcessGroup group) const;
		// The containing Layer manager supplies its own status/visibility gate and native self lifetime.
		void Update(const std::function<void()>& on_process);
		void Draw(std::uint32_t argument);
		[[nodiscard]] bool CanClear() const noexcept;
		// Actual SQLayer retirement uses these phases in serial-then-parallel order.
		void ClearSerial();
		void ClearParallel();
		// Generic native MProcess destruction clears parallel before serial.
		void Clear();

	private:
		SerialTaskSet m_Serial;
		ParallelTaskSet m_Parallel;
		bool m_SerialDrawPaused = false;
		bool m_ParallelDrawPaused = false;
		bool m_Calling = false;
	};

} // namespace sgre::scene
