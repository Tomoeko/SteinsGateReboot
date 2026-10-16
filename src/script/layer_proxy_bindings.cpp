#include "sgre/script/layer_proxy_bindings.hpp"

#include "sgre/script/layer_bindings.hpp"

#include <array>
#include <exception>
#include <map>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>

namespace sgre::script {

	struct LayerProxyRecord {
		SquirrelObjectIdentity Instance;
		scene::LayerHandle Handle;
		scene::LayerHandle TargetHandle;
		std::optional<SquirrelHostRoot> Target;
		scene::ParallelTaskHandle Child;
		std::optional<scene::LayerHandle> BorrowedTarget;
		bool ChildReleased = true;
		bool Drawing = false;
		bool Retiring = false;
	};

	class LayerProxyDrawCall {
	public:
		explicit LayerProxyDrawCall(LayerProxyRecord& record) : m_Record(record) {
			if (record.Drawing || record.Retiring) {
				throw std::runtime_error("LayerProxy cannot reenter its borrowed native draw or retirement");
			}
			m_Record.Drawing = true;
		}

		~LayerProxyDrawCall() {
			m_Record.BorrowedTarget.reset();
			m_Record.Drawing = false;
		}
		LayerProxyDrawCall(const LayerProxyDrawCall&) = delete;
		LayerProxyDrawCall& operator=(const LayerProxyDrawCall&) = delete;

	private:
		LayerProxyRecord& m_Record;
	};

	class ScriptLayerProxyBindings::Implementation {
	public:
		Implementation(SquirrelVm& vm, ScriptObjectBindings& objects, ScriptLayerBindings& layers, scene::LayerTree& tree, ScriptLayerProxyDrawServices services, ScriptLayerProxyLimits limits)
		    : Vm(vm), Objects(objects), Layers(layers), Tree(tree), Services(std::move(services)), Limits(limits) {
			if (!Services.State || !Services.Draw || limits.MaxInstances == 0 || limits.MaxInstances > 65536 || vm.IsClosed()) {
				throw std::runtime_error("LayerProxy requires its live actual draw services and bounded positive wrapper capacity");
			}
		}

		SquirrelVm& Vm;
		ScriptObjectBindings& Objects;
		ScriptLayerBindings& Layers;
		scene::LayerTree& Tree;
		ScriptLayerProxyDrawServices Services;
		ScriptLayerProxyLimits Limits;
		SquirrelValue Class;
		SquirrelHostRoot ClassRoot;
		std::map<std::uint32_t, std::shared_ptr<LayerProxyRecord>> Records;

		bool Owns(scene::LayerHandle handle) const {
			const auto found = Records.find(handle.Index);

			return found != Records.end() && found->second->Handle == handle && !found->second->Retiring && Tree.Contains(handle);
		}

		std::shared_ptr<LayerProxyRecord> Record(scene::LayerHandle handle) const {
			const auto found = Records.find(handle.Index);
			if (found == Records.end() || found->second->Handle != handle || found->second->Retiring || !Tree.Contains(handle)) {
				throw std::runtime_error("LayerProxy requires its live exact common Base owner");
			}

			return found->second;
		}

		scene::LayerHandle TargetArgument(const SquirrelValue& target) const {
			if (Vm.Type(target) != SquirrelValueType::Instance || !Vm.IsInstanceOf(target, Layers.LayerBaseClass()) || !Objects.HasInstance(target)) {
				throw std::runtime_error("LayerProxy target must be an actual attached LayerBase instance");
			}

			return Layers.Handle(target);
		}

		std::optional<scene::LayerHandle> ResolveTarget(const LayerProxyRecord& record) const {
			if (!record.Target) {
				throw std::runtime_error("LayerProxy target ObjectInfo has already retired");
			}

			// Native292C10 pops this resolved temporary before292D80 draws the Proxy's own common groups.
			const auto target = Vm.ResolveWeakReference(record.Target->Value());
			if (Vm.Type(target) != SquirrelValueType::Instance) {
				Vm.SetLastErrorInContext(Vm.RootContext(), std::string("the object is not a class instance"));

				return {};
			}
			if (!Vm.IsInstanceOf(target, Layers.LayerBaseClass())) {
				Vm.SetLastErrorInContext(Vm.RootContext(), std::string("invalid type tag"));

				return {};
			}
			if (!Objects.HasInstance(target)) {
				return {};
			}

			const auto handle = Layers.Handle(target);
			if (handle != record.TargetHandle) {
				throw std::runtime_error("LayerProxy weak target changed its actual native identity");
			}

			return handle;
		}

