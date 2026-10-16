#include "sgre/script/layer_movie_bindings.hpp"

#include "sgre/scene/layer_camera_draw.hpp"
#include "sgre/script/layer_bindings.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <exception>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>

namespace sgre::script {

	struct LayerMoviePayload {
		explicit LayerMoviePayload(media::MoviePlaybackServices services)
		    : Task(std::move(services)) {
		}
		scene::MovieLayerTask Task;
	};

	struct LayerMovieRecord {
		SquirrelObjectIdentity Instance;
		SquirrelObjectIdentity Parent;
		std::weak_ptr<LayerMoviePayload> Payload;
		std::unique_ptr<scene::LayerScaleView> Scale;
		ScriptLayerBindings::BorrowedViewRegistration View;
		bool Releasing = false;
	};

	enum class LayerMovieMethod : std::uint32_t {
		SetOffset = 1,
		SetScale,
		SetRotation,
		SetPrimaryScale,
		SetVisible,
		GetVisible,
		SetOpacity,
		GetOpacity,
		Play,
		Stop,
		Pause,
		Unpause,
		GetPlaying,
		GetPlayStart,
		SetVolume,
		GetVolume,
		SetUseAlpha
	};

	static float MovieFloat(const SquirrelValue& value) {
		if (const auto* number = std::get_if<float>(&value)) {
			return *number;
		}
		if (const auto* number = std::get_if<std::int32_t>(&value)) {
			return static_cast<float>(*number);
		}
		if (const auto* boolean = std::get_if<bool>(&value)) {
			return *boolean ? 1.0f : 0.0f;
		}
		return 0;
	}

	static bool MovieTruth(const SquirrelValue& value) {
		if (std::holds_alternative<std::monostate>(value)) {
			return false;
		}
		if (const auto* boolean = std::get_if<bool>(&value)) {
			return *boolean;
		}
		if (const auto* number = std::get_if<std::int32_t>(&value)) {
			return *number != 0;
		}
		if (const auto* number = std::get_if<float>(&value)) {
			return *number != 0;
		}
		return true;
	}

	static std::array<std::uint8_t, 8> MovieMetadata(LayerMovieMethod method) {
		const auto word = static_cast<std::uint32_t>(method);
		return {static_cast<std::uint8_t>(word), static_cast<std::uint8_t>(word >> 8), static_cast<std::uint8_t>(word >> 16), static_cast<std::uint8_t>(word >> 24), 0, 0, 0, 0};
	}

	static void RequireMovieMetadata(SquirrelVm& vm, LayerMovieMethod method) {
		const auto captured = vm.NativeStackValue(-1);
		if (vm.Type(captured) != SquirrelValueType::UserData) {
			throw std::runtime_error("LayerMovie method requires its genuine captured userdata");
		}
		const auto bytes = vm.UserDataBytes(captured);
		const auto expected = MovieMetadata(method);
		if (bytes.size() != expected.size() || !std::equal(bytes.begin(), bytes.end(), expected.begin())) {
			throw std::runtime_error("LayerMovie captured method identity is invalid");
		}
	}

	class ScriptLayerMovieBindings::Implementation {
	public:
		Implementation(SquirrelVm& vm, ScriptObjectBindings& objects, ScriptLayerBindings& layers, scene::LayerTree& tree, ScriptLayerMovieLimits limits)
		    : Vm(vm), Objects(objects), Layers(layers), Tree(tree), Limits(limits) {
			if (limits.MaxInstances == 0 || limits.MaxInstances > 65536 || limits.MaxCameraTasks == 0 || limits.MaxCameraTasks > 65536) {
				throw std::runtime_error("LayerMovie requires bounded positive wrapper/camera capacities");
			}
		}

		std::shared_ptr<LayerMovieRecord> Record(const SquirrelValue& instance) const {
			const auto identity = Vm.BorrowObjectIdentity(instance);
			const auto found = Records.find(identity.Index);
			if (found == Records.end() || found->second->Instance != identity || found->second->Releasing) {
				throw std::runtime_error("LayerMovie receiver has no live native wrapper identity");
			}
			return found->second;
		}

		std::shared_ptr<LayerMoviePayload> Payload(const LayerMovieRecord& record) const {
			const auto payload = record.Payload.lock();
			if (record.Scale->State().Detached || !payload || payload->Task.State().Closed) {
				throw std::runtime_error("LayerMovie actual player has detached or retired");
			}
			return payload;
		}

