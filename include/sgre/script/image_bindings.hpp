#pragma once

#include "sgre/scene/image_assets.hpp"
#include "sgre/script/object_bindings.hpp"
#include "sgre/script/psb_bindings.hpp"

#include <cstddef>
#include <memory>

namespace sgre::script {

	class ScriptImagePayload;

	class ScriptNativeImageBorrow {
	public:
		ScriptNativeImageBorrow() = default;
		[[nodiscard]] bool IsLive() const;
		// Immutable data ownership does not substitute for the caller's real script Image root.
		[[nodiscard]] std::shared_ptr<const scene::LoadedImageAsset> Asset() const;
		[[nodiscard]] scene::MotionTextureWrapper::Borrowed Source(std::size_t index) const;
		void SetFiltering(std::int32_t minification, std::int32_t magnification) const;

	private:
		std::weak_ptr<ScriptImagePayload> m_Payload;
		friend class ScriptImageBindings;
	};

	class ScriptImageBindings {
	public:
		// VM/Object/PSB/cache/catalog services outlive this binding and remain live through explicit VM Close.
		ScriptImageBindings(SquirrelVm& vm, ScriptObjectBindings& objects, ScriptPsbBindings& psb, scene::ImageAssetCatalog& assets, scene::TextureCache& cache, std::size_t max_instances = 1024);
		~ScriptImageBindings();
		ScriptImageBindings(const ScriptImageBindings&) = delete;
		ScriptImageBindings& operator=(const ScriptImageBindings&) = delete;
		[[nodiscard]] SquirrelValue ImageClass() const;
		[[nodiscard]] ScriptNativeImageBorrow BorrowNativeImage(const SquirrelValue& instance) const;
		void ReleaseInstance(const SquirrelValue& instance);

	private:
		class Implementation;
		std::shared_ptr<Implementation> m_Implementation;
	};

} // namespace sgre::script