		void DrawTarget(LayerProxyRecord& record, std::uint32_t argument) {
			if (!record.Drawing || !record.BorrowedTarget || *record.BorrowedTarget != record.TargetHandle || record.Retiring) {
				throw std::runtime_error("LayerProxy camera child requires its actual outer weak-target draw admission");
			}
			const auto target = *record.BorrowedTarget;
			if (!Tree.Contains(target)) {
				throw std::runtime_error("LayerProxy target retired during its borrowed synchronous native draw");
			}
			// MDrawingProxy537560 reads hidden first, then the complete process status word; it never updates target.
			if (!Tree.State(target).Visible) {
				return;
			}
			const auto state = Services.State(target);
			if (state.Status != scene::TaskStatus::Active && state.Status != scene::TaskStatus::Complete && state.Status != scene::TaskStatus::Retire) {
				throw std::runtime_error("LayerProxy actual target status is outside the recovered 0..2 domain");
			}
			if (state.Status == scene::TaskStatus::Retire) {
				return;
			}
			if (!Tree.Contains(target)) {
				throw std::runtime_error("LayerProxy target state service invalidated its borrowed native owner");
			}

			Services.Draw(target, argument);
		}

		void RequireCanRelease(const LayerProxyRecord& record, const SquirrelValue& receiver) const {
			if (record.Drawing || record.Retiring) {
				throw std::runtime_error("LayerProxy cannot retire during its actual borrowed draw");
			}
			const auto& children = Layers.BaseTasks(receiver).Children();
			if (!record.ChildReleased && !children.Contains(record.Child)) {
				throw std::runtime_error("LayerProxy cannot retire while its actual camera child has detached ownership");
			}
		}

		void ReleaseDerived(const std::shared_ptr<LayerProxyRecord>& record) {
			// Native244C80 releases the target weak ObjectInfo before22C9F0 retires the common Base graph.
			record->Target.reset();
			record->Retiring = true;
			Records.erase(record->Handle.Index);
		}

		void Construct(std::span<const SquirrelValue> arguments, const std::weak_ptr<Implementation>& weak) {
			if (arguments.empty() || Vm.Type(arguments[0]) != SquirrelValueType::Instance || !Vm.IsInstanceOf(arguments[0], Class) || Records.size() >= Limits.MaxInstances) {
				throw std::runtime_error("LayerProxy constructor requires its genuine receiver and available bounded wrapper capacity");
			}

			//292DC0 first admits a sole target; otherwise22D5F0 consumes a leading owner and leaves one target.
			std::size_t target_index = 1;
			SquirrelValue parent;
			if (arguments.size() != 2) {
				if (arguments.size() != 3 || !Layers.IsLayerOwner(arguments[1])) {
					throw std::runtime_error("LayerProxy constructor requires target or leading owner followed by target");
				}
				parent = arguments[1];
				if (Vm.Type(parent) != SquirrelValueType::Null && !Vm.IsInstanceOf(parent, Layers.LayerFolderClass())) {
					//22D5F0 first tries Folder, then accepts Screen while retaining that failed type-up diagnostic.
					Vm.SetLastError(std::string("invalid type tag"));
				}
				target_index = 2;
			}
			const auto target = TargetArgument(arguments[target_index]);
			auto record = std::make_shared<LayerProxyRecord>();
			record->Instance = Vm.BorrowObjectIdentity(arguments[0]);
			record->TargetHandle = target;
			record->Target = Vm.Retain(Vm.CreateWeakReference(arguments[target_index]));
			const auto weak_record = std::weak_ptr<LayerProxyRecord>(record);
			bool attached = false;
			bool recorded = false;
			bool retirement_configured = false;
			try {
				const std::array<SquirrelValue, 2> common_arguments{arguments[0], parent};
				Layers.ConstructBaseInstance(common_arguments);
				attached = true;
				record->Handle = Layers.Handle(arguments[0]);
				if (!Records.emplace(record->Handle.Index, record).second) {
					throw std::runtime_error("LayerProxy common handle collides with an existing native wrapper");
				}
				recorded = true;
				Layers.ConfigureBaseRetirement(
				    arguments[0], [weak, weak_record](const SquirrelValue& receiver) {
					    const auto owner = weak.lock();
					    const auto retained = weak_record.lock();
					    if (!owner || !retained) {
						    throw std::runtime_error("LayerProxy retirement requires its actual derived services");
					    }
					    owner->RequireCanRelease(*retained, receiver);
				    },
				    [weak, weak_record](const SquirrelValue&) {
					    const auto owner = weak.lock();
					    const auto retained = weak_record.lock();
					    if (!owner || !retained) {
						    throw std::runtime_error("LayerProxy cleanup lost its preflighted derived owner");
					    }
					    owner->ReleaseDerived(retained);
				    });
				retirement_configured = true;

				scene::ParallelTaskCallbacks child;
				child.Draw = [weak, weak_record](auto, std::uint32_t argument) {
					const auto owner = weak.lock();
					const auto retained = weak_record.lock();
					if (!owner || !retained) {
						throw std::runtime_error("LayerProxy camera draw requires its live actual derived owner");
					}
					owner->DrawTarget(*retained, argument);
				};
				child.Release = [weak_record](auto) {
					if (const auto retained = weak_record.lock()) {
						retained->ChildReleased = true;
					}
				};
				child.CanRelease = [weak_record](auto) {
					const auto retained = weak_record.lock();

					return !retained || !retained->Drawing;
				};
				record->Child = Layers.BaseTasks(arguments[0]).Children().Append(std::move(child));
				record->ChildReleased = false;
			} catch (...) {
				const auto failure = std::current_exception();
				if (!retirement_configured) {
					record->Target.reset();
					if (recorded) {
						Records.erase(record->Handle.Index);
					}
				}
				if (attached) {
					try {
						Layers.ReleaseInstance(arguments[0]);
					} catch (...) {
						// The original constructor failure remains first; actual Layer retirement owns any unfinished graph.
					}
				}

				std::rethrow_exception(failure);
			}
		}

