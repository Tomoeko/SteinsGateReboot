#pragma once

#include "sgre/scene/motion_parameter_range.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sgre::scene {

	class MotionPlayerCommands;
	class MotionTimelineParameters;
	class MotionLayerParameterBindings;
	class MotionLocalFrames;
	class MotionSelectedLayerNodes;

	// Native numeric low32 conversion is shared by actual layer and whole-timeline parameter record binding.
	[[nodiscard]] std::int32_t ReadMotionParameterRecordIndex(const resource::PsbValue& value);

	struct MotionParameterCell {
		MotionParameterCell() = default;
		MotionParameterCell(const MotionParameterCell&) = delete;
		MotionParameterCell& operator=(const MotionParameterCell&) = delete;
		MotionParameterCell(MotionParameterCell&&) = delete;
		MotionParameterCell& operator=(MotionParameterCell&&) = delete;
		MotionParameterRange Range;
		float Position = 0;
		std::uint32_t ControlFlags = 0;

	private:
		friend class MotionPlayerParameterValues;
		friend class MotionTimelineParameters;
		std::string m_Identifier;
		const MotionTimelineParameters* m_Owner = nullptr;
	};

	class MotionPlayerParameterValues {
	public:
		~MotionPlayerParameterValues() noexcept;
		MotionPlayerParameterValues(const MotionPlayerParameterValues&) = delete;
		MotionPlayerParameterValues& operator=(const MotionPlayerParameterValues&) = delete;
		// Native51acb0 resolves local values and containing descriptor controllers through actual child-player membership.
		[[nodiscard]] float Read(std::string_view identifier) const;
		// Native524ef0 reads only this player's local/controller maps; ancestor initialization remains separate.
		[[nodiscard]] float ReadLocal(std::string_view identifier) const;
		// Native519830 enumerates live registered parameter entries in key order, preserving duplicate identifiers.
		[[nodiscard]] std::vector<std::string> RegisteredKeys() const;
		// Native5199e0/50f990: union actual live own/descendant records; a degenerate or absent range has no match.
		[[nodiscard]] std::optional<std::array<float, 2>> RegisteredBounds(std::string_view identifier) const;
		// Native51f4f0 stores plain values or a canonical descriptor controller and updates actual registered targets.
		void WriteLocal(std::string_view identifier, float value, std::uint32_t control_flags);
		[[nodiscard]] std::size_t LocalValueCount() const noexcept;
		[[nodiscard]] std::size_t RegisteredParameterCount() const noexcept;
		[[nodiscard]] const MotionPlayerCommands& Commands() const noexcept;
		// A nonnull timeline scalar requires its exact currently registered owner; no position snapshot is substituted.
		// The actual current record owner is required when this timeline declares parameter records.
		[[nodiscard]] const MotionTimelineParameters* CurrentTimelineParameters(const resource::MotionTimeline& timeline) const;
		[[nodiscard]] const MotionParameterCell* TimelineParameter(const resource::MotionTimeline& timeline) const;

	private:
		friend class MotionPlayerCommands;
		friend class MotionTimelineParameters;
		friend class MotionSelectedLayerNodes;
		struct Descriptor {
			std::vector<std::string> Path;
			float Value = 0;
			mutable std::vector<const MotionPlayerParameterValues*> Targets;
		};
		struct ParameterWrite {
			MotionParameterCell* Cell = nullptr;
			float Position = 0;
		};
		struct ParameterWritePlan {
			std::vector<ParameterWrite> Writes;
			std::vector<const MotionTimelineParameters*> Owners;
		};
		void BindSelected(const MotionSelectedLayerNodes& selected) const;
		void UnbindSelected(const MotionSelectedLayerNodes& selected) const noexcept;
		void RequireDescriptorSelection() const;
		[[nodiscard]] static std::vector<const MotionPlayerParameterValues*> ResolveTargets(const MotionSelectedLayerNodes& selected, std::span<const std::string> path);
		void AppendRegisteredWrites(std::string_view identifier, float value, ParameterWritePlan& plan) const;
		static void CommitWrites(const ParameterWritePlan& plan, std::uint32_t control_flags) noexcept;
		// The actual command constructor owns the maps; its kind3 constructor supplies its exact immediate parent.
		explicit MotionPlayerParameterValues(const MotionPlayerCommands& commands);
		void BindParent(const MotionPlayerParameterValues& parent);
		void CopyStoredForClone(const MotionPlayerParameterValues& source);
		void RequireKeyBudget(std::size_t bytes, std::size_t references) const;
		void Register(MotionParameterCell& parameter) const;
		void Unregister(const MotionParameterCell& parameter) const noexcept;
		const MotionPlayerCommands& m_Commands;
		const MotionPlayerParameterValues* m_Parent = nullptr;
		const MotionPlayerParameterValues* m_Root = nullptr;
		std::map<std::string, float, std::less<>> m_LocalValues;
		std::map<std::string, Descriptor, std::less<>> m_Descriptors;
		mutable const MotionSelectedLayerNodes* m_Selected = nullptr;
		mutable std::size_t m_DescriptorReferences = 0;
		// Child registration changes borrowed-reference bookkeeping, never the ancestor's stored local values.
		mutable std::multimap<std::string, MotionParameterCell*, std::less<>> m_Parameters;
		std::size_t m_Depth = 0;
		std::size_t m_LocalKeyBytes = 0;
		mutable std::size_t m_ChildOwners = 0;
		mutable std::size_t m_RetainedKeyBytes = 0;
		mutable std::size_t m_ParameterReferences = 0;
		mutable std::size_t m_LocalValueEntries = 0;
		mutable const MotionTimelineParameters* m_CurrentParameters = nullptr;
	};

	class MotionTimelineParameters {
	public:
		// Commands and actual value ancestors outlive these cells; a cleared current selection retains its immutable metadata.
		// Active selected trees still require their live manager registration.
		// Record IDs are owned so exact-pointer cleanup remains safe after raw metadata retirement.
		MotionTimelineParameters(const MotionPlayerParameterValues& values, const resource::MotionTimeline& timeline);
		// Native504cf0 rebuilds destination registrations while retaining copied current parameter positions/flags.
		MotionTimelineParameters(const MotionPlayerParameterValues& values, const MotionTimelineParameters& source);
		~MotionTimelineParameters() noexcept;
		MotionTimelineParameters(const MotionTimelineParameters&) = delete;
		MotionTimelineParameters& operator=(const MotionTimelineParameters&) = delete;
		MotionTimelineParameters(MotionTimelineParameters&&) = delete;
		MotionTimelineParameters& operator=(MotionTimelineParameters&&) = delete;
		[[nodiscard]] std::span<const MotionParameterCell> Entries() const;
		[[nodiscard]] const resource::MotionTimeline& Timeline() const;
		[[nodiscard]] std::uint64_t ValueSerial() const;
		// This borrows the same stable owned cell as Entries(), until this actual registration retires.
		[[nodiscard]] const MotionParameterCell* TimelineParameter() const;
		// Native50c550 still acknowledges actual records after composing the separate root of a cleared player.
		void CompleteClearedFrame() const;

	private:
		friend class MotionPlayerParameterValues;
		friend class MotionLayerParameterBindings;
		friend class MotionLocalFrames;
		void RequireSelection() const;
		void CompleteFrame() const;
		const MotionPlayerParameterValues& m_Values;
		const resource::MotionTimeline& m_Timeline;
		std::unique_ptr<MotionParameterCell[]> m_Entries;
		std::size_t m_EntryCount = 0;
		const MotionParameterCell* m_TimelineParameter = nullptr;
		std::uint64_t m_SelectionSerial = 0;
		mutable std::uint64_t m_ValueSerial = 0;
	};

} // namespace sgre::scene
