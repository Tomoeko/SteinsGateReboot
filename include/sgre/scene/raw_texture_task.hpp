#pragma once

#include "sgre/scene/mutable_texture_owner.hpp"
#include "sgre/scene/parallel_task_set.hpp"
#include "sgre/scene/raw_texture_packet.hpp"
#include "sgre/scene/serial_task_set.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

namespace sgre::scene {

	class RawImage;

	struct RawTextureTaskState {
		resource::MotionPoint LogicalExtent;
		resource::MotionPoint Coordinate;
		std::uint32_t PackedRgb = 0xffffff00;
		std::uint32_t Opacity = 255;
		std::uint32_t BlendMode = 0;
		ParallelTaskState Task{TaskStatus::Active, true};
		bool Closed = false;
	};

	class RawTextureTask {
	public:
		// The backend outlives this exclusive texture and its embedded process task groups.
		RawTextureTask(MutableTextureBackend& backend, resource::MotionPoint logical_extent, std::string_view color_mode = "8888", std::size_t max_tasks_per_group = 64, SerialTaskSet::Completed completed = {});
		~RawTextureTask();
		RawTextureTask(const RawTextureTask&) = delete;
		RawTextureTask& operator=(const RawTextureTask&) = delete;
		RawTextureTask(RawTextureTask&&) = delete;
		RawTextureTask& operator=(RawTextureTask&&) = delete;

		[[nodiscard]] RawTextureTaskState State() const noexcept;
		[[nodiscard]] MutableTextureOwner& Texture();
		[[nodiscard]] SerialTaskSet& SerialTasks();
		[[nodiscard]] ParallelTaskSet& ParallelTasks();
		void SetCoordinate(resource::MotionPoint coordinate);
		void SetColor(const std::array<float, 3>& rgb);
		void SetOpacity(std::uint32_t opacity);
		void SetFiltering(std::uint32_t minimum, std::uint32_t maximum);
		void SetStatus(TaskStatus status);
		void SetDrawPaused(bool paused);
		void RestoreImage(const RawImage& image);
		void RestoreRgb(std::span<const std::uint8_t> rgb);
		// Native MProcess updates its embedded serial group before the parallel group.
		void Update();
		// Caller owns clipping, pause/status selection, texture binding and physical submission.
		[[nodiscard]] RawTextureDrawPacket PrepareDrawPacket() const;
		// Texture retires before parallel then serial task ownership, even after callback failures.
		[[nodiscard]] bool CanClose() const noexcept;
		void Close();

	private:
		void RequireOpen() const;
		RawTextureTaskState m_State;
		bool m_Updating = false;
		SerialTaskSet m_Serial;
		ParallelTaskSet m_Parallel;
		std::unique_ptr<MutableTextureOwner> m_Texture;
	};

} // namespace sgre::scene
