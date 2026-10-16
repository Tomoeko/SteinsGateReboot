#pragma once

#include "sgre/render/frame_host_heap.hpp"
#include "sgre/scene/motion_draw.hpp"
#include "sgre/scene/motion_shader_program.hpp"
#include "sgre/scene/screen_projection_packet.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace sgre::render {

	class SpriteHostInputs {
	public:
		// Actual heap pages own these native format4 bytes until their physical frame recycles.
		// The containing draw fence encloses every returned borrow and the renderer's synchronous copy.
		SpriteHostInputs(FrameHostHeap& heap, std::span<const scene::MotionSpriteVertex> vertices, std::span<const std::uint32_t> indices);
		SpriteHostInputs(FrameHostHeap& heap, std::span<const scene::ScreenProjectionVertex> vertices, std::span<const std::uint32_t> indices);
		[[nodiscard]] scene::MotionVertexLayout Layout() const noexcept;
		SpriteHostInputs(const SpriteHostInputs&) = delete;
		SpriteHostInputs& operator=(const SpriteHostInputs&) = delete;
		[[nodiscard]] std::span<const std::byte> VertexBytes() const;
		[[nodiscard]] std::span<const std::byte> IndexBytes() const;
		[[nodiscard]] std::uint32_t VertexCount() const;
		[[nodiscard]] std::uint32_t IndexCount() const;
		[[nodiscard]] std::uint32_t IndexWordBytes() const;
		[[nodiscard]] bool BelongsTo(const FrameHostHeap& actual_heap) const;

	private:
		void RequireLive() const;
		FrameHostAllocation m_Vertices;
		FrameHostAllocation m_Indices;
		scene::MotionVertexLayout m_Layout = scene::MotionVertexLayout::XyColorUv;
		std::uint32_t m_VertexCount = 0;
		std::uint32_t m_IndexCount = 0;
		std::uint32_t m_IndexWordBytes = 0;
	};

} // namespace sgre::render
