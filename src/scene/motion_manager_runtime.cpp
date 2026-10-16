#include "sgre/scene/motion_manager_runtime.hpp"

#include "sgre/scene/motion_assets.hpp"
#include "sgre/scene/motion_layer_tasks.hpp"
#include "sgre/scene/motion_scene_draw_context.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <utility>
#include <vector>

namespace sgre::scene {

	class MotionManagerRuntime::LayerBindingLifetime {
	public:
		MotionManagerRuntime* Owner = nullptr;
		const MotionLayerTasks* Tasks = nullptr;
		bool Retired = false;
	};

	struct MotionPreparationProjectBorrow {
		MotionProjectId Project;
		std::size_t Count = 0;
	};

	class MotionPreparationProjectBorrows {
	public:
		std::array<MotionPreparationProjectBorrow, kMaximumMotionPreparationOwners> Entries{};
		std::size_t Extent = 0;

		[[nodiscard]] std::span<const MotionPreparationProjectBorrow> LiveEntries() const noexcept {
			return {Entries.data(), Extent};
		}

		std::vector<std::size_t> Acquire(std::span<const MotionProjectId> order) {
			std::vector<std::size_t> result;
			result.reserve(order.size());
			try {
				for (const auto project : order) {
					auto found = std::find_if(Entries.begin(), Entries.end(), [&](const auto& entry) {
						return entry.Count != 0 && entry.Project == project;
					});
					if (found == Entries.end()) {
						found = std::find_if(Entries.begin(), Entries.end(), [](const auto& entry) {
							return entry.Count == 0;
						});
					}
					if (found == Entries.end() || found->Count == std::numeric_limits<std::size_t>::max()) {
						throw std::runtime_error("motion preparation exceeds its bounded live registration-borrow owners");
					}
					found->Project = project;
					++found->Count;
					const auto index = static_cast<std::size_t>(found - Entries.begin());
					Extent = std::max(Extent, index + 1);
					result.push_back(index);
				}
			} catch (...) {
				Release(result);
				throw;
			}
			return result;
		}

		void Release(std::span<const std::size_t> slots) noexcept {
			for (const auto index : slots) {
				if (index >= Entries.size() || Entries[index].Count == 0) {
					std::terminate();
				}
				--Entries[index].Count;
			}
			while (Extent != 0 && Entries[Extent - 1].Count == 0) {
				--Extent;
			}
		}
	};

	class MotionPreparationLease::Data {
	public:
		const MotionResourceManager* Manager = nullptr;
		const MotionManagerRuntime* Runtime = nullptr;
		MotionProjectId Project;
		const resource::MotionTimeline* Raw = nullptr;
		MotionAttributeBlendPolicy Policy = MotionAttributeBlendPolicy::Unchanged;
		std::shared_ptr<const PreparedMotionTimeline> Timeline;
		std::vector<std::shared_ptr<const resource::MotionResource>> Metadata;
		std::unique_ptr<PreparedMotionSourceRegistry> Sources;
		std::vector<MotionProjectId> RegistrationOrder;
		MotionPreparationProjectBorrows* Borrowed = nullptr;
		std::vector<std::size_t> BorrowSlots;

		~Data() noexcept {
			if (Borrowed != nullptr) {
				Borrowed->Release(BorrowSlots);
			}
		}
	};

	MotionPreparationLease::MotionPreparationLease(std::shared_ptr<const Data> data)
	    : m_Data(std::move(data)) {
	}

	bool MotionPreparationLease::IsEmpty() const noexcept {
		return !m_Data;
	}

	bool MotionPreparationLease::UsesRuntime(const MotionManagerRuntime& runtime) const noexcept {
		return m_Data && m_Data->Runtime == &runtime;
	}

	bool MotionPreparationLease::SameOwner(const MotionPreparationLease& other) const noexcept {
		return m_Data == other.m_Data;
	}

	bool MotionPreparationLease::Owns(const MotionResourceManager& manager, const resource::MotionTimeline& timeline, const PreparedMotionTimeline* prepared) const noexcept {
		return m_Data && m_Data->Manager == &manager && m_Data->Raw == &timeline && (prepared == nullptr || prepared == m_Data->Timeline.get());
	}

