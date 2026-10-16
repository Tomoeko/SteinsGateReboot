#include "sgre/render/motion_sprite_frame.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

template <typename Operation>
static void RequireError(Operation operation, std::string_view message) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(message) != std::string_view::npos, error.what());

		return;
	}

	throw std::runtime_error("expected rejected frame: " + std::string(message));
}

static sgre::resource::PsbValue Integer(std::int64_t value) {
	sgre::resource::PsbValue result;
	result.Data = value;

	return result;
}

static sgre::resource::PsbValue Text(std::string value) {
	sgre::resource::PsbValue result;
	result.Data = std::move(value);

	return result;
}

static sgre::resource::PsbValue Coordinate() {
	sgre::resource::PsbValue result;
	result.Data = sgre::resource::PsbArray{Integer(2), Integer(2), Integer(0)};

	return result;
}

static std::shared_ptr<sgre::resource::MotionResource> Model(std::uint32_t original_icon_attributes = 0, std::optional<std::uint32_t> explicit_color = std::nullopt, std::uint32_t blend_mode = 0x10) {
	auto model = std::make_shared<sgre::resource::MotionResource>();
	sgre::resource::MotionSource source;
	source.Name = "atlas";
	source.Texture.SerializedValueOffset = 100;
	source.Texture.Format = sgre::resource::PixelFormat::Alpha8;
	source.Texture.Width = 4;
	source.Texture.Height = 4;
	source.Texture.TruncatedWidth = 4;
	source.Texture.TruncatedHeight = 4;
	source.Texture.PixelRange.Size = 16;
	sgre::resource::MotionIcon icon;
	icon.Name = "tile";
	icon.Width = 4;
	icon.Height = 4;
	icon.AttributeFlags = original_icon_attributes;
	source.Icons.push_back(std::move(icon));
	model->Sources.push_back(std::move(source));
	sgre::resource::MotionLayer layer;
	layer.Label = "sprite";
	layer.Type = 0;
	layer.InheritMask = 4;
	layer.TransformOrder = {0, 3, 2, 1};
	layer.Properties = {{"stencilType", Integer(0)}, {"objTriPriority", Integer(0)}, {"meshTransform", Integer(0)}, {"parameterize", sgre::resource::PsbValue{}}, {"joinTarget", Integer(0)}, {"groundCorrection", Integer(0)}};
	for (std::uint32_t index = 0; index < 3; ++index) {
		sgre::resource::MotionFrame frame;
		frame.Time = static_cast<float>(index * 4);
		frame.Type = index == 2 ? 0 : 2;
		frame.Content.Data = sgre::resource::PsbObject{{"mask", Integer(2 | 1 | 0x400 | 0x20000 | (explicit_color ? 0x200 : 0))}, {"src", Text("atlas")}, {"icon", Text("tile")}, {"coord", Coordinate()}, {"ox", Integer(0)}, {"oy", Integer(0)}, {"opa", Integer(255)}, {"bm", Integer(blend_mode)}};
		if (explicit_color) {
			std::get<sgre::resource::PsbObject>(frame.Content.Data).emplace_back("color", Integer(*explicit_color));
		}

		layer.Frames.push_back(std::move(frame));
	}
	sgre::resource::MotionTimeline timeline;
	timeline.Name = "main";
	timeline.LastTime = 8;
	timeline.Layers.push_back(std::move(layer));
	for (std::uint32_t index = 0; index < 2; ++index) {
		sgre::resource::MotionFrame priority;
		priority.Time = static_cast<float>(index * 8);
		priority.Type = 1;
		if (index == 0) {
			priority.Content.Data = sgre::resource::PsbArray{Integer(0)};
		}
		timeline.Priority.push_back(std::move(priority));
	}
	sgre::resource::MotionObject object;
	object.Name = "character";
	object.Timelines.push_back(std::move(timeline));
	model->Objects.push_back(std::move(object));

	return model;
}

static sgre::render::SpritePassDescription Description(std::size_t vertices = 64) {
	sgre::render::SpritePassDescription result;
	result.Width = 8;
	result.Height = 8;
	result.MaxVertices = vertices;
	result.MaxIndices = 128;
	result.MaxDraws = 16;
	result.MaxTextures = 8;
	result.MaxPipelines = 48;

	return result;
}

