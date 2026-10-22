#include "sgre/scene/layer_camera_draw.hpp"
#include "sgre/script/original_dialogue_layout.hpp"

#include "glyph_atlas_owner_fixture.hpp"
#include "text_glyph_drawing_fixture.hpp"

#include <array>
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

static sgre::script::SquirrelInstruction Instruction(std::uint8_t op, std::uint8_t a0, std::int32_t a1, std::uint8_t a2 = 0, std::uint8_t a3 = 0) {
	sgre::script::SquirrelInstruction result;
	result.Opcode = op;
	result.Argument0 = a0;
	result.Argument1 = a1;
	result.Argument2 = a2;
	result.Argument3 = a3;
	return result;
}

static sgre::script::BytecodeProgram OriginalFitFixture() {
	sgre::script::BytecodeProgram program;
	auto& main = program.MainFunction;
	main.Name = std::string("_updateFit");
	main.SourceName = std::string("authored-dialogue-layout-fixture");
	main.Parameters = {std::string("this")};
	main.StackSize = 3;
	main.Literals = {std::string("fixtureOriginalFit")};
	main.Instructions = {Instruction(8, 1, 0, 0, 2), Instruction(6, 255, 1, 2, 1), Instruction(19, 255, 0)};
	return program;
}

static float Number(sgre::script::SquirrelVm& vm, const sgre::script::SquirrelValue& instance, const char* key) {
	return std::get<float>(vm.GetSlot(instance, std::string(key)));
}

static void UpdatePosition(sgre::script::SquirrelVm& vm, text_drawing_test::Fixture& fixture, const sgre::script::SquirrelValue& receiver) {
	const auto scale = Number(vm, receiver, "_scale");
	const auto base = Number(vm, receiver, "_baseScale");
	const std::array<sgre::script::SquirrelValue, 3> offset{receiver, (Number(vm, receiver, "_offx") - Number(vm, receiver, "_left") * scale) * base, (Number(vm, receiver, "_offy") - Number(vm, receiver, "_top") * scale) * base};
	const std::array<sgre::script::SquirrelValue, 3> axes{receiver, Number(vm, receiver, "_scalex") * base, Number(vm, receiver, "_scaley") * base};
	static_cast<void>(vm.InvokeInContext(vm.CurrentContext(), vm.GetSlot(fixture.Text.TextRenderClass(), std::string("setOffset")), offset));
	static_cast<void>(vm.InvokeInContext(vm.CurrentContext(), vm.GetSlot(fixture.Text.TextRenderClass(), std::string("setScale")), axes));
}

