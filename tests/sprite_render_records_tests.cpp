#include "sgre/render/sprite_render_records.hpp"

#include "sgre/render/sprite_graphics_targets.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>

static_assert(!std::is_copy_constructible_v<sgre::render::SpriteRenderRecords>);
static_assert(!std::is_move_constructible_v<sgre::render::SpriteRenderRecords>);
static_assert(!std::is_copy_assignable_v<sgre::render::SpriteRenderRecords>);
static_assert(!std::is_move_assignable_v<sgre::render::SpriteRenderRecords>);

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

	throw std::runtime_error("missing actual render-record rejection");
}

static std::uint32_t ReadWord(std::istream& input) {
	std::uint32_t result = 0;
	for (std::size_t index = 0; index < 4; ++index) {
		const auto byte = input.get();
		Require(byte != std::char_traits<char>::eof(), "truncated native record fixture");
		result |= static_cast<std::uint32_t>(static_cast<unsigned char>(byte)) << (index * 8);
	}

	return result;
}

static sgre::render::SpritePassDescription Description(std::size_t draws = 32) {
	sgre::render::SpritePassDescription result;
	result.Width = 4;
	result.Height = 4;
	result.MaxVertices = draws * 4;
	result.MaxIndices = draws * 4;
	result.MaxDraws = draws;
	result.MaxTextures = 4;
	result.MaxPipelines = 24;
	result.MaxTextureBytes = 4096;

	return result;
}

static sgre::render::SpriteTextureHandle Upload(sgre::render::SpritePassRenderer& renderer, const std::array<std::uint8_t, 4>& color) {
	sgre::resource::RgbaImage image;
	image.Width = 1;
	image.Height = 1;
	image.Pixels.assign(color.begin(), color.end());

	return renderer.UploadTexture(sgre::resource::PixelFormat::Bc7, image);
}

static std::array<sgre::scene::MotionSpriteVertex, 4> Vertices() {
	std::array<sgre::scene::MotionSpriteVertex, 4> result;
	result[0].Position = {-2, -2};
	result[1].Position = {2, -2};
	result[2].Position = {-2, 2};
	result[3].Position = {2, 2};
	for (auto& vertex : result) {
		vertex.Uv = {0, 0};
		vertex.Color = 0xffffffff;
	}

	return result;
}

static constexpr std::array<std::uint32_t, 4> kIndices{0, 2, 1, 3};
static constexpr std::array<std::uint32_t, 12> kModes{0, 1, 2, 3, 4, 5, 16, 17, 18, 19, 20, 21};

static std::size_t PipelineIndex(std::uint32_t mode, std::uint32_t attributes) {
	for (std::size_t index = 0; index < kModes.size(); ++index) {
		if (kModes[index] == mode) {
			return index * 2 + ((attributes & 4) != 0 ? 1 : 0);
		}
	}

	throw std::runtime_error("native fixture blend mode is outside the admitted original corpus");
}

static std::array<sgre::render::SpritePipelineHandle, 24> Pipelines(sgre::render::SpritePassRenderer& renderer) {
	std::array<sgre::render::SpritePipelineHandle, 24> result;
	for (const auto mode : kModes) {
		for (const auto attributes : {0u, 4u}) {
			sgre::scene::MotionSpriteMaterial material;
			material.BlendMode = mode;
			material.TextureAttributes = attributes;
			sgre::scene::SpritePipelineOptions options;
			result[PipelineIndex(mode, attributes)] = renderer.PreparePipeline(sgre::scene::SelectSpritePipeline(material, options));
		}
	}

	return result;
}

static void CompareRecord(const sgre::render::SpriteRenderRecordScalars& actual, const std::array<std::uint32_t, 18>& expected, bool parameter_known) {
	Require(actual.NativeByte00 == expected[0] && actual.TextureAttributes == expected[4] && actual.NativeAttribute24 == expected[5] && actual.EnableBlending == (expected[7] != 0) && actual.BlendMode == expected[8] && actual.ColorFilter == expected[10] && actual.FilterColor == expected[11] && actual.UseIdentity == (expected[12] != 0) && actual.EnablePixelShader == (expected[13] != 0) && std::bit_cast<std::uint32_t>(actual.NativeScalar48) == expected[14] && actual.UniformArgb == expected[15] && actual.UseExplicitMatrix == (expected[16] != 0) && actual.WriteColor == (expected[17] != 0), "actual typed record scalar fields differ from original native production");
	Require(actual.BlendParameter.has_value() == parameter_known, "unwritten native blend parameter was fabricated or discarded");
	if (parameter_known) {
		Require(std::bit_cast<std::uint32_t>(*actual.BlendParameter) == expected[9], "native retained blend parameter/sign differs");
	}
	// Four native synthetic binding-presence words are observations, not portable handle/pointer inputs.
}

