#include "sgre/script/motion_native_owners.hpp"

#include "sgre/script/motion_layer_getter_bindings.hpp"
#include "sgre/script/motion_layer_setter_bindings.hpp"
#include "sgre/script/motion_resource_owner.hpp"

#include <array>
#include <cstdint>
#include <exception>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

namespace sgre::script {

	struct MotionNativeRecord {
		SquirrelObjectIdentity Instance;
		SquirrelObjectIdentity LayerIdentity;
		std::optional<scene::MotionChildBorrow> Child;
		std::optional<scene::MotionVariableAnimations> ChildAnimations;
		ScriptLayerBindings::BorrowedViewRegistration LayerObject;
		std::optional<scene::MotionPlayerTask> PlayerTask;
		const ScriptMotionResourceOwner* Resources = nullptr;
		std::map<std::string, SquirrelHostRoot, std::less<>> AlternateRoots;
		bool MutatingAlternates = false;
		bool Detached = false;
		bool EventEnabled = false;
		bool DispatchingEvent = false;
	};

	class MotionNativeCall {
	public:
		explicit MotionNativeCall(bool& active)
		    : m_Active(active) {
			if (active) {
				throw std::runtime_error("native Motion wrapper dispatch cannot reenter its active owner");
			}

			m_Active = true;
		}

		~MotionNativeCall() noexcept {
			m_Active = false;
		}
		MotionNativeCall(const MotionNativeCall&) = delete;
		MotionNativeCall& operator=(const MotionNativeCall&) = delete;

	private:
		bool& m_Active;
	};

	static std::string NativeEventString(std::string_view value) {
		// Native228510 pushes these event arguments through its zero-terminated string boundary.
		return std::string(value.substr(0, value.find('\0')));
	}

	class ScriptMotionNativeOwners::Implementation {
	public:
		Implementation(SquirrelVm& vm, ScriptObjectBindings& objects, ScriptLayerBindings& layers, const SquirrelValue& actual_class, std::size_t maximum)
		    : Vm(vm), Objects(objects), Layers(layers), Maximum(maximum), Class(vm.Retain(actual_class)) {
			if (maximum == 0 || vm.Type(actual_class) != SquirrelValueType::Class) {
				throw std::runtime_error("native Motion wrappers require their actual class and bounded instance capacity");
			}
		}

		SquirrelVm& Vm;
		ScriptObjectBindings& Objects;
		ScriptLayerBindings& Layers;
		std::size_t Maximum;
		SquirrelHostRoot Class;
		std::map<std::uint32_t, std::unique_ptr<MotionNativeRecord>> Records;
		const std::thread::id OwnerThread = std::this_thread::get_id();
		bool Constructing = false;
		bool Closed = false;

		void RequireOpen() const {
			if (std::this_thread::get_id() != OwnerThread || Closed) {
				throw std::runtime_error("native Motion wrappers require their live owner thread");
			}
		}

		MotionNativeRecord& Record(const SquirrelObjectIdentity& identity) const {
			RequireOpen();
			const auto found = Records.find(identity.Index);
			if (found == Records.end() || found->second->Instance != identity) {
				throw std::runtime_error("native Motion wrapper identity is expired or belongs to another owner");
			}

			return *found->second;
		}

		MotionNativeRecord& Record(const SquirrelValue& instance) const {
			return Record(Vm.BorrowObjectIdentity(instance));
		}

		void Detach(const SquirrelObjectIdentity& identity) {
			auto& record = Record(identity);
			if (record.DispatchingEvent) {
				throw std::runtime_error("native Motion Layer detach cannot invalidate an active listener");
			}

			// Layer22cb60 marks the wrapper after actual tasks/source managers retire. No payload survives by this view.
			record.AlternateRoots.clear();
			record.Detached = true;
			record.PlayerTask.reset();
			record.Resources = nullptr;
		}

