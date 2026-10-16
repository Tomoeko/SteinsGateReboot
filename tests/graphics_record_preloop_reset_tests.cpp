#include "sgre/render/sprite_render_records.hpp"

#include "sgre/render/sprite_graphics_targets.hpp"

#include <array>
#include <exception>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

static void Require(bool condition, const char* message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

template <typename Operation>
static void RequireFailure(Operation operation, const char* fragment) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string(error.what()).find(fragment) != std::string::npos, error.what());

		return;
	}

	throw std::runtime_error("missing real record Pre phase rejection");
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

static void TestPreparedSequence(const std::filesystem::path& shader) {
	sgre::render::SpriteGraphicsTargets graphics(Description(), shader, 2);
	const auto renderer = graphics.PrimaryTarget();
	const auto other = graphics.AllocateTarget(Description());
	sgre::scene::GraphicsModelStack models(8);
	sgre::render::SpriteGraphicsCamera camera(graphics, models);
	sgre::render::RenderFrameCounters incoming;
	incoming.DrawOperations = 29;
	incoming.SubmittedElements = 0xffffffff;
	sgre::render::RenderFrameCounters previous;
	previous.DrawOperations = 3;
	previous.SubmittedElements = 7;
	sgre::render::RenderFrameStatistics statistics(incoming, previous);
	sgre::render::SpriteRenderRecords records(*renderer, camera, statistics);
	RequireFailure([&] {
		records.BeginPreparedFrame({}, sgre::render::SpritePassLoadAction::Clear);
	},
	    "exact owner's completed Pre reset");
	Require(statistics.Current() == incoming && statistics.Previous() == previous, "unprepared Begin changed actual counters");

	// Actual counters reset before selecting a target or creating a default model/camera slot.
	records.ResetFrameState();
	Require(!records.IsActive() && models.Depth() == 0 && statistics.Current() == sgre::render::RenderFrameCounters{} && statistics.Previous() == incoming, "actual reset phase required a nominal target or fabricated camera");
	RequireFailure([&] {
		records.ResetFrameState();
	},
	    "without a pending reset");
	RequireFailure([&] {
		records.BeginFrame({}, sgre::render::SpritePassLoadAction::Clear);
	},
	    "quiescent record/encoder owner");
	RequireFailure([&] {
		records.BeginPreparedFrame({}, sgre::render::SpritePassLoadAction::Clear);
	},
	    "current target is absent");
	Require(statistics.Previous() == incoming, "correctable target rejection consumed the CPU reset twice");

	sgre::render::FrameHostHeap heap(2);
	heap.Begin();
	static_cast<void>(graphics.SelectTarget(other));
	models.Reset();
	camera.ResetDefaultCamera();
	RequireFailure([&] {
		records.BeginPreparedFrame({}, sgre::render::SpritePassLoadAction::Clear);
	},
	    "another actual render target");
	static_cast<void>(graphics.SelectTarget(renderer));
	camera.ResetDefaultCamera();

	sgre::resource::RgbaImage image;
	image.Width = 1;
	image.Height = 1;
	image.Pixels = {255, 0, 0, 255};
	const auto texture = renderer->UploadTexture(sgre::resource::PixelFormat::Bc7, image);
	sgre::scene::MotionSpriteMaterial material;
	material.BlendMode = 0;
	sgre::scene::SpritePipelineOptions options;
	const auto pipeline = renderer->PreparePipeline(sgre::scene::SelectSpritePipeline(material, options));
	std::array<sgre::scene::MotionSpriteVertex, 4> vertices;
	vertices[0].Position = {-2, -2};
	vertices[1].Position = {2, -2};
	vertices[2].Position = {-2, 2};
	vertices[3].Position = {2, 2};
	for (auto& vertex : vertices) {
		vertex.Color = 0xffffffff;
	}
	const std::array<std::uint32_t, 4> indices{0, 2, 1, 3};
	sgre::render::SpriteHostInputs inputs(heap, vertices, indices);
	records.BeginPreparedFrame({}, sgre::render::SpritePassLoadAction::Clear);
	Require(statistics.Previous() == incoming, "actual Metal Begin reset preceding counters twice");
	RequireFailure([&] {
		records.ResetFrameState();
	},
	    "inactive balanced owner");
	RequireFailure([&] {
		records.BeginPreparedFrame({}, sgre::render::SpritePassLoadAction::Clear);
	},
	    "completed Pre reset");
	records.BeginRecord();
	records.SetFilterColor(0xffffffff);
	records.Append(texture, pipeline, inputs, 0);
	records.EndRecord();
	const auto submission = records.EndFrame();
	heap.End();
	sgre::render::RenderFrameCounters completed;
	completed.DrawOperations = 1;
	completed.SubmittedElements = 4;
	Require(statistics.Current() == completed && statistics.Previous() == incoming, "physical packet statistics did not survive the prepared sequence");
	const auto pixels = renderer->Capture(submission);
	Require(pixels.Rgba.size() == 64, "prepared frame lacked its actual target");
	for (std::size_t pixel = 0; pixel < 16; ++pixel) {
		Require(pixels.Rgba[pixel * 4] == 255 && pixels.Rgba[pixel * 4 + 1] == 0 && pixels.Rgba[pixel * 4 + 2] == 0 && pixels.Rgba[pixel * 4 + 3] == 255, "prepared frame actual copied red pixels differ");
	}

	records.ResetFrameState();
	Require(statistics.Previous() == completed, "next actual reset lost the completed packet counters");
	std::exception_ptr foreign_error;
	std::thread foreign([&] {
		try {
			records.ResetFrameState();
		} catch (...) {
			foreign_error = std::current_exception();
		}
	});
	foreign.join();
	Require(foreign_error != nullptr, "foreign thread changed actual record phase");
	RequireFailure([&] {
		std::rethrow_exception(foreign_error);
	},
	    "serialized owner thread");
	Require(statistics.Previous() == completed, "rejected foreign reset changed actual counters");

	// Actual Abort discards an unconsumed reset; this is authored failure cleanup, not native recovery.
	records.AbortFrame();
	RequireFailure([&] {
		records.BeginPreparedFrame({}, sgre::render::SpritePassLoadAction::Clear);
	},
	    "completed Pre reset");
	records.BeginFrame({}, sgre::render::SpritePassLoadAction::Clear);
	records.BeginRecord();
	Require(records.CurrentRecord().BlendParameter.has_value(), "frame reset erased a physical record's native retained blend parameter");
	records.EndRecord();
	records.AbortFrame();
	Require(!records.IsActive(), "diagnostic compatibility Abort retained a live encoder");
	renderer->ReleaseTexture(texture);
}

int main(int argc, char** argv) {
	try {
		Require(argc == 2, "record Pre proof requires the actual shader source");
		TestPreparedSequence(argv[1]);
		std::cout << "PASS genuine record counter reset before target/pool/camera, single consumption, physical retained record, real16 red pixels; one submitted authored frame\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
