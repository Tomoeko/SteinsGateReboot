#include "sgre/scene/emote_variable_controls.hpp"

#include "sgre/scene/emote_easing.hpp"
#include "sgre/scene/emote_timeline_scheduler.hpp"

#include "sgre/scene/motion_player_commands.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <utility>

namespace sgre::scene {

	static constexpr std::size_t kMaximumEmoteControls = 65536;
	static constexpr std::size_t kMaximumEmoteAnimationNames = 1000000;
	static constexpr std::size_t kMaximumEmoteChannelBytes = 8 * 1024 * 1024;

	struct EmoteScalarChannel {
		std::string Name;
		float Current = 0;
		float Start = 0;
		float Target = 0;
		float Duration = 0;
		float Elapsed = 0;
		float Easing = 0;
		bool Transitioning = false;
	};

	struct EmotePublishedValue {
		float Value = 0;
		bool Present = false;
	};

	struct EmoteGraphChannel {
		explicit EmoteGraphChannel(EmoteGraphChannelDefinition definition) : Name(std::move(definition.Name)), Current(static_cast<float>(definition.InitialFrame)), Graph(std::move(definition.Graph)) {
		}
		std::string Name;
		float Current = 0;
		float Target = 0;
		float Duration = 0;
		float Elapsed = 0;
		float Easing = 0;
		bool Transitioning = false;
		EmoteGraphControl Graph;
	};

	struct EmoteMouthChannel {
		std::string Name;
		std::string TalkName;
		std::int32_t Frame = 0;
		float Talk = 0;
		float Start = 0;
		float Target = 0;
		float Duration = 0;
		float ReciprocalDuration = 0;
		float Progress = 0;
		float Easing = 0;
		bool Pending = false;
		bool Transitioning = false;
	};

	static EmoteGraphChannel EyeGraphChannel(const std::string& name, std::int32_t frame, const EmoteGraphDefinition& graph) {
		EmoteGraphChannelDefinition definition;
		definition.Name = name;
		definition.InitialFrame = frame;
		definition.Graph = graph;
		return EmoteGraphChannel(std::move(definition));
	}

	struct EmoteEyeChannel {
		EmoteEyeChannel(EmoteEyeDefinition definition, EmoteBlinkRandom* random) : Definition(std::move(definition)), Channels{EyeGraphChannel(Definition.Name, Definition.InitialFrame, Definition.Graph), EyeGraphChannel(Definition.LeftName, Definition.InitialFrame, Definition.Graph), EyeGraphChannel(Definition.RightName, Definition.InitialFrame, Definition.Graph)} {
			if (random != nullptr) {
				Blink = std::make_unique<EmoteBlinkControl>(*random, Definition.Blink);
			}
		}
		EmoteEyeDefinition Definition;
		std::array<EmoteGraphChannel, 3> Channels;
		std::unique_ptr<EmoteBlinkControl> Blink;
	};

	template <typename Collection, typename Source, typename Name, typename Operation>
	static void VisitAssignedControls(Collection& destination, const Source& source, Name name, Operation operation) {
		using TargetPointer = decltype(std::addressof(*destination.begin()));
		std::map<std::string_view, TargetPointer, std::less<>> targets;
		for (auto& control : destination) {
			targets.try_emplace(name(control), std::addressof(control));
		}
		for (const auto& control : source) {
			if (const auto found = targets.find(name(control)); found != targets.end()) {
				operation(*found->second, control);
			}
		}
	}

	static void AssignGraphChannel(EmoteGraphChannel& destination, const EmoteGraphChannel& source) {
		destination.Current = source.Current;
		destination.Target = source.Target;
		destination.Duration = source.Duration;
		destination.Elapsed = source.Elapsed;
		destination.Easing = source.Easing;
		destination.Transitioning = source.Transitioning;
		destination.Graph.AssignPath(source.Graph);
	}

	static void RequireEyeAssignment(const EmoteEyeChannel& destination, const EmoteEyeChannel& source) {
		if (!destination.Blink || !source.Blink) {
			throw std::runtime_error("Emote eye assignment requires both actual initialized blink owners");
		}
		const auto index = source.Definition.BlinkParameterIndex;
		if (!destination.Definition.BlinkChoices.empty() && (index < 0 || static_cast<std::size_t>(index) >= destination.Definition.BlinkChoices.size())) {
			// Native2d6180 clamps the stored index but addresses the parameter vector with the original index.
			throw std::runtime_error("Emote eye assignment source parameter index exceeds the destination native-safe vector");
		}
	}

	static void AssignEyeChannel(EmoteEyeChannel& destination, const EmoteEyeChannel& source) {
		for (std::size_t index = 0; index != destination.Channels.size(); ++index) {
			AssignGraphChannel(destination.Channels[index], source.Channels[index]);
		}
		destination.Definition.BlinkParameterIndex = source.Definition.BlinkParameterIndex;
		if (!destination.Definition.BlinkChoices.empty()) {
			destination.Blink->SetParameters(destination.Definition.BlinkChoices[static_cast<std::size_t>(destination.Definition.BlinkParameterIndex)], false);
		}
		destination.Blink->AssignState(*source.Blink);
	}

	static void AssignMouthChannel(EmoteMouthChannel& destination, const EmoteMouthChannel& source) {
		destination.Frame = source.Frame;
		destination.Talk = source.Talk;
		destination.Start = source.Start;
		destination.Target = source.Target;
		destination.Duration = source.Duration;
		destination.ReciprocalDuration = source.ReciprocalDuration;
		destination.Progress = source.Progress;
		destination.Easing = source.Easing;
		destination.Pending = source.Pending;
		destination.Transitioning = source.Transitioning;
	}

