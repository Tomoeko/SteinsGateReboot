#pragma once

#include "sgre/input/device.hpp"

#include <span>
#include <string_view>
#include <vector>

namespace sgre::input {

	// These codes preserve the original keybind vocabulary at a portable platform boundary.
	// They are compatibility identifiers, not assumptions about the host keyboard API.
	enum class KeyboardCodeSet : std::uint8_t {
		Scan,
		Virtual
	};

	struct NamedPhysicalKey {
		std::string_view Name;
		std::uint8_t Code = 0;
		KeyboardCodeSet CodeSet = KeyboardCodeSet::Scan;
	};

	struct KeyConversion {
		std::uint8_t Source = 0;
		std::uint32_t LogicalButtons = 0;
		std::uint32_t ControllerButtons = 0;
	};

	struct KeyboardSample {
		std::array<std::uint8_t, kPhysicalKeyCount> ScanKeys{};
		std::array<std::uint8_t, kPhysicalKeyCount> VirtualKeys{};
		bool Available = false;
		bool BackPulse = false;
	};

	struct ConvertedKeyboard {
		std::uint32_t LogicalButtons = 0;
		std::uint32_t ControllerButtons = 0;
	};

	[[nodiscard]] std::span<const NamedPhysicalKey> PhysicalKeyNames();

	class KeyMappings {
	public:
		explicit KeyMappings(std::size_t maximum_conversions = 1024);
		void Clear();
		void SetDefault();
		// A known source with unknown/empty targets appends a zero mapping, as in the original.
		[[nodiscard]] bool Add(std::string_view source, std::string_view logical_target, std::string_view controller_target);
		[[nodiscard]] ConvertedKeyboard Convert(const KeyboardSample& sample) const;
		[[nodiscard]] std::span<const KeyConversion> ScanConversions() const;
		[[nodiscard]] std::span<const KeyConversion> VirtualConversions() const;

	private:
		std::size_t m_MaximumConversions = 0;
		std::vector<KeyConversion> m_Scan;
		std::vector<KeyConversion> m_Virtual;
	};

} // namespace sgre::input
