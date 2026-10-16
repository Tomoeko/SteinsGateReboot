#include "sgre/script/text_render_bindings.hpp"

#include "font_info_fixture.hpp"
#include "glyph_atlas_owner_fixture.hpp"
#include "sgre/scene/graphics_model_stack.hpp"
#include "sgre/scene/layer_camera_draw.hpp"
#include "sgre/scene/motion_assets.hpp"
#include "sgre/script/layer_bindings.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using sgre::script::SquirrelValue;
using sgre::script::SquirrelVm;

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

	throw std::runtime_error("TextRender font/clear fixture expected failure");
}

static sgre::script::SquirrelVmLimits Limits() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;

	return limits;
}

static std::shared_ptr<sgre::resource::PsbDocument> SpaceDocument() {
	auto document = font_info_test::Document();
	for (auto& [name, value] : std::get<sgre::resource::PsbObject>(document->Root.Data)) {
		if (name == "code") {
			auto& codes = std::get<sgre::resource::PsbObject>(value.Data);
			const auto glyph = codes.front().second;
			codes.emplace_back("\xe3\x80\x80", glyph);

			return document;
		}
	}

	throw std::runtime_error("actual PSB font fixture has no code table");
}

struct SelectionObservation {
	float RequestedSize = 0;
	std::string Face;
	std::int32_t Style = 0;
	sgre::text::TextFormattingCurrent Current;
	sgre::text::TextRenderContextState State;
	bool RootContext = false;
};

struct Fixture {
	SquirrelVm Vm{Limits()};
	sgre::script::ScriptObjectBindings Objects{Vm};
	sgre::script::ScriptPsbBindings Psb{Vm, Objects};
	sgre::script::ScriptVectorFontBindings Vectors{Vm, Objects, [](auto, auto) {
		                                               return []() -> std::optional<sgre::script::ScriptResourceAsset> {
			                                               return {};
		                                               };
	                                               },
	    [](auto, auto) {
	    }};
	SquirrelValue NormalSource = Psb.WrapDocument(SpaceDocument(), font_info_test::Backing());
	SquirrelValue RubySource = Psb.WrapDocument(SpaceDocument(), font_info_test::Backing());
	std::vector<sgre::script::SquirrelHostRoot> Roots;
	std::uint32_t Uploads = 0;
	std::vector<std::array<std::size_t, 2>> RootsOnRelease;
	sgre::scene::TextureCache Textures{[this](auto, const auto&) {
		                                   return sgre::render::SpriteTextureHandle{++Uploads, 1, &Uploads};
	                                   },
	    [this](auto) {
		    RootsOnRelease.push_back({Vm.RetainedReferenceCount(NormalSource), Vm.RetainedReferenceCount(RubySource)});
	    }};
	sgre::scene::BitmapFontAssetCatalog Fonts{Textures};
	sgre::scene::MotionAssetCatalog Motions{Textures};
	sgre::scene::LayerTree Tree{16};
	sgre::script::ScriptLayerBindings Layers{Vm, Objects, Tree};
	text_atlas_test::Backend Backend;
	sgre::script::ScriptTextRenderBindings Text{Vm, Objects, Layers, Tree, Backend};
	SquirrelValue Layer;
	SquirrelValue Instance;
	std::vector<SelectionObservation> Selections;
	std::function<void()> Selecting;
	bool SupplyUnavailable = false;

	explicit Fixture(bool configure = true, bool find_font = true, sgre::scene::LayerCameraDrawScope* actual_scope = nullptr) {
		if (actual_scope) {
			Text.ConfigureFrameOwners(*actual_scope);
		}
		Keep(NormalSource);
		Keep(RubySource);
		Layers.ConfigureMotionOwners(Psb, Motions, Textures);
		if (configure) {
			Text.ConfigureFontOwners(Psb, Vectors, Fonts, Textures);
		}

		const auto derived = Keep(Vm.CreateClass(Text.TextRenderClass()));
		if (find_font) {
			Vm.NewSlot(derived, std::string("findFont"), Vm.CreateNativeClosure("actual_font_reset_selection", [this](auto& active, auto arguments) {
				SelectionObservation observation;
				observation.RequestedSize = std::get<float>(arguments[1]);
				observation.Face = sgre::script::StringText(arguments[2]);
				observation.Style = std::get<std::int32_t>(arguments[3]);
				observation.Current = Text.Context(Instance).Formatting().Current;
				observation.State = Text.Context(Instance).State();
				observation.RootContext = active.CurrentContext() == active.RootContext();
				Selections.push_back(std::move(observation));
				if (Selecting) {
					Selecting();
				}

				const auto ruby = sgre::script::StringText(arguments[2]).starts_with("ruby");

				return sgre::script::SquirrelNativeResult::ReturnValue(SupplyUnavailable ? SquirrelValue(std::int32_t{17}) : ruby ? RubySource
				                                                                                                                  : NormalSource);
			}));
		}

		Layer = Construct(Layers.LayerClass());
		Instance = Construct(derived, {Layer});
	}