	static void AssignScalarChannel(EmoteScalarChannel& destination, const EmoteScalarChannel& source) {
		destination.Current = source.Current;
		destination.Start = source.Start;
		destination.Target = source.Target;
		destination.Duration = source.Duration;
		destination.Elapsed = source.Elapsed;
		destination.Easing = source.Easing;
		destination.Transitioning = source.Transitioning;
	}

	template <typename Channel>
	static void FinishChannelTransition(Channel& channel) noexcept {
		if (channel.Transitioning) {
			channel.Current = channel.Target;
			channel.Transitioning = false;
		}
	}

	static bool IsBlinkSelection(const EmoteEyeChannel& eye, std::string_view name) {
		return name == eye.Definition.BlinkName && name != eye.Definition.Name && name != eye.Definition.LeftName && name != eye.Definition.RightName;
	}

	enum class EmoteVariableRouteKind {
		Transition,
		Immediate,
		Selector,
		Eyebrow,
		Mouth,
		Eye
	};

	struct EmoteVariableRoute {
		EmoteVariableRouteKind Kind = EmoteVariableRouteKind::Immediate;
		std::size_t Index = 0;
	};

	static std::string_view ControlName(std::string_view name) {
		if (name.size() > kMaximumMotionCommandNameBytes) {
			throw std::runtime_error("Emote variable name exceeds actual native bounded string domain");
		}
		return name.substr(0, name.find('\0'));
	}

	class EmoteVariableControls::Implementation final : public EmoteVariableIo {
	public:
		Implementation(MotionPlayerCommands& commands, std::function<void()> require_mutable, EmoteVariableProgram program, std::function<std::uint32_t()> read_entropy_word, std::function<void(float)> update_properties) : Commands(commands), RequireMutable(std::move(require_mutable)), UpdateProperties(std::move(update_properties)), Missing(std::move(program.UnsupportedFamilies)) {
			if (!RequireMutable || program.PrimarySelectorCount > program.Selectors.Selectors.size()) {
				throw std::runtime_error("Emote variable publisher requires actual mutable Motion owner and exact selector order");
			}
			std::size_t total_controls = 0;
			for (const auto count : {program.Transitions.size(), program.Immediate.size(), program.Extensions.size(), program.Selectors.Selectors.size(), program.Eyebrows.size(), program.Mouths.size(), program.Eyes.size(), program.Clamps.size(), program.StereoVariables.size(), program.InstantVariables.size(), program.TimelineCatalog.size()}) {
				if (count > kMaximumEmoteControls - total_controls) {
					throw std::runtime_error("Emote variable program exceeds bounded control ownership");
				}
				total_controls += count;
			}
			RequireMutable();
			Clamps = std::move(program.Clamps);
			for (const auto& clamp : Clamps) {
				for (const auto* name : {&clamp.LeftRight, &clamp.UpDown}) {
					if (name->size() >= kMaximumEmoteChannelBytes - KeyBytes || ControlName(*name) != *name || name->find("::") != std::string::npos || name->find('/') != std::string::npos) {
						throw std::runtime_error("Emote clamp publication key exceeds bounded actual owner storage");
					}
					KeyBytes += name->size() + 1;
				}
				(void)ApplyEmoteClamp(clamp.Transform, clamp.Transform.Minimum, clamp.Transform.Minimum);
			}
			if (read_entropy_word) {
				Random.emplace(read_entropy_word());
			} else if (!program.Eyes.empty()) {
				Missing.emplace_back("eyeControl actual entropy-word service");
			}
			Channels.reserve(program.Transitions.size() + program.Extensions.size());
			Selectors.reserve(program.Selectors.Selectors.size());
			Eyebrows.reserve(program.Eyebrows.size());
			Mouths.reserve(program.Mouths.size());
			Eyes.reserve(program.Eyes.size());
			for (auto& definition : program.Eyes) {
				AddEye(std::move(definition));
			}
			for (auto& definition : program.Eyebrows) {
				AddRoute(definition.Name, {EmoteVariableRouteKind::Eyebrow, Eyebrows.size()});
				Eyebrows.emplace_back(std::move(definition));
			}
			for (auto& definition : program.Mouths) {
				AddMouth(std::move(definition));
			}
			for (auto& name : program.Transitions) {
				AddChannel(std::move(name));
			}
			for (std::size_t index = 0; index != program.PrimarySelectorCount; ++index) {
				AddSelector(std::move(program.Selectors.Selectors[index]));
			}
			for (auto& name : program.Immediate) {
				AddRoute(std::move(name), {EmoteVariableRouteKind::Immediate, 0});
			}
			if (program.DefaultImmediate) {
				// Native 2c24f0/96a478: only absent routes get the default immediate + instant policy.
				for (const auto name : {"face_cheek", "face_tears", "face_eye_hi"}) {
					if (Routes.find(name) == Routes.end()) {
						AddRoute(name, {EmoteVariableRouteKind::Immediate, 0});
						AddInstant(name);
					}
				}
			}
			for (auto& name : program.InstantVariables) {
				AddInstant(name);
			}
			MainTimelines.reserve(program.TimelineCatalog.size());
			DifferenceTimelines.reserve(program.TimelineCatalog.size());
			for (auto& entry : program.TimelineCatalog) {
				OwnPolicyName(entry.Name);
				(entry.Difference ? DifferenceTimelines : MainTimelines).push_back(std::move(entry.Name));
			}
			// Native2c7f70 resolves ranges after the real initial plain update, then dispatches zero only to matches.
			// Stereo replication itself is disabled at construction; a later enabling API is not supplied here.
			for (const auto& name : program.StereoVariables) {
				if (const auto bounds = Commands.ParameterValues().RegisteredBounds(name)) {
					AddStereoMatch(name, *bounds);
					Set(name, 0, 0, 0);
				}
			}

			PrimaryChannelCount = Channels.size();
			for (auto& name : program.Extensions) {
				if (Routes.find(name) == Routes.end() && std::find(program.VariableLabels.begin(), program.VariableLabels.end(), name) == program.VariableLabels.end()) {
					AddChannel(std::move(name));
				}
			}
			for (std::size_t index = program.PrimarySelectorCount; index != program.Selectors.Selectors.size(); ++index) {
				AddSelector(std::move(program.Selectors.Selectors[index]));
			}
			// Native 2bb770 checks the completed route map, including duplicate custom labels.
			for (std::size_t index = program.PrimarySelectorCount; index != Selectors.size(); ++index) {
				const auto& name = Selectors[index]->Definition().Label;
				if (Routes.count(name) == 1) {
					AddInstant(name);
				}
			}
		}

