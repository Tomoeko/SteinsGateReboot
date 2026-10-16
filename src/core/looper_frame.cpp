#include "sgre/core/looper_frame.hpp"

#include "sgre/core/application_clock_pump.hpp"

#include <exception>
#include <stdexcept>
#include <utility>

namespace sgre::core {

	static LooperTaskOperations ActualRootOperations(scene::SerialTask& actual_root) {
		actual_root.RequireFrameCallbacks();
		LooperTaskOperations operations;
		operations.ReadState = [&actual_root] {
			if (actual_root.IsClosed()) {
				throw std::runtime_error("actual Looper root state requires its live containing task");
			}
			const auto state = actual_root.State();
			LooperTaskState result;
			result.Status = state.Status;
			result.DrawPaused = state.DrawPaused;

			return result;
		};
		operations.Update = [&actual_root] {
			actual_root.Update();
		};
		operations.Draw = [&actual_root](std::uint32_t argument) {
			actual_root.Draw(argument);
		};
		operations.CanRetire = [&actual_root] {
			return actual_root.CanClose();
		};
		operations.Retire = [&actual_root] {
			actual_root.Close();
		};

		return operations;
	}

	LooperFrame::LooperFrame(ThreadTaskFrame& workers, scene::SerialTask& actual_root, ApplicationClockPump& actual_pump, std::size_t max_tasks_per_group) : LooperFrame(workers, ActualRootOperations(actual_root), actual_pump.Callback(), actual_pump.m_InitialMilliseconds, max_tasks_per_group) {
		m_ActualRoot = &actual_root;
		try {
			actual_pump.Attach(*this);
			m_ActualPump = &actual_pump;
		} catch (...) {
			// Delegating construction runs this completed object's destructor; discard only its failed borrow.
			m_HasTask = false;
			m_ActualRoot = nullptr;
			m_Task = {};
			m_Pump = {};
			throw;
		}
	}

	LooperFrame::LooperFrame(ThreadTaskFrame& workers, LooperTaskOperations actual_task, std::function<void(LooperFrame&)> actual_pump, std::uint64_t initial_milliseconds, std::size_t max_tasks_per_group) : m_Workers(workers), m_Task(std::move(actual_task)), m_Pump(std::move(actual_pump)), m_Clock(FrameClock::NativeLooper(initial_milliseconds)), m_Hooks(max_tasks_per_group), m_VSync(max_tasks_per_group), m_AfterUpdate(max_tasks_per_group), m_AfterWait(max_tasks_per_group) {
		if (!m_Task.ReadState || !m_Task.Update || !m_Task.Draw || !m_Task.CanRetire || !m_Task.Retire || !m_Pump) {
			throw std::runtime_error("Looper frame requires its actual task operations and event/clock pump");
		}

		m_VSync.SetRetireWhenEmpty(false);
		m_AfterUpdate.SetRetireWhenEmpty(false);
		m_AfterWait.SetRetireWhenEmpty(false);
	}

	LooperFrame::~LooperFrame() noexcept {
		try {
			Close();
		} catch (...) {
			std::terminate();
		}
	}

	LooperContainingMutation::LooperContainingMutation(LooperFrame& actual_looper, ThreadTaskFrame& actual_workers) : m_Looper(actual_looper), m_Workers(actual_workers) {
		m_Looper.EnterContainingMutation(*this, actual_workers);
	}

	LooperContainingMutation::~LooperContainingMutation() noexcept {
		try {
			m_Looper.LeaveContainingMutation(*this);
		} catch (...) {
			std::terminate();
		}
	}

	void LooperContainingMutation::RequireOwner(const LooperFrame& actual_looper, const ThreadTaskFrame& actual_workers) const {
		if (&actual_looper != &m_Looper || &actual_workers != &m_Workers) {
			throw std::runtime_error("containing mutation belongs to another actual Looper or worker controller");
		}
		m_Looper.RequireContainingMutation(*this, actual_workers);
	}

	void LooperFrame::EnterContainingMutation(const LooperContainingMutation& actual_scope, ThreadTaskFrame& actual_workers) {
		RequireOpen();
		if (&actual_workers != &m_Workers) {
			throw std::runtime_error("containing mutation requires this Looper's exact actual worker controller");
		}
		RequireIdleContainingPhase();
		m_Workers.EnterContainingMutation(actual_scope);
		const auto groups = ContainingMutationGroups();
		std::size_t claimed = 0;
		try {
			for (auto* group : groups) {
				group->EnterContainingMutation(actual_scope);
				++claimed;
			}
		} catch (...) {
			const auto first = std::current_exception();
			while (claimed != 0) {
				groups[--claimed]->LeaveContainingMutation(actual_scope);
			}
			m_Workers.LeaveContainingMutation(actual_scope);
			std::rethrow_exception(first);
		}
		m_ContainingMutation = &actual_scope;
	}

