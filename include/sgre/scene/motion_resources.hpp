#pragma once

#include "sgre/scene/motion_projects.hpp"
#include "sgre/scene/motion_shared_textures.hpp"
#include "sgre/scene/motion_textures.hpp"

#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <string_view>

namespace sgre::scene {

	struct MotionResourceManagerLimits {
		MotionProjectLimits Projects;
		std::size_t MaxTextureSlots = 1024;
		std::size_t MaxExternalTextureSlots = 1024;
		std::size_t MaxSourceNameBytes = 8 * 1024 * 1024;
	};

	struct RegisteredMotionSource {
		// The source borrows its immutable project and expires on final unregistration.
		const resource::MotionSource* Source = nullptr;
		MotionTextureSlot Slot;
		CachedTextureHandle Texture;
	};

	class MotionResourceManager {
	public:
		// The shared texture cache outlives this owner. Native script adapters separately root PSB owners.
		explicit MotionResourceManager(TextureCache& cache, MotionResourceManagerLimits limits = {});
		~MotionResourceManager();
		MotionResourceManager(const MotionResourceManager&) = delete;
		MotionResourceManager& operator=(const MotionResourceManager&) = delete;
		MotionResourceManager(MotionResourceManager&&) = delete;
		MotionResourceManager& operator=(MotionResourceManager&&) = delete;

		// Sources import in serialized resource order only for a newly registered document.
		[[nodiscard]] MotionProjectId Register(MotionProjectIdentity identity, std::shared_ptr<const resource::MotionResource> resource, std::span<const std::uint8_t> psb_bytes);
		// Final unregistration releases source textures in native byte-key map order before project removal.
		[[nodiscard]] bool Unregister(MotionProjectId project);
		[[nodiscard]] bool Contains(MotionProjectId project) const;
		[[nodiscard]] std::size_t Count() const;
		[[nodiscard]] MotionProjectIdentity Identity(MotionProjectId project) const;
		// The actual registered model expires on final unregister; this adds no retain.
		[[nodiscard]] const resource::MotionResource& Resource(MotionProjectId project) const;
		// Retains immutable CPU metadata only; registration counts and source textures are unchanged.
		[[nodiscard]] std::shared_ptr<const resource::MotionResource> RetainMetadata(MotionProjectId project) const;
		[[nodiscard]] std::uint32_t ReferenceCount(MotionProjectId project) const;
		[[nodiscard]] std::size_t WriteRegistrationOrder(std::span<MotionProjectId> output) const;
		[[nodiscard]] std::size_t WriteSourceNames(MotionProjectId project, std::span<std::string_view> output) const;
		[[nodiscard]] std::optional<RegisteredMotionSource> FindSource(MotionProjectId project, std::string_view name) const;
		[[nodiscard]] std::size_t WriteCharacterNames(MotionProjectId project, std::span<std::string_view> output) const;
		[[nodiscard]] const resource::MotionObject* FindCharacter(MotionProjectId project, std::string_view name) const;
		// Selection borrows this live manager registration; Close/final unregistration expires it.
		[[nodiscard]] std::optional<ResolvedMotionTimeline> ResolveTimeline(std::string_view character, std::string_view timeline, std::optional<MotionProjectIdentity> requesting_project = std::nullopt) const;
		[[nodiscard]] bool RenameCharacter(MotionProjectId project, std::string_view old_name, std::string_view new_name);
		[[nodiscard]] bool RemoveCharacter(MotionProjectId project, std::string_view name);
		// Native manager-owned external bookkeeping is shared by all actual players and their selected descendants.
		[[nodiscard]] MotionSharedTextures& ExternalTextures() const;
		void SetFiltering(std::int32_t minification, std::int32_t magnification);
		void Close();

	private:
		class Implementation;
		std::unique_ptr<Implementation> m_Implementation;
	};

} // namespace sgre::scene
