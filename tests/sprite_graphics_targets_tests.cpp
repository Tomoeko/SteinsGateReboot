#include "sgre/render/sprite_graphics_targets.hpp"
#include "sgre/scene/motion_copied_region_owner.hpp"

#include <bit>
#include <fstream>
#include <thread>

#include <array>
#include <filesystem>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>

static void Require(bool condition, const char* message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

static void RequireFailure(const auto& operation) {
	try {
		operation();
	} catch (const std::runtime_error&) {
		return;
	}

	throw std::runtime_error("expected invalid shared binding rejection");
}

static sgre::render::SpritePassDescription Description(std::uint32_t size) {
	sgre::render::SpritePassDescription result;
	result.Width = size;
	result.Height = size;
	result.MaxVertices = 4;
	result.MaxIndices = 4;
	result.MaxDraws = 1;
	result.MaxTextures = 4;
	result.MaxPipelines = 2;
	result.MaxTextureBytes = 4096;

	return result;
}

static std::unique_ptr<sgre::scene::MutableTextureOwner> Texture(sgre::scene::MutableTextureBackend& backend, std::array<std::byte, 4> pixel) {
	sgre::scene::MutableTextureSpec spec;
	spec.Storage = sgre::scene::MutableTextureStorage::GpuOnly;
	spec.Description.Width = 1;
	spec.Description.Height = 1;
	sgre::scene::MutableTextureInitialData initial;
	initial.Bytes = pixel;
	initial.RowPitch = 4;

	return std::make_unique<sgre::scene::MutableTextureOwner>(backend, spec, initial);
}

static sgre::render::SpritePipelineHandle Pipeline(sgre::render::SpritePassRenderer& renderer) {
	sgre::scene::MotionSpriteMaterial material;
	material.TextureFormat = sgre::resource::PixelFormat::Bc7;
	sgre::scene::SpritePipelineOptions options;
	options.EnableBlending = false;

	return renderer.PreparePipeline(sgre::scene::SelectSpritePipeline(material, options));
}

static void DrawBound(sgre::render::SpriteGraphicsTargets& graphics, sgre::render::SpritePipelineHandle pipeline, std::array<std::uint8_t, 4> pixel) {
	const auto target = graphics.CurrentTarget();
	auto& renderer = *target;
	std::array<sgre::scene::MotionSpriteVertex, 4> vertices;
	vertices[0].Position = {-1, 1};
	vertices[1].Position = {1, 1};
	vertices[2].Position = {-1, -1};
	vertices[3].Position = {1, -1};
	for (auto& vertex : vertices) {
		vertex.Color = 0xffffffff;
		vertex.Uv = {0.5f, 0.5f};
	}
	const std::array<std::uint32_t, 4> indices{0, 2, 1, 3};
	sgre::render::SpritePassDraw draw;
	draw.Texture = renderer.MutableTextureBinding(0, sgre::scene::MutableTextureStage::Pixel);
	draw.Pipeline = pipeline;
	draw.Vertices = vertices;
	draw.Indices = indices;
	const std::array<float, 16> projection{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0.5f, 0, 0, 0, 1};
	const auto submission = graphics.Submit(std::span(&draw, 1), projection);
	Require(submission.Owner == target.get(), "submitted identity is not tagged with the actual selected renderer");
	const auto capture = renderer.Capture(submission.Identity);
	for (std::size_t offset = 0; offset < capture.Rgba.size(); offset += 4) {
		for (std::size_t channel = 0; channel < pixel.size(); ++channel) {
			Require(capture.Rgba[offset + channel] == pixel[channel], "another target's actual shader binding did not reach the Metal pixels");
		}
	}
}

static std::uint32_t ReadWord(std::ifstream& input) {
	std::array<unsigned char, 4> bytes{};
	input.read(reinterpret_cast<char*>(bytes.data()), 4);
	Require(input.good(), "native selector fixture is truncated");

	return static_cast<std::uint32_t>(bytes[0]) | (static_cast<std::uint32_t>(bytes[1]) << 8) | (static_cast<std::uint32_t>(bytes[2]) << 16) | (static_cast<std::uint32_t>(bytes[3]) << 24);
}

static void ReplayNative(const std::filesystem::path& shader, const std::filesystem::path& fixture) {
	auto description = Description(3);
	description.Height = 5;
	sgre::render::SpriteGraphicsTargets graphics(description, shader, 2);
	description.Width = 7;
	description.Height = 9;
	std::array<std::shared_ptr<sgre::render::SpritePassRenderer>, 3> targets;
	targets[1] = graphics.PrimaryTarget();
	targets[2] = graphics.AllocateTarget(description);
	std::ifstream input(fixture, std::ios::binary);
	Require(input.good() && ReadWord(input) == 0x314f5447 && ReadWord(input) == 8, "wrong native mono-selector fixture");
	for (std::size_t step = 0; step < 8; ++step) {
		const auto selected = ReadWord(input);
		const auto previous = ReadWord(input);
		Require(selected < targets.size() && previous < targets.size(), "native target identity exceeds the admitted fixture");
		const auto old = graphics.SelectTarget(targets[selected]);
		Require(old.lock() == targets[previous], "selected owner did not return the actual previous target");
		std::array<std::uint32_t, 10> expected;
		for (auto& value : expected) {
			value = ReadWord(input);
		}
		if (selected == 0) {
			RequireFailure([&] {
				static_cast<void>(graphics.CurrentLayout());
			});
			continue;
		}

		Require(graphics.CurrentTarget() == targets[selected], "graphics current target identity differs from native");
		const auto layout = graphics.CurrentLayout();
		const std::array<float, 10> actual{layout.Viewport.X, layout.Viewport.Y, layout.Viewport.Width, layout.Viewport.Height, layout.Viewport.MinimumDepth, layout.Viewport.MaximumDepth, layout.FullScissor.MinimumX, layout.FullScissor.MinimumY, layout.FullScissor.MaximumX, layout.FullScissor.MaximumY};
		for (std::size_t word = 0; word < actual.size(); ++word) {
			Require(std::bit_cast<std::uint32_t>(actual[word]) == expected[word], "allocated target layout differs from unchanged native selector words");
		}
	}
	Require(input.peek() == std::char_traits<char>::eof(), "native target fixture has trailing bytes");
}

static void TestActualSubmission(const std::filesystem::path& shader) {
	sgre::render::SpriteGraphicsTargets graphics(Description(4), shader, 2);
	auto root = graphics.PrimaryTarget();
	auto child = graphics.AllocateTarget(Description(2));
	sgre::render::SpriteGraphicsTargets foreign(Description(2), shader);
	auto backend = root->CreateMutableTextureBackend();
	auto red = Texture(*backend, {std::byte{255}, std::byte{0}, std::byte{0}, std::byte{255}});
	const auto pipeline = Pipeline(*root);
	RequireFailure([&] {
		static_cast<void>(graphics.Submit({}, {}));
	});
	static_cast<void>(graphics.SelectTarget(root));
	red->Bind(0, sgre::scene::MutableTextureStage::Pixel);
	const auto binding = root->MutableTextureBinding(0, sgre::scene::MutableTextureStage::Pixel);
	static_cast<void>(graphics.SelectTarget(root));
	Require(child->MutableTextureBinding(0, sgre::scene::MutableTextureStage::Pixel) == binding, "same native target selection must preserve shader slots");
	DrawBound(graphics, pipeline, {255, 0, 0, 255});
	RequireFailure([&] {
		static_cast<void>(graphics.SelectTarget(foreign.PrimaryTarget()));
	});
	Require(graphics.CurrentTarget() == root, "foreign target mutated the real selected state");
	const auto previous = graphics.SelectTarget(child);
	Require(previous.lock() == root, "child target lost its actual containing target");
	RequireFailure([&] {
		static_cast<void>(root->MutableTextureBinding(0, sgre::scene::MutableTextureStage::Pixel));
	});
	red->Bind(0, sgre::scene::MutableTextureStage::Pixel);
	DrawBound(graphics, pipeline, {255, 0, 0, 255});
	static_cast<void>(graphics.SelectTarget({}));
	Require(root->MutableTextureBinding(0, sgre::scene::MutableTextureStage::Pixel) == binding, "native null target unbinding does not clear shader texture slots");
	static_cast<void>(graphics.SelectTarget(child));
	RequireFailure([&] {
		static_cast<void>(child->MutableTextureBinding(0, sgre::scene::MutableTextureStage::Pixel));
	});
	child.reset();
	RequireFailure([&] {
		static_cast<void>(graphics.CurrentTarget());
	});
	RequireFailure([&] {
		static_cast<void>(graphics.SelectTarget(root));
	});
	static_cast<void>(graphics.SelectTarget({}));
	static_cast<void>(graphics.SelectTarget(root));
	child = graphics.AllocateTarget(Description(2));
	RequireFailure([&] {
		static_cast<void>(graphics.AllocateTarget(Description(2)));
	});
	bool thread_rejected = false;
	std::thread thread([&] {
		try {
			static_cast<void>(graphics.SelectTarget(child));
		} catch (const std::runtime_error&) {
			thread_rejected = true;
		}
	});
	thread.join();
	Require(thread_rejected && graphics.CurrentTarget() == root, "foreign thread altered current target state");
}

static void TestActualCurrentCopy(const std::filesystem::path& shader) {
	auto graphics = std::make_unique<sgre::render::SpriteGraphicsTargets>(Description(2), shader, 2);
	auto root = graphics->PrimaryTarget();
	auto child = graphics->AllocateTarget(Description(4));
	auto backend = graphics->CreateMutableTextureBackend();
	auto& targets = dynamic_cast<sgre::scene::MotionCopiedTargetBackend&>(*backend);
	auto& copies = dynamic_cast<sgre::scene::MotionRegionCopyBackend&>(*backend);
	sgre::scene::MotionCopiedRegionOwner copied(*backend, targets, copies);
	sgre::scene::MotionCopyRegionPlan plan;
	plan.Action = sgre::scene::MotionCopyRegionAction::Copy;
	plan.Rectangle = {2, 1, 4, 3};
	RequireFailure([&] {
		static_cast<void>(copied.Apply(plan, 28));
	});

	const std::array<float, 16> projection{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0.5f, 0, 0, 0, 1};
	static_cast<void>(graphics->SelectTarget(root));
	static_cast<void>(graphics->Submit({}, projection, {1, 0, 0, 1}));
	RequireFailure([&] {
		static_cast<void>(copied.Apply(plan, 28));
	});
	static_cast<void>(graphics->SelectTarget(child));
	RequireFailure([&] {
		static_cast<void>(copied.Apply(plan, 28));
	});
	static_cast<void>(graphics->Submit({}, projection, {0, 0, 1, 1}));
	Require(copied.Apply(plan, 28), "actual child source copy did not reach reset phase");
	const auto pipeline = Pipeline(*root);
	static_cast<void>(graphics->SelectTarget(root));
	copied.SelectedTexture()->Bind(0, sgre::scene::MutableTextureStage::Pixel);
	DrawBound(*graphics, pipeline, {0, 0, 255, 255});

	plan.Rectangle = {0, 0, 2, 2};
	Require(copied.Apply(plan, 28), "actual current root source copy failed");
	copied.SelectedTexture()->Bind(0, sgre::scene::MutableTextureStage::Pixel);
	DrawBound(*graphics, pipeline, {0, 0, 255, 255});
	static_cast<void>(graphics->Submit({}, projection, {0, 1, 0, 1}));
	Require(copied.Apply(plan, 28), "reused actual root source copy failed");
	copied.SelectedTexture()->Bind(0, sgre::scene::MutableTextureStage::Pixel);
	DrawBound(*graphics, pipeline, {0, 255, 0, 255});

	static_cast<void>(graphics->SelectTarget(child));
	child.reset();
	RequireFailure([&] {
		static_cast<void>(copied.Apply(plan, 28));
	});
	static_cast<void>(graphics->SelectTarget({}));
	graphics.reset();
	RequireFailure([&] {
		static_cast<void>(copied.Apply(plan, 28));
	});
	Require(root->CanSubmit(), "external primary allocation did not remain live after graphics owner close");
}

int main(int argc, char** argv) {
	try {
		Require(argc == 3, "usage: sprite_graphics_targets_tests sprite_shader native_fixture");
		ReplayNative(argv[1], argv[2]);
		TestActualSubmission(argv[1]);
		TestActualCurrentCopy(argv[1]);
		std::cout << "actual graphics target identities, native mono layouts, shared slot transitions and selected Metal submissions pass\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}

	return 0;
}