	~Fixture() {
		Vm.Close();
	}

	SquirrelValue Keep(const SquirrelValue& value) {
		Roots.push_back(Vm.Retain(value));

		return value;
	}

	SquirrelValue Construct(const SquirrelValue& class_value, std::vector<SquirrelValue> arguments = {}) {
		arguments.insert(arguments.begin(), Vm.RootTable());

		return Keep(Vm.Invoke(class_value, arguments).Value);
	}

	SquirrelValue Properties(std::initializer_list<std::pair<std::string_view, SquirrelValue>> values) {
		const auto table = Keep(Vm.CreateTable());
		for (const auto& [key, value] : values) {
			Vm.NewSlot(table, std::string(key), value);
		}

		return table;
	}

	SquirrelValue Call(std::string_view name, std::vector<SquirrelValue> arguments = {}) {
		arguments.insert(arguments.begin(), Instance);

		return Vm.Invoke(Vm.GetSlot(Instance, std::string(name)), arguments).Value;
	}

	void Close() {
		Text.ReleaseInstance(Instance);
		Layers.MotionTasks(Layer).Group(sgre::scene::MotionTaskGroup::First).Update();
		Layers.ReleaseInstance(Layer);
	}
};

static void TestActualResetAndSequentialFontProperties() {
	Fixture fixture;
	const auto& context = fixture.Text.Context(fixture.Instance);
	fixture.Call("setDefault", {fixture.Properties({{"fontsize", std::int32_t{24}}, {"pitch", 3.0f}, {"color", std::int32_t{0x11223344}}})});
	Require(fixture.Selections.empty() && context.Formatting().Current.FontSize == -1, "setDefault must not invoke providers or reset current font keys");
	fixture.Call("setFontScale", {2.0f});
	fixture.Call("resetFont", {std::int32_t{99}});
	Require(fixture.Selections.size() == 2 && fixture.Selections[0].RequestedSize == 48 && fixture.Selections[1].RequestedSize == 16, "reset must select actual scaled normal/ruby providers in order");
	Require(context.Formatting().Current.FontBaseWidth == 40 && context.Formatting().Current.Pitch == 0 && context.Formatting().Current.Color == 0x11223344, "real U3000 bitmap metrics supply base width while resetFont leaves geometry unreset");
	const auto properties = fixture.Properties({{"face", std::string("alternate")}, {"rubyface", std::string("rubyalternate")}, {"bold", std::int32_t{1}}, {"fontsize", 18.5f}, {"color", std::int32_t{0x55667788}}, {"rubysize", 9.5f}, {"rubyoffset", 7.25f}, {"shadow", false}, {"shadowcolor", std::int32_t{-1}}, {"shadowdiff", -2.75f}, {"edge", true}, {"edgecolor", std::int32_t{123}}, {"edgesize", 5.75f}});
	const auto before = fixture.Selections.size();
	fixture.Call("setFont", {properties, std::string("ignored")});
	Require(fixture.Selections.size() == before + 5, "changed face, rubyface, bold, size and ruby size must each select at their native property position");
	const auto& face = fixture.Selections[before];
	const auto& ruby = fixture.Selections[before + 1];
	const auto& bold = fixture.Selections[before + 2];
	const auto& size = fixture.Selections[before + 3];
	const auto& ruby_size = fixture.Selections[before + 4];
	Require(face.Face == "alternate" && face.RequestedSize == 48 && !face.Current.Bold && face.Current.Color == 0x11223344, "face selection must observe its current face write before later font/appearance properties");
	Require(ruby.Face == "rubyalternate" && ruby.Current.FontSize == 24 && bold.Style == 1 && bold.Current.FontSize == 24 && size.RequestedSize == 37 && size.Current.Color == 0x11223344, "rubyface/bold/float fontsize must preserve separate live ordered selections");
	Require(ruby_size.RequestedSize == 19 && ruby_size.Current.Color == 0x55667788 && ruby_size.Style == 0, "color is stored before ruby size selection and ruby uses native regular style");
	const auto& current = context.Formatting().Current;
	Require(current.FontSize == 18.5f && current.FontBaseWidth == 5.0f * (37.0f / 6.0f) && current.RubySize == 9.5f && current.RubyOffset == 7.25f && !current.Shadow && current.ShadowColor == 0xffffffff && current.ShadowOffset == -2 && current.Edge && current.EdgeColor == 123 && current.EdgeSize == 5, "setFont must route real provider sizing and exact typed current appearance writes");
	Require(context.Formatting().Defaults.FontSize == 24 && context.Formatting().Defaults.Color == 0x11223344, "current font writes must leave stored defaults intact");
	fixture.Call("setFont", {properties});
	fixture.Call("setFont");
	Require(fixture.Selections.size() == before + 5 && fixture.Backend.Observed.Creates == 0, "equal keys and missing fixed property operand must not fabricate font callbacks or atlas work");
	const auto negative = fixture.Properties({{"fontsize", -2.0f}});
	fixture.Call("setFont", {negative});
	const auto request = sgre::text::PrepareTextFontSelectionRequest(context.Formatting(), sgre::text::TextFontFamily::Normal, false);
	const auto selected = fixture.Text.SelectFont(fixture.Instance, request.Family, request.Key, "alternate");
	Require(selected.Font != nullptr, "negative font size still selects its genuine retained bitmap interface");
	selected.Font->SetSize(18);
	fixture.Call("setFont", {negative});
	Require(selected.Font->BitmapScale().Width == -4.0f / 6.0f, "equal negative font size must still reselect and restore actual provider size rather than take the positive-key no-op branch");
	fixture.Close();
}

