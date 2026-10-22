#pragma once

#include "sgre/resource/psb.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sgre::scenario {

	struct ScenarioNavigationLocation {
		std::string Storage;
		std::string Label;
		std::int32_t SourceLine = 0;
		std::int32_t Point = 0;
		std::int32_t Record = 0;
	};

	struct ScenarioNavigationExit {
		std::string Storage;
		std::string Label;
		std::string Condition;
	};

	struct ScenarioNavigationScene {
		std::string Storage;
		std::string Label;
		std::vector<ScenarioNavigationLocation> Snapshots;
		std::vector<ScenarioNavigationExit> Exits;
		// Literal original trigger activations; event links are planner candidates, not executed exits.
		std::vector<std::string> PhoneTriggers;
	};

	// Launch tooling over the original scenario graph. Commands and conditions still
	// execute in the original player; this owner neither evaluates nor changes flags.
	class ScenarioNavigation {
	public:
		void AddDocument(const resource::PsbDocument& document);
		void AddPhoneMaster(const resource::PsbDocument& document);
		[[nodiscard]] bool Reaches(std::string_view storage, std::string_view label, std::string_view target_storage, std::string_view target_label) const;
		[[nodiscard]] std::vector<ScenarioNavigationLocation> Predecessors(const ScenarioNavigationLocation& target) const;
		// Follow only single unconditional dispatch links. A number is usable only when one literal
		// equality reaches the target without revisiting the interaction or its dispatch scene.
		[[nodiscard]] std::optional<std::string> PhoneNumbers(std::string_view storage, std::string_view label, std::string_view target_storage, std::string_view target_label) const;
		[[nodiscard]] const std::vector<ScenarioNavigationScene>& Scenes() const noexcept;

	private:
		bool m_PhoneMasterLoaded = false;
		std::vector<ScenarioNavigationScene> m_Scenes;
		std::vector<std::pair<std::string, std::vector<std::string>>> m_PhoneTargets;
	};

} // namespace sgre::scenario
