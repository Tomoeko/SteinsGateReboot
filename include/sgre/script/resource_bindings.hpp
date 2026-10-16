#pragma once

#include "sgre/script/binary_bindings.hpp"
#include "sgre/script/object_bindings.hpp"
#include "sgre/script/psb_bindings.hpp"

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sgre::script {
	enum class ScriptResourceDataKind {
		Psb,
		Binary
	};

	enum class ScriptResourceRequestMode {
		PsbWrapped,
		BinaryWrapped,
		PsbRaw,
		BinaryRaw
	};

	[[nodiscard]] std::string ResourceReadFilename(std::string_view name, ScriptResourceRequestMode mode);

	struct ScriptResourceAsset {
		ScriptResourceDataKind Kind = ScriptResourceDataKind::Psb;
		std::shared_ptr<const resource::PsbDocument> Document;
		std::shared_ptr<const std::vector<std::uint8_t>> Backing;
		// Native Resource.find acquires a separate manager reference for each returned PSBObject.
		std::function<std::shared_ptr<const std::vector<std::uint8_t>>()> RetainBacking;
		std::string Error;
		// Concrete cache PSB producers acquire one independent find reference for both immutable aliases.
		std::function<ScriptPsbDocumentAsset()> RetainPsb;
		std::shared_ptr<const scene::BitmapFontReceivingDocument> Font;
		std::function<std::shared_ptr<const scene::BitmapFontReceivingDocument>()> RetainFont;
		std::shared_ptr<const scene::MotionReceivingDocument> Motion;
		std::function<std::shared_ptr<const scene::MotionReceivingDocument>()> RetainMotion;
		std::shared_ptr<const scene::ImageReceivingDocument> Image;
		std::function<std::shared_ptr<const scene::ImageReceivingDocument>()> RetainImage;
	};

	struct ScriptResourceBindingLimits {
		std::size_t MaxInstances = 4096;
		std::size_t MaxTargets = 1024;
		std::size_t MaxNameBytes = 4096;
	};

	class ScriptResourceBindings {
	public:
		using LoadPoll = std::function<std::optional<ScriptResourceAsset>()>;
		// Request schedules owned host work. Poll only observes prepared data; it must not decode or perform I/O.
		using AssetRequest = std::function<LoadPoll(std::string_view resource_name)>;
		using TypedAssetRequest = std::function<LoadPoll(std::string_view resource_name, ScriptResourceRequestMode mode)>;
		using ErrorSink = std::function<void(std::string_view resource_name, std::string_view error)>;
		ScriptResourceBindings(SquirrelVm& vm, ScriptObjectBindings& objects, ScriptPsbBindings& psb, AssetRequest request, ErrorSink errors, ScriptResourceBindingLimits limits = {});
		ScriptResourceBindings(SquirrelVm& vm, ScriptObjectBindings& objects, ScriptPsbBindings& psb, ScriptBinaryBindings& binary, TypedAssetRequest request, ErrorSink errors, ScriptResourceBindingLimits limits = {});
		~ScriptResourceBindings();
		ScriptResourceBindings(const ScriptResourceBindings&) = delete;
		ScriptResourceBindings& operator=(const ScriptResourceBindings&) = delete;
		ScriptResourceBindings(ScriptResourceBindings&&) = delete;
		ScriptResourceBindings& operator=(ScriptResourceBindings&&) = delete;

		[[nodiscard]] SquirrelValue ResourceClass() const;
		void ReleaseInstance(const SquirrelValue& instance);

	private:
		ScriptResourceBindings(SquirrelVm& vm, ScriptObjectBindings& objects, ScriptPsbBindings& psb, ScriptBinaryBindings* binary, TypedAssetRequest request, ErrorSink errors, ScriptResourceBindingLimits limits);
		class Implementation;
		std::shared_ptr<Implementation> m_Implementation;
	};
} // namespace sgre::script
