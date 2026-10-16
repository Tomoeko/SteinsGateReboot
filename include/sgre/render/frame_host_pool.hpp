#pragma once

#include "sgre/core/thread_task_frame.hpp"
#include "sgre/render/frame_host_heap.hpp"
#include "sgre/render/sprite_host_inputs.hpp"
#include "sgre/render/sprite_presentation.hpp"
#include "sgre/render/sprite_render_records.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>

namespace sgre::render {

	class FrameHostPool;

	struct FrameHostPoolLimits {
		FrameHostHeapLimits Heap;
		std::size_t MaximumWriters = 1024;
		std::size_t MaximumRetainedInputs = 4096;
	};

	class FrameHostInputBorrow {
		class Admission {
		public:
			Admission(const Admission&) = default;

		private:
			friend class FrameHostPool;
			friend class FrameHostInputBorrow;
			explicit Admission(core::ThreadTaskReceiver* actual_writer)
			    : Writer(actual_writer) {
			}
			core::ThreadTaskReceiver* Writer;
		};
		friend class FrameHostPool;

	public:
		// The exact input cell and pool enclose this nonmoving borrow until the actual record frame is inactive.
		FrameHostInputBorrow(FrameHostPool& actual_pool, const SpriteHostInputs& actual_input, Admission admission);
		~FrameHostInputBorrow() noexcept;
		FrameHostInputBorrow(const FrameHostInputBorrow&) = delete;
		FrameHostInputBorrow& operator=(const FrameHostInputBorrow&) = delete;
		FrameHostInputBorrow(FrameHostInputBorrow&&) = delete;
		FrameHostInputBorrow& operator=(FrameHostInputBorrow&&) = delete;

	private:
		FrameHostPool* m_Pool = nullptr;
		const SpriteHostInputs* m_Input = nullptr;
	};

	class FrameHostWriterBinding {
	public:
		// Register the concrete callback receiver before queueing work; detach only once its actual phases quiesce.
		FrameHostWriterBinding(FrameHostPool& actual_pool, core::ThreadTaskReceiver& actual_writer);
		~FrameHostWriterBinding() noexcept;
		FrameHostWriterBinding(const FrameHostWriterBinding&) = delete;
		FrameHostWriterBinding& operator=(const FrameHostWriterBinding&) = delete;
		FrameHostWriterBinding(FrameHostWriterBinding&&) = delete;
		FrameHostWriterBinding& operator=(FrameHostWriterBinding&&) = delete;
		// Only that receiver's genuine executing Update may use this allocator connection.
		void EmplaceInputs(std::optional<SpriteHostInputs>& actual_cell, std::optional<FrameHostInputBorrow>& actual_borrow, std::span<const scene::MotionSpriteVertex> vertices, std::span<const std::uint32_t> indices);
		void Close();

	private:
		FrameHostPool* m_Pool = nullptr;
		core::ThreadTaskReceiver& m_Writer;
	};

	class FrameHostPool {
	public:
		// All three actual services enclose this owner. Layer capacity constructs the real heap; no bare count is accepted.
		FrameHostPool(SpritePassPresentation& actual_surface, core::ThreadTaskFrame& actual_workers, SpriteRenderRecords& actual_records, FrameHostPoolLimits limits = {});
		~FrameHostPool() noexcept;
		FrameHostPool(const FrameHostPool&) = delete;
		FrameHostPool& operator=(const FrameHostPool&) = delete;
		FrameHostPool(FrameHostPool&&) = delete;
		FrameHostPool& operator=(FrameHostPool&&) = delete;

		// Recycle only after bound writers and exact retained inputs quiesce; layer metadata must still match.
		void Begin();
		// Before actual encoding/packet/commit, observe only this owner's concrete pending/executing writer rows.
		// Active records and retained inputs are allowed; this supplies neither GPU readiness nor cancellation.
		void RequireCompletedWriters() const;
		// A receiver must attach to this exact enclosing record owner before input-cell publication.
		void RequireRecords(const SpriteRenderRecords& actual_records) const;
		// Preserve native entry-byte publication without asserting GPU completion.
		void End();
		// The real main-thread packet receiver constructs one stable input and commits its exact borrow.
		void EmplaceInputs(std::optional<SpriteHostInputs>& actual_cell, std::optional<FrameHostInputBorrow>& actual_borrow, std::span<const scene::MotionSpriteVertex> vertices, std::span<const std::uint32_t> indices);
		void EmplaceProjectionInputs(std::optional<SpriteHostInputs>& actual_cell, std::optional<FrameHostInputBorrow>& actual_borrow, std::span<const scene::ScreenProjectionVertex> vertices, std::span<const std::uint32_t> indices);
		// Quiescent CPU cleanup requires explicit writer unbinding, not healthy GPU/layer policy after Abort.
		void Close();
		[[nodiscard]] std::uint64_t FrameCounter() const;
		[[nodiscard]] std::size_t PhysicalFrame() const;
		[[nodiscard]] std::size_t RetainedInputs() const;
		[[nodiscard]] std::size_t BoundWriters() const;

	private:
		friend class FrameHostInputBorrow;
		friend class FrameHostWriterBinding;
		void RegisterInput(const SpriteHostInputs& actual_input, core::ThreadTaskReceiver* actual_writer);
		void ReleaseInput(const SpriteHostInputs& actual_input);
		void RegisterWriter(core::ThreadTaskReceiver& actual_writer);
		void ReleaseWriter(core::ThreadTaskReceiver& actual_writer);
		void EmplaceWriterInputs(core::ThreadTaskReceiver& actual_writer, std::optional<SpriteHostInputs>& actual_cell, std::optional<FrameHostInputBorrow>& actual_borrow, std::span<const scene::MotionSpriteVertex> vertices, std::span<const std::uint32_t> indices);
		class Implementation;
		std::unique_ptr<Implementation> m_Implementation;
	};

} // namespace sgre::render
