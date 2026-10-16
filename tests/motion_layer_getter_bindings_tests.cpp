#include "sgre/script/motion_bindings.hpp"

#include "sgre/core/frame_clock.hpp"
#include "sgre/scene/motion_scene_draw_context.hpp"
#include "sgre/scene/motion_scene_evaluation.hpp"
#include "sgre/scene/motion_scene_player.hpp"
#include "sgre/script/motion_resource_owner.hpp"
#include "sgre/script/psb_bindings.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <initializer_list>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

static void Require(bool value, std::string_view message) {
	if (!value) {
		throw std::runtime_error(std::string(message));
	}
}

template <typename Operation>
static void RequireFailure(Operation operation, std::string_view expected) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(expected) != std::string_view::npos, error.what());
		return;
	}

	throw std::runtime_error("Motion getter fixture accepted an invalid native owner or operation");
}

template <typename Value>
static sgre::resource::PsbValue Atom(Value value) {
	sgre::resource::PsbValue result;
	result.Data = std::move(value);
	return result;
}

static sgre::resource::PsbValue Integer(std::int64_t value) {
	return Atom(value);
}

static sgre::resource::PsbValue Layer(bool child, std::uint32_t shape, bool empty_child = false) {
	using sgre::resource::PsbArray;
	using sgre::resource::PsbObject;
	const auto attributes = child ? Atom(PsbObject{{"mask", Integer(0)}, {"src", Atom(std::string(empty_child ? "" : "inner"))}, {"icon", Atom(std::string(empty_child ? "" : "main"))}}) : Atom(PsbObject{{"mask", Integer(98)}, {"coord", Atom(PsbArray{Atom(20.0), Atom(40.0), Atom(0.0)})}, {"zx", Atom(2.0)}, {"zy", Atom(3.0)}});
	const auto visible = Atom(PsbObject{{"time", Atom(0.0)}, {"type", Integer(2)}, {"content", attributes}});
	const auto terminal = Atom(PsbObject{{"time", Atom(8.0)}, {"type", Integer(0)}, {"content", Atom(std::monostate{})}});
	return Atom(PsbObject{{"label", Atom(std::string(child ? "actual child label" : "different from lookup key"))}, {"type", Integer(child ? 3 : 1)}, {"shape", Integer(shape)}, {"inheritMask", Integer(4)}, {"coordinate", Integer(0)}, {"transformOrder", Atom(PsbArray{Integer(0), Integer(3), Integer(2), Integer(1)})}, {"frameList", Atom(PsbArray{visible, terminal})}, {"children", Atom(PsbArray{})}, {"stencilType", Integer(0)}, {"meshTransform", Integer(0)}, {"parameterize", Atom(std::monostate{})}, {"joinTarget", Atom(true)}, {"groundCorrection", Atom(false)}, {"exportSelf", Atom(true)}, {"motionIndependentLayerInherit", Atom(false)}});
}

static sgre::resource::PsbValue Timeline(bool inner, std::uint32_t shape, bool empty_prefix) {
	using sgre::resource::PsbArray;
	using sgre::resource::PsbObject;
	auto layers = PsbArray{Layer(false, inner ? 1 : shape)};
	if (!inner) {
		if (empty_prefix) {
			layers.push_back(Layer(true, 0, true));
		}
		layers.push_back(Layer(true, 0));
	}
	PsbArray order;
	for (std::size_t index = 0; index != layers.size(); ++index) {
		order.push_back(Integer(static_cast<std::int64_t>(index)));
	}
	const auto priority = Atom(PsbObject{{"time", Atom(0.0)}, {"type", Integer(1)}, {"content", Atom(order)}});
	const auto terminal = Atom(PsbObject{{"time", Atom(8.0)}, {"type", Integer(1)}, {"content", Atom(std::monostate{})}});
	auto names = inner ? PsbObject{{"nested map key", Integer(0)}} : PsbObject{{"rectangle map key", Integer(0)}, {"child map key", Integer(empty_prefix ? 2 : 1)}};
	if (!inner && empty_prefix) {
		names.emplace_back("empty child map key", Integer(1));
	}
	return Atom(PsbObject{{"type", Integer(0)}, {"bounds", Atom(PsbObject{{"left", Atom(0.0)}, {"top", Atom(0.0)}, {"right", Atom(64.0)}, {"bottom", Atom(96.0)}})}, {"lastTime", Atom(8.0)}, {"loopTime", Atom(-1.0)}, {"layer", Atom(std::move(layers))}, {"layerIndexMap", Atom(names)}, {"priority", Atom(PsbArray{priority, terminal})}, {"tag", Atom(PsbArray{})}, {"parameter", Atom(std::monostate{})}, {"variable", Atom(std::monostate{})}, {"parameterize", Atom(std::monostate{})}});
}

static std::shared_ptr<const sgre::resource::PsbDocument> Document(std::uint32_t shape, bool empty_prefix) {
	using sgre::resource::PsbArray;
	using sgre::resource::PsbObject;
	const auto object = [shape, empty_prefix](bool inner) {
		return Atom(PsbObject{{"type", Integer(0)}, {"motion", Atom(PsbObject{{"main", Timeline(inner, shape, empty_prefix)}})}});
	};
	auto document = std::make_shared<sgre::resource::PsbDocument>();
	document->Header.Version = 4;
	document->Header.RootOffset = 44;
	document->Header.DataOffset = 64;
	document->SerializedSize = 64;
	document->Root = Atom(PsbObject{{"id", Atom(std::string("motion"))}, {"spec", Atom(std::string("win"))}, {"version", Atom(3.03)}, {"label", Atom(std::string("Authored actual shape selection"))}, {"easing", Atom(PsbArray{})}, {"source", Atom(PsbObject{})}, {"object", Atom(PsbObject{{"outer", object(false)}, {"inner", object(true)}})}, {"sourceIconRenameMap", Atom(PsbObject{})}});
	document->Root.SerializedOffset = 44;
	return document;
}

static sgre::resource::PsbValue& FixtureField(sgre::resource::PsbValue& object, std::string_view name) {
	for (auto& entry : std::get<sgre::resource::PsbObject>(object.Data)) {
		if (entry.first == name) {
			return entry.second;
		}
	}

	throw std::runtime_error("borrowed-child fixture field is absent");
}

static std::shared_ptr<const sgre::resource::PsbDocument> NestedGetterDocument() {
	using sgre::resource::PsbArray;
	using sgre::resource::PsbObject;
	auto outer = Timeline(false, 2, false);
	auto inner = Timeline(true, 2, false);
	auto deep = Timeline(true, 1, false);
	FixtureField(outer, "layerIndexMap") = Atom(PsbObject{{"shared lookup key", Integer(0)}, {"outer only", Integer(0)}, {"child map key", Integer(1)}});
	FixtureField(inner, "layerIndexMap") = Atom(PsbObject{{"shared lookup key", Integer(0)}, {"inner only", Integer(0)}, {"deep child key", Integer(1)}});
	FixtureField(deep, "layerIndexMap") = Atom(PsbObject{{"deep shape key", Integer(0)}});
	auto& inner_layers = std::get<PsbArray>(FixtureField(inner, "layer").Data);
	FixtureField(inner_layers[0], "shape") = Integer(2);
	auto& inner_frame = std::get<PsbArray>(FixtureField(inner_layers[0], "frameList").Data)[0];
	FixtureField(FixtureField(inner_frame, "content"), "coord") = Atom(PsbArray{Atom(7.0), Atom(11.0), Atom(0.0)});
	auto child = Layer(true, 0);
	auto& child_frame = std::get<PsbArray>(FixtureField(child, "frameList").Data)[0];
	FixtureField(FixtureField(child_frame, "content"), "src") = Atom(std::string("deep"));
	inner_layers.push_back(std::move(child));
	auto& inner_priority = std::get<PsbArray>(FixtureField(inner, "priority").Data)[0];
	FixtureField(inner_priority, "content") = Atom(PsbArray{Integer(0), Integer(1)});
	auto& deep_layer = std::get<PsbArray>(FixtureField(deep, "layer").Data)[0];
	auto& deep_frame = std::get<PsbArray>(FixtureField(deep_layer, "frameList").Data)[0];
	FixtureField(FixtureField(deep_frame, "content"), "coord") = Atom(PsbArray{Atom(3.0), Atom(5.0), Atom(0.0)});
	const auto object = [](sgre::resource::PsbValue timeline) {
		return Atom(PsbObject{{"type", Integer(0)}, {"motion", Atom(PsbObject{{"main", std::move(timeline)}})}});
	};
	auto document = std::make_shared<sgre::resource::PsbDocument>(*Document(2, false));
	FixtureField(document->Root, "label") = Atom(std::string("Authored scoped borrowed-child getters"));
	FixtureField(document->Root, "object") = Atom(PsbObject{{"outer", object(std::move(outer))}, {"inner", object(std::move(inner))}, {"deep", object(std::move(deep))}});

	return document;
}

static sgre::script::SquirrelVmLimits VmLimits() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	return limits;
}

class Fixture {
public:
	explicit Fixture(std::uint32_t shape = 2, bool empty_prefix = false, std::shared_ptr<const sgre::resource::PsbDocument> document = {})
	    : Vm(VmLimits()), Objects(Vm), Psb(Vm, Objects), Cache([this](auto, const auto&) -> sgre::render::SpriteTextureHandle {
		      ++Uploads;
		      throw std::runtime_error("actual shape owner must never upload a replacement atlas");
	      },
	                                                         [](auto) {
	                                                         }),
	      Assets(Cache), Tree(8), Layers(Vm, Objects, Tree), Clock(0) {
		try {
			Vm.InstallRecoveredBaseLibrary();
			Layers.ConfigureMotionOwners(Psb, Assets, Cache);
			Leaf = Vm.Invoke(Layers.LayerClass(), std::array<sgre::script::SquirrelValue, 1>{Vm.RootTable()}).Value;
			LeafRoot = Vm.Retain(Leaf);
			for (std::size_t index = 0; index != Seed.size(); ++index) {
				Seed[index] = static_cast<std::uint32_t>(index) * 17 + 3;
			}
			sgre::scene::MotionManagerRuntimeServices services;
			services.ReadSourceAliases = [this](auto project) {
				return Layers.MotionResources(Leaf).SourceAliases(project);
			};
			services.SceneAssets = &Assets;
			services.ReadRegisteredAsset = [this](auto project) -> const sgre::scene::LoadedMotionAsset& {
				return Layers.MotionResources(Leaf).BorrowAsset(project);
			};
			auto& resources = Layers.MotionResources(Leaf);
			Primary = std::make_unique<sgre::scene::MotionManagerRuntime>(resources.Primary(), Clock, Seed, services, 32);
			Secondary = std::make_unique<sgre::scene::MotionManagerRuntime>(resources.Secondary(), Clock, Seed, services, 32);
			Layers.AttachMotionRuntime(Leaf, *Primary);
			Layers.AttachMotionRuntime(Leaf, *Secondary);
			sgre::script::ScriptMotionBindingServices binding;
			binding.ReadAsynchronousMode = [] {
				return false;
			};
			binding.ResolveRuntime = [this](const auto& leaf, auto source) -> sgre::scene::MotionManagerRuntime& {
				Require(leaf == Leaf, "Motion factory resolved another actual Layer");
				return source == sgre::script::MotionLayerSource::Primary ? *Primary : *Secondary;
			};
			Bindings = std::make_unique<sgre::script::ScriptMotionBindings>(Vm, Objects, Layers, std::move(binding));
			const auto psb = Psb.WrapDocument(document ? std::move(document) : Document(shape, empty_prefix), std::make_shared<const std::vector<std::uint8_t>>(64, 0));
			static_cast<void>(Call(Leaf, "registerMotionResource", {psb}));
		} catch (...) {
			const auto failure = std::current_exception();
			Close();
			std::rethrow_exception(failure);
		}
	}

	~Fixture() noexcept {
		try {
			Close();
		} catch (...) {
			std::terminate();
		}
	}

	void Close() {
		if (Closed) {
			return;
		}
		Vm.ResetExecution();
		if (std::holds_alternative<sgre::script::SquirrelObjectHandle>(Leaf) && Objects.HasInstance(Leaf)) {
			Layers.ReleaseInstance(Leaf);
		}
		if (Bindings) {
			Bindings->NativeOwners().Close();
		}
		if (Primary) {
			Primary->Close();
		}
		if (Secondary) {
			Secondary->Close();
		}
		Vm.Close();
		Cache.Close();
		Closed = true;
	}

