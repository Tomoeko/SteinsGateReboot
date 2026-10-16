#include "sgre/render/sprite_frame_encoder.hpp"

#include "sgre/render/sprite_graphics_camera.hpp"
#include "sgre/render/sprite_graphics_targets.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
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

	throw std::runtime_error("required prepared metadata owner rejection absent");
}

static sgre::render::SpritePassDescription Description() {
	sgre::render::SpritePassDescription result;
	result.Width = 4;
	result.Height = 4;
	result.MaxVertices = 4;
	result.MaxIndices = 4;
	result.MaxDraws = 1;
	result.MaxTextures = 8;
	result.MaxPipelines = 1;
	result.MaxTextureBytes = 4096;

	return result;
}

static sgre::render::SpriteTextureHandle Upload(sgre::render::SpritePassRenderer& renderer, sgre::resource::PixelFormat format) {
	sgre::resource::RgbaImage image;
	image.Width = 1;
	image.Height = 1;
	image.Pixels = {255, 255, 255, 255};

	return renderer.UploadTexture(format, image);
}

static void TestPreparedOwners(const std::filesystem::path& shader) {
	sgre::render::SpritePassRenderer renderer(Description(), shader);
	sgre::render::SpritePassRenderer foreign(Description(), shader);
	const std::array<sgre::resource::PixelFormat, 3> formats{sgre::resource::PixelFormat::Bc7, sgre::resource::PixelFormat::Alpha8, sgre::resource::PixelFormat::Luminance8};
	const std::array<sgre::scene::MotionShaderTexture, 3> channels{sgre::scene::MotionShaderTexture::Rgba, sgre::scene::MotionShaderTexture::Alpha, sgre::scene::MotionShaderTexture::Luminance};
	std::array<sgre::render::SpriteTextureHandle, 3> textures;
	for (std::size_t index = 0; index < formats.size(); ++index) {
		textures[index] = Upload(renderer, formats[index]);
	}
	const auto stale = Upload(renderer, formats[0]);
	renderer.ReleaseTexture(stale);
	const auto replacement = Upload(renderer, formats[0]);
	Require(replacement.Index == stale.Index && replacement.Generation != stale.Generation, "actual renderer did not recycle the checked source generation");
	const auto other_texture = Upload(foreign, formats[0]);
	sgre::scene::MotionSpriteMaterial material;
	material.TextureFormat = formats[0];
	sgre::scene::SpritePipelineOptions options;
	options.EnableBlending = false;
	const auto configuration = sgre::scene::SelectSpritePipeline(material, options);
	const auto pipeline = renderer.PreparePipeline(configuration);
	const auto other_pipeline = foreign.PreparePipeline(configuration);
	auto backend = renderer.CreateMutableTextureBackend();
	sgre::scene::MutableTextureSpec spec;
	spec.Storage = sgre::scene::MutableTextureStorage::CpuShadow;
	spec.Description.Width = 2;
	spec.Description.Height = 1;
	spec.Description.OriginalFormat = 87;
	auto mutable_owner = std::make_unique<sgre::scene::MutableTextureOwner>(*backend, spec);
	mutable_owner->Bind(0, sgre::scene::MutableTextureStage::Pixel);
	const auto mutable_handle = renderer.MutableTextureBinding(0, sgre::scene::MutableTextureStage::Pixel);
	sgre::render::SpriteFrameEncoder encoder(renderer);
	RequireFailure([&] {
		static_cast<void>(encoder.PreparedPipeline(pipeline));
	},
	    "actual open encoder");
	RequireFailure([&] {
		static_cast<void>(encoder.PreparedTexture(textures[0]));
	},
	    "actual open encoder");
	encoder.Begin({}, sgre::render::SpritePassLoadAction::Clear);
	for (std::size_t index = 0; index < textures.size(); ++index) {
		const auto metadata = encoder.PreparedTexture(textures[index]);
		Require(metadata.Channels == channels[index] && metadata.Width == 1 && metadata.Height == 1 && !metadata.Mutable && !metadata.OriginalDxgiFormat, "prepared immutable metadata invented format provenance or changed actual channel/extent");
	}
	const auto prepared = encoder.PreparedPipeline(pipeline);
	Require(prepared.NativeVertexShaderIndex == configuration.NativeVertexShaderIndex && prepared.NativePixelShaderIndex == configuration.NativePixelShaderIndex && prepared.TextureFormat == configuration.TextureFormat && prepared.Blend.Enabled == configuration.Blend.Enabled, "prepared metadata did not return the exact registered pipeline configuration");
	const auto mutable_metadata = encoder.PreparedTexture(mutable_handle);
	Require(mutable_metadata.Channels == channels[0] && mutable_metadata.Width == 2 && mutable_metadata.Height == 1 && mutable_metadata.Mutable && mutable_metadata.OriginalDxgiFormat == 87, "prepared mutable metadata lost its actual retained DXGI provenance");
	RequireFailure([&] {
		static_cast<void>(encoder.PreparedTexture(stale));
	},
	    "stale");
	RequireFailure([&] {
		static_cast<void>(encoder.PreparedTexture(other_texture));
	},
	    "another renderer");
	RequireFailure([&] {
		static_cast<void>(encoder.PreparedPipeline(other_pipeline));
	},
	    "another renderer");
	bool foreign_rejected = false;
	std::thread other_thread([&] {
		try {
			static_cast<void>(encoder.PreparedPipeline(pipeline));
		} catch (const std::runtime_error&) {
			foreign_rejected = true;
		}
	});
	other_thread.join();
	Require(foreign_rejected && encoder.IsActive(), "read-only metadata query bypassed serialized actual ownership");
	mutable_owner.reset();
	RequireFailure([&] {
		static_cast<void>(encoder.PreparedTexture(mutable_handle));
	},
	    "stale");
	Require(mutable_metadata.OriginalDxgiFormat == 87 && prepared.NativeVertexShaderIndex == configuration.NativeVertexShaderIndex, "metadata values escaped as dangling GPU/container borrows");
	encoder.Abort();
}

