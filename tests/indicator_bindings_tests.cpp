#include "sgre/script/indicator_bindings.hpp"

#include "sgre/scene/bitmap_font_assets.hpp"
#include "sgre/script/font_bindings.hpp"
#include "sgre/script/layer_bindings.hpp"
#include "sgre/script/psb_bindings.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
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

	throw std::runtime_error("bitmap font owner fixture expected failure: " + std::string(expected));
}

template <typename Value>
static sgre::resource::PsbValue Atom(Value value) {
	sgre::resource::PsbValue result;
	result.Data = std::move(value);

	return result;
}

static std::shared_ptr<sgre::resource::PsbDocument> Document() {
	using sgre::resource::PsbArray;
	using sgre::resource::PsbObject;
	PsbArray sources;
	for (std::uint32_t index = 0; index < 2; ++index) {
		sgre::resource::PsbChunkReference chunk;
		chunk.Index = index;
		auto source = Atom(PsbObject{{"type", Atom(std::string("A8"))}, {"width", Atom(std::int64_t{1})}, {"height", Atom(std::int64_t{1})}, {"truncated_width", Atom(std::int64_t{1})}, {"truncated_height", Atom(std::int64_t{1})}, {"pixel", Atom(chunk)}});
		source.SerializedOffset = 64 + index * 32;
		sources.push_back(std::move(source));
	}

	auto glyph = Atom(PsbObject{{"id", Atom(std::int64_t{0})}, {"x", Atom(std::int64_t{0})}, {"y", Atom(std::int64_t{0})}, {"width", Atom(std::int64_t{5})}, {"height", Atom(std::int64_t{6})}, {"w", Atom(std::int64_t{1})}, {"h", Atom(std::int64_t{1})}, {"a", Atom(std::int64_t{2})}, {"b", Atom(std::int64_t{9})}, {"d", Atom(std::int64_t{10})}});
	auto document = std::make_shared<sgre::resource::PsbDocument>();
	document->Header.Version = 4;
	document->Header.RootOffset = 44;
	document->Header.DataOffset = 128;
	document->SerializedSize = 130;
	document->Chunks = {{128, 1}, {129, 1}};
	document->Root = Atom(PsbObject{{"id", Atom(std::string("font"))}, {"spec", Atom(std::string("win"))}, {"version", Atom(1.0800000429153442)}, {"label", Atom(std::string("Synthetic font owner"))}, {"minWidth", Atom(std::int64_t{3})}, {"minHeight", Atom(std::int64_t{6})}, {"maxWidth", Atom(std::int64_t{8})}, {"maxHeight", Atom(std::int64_t{6})}, {"source", Atom(std::move(sources))}, {"code", Atom(PsbObject{{"A", std::move(glyph)}})}});
	document->Root.SerializedOffset = document->Header.RootOffset;

	return document;
}

static std::shared_ptr<std::vector<std::uint8_t>> Backing() {
	auto backing = std::make_shared<std::vector<std::uint8_t>>(130);
	backing->at(128) = 128;
	backing->at(129) = 64;

	return backing;
}

static sgre::script::SquirrelValue Call(sgre::script::SquirrelVm& vm, const sgre::script::SquirrelValue& instance, std::string_view name, std::vector<sgre::script::SquirrelValue> arguments = {}) {
	arguments.insert(arguments.begin(), instance);

	try {
		return vm.Invoke(vm.GetSlot(instance, std::string(name)), arguments).Value;
	} catch (const std::exception& error) {
		throw std::runtime_error(std::string(name) + ": " + error.what());
	}
}

static sgre::script::SquirrelValue Construct(sgre::script::SquirrelVm& vm, const sgre::script::SquirrelValue& class_value, std::vector<sgre::script::SquirrelValue> arguments = {}) {
	arguments.insert(arguments.begin(), vm.RootTable());

	return vm.Invoke(class_value, arguments).Value;
}