	sgre::script::SquirrelValue Call(const sgre::script::SquirrelValue& instance, std::string_view method, std::initializer_list<sgre::script::SquirrelValue> extra = {}) {
		std::vector<sgre::script::SquirrelValue> arguments{instance};
		arguments.insert(arguments.end(), extra.begin(), extra.end());
		return Vm.Invoke(Vm.GetSlot(instance, std::string(method)), arguments).Value;
	}

	sgre::script::SquirrelValue Read(const sgre::script::SquirrelValue& instance, std::string_view name) {
		return Vm.ReadSlotInContext(Vm.RootContext(), instance, std::string(name));
	}

	sgre::scene::ParallelTaskSet& Second() {
		return Layers.MotionTasks(Leaf).Group(sgre::scene::MotionTaskGroup::Second);
	}

	sgre::script::SquirrelVm Vm;
	sgre::script::ScriptObjectBindings Objects;
	sgre::script::ScriptPsbBindings Psb;
	std::size_t Uploads = 0;
	sgre::scene::TextureCache Cache;
	sgre::scene::MotionAssetCatalog Assets;
	sgre::scene::LayerTree Tree;
	sgre::script::ScriptLayerBindings Layers;
	sgre::core::FrameClock Clock;
	std::array<std::uint32_t, sgre::scene::kMotionSharedSeedWords> Seed{};
	sgre::script::SquirrelValue Leaf;
	sgre::script::SquirrelHostRoot LeafRoot;
	std::unique_ptr<sgre::scene::MotionManagerRuntime> Primary;
	std::unique_ptr<sgre::scene::MotionManagerRuntime> Secondary;
	std::unique_ptr<sgre::script::ScriptMotionBindings> Bindings;
	bool Closed = false;
};

static void TestRealMapSelectionAndSnapshotLifetime() {
	Fixture fixture;
	auto motion = fixture.Vm.Invoke(fixture.Bindings->MotionClass(), std::array<sgre::script::SquirrelValue, 2>{fixture.Vm.RootTable(), fixture.Leaf}).Value;
	const auto motion_root = fixture.Vm.Retain(motion);
	Require(std::holds_alternative<std::monostate>(fixture.Call(motion, "getLayerGetter", {std::string("rectangle map key")})), "an actual unselected player has no invented layer catalogue");
	static_cast<void>(fixture.Call(motion, "setChara", {std::string("outer")}));
	static_cast<void>(fixture.Call(motion, "setVisible", {true}));
	static_cast<void>(fixture.Call(motion, "play", {std::string("main")}));
	Require(std::holds_alternative<std::monostate>(fixture.Call(motion, "getLayerGetter", {std::string("different from lookup key")})), "getter must use the real LayerIndexMap rather than raw layer labels");
	const auto getter = fixture.Call(motion, "getLayerGetter", {std::string("rectangle map key\0ignored", 25), std::int32_t{17}});
	const auto getter_root = fixture.Vm.Retain(getter);
	Require(std::get<std::int32_t>(fixture.Read(getter, "type")) == 1, "actual getter.type must report the selected kind1");
	const auto initial = fixture.Read(getter, "shape");
	const auto initial_root = fixture.Vm.Retain(initial);
	Require(std::get<float>(fixture.Read(initial, "width")) == 0, "native constructor snapshot must copy retained zero geometry before the first real update");
	fixture.Second().Update();
	const auto snapshot = fixture.Read(getter, "shape");
	const auto snapshot_root = fixture.Vm.Retain(snapshot);
	Require(std::get<std::int32_t>(fixture.Read(snapshot, "type")) == 2 && std::get<float>(fixture.Read(snapshot, "left")) == 4 && std::get<float>(fixture.Read(snapshot, "top")) == 16 && std::get<float>(fixture.Read(snapshot, "right")) == 36 && std::get<float>(fixture.Read(snapshot, "bottom")) == 64 && std::get<float>(fixture.Read(snapshot, "width")) == 32 && std::get<float>(fixture.Read(snapshot, "height")) == 48, "shape2 fields must copy the actual current native signed half-extent arithmetic");
	Require(std::get<bool>(fixture.Call(snapshot, "contains", {4.0f, 16.0f})) && !std::get<bool>(fixture.Call(snapshot, "contains", {36.0f, 16.0f})) && !std::get<bool>(fixture.Call(snapshot, "contains", {4.0f, 64.0f})), "rectangle containment must include left/top and exclude right/bottom");
	static_cast<void>(fixture.Call(snapshot, "set", {std::string("left"), 1000.0f}));
	static_cast<void>(fixture.Call(snapshot, "set", {std::string("type"), std::int32_t{0}}));
	Require(std::get<float>(fixture.Read(snapshot, "left")) == 1000 && std::get<std::int32_t>(fixture.Read(snapshot, "type")) == 0 && std::get<bool>(fixture.Call(snapshot, "contains", {20.0f, 40.0f})) && !std::get<bool>(fixture.Call(snapshot, "contains", {1000.0f, 40.0f})), "ordinary Object field mutation must not alter the independent native geometry copy used by contains");
	const auto fresh = fixture.Read(getter, "shape");
	const auto fresh_root = fixture.Vm.Retain(fresh);
	Require(std::get<std::int32_t>(fixture.Read(fresh, "type")) == 2 && std::get<float>(fixture.Read(fresh, "left")) == 4, "a new shape snapshot must read actual current geometry rather than a previous user-edited Object dictionary");
	const auto task = fixture.Bindings->NativeOwners().Task(motion);
	const auto* scene = task.GenericPlayer()->Scene();
	const auto nested_reference = scene->FindLayer("nested map key");
	Require(nested_reference && nested_reference->GlobalOrdinal == 4, "recursive lookup must count the actual enclosing root and both selected nodes before child root plus shape");
	const auto nested = fixture.Call(motion, "getLayerGetter", {std::string("nested map key")});
	const auto nested_root = fixture.Vm.Retain(nested);
	Require(std::get<std::int32_t>(fixture.Read(nested, "type")) == 1 && std::get<std::int32_t>(fixture.Read(fixture.Read(nested, "shape"), "type")) == 1, "nested actual selected player must expose its own circle shape");
	const auto child = fixture.Call(motion, "getLayerGetter", {std::string("child map key")});
	Require(std::get<std::int32_t>(fixture.Read(child, "type")) == 3, "getter.type must also report actual nonshape kinds");
	RequireFailure([&] {
		static_cast<void>(fixture.Call(child, "getShape"));
	},
	    "shape");
	Require(fixture.Uploads == 0 && std::all_of(scene->Packets().Nodes().begin(), scene->Packets().Nodes().end(), [](const auto& packet) {
		return packet.Command.Geometry == nullptr;
	}),
	    "actual shape ownership must not create an atlas or geometry; the genuine child may retain its structural packet");
	static_cast<void>(fixture.Call(motion, "play", {std::string("main"), std::int32_t{1}}));
	RequireFailure([&] {
		static_cast<void>(fixture.Call(getter, "getType"));
	},
	    "root selection");
	Require(std::get<float>(fixture.Read(snapshot, "width")) == 32 && std::get<bool>(fixture.Call(snapshot, "contains", {20.0f, 40.0f})), "independent copied shape must survive selected-scene replacement");
	fixture.Second().Update();
	const auto last = fixture.Call(motion, "getLayerGetter", {std::string("rectangle map key")});
	const auto last_root = fixture.Vm.Retain(last);
	fixture.Bindings->NativeOwners().ReleaseInstance(motion);
	fixture.Second().Update();
	Require(!task.IsLive(), "borrowed getters must not keep the actual Second task alive after real wrapper retirement");
	RequireFailure([&] {
		static_cast<void>(fixture.Call(last, "getShape"));
	},
	    "retired");
	Require(std::get<bool>(fixture.Call(snapshot, "contains", {20.0f, 40.0f})), "copied shape containment remains valid after the actual player retires");
	fixture.Vm.ResetExecution();
	static_cast<void>(fixture.Vm.CollectGarbage());
	Require(std::get<float>(fixture.Read(snapshot, "width")) == 32, "real retained shape Object must keep its weak native field delegate alive through collection");
}

static void TestAllSnapshotMapsAndContainment() {
	for (const auto type : {std::uint32_t{0}, std::uint32_t{1}, std::uint32_t{3}}) {
		Fixture fixture(type);
		const auto motion = fixture.Vm.Invoke(fixture.Bindings->MotionClass(), std::array<sgre::script::SquirrelValue, 2>{fixture.Vm.RootTable(), fixture.Leaf}).Value;
		const auto motion_root = fixture.Vm.Retain(motion);
		static_cast<void>(fixture.Call(motion, "setChara", {std::string("outer")}));
		static_cast<void>(fixture.Call(motion, "setVisible", {true}));
		static_cast<void>(fixture.Call(motion, "play", {std::string("main")}));
		fixture.Second().Update();
		const auto getter = fixture.Call(motion, "getLayerGetter", {std::string("rectangle map key")});
		const auto getter_root = fixture.Vm.Retain(getter);
		const auto snapshot = fixture.Read(getter, "shape");
		const auto snapshot_root = fixture.Vm.Retain(snapshot);
		Require(std::get<std::int32_t>(fixture.Read(snapshot, "type")) == static_cast<std::int32_t>(type), "snapshot map must retain its actual constructor shape enum");
		if (type == 0 || type == 1) {
			Require(std::get<float>(fixture.Read(snapshot, "x")) == 20 && std::get<float>(fixture.Read(snapshot, "y")) == 40, "point/circle maps must copy the actual current center");
			if (type == 0) {
				Require(!std::get<bool>(fixture.Call(snapshot, "contains", {20.0f, 40.0f})), "native point containment is always false even at the exact point");
			} else {
				Require(std::get<float>(fixture.Read(snapshot, "r")) == 16 && std::get<bool>(fixture.Call(snapshot, "contains", {36.0f, 40.0f})) && !std::get<bool>(fixture.Call(snapshot, "contains", {37.0f, 40.0f})), "circle map/containment must use signed scaleX radius and include the boundary");
			}
		} else {
			const auto points = fixture.Read(snapshot, "p");
			Require(fixture.Vm.Length(points) == 4, "quad snapshot must expose the native p array rather than an authored alias");
			const std::array<std::array<float, 2>, 4> expected{{{4, 16}, {36, 16}, {36, 64}, {4, 64}}};
			for (std::size_t index = 0; index != expected.size(); ++index) {
				const auto point = fixture.Vm.GetSlot(points, static_cast<std::int32_t>(index));
				Require(std::get<float>(fixture.Vm.GetSlot(point, std::string("x"))) == expected[index][0] && std::get<float>(fixture.Vm.GetSlot(point, std::string("y"))) == expected[index][1], "quad p object must retain native corner order and numeric values");
			}
			Require(std::get<float>(fixture.Read(snapshot, "width")) == 32 && std::get<float>(fixture.Read(snapshot, "height")) == 48 && std::get<bool>(fixture.Call(snapshot, "contains", {4.0f, 16.0f})) && !std::get<bool>(fixture.Call(snapshot, "contains", {3.0f, 16.0f})), "quad map bounds and inclusive edge containment must use the actual copied corners");
		}
	}
}

static void TestEmptyActualChildCatalogueCount() {
	Fixture fixture(2, true);
	const auto motion = fixture.Vm.Invoke(fixture.Bindings->MotionClass(), std::array<sgre::script::SquirrelValue, 2>{fixture.Vm.RootTable(), fixture.Leaf}).Value;
	const auto motion_root = fixture.Vm.Retain(motion);
	static_cast<void>(fixture.Call(motion, "setChara", {std::string("outer")}));
	static_cast<void>(fixture.Call(motion, "setVisible", {true}));
	static_cast<void>(fixture.Call(motion, "play", {std::string("main")}));
	const auto task = fixture.Bindings->NativeOwners().Task(motion);
	const auto* scene = task.GenericPlayer()->Scene();
	Require(scene && !scene->FindLayer("nested map key"), "genuine child commands exist before the first update without an invented selected child scene");
	fixture.Second().Update();
	const auto reference = scene->FindLayer("nested map key");
	Require(reference && reference->GlobalOrdinal == 6 && reference->NativeOrdinal == 1 && scene->LayerType(*reference) == 1, "native catalogue must count outer root plus three nodes, the actual empty first child root, then selected second child root plus shape");
	const auto empty = fixture.Call(motion, "getLayerGetter", {std::string("empty child map key")});
	const auto empty_root = fixture.Vm.Retain(empty);
	Require(std::get<std::int32_t>(fixture.Read(empty, "type")) == 3, "empty child still has its real containing kind3 node without fabricated child geometry");
	const auto nested = fixture.Call(motion, "getLayerGetter", {std::string("nested map key")});
	const auto nested_root = fixture.Vm.Retain(nested);
	const auto snapshot = fixture.Read(nested, "shape");
	const auto snapshot_root = fixture.Vm.Retain(snapshot);
	Require(std::get<std::int32_t>(fixture.Read(snapshot, "type")) == 1 && std::get<float>(fixture.Read(snapshot, "r")) == 16, "lookup after an actual empty child must still resolve the later selected sibling's own circle");
	fixture.Second().Update();
	const auto retained = scene->FindLayer("nested map key");
	Require(retained && retained->GlobalOrdinal == 6 && retained->ContextIdentity == reference->ContextIdentity && std::get<std::int32_t>(fixture.Read(nested, "type")) == 1, "retained empty-child catalogue and actual target token remain stable on an ordinary update");
}

