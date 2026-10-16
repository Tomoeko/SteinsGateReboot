#include "sgre/scene/parallel_task_set.hpp"

#include <exception>
#include <limits>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace sgre::scene {

	static constexpr std::size_t kMaxTasks = 65536;
	static constexpr std::size_t kMaxTraversalVisits = 65536;

	static void RequireTaskStatus(TaskStatus status) {
		if (status != TaskStatus::Active && status != TaskStatus::Complete && status != TaskStatus::Retire) {
			throw std::runtime_error("parallel task status is outside the recovered 0..2 range");
		}
	}

	struct ParallelTaskRecord {
		ParallelTaskCallbacks Callbacks;
		ParallelTaskState State;
		std::optional<std::uint32_t> Next;
		std::uint64_t Generation = 1;
		bool Occupied = false;
	};

	class ParallelTaskSet::Implementation {
	public:
		explicit Implementation(std::size_t max_tasks) {
			if (max_tasks == 0 || max_tasks > kMaxTasks) {
				throw std::runtime_error("parallel task set requires a bounded positive capacity");
			}

			Records.resize(max_tasks);
			FreeSlots.reserve(max_tasks);
			for (std::size_t index = max_tasks; index != 0; --index) {
				FreeSlots.push_back(static_cast<std::uint32_t>(index - 1));
			}
		}

		std::vector<ParallelTaskRecord> Records;
		std::vector<std::uint32_t> FreeSlots;
		std::optional<std::uint32_t> Head;
		std::optional<std::uint32_t> Tail;
		TaskStatus SetStatus = TaskStatus::Active;
		std::size_t TaskCount = 0;
		bool RetireWhenEmpty = false;
		bool Traversing = false;
		bool Releasing = false;
		const core::LooperContainingMutation* ContainingMutation = nullptr;

		class Traversal {
		public:
			explicit Traversal(Implementation& owner) : Owner(owner) {
				if (owner.Traversing || owner.Releasing) {
					throw std::runtime_error("parallel task traversal cannot reenter update, draw or retirement");
				}

				Owner.Traversing = true;
			}

			~Traversal() {
				Owner.Traversing = false;
			}

		private:
			Implementation& Owner;
		};

		void RequireDelivery() const {
			if (ContainingMutation != nullptr) {
				throw std::runtime_error("actual parallel task delivery cannot enter a held containing mutation");
			}
		}

		void RequireMutation() const {
			if (Releasing) {
				throw std::runtime_error("parallel task mutation cannot reenter retirement");
			}
		}

		bool Contains(ParallelTaskHandle handle) const noexcept {
			return handle.Owner == this && handle.Index < Records.size() && Records[handle.Index].Occupied && Records[handle.Index].Generation == handle.Generation;
		}

		ParallelTaskRecord& Record(ParallelTaskHandle handle) {
			if (!Contains(handle)) {
				throw std::runtime_error("parallel task handle is stale or belongs to another owner");
			}

			return Records[handle.Index];
		}

		const ParallelTaskRecord& Record(ParallelTaskHandle handle) const {
			if (!Contains(handle)) {
				throw std::runtime_error("parallel task handle is stale or belongs to another owner");
			}

			return Records[handle.Index];
		}

		ParallelTaskHandle Handle(std::uint32_t index) const {
			return {index, Records[index].Generation, this};
		}

		ParallelTaskHandle Insert(ParallelTaskCallbacks callbacks, ParallelTaskState initial, bool prepend) {
			RequireMutation();
			RequireTaskStatus(initial.Status);
			if (prepend && Traversing) {
				throw std::runtime_error("parallel task prepend requires a stable traversal boundary");
			}

			if (FreeSlots.empty()) {
				throw std::runtime_error("parallel task set capacity is exhausted");
			}

			const auto index = FreeSlots.back();
			auto& record = Records[index];
			record.Callbacks = std::move(callbacks);
			record.State = initial;
			record.Occupied = true;
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

		void Unlink(std::uint32_t index) {
			std::optional<std::uint32_t> previous;
			auto* link = &Head;
			while (*link != index) {
				if (!*link) {
					throw std::runtime_error("parallel task registration is absent from its owner list");
				}

				previous = *link;
				link = &Records[**link].Next;
			}

			*link = Records[index].Next;
			if (Tail == index) {
				Tail = previous;
			}

			--TaskCount;
		}

		void Release(std::uint32_t index) {
			auto& record = Records[index];
			std::exception_ptr failure;
			Releasing = true;
			try {
				if (record.Callbacks.Release) {
					record.Callbacks.Release(Handle(index));
				}
			} catch (...) {
				failure = std::current_exception();
			}

			record.Callbacks = {};
			record.Next.reset();
			record.Occupied = false;
			if (record.Generation != std::numeric_limits<std::uint64_t>::max()) {
				++record.Generation;
				FreeSlots.push_back(index);
			}

			Releasing = false;
			if (failure) {
				std::rethrow_exception(failure);
			}
		}

		bool CanReleaseAll() const noexcept {
			try {
				for (auto index = Head; index; index = Records[*index].Next) {
					const auto& callback = Records[*index].Callbacks.CanRelease;
					if (callback && !callback(Handle(*index))) {
						return false;
					}
				}
			} catch (...) {
				return false;
			}

			return true;
		}

		void RequireCanReleaseAll() const {
			if (!CanReleaseAll()) {
				throw std::runtime_error("parallel task retirement requires every actual dependent borrow to be consumed");
			}
		}

		void Clear() {
			const Traversal traversal(*this);
			RequireCanReleaseAll();
			std::exception_ptr failure;
			while (Head) {
				const auto index = *Head;
				Head = Records[index].Next;
				if (!Head) {
					Tail.reset();
				}

				--TaskCount;
				try {
					Release(index);
				} catch (...) {
					if (!failure) {
						failure = std::current_exception();
					}
				}
			}

			if (failure) {
				std::rethrow_exception(failure);
			}
		}

		void Update() {
			RequireDelivery();
			const Traversal traversal(*this);
			std::optional<std::uint32_t> previous;
			auto* link = &Head;
			std::size_t visits = 0;
			while (*link) {
				if (++visits > kMaxTraversalVisits) {
					throw std::runtime_error("parallel task update exceeds its bounded traversal work");
				}

				const auto index = **link;
				auto& record = Records[index];
				if (record.State.Status == TaskStatus::Active && record.Callbacks.Update) {
					record.Callbacks.Update(Handle(index));
				}

				if (record.State.Status == TaskStatus::Retire) {
					RequireCanReleaseAll();
					*link = record.Next;
					if (Tail == index) {
						Tail = previous;
					}

					--TaskCount;
					Release(index);
				} else {
					previous = index;
					link = &record.Next;
				}
			}

			if (RetireWhenEmpty && !Head) {
				SetStatus = TaskStatus::Retire;
			}
		}

		void Draw(std::uint32_t argument) {
			RequireDelivery();
			const Traversal traversal(*this);
			auto index = Head;
			std::size_t visits = 0;
			while (index) {
				if (++visits > kMaxTraversalVisits) {
					throw std::runtime_error("parallel task draw exceeds its bounded traversal work");
				}

				auto& record = Records[*index];
				if (!record.State.DrawPaused && record.State.Status != TaskStatus::Retire && record.Callbacks.Draw) {
					record.Callbacks.Draw(Handle(*index), argument);
				}

				index = record.Next;
			}
		}
	};

	ParallelTaskSet::ParallelTaskSet(std::size_t max_tasks) : m_Implementation(std::make_unique<Implementation>(max_tasks)) {
	}

	ParallelTaskSet::~ParallelTaskSet() {
		if (!CanClear()) {
			std::terminate();
		}

		try {
			Clear();
		} catch (...) {
			// Explicit Clear reports callback failures; destruction still retires every concrete task.
		}
	}

	ParallelTaskHandle ParallelTaskSet::Append(ParallelTaskCallbacks callbacks, ParallelTaskState initial) {
		return m_Implementation->Insert(std::move(callbacks), initial, false);
	}

	ParallelTaskHandle ParallelTaskSet::Prepend(ParallelTaskCallbacks callbacks, ParallelTaskState initial) {
		return m_Implementation->Insert(std::move(callbacks), initial, true);
	}

	ParallelTaskLease::~ParallelTaskLease() {
		if (m_Owned && m_Callbacks.CanRelease) {
			try {
				if (!m_Callbacks.CanRelease(m_PreviousHandle)) {
					std::terminate();
				}
			} catch (...) {
				std::terminate();
			}
		}

		try {
			Release();
		} catch (...) {
			// Explicit Release reports failure; destruction still releases every owned callback.
		}
	}

	ParallelTaskLease::ParallelTaskLease(ParallelTaskLease&& other) noexcept : m_Callbacks(std::exchange(other.m_Callbacks, ParallelTaskCallbacks{})), m_State(other.m_State), m_PreviousHandle(other.m_PreviousHandle), m_Owned(std::exchange(other.m_Owned, false)) {
	}

	ParallelTaskLease& ParallelTaskLease::operator=(ParallelTaskLease&& other) noexcept {
		if (this == &other) {
			return *this;
		}

		try {
			Release();
		} catch (...) {
			if (m_Owned) {
				std::terminate();
			}
			// A completed Release callback failure does not leave an owned borrow.
		}

		m_Callbacks = std::exchange(other.m_Callbacks, ParallelTaskCallbacks{});
		m_State = other.m_State;
		m_PreviousHandle = other.m_PreviousHandle;
		m_Owned = std::exchange(other.m_Owned, false);

		return *this;
	}

	bool ParallelTaskLease::IsEmpty() const noexcept {
		return !m_Owned;
	}

	ParallelTaskState ParallelTaskLease::State() const {
		if (!m_Owned) {
			throw std::runtime_error("parallel task lease no longer owns a detached task");
		}

		return m_State;
	}

	void ParallelTaskLease::Release() {
		if (!m_Owned) {
			return;
		}

		if (m_Callbacks.CanRelease && !m_Callbacks.CanRelease(m_PreviousHandle)) {
			throw std::runtime_error("parallel detached task retirement requires its actual dependent borrows to be consumed");
		}

		m_Owned = false;
		std::exception_ptr failure;
		try {
			if (m_Callbacks.Release) {
				m_Callbacks.Release(m_PreviousHandle);
			}
		} catch (...) {
			failure = std::current_exception();
		}

		m_Callbacks = {};
		if (failure) {
			std::rethrow_exception(failure);
		}
	}

	void ParallelTaskSet::EnterContainingMutation(const core::LooperContainingMutation& actual_scope) {
		RequireIdleTraversal();
		if (m_Implementation->ContainingMutation != nullptr) {
			throw std::runtime_error("parallel containing mutation cannot acquire an already claimed actual list");
		}
		m_Implementation->ContainingMutation = &actual_scope;
	}

	void ParallelTaskSet::RequireContainingMutation(const core::LooperContainingMutation& actual_scope) const {
		RequireIdleTraversal();
		if (m_Implementation->ContainingMutation != &actual_scope) {
			throw std::runtime_error("parallel containing mutation belongs to another actual scope");
		}
	}

	void ParallelTaskSet::LeaveContainingMutation(const core::LooperContainingMutation& actual_scope) {
		RequireContainingMutation(actual_scope);
		m_Implementation->ContainingMutation = nullptr;
	}

	bool ParallelTaskSet::CanExtract(ParallelTaskHandle task) const noexcept {
		try {
			const auto& state = *m_Implementation;
			if (state.Traversing || state.Releasing || !state.Contains(task)) {
				return false;
			}
			const auto& record = state.Record(task);
			if (record.Generation == std::numeric_limits<std::uint64_t>::max()) {
				return false;
			}

			return !record.Callbacks.CanRelease || record.Callbacks.CanRelease(task);
		} catch (...) {
			return false;
		}
	}

	ParallelTaskLease ParallelTaskSet::Extract(ParallelTaskHandle task) {
		if (!CanExtract(task)) {
			throw std::runtime_error("parallel task extraction requires a live reusable registration at a stable boundary");
		}

		auto& record = m_Implementation->Record(task);
		m_Implementation->Unlink(task.Index);
		ParallelTaskLease lease;
		// Moving std::function can retain an inline target; the unlinked record must own no callback resources.
		lease.m_Callbacks = std::exchange(record.Callbacks, ParallelTaskCallbacks{});
		lease.m_State = record.State;
		lease.m_PreviousHandle = task;
		lease.m_Owned = true;
		record.Next.reset();
		record.Occupied = false;
		++record.Generation;
		m_Implementation->FreeSlots.push_back(task.Index);

		return lease;
	}

	bool ParallelTaskSet::CanAppendLease() const noexcept {
		return CanClear() && !m_Implementation->FreeSlots.empty();
	}

	ParallelTaskHandle ParallelTaskSet::AppendLease(ParallelTaskLease&& task) {
		if (task.IsEmpty() || !CanAppendLease()) {
			throw std::runtime_error("parallel task lease append requires ownership and available capacity at a stable boundary");
		}

		const auto handle = m_Implementation->Insert(std::move(task.m_Callbacks), task.m_State, false);
		task.m_Callbacks = {};
		task.m_Owned = false;

		return handle;
	}

	bool ParallelTaskSet::Contains(ParallelTaskHandle task) const noexcept {
		return m_Implementation->Contains(task);
	}

	ParallelTaskState ParallelTaskSet::State(ParallelTaskHandle task) const {
		return m_Implementation->Record(task).State;
	}

	void ParallelTaskSet::SetStatus(ParallelTaskHandle task, TaskStatus status) {
		m_Implementation->RequireMutation();
		RequireTaskStatus(status);
		m_Implementation->Record(task).State.Status = status;
	}

	void ParallelTaskSet::SetDrawPaused(ParallelTaskHandle task, bool paused) {
		m_Implementation->RequireMutation();
		m_Implementation->Record(task).State.DrawPaused = paused;
	}

	void ParallelTaskSet::SetRetireWhenEmpty(bool enabled) {
		m_Implementation->RequireMutation();
		m_Implementation->RetireWhenEmpty = enabled;
	}

	TaskStatus ParallelTaskSet::Status() const noexcept {
		return m_Implementation->SetStatus;
	}

	std::size_t ParallelTaskSet::Count() const noexcept {
		return m_Implementation->TaskCount;
	}

	std::size_t ParallelTaskSet::WriteOrder(std::span<ParallelTaskHandle> output) const {
		if (output.size() < Count()) {
			throw std::runtime_error("parallel task order output is smaller than its live list");
		}

		std::size_t count = 0;
		for (auto index = m_Implementation->Head; index; index = m_Implementation->Records[*index].Next) {
			output[count++] = m_Implementation->Handle(*index);
		}

		return count;
	}

	void ParallelTaskSet::Update() {
		m_Implementation->Update();
	}

	void ParallelTaskSet::Draw(std::uint32_t argument) {
		m_Implementation->Draw(argument);
	}

	void ParallelTaskSet::RequireIdleTraversal() const {
		if (m_Implementation->Traversing || m_Implementation->Releasing) {
			throw std::runtime_error("parallel containing admission requires inactive actual group traversal and release");
		}
	}

	bool ParallelTaskSet::CanClear() const noexcept {
		return !m_Implementation->Traversing && !m_Implementation->Releasing && m_Implementation->CanReleaseAll();
	}

	void ParallelTaskSet::Clear() {
		m_Implementation->Clear();
	}

} // namespace sgre::scene
