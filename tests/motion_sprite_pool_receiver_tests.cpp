#include "motion_receiver_fixture.hpp"

#include "sgre/core/thread_task_frame.hpp"
#include "sgre/platform/host_performance_counter.hpp"
#include "sgre/render/frame_host_pool.hpp"
#include "sgre/render/sprite_presentation.hpp"

static void TestActualPoolAttachment(const std::filesystem::path& shader, const std::filesystem::path& transfer_shader) {
	sgre::render::SpriteGraphicsTargets graphics(Description(), shader, 2);
	const auto renderer = graphics.PrimaryTarget();
	static_cast<void>(graphics.SelectTarget(renderer));
	sgre::scene::GraphicsModelStack models(8);
	models.Reset();
	auto frame_state = ActualMonoFrameState(graphics);
	sgre::render::SpriteGraphicsCamera camera(graphics, models, frame_state);
	camera.ResetDefaultCamera();
	sgre::render::RenderFrameStatistics statistics({}, {});
	sgre::render::RenderFrameStatistics other_statistics({}, {});
	sgre::render::SpriteRenderRecords records(*renderer, camera, statistics);
	sgre::render::SpriteRenderRecords unrelated(*renderer, camera, other_statistics);
	sgre::render::SpritePassPresentation surface(*renderer, transfer_shader);
	sgre::platform::HostPerformanceCounter counter;
	sgre::core::ThreadTaskFrame workers([&counter] {
		return counter.ReadRawCounter();
	},
	    1, 2);
	sgre::render::FrameHostPool pool(surface, workers, records);
	SceneFixture scene(*renderer);
	auto& red = scene.Add("red", {-4, -4});
	auto& blue = scene.Add("blue", {0, -4});
	auto& unsupported = scene.Add("red", {-4, -4}, 1);
	RequireFailure([&] {
		sgre::render::MotionSpriteRecordReceiver wrong(unrelated, *renderer, graphics, scene.Cache, pool, 2);
	},
	    "another actual record owner");
	Require(pool.RetainedInputs() == 0 && pool.BoundWriters() == 0, "rejected unrelated actual record owner changed pool registrations");
	sgre::render::MotionSpriteRecordReceiver receiver(records, *renderer, graphics, scene.Cache, pool, 2);
	FailureCleanup cleanup{records, receiver, scene.Runtime->Context()};
	const auto& row = *red.Player->PublishedRows()[0];
	receiver.BindTexture(row.Slot.Index, row.Texture);
	Scratch scratch;
	records.ResetFrameState();
	pool.Begin();
	const auto frame_counter = pool.FrameCounter();
	records.BeginPreparedFrame({}, sgre::render::SpritePassLoadAction::Clear);
	red.Player->Draw();
	receiver.Draw(scene.Runtime->Context(), scratch.Borrow());
	Require(pool.RetainedInputs() == 1 && records.IsActive(), "actual first input was not attached to its exact pool and active record owner");
	RequireFailure([&] {
		pool.Begin();
	},
	    "active");
	Require(pool.FrameCounter() == frame_counter, "rejected active recycle advanced the actual physical pool counter");
	blue.Player->Draw();
	receiver.Draw(scene.Runtime->Context(), scratch.Borrow());
	Require(pool.RetainedInputs() == 2, "second genuine packet replaced the first stable pool input borrow");
	const auto submission = records.EndFrame();
	pool.End();
	Require(!records.IsActive() && pool.RetainedInputs() == 2, "actual End fabricated input retirement instead of retaining both concrete cells");
	RequireFailure([&] {
		pool.Begin();
	},
	    "input");
	Require(pool.FrameCounter() == frame_counter, "rejected retained-input recycle advanced its actual physical counter");
	const auto pixels = renderer->Capture(submission);
	RequirePixel(pixels, 1, 1, {255, 0, 0, 255});
	RequirePixel(pixels, 5, 1, {0, 0, 255, 255});
	receiver.ReleaseFrameInputs();
	Require(pool.RetainedInputs() == 0, "quiescent receiver failed to release its exact borrows before destroying input cells");
	records.ResetFrameState();
	pool.Begin();
	Require(pool.FrameCounter() == frame_counter + 1, "genuine released physical pool failed to advance exactly once");
	records.BeginPreparedFrame({}, sgre::render::SpritePassLoadAction::Clear);
	red.Player->Draw();
	receiver.Draw(scene.Runtime->Context(), scratch.Borrow());
	unsupported.Player->Draw();
	RequireFailure([&] {
		receiver.Draw(scene.Runtime->Context(), scratch.Borrow());
	},
	    "color-filter execution remains unsupported");
	Require(!records.IsActive() && pool.RetainedInputs() == 1 && scene.Runtime->Context().Size() == 1, "post-mutation failure did not abort the exact encoder while retaining its actual prior input and failed context");
	pool.End();
	RequireFailure([&] {
		pool.Begin();
	},
	    "input");
	scene.Runtime->Context().Clear();
	receiver.ReleaseFrameInputs();
	Require(pool.RetainedInputs() == 0, "actual Abort cleanup did not release the exact input borrow before its cell");
	pool.Close();
	records.BeginFrame({}, sgre::render::SpritePassLoadAction::Clear);
	red.Player->Draw();
	RequireFailure([&] {
		receiver.Draw(scene.Runtime->Context(), scratch.Borrow());
	},
	    "closed");
	Require(records.IsActive() && records.RecordDepth() == 0 && scene.Runtime->Context().Size() == 1, "closed real input owner aborted or mutated an unrelated usable encoder before admission");
	scene.Runtime->Context().Clear();
	records.AbortFrame();
	workers.Close();
}

int main(int argc, char** argv) {
	try {
		Require(argc == 3, "pool receiver requires actual sprite and transfer shader sources");
		TestActualPoolAttachment(argv[1], argv[2]);
		std::cout << "PASS actual descriptor pool/input/record identity, retained packet borrows, blocked recycle and actual contrasting Metal pixels; no drawable request\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
