#include "sgre/render/graphics_frame_cycle.hpp"

#include "sgre/platform/host_performance_counter.hpp"
#include "sgre/scene/image_receiving_document.hpp"
#include "sgre/scene/parallel_task_set.hpp"

#include "image_receiving_fixture.hpp"
#include "sprite_presentation_fixture.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

template <typename Operation>
static void RequireFailure(Operation operation, std::string_view expected) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(expected) != std::string_view::npos, error.what());

		return;
	}

	throw std::runtime_error("expected Picture frame rejection: " + std::string(expected));
}

static sgre::render::SpritePassDescription Description() {
	sgre::render::SpritePassDescription result;
	result.Width = 8;
	result.Height = 8;
	result.MaxVertices = 64;
	result.MaxIndices = 64;
	result.MaxDraws = 16;
	result.MaxTextures = 8;
	// PreparePictures requests 264 channel/blend/opacity/filter combinations; mixed pairs share native sampler4.
	result.MaxPipelines = 512;
	result.MaxTextureBytes = 65536;

	return result;
}

static void RequirePixel(const sgre::render::ColorPassPixels& pixels, std::size_t x, std::size_t y, const std::array<int, 4>& expected) {
	Require(x < pixels.Width && y < pixels.Height, "Picture pixel exceeds its actual captured target");
	const auto offset = (y * pixels.Width + x) * 4;
	for (std::size_t channel = 0; channel < expected.size(); ++channel) {
		const auto difference = static_cast<int>(pixels.Rgba[offset + channel]) - expected[channel];
		if (difference < -1 || difference > 1) {
			throw std::runtime_error("Picture pixel x=" + std::to_string(x) + " y=" + std::to_string(y) + " channel=" + std::to_string(channel) + " expected=" + std::to_string(expected[channel]) + " actual=" + std::to_string(pixels.Rgba[offset + channel]));
		}
	}
}

static sgre::scene::PictureDrawFrame BorrowFrame(const sgre::scene::PictureLayerTask& task, std::span<const sgre::scene::MotionTextureWrapper::Borrowed> sources) {
	sgre::scene::PictureDrawFrame frame;
	frame.State = task.State();
	frame.Regions = task.Regions();
	frame.Sources = sources;

	return frame;
}

