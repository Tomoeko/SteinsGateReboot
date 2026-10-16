#include "sgre/script/text_render_bindings.hpp"

#include "font_info_fixture.hpp"
#include "glyph_atlas_owner_fixture.hpp"
#include "sgre/scene/motion_assets.hpp"
#include "sgre/script/layer_bindings.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

template <typename Operation>
static void RequireFailure(Operation operation, std::string_view message) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(message) != std::string_view::npos, error.what());

		return;
	}

	throw std::runtime_error("TextRender FontInfo integration expected failure");
}

static sgre::script::SquirrelValue Construct(sgre::script::SquirrelVm& vm, const sgre::script::SquirrelValue& class_value, std::vector<sgre::script::SquirrelValue> arguments = {}) {
	arguments.insert(arguments.begin(), vm.RootTable());

	return vm.Invoke(class_value, arguments).Value;
}

static sgre::text::TextFontSelectionKey Key(float size) {
	sgre::text::TextFontSelectionKey key;
	key.RequestedSize = size;
	key.FaceIndex = 7;

	return key;
}

struct Fixture {
	sgre::script::SquirrelVm Vm;
	sgre::script::ScriptObjectBindings Objects{Vm};
	sgre::script::ScriptPsbBindings Psb{Vm, Objects};
	sgre::script::ScriptVectorFontBindings Vectors{Vm, Objects, [](auto, auto) {
		                                               return []() -> std::optional<sgre::script::ScriptResourceAsset> {
			                                               return {};
		                                               };
	                                               },
	    [](auto, auto) {
	    }};
	sgre::script::SquirrelValue Source = Psb.WrapDocument(font_info_test::Document(), font_info_test::Backing());
	sgre::script::SquirrelValue WeakSelf;
	std::uint32_t Uploads = 0;
	std::vector<std::size_t> WeakRootsOnRelease;
	sgre::scene::TextureCache Textures{[this](auto, const auto&) {
		                                   return sgre::render::SpriteTextureHandle{++Uploads, 1, &Uploads};
	                                   },
	    [this](auto) {
		    Require(Vm.RetainedReferenceCount(Source) >= 1, "actual FontInfo supplied source must survive private provider retirement");
		    WeakRootsOnRelease.push_back(Vm.RetainedReferenceCount(WeakSelf));
	    }};
	sgre::scene::BitmapFontAssetCatalog Fonts{Textures};
	sgre::scene::MotionAssetCatalog Motions{Textures};
	sgre::scene::LayerTree Tree{16};
	sgre::script::ScriptLayerBindings Layers{Vm, Objects, Tree};
	text_atlas_test::Backend Backend;
	sgre::script::ScriptTextRenderBindings Text{Vm, Objects, Layers, Tree, Backend};

	explicit Fixture(bool configure = true) {
		Layers.ConfigureMotionOwners(Psb, Motions, Textures);
		if (configure) {
			Text.ConfigureFontOwners(Psb, Vectors, Fonts, Textures);
		}
	}
	~Fixture() {
		Vm.Close();
	}
};

static void TestActualLayerTaskOwnsNormalRubyInterfaces(bool parent_first) {
	Fixture fixture;
	auto& vm = fixture.Vm;
	const auto class_value = vm.CreateClass(fixture.Text.TextRenderClass());
	std::size_t calls = 0;
	std::size_t process_calls = 0;
	vm.NewSlot(class_value, std::string("onProcess"), vm.CreateNativeClosure("unbound_text_process_member", [&](auto&, auto) {
		++process_calls;

		return sgre::script::SquirrelNativeResult::NoValues();
	}));
	vm.NewSlot(class_value, std::string("findFont"), vm.CreateNativeClosure("actual_text_find_font", [&](auto&, auto arguments) {
		Require(arguments.size() == 4 && arguments[0] == vm.ResolveWeakReference(fixture.WeakSelf), "real native TextRender task must call its own cached weak wrapper");
		++calls;

		return sgre::script::SquirrelNativeResult::ReturnValue(fixture.Source);
	}));
	const auto layer = Construct(vm, fixture.Layers.LayerClass());
	const auto instance = Construct(vm, class_value, {layer});
	fixture.WeakSelf = vm.CreateWeakReference(instance);
	Require(vm.RetainedReferenceCount(fixture.WeakSelf) == 2 && fixture.Uploads == 0 && fixture.Backend.Observed.Creates == 0, "real task construction owns weak roots and empty font collections without allocating replacement assets");
	const std::array<sgre::script::SquirrelValue, 2> smoothing{layer, true};
	static_cast<void>(vm.Invoke(vm.GetSlot(layer, std::string("setSmoothing")), smoothing));
	const auto normal = fixture.Text.SelectFont(instance, sgre::text::TextFontFamily::Normal, Key(24), "face");
	const auto ruby = fixture.Text.SelectFont(instance, sgre::text::TextFontFamily::Ruby, Key(12), "ruby");
	Require(normal.Font && ruby.Font && normal.Font != ruby.Font && normal.Font->BitmapScale().Height == 4 && ruby.Font->BitmapScale().Height == 2 && calls == 2, "task's real callback must create separate normal/ruby interfaces with exact requested sizes");
	Require(normal.Font->BitmapProvider()->Minification(0) == 1 && ruby.Font->BitmapProvider()->Magnification(1) == 1 && fixture.Uploads == 2 && vm.RetainedReferenceCount(fixture.Source) == 2, "Layer filtering must reach both retained font providers while physical source textures remain shared");
	auto& group = fixture.Layers.MotionTasks(layer).Group(sgre::scene::MotionTaskGroup::First);
	group.Update();
	Require(process_calls == 0 && calls == 2, "native TextRender own-callback virtual is empty and must not invoke an inferred onProcess member");
	if (parent_first) {
		fixture.Layers.ReleaseInstance(layer);
		Require(fixture.Text.IsDetached(instance) && fixture.WeakRootsOnRelease == std::vector<std::size_t>{2, 2}, "parent task destruction must release FontInfo before copied weak self and borrowed wrapper detach");
		Require(vm.RetainedReferenceCount(fixture.WeakSelf) == 1 && vm.RetainedReferenceCount(fixture.Source) == 0 && fixture.Textures.Count() == 0, "retained detached wrapper must keep only its base weak self after task interfaces retire");
		RequireFailure([&] {
			static_cast<void>(fixture.Text.SelectFont(instance, sgre::text::TextFontFamily::Normal, Key(24), "face"));
		},
		    "live task");
		fixture.Text.ReleaseInstance(instance);
	} else {
		fixture.Text.ReleaseInstance(instance);
		Require(group.Count() == 1 && vm.RetainedReferenceCount(fixture.Source) == 2 && vm.RetainedReferenceCount(fixture.WeakSelf) == 1 && fixture.WeakRootsOnRelease.empty(), "wrapper retirement must defer FontInfo and cached weak-self release to its actual Layer task");
		group.Update();
		Require(group.Count() == 0 && fixture.WeakRootsOnRelease == std::vector<std::size_t>{1, 1}, "original First task retirement must release both providers before copied weak self");
		fixture.Layers.ReleaseInstance(layer);
	}

	Require(vm.RetainedReferenceCount(fixture.WeakSelf) == 0 && vm.RetainedReferenceCount(fixture.Source) == 0 && fixture.Textures.Count() == 0 && fixture.Backend.Observed.Creates == 0, "actual text task retirement must complete interfaces and weak roots without synthesizing glyph atlas pixels");
}

