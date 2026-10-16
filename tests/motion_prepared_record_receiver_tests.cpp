#include "sgre/render/motion_sprite_record_receiver.hpp"

#include "sgre/scene/graphics_model_stack.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

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

	throw std::runtime_error("missing actual prepared-frame receiver rejection: " + std::string(message));
}

// This opaque block and all sixteen decoded pixels are the independently checked mode0 image fixture.
static constexpr std::array<std::uint8_t, 16> kBlock{0x6d, 0x66, 0x4d, 0xf0, 0x44, 0x42, 0xa6, 0x8a, 0xde, 0x19, 0xa0, 0x53, 0xe7, 0xe9, 0xc7, 0x3f};
static constexpr std::array<std::array<std::uint8_t, 3>, 16> kPixels{{{49, 115, 82}, {49, 56, 82}, {75, 40, 217}, {61, 38, 222}, {49, 92, 82}, {49, 33, 82}, {90, 42, 212}, {132, 49, 198}, {134, 23, 171}, {111, 18, 222}, {99, 16, 247}, {169, 31, 91}, {111, 18, 222}, {99, 16, 247}, {99, 16, 247}, {181, 33, 66}}};

static sgre::render::SpritePassDescription Description() {
	sgre::render::SpritePassDescription description;
	description.Width = 8;
	description.Height = 8;
	description.MaxVertices = 128;
	description.MaxIndices = 256;
	description.MaxDraws = 32;
	description.MaxTextures = 16;
	description.MaxPipelines = 64;
	description.MaxTextureBytes = 64 * 1024;

	return description;
}

static sgre::resource::ImageTexture TextureDescription(sgre::resource::PixelFormat format, std::uint32_t offset) {
	sgre::resource::ImageTexture description;
	description.SerializedValueOffset = offset;
	description.Format = format;
	description.Width = 4;
	description.Height = 4;
	description.TruncatedWidth = 4;
	description.TruncatedHeight = 4;
	description.PixelRange.Size = 16;

	return description;
}

static sgre::scene::LayerCameraFrameState CameraState(const sgre::render::SpriteGraphicsTargets& graphics) {
	const auto layout = graphics.CurrentLayout();
	sgre::scene::LayerCameraFrameState state;
	state.View = layout.Projection;
	state.Viewport.Width = 8;
	state.Viewport.Height = 8;
	state.RawScissor = layout.FullScissor;
	state.RoundedScissor = layout.FullScissor;
	state.AppliedRoundedScissor = layout.FullScissor;

	return state;
}

static std::array<sgre::resource::MotionPoint, 4> Quad(float left, float top, float right, float bottom) {
	return {{{left, top}, {right, top}, {left, bottom}, {right, bottom}}};
}

struct PublishedFrameFixture {
	std::array<std::array<sgre::resource::MotionPoint, 4>, 5> Positions;
	std::array<sgre::scene::MotionClipGeometry, 5> Geometry;
	std::array<sgre::scene::MotionClipCommand, 5> Descriptors;
	std::array<sgre::scene::MotionPreparedFrameCommand, 5> Commands;
	std::array<sgre::scene::MotionClipNode, 1> Nodes;
	std::array<sgre::scene::MotionMaskSettings, 1> Settings;
	std::array<sgre::scene::MotionFrameGroup, 1> Groups;
	std::array<sgre::scene::MotionFrameCandidateBinding, 1> Candidates;
	std::array<sgre::resource::MotionPoint, 2> Sizes{{{4, 4}, {4, 4}}};
	std::array<sgre::resource::MotionPoint, 128> BatchPositions;
	std::array<sgre::resource::MotionPoint, 128> BatchUvs;
	std::array<std::uint32_t, 128> BatchColors;
	std::array<std::uint32_t, 256> BatchIndices;
	std::array<sgre::scene::MotionMaskOperation, 16> MaskOperations;
	std::array<sgre::scene::MotionMaskedBlendPass, 16> BlendPasses;
	sgre::scene::MotionPreparedFrame Frame;
	sgre::scene::MotionFrameScratch Scratch;

