#include "sgre/scene/motion_player_commands.hpp"

#include "sgre/scene/motion_clone_resource.hpp"

#include "sgre/scene/motion_timeline_variables.hpp"

#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>
#include <variant>

namespace sgre::scene {

	MotionPlayerCommands::~MotionPlayerCommands() = default;

	const MotionTimelineVariables* MotionPlayerCommands::TimelineVariables() const noexcept {
		return m_TimelineVariables.get();
	}

	const std::shared_ptr<const resource::MotionResource>& MotionPlayerCommands::CachedResource() const noexcept {
		return m_CachedResource;
	}

	bool MotionPlayerCommands::SelectedLayersCleared() const noexcept {
		return m_SelectedLayersCleared;
	}

	static bool HasMotionPlayFlag(MotionPlayFlags flags, MotionPlayFlags option) noexcept {
		return (static_cast<std::uint32_t>(flags) & static_cast<std::uint32_t>(option)) != 0;
	}

	MotionPlayerCommands::MotionPlayerCommands(const MotionResourceManager& manager, std::size_t max_name_bytes)
	    : m_Manager(manager), m_MaxNameBytes(max_name_bytes), m_ParameterValues(*this), m_LayerUids(*this) {
		if (max_name_bytes == 0 || max_name_bytes > kMaximumMotionCommandNameBytes) {
			throw std::runtime_error("motion command name limit must be between 1 and 65536 bytes");
		}
	}

	MotionPlayerCommands::MotionPlayerCommands(const MotionResourceManager& manager, std::function<void(const ResolvedMotionTimeline&, MotionPlayFlags)> install_selection, std::size_t max_name_bytes)
	    : MotionPlayerCommands(manager, max_name_bytes) {
		if (!install_selection) {
			throw std::runtime_error("motion containing commands require actual successful selection installation");
		}

		m_InstallSelection = std::move(install_selection);
	}

	MotionPlayerCommands::MotionPlayerCommands(const MotionResourceManager& manager, std::function<void(const ResolvedMotionTimeline&, MotionPlayFlags)> install_selection, std::function<void()> capture_join, std::size_t max_name_bytes)
	    : MotionPlayerCommands(manager, std::move(install_selection), max_name_bytes) {
		if (!capture_join) {
			throw std::runtime_error("motion JOIN capture requires its actual selected owner");
		}
		m_CaptureJoin = std::move(capture_join);
	}

	MotionPlayerCommands::MotionPlayerCommands(const MotionResourceManager& manager, const MotionPlayerCommands& parent, std::size_t max_name_bytes)
	    : MotionPlayerCommands(manager, max_name_bytes) {
		if (&manager != &parent.m_Manager) {
			throw std::runtime_error("motion child commands require the parent's actual manager");
		}

		m_ParameterValues.BindParent(parent.m_ParameterValues);
		m_LayerUids.BindParent(parent.m_LayerUids);

		m_PreferredProject = parent.m_PreferredProject;
	}

