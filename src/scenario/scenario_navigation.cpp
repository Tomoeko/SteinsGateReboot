#include "sgre/scenario/scenario_navigation.hpp"

#include <algorithm>
#include <limits>
#include <map>
#include <queue>
#include <stdexcept>
#include <utility>

namespace sgre::scenario {

	static constexpr std::size_t kMaximumScenes = 65536;
	static constexpr std::size_t kMaximumSnapshots = 2000000;
	static constexpr std::size_t kMissingScene = std::numeric_limits<std::size_t>::max();

	static std::string NavigationText(const resource::PsbValue* value, bool optional = false) {
		if (optional && (value == nullptr || std::holds_alternative<std::monostate>(value->Data))) {
			return {};
		}
		const auto* text = value == nullptr ? nullptr : std::get_if<std::string>(&value->Data);
		if (text == nullptr || text->size() > 4096 || text->find('\0') != std::string::npos) {
			throw std::runtime_error("scenario navigation requires a bounded original string");
		}

		return *text;
	}

	static const resource::PsbArray& NavigationArray(const resource::PsbValue* value) {
		const auto* array = value == nullptr ? nullptr : std::get_if<resource::PsbArray>(&value->Data);
		if (array == nullptr) {
			throw std::runtime_error("scenario navigation requires an original array");
		}

		return *array;
	}

	static std::int32_t NavigationInteger(const resource::PsbValue& value) {
		const auto* integer = std::get_if<std::int64_t>(&value.Data);
		if (integer == nullptr || *integer < 0 || *integer > std::numeric_limits<std::int32_t>::max()) {
			throw std::runtime_error("scenario navigation requires a nonnegative original integer");
		}

		return static_cast<std::int32_t>(*integer);
	}

	void ScenarioNavigation::AddDocument(const resource::PsbDocument& document) {
		if (document.Root.Find("scenes") == nullptr) {
			return;
		}
		const auto storage = NavigationText(document.Root.Find("name"));
		if (storage.empty() || std::any_of(m_Scenes.begin(), m_Scenes.end(), [&](const auto& scene) {
			    return scene.Storage == storage;
		    })) {
			throw std::runtime_error("scenario navigation storage is empty or duplicated");
		}
		const auto& scenes = NavigationArray(document.Root.Find("scenes"));
		if (scenes.empty() || scenes.size() > kMaximumScenes - m_Scenes.size()) {
			throw std::runtime_error("scenario navigation scene count exceeds its bound");
		}
		std::vector<ScenarioNavigationScene> pending;
		std::size_t snapshots = 0;
		for (const auto& scene : m_Scenes) {
			snapshots += scene.Snapshots.size();
		}
		for (const auto& value : scenes) {
			ScenarioNavigationScene scene;
			scene.Storage = storage;
			scene.Label = NavigationText(value.Find("label"));
			if (scene.Label.empty() || scene.Label.front() != '*' || std::any_of(pending.begin(), pending.end(), [&](const auto& prior) {
				    return prior.Label == scene.Label;
			    })) {
				throw std::runtime_error("scenario navigation label is empty, invalid or duplicated");
			}
			const auto& lines = NavigationArray(value.Find("lines"));
			if (lines.size() > kMaximumSnapshots) {
				throw std::runtime_error("scenario navigation record count exceeds its bound");
			}
			for (std::size_t index = 0; index < lines.size(); ++index) {
				const auto* row = std::get_if<resource::PsbArray>(&lines[index].Data);
				if (row == nullptr || row->empty() || !std::holds_alternative<std::int64_t>(row->front().Data)) {
					continue;
				}
				if (row->size() < 5 || snapshots == kMaximumSnapshots) {
					throw std::runtime_error("scenario navigation snapshot is truncated or exceeds its bound");
				}
				++snapshots;
				scene.Snapshots.push_back({storage, scene.Label, NavigationInteger((*row)[4]), NavigationInteger(row->front()), static_cast<std::int32_t>(index)});
			}
			const auto* nexts = value.Find("nexts");
			const resource::PsbArray no_exits;
			const auto& exits = nexts == nullptr ? no_exits : NavigationArray(nexts);
			if (exits.size() > 4096) {
				throw std::runtime_error("scenario navigation exit count exceeds its bound");
			}
			for (const auto& exit : exits) {
				const auto* type = exit.Find("type");
				if (type == nullptr || NavigationInteger(*type) > 1) {
					throw std::runtime_error("scenario navigation exit type is not recovered");
				}
				if (NavigationInteger(*type) == 0) {
					scene.Exits.push_back({NavigationText(exit.Find("storage")), NavigationText(exit.Find("target"), true), NavigationText(exit.Find("eval"), true)});
				}
			}
			pending.push_back(std::move(scene));
		}
		m_Scenes.insert(m_Scenes.end(), std::make_move_iterator(pending.begin()), std::make_move_iterator(pending.end()));
	}

	class NavigationIndex {
	public:
		explicit NavigationIndex(const std::vector<ScenarioNavigationScene>& scenes) : Scenes(scenes), Previous(scenes.size()) {
			for (std::size_t index = 0; index < scenes.size(); ++index) {
				Labels.emplace(std::pair{scenes[index].Storage, scenes[index].Label}, index);
				First.try_emplace(scenes[index].Storage, index);
			}
			for (std::size_t index = 0; index < scenes.size(); ++index) {
				for (const auto& exit : scenes[index].Exits) {
					const auto target = Find(exit.Storage, exit.Label);
					if (target != kMissingScene) {
						Previous[target].push_back(index);
					}
				}
			}
		}