static void TestClearFontRetiresProvidersWithoutChangingKeys() {
	Fixture fixture;
	fixture.Call("resetFont");
	const auto calls = fixture.Selections.size();
	const auto size = fixture.Text.Context(fixture.Instance).Formatting().Current.FontSize;
	Require(fixture.Vm.RetainedReferenceCount(fixture.NormalSource) == 2 && fixture.Vm.RetainedReferenceCount(fixture.RubySource) == 2 && fixture.Textures.Count() == 4, "actual FontInfo owns each PSB source and its two real atlas sources");
	fixture.Call("clearFont", {std::string("ignored")});
	Require(fixture.RootsOnRelease == std::vector<std::array<std::size_t, 2>>{{2, 2}, {2, 2}, {1, 2}, {1, 2}}, "clearFont must release normal interfaces before ruby and each texture before its source root");
	Require(fixture.Textures.Count() == 0 && fixture.Text.Context(fixture.Instance).Formatting().Current.FontSize == size, "font-cache clear must not reset actual formatting keys");
	fixture.Call("resetFont");
	Require(fixture.Selections.size() == calls && fixture.Textures.Count() == 0, "matching reset keys after clearFont must not invent reacquisition of cleared native current pointers");
	fixture.Call("setFont", {fixture.Properties({{"fontsize", 18.0f}})});
	Require(fixture.Selections.size() == calls + 1 && fixture.Text.Context(fixture.Instance).Formatting().Current.FontBaseWidth == 15 && fixture.Textures.Count() == 2, "a genuine changed font size must reacquire normal source and measure the actual selected provider");
	fixture.Close();
}

