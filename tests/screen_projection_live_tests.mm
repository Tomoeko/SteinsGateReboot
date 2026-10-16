#include "motion_receiver_fixture.hpp"

#include "sgre/platform/host_performance_counter.hpp"
#include "sgre/render/graphics_frame_cycle.hpp"
#include "sgre/scene/screens.hpp"
#include "sgre/script/screen_projection_bindings.hpp"

#include "sgre/script/psb_bindings.hpp"

#include "sprite_presentation_fixture.hpp"

static sgre::script::SquirrelValue Call(sgre::script::SquirrelVm& vm, const sgre::script::SquirrelValue& receiver, std::string_view method, std::span<const sgre::script::SquirrelValue> inputs = {}) {
	std::vector<sgre::script::SquirrelValue> arguments{receiver};
	arguments.insert(arguments.end(), inputs.begin(), inputs.end());

	return vm.Invoke(vm.GetSlot(receiver, std::string(method)), arguments).Value;
}

static void TestActualScreenProjection(const std::filesystem::path& shader, const std::filesystem::path& transfer, bool chromatic) {
	auto description = Description(16);
	description.MaxTextures = 16;
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
	sgre::render::GraphicsFrameCycle cycle(graphics, surface, workers, cache, {}, {});
	sgre::scene::LayerTree tree(16);
	sgre::scene::ParallelTaskSet tasks(4);
	sgre::scene::ScreenRegistry screens(tree, [&](const auto& request) {
		auto allocated = description;
		allocated.Width = request.Width;
		allocated.Height = request.Height;
		sgre::scene::ScreenTargetAllocation result;
		result.Renderer = graphics.AllocateTarget(allocated);
		result.Width = allocated.Width;
		result.Height = allocated.Height;

		return result;
	});
	sgre::scene::RawTextureTask red(cycle.RawTextureBackend(), {2, 2});
	std::array<std::uint8_t, 12> rgb{};
	for (std::size_t pixel = 0; pixel < 4; ++pixel) {
		rgb[pixel * 3] = 255;
		rgb[pixel * 3 + 1] = chromatic ? 64 : 0;
		rgb[pixel * 3 + 2] = chromatic ? 32 : 0;
	}
	red.RestoreRgb(rgb);
	red.SetCoordinate({-2, -2});
	red.SetDrawPaused(false);
	sgre::scene::ScreenDriverServices services;
	services.ResolveTasks = [&]() -> sgre::scene::ParallelTaskSet& {
		return tasks;
	};
	services.Layers.State = [](auto) {
		return sgre::scene::LayerFrameProcessState{};
	};
	services.Layers.Update = [](auto) {
	};
	services.Layers.PrepareDraw = [&] {
		cycle.ResetRootCamera();
	};
	services.Layers.Draw = [&](auto, auto argument) {
		Require(argument == 0, "Screen projection fixture lost the actual inner native Draw0");
		cycle.DrawRawTexture(red, red.PrepareDrawPacket());
	};
	services.Draw = [&](auto, auto& target, auto& manager, const auto& state, auto) {
		cycle.DrawScreen(target, manager, state);
	};
	screens.ConfigureDriver(std::move(services));
	sgre::script::SquirrelVm vm;
	sgre::script::ScriptObjectBindings objects(vm);
	sgre::script::ScriptPsbBindings psb(vm, objects);
	sgre::scene::MotionAssetCatalog motions(cache);
	sgre::script::ScriptLayerBindings layers(vm, objects, tree);
	layers.ConfigureMotionOwners(psb, motions, cache);
	sgre::script::ScriptScreenBindings screen_bindings(vm, objects, layers, screens);
	std::size_t channel_draws = 0;
	sgre::script::ScriptScreenProjectionBindings projections(vm, layers, screen_bindings, screens, [&](auto texture, const auto& packet) {
		if (chromatic) {
			const std::array<std::uint32_t, 3> colors{0xffff0000, 0xff00ff00, 0xff0000ff};
			Require(packet.Color == colors[channel_draws % 3] && packet.BlendMode == (channel_draws % 3 == 0 ? 0u : 1u) && !packet.Smoothing, "CA must preserve three real channel views and inherited Layer smoothing must not rewrite their filters");
			++channel_draws;
		}
		cycle.DrawScreenProjection(texture, packet);
	});
	const auto screen = vm.Invoke(screen_bindings.ScreenClass(), std::array<sgre::script::SquirrelValue, 3>{vm.RootTable(), std::int32_t{4}, std::int32_t{4}}).Value;
	const auto screen_handle = screen_bindings.Handle(screen);
	screens.SetBackgroundColor(screen_handle, {0, 0, 255, 255});
	const auto raw_child = tree.CreateRawTextureInScreen(screens.State(screen_handle).LayerRoot);
	const auto target = graphics.RequireRegisteredTarget(screens.Renderer(screen_handle));
	const auto sampled = target->SampledTarget();
	RequireFailure([&] {
		primary->ReleaseTexture(sampled);
	},
	    "actual owner");
	const auto projection = vm.Invoke(chromatic ? projections.ChromaticProjectionClass() : projections.ProjectionClass(), std::array<sgre::script::SquirrelValue, 2>{vm.RootTable(), screen}).Value;
	const auto retained_projection = vm.Retain(projection);
	layers.AttachBaseFrame(projection, cycle.CameraScope());
	Require(std::get<float>(Call(vm, projection, "getWidth")) == 4 && std::get<float>(Call(vm, projection, "getHeight")) == 4, "Projection getters differ from the actual fixed Screen allocation");
	Require(vm.IsInstanceOf(projection, layers.LayerBaseClass()) && !vm.IsInstanceOf(projection, layers.LayerClass()), "ScreenProjection must construct its genuine SQLayerBase owner");
	sgre::scene::LayerManagerFrameOperations root_services;
	root_services.State = [&](auto layer) {
		return layers.ProcessState(layer);
	};
	root_services.Update = [&](auto layer) {
		layers.DispatchBaseFrame(layer, [](auto&, auto& frame) {
			frame.Update();
		});
	};
	root_services.PrepareDraw = [&] {
		cycle.ResetRootCamera();
	};
	root_services.Draw = [&](auto layer, auto argument) {
		layers.DispatchBaseFrame(layer, [&](auto&, auto& frame) {
			frame.Draw(argument);
		});
	};
	sgre::scene::LayerManagerFrame root(tree, {}, std::move(root_services));
	try {
		for (std::size_t iteration = 0; iteration < 2; ++iteration) {
			static_cast<void>(Call(vm, projection, "setSmoothing", std::array<sgre::script::SquirrelValue, 1>{iteration != 0}));
			if (chromatic && iteration != 0) {
				for (const auto method : {"setRMesh", "setGMesh", "setBMesh"}) {
					static_cast<void>(Call(vm, projection, method, std::array<sgre::script::SquirrelValue, 3>{std::int32_t{0}, false, 1.0f}));
				}
			}
			cycle.PreFrame();
			tasks.Update();
			root.Update();
			cycle.BeginDrawing();
			tasks.Draw(17);
			root.Draw(29);
			cycle.PostFrame();
			const auto submission = cycle.LastSubmission();
			Require(submission.has_value(), "actual Screen projection never submitted its original outer buffer");
			const auto pixels = primary->Capture(submission->Identity);
			RequirePixel(pixels, 1, 1, {0, 0, 0, 255});
			RequirePixel(pixels, 5, 5, {255, static_cast<std::uint8_t>(chromatic ? 64 : 0), static_cast<std::uint8_t>(chromatic ? 32 : 0), 255});
			RequirePixel(pixels, 7, 7, {0, 0, 255, 255});
			const auto source = target->CaptureAfterFrame(*primary, submission->Identity);
			RequirePixel(source, 1, 1, {255, static_cast<std::uint8_t>(chromatic ? 64 : 0), static_cast<std::uint8_t>(chromatic ? 32 : 0), 255});
			RequirePixel(source, 3, 3, {0, 0, 255, 255});
			Require(cycle.CurrentCounters().DrawOperations == (chromatic ? 4u : 2u) && cycle.CurrentCounters().SubmittedElements == (chromatic ? (iteration == 0 ? 22u : 76u) : 10u), "ScreenProjection must retain real indexed6 elements and Raw4 elements in one owner");
		}
		if (chromatic) {
			Require(!vm.IsInstanceOf(projection, projections.ProjectionClass()), "CA is a direct SQLayerBase sibling of the ordinary Projection class");
			static_cast<void>(Call(vm, projection, "setRMesh", std::array<sgre::script::SquirrelValue, 3>{1.0f, 0.0f, 1.0f}));
			static_cast<void>(Call(vm, projection, "setBMesh", std::array<sgre::script::SquirrelValue, 3>{-1.0f, 0.0f, 1.0f}));
			cycle.PreFrame();
			cycle.BeginDrawing();
			tasks.Draw(1);
			root.Draw(1);
			cycle.PostFrame();
			const auto separated = primary->Capture(cycle.LastSubmission()->Identity);
			RequirePixel(separated, 4, 5, {0, 64, 32, 255});
			RequirePixel(separated, 5, 5, {255, 64, 255, 255});
			static_cast<void>(Call(vm, projection, "setRMesh", std::array<sgre::script::SquirrelValue, 3>{0.0f, 0.0f, 1.0f}));
			static_cast<void>(Call(vm, projection, "setBMesh", std::array<sgre::script::SquirrelValue, 3>{0.0f, 0.0f, 1.0f}));
		}
		static_cast<void>(Call(vm, projection, "setVisible", std::array<sgre::script::SquirrelValue, 1>{false}));
		cycle.PreFrame();
		cycle.BeginDrawing();
		tasks.Draw(1);
		root.Draw(1);
		cycle.PostFrame();
		RequirePixel(primary->Capture(cycle.LastSubmission()->Identity), 6, 6, {0, 0, 0, 255});
		Require(cycle.CurrentCounters().DrawOperations == 1, "hidden actual SQLayerBase must suppress Projection without suppressing Screen rendering");
		static_cast<void>(Call(vm, projection, "setVisible", std::array<sgre::script::SquirrelValue, 1>{true}));
		cycle.PreFrame();
		cycle.BeginDrawing();
		tasks.Draw(1);
		root.Draw(1);
		cycle.AbortFrame();
		Require(!cycle.LastSubmission(), "aborted ScreenProjection committed a replacement frame");
		cycle.PreFrame();
		cycle.BeginDrawing();
		tasks.Draw(1);
		root.Draw(1);
		cycle.PostFrame();
		RequirePixel(primary->Capture(cycle.LastSubmission()->Identity), 5, 5, {255, static_cast<std::uint8_t>(chromatic ? 64 : 0), static_cast<std::uint8_t>(chromatic ? 32 : 0), 255});
		const auto self_sample = primary->SampledTarget();
		std::array<sgre::scene::ScreenProjectionVertex, 4> vertices;
		std::array<std::uint32_t, 6> indices;
		const std::array<sgre::scene::ScreenProjectionRegion, 1> regions{};
		const auto packet = sgre::scene::PrepareScreenProjectionPacket({0, 0, 4, 4}, {8, 8}, {8, 8}, .25f, 0xffffffff, regions, vertices, indices);
		cycle.PreFrame();
		cycle.BeginDrawing();
		RequireFailure([&] {
			cycle.DrawScreenProjection(self_sample, packet);
		},
		    "own active color attachment");
		Require(!cycle.LastSubmission(), "attachment feedback rejection must abort without committing a pretend sampled frame");
		cycle.AbortFrame();

	} catch (...) {
		const auto first = std::current_exception();
		try {
			cycle.AbortFrame();
		} catch (...) {
		}
		layers.ReleaseInstance(projection);
		tree.Release(raw_child);
		screen_bindings.ReleaseInstance(screen);
		vm.Close();
		std::rethrow_exception(first);
	}
	layers.ReleaseInstance(projection);
	tree.Release(raw_child);
	screen_bindings.ReleaseInstance(screen);
	vm.Close();
	screens.Close();
	red.Close();
	cycle.Close();
	workers.Close();
	const auto observed = sgre::render::SpritePassPresentationFixtureBridge::Observe(surface);
	Require(!observed.WindowVisible && !observed.WindowKey && observed.DrawableRequests == 0, "Screen projection requested a visible surface or drawable");
}

int main(int argc, char** argv) {
	try {
		if (argc != 3) {
			throw std::runtime_error("usage: screen_projection_live_tests sprite.metal presentation.metal");
		}
		TestActualScreenProjection(argv[1], argv[2], false);
		TestActualScreenProjection(argv[1], argv[2], true);
		std::cout << "ACTUAL_SCREEN_SAMPLED_TARGET_FORMAT8_SCRIPT_LAYER_AND_ABORT=PASS\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}

	return 0;
}
