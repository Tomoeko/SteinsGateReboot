#pragma once

#include "sgre/scene/emote_player_task.hpp"
#include "sgre/script/emote_bindings.hpp"
#include "sgre/script/motion_bindings.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>

namespace sgre::script {
	class ScriptPsbBindings;

	struct ScriptEmoteBindingServices {
		// Same observational Layer runtime/asynchronous services as the actual Motion factory.
		ScriptMotionBindingServices Motion;
		// Reads actual platform entropy only; it cannot mutate/reenter VM, Layer or runtime ownership.
		std::function<std::uint32_t()> ReadEntropyWord;
		// One application-owned lazy stream encloses every task, including deferred physical retirement.
		scene::EmoteWindRandom* WindRandom = nullptr;
	};

	class ScriptEmoteBindings {
	public:
		// The real VM/Object/Layer/PSB, registered document and selected runtimes enclose every task.
		// This installs the native declaration facet plus genuinely recovered constructor/control routes.
		ScriptEmoteBindings(SquirrelVm& vm, ScriptObjectBindings& objects, ScriptLayerBindings& layers, ScriptPsbBindings& psb, ScriptEmoteBindingServices services, std::size_t maximum_instances = 65536);
		~ScriptEmoteBindings() noexcept;
		ScriptEmoteBindings(const ScriptEmoteBindings&) = delete;
		ScriptEmoteBindings& operator=(const ScriptEmoteBindings&) = delete;
		[[nodiscard]] SquirrelValue EmoteClass() const;
		[[nodiscard]] scene::EmotePlayerTask Task(const SquirrelValue& instance) const;
		[[nodiscard]] std::size_t InstanceCount() const noexcept;
		void ReleaseInstance(const SquirrelValue& instance);
		[[nodiscard]] bool CanClose() const noexcept;
		void Close();

	private:
		class Implementation;
		std::shared_ptr<Implementation> m_Implementation;
	};
} // namespace sgre::script
