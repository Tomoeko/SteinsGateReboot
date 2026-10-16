#include "sgre/render/sprite_graphics_camera.hpp"

#include "sgre/render/sprite_frame_encoder.hpp"
#include "sgre/render/sprite_graphics_targets.hpp"

#include <array>
#include <exception>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

static void Require(bool condition, const char* message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

template <typename Work>
static void RequireFailure(Work work, const char* fragment) {
	try {
		work();
	} catch (const std::runtime_error& error) {
		Require(std::string(error.what()).find(fragment) != std::string::npos, error.what());

		return;
	}

	throw std::runtime_error("missing actual camera globals rejection");
}

static void RequireBounds(const sgre::scene::MotionBounds& actual, const sgre::scene::MotionBounds& expected) {
	Require(actual.MinimumX == expected.MinimumX && actual.MinimumY == expected.MinimumY && actual.MaximumX == expected.MaximumX && actual.MaximumY == expected.MaximumY, "actual shared clip endpoints changed");
}

static void TestActualOwners(const std::filesystem::path& shader) {
	sgre::render::SpritePassDescription description;
	description.Width = 8;
	description.Height = 6;
	description.MaxTextures = 2;
	description.MaxPipelines = 1;
	description.MaxTextureBytes = 4096;
	sgre::render::SpriteGraphicsTargets graphics(description, shader, 2);
	const auto renderer = graphics.PrimaryTarget();
	static_cast<void>(graphics.SelectTarget(renderer));
	sgre::scene::GraphicsModelStack models(8);
	models.Reset();
	sgre::scene::LayerCameraFrameState state;
	state.Viewport = {8, 6};
	state.RawScissor = {.1f, .2f, 7.8f, 5.9f};
	state.RoundedScissor = {0, 0, 8, 6};
	state.AppliedRoundedScissor = {1, 1, 7, 5};
	sgre::render::SpriteGraphicsCamera camera(graphics, models, state);
	RequireFailure([&] {
		static_cast<void>(camera.OrdinaryFrameGlobals({}));
	},
	    "projection reset");
	camera.ResetDefaultCamera();
	sgre::render::SpriteGraphicsCamera unshared(graphics, models);
	unshared.ResetDefaultCamera();
	RequireFailure([&] {
		static_cast<void>(unshared.OrdinaryFrameGlobals({}));
	},
	    "shared camera/clip");

	sgre::resource::RgbaImage image;
	image.Width = 2;
	image.Height = 1;
	image.Pixels.assign(8, 255);
	const auto texture = renderer->UploadTexture(sgre::resource::PixelFormat::Bc7, image);
	sgre::render::SpriteFrameEncoder encoder(*renderer);
	encoder.Begin({}, sgre::render::SpritePassLoadAction::Clear);
	const auto metadata = encoder.PreparedTexture(texture);
	encoder.Abort();
	const std::array<sgre::resource::MotionPoint, 1> sizes{{{static_cast<float>(metadata.Width), static_cast<float>(metadata.Height)}}};
	const auto initial = camera.OrdinaryFrameGlobals(sizes);
	Require(initial.TextureSizes.data() == sizes.data() && initial.TextureSizes.size() == 1 && sizes[0].X == 2 && sizes[0].Y == 1, "globals lost actual prepared texture dimensions or their borrow");
	Require(initial.View.Coefficients == state.View.Coefficients && initial.Model.Coefficients == models.Current().Coefficients && initial.Viewport.Width == 8 && initial.Viewport.Height == 6, "globals did not consume the actual view/model/viewport");
	RequireBounds(initial.InitialScissor, state.RawScissor);
	RequireBounds(initial.AppliedRoundedScissor, state.AppliedRoundedScissor);
	Require(initial.Queue.empty() && initial.ClipQueue.empty() && initial.Groups.empty() && initial.Nodes.empty() && initial.MaskSettings.empty() && initial.Candidates.empty() && !initial.MaskRegionClipping, "ordinary query invented scene or mask storage");

	std::size_t visits = 0;
	sgre::scene::ParallelTaskSet inner(1);
	sgre::scene::ParallelTaskSet outer(1);
	sgre::scene::LayerCameraDrawScope scope(models, state, [](const auto&) {
		throw std::runtime_error("disabled clips cannot invent a physical scissor submission");
	});
	sgre::scene::LayerCameraParameters nested;
	nested.Offset = {3, 0};
	sgre::scene::ParallelTaskCallbacks leaf;
	leaf.Draw = [&](auto, auto argument) {
		Require(argument == 91 && models.Depth() == 3, "genuine nested camera traversal did not reach its actual model");
		const auto actual = camera.OrdinaryFrameGlobals(sizes);
		Require(actual.Model.Coefficients == models.Current().Coefficients && actual.Model.Coefficients[3] == -5, "motion globals did not consume the composed live model");
		++visits;
	};
	static_cast<void>(inner.Append(std::move(leaf)));
	sgre::scene::ParallelTaskCallbacks branch;
	branch.Draw = [&](auto, auto argument) {
		scope.Draw(nested, inner, argument);
	};
	static_cast<void>(outer.Append(std::move(branch)));
	sgre::scene::LayerCameraParameters parent;
	parent.Offset = {2, 0};
	scope.Draw(parent, outer, 91);
	Require(visits == 1 && models.Depth() == 1 && initial.Model.Coefficients == models.Current().Coefficients, "live globals or surrounding model restoration changed");
	outer.Clear();
	inner.Clear();

	state.View.Coefficients[0] *= 2;
	const auto changed = camera.OrdinaryFrameGlobals(sizes);
	Require(changed.View.Coefficients[0] == .5f && initial.View.Coefficients[0] == .25f, "value snapshot followed later shared-view mutation");
	state.Viewport.Width = 7;
	RequireFailure([&] {
		static_cast<void>(camera.OrdinaryFrameGlobals(sizes));
	},
	    "viewport differs");
	state.Viewport.Width = 8;
	const std::array<sgre::resource::MotionPoint, 1> bad_size{{{0, 1}}};
	RequireFailure([&] {
		static_cast<void>(camera.OrdinaryFrameGlobals(bad_size));
	},
	    "texture dimensions");
	const std::array<sgre::resource::MotionPoint, 2> sparse_sizes{{{}, sizes[0]}};
	RequireFailure([&] {
		static_cast<void>(camera.OrdinaryFrameGlobals(sparse_sizes));
	},
	    "texture dimensions");
	const auto sparse = camera.SceneFrameGlobals(sparse_sizes);
	Require(sparse.TextureSizes.data() == sparse_sizes.data() && sparse.TextureSizes.size() == 2 && sparse.TextureSizes[0].X == 0 && sparse.TextureSizes[0].Y == 0 && sparse.TextureSizes[1].X == 2 && sparse.TextureSizes[1].Y == 1, "scene globals must preserve actual sparse slot indices and dimension borrows");
	Require(sparse.View.Coefficients == changed.View.Coefficients && sparse.Model.Coefficients == changed.Model.Coefficients && sparse.Viewport.Width == changed.Viewport.Width && sparse.Viewport.Height == changed.Viewport.Height, "sparse scene globals must use the same actual camera/model/viewport");
	RequireBounds(sparse.InitialScissor, changed.InitialScissor);
	RequireBounds(sparse.AppliedRoundedScissor, changed.AppliedRoundedScissor);
	for (const auto size : {sgre::resource::MotionPoint{0, 1}, sgre::resource::MotionPoint{1, 0}, sgre::resource::MotionPoint{-1, -1}, sgre::resource::MotionPoint{std::numeric_limits<float>::quiet_NaN(), 0}, sgre::resource::MotionPoint{0, std::numeric_limits<float>::infinity()}, sgre::resource::MotionPoint{16385, 1}}) {
		const std::array<sgre::resource::MotionPoint, 1> invalid{{size}};
		RequireFailure([&] {
			static_cast<void>(camera.SceneFrameGlobals(invalid));
		},
		    "texture dimensions");
	}
	state.RawScissor.MaximumX = std::numeric_limits<float>::quiet_NaN();
	RequireFailure([&] {
		static_cast<void>(camera.OrdinaryFrameGlobals(sizes));
	},
	    "finite actual scissor");
	state.RawScissor = initial.InitialScissor;
	std::exception_ptr thread_error;
	std::thread foreign([&] {
		try {
			RequireFailure([&] {
				static_cast<void>(camera.OrdinaryFrameGlobals(sizes));
			},
			    "owner thread");
		} catch (...) {
			thread_error = std::current_exception();
		}
	});
	foreign.join();
	if (thread_error) {
		std::rethrow_exception(thread_error);
	}

	const auto other = graphics.AllocateTarget(description);
	static_cast<void>(graphics.SelectTarget(other));
	RequireFailure([&] {
		static_cast<void>(camera.OrdinaryFrameGlobals(sizes));
	},
	    "projection reset");
	static_cast<void>(graphics.SelectTarget(renderer));
	camera.ResetDefaultCamera();
	renderer->ReleaseTexture(texture);
}

int main(int argc, char** argv) {
	try {
		Require(argc == 2, "camera globals fixture requires the sprite shader path");
		TestActualOwners(argv[1]);
		std::cout << "PASS actual shared camera/model/clip globals, real texture metadata borrow, nested model restoration and rejected stale owners; zero submitted frames\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