		void DeliverEvent(const SquirrelObjectIdentity& identity, const scene::MotionPlayerEvent& event) {
			auto& record = Record(identity);
			if (!record.EventEnabled) {
				return;
			}
			if (record.Detached || !record.Resources || !record.PlayerTask || !record.PlayerTask->IsLive()) {
				throw std::runtime_error("native Motion event requires its actual live wrapper/player/resource borrows");
			}

			if (event.Kind != scene::MotionPlayerEventKind::Action && event.Kind != scene::MotionPlayerEventKind::Sync) {
				throw std::runtime_error("native Motion event kind is outside its action/sync listener slots");
			}

			const auto resource_borrow = record.Resources->BorrowEventDelivery();
			const auto receiver = Vm.ResolveObjectIdentity(identity);
			if (!receiver) {
				return;
			}

			const MotionNativeCall call(record.DispatchingEvent);
			try {
				const auto context = Vm.RootContext();
				auto method = Vm.ReadSlotInContext(context, *receiver, std::string(event.Kind == scene::MotionPlayerEventKind::Action ? "onAction" : "onSync"));
				if (Vm.Type(method) == SquirrelValueType::WeakReference) {
					method = Vm.ResolveWeakReference(method);
				}

				const auto type = Vm.Type(method);
				if (type != SquirrelValueType::Closure && type != SquirrelValueType::NativeClosure) {
					return;
				}

				SquirrelRunResult result;
				if (event.Kind == scene::MotionPlayerEventKind::Action) {
					const std::array<SquirrelValue, 3> arguments{*receiver, NativeEventString(event.Layer), NativeEventString(event.Action)};
					result = Vm.InvokeWithoutResultInContext(context, method, arguments);
				} else if (event.Kind == scene::MotionPlayerEventKind::Sync) {
					const std::array<SquirrelValue, 1> arguments{*receiver};
					result = Vm.InvokeWithoutResultInContext(context, method, arguments);
				} else {
					throw std::runtime_error("native Motion event kind is outside its action/sync listener slots");
				}
				if (result.State != SquirrelRunState::Completed) {
					throw std::runtime_error("native Motion listener suspension requires its actual containing VM/task continuation owner");
				}
			} catch (const SquirrelVmException&) {
				// Native273d20/273ef0 ignore failed sq_get/sq_call status; the real root VM keeps its diagnostic.
			}
		}

		void RequireCanRelease(const SquirrelValue& instance) const {
			const auto& record = Record(instance);
			Objects.RequireCanRelease(instance);
			if (Constructing || record.DispatchingEvent || record.MutatingAlternates || (!record.Detached && !record.Child && (!record.PlayerTask || !record.PlayerTask->IsLive()))) {
				throw std::runtime_error("native Motion release requires a stable wrapper outside active explicit finalization");
			}
		}

		void Release(const SquirrelValue& instance, bool vm_release) {
			const auto identity = Vm.BorrowObjectIdentity(instance);
			const auto found = Records.find(identity.Index);
			if (found == Records.end() || found->second->Instance != identity) {
				return;
			}
			if (vm_release && (Constructing || found->second->DispatchingEvent)) {
				// A failed VM release hook does not cancel deletion. Reject this unproved explicit-finalizer domain before dangling borrows.
				std::terminate();
			}

			RequireCanRelease(instance);
			auto& record = *found->second;
			if (!record.Detached && !record.Child) {
				record.PlayerTask->ClearAlternateTextures();
				record.AlternateRoots.clear();
				record.PlayerTask->RequestRetire();
			}

			record.LayerObject.Reset();
			Records.erase(found);
			Objects.ReleaseInstance(instance);
		}

