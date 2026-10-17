#include "sgre/script/game_bindings.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace sgre::script {
	void InstallGameResourceProfile(SquirrelVm& vm) {
		// Game bootstrap 0455c0 identifies supplied resources, independent of the rewrite's host backend.
		const auto root = vm.RootTable();
		// Native 0x140043980 stringifies slot 2 once; 0x14004a050 counts LF bytes before the first NUL.
		vm.NewSlot(root, std::string("countCR"), vm.CreateNativeClosure("countCR", [](SquirrelVm& active, std::span<const SquirrelValue> arguments) {
			if (arguments.size() < 2) {
				throw std::runtime_error("countCR requires its original text operand");
			}
			const auto text = active.ToString(arguments[1]);
			const auto prefix = std::string_view(text).substr(0, text.find('\0'));
			const auto count = std::count(prefix.begin(), prefix.end(), '\n');
			if (count > std::numeric_limits<std::int32_t>::max()) {
				throw std::runtime_error("countCR result exceeds its native signed integer range");
			}

			return SquirrelNativeResult::ReturnValue(static_cast<std::int32_t>(count));
		}));
		// The paired 0x14004cc10 helper removes LF only; carriage returns and UTF-8 bytes remain exact.
		vm.NewSlot(root, std::string("removeCR"), vm.CreateNativeClosure("removeCR", [](SquirrelVm& active, std::span<const SquirrelValue> arguments) {
			if (arguments.size() < 2) {
				throw std::runtime_error("removeCR requires its original text operand");
			}
			auto text = active.ToString(arguments[1]);
			text.resize(std::string_view(text).substr(0, text.find('\0')).size());
			std::erase(text, '\n');

			return SquirrelNativeResult::ReturnValue(std::move(text));
		}));
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
