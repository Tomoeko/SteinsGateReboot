#include "sgre/script/screen_projection_bindings.hpp"

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>

namespace sgre::script {
	static bool ProjectionSmoothingArgument(std::span<const SquirrelValue> arguments) {
		if (arguments.size() < 2 || std::holds_alternative<std::monostate>(arguments[1])) {
			return false;
		}
		if (const auto* value = std::get_if<float>(&arguments[1])) {
			return *value != 0;
		}
		if (const auto* value = std::get_if<std::int32_t>(&arguments[1])) {
			return *value != 0;
		}
		if (const auto* value = std::get_if<bool>(&arguments[1])) {
			return *value;
		}

		return true;
	}

	static float ProjectionMeshArgument(std::span<const SquirrelValue> arguments, std::size_t index) {
		if (index < arguments.size()) {
			if (const auto* value = std::get_if<float>(&arguments[index])) {
				return *value;
			}
			if (const auto* value = std::get_if<std::int32_t>(&arguments[index])) {
				return static_cast<float>(*value);
			}
			if (const auto* value = std::get_if<bool>(&arguments[index])) {
				return *value ? 1.0f : 0.0f;
			}
		}

		// Native061290 initializes each float to zero and ignores failed222e90 numeric conversions.
		return 0;
	}

	struct ScreenProjectionView {
		std::array<scene::ScreenProjectionRegion, 4> Regions;
		std::size_t RegionCount = 1;
		std::uint32_t Color = 0xffffffff;
		std::uint32_t BlendMode = 0;
	};

	struct ScreenProjectionPayload {
		scene::ScreenRegistry& Screens;
		ScriptScreenProjectionBindings::DrawProjection Drawing;
		scene::ScreenHandle Screen;
		SquirrelHostRoot ScreenOwner;
		render::SpriteTextureHandle Texture;
		bool Live = true;
		bool Smoothing = false;
		bool Chromatic = false;
		std::array<ScreenProjectionView, 3> Views{};

		void Draw() const {
			if (!Live || !Screens.Contains(Screen)) {
				throw std::runtime_error("ScreenProjection source Screen has retired");
			}
			const auto description = Screens.State(Screen).Description;
			const auto texture = Screens.Renderer(Screen).Description();
			const resource::MotionPoint logical_size{static_cast<float>(description.Width), static_cast<float>(description.Height)};
			const resource::MotionPoint texture_size{static_cast<float>(texture.Width), static_cast<float>(texture.Height)};
			const scene::MotionBounds bounds{0, 0, logical_size.X, logical_size.Y};
			std::array<scene::ScreenProjectionVertex, 16> vertices;
			std::array<std::uint32_t, 24> indices;
			for (std::size_t index = 0; index < (Chromatic ? 3u : 1u); ++index) {
				const auto& view = Views[index];
				auto packet = scene::PrepareScreenProjectionPacket(bounds, logical_size, texture_size, 0, view.Color, std::span(view.Regions).first(view.RegionCount), vertices, indices);
				packet.Smoothing = Smoothing;
				packet.BlendMode = view.BlendMode;
				Drawing(Texture, packet);
			}
		}
	};

	struct ScreenProjectionBindingRecord {
		SquirrelObjectIdentity Instance;
		std::weak_ptr<ScreenProjectionPayload> Payload;
	};

	class ScriptScreenProjectionBindings::Implementation {
	public:
		Implementation(SquirrelVm& vm, ScriptLayerBindings& layers, ScriptScreenBindings& screen_bindings, scene::ScreenRegistry& screens, DrawProjection drawing)
		    : Vm(vm), Layers(layers), ScreenBindings(screen_bindings), Screens(screens), Drawing(std::move(drawing)) {
			if (!Drawing) {
				throw std::runtime_error("ScreenProjection requires its actual frame renderer recipient");
			}
		}

		SquirrelVm& Vm;
		ScriptLayerBindings& Layers;
		ScriptScreenBindings& ScreenBindings;
		scene::ScreenRegistry& Screens;
		ScriptScreenProjectionBindings::DrawProjection Drawing;
		SquirrelValue Class;
		SquirrelHostRoot ClassRoot;
		SquirrelValue ChromaticClass;
		SquirrelHostRoot ChromaticClassRoot;
		std::map<std::uint32_t, ScreenProjectionBindingRecord> Instances;

