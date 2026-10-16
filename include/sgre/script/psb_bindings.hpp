#pragma once

#include "sgre/resource/psb.hpp"
#include "sgre/script/object_bindings.hpp"
#include "sgre/script/squirrel_vm.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace sgre::scene {
	class BitmapFontReceivingDocument;
	class MotionReceivingDocument;
	class ImageReceivingDocument;
} // namespace sgre::scene

namespace sgre::script {
	struct ScriptPsbDocumentAsset {
		std::shared_ptr<const resource::PsbDocument> Document;
		std::shared_ptr<const std::vector<std::uint8_t>> Backing;
		std::shared_ptr<const scene::BitmapFontReceivingDocument> Font;
		std::shared_ptr<const scene::MotionReceivingDocument> Motion;
		std::shared_ptr<const scene::ImageReceivingDocument> Image;
	};

	struct ScriptPsbValueAsset {
		ScriptPsbDocumentAsset Owner;
		// Borrowed node identity stays valid while Owner.Document remains alive and immutable.
		const resource::PsbValue* Value = nullptr;
	};

	struct ScriptPsbBindingLimits {
		std::size_t MaxDocuments = 4096;
		std::size_t MaxValues = 65536;
		std::size_t MaxIteratorKeys = 65536;
	};

	class ScriptPsbBindings {
	public:
		// VM and Object bindings outlive this owner. Documents remain immutable while wrapped.
		explicit ScriptPsbBindings(SquirrelVm& vm, ScriptObjectBindings& objects, ScriptPsbBindingLimits limits = {});
		~ScriptPsbBindings();
		ScriptPsbBindings(const ScriptPsbBindings&) = delete;
		ScriptPsbBindings& operator=(const ScriptPsbBindings&) = delete;
		ScriptPsbBindings(ScriptPsbBindings&&) = delete;
		ScriptPsbBindings& operator=(ScriptPsbBindings&&) = delete;

		[[nodiscard]] SquirrelValue ObjectClass() const;
		[[nodiscard]] SquirrelValue ValueClass() const;
		// Nested PSBValue instances retain the document owner; containers are never flattened into VM tables.
		[[nodiscard]] SquirrelValue WrapDocument(std::shared_ptr<const resource::PsbDocument> document, std::shared_ptr<const std::vector<std::uint8_t>> backing = {});
		// A typed purged font keeps its real resource/texture pin; it never supplies fictitious complete backing.
		[[nodiscard]] SquirrelValue WrapFontDocument(std::shared_ptr<const scene::BitmapFontReceivingDocument> document);
		[[nodiscard]] SquirrelValue WrapMotionDocument(std::shared_ptr<const scene::MotionReceivingDocument> document);
		[[nodiscard]] SquirrelValue WrapImageDocument(std::shared_ptr<const scene::ImageReceivingDocument> document);
		// Shares immutable data; native adapters separately retain the script owner for its native lifetime.
		[[nodiscard]] ScriptPsbDocumentAsset DocumentAsset(const SquirrelValue& instance) const;
		// Native resource adapters share immutable data without changing the VM's script-root ownership.
		[[nodiscard]] ScriptPsbValueAsset ValueAsset(const SquirrelValue& instance) const;

	private:
		class Implementation;
		std::shared_ptr<Implementation> m_Implementation;
	};
} // namespace sgre::script