static void TestActualClassTaskAndProviderLifetime() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	sgre::script::SquirrelVm vm(limits);
	sgre::script::ScriptObjectBindings objects(vm);
	sgre::script::ScriptPsbBindings psb(vm, objects);
	std::uint32_t uploads = 0;
	std::vector<std::uint32_t> releases;
	sgre::scene::TextureCache textures([&](auto, const auto&) {
		return sgre::render::SpriteTextureHandle{++uploads, 1, &uploads};
	},
	    [&](auto texture) {
		    releases.push_back(texture.Index);
	    });
	sgre::scene::MotionAssetCatalog motions(textures);
	sgre::scene::BitmapFontAssetCatalog fonts(textures);
	sgre::scene::LayerTree tree(8);
	sgre::script::ScriptLayerBindings layers(vm, objects, tree);
	layers.ConfigureMotionOwners(psb, motions, textures);
	sgre::script::ScriptIndicatorBindings indicators(vm, objects, layers, tree, psb, fonts, textures);
	Require(vm.GetSlot(vm.RootTable(), std::string("LayerView")) == indicators.LayerViewClass() && vm.GetSlot(vm.RootTable(), std::string("Indicator")) == indicators.IndicatorClass(), "native class lookup names must retain genuine LayerView and Indicator identities");
	RequireFailure([&] {
		static_cast<void>(Construct(vm, indicators.LayerViewClass()));
	},
	    "can't create instance");
	auto layer = Construct(vm, layers.LayerClass());
	auto font = psb.WrapDocument(Document(), Backing());
	auto first = Construct(vm, indicators.IndicatorClass(), {layer, font, std::string("ignored constructor tail")});
	auto second = Construct(vm, indicators.IndicatorClass(), {layer, font});
	Require(vm.IsInstanceOf(first, indicators.LayerViewClass()) && vm.IsInstanceOf(first, objects.ObjectClass()) && !vm.IsInstanceOf(first, layers.LayerBaseClass()), "Indicator must have real LayerView/Object inheritance without becoming a native Layer");
	Require(vm.RetainedReferenceCount(font) == 2 && vm.RetainedReferenceCount(layer) == 0, "native wrappers must retain their font PSBObject separately while borrowing the parent");
	auto& group = layers.MotionTasks(layer).Group(sgre::scene::MotionTaskGroup::First);
	std::array<sgre::scene::ParallelTaskHandle, 2> task_order;
	Require(group.Count() == 2 && group.WriteOrder(task_order) == 2 && group.State(task_order[0]).DrawPaused && group.State(task_order[0]).Status == sgre::scene::TaskStatus::Active, "each constructor must attach one initially hidden active task to the actual first view group");
	const auto first_source = indicators.FontSources(first).Source(0);
	const auto second_source = indicators.FontSources(second).Source(0);
	const auto texture = first_source.Texture();
	Require(uploads == 2 && !first_source.SameIdentity(second_source) && first_source.Texture() == second_source.Texture() && textures.Get(texture).References == 2, "actual font tasks must retain separate source wrappers sharing cache-scoped physical textures");
	static_cast<void>(Call(vm, layer, "setSmoothing", {true}));
	Require(indicators.FontSources(first).Minification(0) == 1 && indicators.FontSources(second).Magnification(1) == 1, "Layer smoothing must reach each borrowed Indicator provider");
	static_cast<void>(Call(vm, first, "setVisible", {true}));
	Require(!group.State(task_order[0]).DrawPaused && std::get<bool>(Call(vm, first, "getVisible")), "LayerView visibility must control its real task pause byte");
	static_cast<void>(Call(vm, first, "print", {std::string("A")}));
	static_cast<void>(Call(vm, first, "setCoord", {std::int32_t{10}, 20.0f}));
	Require(std::get<float>(Call(vm, first, "getWidth")) == 5 && std::get<float>(Call(vm, first, "getHeight")) == 6 && std::get<float>(Call(vm, first, "getRight")) == 15 && std::get<float>(Call(vm, first, "getBottom")) == 26, "genuine bitmap task text measurement and placement must feed inherited LayerView getters");
	static_cast<void>(Call(vm, first, "setSize", {999.0f, 999.0f}));
	Require(std::get<float>(Call(vm, first, "getWidth")) == 5, "Indicator inherits the native inert request-size entry");
	indicators.ReleaseInstance(first);
	Require(vm.RetainedReferenceCount(font) == 1 && group.Count() == 2 && group.State(task_order[0]).Status == sgre::scene::TaskStatus::Retire && releases.empty() && first_source.IsLive(), "wrapper deletion marks its task and releases script roots without eagerly destroying font sources");
	group.Update();
	Require(group.Count() == 1 && !first_source.IsLive() && second_source.IsLive() && textures.Get(texture).References == 1 && releases.empty(), "the next owner update must retire only the marked task while preserving shared source pixels");
	layers.ReleaseInstance(layer);
	Require(!second_source.IsLive() && releases == std::vector<std::uint32_t>{1, 2} && vm.RetainedReferenceCount(font) == 1, "parent deletion must retire font tasks before detaching wrappers without releasing a retained wrapper's PSB root");
	Require(!std::get<bool>(Call(vm, second, "getVisible")) && std::get<float>(Call(vm, second, "getWidth")) == 0 && std::get<float>(Call(vm, second, "getFontScaleX")) == 1 && std::get<float>(Call(vm, second, "getFontMaxSizeY")) == 1 && std::get<float>(Call(vm, second, "getSpaceX")) == 1 && std::get<bool>(Call(vm, second, "getRecognizeTag")) && std::get<bool>(Call(vm, second, "getDrawAtIntegerCoord")) && std::get<std::int32_t>(Call(vm, second, "getFontColor")) == -1 && std::get<std::int32_t>(Call(vm, second, "getOpacity")) == 255, "retained detached wrappers must use native defaults without dereferencing deleted tasks or fonts");
	static_cast<void>(Call(vm, second, "setCoord", {1.0f, 2.0f}));
	static_cast<void>(Call(vm, second, "print", {std::string("A")}));
	RequireFailure([&] {
		static_cast<void>(indicators.BitmapState(second));
	},
	    "detached Indicator");
	indicators.ReleaseInstance(second);
	Require(vm.RetainedReferenceCount(font) == 0, "detached wrapper deletion must retire its remaining native ObjectInfo root exactly once");
	vm.Close();
}

