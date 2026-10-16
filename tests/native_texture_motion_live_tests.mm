#include "motion_receiver_fixture.hpp"

#include "native_texture_motion_fixture.hpp"

#include "sgre/platform/host_performance_counter.hpp"
#include "sgre/render/graphics_frame_cycle.hpp"
#include "sgre/scene/image_assets.hpp"
#include "sgre/scene/motion_scene_draw_context.hpp"
#include "sgre/scene/screens.hpp"
#include "sgre/script/binary_bindings.hpp"
#include "sgre/script/image_bindings.hpp"
#include "sgre/script/native_texture_bindings.hpp"
#include "sgre/script/raw_image_bindings.hpp"
#include "sgre/script/raw_texture_bindings.hpp"
#include "sgre/script/screen_bindings.hpp"

#include "sprite_presentation_fixture.hpp"

static sgre::script::SquirrelValue Call(sgre::script::SquirrelVm& vm, const sgre::script::SquirrelValue& receiver, std::string_view name, std::initializer_list<sgre::script::SquirrelValue> inputs = {}) {
	std::vector<sgre::script::SquirrelValue> arguments{receiver};
	arguments.insert(arguments.end(), inputs.begin(), inputs.end());

	return vm.Invoke(vm.GetSlot(receiver, std::string(name)), arguments).Value;
}

static void RequireNativePixel(const sgre::render::ColorPassPixels& pixels, std::uint32_t x, std::uint32_t y, const std::array<std::uint8_t, 4>& expected) {
	const auto offset = (static_cast<std::size_t>(y) * pixels.Width + x) * 4;
	for (std::size_t component = 0; component < expected.size(); ++component) {
		if (pixels.Rgba[offset + component] != expected[component]) {
			std::string message = "NativeTextureRef pixel (" + std::to_string(x) + "," + std::to_string(y) + ") observed=";
			for (std::size_t index = 0; index < expected.size(); ++index) {
				message += std::to_string(pixels.Rgba[offset + index]) + ",";
			}
			message += " expected=";
			for (const auto value : expected) {
				message += std::to_string(value) + ",";
			}
			throw std::runtime_error(message);
		}
	}
}

static void RestoreColor(sgre::scene::RawTextureTask& texture, const std::array<std::uint8_t, 3>& color) {
	std::array<std::uint8_t, 48> rgb;
	for (std::size_t index = 0; index < rgb.size(); ++index) {
		rgb[index] = color[index % color.size()];
	}
	texture.RestoreRgb(rgb);
}