		void Construct(const SquirrelValue& instance, const SquirrelValue& layer, scene::MotionManagerRuntime& runtime, bool asynchronous, scene::MotionTaskInsertion insertion, const std::weak_ptr<Implementation>& weak, const scene::MotionPlayerTask* clone_source = nullptr) {
			RequireOpen();
			if (asynchronous || !Vm.IsInstanceOf(instance, Class.Value()) || !Vm.IsInstanceOf(instance, Objects.ObjectClass()) || !Vm.IsInstanceOf(layer, Layers.LayerClass())) {
				throw std::runtime_error("native Motion new route requires its actual Object-derived receiver, concrete Layer and synchronous mode");
			}
			const auto identity = Vm.BorrowObjectIdentity(instance);
			if (Records.contains(identity.Index) || Objects.HasInstance(instance) || Records.size() >= Maximum) {
				throw std::runtime_error("native Motion receiver is already initialized or bounded capacity is exhausted");
			}
			Layers.RequireCanReleaseInstance(layer);
			const auto& resources = Layers.MotionResources(layer);
			if (&runtime.Manager() != &resources.Primary() && &runtime.Manager() != &resources.Secondary()) {
				throw std::runtime_error("native Motion selected runtime does not belong to this actual Layer source manager");
			}

			const MotionNativeCall call(Constructing);
			auto record = std::make_unique<MotionNativeRecord>();
			record->Instance = identity;
			record->LayerIdentity = Vm.BorrowObjectIdentity(layer);
			record->Resources = &resources;
			Records.emplace(identity.Index, std::move(record));
			try {
				Objects.AttachInstance(instance);
				auto& current = Record(identity);
				current.LayerObject = Layers.BorrowMotionObject(layer, [weak, identity] {
					const auto owner = weak.lock();
					if (!owner) {
						throw std::runtime_error("native Motion detach receiver owner has expired");
					}

					owner->Detach(identity);
				});
				auto listener = [weak, identity](const scene::MotionPlayerEvent& event) {
					const auto owner = weak.lock();
					if (!owner) {
						throw std::runtime_error("native Motion event receiver owner has expired");
					}

					owner->DeliverEvent(identity, event);
				};
				if (clone_source) {
					current.PlayerTask = scene::MotionPlayerTask::AttachClone(runtime, Layers.MotionTasks(layer), *clone_source, asynchronous, std::move(listener), insertion);
				} else {
					current.PlayerTask = scene::MotionPlayerTask::Attach(runtime, Layers.MotionTasks(layer), asynchronous, {}, std::move(listener), insertion);
				}
				Vm.SetInstanceHostIdentity(instance, {this, identity.Generation});
				Objects.ConfigureNativeRelease(instance, [weak](const SquirrelValue& receiver) {
					if (const auto owner = weak.lock()) {
						owner->Release(receiver, true);
					}
				});
			} catch (...) {
				const auto failure = std::current_exception();
				auto& current = Record(identity);
				if (current.PlayerTask && current.PlayerTask->IsLive()) {
					auto task = Layers.MotionTasks(layer).Group(scene::MotionTaskGroup::Second).Extract(current.PlayerTask->Handle());
					task.Release();
				}
				current.LayerObject.Reset();
				Records.erase(identity.Index);
				if (Objects.HasInstance(instance)) {
					Objects.ReleaseInstance(instance);
				}

				std::rethrow_exception(failure);
			}
		}
	};

	ScriptMotionNativeOwners::ScriptMotionNativeOwners(SquirrelVm& vm, ScriptObjectBindings& objects, ScriptLayerBindings& layers, const SquirrelValue& actual_motion_class, std::size_t maximum_instances)
	    : m_Implementation(std::make_shared<Implementation>(vm, objects, layers, actual_motion_class, maximum_instances)) {
	}

	ScriptMotionNativeOwners::~ScriptMotionNativeOwners() noexcept {
		if (!CanClose()) {
			std::terminate();
		}
		m_Implementation->Closed = true;
	}

	void ScriptMotionNativeOwners::ConstructNew(const SquirrelValue& instance, const SquirrelValue& layer, scene::MotionManagerRuntime& actual_runtime, bool actual_asynchronous_mode, scene::MotionTaskInsertion insertion) {
		m_Implementation->Construct(instance, layer, actual_runtime, actual_asynchronous_mode, insertion, m_Implementation);
	}

	void ScriptMotionNativeOwners::ConstructClone(const SquirrelValue& instance, const SquirrelValue& destination_layer, const SquirrelValue& source_motion, scene::MotionManagerRuntime& actual_destination_runtime, bool actual_asynchronous_mode) {
		auto& owner = *m_Implementation;
		const auto& source = owner.Record(source_motion);
		if (source.Child || source.Detached || source.DispatchingEvent || source.MutatingAlternates || !source.PlayerTask || !source.PlayerTask->IsLive()) {
			throw std::runtime_error("Motion clone requires a stable actual root source outside detached, child or callback ownership");
		}
		if (!source.AlternateRoots.empty()) {
			throw std::runtime_error("Motion clone with alternate textures requires recovered independent UID listener and descriptor ownership");
		}

		// The weak source task does not retain the source wrapper, Layer, VM receiver or mutable scene tree.
		const auto task = *source.PlayerTask;
		owner.Construct(instance, destination_layer, actual_destination_runtime, actual_asynchronous_mode, scene::MotionTaskInsertion::Append, m_Implementation, &task);
	}

	bool ScriptMotionNativeOwners::Contains(const SquirrelValue& instance) const {
		m_Implementation->RequireOpen();
		const auto identity = m_Implementation->Vm.BorrowObjectIdentity(instance);
		const auto found = m_Implementation->Records.find(identity.Index);

		return found != m_Implementation->Records.end() && found->second->Instance == identity;
	}

