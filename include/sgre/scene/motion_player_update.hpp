#pragma once

#include "sgre/scene/motion_player_commands.hpp"
#include "sgre/scene/motion_preparation_lease.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string_view>
#include <vector>

namespace sgre::scene {
	class MotionManagerRuntime;

	enum class MotionFrameTraversal {
		Seek,
		Forward,
		Reverse,
		ParameterOnly
	};

	enum class MotionPlayerEventKind {
		Action,
		Sync
	};

	struct MotionPlayerEvent {
		MotionPlayerEventKind Kind = MotionPlayerEventKind::Action;
		// Event strings borrow their immutable resource or supplied layer-event owner through delivery.
		std::string_view Layer;
		std::string_view Action;
	};

	struct MotionPlayerFrameContext {
		const resource::MotionTimeline* Timeline = nullptr;
		MotionTimelineClockState Clock;
	};

	struct MotionPlayerUpdateServices {
		std::function<float()> ReadFrameDelta;
		// Required native frame-cursor/variable/layer phase, after tag traversal; layer actions enter EnqueueAction.
		std::function<void(MotionFrameTraversal, const MotionPlayerFrameContext&)> PrepareFrames;
		std::function<void(const MotionPlayerFrameContext&)> ComposeLayers;
		std::function<void(const MotionPlayerFrameContext&)> PublishDrawPackets;
		std::function<void()> Draw;
		std::function<void(const MotionPlayerEvent&)> DeliverEvent;
	};

	struct MotionPlayerUpdateLimits {
		std::size_t MaxPreparedTimelines = 256;
		std::size_t MaxTags = 65536;
		std::size_t MaxPendingEvents = 4096;
		std::size_t MaxLoopWraps = 65536;
	};

	class MotionPlayerUpdate {
	public:
		// Commands, manager registrations, queued strings and callback services outlive this native task phase.
		// MMotion's synchronous task mode is recovered here; asynchronous frame-service dispatch remains unsupported.
		MotionPlayerUpdate(MotionPlayerCommands& commands, MotionPlayerUpdateServices services, MotionPlayerUpdateLimits limits = {});
		MotionPlayerUpdate(const MotionPlayerUpdate&) = delete;
		MotionPlayerUpdate& operator=(const MotionPlayerUpdate&) = delete;
		// Decode/validate tag metadata before ordinary updates. Delayed play may select only prepared timelines.
		void PrepareTimeline(const resource::MotionTimeline& timeline);
		// Actual selected player replaces its current tag owner; pending raw-string events retain only their real leases.
		void PrepareCurrentTimeline(const resource::MotionTimeline& timeline, MotionPreparationLease owner = {});
		void Update();
		// Native50eb80 runs the same synchronous phases with explicit ticks, without reading the manager clock.
		void Progress(float elapsed_ticks);
		// Native 0x140524910 advances/composes with the parent's scaled delta; its own task phases do not run.
		// Actual inherited parent root/pose binding precedes this phase. Kind-3 admission remains separate.
		void UpdateChild(float parent_delta_ticks);
		// Native 0x140521bf0 prepends each child's queue, reversing child grouping without copying strings.
		// Child and parent event-string owners remain alive through the parent's detached delivery.
		void PrependChildEvents(MotionPlayerUpdate& child);
		void PostUpdate();
		void Draw();
		void EnqueueAction(std::string_view layer, std::string_view action);
		[[nodiscard]] std::size_t PendingEventCount() const noexcept;
		[[nodiscard]] std::size_t TagIndex() const noexcept;

	private:
		friend class MotionScenePlayer;
		friend class MotionSceneEvaluation;
		void CopyForClone(const MotionPlayerUpdate& source, MotionManagerRuntime& runtime);
		// Bound only by the genuine generic scene owner supplying the nested particle phase.
		bool m_HasParticlePhase = false;
		struct PreparedTag {
			float Time = 0;
			std::uint32_t Type = 0;
			bool Align = false;
			bool Sync = false;
			std::string_view Action;
		};
		struct PreparedTimeline {
			const resource::MotionTimeline* Timeline = nullptr;
			MotionPreparationLease Owner;
			std::vector<PreparedTag> Tags;
		};
		void PrepareTimeline(const resource::MotionTimeline& timeline, MotionPreparationLease owner, bool current_only);
		void RetainEventPreparation(const MotionPreparationLease& owner);
		[[nodiscard]] const PreparedTimeline* FindPreparedTimeline() const;
		[[nodiscard]] MotionPlayerFrameContext Context() const;
		[[nodiscard]] bool IsBlocked() const noexcept;
		void AdvanceTimeline();
		void AdvanceParameterizedTimeline(const MotionParameterCell& parameter);
		void TraverseInitialPosition();
		void AdvanceAndCompose(float elapsed_ticks);
		void RunUpdate(std::optional<float> explicit_delta);
		void Traverse(MotionFrameTraversal traversal);
		void ProcessTag(const PreparedTag& tag, bool seek);
		void WrapForward();
		void WrapReverse();
		void EnqueueEvent(MotionPlayerEvent event);
		MotionPlayerCommands& m_Commands;
		MotionPlayerUpdateServices m_Services;
		MotionPlayerUpdateLimits m_Limits;
		std::vector<PreparedTimeline> m_Prepared;
		std::vector<MotionPlayerEvent> m_PendingEvents;
		std::vector<MotionPlayerEvent> m_DeliveringEvents;
		std::vector<MotionPreparationLease> m_PendingPreparationOwners;
		std::vector<MotionPreparationLease> m_DeliveringPreparationOwners;
		std::size_t m_PreparedTagCount = 0;
		std::size_t m_TagIndex = 0;
		bool m_Updating = false;
		bool m_Delivering = false;
	};

} // namespace sgre::scene