		void Construct(std::span<const SquirrelValue> arguments, const std::weak_ptr<Implementation>& weak) {
			if (arguments.size() < 2 || Renderer == nullptr) {
				throw std::runtime_error("LayerMovie constructor requires its actual Layer and configured frame/stream owners");
			}
			const auto receiver = arguments[0];
			const auto parent = arguments[1];
			const auto identity = Vm.BorrowObjectIdentity(receiver);
			if (Records.size() == Limits.MaxInstances || Records.contains(identity.Index)) {
				throw std::runtime_error("LayerMovie wrapper capacity is exhausted or receiver is already initialized");
			}
			auto payload = std::make_shared<LayerMoviePayload>(Services);
			auto record = std::make_shared<LayerMovieRecord>();
			record->Instance = identity;
			record->Parent = Vm.BorrowObjectIdentity(parent);
			record->Payload = payload;
			const auto weak_record = std::weak_ptr<LayerMovieRecord>(record);
			bool attached = false;
			try {
				record->View = Layers.BorrowMotionView(
				    parent, [weak_payload = std::weak_ptr<LayerMoviePayload>(payload)](bool smoothing) {
					    if (const auto owner = weak_payload.lock()) {
						    owner->Task.SetFiltering(smoothing, smoothing);
					    }
				    },
				    [weak_record] {
					    if (const auto owner = weak_record.lock()) {
						    owner->Scale->DetachParent();
						    owner->Payload.reset();
					    }
				    });
				scene::LayerScaleTaskCallbacks callbacks;
				// Actual38d960→4c05e0 sets the initial draw-paused byte. Play does not make the view visible.
				callbacks.InitiallyDrawPaused = true;
				callbacks.Update = [payload] {
					payload->Task.Update();
				};
				callbacks.Draw = [payload](std::uint32_t) {
					payload->Task.Draw();
				};
				callbacks.Release = [payload] {
					payload->Task.Close();
				};
				callbacks.CanRelease = [payload] {
					return payload->Task.CanClose();
				};
				callbacks.DrawWithCamera = [renderer = Renderer](const auto& camera, auto& children, auto argument) {
					renderer->Draw(scene::PrepareLayerCameraParameters(camera), children, argument);
				};
				record->Scale = std::make_unique<scene::LayerScaleView>(Layers.MotionTasks(parent), std::move(callbacks), Limits.MaxCameraTasks);
				const auto smoothing = Tree.State(Layers.Handle(parent)).Smoothing;
				payload->Task.SetFiltering(smoothing, smoothing);
				Objects.AttachInstance(receiver);
				attached = true;
				Records.emplace(identity.Index, record);
				Objects.ConfigureNativeRelease(receiver, [weak](const SquirrelValue& instance) {
					if (const auto owner = weak.lock()) {
						owner->Release(instance);
					}
				});
				EverConstructed = true;
			} catch (...) {
				const auto failure = std::current_exception();
				if (record->Scale) {
					record->Scale->Retire();
				}
				record->View.Reset();
				Records.erase(identity.Index);
				if (attached) {
					try {
						Objects.ReleaseInstance(receiver);
					} catch (...) {
					}
				}
				std::rethrow_exception(failure);
			}
		}

		void Release(const SquirrelValue& instance) {
			Objects.RequireCanRelease(instance);
			const auto record = Record(instance);
			const auto payload = record->Payload.lock();
			if (payload && !payload->Task.CanClose()) {
				throw std::runtime_error("LayerMovie cannot retire its actual queued frame/player operation");
			}
			if (!record->Scale->State().Detached) {
				const auto parent = Vm.ResolveObjectIdentity(record->Parent);
				if (!parent) {
					throw std::runtime_error("LayerMovie retirement requires its actual stable Layer or completed detach");
				}
				Layers.RequireCanReleaseInstance(*parent);
				// Native072d40 stops audio/stream before04f860 marks the actual First task for retirement.
				if (payload && !payload->Task.State().Closed) {
					payload->Task.Stop();
				}
			}
			record->Scale->Retire();
			record->Releasing = true;
			record->View.Reset();
			std::exception_ptr failure;
			try {
				Objects.ReleaseInstance(instance);
			} catch (...) {
				failure = std::current_exception();
			}
			Records.erase(record->Instance.Index);
			if (failure) {
				std::rethrow_exception(failure);
			}
		}

