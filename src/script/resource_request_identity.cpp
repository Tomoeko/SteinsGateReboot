#include "sgre/script/resource_bindings.hpp"

#include <stdexcept>

namespace sgre::script {
	std::string ResourceReadFilename(std::string_view name, ScriptResourceRequestMode mode) {
		if (name.empty() || name.size() > 65536 || name.find('\0') != std::string_view::npos) {
			throw std::runtime_error("Resource read identity is empty, contains NUL, or exceeds its bound");
		}

		if (mode == ScriptResourceRequestMode::PsbWrapped || mode == ScriptResourceRequestMode::BinaryWrapped) {
			return std::string(name) + ".m";
		}
		if (mode == ScriptResourceRequestMode::PsbRaw || mode == ScriptResourceRequestMode::BinaryRaw) {
			return std::string(name);
		}

		throw std::runtime_error("Resource read mode is unsupported");
	}

} // namespace sgre::script
