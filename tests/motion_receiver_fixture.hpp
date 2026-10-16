#pragma once

#include "sgre/render/motion_sprite_record_receiver.hpp"

#include "sgre/core/frame_clock.hpp"
#include "sgre/scene/motion_manager_runtime.hpp"
#include "sgre/scene/motion_preparation_lease.hpp"
#include "sgre/scene/motion_selected_sprite_player.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

static_assert(!std::is_copy_constructible_v<sgre::render::MotionSpriteRecordReceiver>);
static_assert(!std::is_move_constructible_v<sgre::render::MotionSpriteRecordReceiver>);
static_assert(!std::is_copy_assignable_v<sgre::render::MotionSpriteRecordReceiver>);
static_assert(!std::is_move_assignable_v<sgre::render::MotionSpriteRecordReceiver>);

static void Require(bool condition, const char* message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

template <typename Operation>
static void RequireFailure(Operation operation, std::string_view fragment) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(fragment) != std::string_view::npos, error.what());

		return;
	}

	throw std::runtime_error("missing live receiver rejection: " + std::string(fragment));
}

static sgre::resource::PsbValue Integer(std::int64_t number) {
	sgre::resource::PsbValue result;
	result.Data = number;

	return result;
}

static sgre::resource::PsbValue Text(std::string text) {
	sgre::resource::PsbValue result;
	result.Data = std::move(text);

	return result;
}

static sgre::resource::MotionObject Character(std::string name, std::uint32_t color, std::uint32_t blend) {
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
		sgre::resource::PsbValue coordinates;
		coordinates.Data = sgre::resource::PsbArray{Integer(0), Integer(0), Integer(0)};
		frame.Content.Data = sgre::resource::PsbObject{{"mask", Integer(2 | 1 | 0x400 | 0x20000 | 0x200)}, {"src", Text("atlas")}, {"icon", Text("tile")}, {"coord", std::move(coordinates)}, {"ox", Integer(0)}, {"oy", Integer(0)}, {"opa", Integer(255)}, {"bm", Integer(blend)}, {"color", Integer(color)}};
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
	object.Name = std::move(name);
	object.Timelines.push_back(std::move(timeline));

	return object;
}

static std::shared_ptr<const sgre::resource::MotionResource> Resource() {
	auto resource = std::make_shared<sgre::resource::MotionResource>();
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
	source.Icons.push_back(std::move(icon));
	resource->Sources.push_back(std::move(source));
	resource->Objects.push_back(Character("red", 0xff0000ff, 0));
	resource->Objects.push_back(Character("blue", 0x0000ffff, 16));

	return resource;
}

static sgre::render::SpritePassDescription Description(std::size_t draws = 8) {
	sgre::render::SpritePassDescription result;
	result.Width = 8;
	result.Height = 8;
	result.MaxVertices = 64;
	result.MaxIndices = 128;
	result.MaxDraws = draws;
	result.MaxTextures = 4;
	result.MaxPipelines = 48;
	result.MaxTextureBytes = 4096;

	return result;
}

static sgre::scene::TextureCache::Upload Upload(sgre::render::SpritePassRenderer& renderer) {
	return [&renderer](auto format, const auto& image) {
		return renderer.UploadTexture(format, image);
	};
}

static sgre::scene::TextureCache::Release Release(sgre::render::SpritePassRenderer& renderer) {
	return [&renderer](auto texture) {
		renderer.ReleaseTexture(texture);
	};
}

struct PlayerFixture {
	// Direct preparation views remain borrowed until this actual selected player retires.
	sgre::scene::MotionPreparationLease PreparationOwner;
	sgre::scene::MotionPlayerCommands Commands;
	sgre::scene::MotionRootScene Root;
	sgre::scene::MotionPlayerRenderControls Controls;
	std::unique_ptr<sgre::scene::MotionSelectedSpritePlayer> Player;

	PlayerFixture(sgre::scene::MotionManagerRuntime& runtime, std::string_view character, sgre::resource::MotionPoint position, std::uint32_t filter) : Commands(runtime.Manager()) {
		Commands.SetCharacter(character);
		Commands.Play("main");
		Root.SetVisible(true);
		Root.SetCoordinate(position);
		Controls.ColorFilter = filter;
		const auto prepared = runtime.Prepare(*Commands.SelectedProject());
		PreparationOwner = prepared.Owner;
		sgre::scene::MotionSelectedSpriteServices services;
		services.ReadFrameDelta = [&runtime] {
			return runtime.ReadFrameDelta();
		};
		services.ReadDepthProjection = [&runtime] {
			return runtime.DepthProjection();
		};
		services.Construction.ReadManagerRandom = [&runtime](const auto& manager) {
			Require(&manager == &runtime.Manager(), "selected constructor lost its actual manager random owner");

			return runtime.NextUnitFloat();
		};
		services.Textures.FindTexture = [&runtime](const auto& binding) {
			return runtime.FindTexture(binding);
		};
		services.ResourceBorrowsLive = [&runtime] {
			return runtime.ResourceBorrowsLive();
		};
		services.DeliverEvent = [](const auto&) {
			throw std::runtime_error("unobserved authored test resource event");
		};
		Player = std::make_unique<sgre::scene::MotionSelectedSpritePlayer>(Commands, Root, Controls, prepared.Resource, *Commands.CachedTimeline(), prepared.Timeline, prepared.Sources, runtime.Identifiers(), runtime.Context(), std::move(services));
		Player->Update();
		Require(Player->PublishedRows().size() == 1, "actual selected numeric/source/geometry owner did not publish its single row");
	}
};