		SquirrelNativeResult Invoke(LayerMovieMethod method, const SquirrelValue& receiver) {
			const auto record = Record(receiver);
			const bool detached = record->Scale->State().Detached;
			switch (method) {
			case LayerMovieMethod::SetOffset:
				record->Scale->SetOffset({MovieFloat(Vm.NativeAllocatedStackValue(2)), MovieFloat(Vm.NativeAllocatedStackValue(3))});
				break;
			case LayerMovieMethod::SetScale:
				record->Scale->SetScale({MovieFloat(Vm.NativeAllocatedStackValue(2)), MovieFloat(Vm.NativeAllocatedStackValue(3))});
				break;
			case LayerMovieMethod::SetRotation:
				record->Scale->SetRotation(MovieFloat(Vm.NativeAllocatedStackValue(2)));
				break;
			case LayerMovieMethod::SetPrimaryScale:
				record->Scale->SetPrimaryScale(MovieFloat(Vm.NativeAllocatedStackValue(2)));
				break;
			case LayerMovieMethod::SetVisible:
				record->Scale->SetVisible(MovieTruth(Vm.NativeAllocatedStackValue(2)));
				break;
			case LayerMovieMethod::GetVisible:
				return SquirrelNativeResult::ReturnValue(!detached && !record->Scale->TaskState().DrawPaused);
			case LayerMovieMethod::GetPlayStart:
				// The original072f90 tests only detached state, even before the first Play.
				return SquirrelNativeResult::ReturnValue(!detached);
			default:
				return InvokePlayer(method, *record, detached);
			}
			return SquirrelNativeResult::NoValues();
		}

		SquirrelNativeResult InvokePlayer(LayerMovieMethod method, const LayerMovieRecord& record, bool detached) {
			if (method == LayerMovieMethod::GetOpacity) {
				return SquirrelNativeResult::ReturnValue(detached ? std::int32_t{0} : std::bit_cast<std::int32_t>(Payload(record)->Task.State().Opacity));
			}
			if (method == LayerMovieMethod::GetVolume) {
				return SquirrelNativeResult::ReturnValue(detached ? 0.0f : Payload(record)->Task.State().Volume);
			}
			if (method == LayerMovieMethod::GetPlaying) {
				return SquirrelNativeResult::ReturnValue(!detached && Payload(record)->Task.State().Status != scene::MoviePlaybackStatus::Stopped);
			}
			if (detached) {
				return SquirrelNativeResult::NoValues();
			}
			const auto payload = Payload(record);
			auto& player = payload->Task;
			switch (method) {
			case LayerMovieMethod::Play:
				Play(player);
				break;
			case LayerMovieMethod::Stop:
				player.Stop();
				break;
			case LayerMovieMethod::Pause:
				player.Pause();
				break;
			case LayerMovieMethod::Unpause:
				player.Unpause();
				break;
			case LayerMovieMethod::SetOpacity:
				player.SetOpacity(std::bit_cast<std::uint32_t>(TryNativeInteger(Vm.NativeAllocatedStackValue(2)).value_or(0)));
				break;
			case LayerMovieMethod::SetVolume:
				player.SetVolume(MovieFloat(Vm.NativeAllocatedStackValue(2)));
				break;
			case LayerMovieMethod::SetUseAlpha:
				player.SetUseAlpha(MovieTruth(Vm.NativeAllocatedStackValue(2)));
				break;
			default:
				throw std::runtime_error("LayerMovie method has no recovered native dispatch");
			}
			return SquirrelNativeResult::NoValues();
		}

		void Play(scene::MovieLayerTask& player) {
			// Actual072c40 reads a raw string, without script _tostring conversion. Own it across Open callbacks.
			const auto& text = StringText(Vm.NativeAllocatedStackValue(2));
			const std::string storage(text.substr(0, text.find('\0')));
			player.Play(storage);
		}

		SquirrelVm& Vm;
		ScriptObjectBindings& Objects;
		ScriptLayerBindings& Layers;
		scene::LayerTree& Tree;
		ScriptLayerMovieLimits Limits;
		SquirrelValue Class;
		SquirrelHostRoot ClassRoot;
		std::map<std::uint32_t, std::shared_ptr<LayerMovieRecord>> Records;
		scene::LayerCameraDrawScope* Renderer = nullptr;
		media::MoviePlaybackServices Services;
		bool EverConstructed = false;
	};

