#pragma once

#include "sgre/resource/motion.hpp"
#include "sgre/scene/motion_projects.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace sgre::scene {
	class MotionPlayerCommands;
	class MotionPlayerUpdate;
	class MotionLocalFrames;
	enum class MotionFrameTraversal;
	struct PreparedMotionLayer;
	struct PreparedMotionFrame;
	struct MotionLayerEvaluationCache;

	struct MotionTimelineVariableCell {
		std::size_t FrameIndex = 0;
		float Time = 0;
		std::uint32_t Interval = 0;
		bool Hidden = true;
		bool Tween = false;
		float Value = 0;
		std::optional<std::uint32_t> Easing;
	};

	struct MotionTimelineVariableState {
		std::array<MotionTimelineVariableCell, 2> Cells;
		std::size_t CurrentSlot = 0;
		float Value = 0;
		bool Initialized = false;
	};

	// Native50e750's current-player records survive selected-layer Clear and expire on the next successful preparation.
	class MotionTimelineVariables {
	public:
		// A staged record owner becomes active only when the same actual Commands commits that selection serial.
		MotionTimelineVariables(MotionPlayerCommands& commands, const ResolvedMotionTimeline& selection, std::uint64_t serial);
		// Full native clone retains both physical cells/cursor while rebinding the actual command owner.
		MotionTimelineVariables(MotionPlayerCommands& commands, const MotionTimelineVariables& source);
		[[nodiscard]] bool Uses(const MotionPlayerCommands& commands, const resource::MotionTimeline& timeline) const noexcept;
		[[nodiscard]] bool Empty() const noexcept;
		[[nodiscard]] std::size_t Count() const noexcept;
		[[nodiscard]] const MotionTimelineVariableState& State(std::size_t track) const;

	private:
		friend class MotionPlayerCommands;
		friend class MotionPlayerUpdate;
		friend class MotionLocalFrames;
		friend bool EvaluateCachedMotionVariableLayer(const PreparedMotionLayer&, const resource::MotionResource&, float, std::size_t, const PreparedMotionFrame&, const PreparedMotionFrame&, bool, bool, MotionLayerEvaluationCache&, const MotionTimelineVariables&);
		struct Frame {
			float Time = 0;
			std::uint32_t Interval = 0;
			bool Hidden = true;
			bool Tween = false;
			float Value = 0;
			std::optional<std::uint32_t> Easing;
		};
		struct Track {
			std::string Name;
			std::vector<Frame> Frames;
			MotionTimelineVariableState State;
		};
		void RequireLive() const;
		void Select(MotionFrameTraversal traversal, float position);
		void Apply(float position);
		[[nodiscard]] std::uint64_t RequireApplied(float position) const;
		static void Decode(Track& track, std::size_t slot, std::size_t frame);
		MotionPlayerCommands& m_Commands;
		const resource::MotionTimeline& m_Timeline;
		const resource::MotionResource& m_Resource;
		MotionProjectId m_Project;
		std::uint64_t m_SelectionSerial = 0;
		std::vector<Track> m_Tracks;
		bool m_Applied = false;
		float m_AppliedPosition = 0;
		std::uint64_t m_AppliedParameterSerial = 0;
	};
} // namespace sgre::scene
