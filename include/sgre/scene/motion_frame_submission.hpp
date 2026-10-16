#pragma once

#include "sgre/scene/motion_mask_plan.hpp"
#include "sgre/scene/motion_masked_blend.hpp"
#include "sgre/scene/motion_quad_batch.hpp"
#include "sgre/scene/motion_scissor.hpp"
#include "sgre/scene/motion_shared_textures.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <variant>

namespace sgre::scene {

	struct MotionPreparedFrameCommand {
		// Scene storage owns the descriptor, geometry, UVs and original corner colors through submission.
		const MotionClipCommand* Clip = nullptr;
		const MotionBounds* IndividualClip = nullptr;
		std::int32_t Texture = 0;
		std::uint32_t BlendMode = 0;
		float BlendParameter = 0;
		std::uint32_t Filter = 0;
		std::uint32_t FilterColor = 0;
		MotionPixelUvRectangle PixelUvs;
		MotionMeshSubdivision Subdivision;
		std::array<std::uint32_t, 4> Colors{};
		std::uint32_t Key = 0;
	};

	struct MotionFrameGroup {
		std::uint64_t Identity = 0;
		std::uint32_t FirstNode = 0;
	};

	struct MotionFrameCandidateBinding {
		const MotionClipCommand* Descriptor = nullptr;
		const MotionPreparedFrameCommand* Command = nullptr;
	};

	struct MotionFrameExternalTexture {
		const MotionSharedTextures* Table = nullptr;
		MotionSharedTextureSlot Slot;
		const NativeTextureReference* Reference = nullptr;
		resource::MotionPoint Dimensions;
	};

	struct MotionPreparedFrame {
		std::span<const MotionPreparedFrameCommand> Queue;
		std::span<const MotionClipCommand> ClipQueue;
		std::span<const MotionFrameGroup> Groups;
		std::span<const MotionClipNode> Nodes;
		std::span<const MotionMaskSettings> MaskSettings;
		std::span<const MotionFrameCandidateBinding> Candidates;
		std::span<const resource::MotionPoint> TextureSizes;
		// Separate native ~slot source domain; empty entries are actual sparse shared-table holes.
		std::span<const MotionFrameExternalTexture> ExternalTextures;
		bool MaskRegionClipping = false;
		MotionBounds InitialScissor;
		MotionBounds AppliedRoundedScissor;
		MotionScissorMatrix View;
		MotionScissorMatrix Model;
		MotionScissorViewport Viewport;
		MotionBounds TargetBounds;
		resource::MotionPoint AxisScale;
	};

	struct MotionFrameBegin {};
	struct MotionFrameEnd {};
	struct MotionFrameBlend {
		std::uint32_t Mode = 0;
		float Parameter = 0;
	};
	struct MotionFrameFilter {
		std::uint32_t Id = 0;
		std::uint32_t Color = 0;
	};
	struct MotionFrameTexture {
		std::int32_t Index = 0;
	};
	struct MotionFrameMaskFinish {};
	struct MotionFrameCandidate {
		const MotionPreparedFrameCommand* Command = nullptr;
	};
	struct MotionFrameScissor {
		MotionScissorUpdate Update;
	};
	struct MotionFrameBlendBounds {
		MotionBounds Bounds;
	};
	using MotionFrameEvent = std::variant<MotionFrameBegin, MotionFrameEnd, MotionFrameBlend, MotionFrameFilter, MotionFrameTexture, MotionMaskOperation, MotionFrameMaskFinish, MotionFrameCandidate, MotionFrameScissor, MotionFrameBlendBounds, MotionBatchSubmission>;

	struct MotionFrameScratch {
		MotionQuadBatchBuffers Batch;
		std::span<MotionMaskOperation> MaskOperations;
		std::span<MotionMaskedBlendPass> BlendPasses;
	};

	// Finite actual row-major grids retain native mode2 strip topology, individual/group clips and mask/blend phases.
	void SubmitPreparedMotionFrame(const MotionPreparedFrame& input, const MotionFrameScratch& scratch, const std::function<void(const MotionFrameEvent&)>& output);

} // namespace sgre::scene