static void TestRetainedNumericPosition() {
	Fixture fixture;
	const auto motion = fixture.Vm.Invoke(fixture.Bindings->MotionClass(), std::array<sgre::script::SquirrelValue, 2>{fixture.Vm.RootTable(), fixture.Leaf}).Value;
	const auto motion_root = fixture.Vm.Retain(motion);
	static_cast<void>(fixture.Call(motion, "setChara", {std::string("outer")}));
	static_cast<void>(fixture.Call(motion, "setVisible", {true}));
	static_cast<void>(fixture.Call(motion, "play", {std::string("main")}));
	const auto getter = fixture.Call(motion, "getLayerGetter", {std::string("rectangle map key")});
	const auto getter_root = fixture.Vm.Retain(getter);
	Require(std::get<float>(fixture.Read(getter, "left")) == 0 && std::get<float>(fixture.Read(getter, "top")) == 0, "position getter must expose actual initial numeric words without forcing composition");
	fixture.Second().Update();
	Require(std::get<float>(fixture.Read(getter, "left")) == 20 && std::get<float>(fixture.Read(getter, "top")) == 40, "native left/top read the composed node coordinate, not its rectangle half-extent bounds");
	const auto shape = fixture.Read(getter, "shape");
	const auto shape_root = fixture.Vm.Retain(shape);
	Require(std::get<float>(fixture.Read(shape, "left")) == 4 && std::get<float>(fixture.Read(shape, "top")) == 16, "separate shape snapshot must preserve its different actual bounds");
	auto task = fixture.Bindings->NativeOwners().Task(motion);
	task.SetCoordinate3({100, 200, 3});
	Require(std::get<float>(fixture.Read(getter, "left")) == 20 && std::get<float>(fixture.Read(getter, "top")) == 40, "pending root pose cannot make a getter recompute numeric state");
	fixture.Second().Update();
	Require(std::get<float>(fixture.Read(getter, "left")) == 120 && std::get<float>(fixture.Read(getter, "top")) == 240, "same weak getter must read actual updated inherited numeric coordinates");
	const auto nested = fixture.Call(motion, "getLayerGetter", {std::string("nested map key")});
	const auto nested_root = fixture.Vm.Retain(nested);
	Require(std::get<float>(fixture.Read(nested, "left")) == 120 && std::get<float>(fixture.Read(nested, "top")) == 240, "recursive getter coordinates must resolve the genuine selected child context");
	fixture.Bindings->NativeOwners().ReleaseInstance(motion);
	fixture.Second().Update();
	RequireFailure([&] {
		static_cast<void>(fixture.Call(getter, "getLeft"));
	},
	    "retired");
	Require(std::get<float>(fixture.Read(shape, "left")) == 4, "copied shape bounds remain independent after the live coordinate target retires");
}

static void TestActualScriptProgress() {
	Fixture fixture;
	const auto motion = fixture.Vm.Invoke(fixture.Bindings->MotionClass(), std::array<sgre::script::SquirrelValue, 2>{fixture.Vm.RootTable(), fixture.Leaf}).Value;
	const auto motion_root = fixture.Vm.Retain(motion);
	Require(std::holds_alternative<std::monostate>(fixture.Call(motion, "progress")), "native missing physical delta defaults to0 and returns no value");
	static_cast<void>(fixture.Call(motion, "setChara", {std::string("outer")}));
	static_cast<void>(fixture.Call(motion, "setVisible", {true}));
	static_cast<void>(fixture.Call(motion, "play", {std::string("main")}));
	static_cast<void>(fixture.Call(motion, "progress", {0.0f, std::string("ignored extra")}));
	const auto task = fixture.Bindings->NativeOwners().Task(motion);
	Require(task.GenericPlayer()->Scene() != nullptr && task.Commands().ClockState().TickCount == 0 && fixture.Second().Count() == 1 && fixture.Primary->SceneContext().Size() == 0 && fixture.Secondary->SceneContext().Size() == 0, "actual progress must compose the selected scene without scheduling a task, Draw or manager queue");
	static_cast<void>(fixture.Call(motion, "progress", {std::int32_t{2}}));
	static_cast<void>(fixture.Call(motion, "setSpeed", {2.0f}));
	static_cast<void>(fixture.Call(motion, "progress", {1.0f}));
	static_cast<void>(fixture.Call(motion, "progress", {true}));
	Require(task.Commands().ClockState().TickCount == 6 && task.Commands().ClockState().DeltaTicks == 2, "genuine script numeric conversion and actual task speed must feed explicit ticks");
	const auto invalid = fixture.Vm.CreateTable();
	const auto invalid_root = fixture.Vm.Retain(invalid);
	static_cast<void>(fixture.Call(motion, "progress", {invalid}));
	Require(task.Commands().ClockState().TickCount == 6 && task.Commands().ClockState().DeltaTicks == 0, "failed native float conversion must use zero without object tostring or a clock fallback");
	static_cast<void>(fixture.Call(motion, "progress", {-.5f}));
	Require(task.Commands().ClockState().TickCount == 5 && task.Commands().ClockState().DeltaTicks == -1, "actual generic task must run reverse traversal for negative explicit ticks");
	fixture.Second().Draw(0);
	RequireFailure([&] {
		static_cast<void>(fixture.Call(motion, "progress", {1.0f}));
	},
	    "idle");
	fixture.Primary->SceneContext().Clear();
	fixture.Secondary->SceneContext().Clear();
	Require(task.Commands().ClockState().TickCount == 5, "queued scene guard must reject before mutating the explicit clock");
	fixture.Layers.ReleaseInstance(fixture.Leaf);
	Require(std::holds_alternative<std::monostate>(fixture.Call(motion, "progress", {1.0f})), "native progress on a genuinely detached Layer must be inert");
}

static void TestNativeConstructorsAndArgumentGuards() {
	Fixture fixture;
	for (const auto name : {"MotionLayerGetter", "MotionShapeContains"}) {
		RequireFailure([&] {
			static_cast<void>(fixture.Vm.Invoke(fixture.Vm.GetSlot(fixture.Vm.RootTable(), std::string(name)), std::array<sgre::script::SquirrelValue, 1>{fixture.Vm.RootTable()}));
		},
		    "no public native constructor");
	}
	const auto motion = fixture.Vm.Invoke(fixture.Bindings->MotionClass(), std::array<sgre::script::SquirrelValue, 2>{fixture.Vm.RootTable(), fixture.Leaf}).Value;
	const auto motion_root = fixture.Vm.Retain(motion);
	RequireFailure([&] {
		static_cast<void>(fixture.Call(motion, "getLayerGetter", {std::int32_t{3}}));
	},
	    "string name");
	RequireFailure([&] {
		static_cast<void>(fixture.Call(motion, "getLayerGetter"));
	},
	    "string name");
	const auto forged = fixture.Vm.CreateInstance(fixture.Vm.GetSlot(fixture.Vm.RootTable(), std::string("MotionLayerGetter")));
	RequireFailure([&] {
		static_cast<void>(fixture.Call(forged, "getType"));
	},
	    "native receiver");
	fixture.Layers.ReleaseInstance(fixture.Leaf);
	RequireFailure([&] {
		static_cast<void>(fixture.Call(motion, "getLayerGetter", {std::string("absent")}));
	},
	    "owner layer already desructed");
}

static void TestBorrowedChildParametersAndLifetime() {
	Fixture fixture(2, true);
	const auto motion = fixture.Vm.Invoke(fixture.Bindings->MotionClass(), std::array<sgre::script::SquirrelValue, 2>{fixture.Vm.RootTable(), fixture.Leaf}).Value;
	const auto motion_root = fixture.Vm.Retain(motion);
	static_cast<void>(fixture.Call(motion, "setChara", {std::string("outer")}));
	static_cast<void>(fixture.Call(motion, "play", {std::string("main")}));
	fixture.Second().Update();
	auto task = fixture.Bindings->NativeOwners().Task(motion);
	const auto getter = fixture.Call(motion, "getLayerGetter", {std::string("child map key")});
	const auto getter_root = fixture.Vm.Retain(getter);
	const auto child = fixture.Read(getter, "motion");
	const auto child_root = fixture.Vm.Retain(child);
	const auto alias = fixture.Call(getter, "getMotion");
	const auto alias_root = fixture.Vm.Retain(alias);
	Require(child != alias && fixture.Vm.IsInstanceOf(child, fixture.Bindings->MotionClass()), "native getter creates distinct Motion wrappers for the same existing child");
	Require(fixture.Second().Count() == 1 && fixture.Secondary->PlayerCount() == 1, "borrowed child wrappers must not create a task, resource/player owner or listener");
	static_cast<void>(fixture.Call(child, "setVariable", {std::string("disable"), 1.0f}));
	Require(std::get<float>(fixture.Call(alias, "getVariable", {std::string("disable")})) == 1 && task.GetVariable("disable") == 0, "real child variable mutation is shared across wrappers and isolated from its containing player");
	Require(std::get<bool>(fixture.Read(child, "playing")) == fixture.Bindings->NativeOwners().Commands(child).ClockState().Playing, "ordinary child playing getter reads actual child commands");
	const auto empty_getter = fixture.Call(motion, "getLayerGetter", {std::string("empty child map key")});
	const auto empty_getter_root = fixture.Vm.Retain(empty_getter);
	const auto sibling = fixture.Read(empty_getter, "motion");
	const auto sibling_root = fixture.Vm.Retain(sibling);
	static_cast<void>(fixture.Call(sibling, "setVariable", {std::string("disable"), 3.0f}));
	static_cast<void>(fixture.Call(child, "animateVariable", {std::string("disable"), 9.0f, std::int32_t{4}, std::int32_t{0}}));
	static_cast<void>(fixture.Call(sibling, "animateVariable", {std::string("disable"), 7.0f, std::int32_t{4}, std::int32_t{0}}));
	auto& first = fixture.Layers.MotionTasks(fixture.Leaf).Group(sgre::scene::MotionTaskGroup::CommonFirst);
	for (std::size_t frame = 0; frame != 4; ++frame) {
		first.Update();
	}
	Require(std::get<float>(fixture.Call(child, "getVariable", {std::string("disable")})) == 9 && std::get<float>(fixture.Call(sibling, "getVariable", {std::string("disable")})) == 7 && task.GetVariable("disable") == 0, "same-name animations retain distinct actual child targets in the real CommonFirst task group");
	static_cast<void>(fixture.Call(alias, "animateVariable", {std::string("disable"), 12.0f, std::int32_t{0}}));
	static_cast<void>(fixture.Call(child, "animateVariable", {std::string("disable"), 13.0f, std::int32_t{0}}));
	first.Update();
	Require(std::get<float>(fixture.Call(child, "getVariable", {std::string("disable")})) == 12, "native273b80 retains per-wrapper nested-group order even when an earlier wrapper schedules a later same-target animation");
	static_cast<void>(fixture.Call(alias, "animateVariable", {std::string("disable"), 14.0f, std::int32_t{0}}));
	fixture.Bindings->NativeOwners().ReleaseInstance(alias);
	first.Update();
	Require(fixture.Second().State(task.Handle()).Status == sgre::scene::TaskStatus::Active && std::get<float>(fixture.Call(child, "getVariable", {std::string("disable")})) == 14, "native false-policy borrowed wrapper release leaves its already attached animation group to finish without retiring any player");
	static_cast<void>(fixture.Call(child, "animateVariable", {std::string("disable"), 0.0f, std::int32_t{8}}));
	static_cast<void>(fixture.Call(motion, "play", {std::string("main"), std::int32_t{1}}));
	RequireFailure([&] {
		static_cast<void>(fixture.Call(child, "getVariable", {std::string("disable")}));
	},
	    "retired");
	first.Update();
	fixture.Bindings->NativeOwners().ReleaseInstance(child);
	fixture.Bindings->NativeOwners().ReleaseInstance(sibling);
	Require(fixture.Second().Count() == 1 && task.IsLive(), "expired child wrappers release their own registrations without dereferencing or retiring the replacement tree");
	fixture.Second().Update();
	const auto current_getter = fixture.Call(motion, "getLayerGetter", {std::string("child map key")});
	const auto current_getter_root = fixture.Vm.Retain(current_getter);
	const auto current = fixture.Read(current_getter, "motion");
	const auto current_root = fixture.Vm.Retain(current);
	fixture.Bindings->NativeOwners().ReleaseInstance(motion);
	fixture.Second().Update();
	RequireFailure([&] {
		static_cast<void>(fixture.Call(current, "getVariable", {std::string("disable")}));
	},
	    "containing task retirement");
	fixture.Bindings->NativeOwners().ReleaseInstance(current);
}

