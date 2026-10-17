#pragma once

#include "sgre/scenario/scenario_search.hpp"

#include <memory>
#include <optional>
#include <string>

namespace sgre::render {
	class SpritePassPresentation;
}

namespace sgre::platform {

	class DebugInspector final {
	public:
		// Main-thread, in-window tooling. The interactive presentation outlives it.
		// Search owns an independent background catalog and never accesses the VM.
		DebugInspector(render::SpritePassPresentation& presentation, scenario::ScenarioSearchLocation location);
		~DebugInspector();
		DebugInspector(const DebugInspector&) = delete;
		DebugInspector& operator=(const DebugInspector&) = delete;
		void Toggle();
		void Hide();
		void Update();
		void SetJumpStatus(std::string message);
		[[nodiscard]] bool CapturesInput() const noexcept;
		[[nodiscard]] std::optional<std::string> TakeJumpRequest();

	private:
		class Implementation;
		std::unique_ptr<Implementation> m_Implementation;
	};

} // namespace sgre::platform