		MotionPlayerCommands& Commands;
		std::function<void()> RequireMutable;
		std::function<void(float)> UpdateProperties;
		std::vector<std::string> Missing;
		std::vector<EmoteScalarChannel> Channels;
		std::size_t PrimaryChannelCount = 0;
		std::vector<std::unique_ptr<EmoteSelectorControl>> Selectors;
		std::vector<EmoteGraphChannel> Eyebrows;
		std::vector<EmoteMouthChannel> Mouths;
		std::optional<EmoteBlinkRandom> Random;
		std::vector<EmoteEyeChannel> Eyes;
		std::vector<EmoteClampDefinition> Clamps;
		std::map<std::string, std::array<std::array<float, 2>, 2>, std::less<>> StereoMatches;
		std::set<std::string, std::less<>> InstantVariables;
		std::vector<std::string> MainTimelines;
		std::vector<std::string> DifferenceTimelines;
		std::multimap<std::string, EmoteVariableRoute, std::less<>> Routes;
		std::map<std::string, EmotePublishedValue, std::less<>> Values;
		std::size_t ActiveValues = 0;
		std::size_t KeyBytes = 0;
		std::size_t DispatchDepth = 0;
		bool Updating = false;
		// Native2c0350 initial coordinate/scale/rotation/color setters leave Emote+89 dirty before metadata setup.
		bool Dirty = true;
		bool BlinkEnabled = true;

		void OwnPolicyName(std::string_view name) {
			if (name.size() >= kMaximumEmoteChannelBytes - KeyBytes || ControlName(name) != name) {
				throw std::runtime_error("Emote declared timeline/instant policy exceeds bounded actual name storage");
			}
			KeyBytes += name.size() + 1;
		}

		void AddStereoMatch(std::string_view name, const std::array<float, 2>& bounds) {
			// Native2c0350 defaults: two views, scale1 and separation0.2; 2d20b0 prepares each affine in order.
			const auto width = bounds[1] - bounds[0];
			if (!std::isfinite(width) || width <= 0) {
				throw std::runtime_error("Emote stereo parameter bounds exceed the finite prepared domain");
			}
			if (StereoMatches.find(name) != StereoMatches.end()) {
				return;
			}
			OwnPolicyName(name);
			std::array<std::array<float, 2>, 2> views;
			for (std::size_t view = 0; view != views.size(); ++view) {
				const auto origin = static_cast<float>(1 - view) * width * 0.2f / 1.0f + bounds[0];
				const auto scale = (1.0f / width) * (origin + (1.0f - 0.2f) * width - origin);
				const auto offset = origin - scale * bounds[0];
				if (!std::isfinite(scale) || !std::isfinite(offset)) {
					throw std::runtime_error("Emote stereo affine exceeds the finite prepared domain");
				}
				views[view] = {scale, offset};
			}
			StereoMatches.emplace(name, views);
		}

		void AddInstant(std::string_view name) {
			if (InstantVariables.find(name) == InstantVariables.end()) {
				if (InstantVariables.size() == kMaximumEmoteControls) {
					throw std::runtime_error("Emote instant policy exceeds bounded initialization count");
				}
				OwnPolicyName(name);
				InstantVariables.emplace(name);
			}
		}

		void AddRoute(std::string name, EmoteVariableRoute route) {
			const bool new_value = Values.find(name) == Values.end();
			const auto copies = new_value ? std::size_t{2} : std::size_t{1};
			if (name.size() >= (kMaximumEmoteChannelBytes - KeyBytes) / copies || Routes.size() == kMaximumEmoteControls || name.find('\0') != std::string::npos || name.size() > kMaximumMotionCommandNameBytes || name.find("::") != std::string::npos || name.find('/') != std::string::npos) {
				throw std::runtime_error("Emote route keys exceed bounded owner storage");
			}
			KeyBytes += (name.size() + 1) * copies;
			Values.try_emplace(name);
			Routes.emplace(std::move(name), route);
		}

		void AddChannel(std::string name) {
			EmoteScalarChannel channel;
			channel.Name = name;
			AddRoute(std::move(name), {EmoteVariableRouteKind::Transition, Channels.size()});
			Channels.push_back(std::move(channel));
		}

		void AddSelector(resource::EmoteSelectorDefinition definition) {
			auto label = definition.Label;
			// The new selector's constructor applies before its own route enters the actual control map.
			auto selector = std::make_unique<EmoteSelectorControl>(std::move(definition), *this);
			AddRoute(std::move(label), {EmoteVariableRouteKind::Selector, Selectors.size()});
			Selectors.push_back(std::move(selector));
		}

