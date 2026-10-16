#include "sgre/script/text_render_bindings.hpp"

#include "glyph_atlas_owner_fixture.hpp"
#include "sgre/scene/layer_scale_view.hpp"
#include "sgre/scene/motion_assets.hpp"
#include "sgre/script/layer_bindings.hpp"
#include "sgre/script/psb_bindings.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using sgre::script::SquirrelValue;
using sgre::script::SquirrelVm;

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

static SquirrelValue Construct(SquirrelVm& vm, const SquirrelValue& class_value, std::vector<SquirrelValue> arguments = {}) {
	arguments.insert(arguments.begin(), vm.RootTable());
	return vm.Invoke(class_value, arguments).Value;
}

static SquirrelValue Call(SquirrelVm& vm, const SquirrelValue& receiver, std::string_view name, std::vector<SquirrelValue> arguments = {}) {
	arguments.insert(arguments.begin(), receiver);
	return vm.Invoke(vm.GetSlot(receiver, std::string(name)), arguments).Value;
}

struct Fixture {
	SquirrelVm Vm;
	sgre::script::ScriptObjectBindings Objects{Vm};
	sgre::script::ScriptPsbBindings Psb{Vm, Objects};
	sgre::scene::TextureCache Textures{[](auto, const auto&) {
		                                   return sgre::render::SpriteTextureHandle{};
	                                   },
	    [](auto) {
	    }};
	sgre::scene::MotionAssetCatalog Motions{Textures};
	sgre::scene::LayerTree Tree{8};
	sgre::script::ScriptLayerBindings Layers{Vm, Objects, Tree};
	text_atlas_test::Backend Backend;
	sgre::script::ScriptTextRenderBindings Text{Vm, Objects, Layers, Tree, Backend};

	Fixture() {
		Layers.ConfigureMotionOwners(Psb, Motions, Textures);
	}
	~Fixture() {
		Vm.Close();
	}
};

static void TestActualOptionStorageAndNativeConversions() {
	Fixture fixture;
	auto& vm = fixture.Vm;
	const auto layer = Construct(vm, fixture.Layers.LayerClass());
	const auto text = Construct(vm, fixture.Text.TextRenderClass(), {layer});
	const auto option = vm.CreateTable();
	vm.NewSlot(option, std::string("following"), std::string("A\0ignored", 9));
	vm.NewSlot(option, std::string("leading"), std::string("\xE3\x81\x82"));
	vm.NewSlot(option, std::string("begin"), std::string("\xF0\x9F\x98\x80"));
	vm.NewSlot(option, std::string("end"), std::int32_t{99});
	vm.NewSlot(option, std::string("vertical"), true);
	vm.NewSlot(option, std::string("kinsoku_max"), -2.75f);
	vm.NewSlot(option, std::string("word_break"), 0.75f);
	vm.NewSlot(option, std::string("ignore_color"), std::int32_t{2});
	vm.NewSlot(option, std::string("ignore_over"), true);
	vm.NewSlot(option, std::string("ignore_overy"), false);
	vm.NewSlot(option, std::string("ignore_overx"), -1.75f);
	vm.NewSlot(option, std::string("width_time_scale"), true);
	vm.NewSlot(option, std::string("ignore_style"), vm.CreateTable());
	vm.NewSlot(option, std::string("texture_size"), std::int32_t{-17});
	static_cast<void>(Call(vm, text, "setOption", {option, std::int32_t{999}}));
	const auto& context = fixture.Text.Context(text);
	const auto& options = context.Options();
	Require(options.Following == std::vector<std::uint16_t>{'A'} && options.Leading == std::vector<std::uint16_t>{0x3042} && options.Begin == std::vector<std::uint16_t>{0xd83d, 0xde00} && options.End.empty(), "actual option strings preserve native first-NUL/UTF16 conversion and wrong-type empty fallback");
	Require(context.State().Vertical && context.Formatting().Vertical && std::get<bool>(Call(vm, text, "getVertical")), "setOption must write the same actual vertical state read by direct accessors");
	Require(options.MaximumProhibitionCount == -2 && !context.Formatting().Current.WordBreak && !context.Formatting().Defaults.WordBreak && options.IgnoreColor, "native integer truncation and shared current/default word-break stores must propagate");
	Require(!options.IgnoreVerticalOverflow && options.IgnoreHorizontalOverflow && options.WidthTimeScale && !options.IgnoreStyle && options.RequestedTextureSize == -17, "later vertical-overflow alias and final signed texture request preserve exact ordering and converter fallback");
	Require(fixture.Backend.Observed.Creates == 0 && fixture.Backend.Observed.Uploads == 0, "option texture_size stores the actual request without inventing a resize or rendered text");
	static_cast<void>(Call(vm, text, "setVertical", {false}));
	Require(!context.State().Vertical && !context.Formatting().Vertical, "the existing direct setter must update the same retained formatting owner");
	static_cast<void>(Call(vm, text, "setFontScale", {2.5f}));
	Require(context.Formatting().FontScale == 2.5f, "the actual formatter and facade retain one font scale value");
	static_cast<void>(Call(vm, text, "setOption", {vm.CreateTable()}));
	Require(options.RequestedTextureSize == -17 && options.Following == std::vector<std::uint16_t>{'A'}, "absent keys preserve previously supplied options");
	static_cast<void>(Call(vm, text, "setOption"));
	Require(options.RequestedTextureSize == -17, "missing fixed argument sees real captured userdata and performs no fabricated default writes");
}