static constexpr std::array<float, 16> kPhysicalProjection = {0.25f, 0, 0, -1, 0, -0.25f, 0, 1, 0, 0, 0, 0.5f, 0, 0, 0, 1};
static constexpr std::array<float, 16> kIdentityProjection = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0.5f, 0, 0, 0, 1};
static constexpr std::array<std::uint32_t, 4> kStrip = {0, 2, 1, 3};

struct SceneFixture {
	sgre::scene::TextureCache Cache;
	sgre::scene::MotionResourceManager Manager;
	std::shared_ptr<sgre::resource::MotionResource> Resource;
	std::vector<std::uint8_t> Bytes;
	sgre::scene::MotionProjectIdentity Identity;
	sgre::scene::MotionProjectId Project;
	sgre::scene::MotionSourceRegistration Registration;
	std::unique_ptr<sgre::scene::PreparedMotionSourceRegistry> Registry;
	sgre::scene::PreparedMotionTimeline Prepared;
	sgre::scene::MotionPlayerCommands Commands;
	sgre::scene::MotionRootScene Root;
	sgre::scene::MotionPlayerRenderControls Render;
	sgre::scene::MotionLayerIdentifierPool Identifiers;
	sgre::scene::MotionSpriteDrawContext Context;
	std::unique_ptr<sgre::scene::MotionSelectedSpritePlayer> Player;
	std::unique_ptr<sgre::render::MotionSpriteFrameRenderer> Frame;

	explicit SceneFixture(sgre::render::SpritePassRenderer& renderer, std::uint32_t original_icon_attributes = 0, std::optional<std::uint32_t> explicit_color = std::nullopt, std::uint32_t blend_mode = 0x10) : Cache([&renderer](auto format, const auto& image) {
		                                                                                                                                                                                                             return renderer.UploadTexture(format, image);
	                                                                                                                                                                                                             },
	                                                                                                                                                                                                                 [&renderer](auto texture) {
		                                                                                                                                                                                                                 renderer.ReleaseTexture(texture);
	                                                                                                                                                                                                                 }),
	                                                                                                                                                                                                             Manager(Cache), Resource(Model(original_icon_attributes, explicit_color, blend_mode)), Bytes(256, 255), Commands(Manager), Context(16) {
		Identity.DocumentInstance = Cache.AllocateDocumentIdentity();
		Identity.RootOffset = 100;
		Project = Manager.Register(Identity, Resource, Bytes);
		Registration.Project = Identity;
		Registration.Sources = Resource->Sources;
		Registry = std::make_unique<sgre::scene::PreparedMotionSourceRegistry>(std::span<const sgre::scene::MotionSourceRegistration>(&Registration, 1));
		Prepared = sgre::scene::PrepareMotionTimeline(Resource->Objects[0].Timelines[0], *Resource);
		Commands.SetCharacter("character");
		Commands.Play("main");
		Root.SetVisible(true);
		sgre::scene::MotionSelectedSpriteServices services;
		services.ReadFrameDelta = [] {
			return 0.0f;
		};
		services.ReadDepthProjection = [] {
			return 0.0f;
		};
		services.Construction.ReadManagerRandom = [this](const auto& manager) {
			Require(&manager == &Manager, "selected construction lost its actual manager");

			return 0.5f;
		};
		services.Textures.FindTexture = [this](const auto& source) {
			Require(source.Project == Identity && source.Source == &Resource->Sources[0], "selected texture lookup lost original resource identity");

			return Manager.FindSource(Project, source.Source->Name);
		};
		services.DeliverEvent = [](const auto&) {
			throw std::runtime_error("fixture has no authored action/event producer");
		};
		Player = std::make_unique<sgre::scene::MotionSelectedSpritePlayer>(Commands, Root, Render, *Resource, Resource->Objects[0].Timelines[0], Prepared, *Registry, Identifiers, Context, std::move(services));
		Frame = std::make_unique<sgre::render::MotionSpriteFrameRenderer>(renderer, Cache, sgre::scene::SpritePipelineOptions{}, 1);
		Player->Update();
		Require(Player->Rows().size() == 1 && Player->PublishedRows().size() == 1, "actual selected sprite did not publish its persistent row");
		Frame->BindTexture(0, Player->Rows()[0].Texture);
	}

	~SceneFixture() {
		Context.Clear();
		Player.reset();
		Frame.reset();
		Manager.Close();
		Cache.Close();
	}
};

