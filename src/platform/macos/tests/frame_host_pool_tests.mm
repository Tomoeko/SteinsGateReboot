#include "sgre/render/frame_host_pool.hpp"

#include "sgre/render/sprite_graphics_targets.hpp"

#include "sprite_presentation_fixture.hpp"

#include <array>
#include <chrono>
#include <exception>
#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>

static_assert(!std::is_constructible_v<sgre::render::FrameHostInputBorrow, sgre::render::FrameHostPool&, const sgre::render::SpriteHostInputs&>);
static_assert(!std::is_move_constructible_v<sgre::render::FrameHostInputBorrow>);

template <typename Pool, typename Input>
concept ConstructsAdmission = requires(Pool& pool, const Input& input) {
	sgre::render::FrameHostInputBorrow(pool, input, {});
};
static_assert(!ConstructsAdmission<sgre::render::FrameHostPool, sgre::render::SpriteHostInputs>);

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		std::cerr << "first fixture failure: " << message << '\n';
		throw std::runtime_error(std::string(message));
	}
}

template <typename Operation>
static void RequireFailure(Operation operation, std::string_view fragment) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(fragment) != std::string_view::npos, error.what());
		return;
	}
	Require(false, "missing actual containing-pool rejection");
}

static sgre::render::SpritePassDescription Description() {
	sgre::render::SpritePassDescription result;
	result.Width = 4;
	result.Height = 4;
	result.MaxVertices = 8;
	result.MaxIndices = 8;
	result.MaxDraws = 2;
	result.MaxTextures = 1;
	result.MaxPipelines = 1;
	result.MaxTextureBytes = 4;
	return result;
}