	bool ScriptMotionNativeOwners::IsDetached(const SquirrelValue& instance) const {
		return m_Implementation->Record(instance).Detached;
	}

	bool ScriptMotionNativeOwners::EventEnabled(const SquirrelValue& instance) const {
		return m_Implementation->Record(instance).EventEnabled;
	}

	void ScriptMotionNativeOwners::SetEventEnabled(const SquirrelValue& instance, bool enabled) {
		m_Implementation->Record(instance).EventEnabled = enabled;
	}

	scene::MotionPlayerTask ScriptMotionNativeOwners::Task(const SquirrelValue& instance) const {
		const auto& record = m_Implementation->Record(instance);
		if (record.Child) {
			throw std::runtime_error("borrowed child Motion operation requires its recovered child control path");
		}
		if (record.Detached || !record.PlayerTask || !record.PlayerTask->IsLive()) {
			throw std::runtime_error("native Motion task borrow has expired after Layer detach or actual task retirement");
		}

		return *record.PlayerTask;
	}

	bool ScriptMotionNativeOwners::AllPlaying(const SquirrelValue& instance) const {
		const auto& record = m_Implementation->Record(instance);
		static_cast<void>(Commands(instance));

		return record.PlayerTask->AllPlaying(record.Child);
	}

	SquirrelValue ScriptMotionNativeOwners::GetLayerSetter(ScriptMotionLayerSetters& setters, const SquirrelValue& instance, std::string_view name) const {
		const auto& record = m_Implementation->Record(instance);
		static_cast<void>(Commands(instance));
		const auto task = *record.PlayerTask;
		const auto child = record.Child;

		return setters.GetLayerSetter(task, name, child);
	}

	SquirrelValue ScriptMotionNativeOwners::GetLayerGetter(ScriptMotionLayerGetters& getters, const SquirrelValue& instance, std::string_view name) const {
		const auto& record = m_Implementation->Record(instance);
		static_cast<void>(Commands(instance));
		const auto task = *record.PlayerTask;
		const auto child = record.Child;
		const auto layer = record.LayerIdentity;

		return getters.GetLayerGetter(task, name, layer, child);
	}

	SquirrelValue ScriptMotionNativeOwners::GetLayerMotion(ScriptMotionLayerGetters& getters, const SquirrelValue& instance, std::string_view name) const {
		const auto& record = m_Implementation->Record(instance);
		static_cast<void>(Commands(instance));
		const auto task = *record.PlayerTask;
		const auto child = record.Child;
		const auto layer = record.LayerIdentity;

		return getters.GetLayerMotion(task, name, layer, child);
	}

	SquirrelObjectIdentity ScriptMotionNativeOwners::LayerIdentity(const SquirrelValue& instance) const {
		return m_Implementation->Record(instance).LayerIdentity;
	}

	SquirrelValue ScriptMotionNativeOwners::CreateBorrowedChild(const SquirrelObjectIdentity& layer_identity, const scene::MotionPlayerTask& containing, const scene::MotionChildBorrow& child) {
		auto& owner = *m_Implementation;
		owner.RequireOpen();
		static_cast<void>(containing.ChildCommands(child));
		const auto layer = owner.Vm.ResolveObjectIdentity(layer_identity);
		if (!layer || !owner.Vm.IsInstanceOf(*layer, owner.Layers.LayerClass()) || owner.Records.size() >= owner.Maximum) {
			throw std::runtime_error("borrowed child Motion requires its actual enclosing Layer and bounded wrapper capacity");
		}
		owner.Layers.RequireCanReleaseInstance(*layer);
		const MotionNativeCall construction(owner.Constructing);
		const auto instance = owner.Vm.CreateInstance(owner.Class.Value());
		const auto instance_root = owner.Vm.Retain(instance);
		const auto identity = owner.Vm.BorrowObjectIdentity(instance);
		auto record = std::make_unique<MotionNativeRecord>();
		record->Instance = identity;
		record->LayerIdentity = layer_identity;
		record->PlayerTask = containing;
		record->Child = child;
		record->Resources = &owner.Layers.MotionResources(*layer);
		owner.Records.emplace(identity.Index, std::move(record));
		const std::weak_ptr<Implementation> weak = m_Implementation;
		try {
			owner.Objects.AttachInstance(instance);
			auto& current = owner.Record(identity);
			current.LayerObject = owner.Layers.BorrowMotionObject(*layer, [weak, identity] {
				const auto owner = weak.lock();
				if (!owner) {
					throw std::runtime_error("borrowed child Motion detach owner has expired");
				}
				owner->Detach(identity);
			});
			owner.Vm.SetInstanceHostIdentity(instance, {&owner, identity.Generation});
			owner.Objects.ConfigureNativeRelease(instance, [weak](const SquirrelValue& receiver) {
				if (const auto owner = weak.lock()) {
					owner->Release(receiver, true);
				}
			});
		} catch (...) {
			const auto failure = std::current_exception();
			owner.Record(identity).LayerObject.Reset();
			owner.Records.erase(identity.Index);
			if (owner.Objects.HasInstance(instance)) {
				owner.Objects.ReleaseInstance(instance);
			}
			std::rethrow_exception(failure);
		}

		return instance;
	}