	void MotionPlayerCommands::CopyForClone(const MotionPlayerCommands& source) {
		if (&source == this || m_SelectionSerial != 0 || m_CachedTimeline != nullptr || m_SelectedProject || m_TimelineVariables || !m_Names.CurrentCharacter.empty() || !m_Names.RequestedCharacter.empty() || !m_Names.CurrentMotion.empty() || !m_Names.RequestedMotion.empty() || !m_Names.DeferredStealthCharacter.empty() || !m_Names.DeferredStealthMotion.empty() || !m_Names.DelayedMotion.empty()) {
			throw std::runtime_error("motion command clone requires a fresh destination with its actual parent/resource owners");
		}
		auto selection = source.m_SelectedProject;
		if (selection) {
			selection->Project = ResolveMotionCloneProject(m_Manager, source.m_Manager, selection->Project);
		}
		MotionPlayerCommandNames names;
		names.CurrentCharacter = ReadName(source.m_Names.CurrentCharacter);
		names.RequestedCharacter = ReadName(source.m_Names.RequestedCharacter);
		names.DeferredStealthCharacter = ReadName(source.m_Names.DeferredStealthCharacter);
		// Native510cab copies a kind-index vector, deliberately leaving destination+90 unchanged.
		names.CurrentMotion = m_Names.CurrentMotion;
		names.RequestedMotion = ReadName(source.m_Names.RequestedMotion);
		names.DeferredStealthMotion = ReadName(source.m_Names.DeferredStealthMotion);
		names.DelayedMotion = ReadName(source.m_Names.DelayedMotion);
		names.DelayedFlags = source.m_Names.DelayedFlags;
		std::unique_ptr<MotionTimelineVariables> variables;
		if (source.m_TimelineVariables) {
			variables = std::make_unique<MotionTimelineVariables>(*this, *source.m_TimelineVariables);
		}
		m_ParameterValues.CopyStoredForClone(source.m_ParameterValues);
		m_Names = std::move(names);
		m_Clock = source.m_Clock;
		m_SelectedProject = selection;
		m_PreferredProject = source.m_PreferredProject;
		m_CachedTimeline = source.m_CachedTimeline;
		m_CachedResource = source.m_CachedResource;
		m_SelectedLayersCleared = source.m_SelectedLayersCleared;
		m_SelectionSerial = source.m_SelectionSerial;
		m_MeshDivisionRatio = source.m_MeshDivisionRatio;
		m_TimelineVariables = std::move(variables);
	}

	std::string MotionPlayerCommands::ReadName(std::string_view name) const {
		name = name.substr(0, name.size() > m_MaxNameBytes ? m_MaxNameBytes + 1 : name.size());
		name = name.substr(0, name.find('\0'));
		if (name.size() > m_MaxNameBytes) {
			throw std::runtime_error("motion command name exceeds its byte limit");
		}

		return std::string(name);
	}

	void MotionPlayerCommands::AssignCharacter(std::string_view character, MotionCharacterMode mode) {
		const bool stealth = mode == MotionCharacterMode::Stealth;
		const auto& comparison = stealth ? m_Names.CurrentCharacter : m_Names.RequestedCharacter;
		if (comparison == character) {
			return;
		}

		m_Names.CurrentCharacter = character;
		if (!stealth) {
			m_Names.RequestedCharacter = character;
		}

		m_Names.CurrentMotion.clear();
		m_Names.RequestedMotion.clear();
		m_Clock.Stop();
	}

	void MotionPlayerCommands::SetCharacter(std::string_view character, MotionCharacterMode mode) {
		if (mode != MotionCharacterMode::Ordinary && mode != MotionCharacterMode::Stealth) {
			throw std::runtime_error("unsupported motion character mode");
		}

		const auto name = ReadName(character);
		if (mode == MotionCharacterMode::Stealth && m_Names.CurrentCharacter.empty()) {
			m_Names.DeferredStealthCharacter = name;

			return;
		}

		AssignCharacter(name, mode);
		if (!m_Names.DeferredStealthCharacter.empty()) {
			AssignCharacter(m_Names.DeferredStealthCharacter, MotionCharacterMode::Stealth);
			m_Names.DeferredStealthCharacter.clear();
		}
	}