static void TestInitialSmoothingConversionsAndInvalidParents() {
	sgre::script::SquirrelVm vm;
	sgre::script::ScriptObjectBindings objects(vm);
	sgre::script::ScriptPsbBindings psb(vm, objects);
	std::uint32_t uploads = 0;
	sgre::scene::TextureCache textures([&](auto, const auto&) {
		return sgre::render::SpriteTextureHandle{++uploads, 1, &uploads};
	},
	    [](auto) {
	    });
	sgre::scene::MotionAssetCatalog motions(textures);
	sgre::scene::BitmapFontAssetCatalog fonts(textures);
	sgre::scene::LayerTree tree(8);
	sgre::script::ScriptLayerBindings layers(vm, objects, tree);
	layers.ConfigureMotionOwners(psb, motions, textures);
	sgre::script::ScriptIndicatorBindings indicators(vm, objects, layers, tree, psb, fonts, textures);
	const auto folder = Construct(vm, layers.LayerFolderClass());
	const auto layer = Construct(vm, layers.LayerClass());
	const auto font = psb.WrapDocument(Document(), Backing());
	RequireFailure([&] {
		static_cast<void>(Construct(vm, indicators.IndicatorClass(), {folder, font}));
	},
	    "actual Layer");
	RequireFailure([&] {
		static_cast<void>(Construct(vm, indicators.IndicatorClass(), {layer, vm.CreateTable()}));
	},
	    "PSBObject");
	Require(layers.MotionTasks(layer).Group(sgre::scene::MotionTaskGroup::First).Count() == 0 && uploads == 0, "invalid provider/parent paths must leave task and texture ownership unchanged");
	static_cast<void>(Call(vm, layer, "setSmoothing", {true}));
	const auto indicator = Construct(vm, indicators.IndicatorClass(), {layer, font});
	Require(indicators.FontSources(indicator).Minification(0) == 1 && indicators.FontSources(indicator).Magnification(1) == 1, "new providers must inherit current Layer smoothing after task attachment");
	static_cast<void>(Call(vm, indicator, "setFontScaleX", {true}));
	static_cast<void>(Call(vm, indicator, "setFontScaleY", {std::string("ignored numeric type")}));
	Require(std::get<float>(Call(vm, indicator, "getFontScaleX")) == 1 && std::get<float>(Call(vm, indicator, "getFontScaleY")) == 0, "native float conversion must accept booleans and retain initialized zero for nonnumeric values");
	static_cast<void>(Call(vm, indicator, "setFontScaleY"));
	static_cast<void>(Call(vm, indicator, "setFontColor", {std::int32_t{-123}}));
	static_cast<void>(Call(vm, indicator, "setOpacity", {std::int32_t{-2}}));
	Require(std::get<std::int32_t>(Call(vm, indicator, "getFontColor")) == -123 && std::get<std::int32_t>(Call(vm, indicator, "getOpacity")) == -2, "native 32-bit colors and opacity preserve their full signed script words");
	static_cast<void>(Call(vm, indicator, "setRecognizeTag", {std::string("truthy even when empty")}));
	static_cast<void>(Call(vm, indicator, "setDrawAtIntegerCoord", {std::string{}}));
	Require(std::get<bool>(Call(vm, indicator, "getRecognizeTag")) && std::get<bool>(Call(vm, indicator, "getDrawAtIntegerCoord")), "native truth conversion treats strings as true independently of their length");
	static_cast<void>(Call(vm, indicator, "print", {std::int32_t{12}}));
	Require(indicators.BitmapState(indicator).Elements().size() == 2, "the string thunk must execute native tostring for nonstring print input");
	vm.Close();
}

