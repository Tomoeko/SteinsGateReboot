#include "sgre/scenario/calendar_restore.hpp"

#include "sgre/resource/archive_reader.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <map>
#include <set>
#include <span>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace sgre::scenario {

	static constexpr std::size_t kMaximumPrefixRecords = 100000;
	static constexpr std::size_t kMaximumEffectNodes = 100000;
	static constexpr std::size_t kMaximumEffectDepth = 32;
	static constexpr std::size_t kMaximumTextBytes = 4096;

	static const std::string* CalendarText(const resource::PsbValue& value) {
		const auto* text = std::get_if<std::string>(&value.Data);
		if (text == nullptr || text->size() > kMaximumTextBytes || text->find('\0') != std::string::npos) {
			return nullptr;
		}

		return text;
	}

	static bool OneOf(std::string_view value, std::initializer_list<std::string_view> choices) {
		return std::find(choices.begin(), choices.end(), value) != choices.end();
	}

	static bool KnownVisualClass(std::string_view name) {
		return OneOf(name, {"stage", "character", "subwin_info", "bgm", "loopse", "se"});
	}

	static bool DecimalLiteral(std::string_view value) {
		return !value.empty() && std::all_of(value.begin(), value.end(), [](char character) {
			return character >= '0' && character <= '9';
		});
	}

	static bool NumericLiteral(const resource::PsbValue& value) {
		if (std::holds_alternative<std::int64_t>(value.Data)) {
			return true;
		}
		if (const auto* number = std::get_if<double>(&value.Data)) {
			return std::isfinite(*number);
		}
		const auto* text = CalendarText(value);
		if (text == nullptr) {
			return false;
		}
		auto view = std::string_view(*text);
		if (view.starts_with('-')) {
			view.remove_prefix(1);
		}

		return DecimalLiteral(view);
	}

	static bool LiteralCoordinateList(std::string_view value) {
		if (!value.starts_with('#')) {
			return false;
		}
		value.remove_prefix(1);
		while (!value.empty()) {
			const auto comma = value.find(',');
			auto number = value.substr(0, comma);
			if (number.starts_with('-')) {
				number.remove_prefix(1);
			}
			if (!DecimalLiteral(number)) {
				return false;
			}
			if (comma == std::string_view::npos) {
				return true;
			}
			value.remove_prefix(comma + 1);
		}

		return false;
	}

	static bool VisualActions(const resource::PsbValue& value) {
		const auto* actions = std::get_if<resource::PsbArray>(&value.Data);
		if (actions == nullptr || actions->size() > 64) {
			return false;
		}
		for (const auto& item : *actions) {
			if (std::holds_alternative<std::monostate>(item.Data)) {
				continue;
			}
			const auto* action = std::get_if<resource::PsbArray>(&item.Data);
			if (action == nullptr || action->size() != 2) {
				return false;
			}
			const auto* name = CalendarText((*action)[0]);
			if (name == nullptr) {
				return false;
			}
			if (*name == "leveloffset") {
				const auto* text = CalendarText((*action)[1]);
				if (text == nullptr || (!text->empty() && !LiteralCoordinateList(*text))) {
					return false;
				}
			} else if (!OneOf(*name, {"originz", "xpos", "ypos", "zpos", "order", "zoomx", "zoomy", "camerax", "cameray", "camerazoom", "$progress"}) || !NumericLiteral((*action)[1])) {
				return false;
			}
		}

		return true;
	}

	static bool VisualValue(const resource::PsbValue& value, std::size_t depth, std::size_t& nodes, bool variables = false) {
		if (depth > kMaximumEffectDepth || nodes == kMaximumEffectNodes) {
			throw std::runtime_error("calendar restoration effect proof exceeds its bounded depth/work");
		}
		++nodes;
		if (const auto* text = std::get_if<std::string>(&value.Data)) {
			return CalendarText(value) != nullptr && !text->starts_with('&');
		}
		if (const auto* number = std::get_if<double>(&value.Data)) {
			return std::isfinite(*number);
		}
		if (const auto* array = std::get_if<resource::PsbArray>(&value.Data)) {
			return std::all_of(array->begin(), array->end(), [&](const auto& child) {
				return VisualValue(child, depth + 1, nodes);
			});
		}
		if (const auto* object = std::get_if<resource::PsbObject>(&value.Data)) {
			std::set<std::string_view, std::less<>> keys;
			for (const auto& [name, child] : *object) {
				if (name.size() > kMaximumTextBytes || name.find('\0') != std::string::npos || !keys.insert(name).second) {
					return false;
				}
				if (variables) {
					if (name.empty() || !NumericLiteral(child)) {
						return false;
					}
				} else if (!OneOf(name, {"name", "class", "link", "type", "showmode", "action", "redraw", "disp", "posName", "imageFile", "file", "options", "chara", "motion", "speed", "tickcount", "time", "variables", "resolution", "color", "lang", "slot0", "slot1", "slot2", "slot3", "slot4", "slot5", "trans", "method", "rule", "list", "mode", "begin", "end", "wait", "bwait", "blacktrans", "bgcolor", "replay", "filename", "loop", "start", "state", "volume"})) {
					return false;
				}
				if (name == "action" && !VisualActions(child)) {
					return false;
				}
				if (name == "class") {
					const auto* text = CalendarText(child);
					if (text == nullptr || !KnownVisualClass(*text)) {
						return false;
					}
				}
				if (OneOf(name, {"type", "showmode", "disp", "speed", "tickcount", "time", "resolution", "color", "mode", "bwait", "blacktrans", "bgcolor", "loop", "start", "state", "volume"}) && !std::holds_alternative<std::monostate>(child.Data) && !std::holds_alternative<bool>(child.Data) && !NumericLiteral(child)) {
					return false;
				}
				if (name == "method") {
					const auto* text = CalendarText(child);
					if (text == nullptr || !OneOf(*text, {"", "crossfade", "universal"})) {
						return false;
					}
				}
				if (!VisualValue(child, depth + 1, nodes, name == "variables")) {
					return false;
				}
			}

			return true;
		}

		return !std::holds_alternative<resource::PsbChunkReference>(value.Data);
	}

	static bool VisualCommand(std::span<const resource::PsbValue> row, std::size_t& nodes, std::map<std::string, std::string, std::less<>>& classes, bool initial_reset = false) {
		if (row.empty() || row.size() % 2 == 0) {
			return false;
		}
		for (std::size_t index = 1; index < row.size(); index += 2) {
			const auto* key = CalendarText(row[index]);
			if (key == nullptr || !OneOf(*key, {"update", "wait", "trans"}) || !VisualValue(row[index + 1], 0, nodes)) {
				return false;
			}
			if (*key == "update") {
				const auto* updates = std::get_if<resource::PsbArray>(&row[index + 1].Data);
				if (updates == nullptr) {
					return false;
				}
				for (const auto& update : *updates) {
					if (const auto* creation = std::get_if<resource::PsbArray>(&update.Data)) {
						if (creation->size() == 2 && CalendarText((*creation)[0]) != nullptr && *CalendarText((*creation)[0]) == "init") {
							const auto* value = std::get_if<std::int64_t>(&(*creation)[1].Data);
							if (!initial_reset || &update != &updates->front() || value == nullptr || *value != 0) {
								return false;
							}
							// This original reset precedes the calendar producer; delayed or
							// later resets remain outside the local calendar proof.
							classes.clear();
							continue;
						}
						if (creation->size() != 3 || CalendarText((*creation)[0]) == nullptr || *CalendarText((*creation)[0]) != "new" || CalendarText((*creation)[1]) == nullptr || CalendarText((*creation)[2]) == nullptr || !KnownVisualClass(*CalendarText((*creation)[2]))) {
							return false;
						}
						classes[*CalendarText((*creation)[1])] = *CalendarText((*creation)[2]);
					} else if (!std::holds_alternative<resource::PsbObject>(update.Data)) {
						return false;
					} else {
						const auto* name_value = update.Find("name");
						const auto* name = name_value == nullptr ? nullptr : CalendarText(*name_value);
						const auto* class_value = update.Find("class");
						if (name == nullptr || name->empty()) {
							return false;
						}
						if (class_value != nullptr) {
							const auto* class_name = CalendarText(*class_value);
							if (class_name == nullptr || !KnownVisualClass(*class_name)) {
								return false;
							}
							classes[*name] = *class_name;
						} else if (*name != "env" && !classes.contains(*name)) {
							return false;
						}
					}
				}
			}
		}

		return true;
	}

	static bool SamePair(const resource::PsbArray& row, std::size_t index, std::string_view key, std::initializer_list<std::string_view> values) {
		const auto* name = CalendarText(row[index]);
		const auto* value = CalendarText(row[index + 1]);

		return name != nullptr && value != nullptr && *name == key && OneOf(*value, values);
	}

	static bool KnownCalendarPreserving(const resource::PsbArray& row, std::size_t& nodes, std::map<std::string, std::string, std::less<>>& classes, bool before_calendar) {
		const auto* name = CalendarText(row.front());
		if (name == nullptr) {
			return false;
		}
		if (*name == "startline") {
			return row.size() % 2 == 1;
		}
		if (*name == "meswinload") {
			return row.size() == 3 && SamePair(row, 1, "page", {"both"});
		}
		if (*name == "msgon" || *name == "msgoff") {
			return row.size() == 1 || (row.size() == 3 && SamePair(row, 1, "fast", {"true", "false"}));
		}
		if (*name == "resg_subwin") {
			return row.size() == 3 && CalendarText(row[1]) != nullptr && *CalendarText(row[1]) == "progress" && NumericLiteral(row[2]);
		}
		if (*name == "resg_phone") {
			return row.size() == 5 && SamePair(row, 1, "cmd", {"phone", "signal"}) && SamePair(row, 3, "action", {"enable", "disable"});
		}
		if (*name == "envupdate") {
			return VisualCommand(row, nodes, classes, before_calendar);
		}
		if (*name == "delayrun" && row.size() >= 3 && NumericLiteral(row[1]) && CalendarText(row[2]) != nullptr && *CalendarText(row[2]) == "envupdate") {
			return VisualCommand(std::span<const resource::PsbValue>(row).subspan(2), nodes, classes);
		}

		return false;
	}

	ScenarioCalendarRestorePlan ReadScenarioCalendarRestore(const resource::PsbDocument& document, const ScenarioJumpPoint& target) {
		const auto checked = ValidateScenarioJump(document, target.Storage, target.SourceLine);
		if (checked.CanonicalStorage != target.CanonicalStorage || checked.SceneLabel != target.SceneLabel || checked.SnapshotLine != target.SnapshotLine || checked.SnapshotIndex != target.SnapshotIndex || checked.Point != target.Point) {
			throw std::runtime_error("calendar restoration requires the exact validated original jump coordinates");
		}
		ScenarioCalendarRestorePlan result;
		const auto& scenes = std::get<resource::PsbArray>(document.Root.Find("scenes")->Data);
		const resource::PsbValue* scene = nullptr;
		for (const auto& candidate : scenes) {
			if (candidate.Find("label") != nullptr && CalendarText(*candidate.Find("label")) != nullptr && *CalendarText(*candidate.Find("label")) == checked.SceneLabel) {
				if (scene != nullptr) {
					throw std::runtime_error("calendar restoration scene label is duplicated");
				}
				scene = &candidate;
			}
		}
		if (scene == nullptr) {
			throw std::runtime_error("calendar restoration selected scene is absent");
		}
		if (scene->Find("preevals") != nullptr) {
			result.Reason = "original pre-evaluation may write calendar state; authentic checkpoint required";
			return result;
		}
		const auto& lines = std::get<resource::PsbArray>(scene->Find("lines")->Data);
		if (checked.SnapshotIndex > kMaximumPrefixRecords) {
			throw std::runtime_error("calendar restoration prefix exceeds its checked record bound");
		}
		std::size_t nodes = 0;
		std::map<std::string, std::string, std::less<>> classes;
		for (std::size_t index = 0; index < checked.SnapshotIndex; ++index) {
			const auto& line = lines[index];
			if (std::holds_alternative<std::string>(line.Data) || std::holds_alternative<std::int64_t>(line.Data)) {
				continue;
			}
			const auto* row = std::get_if<resource::PsbArray>(&line.Data);
			if (row == nullptr || row->empty()) {
				result.Reason = "unclassified original prefix record; authentic checkpoint required";
				return result;
			}
			if (std::holds_alternative<std::int64_t>(row->front().Data)) {
				// Only the first inline environment checkpoint seeds inherited object
				// classes; later historical snapshots do not publish runtime state.
				if (index == 0 && row->size() >= 2) {
					const auto* data = (*row)[1].Find("data");
					const auto* objects = data == nullptr ? nullptr : std::get_if<resource::PsbArray>(&data->Data);
					if (objects != nullptr && objects->size() <= 4096) {
						for (const auto& object : *objects) {
							const auto* fields = std::get_if<resource::PsbArray>(&object.Data);
							if (fields == nullptr || fields->size() != 3 || CalendarText((*fields)[0]) == nullptr || CalendarText((*fields)[1]) == nullptr || !KnownVisualClass(*CalendarText((*fields)[1]))) {
								result.Reason = "initial environment has an unknown calendar-effect class; authentic checkpoint required";
								return result;
							}
							classes[*CalendarText((*fields)[0])] = *CalendarText((*fields)[1]);
						}
					}
				}
				continue;
			}
			const auto* tag = CalendarText(row->front());
			if (tag != nullptr && *tag == "calendar") {
				if (result.Command || row->size() != 7) {
					result.Command.reset();
					result.Reason = "calendar producer is ambiguous or outside its proven literal schema";
					return result;
				}
				ScenarioCalendarCommand command;
				command.Storage = checked.CanonicalStorage;
				command.SceneLabel = checked.SceneLabel;
				command.CommandIndex = index;
				command.SnapshotIndex = checked.SnapshotIndex;
				for (std::size_t pair = 0; pair < command.Attributes.size(); ++pair) {
					const auto* key = CalendarText((*row)[pair * 2 + 1]);
					const auto* value = CalendarText((*row)[pair * 2 + 2]);
					if (key == nullptr || value == nullptr || !OneOf(*key, {"mon", "day", "week"}) || std::any_of(command.Attributes.begin(), command.Attributes.begin() + static_cast<std::ptrdiff_t>(pair), [&](const auto& prior) {
						    return prior.first == *key;
					    }) ||
					    (*key != "week" && !DecimalLiteral(*value)) || (*key == "week" && !OneOf(*value, {"日", "月", "火", "水", "木", "金", "土"}))) {
						result.Reason = "calendar producer has dynamic, duplicate or unsupported original fields";
						return result;
					}
					command.Attributes[pair] = {*key, *value};
				}
				result.Command = std::move(command);
			} else if (!KnownCalendarPreserving(*row, nodes, classes, !result.Command)) {
				result.Command.reset();
				result.Reason = "unknown, conditional or possibly calendar-writing prefix effect at record " + std::to_string(index) + "; authentic checkpoint required";
				return result;
			}
		}
		result.Reason = result.Command ? "unique unconditional original calendar producer and proven local prefix" : "no local original calendar producer; authentic checkpoint required";

		return result;
	}

	ScenarioCalendarRestorePlan ReadScenarioCalendarRestore(const ScenarioSearchLocation& location, const ScenarioJumpPoint& target) {
		resource::ArchiveLimits limits;
		limits.MaxMetadataBytes = 64 * 1024 * 1024;
		limits.MaxPackedEntryBytes = 64 * 1024 * 1024;
		limits.MaxDecodedEntryBytes = 64 * 1024 * 1024;
		const auto data = location.Installation / "wind3d11data";
		resource::ArchiveReader archive(data / "scenario_info.psb.m", data / "scenario_body.bin", location.ArchiveSeed, limits);
		if (archive.Index().Entries.size() > 4096 || archive.Index().ExpireSuffixes != std::vector<std::string>{".scn.m"} || !archive.Contains(target.CanonicalStorage)) {
			throw std::runtime_error("calendar restoration requires its bounded original scenario archive and exact canonical entry");
		}
		const auto bytes = archive.Read(target.CanonicalStorage, target.CanonicalStorage + ".scn.m");
		const auto header = resource::ReadPsbHeader(bytes);
		if (header.DataOffset > bytes.size()) {
			throw std::runtime_error("calendar restoration scenario metadata exceeds its original decoded entry");
		}
		const auto document = resource::ReadPsbMetadata(std::span<const std::uint8_t>(bytes).first(header.DataOffset), bytes.size());

		return ReadScenarioCalendarRestore(document, target);
	}

} // namespace sgre::scenario