static std::uint64_t Counter() {
	// This actual host counter is an authored fixture input; native clock units remain open.
	return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

static void TestPool(const std::filesystem::path& shader, const std::filesystem::path& transfer) {
	sgre::render::SpriteGraphicsTargets graphics(Description(), shader, 2);
	const auto renderer = graphics.PrimaryTarget();
	const auto other = graphics.AllocateTarget(Description());
	static_cast<void>(graphics.SelectTarget(renderer));
	sgre::scene::GraphicsModelStack models(8);
	models.Reset();
	sgre::render::SpriteGraphicsCamera camera(graphics, models);
	camera.ResetDefaultCamera();
	sgre::render::RenderFrameStatistics statistics({}, {});
	sgre::render::RenderFrameStatistics other_statistics({}, {});
	sgre::render::SpriteRenderRecords records(*renderer, camera, statistics);
	sgre::render::SpriteRenderRecords unrelated(*renderer, camera, other_statistics);
	sgre::core::ThreadTaskFrame workers(Counter, 1, 3);
	sgre::core::ThreadTaskFrame wrong_workers(Counter, 1, 1);
	{
		sgre::render::SpritePassPresentation wrong_surface(*other, transfer, sgre::render::SpritePresentationDescription{});
		RequireFailure([&] {
			sgre::render::FrameHostPool rejected(wrong_surface, workers, records);
		},
		    "another actual renderer");
	}
	sgre::render::SpritePassPresentation surface(*renderer, transfer, sgre::render::SpritePresentationDescription{});
	sgre::render::FrameHostPool pool(surface, workers, records);
	RequireFailure([&] {
		pool.RequireRecords(unrelated);
	},
	    "another actual record");

	std::array<sgre::scene::MotionSpriteVertex, 4> vertices;
	vertices[0].Position = {-2, -2};
	vertices[1].Position = {2, -2};
	vertices[2].Position = {-2, 2};
	vertices[3].Position = {2, 2};
	for (auto& vertex : vertices) {
		vertex.Color = 0xffffffff;
	}
	const std::array<std::uint32_t, 4> indices{0, 2, 1, 3};
	std::optional<sgre::render::SpriteHostInputs> cell;
	std::optional<sgre::render::FrameHostInputBorrow> borrow;
	std::optional<sgre::render::FrameHostWriterBinding> binding;
	std::size_t updates = 0;
	std::size_t posts = 0;
	std::thread::id writer_thread;
	bool fail_update = false;
	sgre::core::ThreadTaskReceiver writer(workers, [&](auto& actual_receiver) {
		actual_receiver.RequireExecutingUpdate();
		writer_thread = std::this_thread::get_id();
		++updates;
		if (fail_update) {
			throw std::runtime_error("authored actual worker failure");
		}
		binding->EmplaceInputs(cell, borrow, vertices, indices);
	},
	    [&](auto& actual_receiver) {
		    ++posts;
		    Require(!actual_receiver.State().PostPending, "native-shaped Post flag did not clear before callback");
		    RequireFailure([&] {
			    pool.End();
		    },
		        "pending or executing");
	    });
	binding.emplace(pool, writer);
	sgre::core::ThreadTaskReceiver wrong_writer(wrong_workers, [](auto&) {
	},
	    [](auto&) {
	    });
	RequireFailure([&] {
		sgre::render::FrameHostWriterBinding rejected(pool, wrong_writer);
	},
	    "exact quiescent actual controller");
	Require(pool.BoundWriters() == 1 && &writer.FrameOwner() == &workers, "actual controller identity or unique writer changed");
	RequireFailure([&] {
		binding->EmplaceInputs(cell, borrow, vertices, indices);
	},
	    "executing Update");

	// A genuinely pending unbound receiver is unrelated to this heap's writers.
	bool other_callback_ran = false;
	sgre::core::ThreadTaskReceiver unbound(workers, [&](auto&) {
		other_callback_ran = true;
		RequireFailure([&] {
			binding->EmplaceInputs(cell, borrow, vertices, indices);
		},
		    "executing Update");
	},
	    [](auto&) {
	    });
	unbound.QueueUpdate();
	pool.Begin();
	const auto frame_counter = pool.FrameCounter();
	writer.QueueUpdate();
	writer.QueuePost();
	writer.QueuePost();
	// An authored containing-root exception leaves actual queued work in the Idle controller.
	try {
		throw std::runtime_error("authored root exit before RunFrame");
	} catch (const std::runtime_error&) {
		workers.RequireCanRunFrame();
		Require(writer.State().UpdatePending && !writer.CanClose(), "root exit lost the genuine pending writer");
		RequireFailure([&] {
			pool.End();
		},
		    "pending or executing");
		RequireFailure([&] {
			binding->Close();
		},
		    "quiescent callback");
		Require(pool.FrameCounter() == frame_counter && !cell && !borrow, "rejected root-exit cleanup changed pages or manufactured input");
	}
	workers.RunFrame();
	Require(other_callback_ran && updates == 1 && posts == 2 && writer_thread != std::this_thread::get_id() && writer.CanClose(), "genuine workers or duplicate Post rows did not quiesce");
	Require(cell && borrow && pool.RetainedInputs() == 1, "joined actual writer did not retain one exact allocation borrow");
	RequireFailure([&] {
		pool.EmplaceInputs(cell, borrow, vertices, indices);
	},
	    "empty exact cells");
	std::optional<sgre::render::SpriteHostInputs> foreign_cell;
	std::optional<sgre::render::FrameHostInputBorrow> foreign_borrow;
	std::exception_ptr foreign_failure;
	std::thread foreign([&] {
		try {
			pool.EmplaceInputs(foreign_cell, foreign_borrow, vertices, indices);
		} catch (...) {
			foreign_failure = std::current_exception();
		}
	});
	foreign.join();
	Require(foreign_failure && !foreign_cell && !foreign_borrow && pool.RetainedInputs() == 1, "foreign thread published an input borrow");
	RequireFailure([&] {
		std::rethrow_exception(foreign_failure);
	},
	    "serialized owner thread");

	sgre::resource::RgbaImage image;
	image.Width = 1;
	image.Height = 1;
	image.Pixels = {255, 0, 0, 255};
	const auto texture = renderer->UploadTexture(sgre::resource::PixelFormat::Bc7, image);
	sgre::scene::MotionSpriteMaterial material;
	material.BlendMode = 0;
	sgre::scene::SpritePipelineOptions options;
	const auto pipeline = renderer->PreparePipeline(sgre::scene::SelectSpritePipeline(material, options));
	records.BeginFrame({}, sgre::render::SpritePassLoadAction::Clear);
	RequireFailure([&] {
		pool.Begin();
	},
	    "inactive record");
	RequireFailure([&] {
		pool.End();
	},
	    "inactive record");
	records.BeginRecord();
	records.SetFilterColor(0xffffffff);
	records.Append(texture, pipeline, *cell, 0);
	records.EndRecord();
	const auto submission = records.EndFrame();
	pool.End();
	RequireFailure([&] {
		pool.Begin();
	},
	    "retained frame inputs");
	RequireFailure([&] {
		pool.Close();
	},
	    "retained frame inputs");
	Require(pool.FrameCounter() == frame_counter, "retained input rejection advanced the real heap");
	borrow.reset();
	cell.reset();
	Require(pool.RetainedInputs() == 0, "actual borrow-before-input retirement left a registry entry");
	pool.Begin();
	Require(pool.FrameCounter() == frame_counter + 1, "quiescent actual recycle did not advance once");
	pool.End();
	const auto pixels = renderer->Capture(submission);
	Require(pixels.Rgba.size() == 64, "actual host copy has no target pixels");
	for (std::size_t pixel = 0; pixel < 16; ++pixel) {
		Require(pixels.Rgba[pixel * 4] == 255 && pixels.Rgba[pixel * 4 + 1] == 0 && pixels.Rgba[pixel * 4 + 2] == 0 && pixels.Rgba[pixel * 4 + 3] == 255, "actual Metal copy changed after CPU borrow retirement/recycle");
	}

	// Real worker failure leaves duplicate Post rows pending; pool closure cannot pretend those callbacks retired.
	pool.Begin();
	fail_update = true;
	writer.QueueUpdate();
	writer.QueuePost();
	writer.QueuePost();
	RequireFailure([&] {
		workers.RunFrame();
	},
	    "authored actual worker failure");
	Require(!writer.State().UpdatePending && writer.State().PostPending && !writer.CanClose(), "worker failure erased genuine pending Post rows");
	RequireFailure([&] {
		pool.End();
	},
	    "pending or executing");
	RequireFailure([&] {
		pool.Close();
	},
	    "pending or executing");
	// The fixture explicitly invokes the real remaining Post phase; the pool never drains or cancels it.
	workers.RunFrame();
	Require(posts == 4 && writer.CanClose(), "actual remaining duplicate Posts did not finish");
	pool.EmplaceInputs(cell, borrow, vertices, indices);
	records.BeginFrame({}, sgre::render::SpritePassLoadAction::Clear);
	records.AbortFrame();
	borrow.reset();
	cell.reset();
	pool.End();
	RequireFailure([&] {
		pool.Close();
	},
	    "must detach");
	binding->Close();
	binding.reset();
	Require(pool.BoundWriters() == 0, "explicit writer detachment retained its raw receiver identity");
	const auto objects = sgre::render::SpritePassPresentationFixtureBridge::BorrowObjects(surface);
	objects.Layer.maximumDrawableCount = 3;
	RequireFailure([&] {
		pool.Begin();
	},
	    "drawable capacity changed");
	pool.Close();
	objects.Layer.maximumDrawableCount = 2;
	RequireFailure([&] {
		pool.RequireRecords(records);
	},
	    "closed");
	const auto observed = sgre::render::SpritePassPresentationFixtureBridge::Observe(surface);
	Require(!observed.WindowVisible && !observed.WindowKey && !observed.WindowMain && observed.DrawableRequests == 0 && !observed.Presenting, "containing pool requested a drawable or activated a window");
	renderer->ReleaseTexture(texture);
	writer.Close();
	unbound.Close();
	wrong_writer.Close();
	workers.Close();
	wrong_workers.Close();
}

int main(int argc, char** argv) {
	@try {
		try {
			Require(argc == 3, "frame-host-pool SPRITE_SHADER TRANSFER_SHADER");
			@autoreleasepool {
				const auto foreground_pid = NSWorkspace.sharedWorkspace.frontmostApplication.processIdentifier;
				Require(foreground_pid > 0, "actual foreground witness absent");
				TestPool(argv[1], argv[2]);
				Require(NSApp.windows.count == 0 && !NSApp.active && NSWorkspace.sharedWorkspace.frontmostApplication.processIdentifier == foreground_pid, "pool fixture changed foreground or retained a window");
				std::cout << "PASS actual layer2 pool, exact writer/controller/record/heap lifetime, duplicate Posts and root/worker exits, actual16 Metal pixels, quiescent fault-safe CPU cleanup; no drawable/Present/native execution\n";
			}
			return 0;
		} catch (const std::exception& error) {
			std::cerr << error.what() << '\n';
			return 1;
		}
	} @catch (NSException* exception) {
		std::cerr << "pool fixture Objective-C exception: " << exception.reason.UTF8String << '\n';
		return 1;
	}
}