		void AddMouth(EmoteMouthDefinition definition) {
			AddRoute(definition.Name, {EmoteVariableRouteKind::Mouth, Mouths.size()});
			AddRoute(definition.TalkName, {EmoteVariableRouteKind::Mouth, Mouths.size()});
			EmoteMouthChannel channel;
			channel.Name = std::move(definition.Name);
			channel.TalkName = std::move(definition.TalkName);
			channel.Frame = definition.InitialFrame;
			Mouths.push_back(std::move(channel));
		}

		void AddEye(EmoteEyeDefinition definition) {
			const EmoteVariableRoute route{EmoteVariableRouteKind::Eye, Eyes.size()};
			AddRoute(definition.Name, route);
			for (const auto* name : {&definition.LeftName, &definition.RightName}) {
				if (!name->empty()) {
					AddRoute(*name, route);
				}
			}
			if (definition.BlinkParameterIndex >= 0 && !definition.BlinkName.empty()) {
				AddRoute(definition.BlinkName, route);
			} else if (!definition.BlinkName.empty() && Values.find(definition.BlinkName) == Values.end()) {
				const auto& name = definition.BlinkName;
				if (name.size() >= kMaximumEmoteChannelBytes - KeyBytes || ControlName(name) != name || name.find("::") != std::string::npos || name.find('/') != std::string::npos || Values.size() == kMaximumEmoteControls) {
					throw std::runtime_error("Emote blink publication key exceeds bounded actual owner storage");
				}
				KeyBytes += name.size() + 1;
				Values.try_emplace(name);
			}
			Eyes.emplace_back(std::move(definition), Random ? &*Random : nullptr);
		}

		bool Animating(std::span<const std::string_view> timeline_variables) const {
			RequireComplete();
			RequireMutable();
			if (Updating || DispatchDepth != 0 || timeline_variables.size() > kMaximumEmoteAnimationNames || !std::is_sorted(timeline_variables.begin(), timeline_variables.end())) {
				throw std::runtime_error("Emote animation query requires idle controls and bounded sorted exact timeline names");
			}
			const auto overridden = [timeline_variables](std::string_view name) {
				return std::binary_search(timeline_variables.begin(), timeline_variables.end(), name);
			};
			for (const auto& selector : Selectors) {
				const auto state = selector->TransitionState();
				if ((state.Active || state.Queued != 0) && !overridden(selector->Definition().Label)) {
					return true;
				}
			}
			for (const auto& channel : Channels) {
				if (channel.Transitioning && !overridden(channel.Name)) {
					return true;
				}
			}
			for (const auto& eye : Eyes) {
				const auto changing = std::any_of(eye.Channels.begin(), eye.Channels.end(), [](const auto& channel) {
					return channel.Transitioning;
				});
				if (changing && !overridden(eye.Definition.Name)) {
					return true;
				}
			}
			for (const auto& channel : Eyebrows) {
				if (channel.Transitioning && !overridden(channel.Name)) {
					return true;
				}
			}
			for (const auto& mouth : Mouths) {
				if ((mouth.Pending || mouth.Transitioning) && !overridden(mouth.Name) && !overridden(mouth.TalkName)) {
					return true;
				}
			}

			return false;
		}

		void RequireCanFinishTransitions() const {
			RequireComplete();
			RequireMutable();
			if (Updating || DispatchDepth != 0) {
				throw std::runtime_error("Emote transition completion requires its idle actual control owner");
			}
		}

		void FinishNamedTransitions() {
			RequireCanFinishTransitions();
			Updating = true;
			try {
				for (auto& eye : Eyes) {
					for (auto& channel : eye.Channels) {
						FinishChannelTransition(channel);
					}
				}
				for (auto& channel : Eyebrows) {
					FinishChannelTransition(channel);
				}
				for (auto& mouth : Mouths) {
					if (mouth.Pending || mouth.Transitioning) {
						mouth.Talk = mouth.Target;
						mouth.Pending = false;
						mouth.Transitioning = false;
					}
				}
				for (auto& selector : Selectors) {
					// Apply uses this exact owner's internal VariableIo, retaining normal recursive dispatch bounds.
					selector->FinishTransitions();
				}
				for (std::size_t index = 0; index != PrimaryChannelCount; ++index) {
					FinishChannelTransition(Channels[index]);
				}
				Updating = false;
			} catch (...) {
				Updating = false;
				throw;
			}
		}

		void FinishExtensionTransitions() {
			RequireCanFinishTransitions();
			// Native2d14d0 visits the ordered extension map, including later speed-control bindings.
			for (const auto& [name, route] : Routes) {
				static_cast<void>(name);
				if (route.Kind == EmoteVariableRouteKind::Transition && route.Index >= PrimaryChannelCount) {
					FinishChannelTransition(Channels[route.Index]);
				}
			}
		}

		void RequireCanAssignState(const Implementation& source) const {
			RequireComplete();
			source.RequireComplete();
			RequireMutable();
			source.RequireMutable();
			if (Updating || DispatchDepth != 0 || source.Updating || source.DispatchDepth != 0 || Random.has_value() != source.Random.has_value()) {
				throw std::runtime_error("Emote variable assignment requires two idle actual owners and compatible random-state producers");
			}

			VisitAssignedControls(Eyes, source.Eyes, [](const auto& eye) -> std::string_view {
				return eye.Definition.Name;
			},
			    RequireEyeAssignment);
			VisitAssignedControls(Selectors, source.Selectors, [](const auto& selector) -> std::string_view {
				return selector->Definition().Label;
			},
			    [](const auto& destination, const auto& origin) {
				    destination->RequireCanAssignState(*origin);
			    });
		}

