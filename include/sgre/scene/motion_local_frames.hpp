#pragma once

#include "sgre/scene/motion_evaluator.hpp"
#include "sgre/scene/motion_join_state.hpp"
#include "sgre/scene/motion_player_update.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace sgre::scene {
	class MotionSelectedLayerNodes;
	class MotionLayerComposition;
	class MotionLayerParameterBindings;

	struct MotionLocalFrameServices {
		// Source-capable selected nodes bind before seek action and after crossing actions; kind 2 has no binding phase.
		std::function<void(std::uint32_t, const PreparedMotionFrame&, const PreparedMotionFrame&)> BindSource;
		// Includes explicitly empty frame actions. This queues, rather than invokes, script callbacks.
		std::function<void(std::string_view, std::string_view)> EnqueueAction;
	};

	struct MotionLocalFrameState {
		std::size_t CurrentIndex = 0;
		std::size_t CurrentSlot = 0;
		bool Initialized = false;
		bool FrameDirty = false;
	};

	class MotionLocalFrames {
	public:
		// Raw timeline, prepared timeline, resource strings and real services outlive this selected owner.
		MotionLocalFrames(const resource::MotionTimeline& timeline, const PreparedMotionTimeline& prepared, MotionLocalFrameServices services);
		// Tween curves borrow the immutable actual resource, which outlives this selected owner and its samples.
		MotionLocalFrames(const resource::MotionTimeline& timeline, const PreparedMotionTimeline& prepared, const resource::MotionResource& resource, MotionLocalFrameServices services);
		// Exact selected storage binds kind3 controls and kind12 summaries to their exclusive owners.
		MotionLocalFrames(MotionSelectedLayerNodes& selected, const resource::MotionResource& resource, MotionLocalFrameServices services);
		~MotionLocalFrames() noexcept;
		MotionLocalFrames(const MotionLocalFrames&) = delete;
		MotionLocalFrames& operator=(const MotionLocalFrames&) = delete;
		// Restore only native527450 common fields in the incoming current physical slot.
		void RestoreJoin(std::uint32_t layer, const MotionJoinedLayer& saved);
		[[nodiscard]] std::uint32_t CurrentMask(std::uint32_t layer) const;
		void Traverse(MotionFrameTraversal traversal, float position);
		// Performs actual cursor preparation/source/action phases before conditional local evaluation.
		void Select(MotionFrameTraversal traversal, float position);
		[[nodiscard]] bool Evaluate(std::uint32_t layer, bool force);
		[[nodiscard]] const MotionLayerEvaluationCache& Evaluation(std::uint32_t layer) const;
		[[nodiscard]] bool EvaluationChanged(std::uint32_t layer) const;
		[[nodiscard]] const PreparedMotionTimeline& Preparation() const noexcept;
		[[nodiscard]] float Position(std::uint32_t layer) const;
		[[nodiscard]] const MotionLayerParameterBindings* ParameterBindings() const;
		// Required only after composition and its genuine geometry/other selected producer phases complete.
		void CompleteComposition();
		[[nodiscard]] std::span<const MotionLayerSample> Samples() const noexcept;
		[[nodiscard]] const MotionLocalFrameState& State(std::uint32_t layer) const;

	private:
		friend class MotionLayerComposition;
		friend class MotionSceneEvaluation;
		friend class MotionLayerMeshLocalOwner;
		class MotionLayerParameterBindings;
		friend class MotionSelectedLayerSources;
		friend class MotionSelectedMeshGeometry;
		friend class MotionSelectedMeshAncestry;
		void PrepareVariableNumerics();
		void MarkMeshDirty(std::uint32_t layer);
		void RequireSelectedOwner() const;
		void RequireCurrentParameters() const;
		[[nodiscard]] bool ParameterValuesCurrent() const noexcept;
		void RequireFinishPhases() const;
		void CompleteInitialUnselectedChild(std::uint32_t layer);
		void DecodeSlot(std::size_t layer, std::size_t slot, std::size_t frame);
		MotionLocalFrames(const resource::MotionTimeline& timeline, const PreparedMotionTimeline& prepared, const resource::MotionResource* resource, MotionLocalFrameServices services, MotionSelectedLayerNodes* selected = nullptr);
		struct Action {
			std::string_view Text;
			bool Present = false;
		};
		struct Layer {
			std::string_view Label;
			std::vector<Action> Actions;
			MotionLocalFrameState State;
			MotionLayerEvaluationCache Evaluation;
			std::optional<PreparedMotionFrame> JoinedDecoded;
			std::optional<std::size_t> JoinedFrame;
			std::size_t JoinedSlot = 0;
			float Position = 0;
			bool EvaluationChanged = false;
			std::uint64_t InitialUnselectedFinishedSerial = 0;
		};
		void AddLayers(const std::vector<resource::MotionLayer>& layers, std::size_t depth);
		void QueueAction(std::size_t layer);
		void BindSource(std::size_t layer);
		const resource::MotionTimeline& m_Timeline;
		const PreparedMotionTimeline& m_Prepared;
		const resource::MotionResource* m_Resource = nullptr;
		MotionSelectedLayerNodes* m_SelectedOwner = nullptr;
		MotionLocalFrameServices m_Services;
		std::vector<Layer> m_Layers;
		std::vector<MotionLayerSample> m_Samples;
		std::size_t m_FrameCount = 0;
		bool m_Published = false;
		bool m_Traversing = false;
		bool m_Selected = false;
		float m_Position = 0;
		std::uint64_t m_SelectionSerial = 0;
		std::uint64_t m_ParameterValueSerial = 0;
		std::uint64_t m_FinishedSelectionSerial = 0;
		std::uint64_t m_ContainingPreparationSerial = 0;
	};

} // namespace sgre::scene