static void TestAutomaticWrapperReclamationKeepsDelayedTask() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	sgre::script::SquirrelVm vm(limits);
	sgre::script::ScriptObjectBindings objects(vm);
	sgre::script::ScriptPsbBindings psb(vm, objects);
	std::uint32_t uploads = 0;
	std::uint32_t releases = 0;
	sgre::scene::TextureCache textures([&](auto, const auto&) {
		return sgre::render::SpriteTextureHandle{++uploads, 1, &uploads};
	},
	    [&](auto) {
		    ++releases;
	    });
	sgre::scene::MotionAssetCatalog motions(textures);
	sgre::scene::BitmapFontAssetCatalog fonts(textures);
	sgre::scene::LayerTree tree(8);
	sgre::script::ScriptLayerBindings layers(vm, objects, tree);
	layers.ConfigureMotionOwners(psb, motions, textures);
	sgre::script::ScriptIndicatorBindings indicators(vm, objects, layers, tree, psb, fonts, textures);
	auto layer = Construct(vm, layers.LayerClass());
	auto document = Document();
	auto backing = Backing();
	const std::weak_ptr<const sgre::resource::PsbDocument> weak_document = document;
	const std::weak_ptr<const std::vector<std::uint8_t>> weak_backing = backing;
	auto font = psb.WrapDocument(document, backing);
	document.reset();
	backing.reset();
	auto indicator = Construct(vm, indicators.IndicatorClass(), {layer, font});
	const auto weak_indicator = vm.CreateWeakReference(indicator);
	const auto weak_font = vm.CreateWeakReference(font);
	const auto weak_layer = vm.CreateWeakReference(layer);
	auto& group = layers.MotionTasks(layer).Group(sgre::scene::MotionTaskGroup::First);
	std::array<sgre::scene::ParallelTaskHandle, 1> order;
	Require(group.WriteOrder(order) == 1, "actual first view group must own the constructed task");
	font = {};
	indicator = {};
	// The native call result slot is a separate strong root until the next invocation overwrites it.
	static_cast<void>(Call(vm, layer, "getSmoothing"));
	Require(std::holds_alternative<std::monostate>(vm.ResolveWeakReference(weak_indicator)) && std::holds_alternative<std::monostate>(vm.ResolveWeakReference(weak_font)) && group.State(order[0]).Status == sgre::scene::TaskStatus::Retire && releases == 0, "automatic wrapper deletion must drop its PSB root but defer task disposal");
	Require(!weak_document.expired() && !weak_backing.expired(), "the task must retain safe immutable input ownership independently of the retired script PSBObject");
	group.Update();
	Require(group.Count() == 0 && releases == 2 && weak_document.expired() && weak_backing.expired(), "deferred task retirement must release font textures and then the last immutable document/backing owners");
	layer = {};
	Require(std::holds_alternative<std::monostate>(vm.ResolveWeakReference(weak_layer)) && tree.ChildCount() == 0, "borrowed Indicator registration must never root its parent Layer");
	vm.Close();
}