static void TestActualClearAndExpiredChildBorrow() {
	Fixture fixture;
	const auto motion = fixture.Vm.Invoke(fixture.Bindings->MotionClass(), std::array<sgre::script::SquirrelValue, 2>{fixture.Vm.RootTable(), fixture.Leaf}).Value;
	const auto motion_root = fixture.Vm.Retain(motion);
	Require(std::holds_alternative<std::monostate>(fixture.Call(motion, "clear", {std::int32_t{17}, fixture.Vm.CreateTable()})), "native clear ignores extra operands and returns no value even before selection");
	static_cast<void>(fixture.Call(motion, "setChara", {std::string("outer")}));
	static_cast<void>(fixture.Call(motion, "play", {std::string("main")}));
	fixture.Second().Update();
	const auto getter = fixture.Call(motion, "getLayerGetter", {std::string("child map key")});
	const auto getter_root = fixture.Vm.Retain(getter);
	const auto child = fixture.Read(getter, "motion");
	const auto child_root = fixture.Vm.Retain(child);
	const auto shape_getter = fixture.Call(motion, "getLayerGetter", {std::string("rectangle map key")});
	const auto shape_getter_root = fixture.Vm.Retain(shape_getter);
	const auto snapshot = fixture.Read(shape_getter, "shape");
	const auto snapshot_root = fixture.Vm.Retain(snapshot);
	static_cast<void>(fixture.Call(motion, "setVariable", {std::string("actual child label::disable"), 1.0f}));
	Require(fixture.Bindings->NativeOwners().Commands(child).ParameterValues().Read("disable") == 1, "clear fixture must bind its genuine child initialization lookup before selected retirement");
	auto task = fixture.Bindings->NativeOwners().Task(motion);
	const auto serial = task.Commands().SelectionSerial();
	static_cast<void>(fixture.Call(motion, "clear"));
	static_cast<void>(fixture.Call(motion, "clear", {false}));
	Require(task.IsLive() && fixture.Second().Count() == 1 && task.Commands().SelectionSerial() == serial && !task.GenericPlayer()->Scene() && fixture.Secondary->Identifiers().LiveCount() == 0, "script clear retires actual selected children without retiring their containing task");
	RequireFailure([&] {
		static_cast<void>(fixture.Call(child, "getVariable", {std::string("disable")}));
	},
	    "retired");
	RequireFailure([&] {
		static_cast<void>(fixture.Call(shape_getter, "getShape"));
	},
	    "no selected scene");
	Require(std::holds_alternative<std::monostate>(fixture.Call(motion, "getLayerGetter", {std::string("rectangle map key")})) && std::get<bool>(fixture.Call(snapshot, "contains", {20.0f, 40.0f})), "clear must remove the live catalogue while independent native geometry snapshots survive");
	static_cast<void>(fixture.Call(motion, "progress", {1.0f}));
	static_cast<void>(fixture.Call(motion, "play", {std::string("main")}));
	Require(!task.GenericPlayer()->Scene(), "script unchanged-name Play must not reconstruct a cleared tree");
	static_cast<void>(fixture.Call(motion, "play", {std::string("main"), std::int32_t{1}}));
	fixture.Second().Update();
	const auto replacement = fixture.Call(motion, "getLayerGetter", {std::string("child map key")});
	const auto replacement_root = fixture.Vm.Retain(replacement);
	const auto replacement_child = fixture.Read(replacement, "motion");
	const auto replacement_child_root = fixture.Vm.Retain(replacement_child);
	Require(fixture.Bindings->NativeOwners().Commands(replacement_child).ParameterValues().Read("disable") == 1 && std::get<float>(fixture.Call(replacement_child, "getVariable", {std::string("disable")})) == 0, "new selected child initialization must receive the retained canonical descriptor while the native local-map getter remains zero");
	RequireFailure([&] {
		static_cast<void>(fixture.Call(child, "getVariable", {std::string("disable")}));
	},
	    "retired");
	fixture.Bindings->NativeOwners().ReleaseInstance(child);
	fixture.Layers.ReleaseInstance(fixture.Leaf);
	Require(std::holds_alternative<std::monostate>(fixture.Call(motion, "clear", {std::int32_t{99}})), "native detached clear returns without touching the physical player");
	Require(!task.IsLive(), "actual containing Layer retirement must follow clear/reinstall normally");
}

static void TestBorrowedChildRootZoom() {
	Fixture fixture(2, true);
	const auto motion = fixture.Vm.Invoke(fixture.Bindings->MotionClass(), std::array<sgre::script::SquirrelValue, 2>{fixture.Vm.RootTable(), fixture.Leaf}).Value;
	const auto motion_root = fixture.Vm.Retain(motion);
	static_cast<void>(fixture.Call(motion, "setChara", {std::string("outer")}));
	static_cast<void>(fixture.Call(motion, "play", {std::string("main")}));
	static_cast<void>(fixture.Call(motion, "setVisible", {true}));
	static_cast<void>(fixture.Call(motion, "setZoom", {2.0f, 3.0f}));
	auto task = fixture.Bindings->NativeOwners().Task(motion);
	const auto getter = fixture.Call(motion, "getLayerGetter", {std::string("child map key")});
	const auto getter_root = fixture.Vm.Retain(getter);
	const auto child = fixture.Read(getter, "motion");
	const auto child_root = fixture.Vm.Retain(child);
	const auto alias = fixture.Read(getter, "motion");
	const auto alias_root = fixture.Vm.Retain(alias);
	const auto empty_getter = fixture.Call(motion, "getLayerGetter", {std::string("empty child map key")});
	const auto empty_getter_root = fixture.Vm.Retain(empty_getter);
	const auto empty = fixture.Read(empty_getter, "motion");
	const auto empty_root = fixture.Vm.Retain(empty);
	Require(std::get<float>(fixture.Call(child, "getZoomX")) == 1 && std::get<float>(fixture.Call(empty, "getZoomY")) == 1, "genuine kind3 constructors own unit zoom even before any selected descendant or parent pose update");
	static_cast<void>(fixture.Call(child, "setZoom", {4.0f, 5.0f}));
	static_cast<void>(fixture.Call(empty, "setZoom", {7.0f, 9.0f}));
	Require(std::get<float>(fixture.Call(alias, "getZoomX")) == 4 && std::get<float>(fixture.Call(alias, "getZoomY")) == 5 && std::get<float>(fixture.Call(empty, "getZoomX")) == 7 && task.Root().Overrides().ZoomX == 2 && task.Root().Overrides().ZoomY == 3 && fixture.Second().Count() == 1, "borrowed zoom writes the exact existing child and aliases share it without a new task or parent redirection");
	fixture.Second().Update();
	const auto completed_x = fixture.Bindings->NativeOwners().Root(child).Frame().Hierarchy.Transform.XX;
	static_cast<void>(fixture.Call(alias, "setZoomX", {-2.0f}));
	static_cast<void>(fixture.Call(child, "setZoomY", {-3.0f}));
	Require(std::get<float>(fixture.Call(child, "getZoomX")) == -2 && std::get<float>(fixture.Call(alias, "getZoomY")) == -3 && fixture.Bindings->NativeOwners().Root(child).Frame().Hierarchy.Transform.XX == completed_x && task.Root().Overrides().ZoomX == 2, "child controls change retained factors only, leaving real completed numeric output and containing root untouched");
	fixture.Second().Draw(0);
	RequireFailure([&] {
		static_cast<void>(fixture.Call(child, "setZoom", {8.0f, 8.0f}));
	},
	    "queued row borrows");
	Require(std::get<float>(fixture.Call(alias, "getZoomX")) == -2, "queued child zoom rejection occurs before actual retained field mutation");
	fixture.Secondary->SceneContext().Clear();
	static_cast<void>(fixture.Call(motion, "play", {std::string("main"), std::int32_t{1}}));
	RequireFailure([&] {
		static_cast<void>(fixture.Call(child, "setZoomY", {6.0f}));
	},
	    "retired");
	RequireFailure([&] {
		static_cast<void>(fixture.Call(alias, "getZoomX"));
	},
	    "retired");
	Require(task.Root().Overrides().ZoomX == 2 && task.Root().Overrides().ZoomY == 3, "expired child write must not redirect to the live containing player after Force");
	fixture.Layers.ReleaseInstance(fixture.Leaf);
	Require(std::get<float>(fixture.Call(child, "getZoomX")) == 0 && std::holds_alternative<std::monostate>(fixture.Call(child, "setZoomY", {true})), "actual Layer detachment preserves borrowed wrapper native defaults without resolving its retired child");
}

static void TestBorrowedChildRootAngles() {
	Fixture fixture(2, true);
	const auto motion = fixture.Vm.Invoke(fixture.Bindings->MotionClass(), std::array<sgre::script::SquirrelValue, 2>{fixture.Vm.RootTable(), fixture.Leaf}).Value;
	const auto motion_root = fixture.Vm.Retain(motion);
	static_cast<void>(fixture.Call(motion, "setChara", {std::string("outer")}));
	static_cast<void>(fixture.Call(motion, "play", {std::string("main")}));
	static_cast<void>(fixture.Call(motion, "setVisible", {true}));
	static_cast<void>(fixture.Call(motion, "setAngleDeg", {30.0f}));
	auto task = fixture.Bindings->NativeOwners().Task(motion);
	const auto getter = fixture.Call(motion, "getLayerGetter", {std::string("child map key")});
	const auto getter_root = fixture.Vm.Retain(getter);
	const auto child = fixture.Read(getter, "motion");
	const auto child_root = fixture.Vm.Retain(child);
	const auto alias = fixture.Read(getter, "motion");
	const auto alias_root = fixture.Vm.Retain(alias);
	const auto empty_getter = fixture.Call(motion, "getLayerGetter", {std::string("empty child map key")});
	const auto empty_getter_root = fixture.Vm.Retain(empty_getter);
	const auto empty = fixture.Read(empty_getter, "motion");
	const auto empty_root = fixture.Vm.Retain(empty);
	Require(std::get<float>(fixture.Call(child, "getAngleDeg")) == 0 && std::get<float>(fixture.Call(empty, "getAngleRad")) == 0, "actual selected and root-only kind3 children have initial ordinary anglemode zero");
	static_cast<void>(fixture.Call(child, "setAngleDeg", {-450.0f}));
	static_cast<void>(fixture.Call(empty, "setAngleRad", {1.57079637f}));
	Require(std::get<float>(fixture.Call(alias, "getAngleDeg")) == -450 && std::get<float>(fixture.Call(empty, "getAngleDeg")) == 90 && task.Root().Overrides().Angle == 30 && fixture.Second().Count() == 1, "borrowed angle aliases update the same genuine child root without normalization, parent redirection or a new task");
	fixture.Second().Update();
	const auto completed = fixture.Bindings->NativeOwners().Root(child).Frame().Hierarchy.Attributes.Angle;
	static_cast<void>(fixture.Call(alias, "setAngleRad", {1.57079637f}));
	Require(std::get<float>(fixture.Call(child, "getAngleDeg")) == 90 && fixture.Bindings->NativeOwners().Root(child).Frame().Hierarchy.Attributes.Angle == completed && task.Root().Overrides().Angle == 30, "tail child angle assignment changes retained state without replacing the completed parent-supplied pose");
	fixture.Second().Draw(0);
	RequireFailure([&] {
		static_cast<void>(fixture.Call(child, "setAngleDeg", {8.0f}));
	},
	    "queued row borrows");
	Require(std::get<float>(fixture.Call(alias, "getAngleDeg")) == 90, "queued borrowed-child angle rejection preserves its real retained field");
	fixture.Secondary->SceneContext().Clear();
	static_cast<void>(fixture.Call(motion, "play", {std::string("main"), std::int32_t{1}}));
	RequireFailure([&] {
		static_cast<void>(fixture.Call(child, "setAngleRad", {2.0f}));
	},
	    "retired");
	RequireFailure([&] {
		static_cast<void>(fixture.Call(alias, "getAngleDeg"));
	},
	    "retired");
	Require(task.Root().Overrides().Angle == 30, "expired child angle must never retarget the live containing root");
	fixture.Layers.ReleaseInstance(fixture.Leaf);
	Require(std::get<float>(fixture.Call(child, "getAngleRad")) == 0 && std::holds_alternative<std::monostate>(fixture.Call(child, "setAngleDeg", {true})), "actual Layer detach preserves the original borrowed wrapper angle defaults");
}