struct FrameInputs {
	std::array<sgre::resource::MotionPoint, 1> Sizes{{{4, 4}}};
	std::array<sgre::resource::MotionPoint, 64> Positions;
	std::array<sgre::resource::MotionPoint, 64> Uvs;
	std::array<std::uint32_t, 64> Colors;
	std::array<std::uint32_t, 128> Indices;
	std::array<sgre::scene::MotionMaskOperation, 8> Masks;
	std::array<sgre::scene::MotionMaskedBlendPass, 8> Blends;
	sgre::scene::MotionPreparedFrame Globals;
	sgre::scene::MotionIntegerScissor Device{0, 0, 8, 8};
	sgre::scene::MotionFrameScratch Scratch;

	FrameInputs() {
		Globals.TextureSizes = Sizes;
		Globals.Viewport = {8, 8};
		Globals.InitialScissor = {0, 0, 8, 8};
		Globals.AppliedRoundedScissor = Globals.InitialScissor;
		Globals.TargetBounds = {-4, -4, 4, 4};
		Globals.AxisScale = {1, 1};
		Scratch.Batch.Positions = Positions;
		Scratch.Batch.Uvs = Uvs;
		Scratch.Batch.Colors = Colors;
		Scratch.Batch.Indices = Indices;
		Scratch.MaskOperations = Masks;
		Scratch.BlendPasses = Blends;
	}
};

static void RequireRegion(const sgre::render::ColorPassPixels& pixels, std::uint32_t left, std::uint32_t top, std::uint32_t right, std::uint32_t bottom, std::array<std::uint8_t, 4> inside, std::array<std::uint8_t, 4> outside) {
	Require(pixels.Width == 8 && pixels.Height == 8 && pixels.Rgba.size() == 256, "capture dimensions or row packing differ");
	for (std::uint32_t y = 0; y < 8; ++y) {
		for (std::uint32_t x = 0; x < 8; ++x) {
			const auto& expected = x >= left && x < right && y >= top && y < bottom ? inside : outside;
			for (std::size_t lane = 0; lane < 4; ++lane) {
				const auto actual = pixels.Rgba[(static_cast<std::size_t>(y) * 8 + x) * 4 + lane];
				if (actual != expected[lane]) {
					throw std::runtime_error("independent integer coverage/channel expectation failed at " + std::to_string(x) + "," + std::to_string(y) + " lane " + std::to_string(lane) + " expected " + std::to_string(expected[lane]) + " actual " + std::to_string(actual));
				}
			}
		}
	}
}

static void TestActualSelectedFrames(sgre::render::SpritePassRenderer& renderer) {
	SceneFixture fixture(renderer);
	FrameInputs input;
	fixture.Player->Draw();
	const auto first = fixture.Frame->Submit(fixture.Context, input.Globals, input.Scratch, input.Device, kPhysicalProjection, {}, sgre::render::SpritePassLoadAction::Clear);
	Require(fixture.Context.Size() == 0, "accepted actual frame must consume its shared context queue");
	RequireRegion(renderer.Capture(first.Identity), 2, 2, 6, 6, {255, 255, 255, 255}, {});
	fixture.Player->Update();
	fixture.Player->Draw();
	input.Globals.AppliedRoundedScissor = {3, 3, 5, 5};
	input.Device = {3, 3, 5, 5};
	const auto clipped = fixture.Frame->Submit(fixture.Context, input.Globals, input.Scratch, input.Device, kPhysicalProjection, {}, sgre::render::SpritePassLoadAction::Clear);
	RequireRegion(renderer.Capture(clipped.Identity), 3, 3, 5, 5, {255, 255, 255, 255}, {});
	fixture.Player->Draw();
	fixture.Cache.SetAddress(fixture.Player->Rows()[0].Texture, 0, 0);
	RequireError([&] {
		static_cast<void>(fixture.Frame->Submit(fixture.Context, input.Globals, input.Scratch, input.Device, kPhysicalProjection, {}, sgre::render::SpritePassLoadAction::Clear));
	},
	    "refresh");
	Require(fixture.Context.Size() == 1, "stale binding preflight must preserve the actual queued row");
	fixture.Frame->BindTexture(0, fixture.Player->Rows()[0].Texture);
	input.Globals.AppliedRoundedScissor = input.Globals.InitialScissor;
	input.Device = {0, 0, 8, 8};
	const auto refreshed = fixture.Frame->Submit(fixture.Context, input.Globals, input.Scratch, input.Device, kPhysicalProjection, {}, sgre::render::SpritePassLoadAction::Clear);
	RequireRegion(renderer.Capture(refreshed.Identity), 2, 2, 6, 6, {255, 255, 255, 255}, {});
	fixture.Player->Draw();
	fixture.Frame->UnbindTexture(0);
	RequireError([&] {
		static_cast<void>(fixture.Frame->Submit(fixture.Context, input.Globals, input.Scratch, input.Device, kPhysicalProjection, {}, sgre::render::SpritePassLoadAction::Clear));
	},
	    "lacks");
	Require(fixture.Context.Size() == 1, "rejected packet reception must keep the actual scene borrows until clear/retry");
	fixture.Frame->BindTexture(0, fixture.Player->Rows()[0].Texture);
	const auto retry = fixture.Frame->Submit(fixture.Context, input.Globals, input.Scratch, input.Device, kPhysicalProjection, {}, sgre::render::SpritePassLoadAction::Clear);
	RequireRegion(renderer.Capture(retry.Identity), 2, 2, 6, 6, {255, 255, 255, 255}, {});
}