static void TestEmptyClearOrdersCallbacksAndRetainsIndependentState() {
	Fixture fixture;
	fixture.Call("resetFont");
	fixture.Call("setFont", {fixture.Properties({{"fontsize", 18.0f}, {"color", std::int32_t{77}}})});
	fixture.Call("setCoord", {11.0f, 13.0f});
	fixture.Call("setClipMode", {std::int32_t{2}});
	fixture.Call("setClipOffset", {5.0f, 7.0f});
	fixture.Call("setOffsetPos", {17.0f, 19.0f});
	fixture.Call("setShowCount", {std::int32_t{23}});
	fixture.Call("setOption", {fixture.Properties({{"following", std::string("kept")}, {"vertical", true}})});
	fixture.Call("setDefault", {fixture.Properties({{"face", std::string("new-normal")}, {"fontsize", std::int32_t{21}}, {"rubysize", 11.0f}, {"pitch", 3.0f}, {"linespacing", 8.0f}, {"align", std::int32_t{2}}})});
	const auto calls = fixture.Selections.size();
	fixture.Call("clear");
	Require(fixture.Selections.size() == calls + 2 && fixture.Selections[calls].State.ShownElementCount == 23 && fixture.Selections[calls].State.OffsetPosition.X == 17 && fixture.Selections[calls].State.ClippingBounds.Left == 16, "clear must reset fonts before shown/manual-offset/clip state is cleared");
	const auto& context = fixture.Text.Context(fixture.Instance);
	const auto& state = context.State();
	Require(state.ShownElementCount == 0 && state.OffsetPosition.X == 0 && state.ScrollOffset.Y == 0 && state.ClippingBounds.Left == 1 && state.ClippingBounds.Right == -1, "empty clear resets actual facade counters/offsets and sentinel without recomputing current clip mode");
	Require(state.Coordinate.X == 11 && state.Coordinate.Y == 13 && state.ClipOffset.X == 5 && state.ClipOffset.Y == 7 && state.ClipMode == 2 && state.Vertical, "clear preserves independent coordinate, clip offset, mode and vertical state");
	Require(context.Formatting().Current.FontBaseWidth == 21 && context.Formatting().Current.Pitch == 3 && context.Formatting().Current.LineSpacing == 8 && context.Formatting().Current.Alignment == 2 && context.Options().Following == std::vector<std::uint16_t>{'k', 'e', 'p', 't'}, "clear resets geometry after font selection while retaining task options");
	Require(context.FaceNames().Count() == 3 && fixture.RootsOnRelease.empty() && fixture.Textures.Count() == 4 && fixture.Backend.Observed.Creates == 0, "actual empty-content clear retains its font cache, interned face names and lazy atlas");
	fixture.Call("clear");
	Require(fixture.Selections.size() == calls + 2, "a repeated empty clear must not force changed font keys");
	fixture.Close();
}

static void TestFixedBootstrapCallbackAndActualNoProviderBranches() {
	Fixture fixture;
	const auto child = fixture.Vm.CreateContext();
	const auto child_root = fixture.Keep(fixture.Vm.CreateTable());
	fixture.Vm.SetRootTableInContext(child, child_root);
	const std::array<SquirrelValue, 1> arguments{fixture.Instance};
	static_cast<void>(fixture.Vm.InvokeInContext(child, fixture.Vm.GetSlot(fixture.Instance, std::string("resetFont")), arguments));
	Require(fixture.Selections.size() == 2 && fixture.Selections[0].RootContext && fixture.Selections[1].RootContext, "real findFont member lookup/call must use fixed bootstrap VM even from a child-context native method");
	Require(fixture.Vm.RootTableInContext(child) == child_root && fixture.Vm.ContextState(child) == sgre::script::SquirrelContextState::Idle, "fixed bootstrap font callbacks must restore the actual child caller");
	static_cast<void>(fixture.Vm.CollectGarbage());
	fixture.Call("clearFont");
	fixture.SupplyUnavailable = true;
	fixture.Call("setFont", {fixture.Properties({{"fontsize", 27.0f}})});
	Require(fixture.Text.Context(fixture.Instance).Formatting().Current.FontBaseWidth == 27 && fixture.Textures.Count() == 0, "a genuine unavailable selected interface measures zero and uses the native unscaled size fallback");
	fixture.Close();

	Fixture missing(false);
	missing.Call("clearFont");
	missing.Call("setFont", {missing.Properties({{"color", std::int32_t{45}}})});
	RequireFailure([&] {
		missing.Call("resetFont");
	},
	    "configured provider services");
	Require(missing.Text.Context(missing.Instance).Formatting().Current.FontSize == 24 && missing.Textures.Count() == 0, "missing selection service preserves prior native key writes without manufacturing providers");
	missing.Close();

	Fixture no_member(true, false);
	no_member.Call("resetFont");
	Require(no_member.Text.Context(no_member.Instance).Formatting().Current.FontBaseWidth == 24 && no_member.Textures.Count() == 0, "failed real findFont lookup must leave null current provider and native zero-measure fallback");
	no_member.Close();
}

