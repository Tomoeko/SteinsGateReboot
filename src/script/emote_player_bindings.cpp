#include "sgre/script/emote_player_bindings.hpp"

#include "sgre/scene/emote_variable_controls.hpp"
#include "sgre/scene/motion_root_scene.hpp"
#include "sgre/script/motion_resource_owner.hpp"
#include "sgre/script/psb_bindings.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <exception>
#include <map>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

namespace sgre::script {
	struct EmoteNativeRecord {
		SquirrelObjectIdentity Instance;
		SquirrelObjectIdentity Layer;
		ScriptLayerBindings::BorrowedViewRegistration LayerObject;
		std::optional<scene::EmotePlayerTask> Task;
		bool Detached = false;
	};

	// Portable capture tokens follow the recovered facade order; native descriptors hold callback pointers.
	enum class EmoteNativeMethod : std::uint8_t {
		SetCoordinate = 2,
		GetLeft = 3,
		GetTop = 4,
		SetScale = 5,
		GetScale = 6,
		SetRotation = 7,
		GetRotation = 8,
		SetColor = 9,
		GetColor = 10,
		SetVariable = 11,
		GetVariable = 12,
		SetMeshDivision = 15,
		GetMeshDivision = 16,
		StartWind = 19,
		StopWind = 20,
		SetHairScale = 21,
		GetHairScale = 22,
		SetPartsScale = 23,
		GetPartsScale = 24,
		SetBustScale = 25,
		GetBustScale = 26,
		SetBlink = 29,
		GetBlink = 30,
		Skip = 31,
		Pass = 32,
		SetVisible = 33,
		GetVisible = 34,
		Pause = 35,
		Clear = 36,
		Play = 52,
		Stop = 53,
		Playing = 54,
		SetBlend = 55,
		GetBlend = 56,
		FadeIn = 57,
		FadeOut = 58,
		MainLabels = 61,
		DifferenceLabels = 62,
		AssignState = 63,
		GetAnimating = 64
	};

	static float EmoteNativeFloat(const SquirrelValue& input) {
		if (const auto* real = std::get_if<float>(&input)) {
			return *real;
		}
		if (const auto* integer = std::get_if<std::int32_t>(&input)) {
			return static_cast<float>(*integer);
		}
		if (const auto* boolean = std::get_if<bool>(&input)) {
			return *boolean ? 1.0f : 0.0f;
		}
		return 0;
	}
	static bool EmoteNativeTruth(const SquirrelValue& input) {
		if (std::holds_alternative<std::monostate>(input)) {
			return false;
		}
		if (const auto* boolean = std::get_if<bool>(&input)) {
			return *boolean;
		}
		if (const auto* number = std::get_if<std::int32_t>(&input)) {
			return *number != 0;
		}
		if (const auto* number = std::get_if<float>(&input)) {
			return *number != 0;
		}
		return true;
	}
	static std::array<std::uint8_t, 8> EmoteCodeDescriptor(EmoteNativeMethod method) {
		return {static_cast<std::uint8_t>(method), 0, 0, 0, 0, 0, 0, 0};
	}

	static ScriptEmoteBindingServices ValidatedEmoteServices(ScriptEmoteBindingServices services, std::size_t maximum) {
		if (!services.Motion.ResolveRuntime || !services.Motion.ReadAsynchronousMode || !services.ReadEntropyWord || maximum == 0 || maximum > 65536) {
			throw std::runtime_error("Emote native binding requires actual Layer/PSB/runtime/entropy services and bounded capacity");
		}
		return services;
	}