	class MotionManagerRuntime::Implementation {
	public:
		Implementation(const MotionManagerRuntime& runtime, const MotionResourceManager& manager, const core::FrameClock& clock, std::span<const std::uint32_t, kMotionSharedSeedWords> seed, MotionManagerRuntimeServices services, std::size_t capacity)
		    : Manager(manager), Clock(clock), Seed(seed), Services(std::move(services)), Context(capacity) {
			if (!Services.ReadSourceAliases || (Services.SceneAssets == nullptr) != !Services.ReadRegisteredAsset) {
				throw std::runtime_error("motion manager runtime requires actual registered source-alias metadata");
			}

			if (Services.SceneAssets != nullptr) {
				SceneContext = std::make_unique<MotionSceneDrawContext>(runtime, capacity);
			}
			Preparations.reserve(kMaximumMotionPreparationOwners);
		}

		const MotionResourceManager& Manager;
		const core::FrameClock& Clock;
		std::span<const std::uint32_t, kMotionSharedSeedWords> Seed;
		MotionManagerRuntimeServices Services;
		MotionLayerIdentifierPool Identifiers;
		MotionSpriteDrawContext Context;
		std::unique_ptr<MotionSceneDrawContext> SceneContext;
		std::unique_ptr<MotionSharedRandom> Random;
		std::mutex RandomMutex;
		std::vector<std::weak_ptr<const MotionPreparationLease::Data>> Preparations;
		MotionPreparationProjectBorrows BorrowedProjects;
		std::size_t Players = 0;
		std::shared_ptr<LayerBindingLifetime> Binding;
		float Projection = 0;
		bool Preparing = false;
		bool Closed = false;

		void RequireLive() const {
			if (Closed) {
				throw std::runtime_error("motion manager runtime is closed");
			}
		}

		std::vector<MotionProjectId> ReadOrder() const {
			const auto count = Manager.Count();
			if (count > 256) {
				throw std::runtime_error("motion manager runtime exceeds its bounded registered preparation scope");
			}

			std::vector<MotionProjectId> order(count);
			if (Manager.WriteRegistrationOrder(order) != count) {
				throw std::runtime_error("motion manager registration order changed during preparation");
			}

			return order;
		}
	};

	MotionManagerRuntime::MotionManagerRuntime(const MotionResourceManager& manager, const core::FrameClock& clock, std::span<const std::uint32_t, kMotionSharedSeedWords> runtime_seed, MotionManagerRuntimeServices services, std::size_t draw_capacity)
	    : m_Implementation(std::make_unique<Implementation>(*this, manager, clock, runtime_seed, std::move(services), draw_capacity)) {
	}

	MotionManagerRuntime::~MotionManagerRuntime() noexcept {
		if ((m_Implementation->Binding && !m_Implementation->Binding->Retired) || !CanClose()) {
			std::terminate();
		}
	}

	MotionManagerRuntime::LayerBinding::LayerBinding(std::shared_ptr<LayerBindingLifetime> lifetime)
	    : m_Lifetime(std::move(lifetime)) {
	}

	MotionManagerRuntime::LayerBinding::~LayerBinding() noexcept {
		if (m_Lifetime && !m_Lifetime->Retired) {
			std::terminate();
		}
	}

	MotionManagerRuntime::LayerBinding::LayerBinding(LayerBinding&& other) noexcept
	    : m_Lifetime(std::move(other.m_Lifetime)) {
	}

	MotionManagerRuntime::LayerBinding& MotionManagerRuntime::LayerBinding::operator=(LayerBinding&& other) noexcept {
		if (this != &other) {
			if (m_Lifetime && !m_Lifetime->Retired) {
				std::terminate();
			}
			m_Lifetime = std::move(other.m_Lifetime);
		}

		return *this;
	}

	bool MotionManagerRuntime::LayerBinding::IsEmpty() const noexcept {
		return !m_Lifetime;
	}

	bool MotionManagerRuntime::LayerBinding::IsRetired() const noexcept {
		return m_Lifetime && m_Lifetime->Retired;
	}

	MotionManagerRuntime& MotionManagerRuntime::LayerBinding::BorrowRuntime() const {
		if (!m_Lifetime) {
			throw std::runtime_error("actual Layer Motion runtime borrow is empty");
		}
		if (m_Lifetime->Retired) {
			throw std::runtime_error("actual Layer Motion runtime borrow is retired");
		}
		if (m_Lifetime->Owner == nullptr) {
			throw std::runtime_error("actual Layer Motion runtime borrow has expired");
		}

		// A valid attached runtime outlives its token; Close retires it before manager/source disposal.
		static_cast<void>(m_Lifetime->Owner->Manager());

		return *m_Lifetime->Owner;
	}

