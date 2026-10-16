#pragma once

#include "sgre/scene/motion_layer_uids.hpp"
#include "sgre/scene/motion_parameter_values.hpp"
#include "sgre/scene/motion_resources.hpp"
#include "sgre/scene/motion_timeline_clock.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace sgre::scene {
	class MotionTimelineVariables;

	inline constexpr std::size_t kMaximumMotionCommandNameBytes = 65536;

	enum class MotionCharacterMode {
		Ordinary,
		Stealth
	};

	enum class MotionPlayFlags : std::uint32_t {
		None = 0,
		Force = 1,
		Chain = 2,
		AsCan = 4,
		Join = 8,
		Stealth = 16,
		Delay1 = 32,
		Delay2 = 64
	};

	[[nodiscard]] constexpr MotionPlayFlags operator|(MotionPlayFlags left, MotionPlayFlags right) noexcept {
		return static_cast<MotionPlayFlags>(static_cast<std::uint32_t>(left) | static_cast<std::uint32_t>(right));
	}

	struct MotionPlayerCommandNames {
		std::string CurrentCharacter;
		std::string RequestedCharacter;
		std::string DeferredStealthCharacter;
		std::string CurrentMotion;
		std::string RequestedMotion;
		std::string DeferredStealthMotion;
		std::string DelayedMotion;
		MotionPlayFlags DelayedFlags = MotionPlayFlags::None;
	};

	struct MotionDelayedPlayResult {
		bool Consumed = false;
		bool SkipRemainingUpdate = false;
	};

	class MotionPlayerCommands {
	public:
		// The manager and every selected registration outlive this bounded command phase.
		explicit MotionPlayerCommands(const MotionResourceManager& manager, std::size_t max_name_bytes = kMaximumMotionCommandNameBytes);
		// Full ordinary preparation is notified on success, including Force of an unchanged timeline.
		MotionPlayerCommands(const MotionResourceManager& manager, std::function<void(const ResolvedMotionTimeline&, MotionPlayFlags)> install_selection, std::size_t max_name_bytes = kMaximumMotionCommandNameBytes);
		// Actual containing selected owner captures outgoing JOIN state before lookup mutates this selector.
		MotionPlayerCommands(const MotionResourceManager& manager, std::function<void(const ResolvedMotionTimeline&, MotionPlayFlags)> install_selection, std::function<void()> capture_join, std::size_t max_name_bytes = kMaximumMotionCommandNameBytes);
		// Native kind3 construction copies the preferred root and borrows the exact parent for parameter lookup; the parent outlives this child.
		MotionPlayerCommands(const MotionResourceManager& manager, const MotionPlayerCommands& parent, std::size_t max_name_bytes = kMaximumMotionCommandNameBytes);
		~MotionPlayerCommands();
		MotionPlayerCommands(const MotionPlayerCommands&) = delete;
		MotionPlayerCommands& operator=(const MotionPlayerCommands&) = delete;

		// Native510c00 copies command/map/clock state without replay or selection callbacks; the new selected owner rebinds records afterward.
		// Destination must be fresh with genuine parent links; a changed manager requires the exact same immutable resource registered there.
		void CopyForClone(const MotionPlayerCommands& source);
		void SetCharacter(std::string_view character, MotionCharacterMode mode = MotionCharacterMode::Ordinary);
		void Play(std::string_view motion, MotionPlayFlags flags = MotionPlayFlags::None);
		// This is only the native update-entry command phase; callers must honor Delay2's skip.
		[[nodiscard]] MotionDelayedPlayResult ProcessDelayedPlay();
		void Stop() noexcept;
		void SkipToSync();
		void ReleaseSyncWait() noexcept;
		// Actual selected-node/child/resource retirement is required; snapshot/JOIN owners remain unsupported.
		// Native reset stops first, retires the selected tree, then clears only current/requested motion names.
		void ResetChildMotion(const std::function<void()>& retire_selected_tree);
		void SetTickCount(float ticks);
		void SetTickLabel(std::string_view label);
		// Actual selected-child timing sets this after assigning its initial tick when the parent has no assigned tick.
		// It persists through other controls until the pending native initial-tag phase consumes it.
		void RequestInitialBoundaryTraversal() noexcept;
		void SetSpeed(float speed) noexcept;
		// Actual 51f150 stores the player policy without dirtying numeric or geometry caches.
		void SetMeshDivisionRatio(float ratio) noexcept;
		[[nodiscard]] float MeshDivisionRatio() const noexcept;
		// Maps are owned by these actual commands; selected-kind3 construction links the exact immediate parent.
		[[nodiscard]] MotionPlayerParameterValues& ParameterValues() noexcept;
		[[nodiscard]] const MotionPlayerParameterValues& ParameterValues() const noexcept;
		// Borrowed layer registrations mutate bookkeeping owned by this actual ultimate player.
		[[nodiscard]] MotionPlayerLayerUids& LayerUids() const noexcept;
		// Every successful ordinary preparation, including FORCE of an unchanged pointer, advances this owner serial.
		[[nodiscard]] std::uint64_t SelectionSerial() const noexcept;
		// Actual current-player variable records survive Clear and failed lookup.
		[[nodiscard]] const MotionTimelineVariables* TimelineVariables() const noexcept;
		// The actual manager borrow is shared by child construction; this accessor adds no registration/root.
		[[nodiscard]] const MotionResourceManager& Manager() const noexcept;
		[[nodiscard]] const MotionPlayerCommandNames& Names() const noexcept;
		[[nodiscard]] const MotionTimelineClockState& ClockState() const noexcept;
		[[nodiscard]] const std::optional<ResolvedMotionTimeline>& SelectedProject() const noexcept;
		[[nodiscard]] const std::optional<MotionProjectIdentity>& PreferredProject() const noexcept;
		[[nodiscard]] const resource::MotionTimeline* CachedTimeline() const noexcept;

	private:
		friend class MotionPlayerUpdate;
		friend class MotionSceneEvaluation;
		[[nodiscard]] std::string ReadName(std::string_view name) const;
		void AssignCharacter(std::string_view character, MotionCharacterMode mode);
		void SelectMotion(std::string_view motion, MotionPlayFlags flags);
		const MotionResourceManager& m_Manager;
		std::size_t m_MaxNameBytes;
		MotionPlayerCommandNames m_Names;
		MotionTimelineClock m_Clock;
		MotionPlayerParameterValues m_ParameterValues;
		mutable MotionPlayerLayerUids m_LayerUids;
		std::unique_ptr<MotionTimelineVariables> m_TimelineVariables;
		std::uint64_t m_SelectionSerial = 0;
		float m_MeshDivisionRatio = 1;
		std::optional<ResolvedMotionTimeline> m_SelectedProject;
		std::optional<MotionProjectIdentity> m_PreferredProject;
		const resource::MotionTimeline* m_CachedTimeline = nullptr;
		std::function<void(const ResolvedMotionTimeline&, MotionPlayFlags)> m_InstallSelection;
		std::function<void()> m_CaptureJoin;
	};

} // namespace sgre::scene
