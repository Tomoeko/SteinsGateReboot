#include "sgre/render/frame_host_pool.hpp"

#include <algorithm>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

namespace sgre::render {

	static constexpr std::size_t kMaximumPoolWriters = 1024;
	static constexpr std::size_t kMaximumPoolInputs = 16384;

	class FrameHostPool::Implementation {
	public:
		class CopyScope {
		public:
			explicit CopyScope(Implementation& state)
			    : m_State(state) {
				++m_State.ActiveCopies;
			}

			~CopyScope() noexcept {
				const std::lock_guard lock(m_State.Mutex);
				--m_State.ActiveCopies;
			}

		private:
			Implementation& m_State;
		};

		Implementation(SpritePassPresentation& surface, core::ThreadTaskFrame& workers, SpriteRenderRecords& records, std::uint32_t capacity, FrameHostPoolLimits limits)
		    : Surface(surface), Workers(workers), Records(records), Capacity(capacity), Limits(limits), Heap(capacity, limits.Heap) {
			Writers.reserve(limits.MaximumWriters);
			Inputs.reserve(limits.MaximumRetainedInputs);
		}

		void RequireOpen() const {
			if (Closed) {
				throw std::runtime_error("frame host pool has closed");
			}
		}

		void RequireOwner() const {
			RequireOpen();
			if (std::this_thread::get_id() != OwnerThread) {
				throw std::runtime_error("frame host pool requires its actual serialized owner thread");
			}
		}

		void RequireInactiveRecords() const {
			if (Records.IsActive()) {
				throw std::runtime_error("frame host pool mutation requires its actual inactive record frame");
			}
		}

		void RequireWritersQuiescent() const {
			for (const auto* writer : Writers) {
				if (!writer->CanClose()) {
					throw std::runtime_error("frame host pool has an actual pending or executing bound writer");
				}
			}
		}

		void RequireNoInputBorrows() const {
			if (ActiveCopies != 0 || !Inputs.empty()) {
				throw std::runtime_error("frame host pool has actual copying or retained frame inputs");
			}
		}

		void RequireInputSlots(const std::optional<SpriteHostInputs>& cell, const std::optional<FrameHostInputBorrow>& borrow) const {
			RequireOpen();
			if (!FrameOpen || cell || borrow || Inputs.size() == Limits.MaximumRetainedInputs || ActiveCopies == Limits.MaximumRetainedInputs) {
				throw std::runtime_error("frame host input construction requires an open frame, empty exact cells and bounded borrow capacity");
			}
		}

		SpritePassPresentation& Surface;
		core::ThreadTaskFrame& Workers;
		SpriteRenderRecords& Records;
		const std::uint32_t Capacity;
		const FrameHostPoolLimits Limits;
		FrameHostHeap Heap;
		std::vector<core::ThreadTaskReceiver*> Writers;
		std::vector<const SpriteHostInputs*> Inputs;
		mutable std::mutex Mutex;
		const std::thread::id OwnerThread = std::this_thread::get_id();
		std::size_t ActiveCopies = 0;
		bool FrameOpen = false;
		bool Closed = false;
	};

	FrameHostPool::FrameHostPool(SpritePassPresentation& actual_surface, core::ThreadTaskFrame& actual_workers, SpriteRenderRecords& actual_records, FrameHostPoolLimits limits) {
		if (limits.MaximumWriters == 0 || limits.MaximumWriters > kMaximumPoolWriters || limits.MaximumRetainedInputs == 0 || limits.MaximumRetainedInputs > kMaximumPoolInputs) {
			throw std::runtime_error("frame host pool requires bounded positive writer and input storage");
		}
		actual_workers.RequireCanRunFrame();
		actual_surface.RequireRenderer(actual_records.m_Renderer);
		if (actual_records.IsActive()) {
			throw std::runtime_error("frame host pool construction requires its actual inactive record owner");
		}
		const auto capacity = actual_surface.DrawableCapacity();
		m_Implementation = std::make_unique<Implementation>(actual_surface, actual_workers, actual_records, capacity, limits);
	}

	FrameHostPool::~FrameHostPool() noexcept {
		try {
			Close();
		} catch (...) {
			std::terminate();
		}
	}

	void FrameHostPool::RequireRecords(const SpriteRenderRecords& actual_records) const {
		const auto& state = *m_Implementation;
		const std::lock_guard lock(state.Mutex);
		state.RequireOwner();
		if (&actual_records != &state.Records) {
			throw std::runtime_error("frame host pool belongs to another actual record owner");
		}
	}

	void FrameHostPool::RequireCompletedWriters() const {
		const auto& state = *m_Implementation;
		const std::lock_guard lock(state.Mutex);
		state.RequireOwner();
		state.Workers.RequireCanRunFrame();
		state.RequireWritersQuiescent();
	}