static void TestReentryAndRetirementAcrossActualFindFont() {
	Fixture fixture;
	std::size_t rejected_clear = 0;
	fixture.Selecting = [&] {
		RequireFailure([&] {
			fixture.Call("clearFont");
		},
		    "cannot mutate");
		RequireFailure([&] {
			static_cast<void>(fixture.Text.SelectFont(fixture.Instance, sgre::text::TextFontFamily::Ruby, {}, "ruby"));
		},
		    "reenter");
		++rejected_clear;
	};
	fixture.Call("resetFont");
	Require(rejected_clear == 2 && fixture.Textures.Count() == 4, "actual normal/ruby findFont callbacks must reject freeing/reentering their active provider owner");
	fixture.Selecting = {};
	fixture.Close();

	Fixture retired;
	retired.Selecting = [&] {
		retired.Layers.ReleaseInstance(retired.Layer);
	};
	RequireFailure([&] {
		retired.Call("resetFont");
	},
	    "retired during");
	Require(retired.Text.IsDetached(retired.Instance) && retired.Textures.Count() == 0 && retired.Vm.RetainedReferenceCount(retired.NormalSource) == 1, "parent retirement during callback must finish deferred FontInfo close after actual property unwind");
	retired.Call("clear");
	retired.Call("resetFont");
	retired.Call("clearFont");
	retired.Call("setFont", {std::string("ignored after detach")});
	retired.Text.ReleaseInstance(retired.Instance);
}

static void TestActualRenderSizeClearAndFixedArgumentCells() {
	Fixture fixture;
	fixture.Call("setShowCount", {std::int32_t{23}});
	fixture.Call("setOffsetPos", {17.0f, 19.0f});
	fixture.Call("setRenderSize", {std::int32_t{320}, 180.5f, std::string("ignored")});
	const auto& context = fixture.Text.Context(fixture.Instance);
	Require(fixture.Selections.size() == 2 && fixture.Selections[0].State.RenderSize.X == 320 && fixture.Selections[0].State.RenderSize.Y == 180.5f && fixture.Selections[0].State.ShownElementCount == 23 && fixture.Selections[0].State.OffsetPosition.X == 17, "actual resize binding must store both fixed numeric arguments before native clear callbacks");
	Require(context.State().ShownElementCount == 0 && context.State().OffsetPosition.X == 0 && std::get<float>(fixture.Call("getWidth")) == 0, "setRenderSize must clear facade state while retaining the preceding fixed-view extent");
	fixture.Call("done");
	Require(std::get<float>(fixture.Call("getWidth")) == 320 && std::get<float>(fixture.Call("getHeight")) == 180.5f, "true done must publish requested render extent through the actual fixed-view width/height getters");
	fixture.Call("setRenderSize", {19.75f});
	Require(context.State().RenderSize.X == 19.75f && context.State().RenderSize.Y == 0 && context.State().Size.X == 320, "one supplied operand leaves captured userdata in the fixed height cell and must not duplicate width");
	const std::array<SquirrelValue, 2> history{7.25f, 3.5f};
	const auto seed = fixture.Keep(fixture.Vm.CreateNativeClosureWithCaptures(
	    "retain_resize_physical_cells", [](auto&, auto) {
		    return sgre::script::SquirrelNativeResult::NoValues();
	    },
	    history));
	const std::array<SquirrelValue, 1> receiver{std::int32_t{17}};
	static_cast<void>(fixture.Vm.Invoke(seed, receiver));
	fixture.Call("setRenderSize");
	Require(context.State().RenderSize.X == 0 && context.State().RenderSize.Y == 7.25f, "missing logical operands must preserve the actual allocated height cell rather than fabricate two zero defaults");
	fixture.Call("setRenderSize", {true, std::string("not numeric")});
	Require(context.State().RenderSize.X == 1 && context.State().RenderSize.Y == 0 && fixture.Backend.Observed.Creates == 0, "actual float bridge accepts bool but performs no string conversion or atlas allocation");
	fixture.Close();

	Fixture retired;
	retired.Selecting = [&] {
		retired.Layers.ReleaseInstance(retired.Layer);
	};
	RequireFailure([&] {
		retired.Call("setRenderSize", {31.0f, 37.0f});
	},
	    "retired during");
	Require(retired.Text.IsDetached(retired.Instance) && retired.Textures.Count() == 0, "resize callback retirement must finish real deferred provider cleanup before releasing retained task operands");
	retired.Call("setRenderSize", {41.0f, 43.0f});
	retired.Text.ReleaseInstance(retired.Instance);
}