static void TestActualNativeTextureMotion(const std::filesystem::path& shader, const std::filesystem::path& transfer) {
	auto description = Description(32);
	description.MaxTextures = 64;
	description.MaxPipelines = 128;
	description.MaxTextureBytes = 65536;
	sgre::render::SpriteGraphicsTargets graphics(description, shader, 4);
	const auto primary = graphics.PrimaryTarget();
	sgre::render::SpritePassPresentation surface(*primary, transfer, sgre::render::SpritePresentationDescription{});
	sgre::platform::HostPerformanceCounter counter;
	sgre::core::ThreadTaskFrame workers([&] {
		return counter.ReadRawCounter();
	},
	    1, 4);
	sgre::scene::TextureCache cache(Upload(*primary), Release(*primary));
	sgre::scene::MotionAssetCatalog assets(cache);
	sgre::scene::ImageAssetCatalog image_assets(cache);
	sgre::render::GraphicsFrameCycle cycle(graphics, surface, workers, cache, {}, {});
	sgre::scene::LayerTree tree(16);
	sgre::scene::ParallelTaskSet priority(8);
	sgre::scene::ScreenRegistry screens(tree, [&](const auto& request) {
		auto target = description;
		target.Width = request.Width;
		target.Height = request.Height;
		sgre::scene::ScreenTargetAllocation result;
		result.Renderer = graphics.AllocateTarget(target);
		result.Width = target.Width;
		result.Height = target.Height;
		return result;
	});
	sgre::scene::ScreenDriverServices screen_services;
	screen_services.ResolveTasks = [&]() -> sgre::scene::ParallelTaskSet& {
		return priority;
	};
	screen_services.Layers.State = [](auto) {
		return sgre::scene::LayerFrameProcessState{};
	};
	screen_services.Layers.Update = [](auto) {
	};
	screen_services.Layers.PrepareDraw = [&] {
		cycle.ResetRootCamera();
	};
	screen_services.Layers.Draw = [](auto, auto) {
		throw std::runtime_error("this actual Screen fixture has no inner children");
	};
	screen_services.Draw = [&](auto, auto& target, auto& manager, const auto& state, auto) {
		cycle.DrawScreen(target, manager, state);
	};
	screens.ConfigureDriver(std::move(screen_services));
	sgre::script::SquirrelVm vm;
	sgre::script::ScriptObjectBindings objects(vm);
	sgre::script::ScriptPsbBindings psb(vm, objects);
	sgre::script::ScriptLayerBindings layers(vm, objects, tree);
	layers.ConfigureMotionOwners(psb, assets, cache);
	sgre::script::ScriptScreenBindings screen_bindings(vm, objects, layers, screens);
	sgre::script::ScriptBinaryBindings binary(vm, objects);
	sgre::script::ScriptRawImageBindings raw_images(vm, objects, binary);
	sgre::script::ScriptRawTextureBindings raw(vm, objects, layers, raw_images, binary, tree, cycle.RawTextureBackend(), priority);
	sgre::script::ScriptImageBindings images(vm, objects, psb, image_assets, cache);
	sgre::script::ScriptNativeTextureBindings references(vm, objects, screen_bindings, screens, raw, images, cache, *primary);
	sgre::core::FrameClock clock(0);
	std::array<std::uint32_t, sgre::scene::kMotionSharedSeedWords> seed;
	for (std::size_t index = 0; index < seed.size(); ++index) {
		seed[index] = static_cast<std::uint32_t>(index) * 0x9e3779b9u + 17;
	}
	sgre::script::SquirrelValue leaf;
	sgre::script::SquirrelValue motion;
	sgre::script::SquirrelValue screen;
	sgre::script::SquirrelValue raw_source;
	sgre::script::SquirrelValue screen_ref;
	sgre::script::SquirrelValue raw_ref;
	std::unique_ptr<sgre::scene::MotionManagerRuntime> runtime;
	sgre::script::ScriptMotionBindingServices motion_services;
	motion_services.ResolveRuntime = [&](const auto& actual_leaf, auto source) -> sgre::scene::MotionManagerRuntime& {
		Require(actual_leaf == leaf && source == sgre::script::MotionLayerSource::Secondary && runtime != nullptr, "physical NativeTextureRef must select its actual generic Motion Layer runtime");
		return *runtime;
	};
	motion_services.ReadAsynchronousMode = [] {
		return false;
	};
	sgre::script::ScriptMotionBindings motions(vm, objects, layers, std::move(motion_services));
	motions.ConfigureAlternateTextures(references.NativeTextureClass(), [&](const auto& value) -> const sgre::scene::NativeTextureReference& {
		return references.BorrowNativeTexture(value).Reference();
	});
	bool cleaned = false;
	const auto cleanup = [&] {
		if (cleaned) {
			return;
		}
		cycle.AbortFrame();
		if (runtime) {
			runtime->SceneContext().Clear();
		}
		if (std::holds_alternative<sgre::script::SquirrelObjectHandle>(motion) && motions.NativeOwners().Contains(motion)) {
			motions.NativeOwners().ReleaseInstance(motion);
		}
		motion = {};
		if (std::holds_alternative<sgre::script::SquirrelObjectHandle>(screen_ref)) {
			references.ReleaseInstance(screen_ref);
			screen_ref = {};
		}
		if (std::holds_alternative<sgre::script::SquirrelObjectHandle>(raw_ref)) {
			references.ReleaseInstance(raw_ref);
			raw_ref = {};
		}
		if (std::holds_alternative<sgre::script::SquirrelObjectHandle>(raw_source)) {
			raw.ReleaseInstance(raw_source);
			raw_source = {};
		}
		priority.Update();
		if (std::holds_alternative<sgre::script::SquirrelObjectHandle>(screen)) {
			screen_bindings.ReleaseInstance(screen);
			screen = {};
		}
		if (std::holds_alternative<sgre::script::SquirrelObjectHandle>(leaf)) {
			layers.ReleaseInstance(leaf);
			leaf = {};
		}
		motions.NativeOwners().Close();
		if (runtime) {
			runtime->Close();
		}
		vm.ResetExecution();
		vm.Close();
		screens.Close();
		priority.Clear();
		cycle.Close();
		workers.Close();
		cleaned = true;
	};
	try {
		leaf = vm.Invoke(layers.LayerClass(), std::array<sgre::script::SquirrelValue, 1>{vm.RootTable()}).Value;
		sgre::scene::MotionManagerRuntimeServices runtime_services;
		runtime_services.ReadSourceAliases = [&](auto project) {
			return layers.MotionResources(leaf).SourceAliases(project);
		};
		runtime_services.SceneAssets = &assets;
		runtime_services.ReadRegisteredAsset = [&](auto project) -> const sgre::scene::LoadedMotionAsset& {
			return layers.MotionResources(leaf).BorrowAsset(project);
		};
		runtime = std::make_unique<sgre::scene::MotionManagerRuntime>(layers.MotionResources(leaf).Secondary(), clock, seed, std::move(runtime_services), 32);
		layers.AttachMotionRuntime(leaf, *runtime);
		// Allocate two genuine registrations, then retire the first. The live atlas must retain native slot1.
		const auto unused_resource = psb.WrapDocument(Document(), std::make_shared<const std::vector<std::uint8_t>>(144, 255));
		const auto unused_project = Call(vm, leaf, "registerMotionResource", {unused_resource});
		const auto resource = psb.WrapDocument(Document(), std::make_shared<const std::vector<std::uint8_t>>(144, 255));
		static_cast<void>(Call(vm, leaf, "registerMotionResource", {resource}));
		static_cast<void>(Call(vm, leaf, "unregisterMotionResource", {unused_project}));
		screen = vm.Invoke(screen_bindings.ScreenClass(), std::array<sgre::script::SquirrelValue, 3>{vm.RootTable(), std::int32_t{4}, std::int32_t{4}}).Value;
		screens.SetBackgroundColor(screen_bindings.Handle(screen), {255, 0, 0, 255});
		raw_source = vm.Invoke(raw.RawTextureClass(), std::array<sgre::script::SquirrelValue, 3>{vm.RootTable(), std::int32_t{4}, std::int32_t{4}}).Value;
		RestoreColor(raw.Owner(raw_source).TextureTask(), {0, 255, 0});
		screen_ref = vm.Invoke(references.NativeTextureClass(), std::array<sgre::script::SquirrelValue, 2>{vm.RootTable(), screen}).Value;
		raw_ref = vm.Invoke(references.NativeTextureClass(), std::array<sgre::script::SquirrelValue, 2>{vm.RootTable(), raw_source}).Value;
		const auto screen_borrow = references.BorrowNativeTexture(screen_ref);
		const auto raw_borrow = references.BorrowNativeTexture(raw_ref);
		Require(screen_borrow.Reference().Read().Texture == screens.Renderer(screen_bindings.Handle(screen)).SampledTarget() && raw_borrow.Reference().Read().MutableSource == &raw.Owner(raw_source).TextureTask().Texture(), "NativeTextureRef constructors substituted physical source ownership");
		const auto size = Call(vm, screen_ref, "getSize");
		const auto bounds = Call(vm, raw_ref, "getActiveBounds");
		Require(std::get<float>(vm.GetSlot(size, std::string("width"))) == 4 && std::get<float>(vm.GetSlot(bounds, std::string("right"))) == 4, "actual descriptor size or active bounds changed");
		motion = vm.Invoke(motions.MotionClass(), std::array<sgre::script::SquirrelValue, 2>{vm.RootTable(), leaf}).Value;
		const auto rectangle = vm.CreateTable();
		for (const auto& [key, value] : std::array<std::pair<std::string_view, float>, 4>{{{"left", 0}, {"top", 0}, {"right", 4}, {"bottom", 4}}}) {
			vm.NewSlot(rectangle, std::string(key), value);
		}
		const auto origin = vm.CreateTable();
		vm.NewSlot(origin, std::string("x"), 0.0f);
		vm.NewSlot(origin, std::string("y"), 0.0f);
		static_cast<void>(Call(vm, motion, "registerAlternateTexture", {std::string("screen"), screen_ref, rectangle, origin}));
		static_cast<void>(Call(vm, motion, "registerAlternateTexture", {std::string("nested-screen"), raw_ref, rectangle, origin}));
		static_cast<void>(Call(vm, motion, "setChara", {std::string("character")}));
		static_cast<void>(Call(vm, motion, "play", {std::string("main")}));
		static_cast<void>(Call(vm, motion, "setVisible", {true}));
		auto player = motions.NativeOwners().Task(motion);
		player.SetCoordinate3({-4, -4, 0});
		auto& group = layers.MotionTasks(leaf).Group(sgre::scene::MotionTaskGroup::Second);
		std::array<sgre::scene::MotionProjectId, 1> projects;
		Require(runtime->Manager().WriteRegistrationOrder(projects) == 1, "physical fixture must use its actual one registered Motion resource");
		const auto atlas = runtime->Manager().FindSource(projects[0], "atlas");
		Require(atlas.has_value() && atlas->Slot.Index == 1, "actual source retirement must preserve the later ordinary atlas at slot1");
		const std::array<sgre::scene::CachedTextureHandle, 2> local_slots{{{}, atlas->Texture}};
		Scratch scratch;
		const auto draw = [&](bool abort) {
			player.SetTickCount(0);
			cycle.PreFrame();
			priority.Update();
			group.Update();
			cycle.BeginDrawing();
			priority.Draw(17);
			group.Draw(29);
			cycle.DrawScene(runtime->SceneContext(), local_slots, scratch.Borrow());
			if (abort) {
				cycle.AbortFrame();
			} else {
				cycle.PostFrame();
			}
		};
		const auto reject_slots = [&](std::span<const sgre::scene::CachedTextureHandle> supplied, std::string_view expected) {
			player.SetTickCount(0);
			cycle.PreFrame();
			priority.Update();
			group.Update();
			cycle.BeginDrawing();
			priority.Draw(17);
			group.Draw(29);
			RequireFailure([&] {
				cycle.DrawScene(runtime->SceneContext(), supplied, scratch.Borrow());
			},
			    expected);
			Require(!cycle.LastSubmission(), "invalid source table published a pretend physical frame");
			cycle.AbortFrame();
		};
		// Even while both rows sample external descriptors, a supplied real cache handle cannot be stale or partial.
		auto stale = local_slots;
		++stale[1].Generation;
		reject_slots(stale, "stale or belongs");
		auto partial = local_slots;
		partial[0].Index = 1;
		reject_slots(partial, "stale or belongs");
		for (std::size_t iteration = 0; iteration < 2; ++iteration) {
			if (iteration != 0) {
				RestoreColor(raw.Owner(raw_source).TextureTask(), {0, 0, 255});
			}
			static_cast<void>(Call(vm, screen_ref, "setSmoothing", {iteration != 0}));
			static_cast<void>(Call(vm, raw_ref, "setSmoothing", {iteration != 0}));
			draw(false);
			Require(cycle.LastSubmission().has_value(), "external source draw did not commit its actual outer command buffer");
			const auto pixels = primary->Capture(cycle.LastSubmission()->Identity);
			RequireNativePixel(pixels, 1, 1, {255, 0, 0, 255});
			// Native reversed priority indices draw the selected child before the root sprite in the overlap.
			RequireNativePixel(pixels, 3, 1, {255, 0, 0, 255});
			RequireNativePixel(pixels, 5, 1, iteration == 0 ? std::array<std::uint8_t, 4>{0, 255, 0, 255} : std::array<std::uint8_t, 4>{0, 0, 255, 255});
			RequireNativePixel(pixels, 7, 7, {0, 0, 0, 255});
			Require(cycle.CurrentCounters().DrawOperations == 2 && cycle.CurrentCounters().SubmittedElements == 8, "external source receiver changed actual native quad batching");
			RequireFailure([&] {
				references.ReleaseInstance(screen_ref);
			},
			    "listeners");
		}
		draw(true);
		Require(!cycle.LastSubmission(), "external sources published a pretend frame after Abort");
		draw(false);
		RequireNativePixel(primary->Capture(cycle.LastSubmission()->Identity), 5, 1, {0, 0, 255, 255});
		static_cast<void>(Call(vm, motion, "unregisterAlternateTexture", {std::string("screen")}));
		screen_borrow.Reference().RequireCanRelease();
		const std::array<sgre::scene::CachedTextureHandle, 2> missing{};
		reject_slots(missing, "positive finite");
		draw(false);
		const auto restored = primary->Capture(cycle.LastSubmission()->Identity);
		RequireNativePixel(restored, 1, 1, {255, 255, 255, 255});
		RequireNativePixel(restored, 3, 1, {0, 0, 255, 255});
		Require(runtime->Manager().ExternalTextures().RegisteredCount() == 1, "restoring authored root source disturbed nested descriptor ownership");
		static_cast<void>(Call(vm, motion, "releaseAllAlternateTextures"));
		raw_borrow.Reference().RequireCanRelease();
		cleanup();
		RequireFailure([&] {
			static_cast<void>(raw_borrow.Reference());
		},
		    "no live");
	} catch (...) {
		const auto first = std::current_exception();
		try {
			cleanup();
		} catch (...) {
			std::terminate();
		}
		std::rethrow_exception(first);
	}
	const auto observed = sgre::render::SpritePassPresentationFixtureBridge::Observe(surface);
	Require(!observed.WindowVisible && !observed.WindowKey && observed.DrawableRequests == 0, "NativeTextureRef fixture requested a visible surface or drawable");
}

int main(int argc, char** argv) {
	try {
		if (argc != 3) {
			throw std::runtime_error("usage: native_texture_motion_live_tests sprite.metal presentation.metal");
		}
		TestActualNativeTextureMotion(argv[1], argv[2]);
		std::cout << "NATIVE_TEXTURE_REF_SPARSE_ORDINARY_SLOT1_DIRTY_RESTORE_ABORT_AND_INVALID_HANDLES=PASS\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
	return 0;
}