struct SceneFixture {
	sgre::scene::TextureCache Cache;
	sgre::scene::MotionResourceManager Manager;
	std::shared_ptr<const sgre::resource::MotionResource> Model = Resource();
	std::array<std::uint8_t, 16> Bytes;
	sgre::scene::MotionProjectIdentity Identity;
	sgre::scene::MotionProjectId Project;
	sgre::core::FrameClock Clock;
	std::array<std::uint32_t, sgre::scene::kMotionSharedSeedWords> Seed;
	std::vector<sgre::scene::MotionSourceAlias> Aliases;
	std::unique_ptr<sgre::scene::MotionManagerRuntime> Runtime;
	std::vector<std::unique_ptr<PlayerFixture>> Players;

	explicit SceneFixture(sgre::render::SpritePassRenderer& renderer) : Cache(Upload(renderer), Release(renderer)), Manager(Cache) {
		Bytes.fill(255);
		for (std::size_t index = 0; index < Seed.size(); ++index) {
			Seed[index] = static_cast<std::uint32_t>(index + 1);
		}
		Identity.DocumentInstance = Cache.AllocateDocumentIdentity();
		Identity.RootOffset = 100;
		Project = Manager.Register(Identity, Model, Bytes);
		sgre::scene::MotionManagerRuntimeServices services;
		services.ReadSourceAliases = [this](auto project) {
			Require(project == Project && Manager.Contains(project), "alias query lost its actual registered source owner");

			return std::span<const sgre::scene::MotionSourceAlias>(Aliases);
		};
		Runtime = std::make_unique<sgre::scene::MotionManagerRuntime>(Manager, Clock, Seed, std::move(services), 8);
	}

	~SceneFixture() noexcept {
		try {
			Runtime->Context().Clear();
			Players.clear();
			Runtime->Close();
			Manager.Close();
			Cache.Close();
		} catch (...) {
			std::terminate();
		}
	}

	PlayerFixture& Add(std::string_view character, sgre::resource::MotionPoint position, std::uint32_t filter = 0) {
		Players.push_back(std::make_unique<PlayerFixture>(*Runtime, character, position, filter));

		return *Players.back();
	}
};

struct Scratch {
	std::array<sgre::resource::MotionPoint, 64> Positions;
	std::array<sgre::resource::MotionPoint, 64> Uvs;
	std::array<std::uint32_t, 64> Colors;
	std::array<std::uint32_t, 128> Indices;
	std::array<sgre::scene::MotionMaskOperation, 8> Mask;
	std::array<sgre::scene::MotionMaskedBlendPass, 8> Blend;

	sgre::scene::MotionFrameScratch Borrow() {
		sgre::scene::MotionFrameScratch result;
		result.Batch = {Positions, Uvs, Colors, Indices};
		result.MaskOperations = Mask;
		result.BlendPasses = Blend;

		return result;
	}
};

[[maybe_unused]] static sgre::scene::LayerCameraFrameState ActualMonoFrameState(const sgre::render::SpriteGraphicsTargets& graphics) {
	const auto layout = graphics.CurrentLayout();
	sgre::scene::LayerCameraFrameState result;
	result.View = layout.Projection;
	result.Viewport.Width = static_cast<std::int32_t>(layout.Viewport.Width);
	result.Viewport.Height = static_cast<std::int32_t>(layout.Viewport.Height);
	result.RawScissor = layout.FullScissor;
	result.RoundedScissor = layout.FullScissor;
	result.AppliedRoundedScissor = layout.FullScissor;
	// This fixture begins the actual encoder at its real full target; later changes use the real camera scope.

	return result;
}

static void RequirePixel(const sgre::render::ColorPassPixels& pixels, std::uint32_t x, std::uint32_t y, const std::array<std::uint8_t, 4>& color) {
	const auto offset = (static_cast<std::size_t>(y) * pixels.Width + x) * 4;
	for (std::size_t index = 0; index < color.size(); ++index) {
		Require(pixels.Rgba[offset + index] == color[index], "actual packet receiver pixel differs from its contrasting owner/clip expectation");
	}
}

struct FailureCleanup {
	sgre::render::SpriteRenderRecords& Records;
	sgre::render::MotionSpriteRecordReceiver& Receiver;
	sgre::scene::MotionSpriteDrawContext& Context;

	~FailureCleanup() noexcept {
		try {
			if (Records.IsActive()) {
				Records.AbortFrame();
			}
			Context.Clear();
			Receiver.ReleaseFrameInputs();
		} catch (...) {
			std::terminate();
		}
	}
};
