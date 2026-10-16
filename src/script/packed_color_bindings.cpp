#include "sgre/script/packed_color_bindings.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace sgre::script {
	enum class PackedColorConversion : std::uint8_t {
		ArgbToRgba = 1,
		RgbaToArgb = 2
	};

	static std::array<std::uint8_t, 8> PackedColorMetadata(PackedColorConversion conversion) {
		return {static_cast<std::uint8_t>(conversion), 0, 0, 0, 0, 0, 0, 0};
	}

	void RegisterPackedColorFunctions(SquirrelVm& vm) {
		for (const auto conversion : {PackedColorConversion::ArgbToRgba, PackedColorConversion::RgbaToArgb}) {
			const std::string name = conversion == PackedColorConversion::ArgbToRgba ? "ARGB2RGBA" : "RGBA2ARGB";
			const std::array<SquirrelValue, 1> captures{vm.CreateUserData(PackedColorMetadata(conversion))};
			const auto callback = vm.CreateNativeClosureWithCaptures(name, [conversion](SquirrelVm& active, std::span<const SquirrelValue>) {
				const auto metadata = active.NativeStackValue(-1);
				if (active.Type(metadata) != SquirrelValueType::UserData) {
					throw std::runtime_error("packed color conversion requires its native function userdata");
				}
				const auto bytes = active.UserDataBytes(metadata);
				const auto expected = PackedColorMetadata(conversion);
				if (bytes.size() != expected.size() || !std::equal(bytes.begin(), bytes.end(), expected.begin())) {
					throw std::runtime_error("packed color conversion function identity is invalid");
				}

				// Native0437e0 ignores222ff0 conversion failure, preserving its initialized zero argument.
				const auto input = std::bit_cast<std::uint32_t>(TryNativeInteger(active.NativeAllocatedStackValue(2)).value_or(0));
				const auto output = conversion == PackedColorConversion::ArgbToRgba ? std::rotl(input, 8) : std::rotr(input, 8);

				return SquirrelNativeResult::ReturnValue(std::bit_cast<std::int32_t>(output));
			},
			    captures);
			vm.NewSlot(vm.RootTable(), name, callback);
		}
	}
} // namespace sgre::script