	void MotionManagerRuntime::LayerBinding::RequireCanRetirePlayers() const {
		if (!m_Lifetime || m_Lifetime->Retired) {
			return;
		}
		if (m_Lifetime->Owner == nullptr) {
			throw std::runtime_error("actual Layer Motion runtime borrow has expired without retirement");
		}

		m_Lifetime->Owner->RequireCanRetirePlayers();
	}

	void MotionManagerRuntime::LayerBinding::CloseRuntime() {
		RequireCanRetirePlayers();
		if (m_Lifetime && !m_Lifetime->Retired) {
			m_Lifetime->Owner->Close();
		}
	}

	MotionManagerRuntime::LayerBinding MotionManagerRuntime::BindLayerTasks(MotionLayerTasks& tasks) {
		m_Implementation->RequireLive();
		if (m_Implementation->Binding || !CanClose()) {
			throw std::runtime_error("actual Motion runtime Layer binding requires fresh unattached player/context ownership");
		}

		auto lifetime = std::make_shared<LayerBindingLifetime>();
		lifetime->Owner = this;
		lifetime->Tasks = &tasks;
		m_Implementation->Binding = lifetime;

		return LayerBinding(std::move(lifetime));
	}

	void MotionManagerRuntime::RequireLayerTaskOwner(const MotionLayerTasks& tasks) const {
		m_Implementation->RequireLive();
		const auto& binding = m_Implementation->Binding;
		if (tasks.m_RequiresRegisteredRuntime && !binding) {
			throw std::runtime_error("actual installed Layer tasks require their registered Motion runtime preflight");
		}
		if (binding && (binding->Retired || binding->Owner != this || binding->Tasks != &tasks)) {
			throw std::runtime_error("bound Motion runtime tasks require their actual installed Layer owner");
		}
	}

	void MotionManagerRuntime::RequireCanRetirePlayers() const {
		const auto& owner = *m_Implementation;
		if (owner.Preparing || !owner.Context.CanClear() || owner.Context.Size() != 0 || (owner.SceneContext && (!owner.SceneContext->CanClear() || owner.SceneContext->Size() != 0))) {
			throw std::runtime_error("actual Motion runtime retirement requires idle preparation and consumed renderer row borrows");
		}
	}

	const MotionResourceManager& MotionManagerRuntime::Manager() const {
		m_Implementation->RequireLive();

		return m_Implementation->Manager;
	}

	MotionLayerIdentifierPool& MotionManagerRuntime::Identifiers() {
		m_Implementation->RequireLive();

		return m_Implementation->Identifiers;
	}

	MotionSpriteDrawContext& MotionManagerRuntime::Context() {
		m_Implementation->RequireLive();

		return m_Implementation->Context;
	}

	bool MotionManagerRuntime::HasSceneServices() const {
		m_Implementation->RequireLive();
		return m_Implementation->Services.SceneAssets != nullptr;
	}

	MotionAssetCatalog& MotionManagerRuntime::SceneAssets() const {
		if (!HasSceneServices()) {
			throw std::runtime_error("motion scene requires its actual catalog and registered asset services");
		}
		return *m_Implementation->Services.SceneAssets;
	}

	const LoadedMotionAsset& MotionManagerRuntime::ReadRegisteredAsset(MotionProjectId project) const {
		static_cast<void>(SceneAssets());
		const auto& asset = m_Implementation->Services.ReadRegisteredAsset(project);
		if (!Manager().Contains(project) || Manager().Identity(project) != asset.Identity || asset.Model.get() != &Manager().Resource(project) || !SceneAssets().ContainsPrepared(asset)) {
			throw std::runtime_error("motion scene asset borrow differs from its actual manager/catalog registration");
		}
		return asset;
	}

	MotionSceneDrawContext& MotionManagerRuntime::SceneContext() {
		m_Implementation->RequireLive();
		if (!m_Implementation->SceneContext) {
			throw std::runtime_error("legacy explicit motion runtime has no configured generic scene context");
		}
		return *m_Implementation->SceneContext;
	}

	float MotionManagerRuntime::ReadFrameDelta() const {
		m_Implementation->RequireLive();
		const auto& clock = m_Implementation->Clock;

		return clock.VariableFrame() ? std::max(clock.VariableStep(), 1.0f) : 1.0f;
	}