		std::map<std::string, EmotePublishedValue, std::less<>> PrepareAssignedValues(const Implementation& source, std::size_t& key_bytes, std::size_t& active_values) const {
			std::map<std::string, EmotePublishedValue, std::less<>> values;
			for (const auto& route : Routes) {
				values.try_emplace(route.first);
			}
			for (const auto& eye : Eyes) {
				if (!eye.Definition.BlinkName.empty()) {
					values.try_emplace(eye.Definition.BlinkName);
				}
			}
			active_values = 0;
			for (const auto& entry : source.Values) {
				if (entry.second.Present) {
					values.insert_or_assign(entry.first, entry.second);
					++active_values;
				}
			}
			if (values.size() > kMaximumEmoteControls) {
				throw std::runtime_error("Emote assignment value slots exceed the actual bounded owner count");
			}

			std::size_t old_value_bytes = 0;
			for (const auto& entry : Values) {
				if (entry.first.size() >= KeyBytes - old_value_bytes) {
					throw std::runtime_error("Emote assignment lost its actual value-key accounting");
				}
				old_value_bytes += entry.first.size() + 1;
			}
			key_bytes = KeyBytes - old_value_bytes;
			for (const auto& entry : values) {
				if (entry.first.size() >= kMaximumEmoteChannelBytes - key_bytes) {
					throw std::runtime_error("Emote assignment value keys exceed the actual bounded owner storage");
				}
				key_bytes += entry.first.size() + 1;
			}

			return values;
		}

		void AssignState(const Implementation& source) {
			RequireCanAssignState(source);
			if (this == &source) {
				return;
			}

			std::size_t key_bytes = 0;
			std::size_t active_values = 0;
			auto values = PrepareAssignedValues(source, key_bytes, active_values);
			if (Random) {
				*Random = *source.Random;
			}
			Values = std::move(values);
			KeyBytes = key_bytes;
			ActiveValues = active_values;
			BlinkEnabled = source.BlinkEnabled;

			VisitAssignedControls(Eyes, source.Eyes, [](const auto& eye) -> std::string_view {
				return eye.Definition.Name;
			},
			    AssignEyeChannel);
			VisitAssignedControls(Eyebrows, source.Eyebrows, [](const auto& channel) -> std::string_view {
				return channel.Name;
			},
			    AssignGraphChannel);
			VisitAssignedControls(Mouths, source.Mouths, [](const auto& channel) -> std::string_view {
				return channel.Name;
			},
			    AssignMouthChannel);
			VisitAssignedControls(Selectors, source.Selectors, [](const auto& selector) -> std::string_view {
				return selector->Definition().Label;
			},
			    [](auto& destination, const auto& origin) {
				    destination->AssignState(*origin);
			    });
			VisitAssignedControls(Channels, source.Channels, [](const auto& channel) -> std::string_view {
				return channel.Name;
			},
			    AssignScalarChannel);
		}

		float Read(std::string_view name) const override {
			name = ControlName(name);
			if (const auto found = Values.find(name); found != Values.end() && found->second.Present) {
				return found->second.Value;
			}
			return Commands.ParameterValues().Read(name);
		}

		void Set(std::string_view name, float value, float duration, float easing) override {
			name = ControlName(name);
			if (!std::isfinite(value) || !std::isfinite(duration) || !std::isfinite(easing) || DispatchDepth == 64) {
				throw std::runtime_error("Emote variable dispatch exceeds finite bounded actual controller domain");
			}
			const auto [begin, end] = Routes.equal_range(name);
			for (auto route = begin; route != end; ++route) {

				if (route->second.Kind == EmoteVariableRouteKind::Selector) {
					if (duration > 0 && !std::isfinite(1.0f / duration)) {
						throw std::runtime_error("Emote selector transition requires a finite native reciprocal duration");
					}
					if (value < -2147483648.0f || value >= 2147483648.0f) {
						throw std::runtime_error("Emote selector value exceeds native finite signed conversion");
					}
				}
				if (route->second.Kind == EmoteVariableRouteKind::Mouth && name == Mouths[route->second.Index].Name && (value < -2147483648.0f || value >= 2147483648.0f)) {
					throw std::runtime_error("Emote mouth frame exceeds native finite signed conversion");
				}
				if (route->second.Kind == EmoteVariableRouteKind::Eye && IsBlinkSelection(Eyes[route->second.Index], name)) {
					const auto& eye = Eyes[route->second.Index];
					if (value < -2147483648.0f || value >= 2147483648.0f || static_cast<std::int32_t>(value) < 0 || static_cast<std::size_t>(static_cast<std::int32_t>(value)) >= eye.Definition.BlinkChoices.size()) {
						throw std::runtime_error("Emote blink parameter selection exceeds its actual owned parameter vector");
					}
				}
			}
			Dirty = true;
			++DispatchDepth;
			try {
				for (auto route = begin; route != end; ++route) {
					switch (route->second.Kind) {
					case EmoteVariableRouteKind::Transition:
						SetChannel(Channels[route->second.Index], value, duration, easing);
						break;
					case EmoteVariableRouteKind::Immediate:
						AssignValue(name, value);
						break;
					case EmoteVariableRouteKind::Selector:
						Selectors[route->second.Index]->Set(value, duration, easing);
						break;
					case EmoteVariableRouteKind::Eyebrow:
						SetGraph(Eyebrows[route->second.Index], value, duration, easing);
						break;
					case EmoteVariableRouteKind::Mouth:
						SetMouth(Mouths[route->second.Index], name, value, duration, easing);
						break;
					case EmoteVariableRouteKind::Eye:
						SetEye(Eyes[route->second.Index], name, value, duration, easing);
						break;
					}
				}
				--DispatchDepth;
			} catch (...) {
				--DispatchDepth;
				throw;
			}
		}

