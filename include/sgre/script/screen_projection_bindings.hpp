#pragma once

#include "sgre/render/sprite_pass.hpp"
#include "sgre/scene/screen_projection_packet.hpp"
#include "sgre/script/screen_bindings.hpp"

#include <functional>
#include <memory>

namespace sgre::script {
	class ScriptScreenProjectionBindings {
	public:
		using DrawProjection = std::function<void(render::SpriteTextureHandle, const scene::ScreenProjectionPacket&)>;
		// The genuine VM/Layer/Screen/graphics services enclose all derived wrappers and their actual tasks.
		ScriptScreenProjectionBindings(SquirrelVm& vm, ScriptLayerBindings& layers, ScriptScreenBindings& screen_bindings, scene::ScreenRegistry& screens, DrawProjection drawing);
		~ScriptScreenProjectionBindings();
		ScriptScreenProjectionBindings(const ScriptScreenProjectionBindings&) = delete;
		ScriptScreenProjectionBindings& operator=(const ScriptScreenProjectionBindings&) = delete;
		[[nodiscard]] SquirrelValue ProjectionClass() const;
		[[nodiscard]] SquirrelValue ChromaticProjectionClass() const;

	private:
		class Implementation;
		std::shared_ptr<Implementation> m_Implementation;
	};
} // namespace sgre::script