static void ReplayRecords(const std::filesystem::path& fixture, const std::filesystem::path& shader) {
	std::ifstream input(fixture, std::ios::binary);
	Require(input.good(), "cannot open original record fixture");
	std::array<char, 4> signature;
	input.read(signature.data(), static_cast<std::streamsize>(signature.size()));
	Require(signature == std::array<char, 4>{'G', 'R', 'O', '1'} && ReadWord(input) == 16, "wrong bounded native record fixture");
	sgre::render::SpriteGraphicsTargets graphics(Description(), shader, 2);
	const auto renderer = graphics.PrimaryTarget();
	static_cast<void>(graphics.SelectTarget(renderer));
	sgre::scene::GraphicsModelStack models(8);
	models.Reset();
	sgre::render::SpriteGraphicsCamera camera(graphics, models);
	camera.ResetDefaultCamera();
	const auto texture = Upload(*renderer, {255, 255, 255, 255});
	const auto pipelines = Pipelines(*renderer);
	std::size_t scalar_observations = 0;
	std::size_t actual_draws = 0;
	for (std::size_t frame = 0; frame < 16; ++frame) {
		sgre::render::RenderFrameCounters current;
		current.DrawOperations = ReadWord(input);
		current.SubmittedElements = ReadWord(input);
		sgre::render::RenderFrameCounters previous;
		previous.DrawOperations = ReadWord(input);
		previous.SubmittedElements = ReadWord(input);
		const auto step_count = ReadWord(input);
		Require(step_count <= 48, "native record work exceeds the measured bound");
		sgre::render::FrameHostHeap heap(1);
		heap.Begin();
		const auto vertices = Vertices();
		sgre::render::SpriteHostInputs inputs(heap, vertices, kIndices);
		sgre::render::RenderFrameStatistics statistics(current, previous);
		sgre::render::SpriteRenderRecords records(*renderer, camera, statistics);
		records.BeginFrame({}, sgre::render::SpritePassLoadAction::Clear);
		Require(statistics.Previous() == current && statistics.Current() == sgre::render::RenderFrameCounters{}, "containing Begin did not execute the actual native counter-reset semantics");
		std::uint32_t draws = 0;
		for (std::size_t step = 0; step < step_count; ++step) {
			std::array<std::uint32_t, 26> fields;
			for (auto& word : fields) {
				word = ReadWord(input);
			}
			if (fields[0] == 1) {
				records.BeginRecord();
			} else if (fields[0] == 2) {
				if (fields[1] != 0) {
					records.SetBlend(fields[2], std::bit_cast<float>(fields[3]));
				}
				records.SetFilterColor(fields[5]);
				records.Append(texture, pipelines[PipelineIndex(fields[2], fields[4])], inputs, fields[4]);
				++draws;
			} else {
				Require(fields[0] == 3, "unknown native record operation");
				records.EndRecord();
			}
			Require(records.RecordDepth() == fields[6] && fields[6] <= 7, "actual physical record depth differs from original");
			if (fields[6] != 0) {
				std::array<std::uint32_t, 18> expected;
				for (std::size_t index = 0; index < expected.size(); ++index) {
					expected[index] = fields[index + 8];
				}
				CompareRecord(records.CurrentRecord(), expected, fields[7] != 0);
				++scalar_observations;
			}
		}
		const auto submission = records.EndFrame();
		Require(!records.IsActive() && statistics.Current().DrawOperations == draws && statistics.Current().SubmittedElements == draws * 4, "actual successful draw/element counters differ from encoded host input counts");
		const auto pixels = renderer->Capture(submission);
		Require(pixels.Width == 4 && pixels.Height == 4, "actual native-case submission lacked its physical target");
		heap.End();
		actual_draws += draws;
	}
	Require(input.peek() == std::char_traits<char>::eof(), "extra native record fixture bytes");
	std::cout << "Native16 record cases: " << scalar_observations << " scalar observations, " << actual_draws << " real Metal draws\n";
}

