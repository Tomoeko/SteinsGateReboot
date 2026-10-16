#pragma once

#include "sgre/scene/capture_task.hpp"
#include "sgre/scene/captured_opacity_map.hpp"
#include "sgre/scene/motion_textures.hpp"
#include "sgre/scene/raw_texture_packet.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <thread>

namespace sgre::scene {
	struct LoadedImageAsset;
	struct CapturedTaskState {
		resource::MotionPoint FixedSize;
		resource::MotionPoint Coordinate;
		float Opacity = 255;
		std::uint32_t MinimumFilter = 0;
		std::uint32_t MaximumFilter = 0;
		bool Ready = false;
		bool SourceRetired = false;
		bool Closed = false;
	};
	// Native4fb369..4fb8a3 builds ordinary format2 geometry from the actual retained fixed-view fields.
	[[nodiscard]] RawTextureDrawPacket PrepareCapturedDrawPacket(const CapturedTaskState& actual_state);

	// Native4fac50/4fae20: the physical source owns the backlink; its script wrapper is rooted by SQCaptured.
	class CapturedTask final : public CaptureTaskObserver {
	public:
		using DrawTexture = std::function<void(MutableTextureOwner&, const RawTextureDrawPacket&)>;
		using DrawOpacityMap = std::function<void(MutableTextureOwner&, MotionTextureWrapper::Borrowed, const CapturedOpacityMapDrawPacket&)>;
		explicit CapturedTask(const std::shared_ptr<CaptureTask>& actual_source);
		~CapturedTask() noexcept override;
		CapturedTask(const CapturedTask&) = delete;
		CapturedTask& operator=(const CapturedTask&) = delete;
		[[nodiscard]] CapturedTaskState State() const noexcept;
		void SetOpacity(float opacity);
		// Native borrows the actual Image source0 without another VM ObjectInfo root.
		void SetOpacityMap(const LoadedImageAsset& actual_image, MotionTextureWrapper::Borrowed actual_source, std::int32_t width);
		void ResetOpacityMap();
		void SetOpacityMapSmoothing(bool smoothing);
		void SetOpacityMapRepeat(resource::MotionPoint repeat);
		[[nodiscard]] CapturedOpacityMapState OpacityMapState() const noexcept;
		// Native4fb2d0 ordinary no-opacity-map path. The actual current camera/model remains caller-owned.
		void Draw(const DrawTexture& actual_drawing, const DrawOpacityMap& actual_map_drawing = {});
		[[nodiscard]] bool CanClose() const noexcept;
		void Close();
		void CaptureResized(resource::MotionPoint physical_size) noexcept override;
		void CaptureInvalidated() noexcept override;
		void CaptureCompleted() noexcept override;
		void CaptureRetired() noexcept override;

	private:
		void RequireIdle() const;
		std::weak_ptr<CaptureTask> m_Source;
		CapturedTaskState m_State;
		CapturedOpacityMapState m_OpacityMap;
		std::optional<MotionTextureWrapper::Borrowed> m_MapSource;
		const std::thread::id m_Thread = std::this_thread::get_id();
		bool m_Drawing = false;
	};
} // namespace sgre::scene
