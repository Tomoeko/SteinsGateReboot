#include "sgre/audio/buffered_sound_cues.hpp"

#include "sgre/audio/voice_allocation.hpp"
#include "sgre/audio/voice_uniqueness.hpp"

#include <algorithm>
#include <array>
#include <exception>
#include <limits>
#include <list>
#include <map>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace sgre::audio {

	static constexpr std::size_t kBuffered32Slots = 32;
	static constexpr std::size_t kBuffered8Slots = 8;
	static constexpr std::size_t kBufferedCueCapacity = kBuffered32Slots + kBuffered8Slots;
	static constexpr std::size_t kMaxVolumeGroups = 4096;
	static constexpr std::size_t kMaxGroupNameBytes = 65536;
	static constexpr std::size_t kMaxOwnedGroupBytes = 4 * 1024 * 1024;
	static constexpr std::uint32_t kNativeCueIdMask = 0x0fffffff;

	struct BufferedCueChannel {
		SoundVoiceAddress Source;
		std::optional<SoundMix> Mix;
		SoundMixParameters Parameters;
		bool DisableRateConversion = false;
		bool Streaming = false;
		bool PendingPreparation = false;
		std::string Error;
		std::vector<float> MixWorkspace;
	};

	struct BufferedCue {
		BufferedSoundCueId Id;
		std::unique_ptr<SoundArchiveCueResource> Resource;
		SoundVoiceType Type = SoundVoiceType::Buffered32;
		float Priority = 0;
		float VoiceVolume = 1.0f;
		std::string Group;
		std::string Uniqueness;
		std::vector<std::uint8_t> Slots;
		std::vector<BufferedCueChannel> Channels;
		std::uint32_t PauseReasons = 0;
		BufferedSoundCuePreparationStatus Preparation = BufferedSoundCuePreparationStatus::FailedSources;
	};

	class BufferedSoundCues::Implementation {
	public:
		Implementation(SoundConfigurationLoads& actual_loads, BufferedSoundSourceLimits source_limits, BufferedSoundCueLimits cue_limits)
		    : Loads(actual_loads), Sources(source_limits), Limits(cue_limits) {
			if (cue_limits.MaxConsumeFrames == 0 || cue_limits.MaxConsumeFrames > 8192) {
				throw std::runtime_error("buffered cue consumption requires a bounded positive frame workspace");
			}

			Free32.fill(true);
			Free8.fill(true);
		}

		SoundConfigurationLoads& Loads;
		BufferedSoundSources Sources;
		BufferedSoundCueLimits Limits;
		const std::thread::id OwnerThread = std::this_thread::get_id();
		std::array<bool, kBuffered32Slots> Free32;
		std::array<bool, kBuffered8Slots> Free8;
		std::list<std::unique_ptr<BufferedCue>> Active;
		std::list<std::unique_ptr<BufferedCue>> Retiring;
		std::map<std::uint32_t, BufferedCue*> Ids;
		std::map<std::string, float, std::less<>> GroupVolumes;
		std::size_t OwnedGroupBytes = 0;
		std::uint32_t LastId = 0;
		std::optional<UserAudioSettings> UserSettings;
		bool AllPaused = false;
		bool Closing = false;
		bool Closed = false;

		void RequireOwner() const {
			if (Closed || std::this_thread::get_id() != OwnerThread) {
				throw std::runtime_error("buffered cues require their live serialized source/resource owner");
			}
		}

		static void RequireGroupName(std::string_view exact_group) {
			if (exact_group.size() > kMaxGroupNameBytes || exact_group.find('\0') != std::string_view::npos) {
				throw std::runtime_error("sound volume group exceeds its exact bounded string domain");
			}
		}

		float GroupVolume(std::string_view exact_group) const {
			RequireGroupName(exact_group);
			const auto found = GroupVolumes.find(exact_group);

			return found == GroupVolumes.end() ? 1.0f : found->second;
		}

		std::span<bool> FreeSlots(SoundVoiceType type) {
			if (type == SoundVoiceType::Buffered32) {
				return Free32;
			}
			if (type == SoundVoiceType::Buffered8) {
				return Free8;
			}

			throw std::runtime_error("buffered cue requires the actual device0/1 pool");
		}

		bool CallbackActive(const BufferedCue& cue) const {
			return std::any_of(cue.Slots.begin(), cue.Slots.end(), [&](const auto slot) {
				return Sources.Inspect({cue.Type, slot}).Samples.CallbackActive;
			});
		}

		static BufferedSoundCuePreparationStatus PreparationStatus(const BufferedCue& cue) {
			std::size_t prepared = 0;
			for (const auto& channel : cue.Channels) {
				if (channel.PendingPreparation) {
					return BufferedSoundCuePreparationStatus::PendingSources;
				}
				if (channel.Mix && channel.Error.empty()) {
					++prepared;
				}
			}

			return prepared == cue.Channels.size() ? BufferedSoundCuePreparationStatus::Prepared : prepared == 0 ? BufferedSoundCuePreparationStatus::FailedSources
			                                                                                                     : BufferedSoundCuePreparationStatus::PartiallyPrepared;
		}

		void SourceFailed(BufferedCueChannel& channel, const std::runtime_error& error) {
			// A worker may still borrow encoded archive data. Stop and retain the real payload until acknowledgement.
			Sources.RequestStop(channel.Source);
			channel.PendingPreparation = false;
			channel.Mix.reset();
			channel.Error = error.what();
		}

		void CompletePendingSources() {
			for (const auto& cue : Active) {
				for (auto& channel : cue->Channels) {
					if (!channel.Streaming || !channel.Error.empty()) {
						continue;
					}
					try {
						Sources.RethrowWmaStreamFailure(channel.Source);
						if (channel.PendingPreparation) {
							const auto completion = Sources.CompleteWmaStreamPreparation(channel.Source);
							if (completion == BufferedSoundStreamPreparation::Stopped) {
								throw std::runtime_error("incremental cue source stopped before its actual preparation completed");
							}
							channel.PendingPreparation = completion == BufferedSoundStreamPreparation::Prefilling;
						}
					} catch (const std::runtime_error& error) {
						SourceFailed(channel, error);
					}
				}
				cue->Preparation = PreparationStatus(*cue);
			}
		}

		BufferedCue& Find(BufferedSoundCueId id) const {
			const auto found = Ids.find(id.Value);
			if (found == Ids.end()) {
				throw std::runtime_error("buffered cue has no actual active ID lookup");
			}

			return *found->second;
		}

		static BufferedCueChannel& Channel(BufferedCue& cue, std::size_t index) {
			if (index >= cue.Channels.size()) {
				throw std::runtime_error("buffered cue channel exceeds its actual definition/source extent");
			}

			return cue.Channels[index];
		}

		void Stop(std::list<std::unique_ptr<BufferedCue>>::iterator position) {
			auto& cue = **position;
			for (const auto& channel : cue.Channels) {
				if (cue.PauseReasons != 0) {
					Sources.RequestResume(channel.Source);
				}
				Sources.RequestStop(channel.Source);
			}
			// Native383b80 erases ID lookup before moving the same resource/channel payload into retirement.
			Ids.erase(cue.Id.Value);
			Retiring.splice(Retiring.end(), Active, position);
		}

		bool Stop(BufferedSoundCueId id) {
			const auto found = Ids.find(id.Value);
			if (found == Ids.end()) {
				return false;
			}

			const auto position = std::find_if(Active.begin(), Active.end(), [&](const auto& cue) {
				return cue.get() == found->second;
			});
			Stop(position);

			return true;
		}

		void SweepFinished() {
			for (auto position = Active.begin(); position != Active.end();) {
				const auto current = position++;
				if ((*current)->Resource->IsUnloading() || !CallbackActive(**current)) {
					Stop(current);
				}
			}
		}

		void RetireReady() {
			for (auto position = Retiring.begin(); position != Retiring.end();) {
				auto& cue = **position;
				if (CallbackActive(cue)) {
					++position;
					continue;
				}

				bool worker_pending = false;
				for (const auto slot : cue.Slots) {
					const auto info = Sources.Inspect({cue.Type, slot});
					worker_pending = worker_pending || (info.Streaming && !info.Streaming->WorkerQuiescent);
				}
				if (worker_pending) {
					++position;
					continue;
				}
				auto free_slots = FreeSlots(cue.Type);
				for (const auto slot : cue.Slots) {
					// Retain idle converter nodes/tail, but remove every encoded span before the actual archive borrow dies.
					Sources.ReleaseWmaStreamInputBorrow({cue.Type, slot});
				}
				for (const auto slot : cue.Slots) {
					free_slots[slot] = true;
				}
				// Native380970 returns slots and releases cue/archive borrows, retaining idle source/callback storage.
				position = Retiring.erase(position);
			}
		}

		void CommitAndRetire() {
			Sources.PollWmaStreams();
			if (!Closing) {
				CompletePendingSources();
			}
			SweepFinished();
			Sources.Commit();
			Sources.PollWmaStreams();
			RetireReady();
		}

		struct PreparationGuard {
			Implementation& Owner;
			std::list<std::unique_ptr<BufferedCue>>& Unpublished;
			bool Published = false;

			~PreparationGuard() noexcept {
				if (Published || Unpublished.empty()) {
					return;
				}
				try {
					auto& cue = *Unpublished.front();
					for (const auto slot : cue.Slots) {
						Owner.Sources.RequestStop({cue.Type, slot});
					}
					Owner.Sources.Commit();
					// Splice the already allocated node: an unpublished async source must not lose its archive on unwind.
					Owner.Retiring.splice(Owner.Retiring.end(), Unpublished);
					Owner.Sources.PollWmaStreams();
					Owner.RetireReady();
				} catch (...) {
					std::terminate();
				}
			}
		};

		BufferedSoundCuePreparation Prepare(const BufferedSoundCueRequest& request, const SoundMixParameters& actual_parameters, std::uint8_t destination_channels) {
			RequireOwner();
			if (Closing) {
				throw std::runtime_error("buffered cue preparation requires an owner without a pending close request");
			}
			if (destination_channels == 0 || destination_channels > 8) {
				throw std::runtime_error("buffered cue requires actual bounded output channel geometry");
			}

			Sources.PollWmaStreams();
			SweepFinished();
			RetireReady();
			BufferedSoundCuePreparation result;
			auto resource = Loads.BorrowCueResource(request.ArchiveName, request.CueName);
			if (!resource) {
				Sources.Commit();

				return result;
			}

			const auto& definition = resource->Definition();
			if (definition.Device != 0 && definition.Device != 1) {
				throw std::runtime_error("buffered cue definition requires actual device0/1 sources; continuous type2 remains open");
			}
			const auto type = definition.Device == 0 ? SoundVoiceType::Buffered32 : SoundVoiceType::Buffered8;
			const auto priority = request.Priority == 0 ? definition.Priority : request.Priority;
			const auto group = request.Group.empty() ? std::string_view(definition.Group) : request.Group;
			const auto unique_key = request.Uniqueness.empty() ? std::string_view(definition.Uniqueness) : request.Uniqueness;
			RequireGroupName(group);
			RequireGroupName(unique_key);
			std::array<SoundUniqueCandidate, kBufferedCueCapacity> unique_candidates;
			std::size_t candidate_count = 0;
			for (const auto& cue : Active) {
				auto& candidate = unique_candidates[candidate_count++];
				candidate.CueId = cue->Id.Value;
				candidate.Priority = cue->Priority;
				candidate.Archive = cue->Resource->ArchiveName();
				candidate.Key = cue->Uniqueness;
			}
			const auto uniqueness = ResolveSoundUniqueness(resource->ArchiveName(), unique_key, priority, std::span(unique_candidates).first(candidate_count), request.LimitUniquenessToArchive);
			for (std::size_t index = 0; index < uniqueness.EvictionCount; ++index) {
				static_cast<void>(Stop(BufferedSoundCueId{uniqueness.EvictedCues[index]}));
			}
			if (!uniqueness.Accepted) {
				Sources.Commit();
				result.Status = BufferedSoundCuePreparationStatus::RejectedUniqueness;

				return result;
			}

			std::array<SoundSlotCandidate, kBufferedCueCapacity> slot_candidates;
			candidate_count = 0;
			for (const auto& cue : Active) {
				auto& candidate = slot_candidates[candidate_count++];
				candidate.CueId = cue->Id.Value;
				candidate.Type = cue->Type;
				candidate.Priority = cue->Priority;
				candidate.Slots = cue->Slots;
			}
			const auto slots = SelectSoundVoiceSlots(type, definition.Channels.size(), priority, FreeSlots(type), std::span(slot_candidates).first(candidate_count));
			for (std::size_t index = 0; index < slots.EvictionCount; ++index) {
				static_cast<void>(Stop(BufferedSoundCueId{slots.EvictedCues[index]}));
			}
			if (!slots.Accepted) {
				Sources.Commit();
				result.Status = BufferedSoundCuePreparationStatus::RejectedSlots;

				return result;
			}

			Sources.Commit();
			RetireReady();
			// Allocate unpublished payload/list/ID storage before creating sources, preserving visible native publication order.
			std::list<std::unique_ptr<BufferedCue>> unpublished;
			auto cue = std::make_unique<BufferedCue>();
			cue->Resource = std::move(resource);
			cue->Type = type;
			cue->Priority = priority;
			cue->Group = group;
			cue->Uniqueness = unique_key;
			cue->VoiceVolume = actual_parameters.VoiceVolume;
			cue->PauseReasons = (request.StartPaused ? static_cast<std::uint32_t>(SoundPauseReason::Voice) : 0U) | (AllPaused ? static_cast<std::uint32_t>(SoundPauseReason::All) : 0U);
			cue->Channels.resize(definition.Channels.size());
			cue->Slots.reserve(definition.Channels.size());
			unpublished.push_back(std::move(cue));
			auto& prepared = *unpublished.front();
			std::map<std::uint32_t, BufferedCue*> unpublished_id;
			unpublished_id.emplace(0, &prepared);
			auto id_node = unpublished_id.extract(0);
			PreparationGuard guard{*this, unpublished};
			auto free_slots = FreeSlots(type);
			for (std::size_t index = 0; index < prepared.Channels.size(); ++index) {
				const auto slot = slots.Slots[index];
				if (!free_slots[slot]) {
					throw std::runtime_error("buffered cue selected slot remains owned by an actual unfinished source/cue");
				}
				prepared.Slots.push_back(slot);
				free_slots[slot] = false;
				prepared.Channels[index].Source.Type = type;
				prepared.Channels[index].Source.Slot = slot;
			}

			for (std::size_t index = 0; index < prepared.Channels.size(); ++index) {
				auto& channel = prepared.Channels[index];
				try {
					const auto data = ReadBufferedSoundSourceData(prepared.Resource->File(), definition, index, request.PlayCount);
					channel.MixWorkspace.resize(Limits.MaxConsumeFrames * data.Description.Format.Channels);
					if (request.IncrementalWma && data.Description.Format.Encoding == resource::WaveEncoding::WmaStandard) {
						if (!request.VerifiedWmaEncodeOptions) {
							throw std::runtime_error("incremental cue requires explicitly verified WMA encode options");
						}
						Sources.PrepareWmaStreaming(channel.Source, data, *request.VerifiedWmaEncodeOptions);
						channel.Streaming = true;
						channel.PendingPreparation = true;
					} else {
						Sources.Prepare(channel.Source, data, request.VerifiedWmaEncodeOptions);
					}
					if (prepared.PauseReasons != 0) {
						Sources.RequestPause(channel.Source);
					}
					auto parameters = actual_parameters;
					parameters.ResourceVolume = definition.Volume;
					parameters.GroupVolume = GroupVolume(prepared.Group);
					parameters.UserVolume = UserSettings ? UserAudioGroupGain(*UserSettings, prepared.Group) : 1.0f;
					parameters.ResourcePitchOctaves = definition.PitchOctaves;
					parameters.ResourcePan = definition.Channels[index].Pan;
					channel.Parameters = parameters;
					channel.DisableRateConversion = data.Submission.DisableRateConversion;
					channel.Mix = EvaluateSoundMix(parameters, static_cast<std::uint8_t>(Sources.Inspect(channel.Source).Samples.Channels), destination_channels, data.Submission.DisableRateConversion);
					Sources.ApplyOutputMix(channel.Source, *channel.Mix);
					if (channel.Streaming) {
						const auto completion = Sources.CompleteWmaStreamPreparation(channel.Source);
						if (completion == BufferedSoundStreamPreparation::Stopped) {
							throw std::runtime_error("incremental cue source stopped before its actual preparation completed");
						}
						channel.PendingPreparation = completion == BufferedSoundStreamPreparation::Prefilling;
					}
				} catch (const std::runtime_error& error) {
					if (channel.Streaming) {
						SourceFailed(channel, error);
					} else {
						Sources.Retire(channel.Source);
						channel.Mix.reset();
						channel.Error = error.what();
					}
				}
			}

			Sources.Commit();
			do {
				LastId = (LastId + 1) & kNativeCueIdMask;
			} while (Ids.contains(LastId));
			prepared.Id.Value = LastId;
			id_node.key() = LastId;
			Active.splice(Active.end(), unpublished);
			Ids.insert(std::move(id_node));
			guard.Published = true;
			result.Cue = prepared.Id;
			prepared.Preparation = PreparationStatus(prepared);
			result.Status = prepared.Preparation;

			return result;
		}
	};

	BufferedSoundCues::BufferedSoundCues(SoundConfigurationLoads& actual_loads, BufferedSoundSourceLimits source_limits, BufferedSoundCueLimits cue_limits)
	    : m_Implementation(std::make_unique<Implementation>(actual_loads, source_limits, cue_limits)) {
	}

	BufferedSoundCues::~BufferedSoundCues() noexcept {
		try {
			Close();
		} catch (...) {
			std::terminate();
		}
	}

	void BufferedSoundCues::ConnectOutput(BufferedSoundOutput& actual_output) {
		auto& owner = *m_Implementation;
		owner.RequireOwner();
		if (!owner.Active.empty() || !owner.Retiring.empty()) {
			throw std::runtime_error("buffered cue output requires its fresh actual source/controller owner");
		}
		owner.Sources.ConnectOutput(actual_output);
	}

	bool BufferedSoundCues::SetVoiceVolume(BufferedSoundCueId cue, float linear_volume) {
		auto& owner = *m_Implementation;
		owner.RequireOwner();
		const auto found = owner.Ids.find(cue.Value);
		if (cue.Value == std::numeric_limits<std::uint32_t>::max() || found == owner.Ids.end()) {
			return false;
		}

		auto& active = *found->second;
		if (active.VoiceVolume == linear_volume) {
			return true;
		}
		active.VoiceVolume = linear_volume;
		for (auto& channel : active.Channels) {
			if (!channel.Mix) {
				continue;
			}

			channel.Parameters.VoiceVolume = linear_volume;
			channel.Mix = EvaluateSoundMix(channel.Parameters, channel.Mix->SourceChannels, channel.Mix->DestinationChannels, channel.DisableRateConversion);
			owner.Sources.ApplyOutputMix(channel.Source, *channel.Mix);
		}
		owner.Sources.Commit();

		return true;
	}

	float BufferedSoundCues::VoiceVolume(BufferedSoundCueId cue) const {
		const auto& owner = *m_Implementation;
		owner.RequireOwner();
		const auto found = owner.Ids.find(cue.Value);

		return found == owner.Ids.end() ? 1.0f : found->second->VoiceVolume;
	}

	void BufferedSoundCues::SetUserAudioSettings(const UserAudioSettings& settings) {
		auto& owner = *m_Implementation;
		owner.RequireOwner();
		ValidateUserAudioSettings(settings);
		if (owner.UserSettings == settings) {
			return;
		}
		owner.UserSettings = settings;
		for (const auto& cue : owner.Active) {
			for (auto& channel : cue->Channels) {
				if (!channel.Mix) {
					continue;
				}

				channel.Parameters.UserVolume = UserAudioGroupGain(settings, cue->Group);
				channel.Mix = EvaluateSoundMix(channel.Parameters, channel.Mix->SourceChannels, channel.Mix->DestinationChannels, channel.DisableRateConversion);
				owner.Sources.ApplyOutputMix(channel.Source, *channel.Mix);
			}
		}
		owner.Sources.Commit();
	}

	void BufferedSoundCues::SetMasterVolume(float linear_volume) {
		auto& owner = *m_Implementation;
		owner.RequireOwner();
		for (const auto& cue : owner.Active) {
			for (auto& channel : cue->Channels) {
				if (!channel.Mix) {
					continue;
				}

				channel.Parameters.MasterVolume = linear_volume;
				channel.Mix = EvaluateSoundMix(channel.Parameters, channel.Mix->SourceChannels, channel.Mix->DestinationChannels, channel.DisableRateConversion);
				owner.Sources.ApplyOutputMix(channel.Source, *channel.Mix);
			}
		}
		owner.Sources.Commit();
	}

	void BufferedSoundCues::SetGroupVolume(std::string_view exact_group, float linear_volume) {
		auto& owner = *m_Implementation;
		owner.RequireOwner();
		Implementation::RequireGroupName(exact_group);
		const auto found = owner.GroupVolumes.find(exact_group);
		if (linear_volume == 1.0f) {
			if (found == owner.GroupVolumes.end()) {
				return;
			}

			owner.OwnedGroupBytes -= found->first.size();
			owner.GroupVolumes.erase(found);
		} else if (found != owner.GroupVolumes.end()) {
			if (found->second == linear_volume) {
				return;
			}

			found->second = linear_volume;
		} else {
			if (owner.GroupVolumes.size() >= kMaxVolumeGroups || exact_group.size() > kMaxOwnedGroupBytes - owner.OwnedGroupBytes) {
				throw std::runtime_error("sound volume groups exceed their bounded owned key storage");
			}

			owner.GroupVolumes.emplace(exact_group, linear_volume);
			owner.OwnedGroupBytes += exact_group.size();
		}

		// Only active payloads participate; retiring cues keep their already submitted source state.
		for (const auto& cue : owner.Active) {
			if (cue->Group != exact_group) {
				continue;
			}

			for (auto& channel : cue->Channels) {
				if (!channel.Mix) {
					continue;
				}

				channel.Parameters.GroupVolume = linear_volume;
				channel.Mix = EvaluateSoundMix(channel.Parameters, channel.Mix->SourceChannels, channel.Mix->DestinationChannels, channel.DisableRateConversion);
				owner.Sources.ApplyOutputMix(channel.Source, *channel.Mix);
			}
		}
		owner.Sources.Commit();
	}

	float BufferedSoundCues::GroupVolume(std::string_view exact_group) const {
		m_Implementation->RequireOwner();

		return m_Implementation->GroupVolume(exact_group);
	}

	BufferedSoundCuePreparation BufferedSoundCues::Prepare(const BufferedSoundCueRequest& request, const SoundMixParameters& actual_mix_parameters, std::uint8_t actual_destination_channels) {
		return m_Implementation->Prepare(request, actual_mix_parameters, actual_destination_channels);
	}

	bool BufferedSoundCues::Stop(BufferedSoundCueId cue) {
		m_Implementation->RequireOwner();

		return m_Implementation->Stop(cue);
	}

	bool BufferedSoundCues::Pause(BufferedSoundCueId id) {
		auto& owner = *m_Implementation;
		owner.RequireOwner();
		const auto found = owner.Ids.find(id.Value);
		const auto reason = static_cast<std::uint32_t>(SoundPauseReason::Voice);
		if (found == owner.Ids.end() || (found->second->PauseReasons & reason) != 0) {
			return false;
		}

		auto& cue = *found->second;
		const auto previous = cue.PauseReasons;
		cue.PauseReasons |= reason;
		if (previous == 0) {
			for (const auto& channel : cue.Channels) {
				owner.Sources.RequestPause(channel.Source);
			}
		}

		return true;
	}

	bool BufferedSoundCues::Resume(BufferedSoundCueId id) {
		auto& owner = *m_Implementation;
		owner.RequireOwner();
		const auto found = owner.Ids.find(id.Value);
		const auto reason = static_cast<std::uint32_t>(SoundPauseReason::Voice);
		if (found == owner.Ids.end() || (found->second->PauseReasons & reason) == 0) {
			return false;
		}

		auto& cue = *found->second;
		cue.PauseReasons &= ~reason;
		if (cue.PauseReasons == 0) {
			for (const auto& channel : cue.Channels) {
				owner.Sources.RequestResume(channel.Source);
			}
		}

		return true;
	}

	bool BufferedSoundCues::PauseAll() {
		auto& owner = *m_Implementation;
		owner.RequireOwner();
		if (owner.AllPaused) {
			return false;
		}

		owner.AllPaused = true;
		const auto reason = static_cast<std::uint32_t>(SoundPauseReason::All);
		for (auto& cue : owner.Active) {
			const auto previous = cue->PauseReasons;
			cue->PauseReasons |= reason;
			if (previous == 0) {
				for (const auto& channel : cue->Channels) {
					owner.Sources.RequestPause(channel.Source);
				}
			}
		}
		owner.Sources.Commit();

		return true;
	}

	bool BufferedSoundCues::ResumeAll() {
		auto& owner = *m_Implementation;
		owner.RequireOwner();
		if (!owner.AllPaused) {
			return false;
		}

		owner.AllPaused = false;
		const auto reason = static_cast<std::uint32_t>(SoundPauseReason::All);
		for (auto& cue : owner.Active) {
			cue->PauseReasons &= ~reason;
			if (cue->PauseReasons == 0) {
				for (const auto& channel : cue->Channels) {
					owner.Sources.RequestResume(channel.Source);
				}
			}
		}
		owner.Sources.Commit();

		return true;
	}

	bool BufferedSoundCues::GetAllPaused() const {
		m_Implementation->RequireOwner();

		return m_Implementation->AllPaused;
	}

	std::optional<BufferedSoundCueId> BufferedSoundCues::FindNamedCue(std::string_view exact_cue_name, std::optional<std::string_view> exact_archive_name) const {
		const auto& owner = *m_Implementation;
		owner.RequireOwner();
		for (const auto& cue : owner.Active) {
			if (cue->Resource->CueName() == exact_cue_name && (!exact_archive_name || cue->Resource->ArchiveName() == *exact_archive_name)) {
				return cue->Id;
			}
		}

		return std::nullopt;
	}

	bool BufferedSoundCues::ApplyControl(BufferedSoundCueId id, BufferedSoundCueControl control) {
		auto& owner = *m_Implementation;
		owner.RequireOwner();
		if (control != BufferedSoundCueControl::Pause && control != BufferedSoundCueControl::Resume && control != BufferedSoundCueControl::Stop) {
			throw std::runtime_error("buffered cue control is outside the recovered voice operations");
		}
		const auto found = owner.Ids.find(id.Value);
		if (found == owner.Ids.end()) {
			return false;
		}

		auto& cue = *found->second;
		const auto reason = static_cast<std::uint32_t>(SoundPauseReason::Voice);
		const auto previous = cue.PauseReasons;
		switch (control) {
		case BufferedSoundCueControl::Pause:
			cue.PauseReasons |= reason;
			if (previous == 0) {
				for (const auto& channel : cue.Channels) {
					owner.Sources.RequestPause(channel.Source);
				}
			}
			break;
		case BufferedSoundCueControl::Resume:
			cue.PauseReasons &= ~reason;
			if (cue.PauseReasons == 0) {
				for (const auto& channel : cue.Channels) {
					owner.Sources.RequestResume(channel.Source);
				}
			}
			break;
		case BufferedSoundCueControl::Stop:
			static_cast<void>(owner.Stop(id));
			break;
		}
		owner.Sources.Commit();
		if (control == BufferedSoundCueControl::Stop) {
			// Native383c80 calls380970; unrelated active cue completion waits for its real postloop.
			owner.RetireReady();
		}

		return true;
	}

	bool BufferedSoundCues::IsPlaying(BufferedSoundCueId id) const {
		const auto& owner = *m_Implementation;
		owner.RequireOwner();
		const auto found = owner.Ids.find(id.Value);

		return found != owner.Ids.end() && owner.CallbackActive(*found->second);
	}

	bool BufferedSoundCues::UnloadArchive(std::string_view archive_name) {
		auto& owner = *m_Implementation;
		owner.RequireOwner();
		for (auto position = owner.Active.begin(); position != owner.Active.end();) {
			const auto current = position++;
			if ((*current)->Resource->ArchiveName() == archive_name && owner.Loads.SelectsCueArchive(*(*current)->Resource)) {
				owner.Stop(current);
			}
		}

		return owner.Loads.UnloadArchive(archive_name);
	}

	bool BufferedSoundCues::UnloadConfiguration(std::string_view filename) {
		auto& owner = *m_Implementation;
		owner.RequireOwner();
		for (auto position = owner.Active.begin(); position != owner.Active.end();) {
			const auto current = position++;
			if ((*current)->Resource->ConfigurationFilename() == filename) {
				owner.Stop(current);
			}
		}

		return owner.Loads.Unload(filename);
	}

	void BufferedSoundCues::CommitAndRetire() {
		m_Implementation->RequireOwner();
		m_Implementation->CommitAndRetire();
	}

	BufferedSoundCueInfo BufferedSoundCues::Inspect(BufferedSoundCueId id) const {
		const auto& owner = *m_Implementation;
		owner.RequireOwner();
		const auto& cue = owner.Find(id);
		BufferedSoundCueInfo info;
		info.ConfigurationFilename = cue.Resource->ConfigurationFilename();
		info.ArchiveName = cue.Resource->ArchiveName();
		info.CueName = cue.Resource->CueName();
		info.Channels = cue.Channels.size();
		info.CallbackActive = owner.CallbackActive(cue);
		info.Paused = cue.PauseReasons != 0;
		info.PauseReasons = cue.PauseReasons;
		info.Preparation = cue.Preparation;

		return info;
	}

	BufferedSoundCueChannelInfo BufferedSoundCues::InspectChannel(BufferedSoundCueId id, std::size_t index) const {
		const auto& owner = *m_Implementation;
		owner.RequireOwner();
		const auto& channel = Implementation::Channel(owner.Find(id), index);
		BufferedSoundCueChannelInfo info;
		info.Source = channel.Source;
		info.State = owner.Sources.Inspect(channel.Source);
		info.Mix = channel.Mix;
		info.Error = channel.Error;
		info.PendingPreparation = channel.PendingPreparation;

		return info;
	}

	BufferedSoundSourceInfo BufferedSoundCues::InspectSource(SoundVoiceAddress address) const {
		m_Implementation->RequireOwner();

		return m_Implementation->Sources.Inspect(address);
	}

	WmaStreamReadResult BufferedSoundCues::ConsumeStreamingChannelFrames(BufferedSoundCueId id, std::size_t index, std::span<float> destination) {
		auto& owner = *m_Implementation;
		owner.RequireOwner();
		const auto& channel = Implementation::Channel(owner.Find(id), index);
		if (!channel.Streaming || !channel.Error.empty()) {
			throw std::runtime_error("typed cue consumption requires its actual nonfailed incremental source");
		}

		return owner.Sources.ConsumeWmaStreamFrames(channel.Source, destination);
	}

	std::size_t BufferedSoundCues::ConsumeChannelFrames(BufferedSoundCueId id, std::size_t index, std::span<float> destination) {
		auto& owner = *m_Implementation;
		owner.RequireOwner();
		const auto& channel = Implementation::Channel(owner.Find(id), index);
		if (!channel.Error.empty()) {
			throw std::runtime_error("buffered cue channel has an explicit source preparation failure: " + channel.Error);
		}

		return owner.Sources.ConsumeFrames(channel.Source, destination);
	}

	std::size_t BufferedSoundCues::ConsumeMixedChannelFrames(BufferedSoundCueId id, std::size_t index, std::span<float> output) {
		auto& owner = *m_Implementation;
		owner.RequireOwner();
		auto& channel = Implementation::Channel(owner.Find(id), index);
		if (!channel.Mix) {
			throw std::runtime_error("buffered cue mixed consumption requires its actual prepared source/matrix: " + channel.Error);
		}

		const auto& mix = *channel.Mix;
		if ((mix.FrequencyRatio && *mix.FrequencyRatio != 1) || output.size() % mix.DestinationChannels != 0 || output.size() / mix.DestinationChannels > owner.Limits.MaxConsumeFrames) {
			throw std::runtime_error("buffered cue mixed consumption requires bounded complete source-rate frames and recovered unit pitch; resampling remains open");
		}

		const auto requested = output.size() / mix.DestinationChannels;
		const auto consumed = owner.Sources.ConsumeFrames(channel.Source, std::span(channel.MixWorkspace).first(requested * mix.SourceChannels));
		AccumulateSoundMix(mix, std::span(channel.MixWorkspace).first(consumed * mix.SourceChannels), output.first(consumed * mix.DestinationChannels));

		return consumed;
	}

	std::size_t BufferedSoundCues::ActiveCount() const {
		m_Implementation->RequireOwner();

		return m_Implementation->Active.size();
	}

	std::size_t BufferedSoundCues::RetiringCount() const {
		m_Implementation->RequireOwner();

		return m_Implementation->Retiring.size();
	}

	void BufferedSoundCues::RequestClose() {
		auto& owner = *m_Implementation;
		if (owner.Closed) {
			return;
		}
		owner.RequireOwner();
		owner.Closing = true;
		while (!owner.Active.empty()) {
			owner.Stop(owner.Active.begin());
		}
		owner.CommitAndRetire();
	}

	bool BufferedSoundCues::CanClose() const {
		const auto& owner = *m_Implementation;
		if (owner.Closed) {
			return true;
		}
		owner.RequireOwner();
		if (!owner.Active.empty() || !owner.Retiring.empty()) {
			return false;
		}
		for (std::size_t index = 0; index < kBufferedCueCapacity; ++index) {
			const SoundVoiceAddress address{index < kBuffered32Slots ? SoundVoiceType::Buffered32 : SoundVoiceType::Buffered8, static_cast<std::uint8_t>(index < kBuffered32Slots ? index : index - kBuffered32Slots)};
			const auto info = owner.Sources.Inspect(address);
			if (info.Samples.CallbackActive || (info.Streaming && !info.Streaming->WorkerQuiescent)) {
				return false;
			}
		}

		return true;
	}

	void BufferedSoundCues::Close() {
		auto& owner = *m_Implementation;
		if (owner.Closed) {
			return;
		}
		RequestClose();
		if (!CanClose()) {
			throw std::runtime_error("buffered cue close requires actual queued source/callback retirement and decoder acknowledgement");
		}
		owner.Sources.Close();
		owner.Closed = true;
	}

} // namespace sgre::audio
