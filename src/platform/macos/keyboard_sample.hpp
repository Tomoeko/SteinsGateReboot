#pragma once

#include "sgre/input/key_mapping.hpp"

#include <cstdint>

namespace sgre::platform {

	struct MacPhysicalKeyReader {
		// Synchronous non-owning OS boundary; Context encloses this one sample only.
		void* Context = nullptr;
		bool (*Read)(void*, std::uint16_t) = nullptr;
	};

	// Available must already come from the actual owned-window focus check. This helper never changes focus.
	// Keep the caller's function/mouse snapshot; validate all mappings before reading physical positions.
	void SampleMacPhysicalKeyboard(input::KeyboardSample& sample, const input::KeyMappings& actual_mappings, bool break_key_available, const MacPhysicalKeyReader& reader);

} // namespace sgre::platform
