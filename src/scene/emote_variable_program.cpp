#include "sgre/scene/emote_variable_controls.hpp"

#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>
#include <variant>

namespace sgre::scene {

	static constexpr std::size_t kMaximumEmoteProgramEntries = 65536;
	static constexpr std::size_t kMaximumEmoteProgramStringBytes = 8 * 1024 * 1024;

	struct EmoteProgramBudget {
		std::size_t Entries = 0;
		std::size_t StringBytes = 0;
	};

	static const resource::PsbValue& RequireProgramField(const resource::PsbValue& value, std::string_view name) {
		if (const auto* field = value.Find(name)) {
			return *field;
		}
		throw std::runtime_error("Emote control metadata is missing " + std::string(name));
	}

	static const resource::PsbArray& ProgramArray(const resource::PsbValue& value) {
		const auto* array = std::get_if<resource::PsbArray>(&value.Data);
		if (array == nullptr) {
			throw std::runtime_error("Emote control list requires actual array metadata");
		}
		return *array;
	}

	static bool ProgramEnabled(const resource::PsbValue& value) {
		if (const auto* boolean = std::get_if<bool>(&value.Data)) {
			return *boolean;
		}
		if (const auto* integer = std::get_if<std::int64_t>(&value.Data)) {
			return static_cast<std::uint8_t>(*integer) != 0;
		}
		if (const auto* real = std::get_if<double>(&value.Data)) {
			return *real != 0;
		}
		throw std::runtime_error("Emote control enabled flag requires native numeric metadata");
	}

	static std::string ProgramName(const resource::PsbValue& value, EmoteProgramBudget& budget) {
		const auto* text = std::get_if<std::string>(&value.Data);
		if (text == nullptr) {
			throw std::runtime_error("Emote control label requires actual string metadata");
		}
		const auto name = std::string_view(*text).substr(0, text->find('\0'));
		if (name.size() >= kMaximumEmoteProgramStringBytes - budget.StringBytes) {
			throw std::runtime_error("Emote control labels exceed bounded initialization storage");
		}
		budget.StringBytes += name.size() + 1;
		return std::string(name);
	}