static void TestResetStyleRetainsActualFontAndContentState() {
	Fixture fixture;
	fixture.Call("resetFont");
	const auto font_base_width = fixture.Text.Context(fixture.Instance).Formatting().Current.FontBaseWidth;
	fixture.Call("setFontScale", {2.0f});
	fixture.Call("setOption", {fixture.Properties({{"word_break", false}})});
	fixture.Call("setShowCount", {std::int32_t{23}});
	fixture.Call("setOffsetPos", {17.0f, 19.0f});
	fixture.Call("setDefault", {fixture.Properties({{"fontsize", 48.0f}, {"linesize", 30.0f}, {"linespacing", 7.0f}, {"pitch", 3.0f}, {"align", std::int32_t{2}}, {"valign", std::int32_t{1}}, {"bold", true}})});
	const auto selections = fixture.Selections.size();
	fixture.Call("resetStyle", {std::string("ignored"), true});
	const auto& context = fixture.Text.Context(fixture.Instance);
	const auto& current = context.Formatting().Current;
	Require(current.Pitch == 3 && current.Alignment == 2 && current.VerticalAlignment == 1 && current.LineSize == 60 && current.LineSpacing == 14 && !current.WordBreak, "resetStyle must apply native geometry defaults using the existing current font size and live scale");
	Require(current.FontSize == 24 && !current.Bold && current.FontBaseWidth == font_base_width && fixture.Selections.size() == selections && fixture.Textures.Count() == 4, "resetStyle must preserve real current font keys, unit measurement and retained provider owners");
	Require(context.State().ShownElementCount == 23 && context.State().OffsetPosition.X == 17 && context.State().OffsetPosition.Y == 19 && fixture.Backend.Observed.Creates == 0, "resetStyle must preserve content controls and lazy atlas state");
	fixture.Close();

	Fixture detached;
	detached.Layers.ReleaseInstance(detached.Layer);
	detached.Call("resetStyle", {std::int32_t{37}});
	detached.Text.ReleaseInstance(detached.Instance);
}

static void TestActualRenderOwnsBitmapRunsAndSurvivesFontCacheRetirement() {
	Fixture fixture;
	fixture.Call("setRenderSize", {100.0f, 0.0f});
	const auto selections = fixture.Selections.size();
	const auto result = fixture.Call("render", {std::string("%CA\nB"), std::int32_t{0}, std::int32_t{0}, std::int32_t{0}, false});
	const auto& context = fixture.Text.Context(fixture.Instance);
	Require(std::get<bool>(result) && fixture.Selections.size() == selections, "actual render reselects cached normal/ruby without fabricating source callbacks");
	Require(context.Content().Lines().size() == 1 && context.Content().CurrentLine().size() == 1, "real LF finalizes first line while preserving actual second current line");
	const auto& first = context.Content().Lines()[0].Elements[0];
	Require(first.Text == u"A" && first.Run.Glyphs.size() == 1 && first.Run.Glyphs[0].GlyphIndex == u'A' && first.Geometry.Metrics.Width == 20 && first.Geometry.Metrics.Height == 24, "actual bitmap provider creates native code-unit run and independent nominal line height");
	fixture.Call("done");
	Require(context.State().ShownElementCount == 2, "native task done copies the actual rendered-element counter, not the preceding empty-owner zero");
	Require(std::get<std::int32_t>(fixture.Call("getRenderLines")) == 2 && !std::get<bool>(fixture.Call("getRenderOver")), "native getters read actual retained history and overflow flag");
	const auto bounds = fixture.Keep(fixture.Call("getRenderBounds"));
	Require(fixture.Vm.Type(bounds) == sgre::script::SquirrelValueType::Table && std::get<float>(fixture.Vm.GetSlot(bounds, std::string("height"))) == 54, "native ObjectInfo bounds returns a real independent table from owned geometry");
	fixture.Call("clearFont");
	static_cast<void>(fixture.Vm.CollectGarbage());
	Require(context.Content().Lines()[0].Elements[0].Run.Glyphs[0].GlyphIndex == u'A' && fixture.Textures.Count() == 0, "font-cache retirement releases real providers while shaped text history remains independently owned");
	fixture.Call("setFont", {fixture.Properties({{"fontsize", 18.0f}})});
	std::size_t observed_history = 0;
	fixture.Selecting = [&] {
		Require(context.Content().CurrentLine().empty() && context.Content().Bounds().Bottom == 0, "native clear resets current-line/bounds before actual font callback");
		Require(context.Content().Lines().size() == 2 && !context.Content().ProcessedText().empty(), "native clear retains old deep history/processed text through font reset callback");
		++observed_history;
	};
	fixture.Call("clear");
	fixture.Selecting = {};
	Require(observed_history != 0 && context.Content().Lines().empty() && context.Content().CurrentLine().empty() && context.Content().ProcessedText().empty(), "native clear retires populated history only after ordered actual font reset callbacks");
	fixture.Close();
}

