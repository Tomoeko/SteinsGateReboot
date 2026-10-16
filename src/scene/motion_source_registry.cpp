#include "sgre/scene/motion_source_registry.hpp"

#include <algorithm>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace sgre::scene {

	static constexpr std::size_t kMaximumRegistrations = 65536;
	static constexpr std::size_t kMaximumIndexEntries = 1000000;
	static constexpr std::size_t kMaximumNameBytes = 65536;
	static constexpr std::size_t kMaximumAggregateNameBytes = 64 * 1024 * 1024;

	static void ValidateName(std::string_view name, std::string_view context) {
		if (name.size() > kMaximumNameBytes || name.find('\0') != std::string_view::npos) {
			throw std::runtime_error("motion source registry: " + std::string(context) + " exceeds name limit or contains an embedded NUL");
		}
	}

	static void ConsumeEntries(std::size_t count, std::size_t& remaining) {
		if (count > remaining) {
			throw std::runtime_error("motion source registry: aggregate index entry limit exceeded");
		}

		remaining -= count;
	}

	static void ConsumeName(std::string_view name, std::string_view context, std::size_t& remaining) {
		ValidateName(name, context);
		if (name.size() > remaining) {
			throw std::runtime_error("motion source registry: aggregate name byte limit exceeded");
		}

		remaining -= name.size();
	}

	static int CompareAliasKey(std::string_view key, std::string_view source, std::string_view icon) {
		// Native keys concatenate the names; splitting at '/' would change slash-containing names.
		const std::size_t joined_size = source.size() + 1 + icon.size();
		const std::size_t common_size = std::min(key.size(), joined_size);
		for (std::size_t index = 0; index < common_size; ++index) {
			char joined = '/';
			if (index < source.size()) {
				joined = source[index];
			} else if (index > source.size()) {
				joined = icon[index - source.size() - 1];
			}

			const auto key_byte = static_cast<unsigned char>(key[index]);
			const auto joined_byte = static_cast<unsigned char>(joined);
			if (key_byte < joined_byte) {
				return -1;
			}
			if (key_byte > joined_byte) {
				return 1;
			}
		}

		if (key.size() < joined_size) {
			return -1;
		}
		if (key.size() > joined_size) {
			return 1;
		}

		return 0;
	}

	std::vector<MotionSourceAlias> ReadMotionSourceAliases(const resource::PsbDocument& document) {
		const auto* value = document.Root.Find("sourceIconRenameMap");
		if (value == nullptr || std::holds_alternative<std::monostate>(value->Data)) {
			return {};
		}

		const auto* object = std::get_if<resource::PsbObject>(&value->Data);
		if (object == nullptr || object->size() > kMaximumIndexEntries) {
			throw std::runtime_error("motion sourceIconRenameMap: expected a bounded object");
		}

		std::vector<MotionSourceAlias> aliases;
		aliases.reserve(object->size());
		std::size_t remaining_name_bytes = kMaximumAggregateNameBytes;
		for (const auto& [key, target] : *object) {
			const auto* array = std::get_if<resource::PsbArray>(&target.Data);
			if (array == nullptr || array->size() < 2) {
				throw std::runtime_error("motion sourceIconRenameMap: '" + key + "' requires source and icon strings");
			}

			const auto* source = std::get_if<std::string>(&(*array)[0].Data);
			const auto* icon = std::get_if<std::string>(&(*array)[1].Data);
			if (source == nullptr || icon == nullptr) {
				throw std::runtime_error("motion sourceIconRenameMap: '" + key + "' requires source and icon strings");
			}

			ConsumeName(key, "alias key", remaining_name_bytes);
			ConsumeName(*source, "alias source", remaining_name_bytes);
			ConsumeName(*icon, "alias icon", remaining_name_bytes);
			MotionSourceAlias alias;
			alias.Key = key;
			alias.Source = *source;
			alias.Icon = *icon;
			aliases.push_back(std::move(alias));
		}

		return aliases;
	}

	PreparedMotionSourceRegistry::PreparedMotionSourceRegistry(std::span<const MotionSourceRegistration> registrations) {
		if (registrations.size() > kMaximumRegistrations) {
			throw std::runtime_error("motion source registry: registration count limit exceeded");
		}

		std::unordered_set<std::uint64_t> documents;
		documents.reserve(registrations.size());
		std::size_t remaining_entries = kMaximumIndexEntries;
		std::size_t remaining_name_bytes = kMaximumAggregateNameBytes;
		m_Registrations.reserve(registrations.size());
		for (const auto& registration : registrations) {
			if (registration.Project.DocumentInstance == 0 || !documents.insert(registration.Project.DocumentInstance).second) {
				throw std::runtime_error("motion source registry: document instance must be nonzero and registered once");
			}

			ConsumeEntries(registration.Sources.size(), remaining_entries);
			ConsumeEntries(registration.Aliases.size(), remaining_entries);
			RegistrationIndex prepared;
			prepared.Project = registration.Project;
			prepared.Sources.reserve(registration.Sources.size());
			for (const auto& source : registration.Sources) {
				ConsumeName(source.Name, "source", remaining_name_bytes);
				ConsumeEntries(source.Icons.size(), remaining_entries);
				SourceIndex source_index;
				source_index.Source = &source;
				source_index.Icons.reserve(source.Icons.size());
				for (const auto& icon : source.Icons) {
					ConsumeName(icon.Name, "icon", remaining_name_bytes);
					source_index.Icons.push_back(&icon);
				}

				std::sort(source_index.Icons.begin(), source_index.Icons.end(), [](const auto* first, const auto* second) {
					return first->Name < second->Name;
				});
				for (std::size_t index = 1; index < source_index.Icons.size(); ++index) {
					if (source_index.Icons[index - 1]->Name == source_index.Icons[index]->Name) {
						throw std::runtime_error("motion source registry: duplicate icon in source '" + source.Name + "'");
					}
				}

				prepared.Sources.push_back(std::move(source_index));
			}

			std::sort(prepared.Sources.begin(), prepared.Sources.end(), [](const auto& first, const auto& second) {
				return first.Source->Name < second.Source->Name;
			});
			for (std::size_t index = 1; index < prepared.Sources.size(); ++index) {
				if (prepared.Sources[index - 1].Source->Name == prepared.Sources[index].Source->Name) {
					throw std::runtime_error("motion source registry: duplicate source in one document");
				}
			}

			prepared.Aliases.reserve(registration.Aliases.size());
			for (const auto& alias : registration.Aliases) {
				ConsumeName(alias.Key, "alias key", remaining_name_bytes);
				ConsumeName(alias.Source, "alias source", remaining_name_bytes);
				ConsumeName(alias.Icon, "alias icon", remaining_name_bytes);
				prepared.Aliases.push_back(&alias);
			}

			std::sort(prepared.Aliases.begin(), prepared.Aliases.end(), [](const auto* first, const auto* second) {
				return first->Key < second->Key;
			});
			for (std::size_t index = 1; index < prepared.Aliases.size(); ++index) {
				if (prepared.Aliases[index - 1]->Key == prepared.Aliases[index]->Key) {
					throw std::runtime_error("motion source registry: duplicate alias key in one document");
				}
			}

			m_Registrations.push_back(std::move(prepared));
		}
	}

	std::optional<ResolvedMotionIcon> PreparedMotionSourceRegistry::Find(std::size_t registration_index, std::string_view& source, std::string_view& icon, bool project_local) const {
		const auto& registration = m_Registrations[registration_index];
		const std::pair<std::string_view, std::string_view> requested_names(source, icon);
		const auto alias = std::lower_bound(registration.Aliases.begin(), registration.Aliases.end(), requested_names, [](const auto* entry, const auto& requested) {
			return CompareAliasKey(entry->Key, requested.first, requested.second) < 0;
		});
		if (alias != registration.Aliases.end() && CompareAliasKey((*alias)->Key, source, icon) == 0) {
			source = (*alias)->Source;
			icon = (*alias)->Icon;
		}

		const auto source_entry = std::lower_bound(registration.Sources.begin(), registration.Sources.end(), source, [](const auto& entry, std::string_view name) {
			return entry.Source->Name < name;
		});
		if (source_entry == registration.Sources.end() || source_entry->Source->Name != source) {
			return std::nullopt;
		}

		const auto icon_entry = std::lower_bound(source_entry->Icons.begin(), source_entry->Icons.end(), icon, [](const auto* entry, std::string_view name) {
			return entry->Name < name;
		});
		if (icon_entry == source_entry->Icons.end() || (*icon_entry)->Name != icon) {
			return std::nullopt;
		}

		ResolvedMotionIcon resolved;
		resolved.Project = registration.Project;
		resolved.RegistrationIndex = registration_index;
		resolved.Source = source_entry->Source;
		resolved.Icon = *icon_entry;
		resolved.ProjectLocal = project_local;

		return resolved;
	}

	std::optional<ResolvedMotionIcon> PreparedMotionSourceRegistry::Resolve(std::string_view source, std::string_view icon, std::optional<MotionProjectIdentity> requesting_project) const {
		ValidateName(source, "requested source");
		ValidateName(icon, "requested icon");
		if (requesting_project.has_value()) {
			for (std::size_t index = 0; index < m_Registrations.size(); ++index) {
				if (m_Registrations[index].Project == *requesting_project) {
					const auto resolved = Find(index, source, icon, true);
					if (resolved.has_value()) {
						return resolved;
					}
				}
			}
		}

		// Failed local lookups retain rewrites; the fallback can apply the same alias map again.
		for (std::size_t index = 0; index < m_Registrations.size(); ++index) {
			const auto resolved = Find(index, source, icon, false);
			if (resolved.has_value()) {
				return resolved;
			}
		}

		return std::nullopt;
	}

} // namespace sgre::scene