static void TestActualDelegateAndChildContextReceiver() {
	Fixture fixture;
	auto& vm = fixture.Vm;
	const auto layer = Construct(vm, fixture.Layers.LayerClass());
	const auto text = Construct(vm, fixture.Text.TextRenderClass(), {layer});
	const auto delegate = vm.CreateTable();
	vm.NewSlot(delegate, std::string("vertical"), true);
	vm.NewSlot(delegate, std::string("texture_size"), 17.75f);
	const auto option = vm.CreateUserData({});
	vm.SetUserDataDelegate(option, delegate);
	const auto child = vm.CreateContext();
	const auto child_root = vm.CreateTable();
	vm.SetRootTableInContext(child, child_root);
	const std::array<SquirrelValue, 2> arguments{text, option};
	static_cast<void>(vm.InvokeInContext(child, vm.GetSlot(text, std::string("setOption")), arguments));
	Require(fixture.Text.Context(text).Options().RequestedTextureSize == 17 && fixture.Text.Context(text).State().Vertical, "raw presence must reach the supplied real userdata delegate rather than FindRawSlot own-only storage");
	Require(vm.RootTableInContext(child) == child_root && vm.ContextState(child) == sgre::script::SquirrelContextState::Idle, "the fixed base-VM property route must restore its actual child native caller");
	fixture.Layers.ReleaseInstance(layer);
	static_cast<void>(Call(vm, text, "setOption", {std::string("detached ignored input")}));
	fixture.Text.ReleaseInstance(text);
}