		std::shared_ptr<ScreenProjectionPayload> Payload(const SquirrelValue& instance) const {
			if (Vm.Type(instance) != SquirrelValueType::Instance) {
				throw std::runtime_error("ScreenProjection requires its native receiver");
			}
			const auto found = Instances.find(std::get<SquirrelObjectHandle>(instance).Index);
			if (found == Instances.end() || found->second.Instance != Vm.BorrowObjectIdentity(instance)) {
				throw std::runtime_error("ScreenProjection receiver has no live native owner");
			}
			const auto payload = found->second.Payload.lock();
			if (!payload || !payload->Live || !Screens.Contains(payload->Screen)) {
				throw std::runtime_error("ScreenProjection native task or source Screen has retired");
			}

			return payload;
		}

		void Construct(std::span<const SquirrelValue> arguments, const std::weak_ptr<Implementation>& weak, bool chromatic) {
			if ((arguments.size() != 2 && arguments.size() != 3) || !Vm.IsInstanceOf(arguments[0], chromatic ? ChromaticClass : Class) || !Vm.IsInstanceOf(arguments.back(), ScreenBindings.ScreenClass())) {
				throw std::runtime_error("invalid argument.");
			}
			const auto index = std::get<SquirrelObjectHandle>(arguments[0]).Index;
			if (Instances.contains(index) || Instances.size() == 65536) {
				throw std::runtime_error("ScreenProjection receiver is initialized or exceeds its bounded wrapper limit");
			}
			const auto screen = ScreenBindings.Handle(arguments.back());
			const auto texture = Screens.Renderer(screen).SampledTarget();
			auto payload = std::make_shared<ScreenProjectionPayload>(ScreenProjectionPayload{Screens, Drawing, screen, Vm.Retain(arguments.back()), texture});
			payload->Chromatic = chromatic;
			if (chromatic) {
				// Native061520 uses RGBA view colors; native4fa480 converts these to draw-record ARGB.
				payload->Views[0].Color = 0xffff0000;
				payload->Views[1].Color = 0xff00ff00;
				payload->Views[2].Color = 0xff0000ff;
				payload->Views[1].BlendMode = 1;
				payload->Views[2].BlendMode = 1;
			}
			const std::array<SquirrelValue, 2> base_arguments{arguments[0], arguments.size() == 3 ? arguments[1] : SquirrelValue{}};
			Layers.ConstructBaseInstance(base_arguments);
			try {
				ScreenProjectionBindingRecord record;
				record.Instance = Vm.BorrowObjectIdentity(arguments[0]);
				record.Payload = payload;
				Instances.emplace(index, std::move(record));
				Layers.ConfigureBaseRetirement(
				    arguments[0], [weak](const auto& instance) {
					    if (const auto owner = weak.lock()) {
						    const auto found = owner->Instances.find(std::get<SquirrelObjectHandle>(instance).Index);
						    if (found == owner->Instances.end() || found->second.Instance != owner->Vm.BorrowObjectIdentity(instance) || found->second.Payload.expired()) {
							    throw std::runtime_error("ScreenProjection retirement requires its actual native payload identity");
						    }
					    }
				    },
				    [weak](const auto& instance) {
					    if (const auto owner = weak.lock()) {
						    const auto found = owner->Instances.find(std::get<SquirrelObjectHandle>(instance).Index);
						    const auto payload = found == owner->Instances.end() ? std::shared_ptr<ScreenProjectionPayload>{} : found->second.Payload.lock();
						    owner->Instances.erase(std::get<SquirrelObjectHandle>(instance).Index);
						    // Native244c80 drops the strong Screen before the actual SQLayerBase graph retires.
						    if (payload) {
							    payload->ScreenOwner = {};
						    }
					    }
				    });
				scene::ParallelTaskCallbacks callbacks;
				callbacks.Draw = [payload](auto, auto) {
					payload->Draw();
				};
				callbacks.Release = [payload](auto) {
					payload->Live = false;
					payload->ScreenOwner = {};
				};
				static_cast<void>(Layers.BaseTasks(arguments[0]).Children().Append(std::move(callbacks)));
			} catch (...) {
				Layers.ReleaseInstance(arguments[0]);
				Instances.erase(index);
				throw;
			}
		}
	};