static void TestRenderReentrantStringAndActualParentRetirement() {
	Fixture fixture;
	fixture.Call("clear");
	const auto operand = fixture.Keep(fixture.Vm.CreateUserData(std::array<std::uint8_t, 1>{0}));
	const auto delegate = fixture.Keep(fixture.Vm.CreateTable());
	fixture.Vm.SetUserDataDelegate(operand, delegate);
	fixture.Vm.NewSlot(delegate, std::string("_tostring"), fixture.Vm.CreateNativeClosure("actual_text_operand_retirement", [&](auto& vm, auto) {
		fixture.Layers.ReleaseInstance(fixture.Layer);
		return sgre::script::SquirrelNativeResult::ReturnValue(vm.CreateString("A"));
	}));
	Require(!std::get<bool>(fixture.Call("render", {operand, std::int32_t{0}, std::int32_t{0}, std::int32_t{0}, false})), "native detached render still converts input before false result, without touching retired font/content owners");
	Require(fixture.Text.IsDetached(fixture.Instance), "real ToString parent disposal retires the actual containing First task");
	fixture.Text.ReleaseInstance(fixture.Instance);
}

static void TestActualCameraScopeForConfiguredEmptyTask() {
	sgre::scene::GraphicsModelStack models(8);
	models.Reset();
	sgre::scene::LayerCameraFrameState state;
	state.Viewport = {100, 80};
	state.RawScissor = {0, 0, 100, 80};
	state.RoundedScissor = state.RawScissor;
	state.AppliedRoundedScissor = state.RawScissor;
	std::size_t submissions = 0;
	std::size_t admissions = 0;
	sgre::scene::LayerCameraDrawScope scope(models, state, [&](const auto&) {
		++submissions;
	},
	    64, [&] {
		    ++admissions;
	    });
	Fixture fixture(true, true, &scope);
	fixture.Call("setScale", {2.0f, 3.0f});
	fixture.Call("setVisible", {true});
	fixture.Layers.MotionTasks(fixture.Layer).Group(sgre::scene::MotionTaskGroup::First).Draw(0);
	const auto observed = "models=" + std::to_string(models.Depth()) + " scope=" + std::to_string(scope.Depth()) + " drawing=" + std::to_string(scope.IsDrawing()) + " submissions=" + std::to_string(submissions) + " admissions=" + std::to_string(admissions);
	// Depth observes the borrowed real model owner. A disabled Scale clip makes no scissor submission.
	Require(models.Depth() == 1 && scope.Depth() == 1 && !scope.IsDrawing() && admissions == 1 && submissions == 0, "actual configured empty Text camera must enter then restore its true model owner and disabled clip: " + observed);
	Require(models.Current().Coefficients == sgre::scene::MotionScissorMatrix{}.Coefficients && state.RawScissor.MinimumX == 0 && state.RawScissor.MaximumX == 100, "actual empty Text camera must restore original model values and preserve the disabled-clip scissor");
	Require(fixture.Text.Context(fixture.Instance).Content().ElementCount() == 0 && fixture.Backend.Observed.Creates == 0, "empty native glyph traversal creates no atlas or replacement drawing resources");
	fixture.Close();
}

int main() {
	try {
		TestActualResetAndSequentialFontProperties();
		TestClearFontRetiresProvidersWithoutChangingKeys();
		TestEmptyClearOrdersCallbacksAndRetainsIndependentState();
		TestFixedBootstrapCallbackAndActualNoProviderBranches();
		TestReentryAndRetirementAcrossActualFindFont();
		TestActualRenderSizeClearAndFixedArgumentCells();
		TestResetStyleRetainsActualFontAndContentState();
		TestActualRenderOwnsBitmapRunsAndSurvivesFontCacheRetirement();
		TestRenderReentrantStringAndActualParentRetirement();
		TestActualCameraScopeForConfiguredEmptyTask();
		std::cout << "Five actual TextRender font/reset/clear provider and retirement groups passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
