#pragma once

#include "sgre/scene/layer_camera_draw.hpp"
#include "sgre/scene/raw_layer_owner.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>

namespace sgre::scene {

	struct RawLayerFrameState {
		MotionBounds BaseClip{1, 1, -1, -1};
		LayerCameraParameters Camera;
	};

	class RawLayerFrame {
	public:
		using ReadState = std::function<RawLayerFrameState()>;
		using DrawTexture = std::function<void(RawTextureTask&, const RawTextureDrawPacket&, std::uint32_t)>;
		// Actual Raw owner, renderer and draw service outlive this attachment. No Motion source is substituted.
		// The draw operation consumes this exact task, its mutable texture/blend and unchanged native XY/UV strip.
		RawLayerFrame(RawLayerOwner& owner, LayerCameraDrawScope& renderer, ReadState state, DrawTexture drawing);
		~RawLayerFrame() noexcept;
		RawLayerFrame(const RawLayerFrame&) = delete;
		RawLayerFrame& operator=(const RawLayerFrame&) = delete;
		RawLayerFrame(RawLayerFrame&&) = delete;
		RawLayerFrame& operator=(RawLayerFrame&&) = delete;

		// SQRawTex291090 supplies actual script onProcess; MRawTex's own idle body remains distinct.
		void Update(const std::function<void()>& on_process);
		void Draw(std::uint32_t argument);
		[[nodiscard]] bool CanClose() const noexcept;
		// Detaches adapters without retiring the actual Raw owner or its resources.
		void Close();

	private:
		void RequireOpen() const;
		void RegisterRoot(std::size_t index, ParallelTaskCallbacks callbacks);
		[[nodiscard]] ParallelTaskCallbacks GroupCallbacks(ParallelTaskSet& group);
		RawLayerOwner& m_Owner;
		CommonProcessTasks& m_Process;
		ParallelTaskSet& m_First;
		ParallelTaskSet& m_Second;
		RawTextureTask& m_Texture;
		LayerCameraDrawScope& m_Renderer;
		ReadState m_ReadState;
		DrawTexture m_DrawTexture;
		ParallelTaskSet m_BaseClipChildren{1};
		ParallelTaskSet m_CameraChildren{1};
		std::array<ParallelTaskHandle, 3> m_Roots{};
		std::array<bool, 3> m_RootReleased{};
		bool m_Detaching = false;
		bool m_Closed = false;
	};

} // namespace sgre::scene
