#pragma once

#include "sgre/input/key_mapping.hpp"

#include <array>
#include <cstdint>

namespace sgre::platform {

	struct MacPhysicalKeyReader {
		// Synchronous non-owning OS boundary; Context encloses this one sample only.
		void* Context = nullptr;
		// Established only by a flags-changed event for the Fn physical key itself.
		bool FunctionHeld = false;
		bool (*Read)(void*, std::uint16_t) = nullptr;
	};

	void UpdateMacFunctionModifier(bool& held, std::uint16_t code, bool flags_changed, bool function_flag) noexcept;

	// Available must already come from the actual owned-window focus check. This helper never changes focus.
	// macOS F1-F12 require the physical Fn key, independently of AppKit's function-key character flag.
	void SampleMacFunctionKeyboard(input::KeyboardSample& sample, const std::array<std::uint16_t, 24>& codes, const MacPhysicalKeyReader& reader);
	// Preserve unrelated snapshot fields and enforce Fn; validate all mappings before reading physical positions.
	void SampleMacPhysicalKeyboard(input::KeyboardSample& sample, const input::KeyMappings& actual_mappings, bool break_key_available, const MacPhysicalKeyReader& reader);

} // namespace sgre::platform