	void LooperFrame::RequireContainingMutation(const LooperContainingMutation& actual_scope, const ThreadTaskFrame& actual_workers) const {
		RequireOpen();
		if (m_ContainingMutation != &actual_scope || &actual_workers != &m_Workers || m_Running || m_InWait) {
			throw std::runtime_error("containing mutation requires its exact active scope and inactive actual Looper");
		}
		m_Workers.RequireContainingMutation(actual_scope);
		m_Hooks.Group(scene::LooperHookPhase::PreLoop).RequireContainingMutation(actual_scope);
		m_Hooks.Group(scene::LooperHookPhase::PostLoop).RequireContainingMutation(actual_scope);
		m_VSync.RequireContainingMutation(actual_scope);
		m_AfterUpdate.RequireContainingMutation(actual_scope);
		m_AfterWait.RequireContainingMutation(actual_scope);
	}

	void LooperFrame::LeaveContainingMutation(const LooperContainingMutation& actual_scope) {
		RequireContainingMutation(actual_scope, m_Workers);
		for (auto* group : ContainingMutationGroups()) {
			group->LeaveContainingMutation(actual_scope);
		}
		m_Workers.LeaveContainingMutation(actual_scope);
		m_ContainingMutation = nullptr;
	}

	std::array<scene::ParallelTaskSet*, 5> LooperFrame::ContainingMutationGroups() {
		return {&m_Hooks.Group(scene::LooperHookPhase::PreLoop), &m_Hooks.Group(scene::LooperHookPhase::PostLoop), &m_VSync, &m_AfterUpdate, &m_AfterWait};
	}

	void LooperFrame::RequireOpen() const {
		if (std::this_thread::get_id() != m_OwnerThread || m_Closed) {
			throw std::runtime_error("Looper frame requires its live serialized containing thread");
		}
	}

	FrameClock& LooperFrame::Clock() {
		RequireOpen();

		return m_Clock;
	}

	scene::LooperHookTasks& LooperFrame::Hooks() {
		RequireOpen();

		return m_Hooks;
	}

	scene::ParallelTaskSet& LooperFrame::Group(LooperFrameGroup group) {
		RequireOpen();
		switch (group) {
		case LooperFrameGroup::VSync:
			return m_VSync;
		case LooperFrameGroup::AfterUpdate:
			return m_AfterUpdate;
		case LooperFrameGroup::AfterWait:
			return m_AfterWait;
		}

		throw std::runtime_error("Looper frame group is outside the recovered physical groups");
	}

	void LooperFrame::UpdateAndDraw() {
		if (m_Hooks.Group(scene::LooperHookPhase::PreLoop).Status() == scene::TaskStatus::Active) {
			m_Hooks.Update(scene::LooperHookPhase::PreLoop);
		}
		const auto state = m_Task.ReadState();
		if (state.Status == scene::TaskStatus::Active) {
			if (state.Asynchronous) {
				throw std::runtime_error("asynchronous root selection requires its coupled native worker proof");
			}

			m_Task.Update();
		}
		m_Workers.RunFrame();
		if (m_AfterUpdate.Status() == scene::TaskStatus::Active) {
			m_AfterUpdate.Update();
		}
		const auto draw_state = m_Task.ReadState();
		if (!draw_state.DrawPaused && draw_state.Status != scene::TaskStatus::Retire) {
			m_Task.Draw(0);
		}
		if (m_Hooks.Group(scene::LooperHookPhase::PostLoop).Status() == scene::TaskStatus::Active) {
			m_Hooks.Update(scene::LooperHookPhase::PostLoop);
		}
	}

	void LooperFrame::AfterWait() {
		if (m_SkipNext) {
			return;
		}

		m_RemainingFixedTicks = m_FixedBaseCount;
		++m_FrameNumber;
		m_AccumulatedTicks += static_cast<double>(m_Clock.VariableStep());
		if (m_VSync.Status() == scene::TaskStatus::Active) {
			m_VSync.Update();
		}
		const std::lock_guard lock(m_AfterWaitMutex);
		if (m_AfterWait.Status() == scene::TaskStatus::Active) {
			m_AfterWait.Update();
		}
	}