static void TestExpiredRenderer(const std::filesystem::path& shader) {
	auto renderer = std::make_unique<sgre::render::SpritePassRenderer>(Description(), shader);
	const auto texture = Upload(*renderer, sgre::resource::PixelFormat::Bc7);
	sgre::render::SpriteFrameEncoder encoder(*renderer);
	encoder.Begin({}, sgre::render::SpritePassLoadAction::Clear);
	renderer.reset();
	RequireFailure([&] {
		static_cast<void>(encoder.PreparedTexture(texture));
	},
	    "renderer has retired");
	encoder.Abort();
}

static void TestExactProjectedTarget(const std::filesystem::path& shader) {
	sgre::render::SpriteGraphicsTargets graphics(Description(), shader, 2);
	const auto primary = graphics.PrimaryTarget();
	const auto other = graphics.AllocateTarget(Description());
	static_cast<void>(graphics.SelectTarget(primary));
	sgre::scene::GraphicsModelStack models(4);
	models.Reset();
	sgre::render::SpriteGraphicsCamera camera(graphics, models);
	camera.ResetDefaultCamera();
	camera.RequireTarget(*primary);
	RequireFailure([&] {
		camera.RequireTarget(*other);
	},
	    "another actual render target");
	static_cast<void>(graphics.SelectTarget(other));
	RequireFailure([&] {
		camera.RequireTarget(*other);
	},
	    "current projection reset");
	camera.ResetDefaultCamera();
	camera.RequireTarget(*other);
	static_cast<void>(graphics.SelectTarget(nullptr));
	RequireFailure([&] {
		camera.RequireTarget(*other);
	},
	    "current target is absent");
}

int main(int argc, char** argv) {
	try {
		Require(argc == 2, "usage: live_encoder_metadata_tests shader");
		TestPreparedOwners(argv[1]);
		TestExpiredRenderer(argv[1]);
		TestExactProjectedTarget(argv[1]);
		std::cout << "Actual prepared pipeline/texture metadata, provenance, value lifetime and active receiver guards passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