static void TestBorrowedChildRootOpacity() {
	Fixture fixture(2, true);
	const auto motion = fixture.Vm.Invoke(fixture.Bindings->MotionClass(), std::array<sgre::script::SquirrelValue, 2>{fixture.Vm.RootTable(), fixture.Leaf}).Value;
	const auto motion_root = fixture.Vm.Retain(motion);
	static_cast<void>(fixture.Call(motion, "setChara", {std::string("outer")}));
	static_cast<void>(fixture.Call(motion, "play", {std::string("main")}));
	static_cast<void>(fixture.Call(motion, "setVisible", {true}));
	static_cast<void>(fixture.Call(motion, "setOpacity", {std::int32_t{201}}));
	auto task = fixture.Bindings->NativeOwners().Task(motion);
	const auto getter = fixture.Call(motion, "getLayerGetter", {std::string("child map key")});
	const auto getter_root = fixture.Vm.Retain(getter);
	const auto child = fixture.Read(getter, "motion");
	const auto child_root = fixture.Vm.Retain(child);
	const auto alias = fixture.Read(getter, "motion");
	const auto alias_root = fixture.Vm.Retain(alias);
	const auto empty_getter = fixture.Call(motion, "getLayerGetter", {std::string("empty child map key")});
	const auto empty_getter_root = fixture.Vm.Retain(empty_getter);
	const auto empty = fixture.Read(empty_getter, "motion");
	const auto empty_root = fixture.Vm.Retain(empty);
	Require(std::get<std::int32_t>(fixture.Call(child, "getOpacity")) == 255 && std::get<std::int32_t>(fixture.Call(empty, "getOpacity")) == 255, "actual child root constructors own opacity255 before first parent update");
	static_cast<void>(fixture.Call(child, "setOpacity", {std::int32_t{37}}));
	static_cast<void>(fixture.Call(empty, "setOpacity", {std::int32_t{91}}));
	Require(std::get<std::int32_t>(fixture.Call(alias, "getOpacity")) == 37 && std::get<std::int32_t>(fixture.Call(empty, "getOpacity")) == 91 && task.Root().Overrides().Opacity == 201 && fixture.Second().Count() == 1, "borrowed opacity aliases mutate only their genuine capsule without parent redirection or new task");
	fixture.Second().Update();
	const auto completed = fixture.Bindings->NativeOwners().Root(child).Frame().Hierarchy.Attributes.Opacity;
	static_cast<void>(fixture.Call(alias, "setOpacity", {std::int32_t{65}}));
	Require(std::get<std::int32_t>(fixture.Call(child, "getOpacity")) == 65 && fixture.Bindings->NativeOwners().Root(child).Frame().Hierarchy.Attributes.Opacity == completed && task.Root().Overrides().Opacity == 201, "borrowed setter preserves completed supplied pose until actual next update");
	fixture.Second().Draw(0);
	RequireFailure([&] {
		static_cast<void>(fixture.Call(child, "setOpacity", {std::int32_t{8}}));
	},
	    "queued row borrows");
	Require(std::get<std::int32_t>(fixture.Call(alias, "getOpacity")) == 65, "queued child opacity rejection preserves actual retained state");
	fixture.Secondary->SceneContext().Clear();
	static_cast<void>(fixture.Call(motion, "play", {std::string("main"), std::int32_t{1}}));
	RequireFailure([&] {
		static_cast<void>(fixture.Call(child, "setOpacity", {std::int32_t{2}}));
	},
	    "retired");
	RequireFailure([&] {
		static_cast<void>(fixture.Call(alias, "getOpacity"));
	},
	    "retired");
	Require(task.Root().Overrides().Opacity == 201, "expired child opacity cannot retarget containing root");
	fixture.Layers.ReleaseInstance(fixture.Leaf);
	Require(std::get<std::int32_t>(fixture.Call(child, "getOpacity")) == 255 && std::holds_alternative<std::monostate>(fixture.Call(child, "setOpacity", {true})), "actual Layer detach preserves borrowed-wrapper native opacity defaults");
}

static void TestDirectBorrowedSyncControls() {
	Fixture fixture(2, true);
	const auto motion = fixture.Vm.Invoke(fixture.Bindings->MotionClass(), std::array<sgre::script::SquirrelValue, 2>{fixture.Vm.RootTable(), fixture.Leaf}).Value;
	const auto motion_root = fixture.Vm.Retain(motion);
	static_cast<void>(fixture.Call(motion, "setChara", {std::string("outer")}));
	static_cast<void>(fixture.Call(motion, "play", {std::string("main")}));
	fixture.Second().Update();
	const auto child = fixture.Call(motion, "getLayerMotion", {std::string("child map key")});
	const auto child_root = fixture.Vm.Retain(child);
	const auto alias = fixture.Call(motion, "getLayerMotion", {std::string("child map key")});
	const auto alias_root = fixture.Vm.Retain(alias);
	Require(std::get<float>(fixture.Call(child, "getTickCount")) == 0 && std::get<float>(fixture.Call(motion, "getTickCount")) == 0, "actual parent and newly evaluated kind3 child start at their independently assigned zero clocks");
	Require(std::get<float>(fixture.Call(child, "getLoopTime", {true})) == -1, "borrowed loop getter reads the exact selected child");
	static_cast<void>(fixture.Call(child, "skipToSync", {true}));
	Require(std::get<float>(fixture.Call(alias, "getTickCount")) == 8 && std::get<float>(fixture.Call(motion, "getTickCount")) == 0 && fixture.Second().Count() == 1, "child skip seeks the existing aliased child to its own real terminal time without retargeting or advancing its containing task");
	Require(!std::get<bool>(fixture.Call(alias, "getSyncWaiting", {true})), "a real child with no sync markers does not invent a wait");
	static_cast<void>(fixture.Call(child, "releaseSyncWait", {true}));
	Require(std::get<float>(fixture.Call(alias, "getTickCount")) == 8, "child release does not rewrite its retained seek");
	static_cast<void>(fixture.Call(motion, "clear"));
	RequireFailure([&] {
		static_cast<void>(fixture.Call(child, "skipToSync"));
	},
	    "retired");
	RequireFailure([&] {
		static_cast<void>(fixture.Call(alias, "getSyncWaiting"));
	},
	    "retired");
	Require(std::get<float>(fixture.Call(motion, "getTickCount")) == 0, "retired child controls cannot mutate the still-live containing player");
	fixture.Bindings->NativeOwners().ReleaseInstance(child);
	fixture.Bindings->NativeOwners().ReleaseInstance(alias);
}

static void TestDirectBorrowedChildCoordinate() {
	Fixture fixture(2, true);
	const auto motion = fixture.Vm.Invoke(fixture.Bindings->MotionClass(), std::array<sgre::script::SquirrelValue, 2>{fixture.Vm.RootTable(), fixture.Leaf}).Value;
	const auto motion_root = fixture.Vm.Retain(motion);
	static_cast<void>(fixture.Call(motion, "setChara", {std::string("outer")}));
	static_cast<void>(fixture.Call(motion, "play", {std::string("main")}));
	static_cast<void>(fixture.Call(motion, "setCoord3", {100.0f, 200.0f, 17.0f}));
	fixture.Second().Update();
	const auto child = fixture.Call(motion, "getLayerMotion", {std::string("child map key")});
	const auto child_root = fixture.Vm.Retain(child);
	const auto alias = fixture.Call(motion, "getLayerMotion", {std::string("child map key")});
	const auto alias_root = fixture.Vm.Retain(alias);
	const auto empty = fixture.Call(motion, "getLayerMotion", {std::string("empty child map key")});
	const auto empty_root = fixture.Vm.Retain(empty);
	const auto original_depth = fixture.Bindings->NativeOwners().Root(child).Overrides().Coordinate[2];
	const auto original_frame = fixture.Bindings->NativeOwners().Root(child).Frame().Hierarchy;

	static_cast<void>(fixture.Call(child, "setCoord", {12.5f, -7.25f, 999.0f}));
	Require(std::get<float>(fixture.Call(alias, "getLeft")) == 12.5f && std::get<float>(fixture.Call(alias, "getTop")) == -7.25f && std::get<float>(fixture.Call(alias, "getZ")) == original_depth, "direct child coordinate writes the actual borrowed root and preserves its depth");
	Require(fixture.Bindings->NativeOwners().Root(motion).Overrides().Coordinate == std::array<float, 3>{100, 200, 17} && fixture.Second().Count() == 1, "borrowed coordinates must not retarget the containing root or create a player task");
	Require(fixture.Bindings->NativeOwners().Root(child).Frame().Hierarchy.Attributes.Coordinate == original_frame.Attributes.Coordinate && fixture.Bindings->NativeOwners().Root(child).Frame().Hierarchy.Transform.XX == original_frame.Transform.XX, "tail coordinate mutation preserves completed coordinates and supplied transform until its next update");
	static_cast<void>(fixture.Call(empty, "setCoord", {-3.0f, 4.0f}));
	Require(std::get<float>(fixture.Call(empty, "getLeft")) == -3 && std::get<float>(fixture.Call(alias, "getLeft")) == 12.5f, "an empty kind3 child retains its independent actual coordinate state");
	static_cast<void>(fixture.Call(alias, "setCoord", {std::string("not numeric"), true}));
	Require(std::get<float>(fixture.Call(child, "getLeft")) == 0 && std::get<float>(fixture.Call(child, "getTop")) == 1, "borrowed setter retains the existing physical numeric bridge conversion");
	static_cast<void>(fixture.Call(motion, "clear"));
	RequireFailure([&] {
		static_cast<void>(fixture.Call(child, "setCoord", {5.0f, 6.0f}));
	},
	    "retired");
	Require(fixture.Bindings->NativeOwners().Root(motion).Overrides().Coordinate == std::array<float, 3>{100, 200, 17}, "retired child coordinate cannot mutate the still-live containing player");
	fixture.Bindings->NativeOwners().ReleaseInstance(child);
	fixture.Bindings->NativeOwners().ReleaseInstance(alias);
	fixture.Bindings->NativeOwners().ReleaseInstance(empty);
}

static void TestDirectNamedChildMotion() {
	Fixture fixture(2, true);
	const auto motion = fixture.Vm.Invoke(fixture.Bindings->MotionClass(), std::array<sgre::script::SquirrelValue, 2>{fixture.Vm.RootTable(), fixture.Leaf}).Value;
	const auto motion_root = fixture.Vm.Retain(motion);
	Require(std::holds_alternative<std::monostate>(fixture.Call(motion, "getLayerMotion", {std::string("child map key")})), "an unselected native player must not fabricate a child Motion");
	static_cast<void>(fixture.Call(motion, "setChara", {std::string("outer")}));
	static_cast<void>(fixture.Call(motion, "play", {std::string("main")}));
	fixture.Second().Update();
	for (const auto name : {"absent", "actual child label", "rectangle map key"}) {
		Require(std::holds_alternative<std::monostate>(fixture.Call(motion, "getLayerMotion", {std::string(name)})), "direct child lookup must use the actual map and reject nonchild kinds");
	}
	const auto child = fixture.Call(motion, "getLayerMotion", {std::string("child map key\0ignored", 21), std::int32_t{17}});
	const auto child_root = fixture.Vm.Retain(child);
	const auto alias = fixture.Call(motion, "getLayerMotion", {std::string("child map key")});
	const auto alias_root = fixture.Vm.Retain(alias);
	Require(child != alias && fixture.Vm.IsInstanceOf(child, fixture.Bindings->MotionClass()), "each native direct lookup creates a fresh wrapper for the same actual child");
	Require(fixture.Second().Count() == 1, "direct child wrappers must not create another player task or listener");
	static_cast<void>(fixture.Call(child, "setVariable", {std::string("disable"), 7.0f}));
	Require(std::get<float>(fixture.Call(alias, "getVariable", {std::string("disable")})) == 7 && std::get<float>(fixture.Call(motion, "getVariable", {std::string("disable")})) == 0, "direct aliases must share the actual child without mutating its parent");
	const auto empty = fixture.Call(motion, "getLayerMotion", {std::string("empty child map key")});
	const auto empty_root = fixture.Vm.Retain(empty);
	Require(fixture.Vm.IsInstanceOf(empty, fixture.Bindings->MotionClass()), "an actual empty kind3 child still owns its real wrapper");
	RequireFailure([&] {
		static_cast<void>(fixture.Call(motion, "getLayerMotion", {std::int32_t{3}}));
	},
	    "actual string name");
	static_cast<void>(fixture.Call(motion, "clear"));
	Require(std::holds_alternative<std::monostate>(fixture.Call(motion, "getLayerMotion", {std::string("child map key")})), "clear retires the selected catalogue without inventing a replacement child");
	RequireFailure([&] {
		static_cast<void>(fixture.Call(child, "getVariable", {std::string("disable")}));
	},
	    "retired");
	fixture.Bindings->NativeOwners().ReleaseInstance(child);
	fixture.Bindings->NativeOwners().ReleaseInstance(alias);
	fixture.Bindings->NativeOwners().ReleaseInstance(empty);
	fixture.Layers.ReleaseInstance(fixture.Leaf);
	RequireFailure([&] {
		static_cast<void>(fixture.Call(motion, "getLayerMotion", {std::int32_t{3}}));
	},
	    "owner layer already desructed");
}