static void TestConstructionRollbackAndParentReleaseFailure() {
	sgre::script::SquirrelVm vm;
	sgre::script::ScriptObjectBindings objects(vm);
	sgre::script::ScriptPsbBindings psb(vm, objects);
	std::uint32_t uploads = 0;
	std::vector<std::uint32_t> releases;
	bool fail_release = false;
	sgre::scene::TextureCache textures([&](auto, const auto&) {
		++uploads;
		if (uploads == 2) {
			throw std::runtime_error("font upload fixture");
		}

		return sgre::render::SpriteTextureHandle{uploads, 1, &uploads};
	},
	    [&](auto texture) {
		    releases.push_back(texture.Index);
		    if (fail_release && texture.Index == 3) {
			    throw std::runtime_error("font release fixture");
		    }
	    });
	sgre::scene::MotionAssetCatalog motions(textures);
	sgre::scene::BitmapFontAssetCatalog fonts(textures);
	sgre::scene::LayerTree tree(8);
	sgre::script::ScriptLayerBindings layers(vm, objects, tree);
	layers.ConfigureMotionOwners(psb, motions, textures);
	sgre::script::ScriptIndicatorBindings indicators(vm, objects, layers, tree, psb, fonts, textures);
	const auto layer = Construct(vm, layers.LayerClass());
	const auto layer_handle = layers.Handle(layer);
	const auto font = psb.WrapDocument(Document(), Backing());
	RequireFailure([&] {
		static_cast<void>(Construct(vm, indicators.IndicatorClass(), {layer, font}));
	},
	    "font upload fixture");
	Require(releases == std::vector<std::uint32_t>{1} && layers.MotionTasks(layer).Group(sgre::scene::MotionTaskGroup::First).Count() == 0 && vm.RetainedReferenceCount(font) == 0, "provider construction failure must retire acquired textures without leaving task or PSB roots");
	static_cast<void>(Call(vm, layer, "setSmoothing", {true}));
	const auto indicator = Construct(vm, indicators.IndicatorClass(), {layer, font});
	const auto first_source = indicators.FontSources(indicator).Source(0);
	const auto second_source = indicators.FontSources(indicator).Source(1);
	fail_release = true;
	RequireFailure([&] {
		layers.ReleaseInstance(layer);
	},
	    "font release fixture");
	Require(!tree.Contains(layer_handle) && !first_source.IsLive() && !second_source.IsLive() && releases == std::vector<std::uint32_t>{1, 3, 4}, "one physical release failure must still retire every source, task, view and parent scene record");
	Require(std::get<float>(Call(vm, indicator, "getFontScale")) == 1 && vm.RetainedReferenceCount(font) == 1, "failed parent cleanup still detaches the retained wrapper and preserves its separate PSB root");
	indicators.ReleaseInstance(indicator);
	Require(vm.RetainedReferenceCount(font) == 0, "retained wrapper cleanup must complete after failed parent disposal");
	vm.Close();
}

static void TestPsbFontProviderBranchAndIndependentRoots() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	sgre::script::SquirrelVm vm(limits);
	sgre::script::ScriptObjectBindings objects(vm);
	sgre::script::ScriptPsbBindings psb(vm, objects);
	sgre::script::ScriptFontBindings font_bindings(vm, objects, psb);
	std::uint32_t uploads = 0;
	std::uint32_t releases = 0;
	sgre::scene::TextureCache textures([&](auto, const auto&) {
		return sgre::render::SpriteTextureHandle{++uploads, 1, &uploads};
	},
	    [&](auto) {
		    ++releases;
	    });
	sgre::scene::MotionAssetCatalog motions(textures);
	sgre::scene::BitmapFontAssetCatalog fonts(textures);
	sgre::scene::LayerTree tree(8);
	sgre::script::ScriptLayerBindings layers(vm, objects, tree);
	layers.ConfigureMotionOwners(psb, motions, textures);
	sgre::script::ScriptIndicatorBindings indicators(vm, objects, layers, tree, psb, fonts, textures, &font_bindings);
	const auto layer = Construct(vm, layers.LayerClass());
	const auto owner = psb.WrapDocument(Document(), Backing());
	const auto provider = Construct(vm, font_bindings.PsbFontClass(), {owner});
	Require(uploads == 0 && fonts.DocumentCount() == 0 && vm.RetainedReferenceCount(owner) == 1, "PSBFont construction must retain its PSB object without parsing/loading texture sources");
	const auto unavailable = Construct(vm, font_bindings.SystemFontClass());
	RequireFailure([&] {
		static_cast<void>(Construct(vm, indicators.IndicatorClass(), {layer, unavailable}));
	},
	    "no native font provider");
	Require(uploads == 0 && layers.MotionTasks(layer).Group(sgre::scene::MotionTaskGroup::First).Count() == 0 && vm.RetainedReferenceCount(owner) == 1, "unavailable native SystemFont must reject unsafe consumers before borrowing tasks or textures");
	const auto first = Construct(vm, indicators.IndicatorClass(), {layer, provider});
	const auto second = Construct(vm, indicators.IndicatorClass(), {layer, provider, std::string("ignored tail")});
	Require(vm.RetainedReferenceCount(owner) == 3 && vm.RetainedReferenceCount(provider) == 0 && uploads == 2, "Indicator Font branch must copy underlying PSB roots independently and request fresh sharing providers");
	const auto first_source = indicators.FontSources(first).Source(0);
	const auto second_source = indicators.FontSources(second).Source(0);
	Require(!first_source.SameIdentity(second_source) && first_source.Texture() == second_source.Texture() && textures.Get(first_source.Texture()).References == 2, "Font branch reused one mutable provider wrapper instead of creating each native MPSBFont");
	font_bindings.ReleaseInstance(provider);
	Require(vm.RetainedReferenceCount(owner) == 2 && objects.HasInstance(first) && first_source.IsLive() && second_source.IsLive(), "PSBFont deletion must preserve independent Indicator roots/providers");
	static_cast<void>(Call(vm, first, "print", {std::string("A")}));
	Require(std::get<float>(Call(vm, first, "getFontScale")) == 1 && std::get<float>(Call(vm, first, "getWidth")) == 5, "fresh PSBFont provider must retain native initial scale1 and actual glyph measurement");
	indicators.ReleaseInstance(first);
	Require(vm.RetainedReferenceCount(owner) == 1 && first_source.IsLive(), "Font branch script retirement must defer physical provider deletion to the Layer");
	layers.MotionTasks(layer).Group(sgre::scene::MotionTaskGroup::First).Update();
	Require(!first_source.IsLive() && second_source.IsLive() && releases == 0, "retiring one Font branch provider damaged its independent shared texture owner");
	layers.ReleaseInstance(layer);
	Require(!second_source.IsLive() && releases == 2 && vm.RetainedReferenceCount(owner) == 1, "parent cleanup must detach Font branch wrappers while retaining their separate document root");
	indicators.ReleaseInstance(second);
	Require(vm.RetainedReferenceCount(owner) == 0, "Font branch final deletion leaked its copied root");
	vm.Close();
}