	PublishedFrameFixture(const sgre::render::SpriteGraphicsCamera& camera, std::uint32_t blend) {
		Positions[0] = Quad(-4, -4, 4, 4);
		Positions[1] = Quad(-3, -3, 1, 1);
		Positions[2] = Quad(-1, 1, 1, 3);
		Positions[3] = Quad(1, -1, 2, 0);
		Positions[4] = Quad(-4, -4, 0, 4);
		for (std::size_t index = 0; index < Commands.size(); ++index) {
			Geometry[index].Positions = Positions[index];
			Descriptors[index].Geometry = &Geometry[index];
			Descriptors[index].GeometryCount = 4;
			Commands[index].Clip = &Descriptors[index];
			Commands[index].Texture = index == 4 ? 1 : 0;
			Commands[index].PixelUvs.X = .5f;
			Commands[index].PixelUvs.Y = .5f;
			Commands[index].Colors.fill(0x10203040);
			Commands[index].Key = 4;
			if (index > 0 && index < 4) {
				Descriptors[index].ClipGroup = 1;
				Commands[index].BlendMode = blend;
				Commands[index].Key = static_cast<std::uint32_t>(index + 4);
			}
		}
		Commands[0].PixelUvs.X = 0;
		Commands[0].PixelUvs.Y = 0;
		Commands[0].PixelUvs.Width = 4;
		Commands[0].PixelUvs.Height = 4;
		Nodes[0].Command.Hidden = true;
		Nodes[0].Members = std::span<const sgre::scene::MotionClipCommand>(Descriptors).subspan(4, 1);
		Nodes[0].ModeFlags = 5;
		Groups[0].Identity = 1;
		Candidates[0].Descriptor = &Descriptors[4];
		Candidates[0].Command = &Commands[4];
		Frame = camera.OrdinaryFrameGlobals(Sizes);
		Frame.Queue = std::span<const sgre::scene::MotionPreparedFrameCommand>(Commands).first(4);
		Frame.ClipQueue = std::span<const sgre::scene::MotionClipCommand>(Descriptors).first(4);
		Frame.Nodes = Nodes;
		Frame.MaskSettings = Settings;
		Frame.Groups = Groups;
		Frame.Candidates = Candidates;
		Scratch.Batch.Positions = BatchPositions;
		Scratch.Batch.Uvs = BatchUvs;
		Scratch.Batch.Colors = BatchColors;
		Scratch.Batch.Indices = BatchIndices;
		Scratch.MaskOperations = MaskOperations;
		Scratch.BlendPasses = BlendPasses;
	}

	PublishedFrameFixture(const PublishedFrameFixture&) = delete;
	PublishedFrameFixture& operator=(const PublishedFrameFixture&) = delete;
};

static void RequirePixels(const sgre::render::ColorPassPixels& pixels, bool overlay) {
	Require(pixels.Width == 8 && pixels.Height == 8 && pixels.Rgba.size() == 256, "actual prepared receiver capture extent differs");
	for (std::size_t y = 0; y < 8; ++y) {
		for (std::size_t x = 0; x < 8; ++x) {
			const auto& destination = kPixels[(y / 2) * 4 + x / 2];
			const bool copied = (x >= 1 && x < 4 && y >= 1 && y < 5) || (x == 3 && y >= 5 && y < 7);
			for (std::size_t channel = 0; channel < 4; ++channel) {
				int expected = channel == 3 ? 255 : destination[channel];
				if (copied && channel < 3) {
					const double source = static_cast<double>(kPixels[0][channel]) / 255;
					const double dest = static_cast<double>(destination[channel]) / 255;
					const double combined = overlay ? (dest < .5 ? (dest * source) * 2 : 1 - ((1 - dest) * (1 - source)) * 2) : std::min(dest + source, 1.0);
					expected = static_cast<int>(std::round((dest + (128.0 / 255) * (combined - dest)) * 255));
				}
				const auto actual = static_cast<int>(pixels.Rgba[(y * 8 + x) * 4 + channel]);
				if (std::abs(actual - expected) > 2) {
					throw std::runtime_error("prepared scene receiver copied/masked pixel differs at " + std::to_string(x) + "," + std::to_string(y) + " channel " + std::to_string(channel) + " expected " + std::to_string(expected) + " actual " + std::to_string(actual));
				}
			}
		}
	}
}

struct FrameCleanup {
	sgre::render::SpriteRenderRecords& Records;
	sgre::render::MotionSpriteRecordReceiver& Receiver;
	sgre::render::FrameHostHeap& Heap;
	bool HeapBegun = false;

	~FrameCleanup() noexcept {
		try {
			if (Records.IsActive()) {
				Records.AbortFrame();
			}
			Receiver.ReleaseFrameInputs();
			if (HeapBegun) {
				Heap.End();
			}
		} catch (...) {
			std::terminate();
		}
	}
};