	float MotionManagerRuntime::NextUnitFloat() {
		m_Implementation->RequireLive();
		const std::lock_guard lock(m_Implementation->RandomMutex);
		if (!m_Implementation->Random) {
			m_Implementation->Random = std::make_unique<MotionSharedRandom>(m_Implementation->Seed);
		}

		return m_Implementation->Random->NextUnitFloat();
	}

	float MotionManagerRuntime::DepthProjection() const {
		m_Implementation->RequireLive();

		return m_Implementation->Projection;
	}

	void MotionManagerRuntime::SetDepthProjection(float coefficient) {
		m_Implementation->RequireLive();
		if (!std::isfinite(coefficient) || m_Implementation->Players != 0 || !m_Implementation->Context.CanClear() || m_Implementation->Context.Size() != 0 || (m_Implementation->SceneContext && (!m_Implementation->SceneContext->CanClear() || m_Implementation->SceneContext->Size() != 0)) || m_Implementation->Preparing) {
			throw std::runtime_error("motion projection setup requires a finite coefficient before player construction and outside preparation/submission and queued row borrows");
		}

		m_Implementation->Projection = coefficient;
	}

	bool MotionManagerRuntime::IsClosed() const noexcept {
		return m_Implementation->Closed;
	}

	bool MotionManagerRuntime::ResourceBorrowsLive() const {
		return !m_Implementation->Closed && std::all_of(m_Implementation->BorrowedProjects.LiveEntries().begin(), m_Implementation->BorrowedProjects.LiveEntries().end(), [&](const auto& entry) {
			return entry.Count == 0 || Manager().Contains(entry.Project);
		});
	}

	MotionRuntimeSelection MotionManagerRuntime::Prepare(const ResolvedMotionTimeline& selection) {
		return Prepare(selection, MotionAttributeBlendPolicy::Unchanged);
	}

	MotionRuntimeSelection MotionManagerRuntime::PrepareScene(const ResolvedMotionTimeline& selection) {
		static_cast<void>(ReadRegisteredAsset(selection.Project));
		return Prepare(selection, MotionAttributeBlendPolicy::HalveRgbForClearBlendNibble);
	}

	MotionRuntimeSelection MotionManagerRuntime::Prepare(const ResolvedMotionTimeline& selection, MotionAttributeBlendPolicy policy) {
		auto& state = *m_Implementation;
		state.RequireLive();
		if (state.Preparing || !ResourceBorrowsLive() || !state.Manager.Contains(selection.Project) || state.Manager.Identity(selection.Project) != selection.Identity || selection.Timeline == nullptr || selection.Character == nullptr) {
			throw std::runtime_error("motion full preparation requires its actual live selection and registered backing outside reentry");
		}

		const auto order = state.ReadOrder();
		const auto& resource = state.Manager.Resource(selection.Project);
		if (resource.Objects.size() > 1000000) {
			throw std::runtime_error("motion preparation exceeds its bounded immutable character/timeline count");
		}

		bool owned = false;
		for (const auto& character : resource.Objects) {
			if (&character == selection.Character) {
				if (character.Timelines.size() > 1000000) {
					throw std::runtime_error("motion preparation exceeds its bounded immutable timeline count");
				}

				for (const auto& timeline : character.Timelines) {
					owned = owned || &timeline == selection.Timeline;
				}
			}
		}
		if (!owned) {
			throw std::runtime_error("motion preparation selection is outside its actual immutable registration");
		}

		std::erase_if(state.Preparations, [](const auto& entry) {
			return entry.expired();
		});
		for (const auto& entry : state.Preparations) {
			const auto preparation = entry.lock();
			if (preparation && preparation->Project == selection.Project && preparation->Raw == selection.Timeline && preparation->Policy == policy && preparation->RegistrationOrder == order) {
				return {resource, *preparation->Timeline, *preparation->Sources, MotionPreparationLease(preparation)};
			}
		}
		if (state.Preparations.size() == kMaximumMotionPreparationOwners) {
			throw std::runtime_error("motion full preparations exceed their bounded simultaneously live frame owners");
		}

		state.Preparing = true;
		try {
			auto preparation = std::make_shared<MotionPreparationLease::Data>();
			preparation->Manager = &state.Manager;
			preparation->Runtime = this;
			preparation->Project = selection.Project;
			preparation->Raw = selection.Timeline;
			preparation->Policy = policy;
			preparation->RegistrationOrder = order;
			preparation->Timeline = std::make_shared<const PreparedMotionTimeline>(PrepareMotionTimeline(*selection.Timeline, resource, policy));
			std::vector<MotionSourceRegistration> sources;
			sources.reserve(order.size());
			preparation->Metadata.reserve(order.size());
			for (const auto project : order) {
				MotionSourceRegistration registration;
				registration.Project = state.Manager.Identity(project);
				auto metadata = state.Manager.RetainMetadata(project);
				registration.Sources = metadata->Sources;
				preparation->Metadata.push_back(std::move(metadata));
				registration.Aliases = state.Services.ReadSourceAliases(project);
				sources.push_back(registration);
			}
			preparation->Sources = std::make_unique<PreparedMotionSourceRegistry>(sources);
			if (state.ReadOrder() != order) {
				throw std::runtime_error("motion alias preparation changed its actual source registration order");
			}

			preparation->BorrowSlots = state.BorrowedProjects.Acquire(order);
			preparation->Borrowed = &state.BorrowedProjects;
			const MotionRuntimeSelection result{resource, *preparation->Timeline, *preparation->Sources, MotionPreparationLease(preparation)};
			state.Preparations.push_back(preparation);
			state.Preparing = false;

			return result;
		} catch (...) {
			state.Preparing = false;
			throw;
		}
	}