	ScriptScreenProjectionBindings::ScriptScreenProjectionBindings(SquirrelVm& vm, ScriptLayerBindings& layers, ScriptScreenBindings& screen_bindings, scene::ScreenRegistry& screens, DrawProjection drawing)
	    : m_Implementation(std::make_shared<Implementation>(vm, layers, screen_bindings, screens, std::move(drawing))) {
		auto& state = *m_Implementation;
		state.Class = vm.CreateClass(layers.LayerBaseClass());
		state.ClassRoot = vm.Retain(state.Class);
		state.ChromaticClass = vm.CreateClass(layers.LayerBaseClass());
		state.ChromaticClassRoot = vm.Retain(state.ChromaticClass);
		const auto weak = std::weak_ptr<Implementation>(m_Implementation);
		const auto method = [&](const SquirrelValue& target, const std::string& name, auto operation) {
			const auto callback = vm.CreateNativeClosure("ScreenProjection." + name, [weak, operation](SquirrelVm&, std::span<const SquirrelValue> arguments) {
				const auto owner = weak.lock();
				if (!owner || arguments.empty()) {
					throw std::runtime_error("ScreenProjection callback owner has expired or lacks its receiver");
				}
				SquirrelNativeResult result;
				result.Value = operation(*owner, arguments);

				return result;
			});
			vm.NewSlot(target, name, callback);
		};
		method(state.Class, "constructor", [weak](Implementation& owner, const auto arguments) -> SquirrelValue {
			owner.Construct(arguments, weak, false);

			return {};
		});
		method(state.Class, "setSmoothing", [](Implementation& owner, const auto arguments) -> SquirrelValue {
			const auto payload = owner.Payload(arguments[0]);
			// Native245d90 invokes the common Layer setter before updating this owned view's two filter fields.
			const auto smoothing = ProjectionSmoothingArgument(arguments);
			owner.Layers.SetSmoothing(arguments[0], smoothing);
			payload->Smoothing = smoothing;

			return {};
		});

		method(state.ChromaticClass, "constructor", [weak](Implementation& owner, const auto arguments) -> SquirrelValue {
			owner.Construct(arguments, weak, true);

			return {};
		});
		for (const auto& target : {state.Class, state.ChromaticClass}) {
			method(target, "getWidth", [](Implementation& owner, const auto arguments) -> SquirrelValue {
				return static_cast<float>(owner.Screens.State(owner.Payload(arguments[0])->Screen).Description.Width);
			});
			method(target, "getHeight", [](Implementation& owner, const auto arguments) -> SquirrelValue {
				return static_cast<float>(owner.Screens.State(owner.Payload(arguments[0])->Screen).Description.Height);
			});
		}
		const std::array<std::string, 3> mesh_methods{"setRMesh", "setGMesh", "setBMesh"};
		for (std::size_t index = 0; index < mesh_methods.size(); ++index) {
			method(state.ChromaticClass, mesh_methods[index], [index](Implementation& owner, const auto arguments) -> SquirrelValue {
				const auto payload = owner.Payload(arguments[0]);
				if (!payload->Chromatic) {
					throw std::runtime_error("ScreenProjectionCA mesh method requires its actual three-view owner");
				}
				const auto description = owner.Screens.State(payload->Screen).Description;
				const resource::MotionPoint dimensions{static_cast<float>(description.Width), static_cast<float>(description.Height)};
				const resource::MotionPoint displacement{ProjectionMeshArgument(arguments, 1), ProjectionMeshArgument(arguments, 2)};
				auto& view = payload->Views[index];
				view.Regions = scene::PrepareChromaticProjectionRegions(dimensions, displacement, ProjectionMeshArgument(arguments, 3));
				view.RegionCount = view.Regions.size();

				return {};
			});
		}
		vm.NewSlot(vm.RootTable(), std::string("ScreenProjection"), state.Class);
		vm.NewSlot(vm.RootTable(), std::string("ScreenProjectionCA"), state.ChromaticClass);
	}

	ScriptScreenProjectionBindings::~ScriptScreenProjectionBindings() = default;

	SquirrelValue ScriptScreenProjectionBindings::ProjectionClass() const {
		return m_Implementation->Class;
	}
	SquirrelValue ScriptScreenProjectionBindings::ChromaticProjectionClass() const {
		return m_Implementation->ChromaticClass;
	}
} // namespace sgre::script
