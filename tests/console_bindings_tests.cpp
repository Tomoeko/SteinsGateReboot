#include "sgre/script/console_bindings.hpp"

#include "sgre/scene/bitmap_font_assets.hpp"
#include "sgre/script/font_bindings.hpp"
#include "sgre/script/indicator_bindings.hpp"
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


static std::shared_ptr<sgre::resource::BitmapFont> CoreFont() {
	auto font = std::make_shared<sgre::resource::BitmapFont>();
	font->MaxWidth = 8;
	font->MaxHeight = 6;
	sgre::resource::BitmapGlyph hyphen;
	hyphen.NominalWidth = 3;
	hyphen.NominalHeight = 6;
	font->Glyphs.emplace("-", hyphen);
	for (const auto [letter, width, height] : std::array<std::array<int, 3>, 6>{{{'A', 5, 6}, {'B', 2, 9}, {'C', 7, 3}, {'D', 4, 8}, {' ', 3, 6}, {'Z', 9, 5}}}) {
		auto glyph = hyphen;
		glyph.NominalWidth = static_cast<float>(width);
		glyph.NominalHeight = static_cast<float>(height);
		font->Glyphs.emplace(std::string(1, static_cast<char>(letter)), glyph);
	}

	return font;
}

static void TestGlyphTimingFilterAndPages() {
	sgre::scene::BitmapConsole console(CoreFont(), {1000, 1000});
	Require(console.State().Running && console.PendingCount() == 0 && console.State().ProhibitionWidth == 0 && console.State().HyphenWidth == 3 && console.State().FontScale == 1, "native Console constructor defaults changed");
	console.SetFontSpeed(8);
	console.Print("AB");
	console.Update(0);
	Require(console.PendingCount() == 2 && console.Displayed().empty() && console.State().AdvanceBudget == 0, "zero elapsed must retain the initial speed boundary");
	console.Update(0.5f);
	Require(console.PendingCount() == 1 && console.Displayed().size() == 1 && console.State().Cursor.Width == 5 && console.State().Drawn.Width == 5 && console.State().AdvanceBudget == 1, "native first-glyph nominal cell and width budget changed");
	console.Update(0.125f);
	Require(console.PendingCount() == 1 && console.State().AdvanceBudget == 0, "equal budget must not consume the next native token");
	console.Update(0.125f);
	Require(console.PendingCount() == 0 && console.Displayed().size() == 2 && console.State().Cursor.Width == 7 && console.State().Running, "final character does not complete before the next speed boundary");
	console.SetFontSpeed(0);
	console.Update(0);
	Require(!console.State().Running, "the actual empty-list branch must complete the task");
	console.Print("C");
	console.ClearDisplayed();
	Require(console.PendingCount() == 1 && console.Displayed().empty() && console.State().Running && console.State().Cursor.Width == 0, "clear must reset layout and reactivate while preserving pending text");
	console.SetFontScale(2);
	console.SetFontColor(0x80402010);
	console.Update(0);
	Require(console.Displayed().size() == 1 && console.Displayed()[0].Scale == 2 && console.Displayed()[0].Color == 0x80402010 && console.State().Cursor.Width == 14 && console.State().Drawn.Height == 6, "captured color/scale must precede nominal glyph measurement");
	console.StorePage();
	console.Print("D");
	console.ClearDisplayed();
	console.RestorePage(0);
	Require(console.Displayed().size() == 1 && console.State().Cursor.Width == 0 && console.PendingCount() == 1 && console.State().Running, "page restoration must replace only displayed text");
	console.MakeEmpty();
	Require(console.PendingCount() == 0 && console.Displayed().size() == 1 && console.State().Running, "makeEmpty must retain displayed/task state");
	console.SetFontScale(1);
	console.SetFontColor(0xffffffff);
	console.Print("A\nB\fC");
	std::size_t filtered = 0;
	console.Update(0, [&](const auto& element) {
		++filtered;
		return element.CodeUnits.front() == 'A';
	});
	Require(filtered == 4 && console.PendingCount() == 1 && !console.State().Running && console.State().Cursor.Width == 2, "filter must see popped control elements and formfeed must retain remaining pending text");
	console.SetAlignment(1);
	Require(console.AlignedCursor().Width == 501, "center cursor reports native remaining-width plus current position");
	console.SetAlignment(2);
	Require(console.AlignedCursor().Width == 1000, "right cursor must follow native arithmetic order");
	console.ClearDisplayed();
	console.MakeEmpty();
	console.SetEraseLeftEndSpace(true);
	console.Print(" A");
	console.Update(0);
	Require(console.Displayed().size() == 1 && console.Displayed()[0].CodeUnits.front() == 'A' && console.State().Cursor.Width == 5, "leading-space erasure must not consume a cell");
	sgre::text::IndicatorTextElement pushed;
	pushed.CodeUnits.push_back('Z');
	pushed.Color = 0x12345678;
	pushed.Scale = 2;
	console.PushPending(pushed);
	Require(!console.State().Running && console.PendingCount() == 1, "native push does not reactivate a completed task");
	const auto popped = console.PopPending();
	Require(popped.CodeUnits == pushed.CodeUnits && popped.Color == pushed.Color && popped.Scale == pushed.Scale && console.PendingCount() == 0, "native pop preserves the real prepared token");
	RequireFailure([&] {
		static_cast<void>(console.PopPending());
	}, "console is empty");

	sgre::scene::BitmapConsole wrap(CoreFont(), {6, 10});
	wrap.Print("AB");
	wrap.Update(0);
	Require(wrap.Displayed().size() == 2 && wrap.Displayed()[0].CodeUnits.front() == 'A' && wrap.Displayed()[1].IsLineBreak() && wrap.PendingCount() == 1 && !wrap.State().Running && wrap.State().Cursor.Width == 0 && wrap.State().Cursor.Height == 6 && wrap.State().LineBottom == 15 && wrap.State().Drawn.Height == 6, "native wrap must publish its newline before the following real glyph exceeds height and is reinserted");
	sgre::scene::BitmapConsole height(CoreFont(), {1000, 1});
	height.Print("A");
	height.Update(0);
	Require(height.PendingCount() == 1 && height.Displayed().empty() && !height.State().Running, "height overflow must reinsert its actual token and complete without invented scrolling");
}

