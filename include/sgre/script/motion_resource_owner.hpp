#pragma once

#include "sgre/scene/motion_assets.hpp"
#include "sgre/scene/motion_resources.hpp"
#include "sgre/script/squirrel_vm.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string_view>

namespace sgre::script {

	class ScriptPsbBindings;

	class ScriptMotionResourceOwner {
	private:
		class Implementation;

	public:
		class EventBorrow {
		public:
			~EventBorrow() noexcept;
			EventBorrow(const EventBorrow&) = delete;
			EventBorrow& operator=(const EventBorrow&) = delete;
			EventBorrow(EventBorrow&&) = delete;
			EventBorrow& operator=(EventBorrow&&) = delete;

		private:
			explicit EventBorrow(Implementation& owner);
			Implementation& m_Owner;
			friend class ScriptMotionResourceOwner;
		};
		// VM/PSB/catalog/cache services outlive this per-Layer owner. It owns both actual motion managers.
		ScriptMotionResourceOwner(SquirrelVm& vm, ScriptPsbBindings& psb, scene::MotionAssetCatalog& assets, scene::TextureCache& cache, scene::MotionResourceManagerLimits limits = {});
		~ScriptMotionResourceOwner();
		ScriptMotionResourceOwner(const ScriptMotionResourceOwner&) = delete;
		ScriptMotionResourceOwner& operator=(const ScriptMotionResourceOwner&) = delete;
		ScriptMotionResourceOwner(ScriptMotionResourceOwner&&) = delete;
		ScriptMotionResourceOwner& operator=(ScriptMotionResourceOwner&&) = delete;

		[[nodiscard]] scene::MotionProjectId Register(const SquirrelValue& psb_object);
		void Unregister(scene::MotionProjectId project);
		[[nodiscard]] bool Contains(scene::MotionProjectId project) const;
		[[nodiscard]] std::size_t Count() const;
		[[nodiscard]] std::uint32_t ReferenceCount(scene::MotionProjectId project) const;
		// A returned VM value/asset owner is scoped; these views never substitute a permanent native root.
		[[nodiscard]] SquirrelValue RegisteredPsbOwner(scene::MotionProjectId project) const;
		[[nodiscard]] std::shared_ptr<const scene::LoadedMotionAsset> Asset(scene::MotionProjectId project) const;
		// Exact registered loaded owner; this borrow adds no shared/PSB/model/VM retain.
		[[nodiscard]] const scene::LoadedMotionAsset& BorrowAsset(scene::MotionProjectId project) const;
		// Alias metadata borrows the same registered asset/PSB owner and expires on final unregister.
		[[nodiscard]] std::span<const scene::MotionSourceAlias> SourceAliases(scene::MotionProjectId project) const;
		[[nodiscard]] const scene::MotionResourceManager& Primary() const;
		[[nodiscard]] const scene::MotionResourceManager& Secondary() const;
		void RenameCharacter(scene::MotionProjectId project, std::string_view old_name, std::string_view new_name);
		void RemoveCharacter(scene::MotionProjectId project, std::string_view name);
		void SetFiltering(std::int32_t minification, std::int32_t magnification);
		// Authored observational guard for raw queued strings; no PSB/model/VM retain is added.
		// Existing owner/manager mutations reject until this synchronous delivery scope returns.
		[[nodiscard]] EventBorrow BorrowEventDelivery() const;
		// Default close retires both managers before resource roots; actual Layer close supplies the measured task midpoint.
		[[nodiscard]] bool CanClose() const noexcept;
		void Close();
		// Actual Layer retires its first view task group between the two source-manager destructors.
		void Close(std::function<void()> retire_between_managers);

	private:
		std::unique_ptr<Implementation> m_Implementation;
	};

} // namespace sgre::script