static void TestPreparedMaskedCopies(const std::filesystem::path& shader, std::uint32_t blend) {
	sgre::render::SpriteGraphicsTargets graphics(Description(), shader);
	const auto renderer = graphics.PrimaryTarget();
	static_cast<void>(graphics.SelectTarget(renderer));
	graphics.SetMaskMode(1);
	sgre::scene::GraphicsModelStack models(4);
	models.Reset();
	auto camera_state = CameraState(graphics);
	sgre::render::SpriteGraphicsCamera camera(graphics, models, camera_state);
	camera.ResetDefaultCamera();
	sgre::render::RenderFrameStatistics statistics({}, {});
	sgre::render::SpriteRenderRecords records(*renderer, camera, statistics);
	sgre::render::FrameHostHeap heap(2);
	sgre::scene::TextureCache cache([&](auto format, const auto& image) {
		return renderer->UploadTexture(format, image);
	}, [&](auto handle) {
		renderer->ReleaseTexture(handle);
	});
	const auto document = cache.AllocateDocumentIdentity();
	const auto rgba = cache.Acquire(document, TextureDescription(sgre::resource::PixelFormat::Bc7, 16), kBlock);
	std::array<std::uint8_t, 16> half;
	half.fill(128);
	const auto alpha = cache.Acquire(document, TextureDescription(sgre::resource::PixelFormat::Alpha8, 32), half);
	sgre::render::MotionSpriteRecordReceiver receiver(records, *renderer, graphics, cache, heap, 2);
	receiver.BindTexture(0, rgba);
	receiver.BindTexture(1, alpha);
	PublishedFrameFixture publication(camera, blend);
	FrameCleanup cleanup{records, receiver, heap};
	for (std::size_t iteration = 0; iteration < 2; ++iteration) {
		receiver.PrepareFrame(publication.Frame, publication.Scratch);
		RequireFailure([&] {
			receiver.UnbindTexture(0);
		}, "outer-frame quiescence");
		heap.Begin();
		cleanup.HeapBegun = true;
		records.BeginFrame({}, sgre::render::SpritePassLoadAction::Clear);
		receiver.DrawPreparedFrame(publication.Scratch);
		Require(statistics.Current().DrawOperations == 5 && statistics.Current().SubmittedElements == 20, "prepared scene receiver did not submit the exact real background/mask/three copied packets");
		RequireFailure([&] {
			receiver.DrawPreparedFrame(publication.Scratch);
		}, "unconsumed publication");
		const auto submitted = records.EndFrame();
		RequirePixels(renderer->Capture(submitted), blend == 0x112);
		receiver.ReleaseFrameInputs();
		heap.End();
		cleanup.HeapBegun = false;
	}

	// A live change is rejected before its changed packet; the already-mutated actual frame is discarded.
	receiver.PrepareFrame(publication.Frame, publication.Scratch);
	publication.Commands[1].Colors[0] ^= 1;
	heap.Begin();
	cleanup.HeapBegun = true;
	records.BeginFrame({}, sgre::render::SpritePassLoadAction::Clear);
	RequireFailure([&] {
		receiver.DrawPreparedFrame(publication.Scratch);
	}, "prepared event/geometry stream");
	Require(!records.IsActive(), "changed published geometry must abort its actual outer encoding");
	receiver.ReleaseFrameInputs();
	heap.End();
	cleanup.HeapBegun = false;
	publication.Commands[1].Colors[0] ^= 1;

	receiver.PrepareFrame(publication.Frame, publication.Scratch);
	heap.Begin();
	cleanup.HeapBegun = true;
	records.BeginFrame({}, sgre::render::SpritePassLoadAction::Clear);
	receiver.DrawPreparedFrame(publication.Scratch);
	RequirePixels(renderer->Capture(records.EndFrame()), blend == 0x112);
	receiver.ReleaseFrameInputs();
	heap.End();
	cleanup.HeapBegun = false;
}

int main(int argc, char** argv) {
	try {
		Require(argc == 2, "usage: motion_prepared_record_receiver_tests shader-source");
		TestPreparedMaskedCopies(argv[1], 0x110);
		TestPreparedMaskedCopies(argv[1], 0x112);
		std::cout << "actual prepared Motion frame live mask/copy packets, key5/6/7, bounded region replacement/retention, 384 pixels and mutation-abort replay passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
