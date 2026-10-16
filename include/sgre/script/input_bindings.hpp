#pragma once

#include "sgre/input/frame_sample.hpp"
#include "sgre/input/function_keys.hpp"
#include "sgre/input/hub.hpp"
#include "sgre/input/key_mapping.hpp"
#include "sgre/input/mouse.hpp"
#include "sgre/script/object_bindings.hpp"

#include <functional>
#include <memory>

namespace sgre::core {
	class LooperFrame;
}

namespace sgre::scene {
	class ParallelTaskSet;
}

namespace sgre::script {

	class ScriptInputBindings {
	public:
		using ClientReader = std::function<input::ClientRectangle()>;
		using HubFrameReader = std::function<input::HubFrameSample(const input::InputHub&, const input::KeyMappings&)>;
		using MouseFrameReader = std::function<input::MouseSample()>;
		using FunctionKeyReader = std::function<input::FunctionKeySnapshot()>;
		using KeyboardTextReader = std::function<input::KeyboardTextSnapshot()>;
		using CursorRequest = std::function<void(input::MousePixels)>;

		// The VM, Object adapter, and session key mappings outlive this adapter.
		ScriptInputBindings(SquirrelVm& vm, ScriptObjectBindings& objects, input::KeyMappings& mappings);
		~ScriptInputBindings();
		ScriptInputBindings(const ScriptInputBindings&) = delete;
		ScriptInputBindings& operator=(const ScriptInputBindings&) = delete;
		ScriptInputBindings(ScriptInputBindings&&) = delete;
		ScriptInputBindings& operator=(ScriptInputBindings&&) = delete;

		[[nodiscard]] SquirrelValue InputHubClass() const;
		[[nodiscard]] SquirrelValue InputClass() const;
		// Device state borrows the actual Hub task, which retires after its wrapper marks it Retire.
		[[nodiscard]] input::InputHub& Hub(const SquirrelValue& instance);
		[[nodiscard]] input::InputDevice& Device(const SquirrelValue& instance);
		[[nodiscard]] input::MouseInput& Mouse(const SquirrelValue& instance);
		void ConfigureCursorRequests(ClientReader client, CursorRequest request);
		// The same-thread actual snapshot owner encloses this adapter; callbacks never advance input.
		void ConfigureFunctionKeys(FunctionKeyReader actual_snapshot);
		// Borrows the same window-event/VSync owner as FunctionKeyReader; roots never publish or consume it.
		void ConfigureKeyboardText(KeyboardTextReader actual_snapshot);
		// Configure before constructing any Hub/Input. The actual Driver Common.parallel (+68), Looper,
		// and readers enclose this adapter. Each Hub MTask survives wrapper release until physical retirement.
		// Each mouse wrapper separately owns its VSync registration; getters never sample host state.
		void ConfigureFrameOwners(core::LooperFrame& actual_looper, scene::ParallelTaskSet& actual_driver_parallel, HubFrameReader actual_hub, MouseFrameReader actual_mouse);
		void ReleaseInstance(const SquirrelValue& instance);

	private:
		class Implementation;
		std::shared_ptr<Implementation> m_Implementation;
	};

} // namespace sgre::script