	class ScriptEmoteBindings::Implementation {
	public:
		Implementation(SquirrelVm& vm, ScriptObjectBindings& objects, ScriptLayerBindings& layers, ScriptPsbBindings& psb, ScriptEmoteBindingServices services, std::size_t maximum)
		    : Vm(vm), Objects(objects), Layers(layers), Psb(psb), Services(ValidatedEmoteServices(std::move(services), maximum)), Maximum(maximum), Registration(vm, objects) {
		}
		void RequireOpen() const {
			if (Closed || Thread != std::this_thread::get_id()) {
				throw std::runtime_error("Emote native wrapper requires its live owner thread");
			}
		}
		std::shared_ptr<EmoteNativeRecord> Record(const SquirrelValue& instance) const {
			RequireOpen();
			const auto identity = Vm.BorrowObjectIdentity(instance);
			const auto found = Records.find(identity.Index);
			if (!Vm.IsInstanceOf(instance, Registration.EmoteClass()) || found == Records.end() || found->second->Instance != identity) {
				throw std::runtime_error("Emote receiver has no actual live native wrapper");
			}
			return found->second;
		}
		void Detach(SquirrelObjectIdentity identity) {
			const auto found = Records.find(identity.Index);
			if (found == Records.end() || found->second->Instance != identity || Constructing) {
				throw std::runtime_error("Emote Layer detach requires its stable native wrapper");
			}
			found->second->Detached = true;
			found->second->Task.reset();
		}
		scene::MotionProjectId RegisteredProject(const SquirrelValue& layer, const ScriptPsbDocumentAsset& document, scene::MotionManagerRuntime& runtime) {
			const auto& resources = Layers.MotionResources(layer);
			std::vector<scene::MotionProjectId> projects(runtime.Manager().Count());
			const auto count = runtime.Manager().WriteRegistrationOrder(projects);
			if (count != projects.size()) {
				throw std::runtime_error("Emote actual resource registration order changed during native construction");
			}
			for (const auto project : projects) {
				const auto& registered = resources.BorrowAsset(project);
				if (registered.Document.get() == document.Document.get() && &registered == &runtime.ReadRegisteredAsset(project)) {
					return project;
				}
			}
			throw std::runtime_error("Emote constructor PSB requires its exact actual Layer registration");
		}
		void Construct(std::span<const SquirrelValue> arguments, const std::weak_ptr<Implementation>& weak) {
			RequireOpen();
			if (Constructing || arguments.size() < 3 || arguments.size() > 5 || !Vm.IsInstanceOf(arguments[0], Registration.EmoteClass()) || !Vm.IsInstanceOf(arguments[1], Layers.LayerClass()) || !Vm.IsInstanceOf(arguments[2], Psb.ObjectClass())) {
				throw std::runtime_error("Emote constructor invalid argument");
			}
			const auto identity = Vm.BorrowObjectIdentity(arguments[0]);
			if (Records.contains(identity.Index) || Objects.HasInstance(arguments[0]) || Records.size() >= Maximum) {
				throw std::runtime_error("Emote native receiver is initialized or wrapper capacity is exhausted");
			}
			std::array<SquirrelValue, 5> captured;
			std::copy(arguments.begin(), arguments.end(), captured.begin());
			arguments = std::span<const SquirrelValue>(captured).first(arguments.size());
			Layers.RequireCanReleaseInstance(arguments[1]);
			const auto selector = arguments.size() >= 4 ? TryNativeInteger(arguments[3]).value_or(0) : 0;
			// Native2ad4a8 deliberately reads physical4 again when a fifth argument is present.
			const auto insertion = arguments.size() == 5 && selector != 0 ? scene::MotionTaskInsertion::Prepend : scene::MotionTaskInsertion::Append;
			const auto source = selector == 0 ? MotionLayerSource::Secondary : MotionLayerSource::Primary;
			const auto document = Psb.DocumentAsset(arguments[2]);
			Constructing = true;
			bool attached = false;
			auto record = std::make_shared<EmoteNativeRecord>();
			record->Instance = identity;
			record->Layer = Vm.BorrowObjectIdentity(arguments[1]);
			try {
				auto& runtime = Services.Motion.ResolveRuntime(arguments[1], source);
				const auto& resources = Layers.MotionResources(arguments[1]);
				const auto& expected = source == MotionLayerSource::Primary ? resources.Primary() : resources.Secondary();
				if (&runtime.Manager() != &expected) {
					throw std::runtime_error("Emote resolver returned a different actual Layer source manager");
				}
				const auto project = RegisteredProject(arguments[1], document, runtime);
				if (Services.Motion.ReadAsynchronousMode()) {
					throw std::runtime_error("Emote asynchronous task execution requires the genuine worker producer");
				}
				Records.emplace(identity.Index, record);
				Objects.AttachInstance(arguments[0]);
				attached = true;
				record->LayerObject = Layers.BorrowMotionObject(arguments[1], [weak, identity] {
					const auto owner = weak.lock();
					if (!owner) {
						throw std::runtime_error("Emote Layer detach services expired");
					}
					owner->Detach(identity);
				});
				record->Task = scene::EmotePlayerTask::Attach(runtime, Layers.MotionTasks(arguments[1]), project, false, Services.ReadEntropyWord, insertion, Services.WindRandom);
				Objects.ConfigureNativeRelease(arguments[0], [weak](const SquirrelValue& receiver) {
					if (const auto owner = weak.lock()) {
						owner->Release(receiver);
					}
				});
				Constructing = false;
			} catch (...) {
				const auto failure = std::current_exception();
				if (record->Task && record->Task->IsLive()) {
					auto task = Layers.MotionTasks(arguments[1]).Group(scene::MotionTaskGroup::Second).Extract(record->Task->Handle());
					task.Release();
				}
				record->LayerObject.Reset();
				Records.erase(identity.Index);
				if (attached) {
					try {
						Objects.ReleaseInstance(arguments[0]);
					} catch (...) {
					}
				}
				Constructing = false;
				std::rethrow_exception(failure);
			}
		}
		void Release(const SquirrelValue& instance) {
			const auto record = Record(instance);
			Objects.RequireCanRelease(instance);
			if (Constructing || (!record->Detached && (!record->Task || !record->Task->IsLive() || !record->Task->CanRetire()))) {
				throw std::runtime_error("Emote native retirement requires its actual idle unqueued player");
			}
			if (!record->Detached) {
				const auto layer = Vm.ResolveObjectIdentity(record->Layer);
				if (!layer) {
					throw std::runtime_error("Emote retirement requires actual parent Layer or completed detach");
				}
				Layers.RequireCanReleaseInstance(*layer);
				record->Task->RequestRetire();
			}
			record->LayerObject.Reset();
			Records.erase(record->Instance.Index);
			Objects.ReleaseInstance(instance);
		}
		std::string Name(const SquirrelValue& input) {
			auto name = Vm.ToString(input);
			name.resize(name.find('\0') == std::string::npos ? name.size() : name.find('\0'));
			return name;
		}
		SquirrelNativeResult Dispatch(EmoteNativeMethod method, std::span<const SquirrelValue> arguments) {
			if (arguments.empty()) {
				throw std::runtime_error("bad instance");
			}
			const auto receiver = arguments.front();
			auto record = Record(receiver);
			const auto input = [&](std::int32_t index) {
				return Vm.NativeAllocatedStackValue(index);
			};
			if (method == EmoteNativeMethod::Skip) {
				if (!record->Detached) {
					record->Task->Player().Skip();
				}

				return SquirrelNativeResult::NoValues();
			}
			if (method == EmoteNativeMethod::Pass) {
				if (!record->Detached) {
					record->Task->Player().Pass();
				}

				return SquirrelNativeResult::NoValues();
			}
			if (method == EmoteNativeMethod::GetAnimating) {
				return SquirrelNativeResult::ReturnValue(!record->Detached && record->Task->Player().Animating());
			}
			if (method == EmoteNativeMethod::AssignState) {
				// Native2AC820 keeps its capture and performs typed physical2 extraction before either detach test.
				const auto source_value = input(2);
				std::shared_ptr<EmoteNativeRecord> source;
				if (Vm.IsInstanceOf(source_value, Registration.EmoteClass())) {
					const auto identity = Vm.BorrowObjectIdentity(source_value);
					const auto found = Records.find(identity.Index);
					if (found != Records.end() && found->second->Instance == identity) {
						source = found->second;
					}
				}
				if (record->Detached) {
					return SquirrelNativeResult::NoValues();
				}
				if (!source) {
					throw std::runtime_error("Emote assignment requires its actual typed source wrapper; native null-source dereference is unsupported");
				}
				if (!source->Detached) {
					record->Task->Player().AssignState(source->Task->Player());
				}
				return SquirrelNativeResult::NoValues();
			}
			if (method == EmoteNativeMethod::StartWind) {
				// Native2acaf0 extracts physical6..2 before the member's detached-owner check.
				scene::EmoteWindParameters parameters;
				parameters.Maximum = EmoteNativeFloat(input(6));
				parameters.Minimum = EmoteNativeFloat(input(5));
				parameters.Speed = EmoteNativeFloat(input(4));
				parameters.Goal = EmoteNativeFloat(input(3));
				parameters.Start = EmoteNativeFloat(input(2));
				if (!record->Detached) {
					record->Task->Player().StartWind(parameters);
				}
				return SquirrelNativeResult::NoValues();
			}
			if (method == EmoteNativeMethod::StopWind) {
				if (!record->Detached) {
					record->Task->Player().StopWind();
				}
				return SquirrelNativeResult::NoValues();
			}
			if (method == EmoteNativeMethod::GetLeft || method == EmoteNativeMethod::GetTop) {
				const auto coordinate = record->Detached ? std::array<float, 2>{} : record->Task->Player().Coordinate();
				const auto index = method == EmoteNativeMethod::GetLeft ? std::size_t{0} : std::size_t{1};
				return SquirrelNativeResult::ReturnValue(coordinate[index]);
			}
			if (method == EmoteNativeMethod::GetScale) {
				return SquirrelNativeResult::ReturnValue(record->Detached ? 1.0f : record->Task->Player().Scale());
			}
			if (method == EmoteNativeMethod::GetRotation) {
				return SquirrelNativeResult::ReturnValue(record->Detached ? 0.0f : record->Task->Player().Rotation());
			}
			if (method == EmoteNativeMethod::GetMeshDivision || method == EmoteNativeMethod::GetBustScale || method == EmoteNativeMethod::GetHairScale || method == EmoteNativeMethod::GetPartsScale) {
				float value = 1;
				if (!record->Detached) {
					auto& player = record->Task->Player();
					if (method == EmoteNativeMethod::GetMeshDivision) {
						value = player.MeshDivisionRatio();
					} else if (method == EmoteNativeMethod::GetBustScale) {
						value = player.BustScale();
					} else if (method == EmoteNativeMethod::GetHairScale) {
						value = player.HairScale();
					} else {
						value = player.PartsScale();
					}
				}
				return SquirrelNativeResult::ReturnValue(value);
			}
			if (method == EmoteNativeMethod::SetMeshDivision || method == EmoteNativeMethod::SetBustScale || method == EmoteNativeMethod::SetHairScale || method == EmoteNativeMethod::SetPartsScale) {
				if (record->Detached && method == EmoteNativeMethod::SetMeshDivision) {
					// Native2b2410 ignores its detach test and would dereference the retired property owner.
					throw std::runtime_error("detached Emote mesh division setter has no safe native property owner");
				}
				if (!record->Detached) {
					// Native038450 keeps its capture, reads physical2 with zero fallback, and ignores extras.
					const auto value = EmoteNativeFloat(input(2));
					auto& player = record->Task->Player();
					if (method == EmoteNativeMethod::SetMeshDivision) {
						player.SetMeshDivisionRatio(value);
					} else if (method == EmoteNativeMethod::SetBustScale) {
						player.SetBustScale(value);
					} else if (method == EmoteNativeMethod::SetHairScale) {
						player.SetHairScale(value);
					} else {
						player.SetPartsScale(value);
					}
				}
				return SquirrelNativeResult::NoValues();
			}
			if (method == EmoteNativeMethod::GetColor) {
				const auto color = record->Detached ? 0x808080ffu : record->Task->Player().Color();
				return SquirrelNativeResult::ReturnValue(std::bit_cast<std::int32_t>(color));
			}
			if (method == EmoteNativeMethod::Pause) {
				const auto paused = EmoteNativeTruth(input(2));
				if (!record->Detached) {
					record->Task->Pause(paused);
				}
				return SquirrelNativeResult::NoValues();
			}
			if (method == EmoteNativeMethod::Clear) {
				if (!record->Detached) {
					record->Task->Player().Clear();
				}
				return SquirrelNativeResult::NoValues();
			}
			if (method == EmoteNativeMethod::GetVisible || method == EmoteNativeMethod::GetBlink) {
				return SquirrelNativeResult::ReturnValue(method == EmoteNativeMethod::GetVisible ? !record->Detached && record->Task->Visible() : record->Detached || record->Task->Player().Variables().BlinkEnabled());
			}
			if (method == EmoteNativeMethod::SetVisible || method == EmoteNativeMethod::SetBlink) {
				if (!record->Detached) {
					const auto value = EmoteNativeTruth(input(2));
					if (method == EmoteNativeMethod::SetVisible) {
						record->Task->SetVisible(value);
					} else {
						record->Task->Player().SetBlinkEnabled(value);
					}
				}
				return SquirrelNativeResult::NoValues();
			}
			if (method == EmoteNativeMethod::MainLabels || method == EmoteNativeMethod::DifferenceLabels) {
				std::vector<SquirrelValue> values;
				if (!record->Detached) {
					const auto labels = record->Task->Player().Variables().TimelineLabels(method == EmoteNativeMethod::DifferenceLabels);
					values.reserve(labels.size());
					for (const auto& label : labels) {
						values.push_back(Vm.CreateString(label));
					}
				}
				return SquirrelNativeResult::ReturnValue(Vm.CreateArray(values));
			}
			if (method == EmoteNativeMethod::GetVariable || method == EmoteNativeMethod::GetBlend) {
				const auto name_input = input(2);
				const auto name = Name(name_input);
				record = Record(receiver);
				return SquirrelNativeResult::ReturnValue(record->Detached ? 0.0f : method == EmoteNativeMethod::GetVariable ? record->Task->Player().ReadVariable(name)
				                                                                                                            : record->Task->Player().TimelineBlendRatio(name));
			}
			// Native direct members return for detached receivers before count or string conversion.
			if (record->Detached) {
				return method == EmoteNativeMethod::Playing ? SquirrelNativeResult::ReturnValue(false) : SquirrelNativeResult::NoValues();
			}
			if (method == EmoteNativeMethod::SetCoordinate) {
				if (arguments.size() < 3 || arguments.size() > 5) {
					throw std::runtime_error("invalid argument.");
				}
				// Native227E00 and227920 read numbers/bools and zero unsupported values without string coercion.
				const std::array<float, 2> coordinate{EmoteNativeFloat(arguments[1]), EmoteNativeFloat(arguments[2])};
				const auto duration = arguments.size() >= 4 ? EmoteNativeFloat(arguments[3]) : 0.0f;
				const auto easing = arguments.size() >= 5 ? EmoteNativeFloat(arguments[4]) : 0.0f;
				record->Task->Player().SetCoordinate(coordinate, duration, easing);
				return SquirrelNativeResult::NoValues();
			}
			if (method == EmoteNativeMethod::SetScale) {
				if (arguments.size() < 2 || arguments.size() > 4) {
					throw std::runtime_error("invalid argument.");
				}
				const auto scale = EmoteNativeFloat(arguments[1]);
				const auto duration = arguments.size() >= 3 ? EmoteNativeFloat(arguments[2]) : 0.0f;
				const auto easing = arguments.size() >= 4 ? EmoteNativeFloat(arguments[3]) : 0.0f;
				record->Task->Player().SetScale(scale, duration, easing);
				return SquirrelNativeResult::NoValues();
			}
			if (method == EmoteNativeMethod::SetRotation) {
				if (arguments.size() < 2 || arguments.size() > 4) {
					throw std::runtime_error("invalid argument.");
				}
				const auto radians = EmoteNativeFloat(arguments[1]);
				const auto duration = arguments.size() >= 3 ? EmoteNativeFloat(arguments[2]) : 0.0f;
				const auto easing = arguments.size() >= 4 ? EmoteNativeFloat(arguments[3]) : 0.0f;
				record->Task->Player().SetRotation(radians, duration, easing);
				return SquirrelNativeResult::NoValues();
			}
			if (method == EmoteNativeMethod::SetColor) {
				if (arguments.size() < 2 || arguments.size() > 4) {
					throw std::runtime_error("invalid argument.");
				}
				// Native227920 zero-initializes the failed integer result; it does not throw.
				const auto color = TryNativeInteger(arguments[1]).value_or(0);
				const auto duration = arguments.size() >= 3 ? EmoteNativeFloat(arguments[2]) : 0.0f;
				const auto easing = arguments.size() >= 4 ? EmoteNativeFloat(arguments[3]) : 0.0f;
				record->Task->Player().SetColor(std::bit_cast<std::uint32_t>(color), duration, easing);
				return SquirrelNativeResult::NoValues();
			}
			if (method == EmoteNativeMethod::Playing || method == EmoteNativeMethod::Stop) {
				if (arguments.size() > 2) {
					throw std::runtime_error("invalid argument.");
				}
				const auto* text = arguments.size() == 2 ? TryStringText(arguments[1]) : nullptr;
				if (arguments.size() == 2 && text == nullptr) {
					throw std::runtime_error("invalid argument.");
				}
				const auto name = text == nullptr ? std::string{} : text->substr(0, text->find('\0'));
				if (method == EmoteNativeMethod::Playing) {
					return SquirrelNativeResult::ReturnValue(record->Task->Player().TimelinePlaying(name));
				}
				record->Task->Player().StopTimeline(name);
				return SquirrelNativeResult::NoValues();
			}
			if (method == EmoteNativeMethod::Play) {
				if (arguments.size() < 2 || arguments.size() > 4 || !TryStringText(arguments[1])) {
					throw std::runtime_error("invalid argument.");
				}
				const auto& name = StringText(arguments[1]);
				const auto flags = arguments.size() == 3 ? static_cast<std::uint32_t>(TryNativeInteger(arguments[2]).value_or(0)) : 0;
				record->Task->Player().PlayTimeline(name, flags);
				return SquirrelNativeResult::NoValues();
			}
			const bool setting = method == EmoteNativeMethod::SetVariable || method == EmoteNativeMethod::SetBlend;
			if ((setting && (arguments.size() < 3 || arguments.size() > 5)) || (!setting && (arguments.size() < 2 || arguments.size() > 4))) {
				throw std::runtime_error("invalid argument.");
			}
			// Native string conversion may reenter VM/reallocate operands; snapshot every subsequent scalar first.
			const auto name_input = arguments[1];
			const auto value_input = setting ? arguments[2] : SquirrelValue{};
			const auto duration_input = arguments.size() >= (setting ? 4U : 3U) ? arguments[setting ? 3 : 2] : SquirrelValue{};
			const auto easing_input = arguments.size() >= (setting ? 5U : 4U) ? arguments[setting ? 4 : 3] : SquirrelValue{};
			const auto name = Name(name_input);
			const auto value = EmoteNativeFloat(value_input);
			const auto duration = EmoteNativeFloat(duration_input);
			const auto easing = EmoteNativeFloat(easing_input);
			record = Record(receiver);
			if (!record->Detached) {
				auto& player = record->Task->Player();
				switch (method) {
				case EmoteNativeMethod::SetVariable:
					player.SetVariable(name, value, duration, easing);
					break;
				case EmoteNativeMethod::SetBlend:
					player.SetTimelineBlendRatio(name, value, duration, easing);
					break;
				case EmoteNativeMethod::FadeIn:
					player.FadeInTimeline(name, duration, easing);
					break;
				case EmoteNativeMethod::FadeOut:
					player.FadeOutTimeline(name, duration, easing);
					break;
				default:
					throw std::runtime_error("Emote native method has no recovered actual owner route");
				}
			}
			return SquirrelNativeResult::NoValues();
		}

