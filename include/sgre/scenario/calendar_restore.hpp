#pragma once

#include "sgre/resource/psb.hpp"
#include "sgre/scenario/scenario_search.hpp"

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>

namespace sgre::scenario {

	struct ScenarioCalendarCommand {
		std::string Storage;
		std::string SceneLabel;
		std::size_t CommandIndex = 0;
		std::size_t SnapshotIndex = 0;
		// Owned original doTag pairs, before its ordinary table conversion.
		std::array<std::pair<std::string, std::string>, 3> Attributes;
	};

	struct ScenarioCalendarRestorePlan {
		std::optional<ScenarioCalendarCommand> Command;
		std::string Reason;
	};

	// A deliberately narrow effect proof. Unsupported or ambiguous prefixes require
	// authentic saved state; they never select a date from another scene or filename.
	[[nodiscard]] ScenarioCalendarRestorePlan ReadScenarioCalendarRestore(const resource::PsbDocument& document, const ScenarioJumpPoint& target);
	// Startup-only convenience for a caller whose catalog already validated target.
	// Streams the exact canonical original entry; no extraction or config writes.
	[[nodiscard]] ScenarioCalendarRestorePlan ReadScenarioCalendarRestore(const ScenarioSearchLocation& location, const ScenarioJumpPoint& target);

} // namespace sgre::scenario