		void SetChannel(EmoteScalarChannel& channel, float value, float duration, float easing) {
			if (!(duration > 0)) {
				channel.Current = value;
				channel.Transitioning = false;
				return;
			}
			channel.Start = channel.Current;
			channel.Target = value;
			channel.Duration = duration;
			channel.Elapsed = 0;
			channel.Easing = easing;
			channel.Transitioning = true;
		}

		void SetGraph(EmoteGraphChannel& channel, float value, float duration, float easing) {
			if (!(duration > 0)) {
				channel.Current = value;
				channel.Transitioning = false;
				return;
			}
			channel.Graph.Prepare(channel.Current, value);
			channel.Target = value;
			channel.Duration = duration;
			channel.Elapsed = 0;
			channel.Easing = easing;
			channel.Transitioning = true;
		}

		void SetMouth(EmoteMouthChannel& channel, std::string_view name, float value, float duration, float easing) {
			if (name == channel.Name) {
				channel.Frame = static_cast<std::int32_t>(value);
				return;
			}
			if (!(duration > 0)) {
				channel.Talk = value;
				channel.Pending = false;
				channel.Transitioning = false;
				return;
			}
			if (channel.Transitioning && channel.Target == value && duration > (1.0f - channel.Progress) / channel.ReciprocalDuration) {
				return;
			}
			channel.Target = value;
			channel.Duration = duration;
			channel.Easing = easing;
			channel.Pending = true;
			channel.Transitioning = false;
		}

		void SetEye(EmoteEyeChannel& eye, std::string_view name, float value, float duration, float easing) {
			if (name == eye.Definition.Name) {
				if (eye.Blink) {
					eye.Blink->ResetIdleClock();
				}
				for (auto& channel : eye.Channels) {
					SetGraph(channel, value, duration, easing);
				}
			} else if (name == eye.Definition.LeftName) {
				if (eye.Blink) {
					eye.Blink->ResetIdleClock();
				}
				SetGraph(eye.Channels[1], value, duration, easing);
			} else if (name == eye.Definition.RightName) {
				if (eye.Blink) {
					eye.Blink->ResetIdleClock();
				}
				SetGraph(eye.Channels[2], value, duration, easing);
			} else if (name == eye.Definition.BlinkName) {
				const auto index = static_cast<std::int32_t>(value);
				eye.Definition.BlinkParameterIndex = index;
				if (eye.Blink) {
					eye.Blink->SetParameters(eye.Definition.BlinkChoices[static_cast<std::size_t>(index)], true);
				}
			}
		}

		void StepEyeChannels(float step) {
			for (auto& eye : Eyes) {
				for (auto& channel : eye.Channels) {
					if (channel.Transitioning) {
						channel.Elapsed += step;
						const auto ratio = std::min(channel.Elapsed / channel.Duration, 1.0f);
						channel.Current = channel.Graph.Evaluate(PrepareEmoteEasing(channel.Easing, ratio));
						if (!(ratio < 1)) {
							channel.Transitioning = false;
						}
					}
				}
				eye.Blink->Step(step);
				for (std::size_t index = 0; index != eye.Channels.size(); ++index) {
					const auto& channel = eye.Channels[index];
					if (index == 0 || !channel.Name.empty()) {
						AssignValue(channel.Name, ApplyEmoteBlinkFrame(channel.Current, static_cast<float>(eye.Definition.LowerFrame), static_cast<float>(eye.Definition.EndFrame), eye.Blink->Amount(), eye.Definition.BlinkParameterIndex));
					}
				}
				if (!eye.Definition.BlinkName.empty()) {
					AssignValue(eye.Definition.BlinkName, static_cast<float>(eye.Definition.BlinkParameterIndex));
				}
			}
		}

		void StepGraphChannels(float step) {
			for (auto& channel : Eyebrows) {
				if (channel.Transitioning) {
					channel.Elapsed += step;
					const auto ratio = std::min(channel.Elapsed / channel.Duration, 1.0f);
					channel.Current = channel.Graph.Evaluate(PrepareEmoteEasing(channel.Easing, ratio));
					if (!(ratio < 1)) {
						channel.Transitioning = false;
					}
				}
				AssignValue(channel.Name, channel.Current);
			}
			for (auto& channel : Mouths) {
				if (channel.Pending) {
					channel.Start = channel.Talk;
					channel.ReciprocalDuration = 1.0f / channel.Duration;
					channel.Progress = 0;
					channel.Pending = false;
					channel.Transitioning = true;
				}
				if (channel.Transitioning) {
					channel.Progress += step * channel.ReciprocalDuration;
					if (channel.Progress - 1.0f >= std::bit_cast<float>(std::uint32_t{0xb8d1b717})) {
						channel.Talk = channel.Target;
						channel.Progress = 1;
						channel.Transitioning = false;
					} else {
						channel.Talk = channel.Start + PrepareEmoteEasing(channel.Easing, channel.Progress) * (channel.Target - channel.Start);
					}
				}
				AssignValue(channel.Name, static_cast<float>(channel.Frame));
				AssignValue(channel.TalkName, channel.Talk);
			}
		}

		void AssignValue(std::string_view name, float value) {
			const auto found = Values.find(name);
			if (found == Values.end()) {
				throw std::runtime_error("Emote channel lost its actual initialized value slot");
			}
			if (!found->second.Present) {
				++ActiveValues;
				found->second.Present = true;
			}
			found->second.Value = value;
		}