static void TestConfiguredServicesAndMissingMember() {
	Fixture fixture(false);
	auto& vm = fixture.Vm;
	const auto layer = Construct(vm, fixture.Layers.LayerClass());
	const auto instance = Construct(vm, fixture.Text.TextRenderClass(), {layer});
	RequireFailure([&] {
		static_cast<void>(fixture.Text.SelectFont(instance, sgre::text::TextFontFamily::Normal, Key(24), "face"));
	},
	    "configured provider services");
	RequireFailure([&] {
		fixture.Text.ConfigureFontOwners(fixture.Psb, fixture.Vectors, fixture.Fonts, fixture.Textures);
	},
	    "before any actual task");
	fixture.Text.ReleaseInstance(instance);
	fixture.Layers.ReleaseInstance(layer);

	Fixture configured;
	const auto second_layer = Construct(configured.Vm, configured.Layers.LayerClass());
	const auto second = Construct(configured.Vm, configured.Text.TextRenderClass(), {second_layer});
	const auto failed = configured.Text.SelectFont(second, sgre::text::TextFontFamily::Normal, Key(24), "face");
	Require(failed.CallbackFailed && !failed.CallbackInvoked && !failed.Font && configured.Uploads == 0, "native TextRender without findFont must not create fallback content or textures");
}

static void TestCallbackRetirementIsBoundedAndSafe(bool parent_first) {
	Fixture fixture;
	auto& vm = fixture.Vm;
	const auto layer = Construct(vm, fixture.Layers.LayerClass());
	const auto class_value = vm.CreateClass(fixture.Text.TextRenderClass());
	sgre::script::SquirrelValue instance;
	vm.NewSlot(class_value, std::string("findFont"), vm.CreateNativeClosure("retiring_actual_text_font", [&](auto&, auto) {
		RequireFailure([&] {
			static_cast<void>(fixture.Text.SelectFont(instance, sgre::text::TextFontFamily::Ruby, Key(12), "ruby"));
		},
		    "reenter");
		if (parent_first) {
			fixture.Layers.ReleaseInstance(layer);
		} else {
			fixture.Text.ReleaseInstance(instance);
		}

		return sgre::script::SquirrelNativeResult::ReturnValue(fixture.Source);
	}));
	instance = Construct(vm, class_value, {layer});
	fixture.WeakSelf = vm.CreateWeakReference(instance);
	RequireFailure([&] {
		static_cast<void>(fixture.Text.SelectFont(instance, sgre::text::TextFontFamily::Normal, Key(24), "face"));
	},
	    "retired during");
	if (parent_first) {
		Require(fixture.Text.IsDetached(instance) && vm.RetainedReferenceCount(fixture.Source) == 0 && fixture.Textures.Count() == 0, "active callback's strong task guard must defer close only until selection unwinds");
		fixture.Text.ReleaseInstance(instance);
	} else {
		Require(vm.RetainedReferenceCount(fixture.Source) == 1, "retiring wrapper must preserve its newly created interface until actual task retirement");
		fixture.Layers.MotionTasks(layer).Group(sgre::scene::MotionTaskGroup::First).Update();
		fixture.Layers.ReleaseInstance(layer);
	}

	Require(vm.RetainedReferenceCount(fixture.Source) == 0 && fixture.Textures.Count() == 0 && vm.RetainedReferenceCount(fixture.WeakSelf) == 0, "reentrant retirement must finish all borrowed provider/task lifetimes without returning a dangling FontInfo");
}

int main() {
	try {
		TestActualLayerTaskOwnsNormalRubyInterfaces(false);
		TestActualLayerTaskOwnsNormalRubyInterfaces(true);
		TestConfiguredServicesAndMissingMember();
		TestCallbackRetirementIsBoundedAndSafe(false);
		TestCallbackRetirementIsBoundedAndSafe(true);
		std::cout << "Five actual TextRender task/normal-ruby FontInfo/weak-self retirement groups passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
