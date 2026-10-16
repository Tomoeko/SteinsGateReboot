#include "sgre/script/motion_resource_owner.hpp"

#include "sgre/script/psb_bindings.hpp"

#include <exception>
#include <iterator>
#include <limits>
#include <map>
#include <stdexcept>
#include <utility>

namespace sgre::script {

	struct ScriptMotionResourceRecord {
		std::uint32_t References = 1;
		std::shared_ptr<const scene::LoadedMotionAsset> Asset;
		SquirrelHostRoot PsbOwner;
	};

	class ScriptMotionResourceOwner::Implementation {
	public:
		Implementation(SquirrelVm& vm, ScriptPsbBindings& psb, scene::MotionAssetCatalog& assets, scene::TextureCache& cache, scene::MotionResourceManagerLimits limits) : Vm(vm), Psb(psb), Assets(assets), Primary(cache, limits), Secondary(cache, limits) {
			if (!assets.UsesCache(cache) || vm.Type(psb.ObjectClass()) != SquirrelValueType::Class) {
				throw std::runtime_error("motion resource owner requires PSB and loaded-asset services in its VM/cache scope");
			}
		}

		SquirrelVm& Vm;
		ScriptPsbBindings& Psb;
		scene::MotionAssetCatalog& Assets;
		scene::MotionResourceManager Primary;
		scene::MotionResourceManager Secondary;
		std::map<std::int32_t, ScriptMotionResourceRecord> Records;
		std::size_t EventBorrows = 0;
		bool Mutating = false;
		bool Closed = false;

		class Mutation {
		public:
			explicit Mutation(Implementation& owner) : Owner(owner) {
				if (owner.EventBorrows != 0) {
					throw std::runtime_error("script motion resources cannot mutate during borrowed event delivery");
				}
				if (owner.Mutating || owner.Closed) {
					throw std::runtime_error("script motion resource mutation requires its live owner outside native cleanup");
				}

				owner.Mutating = true;
			}

			~Mutation() {
				Owner.Mutating = false;
			}

		private:
			Implementation& Owner;
		};

		const ScriptMotionResourceRecord& Record(scene::MotionProjectId project) const {
			const auto found = Records.find(project.Value);
			if (found == Records.end()) {
				throw std::runtime_error("invalid resource id");
			}

			return found->second;
		}

		void RollBack(scene::MotionProjectId project, bool secondary_registered) {
			if (secondary_registered) {
				try {
					static_cast<void>(Secondary.Unregister(project));
				} catch (...) {
					// Preparation rollback preserves the initiating failure after attempting each acquired manager.
				}
			}

			try {
				static_cast<void>(Primary.Unregister(project));
			} catch (...) {
				// Physical owners are retired independently by the manager's failure-aware cleanup.
			}
		}

		void Close(const std::function<void()>& retire_between_managers) {
			if (Closed) {
				return;
			}

			const Mutation mutation(*this);
			std::exception_ptr failure;
			try {
				Secondary.Close();
			} catch (...) {
				failure = std::current_exception();
			}

			try {
				if (retire_between_managers) {
					retire_between_managers();
				}
			} catch (...) {
				if (!failure) {
					failure = std::current_exception();
				}
			}

			try {
				Primary.Close();
			} catch (...) {
				if (!failure) {
					failure = std::current_exception();
				}
			}

			// Native Layer's retained-resource map retires owners in descending key order after task disposal.
			while (!Records.empty()) {
				Records.erase(std::prev(Records.end()));
			}

			Closed = true;
			if (failure) {
				std::rethrow_exception(failure);
			}
		}
	};

	ScriptMotionResourceOwner::ScriptMotionResourceOwner(SquirrelVm& vm, ScriptPsbBindings& psb, scene::MotionAssetCatalog& assets, scene::TextureCache& cache, scene::MotionResourceManagerLimits limits) : m_Implementation(std::make_unique<Implementation>(vm, psb, assets, cache, limits)) {
	}

	ScriptMotionResourceOwner::~ScriptMotionResourceOwner() {
		if (!CanClose()) {
			// Active scopes borrow this exact owner; failing a preflight cannot cancel native destruction.
			std::terminate();
		}
		try {
			Close();
		} catch (...) {
			// Explicit Close reports platform failures after retiring both managers and every VM resource root.
		}
	}

	scene::MotionProjectId ScriptMotionResourceOwner::Register(const SquirrelValue& psb_object) {
		auto& state = *m_Implementation;
		const Implementation::Mutation mutation(state);
		const auto document = state.Psb.DocumentAsset(psb_object);
		// Typed adoption aliases the real receiving owner, retaining metadata and source-cache pins through both managers.
		const auto asset = document.Motion ? state.Assets.Prepare(document.Motion) : state.Assets.Prepare(document.Document, document.Backing);
		if (document.Motion && asset->Document.get() != document.Document.get()) {
			throw std::runtime_error("motion receiving resource does not match its actual PSB metadata owner");
		}
		const auto project = state.Primary.Register(asset->Identity, asset->Model, *asset->Backing);
		bool secondary_registered = false;
		try {
			const auto secondary = state.Secondary.Register(asset->Identity, asset->Model, *asset->Backing);
			secondary_registered = true;
			if (secondary != project) {
				static_cast<void>(state.Secondary.Unregister(secondary));
				secondary_registered = false;

				throw std::runtime_error("paired native motion managers have inconsistent project identifiers");
			}

			const auto found = state.Records.find(project.Value);
			if (found != state.Records.end()) {
				if (found->second.References == static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max())) {
					throw std::runtime_error("script motion resource reference count exceeds its signed native range");
				}

				++found->second.References;
			} else {
				ScriptMotionResourceRecord record;
				record.Asset = asset;
				record.PsbOwner = state.Vm.Retain(psb_object);
				state.Records.emplace(project.Value, std::move(record));
			}
		} catch (...) {
			const auto failure = std::current_exception();
			state.RollBack(project, secondary_registered);

			std::rethrow_exception(failure);
		}