static void TestLiveScopes(const std::filesystem::path& shader) {
	sgre::render::SpriteGraphicsTargets graphics(Description(), shader, 2);
	const auto renderer = graphics.PrimaryTarget();
	const auto other = graphics.AllocateTarget(Description());
	static_cast<void>(graphics.SelectTarget(renderer));
	sgre::scene::GraphicsModelStack models(8);
	models.Reset();
	sgre::render::SpriteGraphicsCamera camera(graphics, models);
	camera.ResetDefaultCamera();
	const auto red = Upload(*renderer, {255, 0, 0, 255});
	const auto blue = Upload(*renderer, {0, 0, 255, 255});
	const auto stale = Upload(*renderer, {255, 255, 255, 255});
	renderer->ReleaseTexture(stale);
	sgre::scene::MotionSpriteMaterial material;
	material.BlendMode = 0;
	sgre::scene::SpritePipelineOptions options;
	const auto pipeline = renderer->PreparePipeline(sgre::scene::SelectSpritePipeline(material, options));
	material.TextureAttributes = 4;
	const auto mismatch = renderer->PreparePipeline(sgre::scene::SelectSpritePipeline(material, options));
	material.TextureAttributes = 0;
	auto unsupported_sampler = sgre::scene::SelectSpritePipeline(material, options);
	unsupported_sampler.Sampler.AddressU = sgre::scene::TextureAddress::Repeat;
	unsupported_sampler.Sampler.NativeFilterCode = 1;
	const auto sampler_mismatch = renderer->PreparePipeline(unsupported_sampler);
	sgre::render::FrameHostHeap heap(1);
	const auto vertices = Vertices();
	sgre::render::SpriteHostInputs inputs(heap, vertices, kIndices);
	sgre::render::RenderFrameStatistics statistics({}, {});
	sgre::render::SpriteRenderRecords records(*renderer, camera, statistics);
	RequireFailure([&] {
		records.BeginRecord();
	},
	    "active contained encoder");
	records.BeginFrame({}, sgre::render::SpritePassLoadAction::Clear);
	records.BeginRecord();
	RequireFailure([&] {
		records.Append(red, pipeline, inputs, 0);
	},
	    "color initialization");
	records.SetFilterColor(0);
	RequireFailure([&] {
		records.Append(stale, pipeline, inputs, 0);
	},
	    "stale");
	RequireFailure([&] {
		records.Append(red, mismatch, inputs, 0);
	},
	    "actual prepared ordinary/copied pipeline");
	RequireFailure([&] {
		records.Append(red, sampler_mismatch, inputs, 0);
	},
	    "actual prepared ordinary/copied pipeline");
	auto invalid_model = models.Current();
	invalid_model.Coefficients[0] = std::numeric_limits<float>::quiet_NaN();
	models.SetCurrent(invalid_model);
	RequireFailure([&] {
		records.Append(red, pipeline, inputs, 0);
	},
	    "finite actual camera/model registers");
	camera.ResetDefaultCamera();
	Require(!records.CurrentRecord().BlendParameter && records.CurrentRecord().TextureAttributes == 0, "rejected actual binding mutated native packet fields");
	bool thread_rejected = false;
	std::thread foreign_thread([&] {
		try {
			records.SetBlend(0, 0);
		} catch (const std::runtime_error&) {
			thread_rejected = true;
		}
	});
	foreign_thread.join();
	Require(thread_rejected, "foreign thread mutated actual record owner");
	RequireFailure([&] {
		static_cast<void>(graphics.SelectTarget(other));
	},
	    "quiescent successful live encoding");
	Require(records.RecordDepth() == 1, "rejected live target selection changed physical record depth");
	records.Append(red, pipeline, inputs, 0);
	records.BeginRecord();
	models.Push();
	auto moved = models.Current();
	moved.Coefficients[3] = 2;
	models.SetCurrent(moved);
	records.Append(blue, pipeline, inputs, 0);
	moved.Coefficients[3] = 200;
	models.SetCurrent(moved);
	models.Pop();
	records.EndRecord();
	RequireFailure([&] {
		static_cast<void>(records.EndFrame());
	},
	    "unbalanced");
	records.EndRecord();
	const auto submission = records.EndFrame();
	const auto pixels = renderer->Capture(submission);
	for (std::size_t y = 0; y < 4; ++y) {
		for (std::size_t x = 0; x < 4; ++x) {
			const auto offset = (y * 4 + x) * 4;
			Require(pixels.Rgba[offset] == (x < 2 ? 255 : 0) && pixels.Rgba[offset + 1] == 0 && pixels.Rgba[offset + 2] == (x < 2 ? 0 : 255) && pixels.Rgba[offset + 3] == 255, "actual nested record texture/model state was not copied into its immediate draw");
		}
	}
	static_cast<void>(graphics.SelectTarget(other));
	camera.ResetDefaultCamera();
	const auto before_rejection = statistics.Current();
	RequireFailure([&] {
		records.BeginFrame({}, sgre::render::SpritePassLoadAction::Clear);
	},
	    "another actual render target");
	Require(!records.IsActive() && statistics.Current() == before_rejection, "wrong-target frame admission consumed actual counters or an encoder slot");
	static_cast<void>(graphics.SelectTarget(renderer));
	camera.ResetDefaultCamera();
	records.BeginFrame({}, sgre::render::SpritePassLoadAction::Clear);
	for (std::size_t depth = 0; depth < 7; ++depth) {
		records.BeginRecord();
	}
	RequireFailure([&] {
		records.BeginRecord();
	},
	    "eight physical slots");
	Require(records.RecordDepth() == 7, "overflow altered the bounded physical cursor");
	for (std::size_t depth = 0; depth < 7; ++depth) {
		records.EndRecord();
	}
	RequireFailure([&] {
		records.EndRecord();
	},
	    "begun physical record");
	records.AbortFrame();
	records.BeginFrame({}, sgre::render::SpritePassLoadAction::Clear);
	records.BeginRecord();
	heap.Begin();
	RequireFailure([&] {
		records.Append(red, pipeline, inputs, 0);
	},
	    "recycled");
	sgre::render::SpriteHostInputs fresh_inputs(heap, vertices, kIndices);
	records.Append(red, pipeline, fresh_inputs, 0);
	records.EndRecord();
	const auto next = records.EndFrame();
	Require(next == submission + 1, "rejected/aborted traversal published a nominal submission");
	static_cast<void>(renderer->Capture(next));
}

