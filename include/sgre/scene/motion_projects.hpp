#pragma once

#include "sgre/scene/motion_source_registry.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>

namespace sgre::scene {

	struct MotionProjectId {
		std::int32_t Value = -1;
		bool operator==(const MotionProjectId&) const = default;
	};

	struct ResolvedMotionTimeline {
		MotionProjectId Project;
		MotionProjectIdentity Identity;
		// Borrows the registry registration; final unregistration invalidates both resource pointers.
		const resource::MotionObject* Character = nullptr;
		const resource::MotionTimeline* Timeline = nullptr;
	};

	struct MotionProjectLimits {
		std::size_t MaxProjects = 256;
		std::size_t MaxCharacters = 1000000;
		std::size_t MaxNameBytes = 8 * 1024 * 1024;
	};

	class MotionProjectRegistry {
	public:
		explicit MotionProjectRegistry(MotionProjectLimits limits = {});
		~MotionProjectRegistry();
		MotionProjectRegistry(const MotionProjectRegistry&) = delete;
		MotionProjectRegistry& operator=(const MotionProjectRegistry&) = delete;
		MotionProjectRegistry(MotionProjectRegistry&&) = delete;
		MotionProjectRegistry& operator=(MotionProjectRegistry&&) = delete;

		// Registration deduplicates the loaded document identity and preserves its first position.
		// The resource must remain immutable. A native script adapter separately roots its PSB owner.
		[[nodiscard]] MotionProjectId Register(MotionProjectIdentity identity, std::shared_ptr<const resource::MotionResource> resource);
		[[nodiscard]] bool Unregister(MotionProjectId project);
		[[nodiscard]] bool Contains(MotionProjectId project) const;
		[[nodiscard]] std::size_t Count() const;
		[[nodiscard]] std::uint32_t ReferenceCount(MotionProjectId project) const;
		[[nodiscard]] std::size_t WriteRegistrationOrder(std::span<MotionProjectId> output) const;
		[[nodiscard]] MotionProjectIdentity Identity(MotionProjectId project) const;
		// Borrow the exact immutable registration without adding a document/PSB retain.
		[[nodiscard]] const resource::MotionResource& Resource(MotionProjectId project) const;
		// Name order is the native case-sensitive object-map order. Missing projects produce no names.
		[[nodiscard]] std::size_t WriteCharacterNames(MotionProjectId project, std::span<std::string_view> output) const;
		// Character and name views borrow this live registration and expire after removal/rename.
		[[nodiscard]] const resource::MotionObject* FindCharacter(MotionProjectId project, std::string_view name) const;
		// Current document/root is searched before other registrations, preserving their native insertion order.
		// Query names cross the native C-string boundary. Lookup neither allocates nor retains resource ownership.
		[[nodiscard]] std::optional<ResolvedMotionTimeline> ResolveTimeline(std::string_view character, std::string_view timeline, std::optional<MotionProjectIdentity> requesting_project = std::nullopt) const;
		[[nodiscard]] bool RenameCharacter(MotionProjectId project, std::string_view old_name, std::string_view new_name);
		// The native manager reports success for a registered project even when the name is absent.
		[[nodiscard]] bool RemoveCharacter(MotionProjectId project, std::string_view name);

	private:
		class Implementation;
		std::unique_ptr<Implementation> m_Implementation;
	};

} // namespace sgre::scene