	void FrameHostPool::Begin() {
		auto& state = *m_Implementation;
		const std::lock_guard lock(state.Mutex);
		state.RequireOwner();
		state.Workers.RequireCanRunFrame();
		state.RequireInactiveRecords();
		state.RequireWritersQuiescent();
		state.RequireNoInputBorrows();
		if (state.FrameOpen) {
			throw std::runtime_error("frame host pool cannot recycle an unended actual frame");
		}
		if (state.Surface.DrawableCapacity() != state.Capacity) {
			throw std::runtime_error("frame host pool actual layer capacity changed before recycle");
		}

		state.Heap.Begin();
		state.FrameOpen = true;
	}

	void FrameHostPool::End() {
		auto& state = *m_Implementation;
		const std::lock_guard lock(state.Mutex);
		state.RequireOwner();
		state.RequireInactiveRecords();
		state.RequireWritersQuiescent();
		if (!state.FrameOpen || state.ActiveCopies != 0) {
			throw std::runtime_error("frame host pool End requires its actual open frame without copying");
		}

		state.Heap.End();
		state.FrameOpen = false;
	}

	void FrameHostPool::RegisterWriter(core::ThreadTaskReceiver& actual_writer) {
		auto& state = *m_Implementation;
		const std::lock_guard lock(state.Mutex);
		state.RequireOwner();
		state.RequireInactiveRecords();
		if (&actual_writer.FrameOwner() != &state.Workers || !actual_writer.CanClose()) {
			throw std::runtime_error("frame host writer binding requires its exact quiescent actual controller");
		}
		if (state.FrameOpen || state.Writers.size() == state.Limits.MaximumWriters || std::find(state.Writers.begin(), state.Writers.end(), &actual_writer) != state.Writers.end()) {
			throw std::runtime_error("frame host writer binding requires an idle unbound receiver and bounded storage");
		}

		state.Writers.push_back(&actual_writer);
	}

	void FrameHostPool::ReleaseWriter(core::ThreadTaskReceiver& actual_writer) {
		auto& state = *m_Implementation;
		const std::lock_guard lock(state.Mutex);
		state.RequireOwner();
		state.RequireInactiveRecords();
		const auto found = std::find(state.Writers.begin(), state.Writers.end(), &actual_writer);
		if (found == state.Writers.end() || !actual_writer.CanClose()) {
			throw std::runtime_error("frame host writer detachment requires its exact quiescent binding");
		}

		state.Writers.erase(found);
	}

	void FrameHostPool::RegisterInput(const SpriteHostInputs& actual_input, core::ThreadTaskReceiver* actual_writer) {
		if (actual_writer != nullptr) {
			actual_writer->RequireExecutingUpdate();
		}
		auto& state = *m_Implementation;
		const std::lock_guard lock(state.Mutex);
		state.RequireOpen();
		if (actual_writer == nullptr) {
			state.RequireOwner();
		} else if (std::find(state.Writers.begin(), state.Writers.end(), actual_writer) == state.Writers.end()) {
			throw std::runtime_error("frame host input publication requires its exact bound executing writer");
		}
		if (!state.FrameOpen || state.Inputs.size() == state.Limits.MaximumRetainedInputs || std::find(state.Inputs.begin(), state.Inputs.end(), &actual_input) != state.Inputs.end() || !actual_input.BelongsTo(state.Heap)) {
			throw std::runtime_error("frame host input borrow requires its exact live heap allocations, open frame and unique bounded cell");
		}

		state.Inputs.push_back(&actual_input);
	}

	void FrameHostPool::ReleaseInput(const SpriteHostInputs& actual_input) {
		auto& state = *m_Implementation;
		const std::lock_guard lock(state.Mutex);
		state.RequireOwner();
		state.RequireInactiveRecords();
		const auto found = std::find(state.Inputs.begin(), state.Inputs.end(), &actual_input);
		if (found == state.Inputs.end() || !actual_input.BelongsTo(state.Heap)) {
			throw std::runtime_error("frame host input retirement requires its exact retained live allocation");
		}

		state.Inputs.erase(found);
	}

	void FrameHostPool::EmplaceInputs(std::optional<SpriteHostInputs>& actual_cell, std::optional<FrameHostInputBorrow>& actual_borrow, std::span<const scene::MotionSpriteVertex> vertices, std::span<const std::uint32_t> indices) {
		auto& state = *m_Implementation;
		std::unique_lock lock(state.Mutex);
		state.RequireOwner();
		state.RequireInputSlots(actual_cell, actual_borrow);
		Implementation::CopyScope copying(state);
		lock.unlock();
		try {
			actual_cell.emplace(state.Heap, vertices, indices);
			actual_borrow.emplace(*this, *actual_cell, FrameHostInputBorrow::Admission(nullptr));
		} catch (...) {
			// No borrow was published if its construction failed; retain no replacement input.
			actual_cell.reset();
			throw;
		}
	}