	void MotionPlayerCommands::SelectMotion(std::string_view motion, MotionPlayFlags flags) {
		const bool stealth = HasMotionPlayFlag(flags, MotionPlayFlags::Stealth);
		const bool force = HasMotionPlayFlag(flags, MotionPlayFlags::Force);
		const bool as_can = HasMotionPlayFlag(flags, MotionPlayFlags::AsCan);
		const auto& comparison = stealth ? m_Names.CurrentMotion : m_Names.RequestedMotion;
		if (!force && !as_can && comparison == motion) {
			return;
		}

		if (!force && m_Clock.State().Playing && as_can) {
			return;
		}

		// Native51c0a0 does nothing after an explicit tick assignment; ordinary selection still follows.
		if (HasMotionPlayFlag(flags, MotionPlayFlags::Join) && !m_Clock.State().TickAssigned) {
			if (!m_CaptureJoin) {
				throw std::runtime_error("motion JOIN selection requires its actual outgoing selected snapshot owner");
			}
			m_CaptureJoin();
		}

		const auto selection = m_Manager.ResolveTimeline(m_Names.CurrentCharacter, motion, m_PreferredProject);
		if (!selection) {
			// Native +150 loses its selected root, while +140 and cached timing survive failed lookup.
			m_SelectedProject.reset();
			m_PreferredProject.reset();
			m_Clock.Stop();

			return;
		}

		if (selection->Timeline->Kind != resource::MotionTimelineKind::Frames) {
			throw std::runtime_error("motion division playback requires root angle transfer and child timeline preparation");
		}

		if (m_SelectionSerial == std::numeric_limits<std::uint64_t>::max()) {
			throw std::runtime_error("motion successful preparation owner serial is exhausted");
		}
		auto metadata = m_Manager.RetainMetadata(selection->Project);
		// Validate/copy the new native50e750 records before replacing the actual current owner.
		auto variables = std::make_unique<MotionTimelineVariables>(*this, *selection, m_SelectionSerial + 1);
		const auto continuation = HasMotionPlayFlag(flags, MotionPlayFlags::Chain) ? MotionTimelineContinuation::Chain : MotionTimelineContinuation::Reset;
		m_Clock.ApplyTimeline(*selection->Timeline, continuation);
		++m_SelectionSerial;
		m_SelectedProject = selection;
		m_PreferredProject = selection->Identity;
		m_CachedTimeline = selection->Timeline;
		m_CachedResource = std::move(metadata);
		m_SelectedLayersCleared = false;
		m_TimelineVariables = std::move(variables);
		m_Names.CurrentMotion = motion;
		if (!stealth) {
			m_Names.RequestedMotion = motion;
		}
		if (m_InstallSelection) {
			m_InstallSelection(*selection, flags);
		}
	}

	void MotionPlayerCommands::Play(std::string_view motion, MotionPlayFlags flags) {
		const auto name = ReadName(motion);
		if (HasMotionPlayFlag(flags, MotionPlayFlags::Delay1 | MotionPlayFlags::Delay2)) {
			m_Names.DelayedMotion = name;
			m_Names.DelayedFlags = flags;

			return;
		}

		if (HasMotionPlayFlag(flags, MotionPlayFlags::Stealth) && m_Names.CurrentCharacter.empty()) {
			m_Names.DeferredStealthMotion = name;

			return;
		}

		SelectMotion(name, flags);
		if (!m_Names.DeferredStealthMotion.empty()) {
			SelectMotion(m_Names.DeferredStealthMotion, MotionPlayFlags::Stealth);
			m_Names.DeferredStealthMotion.clear();
		}
	}

	MotionDelayedPlayResult MotionPlayerCommands::ProcessDelayedPlay() {
		MotionDelayedPlayResult result;
		if (m_Names.DelayedMotion.empty()) {
			return result;
		}

		const auto delay_mask = static_cast<std::uint32_t>(MotionPlayFlags::Delay1 | MotionPlayFlags::Delay2);
		const auto flags = static_cast<MotionPlayFlags>(static_cast<std::uint32_t>(m_Names.DelayedFlags) & ~delay_mask);
		Play(m_Names.DelayedMotion, flags);
		m_Names.DelayedMotion.clear();
		result.Consumed = true;
		result.SkipRemainingUpdate = HasMotionPlayFlag(m_Names.DelayedFlags, MotionPlayFlags::Delay2);

		return result;
	}

	void MotionPlayerCommands::Stop() noexcept {
		m_Clock.Stop();
	}

	static bool ReadMotionSyncMarker(const resource::PsbValue& value) {
		if (const auto* boolean = std::get_if<bool>(&value.Data)) {
			return *boolean;
		}
		if (const auto* integer = std::get_if<std::int64_t>(&value.Data)) {
			return *integer != 0;
		}
		if (const auto* real = std::get_if<double>(&value.Data)) {
			return *real != 0;
		}

		throw std::runtime_error("motion sync marker requires its actual boolean or numeric field");
	}

