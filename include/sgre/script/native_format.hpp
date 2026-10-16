#pragma once

#include "sgre/script/squirrel_vm.hpp"

#include <cstddef>
#include <functional>
#include <span>
#include <string>
#include <string_view>

namespace sgre::script {
	[[nodiscard]] std::string FormatNativeString(SquirrelVm& vm, std::string_view format, std::span<const SquirrelValue> values, std::size_t max_output_bytes = 1024 * 1024);

	// Printing is routed to the host's bounded log; the formatter does not write to a terminal or OS device.
	using NativePrintCallback = std::function<void(std::string_view, bool)>;
	void InstallNativePrinting(SquirrelVm& vm, NativePrintCallback output, std::size_t max_output_bytes = 1024 * 1024);
} // namespace sgre::script
