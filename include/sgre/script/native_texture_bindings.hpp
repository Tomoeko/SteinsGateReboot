#pragma once

#include "sgre/scene/native_texture_reference.hpp"
#include "sgre/script/object_bindings.hpp"

#include <memory>

namespace sgre::scene {
	class ScreenRegistry;
	class TextureCache;
} // namespace sgre::scene

namespace sgre::script {
	class ScriptScreenBindings;
	class ScriptRawTextureBindings;
	class ScriptImageBindings;
	class ScriptNativeTexturePayload;

	class ScriptNativeTextureBorrow {
	public:
		ScriptNativeTextureBorrow() = default;
		// This weak native borrow does not root a VM object. The containing alternate-texture map retains that real root.
		[[nodiscard]] const scene::NativeTextureReference& Reference() const;

	private:
		std::weak_ptr<ScriptNativeTexturePayload> m_Payload;
		friend class ScriptNativeTextureBindings;
	};

	class ScriptNativeTextureBindings {
	public:
		// Real VM/source bindings/cache and same-domain renderer enclose this owner through VM Close.
		ScriptNativeTextureBindings(SquirrelVm& vm, ScriptObjectBindings& objects, ScriptScreenBindings& screen_bindings, scene::ScreenRegistry& screens, ScriptRawTextureBindings& raw, ScriptImageBindings& images, scene::TextureCache& cache, render::SpritePassRenderer& renderer);
		~ScriptNativeTextureBindings();
		ScriptNativeTextureBindings(const ScriptNativeTextureBindings&) = delete;
		ScriptNativeTextureBindings& operator=(const ScriptNativeTextureBindings&) = delete;
		[[nodiscard]] SquirrelValue NativeTextureClass() const;
		[[nodiscard]] ScriptNativeTextureBorrow BorrowNativeTexture(const SquirrelValue& instance) const;
		void ReleaseInstance(const SquirrelValue& instance);

	private:
		class Implementation;
		std::shared_ptr<Implementation> m_Implementation;
	};

} // namespace sgre::script