static void TestBorrowedChildScopedGettersAndRetainedTransforms() {
	Fixture fixture(2, false, NestedGetterDocument());
	const auto motion = fixture.Vm.Invoke(fixture.Bindings->MotionClass(), std::array<sgre::script::SquirrelValue, 2>{fixture.Vm.RootTable(), fixture.Leaf}).Value;
	const auto motion_root = fixture.Vm.Retain(motion);
	static_cast<void>(fixture.Call(motion, "setChara", {std::string("outer")}));
	static_cast<void>(fixture.Call(motion, "setVisible", {true}));
	static_cast<void>(fixture.Call(motion, "setCoord", {100.0f, 200.0f}));
	static_cast<void>(fixture.Call(motion, "play", {std::string("main")}));
	static_cast<void>(fixture.Call(motion, "progress", {0.0f}));
	const auto task = fixture.Bindings->NativeOwners().Task(motion);
	const auto identifiers = fixture.Secondary->Identifiers().LiveCount();
	const auto tick = task.Commands().ClockState().TickCount;
	const auto child = fixture.Call(motion, "getLayerMotion", {std::string("child map key")});
	const auto child_root = fixture.Vm.Retain(child);
	const auto getter = fixture.Call(child, "getLayerGetter", {std::string("shared lookup key\0tail", 22), fixture.Vm.CreateTable()});
	const auto getter_root = fixture.Vm.Retain(getter);
	const auto parent = fixture.Call(motion, "getLayerGetter", {std::string("shared lookup key")});
	const auto parent_root = fixture.Vm.Retain(parent);
	Require(std::get<std::int32_t>(fixture.Read(getter, "type")) == 1 && std::get<float>(fixture.Read(getter, "left")) == 107 && std::get<float>(fixture.Read(getter, "top")) == 211, "borrowed-child lookup must start at its actual selected catalogue and read composed inherited coordinates");
	Require(std::get<float>(fixture.Read(parent, "left")) == 120 && std::get<float>(fixture.Read(parent, "top")) == 240, "the duplicate containing map key must remain a distinct real target");
	Require(std::holds_alternative<std::monostate>(fixture.Call(child, "getLayerGetter", {std::string("outer only")})) && !std::holds_alternative<std::monostate>(fixture.Call(motion, "getLayerGetter", {std::string("outer only")})), "borrowed lookup must not escape upward to a matching containing-only layer");
	Require(std::holds_alternative<std::monostate>(fixture.Call(child, "getLayerGetter", {std::string("absent")})), "a missing child-scoped map key returns null without a surrogate root lookup");
	const auto shape = fixture.Read(getter, "shape");
	const auto shape_root = fixture.Vm.Retain(shape);
	Require(std::get<std::int32_t>(fixture.Read(shape, "type")) == 2 && std::get<float>(fixture.Read(shape, "left")) == 91 && std::get<float>(fixture.Read(shape, "top")) == 187 && std::get<float>(fixture.Read(shape, "width")) == 32 && std::get<float>(fixture.Read(shape, "height")) == 48, "child getter shape must snapshot the actual composed signed half extents");
	static_cast<void>(fixture.Call(motion, "setCoord", {200.0f, 300.0f}));
	Require(std::get<float>(fixture.Read(getter, "left")) == 107 && std::get<float>(fixture.Read(getter, "top")) == 211, "tail containing pose mutation cannot force child getter composition");
	static_cast<void>(fixture.Call(motion, "progress", {0.0f}));
	Require(std::get<float>(fixture.Read(getter, "left")) == 207 && std::get<float>(fixture.Read(getter, "top")) == 311 && std::get<float>(fixture.Read(shape, "left")) == 91, "a genuine containing phase updates the child numeric row while its independent snapshot remains unchanged");
	Require(fixture.Second().Count() == 1 && fixture.Secondary->PlayerCount() == 1 && fixture.Secondary->Identifiers().LiveCount() == identifiers && task.Commands().ClockState().TickCount == tick && fixture.Uploads == 0, "readonly child getters create no task, selected UID, atlas upload or clock progress");
}

static void TestNestedBorrowedGetterMotionAndSourceFirstRetirement() {
	Fixture fixture(2, false, NestedGetterDocument());
	const auto motion = fixture.Vm.Invoke(fixture.Bindings->MotionClass(), std::array<sgre::script::SquirrelValue, 2>{fixture.Vm.RootTable(), fixture.Leaf}).Value;
	const auto motion_root = fixture.Vm.Retain(motion);
	static_cast<void>(fixture.Call(motion, "setChara", {std::string("outer")}));
	static_cast<void>(fixture.Call(motion, "setCoord", {100.0f, 200.0f}));
	static_cast<void>(fixture.Call(motion, "play", {std::string("main")}));
	static_cast<void>(fixture.Call(motion, "progress", {0.0f}));
	const auto task = fixture.Bindings->NativeOwners().Task(motion);
	const auto identifiers = fixture.Secondary->Identifiers().LiveCount();
	const auto child = fixture.Call(motion, "getLayerMotion", {std::string("child map key")});
	const auto child_root = fixture.Vm.Retain(child);
	const auto getter = fixture.Call(child, "getLayerGetter", {std::string("deep child key")});
	const auto getter_root = fixture.Vm.Retain(getter);
	Require(std::get<std::int32_t>(fixture.Read(getter, "type")) == 3, "borrowed lookup must retain the nested actual kind3 node");
	const auto deep = fixture.Read(getter, "motion");
	const auto deep_root = fixture.Vm.Retain(deep);
	const auto alias = fixture.Call(child, "getLayerMotion", {std::string("deep child key")});
	const auto alias_root = fixture.Vm.Retain(alias);
	Require(deep != alias && fixture.Vm.IsInstanceOf(deep, fixture.Bindings->MotionClass()), "both real child-scoped routes wrap the same deeper player with separate VM instances");
	static_cast<void>(fixture.Call(deep, "setVariable", {std::string("nested proof"), 9.0f}));
	Require(std::get<float>(fixture.Call(alias, "getVariable", {std::string("nested proof")})) == 9 && std::get<float>(fixture.Call(child, "getVariable", {std::string("nested proof")})) == 0 && task.GetVariable("nested proof") == 0, "nested wrapper aliases must share the deeper player without redirecting state to either ancestor");
	Require(std::holds_alternative<std::monostate>(fixture.Call(child, "getLayerMotion", {std::string("shared lookup key")})), "child-scoped getLayerMotion must return null for an actual shape rather than wrapping its containing player");
	const auto shape_getter = fixture.Call(deep, "getLayerGetter", {std::string("deep shape key")});
	const auto shape_getter_root = fixture.Vm.Retain(shape_getter);
	const auto shape = fixture.Read(shape_getter, "shape");
	const auto shape_root = fixture.Vm.Retain(shape);
	Require(std::get<float>(fixture.Read(shape_getter, "left")) == 103 && std::get<float>(fixture.Read(shape_getter, "top")) == 205 && std::get<std::int32_t>(fixture.Read(shape, "type")) == 1, "deeper getter reads its genuine inherited row and circle snapshot");
	Require(fixture.Second().Count() == 1 && fixture.Secondary->PlayerCount() == 1 && fixture.Secondary->Identifiers().LiveCount() == identifiers, "nested getter factories cannot install another task, player registration or selected UID");
	fixture.Bindings->NativeOwners().ReleaseInstance(child);
	Require(std::get<std::int32_t>(fixture.Read(getter, "type")) == 3 && std::get<float>(fixture.Read(shape_getter, "left")) == 103, "retiring an intermediate borrowed VM wrapper must not retire its real selected subtree or derived getters");
	fixture.Bindings->NativeOwners().ReleaseInstance(motion);
	fixture.Second().Update();
	Require(!task.IsLive() && fixture.Secondary->PlayerCount() == 0 && fixture.Secondary->Identifiers().LiveCount() == 0, "weak child getters and nested wrappers must not pin the actual source task or selected UIDs");
	RequireFailure([&] {
		static_cast<void>(fixture.Call(shape_getter, "getLeft"));
	},
	    "retired");
	RequireFailure([&] {
		static_cast<void>(fixture.Call(alias, "getVariable", {std::string("nested proof")}));
	},
	    "retirement");
	fixture.Bindings->NativeOwners().ReleaseInstance(deep);
	fixture.Bindings->NativeOwners().ReleaseInstance(alias);
	fixture.Vm.ResetExecution();
	static_cast<void>(fixture.Vm.CollectGarbage());
	Require(std::get<float>(fixture.Read(shape, "x")) == 103 && std::get<float>(fixture.Read(shape, "y")) == 205 && std::get<bool>(fixture.Call(shape, "contains", {103.0f, 205.0f})), "independent copied shape survives source-first selected/task retirement and actual collection");
}

static void TestBorrowedChildGetterQueueAndExpiredSelectionGuards() {
	Fixture fixture(2, false, NestedGetterDocument());
	const auto motion = fixture.Vm.Invoke(fixture.Bindings->MotionClass(), std::array<sgre::script::SquirrelValue, 2>{fixture.Vm.RootTable(), fixture.Leaf}).Value;
	const auto motion_root = fixture.Vm.Retain(motion);
	static_cast<void>(fixture.Call(motion, "setChara", {std::string("outer")}));
	static_cast<void>(fixture.Call(motion, "setVisible", {true}));
	static_cast<void>(fixture.Call(motion, "play", {std::string("main")}));
	static_cast<void>(fixture.Call(motion, "progress", {0.0f}));
	const auto task = fixture.Bindings->NativeOwners().Task(motion);
	const auto child = fixture.Call(motion, "getLayerMotion", {std::string("child map key")});
	const auto child_root = fixture.Vm.Retain(child);
	const auto getter = fixture.Call(child, "getLayerGetter", {std::string("shared lookup key")});
	const auto getter_root = fixture.Vm.Retain(getter);
	const auto identifiers = fixture.Secondary->Identifiers().LiveCount();
	const auto tick = task.Commands().ClockState().TickCount;
	fixture.Second().Draw(0);
	try {
		Require(task.GenericPlayer()->Scene() != nullptr && fixture.Secondary->SceneContext().IsBorrowing(*task.GenericPlayer()->Scene()), "queue fixture must hold a genuine published scene borrow before guarded operations");
		Require(std::get<float>(fixture.Read(getter, "left")) == 7 && std::get<float>(fixture.Read(getter, "top")) == 11, "existing readonly borrowed getter may inspect retained numeric state while the real scene is queued");
		const auto queued_getter = fixture.Call(child, "getLayerGetter", {std::string("shared lookup key")});
		const auto queued_getter_root = fixture.Vm.Retain(queued_getter);
		Require(std::get<std::int32_t>(fixture.Read(queued_getter, "type")) == 1, "child query itself must preserve existing root-query admission while queued");
		RequireFailure([&] {
			static_cast<void>(fixture.Call(child, "setCoord", {9.0f, 10.0f}));
		},
		    "queued row borrows");
		RequireFailure([&] {
			static_cast<void>(fixture.Call(motion, "clear"));
		},
		    "queued");
		RequireFailure([&] {
			fixture.Bindings->NativeOwners().ReleaseInstance(motion);
		},
		    "idle");
	} catch (...) {
		fixture.Primary->SceneContext().Clear();
		fixture.Secondary->SceneContext().Clear();
		throw;
	}
	fixture.Primary->SceneContext().Clear();
	fixture.Secondary->SceneContext().Clear();
	Require(task.Commands().ClockState().TickCount == tick && fixture.Secondary->Identifiers().LiveCount() == identifiers && std::get<float>(fixture.Read(getter, "left")) == 7, "queued rejection preserves actual clock, selected IDs and numeric row before consumption");
	static_cast<void>(fixture.Call(motion, "clear"));
	RequireFailure([&] {
		static_cast<void>(fixture.Call(getter, "getShape"));
	},
	    "retired");
	RequireFailure([&] {
		static_cast<void>(fixture.Call(child, "getLayerGetter", {std::string("shared lookup key")}));
	},
	    "retired");
	Require(task.IsLive() && fixture.Secondary->Identifiers().LiveCount() == 0, "expired child-scope queries cannot recreate the cleared selected tree");
	fixture.Bindings->NativeOwners().ReleaseInstance(child);
}

