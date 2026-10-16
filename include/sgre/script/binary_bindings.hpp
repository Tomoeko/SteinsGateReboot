#pragma once

#include "sgre/script/object_bindings.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace sgre::script {
	struct ScriptBinaryBindingLimits {
		std::size_t MaxInstances = 4096;
		std::size_t MaxBufferBytes = 160 * 1024 * 1024;
	};

	class ScriptBinaryBindings {
	public:
		// VM and Object bindings outlive this adapter. Wrapped buffers remain immutable and strongly owned.
		ScriptBinaryBindings(SquirrelVm& vm, ScriptObjectBindings& objects, ScriptBinaryBindingLimits limits = {});
		~ScriptBinaryBindings();
		ScriptBinaryBindings(const ScriptBinaryBindings&) = delete;
		ScriptBinaryBindings& operator=(const ScriptBinaryBindings&) = delete;
		ScriptBinaryBindings(ScriptBinaryBindings&&) = delete;
		ScriptBinaryBindings& operator=(ScriptBinaryBindings&&) = delete;

		[[nodiscard]] SquirrelValue BinaryClass() const;
		// Borrows the retained immutable character bytes until this instance is released.
		[[nodiscard]] std::span<const std::uint8_t> Bytes(const SquirrelValue& instance) const;
		[[nodiscard]] SquirrelValue WrapBuffer(std::shared_ptr<const std::vector<std::uint8_t>> backing);

	private:
		class Implementation;
		std::shared_ptr<Implementation> m_Implementation;
	};
} // namespace sgre::script