	MotionRuntimeSelection MotionManagerRuntime::ClonePreparation(const MotionPreparationLease& source) {
		auto& state = *m_Implementation;
		state.RequireLive();
		const auto& original = source.m_Data;
		if (state.Preparing || !ResourceBorrowsLive() || !original || original->Manager == nullptr || original->Runtime == nullptr || !original->Runtime->ResourceBorrowsLive() || !original->Manager->Contains(original->Project) || original->Raw == nullptr || !original->Timeline || !original->Sources) {
			throw std::runtime_error("motion preparation clone requires an idle live immutable source and actual destination registrations");
		}
		if (original->Runtime == this) {
			return {state.Manager.Resource(original->Project), *original->Timeline, *original->Sources, source};
		}
		const auto identity = original->Manager->Identity(original->Project);
		const auto& resource = original->Manager->Resource(original->Project);
		const auto order = state.ReadOrder();
		std::optional<MotionProjectId> project;
		for (const auto candidate : order) {
			if (state.Manager.Identity(candidate) == identity && &state.Manager.Resource(candidate) == &resource) {
				if (project) {
					throw std::runtime_error("motion preparation clone has ambiguous destination identity aliases");
				}
				project = candidate;
			}
		}
		if (!project) {
			throw std::runtime_error("motion preparation clone requires the identical registered document/resource in its destination manager");
		}
		if (state.Services.SceneAssets != nullptr) {
			static_cast<void>(ReadRegisteredAsset(*project));
		}
		std::erase_if(state.Preparations, [](const auto& entry) {
			return entry.expired();
		});
		for (const auto& entry : state.Preparations) {
			const auto prepared = entry.lock();
			if (prepared && prepared->Project == *project && prepared->Raw == original->Raw && prepared->Timeline == original->Timeline && prepared->RegistrationOrder == order) {
				return {resource, *prepared->Timeline, *prepared->Sources, MotionPreparationLease(prepared)};
			}
		}
		if (state.Preparations.size() == kMaximumMotionPreparationOwners) {
			throw std::runtime_error("motion cloned preparations exceed their bounded simultaneously live frame owners");
		}

		state.Preparing = true;
		try {
			auto prepared = std::make_shared<MotionPreparationLease::Data>();
			prepared->Manager = &state.Manager;
			prepared->Runtime = this;
			prepared->Project = *project;
			prepared->Raw = original->Raw;
			prepared->Policy = original->Policy;
			prepared->Timeline = original->Timeline;
			prepared->RegistrationOrder = order;
			std::vector<MotionSourceRegistration> registrations;
			registrations.reserve(order.size());
			prepared->Metadata.reserve(order.size());
			for (const auto registered : order) {
				MotionSourceRegistration registration;
				registration.Project = state.Manager.Identity(registered);
				auto metadata = state.Manager.RetainMetadata(registered);
				registration.Sources = metadata->Sources;
				prepared->Metadata.push_back(std::move(metadata));
				registration.Aliases = state.Services.ReadSourceAliases(registered);
				registrations.push_back(registration);
			}
			prepared->Sources = std::make_unique<PreparedMotionSourceRegistry>(registrations);
			if (state.ReadOrder() != order) {
				throw std::runtime_error("motion preparation clone changed destination registration order during alias preparation");
			}
			prepared->BorrowSlots = state.BorrowedProjects.Acquire(order);
			prepared->Borrowed = &state.BorrowedProjects;
			const MotionRuntimeSelection result{resource, *prepared->Timeline, *prepared->Sources, MotionPreparationLease(prepared)};
			state.Preparations.push_back(prepared);
			state.Preparing = false;

			return result;
		} catch (...) {
			state.Preparing = false;
			throw;
		}
	}

