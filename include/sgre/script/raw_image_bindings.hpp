#pragma once

#include "sgre/scene/raw_image.hpp"
#include "sgre/script/object_bindings.hpp"

#include <cstddef>
#include <memory>

namespace sgre::script {

	class ScriptBinaryBindings;
	class ScriptStructBindings;

	struct ScriptRawImageLimits {
		std::size_t MaxInstances = 1024;
		std::size_t MaxPixelBytes = 256 * 1024 * 1024;
	};

	class ScriptRawImageBindings {
	public:
		// VM, Object and immutable Binary services outlive instance retirement and VM Close.
		ScriptRawImageBindings(SquirrelVm& vm, ScriptObjectBindings& objects, ScriptBinaryBindings& binary, ScriptRawImageLimits limits = {});
		~ScriptRawImageBindings();
		ScriptRawImageBindings(const ScriptRawImageBindings&) = delete;
		ScriptRawImageBindings& operator=(const ScriptRawImageBindings&) = delete;
		ScriptRawImageBindings(ScriptRawImageBindings&&) = delete;
		ScriptRawImageBindings& operator=(ScriptRawImageBindings&&) = delete;

		[[nodiscard]] SquirrelValue RawImageClass() const;
		// Configure before image construction; the mutable Struct service encloses all store callbacks.
		void ConfigureStorageOwners(ScriptStructBindings& actual_structs);
		// Borrows the actual mutable image until its script instance retires.
		[[nodiscard]] const scene::RawImage& Image(const SquirrelValue& instance) const;
		void StoreThumbnail(const SquirrelValue& instance, std::span<const std::uint8_t> bgra);
		void ReleaseInstance(const SquirrelValue& instance);

	private:
		class Implementation;
		std::shared_ptr<Implementation> m_Implementation;
	};

} // namespace sgre::script