	const scene::MotionPlayerCommands& ScriptMotionNativeOwners::Commands(const SquirrelValue& instance) const {
		const auto& record = m_Implementation->Record(instance);
		if (record.Detached || !record.PlayerTask || !record.PlayerTask->IsLive()) {
			throw std::runtime_error("Motion player borrow has expired after Layer or containing task retirement");
		}

		return record.Child ? record.PlayerTask->ChildCommands(*record.Child) : record.PlayerTask->Commands();
	}

	const scene::MotionRootScene& ScriptMotionNativeOwners::Root(const SquirrelValue& instance) const {
		const auto& record = m_Implementation->Record(instance);
		static_cast<void>(Commands(instance));

		return record.Child ? record.PlayerTask->ChildRoot(*record.Child) : record.PlayerTask->Root();
	}

	void ScriptMotionNativeOwners::Stop(const SquirrelValue& instance) {
		const auto& record = m_Implementation->Record(instance);
		static_cast<void>(Commands(instance));
		auto task = *record.PlayerTask;
		if (record.Child) {
			task.StopChild(*record.Child);
		} else {
			task.Stop();
		}
	}

	void ScriptMotionNativeOwners::SkipToSync(const SquirrelValue& instance) {
		auto& record = m_Implementation->Record(instance);
		static_cast<void>(Commands(instance));
		if (record.Child) {
			record.PlayerTask->SkipChildToSync(*record.Child);
		} else {
			record.PlayerTask->SkipToSync();
		}
	}

	void ScriptMotionNativeOwners::ReleaseSyncWait(const SquirrelValue& instance) {
		auto& record = m_Implementation->Record(instance);
		static_cast<void>(Commands(instance));
		if (record.Child) {
			record.PlayerTask->ReleaseChildSyncWait(*record.Child);
		} else {
			record.PlayerTask->ReleaseSyncWait();
		}
	}

	void ScriptMotionNativeOwners::SetCoordinate(const SquirrelValue& instance, resource::MotionPoint coordinate) {
		auto& record = m_Implementation->Record(instance);
		static_cast<void>(Commands(instance));
		if (record.Child) {
			record.PlayerTask->SetChildCoordinate(*record.Child, coordinate);
		} else {
			record.PlayerTask->SetCoordinate(coordinate);
		}
	}

	void ScriptMotionNativeOwners::SetZoom(const SquirrelValue& instance, float horizontal, float vertical) {
		auto& record = m_Implementation->Record(instance);
		static_cast<void>(Commands(instance));
		if (record.Child) {
			record.PlayerTask->SetChildZoom(*record.Child, horizontal, vertical);
		} else {
			record.PlayerTask->SetZoom(horizontal, vertical);
		}
	}

	void ScriptMotionNativeOwners::SetVisible(const SquirrelValue& instance, bool visible) {
		auto& record = m_Implementation->Record(instance);
		static_cast<void>(Commands(instance));
		if (record.Child) {
			record.PlayerTask->SetChildVisible(*record.Child, visible);
		} else {
			record.PlayerTask->SetVisible(visible);
		}
	}

	void ScriptMotionNativeOwners::SetAngleDegrees(const SquirrelValue& instance, float degrees) {
		auto& record = m_Implementation->Record(instance);
		static_cast<void>(Commands(instance));
		if (record.Child) {
			record.PlayerTask->SetChildAngleDegrees(*record.Child, degrees);
		} else {
			record.PlayerTask->SetAngleDegrees(degrees);
		}
	}

	void ScriptMotionNativeOwners::SetAngleRadians(const SquirrelValue& instance, float radians) {
		auto& record = m_Implementation->Record(instance);
		static_cast<void>(Commands(instance));
		if (record.Child) {
			record.PlayerTask->SetChildAngleRadians(*record.Child, radians);
		} else {
			record.PlayerTask->SetAngleRadians(radians);
		}
	}