		return project;
	}

	void ScriptMotionResourceOwner::Unregister(scene::MotionProjectId project) {
		auto& state = *m_Implementation;
		const Implementation::Mutation mutation(state);
		const auto found = state.Records.find(project.Value);
		if (found == state.Records.end()) {
			throw std::runtime_error("invalid resource id");
		}

		// Retain portable parsed data through manager cleanup even when native script ownership expires first.
		const auto asset = found->second.Asset;
		if (--found->second.References == 0) {
			state.Records.erase(found);
		}

		std::exception_ptr failure;
		try {
			static_cast<void>(state.Primary.Unregister(project));
		} catch (...) {
			failure = std::current_exception();
		}

		try {
			static_cast<void>(state.Secondary.Unregister(project));
		} catch (...) {
			if (!failure) {
				failure = std::current_exception();
			}
		}

		if (failure) {
			std::rethrow_exception(failure);
		}
	}

	bool ScriptMotionResourceOwner::Contains(scene::MotionProjectId project) const {
		return m_Implementation->Records.contains(project.Value);
	}

	std::size_t ScriptMotionResourceOwner::Count() const {
		return m_Implementation->Records.size();
	}

	std::uint32_t ScriptMotionResourceOwner::ReferenceCount(scene::MotionProjectId project) const {
		return m_Implementation->Record(project).References;
	}

	SquirrelValue ScriptMotionResourceOwner::RegisteredPsbOwner(scene::MotionProjectId project) const {
		return m_Implementation->Record(project).PsbOwner.Value();
	}

	std::shared_ptr<const scene::LoadedMotionAsset> ScriptMotionResourceOwner::Asset(scene::MotionProjectId project) const {
		return m_Implementation->Record(project).Asset;
	}

	const scene::LoadedMotionAsset& ScriptMotionResourceOwner::BorrowAsset(scene::MotionProjectId project) const {
		return *m_Implementation->Record(project).Asset;
	}

	std::span<const scene::MotionSourceAlias> ScriptMotionResourceOwner::SourceAliases(scene::MotionProjectId project) const {
		return m_Implementation->Record(project).Asset->Aliases;
	}

	const scene::MotionResourceManager& ScriptMotionResourceOwner::Primary() const {
		return m_Implementation->Primary;
	}

	const scene::MotionResourceManager& ScriptMotionResourceOwner::Secondary() const {
		return m_Implementation->Secondary;
	}

	void ScriptMotionResourceOwner::RenameCharacter(scene::MotionProjectId project, std::string_view old_name, std::string_view new_name) {
		const Implementation::Mutation mutation(*m_Implementation);
		if (!m_Implementation->Primary.RenameCharacter(project, old_name, new_name) || !m_Implementation->Secondary.RenameCharacter(project, old_name, new_name)) {
			throw std::runtime_error("rename motion chara failed");
		}
	}

	void ScriptMotionResourceOwner::RemoveCharacter(scene::MotionProjectId project, std::string_view name) {
		const Implementation::Mutation mutation(*m_Implementation);
		if (!m_Implementation->Primary.RemoveCharacter(project, name) || !m_Implementation->Secondary.RemoveCharacter(project, name)) {
			throw std::runtime_error("remove motion chara failed");
		}
	}

	void ScriptMotionResourceOwner::SetFiltering(std::int32_t minification, std::int32_t magnification) {
		const Implementation::Mutation mutation(*m_Implementation);
		m_Implementation->Primary.SetFiltering(minification, magnification);
		m_Implementation->Secondary.SetFiltering(minification, magnification);
	}

	ScriptMotionResourceOwner::EventBorrow::EventBorrow(Implementation& owner) : m_Owner(owner) {
		if (owner.Closed || owner.Mutating || owner.EventBorrows == std::numeric_limits<std::size_t>::max()) {
			throw std::runtime_error("script motion event delivery requires its stable live resource owner");
		}

		++m_Owner.EventBorrows;
	}

	ScriptMotionResourceOwner::EventBorrow::~EventBorrow() noexcept {
		--m_Owner.EventBorrows;
	}

	ScriptMotionResourceOwner::EventBorrow ScriptMotionResourceOwner::BorrowEventDelivery() const {
		return EventBorrow(*m_Implementation);
	}

	bool ScriptMotionResourceOwner::CanClose() const noexcept {
		return m_Implementation->Closed || (!m_Implementation->Mutating && m_Implementation->EventBorrows == 0);
	}

	void ScriptMotionResourceOwner::Close() {
		m_Implementation->Close({});
	}

	void ScriptMotionResourceOwner::Close(std::function<void()> retire_between_managers) {
		m_Implementation->Close(retire_between_managers);
	}

} // namespace sgre::script