static sgre::resource::RgbaImage Solid(std::array<std::uint8_t, 4> color) {
	sgre::resource::RgbaImage result;
	result.Width = 1;
	result.Height = 1;
	result.Pixels.assign(color.begin(), color.end());

	return result;
}

static void TestPhysicalScissor(sgre::render::SpritePassRenderer& renderer) {
	const auto red = renderer.UploadTexture(sgre::resource::PixelFormat::Bc7, Solid({255, 0, 0, 255}));
	const auto blue = renderer.UploadTexture(sgre::resource::PixelFormat::Bc7, Solid({0, 0, 255, 255}));
	sgre::scene::MotionSpriteMaterial material;
	material.TextureFormat = sgre::resource::PixelFormat::Bc7;
	material.TextureAttributes = 4;
	material.BlendMode = 0;
	const auto pipeline = renderer.PreparePipeline(sgre::scene::SelectSpritePipeline(material, sgre::scene::SpritePipelineOptions{}));
	std::array<sgre::scene::MotionSpriteVertex, 4> vertices;
	vertices[0].Position = {-1, 1};
	vertices[1].Position = {1, 1};
	vertices[2].Position = {-1, -1};
	vertices[3].Position = {1, -1};
	for (auto& vertex : vertices) {
		vertex.Uv = {0.5f, 0.5f};
	}
	sgre::render::SpritePassDraw draw;
	draw.Texture = red;
	draw.Pipeline = pipeline;
	draw.Vertices = vertices;
	draw.Indices = kStrip;
	draw.Scissor = sgre::render::SpritePassScissor{2, 2, 4, 4};
	const auto clipped = renderer.Submit(std::span<const sgre::render::SpritePassDraw>(&draw, 1), kIdentityProjection);
	RequireRegion(renderer.Capture(clipped), 2, 2, 6, 6, {255, 0, 0, 255}, {});
	std::array<sgre::render::SpritePassDraw, 2> draws{draw, draw};
	draws[1].Texture = blue;
	draws[1].Scissor.reset();
	const auto restored = renderer.Submit(draws, kIdentityProjection);
	RequireRegion(renderer.Capture(restored), 0, 0, 8, 8, {0, 0, 255, 255}, {});
	draw.Scissor = sgre::render::SpritePassScissor{8, 8, 0, 0};
	const auto empty = renderer.Submit(std::span<const sgre::render::SpritePassDraw>(&draw, 1), kIdentityProjection, {0, 1, 0, 1});
	RequireRegion(renderer.Capture(empty), 0, 0, 8, 8, {0, 255, 0, 255}, {});
	// A skipped leading packet still advances all packed backend stream offsets.
	std::array<sgre::scene::MotionSpriteVertex, 4> inset = vertices;
	for (auto& vertex : inset) {
		vertex.Position.X *= 0.5f;
		vertex.Position.Y *= 0.5f;
	}
	draws[0] = draw;
	draws[1].Vertices = inset;
	draws[1].Scissor.reset();
	const auto skipped_first = renderer.Submit(draws, kIdentityProjection);
	RequireRegion(renderer.Capture(skipped_first), 2, 2, 6, 6, {0, 0, 255, 255}, {});
	draw.Scissor = sgre::render::SpritePassScissor{7, 0, 2, 8};
	RequireError([&] {
		static_cast<void>(renderer.Submit(std::span<const sgre::render::SpritePassDraw>(&draw, 1), kIdentityProjection));
	},
	    "scissor exceeds");
	renderer.ReleaseTexture(red);
	renderer.ReleaseTexture(blue);
}

