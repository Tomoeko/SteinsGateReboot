#include "sgre/scene/emote_timeline_scheduler.hpp"

#include "sgre/scene/emote_timeline_tracks.hpp"
#include "sgre/scene/emote_timeline_transition.hpp"
#include "sgre/scene/emote_variable_controls.hpp"
#include "sgre/scene/motion_player_commands.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

namespace sgre::scene {
	static constexpr std::size_t kMaximumSchedulerEntries = 65536;
	static constexpr std::size_t kMaximumSchedulerNameBytes = 8 * 1024 * 1024;
	static constexpr std::size_t kMaximumSchedulerWork = 1000000;

	static std::string TimelineName(std::string_view name) {
		if (name.size() > kMaximumMotionCommandNameBytes) {
			throw std::runtime_error("Emote timeline name exceeds native bounded string domain");
		}
		name = name.substr(0, name.find('\0'));
		std::string result;
		result.reserve(name.size());
		for (std::size_t index = 0; index < name.size();) {
			const auto first = static_cast<std::uint8_t>(name[index]);
			const auto begin = index++;
			std::uint32_t code = first;
			std::size_t extra = 0;
			std::uint32_t minimum = 0;
			if (first >= 0xc2 && first <= 0xdf) {
				code &= 0x1f;
				extra = 1;
				minimum = 0x80;
			} else if (first >= 0xe0 && first <= 0xef) {
				code &= 0x0f;
				extra = 2;
				minimum = 0x800;
			} else if (first >= 0xf0 && first <= 0xf4) {
				code &= 7;
				extra = 3;
				minimum = 0x10000;
			} else if (first > 0x7f) {
				throw std::runtime_error("Emote timeline name requires supported actual UTF-8");
			}
			if (extra > name.size() - index) {
				throw std::runtime_error("Emote timeline name has truncated actual UTF-8");
			}
			for (std::size_t part = 0; part != extra; ++part) {
				const auto byte = static_cast<std::uint8_t>(name[index++]);
				if ((byte & 0xc0) != 0x80) {
					throw std::runtime_error("Emote timeline name has invalid actual UTF-8 continuation");
				}
				code = (code << 6) | (byte & 0x3f);
			}
			if (code < minimum || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff)) {
				throw std::runtime_error("Emote timeline name exceeds supported Unicode scalar domain");
			}
			// Native2d24c0 changes only fullwidth ASCII punctuation/letters/digits, preserving case.
			if (code >= 0xff01 && code <= 0xff5e) {
				result.push_back(static_cast<char>(code - 0xfee0));
			} else {
				result.append(name.substr(begin, index - begin));
			}
		}
		return result;
	}

	class EmoteTimelineScheduler::Implementation {
	public:
		struct Entry {
			const resource::PsbValue* Metadata = nullptr;
			std::unique_ptr<EmoteTimelineTracks> Tracks;
			std::uint32_t Flags = 0;
			bool Looped = false;
			EmoteTimelineTransition Blend;
			float BlendRatio = 1;
			bool StopWhenBlendFinished = false;
		};

		Implementation(EmoteVariableControls& controls, const resource::PsbDocument& document, std::function<void()> require_mutable)
		    : Controls(controls), RequireMutable(std::move(require_mutable)) {
			if (!RequireMutable) {
				throw std::runtime_error("Emote timeline scheduler requires its actual containing mutation guard");
			}
			RequireMutable();
			const auto* metadata = document.Root.Find("metadata");
			const auto* timelines = metadata == nullptr ? nullptr : metadata->Find("timelineControl");
			if (timelines == nullptr) {
				return;
			}
			const auto* entries = std::get_if<resource::PsbArray>(&timelines->Data);
			if (entries == nullptr || entries->size() > kMaximumSchedulerEntries) {
				throw std::runtime_error("Emote timeline scheduler requires bounded actual catalogue array");
			}
			std::size_t names = 0;
			for (const auto& actual : *entries) {
				const auto* label = actual.Find("label");
				const auto* text = label == nullptr ? nullptr : std::get_if<std::string>(&label->Data);
				if (text == nullptr) {
					throw std::runtime_error("Emote timeline catalogue requires its actual string label");
				}
				auto name = TimelineName(*text);
				if (name.size() >= kMaximumSchedulerNameBytes - names) {
					throw std::runtime_error("Emote timeline scheduler exceeds bounded catalogue name storage");
				}
				names += name.size() + 1;
				Entry entry;
				entry.Metadata = &actual;
				// Native2b37b0 map insertion retains the first definition for an equivalent normalized key.
				Entries.emplace(std::move(name), std::move(entry));
			}
			Active.reserve(Entries.size());
		}

		void RequireIdle() const {
			if (Thread != std::this_thread::get_id() || Operating || !Valid) {
				throw std::runtime_error("Emote timeline scheduler requires its owner thread and idle valid phase");
			}
		}
		void RequireMutation() const {
			RequireIdle();
			RequireMutable();
		}
		bool Animating() {
			RequireMutation();
			for (const auto* entry : Active) {
				if (entry->Blend.Changing() || entry->Tracks->Definition().LoopBegin < 0) {
					return true;
				}
			}
			if (TimelineVariablesDirty) {
				std::size_t count = 0;
				for (const auto* entry : Active) {
					const auto size = entry->Tracks->Definition().Tracks.size();
					if (size > kMaximumSchedulerWork - count) {
						throw std::runtime_error("Emote animation query exceeds bounded active timeline track names");
					}
					count += size;
				}
				TimelineVariables.clear();
				TimelineVariables.reserve(count);
				for (const auto* entry : Active) {
					for (const auto& track : entry->Tracks->Definition().Tracks) {
						TimelineVariables.push_back(track.Name);
					}
				}
				std::sort(TimelineVariables.begin(), TimelineVariables.end());
				TimelineVariables.erase(std::unique(TimelineVariables.begin(), TimelineVariables.end()), TimelineVariables.end());
				TimelineVariablesDirty = false;
			}

			return Controls.Animating(TimelineVariables);
		}

		void Play(std::string_view label, std::uint32_t flags) {
			RequireMutation();
			if ((flags & ~std::uint32_t{7}) != 0) {
				throw std::runtime_error("Emote timeline scheduler flags exceed recovered native playback policy");
			}
			const auto name = TimelineName(label);
			TimelineVariablesDirty = true;
			Operating = true;
			try {
				if ((flags & 1) == 0) {
					Active.clear();
				}
				const auto found = Entries.find(name);
				if (found != Entries.end()) {
					auto& entry = found->second;
					if (std::find(Active.begin(), Active.end(), &entry) == Active.end()) {
						Active.push_back(&entry);
					}
					if (!entry.Tracks) {
						entry.Tracks = std::make_unique<EmoteTimelineTracks>(Controls, *entry.Metadata, flags);
					}
					entry.Tracks->Reset(flags);
					entry.Flags = flags;
					entry.Looped = false;
					entry.BlendRatio = 1;
					entry.StopWhenBlendFinished = false;
					entry.Blend.Set(1);
					entry.Tracks->Seek(0);
				}
				Operating = false;
			} catch (...) {
				Operating = false;
				Valid = false;
				throw;
			}
		}
		void Stop(std::string_view label) {
			RequireMutation();
			TimelineVariablesDirty = true;
			const auto name = TimelineName(label);
			if (name.empty()) {
				Active.clear();
			} else if (const auto found = Entries.find(name); found != Entries.end()) {
				std::erase(Active, &found->second);
			}
		}
		void SetBlend(std::string_view label, float ratio, float duration, float easing, bool stop) {
			RequireMutation();
			// Validate even an absent label before entering any fallible transition mutation.
			EmoteTimelineTransition validated;
			validated.Set(ratio, duration, easing);
			if (const auto found = Entries.find(TimelineName(label)); found != Entries.end() && found->second.Tracks) {
				found->second.Blend.Set(ratio, duration, easing);
				found->second.StopWhenBlendFinished = stop;
			}
		}
		const std::string& EntryName(const Entry* entry, std::size_t& work) const {
			for (const auto& [name, candidate] : Entries) {
				if (work++ == kMaximumSchedulerWork) {
					throw std::runtime_error("Emote timeline assignment exceeds bounded catalogue matching work");
				}
				if (&candidate == entry) {
					return name;
				}
			}
			throw std::runtime_error("Emote timeline assignment requires actual source catalogue ownership");
		}
		void RequireCanAssign(const Implementation& source) const {
			RequireMutation();
			source.RequireMutation();
			std::size_t work = 0;
			for (const auto* origin : source.Active) {
				const auto& name = source.EntryName(origin, work);
				if (!origin->Tracks || (origin->Flags & ~std::uint32_t{7}) != 0) {
					throw std::runtime_error("Emote timeline assignment requires actual prepared source playback");
				}
				if (const auto destination = Entries.find(name); destination != Entries.end() && destination->second.Tracks) {
					destination->second.Tracks->RequireCanAssignState(*origin->Tracks);
				}
			}
		}
		void AssignState(const Implementation& source) {
			RequireCanAssign(source);
			if (this == &source) {
				return;
			}
			Operating = true;
			try {
				TimelineVariablesDirty = true;
				Active.clear();
				std::size_t work = 0;
				for (const auto* origin : source.Active) {
					const auto found = Entries.find(source.EntryName(origin, work));
					if (found == Entries.end()) {
						continue;
					}
					auto& destination = found->second;
					const auto flags = origin->Flags | 1u;
					if (!destination.Tracks) {
						destination.Tracks = std::make_unique<EmoteTimelineTracks>(Controls, *destination.Metadata, flags);
					}
					destination.Tracks->AssignState(*origin->Tracks, flags);
					destination.Flags = flags;
					destination.Looped = false;
					destination.Blend = origin->Blend;
					destination.BlendRatio = origin->BlendRatio;
					destination.StopWhenBlendFinished = origin->StopWhenBlendFinished;
					Active.push_back(&destination);
				}
				Operating = false;
			} catch (...) {
				Operating = false;
				Valid = false;
				throw;
			}
		}
		static void AdvanceDifference(Entry& entry, float elapsed) {
			if ((entry.Flags & 2) != 0) {
				entry.BlendRatio = entry.Blend.Advance(elapsed);
				entry.Tracks->AdvanceDifference(elapsed);
			}
		}
		void Preflight(float elapsed) const {
			std::size_t work = 0;
			for (const auto* entry : Active) {
				const auto& definition = entry->Tracks->Definition();
				std::size_t keys = 1;
				for (const auto& track : definition.Tracks) {
					if (track.Keys.size() > kMaximumSchedulerWork - keys) {
						throw std::runtime_error("Emote timeline scheduler exceeds bounded key work");
					}
					keys += track.Keys.size();
				}
				double repetitions = 1;
				if (definition.LoopBegin >= 0) {
					const auto span = definition.LoopEnd - definition.LoopBegin;
					if (!(span > 0) || !std::isfinite(span) || entry->Tracks->Position() > definition.LoopEnd) {
						throw std::runtime_error("Emote timeline loop requires an ordered finite positive native interval");
					}
					repetitions += std::ceil(static_cast<double>(elapsed) / static_cast<double>(span));
				}
				if (repetitions > static_cast<double>((kMaximumSchedulerWork - work) / keys)) {
					throw std::runtime_error("Emote timeline scheduler exceeds bounded crossed-loop work");
				}
				work += static_cast<std::size_t>(repetitions) * keys;
			}
		}
		void Update(float elapsed) {
			RequireMutation();
			if (!std::isfinite(elapsed) || elapsed < 0) {
				throw std::runtime_error("Emote timeline scheduler requires finite nonnegative elapsed ticks");
			}
			if (elapsed == 0) {
				return;
			}
			Preflight(elapsed);
			Operating = true;
			try {
				for (auto iterator = Active.begin(); iterator != Active.end();) {
					auto& entry = **iterator;
					auto& tracks = *entry.Tracks;
					const auto& definition = tracks.Definition();
					if (definition.LoopBegin >= 0) {
						AdvanceDifference(entry, elapsed);
						float remaining = elapsed;
						while (!(tracks.Position() + remaining < definition.LoopEnd)) {
							remaining -= definition.LoopEnd - tracks.Position();
							tracks.CrossTo(definition.LoopEnd, false);
							tracks.Seek(definition.LoopBegin);
							entry.Looped = true;
						}
						tracks.CrossTo(tracks.Position() + std::max(0.0f, remaining));
					} else {
						tracks.CrossTo(tracks.Position() + elapsed);
						AdvanceDifference(entry, elapsed);
					}
					if ((definition.LoopBegin < 0 && !(tracks.Position() < definition.LastTime)) || (entry.StopWhenBlendFinished && !entry.Blend.Changing())) {
						TimelineVariablesDirty = true;
						iterator = Active.erase(iterator);
					} else {
						++iterator;
					}
				}
				Operating = false;
			} catch (...) {
				Operating = false;
				Valid = false;
				throw;
			}
		}

		// Views borrow immutable prepared track definitions; membership changes invalidate this query cache.
		std::vector<std::string_view> TimelineVariables;
		bool TimelineVariablesDirty = true;
		EmoteVariableControls& Controls;
		std::function<void()> RequireMutable;
		std::map<std::string, Entry, std::less<>> Entries;
		std::vector<Entry*> Active;
		const std::thread::id Thread = std::this_thread::get_id();
		bool Operating = false;
		bool Valid = true;
	};

	EmoteTimelineScheduler::EmoteTimelineScheduler(EmoteVariableControls& controls, const resource::PsbDocument& document, std::function<void()> require_mutable)
	    : m_Implementation(std::make_unique<Implementation>(controls, document, std::move(require_mutable))) {
	}
	EmoteTimelineScheduler::~EmoteTimelineScheduler() noexcept = default;
	void EmoteTimelineScheduler::Play(std::string_view name, std::uint32_t flags) {
		m_Implementation->Play(name, flags);
	}
	void EmoteTimelineScheduler::Stop(std::string_view name) {
		m_Implementation->Stop(name);
	}
	void EmoteTimelineScheduler::RequireCanAssignState(const EmoteTimelineScheduler& source) const {
		m_Implementation->RequireCanAssign(*source.m_Implementation);
	}
	void EmoteTimelineScheduler::AssignState(const EmoteTimelineScheduler& source) {
		m_Implementation->AssignState(*source.m_Implementation);
	}
	void EmoteTimelineScheduler::SetBlendRatio(std::string_view name, float ratio, float duration, float easing, bool stop_when_finished) {
		m_Implementation->SetBlend(name, ratio, duration, easing, stop_when_finished);
	}
	void EmoteTimelineScheduler::FadeIn(std::string_view name, float duration, float easing) {
		if (!IsPlaying(name)) {
			Play(name, 3);
			SetBlendRatio(name, 0);
		}
		SetBlendRatio(name, 1, duration, easing);
	}
	void EmoteTimelineScheduler::FadeOut(std::string_view name, float duration, float easing) {
		SetBlendRatio(name, 0, duration, easing, true);
	}
	float EmoteTimelineScheduler::BlendRatio(std::string_view name) const {
		m_Implementation->RequireIdle();
		const auto found = m_Implementation->Entries.find(TimelineName(name));
		return found != m_Implementation->Entries.end() && found->second.Tracks ? found->second.BlendRatio : 0;
	}
	void EmoteTimelineScheduler::RequirePublicationOwner(const EmoteVariableControls& actual_controls, std::size_t actual_value_count) const {
		m_Implementation->RequireIdle();
		if (&m_Implementation->Controls != &actual_controls) {
			throw std::runtime_error("Emote additive publication requires its exact actual control owner");
		}
		std::size_t visits = 0;
		for (const auto* entry : m_Implementation->Active) {
			const auto count = (entry->Flags & 2) != 0 ? entry->Tracks->Definition().Tracks.size() : 1;
			if (count > kMaximumSchedulerWork - visits) {
				throw std::runtime_error("Emote additive publication exceeds bounded active-track work");
			}
			visits += count;
		}
		if (visits != 0 && actual_value_count > kMaximumSchedulerWork / visits) {
			throw std::runtime_error("Emote additive publication exceeds bounded value/track work");
		}
	}
	float EmoteTimelineScheduler::ApplyDifference(std::string_view name, float base) const {
		m_Implementation->RequireIdle();
		for (const auto* entry : m_Implementation->Active) {
			base = entry->Tracks->ApplyDifference(name, base, entry->BlendRatio);
		}
		return base;
	}
	bool EmoteTimelineScheduler::Animating() const {
		return m_Implementation->Animating();
	}

	bool EmoteTimelineScheduler::IsPlaying(std::string_view name) const {
		auto& state = *m_Implementation;
		state.RequireIdle();
		const auto normalized = TimelineName(name);
		if (normalized.empty()) {
			return !state.Active.empty();
		}
		const auto found = state.Entries.find(normalized);
		return found != state.Entries.end() && std::find(state.Active.begin(), state.Active.end(), &found->second) != state.Active.end();
	}
	std::optional<EmoteTimelinePlaybackState> EmoteTimelineScheduler::State(std::string_view name) const {
		auto& state = *m_Implementation;
		state.RequireIdle();
		const auto found = state.Entries.find(TimelineName(name));
		if (found == state.Entries.end()) {
			return std::nullopt;
		}
		const auto& entry = found->second;
		EmoteTimelinePlaybackState result;
		result.Position = entry.Tracks ? entry.Tracks->Position() : 0;
		result.Playing = std::find(state.Active.begin(), state.Active.end(), &entry) != state.Active.end();
		result.Looped = entry.Looped;
		result.Flags = entry.Flags;
		result.BlendRatio = entry.BlendRatio;
		return result;
	}
	std::size_t EmoteTimelineScheduler::ActiveCount() const {
		m_Implementation->RequireIdle();
		return m_Implementation->Active.size();
	}
	std::size_t EmoteTimelineScheduler::CatalogCount() const {
		m_Implementation->RequireIdle();
		return m_Implementation->Entries.size();
	}
	void EmoteTimelineScheduler::Update(float elapsed_ticks) {
		m_Implementation->Update(elapsed_ticks);
	}
} // namespace sgre::scene
