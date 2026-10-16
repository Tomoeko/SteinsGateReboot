#pragma once

#include "sgre/scene/motion_child_construction.hpp"
#include "sgre/scene/motion_player_update.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string_view>
#include <vector>

namespace sgre::scene {

	class MotionLayerParameterBindings;
	class MotionLocalFrames;
	class MotionLayerComposition;
	enum class MotionChildFrameSelection;

	inline constexpr std::size_t kMaximumMotionChildSlotFrames = 65536;

	struct MotionChildFrameSlotServices {
		// Required actual action-queue producer; this never delivers a script callback during frame preparation.
		std::function<void(std::string_view, std::string_view)> EnqueueAction;
	};

	struct MotionChildFrameSlotState {
		std::array<std::size_t, 2> FrameIndices{};
		std::size_t CurrentSlot = 0;
		bool Initialized = false;
		bool FrameDirty = false;
	};

	class MotionChildFrameSlots {
	public:
		// Actual matching raw/prepared kind3 node, construction state and borrowed action/target strings outlive this owner.
		// This owns decoded-slot cursor/control preparation; child selection/update and geometry remain required enclosing phases.
		MotionChildFrameSlots(const resource::MotionLayer& layer, const PreparedMotionLayer& prepared, float last_time, MotionChildConstructionState& child, MotionChildFrameSlotServices services);
		MotionChildFrameSlots(const resource::MotionLayer& layer, const PreparedMotionLayer& prepared, float last_time, MotionChildConstructionState& child, MotionChildFrameSlotServices services, const MotionLayerParameterBindings& parameters, std::uint32_t layer_index);
		MotionChildFrameSlots(const MotionChildFrameSlots& source, MotionChildConstructionState& child, MotionChildFrameSlotServices services, const MotionLayerParameterBindings& parameters);
		[[nodiscard]] const MotionLayerParameterBindings* ParameterBindings() const noexcept;
		[[nodiscard]] std::uint32_t LayerIndex() const noexcept;
		MotionChildFrameSlots(const MotionChildFrameSlots&) = delete;
		MotionChildFrameSlots& operator=(const MotionChildFrameSlots&) = delete;
		void Select(MotionFrameTraversal traversal, float position);
		// Call only after actual numeric/child/geometry composition succeeds, matching native final dirty acknowledgement.
		void CompleteComposition();
		// The actual exclusive constructor owner remains live independently of its selected frame cursor.
		[[nodiscard]] MotionChildConstructionState& Child() noexcept;
		[[nodiscard]] const MotionChildFrameSlotState& State() const;
		[[nodiscard]] const PreparedMotionFrame& CurrentFrame() const;
		[[nodiscard]] const PreparedMotionFrame& FollowingFrame() const;
		[[nodiscard]] const MotionChildFrameControl& CurrentControl() const;
		[[nodiscard]] const MotionChildFrameControl& FollowingControl() const;
		// True only after this current slot actually attempted Play without installing any first timeline.
		[[nodiscard]] bool HasInitialUnselectedTarget() const;
		// Native510c00 omits CurrentMotion; a later ordinary same-target Play can retain that selected child.
		[[nodiscard]] bool HasClonedSelectedTarget() const;

	private:
		friend class MotionSelectedLayerNodes;
		friend class MotionLocalFrames;
		friend MotionChildFrameSelection SelectMotionChildFrameCommands(MotionChildFrameSlots&, const MotionTimelineClockState&, bool, std::uint32_t, const std::function<void()>&);
		friend MotionChildFrameSelection SelectMotionChildFrameCommands(MotionChildFrameSlots&, const MotionLayerComposition&, const std::function<void()>&);
		void RecordCommandSelection();
		[[nodiscard]] bool MatchesInitialUnselectedTarget() const;
		[[nodiscard]] bool MatchesClonedSelectedOwner() const;
		MotionChildFrameSlots(const resource::MotionLayer& layer, const PreparedMotionLayer& prepared, float last_time, MotionChildConstructionState& child, MotionChildFrameSlotServices services, const MotionLayerParameterBindings* parameters, std::uint32_t layer_index);
		void SelectParameter(MotionFrameTraversal traversal, float ordinary_position);
		void SelectPosition(MotionFrameTraversal traversal, float position, bool parameter, float ordinary_position);
		struct Frame {
			std::optional<MotionChildFrameControl> Control;
			std::string_view Action;
			bool ActionPresent = false;
		};
		void Decode(std::size_t slot, std::size_t frame);
		void PrepareSlot(std::size_t slot);
		void QueueCurrentAction();
		const MotionLayerParameterBindings* m_Parameters = nullptr;
		std::uint32_t m_LayerIndex = 0;
		const resource::MotionLayer& m_Layer;
		const PreparedMotionLayer& m_Prepared;
		float m_LastTime = 0;
		MotionChildConstructionState* m_Child = nullptr;
		MotionChildFrameSlotServices m_Services;
		std::vector<Frame> m_Frames;
		MotionChildFrameSlotState m_State;
		const PreparedMotionFrame* m_InitialUnselectedFrame = nullptr;
		const PreparedMotionFrame* m_ClonedTargetFrame = nullptr;
		const resource::MotionTimeline* m_ClonedTargetTimeline = nullptr;
		std::uint64_t m_ClonedTargetSelectionSerial = 0;
		bool m_ClonedTargetNeedsCommand = false;
		std::array<bool, 2> m_PreparedSlots{};
		bool m_Selecting = false;
		bool m_Published = false;
	};

} // namespace sgre::scene