	void ScriptMotionNativeOwners::SetOpacity(const SquirrelValue& instance, std::uint32_t opacity) {
		auto& record = m_Implementation->Record(instance);
		static_cast<void>(Commands(instance));
		if (record.Child) {
			record.PlayerTask->SetChildOpacity(*record.Child, opacity);
		} else {
			record.PlayerTask->SetOpacity(opacity);
		}
	}

	void ScriptMotionNativeOwners::SetVariable(const SquirrelValue& instance, std::string_view name, float value, std::uint32_t flags) {
		auto& record = m_Implementation->Record(instance);
		static_cast<void>(Commands(instance));
		if (record.Child) {
			record.PlayerTask->SetChildVariable(*record.Child, name, value, flags);
		} else {
			record.PlayerTask->SetVariable(name, value, flags);
		}
	}

	void ScriptMotionNativeOwners::AnimateVariable(const SquirrelValue& instance, std::string_view name, float value, std::int32_t duration, std::int32_t curve) {
		auto& record = m_Implementation->Record(instance);
		static_cast<void>(Commands(instance));
		if (record.Child) {
			record.PlayerTask->AnimateChildVariable(*record.Child, record.ChildAnimations, name, value, duration, curve);
		} else {
			record.PlayerTask->AnimateVariable(name, value, duration, curve);
		}
	}

	void ScriptMotionNativeOwners::RegisterAlternateTexture(const SquirrelValue& instance, const scene::NativeTextureReference& descriptor, std::string_view uid, const SquirrelValue& texture, const scene::MotionBounds& rectangle, resource::MotionPoint origin) {
		auto& state = *m_Implementation;
		auto& record = state.Record(instance);
		if (record.Detached) {
			return;
		}
		auto task = Task(instance);
		const MotionNativeCall mutation(record.MutatingAlternates);
		if (uid.size() > scene::kMaximumMotionCommandNameBytes) {
			throw std::runtime_error("Motion alternate texture UID exceeds its native string bound");
		}
		const auto name = std::string(uid.substr(0, uid.find('\0')));
		std::map<std::string, SquirrelHostRoot, std::less<>> replacement;
		replacement.emplace(name, state.Vm.Retain(texture));
		// Both real VM roots enclose UID listener replacement. Admission failure cannot orphan the old descriptor.
		task.RegisterAlternateTexture(name, descriptor, rectangle, origin);
		auto previous = record.AlternateRoots.extract(name);
		record.AlternateRoots.insert(replacement.extract(replacement.begin()));
		static_cast<void>(previous);
	}

	void ScriptMotionNativeOwners::UnregisterAlternateTexture(const SquirrelValue& instance, std::string_view uid) {
		auto& record = m_Implementation->Record(instance);
		if (record.Detached) {
			return;
		}
		const MotionNativeCall mutation(record.MutatingAlternates);
		const auto name = std::string(uid.substr(0, uid.find('\0')));
		auto task = Task(instance);
		static_cast<void>(task.UnregisterAlternateTexture(name));
		record.AlternateRoots.erase(name);
	}

	void ScriptMotionNativeOwners::ClearAlternateTextures(const SquirrelValue& instance) {
		auto& record = m_Implementation->Record(instance);
		if (record.Detached) {
			return;
		}
		const MotionNativeCall mutation(record.MutatingAlternates);
		auto task = Task(instance);
		task.ClearAlternateTextures();
		record.AlternateRoots.clear();
	}

	void ScriptMotionNativeOwners::RequireCanRelease(const SquirrelValue& instance) const {
		m_Implementation->RequireCanRelease(instance);
	}

	void ScriptMotionNativeOwners::ReleaseInstance(const SquirrelValue& instance) {
		m_Implementation->Release(instance, false);
	}

	std::size_t ScriptMotionNativeOwners::InstanceCount() const noexcept {
		return m_Implementation->Records.size();
	}

	bool ScriptMotionNativeOwners::CanClose() const noexcept {
		return std::this_thread::get_id() == m_Implementation->OwnerThread && !m_Implementation->Constructing && m_Implementation->Records.empty();
	}

	void ScriptMotionNativeOwners::Close() {
		if (!CanClose()) {
			throw std::runtime_error("native Motion wrappers must complete before their containing adapter closes");
		}

		m_Implementation->Closed = true;
	}

} // namespace sgre::script