		void Draw(scene::LayerHandle handle, std::uint32_t argument) {
			const auto record = Record(handle);
			const LayerProxyDrawCall call(*record);
			record->BorrowedTarget = ResolveTarget(*record);
			if (!record->BorrowedTarget) {
				return;
			}

			Layers.DispatchBaseFrame(handle, [argument](const SquirrelValue&, scene::LayerBaseFrame& frame) {
				frame.Draw(argument);
			});
		}
	};

	ScriptLayerProxyBindings::ScriptLayerProxyBindings(SquirrelVm& vm, ScriptObjectBindings& objects, ScriptLayerBindings& layers, scene::LayerTree& tree, ScriptLayerProxyDrawServices services, ScriptLayerProxyLimits limits)
	    : m_Implementation(std::make_shared<Implementation>(vm, objects, layers, tree, std::move(services), limits)) {
		auto& state = *m_Implementation;
		state.Class = vm.CreateClass(layers.LayerBaseClass());
		state.ClassRoot = vm.Retain(state.Class);
		const auto weak = std::weak_ptr<Implementation>(m_Implementation);
		vm.NewSlot(state.Class, std::string("constructor"), vm.CreateNativeClosure("LayerProxy.constructor", [weak](SquirrelVm&, std::span<const SquirrelValue> arguments) {
			const auto owner = weak.lock();
			if (!owner) {
				throw std::runtime_error("LayerProxy constructor services have expired");
			}
			owner->Construct(arguments, weak);

			return SquirrelNativeResult::NoValues();
		}));
		vm.NewSlot(state.Class, std::string("weakref"), vm.CreateNativeClosure("LayerProxy.weakref", [weak](SquirrelVm& active, std::span<const SquirrelValue> arguments) {
			if (weak.expired() || arguments.empty()) {
				throw std::runtime_error("LayerProxy weakref requires its live services and final original argument");
			}

			return SquirrelNativeResult::ReturnValue(active.CreateWeakReference(arguments.back()));
		}));
		vm.NewSlot(vm.RootTable(), std::string("LayerProxy"), state.Class);
	}

	ScriptLayerProxyBindings::~ScriptLayerProxyBindings() = default;

	SquirrelValue ScriptLayerProxyBindings::LayerProxyClass() const {
		return m_Implementation->Class;
	}

	bool ScriptLayerProxyBindings::Owns(scene::LayerHandle handle) const {
		return m_Implementation->Owns(handle);
	}

	void ScriptLayerProxyBindings::Draw(scene::LayerHandle handle, std::uint32_t argument) {
		m_Implementation->Draw(handle, argument);
	}

	void ScriptLayerProxyBindings::ReleaseInstance(const SquirrelValue& instance) {
		auto& state = *m_Implementation;
		const auto record = state.Record(state.Layers.Handle(instance));
		if (record->Instance != state.Vm.BorrowObjectIdentity(instance)) {
			throw std::runtime_error("LayerProxy release requires its exact native wrapper identity");
		}

		state.Layers.ReleaseInstance(instance);
	}

} // namespace sgre::script