	ScriptLayerMovieBindings::ScriptLayerMovieBindings(SquirrelVm& vm, ScriptObjectBindings& objects, ScriptLayerBindings& layers, scene::LayerTree& tree, ScriptLayerMovieLimits limits)
	    : m_Implementation(std::make_shared<Implementation>(vm, objects, layers, tree, limits)) {
		auto& state = *m_Implementation;
		state.Class = vm.CreateClass(objects.ObjectClass());
		state.ClassRoot = vm.Retain(state.Class);
		const auto weak = std::weak_ptr<Implementation>(m_Implementation);
		vm.NewSlot(state.Class, std::string("constructor"), vm.CreateNativeClosure("LayerMovie.constructor", [weak](SquirrelVm&, auto arguments) {
			const auto owner = weak.lock();
			if (!owner) {
				throw std::runtime_error("LayerMovie constructor services expired");
			}
			owner->Construct(arguments, weak);
			return SquirrelNativeResult::NoValues();
		}));
		vm.NewSlot(state.Class, std::string("weakref"), vm.CreateNativeClosure("LayerMovie.weakref", [weak](SquirrelVm&, auto arguments) {
			const auto owner = weak.lock();
			if (!owner || arguments.empty()) {
				throw std::runtime_error("LayerMovie weakref requires its final original argument and live services");
			}
			return SquirrelNativeResult::ReturnValue(owner->Vm.CreateWeakReference(arguments.back()));
		}));
		const auto method = [&](const char* name, LayerMovieMethod identity) {
			const std::array<SquirrelValue, 1> captures{vm.CreateUserData(MovieMetadata(identity))};
			vm.NewSlot(state.Class, std::string(name), vm.CreateNativeClosureWithCaptures(
			                                               std::string("LayerMovie.") + name, [weak, identity](SquirrelVm& active, auto arguments) {
				                                               const auto owner = weak.lock();
				                                               if (!owner || arguments.empty() || &active != &owner->Vm) {
					                                               throw std::runtime_error("LayerMovie method requires its actual VM, live owner and original receiver");
				                                               }
				                                               RequireMovieMetadata(active, identity);
				                                               return owner->Invoke(identity, arguments[0]);
			                                               },
			                                               captures));
		};
		method("setOffset", LayerMovieMethod::SetOffset);
		method("setScale", LayerMovieMethod::SetScale);
		method("setRot", LayerMovieMethod::SetRotation);
		method("setPrimaryScale", LayerMovieMethod::SetPrimaryScale);
		method("setVisible", LayerMovieMethod::SetVisible);
		method("getVisible", LayerMovieMethod::GetVisible);
		method("setOpacity", LayerMovieMethod::SetOpacity);
		method("getOpacity", LayerMovieMethod::GetOpacity);
		method("play", LayerMovieMethod::Play);
		method("stop", LayerMovieMethod::Stop);
		method("pause", LayerMovieMethod::Pause);
		method("unpause", LayerMovieMethod::Unpause);
		method("getPlaying", LayerMovieMethod::GetPlaying);
		method("getPlayStart", LayerMovieMethod::GetPlayStart);
		method("setVolume", LayerMovieMethod::SetVolume);
		method("getVolume", LayerMovieMethod::GetVolume);
		method("setUseAlpha", LayerMovieMethod::SetUseAlpha);
		vm.NewSlot(vm.RootTable(), std::string("LayerMovie"), state.Class);
	}

	ScriptLayerMovieBindings::~ScriptLayerMovieBindings() = default;

	void ScriptLayerMovieBindings::ConfigureFrameOwners(scene::LayerCameraDrawScope& actual_renderer, media::MoviePlaybackServices services) {
		auto& state = *m_Implementation;
		if (state.Renderer != nullptr || state.EverConstructed || !state.Records.empty() || state.Vm.IsClosed() || !services.Open || !services.ReadClock100ns || !services.Draw || !services.CanRetireDrawOwner || !services.RetireDrawOwner) {
			throw std::runtime_error("LayerMovie frame services require complete actual owners once before construction");
		}
		state.Services = std::move(services);
		state.Renderer = &actual_renderer;
	}

	SquirrelValue ScriptLayerMovieBindings::LayerMovieClass() const {
		return m_Implementation->Class;
	}

	scene::MovieLayerState ScriptLayerMovieBindings::State(const SquirrelValue& instance) const {
		return m_Implementation->Payload(*m_Implementation->Record(instance))->Task.State();
	}

	scene::LayerScaleView& ScriptLayerMovieBindings::ScaleView(const SquirrelValue& instance) {
		return *m_Implementation->Record(instance)->Scale;
	}

	void ScriptLayerMovieBindings::ReleaseInstance(const SquirrelValue& instance) {
		m_Implementation->Release(instance);
	}

} // namespace sgre::script
