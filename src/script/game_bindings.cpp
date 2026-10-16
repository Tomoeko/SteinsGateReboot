#include "sgre/script/game_bindings.hpp"

#include <bit>
#include <cmath>
#include <cstdint>
#include <span>
#include <string>

namespace sgre::script {
	void InstallGameResourceProfile(SquirrelVm& vm) {
		// Game bootstrap 0455c0 identifies supplied resources, independent of the rewrite's host backend.
		const auto root = vm.RootTable();
		// Native043850 ignores failed scalar conversion and extra operands; 049da0 keeps unordered NaN.
		vm.NewSlot(root, std::string("calcPitch"), vm.CreateNativeClosure("calcPitch", [](SquirrelVm&, std::span<const SquirrelValue> arguments) {
			float value = 0;
			if (arguments.size() > 1) {
				if (const auto* number = std::get_if<float>(&arguments[1])) {
					value = *number;
				} else if (const auto integer = TryNativeInteger(arguments[1])) {
					value = static_cast<float>(*integer);
				}
			}
			if (value <= 0) {
				value = 100;
			}
			const float ratio = value / 100.0f;
			// Original SSE log(2) returns this float; preserve the denominator's rounding.
			const float result = std::log(ratio) / std::bit_cast<float>(std::uint32_t{0x3f317218});

			return SquirrelNativeResult::ReturnValue(result);
		}));
		// Native046a3a registers this root function through0451f0;04b560 reads965000's initial1.
		vm.NewSlot(root, std::string("isCircleButtonOK"), vm.CreateNativeClosure("isCircleButtonOK", [](SquirrelVm&, auto) {
			return SquirrelNativeResult::ReturnValue(true);
		}));
		// Native0462a9 captures literal RET030f20 through043b70; this desktop target records no state.
		vm.NewSlot(root, std::string("setRecordEnable"), vm.CreateNativeClosure("setRecordEnable", [](SquirrelVm&, auto) {
			return SquirrelNativeResult{};
		}));
		vm.NewSlot(root, std::string("TARGET"), std::string("WIND3D11"));
		vm.NewSlot(root, std::string("ARCHIVE_INFO_EXT"), std::string(".psb.m"));
		vm.NewSlot(root, std::string("ARCHIVE_BODY_EXT"), std::string(".bin"));
	}
} // namespace sgre::script