static bool SameDevice(sgre::scene::MotionIntegerScissor left, sgre::scene::MotionIntegerScissor right) {
	return left.MinimumX == right.MinimumX && left.MinimumY == right.MinimumY && left.MaximumX == right.MaximumX && left.MaximumY == right.MaximumY;
}

static void TestDeviceCacheSeparation(sgre::render::SpritePassRenderer& renderer) {
	SceneFixture fixture(renderer);
	FrameInputs input;
	input.Globals.InitialScissor = {2.1f, 2.1f, 6.1f, 6.1f};
	input.Globals.AppliedRoundedScissor = {2, 2, 6, 6};
	input.Device = {1, 1, 5, 5};
	fixture.Player->Draw();
	const auto result = fixture.Frame->Submit(fixture.Context, input.Globals, input.Scratch, input.Device, kPhysicalProjection, {}, sgre::render::SpritePassLoadAction::Clear);
	RequireRegion(renderer.Capture(result.Identity), 2, 2, 5, 5, {255, 255, 255, 255}, {});
	Require(SameDevice(result.AppliedDevice, input.Device), "rounded-cache suppression must preserve the separate actual device rectangle");
}

static void TestTargetLoadRetry(const std::filesystem::path& shader) {
	sgre::render::SpritePassRenderer renderer(Description(), shader);
	SceneFixture fixture(renderer);
	FrameInputs input;
	fixture.Player->Draw();
	RequireError([&] {
		static_cast<void>(fixture.Frame->Submit(fixture.Context, input.Globals, input.Scratch, input.Device, kPhysicalProjection, {}, sgre::render::SpritePassLoadAction::Preserve));
	},
	    "initialized target");
	Require(fixture.Context.Size() == 1 && fixture.Context.IsBorrowing(*fixture.Player) && renderer.CanSubmit(), "backend rejection must preserve actual queued rows and borrows for retry");
	const auto retry = fixture.Frame->Submit(fixture.Context, input.Globals, input.Scratch, input.Device, kPhysicalProjection, {}, sgre::render::SpritePassLoadAction::Clear);
	RequireRegion(renderer.Capture(retry.Identity), 2, 2, 6, 6, {255, 255, 255, 255}, {});
	Require(fixture.Context.Size() == 0, "successful retry must consume the actual queued row");
	input.Device = {-3, -2, 9, 10};
	const auto empty_clear = fixture.Frame->Submit(fixture.Context, input.Globals, input.Scratch, input.Device, kPhysicalProjection, {0, 1, 0, 1}, sgre::render::SpritePassLoadAction::Clear);
	RequireRegion(renderer.Capture(empty_clear.Identity), 0, 0, 8, 8, {0, 255, 0, 255}, {});
	Require(SameDevice(empty_clear.AppliedDevice, input.Device), "empty clear must retain the unclamped device state");
	const auto empty_preserve = fixture.Frame->Submit(fixture.Context, input.Globals, input.Scratch, input.Device, kPhysicalProjection, {1, 0, 0, 1}, sgre::render::SpritePassLoadAction::Preserve);
	RequireRegion(renderer.Capture(empty_preserve.Identity), 0, 0, 8, 8, {0, 255, 0, 255}, {});
	Require(SameDevice(empty_preserve.AppliedDevice, input.Device), "empty preserve must retain actual device state without scene events");
}

static void TestCapacity(const std::filesystem::path& shader) {
	sgre::render::SpritePassRenderer renderer(Description(3), shader);
	SceneFixture fixture(renderer);
	FrameInputs input;
	fixture.Player->Draw();
	RequireError([&] {
		static_cast<void>(fixture.Frame->Submit(fixture.Context, input.Globals, input.Scratch, input.Device, kPhysicalProjection, {}, sgre::render::SpritePassLoadAction::Clear));
	},
	    "capacity");
	Require(fixture.Context.Size() == 1 && renderer.CanSubmit(), "capacity failure must preserve scene borrows and avoid committing a GPU frame");
	fixture.Context.Clear();
}

