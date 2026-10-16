#pragma once

#include "sgre/scene/motion_evaluator.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace sgre::scene {
	class MotionSelectedLayerNodes;
	struct MotionSelectedLayerNode;

	struct MotionStencilFrameControl {
		bool Enabled = false;
		bool ReverseWrite = false;
		float Threshold = 0;
		float Softness = 0;
		std::optional<std::uint32_t> EasingIndex;
	};

	struct MotionStencilMaskSummary {
		bool Enabled = false;
		bool ReverseWrite = false;
		float Scale = 0;
		float Offset = 0;
	};

	class MotionStencilMaskOwner {
	public:
		// Actual raw/prepared kind12 storage outlives this owner. Native5157e6 starts both slots and summary at zero.
		MotionStencilMaskOwner(const resource::MotionLayer& layer, const PreparedMotionLayer& prepared);
		// The registered immutable resource encloses this owner and its retained per-slot easing indices.
		MotionStencilMaskOwner(const resource::MotionLayer& layer, const PreparedMotionLayer& prepared, const resource::MotionResource& resource);
		// Native clone retains both controls and summary; mask member pointers bind to the destination selected tree later.
		MotionStencilMaskOwner(const MotionStencilMaskOwner& source);
		MotionStencilMaskOwner& operator=(const MotionStencilMaskOwner&) = delete;
		// The genuine enclosing cursor decoder supplies its actual slot and frame index before numeric evaluation.
		// Hidden frames preserve the slot; disabled visible controls retain threshold/softness/reversal.
		void PrepareFrame(std::size_t slot, std::size_t frame_index);
		[[nodiscard]] const MotionStencilFrameControl& Control(std::size_t slot) const;
		// Uses the actual prepared current/following pair and native dirty/force/ratio suppression.
		[[nodiscard]] bool Evaluate(const resource::MotionResource& resource, float position, std::size_t current_slot, bool force, bool frame_dirty);
		[[nodiscard]] const MotionStencilMaskSummary& Summary() const;
		[[nodiscard]] std::size_t EvaluatedFrameIndex() const;
		[[nodiscard]] float EvaluatedPosition() const;
		// Actual510650 uses serialized LayerIndexMap, preserving matching type0/3 order and duplicates.
		// Borrowed node identities survive until this exclusive selected tree is retired; no IDs or rows are fabricated.
		void BindMaskLayers(MotionSelectedLayerNodes& selected);
		[[nodiscard]] std::span<const MotionSelectedLayerNode* const> MaskLayers() const;
		[[nodiscard]] bool MaskLayersBound() const noexcept;

	private:
		MotionStencilMaskOwner(const resource::MotionLayer& layer, const PreparedMotionLayer& prepared, const resource::MotionResource* resource);
		const resource::MotionResource* m_Resource = nullptr;
		const resource::MotionLayer& m_Layer;
		const PreparedMotionLayer& m_Prepared;
		const resource::PsbValue* m_MaskNames = nullptr;
		std::array<MotionStencilFrameControl, 2> m_Controls{};
		std::array<std::optional<std::size_t>, 2> m_FrameIndices{};
		MotionLayerEvaluationCache m_Evaluation;
		MotionStencilMaskSummary m_Summary;
		std::vector<const MotionSelectedLayerNode*> m_MaskLayers;
		bool m_Bound = false;
		bool m_SummaryPublished = false;
		std::size_t m_EvaluatedFrameIndex = 0;
		float m_EvaluatedPosition = 0;
	};

} // namespace sgre::scene