	static std::int32_t ProgramInteger(const resource::PsbValue& value) {
		if (const auto* integer = std::get_if<std::int64_t>(&value.Data)) {
			return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(*integer));
		}
		if (const auto* boolean = std::get_if<bool>(&value.Data)) {
			return *boolean ? 1 : 0;
		}
		if (const auto* real = std::get_if<double>(&value.Data); real != nullptr && std::isfinite(*real) && *real >= -2147483648.0 && *real < 2147483648.0) {
			return static_cast<std::int32_t>(*real);
		}
		throw std::runtime_error("Emote graph frame requires native bounded integer metadata");
	}

	static float ProgramNumber(const resource::PsbValue* value, bool nonnegative = true) {
		if (value == nullptr) {
			return 0;
		}
		float number = 0;
		if (const auto* integer = std::get_if<std::int64_t>(&value->Data)) {
			number = static_cast<float>(*integer);
		} else if (const auto* real = std::get_if<double>(&value->Data)) {
			number = static_cast<float>(*real);
		} else if (const auto* boolean = std::get_if<bool>(&value->Data)) {
			number = *boolean ? 1 : 0;
		} else {
			throw std::runtime_error("Emote blink timing requires actual numeric metadata");
		}
		if (!std::isfinite(number) || (nonnegative && number < 0)) {
			throw std::runtime_error("Emote blink timing exceeds finite nonnegative ticks");
		}
		return number;
	}

	static void ChargeProgramEntries(std::size_t count, EmoteProgramBudget& budget) {
		if (count > kMaximumEmoteProgramEntries - budget.Entries) {
			throw std::runtime_error("Emote graph/control metadata exceeds bounded initialization storage");
		}
		budget.Entries += count;
	}

	static EmoteGraphDefinition ReadProgramGraph(const resource::PsbValue& entry, EmoteProgramBudget& budget) {
		EmoteGraphDefinition graph;
		const auto& edges = ProgramArray(RequireProgramField(entry, "edge"));
		ChargeProgramEntries(edges.size(), budget);
		for (const auto& value : edges) {
			const auto& edge = ProgramArray(value);
			if (edge.size() < 2) {
				throw std::runtime_error("Emote graph edge requires its two native frame endpoints");
			}
			graph.Edges.push_back({ProgramInteger(edge[0]), ProgramInteger(edge[1])});
		}
		const auto& nodes = ProgramArray(RequireProgramField(entry, "node"));
		ChargeProgramEntries(nodes.size(), budget);
		for (const auto& value : nodes) {
			const auto& node = ProgramArray(value);
			ChargeProgramEntries(node.size(), budget);
			std::vector<std::int32_t> points;
			points.reserve(node.size());
			for (const auto& point : node) {
				points.push_back(ProgramInteger(point));
			}
			graph.Nodes.push_back(std::move(points));
		}
		return graph;
	}

	static void ReadGraphPrograms(const resource::PsbValue& metadata, EmoteVariableProgram& program, EmoteProgramBudget& budget) {
		if (const auto* list = metadata.Find("eyebrowControl")) {
			const auto& entries = ProgramArray(*list);
			ChargeProgramEntries(entries.size(), budget);
			for (const auto& entry : entries) {
				if (ProgramEnabled(RequireProgramField(entry, "enabled"))) {
					EmoteGraphChannelDefinition definition;
					definition.Name = ProgramName(RequireProgramField(entry, "label"), budget);
					definition.InitialFrame = ProgramInteger(RequireProgramField(entry, "beginFrame"));
					definition.Graph = ReadProgramGraph(entry, budget);
					program.Eyebrows.push_back(std::move(definition));
				}
			}
		}
		if (const auto* list = metadata.Find("mouthControl")) {
			const auto& entries = ProgramArray(*list);
			ChargeProgramEntries(entries.size(), budget);
			for (const auto& entry : entries) {
				if (ProgramEnabled(RequireProgramField(entry, "enabled"))) {
					EmoteMouthDefinition definition;
					definition.Name = ProgramName(RequireProgramField(entry, "label"), budget);
					definition.TalkName = ProgramName(RequireProgramField(entry, "talkLabel"), budget);
					definition.InitialFrame = ProgramInteger(RequireProgramField(entry, "beginFrame"));
					program.Mouths.push_back(std::move(definition));
				}
			}
		}
		if (const auto* list = metadata.Find("eyeControl")) {
			std::vector<std::pair<std::string, EmoteBlinkParameters>> blink_parameters;
			if (const auto* choices = metadata.Find("blinkParameter")) {
				const auto& entries = ProgramArray(*choices);
				ChargeProgramEntries(entries.size(), budget);
				for (const auto& entry : entries) {
					EmoteBlinkParameters parameters;
					parameters.IntervalMinimum = ProgramNumber(entry.Find("intervalMin")) * 60.0f;
					parameters.IntervalMaximum = ProgramNumber(entry.Find("intervalMax")) * 60.0f;
					parameters.CloseTicks = ProgramNumber(entry.Find("closeTime")) * 60.0f;
					parameters.HoldTicks = ProgramNumber(entry.Find("holdTime")) * 60.0f;
					parameters.OpenTicks = ProgramNumber(entry.Find("openTime")) * 60.0f;
					const auto* id = entry.Find("id");
					blink_parameters.emplace_back(id == nullptr ? std::string{} : ProgramName(*id, budget), parameters);
				}
			}
			const auto& entries = ProgramArray(*list);
			ChargeProgramEntries(entries.size(), budget);
			for (const auto& entry : entries) {
				if (ProgramEnabled(RequireProgramField(entry, "enabled"))) {
					EmoteEyeDefinition definition;
					definition.Name = ProgramName(RequireProgramField(entry, "label"), budget);
					definition.InitialFrame = ProgramInteger(RequireProgramField(entry, "beginFrame"));
					definition.LowerFrame = definition.InitialFrame;
					// Native4cf6d0 delegates slot lookup4ce460; the object is not an enabled scalar.
					if (metadata.Find("velvetyCommonParams") != nullptr) {
						definition.InitialFrame = 0;
					}
					definition.EndFrame = ProgramInteger(RequireProgramField(entry, "endFrame"));
					definition.Graph = ReadProgramGraph(entry, budget);
					if (const auto* field = entry.Find("varL")) {
						definition.LeftName = ProgramName(*field, budget);
					}
					if (const auto* field = entry.Find("varR")) {
						definition.RightName = ProgramName(*field, budget);
					}
					if (const auto* field = entry.Find("varB")) {
						definition.BlinkName = ProgramName(*field, budget);
					}
					const auto* field = entry.Find("parameter");
					const auto parameter = field == nullptr ? std::string{} : ProgramName(*field, budget);
					for (std::size_t index = 0; index != blink_parameters.size(); ++index) {
						if (blink_parameters[index].first == parameter) {
							definition.BlinkParameterIndex = static_cast<std::int32_t>(index);
							definition.Blink = blink_parameters[index].second;
							break;
						}
					}
					if (definition.BlinkParameterIndex >= 0 && !definition.BlinkName.empty()) {
						ChargeProgramEntries(blink_parameters.size(), budget);
						for (const auto& choice : blink_parameters) {
							definition.BlinkChoices.push_back(choice.second);
						}
					} else if (definition.BlinkParameterIndex < 0) {
						definition.Blink.IntervalMinimum = ProgramNumber(entry.Find("blinkIntervalMin"));
						definition.Blink.IntervalMaximum = ProgramNumber(entry.Find("blinkIntervalMax"));
						const auto frames = ProgramNumber(entry.Find("blinkFrameCount"));
						definition.Blink.CloseTicks = (frames + frames) / 5.0f;
						definition.Blink.HoldTicks = frames / 5.0f;
						definition.Blink.OpenTicks = definition.Blink.CloseTicks;
					}
					program.Eyes.push_back(std::move(definition));
				}
			}
		}
	}

	static void ReadNamedProgram(const resource::PsbValue& metadata, std::string_view field, std::vector<std::string>& output, EmoteProgramBudget& budget, bool enabled) {
		const auto* value = metadata.Find(field);
		if (value == nullptr) {
			return;
		}
		const auto& entries = ProgramArray(*value);
		if (entries.size() > kMaximumEmoteProgramEntries - budget.Entries) {
			throw std::runtime_error("Emote control lists exceed bounded initialization count");
		}
		budget.Entries += entries.size();
		for (const auto& entry : entries) {
			if (enabled && !ProgramEnabled(RequireProgramField(entry, "enabled"))) {
				continue;
			}
			output.push_back(ProgramName(enabled || field == "variableList" ? RequireProgramField(entry, "label") : entry, budget));
		}
	}

	EmoteVariableProgram ReadEmoteVariableProgram(const resource::PsbDocument& document) {
		EmoteVariableProgram program;
		program.Selectors = resource::ReadEmoteSelectorMetadata(document);
		const auto& metadata = RequireProgramField(document.Root, "metadata");
		EmoteProgramBudget budget;
		ReadNamedProgram(metadata, "transitionControl", program.Transitions, budget, true);
		ReadNamedProgram(metadata, "immediateControl", program.Immediate, budget, true);
		ReadNamedProgram(metadata, "extensionVariableList", program.Extensions, budget, false);
		ReadNamedProgram(metadata, "variableList", program.VariableLabels, budget, false);
		ReadGraphPrograms(metadata, program, budget);
		if (const auto* list = metadata.Find("clampControl")) {
			const auto& entries = ProgramArray(*list);
			ChargeProgramEntries(entries.size(), budget);
			for (const auto& entry : entries) {
				if (!ProgramEnabled(RequireProgramField(entry, "enabled"))) {
					continue;
				}
				const auto kind = ProgramInteger(RequireProgramField(entry, "type"));
				if (kind != 0 && kind != 1) {
					program.UnsupportedFamilies.emplace_back("clampControl unsupported type " + std::to_string(kind));
					continue;
				}
				EmoteClampDefinition definition;
				definition.LeftRight = ProgramName(RequireProgramField(entry, "var_lr"), budget);
				definition.UpDown = ProgramName(RequireProgramField(entry, "var_ud"), budget);
				definition.Transform.Kind = static_cast<EmoteClampKind>(kind);
				definition.Transform.Minimum = ProgramNumber(&RequireProgramField(entry, "min"), false);
				definition.Transform.Maximum = ProgramNumber(&RequireProgramField(entry, "max"), false);
				program.Clamps.push_back(std::move(definition));
			}
		}
		if (const auto* primary = metadata.Find("selectorControl")) {
			for (const auto& definition : ProgramArray(*primary)) {
				if (ProgramEnabled(RequireProgramField(definition, "enabled"))) {
					++program.PrimarySelectorCount;
				}
			}
		}
		// These families own different state/algorithms; preserving their declaration is not implementation.
		for (const auto field : {"bustControl", "hairControl", "partsControl", "orbitControl"}) {
			if (const auto* list = metadata.Find(field)) {
				const auto& entries = ProgramArray(*list);
				if (entries.size() > kMaximumEmoteProgramEntries - budget.Entries) {
					throw std::runtime_error("Emote controller declarations exceed bounded initialization count");
				}
				budget.Entries += entries.size();
				for (const auto& entry : entries) {
					if (ProgramEnabled(RequireProgramField(entry, "enabled"))) {
						program.UnsupportedFamilies.emplace_back(field);
						break;
					}
				}
			}
		}
		if (const auto* stereo = metadata.Find("stereovisionControl")) {
			ReadNamedProgram(*stereo, "variableMatchList", program.StereoVariables, budget, false);
		}

		if (const auto* mirror = metadata.Find("mirror"); mirror != nullptr && ProgramEnabled(*mirror)) {
			program.UnsupportedFamilies.emplace_back("mirrored variable publication");
		}
		ReadNamedProgram(metadata, "instantVariableList", program.InstantVariables, budget, false);
		if (const auto* timelines = metadata.Find("timelineControl")) {
			const auto& entries = ProgramArray(*timelines);
			ChargeProgramEntries(entries.size(), budget);
			for (const auto& entry : entries) {
				// Native 2c4f50 declares all names; the active list is populated only by explicit playback.
				EmoteTimelineCatalogEntry timeline;
				timeline.Name = ProgramName(RequireProgramField(entry, "label"), budget);
				if (const auto* difference = entry.Find("diff")) {
					timeline.Difference = ProgramEnabled(*difference);
				}
				program.TimelineCatalog.push_back(std::move(timeline));
			}
		}
		if (const auto* definitions = metadata.Find("variableMetaInfoList")) {
			const auto& entries = ProgramArray(*definitions);
			if (entries.size() > kMaximumEmoteProgramEntries - budget.Entries) {
				throw std::runtime_error("Emote variable descriptions exceed bounded initialization count");
			}
			budget.Entries += entries.size();
			for (const auto& entry : entries) {
				const auto* cyclic = entry.Find("cyclic");
				const auto* owned = entry.Find("owned");
				if ((owned == nullptr || !ProgramEnabled(*owned)) && cyclic != nullptr && ProgramEnabled(*cyclic)) {
					program.UnsupportedFamilies.emplace_back("cyclic extension transition");
					break;
				}
			}
		}
		if (metadata.Find("immediateControl") == nullptr) {
			program.DefaultImmediate = true;
		}
		return program;
	}

} // namespace sgre::scene
