#pragma once

#include "sgre/resource/struct_storage.hpp"
#include "sgre/script/binary_bindings.hpp"
#include "sgre/script/object_bindings.hpp"
#include "sgre/script/psb_bindings.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace sgre::script {
	struct ScriptStructBindingLimits {
		std::size_t MaxDocuments = 4096;
		std::size_t MaxValues = 65536;
		resource::StructStorageLimits Storage;
	};

	class ScriptStructBindings {
	public:
		// VM, Object, PSB and Binary adapters outlive this owner; container views retain their actual Struct receiver.
		ScriptStructBindings(SquirrelVm& vm, ScriptObjectBindings& objects, ScriptPsbBindings& psb, ScriptBinaryBindings& binary, ScriptStructBindingLimits limits = {});
		~ScriptStructBindings();
		ScriptStructBindings(const ScriptStructBindings&) = delete;
		ScriptStructBindings& operator=(const ScriptStructBindings&) = delete;
		ScriptStructBindings(ScriptStructBindings&&) = delete;
		ScriptStructBindings& operator=(ScriptStructBindings&&) = delete;
		[[nodiscard]] SquirrelValue StructClass() const;
		[[nodiscard]] SquirrelValue ValueClass() const;
		// Borrow the actual live schema owner; the caller keeps this exact VM document receiver alive through use.
		[[nodiscard]] resource::StructStorage& BorrowStructStorage(const SquirrelValue& instance);
		[[nodiscard]] std::size_t CharacterArraySize(const SquirrelValue& instance) const;
		void StoreCharacterArray(const SquirrelValue& instance, std::span<const std::uint8_t> bytes);

	private:
		class Implementation;
		std::shared_ptr<Implementation> m_Implementation;
	};
} // namespace sgre::script