		SquirrelVm& Vm;
		ScriptObjectBindings& Objects;
		ScriptLayerBindings& Layers;
		ScriptPsbBindings& Psb;
		ScriptEmoteBindingServices Services;
		std::size_t Maximum;
		ScriptEmoteRegistrationFacet Registration;
		std::map<std::uint32_t, std::shared_ptr<EmoteNativeRecord>> Records;
		const std::thread::id Thread = std::this_thread::get_id();
		bool Constructing = false;
		bool Closed = false;
	};

	ScriptEmoteBindings::ScriptEmoteBindings(SquirrelVm& vm, ScriptObjectBindings& objects, ScriptLayerBindings& layers, ScriptPsbBindings& psb, ScriptEmoteBindingServices services, std::size_t maximum_instances)
	    : m_Implementation(std::make_shared<Implementation>(vm, objects, layers, psb, std::move(services), maximum_instances)) {
		auto& state = *m_Implementation;
		const std::weak_ptr<Implementation> weak = m_Implementation;
		const auto actual_class = state.Registration.EmoteClass();
		vm.NewSlot(actual_class, "constructor", vm.CreateNativeClosure("Emote.constructor", [weak](SquirrelVm&, auto arguments) {
			const auto owner = weak.lock();
			if (!owner) {
				throw std::runtime_error("Emote actual constructor services expired");
			}
			owner->Construct(arguments, weak);
			return SquirrelNativeResult::NoValues();
		}));
		const std::array<std::pair<std::string_view, EmoteNativeMethod>, 49> methods{{{"skip", EmoteNativeMethod::Skip}, {"pass", EmoteNativeMethod::Pass}, {"getAnimating", EmoteNativeMethod::GetAnimating}, {"assignState", EmoteNativeMethod::AssignState}, {"startWind", EmoteNativeMethod::StartWind}, {"stopWind", EmoteNativeMethod::StopWind}, {"setCoord", EmoteNativeMethod::SetCoordinate}, {"getLeft", EmoteNativeMethod::GetLeft}, {"getTop", EmoteNativeMethod::GetTop}, {"setScale", EmoteNativeMethod::SetScale}, {"getScale", EmoteNativeMethod::GetScale}, {"setRot", EmoteNativeMethod::SetRotation}, {"getRot", EmoteNativeMethod::GetRotation}, {"setMeshDivisionRatio", EmoteNativeMethod::SetMeshDivision}, {"getMeshDivisionRatio", EmoteNativeMethod::GetMeshDivision}, {"setBustScale", EmoteNativeMethod::SetBustScale}, {"getBustScale", EmoteNativeMethod::GetBustScale}, {"setHairScale", EmoteNativeMethod::SetHairScale}, {"getHairScale", EmoteNativeMethod::GetHairScale}, {"setPartsScale", EmoteNativeMethod::SetPartsScale}, {"getPartsScale", EmoteNativeMethod::GetPartsScale}, {"setColor", EmoteNativeMethod::SetColor}, {"getColor", EmoteNativeMethod::GetColor}, {"pause", EmoteNativeMethod::Pause}, {"clear", EmoteNativeMethod::Clear}, {"setVariable", EmoteNativeMethod::SetVariable}, {"getVariable", EmoteNativeMethod::GetVariable},
		    {"setBlinkEnabled", EmoteNativeMethod::SetBlink}, {"getBlinkEnabled", EmoteNativeMethod::GetBlink},
		    {"setVisible", EmoteNativeMethod::SetVisible}, {"getVisible", EmoteNativeMethod::GetVisible},
		    {"playTimeline", EmoteNativeMethod::Play}, {"stopTimeline", EmoteNativeMethod::Stop}, {"getTimelinePlaying", EmoteNativeMethod::Playing},
		    {"setTimelineBlendRatio", EmoteNativeMethod::SetBlend}, {"getTimelineBlendRatio", EmoteNativeMethod::GetBlend},
		    {"fadeInTimeline", EmoteNativeMethod::FadeIn}, {"fadeOutTimeline", EmoteNativeMethod::FadeOut},
		    {"getMainTimelineLabelList", EmoteNativeMethod::MainLabels}, {"getDiffTimelineLabelList", EmoteNativeMethod::DifferenceLabels},
		    {"playMotion", EmoteNativeMethod::Play}, {"stopMotion", EmoteNativeMethod::Stop}, {"getMotionPlaying", EmoteNativeMethod::Playing},
		    {"setMotionBlendRatio", EmoteNativeMethod::SetBlend}, {"getMotionBlendRatio", EmoteNativeMethod::GetBlend},
		    {"fadeInMotion", EmoteNativeMethod::FadeIn}, {"fadeOutMotion", EmoteNativeMethod::FadeOut},
		    {"getMainMotionLabelList", EmoteNativeMethod::MainLabels}, {"getDiffMotionLabelList", EmoteNativeMethod::DifferenceLabels}}};
		for (const auto& [name, method] : methods) {
			const auto descriptor = EmoteCodeDescriptor(method);
			const std::array<SquirrelValue, 1> captures{vm.CreateUserData(descriptor)};
			vm.NewSlot(actual_class, std::string(name), vm.CreateNativeClosureWithCaptures("Emote." + std::string(name), [weak, method, descriptor](SquirrelVm& active, auto arguments) {
				const auto owner = weak.lock();
				if (!owner || &owner->Vm != &active || arguments.empty()) {
					throw std::runtime_error("Emote native route has expired or has no original receiver");
				}
				const auto bytes = active.UserDataBytes(active.NativeStackValue(-1));
				if (bytes.size() != descriptor.size() || !std::equal(bytes.begin(), bytes.end(), descriptor.begin())) {
					throw std::runtime_error("Emote captured route does not match its actual native method");
				}
				if (method != EmoteNativeMethod::Skip && method != EmoteNativeMethod::Pass && method != EmoteNativeMethod::GetAnimating && method != EmoteNativeMethod::AssignState && method != EmoteNativeMethod::StartWind && method != EmoteNativeMethod::StopWind && method != EmoteNativeMethod::SetMeshDivision && method != EmoteNativeMethod::GetMeshDivision && method != EmoteNativeMethod::SetBustScale && method != EmoteNativeMethod::GetBustScale && method != EmoteNativeMethod::SetHairScale && method != EmoteNativeMethod::GetHairScale && method != EmoteNativeMethod::SetPartsScale && method != EmoteNativeMethod::GetPartsScale && method != EmoteNativeMethod::GetLeft && method != EmoteNativeMethod::GetTop && method != EmoteNativeMethod::GetScale && method != EmoteNativeMethod::GetRotation && method != EmoteNativeMethod::GetColor && method != EmoteNativeMethod::Pause && method != EmoteNativeMethod::Clear && method != EmoteNativeMethod::GetVariable && method != EmoteNativeMethod::GetBlend && method != EmoteNativeMethod::SetVisible && method != EmoteNativeMethod::GetVisible && method != EmoteNativeMethod::SetBlink && method != EmoteNativeMethod::GetBlink) {
					// Native04dc10 pops its code descriptor before calling raw-VM methods.
					active.SetNativeStackSize(active.NativeStackSize() - 1);
					arguments = arguments.first(arguments.size() - 1);
				}
				return owner->Dispatch(method, arguments);
			},
			                                                captures));
		}
	}
	ScriptEmoteBindings::~ScriptEmoteBindings() noexcept {
		if (!CanClose()) {
			std::terminate();
		}
	}
	SquirrelValue ScriptEmoteBindings::EmoteClass() const {
		return m_Implementation->Registration.EmoteClass();
	}
	scene::EmotePlayerTask ScriptEmoteBindings::Task(const SquirrelValue& instance) const {
		const auto record = m_Implementation->Record(instance);
		if (record->Detached || !record->Task) {
			throw std::runtime_error("Emote wrapper actual task has detached");
		}
		return *record->Task;
	}
	std::size_t ScriptEmoteBindings::InstanceCount() const noexcept {
		return m_Implementation->Records.size();
	}
	void ScriptEmoteBindings::ReleaseInstance(const SquirrelValue& instance) {
		m_Implementation->Release(instance);
	}
	bool ScriptEmoteBindings::CanClose() const noexcept {
		return m_Implementation->Thread == std::this_thread::get_id() && !m_Implementation->Constructing && m_Implementation->Records.empty();
	}
	void ScriptEmoteBindings::Close() {
		if (!CanClose()) {
			throw std::runtime_error("Emote binding close requires completed actual wrapper retirement");
		}
		m_Implementation->Closed = true;
	}
} // namespace sgre::script