		void RequireComplete() const {
			if (!Missing.empty()) {
				throw std::runtime_error("Emote variable update requires recovered " + Missing.front());
			}
		}

		void PublishClamps(const EmoteTimelineScheduler* timelines) {
			for (const auto& clamp : Clamps) {
				const auto current = [&](std::string_view name) {
					const auto found = Values.find(name);
					const auto base = found != Values.end() && found->second.Present ? found->second.Value : 0.0f;

					// Native 2cd7bb/2cd7ce compose active difference tracks before the paired clamp.
					return timelines == nullptr ? base : timelines->ApplyDifference(name, base);
				};
				const auto pair = ApplyEmoteClamp(clamp.Transform, current(clamp.LeftRight), current(clamp.UpDown));
				Commands.ParameterValues().WriteLocal(clamp.LeftRight, pair[0], 0);
				Commands.ParameterValues().WriteLocal(clamp.UpDown, pair[1], 0);
			}
		}

		void Update(float elapsed, const EmoteTimelineScheduler* timelines = nullptr) {
			RequireComplete();
			if (!std::isfinite(elapsed) || elapsed < 0 || elapsed > 1000000.0f || Updating || DispatchDepth != 0) {
				throw std::runtime_error("Emote variable update requires finite nonnegative actual delta and idle controls");
			}
			const auto channels = std::max(std::size_t{1}, Channels.size() + Selectors.size() + Eyebrows.size() + Mouths.size() + 3 * Eyes.size());
			const auto steps = std::ceil(static_cast<double>(elapsed) / static_cast<double>(1.1f)) + 1.0;
			if (steps > static_cast<double>(1000000 / channels)) {
				throw std::runtime_error("Emote update exceeds the bounded channel/step work domain");
			}
			RequireMutable();
			Updating = true;
			try {
				float remaining = elapsed;
				while (remaining > 0 || Dirty) {
					const auto step = std::min(1.1f, remaining);
					Dirty = false;
					StepEyeChannels(step);
					StepGraphChannels(step);
					for (std::size_t index = 0; index != Selectors.size(); ++index) {
						Selectors[index]->Update(step);
						AssignValue(Selectors[index]->Definition().Label, static_cast<float>(Selectors[index]->Selection()));
					}
					for (auto& channel : Channels) {
						if (channel.Transitioning) {
							channel.Elapsed += step;
							const auto ratio = PrepareEmoteEasing(channel.Easing, std::min(channel.Elapsed / channel.Duration, 1.0f));
							channel.Current = (1.0f - ratio) * channel.Start + ratio * channel.Target;
							if (!(channel.Elapsed < channel.Duration)) {
								channel.Transitioning = false;
							}
						}
						AssignValue(channel.Name, channel.Current);
					}
					// Native2d2f6b runs actual2cc510 after scalar channels, within the same bounded dirty loop.
					if (UpdateProperties) {
						UpdateProperties(step);
					}
					remaining -= step;
				}
				for (const auto& [name, value] : Values) {
					// Native 2ce880 writes flags0 after processing controls, before actual plain-player Update.
					if (value.Present) {
						Commands.ParameterValues().WriteLocal(name, timelines == nullptr ? value.Value : timelines->ApplyDifference(name, value.Value), 0);
					}
				}
				PublishClamps(timelines);
				Updating = false;
			} catch (...) {
				Updating = false;
				throw;
			}
		}
	};

	EmoteVariableControls::EmoteVariableControls(MotionPlayerCommands& commands, std::function<void()> require_mutable, EmoteVariableProgram program, std::function<std::uint32_t()> read_entropy_word, std::function<void(float)> update_properties) : m_Implementation(std::make_unique<Implementation>(commands, std::move(require_mutable), std::move(program), std::move(read_entropy_word), std::move(update_properties))) {
	}

	EmoteVariableControls::~EmoteVariableControls() noexcept = default;

	bool EmoteVariableControls::Animating(std::span<const std::string_view> timeline_variables) const {
		return m_Implementation->Animating(timeline_variables);
	}

	void EmoteVariableControls::RequireCanAssignState(const EmoteVariableControls& source) const {
		m_Implementation->RequireCanAssignState(*source.m_Implementation);
	}

	void EmoteVariableControls::AssignState(const EmoteVariableControls& source) {
		m_Implementation->AssignState(*source.m_Implementation);
	}
	void EmoteVariableControls::FinishNamedTransitions() {
		m_Implementation->FinishNamedTransitions();
	}
	void EmoteVariableControls::FinishExtensionTransitions() {
		m_Implementation->FinishExtensionTransitions();
	}

	void EmoteVariableControls::UpdateWithTimelines(float elapsed_ticks, const EmoteTimelineScheduler& actual_timelines) {
		actual_timelines.RequirePublicationOwner(*this, m_Implementation->Values.size());
		m_Implementation->Update(elapsed_ticks, &actual_timelines);
	}

	std::span<const std::string> EmoteVariableControls::MissingFamilies() const noexcept {
		return m_Implementation->Missing;
	}