	void MotionPlayerCommands::SkipToSync() {
		const auto& state = m_Clock.State();
		if (!state.Playing || state.LoopTime >= 0) {
			return;
		}

		auto target = state.LastTime;
		if (m_CachedTimeline != nullptr) {
			static constexpr std::size_t kMaximumControlTags = 65536;
			if (m_CachedTimeline->Tags.size() > kMaximumControlTags) {
				throw std::runtime_error("motion sync control exceeds the prepared tag limit");
			}
			for (const auto& tag : m_CachedTimeline->Tags) {
				if (tag.Type != 1 || tag.Time < state.EvaluationTime) {
					continue;
				}
				const auto* sync = tag.Content.Find("sync");
				if (sync == nullptr) {
					throw std::runtime_error("motion sync tag requires its actual sync field");
				}
				if (ReadMotionSyncMarker(*sync)) {
					target = tag.Time;
					break;
				}
			}
		}

		// Native51fd30 scans cached tags before testing its independently retained selected root.
		SetTickCount(target);
	}

	void MotionPlayerCommands::ReleaseSyncWait() noexcept {
		m_Clock.m_State.SyncWaiting = false;
	}

	void MotionPlayerCommands::ResetChildMotion(const std::function<void()>& retire_selected_tree) {
		if (!retire_selected_tree) {
			throw std::runtime_error("motion child reset requires actual selected-tree retirement");
		}

		m_Clock.Stop();
		retire_selected_tree();
		m_Names.CurrentMotion.clear();
		m_Names.RequestedMotion.clear();
	}

	void MotionPlayerCommands::SetTickCount(float ticks) {
		if (m_PreferredProject) {
			m_Clock.AssignTickCount(ticks);
		}
	}

	void MotionPlayerCommands::SetTickLabel(std::string_view label) {
		if (m_PreferredProject) {
			const auto native_label = ReadName(label);
			static_cast<void>(m_Clock.SetTickLabel(native_label));
		}
	}

	void MotionPlayerCommands::SetSpeed(float speed) noexcept {
		m_Clock.SetSpeed(speed);
	}

	void MotionPlayerCommands::RequestInitialBoundaryTraversal() noexcept {
		m_Clock.m_State.InitialBoundaryTraversalPending = true;
	}

	void MotionPlayerCommands::SetMeshDivisionRatio(float ratio) noexcept {
		m_MeshDivisionRatio = ratio;
	}

	float MotionPlayerCommands::MeshDivisionRatio() const noexcept {
		return m_MeshDivisionRatio;
	}

	MotionPlayerParameterValues& MotionPlayerCommands::ParameterValues() noexcept {
		return m_ParameterValues;
	}

	const MotionPlayerParameterValues& MotionPlayerCommands::ParameterValues() const noexcept {
		return m_ParameterValues;
	}

	MotionPlayerLayerUids& MotionPlayerCommands::LayerUids() const noexcept {
		return m_LayerUids.Root();
	}

	std::uint64_t MotionPlayerCommands::SelectionSerial() const noexcept {
		return m_SelectionSerial;
	}

	const MotionResourceManager& MotionPlayerCommands::Manager() const noexcept {
		return m_Manager;
	}

	const MotionPlayerCommandNames& MotionPlayerCommands::Names() const noexcept {
		return m_Names;
	}

	const MotionTimelineClockState& MotionPlayerCommands::ClockState() const noexcept {
		return m_Clock.State();
	}

	const std::optional<ResolvedMotionTimeline>& MotionPlayerCommands::SelectedProject() const noexcept {
		return m_SelectedProject;
	}

	const resource::MotionTimeline* MotionPlayerCommands::CachedTimeline() const noexcept {
		return m_CachedTimeline;
	}

	const std::optional<MotionProjectIdentity>& MotionPlayerCommands::PreferredProject() const noexcept {
		return m_PreferredProject;
	}

} // namespace sgre::scene
