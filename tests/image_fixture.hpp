#pragma once

#include "sgre/scene/image_assets.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

static void Require(bool condition, const std::string& message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
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

static sgre::resource::PsbValue Object(sgre::resource::PsbObject value) {
	sgre::resource::PsbValue result;
	result.Data = std::move(value);

	return result;
}

static sgre::resource::PsbValue Array(sgre::resource::PsbArray value) {
	sgre::resource::PsbValue result;
	result.Data = std::move(value);

	return result;
}

static sgre::resource::PsbValue Texture(std::uint32_t identifier) {
	sgre::resource::PsbValue pixel;
	pixel.Data = sgre::resource::PsbChunkReference{identifier, false};
	auto texture = Object({{"type", Text("A8")}, {"width", Integer(1)}, {"height", Integer(1)}, {"truncated_width", Integer(1)}, {"truncated_height", Integer(1)}, {"pixel", pixel}});
	texture.SerializedOffset = 0x100u + identifier;

	return texture;
}

static sgre::resource::PsbValue Tile(std::uint32_t left, std::uint32_t top, std::uint32_t width, std::uint32_t height, std::uint32_t identifier) {
	return Object({{"left", Integer(left)}, {"top", Integer(top)}, {"width", Integer(width)}, {"height", Integer(height)}, {"image", Texture(identifier)}});
}

static sgre::resource::PsbValue Entry(std::string label, std::uint32_t width, std::uint32_t height, sgre::resource::PsbArray tiles) {
	return Object({{"label", Text(std::move(label))}, {"width", Integer(width)}, {"height", Integer(height)}, {"texture", Array(std::move(tiles))}});
}

static std::shared_ptr<const sgre::resource::PsbDocument> Document(sgre::resource::PsbArray entries) {
	auto document = std::make_shared<sgre::resource::PsbDocument>();
	document->Header.Version = 4;
	document->SerializedSize = 4;
	document->Chunks = {{0, 1}, {1, 1}, {2, 1}, {3, 1}};
	document->Root = Object({{"imageList", Array(std::move(entries))}});

	return document;
}

struct ImageFixture {
	std::shared_ptr<const std::vector<std::uint8_t>> Backing = std::make_shared<const std::vector<std::uint8_t>>(std::vector<std::uint8_t>{17, 18, 19, 20});
	std::array<bool, 4> Active{};
	std::array<std::uint32_t, 4> Generation{};
	std::vector<std::uint32_t> Released;
	std::size_t Uploads = 0;
	bool FailRelease = false;
	std::function<void()> OnDrop;
	sgre::scene::TextureCache Cache;
	sgre::scene::ImageAssetCatalog Catalog;

	ImageFixture() : Cache([this](sgre::resource::PixelFormat format, const sgre::resource::RgbaImage& image) {
		                 Require(format == sgre::resource::PixelFormat::Alpha8 && image.Width == 1 && image.Height == 1 && image.Pixels.size() == 4, "first-party source fixture uploads its actual decoded A8 sample");
		                 const auto identifier = static_cast<std::uint32_t>(image.Pixels[3] - 17);
		                 Require(identifier < Active.size() && !Active[identifier], "source upload has a distinct live platform owner");
		                 Active[identifier] = true;
		                 ++Uploads;

		                 return sgre::render::SpriteTextureHandle{identifier, ++Generation[identifier], this};
	                 },
	                     [this](sgre::render::SpriteTextureHandle handle) {
		                     Require(handle.Owner == this && handle.Index < Active.size() && Active[handle.Index] && handle.Generation == Generation[handle.Index], "source retirement matches its owned platform texture");
		                     Active[handle.Index] = false;
		                     Released.push_back(handle.Index);
		                     if (OnDrop) {
			                     OnDrop();
		                     }
		                     if (FailRelease) {
			                     FailRelease = false;
			                     throw std::runtime_error("measured fixture backend retirement failure");
		                     }
	                     }),
	                 Catalog(Cache) {
	}
};