	std::optional<float> EmoteVariableControls::TransitionValue(std::string_view identifier) const {
		m_Implementation->RequireMutable();
		identifier = ControlName(identifier);
		for (const auto& channel : m_Implementation->Channels) {
			if (channel.Name == identifier) {
				return channel.Current;
			}
		}
		for (const auto& channel : m_Implementation->Eyebrows) {
			if (channel.Name == identifier) {
				return channel.Current;
			}
		}
		for (const auto& channel : m_Implementation->Mouths) {
			if (channel.Name == identifier) {
				return static_cast<float>(channel.Frame);
			}
			if (channel.TalkName == identifier) {
				return channel.Talk;
			}
		}
		for (const auto& eye : m_Implementation->Eyes) {
			if (eye.Definition.Name == identifier) {
				return eye.Channels[0].Current;
			}
			if (!eye.Definition.LeftName.empty() && eye.Definition.LeftName == identifier) {
				return eye.Channels[1].Current;
			}
			if (!eye.Definition.RightName.empty() && eye.Definition.RightName == identifier) {
				return eye.Channels[2].Current;
			}
			if (!eye.Definition.BlinkName.empty() && eye.Definition.BlinkName == identifier) {
				return static_cast<float>(eye.Definition.BlinkParameterIndex);
			}
		}
		return std::nullopt;
	}

	bool EmoteVariableControls::UsesCommands(const MotionPlayerCommands& actual_commands) const noexcept {
		return &m_Implementation->Commands == &actual_commands;
	}

	bool EmoteVariableControls::BindSpeedTransition(std::string_view identifier) {
		auto& state = *m_Implementation;
		state.RequireMutable();
		identifier = ControlName(identifier);
		if (state.Updating || state.DispatchDepth != 0) {
			throw std::runtime_error("Emote speed binding requires its actual idle transition owner");
		}
		const auto [begin, end] = state.Routes.equal_range(identifier);
		for (auto route = begin; route != end; ++route) {
			if (route->second.Kind == EmoteVariableRouteKind::Transition) {
				return true;
			}
		}
		if (begin != end) {
			return false;
		}
		if (identifier.size() >= (kMaximumEmoteChannelBytes - state.KeyBytes) / 2 || state.Routes.size() == kMaximumEmoteControls || identifier.find("::") != std::string_view::npos || identifier.find('/') != std::string_view::npos) {
			throw std::runtime_error("Emote speed transition exceeds bounded actual route storage");
		}
		// Prepare every allocation before publishing the new route/slot. Indices survive channel-vector relocation.
		EmoteScalarChannel channel;
		channel.Name = identifier;
		channel.Current = 1;
		std::map<std::string, EmotePublishedValue, std::less<>> values;
		std::multimap<std::string, EmoteVariableRoute, std::less<>> routes;
		values.emplace(std::string(identifier), EmotePublishedValue{});
		routes.emplace(std::string(identifier), EmoteVariableRoute{EmoteVariableRouteKind::Transition, state.Channels.size()});
		state.Channels.reserve(state.Channels.size() + 1);
		state.Values.insert(values.extract(values.begin()));
		state.Routes.insert(routes.extract(routes.begin()));
		state.Channels.push_back(std::move(channel));
		state.KeyBytes += (identifier.size() + 1) * 2;
		state.Dirty = true;
		return true;
	}

	std::optional<float> EmoteVariableControls::SpeedTransitionValue(std::string_view identifier) const {
		const auto& state = *m_Implementation;
		state.RequireMutable();
		identifier = ControlName(identifier);
		const auto [begin, end] = state.Routes.equal_range(identifier);
		for (auto route = begin; route != end; ++route) {
			if (route->second.Kind == EmoteVariableRouteKind::Transition) {
				return state.Channels.at(route->second.Index).Current;
			}
		}
		return std::nullopt;
	}

	float EmoteVariableControls::Read(std::string_view identifier) const {
		m_Implementation->RequireMutable();
		return m_Implementation->Read(identifier);
	}

	void EmoteVariableControls::Set(std::string_view identifier, float value, float duration, float easing) {
		auto& state = *m_Implementation;
		state.RequireComplete();
		if (state.Updating) {
			throw std::runtime_error("Emote variable control cannot reenter actual publication");
		}
		state.RequireMutable();
		state.Set(identifier, value, duration, easing);
	}

	void EmoteVariableControls::MarkPropertiesDirty() {
		auto& state = *m_Implementation;
		state.RequireComplete();
		if (state.Updating || state.DispatchDepth != 0 || !state.UpdateProperties) {
			throw std::runtime_error("Emote property mutation requires its actual idle containing publisher");
		}
		state.RequireMutable();
		state.Dirty = true;
	}

	void EmoteVariableControls::Update(float elapsed_ticks) {
		m_Implementation->Update(elapsed_ticks);
	}

	void EmoteVariableControls::SetBlinkEnabled(bool enabled) {
		auto& state = *m_Implementation;
		state.RequireComplete();
		if (state.Updating || state.DispatchDepth != 0) {
			throw std::runtime_error("Emote blink control cannot reenter actual publication");
		}
		state.RequireMutable();
		state.BlinkEnabled = enabled;
		for (auto& eye : state.Eyes) {
			eye.Blink->SetSuppressed(!enabled);
		}
	}

	bool EmoteVariableControls::BlinkEnabled() const {
		m_Implementation->RequireMutable();
		return m_Implementation->BlinkEnabled;
	}

	bool EmoteVariableControls::IsInstantVariable(std::string_view identifier) const {
		m_Implementation->RequireMutable();
		return m_Implementation->InstantVariables.find(ControlName(identifier)) != m_Implementation->InstantVariables.end();
	}

	std::span<const std::string> EmoteVariableControls::TimelineLabels(bool difference) const {
		m_Implementation->RequireMutable();
		return difference ? m_Implementation->DifferenceTimelines : m_Implementation->MainTimelines;
	}

	std::size_t EmoteVariableControls::PublishedVariableCount() const noexcept {
		return m_Implementation->ActiveValues;
	}

} // namespace sgre::scene