	bool LooperFrame::RunIteration() {
		RequireOpen();
		if (m_Running || m_ContainingMutation != nullptr || !m_HasTask) {
			throw std::runtime_error("Looper iteration requires its live task and nonrecursive phase");
		}
		m_Workers.RequireCanRunFrame();
		m_Running = true;
		bool continuing = false;
		try {
			const auto admission = m_Task.ReadState();
			if (admission.Asynchronous) {
				throw std::runtime_error("asynchronous Looper root requires its coupled actual worker selection proof before frame mutation");
			}
			if (m_SkipNext) {
				m_SkipNext = false;
			} else {
				UpdateAndDraw();
			}
			if (m_Task.ReadState().Status == scene::TaskStatus::Retire) {
				RetireTask();
			}
			m_InWait = true;
			m_Pump(*this);
			m_InWait = false;
			AfterWait();
			continuing = m_HasTask && m_Task.ReadState().Status == scene::TaskStatus::Active;
			m_Running = false;
		} catch (...) {
			m_InWait = false;
			m_Running = false;
			throw;
		}

		return continuing;
	}

	void LooperFrame::RetireTask() {
		if (!m_Task.CanRetire()) {
			throw std::runtime_error("actual Looper task borrowers must finish before retirement");
		}

		m_HasTask = false;
		m_Task.Retire();
	}

	void LooperFrame::ReleaseTask() {
		RequireOpen();
		if (m_Running || m_ContainingMutation != nullptr) {
			throw std::runtime_error("containing Looper task release requires a quiescent frame phase");
		}
		if (!m_HasTask) {
			return;
		}

		m_Running = true;
		bool retirement_invoked = false;
		try {
			if (!m_Task.CanRetire()) {
				throw std::runtime_error("actual Looper task borrowers must finish before containing retirement");
			}

			retirement_invoked = true;
			m_Task.Retire();
			m_HasTask = false;
			m_Running = false;
		} catch (...) {
			if (retirement_invoked) {
				m_HasTask = false;
			}
			m_Running = false;
			throw;
		}
	}

	void LooperFrame::MarkCurrentTaskRetire() {
		RequireOpen();
		if (!m_Running || !m_InWait || !m_HasTask || m_ActualRoot == nullptr || m_ActualRoot->IsClosed()) {
			throw std::runtime_error("application quit requires the exact live typed current root during Looper wait");
		}

		m_ActualRoot->SetStatus(scene::TaskStatus::Retire);
	}

	void LooperFrame::SkipNextIteration() {
		RequireOpen();
		m_SkipNext = true;
	}

	void LooperFrame::ConsumeFixedTick() {
		RequireOpen();
		--m_RemainingFixedTicks;
	}

	std::uint32_t LooperFrame::FixedBaseCount() const {
		RequireOpen();

		return m_FixedBaseCount;
	}

	std::uint32_t LooperFrame::RemainingFixedTicks() const {
		RequireOpen();

		return m_RemainingFixedTicks;
	}

	std::uint32_t LooperFrame::FrameNumber() const {
		RequireOpen();

		return m_FrameNumber;
	}

	double LooperFrame::AccumulatedTicks() const {
		RequireOpen();

		return m_AccumulatedTicks;
	}

	bool LooperFrame::InWait() const {
		RequireOpen();

		return m_InWait;
	}

	bool LooperFrame::SkipPending() const {
		RequireOpen();

		return m_SkipNext;
	}

	bool LooperFrame::HasTask() const {
		RequireOpen();

		return m_HasTask;
	}

	void LooperFrame::RequireIdleContainingPhase() const {
		RequireOpen();
		if (m_Running || m_InWait || m_ContainingMutation != nullptr) {
			throw std::runtime_error("containing Looper resource/hook mutation requires an inactive iteration and wait phase");
		}
		m_Workers.RequireCanRunFrame();
		m_Hooks.Group(scene::LooperHookPhase::PreLoop).RequireIdleTraversal();
		m_Hooks.Group(scene::LooperHookPhase::PostLoop).RequireIdleTraversal();
		m_VSync.RequireIdleTraversal();
		m_AfterUpdate.RequireIdleTraversal();
		m_AfterWait.RequireIdleTraversal();
	}

	bool LooperFrame::CanClose() const noexcept {
		return std::this_thread::get_id() == m_OwnerThread && (m_Closed || (!m_Running && m_ContainingMutation == nullptr && !m_HasTask && (m_ActualPump == nullptr || m_ActualPump->CanDetach(*this)) && m_Hooks.CanClose() && m_VSync.Count() == 0 && m_AfterUpdate.Count() == 0 && m_AfterWait.Count() == 0 && m_VSync.CanClear() && m_AfterUpdate.CanClear() && m_AfterWait.CanClear()));
	}

	void LooperFrame::Close() {
		if (!CanClose()) {
			throw std::runtime_error("Looper task and actual hook/group callbacks must retire before containing close");
		}
		if (m_Closed) {
			return;
		}

		m_AfterWait.Clear();
		m_VSync.Clear();
		m_AfterUpdate.Clear();
		m_Hooks.Close();
		if (m_ActualPump != nullptr) {
			m_ActualPump->Detach(*this);
			m_ActualPump = nullptr;
		}
		m_Closed = true;
	}

} // namespace sgre::core
