#include "sgre/media/movie_audio.hpp"

#include "offline_sound_boundary.hpp"

#include "sgre/audio/sound_resource_control.hpp"
#include "sgre/media/movie_gain.hpp"

#import <AVFAudio/AVFAudio.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <exception>
#include <limits>
#include <optional>
#include <stdexcept>
#include <thread>
#include <utility>

namespace sgre::media {

	static constexpr std::uint32_t kMovieMasterRate = 48000;
	static constexpr std::uint32_t kMovieMasterChannels = 2;
	static constexpr std::uint32_t kMovieRenderMaximum = 2048;
	static constexpr std::size_t kMovieVoiceCapacity = 16;

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
				voice.BlockClock100ns = MovieCallbackClock(registry);
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
				voice.BlockClock100ns = MovieCallbackClock(registry);
				actual.reset();
				voice.CurrentOffset = 0;
				voice.CurrentStarted = false;
				voice.SubmittedRead = (voice.SubmittedRead + 1) % voice.Submitted.size();
				--voice.SubmittedCount;
				SubmitMoviePending(voice);
			}
		}
	}

	static OSStatus RenderMovieSources(const std::shared_ptr<MovieSilentState>& state, BOOL* silence, AVAudioFrameCount frames, AudioBufferList* output) noexcept {
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
						                                       renderBlock:^OSStatus(BOOL* silence, const AudioTimeStamp*, AVAudioFrameCount frames, AudioBufferList* output) {
							                                     return RenderMovieSources(weak.lock(), silence, frames, output);
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
		result.CompletedFrames = voice.CompletedFrames;
		result.AppliedGain = voice.Gain;
		result.OutputDrained = voice.InputEnded && voice.PendingCount == 0 && voice.SubmittedCount == 0;
		if (voice.BlockClock100ns && voice.SampleEpoch100ns) {
			if (voice.CompletedFrames > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) / 10000000 || now100ns < *voice.BlockClock100ns) {
				throw std::runtime_error("movie actual completed-buffer clock exceeds its supported monotonic range");
			}
			const auto completed_time = static_cast<std::int64_t>(voice.CompletedFrames * 10000000 / voice.Description.SamplesPerSecond);
			const auto elapsed = now100ns - *voice.BlockClock100ns;
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
					const auto status = [state.Engine renderOffline:frames toBuffer:state.Buffer error:&error];
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

} // namespace sgre::media
