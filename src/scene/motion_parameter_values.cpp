#include "sgre/scene/motion_parameter_values.hpp"

#include "sgre/scene/motion_clone_resource.hpp"

#include "sgre/scene/motion_player_commands.hpp"
#include "sgre/scene/motion_selected_layer_nodes.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <exception>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace sgre::scene {

	static constexpr std::size_t kMaximumParameterValues = 4096;
	static constexpr std::size_t kMaximumParameterRegistrations = 65536;
	static constexpr std::size_t kMaximumParameterParentDepth = 64;
	static constexpr std::size_t kMaximumRetainedParameterKeyBytes = 8 * 1024 * 1024;

	std::int32_t ReadMotionParameterRecordIndex(const resource::PsbValue& value) {
		if (const auto* boolean = std::get_if<bool>(&value.Data)) {
			return *boolean ? 1 : 0;
		}
		if (const auto* integer = std::get_if<std::int64_t>(&value.Data)) {
			return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(*integer));
		}
		if (const auto* real = std::get_if<double>(&value.Data)) {
			if (!std::isfinite(*real) || *real < -2147483648.0 || *real >= 2147483648.0) {
				throw std::runtime_error("motion parameter index is outside the native scalar conversion domain");
			}

			return static_cast<std::int32_t>(*real);
		}

		throw std::runtime_error("motion parameter record binding requires the actual native numeric index or null");
	}

	static std::string_view ParameterIdentifier(std::string_view identifier) {
		if (identifier.size() > kMaximumMotionCommandNameBytes) {
			throw std::runtime_error("motion variable identifier exceeds its native string domain");
		}

		return identifier.substr(0, identifier.find('\0'));
	}

	struct MotionVariableDescriptorName {
		std::string_view Prefix;
		std::string_view Parameter;
	};

	static std::optional<MotionVariableDescriptorName> SplitVariableDescriptor(std::string_view name) {
		// Native51b600 gives the last double-colon precedence over the last slash.
		auto separator = name.rfind("::");
		std::size_t width = 2;
		if (separator == std::string_view::npos) {
			separator = name.rfind('/');
			width = 1;
		}
		if (separator == std::string_view::npos) {
			return std::nullopt;
		}

		return MotionVariableDescriptorName{name.substr(0, separator), name.substr(separator + width)};
	}

	static std::string DescriptorKey(const MotionVariableDescriptorName& name) {
		return std::string(name.Prefix) + "::" + std::string(name.Parameter);
	}

	static std::string_view TrimDescriptorSpaces(std::string_view name) {
		// Native5280a0's byte table contains only ASCII space, not general whitespace.
		const auto first = name.find_first_not_of(' ');
		if (first == std::string_view::npos) {
			return {};
		}

		return name.substr(first, name.find_last_not_of(' ') - first + 1);
	}

	static std::vector<std::string> DescriptorPath(std::string_view name) {
		std::vector<std::string> path;
		name = TrimDescriptorSpaces(name);
		while (!name.empty()) {
			const auto slash = name.find('/');
			path.emplace_back(name.substr(0, slash));
			if (slash == std::string_view::npos) {
				break;
			}
			name = TrimDescriptorSpaces(name.substr(slash + 1));
		}

		return path;
	}

	MotionPlayerParameterValues::MotionPlayerParameterValues(const MotionPlayerCommands& commands)
	    : m_Commands(commands), m_Root(this) {
	}

	void MotionPlayerParameterValues::BindParent(const MotionPlayerParameterValues& parent) {
		if (m_Parent != nullptr || !m_LocalValues.empty() || !m_Descriptors.empty() || m_Selected != nullptr || !m_Parameters.empty() || &m_Commands.Manager() != &parent.Commands().Manager() || parent.m_Depth + 1 >= kMaximumParameterParentDepth || m_ChildOwners != 0 || parent.m_ChildOwners == std::numeric_limits<std::size_t>::max()) {
			throw std::runtime_error("motion parameter parent binding requires the actual fresh containing command constructor");
		}

		m_Parent = &parent;
		m_Root = parent.m_Root;
		m_Depth = parent.m_Depth + 1;
		++parent.m_ChildOwners;
	}

	void MotionPlayerParameterValues::CopyStoredForClone(const MotionPlayerParameterValues& source) {
		if (&source == this || !m_LocalValues.empty() || !m_Descriptors.empty() || m_Selected != nullptr || !m_Parameters.empty() || m_CurrentParameters != nullptr || m_ChildOwners != 0) {
			throw std::runtime_error("motion cloned variable maps require a fresh actual command owner");
		}
		const auto count = source.m_LocalValues.size() + source.m_Descriptors.size();
		if (count > kMaximumParameterValues - m_Root->m_LocalValueEntries) {
			throw std::runtime_error("motion cloned variable maps exceed their actual root entry count");
		}
		RequireKeyBudget(source.m_LocalKeyBytes, 0);
		auto values = source.m_LocalValues;
		std::map<std::string, Descriptor, std::less<>> descriptors;
		for (const auto& [key, source_descriptor] : source.m_Descriptors) {
			Descriptor descriptor;
			descriptor.Path = source_descriptor.Path;
			descriptor.Value = source_descriptor.Value;
			descriptors.emplace(key, std::move(descriptor));
		}
		// Native504cf0 reconstructs descriptor membership against the full copied child tree.
		// No registered parameter cell or target pointer is borrowed from the outgoing player.
		m_LocalValues = std::move(values);
		m_Descriptors = std::move(descriptors);
		m_LocalKeyBytes = source.m_LocalKeyBytes;
		m_Root->m_RetainedKeyBytes += m_LocalKeyBytes;
		m_Root->m_LocalValueEntries += count;
	}

	MotionPlayerParameterValues::~MotionPlayerParameterValues() noexcept {
		if (!m_Parameters.empty() || m_CurrentParameters != nullptr || m_ChildOwners != 0 || m_Selected != nullptr) {
			// Selected parameter registrations retire before their exact command/value ancestors.
			std::terminate();
		}
		m_Root->m_RetainedKeyBytes -= m_LocalKeyBytes;
		m_Root->m_LocalValueEntries -= m_LocalValues.size() + m_Descriptors.size();
		if (m_Parent != nullptr) {
			--m_Parent->m_ChildOwners;
		}
	}

	float MotionPlayerParameterValues::Read(std::string_view identifier) const {
		identifier = ParameterIdentifier(identifier);
		for (const auto* owner = this; owner != nullptr; owner = owner->m_Parent) {
			const auto found = owner->m_LocalValues.find(identifier);
			if (found != owner->m_LocalValues.end()) {
				return found->second;
			}
			if (owner->m_Parent != nullptr) {
				const auto& parent = *owner->m_Parent;
				const auto key_fragment = "::" + std::string(identifier);
				for (const auto& [key, descriptor] : parent.m_Descriptors) {
					// Native51acb0 searches this substring, then tests actual player membership.
					if (key.find(key_fragment) != std::string::npos && std::find(descriptor.Targets.begin(), descriptor.Targets.end(), owner) != descriptor.Targets.end()) {
						parent.RequireDescriptorSelection();
						return descriptor.Value;
					}
				}
			}
		}

		return 0;
	}

	float MotionPlayerParameterValues::ReadLocal(std::string_view identifier) const {
		identifier = ParameterIdentifier(identifier);
		if (const auto name = SplitVariableDescriptor(identifier)) {
			const auto found = m_Descriptors.find(DescriptorKey(*name));
			return found == m_Descriptors.end() ? 0.0f : found->second.Value;
		}
		const auto found = m_LocalValues.find(identifier);

		return found == m_LocalValues.end() ? 0.0f : found->second;
	}

	std::vector<std::string> MotionPlayerParameterValues::RegisteredKeys() const {
		std::vector<std::string> keys;
		keys.reserve(m_Parameters.size());
		for (const auto& [name, parameter] : m_Parameters) {
			if (parameter == nullptr || parameter->m_Owner == nullptr) {
				throw std::runtime_error("motion variable keys require their actual registered parameter owners");
			}
			parameter->m_Owner->RequireSelection();
			keys.push_back(name);
		}

		return keys;
	}

	std::optional<std::array<float, 2>> MotionPlayerParameterValues::RegisteredBounds(std::string_view identifier) const {
		identifier = ParameterIdentifier(identifier);
		float minimum = std::numeric_limits<float>::max();
		float maximum = -std::numeric_limits<float>::max();
		const auto [first, last] = m_Parameters.equal_range(identifier);
		for (auto entry = first; entry != last; ++entry) {
			const auto& parameter = *entry->second;
			if (parameter.m_Owner == nullptr) {
				throw std::runtime_error("motion registered bounds lack their actual selected parameter owner");
			}
			parameter.m_Owner->RequireSelection();
			minimum = std::min(minimum, std::min(parameter.Range.Begin, parameter.Range.End));
			maximum = std::max(maximum, std::max(parameter.Range.Begin, parameter.Range.End));
		}
		if (maximum > minimum) {
			return std::array<float, 2>{minimum, maximum};
		}
		return std::nullopt;
	}

	std::vector<const MotionPlayerParameterValues*> MotionPlayerParameterValues::ResolveTargets(const MotionSelectedLayerNodes& selected, std::span<const std::string> path) {
		std::vector<const MotionPlayerParameterValues*> targets;
		std::vector<std::string_view> window;
		window.reserve(path.size() + 1);
		// Native512490 retains this sliding window between candidates, too.
		for (std::size_t index = 0; index != selected.m_Nodes.size(); ++index) {
			const auto& candidate = selected.m_Nodes[index];
			if (candidate.Prepared->Type != 3 && candidate.Prepared->Type != 4) {
				continue;
			}
			std::optional<std::uint32_t> current = static_cast<std::uint32_t>(index);
			while (current) {
				const auto& node = selected.m_Nodes[*current];
				const auto label = std::string_view(node.Raw->Label);
				window.insert(window.begin(), label.substr(0, label.find('\0')));
				if (window.size() > path.size()) {
					window.pop_back();
				}
				if (window.size() == path.size() && std::equal(window.begin(), window.end(), path.begin())) {
					if (candidate.Prepared->Type == 4) {
						throw std::runtime_error("motion descriptor targets an unrecovered actual particle-player owner");
					}
					targets.push_back(&selected.DescriptorChildValues(static_cast<std::uint32_t>(index)));
					break;
				}
				current = node.Parent;
			}
		}

		return targets;
	}

	void MotionPlayerParameterValues::RequireDescriptorSelection() const {
		if (m_Selected == nullptr || m_Selected->m_CommandSelectionSerial != m_Commands.SelectionSerial() || m_Commands.CachedTimeline() != &m_Selected->m_Timeline || !m_Commands.CachedSelection() || !m_Commands.Manager().Contains(m_Commands.CachedSelection()->Project)) {
			throw std::runtime_error("motion descriptor requires its actual current selected containing owner");
		}
	}

	void MotionPlayerParameterValues::BindSelected(const MotionSelectedLayerNodes& selected) const {
		if (m_Selected != nullptr || &selected.m_Commands != &m_Commands || selected.m_CommandSelectionSerial != m_Commands.SelectionSerial()) {
			throw std::runtime_error("motion descriptor registration requires the exact fresh selected owner");
		}
		std::vector<std::vector<const MotionPlayerParameterValues*>> resolved;
		resolved.reserve(m_Descriptors.size());
		std::size_t references = 0;
		for (const auto& [key, descriptor] : m_Descriptors) {
			static_cast<void>(key);
			auto targets = ResolveTargets(selected, descriptor.Path);
			if (targets.size() > kMaximumParameterRegistrations - references) {
				throw std::runtime_error("motion descriptor target associations exceed the actual root budget");
			}
			references += targets.size();
			resolved.push_back(std::move(targets));
		}
		if (references > kMaximumParameterRegistrations - m_Root->m_DescriptorReferences) {
			throw std::runtime_error("motion descriptor target associations exceed the actual root budget");
		}
		std::size_t index = 0;
		for (const auto& [key, descriptor] : m_Descriptors) {
			static_cast<void>(key);
			descriptor.Targets = std::move(resolved[index++]);
		}
		m_Selected = &selected;
		m_Root->m_DescriptorReferences += references;
	}

	void MotionPlayerParameterValues::UnbindSelected(const MotionSelectedLayerNodes& selected) const noexcept {
		if (m_Selected != &selected) {
			return;
		}
		for (const auto& [key, descriptor] : m_Descriptors) {
			static_cast<void>(key);
			m_Root->m_DescriptorReferences -= descriptor.Targets.size();
			descriptor.Targets.clear();
		}
		m_Selected = nullptr;
	}

	void MotionPlayerParameterValues::AppendRegisteredWrites(std::string_view identifier, float value, ParameterWritePlan& plan) const {
		const auto [first, last] = m_Parameters.equal_range(identifier);
		for (auto entry = first; entry != last; ++entry) {
			if (entry->second == nullptr || entry->second->m_Owner == nullptr) {
				throw std::runtime_error("motion registered parameter lacks its actual selected owner");
			}
			const auto* owner = entry->second->m_Owner;
			owner->RequireSelection();
			if (std::find(plan.Owners.begin(), plan.Owners.end(), owner) == plan.Owners.end()) {
				if (owner->m_ValueSerial == std::numeric_limits<std::uint64_t>::max()) {
					throw std::runtime_error("motion parameter value serial exhausted its actual owner lifetime");
				}
				plan.Owners.push_back(owner);
			}
			if (plan.Writes.size() == kMaximumParameterRegistrations) {
				throw std::runtime_error("motion variable fanout exceeds its bounded real registered cells");
			}
			plan.Writes.push_back({entry->second, EvaluateMotionParameterPosition(entry->second->Range, value)});
		}
	}

	void MotionPlayerParameterValues::CommitWrites(const ParameterWritePlan& plan, std::uint32_t control_flags) noexcept {
		for (const auto& write : plan.Writes) {
			write.Cell->Position = write.Position;
			write.Cell->ControlFlags = control_flags;
		}
		for (const auto* owner : plan.Owners) {
			++owner->m_ValueSerial;
		}
	}

	void MotionPlayerParameterValues::WriteLocal(std::string_view identifier, float value, std::uint32_t control_flags) {
		identifier = ParameterIdentifier(identifier);
		if (!std::isfinite(value)) {
			throw std::runtime_error("motion variable requires a finite value");
		}
		ParameterWritePlan plan;
		if (const auto name = SplitVariableDescriptor(identifier)) {
			const auto key = DescriptorKey(*name);
			auto found = m_Descriptors.find(key);
			if (found == m_Descriptors.end()) {
				Descriptor descriptor;
				descriptor.Path = DescriptorPath(name->Prefix);
				descriptor.Value = value;
				std::size_t key_bytes = key.size() + 1;
				for (const auto& segment : descriptor.Path) {
					key_bytes += segment.size() + 1;
				}
				if (m_Root->m_LocalValueEntries == kMaximumParameterValues) {
					throw std::runtime_error("motion variable maps exceed their bounded actual-root entry count");
				}
				RequireKeyBudget(key_bytes, 0);
				if (m_Selected != nullptr) {
					RequireDescriptorSelection();
					descriptor.Targets = ResolveTargets(*m_Selected, descriptor.Path);
				}
				if (descriptor.Targets.size() > kMaximumParameterRegistrations - m_Root->m_DescriptorReferences) {
					throw std::runtime_error("motion descriptor target associations exceed the actual root budget");
				}
				for (const auto* target : descriptor.Targets) {
					target->AppendRegisteredWrites(name->Parameter, value, plan);
				}
				found = m_Descriptors.emplace(key, std::move(descriptor)).first;
				m_Root->m_DescriptorReferences += found->second.Targets.size();
				m_LocalKeyBytes += key_bytes;
				m_Root->m_RetainedKeyBytes += key_bytes;
				++m_Root->m_LocalValueEntries;
			} else {
				if (m_Selected != nullptr) {
					RequireDescriptorSelection();
				}
				for (const auto* target : found->second.Targets) {
					target->AppendRegisteredWrites(name->Parameter, value, plan);
				}
				found->second.Value = value;
			}
			CommitWrites(plan, control_flags);
			return;
		}
		const bool inserting = m_LocalValues.find(identifier) == m_LocalValues.end();
		const auto key_bytes = inserting ? identifier.size() + 1 : 0;
		if (inserting && m_Root->m_LocalValueEntries == kMaximumParameterValues) {
			throw std::runtime_error("motion local variable maps exceed their bounded actual-root entry count");
		}
		RequireKeyBudget(key_bytes, 0);
		AppendRegisteredWrites(identifier, value, plan);
		m_LocalValues.insert_or_assign(std::string(identifier), value);
		if (inserting) {
			m_LocalKeyBytes += key_bytes;
			m_Root->m_RetainedKeyBytes += key_bytes;
			++m_Root->m_LocalValueEntries;
		}
		CommitWrites(plan, control_flags);
	}

	void MotionPlayerParameterValues::RequireKeyBudget(std::size_t bytes, std::size_t references) const {
		if (bytes > kMaximumRetainedParameterKeyBytes - m_Root->m_RetainedKeyBytes || references > kMaximumParameterRegistrations - m_Root->m_ParameterReferences) {
			throw std::runtime_error("motion parameter keys or references exceed their bounded actual command-root ownership");
		}
	}

	void MotionPlayerParameterValues::Register(MotionParameterCell& parameter) const {
		const auto key_bytes = (parameter.Range.Identifier.size() + 1) * (m_Depth + 2);
		RequireKeyBudget(key_bytes, m_Depth + 1);
		std::vector<const MotionPlayerParameterValues*> registered;
		registered.reserve(m_Depth + 1);
		try {
			for (const auto* owner = this; owner != nullptr; owner = owner->m_Parent) {
				if (owner->m_Parameters.size() == kMaximumParameterRegistrations) {
					throw std::runtime_error("motion parameter registration exceeds its bounded ancestor entry count");
				}
				owner->m_Parameters.emplace(std::string(parameter.Range.Identifier), &parameter);
				registered.push_back(owner);
			}
		} catch (...) {
			for (const auto* owner : registered) {
				const auto [first, last] = owner->m_Parameters.equal_range(parameter.Range.Identifier);
				for (auto entry = first; entry != last; ++entry) {
					if (entry->second == &parameter) {
						owner->m_Parameters.erase(entry);
						break;
					}
				}
			}
			throw;
		}
		m_Root->m_RetainedKeyBytes += key_bytes;
		m_Root->m_ParameterReferences += m_Depth + 1;
	}

	void MotionPlayerParameterValues::Unregister(const MotionParameterCell& parameter) const noexcept {
		for (const auto* owner = this; owner != nullptr; owner = owner->m_Parent) {
			const auto [first, last] = owner->m_Parameters.equal_range(parameter.Range.Identifier);
			for (auto entry = first; entry != last; ++entry) {
				if (entry->second == &parameter) {
					owner->m_Parameters.erase(entry);
					break;
				}
			}
		}
		m_Root->m_RetainedKeyBytes -= (parameter.Range.Identifier.size() + 1) * (m_Depth + 2);
		m_Root->m_ParameterReferences -= m_Depth + 1;
	}

	std::size_t MotionPlayerParameterValues::LocalValueCount() const noexcept {
		return m_LocalValues.size();
	}

	std::size_t MotionPlayerParameterValues::RegisteredParameterCount() const noexcept {
		return m_Parameters.size();
	}

	const MotionPlayerCommands& MotionPlayerParameterValues::Commands() const noexcept {
		return m_Commands;
	}

	const MotionTimelineParameters* MotionPlayerParameterValues::CurrentTimelineParameters(const resource::MotionTimeline& timeline) const {
		if (m_Commands.CachedTimeline() != &timeline) {
			throw std::runtime_error("motion timeline parameter requires this actual command-selected timeline");
		}
		if (m_CurrentParameters == nullptr) {
			const auto* definitions = std::get_if<resource::PsbArray>(&timeline.Parameters.Data);
			if (!std::holds_alternative<std::monostate>(timeline.Parameterization.Data) || (definitions != nullptr && !definitions->empty())) {
				throw std::runtime_error("motion timeline parameter requires its genuine registered ordered cell owner");
			}
			if (definitions == nullptr && !std::holds_alternative<std::monostate>(timeline.Parameters.Data)) {
				throw std::runtime_error("motion timeline parameter definitions require the actual array or null metadata");
			}

			return nullptr;
		}
		if (&m_CurrentParameters->Timeline() != &timeline) {
			throw std::runtime_error("motion timeline parameter differs from the current registered parameter owner");
		}

		return m_CurrentParameters;
	}

	const MotionParameterCell* MotionPlayerParameterValues::TimelineParameter(const resource::MotionTimeline& timeline) const {
		const auto* parameters = CurrentTimelineParameters(timeline);

		return parameters == nullptr ? nullptr : parameters->TimelineParameter();
	}

	MotionTimelineParameters::MotionTimelineParameters(const MotionPlayerParameterValues& values, const resource::MotionTimeline& timeline)
	    : m_Values(values), m_Timeline(timeline), m_SelectionSerial(values.Commands().SelectionSerial()) {
		RequireSelection();
		if (values.m_CurrentParameters != nullptr) {
			throw std::runtime_error("motion selected parameter records require retirement of the actual previous registration");
		}

		const auto ranges = ReadMotionParameterRanges(timeline);
		std::size_t key_bytes = 0;
		for (const auto& range : ranges) {
			const auto identifier = ParameterIdentifier(range.Identifier);
			const auto bytes = (identifier.size() + 1) * (values.m_Depth + 2);
			if (bytes > kMaximumRetainedParameterKeyBytes - key_bytes) {
				throw std::runtime_error("motion selected parameter identifiers exceed their bounded owned storage");
			}
			key_bytes += bytes;
		}
		values.RequireKeyBudget(key_bytes, ranges.size() * (values.m_Depth + 1));
		m_EntryCount = ranges.size();
		m_Entries = std::make_unique<MotionParameterCell[]>(m_EntryCount);
		for (std::size_t index = 0; index < m_EntryCount; ++index) {
			const auto& range = ranges[index];
			auto& parameter = m_Entries[index];
			parameter.Range = range;
			// Native 515910 copies the identifier into each stable parameter record before registration.
			parameter.m_Identifier = range.Identifier;
			parameter.Range.Identifier = parameter.m_Identifier;
			parameter.Position = EvaluateMotionParameterPosition(range, values.Read(range.Identifier));
			parameter.m_Owner = this;
		}
		std::size_t registered = 0;
		try {
			for (std::size_t index = 0; index < m_EntryCount; ++index) {
				values.Register(m_Entries[index]);
				++registered;
			}
			if (!std::holds_alternative<std::monostate>(timeline.Parameterization.Data)) {
				const auto index = ReadMotionParameterRecordIndex(timeline.Parameterization);
				if (index < 0 || static_cast<std::size_t>(index) >= m_EntryCount) {
					throw std::runtime_error("motion timeline parameter index exceeds this exact selected player's ordered records");
				}

				// Native51bfbe binds only after the actual records have been registered with their value ancestors.
				m_TimelineParameter = &m_Entries[static_cast<std::size_t>(index)];
			}
		} catch (...) {
			for (std::size_t index = 0; index < registered; ++index) {
				values.Unregister(m_Entries[index]);
			}
			throw;
		}

		values.m_CurrentParameters = this;
	}

	MotionTimelineParameters::MotionTimelineParameters(const MotionPlayerParameterValues& values, const MotionTimelineParameters& source)
	    : MotionTimelineParameters(values, source.m_Timeline) {
		source.RequireSelection();
		RequireMotionCloneSelection(values.Commands(), source.m_Values.Commands());
		if (m_EntryCount != source.m_EntryCount || m_SelectionSerial != source.m_SelectionSerial) {
			throw std::runtime_error("motion copied parameter records require their exact cloned command selection");
		}
		for (std::size_t index = 0; index != m_EntryCount; ++index) {
			m_Entries[index].Position = source.m_Entries[index].Position;
			m_Entries[index].ControlFlags = source.m_Entries[index].ControlFlags;
		}
		m_ValueSerial = source.m_ValueSerial;
	}

	MotionTimelineParameters::~MotionTimelineParameters() noexcept {
		for (std::size_t index = 0; index < m_EntryCount; ++index) {
			m_Values.Unregister(m_Entries[index]);
		}

		if (m_Values.m_CurrentParameters == this) {
			m_Values.m_CurrentParameters = nullptr;
		}
	}

	void MotionTimelineParameters::RequireSelection() const {
		const auto& commands = m_Values.Commands();
		const auto& selection = commands.CachedSelection();
		// Native50fd90 retains these exact cells after selected nodes retire; the command owner retains their metadata.
		const bool cleared_current = commands.SelectedLayersCleared() && commands.CachedResource() && m_Values.m_Selected == nullptr && m_Values.m_CurrentParameters == this;
		if (!selection || commands.SelectionSerial() != m_SelectionSerial || commands.CachedTimeline() != &m_Timeline || selection->Timeline != &m_Timeline || (!cleared_current && !commands.Manager().Contains(selection->Project))) {
			throw std::runtime_error("motion parameter records require their exact live command-selected registered timeline");
		}
	}

	std::span<const MotionParameterCell> MotionTimelineParameters::Entries() const {
		RequireSelection();

		return std::span<const MotionParameterCell>(m_Entries.get(), m_EntryCount);
	}

	const MotionParameterCell* MotionTimelineParameters::TimelineParameter() const {
		RequireSelection();
		if (m_Values.m_CurrentParameters != this) {
			throw std::runtime_error("motion timeline parameter requires its exact current registration lifetime");
		}

		return m_TimelineParameter;
	}

	std::uint64_t MotionTimelineParameters::ValueSerial() const {
		RequireSelection();

		return m_ValueSerial;
	}

	void MotionTimelineParameters::CompleteClearedFrame() const {
		if (m_Values.m_Selected != nullptr || m_Values.m_CurrentParameters != this) {
			throw std::runtime_error("motion cleared parameter acknowledgement requires its actual retained records without a selected tree");
		}
		CompleteFrame();
	}

	void MotionTimelineParameters::CompleteFrame() const {
		RequireSelection();
		for (std::size_t index = 0; index < m_EntryCount; ++index) {
			m_Entries[index].ControlFlags = 0;
		}
	}

	const resource::MotionTimeline& MotionTimelineParameters::Timeline() const {
		RequireSelection();

		return m_Timeline;
	}

} // namespace sgre::scene