static void TestCompletedDialogueFitAndRole() {
	text_atlas_test::Backend backend;
	sgre::scene::TextureCache cache([](auto, const auto&) -> sgre::render::SpriteTextureHandle {
		throw std::runtime_error("dialogue fixture must not load bitmap resources");
	},
	    [](auto) {
	    });
	sgre::scene::LayerCameraFrameState frame;
	frame.Viewport.Width = 128;
	frame.Viewport.Height = 128;
	frame.RawScissor = {0, 0, 128, 128};
	sgre::scene::LayerCameraDrawScope camera(frame, {}, [](const auto&) {
	});
	std::unique_ptr<sgre::script::OriginalDialogueLayout> policy;
	sgre::script::SquirrelValue info;
	text_drawing_test::Fixture* active = nullptr;
	std::size_t original_calls = 0;
	std::size_t draws = 0;
	float drawn_x_scale = 0;
	float drawn_y_scale = 0;
	sgre::text::TextBounds drawn_clip;
	text_drawing_test::Fixture fixture(cache, backend, [&](auto&, auto rectangles, const auto& clip) {
		++draws;
		const auto& model = camera.CurrentModel();
		drawn_x_scale = model.Coefficients[0];
		drawn_y_scale = model.Coefficients[5];
		drawn_clip = clip;
		for (const auto& rectangle : rectangles) {
			const auto& ink = rectangle.DestinationPixels;
			if (active->Text.Context(active->Instance).Content().Lines().size() > 3 && drawn_x_scale == drawn_y_scale) {
				Require(ink.Left >= clip.Left && ink.Top >= clip.Top && ink.Right <= clip.Right + 0.001f && ink.Bottom <= clip.Bottom + 0.001f, "full dialogue ink, outline and shadow must stay inside the authored clip");
				for (const auto point : {sgre::text::TextCursor{ink.Left, ink.Top}, sgre::text::TextCursor{ink.Right, ink.Bottom}}) {
					const auto x = point.X * model.Coefficients[0] + model.Coefficients[3];
					const auto y = point.Y * model.Coefficients[5] + model.Coefficients[7];
					Require(x >= 5 - 0.001f && x <= 37 + 0.001f && y >= 7 - 0.001f && y <= 31 + 0.001f, "uniform fit must keep every transformed glyph inside the original positioned dialogue box");
				}
			}
		}
	},
	    &camera, [&](auto& setup, const auto& type) {
		    active = &setup;
		    auto& vm = setup.Vm;
		    const auto env = setup.Keep(vm.CreateClass());
		    const auto info_type = setup.Keep(vm.CreateClass());
		    vm.NewSlot(info_type, std::string("render"), sgre::script::SquirrelValue{});
		    info = setup.Construct(info_type);
		    const auto selected_type = setup.Keep(vm.CreateClass());
		    vm.NewSlot(selected_type, std::string("info"), info);
		    const auto selected = setup.Construct(selected_type);
		    vm.NewSlot(env, std::string("getText"), vm.CreateNativeClosure("fixture main dialogue role", [&, selected](auto&, auto arguments) {
			    Require(arguments.size() == 2 && sgre::script::TryStringText(arguments[1]) && *sgre::script::TryStringText(arguments[1]) == "message", "policy must select the original semantic dialogue slot");
			    return sgre::script::SquirrelNativeResult::ReturnValue(selected);
		    }));
		    const auto owner = setup.Construct(env);
		    vm.NewSlot(vm.RootTable(), std::string("EnvMessage"), env);
		    vm.NewSlot(type, std::string("_owner"), vm.CreateWeakReference(owner));
		    for (const auto& [key, value] : std::array<std::pair<const char*, float>, 11>{{{"_scale", 1}, {"_baseScale", 1}, {"_width", 32}, {"_height", 24}, {"_left", 5}, {"_top", 7}, {"_offx", 0}, {"_offy", 0}, {"_scalex", 1}, {"_scaley", 1}, {"_fitMode", 2}}}) {
			    vm.NewSlot(type, std::string(key), value);
		    }
		    vm.NewSlot(type, std::string("_updatePosition"), vm.CreateNativeClosure("fixture original positioning", [&](auto& current, auto arguments) {
			    UpdatePosition(current, setup, arguments.front());
			    return sgre::script::SquirrelNativeResult::NoValues();
		    }));
		    vm.NewSlot(type, std::string("fixtureOriginalFit"), vm.CreateNativeClosure("fixture original independent axis fitting", [&](auto& current, auto arguments) {
			    ++original_calls;
			    const auto& receiver = arguments.front();
			    const auto bounds = setup.Text.Context(receiver).Content().Bounds();
			    current.SetSlot(receiver, std::string("_scalex"), 1.0f);
			    current.SetSlot(receiver, std::string("_scaley"), std::min(1.0f, Number(current, receiver, "_height") / (bounds.Bottom - bounds.Top)));
			    current.SetSlot(receiver, std::string("_offx"), bounds.Left);
			    current.SetSlot(receiver, std::string("_offy"), bounds.Top * Number(current, receiver, "_scaley"));
			    UpdatePosition(current, setup, receiver);
			    return sgre::script::SquirrelNativeResult::NoValues();
		    }));
		    vm.NewSlot(type, std::string("_updateFit"), vm.CreateClosure(OriginalFitFixture()));
		    vm.NewSlot(vm.RootTable(), std::string("BasicRender"), type);
		    policy = std::make_unique<sgre::script::OriginalDialogueLayout>(vm, setup.Text);
		    policy->InstallAvailableHook();
		    policy->InstallAvailableHook();
	    });
	fixture.Vm.SetSlot(info, std::string("render"), fixture.Instance);
	fixture.Call("setRenderSize", {32.0f, 24.0f});
	fixture.Call("setOption", {fixture.Properties({{"ignore_overy", true}})});
	fixture.Call("setFont", {fixture.Properties({{"edge", true}, {"edgesize", std::int32_t{1}}, {"shadow", true}, {"shadowdiff", std::int32_t{2}}})});
	Require(std::get<bool>(fixture.Call("render", {std::string("BB\nBB\nBB\nBB"), std::int32_t{0}, std::int32_t{3}, std::int32_t{0}, false})), "four-line fixture must render completely");
	fixture.Call("done");
	Require(fixture.Text.Context(fixture.Instance).Content().Lines().size() == 4, "actual measured fixture must retain four complete lines");
	const auto bounds = fixture.Text.DrawingBounds(fixture.Instance);
	Require(bounds.Left == -1 && bounds.Top == -1 && bounds.Right == 18 && bounds.Bottom == 34, "completed ink includes actual eight-pixel glyphs plus edge1 and shadow2");
	const auto count = fixture.Call("getRenderCount");
	const auto duration = fixture.Call("getRenderDelay");
	Require(std::get<float>(duration) > 0, "fixture must retain nonzero actual text reveal timing");
	const auto finds = fixture.FindCalls;
	const auto allocations = backend.Observed.Creates;
	fixture.Call("setClipMode", {std::int32_t{2}});
	fixture.Call("setCoord", {0.0f, 0.0f});
	fixture.Call("_updateFit");
	const auto expected = 24.0f / 35.0f;
	Require(Number(fixture.Vm, fixture.Instance, "_scalex") == expected && Number(fixture.Vm, fixture.Instance, "_scaley") == expected, "four-line dialogue must use a single scale including actual effects instead of vertical compression");
	Require(fixture.Call("getRenderCount") == count && fixture.Call("getRenderDelay") == duration && backend.Observed.Creates == allocations && fixture.FindCalls == finds, "presentation must preserve native text/reveal and reuse the prepared glyph atlas");
	fixture.Call("setShowCount", {std::int32_t{1}});
	fixture.Draw();
	Require(drawn_x_scale == expected && drawn_y_scale == expected && drawn_clip.Bottom >= 34, "partial reveal must use the completed block fit and expanded local clip");
	fixture.Call("setShowCount", {std::int32_t{1000}});
	fixture.Draw();
	Require(drawn_x_scale == expected && drawn_y_scale == expected && draws >= 4, "revealing the final line must not resize earlier letters");

	fixture.Vm.SetSlot(fixture.Instance, std::string("_scale"), 2.0f / 3.0f);
	fixture.Vm.SetSlot(fixture.Instance, std::string("_baseScale"), 1.5f);
	fixture.Call("_updateFit");
	fixture.Draw();
	Require(drawn_x_scale == drawn_y_scale && std::abs(drawn_x_scale - expected) < 0.000001f, "original logical scale and reciprocal view base must retain the same fitted physical box");
	fixture.Vm.SetSlot(fixture.Instance, std::string("_scale"), 1.0f);
	fixture.Vm.SetSlot(fixture.Instance, std::string("_baseScale"), 1.0f);

	fixture.Vm.SetSlot(info, std::string("render"), sgre::script::SquirrelValue{});
	fixture.Call("_updateFit");
	Require(Number(fixture.Vm, fixture.Instance, "_scalex") == 1 && Number(fixture.Vm, fixture.Instance, "_scaley") == 0.75f, "other text sharing the actual message owner retains original independent fitting");
	fixture.Vm.SetSlot(info, std::string("render"), fixture.Instance);
	fixture.Call("clear");
	fixture.Call("setFont", {fixture.Properties({{"edge", true}, {"edgesize", std::int32_t{1}}, {"shadow", true}, {"shadowdiff", std::int32_t{2}}})});
	fixture.Render("[B,1]BB\nBB\nBB\nBB\nBB\nBB");
	Require(fixture.Text.Context(fixture.Instance).Content().Lines().size() == 6, "longer dialogue must preserve all six original lines and ruby");
	fixture.Call("_updateFit");
	fixture.Draw();
	Require(drawn_x_scale == drawn_y_scale && drawn_y_scale < expected, "six-line text and ruby must also fit proportionally inside the actual box");

	fixture.Call("clear");
	fixture.Render("BB\nBB\nBB");
	fixture.Vm.SetSlot(fixture.Instance, std::string("_height"), 16.0f);
	fixture.Call("_updateFit");
	Require(Number(fixture.Vm, fixture.Instance, "_scalex") == 1 && Number(fixture.Vm, fixture.Instance, "_scaley") == 16.0f / 24.0f, "up to three lines retain the user's earlier original-layout requirement");
	fixture.Draw();
	Require(drawn_x_scale == 1 && drawn_y_scale == 16.0f / 24.0f && drawn_clip.Bottom == 24, "clear/re-render must retire the authored clip and restore ordinary native clipping");
	Require(original_calls == 5, "the original fit must execute exactly once for each policy call");
}

int main() {
	try {
		TestCompletedDialogueFitAndRole();
		std::cout << "Original dialogue layout tests passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << "Original dialogue layout tests failed: " << error.what() << '\n';
		return 1;
	}
}