static void TestEmptyBorrowedChildLookupDoesNotEscapeToSibling() {
	Fixture fixture(2, true);
	const auto motion = fixture.Vm.Invoke(fixture.Bindings->MotionClass(), std::array<sgre::script::SquirrelValue, 2>{fixture.Vm.RootTable(), fixture.Leaf}).Value;
	const auto motion_root = fixture.Vm.Retain(motion);
	static_cast<void>(fixture.Call(motion, "setChara", {std::string("outer")}));
	static_cast<void>(fixture.Call(motion, "play", {std::string("main")}));
	static_cast<void>(fixture.Call(motion, "progress", {0.0f}));
	const auto empty = fixture.Call(motion, "getLayerMotion", {std::string("empty child map key")});
	const auto empty_root = fixture.Vm.Retain(empty);
	Require(!std::holds_alternative<std::monostate>(fixture.Call(motion, "getLayerGetter", {std::string("nested map key")})), "later selected sibling must genuinely contain the discriminating map entry");
	Require(std::holds_alternative<std::monostate>(fixture.Call(empty, "getLayerGetter", {std::string("nested map key")})) && std::holds_alternative<std::monostate>(fixture.Call(empty, "getLayerMotion", {std::string("child map key")})), "a live root-only child has no layer catalogue and cannot search its containing or sibling player");
	Require(fixture.Second().Count() == 1 && fixture.Secondary->PlayerCount() == 1 && fixture.Uploads == 0, "empty borrowed-child queries do not create a selected player, task or replacement texture");
}

static sgre::script::SquirrelValue ContainsMouseCall(Fixture& fixture, const sgre::script::SquirrelValue& getter, const sgre::script::SquirrelValue& x, const sgre::script::SquirrelValue& y) {
	const auto instruction = [](std::uint8_t opcode, std::uint8_t destination, std::int32_t argument1, std::uint8_t argument2 = 0, std::uint8_t argument3 = 0) {
		sgre::script::SquirrelInstruction result;
		result.Opcode = opcode;
		result.Argument0 = destination;
		result.Argument1 = argument1;
		result.Argument2 = argument2;
		result.Argument3 = argument3;

		return result;
	};
	sgre::script::BytecodeProgram program;
	program.MainFunction.Name = std::string("independent_mouse_hit_test");
	program.MainFunction.SourceName = std::string("independent-input-fixture.nut");
	program.MainFunction.Parameters = {std::string("this"), std::string("getter"), std::string("mouse_x"), std::string("mouse_y")};
	program.MainFunction.StackSize = 8;
	program.MainFunction.Literals = {std::string("contains")};
	program.MainFunction.Instructions = {instruction(8, 4, 0, 1, 5), instruction(10, 6, 2), instruction(10, 7, 3), instruction(5, 4, 4, 5, 3), instruction(19, 1, 4)};
	const auto callable = fixture.Vm.CreateClosure(std::move(program));
	const auto result = fixture.Vm.Invoke(callable, std::array<sgre::script::SquirrelValue, 4>{fixture.Vm.RootTable(), getter, x, y});
	Require(result.State == sgre::script::SquirrelRunState::Completed && fixture.Vm.Type(result.Value) == sgre::script::SquirrelValueType::Boolean, "actual script mouse hit-test must return the native boolean without suspending");

	return result.Value;
}

static std::shared_ptr<const sgre::resource::PsbDocument> FolderContainsDocument() {
	using sgre::resource::PsbArray;
	using sgre::resource::PsbObject;
	auto shape = Layer(false, 2);
	auto ordinary = Layer(false, 0);
	FixtureField(ordinary, "type") = Integer(0);
	std::get<PsbObject>(ordinary.Data).emplace_back("objTriPriority", Integer(0));
	auto& ordinary_frame = std::get<PsbArray>(FixtureField(ordinary, "frameList").Data)[0];
	auto& ordinary_content = std::get<PsbObject>(FixtureField(ordinary_frame, "content").Data);
	ordinary_content.emplace_back("src", Atom(std::string("")));
	ordinary_content.emplace_back("icon", Atom(std::string("")));
	FixtureField(ordinary, "children") = Atom(PsbArray{std::move(shape)});
	auto folder = Layer(false, 0);
	FixtureField(folder, "type") = Integer(2);
	FixtureField(folder, "children") = Atom(PsbArray{std::move(ordinary)});
	auto timeline = Timeline(true, 2, false);
	FixtureField(timeline, "layer") = Atom(PsbArray{std::move(folder)});
	FixtureField(timeline, "layerIndexMap") = Atom(PsbObject{{"folder", Integer(0)}, {"ordinary", Integer(1)}, {"leaf shape", Integer(2)}});
	auto& priority = std::get<PsbArray>(FixtureField(timeline, "priority").Data)[0];
	FixtureField(priority, "content") = Atom(PsbArray{Integer(0), Integer(1), Integer(2)});
	auto document = std::make_shared<sgre::resource::PsbDocument>(*Document(2, false));
	auto& outer = FixtureField(FixtureField(document->Root, "object"), "outer");
	FixtureField(FixtureField(outer, "motion"), "main") = std::move(timeline);

	return document;
}

static void TestGetterContainsActualVmMouseAndTypedArguments() {
	for (const auto shape : {std::uint32_t{0}, std::uint32_t{1}, std::uint32_t{2}, std::uint32_t{3}}) {
		Fixture fixture(shape);
		const auto motion = fixture.Vm.Invoke(fixture.Bindings->MotionClass(), std::array<sgre::script::SquirrelValue, 2>{fixture.Vm.RootTable(), fixture.Leaf}).Value;
		const auto motion_root = fixture.Vm.Retain(motion);
		static_cast<void>(fixture.Call(motion, "setChara", {std::string("outer")}));
		static_cast<void>(fixture.Call(motion, "play", {std::string("main")}));
		static_cast<void>(fixture.Call(motion, "progress", {0.0f}));
		const auto getter = fixture.Call(motion, "getLayerGetter", {std::string("rectangle map key")});
		const auto getter_root = fixture.Vm.Retain(getter);
		const auto tick = fixture.Bindings->NativeOwners().Commands(motion).ClockState().TickCount;
		Require(std::get<bool>(ContainsMouseCall(fixture, getter, 20.0f, 40.0f)) == (shape != 0), "script mouse hit-test must use the actual retained point/circle/rectangle/quad shape");
		if (shape == 1) {
			Require(std::get<bool>(ContainsMouseCall(fixture, getter, std::int32_t{36}, std::int32_t{40})) && !std::get<bool>(ContainsMouseCall(fixture, getter, 37.0f, 40.0f)), "live getter circle includes its native closed radius boundary");
		} else if (shape == 2) {
			Require(std::get<bool>(ContainsMouseCall(fixture, getter, 4.0f, 16.0f)) && !std::get<bool>(ContainsMouseCall(fixture, getter, 36.0f, 16.0f)) && !std::get<bool>(ContainsMouseCall(fixture, getter, 4.0f, 64.0f)), "live getter rectangle includes lower bounds and excludes upper bounds");
		} else if (shape == 3) {
			Require(std::get<bool>(ContainsMouseCall(fixture, getter, 36.0f, 64.0f)) && !std::get<bool>(ContainsMouseCall(fixture, getter, 37.0f, 64.0f)), "live getter quad keeps closed edge containment");
		}
		Require(fixture.Bindings->NativeOwners().Commands(motion).ClockState().TickCount == tick && fixture.Uploads == 0, "mouse queries cannot advance the player clock or invent texture work");
	}

	Fixture fixture;
	const auto motion = fixture.Vm.Invoke(fixture.Bindings->MotionClass(), std::array<sgre::script::SquirrelValue, 2>{fixture.Vm.RootTable(), fixture.Leaf}).Value;
	const auto motion_root = fixture.Vm.Retain(motion);
	static_cast<void>(fixture.Call(motion, "setChara", {std::string("outer")}));
	static_cast<void>(fixture.Call(motion, "play", {std::string("main")}));
	static_cast<void>(fixture.Call(motion, "setCoord", {-20.0f, -40.0f}));
	static_cast<void>(fixture.Call(motion, "progress", {0.0f}));
	const auto getter = fixture.Call(motion, "getLayerGetter", {std::string("rectangle map key")});
	const auto getter_root = fixture.Vm.Retain(getter);
	std::size_t conversions = 0;
	const auto conversion_class = fixture.Vm.CreateClass();
	const auto class_root = fixture.Vm.Retain(conversion_class);
	fixture.Vm.NewSlot(conversion_class, std::string("_tostring"), fixture.Vm.CreateNativeClosure("forbidden_hit_test_conversion", [&](auto&, const auto) {
		++conversions;

		return sgre::script::SquirrelNativeResult::ReturnValue(std::string("99"));
	}));
	const auto poison = fixture.Vm.CreateInstance(conversion_class);
	const auto poison_root = fixture.Vm.Retain(poison);
	Require(std::get<bool>(ContainsMouseCall(fixture, getter, std::string("99"), poison)) && std::get<bool>(fixture.Call(getter, "contains", {true, std::int32_t{1}, poison})) && std::get<bool>(fixture.Call(getter, "contains", {sgre::script::SquirrelValue{}, false})), "typed actual hit-test uses zero for failed conversions, numeric booleans and ignores extras");
	Require(conversions == 0, "contains must not perform object/string conversion for float arguments or extras");
	const auto forged = fixture.Vm.CreateInstance(fixture.Vm.GetSlot(fixture.Vm.RootTable(), std::string("MotionLayerGetter")));
	const auto forged_root = fixture.Vm.Retain(forged);
	RequireFailure([&] {
		static_cast<void>(fixture.Call(forged, "contains", {0.0f, 0.0f}));
	},
	    "native receiver");
}

static void TestGetterContainsRetainedGeometryAndNoVisibilityFilter() {
	Fixture fixture;
	const auto motion = fixture.Vm.Invoke(fixture.Bindings->MotionClass(), std::array<sgre::script::SquirrelValue, 2>{fixture.Vm.RootTable(), fixture.Leaf}).Value;
	const auto motion_root = fixture.Vm.Retain(motion);
	static_cast<void>(fixture.Call(motion, "setChara", {std::string("outer")}));
	static_cast<void>(fixture.Call(motion, "play", {std::string("main")}));
	const auto getter = fixture.Call(motion, "getLayerGetter", {std::string("rectangle map key")});
	const auto getter_root = fixture.Vm.Retain(getter);
	Require(!std::get<bool>(fixture.Call(getter, "contains", {20.0f, 40.0f})), "initial zero rectangle must remain uncomposed during a getter hit-test");
	static_cast<void>(fixture.Call(motion, "progress", {0.0f}));
	const auto snapshot = fixture.Call(getter, "getShape");
	const auto snapshot_root = fixture.Vm.Retain(snapshot);
	const auto task = fixture.Bindings->NativeOwners().Task(motion);
	const auto tick = task.Commands().ClockState().TickCount;
	const auto identifiers = fixture.Secondary->Identifiers().LiveCount();
	static_cast<void>(fixture.Call(motion, "setCoord", {100.0f, 200.0f}));
	Require(std::get<bool>(ContainsMouseCall(fixture, getter, 20.0f, 40.0f)) && !std::get<bool>(ContainsMouseCall(fixture, getter, 120.0f, 240.0f)), "hit-test must read completed geometry while a different pose is only pending");
	static_cast<void>(fixture.Call(motion, "progress", {0.0f}));
	Require(!std::get<bool>(fixture.Call(getter, "contains", {20.0f, 40.0f})) && std::get<bool>(fixture.Call(getter, "contains", {120.0f, 240.0f})) && std::get<bool>(fixture.Call(snapshot, "contains", {20.0f, 40.0f})), "only genuine progression changes live geometry; prior copied snapshot remains independent");
	static_cast<void>(fixture.Call(motion, "setVisible", {false}));
	Require(std::get<bool>(fixture.Call(getter, "contains", {120.0f, 240.0f})) && task.Commands().ClockState().TickCount == tick && fixture.Secondary->Identifiers().LiveCount() == identifiers && fixture.Uploads == 0, "native getter hit-test has no containing visibility filter, clock step or ownership allocation");
}

