#include "sgre/media/movie_audio.hpp"

#include "sgre/media/movie_pcm_delivery.hpp"
#include "sgre/platform/macos_pcm_output.hpp"

#include "offline_sound_boundary.hpp"

#include "sgre/audio/sound_resource_control.hpp"
#include "sgre/media/movie_gain.hpp"

#import <AVFAudio/AVFAudio.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <exception>
#include <limits>
#include <optional>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace sgre::media {

	static constexpr std::uint32_t kMovieMasterRate = 48000;
	static constexpr std::uint32_t kMovieMasterChannels = 2;
	static constexpr std::uint32_t kMovieRenderMaximum = 2048;
	static constexpr std::size_t kMovieVoiceCapacity = 16;

	struct MovieVoiceState;
	struct MoviePhaseBlockEvent {
		MovieVoiceState* Voice = nullptr;
		std::uint32_t MasterOffset = 0;
		std::uint32_t CompletedBlockFrames = 0;
	};

	struct MovieVoiceState {
		MoviePcmDescription Description;
		std::array<std::optional<MovieAudioBlock>, 3> Pending;
		std::array<std::optional<MovieAudioBlock>, 2> Submitted;
		std::size_t PendingRead = 0;
		std::size_t PendingWrite = 0;
		std::size_t PendingCount = 0;
		std::size_t SubmittedRead = 0;
		std::size_t SubmittedWrite = 0;
		std::size_t SubmittedCount = 0;
		std::size_t CurrentOffset = 0;
		std::uint32_t AssembledBlocks = 0;
		std::uint64_t CompletedFrames = 0;
		std::uint64_t DeliveredFrames = 0;
		std::optional<std::int64_t> DeliveryClock100ns;
		std::optional<std::int64_t> SampleEpoch100ns;
		std::optional<std::int64_t> BlockClock100ns;
		float Volume = 1;
		float Gain = 1;
		bool CurrentStarted = false;
		bool Started = false;
		bool Paused = false;
		bool InputEnded = false;
		bool Closed = false;
	};

	struct MovieSilentState {
		const std::thread::id OwnerThread = std::this_thread::get_id();
		std::function<std::int64_t()> ReadClock100ns;
		audio::SoundResourceControl* Sound = nullptr;
		std::array<std::shared_ptr<MovieVoiceState>, kMovieVoiceCapacity> Voices;
		std::exception_ptr CallbackFailure;
		std::atomic<bool> ForeignCallback = false;
		std::atomic<bool> Closed = false;
		bool Rendering = false;
		std::uint64_t RenderedFrames = 0;
		const MoviePcmDelivery* DeliveryOwner = nullptr;
		std::array<MoviePhaseBlockEvent, kMovieVoiceCapacity * 10> PhaseEvents;
		std::size_t PhaseEventCount = 0;
		std::uint32_t DeliveryRenderFrames = 0;
		std::uint32_t DeliveryCallbacks = 0;
		std::int64_t DeliverySampleOrigin = 0;
		bool DeliveryRendering = false;
		bool DeliveryStopped = false;
	};

	static void RequireMovieAudioOwner(const MovieSilentState& state) {
		if (state.Closed.load() || state.Rendering || std::this_thread::get_id() != state.OwnerThread) {
			throw std::runtime_error("movie silent audio requires its actual live idle host rendering owner");
		}
		if (state.CallbackFailure) {
			std::rethrow_exception(state.CallbackFailure);
		}
		if (state.ForeignCallback.load()) {
			throw std::runtime_error("movie silent audio system callback left its serialized manual rendering thread");
		}
	}

	static float ReadMovieSourceGain(const MovieSilentState& registry, float raw_volume) {
		if (registry.Sound == nullptr) {
			return raw_volume;
		}
		MovieSoundVolumeState actual;
		actual.MasterLinear = registry.Sound->MasterVolume();
		actual.LoudnessEnabled = registry.Sound->LoudnessVolumeEnabled();
		// Native Sound ctor37f380 initializes its independent master-bypass byte false.
		const auto gain = CalculateMovieSourceGain(raw_volume, actual);
		if (!std::isfinite(gain)) {
			throw std::runtime_error("movie converted nonfinite source gain requires its native source-voice error witness");
		}

		return gain;
	}

	static void SubmitMoviePending(MovieVoiceState& voice) {
		if (!voice.Started) {
			return;
		}
		while (voice.PendingCount != 0 && voice.SubmittedCount < voice.Submitted.size()) {
			voice.Submitted[voice.SubmittedWrite] = std::move(voice.Pending[voice.PendingRead]);
			voice.Pending[voice.PendingRead].reset();
			voice.PendingRead = (voice.PendingRead + 1) % voice.Pending.size();
			--voice.PendingCount;
			voice.SubmittedWrite = (voice.SubmittedWrite + 1) % voice.Submitted.size();
			++voice.SubmittedCount;
		}
	}

	static std::int64_t MovieCallbackClock(MovieSilentState& registry) {
		const auto clock = registry.ReadClock100ns();
		if (clock < 0) {
			throw std::runtime_error("movie callback requires its actual nonnegative monotonic host clock");
		}

		return clock;
	}

	static void RetireMovieVoice(MovieVoiceState& voice) noexcept {
		voice.Closed = true;
		for (auto& block : voice.Pending) {
			block.reset();
		}
		for (auto& block : voice.Submitted) {
			block.reset();
		}
		voice.PendingCount = 0;
		voice.SubmittedCount = 0;
	}

	static void RecordMovieBlockEvent(MovieSilentState& registry, MovieVoiceState& voice, std::size_t offset, std::size_t completed_frames) {
		if (!registry.DeliveryRendering || registry.PhaseEventCount == registry.PhaseEvents.size() || offset > registry.DeliveryRenderFrames || completed_frames > 65536) {
			throw std::runtime_error("Movie delivery lost its bounded actual render block-event extent");
		}
		auto& event = registry.PhaseEvents[registry.PhaseEventCount++];
		event.Voice = &voice;
		event.MasterOffset = static_cast<std::uint32_t>(offset);
		event.CompletedBlockFrames = static_cast<std::uint32_t>(completed_frames);
	}

	static void MixMovieVoice(MovieSilentState& registry, MovieVoiceState& voice, AVAudioFrameCount frames, AudioBufferList& output) {
		if (voice.Closed || !voice.Started) {
			return;
		}
		SubmitMoviePending(voice);
		std::size_t offset = 0;
		while (offset < frames && voice.SubmittedCount != 0) {
			auto& actual = voice.Submitted[voice.SubmittedRead];
			if (!actual) {
				throw std::runtime_error("movie submitted PCM queue lost its actual buffer owner");
			}
			if (!voice.CurrentStarted) {
				if (registry.DeliveryOwner != nullptr) {
					RecordMovieBlockEvent(registry, voice, offset, 0);
				} else {
					voice.BlockClock100ns = MovieCallbackClock(registry);
				}
				voice.CurrentStarted = true;
			}
			const auto block_frames = actual->Samples.size() / voice.Description.Channels;
			const auto count = std::min<std::size_t>(frames - offset, block_frames - voice.CurrentOffset);
			for (std::size_t channel = 0; channel < kMovieMasterChannels; ++channel) {
				auto* plane = static_cast<float*>(output.mBuffers[channel].mData);
				for (std::size_t frame = 0; frame < count; ++frame) {
					plane[offset + frame] += actual->Samples[(voice.CurrentOffset + frame) * voice.Description.Channels + channel] * voice.Gain;
				}
			}
			voice.CurrentOffset += count;
			offset += count;
			if (voice.CurrentOffset == block_frames) {
				voice.CompletedFrames += block_frames;
				if (registry.DeliveryOwner != nullptr) {
					RecordMovieBlockEvent(registry, voice, offset, block_frames);
				} else {
					voice.BlockClock100ns = MovieCallbackClock(registry);
				}
				actual.reset();
				voice.CurrentOffset = 0;
				voice.CurrentStarted = false;
				voice.SubmittedRead = (voice.SubmittedRead + 1) % voice.Submitted.size();
				--voice.SubmittedCount;
				SubmitMoviePending(voice);
			}
		}
	}

	static OSStatus RenderMovieSources(const std::shared_ptr<MovieSilentState>& state, BOOL* silence, const AudioTimeStamp* timestamp, AVAudioFrameCount frames, AudioBufferList* output) noexcept {
		if (!state || state->Closed.load()) {
			return kAudio_ParamError;
		}
		if (std::this_thread::get_id() != state->OwnerThread) {
			state->ForeignCallback.store(true);
			return kAudio_ParamError;
		}
		try {
			if (!state->Rendering || silence == nullptr || output == nullptr || output->mNumberBuffers != kMovieMasterChannels || frames > kMovieRenderMaximum) {
				throw std::runtime_error("movie source callback lacks its actual bounded manual rendering extent");
			}
			if (state->DeliveryOwner != nullptr) {
				// Exact measured same-rate manual graph domain. Different callback shapes are not silently remapped.
				if (!state->DeliveryRendering || state->DeliveryCallbacks != 0 || frames != state->DeliveryRenderFrames || timestamp == nullptr || (timestamp->mFlags & kAudioTimeStampSampleTimeValid) == 0 || !std::isfinite(timestamp->mSampleTime) || timestamp->mSampleTime != static_cast<double>(state->DeliverySampleOrigin)) {
					throw std::runtime_error("Movie delivery requires the actual contiguous same-rate manual source callback timeline");
				}
				++state->DeliveryCallbacks;
			}
			for (std::size_t channel = 0; channel < kMovieMasterChannels; ++channel) {
				if (output->mBuffers[channel].mNumberChannels != 1 || output->mBuffers[channel].mData == nullptr || output->mBuffers[channel].mDataByteSize < frames * sizeof(float)) {
					throw std::runtime_error("movie source callback output is not its actual planar Float32 stereo format");
				}
				auto* plane = static_cast<float*>(output->mBuffers[channel].mData);
				std::fill_n(plane, frames, 0);
			}
			for (const auto& voice : state->Voices) {
				if (voice) {
					MixMovieVoice(*state, *voice, frames, *output);
				}
			}
			*silence = NO;

			return noErr;
		} catch (...) {
			if (!state->CallbackFailure) {
				state->CallbackFailure = std::current_exception();
			}

			return kAudio_ParamError;
		}
	}

	class MovieAudioOutput::Implementation {
	public:
		std::weak_ptr<MovieSilentState> Registry;
		std::shared_ptr<MovieVoiceState> Voice;

		std::shared_ptr<MovieSilentState> RequireLive() const {
			const auto registry = Registry.lock();
			if (!registry || Voice->Closed) {
				throw std::runtime_error("movie output lease no longer has its actual silent registry/source owner");
			}
			RequireMovieAudioOwner(*registry);

			return registry;
		}

		void Close() noexcept {
			if (Voice->Closed) {
				return;
			}
			if (const auto registry = Registry.lock()) {
				if (registry->Rendering || std::this_thread::get_id() != registry->OwnerThread) {
					std::terminate();
				}
				for (auto& slot : registry->Voices) {
					if (slot == Voice) {
						slot.reset();
						break;
					}
				}
			}
			RetireMovieVoice(*Voice);
		}
	};

	class MovieSilentAudioRegistry::Implementation {
	public:
		Implementation(std::function<std::int64_t()> read_clock100ns, audio::SoundResourceControl* actual_sound)
		    : State(std::make_shared<MovieSilentState>()) {
			if (!read_clock100ns) {
				throw std::runtime_error("movie silent registry requires its actual monotonic callback clock");
			}
			State->ReadClock100ns = std::move(read_clock100ns);
			State->Sound = actual_sound;
			static_cast<void>(ReadMovieSourceGain(*State, 1));
			try {
				@autoreleasepool {
					Boundary.Invoke(audio::OfflineSoundSystemOperation::GraphCreation, [&] {
						Format = [[AVAudioFormat alloc] initWithCommonFormat:AVAudioPCMFormatFloat32 sampleRate:kMovieMasterRate channels:kMovieMasterChannels interleaved:NO];
						Engine = [[AVAudioEngine alloc] init];
						NSError* error = nil;
						// This call precedes every mixer/output-node access, exactly as the accepted silent mastering owner.
						if (Engine == nil || Format == nil || ![Engine enableManualRenderingMode:AVAudioEngineManualRenderingModeOffline format:Format maximumFrameCount:kMovieRenderMaximum error:&error]) {
							throw std::runtime_error("movie manual offline graph initialization failed");
						}
						const auto actual_format = Engine.manualRenderingFormat;
						if (!Engine.isInManualRenderingMode || actual_format == nil || actual_format.commonFormat != AVAudioPCMFormatFloat32 || actual_format.isInterleaved || actual_format.sampleRate != kMovieMasterRate || actual_format.channelCount != kMovieMasterChannels) {
							throw std::runtime_error("movie manual engine did not retain its actual requested silent PCM format");
						}
						Buffer = [[AVAudioPCMBuffer alloc] initWithPCMFormat:Format frameCapacity:kMovieRenderMaximum];
						const std::weak_ptr<MovieSilentState> weak = State;
						Source = [[AVAudioSourceNode alloc] initWithFormat:Format
						                                       renderBlock:^OSStatus(BOOL* silence, const AudioTimeStamp* timestamp, AVAudioFrameCount frames, AudioBufferList* output) {
							                                     return RenderMovieSources(weak.lock(), silence, timestamp, frames, output);
						                                       }];
						if (Buffer == nil || Source == nil) {
							throw std::runtime_error("movie silent source/output buffer allocation failed");
						}
						[Engine attachNode:Source];
						[Engine connect:Source to:Engine.mainMixerNode format:Format];
						[Engine connect:Engine.mainMixerNode to:Engine.outputNode format:Format];
						Engine.mainMixerNode.outputVolume = 1;
						if (![Engine startAndReturnError:&error]) {
							throw std::runtime_error("movie silent manual engine start failed");
						}
					});
				}
			} catch (...) {
				Close();
				throw;
			}
		}

		~Implementation() {
			Close();
		}

		void Close() noexcept {
			if (State->DeliveryOwner != nullptr) {
				std::terminate();
			}
			if (State->Closed.load()) {
				return;
			}
			if (State->Rendering || std::this_thread::get_id() != State->OwnerThread) {
				std::terminate();
			}
			State->Closed.store(true);
			@autoreleasepool {
				@try {
					[Engine stop];
					if (Source != nil) {
						[Engine disconnectNodeOutput:Source];
						[Engine detachNode:Source];
					}
					for (auto& voice : State->Voices) {
						if (voice) {
							RetireMovieVoice(*voice);
							voice.reset();
						}
					}
					Source = nil;
					Buffer = nil;
					Format = nil;
					Engine = nil;
				} @catch (NSException*) {
					std::terminate();
				}
			}
		}

		std::shared_ptr<MovieSilentState> State;
		audio::OfflineSoundSystemBoundary Boundary;
		AVAudioEngine* Engine = nil;
		AVAudioFormat* Format = nil;
		AVAudioPCMBuffer* Buffer = nil;
		AVAudioSourceNode* Source = nil;
	};

	MovieAudioOutput::MovieAudioOutput(std::unique_ptr<Implementation> implementation)
	    : m_Implementation(std::move(implementation)) {
	}

	MovieAudioOutput::~MovieAudioOutput() {
		Close();
	}

	bool MovieAudioOutput::TryQueue(MovieAudioBlock& block) {
		const auto registry = m_Implementation->RequireLive();
		auto& voice = *m_Implementation->Voice;
		if (voice.InputEnded || block.Samples.empty() || block.Samples.size() % voice.Description.Channels != 0 || block.Samples.size() / voice.Description.Channels > 65536 || std::any_of(block.Samples.begin(), block.Samples.end(), [](float sample) {
			    return !std::isfinite(sample);
		    })) {
			throw std::runtime_error("movie PCM assembly requires a real nonempty bounded finite decoded block before input EOF");
		}
		if (voice.Paused) {
			return false;
		}
		if (voice.PendingCount == voice.Pending.size()) {
			// 38f610 starts the actual voice only when another sample encounters the full three-slot ring.
			voice.Started = true;
			SubmitMoviePending(voice);

			return false;
		}
		if (!voice.SampleEpoch100ns) {
			voice.SampleEpoch100ns = block.Timestamp100ns;
		}
		voice.Pending[voice.PendingWrite] = std::move(block);
		voice.PendingWrite = (voice.PendingWrite + 1) % voice.Pending.size();
		++voice.PendingCount;
		++voice.AssembledBlocks;
		SubmitMoviePending(voice);

		return true;
	}

	void MovieAudioOutput::MarkInputEnded() {
		const auto registry = m_Implementation->RequireLive();
		m_Implementation->Voice->InputEnded = true;
	}

	MovieAudioOutputSnapshot MovieAudioOutput::Snapshot(std::int64_t now100ns) const {
		const auto registry = m_Implementation->RequireLive();
		const auto& voice = *m_Implementation->Voice;
		if (now100ns < 0) {
			throw std::runtime_error("movie output snapshot requires its actual nonnegative monotonic clock");
		}
		MovieAudioOutputSnapshot result;
		result.Progress.InputEnded = voice.InputEnded;
		result.Progress.AssembledBlocks = voice.AssembledBlocks;
		result.AssembledBlocks = voice.AssembledBlocks;
		const auto delivery_mode = registry->DeliveryOwner != nullptr || registry->DeliveryStopped;
		result.CompletedFrames = delivery_mode ? voice.DeliveredFrames : voice.CompletedFrames;
		result.AppliedGain = voice.Gain;
		result.OutputDrained = voice.InputEnded && voice.PendingCount == 0 && voice.SubmittedCount == 0 && (!delivery_mode || voice.DeliveredFrames == voice.CompletedFrames);
		const auto clock = delivery_mode ? voice.DeliveryClock100ns : voice.BlockClock100ns;
		if (clock && voice.SampleEpoch100ns) {
			if (result.CompletedFrames > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) / 10000000 || now100ns < *clock) {
				throw std::runtime_error("movie actual completed-buffer clock exceeds its supported monotonic range");
			}
			const auto completed_time = static_cast<std::int64_t>(result.CompletedFrames * 10000000 / voice.Description.SamplesPerSecond);
			const auto elapsed = now100ns - *clock;
			if (completed_time > std::numeric_limits<std::int64_t>::max() - elapsed || *voice.SampleEpoch100ns > std::numeric_limits<std::int64_t>::max() - (completed_time + elapsed)) {
				throw std::runtime_error("movie actual output position exceeds signed 100ns range");
			}
			result.Progress.Position100ns = *voice.SampleEpoch100ns + completed_time + elapsed;
		}

		return result;
	}

	void MovieAudioOutput::SetPaused(bool paused) {
		const auto registry = m_Implementation->RequireLive();
		m_Implementation->Voice->Paused = paused;
	}

	void MovieAudioOutput::SetVolume(float volume) {
		const auto registry = m_Implementation->RequireLive();
		if (!std::isfinite(volume)) {
			throw std::runtime_error("movie silent output nonfinite native volume requires its system error witness");
		}
		const auto gain = ReadMovieSourceGain(*registry, volume);
		m_Implementation->Voice->Volume = volume;
		m_Implementation->Voice->Gain = gain;
	}

	void MovieAudioOutput::Close() noexcept {
		m_Implementation->Close();
	}

	MovieSilentAudioRegistry::MovieSilentAudioRegistry(std::function<std::int64_t()> read_clock100ns)
	    : m_Implementation(std::make_unique<Implementation>(std::move(read_clock100ns), nullptr)) {
	}

	MovieSilentAudioRegistry::MovieSilentAudioRegistry(std::function<std::int64_t()> read_clock100ns, audio::SoundResourceControl& actual_sound)
	    : m_Implementation(std::make_unique<Implementation>(std::move(read_clock100ns), &actual_sound)) {
	}

	MovieSilentAudioRegistry::~MovieSilentAudioRegistry() = default;

	std::unique_ptr<MovieAudioOutput> MovieSilentAudioRegistry::Open(const MoviePcmDescription& description) {
		auto& registry = *m_Implementation->State;
		RequireMovieAudioOwner(registry);
		if (registry.DeliveryStopped) {
			throw std::runtime_error("Movie source cannot open after its containing delivery output retired");
		}
		if (description.SamplesPerSecond != kMovieMasterRate || description.Channels != kMovieMasterChannels) {
			throw std::runtime_error("movie silent output requires the actual measured 48kHz stereo source/mastering domain");
		}
		const auto free = std::find(registry.Voices.begin(), registry.Voices.end(), nullptr);
		if (free == registry.Voices.end()) {
			throw std::runtime_error("movie silent registry live source capacity exceeded");
		}
		auto implementation = std::make_unique<MovieAudioOutput::Implementation>();
		implementation->Registry = m_Implementation->State;
		implementation->Voice = std::make_shared<MovieVoiceState>();
		implementation->Voice->Description = description;
		implementation->Voice->Gain = ReadMovieSourceGain(registry, implementation->Voice->Volume);
		auto output = std::unique_ptr<MovieAudioOutput>(new MovieAudioOutput(std::move(implementation)));
		*free = output->m_Implementation->Voice;

		return output;
	}

	void MovieSilentAudioRegistry::RenderSilent(std::uint32_t frames) {
		RenderFrames(frames, {});
	}

	void MovieSilentAudioRegistry::RenderPcm(std::span<float> interleaved) {
		if (interleaved.size() % kMovieMasterChannels != 0 || interleaved.size() / kMovieMasterChannels > kMovieRenderMaximum) {
			throw std::runtime_error("movie PCM readout requires bounded whole48k stereo frames");
		}
		RenderFrames(static_cast<std::uint32_t>(interleaved.size() / kMovieMasterChannels), interleaved);
	}

	void MovieSilentAudioRegistry::RenderFrames(std::uint32_t frames, std::span<float> output) {
		auto& state = *m_Implementation;
		RequireMovieAudioOwner(*state.State);
		if (state.State->DeliveryStopped || (state.State->DeliveryOwner != nullptr && !state.State->DeliveryRendering)) {
			throw std::runtime_error("Movie manual render must use its exact containing delivery/submit phase");
		}
		if (frames > kMovieRenderMaximum) {
			throw std::runtime_error("movie silent render exceeds its preallocated actual manual chunk");
		}
		if (frames == 0) {
			return;
		}
		state.State->Rendering = true;
		try {
			@autoreleasepool {
				state.Boundary.Invoke(audio::OfflineSoundSystemOperation::Render, [&] {
					NSError* error = nil;
					if (state.State->DeliveryOwner != nullptr) {
						const auto before = state.Engine.manualRenderingSampleTime;
						if (before < 0 || before > 9007199254740991LL - frames) {
							throw std::runtime_error("Movie delivery manual sample timeline exceeds exact admitted integer range");
						}
						state.State->DeliverySampleOrigin = before;
					}
					const auto status = [state.Engine renderOffline:frames toBuffer:state.Buffer error:&error];
					if (state.State->DeliveryOwner != nullptr && (state.State->DeliveryCallbacks != 1 || state.Engine.manualRenderingSampleTime != state.State->DeliverySampleOrigin + frames)) {
						throw std::runtime_error("Movie delivery output did not retain the actual source-to-manual timeline extent");
					}
					if (state.State->CallbackFailure) {
						std::rethrow_exception(state.State->CallbackFailure);
					}
					if (state.State->ForeignCallback.load() || status != AVAudioEngineManualRenderingStatusSuccess || state.Buffer.frameLength != frames) {
						throw std::runtime_error("movie real manual offline render did not complete its requested PCM extent");
					}
					if (!output.empty()) {
						const auto planes = state.Buffer.floatChannelData;
						if (planes == nullptr || output.size() != static_cast<std::size_t>(frames) * kMovieMasterChannels) {
							throw std::runtime_error("movie PCM readout lost its exact planar mixer output");
						}
						for (std::size_t channel = 0; channel < kMovieMasterChannels; ++channel) {
							if (planes[channel] == nullptr) {
								throw std::runtime_error("movie PCM readout lost a real output plane");
							}
						}
						for (std::size_t frame = 0; frame < frames; ++frame) {
							for (std::size_t channel = 0; channel < kMovieMasterChannels; ++channel) {
								output[frame * kMovieMasterChannels + channel] = planes[channel][frame];
							}
						}
					}
				});
			}
			state.State->RenderedFrames += frames;
			state.State->Rendering = false;
		} catch (...) {
			state.State->Rendering = false;
			throw;
		}
	}

	std::uint64_t MovieSilentAudioRegistry::RenderedFrames() const noexcept {
		return m_Implementation->State->RenderedFrames;
	}

	void MovieSilentAudioRegistry::Close() noexcept {
		m_Implementation->Close();
	}

	class MoviePcmDelivery::Implementation {
	public:
		struct PendingEvent {
			std::weak_ptr<MovieVoiceState> Voice;
			std::uint64_t MasterFrame = 0;
			std::uint32_t CompletedBlockFrames = 0;
		};

		Implementation(const MoviePcmDelivery* claim, MovieSilentAudioRegistry& movies, audio::OfflineBufferedSoundPool& sound, platform::MacPcmOutput* hardware, audio::PcmOutputSubmissionOwner* software)
		    : Claim(claim), Movies(movies), Sound(sound), Hardware(hardware), Software(software) {
			auto& registry = *Movies.m_Implementation->State;
			RequireMovieAudioOwner(registry);
			if (registry.DeliveryOwner != nullptr || registry.DeliveryStopped || registry.RenderedFrames != 0 || std::any_of(registry.Voices.begin(), registry.Voices.end(), [](const auto& voice) {
				    return voice != nullptr;
			    })) {
				throw std::runtime_error("Movie delivery binding requires its actual fresh unbound manual registry");
			}
			const auto sound_description = Sound.Description();
			if (sound_description.SamplesPerSecond != kMovieMasterRate || sound_description.Channels != kMovieMasterChannels || sound_description.MaximumFrames == 0) {
				throw std::runtime_error("Movie delivery requires its actual48k stereo Sound master");
			}
			std::size_t capacity = 0;
			std::size_t maximum = 0;
			if (Hardware != nullptr) {
				Hardware->RequireMovieAdmission(Claim);
				const auto description = Hardware->Description();
				capacity = description.CapacityFrames;
				maximum = description.MaximumCallbackFrames;
			} else {
				Software->RequireMovieAdmission(Claim);
				const auto description = Software->m_Ring.Description();
				if (description.Channels != kMovieMasterChannels) {
					throw std::runtime_error("software Movie delivery requires explicit real stereo master storage");
				}
				capacity = description.CapacityFrames;
				maximum = description.MaximumTransferFrames;
			}
			if (capacity == 0 || capacity > 65536) {
				throw std::runtime_error("Movie delivery output exceeds the bounded preallocated block-event domain");
			}
			MaximumFrames = std::min({static_cast<std::size_t>(kMovieRenderMaximum), static_cast<std::size_t>(sound_description.MaximumFrames), maximum});
			// A one-frame block may start and end for every voice in each queued master frame.
			// The extra phase reserve covers the synchronous render before its actual ring write commits.
			Events.resize(capacity * kMovieVoiceCapacity * 2 + registry.PhaseEvents.size());
			if (Hardware != nullptr) {
				Hardware->BindMovie(Claim);
			} else {
				Software->BindMovie(Claim);
			}
			registry.DeliveryOwner = Claim;
		}

		void RequireOwner() const {
			if (Closed || Operating || Thread != std::this_thread::get_id()) {
				throw std::runtime_error("Movie delivery requires its actual live idle serialized containing owner");
			}
			if (Failure) {
				std::rethrow_exception(Failure);
			}
			const auto& state = *Movies.m_Implementation->State;
			RequireMovieAudioOwner(state);
			if (state.DeliveryOwner != Claim) {
				throw std::runtime_error("Movie delivery lost its exact actual registry binding");
			}
		}
		void RetainFailure() noexcept {
			if (!Failure) {
				Failure = std::current_exception();
			}
			auto& state = *Movies.m_Implementation->State;
			if (!state.CallbackFailure) {
				state.CallbackFailure = Failure;
			}
		}
		std::optional<audio::PcmOutputDeliveryReceipt> ReadDelivery() {
			return Hardware != nullptr ? Hardware->ReadMovieDelivery(Claim) : Software->ReadMovieDelivery(Claim);
		}
		void ApplyDelivery(const audio::PcmOutputDeliveryReceipt& receipt) {
			const auto& observation = receipt.Observation();
			const auto end = observation.FirstMasterFrame + observation.CopiedFrames;
			if (observation.FirstMasterFrame != DeliveredMaster || end > SubmittedMaster) {
				throw std::runtime_error("Movie delivery receipt left its actual successful submission intervals");
			}
			while (EventCount != 0 && observation.CopiedFrames != 0) {
				auto& event = Events[EventRead];
				const auto reached = event.CompletedBlockFrames != 0 ? event.MasterFrame <= end : event.MasterFrame < end;
				if (!reached) {
					break;
				}
				if (const auto voice = event.Voice.lock(); voice && !voice->Closed) {
					if (voice->DeliveredFrames > voice->CompletedFrames || event.CompletedBlockFrames > voice->CompletedFrames - voice->DeliveredFrames) {
						throw std::runtime_error("Movie delivered block completion exceeds its actual rendered source owner");
					}
					voice->DeliveredFrames += event.CompletedBlockFrames;
					voice->DeliveryClock100ns = receipt.CallbackContinuous100ns();
				}
				event = {};
				EventRead = (EventRead + 1) % Events.size();
				--EventCount;
			}
			DeliveredMaster = end;
		}
		std::size_t PollActual() {
			std::size_t count = 0;
			// One owner visit has a fixed bound even when the device keeps producing receipts.
			for (; count != 65536; ++count) {
				const auto receipt = ReadDelivery();
				if (!receipt) {
					break;
				}
				ApplyDelivery(*receipt);
			}
			return count;
		}
		std::size_t Poll() {
			RequireOwner();
			Operating = true;
			try {
				const auto count = PollActual();
				Operating = false;
				return count;
			} catch (...) {
				RetainFailure();
				Operating = false;
				throw;
			}
		}
		audio::PcmOutputSubmission Submit(std::span<const float> mixed) {
			return Hardware != nullptr ? Hardware->SubmitMovie(Claim, mixed) : Software->SubmitMovie(Claim, mixed);
		}
		void CommitEvents(const audio::PcmOutputSubmission& submission) {
			auto& state = *Movies.m_Implementation->State;
			if (submission.FirstMasterFrame() != SubmittedMaster || submission.Sequence() != SubmissionSequence + 1 || submission.Frames() != state.DeliveryRenderFrames) {
				throw std::runtime_error("Movie render did not commit its one actual contiguous output submission");
			}
			std::array<std::size_t, kMovieVoiceCapacity * 10> order{};
			for (std::size_t index = 0; index < state.PhaseEventCount; ++index) {
				order[index] = index;
			}
			std::sort(order.begin(), order.begin() + static_cast<std::ptrdiff_t>(state.PhaseEventCount), [&](std::size_t left, std::size_t right) {
				const auto l = state.PhaseEvents[left].MasterOffset;
				const auto r = state.PhaseEvents[right].MasterOffset;
				return l < r || (l == r && left < right);
			});
			for (std::size_t index = 0; index < state.PhaseEventCount; ++index) {
				const auto& actual = state.PhaseEvents[order[index]];
				const auto voice = std::find_if(state.Voices.begin(), state.Voices.end(), [&](const auto& candidate) {
					return candidate.get() == actual.Voice;
				});
				if (voice == state.Voices.end() || !*voice || (*voice)->Closed) {
					throw std::runtime_error("Movie block event lost its exact enclosing source before submission commit");
				}
				auto& event = Events[EventWrite];
				event.Voice = *voice;
				event.MasterFrame = submission.FirstMasterFrame() + actual.MasterOffset;
				event.CompletedBlockFrames = actual.CompletedBlockFrames;
				EventWrite = (EventWrite + 1) % Events.size();
				++EventCount;
			}
			SubmittedMaster += submission.Frames();
			SubmissionSequence = submission.Sequence();
		}
		audio::OfflineSoundRenderResult RenderAndSubmit(std::uint32_t frames, bool silent) {
			RequireOwner();
			if (frames == 0 || frames > MaximumFrames) {
				throw std::runtime_error("Movie delivery render requires a nonempty actual bounded master chunk");
			}
			Operating = true;
			auto& state = *Movies.m_Implementation->State;
			try {
				static_cast<void>(PollActual());
				if (state.PhaseEvents.size() > Events.size() - EventCount) {
					throw std::runtime_error("Movie delivery must retire actual callback block receipts before another render");
				}
				auto mixed = std::span(Mixed).first(static_cast<std::size_t>(frames) * kMovieMasterChannels);
				auto movie = std::span(Movie).first(mixed.size());
				const auto sound = Sound.RenderTyped(mixed);
				if (sound.MasterFrames != frames || sound.SourceCount > sound.Sources.size() || sound.Status == audio::OfflineSoundRenderStatus::SourceFailed) {
					throw std::runtime_error("Movie delivery received an actual failed/different Sound master extent");
				}
				state.PhaseEventCount = 0;
				state.DeliveryRenderFrames = frames;
				state.DeliveryCallbacks = 0;
				state.DeliveryRendering = true;
				Movies.RenderPcm(movie);
				state.DeliveryRendering = false;
				for (std::size_t index = 0; index != mixed.size(); ++index) {
					mixed[index] += movie[index];
					if (!std::isfinite(mixed[index])) {
						throw std::runtime_error("Movie/Sound actual mixed sample exceeds the finite output domain");
					}
				}
				if (silent) {
					std::fill(mixed.begin(), mixed.end(), 0.0f);
				}

				const auto submission = Submit(mixed);
				CommitEvents(submission);
				Operating = false;
				return sound;
			} catch (...) {
				state.DeliveryRendering = false;
				RetainFailure();
				Operating = false;
				throw;
			}
		}
		void Close() {
			if (Closed) {
				return;
			}
			if (Operating || Thread != std::this_thread::get_id()) {
				throw std::runtime_error("Movie delivery retirement requires its actual idle serialized owner");
			}
			std::exception_ptr cleanup;
			try {
				if (Hardware != nullptr) {
					Hardware->CloseMovie(Claim);
				} else {
					Software->CloseMovie(Claim);
				}
			} catch (...) {
				cleanup = std::current_exception();
			}
			const auto retired = Hardware != nullptr ? Hardware->MovieRetired() : Software->m_Closed;
			if (retired) {
				if (Hardware != nullptr) {
					Hardware->ReleaseMovie(Claim);
				} else {
					Software->ReleaseMovie(Claim);
				}
				auto& state = *Movies.m_Implementation->State;
				state.DeliveryOwner = nullptr;
				state.DeliveryStopped = true;
				for (auto& event : Events) {
					event = {};
				}
				EventCount = 0;
				Closed = true;
			}
			if (Failure) {
				std::rethrow_exception(Failure);
			}
			if (cleanup) {
				std::rethrow_exception(cleanup);
			}
		}

		const MoviePcmDelivery* Claim;
		MovieSilentAudioRegistry& Movies;
		audio::OfflineBufferedSoundPool& Sound;
		platform::MacPcmOutput* Hardware;
		audio::PcmOutputSubmissionOwner* Software;
		const std::thread::id Thread = std::this_thread::get_id();
		std::vector<PendingEvent> Events;
		std::array<float, kMovieRenderMaximum * kMovieMasterChannels> Mixed{};
		std::array<float, kMovieRenderMaximum * kMovieMasterChannels> Movie{};
		std::exception_ptr Failure;
		std::size_t MaximumFrames = 0;
		std::size_t EventRead = 0;
		std::size_t EventWrite = 0;
		std::size_t EventCount = 0;
		std::uint64_t SubmittedMaster = 0;
		std::uint64_t DeliveredMaster = 0;
		std::uint64_t SubmissionSequence = 0;
		bool Operating = false;
		bool Closed = false;
	};

	MoviePcmDelivery::MoviePcmDelivery(MovieSilentAudioRegistry& movies, audio::OfflineBufferedSoundPool& sound, platform::MacPcmOutput& output)
	    : m_Implementation(std::make_unique<Implementation>(this, movies, sound, &output, nullptr)) {
	}
	MoviePcmDelivery::MoviePcmDelivery(MoviePcmSoftwareOnly, MovieSilentAudioRegistry& movies, audio::OfflineBufferedSoundPool& sound, audio::PcmOutputSubmissionOwner& output)
	    : m_Implementation(std::make_unique<Implementation>(this, movies, sound, nullptr, &output)) {
	}
	MoviePcmDelivery::~MoviePcmDelivery() noexcept {
		try {
			m_Implementation->Close();
		} catch (...) {
			if (!m_Implementation->Closed) {
				std::terminate();
			}
		}
	}
	audio::OfflineSoundRenderResult MoviePcmDelivery::RenderAndSubmit(std::uint32_t master_frames) {
		return RenderAndSubmit(master_frames, false);
	}
	audio::OfflineSoundRenderResult MoviePcmDelivery::RenderAndSubmit(std::uint32_t master_frames, bool silent) {
		return m_Implementation->RenderAndSubmit(master_frames, silent);
	}
	std::size_t MoviePcmDelivery::Poll() {
		return m_Implementation->Poll();
	}
	std::size_t MoviePcmDelivery::PendingBlockEvents() const {
		m_Implementation->RequireOwner();
		return m_Implementation->EventCount;
	}
	void MoviePcmDelivery::Close() {
		m_Implementation->Close();
	}
	bool MoviePcmDelivery::IsClosed() const noexcept {
		return m_Implementation->Closed;
	}

} // namespace sgre::media
