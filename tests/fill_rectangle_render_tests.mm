#include "motion_receiver_fixture.hpp"

#include "sgre/platform/host_performance_counter.hpp"
#include "sgre/render/graphics_frame_cycle.hpp"

#include "sprite_presentation_fixture.hpp"

static sgre::scene::FillRectangleDrawPacket Rectangle(float left, float top, float right, float bottom, std::uint32_t color) {
	sgre::scene::FillRectangleDrawPacket packet;
	packet.Vertices = {{{left, top}, {left, bottom}, {right, top}, {right, bottom}}};
	packet.Color = color;

	return packet;
}

static void TestActualFillRectangle(const std::filesystem::path& shader, const std::filesystem::path& transfer) {
	auto description = Description(8);
	sgre::render::SpriteGraphicsTargets graphics(description, shader, 2);
	const auto primary = graphics.PrimaryTarget();
	sgre::render::SpritePassPresentation surface(*primary, transfer, sgre::render::SpritePresentationDescription{});
	sgre::platform::HostPerformanceCounter counter;
	sgre::core::ThreadTaskFrame workers([&] {
		return counter.ReadRawCounter();
	},
	    1, 4);
	sgre::scene::TextureCache cache(Upload(*primary), Release(*primary));
	sgre::render::GraphicsFrameCycle cycle(graphics, surface, workers, cache, {}, {});
	cycle.PrepareFillRectangle();
	try {
		cycle.PreFrame();
		cycle.BeginDrawing();
		cycle.ResetRootCamera();
		cycle.DrawFillRectangle(Rectangle(-3, -2, 1, 1, 0xff804020));
		cycle.DrawFillRectangle(Rectangle(0, -1, 3, 2, 0xff204080));
		cycle.PostFrame();
		const auto pixels = primary->Capture(cycle.LastSubmission()->Identity);
		RequirePixel(pixels, 0, 0, {0, 0, 0, 255});
		RequirePixel(pixels, 1, 2, {64, 16, 4, 255});
		RequirePixel(pixels, 4, 3, {4, 16, 64, 255});
		RequirePixel(pixels, 6, 5, {4, 16, 64, 255});
		Require(cycle.CurrentCounters().DrawOperations == 2 && cycle.CurrentCounters().SubmittedElements == 8, "FillRect must submit two genuine nonindexed format0 strips through actual frame records");
		cycle.PreFrame();
		cycle.BeginDrawing();
		cycle.ResetRootCamera();
		cycle.DrawFillRectangle(Rectangle(-3, -2, 1, 1, 0x80804020));
		cycle.PostFrame();
		RequirePixel(primary->Capture(cycle.LastSubmission()->Identity), 1, 2, {16, 4, 1, 255});
		cycle.PreFrame();
		cycle.BeginDrawing();
		auto invalid = Rectangle(-3, -2, 1, 1, 0xffffffff);
		invalid.Vertices[0].X = std::numeric_limits<float>::quiet_NaN();
		RequireFailure([&] {
			cycle.DrawFillRectangle(invalid);
		},
		    "finite actual XY");
		Require(!cycle.LastSubmission(), "invalid FillRect must abort the actual frame instead of committing replacement pixels");
		cycle.PreFrame();
		cycle.BeginDrawing();
		cycle.ResetRootCamera();
		cycle.DrawFillRectangle(Rectangle(-3, -2, 1, 1, 0xffffffff));
		cycle.AbortFrame();
		Require(!cycle.LastSubmission(), "aborted FillRect must not publish a frame identity");
		cycle.PreFrame();
		cycle.BeginDrawing();
		cycle.ResetRootCamera();
		cycle.DrawFillRectangle(Rectangle(-3, -2, 1, 1, 0xffffffff));
		cycle.PostFrame();
		RequirePixel(primary->Capture(cycle.LastSubmission()->Identity), 1, 2, {255, 255, 255, 255});
	} catch (...) {
		cycle.AbortFrame();
		throw;
	}
	cycle.Close();
	workers.Close();
	const auto observed = sgre::render::SpritePassPresentationFixtureBridge::Observe(surface);
	Require(!observed.WindowVisible && !observed.WindowKey && observed.DrawableRequests == 0, "FillRect requested a visible window or OS drawable");
}

static void TestDirectSubmitAndExclusiveGeometry(const std::filesystem::path& shader) {
	sgre::render::SpritePassRenderer renderer(Description(8), shader);
	sgre::scene::MotionSpriteMaterial material;
	material.BlendMode = 0;
	auto solid_pipeline = sgre::scene::SelectSpritePipeline(material, {});
	solid_pipeline.NativeVertexShaderIndex = 3;
	solid_pipeline.NativePixelShaderIndex = 60;
	solid_pipeline.VertexColor = sgre::scene::SpriteVertexColor::Multiply;
	const auto solid = renderer.PreparePipeline(solid_pipeline);
	const auto packet = Rectangle(-1, -1, 1, 1, 0xff804020);
	sgre::render::SpritePassDraw draw;
	draw.Pipeline = solid;
	draw.FillRectangleVertices = packet.Vertices;
	draw.FillRectangleColor = packet.Color;
	const std::array<float, 16> identity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
	const auto submission = renderer.Submit(std::span(&draw, 1), identity);
	RequirePixel(renderer.Capture(submission), 2, 2, {64, 16, 4, 255});
	material.TextureAttributes = 4;
	auto raw_pipeline = sgre::scene::SelectSpritePipeline(material, {});
	raw_pipeline.NativeVertexShaderIndex = 4;
	const auto raw = renderer.PreparePipeline(raw_pipeline);
	sgre::resource::RgbaImage image;
	image.Width = 1;
	image.Height = 1;
	image.Pixels = {255, 255, 255, 255};
	const auto texture = renderer.UploadTexture(sgre::resource::PixelFormat::Bc7, image);
	const std::array<sgre::scene::RawTextureVertex, 4> raw_vertices{};
	draw.Pipeline = raw;
	draw.Texture = texture;
	draw.RawTextureVertices = raw_vertices;
	RequireFailure([&] {
		static_cast<void>(renderer.Submit(std::span(&draw, 1), identity));
	},
	    "RawTex requires four XYUV");
	sgre::render::SpriteFrameEncoder encoder(renderer);
	encoder.Begin({}, sgre::render::SpritePassLoadAction::Clear);
	RequireFailure([&] {
		encoder.Append(draw, identity);
	},
	    "RawTex requires four XYUV");
	encoder.Abort();
	renderer.ReleaseTexture(texture);
}

int main(int argc, char** argv) {
	try {
		if (argc != 3) {
			throw std::runtime_error("usage: fill_rectangle_render_tests sprite.metal presentation.metal");
		}
		TestActualFillRectangle(argv[1], argv[2]);
		TestDirectSubmitAndExclusiveGeometry(argv[1]);
		std::cout << "ACTUAL_FILLRECT_FORMAT0_VS3_PS60_COLOR_SQUARE_AND_ABORT=PASS\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}

	return 0;
}