static void TestExplicitColorAndNativeKeys(sgre::render::SpritePassRenderer& renderer) {
	struct ColorCase {
		std::uint32_t Rgba;
		std::array<std::uint8_t, 4> Expected;
	};
	const std::array<ColorCase, 4> colors = {{{0xff0000ff, {255, 0, 0, 255}}, {0x00ff00ff, {0, 255, 0, 255}}, {0x0000ffff, {0, 0, 255, 255}}, {0x7bd14f83, {63, 107, 41, 131}}}};
	for (const auto& color : colors) {
		std::array<std::vector<std::uint8_t>, 4> captures;
		const std::array<std::uint32_t, 4> keys = {0, 2, 4, 6};
		for (std::size_t index = 0; index < keys.size(); ++index) {
			SceneFixture fixture(renderer, keys[index], color.Rgba, 0);
			FrameInputs input;
			const auto& source = fixture.Resource->Sources[0];
			const auto& row = fixture.Player->Rows()[0];
			Require(source.Icons[0].AttributeFlags == keys[index] && row.Command.Key == keys[index] && row.Command.Colors[0] == color.Rgba, "explicit original synthetic color/key must reach the actual persistent row unchanged");
			fixture.Player->Draw();
			const auto frame = fixture.Frame->Submit(fixture.Context, input.Globals, input.Scratch, input.Device, kPhysicalProjection, {}, sgre::render::SpritePassLoadAction::Clear);
			const auto pixels = renderer.Capture(frame.Identity);
			const auto expected = (keys[index] & 4) == 0 ? color.Expected : std::array<std::uint8_t, 4>{255, 255, 255, 255};
			RequireRegion(pixels, 2, 2, 6, 6, expected, {});
			captures[index] = pixels.Rgba;
			Require(fixture.Context.Size() == 0 && source.Icons[0].AttributeFlags == keys[index], "successful actual colored frame must consume rows without rewriting original synthetic icon flags");
		}
		Require(captures[0] == captures[1] && captures[2] == captures[3], "native bit2 must preserve complete ordinary RGBA pixels within each measured bit4 pair");
	}
}

static void TestUnsupportedKeyRecovery(sgre::render::SpritePassRenderer& renderer) {
	SceneFixture fixture(renderer, 8, 0xff0000ff, 0);
	FrameInputs input;
	fixture.Player->Draw();
	RequireError([&] {
		static_cast<void>(fixture.Frame->Submit(fixture.Context, input.Globals, input.Scratch, input.Device, kPhysicalProjection, {}, sgre::render::SpritePassLoadAction::Clear));
	},
	    "texture attributes");
	Require(fixture.Context.Size() == 1 && fixture.Context.IsBorrowing(*fixture.Player) && fixture.Player->Rows()[0].Command.Key == 8 && renderer.CanSubmit(), "unsupported bit8 must preserve the actual pending row/key/borrow and avoid GPU commit");
	fixture.Context.Clear();
	// Only this synthetic fixture re-prepares an admitted key after explicitly releasing its failed frame borrow.
	fixture.Resource->Sources[0].Icons[0].AttributeFlags = 0;
	fixture.Player->Update();
	fixture.Player->Draw();
	const auto recovered = fixture.Frame->Submit(fixture.Context, input.Globals, input.Scratch, input.Device, kPhysicalProjection, {}, sgre::render::SpritePassLoadAction::Clear);
	RequireRegion(renderer.Capture(recovered.Identity), 2, 2, 6, 6, {255, 0, 0, 255}, {});
	Require(fixture.Context.Size() == 0, "explicit clear/reprepare must recover a valid synthetic frame after unsupported-key rejection");
}

int main(int argc, char** argv) {
	try {
		Require(argc == 2 || (argc == 3 && std::string_view(argv[2]) == "--color-handoff"), "motion sprite frame tests require the authored Metal shader source and optional --color-handoff");
		sgre::render::SpritePassRenderer renderer(Description(), argv[1]);
		TestExplicitColorAndNativeKeys(renderer);
		TestUnsupportedKeyRecovery(renderer);
		if (argc == 3) {
			std::cout << "actual persistent motion RGBA/key0/2/4/6 color handoff checks passed\n";

			return 0;
		}

		TestActualSelectedFrames(renderer);
		TestPhysicalScissor(renderer);
		TestDeviceCacheSeparation(renderer);
		TestTargetLoadRetry(argv[1]);
		TestCapacity(argv[1]);
		std::cout << "actual selected motion/Metal packets, physical scissor, lifetime and capacity tests passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