		[[nodiscard]] std::size_t Find(std::string_view storage, std::string_view label) const {
			if (label.empty()) {
				const auto found = First.find(std::string(storage));

				return found == First.end() ? kMissingScene : found->second;
			}
			const auto found = Labels.find({std::string(storage), std::string(label)});

			return found == Labels.end() ? kMissingScene : found->second;
		}

		[[nodiscard]] std::vector<std::size_t> Distances(std::size_t target, std::size_t excluded = kMissingScene, std::size_t excluded_dispatch = kMissingScene) const {
			std::vector<std::size_t> distances(Scenes.size(), kMissingScene);
			if (target == kMissingScene || target == excluded || target == excluded_dispatch) {
				return distances;
			}
			std::queue<std::size_t> pending;
			distances[target] = 0;
			pending.push(target);
			while (!pending.empty()) {
				const auto index = pending.front();
				pending.pop();
				for (const auto previous : Previous[index]) {
					if (previous != excluded && previous != excluded_dispatch && distances[previous] == kMissingScene) {
						distances[previous] = distances[index] + 1;
						pending.push(previous);
					}
				}
			}

			return distances;
		}

		const std::vector<ScenarioNavigationScene>& Scenes;
		std::map<std::pair<std::string, std::string>, std::size_t> Labels;
		std::map<std::string, std::size_t> First;
		std::vector<std::vector<std::size_t>> Previous;
	};

	std::vector<ScenarioNavigationLocation> ScenarioNavigation::Predecessors(const ScenarioNavigationLocation& target) const {
		const NavigationIndex index(m_Scenes);
		const auto goal = index.Find(target.Storage, target.Label);
		if (goal == kMissingScene) {
			throw std::runtime_error("scenario navigation target is absent from the original graph");
		}
		const auto distances = index.Distances(goal);
		std::vector<std::pair<std::size_t, ScenarioNavigationLocation>> candidates;
		for (std::size_t scene = 0; scene < m_Scenes.size(); ++scene) {
			if (distances[scene] == kMissingScene) {
				continue;
			}
			for (const auto& snapshot : m_Scenes[scene].Snapshots) {
				if (scene != goal || snapshot.Record < target.Record) {
					candidates.emplace_back(distances[scene], snapshot);
				}
			}
		}
		std::stable_sort(candidates.begin(), candidates.end(), [](const auto& left, const auto& right) {
			return left.first != right.first ? left.first < right.first : left.second.Record > right.second.Record;
		});
		std::vector<ScenarioNavigationLocation> result;
		result.reserve(candidates.size());
		for (auto& candidate : candidates) {
			result.push_back(std::move(candidate.second));
		}

		return result;
	}

	static std::optional<std::string> LiteralPhoneNumbers(std::string_view condition) {
		static constexpr std::string_view kIdentifier = "phoneNumbers";
		static constexpr std::string_view kWhitespace = " \t\r\n";
		const auto trim = [&](std::string_view& text) {
			const auto first = text.find_first_not_of(kWhitespace);
			text = first == std::string_view::npos ? std::string_view{} : text.substr(first, text.find_last_not_of(kWhitespace) - first + 1);
		};
		trim(condition);
		if (!condition.starts_with(kIdentifier)) {
			return {};
		}
		condition.remove_prefix(kIdentifier.size());
		trim(condition);
		if (!condition.starts_with("==")) {
			return {};
		}
		condition.remove_prefix(2);
		trim(condition);
		if (condition.size() < 3 || condition.front() != '"' || condition.back() != '"') {
			return {};
		}
		condition = condition.substr(1, condition.size() - 2);
		if (condition.size() > 64 || condition.find_first_not_of("0123456789*#") != std::string_view::npos) {
			return {};
		}

		return std::string(condition);
	}

	static std::optional<std::size_t> FindPhoneDispatch(const std::vector<ScenarioNavigationScene>& scenes, const NavigationIndex& index, std::size_t current) {
		std::vector<bool> visited(scenes.size());
		while (current != kMissingScene && !visited[current]) {
			visited[current] = true;
			const auto& exits = scenes[current].Exits;
			if (std::any_of(exits.begin(), exits.end(), [](const auto& exit) {
				    return LiteralPhoneNumbers(exit.Condition).has_value();
			    })) {
				return current;
			}
			if (exits.size() != 1 || !exits.front().Condition.empty()) {
				return {};
			}
			current = index.Find(exits.front().Storage, exits.front().Label);
		}
		return {};
	}

	std::optional<std::string> ScenarioNavigation::PhoneNumbers(std::string_view storage, std::string_view label, std::string_view target_storage, std::string_view target_label) const {
		const NavigationIndex index(m_Scenes);
		const auto current = index.Find(storage, label);
		const auto target = index.Find(target_storage, target_label);
		if (current == kMissingScene || target == kMissingScene || current == target) {
			return {};
		}
		const auto dispatch = FindPhoneDispatch(m_Scenes, index, current);
		if (!dispatch || *dispatch == target) {
			return {};
		}
		const auto distances = index.Distances(target, current, *dispatch);
		std::optional<std::string> result;
		for (const auto& exit : m_Scenes[*dispatch].Exits) {
			const auto destination = index.Find(exit.Storage, exit.Label);
			const auto numbers = LiteralPhoneNumbers(exit.Condition);
			if (!numbers || destination == kMissingScene || distances[destination] == kMissingScene) {
				continue;
			}
			if (result && result != numbers) {
				throw std::runtime_error("jump reconstruction has multiple original phone-number branches to its target; select a recorded route state");
			}
			result = numbers;
		}

		return result;
	}

	const std::vector<ScenarioNavigationScene>& ScenarioNavigation::Scenes() const noexcept {
		return m_Scenes;
	}

} // namespace sgre::scenario