static void RequireDisplayed(const sgre::scene::BitmapConsole& console, std::u16string_view expected) {
	Require(console.Displayed().size() == expected.size(), "native wrapping displayed token count differs");
	for (std::size_t index = 0; index < expected.size(); ++index) {
		const auto& token = console.Displayed()[index];
		Require(token.CodeUnits.size() == 1 && token.CodeUnits.front() == static_cast<std::uint16_t>(expected[index]), "native wrapping changed the actual character/newline order");
	}
}

static void TestNativeEmptyTableWrapping() {
	sgre::scene::BitmapConsole cells(CoreFont(), {6, 100});
	cells.Print("AAB");
	cells.Update(0);
	RequireDisplayed(cells, u"A\nA\nB");
	Require(!cells.State().Running && cells.State().Cursor.Width == 2 && cells.State().Cursor.Height == 12 && cells.State().LineBottom == 21 && cells.State().Drawn.Width == 5 && cells.State().Drawn.Height == 21 && cells.State().PreviousCodeUnit == 'B', "native empty membership must wrap glyph cells without inventing Latin word grouping or hyphens");

	sgre::scene::BitmapConsole boundary(CoreFont(), {7, 100});
	boundary.Print("ABC");
	boundary.Update(0);
	RequireDisplayed(boundary, u"AB\nC");
	Require(boundary.State().Cursor.Width == 7 && boundary.State().Cursor.Height == 9 && boundary.State().Drawn.Width == 7 && boundary.State().Drawn.Height == 12, "native equal width must fit and the next glyph must advance from actual prior line height");

	sgre::scene::BitmapConsole reserve(CoreFont(), {7, 100});
	reserve.SetProhibitionWidth(1);
	reserve.Print("AB");
	reserve.Update(0);
	RequireDisplayed(reserve, u"A\nB");
	Require(reserve.State().Cursor.Width == 2 && reserve.State().Cursor.Height == 6 && reserve.State().LineBottom == 15, "native numeric reserve must participate after width+cursor even with an empty prohibition set");

	sgre::scene::BitmapConsole spacing(CoreFont(), {7, 100});
	spacing.SetSpacing({1, 2});
	spacing.Print("AB");
	spacing.Update(0);
	RequireDisplayed(spacing, u"A\nB");
	Require(spacing.State().Cursor.Width == 3 && spacing.State().Cursor.Height == 8 && spacing.State().Drawn.Width == 5 && spacing.State().Drawn.Height == 17, "native wrapping must keep interglyph spacing out of drawn width and add vertical spacing after line bottom");

	sgre::scene::BitmapConsole large(CoreFont(), {6, 100});
	large.SetFontScale(2);
	large.SetFontColor(0x80402010);
	large.Print("A");
	large.Update(0);
	RequireDisplayed(large, u"\nA");
	Require(large.Displayed()[0].Scale == 1 && large.Displayed()[0].Color == 0xffffffff && large.Displayed()[1].Scale == 2 && large.Displayed()[1].Color == 0x80402010 && large.State().Cursor.Width == 10 && large.State().Cursor.Height == 0 && large.State().Drawn.Width == 10 && large.State().Drawn.Height == 12, "native oversized first glyph must retain captured style after one default LF without shrinking to the view");

	sgre::scene::BitmapConsole erased(CoreFont(), {6, 100});
	erased.SetEraseLeftEndSpace(true);
	erased.Print("A B");
	erased.Update(0);
	RequireDisplayed(erased, u"A\nB");
	Require(erased.State().Cursor.Width == 2 && erased.State().Cursor.Height == 6 && erased.State().Drawn.Height == 15, "native left-space erasure must follow real wrapping and cell-height preparation");

	sgre::scene::BitmapConsole scrolling(CoreFont(), {6, 10});
	scrolling.SetScrollSpeed(1);
	scrolling.Print("AA");
	scrolling.Update(0);
	RequireDisplayed(scrolling, u"A\n");
	Require(scrolling.PendingCount() == 1 && scrolling.State().Running && scrolling.State().ScrollRemaining == 2 && scrolling.State().AdvanceBudget == 0, "native wrap overflow must stage its actual scroll amount without discarding the pending styled glyph");
	RequireFailure([&] {
		scrolling.Update(1);
	}, "scrolling");

	sgre::scene::BitmapConsole negative(CoreFont(), {10, 100});
	negative.SetFontScale(-1);
	negative.Print("A");
	RequireFailure([&] {
		negative.Update(0);
	}, "negative element scale");
	sgre::scene::BitmapConsole negative_spacing(CoreFont(), {10, 100});
	negative_spacing.SetSpacing({-1, 0});
	negative_spacing.Print("A");
	RequireFailure([&] {
		negative_spacing.Update(0);
	}, "negative cell/spacing");
}