static void TestGetterContainsFolderAndActualChildSubtree() {
	{
		Fixture fixture(2, false, FolderContainsDocument());
		const auto motion = fixture.Vm.Invoke(fixture.Bindings->MotionClass(), std::array<sgre::script::SquirrelValue, 2>{fixture.Vm.RootTable(), fixture.Leaf}).Value;
		const auto motion_root = fixture.Vm.Retain(motion);
		static_cast<void>(fixture.Call(motion, "setChara", {std::string("outer")}));
		static_cast<void>(fixture.Call(motion, "play", {std::string("main")}));
		static_cast<void>(fixture.Call(motion, "progress", {0.0f}));
		const auto folder = fixture.Call(motion, "getLayerGetter", {std::string("folder")});
		const auto folder_root = fixture.Vm.Retain(folder);
		const auto ordinary = fixture.Call(motion, "getLayerGetter", {std::string("ordinary")});
		const auto ordinary_root = fixture.Vm.Retain(ordinary);
		const auto leaf = fixture.Call(motion, "getLayerGetter", {std::string("leaf shape")});
		const auto leaf_root = fixture.Vm.Retain(leaf);
		const auto shape = fixture.Call(leaf, "getShape");
		const auto shape_root = fixture.Vm.Retain(shape);
		const auto x = std::get<float>(fixture.Read(shape, "left")) + std::get<float>(fixture.Read(shape, "width")) * .5f;
		const auto y = std::get<float>(fixture.Read(shape, "top")) + std::get<float>(fixture.Read(shape, "height")) * .5f;
		Require(std::get<bool>(fixture.Call(leaf, "contains", {x, y})) && std::get<bool>(fixture.Call(folder, "contains", {x, y})) && !std::get<bool>(fixture.Call(ordinary, "contains", {x, y})), "folder recursive descent crosses ordinary nodes while a direct ordinary getter remains false");
	}
	{
		auto document = std::make_shared<sgre::resource::PsbDocument>(*NestedGetterDocument());
		auto& inner = FixtureField(FixtureField(FixtureField(FixtureField(document->Root, "object"), "inner"), "motion"), "main");
		auto& layer = std::get<sgre::resource::PsbArray>(FixtureField(inner, "layer").Data)[0];
		auto& frame = std::get<sgre::resource::PsbArray>(FixtureField(layer, "frameList").Data)[0];
		FixtureField(FixtureField(frame, "content"), "coord") = Atom(sgre::resource::PsbArray{Atom(100.0), Atom(100.0), Atom(0.0)});
		Fixture fixture(2, false, std::move(document));
		const auto motion = fixture.Vm.Invoke(fixture.Bindings->MotionClass(), std::array<sgre::script::SquirrelValue, 2>{fixture.Vm.RootTable(), fixture.Leaf}).Value;
		const auto motion_root = fixture.Vm.Retain(motion);
		static_cast<void>(fixture.Call(motion, "setChara", {std::string("outer")}));
		static_cast<void>(fixture.Call(motion, "play", {std::string("main")}));
		static_cast<void>(fixture.Call(motion, "progress", {0.0f}));
		const auto child_getter = fixture.Call(motion, "getLayerGetter", {std::string("child map key")});
		const auto getter_root = fixture.Vm.Retain(child_getter);
		const auto child = fixture.Call(child_getter, "getMotion");
		const auto child_root = fixture.Vm.Retain(child);
		const auto inner_getter = fixture.Call(child, "getLayerGetter", {std::string("shared lookup key")});
		const auto inner_root = fixture.Vm.Retain(inner_getter);
		const auto deep = fixture.Call(child, "getLayerMotion", {std::string("deep child key")});
		const auto deep_root = fixture.Vm.Retain(deep);
		const auto deep_getter = fixture.Call(deep, "getLayerGetter", {std::string("deep shape key")});
		const auto deep_getter_root = fixture.Vm.Retain(deep_getter);
		Require(!std::get<bool>(fixture.Call(inner_getter, "contains", {3.0f, 5.0f})) && std::get<bool>(fixture.Call(deep_getter, "contains", {3.0f, 5.0f})) && std::get<bool>(fixture.Call(child_getter, "contains", {3.0f, 5.0f})), "kind3 hit-test includes actual descendant child shape catalogue beyond its immediate player's shape");
		Require(fixture.Second().Count() == 1 && fixture.Secondary->PlayerCount() == 1 && fixture.Uploads == 0, "recursive hit-test cannot create wrappers as new tasks, players or textures");
	}
}

static void TestGetterContainsQueuedReadonlyAndWeakRetirement() {
	Fixture fixture;
	const auto motion = fixture.Vm.Invoke(fixture.Bindings->MotionClass(), std::array<sgre::script::SquirrelValue, 2>{fixture.Vm.RootTable(), fixture.Leaf}).Value;
	const auto motion_root = fixture.Vm.Retain(motion);
	static_cast<void>(fixture.Call(motion, "setChara", {std::string("outer")}));
	static_cast<void>(fixture.Call(motion, "setVisible", {true}));
	static_cast<void>(fixture.Call(motion, "play", {std::string("main")}));
	static_cast<void>(fixture.Call(motion, "progress", {0.0f}));
	const auto getter = fixture.Call(motion, "getLayerGetter", {std::string("rectangle map key")});
	const auto getter_root = fixture.Vm.Retain(getter);
	const auto snapshot = fixture.Call(getter, "getShape");
	const auto snapshot_root = fixture.Vm.Retain(snapshot);
	const auto task = fixture.Bindings->NativeOwners().Task(motion);
	const auto tick = task.Commands().ClockState().TickCount;
	const auto identifiers = fixture.Secondary->Identifiers().LiveCount();
	fixture.Second().Draw(0);
	try {
		Require(fixture.Secondary->SceneContext().Size() != 0 && std::get<bool>(ContainsMouseCall(fixture, getter, 20.0f, 40.0f)), "real queued scene permits readonly hit-testing of already retained geometry");
		RequireFailure([&] {
			static_cast<void>(fixture.Call(motion, "progress", {1.0f}));
		},
		    "idle");
		RequireFailure([&] {
			static_cast<void>(fixture.Call(motion, "clear"));
		},
		    "idle");
	} catch (...) {
		fixture.Primary->SceneContext().Clear();
		fixture.Secondary->SceneContext().Clear();
		throw;
	}
	fixture.Primary->SceneContext().Clear();
	fixture.Secondary->SceneContext().Clear();
	Require(task.Commands().ClockState().TickCount == tick && fixture.Secondary->Identifiers().LiveCount() == identifiers && std::get<bool>(fixture.Call(getter, "contains", {20.0f, 40.0f})), "queued query and rejected mutations preserve actual completed state");
	static_cast<void>(fixture.Call(motion, "play", {std::string("main"), std::int32_t{1}}));
	RequireFailure([&] {
		static_cast<void>(fixture.Call(getter, "contains", {20.0f, 40.0f}));
	},
	    "root selection");
	static_cast<void>(fixture.Call(motion, "progress", {0.0f}));
	const auto last = fixture.Call(motion, "getLayerGetter", {std::string("rectangle map key")});
	const auto last_root = fixture.Vm.Retain(last);
	fixture.Bindings->NativeOwners().ReleaseInstance(motion);
	fixture.Second().Update();
	Require(!task.IsLive(), "live getter hit-test weak borrow cannot keep its source task alive");
	RequireFailure([&] {
		static_cast<void>(fixture.Call(last, "contains", {20.0f, 40.0f}));
	},
	    "retired");
	Require(std::get<bool>(fixture.Call(snapshot, "contains", {20.0f, 40.0f})), "separate copied shape remains usable after live getter source retirement");
}

static void TestImmediateChildrenAndWrapperLifetime() {
	using sgre::resource::PsbArray;
	auto document = std::make_shared<sgre::resource::PsbDocument>(*FolderContainsDocument());
	auto& timeline = FixtureField(FixtureField(FixtureField(FixtureField(document->Root, "object"), "outer"), "motion"), "main");
	auto& folder = std::get<PsbArray>(FixtureField(timeline, "layer").Data)[0];
	std::get<PsbArray>(FixtureField(folder, "children").Data).push_back(Layer(false, 2));
	auto& priority = std::get<PsbArray>(FixtureField(timeline, "priority").Data)[0];
	FixtureField(priority, "content") = Atom(PsbArray{Integer(0), Integer(1), Integer(2), Integer(3)});
	Fixture fixture(2, false, std::move(document));
	const auto motion = fixture.Vm.Invoke(fixture.Bindings->MotionClass(), std::array<sgre::script::SquirrelValue, 2>{fixture.Vm.RootTable(), fixture.Leaf}).Value;
	const auto motion_root = fixture.Vm.Retain(motion);
	static_cast<void>(fixture.Call(motion, "setChara", {std::string("outer")}));
	static_cast<void>(fixture.Call(motion, "play", {std::string("main")}));
	static_cast<void>(fixture.Call(motion, "progress", {0.0f}));
	const auto getter = fixture.Call(motion, "getLayerGetter", {std::string("folder")});
	const auto getter_root = fixture.Vm.Retain(getter);
	const auto children = fixture.Call(getter, "getChildren");
	const auto children_root = fixture.Vm.Retain(children);
	Require(fixture.Vm.Length(children) == 2, "immediate children must exclude a grandchild while retaining both physical siblings");
	const auto ordinary = fixture.Vm.GetSlot(children, std::int32_t{0});
	const auto second = fixture.Vm.GetSlot(children, std::int32_t{1});
	Require(std::get<std::int32_t>(fixture.Call(ordinary, "getType")) == 0 && std::get<std::int32_t>(fixture.Call(second, "getType")) == 1, "child getters lost native ordinal order or used ambiguous layer labels");
	const auto grandchildren = fixture.Read(ordinary, "children");
	const auto grandchildren_root = fixture.Vm.Retain(grandchildren);
	Require(fixture.Vm.Length(grandchildren) == 1, "children property did not preserve the immediate child's own subtree");
	const auto leaf = fixture.Vm.GetSlot(grandchildren, std::int32_t{0});
	Require(fixture.Vm.Length(fixture.Call(leaf, "getChildren")) == 0, "a leaf getter must return an actual empty array");
	const auto repeated = fixture.Read(getter, "children");
	const auto repeated_root = fixture.Vm.Retain(repeated);
	Require(repeated != children && fixture.Vm.GetSlot(repeated, std::int32_t{0}) != ordinary, "each native children query must construct fresh getter wrappers");
	Require(fixture.Secondary->PlayerCount() == 1 && fixture.Uploads == 0, "enumerating layer children created another player or texture");
	static_cast<void>(fixture.Call(motion, "clear"));
	RequireFailure([&] {
		static_cast<void>(fixture.Call(ordinary, "getChildren"));
	},
	    "selected scene");
	RequireFailure([&] {
		static_cast<void>(fixture.Call(second, "getType"));
	},
	    "selected scene");
}

int main() {
	try {
		TestImmediateChildrenAndWrapperLifetime();
		TestGetterContainsActualVmMouseAndTypedArguments();
		TestGetterContainsRetainedGeometryAndNoVisibilityFilter();
		TestGetterContainsFolderAndActualChildSubtree();
		TestGetterContainsQueuedReadonlyAndWeakRetirement();
		TestBorrowedChildScopedGettersAndRetainedTransforms();
		TestNestedBorrowedGetterMotionAndSourceFirstRetirement();
		TestBorrowedChildGetterQueueAndExpiredSelectionGuards();
		TestEmptyBorrowedChildLookupDoesNotEscapeToSibling();
		TestDirectBorrowedSyncControls();
		TestDirectBorrowedChildCoordinate();
		TestDirectNamedChildMotion();
		TestBorrowedChildRootZoom();
		TestBorrowedChildRootAngles();
		TestBorrowedChildRootOpacity();
		TestActualClearAndExpiredChildBorrow();
		TestBorrowedChildParametersAndLifetime();
		TestRealMapSelectionAndSnapshotLifetime();
		TestAllSnapshotMapsAndContainment();
		TestEmptyActualChildCatalogueCount();
		TestRetainedNumericPosition();
		TestActualScriptProgress();
		TestNativeConstructorsAndArgumentGuards();
		std::cout << "Motion layer getter: real recursive map, weak generation borrow and independent shape snapshots PASS\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
