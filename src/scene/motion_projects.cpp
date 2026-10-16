#include "sgre/scene/motion_projects.hpp"

#include <algorithm>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace sgre::scene {

	struct MotionProjectRecord {
		MotionProjectId Id;
		MotionProjectIdentity Identity;
		std::uint32_t References = 1;
		std::shared_ptr<const resource::MotionResource> Resource;
		std::map<std::string, const resource::MotionObject*, std::less<>> Characters;
	};

	static std::string_view NativeMotionName(std::string_view name) {
		return name.substr(0, name.find('\0'));
	}

	class MotionProjectRegistry::Implementation {
	public:
		explicit Implementation(MotionProjectLimits limits) : Limits(limits) {
			if (limits.MaxProjects == 0 || limits.MaxProjects > 65536 || limits.MaxCharacters == 0 || limits.MaxCharacters > 1000000 || limits.MaxNameBytes == 0 || limits.MaxNameBytes > 64 * 1024 * 1024) {
				throw std::runtime_error("motion project registry limits must be bounded and positive");
			}

			Projects.reserve(limits.MaxProjects);
		}

		MotionProjectLimits Limits;
		std::int32_t NextId = 0;
		std::size_t CharacterCount = 0;
		std::size_t NameBytes = 0;
		std::vector<MotionProjectRecord> Projects;

		auto Find(MotionProjectId project) {
			return std::find_if(Projects.begin(), Projects.end(), [project](const auto& record) {
				return record.Id == project;
			});
		}

		auto Find(MotionProjectId project) const {
			return std::find_if(Projects.begin(), Projects.end(), [project](const auto& record) {
				return record.Id == project;
			});
		}

		const MotionProjectRecord& Require(MotionProjectId project) const {
			const auto found = Find(project);
			if (found == Projects.end()) {
				throw std::runtime_error("motion project registration does not exist");
			}

			return *found;
		}
	};

	MotionProjectRegistry::MotionProjectRegistry(MotionProjectLimits limits) : m_Implementation(std::make_unique<Implementation>(limits)) {
	}

	MotionProjectRegistry::~MotionProjectRegistry() = default;

	MotionProjectId MotionProjectRegistry::Register(MotionProjectIdentity identity, std::shared_ptr<const resource::MotionResource> resource) {
		auto& state = *m_Implementation;
		if (identity.DocumentInstance == 0 || !resource) {
			throw std::runtime_error("motion project registration requires a loaded document and immutable resource");
		}

		for (auto& record : state.Projects) {
			if (record.Identity.DocumentInstance == identity.DocumentInstance) {
				if (record.References == static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max())) {
					throw std::runtime_error("motion project reference count exceeds its signed native range");
				}

				++record.References;

				return record.Id;
			}
		}

		if (state.Projects.size() == state.Limits.MaxProjects || state.NextId == std::numeric_limits<std::int32_t>::max()) {
			throw std::runtime_error("motion project registration capacity or identifier range is exhausted");
		}

		MotionProjectRecord record;
		record.Id.Value = state.NextId;
		record.Identity = identity;
		record.Resource = std::move(resource);
		std::size_t name_bytes = 0;
		for (const auto& object : record.Resource->Objects) {
			const auto name = NativeMotionName(object.Name);
			const auto found = record.Characters.find(name);
			if (found != record.Characters.end()) {
				found->second = &object;
				continue;
			}

			if (record.Characters.size() == state.Limits.MaxCharacters - state.CharacterCount || name.size() > state.Limits.MaxNameBytes - state.NameBytes - name_bytes) {
				throw std::runtime_error("motion project character/name storage exceeds its preparation limit");
			}

			record.Characters.emplace(std::string(name), &object);
			name_bytes += name.size();
		}

		const auto id = record.Id;
		const auto character_count = record.Characters.size();
		state.Projects.push_back(std::move(record));
		state.CharacterCount += character_count;
		state.NameBytes += name_bytes;
		++state.NextId;

		return id;
	}

	bool MotionProjectRegistry::Unregister(MotionProjectId project) {
		auto& state = *m_Implementation;
		const auto found = state.Find(project);
		if (found == state.Projects.end()) {
			return false;
		}

		if (--found->References != 0) {
			return true;
		}

		state.CharacterCount -= found->Characters.size();
		for (const auto& [name, object] : found->Characters) {
			static_cast<void>(object);
			state.NameBytes -= name.size();
		}

		state.Projects.erase(found);

		return true;
	}

	bool MotionProjectRegistry::Contains(MotionProjectId project) const {
		return m_Implementation->Find(project) != m_Implementation->Projects.end();
	}

	std::size_t MotionProjectRegistry::Count() const {
		return m_Implementation->Projects.size();
	}

	std::uint32_t MotionProjectRegistry::ReferenceCount(MotionProjectId project) const {
		return m_Implementation->Require(project).References;
	}

	std::size_t MotionProjectRegistry::WriteRegistrationOrder(std::span<MotionProjectId> output) const {
		const auto& projects = m_Implementation->Projects;
		if (output.size() < projects.size()) {
			throw std::runtime_error("motion project registration order output is too small");
		}

		for (std::size_t index = 0; index < projects.size(); ++index) {
			output[index] = projects[index].Id;
		}

		return projects.size();
	}

	const resource::MotionResource& MotionProjectRegistry::Resource(MotionProjectId project) const {
		return *m_Implementation->Require(project).Resource;
	}

	std::shared_ptr<const resource::MotionResource> MotionProjectRegistry::RetainMetadata(MotionProjectId project) const {
		return m_Implementation->Require(project).Resource;
	}

	MotionProjectIdentity MotionProjectRegistry::Identity(MotionProjectId project) const {
		return m_Implementation->Require(project).Identity;
	}

	std::size_t MotionProjectRegistry::WriteCharacterNames(MotionProjectId project, std::span<std::string_view> output) const {
		const auto found = m_Implementation->Find(project);
		if (found == m_Implementation->Projects.end()) {
			return 0;
		}

		if (output.size() < found->Characters.size()) {
			throw std::runtime_error("motion project character name output is too small");
		}

		std::size_t index = 0;
		for (const auto& [name, object] : found->Characters) {
			static_cast<void>(object);
			output[index++] = name;
		}

		return index;
	}

	const resource::MotionObject* MotionProjectRegistry::FindCharacter(MotionProjectId project, std::string_view name) const {
		const auto found = m_Implementation->Find(project);
		if (found == m_Implementation->Projects.end()) {
			return nullptr;
		}

		const auto character = found->Characters.find(NativeMotionName(name));

		return character == found->Characters.end() ? nullptr : character->second;
	}

	std::optional<ResolvedMotionTimeline> MotionProjectRegistry::ResolveTimeline(std::string_view character, std::string_view timeline, std::optional<MotionProjectIdentity> requesting_project) const {
		character = NativeMotionName(character);
		timeline = NativeMotionName(timeline);
		const bool has_request = requesting_project && requesting_project->DocumentInstance != 0;
		for (std::uint32_t pass = 0; pass < 2; ++pass) {
			if (pass == 0 && !has_request) {
				continue;
			}

			for (const auto& record : m_Implementation->Projects) {
				const bool project_local = has_request && record.Identity == *requesting_project;
				if (project_local != (pass == 0)) {
					continue;
				}

				const auto found_character = record.Characters.find(character);
				if (found_character == record.Characters.end()) {
					continue;
				}

				const auto& timelines = found_character->second->Timelines;
				const auto found_timeline = std::find_if(timelines.begin(), timelines.end(), [timeline](const auto& candidate) {
					return candidate.Name == timeline;
				});
				if (found_timeline == timelines.end()) {
					continue;
				}

				ResolvedMotionTimeline result;
				result.Project = record.Id;
				result.Identity = record.Identity;
				result.Character = found_character->second;
				result.Timeline = &*found_timeline;

				return result;
			}
		}

		return {};
	}

	bool MotionProjectRegistry::RenameCharacter(MotionProjectId project, std::string_view old_name, std::string_view new_name) {
		auto& state = *m_Implementation;
		const auto found = state.Find(project);
		if (found == state.Projects.end()) {
			return false;
		}

		old_name = NativeMotionName(old_name);
		new_name = NativeMotionName(new_name);
		const auto old_character = found->Characters.find(old_name);
		if (old_character == found->Characters.end() || found->Characters.contains(new_name)) {
			return false;
		}

		const auto remaining_bytes = state.NameBytes - old_character->first.size();
		if (new_name.size() > state.Limits.MaxNameBytes - remaining_bytes) {
			throw std::runtime_error("motion project renamed character exceeds the name preparation limit");
		}

		found->Characters.emplace(std::string(new_name), old_character->second);
		found->Characters.erase(old_character);
		state.NameBytes = remaining_bytes + new_name.size();

		return true;
	}

	bool MotionProjectRegistry::RemoveCharacter(MotionProjectId project, std::string_view name) {
		auto& state = *m_Implementation;
		const auto found = state.Find(project);
		if (found == state.Projects.end()) {
			return false;
		}

		const auto character = found->Characters.find(NativeMotionName(name));
		if (character != found->Characters.end()) {
			state.NameBytes -= character->first.size();
			--state.CharacterCount;
			found->Characters.erase(character);
		}

		return true;
	}

} // namespace sgre::scene