static void TestActualPictureCycle(const std::filesystem::path& shader, const std::filesystem::path& transfer) {
	sgre::render::SpriteGraphicsTargets graphics(Description(), shader, 2);
	const auto primary = graphics.PrimaryTarget();
	sgre::render::SpritePassPresentation surface(*primary, transfer, sgre::render::SpritePresentationDescription{});
	sgre::platform::HostPerformanceCounter counter;
	sgre::core::ThreadTaskFrame workers([&] {
		return counter.ReadRawCounter();
	},
	    1, 4);
	std::size_t uploads = 0;
	std::vector<sgre::render::SpriteTextureHandle> released;
	sgre::scene::TextureCache cache([&](auto format, const auto& image) {
		Require(format == sgre::resource::PixelFormat::Alpha8 && image.Width == 1 && image.Height == 1, "Picture must consume the actual independent A8 receiving fixture");
		const std::uint8_t alpha = uploads == 0 ? 128 : 64;
		Require(image.Pixels == std::vector<std::uint8_t>{0, 0, 0, alpha}, "native Image preload must preserve A8 raw channels before shader interpretation");
		++uploads;

		return primary->UploadTexture(format, image);
	},
	    [&](auto texture) {
		    released.push_back(texture);
		    primary->ReleaseTexture(texture);
	    });
	sgre::scene::ImageAssetCatalog catalog(cache);
	auto receiving = std::make_shared<const sgre::scene::ImageReceivingDocument>(cache, std::vector<std::uint8_t>(kReceivingImageFixture.begin(), kReceivingImageFixture.end()));
	const auto first_preload = receiving->Source(0);
	const auto second_preload = receiving->Source(1);
	const auto first_handle = first_preload.Texture();
	const auto second_handle = second_preload.Texture();
	const std::array<sgre::render::SpriteTextureHandle, 2> backend_order{cache.Get(first_handle).Backend, cache.Get(second_handle).Backend};
	Require(uploads == 2 && receiving->MetadataBytes().size() + 2 == kReceivingImageFixture.size(), "Picture document must upload both sources before genuine pixel purge");
	auto first_asset = catalog.Prepare(receiving);
	auto second_asset = catalog.Prepare(receiving, "second");
	auto first_image = std::make_unique<sgre::scene::ImageTextureOwner>(cache, first_asset);
	auto second_image = std::make_unique<sgre::scene::ImageTextureOwner>(cache, second_asset);
	std::array<sgre::scene::MotionTextureWrapper::Borrowed, 1> first_sources{first_image->Source(0)};
	const std::array<sgre::scene::MotionTextureWrapper::Borrowed, 1> second_sources{second_image->Source(0)};
	Require(uploads == 2 && first_sources[0].Texture() == first_handle && second_sources[0].Texture() == second_handle && cache.Get(first_handle).References == 2 && cache.Get(second_handle).References == 2, "selected post-purge Image owners must use both genuine preloaded textures");
	Require(first_image->AddressU(0) == 2 && first_image->AddressV(0) == 2 && second_image->AddressU(0) == 2 && second_image->AddressV(0) == 2, "selected Image wrappers must retain native mirror addressing");
	Require(cache.Get(first_handle).Minification == 0 && cache.Get(first_handle).Magnification == 0, "Picture test requires genuine initial preload filtering before Draw changes it");
	sgre::scene::PictureLayerTask first(first_asset);
	sgre::scene::PictureLayerTask second(second_asset);
	first.SetCoordinate({-2, -1});
	second.SetCoordinate({1, -1});
	first.SetFiltering(1, 1);
	second.SetFiltering(1, 1);
	sgre::render::GraphicsFrameCycle cycle(graphics, surface, workers, cache, {}, {});
	cycle.PreparePictures();
	sgre::scene::ParallelTaskSet children(1);
	std::size_t callback_count = 0;
	sgre::scene::ParallelTaskCallbacks callback;
	callback.Draw = [&](auto, auto) {
		Require(cycle.CameraScope().IsDrawing(), "Picture must submit inside the real containing camera scope");
		++callback_count;
		cycle.DrawPicture(BorrowFrame(first, first_sources));
		cycle.DrawPicture(BorrowFrame(second, second_sources));
	};
	static_cast<void>(children.Append(std::move(callback)));
	const auto draw = [&] {
		cycle.PreFrame();
		cycle.BeginDrawing();
		cycle.ResetRootCamera();
		cycle.CameraScope().Draw(sgre::scene::LayerCameraParameters{}, children, 0);
		cycle.PostFrame();
		const auto submission = cycle.LastSubmission();
		Require(submission && submission->Owner == primary.get(), "Picture cycle must publish the genuine primary submission");

		return primary->Capture(submission->Identity);
	};
	try {
		auto pixels = draw();
		RequirePixel(pixels, 2, 3, {128, 128, 128, 255});
		RequirePixel(pixels, 5, 3, {64, 64, 64, 255});
		RequirePixel(pixels, 0, 0, {0, 0, 0, 255});
		Require(cycle.CurrentCounters().DrawOperations == 2 && cycle.CurrentCounters().SubmittedElements == 8, "actual two-source Picture frame must emit exactly two native four-vertex strips");
		Require(cache.Get(first_handle).Minification == 1 && cache.Get(first_handle).Magnification == 1 && cache.Get(second_handle).Minification == 1 && cache.Get(second_handle).Magnification == 1 && uploads == 2, "Picture Draw must change the real shared source filters without replacing or uploading textures");
		first.SetOpacity(128);
		second.SetOpacity(128);
		pixels = draw();
		RequirePixel(pixels, 2, 3, {64, 64, 64, 255});
		RequirePixel(pixels, 5, 3, {32, 32, 32, 255});
		first.SetOpacity(255);
		second.SetOpacity(255);
		first.ClearImageRanges();
		second.ClearImageRanges();
		const auto before_empty = callback_count;
		pixels = draw();
		Require(callback_count == before_empty + 1 && cycle.CurrentCounters() == sgre::render::RenderFrameCounters{}, "empty Picture must complete actual Begin/End receiving without a GPU draw");
		RequirePixel(pixels, 2, 3, {0, 0, 0, 255});
		RequirePixel(pixels, 5, 3, {0, 0, 0, 255});
		first.AppendImageRange({0, 0, 1, 1});
		second.AppendImageRange({0, 0, 1, 1});
		pixels = draw();
		RequirePixel(pixels, 2, 3, {128, 128, 128, 255});
		Require(cycle.CurrentCounters().DrawOperations == 2, "empty Begin/End must leave subsequent real records balanced");

		first.SetFiltering(0, 1);
		first_image->SetAddress(1, 2);
		RequireFailure([&] {
			static_cast<void>(draw());
		},
		    "mirror-addressed source");
		Require(!cycle.LastSubmission() && cache.Get(first_handle).Minification == 1 && cache.Get(first_handle).Magnification == 1, "bad-address preflight must abort before changing the real source sampler");
		cycle.AbortFrame();
		first_image->SetAddress(2, 2);
		pixels = draw();
		RequirePixel(pixels, 2, 3, {128, 128, 128, 255});
		Require(cache.Get(first_handle).Minification == 0 && cache.Get(first_handle).Magnification == 1, "retry must publish the actual user mixed-filter words to its shared Image source");

		const auto retired_source = first_sources[0];
		first_image->Close();
		Require(!retired_source.IsLive() && first_preload.IsLive() && released.empty(), "selected wrapper retirement must expire its borrow while genuine document preloads remain");
		RequireFailure([&] {
			static_cast<void>(draw());
		},
		    "live Image source wrapper");
		Require(!cycle.LastSubmission(), "stale-source frame must not publish a replacement submission");
		cycle.AbortFrame();
		first_image = std::make_unique<sgre::scene::ImageTextureOwner>(cache, first_asset);
		first_sources[0] = first_image->Source(0);
		Require(first_sources[0].Texture() == first_handle && uploads == 2, "retired selected wrapper retry must reacquire its real post-purge source identity");
		pixels = draw();
		RequirePixel(pixels, 2, 3, {128, 128, 128, 255});
		RequirePixel(pixels, 5, 3, {64, 64, 64, 255});
		Require(cycle.CameraScope().Depth() == 1 && !cycle.CameraScope().IsDrawing(), "failed and successful Picture traversals must restore the actual containing model/camera");
		const auto observation = sgre::render::SpritePassPresentationFixtureBridge::Observe(surface);
		Require(!observation.Presenting && !observation.WindowVisible && !observation.WindowKey && !observation.WindowMain && observation.DrawableRequests == 0, "offscreen Picture receiving must not present, focus or request a drawable");
		children.Clear();
		cycle.Close();
	} catch (...) {
		children.Clear();
		cycle.AbortFrame();
		cycle.Close();
		throw;
	}
	first.Close();
	second.Close();
	first_image.reset();
	second_image.reset();
	first_asset.reset();
	second_asset.reset();
	receiving.reset();
	catalog.Prune();
	Require(!first_preload.IsLive() && !second_preload.IsLive() && cache.Count() == 0 && catalog.DocumentCount() == 0 && released == std::vector<sgre::render::SpriteTextureHandle>(backend_order.begin(), backend_order.end()), "final actual Image retirement must release ordered preloads after all Picture frame borrows end");
	cache.Close();
	workers.Close();
}

int main(int argc, char** argv) {
	try {
		if (argc != 3) {
			throw std::runtime_error("Picture frame fixture requires exact sprite/presentation shader paths");
		}
		TestActualPictureCycle(argv[1], argv[2]);
		std::cout << "actual Picture receiving: preloaded A8 pixels/filter mutation, empty scope, abort/retry and ordered retirement passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