static void TestReclaimedPsbFontDoesNotRetireItsConsumer() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	sgre::script::SquirrelVm vm(limits);
	sgre::script::ScriptObjectBindings objects(vm);
	sgre::script::ScriptPsbBindings psb(vm, objects);
	sgre::script::ScriptFontBindings font_bindings(vm, objects, psb);
	std::uint32_t uploads = 0;
	sgre::scene::TextureCache textures([&](auto, const auto&) {
		return sgre::render::SpriteTextureHandle{++uploads, 1, &uploads};
	},
	    [](auto) {
	    });
	sgre::scene::MotionAssetCatalog motions(textures);
	sgre::scene::BitmapFontAssetCatalog fonts(textures);
	sgre::scene::LayerTree tree(8);
	sgre::script::ScriptLayerBindings layers(vm, objects, tree);
	layers.ConfigureMotionOwners(psb, motions, textures);
	sgre::script::ScriptIndicatorBindings indicators(vm, objects, layers, tree, psb, fonts, textures, &font_bindings);
	const auto layer = Construct(vm, layers.LayerClass());
	auto owner = psb.WrapDocument(Document(), Backing());
	auto provider = Construct(vm, font_bindings.PsbFontClass(), {owner});
	const auto weak_provider = vm.CreateWeakReference(provider);
	const auto weak_owner = vm.CreateWeakReference(owner);
	const auto indicator = Construct(vm, indicators.IndicatorClass(), {layer, provider});
	provider = {};
	owner = {};
	static_cast<void>(Call(vm, layer, "getSmoothing"));
	Require(std::holds_alternative<std::monostate>(vm.ResolveWeakReference(weak_provider)) && !std::holds_alternative<std::monostate>(vm.ResolveWeakReference(weak_owner)), "Indicator must keep the underlying PSB object alive while allowing unused PSBFont reclamation");
	static_cast<void>(Call(vm, indicator, "print", {std::string("A")}));
	Require(indicators.BitmapState(indicator).Elements().size() == 1 && uploads == 2, "reclaimed Font wrapper invalidated an owned Indicator provider");
	vm.Close();
}

int main() {
	try {
		TestActualClassTaskAndProviderLifetime();
		TestInitialSmoothingConversionsAndInvalidParents();
		TestAutomaticWrapperReclamationKeepsDelayedTask();
		TestConstructionRollbackAndParentReleaseFailure();
		TestPsbFontProviderBranchAndIndependentRoots();
		TestReclaimedPsbFontDoesNotRetireItsConsumer();
		std::cout << "actual Indicator bitmap provider, Layer task ownership, script roots and detached views passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