static void TestActualFailureLatch(const std::filesystem::path& shader) {
	sgre::render::SpriteGraphicsTargets graphics(Description(1), shader, 1);
	const auto renderer = graphics.PrimaryTarget();
	static_cast<void>(graphics.SelectTarget(renderer));
	sgre::scene::GraphicsModelStack models(2);
	models.Reset();
	sgre::render::SpriteGraphicsCamera camera(graphics, models);
	camera.ResetDefaultCamera();
	const auto texture = Upload(*renderer, {255, 255, 255, 255});
	sgre::scene::MotionSpriteMaterial material;
	material.BlendMode = 0;
	sgre::scene::SpritePipelineOptions options;
	const auto pipeline = renderer->PreparePipeline(sgre::scene::SelectSpritePipeline(material, options));
	sgre::render::FrameHostHeap heap(1);
	const auto vertices = Vertices();
	sgre::render::SpriteHostInputs inputs(heap, vertices, kIndices);
	sgre::render::RenderFrameStatistics statistics({}, {});
	sgre::render::SpriteRenderRecords records(*renderer, camera, statistics);
	records.BeginFrame({}, sgre::render::SpritePassLoadAction::Clear);
	records.BeginRecord();
	records.SetFilterColor(0);
	records.Append(texture, pipeline, inputs, 0);
	RequireFailure([&] {
		records.Append(texture, pipeline, inputs, 0);
	},
	    "capacity");
	RequireFailure([&] {
		records.BeginRecord();
	},
	    "capacity");
	RequireFailure([&] {
		static_cast<void>(records.EndFrame());
	},
	    "capacity");
	Require(statistics.Current().DrawOperations == 1 && statistics.Current().SubmittedElements == 4, "failed physical append fabricated a successful draw counter");
	records.AbortFrame();
	records.BeginFrame({}, sgre::render::SpritePassLoadAction::Clear);
	records.BeginRecord();
	records.Append(texture, pipeline, inputs, 0);
	records.EndRecord();
	const auto submission = records.EndFrame();
	Require(submission == 1, "failed actual GPU traversal escaped Abort as a nominal frame");
	static_cast<void>(renderer->Capture(submission));
}

int main(int argc, char** argv) {
	try {
		Require(argc == 3, "usage: sprite_render_records_tests original_fixture shader");
		ReplayRecords(argv[1], argv[2]);
		TestLiveScopes(argv[2]);
		TestActualFailureLatch(argv[2]);
		std::cout << "PASS actual eight-record/encoder ownership, resource preflight, nested model pixels, phase failures and submission retirement\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