	bool MotionManagerRuntime::RegistrationOrderMatches(const PreparedMotionSourceRegistry& sources) const {
		if (!ResourceBorrowsLive()) {
			return false;
		}

		std::shared_ptr<const MotionPreparationLease::Data> found;
		for (const auto& entry : m_Implementation->Preparations) {
			const auto preparation = entry.lock();
			if (preparation && preparation->Sources.get() == &sources) {
				found = preparation;
				break;
			}
		}
		if (!found) {
			return false;
		}

		const auto& order = found->RegistrationOrder;
		if (order.size() != Manager().Count()) {
			return false;
		}

		std::array<MotionProjectId, 256> current{};
		const auto count = Manager().WriteRegistrationOrder(current);

		return count == order.size() && std::equal(order.begin(), order.end(), current.begin());
	}

	std::optional<RegisteredMotionSource> MotionManagerRuntime::FindTexture(const ResolvedMotionIcon& binding) const {
		if (!ResourceBorrowsLive() || binding.Source == nullptr || binding.Icon == nullptr) {
			throw std::runtime_error("motion texture binding requires its actual live prepared source/icon owners");
		}

		for (const auto& entry : m_Implementation->BorrowedProjects.LiveEntries()) {
			if (entry.Count == 0) {
				continue;
			}
			const auto project = entry.Project;
			if (Manager().Identity(project) == binding.Project) {
				const auto found = Manager().FindSource(project, binding.Source->Name);
				if (!found || found->Source != binding.Source || std::none_of(found->Source->Icons.begin(), found->Source->Icons.end(), [&](const auto& icon) {
					    return &icon == binding.Icon;
				    })) {
					throw std::runtime_error("motion texture binding does not name its actual registered source/icon");
				}

				return found;
			}
		}

		throw std::runtime_error("motion texture binding project is absent from its prepared registration owner");
	}

	void MotionManagerRuntime::AcquirePlayer() {
		m_Implementation->RequireLive();
		if (m_Implementation->Players == 1024) {
			throw std::runtime_error("motion manager exceeds its bounded concrete player-owner count");
		}

		++m_Implementation->Players;
	}

	void MotionManagerRuntime::ReleasePlayer() noexcept {
		if (m_Implementation->Players == 0) {
			std::terminate();
		}

		--m_Implementation->Players;
	}

	std::size_t MotionManagerRuntime::PlayerCount() const noexcept {
		return m_Implementation->Players;
	}

	bool MotionManagerRuntime::CanClose() const noexcept {
		try {
			const bool preparations_retired = std::all_of(m_Implementation->Preparations.begin(), m_Implementation->Preparations.end(), [](const auto& entry) {
				return entry.expired();
			});
			return m_Implementation->Players == 0 && !m_Implementation->Preparing && preparations_retired && m_Implementation->Identifiers.LiveCount() == 0 && m_Implementation->Context.CanClear() && m_Implementation->Context.Size() == 0 && (!m_Implementation->SceneContext || (m_Implementation->SceneContext->CanClear() && m_Implementation->SceneContext->Size() == 0));
		} catch (...) {
			return false;
		}
	}

	void MotionManagerRuntime::Close() {
		if (!CanClose()) {
			throw std::runtime_error("motion manager runtime close requires all concrete players, preparation leases and actual row borrows to retire first");
		}

		m_Implementation->Preparations.clear();
		m_Implementation->Random.reset();
		m_Implementation->Closed = true;
		if (m_Implementation->Binding) {
			m_Implementation->Binding->Retired = true;
			m_Implementation->Binding->Owner = nullptr;
			m_Implementation->Binding->Tasks = nullptr;
		}
	}

} // namespace sgre::scene
