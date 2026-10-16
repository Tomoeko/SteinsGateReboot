#include "sgre/render/sprite_pass.hpp"
#include "sgre/render/sprite_frame_encoder.hpp"
#include "sgre/scene/bitmap_font_preparation_service.hpp"

#include "bitmap_font_receiving_fixture.hpp"
#include "bc7_block_fixture.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <vector>

static void Require(bool condition, const char* message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

static void RequireFailure(const auto& operation) {
	bool rejected = false;
	try {
		operation();
	} catch (const std::runtime_error&) {
		rejected = true;
	}

	Require(rejected, "foreign, stale or over-budget shared resource must reject");
}

static sgre::render::SpritePassDescription Description(std::uint32_t size) {
	sgre::render::SpritePassDescription description;
	description.Width = size;
	description.Height = size;
	description.MaxVertices = 4;
	description.MaxIndices = 4;
	description.MaxDraws = 1;
	description.MaxTextures = 1;
	description.MaxPipelines = 1;
	description.MaxTextureBytes = 4;

	return description;
}

static void DrawColor(sgre::render::SpritePassRenderer& renderer, sgre::render::SpriteTextureHandle texture, sgre::render::SpritePipelineHandle pipeline, const std::array<std::uint8_t, 4>& expected) {
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
	draw.Texture = texture;
	draw.Pipeline = pipeline;
	draw.Vertices = vertices;
	draw.Indices = indices;
	const std::array<float, 16> projection{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0.5f, 0, 0, 0, 1};
	const auto submission = renderer.Submit(std::span(&draw, 1), projection);
	const auto pixels = renderer.Capture(submission);
	for (std::size_t offset = 0; offset < pixels.Rgba.size(); offset += 4) {
		for (std::size_t channel = 0; channel < expected.size(); ++channel) {
			Require(pixels.Rgba[offset + channel] == expected[channel], "shared target pixels differ from independent opaque color");
		}
	}
}

static void TestSharing(const std::filesystem::path& shader) {
	auto parent = std::make_unique<sgre::render::SpritePassRenderer>(Description(4), shader);
	sgre::render::SpritePassRenderer child(Description(2), *parent);
	sgre::render::SpritePassRenderer foreign(Description(2), shader);
	auto wrong_limits = Description(2);
	wrong_limits.MaxTextures = 2;
	RequireFailure([&] {
		sgre::render::SpritePassRenderer incompatible(wrong_limits, *parent);
	});

	sgre::resource::RgbaImage image;
	image.Width = 1;
	image.Height = 1;
	image.Pixels = {255, 0, 0, 255};
	const auto red = parent->UploadTexture(sgre::resource::PixelFormat::Bc7, image);
	sgre::scene::MotionSpriteMaterial material;
	material.TextureFormat = sgre::resource::PixelFormat::Bc7;
	sgre::scene::SpritePipelineOptions options;
	options.EnableBlending = false;
	const auto configuration = sgre::scene::SelectSpritePipeline(material, options);
	const auto pipeline = parent->PreparePipeline(configuration);
	const auto same_pipeline = child.PreparePipeline(configuration);
	Require(pipeline == same_pipeline, "targets must share the actual prepared pipeline identity");
	DrawColor(*parent, red, pipeline, {255, 0, 0, 255});
	DrawColor(child, red, pipeline, {255, 0, 0, 255});
	RequireFailure([&] {
		static_cast<void>(child.UploadTexture(sgre::resource::PixelFormat::Bc7, image));
	});
	RequireFailure([&] {
		DrawColor(foreign, red, pipeline, {255, 0, 0, 255});
	});

	child.ReleaseTexture(red);
	image.Pixels = {0, 0, 255, 255};
	const auto blue = child.UploadTexture(sgre::resource::PixelFormat::Bc7, image);
	Require(blue.Owner == red.Owner && blue.Index == red.Index && blue.Generation != red.Generation, "global reuse must advance the shared generation");
	RequireFailure([&] {
		DrawColor(*parent, red, pipeline, {255, 0, 0, 255});
	});
	DrawColor(*parent, blue, pipeline, {0, 0, 255, 255});
	DrawColor(child, blue, pipeline, {0, 0, 255, 255});
	parent.reset();
	DrawColor(child, blue, pipeline, {0, 0, 255, 255});
	child.ReleaseTexture(blue);
	RequireFailure([&] {
		DrawColor(child, blue, pipeline, {0, 0, 255, 255});
	});
	std::cout << "two actual Metal targets share textures/pipelines/budgets and survive creator-target retirement\n";
}

static void TestMutableRetirement(const std::filesystem::path& shader) {
	auto description = Description(4);
	// One RGBA texel plus the three actual 256-byte upload slots.
	description.MaxTextureBytes = 4 + 3 * 256;
	auto parent = std::make_unique<sgre::render::SpritePassRenderer>(description, shader);
	description.Width = 2;
	description.Height = 2;
	sgre::render::SpritePassRenderer child(description, *parent);
	auto backend = parent->CreateMutableTextureBackend();
	sgre::scene::MutableTextureSpec spec;
	spec.Storage = sgre::scene::MutableTextureStorage::GpuOnly;
	spec.Description.Width = 1;
	spec.Description.Height = 1;
	spec.Description.OriginalFormat = 28;
	const std::array<std::byte, 4> seed{std::byte{0}, std::byte{255}, std::byte{0}, std::byte{255}};
	sgre::scene::MutableTextureInitialData initial;
	initial.Bytes = seed;
	initial.RowPitch = 4;
	auto texture = std::make_unique<sgre::scene::MutableTextureOwner>(*backend, spec, initial);
	texture->Bind(0, sgre::scene::MutableTextureStage::Pixel);
	const auto binding = parent->MutableTextureBinding(0, sgre::scene::MutableTextureStage::Pixel);
	sgre::scene::MotionSpriteMaterial material;
	material.TextureFormat = sgre::resource::PixelFormat::Bc7;
	sgre::scene::SpritePipelineOptions options;
	options.EnableBlending = false;
	const auto pipeline = child.PreparePipeline(sgre::scene::SelectSpritePipeline(material, options));
	DrawColor(child, binding, pipeline, {0, 255, 0, 255});
	parent.reset();
	texture.reset();
	RequireFailure([&] {
		DrawColor(child, binding, pipeline, {0, 255, 0, 255});
	});

	sgre::resource::RgbaImage image;
	image.Width = 1;
	image.Height = 1;
	image.Pixels = {255, 255, 0, 255};
	const auto reused = child.UploadTexture(sgre::resource::PixelFormat::Bc7, image);
	Require(reused.Owner == binding.Owner && reused.Index == binding.Index && reused.Generation != binding.Generation, "mutable retirement after creator closure must return shared budget and generation");
	DrawColor(child, reused, pipeline, {255, 255, 0, 255});
	child.ReleaseTexture(reused);
}

static void TestAutomaticBudget(const std::filesystem::path& shader) {
	auto requested = Description(2);
	requested.MaxTextures = 2;
	requested.MaxTextureBytes = 0;
	sgre::render::SpritePassRenderer parent(requested, shader);
	const auto resolved = parent.Description();
	Require(resolved.MaxTextureBytes != 0 && resolved.Width == requested.Width && resolved.MaxTextures == requested.MaxTextures && resolved.MaxTargetBytes == requested.MaxTargetBytes, "automatic policy must publish a positive texture ceiling without changing other resource bounds");
	sgre::render::SpritePassRenderer inherited(requested, parent);
	Require(inherited.Description().MaxTextureBytes == resolved.MaxTextureBytes, "automatic shared target must inherit the owner's resolved budget");
	sgre::render::SpritePassRenderer exact(resolved, parent);
	auto mismatch = resolved;
	mismatch.MaxTextureBytes = resolved.MaxTextureBytes == 1 ? 2 : resolved.MaxTextureBytes - 1;
	RequireFailure([&] {
		sgre::render::SpritePassRenderer incompatible(mismatch, parent);
	});

	// The same effective description configures the real receiving cache in the application.
	sgre::scene::TextureCacheLimits cache_limits;
	cache_limits.MaxTextures = resolved.MaxTextures;
	cache_limits.MaxTextureBytes = resolved.MaxTextureBytes;
	Require(cache_limits.MaxDecodedTextureBytes == 64 * 1024 * 1024, "automatic aggregate policy must preserve the per-texture decoder bound");
	sgre::scene::BitmapFontPreparationService preparation([&](auto format, const auto& image) {
		return parent.UploadTexture(format, image);
	},
	    [&](auto handle) {
		    parent.ReleaseTexture(handle);
	    },
	    cache_limits);
	auto document = preparation.Prepare(std::vector<std::uint8_t>(kReceivingFontFixture.begin(), kReceivingFontFixture.end()));
	Require(preparation.Cache().Count() == 2 && preparation.Cache().UploadedBytes() == 8, "effective budget bridge must prepare both genuine synthetic atlas owners");
	sgre::scene::MotionSpriteMaterial material;
	material.TextureFormat = sgre::resource::PixelFormat::Alpha8;
	sgre::scene::SpritePipelineOptions options;
	options.EnableBlending = false;
	const auto pipeline = parent.PreparePipeline(sgre::scene::SelectSpritePipeline(material, options));
	const auto first = preparation.Cache().Get(document->Source(0).Texture()).Backend;
	const auto second = preparation.Cache().Get(document->Source(1).Texture()).Backend;
	DrawColor(inherited, first, pipeline, {255, 255, 255, 128});
	DrawColor(exact, second, pipeline, {255, 255, 255, 64});
	document.reset();
	Require(preparation.Cache().Count() == 0 && preparation.Cache().UploadedBytes() == 0, "automatic policy must not retain retired receiving textures");
	RequireFailure([&] {
		DrawColor(inherited, first, pipeline, {255, 255, 255, 128});
	});
	preparation.Close();
	std::cout << "selected Metal device resolved texture ceiling=" << resolved.MaxTextureBytes << " bytes; shared targets and actual preparation cache agree\n";
}

static void TestAutomaticChildOfExplicitBudget(const std::filesystem::path& shader) {
	auto explicit_limits = Description(2);
	explicit_limits.MaxTextures = 2;
	sgre::render::SpritePassRenderer parent(explicit_limits, shader);
	auto inherited_limits = explicit_limits;
	inherited_limits.MaxTextureBytes = 0;
	sgre::render::SpritePassRenderer child(inherited_limits, parent);
	Require(parent.Description().MaxTextureBytes == 4 && child.Description().MaxTextureBytes == 4, "automatic child must preserve the actual explicit shared owner ceiling");
	sgre::resource::RgbaImage image;
	image.Width = 1;
	image.Height = 1;
	image.Pixels = {255, 0, 0, 255};
	const auto first = child.UploadTexture(sgre::resource::PixelFormat::Bc7, image);
	RequireFailure([&] {
		static_cast<void>(parent.UploadTexture(sgre::resource::PixelFormat::Bc7, image));
	});
	child.ReleaseTexture(first);
	const auto second = parent.UploadTexture(sgre::resource::PixelFormat::Bc7, image);
	Require(second.Generation != first.Generation, "explicit budget retirement must return capacity with a fresh generation");
	parent.ReleaseTexture(second);
}

static void DrawBc7Pixels(sgre::render::SpritePassRenderer& renderer, sgre::render::SpriteTextureHandle texture, sgre::render::SpritePipelineHandle pipeline, std::span<const std::array<std::uint8_t, 4>> expected) {
	std::array<sgre::scene::MotionSpriteVertex, 4> vertices;
	vertices[0].Position = {-1, 1};
	vertices[0].Uv = {0, 0};
	vertices[1].Position = {1, 1};
	vertices[1].Uv = {1, 0};
	vertices[2].Position = {-1, -1};
	vertices[2].Uv = {0, 1};
	vertices[3].Position = {1, -1};
	vertices[3].Uv = {1, 1};
	for (auto& vertex : vertices) {
		vertex.Color = 0xffffffff;
	}
	const std::array<std::uint32_t, 4> indices{0, 2, 1, 3};
	sgre::render::SpritePassDraw draw;
	draw.Texture = texture;
	draw.Pipeline = pipeline;
	draw.Vertices = vertices;
	draw.Indices = indices;
	const std::array<float, 16> projection{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0.5f, 0, 0, 0, 1};
	const auto pixels = renderer.Capture(renderer.Submit(std::span(&draw, 1), projection));
	Require(pixels.Rgba.size() == expected.size() * 4, "BC7 capture extent differs from the independent source texels");
	for (std::size_t i = 0; i < expected.size(); ++i) {
		for (std::size_t c = 0; c < 4; ++c) {
			Require(pixels.Rgba[i * 4 + c] == expected[i][c], "Metal BC7 sampled texel differs from independent8-mode decode vector");
		}
	}
}

static void TestStoredBc7Metal(const std::filesystem::path& shader) {
	auto description = Description(4);
	description.MaxTextures = 2;
	description.MaxTextureBytes = 64;
	auto parent = std::make_unique<sgre::render::SpritePassRenderer>(description, shader);
	if (!parent->SupportsBc7Textures()) {
		std::cout << "Selected Metal device does not support BC textures; compressedGPU coverage was not executed\n";

		return;
	}
	auto mutable_backend = parent->CreateMutableTextureBackend();
	sgre::scene::MutableTextureSpec unsupported_copy;
	unsupported_copy.Storage = sgre::scene::MutableTextureStorage::GpuOnly;
	unsupported_copy.Description.Width = 4;
	unsupported_copy.Description.Height = 4;
	unsupported_copy.Description.OriginalFormat = 98;
	RequireFailure([&] {
		sgre::scene::MutableTextureOwner forbidden(*mutable_backend, unsupported_copy);
	});
	sgre::scene::MotionSpriteMaterial material;
	material.TextureFormat = sgre::resource::PixelFormat::Bc7;
	// Native attribute4 skips vertex modulation so these expected texels measure BC7 sampling alone.
	material.TextureAttributes = 4;
	sgre::scene::SpritePipelineOptions options;
	options.EnableBlending = false;
	const auto pipeline = parent->PreparePipeline(sgre::scene::SelectSpritePipeline(material, options));
	sgre::scene::TextureCacheLimits limits;
	limits.MaxTextures = 2;
	limits.MaxTextureBytes = 64;
	std::uint32_t compressed_uploads = 0;
	sgre::scene::TextureCache cache([](auto, const auto&) -> sgre::render::SpriteTextureHandle {
		throw std::runtime_error("actual BC7 fixture unexpectedly used the RGBA route");
	}, [&](auto handle) {
		parent->ReleaseTexture(handle);
	}, limits, [&](const auto& view) {
		++compressed_uploads;

		return parent->UploadBc7Texture(view);
	});
	std::uint64_t previous_generation = 0;
	for (std::size_t case_index = 0; case_index < 12; ++case_index) {
		const std::uint32_t width = case_index < 8 ? 4 : case_index == 8 ? 1 : case_index == 9 ? 3 : case_index == 10 ? 5 : 8;
		const std::uint32_t height = case_index < 8 ? 4 : case_index == 8 ? 1 : case_index == 9 ? 2 : case_index == 10 ? 5 : 8;
		const std::size_t block_count = static_cast<std::size_t>((width + 3) / 4) * ((height + 3) / 4);
		const std::size_t first_mode = case_index < 8 ? case_index : case_index == 8 ? 6 : case_index == 9 ? 7 : case_index == 10 ? 0 : 4;
		std::vector<std::uint8_t> blocks;
		for (std::size_t block = 0; block < block_count; ++block) {
			const auto& vector = kIndependentBc7Vectors[first_mode + block];
			blocks.insert(blocks.end(), vector.Block.begin(), vector.Block.end());
		}
		std::vector<std::array<std::uint8_t, 4>> expected;
		for (std::uint32_t y = 0; y < height; ++y) {
			for (std::uint32_t x = 0; x < width; ++x) {
				const auto mode = first_mode + (y / 4) * ((width + 3) / 4) + x / 4;
				expected.push_back(kIndependentBc7Vectors[mode].Pixels[(y % 4) * 4 + x % 4]);
			}
		}
		sgre::resource::ImageTexture source;
		source.SerializedValueOffset = static_cast<std::uint32_t>(100 + case_index);
		source.Format = sgre::resource::PixelFormat::Bc7;
		source.Width = width;
		source.Height = height;
		source.PixelRange.Size = blocks.size();
		const auto texture = cache.Acquire(1, source, blocks);
		const auto retained = cache.Acquire(1, source, {});
		const auto backend = cache.Get(texture).Backend;
		Require(texture == retained && cache.UploadedBytes() == blocks.size() && compressed_uploads == case_index + 1 && backend.Generation != previous_generation, "BC7 cache must reuse exact identity and charge stored bytes with independent lifetime generations");
		previous_generation = backend.Generation;
		std::fill(blocks.begin(), blocks.end(), std::uint8_t{0xee});
		blocks.clear();
		auto child_description = description;
		child_description.Width = width;
		child_description.Height = height;
		child_description.MaxTextureBytes = 0;
		sgre::render::SpritePassRenderer child(child_description, *parent);
		sgre::render::SpriteFrameEncoder encoder(child);
		encoder.Begin({}, sgre::render::SpritePassLoadAction::Clear);
		const auto metadata = encoder.PreparedTexture(backend);
		Require(!metadata.Mutable && !metadata.RenderTarget && metadata.OriginalDxgiFormat == 98 && metadata.Channels == sgre::scene::MotionShaderTexture::Rgba, "compressed source metadata must report actual BC7 storage without a mutable RGBA view");
		encoder.Abort();
		DrawBc7Pixels(child, backend, pipeline, expected);
		cache.ReleaseTexture(texture);
		DrawBc7Pixels(child, backend, pipeline, expected);
		cache.ReleaseTexture(retained);
		Require(cache.Count() == 0 && cache.UploadedBytes() == 0, "last BC7 reference must return its exact stored byte charge");
		RequireFailure([&] {
			DrawBc7Pixels(child, backend, pipeline, expected);
		});
	}
	cache.Close();

	auto bytes = kIndependentBc7Vectors[0].Block;
	sgre::resource::ImageTexture source;
	source.Format = sgre::resource::PixelFormat::Bc7;
	source.Width = 4;
	source.Height = 4;
	source.PixelRange.Size = bytes.size();
	auto view = sgre::resource::BorrowBc7Texture(source, bytes);
	view.BlockRowBytes += 16;
	RequireFailure([&] {
		static_cast<void>(parent->UploadBc7Texture(view));
	});
	view.BlockRowBytes -= 16;
	const auto final_texture = parent->UploadBc7Texture(view);
	sgre::render::SpritePassRenderer surviving(description, *parent);
	parent.reset();
	DrawBc7Pixels(surviving, final_texture, pipeline, kIndependentBc7Vectors[0].Pixels);
	surviving.ReleaseTexture(final_texture);
	std::cout << "BC7 Metal: 12 bounded cases, 224 independent pixels, identity/borrow/retirement checks completed\n";
}

int main(int argc, char** argv) {
	try {
		Require(argc == 2, "usage: shared_sprite_resources_tests sprite_shader");
		TestSharing(argv[1]);
		TestMutableRetirement(argv[1]);
		TestAutomaticBudget(argv[1]);
		TestAutomaticChildOfExplicitBudget(argv[1]);
		TestStoredBc7Metal(argv[1]);

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