static void TestActualNativeTaskAndScriptFilter() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	sgre::script::SquirrelVm vm(limits);
	sgre::script::ScriptObjectBindings objects(vm);
	sgre::script::ScriptPsbBindings psb(vm, objects);
	std::uint32_t uploads = 0;
	std::size_t releases = 0;
	sgre::scene::TextureCache textures([&](auto, const auto&) {
		return sgre::render::SpriteTextureHandle{++uploads, 1, &uploads};
	}, [&](auto) {
		++releases;
	});
	sgre::scene::MotionAssetCatalog motions(textures);
	sgre::scene::BitmapFontAssetCatalog fonts(textures);
	sgre::scene::LayerTree tree(8);
	sgre::script::ScriptLayerBindings layers(vm, objects, tree);
	layers.ConfigureMotionOwners(psb, motions, textures);
	sgre::script::ScriptFontBindings font_bindings(vm, objects, psb);
	sgre::script::ScriptIndicatorBindings views(vm, objects, layers, tree, psb, fonts, textures, &font_bindings);
	float actual_elapsed = 0;
	std::size_t draws = 0;
	sgre::script::ScriptConsoleServices services;
	services.ReadFrameDelta = [&] {
		return actual_elapsed;
	};
	services.Draw = [&](const auto& console, const auto& placement, const auto& provider, auto argument) {
		Require(console.Displayed().size() == 1 && placement.Visible() && provider.SourceCount() == 2 && argument == 19, "genuine task draw must forward its actual font/text/placement and containing argument");
		++draws;
	};
	sgre::script::ScriptConsoleBindings consoles(vm, objects, layers, tree, psb, fonts, textures, views, std::move(services), &font_bindings);
	const auto derived = vm.CreateClass(consoles.ConsoleClass());
	std::vector<std::string> filtered;
	bool release_rejected = false;
	vm.NewSlot(derived, std::string("onFilter"), vm.CreateNativeClosure("Fixture.Console.onFilter", [&](auto&, auto arguments) {
		filtered.push_back(sgre::script::StringText(arguments[1]));
		if (filtered.size() == 1) {
			RequireFailure([&] {
				consoles.ReleaseInstance(arguments[0]);
			}, "borrowed");
			release_rejected = true;
		}

		return sgre::script::SquirrelNativeResult::ReturnValue(filtered.back() == "B");
	}));
	const auto layer = Construct(vm, layers.LayerClass());
	const auto psb_font = psb.WrapDocument(Document(), Backing());
	const auto font = Construct(vm, font_bindings.PsbFontClass(), {psb_font});
	const auto console = Construct(vm, derived, {layer, font, std::int32_t{1000}, std::int32_t{100}});
	Require(vm.IsInstanceOf(console, consoles.ConsoleClass()) && vm.IsInstanceOf(console, views.LayerViewClass()) && !vm.IsInstanceOf(console, layers.LayerBaseClass()), "Console must share true LayerView ancestry without inventing a Layer owner");
	Require(vm.RetainedReferenceCount(layer) == 0 && vm.RetainedReferenceCount(psb_font) == 2 && uploads == 2, "Console must borrow the actual parent and retain its provider independently");
	auto& group = layers.MotionTasks(layer).Group(sgre::scene::MotionTaskGroup::First);
	std::array<sgre::scene::ParallelTaskHandle, 1> order;
	Require(group.Count() == 1 && group.WriteOrder(order) == 1 && group.State(order[0]).DrawPaused, "Console must attach one genuinely hidden active task to First");
	static_cast<void>(Call(vm, console, "setFontSpeed", {8.0f}));
	static_cast<void>(Call(vm, console, "print", {std::string("AB")}));
	group.Update();
	Require(filtered.empty() && consoles.BitmapState(console).PendingCount() == 2, "real zero elapsed frame must not consume paced Console text");
	actual_elapsed = 0.5f;
	group.Update();
	Require(filtered == std::vector<std::string>{"A"} && release_rejected && consoles.BitmapState(console).Displayed().size() == 1 && consoles.BitmapState(console).State().Cursor.Width == 5, "real task update must invoke the actual derived filter on the popped first character");
	actual_elapsed = 0.25f;
	group.Update();
	Require(filtered == std::vector<std::string>{"A", "B"} && consoles.BitmapState(console).PendingCount() == 0 && !std::get<bool>(Call(vm, console, "getRunning")), "consumed filter token must not allocate a cell and the empty branch completes after it");
	static_cast<void>(Call(vm, console, "setCoord", {10.0f, 20.0f}));
	static_cast<void>(Call(vm, console, "setSize", {100.0f, 200.0f}));
	Require(std::get<float>(Call(vm, console, "getRight")) == 110 && std::get<float>(Call(vm, console, "getBottom")) == 220, "Console must use the actual mutable MFixedSizeView extent");
	static_cast<void>(Call(vm, console, "setVisible", {true}));
	group.Draw(19);
	Require(draws == 1, "a complete Console remains genuinely drawable until retirement");
	static_cast<void>(Call(vm, console, "push", {std::string("ZQ")}));
	Require(sgre::script::StringText(Call(vm, console, "pop")) == "Z", "native push/pop must expose exactly the first UTF-16 code unit");
	static_cast<void>(Call(vm, console, "clear"));
	Require(group.State(order[0]).Status == sgre::scene::TaskStatus::Active, "clear must reactivate the real containing task");
	layers.ReleaseInstance(layer);
	Require(releases == 2 && textures.Count() == 0 && !std::get<bool>(Call(vm, console, "getRunning")) && std::get<float>(Call(vm, console, "getCursorX")) == 1, "actual parent retirement must release physical font sources and leave a safe detached wrapper");
	consoles.ReleaseInstance(console);
	font_bindings.ReleaseInstance(font);
	Require(vm.RetainedReferenceCount(psb_font) == 0, "Console and PSBFont must each retire their distinct real PSB root");
	vm.Close();
}

int main() {
	try {
		TestGlyphTimingFilterAndPages();
		TestNativeEmptyTableWrapping();
		TestActualNativeTaskAndScriptFilter();
		std::cout << "bitmap Console native-derived glyph cells/default-empty wrapping and genuine task/filter lifecycle checks passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << "Console test error: " << error.what() << '\n';

		return 1;
	}
}
