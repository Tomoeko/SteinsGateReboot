#include "sgre/scene/serial_task_set.hpp"

#include <exception>
#include <limits>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace sgre::scene {

	static constexpr std::size_t kMaxSerialTasks = 65536;

	static void RequireSerialTaskStatus(TaskStatus status) {
		if (status != TaskStatus::Active && status != TaskStatus::Complete && status != TaskStatus::Retire) {
			throw std::runtime_error("serial task status is outside the recovered 0..2 range");
		}
	}

	SerialTask::SerialTask(SerialTaskCallbacks callbacks, ParallelTaskState initial) : m_Callbacks(std::move(callbacks)), m_State(initial) {
		RequireSerialTaskStatus(initial.Status);
	}

	SerialTask::~SerialTask() {
		try {
			Close();
		} catch (...) {
			if (!m_Closed) {
				std::terminate();
			}
			// Explicit Close reports a failure only after concrete ownership has retired.
		}
	}

	ParallelTaskState SerialTask::State() const noexcept {
		return m_State;
	}

	bool SerialTask::IsClosed() const noexcept {
		return m_Closed;
	}

	void SerialTask::RequireFrameCallbacks() const {
		if (m_Closed || m_Calling || !m_Callbacks.Update || !m_Callbacks.Draw || !m_Callbacks.Release) {
			throw std::runtime_error("actual Looper root requires its live quiescent concrete update/draw/retirement callbacks");
		}
	}

	void SerialTask::SetStatus(TaskStatus status) {
		RequireSerialTaskStatus(status);
		if (m_Closed) {
			throw std::runtime_error("serial task status requires its live owner");
		}

		m_State.Status = status;
	}

	void SerialTask::SetDrawPaused(bool paused) {
		if (m_Closed) {
			throw std::runtime_error("serial task draw pause requires its live owner");
		}

		m_State.DrawPaused = paused;
	}

	void SerialTask::Update() {
		if (m_Closed || m_Calling) {
			throw std::runtime_error("serial task update requires a live nonreentrant callback");
		}
		if (m_State.Status != TaskStatus::Active || !m_Callbacks.Update) {
			return;
		}

		m_Calling = true;
		try {
			m_Callbacks.Update(*this);
		} catch (...) {
			m_Calling = false;
			throw;
		}
		m_Calling = false;
	}

	void SerialTask::Draw(std::uint32_t argument) {
		if (m_Closed || m_Calling) {
			throw std::runtime_error("serial task draw requires a live nonreentrant callback");
		}
		if (m_State.DrawPaused || m_State.Status == TaskStatus::Retire || !m_Callbacks.Draw) {
			return;
		}

		m_Calling = true;
		try {
			m_Callbacks.Draw(*this, argument);
		} catch (...) {
			m_Calling = false;
			throw;
		}
		m_Calling = false;
	}

	bool SerialTask::CanClose() const noexcept {
		if (m_Closed) {
			return true;
		}
		if (m_Calling) {
			return false;
		}

		try {
			return !m_Callbacks.CanRelease || m_Callbacks.CanRelease();
		} catch (...) {
			return false;
		}
	}

	void SerialTask::Close() {
		if (m_Closed) {
			return;
		}
		if (m_Calling) {
			throw std::runtime_error("serial task cannot close from its active callback");
		}

		if (!CanClose()) {
			throw std::runtime_error("serial task concrete owner requires a safe retirement boundary");
		}

		m_Closed = true;
		auto callbacks = std::move(m_Callbacks);
		if (callbacks.Release) {
			callbacks.Release();
		}
	}

	struct SerialTaskRecord {
		std::unique_ptr<SerialTask> Task;
		std::optional<std::uint32_t> Next;
		std::uint64_t Generation = 1;
	};

	class SerialTaskSet::Implementation {
	public:
		Implementation(std::size_t max_tasks, Completed completed) : TakeCompleted(std::move(completed)) {
			if (max_tasks == 0 || max_tasks > kMaxSerialTasks) {
				throw std::runtime_error("serial task set requires a bounded positive capacity");
			}

			Records.resize(max_tasks);
			FreeSlots.reserve(max_tasks);
			for (std::size_t index = max_tasks; index != 0; --index) {
				FreeSlots.push_back(static_cast<std::uint32_t>(index - 1));
			}
		}

		std::vector<SerialTaskRecord> Records;
		std::vector<std::uint32_t> FreeSlots;
		Completed TakeCompleted;
		std::optional<std::uint32_t> Head;
		std::optional<std::uint32_t> Tail;
		TaskStatus SetStatus = TaskStatus::Active;
		std::size_t TaskCount = 0;
		bool RetireWhenEmpty = true;
		bool Traversing = false;
		bool Releasing = false;

		class Traversal {
		public:
			explicit Traversal(Implementation& owner) : m_Owner(owner) {
				if (owner.Traversing || owner.Releasing) {
					throw std::runtime_error("serial task traversal cannot reenter update, draw or ownership transfer");
				}

				m_Owner.Traversing = true;
			}

			~Traversal() {
				m_Owner.Traversing = false;
			}

		private:
			Implementation& m_Owner;
		};

		void RequireMutation() const {
			if (Releasing) {
				throw std::runtime_error("serial task mutation cannot reenter retirement or completed ownership transfer");
			}
		}

		bool Contains(SerialTaskHandle handle) const noexcept {
			return handle.Owner == this && handle.Index < Records.size() && Records[handle.Index].Task && Records[handle.Index].Generation == handle.Generation;
		}

		SerialTaskRecord& Record(SerialTaskHandle handle) {
			if (!Contains(handle)) {
				throw std::runtime_error("serial task handle is stale or belongs to another owner");
			}

			return Records[handle.Index];
		}

		const SerialTaskRecord& Record(SerialTaskHandle handle) const {
			if (!Contains(handle)) {
				throw std::runtime_error("serial task handle is stale or belongs to another owner");
			}

			return Records[handle.Index];
		}

		SerialTaskHandle Handle(std::uint32_t index) const {
			return {index, Records[index].Generation, this};
		}

		SerialTaskHandle Insert(std::unique_ptr<SerialTask> task, bool prepend) {
			RequireMutation();
			if (!task || task->IsClosed() || (prepend && Traversing)) {
				throw std::runtime_error("serial task insertion requires a live owner and stable prepend boundary");
			}
			if (FreeSlots.empty()) {
				throw std::runtime_error("serial task set capacity is exhausted");
			}

			const auto index = FreeSlots.back();
			auto& record = Records[index];
			record.Task = std::move(task);
			FreeSlots.pop_back();
			++TaskCount;
			if (prepend) {
				record.Next = Head;
				Head = index;
				if (!Tail) {
					Tail = index;
				}
			} else {
				record.Next.reset();
				if (Tail) {
					Records[*Tail].Next = index;
				} else {
					Head = index;
				}

				Tail = index;
			}

			return Handle(index);
		}

		void RemoveHead(bool complete) {
			const auto index = *Head;
			auto& record = Records[index];
			if (!complete && !record.Task->CanClose()) {
				throw std::runtime_error("serial task removal requires its concrete owner retirement preflight");
			}

			auto task = std::move(record.Task);
			Head = record.Next;
			record.Next.reset();
			if (!Head) {
				Tail.reset();
			}
			--TaskCount;
			if (record.Generation != std::numeric_limits<std::uint64_t>::max()) {
				++record.Generation;
				FreeSlots.push_back(index);
			}

			Releasing = true;
			try {
				if (complete) {
					TakeCompleted(std::move(task));
				} else {
					task->Close();
				}
			} catch (...) {
				Releasing = false;
				throw;
			}
			Releasing = false;
		}

		void Update() {
			const Traversal traversal(*this);
			std::size_t visits = 0;
			while (Head) {
				if (++visits > kMaxSerialTasks) {
					throw std::runtime_error("serial task update exceeds its bounded traversal work");
				}

				auto& task = *Records[*Head].Task;
				task.Update();
				const auto status = task.State().Status;
				if (status == TaskStatus::Active) {
					break;
				}
				if (status == TaskStatus::Complete && !TakeCompleted) {
					throw std::runtime_error("serial Complete requires an external ownership receiver");
				}

				RemoveHead(status == TaskStatus::Complete);
			}

			if (RetireWhenEmpty && !Head) {
				SetStatus = TaskStatus::Retire;
			}
		}

		void Draw(std::uint32_t argument) {
			const Traversal traversal(*this);
			if (Head) {
				Records[*Head].Task->Draw(argument);
			}
		}

		void Clear() {
			const Traversal traversal(*this);
			std::exception_ptr failure;
			while (Head) {
				const auto previous_head = Head;
				try {
					RemoveHead(false);
				} catch (...) {
					if (!failure) {
						failure = std::current_exception();
					}
					if (Head == previous_head) {
						break;
					}
				}
			}

			if (failure) {
				std::rethrow_exception(failure);
			}
		}
	};

	SerialTaskSet::SerialTaskSet(std::size_t max_tasks, Completed completed) : m_Implementation(std::make_unique<Implementation>(max_tasks, std::move(completed))) {
	}

	SerialTaskSet::~SerialTaskSet() {
		try {
			Clear();
		} catch (...) {
			if (m_Implementation->TaskCount != 0) {
				std::terminate();
			}
			// Release failures are suppressed only after every linked owner has retired.
		}
	}

	SerialTaskHandle SerialTaskSet::Append(std::unique_ptr<SerialTask> task) {
		return m_Implementation->Insert(std::move(task), false);
	}

	SerialTaskHandle SerialTaskSet::Prepend(std::unique_ptr<SerialTask> task) {
		return m_Implementation->Insert(std::move(task), true);
	}

	bool SerialTaskSet::Contains(SerialTaskHandle task) const noexcept {
		return m_Implementation->Contains(task);
	}

	ParallelTaskState SerialTaskSet::State(SerialTaskHandle task) const {
		return m_Implementation->Record(task).Task->State();
	}

	void SerialTaskSet::SetStatus(SerialTaskHandle task, TaskStatus status) {
		m_Implementation->RequireMutation();
		m_Implementation->Record(task).Task->SetStatus(status);
	}

	void SerialTaskSet::SetDrawPaused(SerialTaskHandle task, bool paused) {
		m_Implementation->RequireMutation();
		m_Implementation->Record(task).Task->SetDrawPaused(paused);
	}

	void SerialTaskSet::SetRetireWhenEmpty(bool enabled) {
		m_Implementation->RequireMutation();
		m_Implementation->RetireWhenEmpty = enabled;
	}

	TaskStatus SerialTaskSet::Status() const noexcept {
		return m_Implementation->SetStatus;
	}

	std::size_t SerialTaskSet::Count() const noexcept {
		return m_Implementation->TaskCount;
	}

	std::size_t SerialTaskSet::WriteOrder(std::span<SerialTaskHandle> output) const {
		if (output.size() < Count()) {
			throw std::runtime_error("serial task order output is smaller than its linked list");
		}

		std::size_t count = 0;
		for (auto index = m_Implementation->Head; index; index = m_Implementation->Records[*index].Next) {
			output[count++] = m_Implementation->Handle(*index);
		}

		return count;
	}

	void SerialTaskSet::Update() {
		m_Implementation->Update();
	}

	void SerialTaskSet::Draw(std::uint32_t argument) {
		m_Implementation->Draw(argument);
	}

	bool SerialTaskSet::CanClear() const noexcept {
		const auto& owner = *m_Implementation;
		if (owner.Traversing || owner.Releasing) {
			return false;
		}

		for (auto index = owner.Head; index; index = owner.Records[*index].Next) {
			if (!owner.Records[*index].Task->CanClose()) {
				return false;
			}
		}

		return true;
	}

	void SerialTaskSet::Clear() {
		if (!CanClear()) {
			throw std::runtime_error("serial task clear requires every actual owner retirement preflight");
		}

		m_Implementation->Clear();
	}

} // namespace sgre::scene