static void TestActualDefaultStorageAndFaceOwnership() {
	Fixture fixture;
	auto& vm = fixture.Vm;
	const auto layer = Construct(vm, fixture.Layers.LayerClass());
	const auto text = Construct(vm, fixture.Text.TextRenderClass(), {layer});
	const auto& context = fixture.Text.Context(text);
	Require(context.FaceNames().Count() == 2 && context.FaceNames().Utf8Name(0) == "normal" && context.FaceNames().Utf8Name(1) == "ruby", "native task owns its initial normal/ruby face identities");
	const auto defaults = vm.CreateTable();
	vm.NewSlot(defaults, std::string("face"), std::string("face\0ignored", 12));
	vm.NewSlot(defaults, std::string("rubyface"), std::string("\xF0\x9F\x98\x80"));
	vm.NewSlot(defaults, std::string("fontsize"), 25.75f);
	vm.NewSlot(defaults, std::string("bold"), std::int32_t{1});
	vm.NewSlot(defaults, std::string("color"), std::int32_t{0x12345678});
	vm.NewSlot(defaults, std::string("align"), std::int32_t{2});
	const auto child = vm.CreateContext();
	const auto child_root = vm.CreateTable();
	vm.SetRootTableInContext(child, child_root);
	const std::array<SquirrelValue, 3> arguments{text, defaults, std::int32_t{99}};
	static_cast<void>(vm.InvokeInContext(child, vm.GetSlot(text, std::string("setDefault")), arguments));
	const auto& stored = context.Formatting().Defaults;
	Require(stored.NormalFaceIndex == 2 && stored.RubyFaceIndex == 3 && context.FaceNames().Utf8Name(2) == "face" && context.FaceNames().CodeUnits(3).size() == 2 && context.FaceNames().CodeUnits(3)[0] == 0xd83d && context.FaceNames().CodeUnits(3)[1] == 0xde00, "real default face writes retain first-NUL strings and intern supplementary code units in the task owner");
	Require(stored.FontSize == 25 && stored.BigFontSize == 50 && stored.SmallFontSize == 12.5f && stored.RubySize == 25.0f / 3.0f && stored.LineSize == 25.75f && stored.Bold && stored.Color == 0x12345678 && stored.Alignment == 2, "native default property conversions retain integer fontsize and the later float linesize reread");
	Require(context.Formatting().Current.FontSize == -1 && context.Formatting().Current.Color == 0 && fixture.Backend.Observed.Creates == 0, "setDefault leaves current font/layout state and atlas allocation to their actual later operations");
	Require(vm.RootTableInContext(child) == child_root && vm.ContextState(child) == sgre::script::SquirrelContextState::Idle, "default property reads restore the real child caller after fixed-root receiving");
	static_cast<void>(Call(vm, text, "setDefault", {defaults}));
	Require(context.FaceNames().Count() == 4, "repeated actual faces preserve intern identities without duplicate owner entries");
	const auto broken = vm.CreateTable();
	vm.NewSlot(broken, std::string("face"), std::string("retained-before-failure"));
	vm.NewSlot(broken, std::string("rubyface"), std::string("\xFF"));
	vm.NewSlot(broken, std::string("fontsize"), std::int32_t{10});
	bool failed = false;
	try {
		static_cast<void>(Call(vm, text, "setDefault", {broken}));
	} catch (const std::runtime_error&) {
		failed = true;
	}
	Require(failed && stored.NormalFaceIndex == 4 && stored.RubyFaceIndex == 3 && stored.FontSize == 25 && context.FaceNames().Utf8Name(4) == "retained-before-failure", "explicit encoding failure preserves the earlier native face write and skips later properties");
	static_cast<void>(Call(vm, text, "setDefault"));
	Require(stored.NormalFaceIndex == 4 && stored.FontSize == 25, "absent fixed argument does not fabricate default reset writes");
	fixture.Layers.ReleaseInstance(layer);
	static_cast<void>(Call(vm, text, "setDefault", {broken}));
	fixture.Text.ReleaseInstance(text);
}

static void TestPartialWritesAndRichApplicationBoundary() {
	Fixture fixture;
	auto& vm = fixture.Vm;
	const auto layer = Construct(vm, fixture.Layers.LayerClass());
	const auto text = Construct(vm, fixture.Text.TextRenderClass(), {layer});
	const auto option = vm.CreateTable();
	vm.NewSlot(option, std::string("following"), std::string("first"));
	vm.NewSlot(option, std::string("leading"), std::string("\xFF"));
	vm.NewSlot(option, std::string("texture_size"), std::int32_t{5});
	bool failed = false;
	try {
		static_cast<void>(Call(vm, text, "setOption", {option}));
	} catch (const std::runtime_error&) {
		failed = true;
	}
	const auto& context = fixture.Text.Context(text);
	Require(failed && context.Options().Following == std::vector<std::uint16_t>{'f', 'i', 'r', 's', 't'} && context.Options().RequestedTextureSize == 1024, "an explicit encoding failure retains earlier native writes without applying later fields");
	Require(vm.HasSlot(text, std::string("render")) && !vm.HasSlot(text, std::string("setText")) && context.Content().ProcessedText().empty() && fixture.Backend.Observed.Creates == 0, "setOption preserves the real empty content owner without preparing an atlas or invoking render");
}

int main() {
	try {
		TestActualOptionStorageAndNativeConversions();
		TestActualDelegateAndChildContextReceiver();
		TestActualDefaultStorageAndFaceOwnership();
		TestPartialWritesAndRichApplicationBoundary();
		std::cout << "TextRender actual setOption checks passed\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
	return 0;
}