	void FrameHostPool::EmplaceProjectionInputs(std::optional<SpriteHostInputs>& actual_cell, std::optional<FrameHostInputBorrow>& actual_borrow, std::span<const scene::ScreenProjectionVertex> vertices, std::span<const std::uint32_t> indices) {
		auto& state = *m_Implementation;
		std::unique_lock lock(state.Mutex);
		state.RequireOwner();
		state.RequireInputSlots(actual_cell, actual_borrow);
		Implementation::CopyScope copying(state);
		lock.unlock();
		try {
			actual_cell.emplace(state.Heap, vertices, indices);
			actual_borrow.emplace(*this, *actual_cell, FrameHostInputBorrow::Admission(nullptr));
		} catch (...) {
			// No borrow was published if its construction failed; retain no replacement input.
			actual_cell.reset();
			throw;
		}
	}

	void FrameHostPool::EmplaceWriterInputs(core::ThreadTaskReceiver& actual_writer, std::optional<SpriteHostInputs>& actual_cell, std::optional<FrameHostInputBorrow>& actual_borrow, std::span<const scene::MotionSpriteVertex> vertices, std::span<const std::uint32_t> indices) {
		actual_writer.RequireExecutingUpdate();
		auto& state = *m_Implementation;
		std::unique_lock lock(state.Mutex);
		state.RequireInputSlots(actual_cell, actual_borrow);
		if (std::find(state.Writers.begin(), state.Writers.end(), &actual_writer) == state.Writers.end()) {
			throw std::runtime_error("actual heap writer has detached its pool binding");
		}
		Implementation::CopyScope copying(state);
		lock.unlock();
		try {
			actual_cell.emplace(state.Heap, vertices, indices);
			actual_borrow.emplace(*this, *actual_cell, FrameHostInputBorrow::Admission(&actual_writer));
		} catch (...) {
			actual_cell.reset();
			throw;
		}
	}

	void FrameHostPool::Close() {
		auto& state = *m_Implementation;
		const std::lock_guard lock(state.Mutex);
		if (state.Closed) {
			return;
		}
		state.RequireOwner();
		state.RequireInactiveRecords();
		state.RequireWritersQuiescent();
		state.RequireNoInputBorrows();
		if (!state.Writers.empty()) {
			throw std::runtime_error("frame host pool actual writer bindings must detach before close");
		}

		// CPU retirement follows actual Abort/borrow cleanup even if the GPU or layer has faulted.
		state.Heap.Close();
		state.FrameOpen = false;
		state.Closed = true;
	}

	std::uint64_t FrameHostPool::FrameCounter() const {
		const auto& state = *m_Implementation;
		const std::lock_guard lock(state.Mutex);
		state.RequireOwner();

		return state.Heap.FrameCounter();
	}

	std::size_t FrameHostPool::PhysicalFrame() const {
		const auto& state = *m_Implementation;
		const std::lock_guard lock(state.Mutex);
		state.RequireOwner();

		return state.Heap.PhysicalFrame();
	}

	std::size_t FrameHostPool::RetainedInputs() const {
		const auto& state = *m_Implementation;
		const std::lock_guard lock(state.Mutex);
		state.RequireOwner();

		return state.Inputs.size();
	}

	std::size_t FrameHostPool::BoundWriters() const {
		const auto& state = *m_Implementation;
		const std::lock_guard lock(state.Mutex);
		state.RequireOwner();

		return state.Writers.size();
	}

	FrameHostInputBorrow::FrameHostInputBorrow(FrameHostPool& actual_pool, const SpriteHostInputs& actual_input, Admission admission)
	    : m_Pool(&actual_pool), m_Input(&actual_input) {
		actual_pool.RegisterInput(actual_input, admission.Writer);
	}

	FrameHostInputBorrow::~FrameHostInputBorrow() noexcept {
		try {
			m_Pool->ReleaseInput(*m_Input);
		} catch (...) {
			std::terminate();
		}
	}

	FrameHostWriterBinding::FrameHostWriterBinding(FrameHostPool& actual_pool, core::ThreadTaskReceiver& actual_writer)
	    : m_Pool(&actual_pool), m_Writer(actual_writer) {
		actual_pool.RegisterWriter(actual_writer);
	}

	FrameHostWriterBinding::~FrameHostWriterBinding() noexcept {
		try {
			Close();
		} catch (...) {
			std::terminate();
		}
	}

	void FrameHostWriterBinding::EmplaceInputs(std::optional<SpriteHostInputs>& actual_cell, std::optional<FrameHostInputBorrow>& actual_borrow, std::span<const scene::MotionSpriteVertex> vertices, std::span<const std::uint32_t> indices) {
		m_Writer.RequireExecutingUpdate();
		if (m_Pool == nullptr) {
			throw std::runtime_error("actual frame host writer has detached its pool");
		}

		m_Pool->EmplaceWriterInputs(m_Writer, actual_cell, actual_borrow, vertices, indices);
	}

	void FrameHostWriterBinding::Close() {
		if (!m_Writer.CanClose()) {
			throw std::runtime_error("frame host writer closure requires its actual quiescent callback receiver");
		}
		if (m_Pool == nullptr) {
			return;
		}

		m_Pool->ReleaseWriter(m_Writer);
		m_Pool = nullptr;
	}

} // namespace sgre::render
