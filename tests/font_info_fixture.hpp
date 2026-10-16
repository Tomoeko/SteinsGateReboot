#pragma once

#include "sgre/resource/psb.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace font_info_test {
	template <typename Value>
	inline sgre::resource::PsbValue Atom(Value value) {
		sgre::resource::PsbValue result;
		result.Data = std::move(value);

		return result;
	}

	inline std::shared_ptr<sgre::resource::PsbDocument> Document() {
		using sgre::resource::PsbArray;
		using sgre::resource::PsbObject;
		PsbArray sources;
		for (std::uint32_t index = 0; index < 2; ++index) {
			sgre::resource::PsbChunkReference chunk;
			chunk.Index = index;
			auto source = Atom(PsbObject{{"type", Atom(std::string("A8"))}, {"width", Atom(std::int64_t{1})}, {"height", Atom(std::int64_t{1})}, {"truncated_width", Atom(std::int64_t{1})}, {"truncated_height", Atom(std::int64_t{1})}, {"pixel", Atom(chunk)}});
			source.SerializedOffset = 64 + index * 32;
			sources.push_back(std::move(source));
		}

		auto glyph = Atom(PsbObject{{"id", Atom(std::int64_t{0})}, {"x", Atom(std::int64_t{0})}, {"y", Atom(std::int64_t{0})}, {"width", Atom(std::int64_t{5})}, {"height", Atom(std::int64_t{6})}, {"w", Atom(std::int64_t{1})}, {"h", Atom(std::int64_t{1})}, {"a", Atom(std::int64_t{2})}, {"b", Atom(std::int64_t{9})}, {"d", Atom(std::int64_t{10})}});
		auto document = std::make_shared<sgre::resource::PsbDocument>();
		document->Header.Version = 4;
		document->Header.RootOffset = 44;
		document->Header.DataOffset = 128;
		document->SerializedSize = 130;
		document->Chunks = {{128, 1}, {129, 1}};
		document->Root = Atom(PsbObject{{"id", Atom(std::string("font"))}, {"spec", Atom(std::string("win"))}, {"version", Atom(1.0800000429153442)}, {"label", Atom(std::string("Synthetic font owner"))}, {"minWidth", Atom(std::int64_t{3})}, {"minHeight", Atom(std::int64_t{6})}, {"maxWidth", Atom(std::int64_t{8})}, {"maxHeight", Atom(std::int64_t{6})}, {"source", Atom(std::move(sources))}, {"code", Atom(PsbObject{{"A", std::move(glyph)}})}});
		document->Root.SerializedOffset = document->Header.RootOffset;

		return document;
	}

	inline std::shared_ptr<std::vector<std::uint8_t>> Backing() {
		auto backing = std::make_shared<std::vector<std::uint8_t>>(130);
		backing->at(128) = 128;
		backing->at(129) = 64;

		return backing;
	}

} // namespace font_info_test
