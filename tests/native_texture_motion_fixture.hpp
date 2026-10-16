#pragma once

#include "sgre/scene/motion_assets.hpp"
#include "sgre/scene/motion_scene_player.hpp"
#include "sgre/script/motion_bindings.hpp"
#include "sgre/script/motion_resource_owner.hpp"
#include "sgre/script/psb_bindings.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

template <typename Value>
static sgre::resource::PsbValue Atom(Value value) {
	sgre::resource::PsbValue result;
	result.Data = std::move(value);
	return result;
}

static sgre::resource::PsbValue Layer(std::string label, bool child) {
	sgre::resource::PsbArray frames;
	for (std::size_t index = 0; index != 2; ++index) {
		const auto content = Atom(sgre::resource::PsbObject{{"mask", Atom(std::int64_t{0x60603})}, {"src", Atom(std::string(child ? "child" : "atlas"))}, {"icon", Atom(std::string(child ? "main" : "tile"))}, {"coord", Atom(sgre::resource::PsbArray{Atom(child ? 2.0 : 0.0), Atom(0.0), Atom(0.0)})}, {"ox", Atom(0.0)}, {"oy", Atom(0.0)}, {"opa", Atom(std::int64_t{255})}, {"color", Atom(std::int64_t{0x808080ff})}, {"bm", Atom(std::int64_t{0x10})}, {"act", Atom(std::string("first"))}});
		frames.push_back(Atom(sgre::resource::PsbObject{{"time", Atom(index == 0 ? 0.0 : 8.0)}, {"type", Atom(index == 0 ? std::int64_t{2} : std::int64_t{0})}, {"content", content}}));
	}
	const auto uid = child ? "control" : (label == "nested" ? "nested-screen" : "screen");
	return Atom(sgre::resource::PsbObject{{"uid", Atom(std::string(uid))},
	    {"label", Atom(std::move(label))}, {"type", Atom(child ? std::int64_t{3} : std::int64_t{0})}, {"inheritMask", Atom(std::int64_t{4})}, {"coordinate", Atom(std::int64_t{0})}, {"frameList", Atom(std::move(frames))}, {"transformOrder", Atom(sgre::resource::PsbArray{Atom(std::int64_t{0}), Atom(std::int64_t{3}), Atom(std::int64_t{2}), Atom(std::int64_t{1})})}, {"children", Atom(sgre::resource::PsbArray{})}, {"stencilType", Atom(std::int64_t{0})}, {"meshTransform", Atom(std::int64_t{0})}, {"objTriPriority", Atom(std::int64_t{0})}, {"parameterize", Atom(std::monostate{})}, {"joinTarget", Atom(false)}, {"groundCorrection", Atom(false)}, {"motionIndependentLayerInherit", Atom(false)}});
}

static sgre::resource::PsbValue Timeline(bool child) {
	const auto order = child ? sgre::resource::PsbArray{Atom(std::int64_t{0})} : sgre::resource::PsbArray{Atom(std::int64_t{0}), Atom(std::int64_t{1})};
	const auto priority = Atom(sgre::resource::PsbObject{{"time", Atom(0.0)}, {"type", Atom(std::int64_t{1})}, {"content", Atom(order)}});
	const auto terminal = Atom(sgre::resource::PsbObject{{"time", Atom(8.0)}, {"type", Atom(std::int64_t{1})}, {"content", Atom(std::monostate{})}});
	auto layers = sgre::resource::PsbArray{Layer(child ? "nested" : "sprite", false)};
	if (!child) {
		layers.push_back(Layer("selected-child", true));
	}
	return Atom(sgre::resource::PsbObject{{"type", Atom(std::int64_t{0})}, {"bounds", Atom(sgre::resource::PsbObject{{"left", Atom(0.0)}, {"top", Atom(0.0)}, {"right", Atom(16.0)}, {"bottom", Atom(8.0)}})}, {"lastTime", Atom(8.0)}, {"loopTime", Atom(-1.0)}, {"layer", Atom(std::move(layers))}, {"priority", Atom(sgre::resource::PsbArray{priority, terminal})}, {"tag", Atom(sgre::resource::PsbArray{})}, {"parameter", Atom(std::monostate{})}, {"variable", Atom(std::monostate{})}, {"parameterize", Atom(std::monostate{})}});
}

static std::shared_ptr<sgre::resource::PsbDocument> Document() {
	sgre::resource::PsbChunkReference chunk;
	auto texture = Atom(sgre::resource::PsbObject{{"type", Atom(std::string("A8"))}, {"width", Atom(std::int64_t{4})}, {"height", Atom(std::int64_t{4})}, {"truncated_width", Atom(std::int64_t{4})}, {"truncated_height", Atom(std::int64_t{4})}, {"pixel", Atom(chunk)}});
	texture.SerializedOffset = 64;
	const auto icon = Atom(sgre::resource::PsbObject{{"left", Atom(0.0)}, {"top", Atom(0.0)}, {"width", Atom(2.0)}, {"height", Atom(2.0)}, {"originX", Atom(0.0)}, {"originY", Atom(0.0)}, {"attr", Atom(std::int64_t{2})}});
	const auto source = Atom(sgre::resource::PsbObject{{"texture", texture}, {"icon", Atom(sgre::resource::PsbObject{{"tile", icon}})}});
	const auto object = [](bool child) {
		return Atom(sgre::resource::PsbObject{{"type", Atom(std::int64_t{0})}, {"motion", Atom(sgre::resource::PsbObject{{"main", Timeline(child)}})}});
	};
	auto document = std::make_shared<sgre::resource::PsbDocument>();
	document->Header.Version = 4;
	document->Header.RootOffset = 44;
	document->Header.DataOffset = 128;
	document->SerializedSize = 144;
	document->Chunks = {{128, 16}};
	document->Root = Atom(sgre::resource::PsbObject{{"id", Atom(std::string("motion"))}, {"spec", Atom(std::string("win"))}, {"version", Atom(3.03)}, {"label", Atom(std::string("Actual task scene fixture"))}, {"easing", Atom(sgre::resource::PsbArray{})}, {"source", Atom(sgre::resource::PsbObject{{"atlas", source}})}, {"object", Atom(sgre::resource::PsbObject{{"character", object(false)}, {"child", object(true)}})}, {"sourceIconRenameMap", Atom(sgre::resource::PsbObject{})}});
	document->Root.SerializedOffset = 44;
	return document;
}
