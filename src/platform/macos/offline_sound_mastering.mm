#include "sgre/audio/offline_sound_mastering.hpp"

#include "offline_sound_boundary.hpp"

#import <AVFAudio/AVFAudio.h>

#include <algorithm>
#include <atomic>
#include <exception>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace sgre::audio {
	static constexpr std::size_t kMaximumBufferedSourceNodes = 40;
	static constexpr std::uint32_t kMaximumOfflineFrames = 2048;

	static std::runtime_error AudioFailure(const char* operation, NSError* error) {
		const char* detail = error == nil ? nullptr : error.localizedDescription.UTF8String;

		return std::runtime_error(std::string(operation) + (detail == nullptr ? " failed" : ": " + std::string(detail)));
	}

	struct OfflineSourceBorrow {
		BufferedSoundCueId Cue;
		std::size_t Channel = 0;
		SoundVoiceAddress Source;
		std::vector<float> Workspace;
	};

	enum class OfflineCallbackFailure {
		None,
		ForeignThread,
		OutputExtent,
		SourceException
	};

	struct OfflineRenderState {
		BufferedSoundCues* Cues = nullptr;
		OfflineSoundMasteringDescription Description;
		std::vector<OfflineSourceBorrow> Sources;
		std::thread::id OwnerThread = std::this_thread::get_id();
		std::atomic<bool> Closed = false;
		std::atomic<OfflineCallbackFailure> Failure = OfflineCallbackFailure::None;
		// Only the serialized owner writes/reads this; foreign callbacks never access source owners or this cell.
		std::exception_ptr SourceFailure;
	};

	static OSStatus RenderSource(const std::shared_ptr<OfflineRenderState>& state, std::size_t index, BOOL* silence, AVAudioFrameCount frames, AudioBufferList* output) noexcept {
		if (!state || state->Closed.load()) {
			return kAudio_ParamError;
		}
		if (std::this_thread::get_id() != state->OwnerThread) {
			state->Failure.store(OfflineCallbackFailure::ForeignThread);
			return kAudio_ParamError;
		}
		const auto channels = state->Description.Channels;
		if (silence == nullptr || output == nullptr || index >= state->Sources.size() || frames > state->Description.MaximumFrames || output->mNumberBuffers != channels) {
			state->Failure.store(OfflineCallbackFailure::OutputExtent);
			return kAudio_ParamError;
		}
		for (std::size_t channel = 0; channel < channels; ++channel) {
			if (output->mBuffers[channel].mNumberChannels != 1 || output->mBuffers[channel].mData == nullptr || output->mBuffers[channel].mDataByteSize < static_cast<std::size_t>(frames) * sizeof(float)) {
				state->Failure.store(OfflineCallbackFailure::OutputExtent);
				return kAudio_ParamError;
			}
		}
		try {
			auto& source = state->Sources[index];
			const auto current = state->Cues->InspectChannel(source.Cue, source.Channel);
			if (current.Source.Type != source.Source.Type || current.Source.Slot != source.Source.Slot) {
				throw std::runtime_error("offline source borrow changed its actual pooled source identity");
			}
			auto samples = std::span(source.Workspace).first(static_cast<std::size_t>(frames) * channels);
			std::fill(samples.begin(), samples.end(), 0);
			const auto consumed = state->Cues->ConsumeMixedChannelFrames(source.Cue, source.Channel, samples);
			for (std::size_t channel = 0; channel < channels; ++channel) {
				// The foreign API explicitly supplies noninterleaved Float32 planes for this exact AVAudioFormat.
				auto* plane = static_cast<float*>(output->mBuffers[channel].mData);
				for (std::size_t frame = 0; frame < frames; ++frame) {
					plane[frame] = samples[frame * channels + channel];
				}
			}
			*silence = consumed == 0;

			return noErr;
		} catch (...) {
			if (!state->SourceFailure) {
				state->SourceFailure = std::current_exception();
			}
			state->Failure.store(OfflineCallbackFailure::SourceException);

			return kAudio_ParamError;
		}
	}

	class OfflineSoundMastering::Implementation {
	public:
		Implementation(BufferedSoundCues& actual_sources, std::span<const BufferedSoundCueId> actual_cues, OfflineSoundMasteringDescription description) {
			if (actual_cues.empty() || actual_cues.size() > kMaximumBufferedSourceNodes || description.Channels == 0 || description.Channels > 8 || description.SamplesPerSecond < 8000 || description.SamplesPerSecond > 192000 || description.MaximumFrames == 0 || description.MaximumFrames > kMaximumOfflineFrames) {
				throw std::runtime_error("offline mastering requires an actual bounded source batch and explicit supported PCM format");
			}
			State = std::make_shared<OfflineRenderState>();
			State->Cues = &actual_sources;
			State->Description = description;
			State->Sources.reserve(kMaximumBufferedSourceNodes);
			for (const auto cue : actual_cues) {
				const auto info = actual_sources.Inspect(cue);
				if (info.Channels == 0 || info.Channels > kMaximumBufferedSourceNodes - State->Sources.size()) {
					throw std::runtime_error("offline mastering source count exceeds its real buffered pools");
				}
				for (std::size_t channel = 0; channel < info.Channels; ++channel) {
					const auto source = actual_sources.InspectChannel(cue, channel);
					if (!source.Mix || !source.State.Samples.CallbackActive || !source.State.Samples.Queued || source.State.Samples.BufferFrames == 0 || source.State.Samples.SamplesPerSecond != description.SamplesPerSecond || source.Mix->DestinationChannels != description.Channels || (source.Mix->FrequencyRatio && *source.Mix->FrequencyRatio != 1)) {
						throw std::runtime_error("offline mastering requires genuine decoded queued sources at its explicit unchanged rate/mix format");
					}
					if (source.Source.Type == SoundVoiceType::Streaming1 || std::any_of(State->Sources.begin(), State->Sources.end(), [&](const auto& prior) {
						    return prior.Source.Type == source.Source.Type && prior.Source.Slot == source.Source.Slot;
					    })) {
						throw std::runtime_error("offline mastering requires distinct actual device0/1 source slots");
					}
					OfflineSourceBorrow borrow;
					borrow.Cue = cue;
					borrow.Channel = channel;
					borrow.Source = source.Source;
					borrow.Workspace.resize(static_cast<std::size_t>(description.MaximumFrames) * description.Channels);
					State->Sources.push_back(std::move(borrow));
				}
			}
			try {
				CreateGraph();
			} catch (...) {
				Close();
				throw;
			}
		}

		~Implementation() {
			Close();
		}

		void RequireOwner() const {
			if (State->Closed.load() || std::this_thread::get_id() != State->OwnerThread) {
				throw std::runtime_error("offline mastering requires its actual live serialized manual system engine");
			}
			Boundary.Invoke(OfflineSoundSystemOperation::EngineState, [&] {
				if (!Engine.isInManualRenderingMode || Engine.manualRenderingMode != AVAudioEngineManualRenderingModeOffline || !Engine.isRunning) {
					throw std::runtime_error("offline mastering requires its actual live serialized manual system engine");
				}
			});
		}

		void CreateGraph() {
			@autoreleasepool {
				Boundary.Invoke(OfflineSoundSystemOperation::GraphCreation, [&] {
					const auto description = State->Description;
					Format = [[AVAudioFormat alloc] initWithCommonFormat:AVAudioPCMFormatFloat32 sampleRate:description.SamplesPerSecond channels:description.Channels interleaved:NO];
					Engine = [[AVAudioEngine alloc] init];
					NSError* error = nil;
					// Enable offline mode before touching mixer/output nodes; the SDK warns earlier node access may configure hardware.
					if (Format == nil || Engine == nil || ![Engine enableManualRenderingMode:AVAudioEngineManualRenderingModeOffline format:Format maximumFrameCount:description.MaximumFrames error:&error]) {
						throw AudioFailure("manual offline engine configuration", error);
					}
					const auto actual_format = Engine.manualRenderingFormat;
					if (actual_format == nil || actual_format.commonFormat != AVAudioPCMFormatFloat32 || actual_format.isInterleaved || actual_format.channelCount != description.Channels || actual_format.sampleRate != description.SamplesPerSecond || Engine.manualRenderingMaximumFrameCount != description.MaximumFrames) {
						throw std::runtime_error("actual manual mastering format differs from its explicitly requested source batch");
					}
					Buffer = [[AVAudioPCMBuffer alloc] initWithPCMFormat:Format frameCapacity:description.MaximumFrames];
					if (Buffer == nil) {
						throw std::runtime_error("manual offline output buffer allocation failed");
					}
					Nodes.reserve(State->Sources.size());
					const std::weak_ptr<OfflineRenderState> weak = State;
					for (std::size_t index = 0; index < State->Sources.size(); ++index) {
						AVAudioSourceNode* node = [[AVAudioSourceNode alloc] initWithFormat:Format
						                                                        renderBlock:^OSStatus(BOOL* silence, const AudioTimeStamp*, AVAudioFrameCount frames, AudioBufferList* output) {
							                                                      return RenderSource(weak.lock(), index, silence, frames, output);
						                                                        }];
						if (node == nil) {
							throw std::runtime_error("manual offline source node allocation failed");
						}
						[Engine attachNode:node];
						Nodes.push_back(node);
						[Engine connect:node to:Engine.mainMixerNode format:Format];
						node.volume = 1;
						node.pan = 0;
					}
					[Engine connect:Engine.mainMixerNode to:Engine.outputNode format:Format];
					Engine.mainMixerNode.outputVolume = 1;
					if (![Engine startAndReturnError:&error]) {
						throw AudioFailure("manual offline engine start", error);
					}
					RequireOwner();
				});
			}
		}

		void Close() noexcept {
			if (std::this_thread::get_id() != State->OwnerThread) {
				std::terminate();
			}
			if (State->Closed.exchange(true)) {
				return;
			}
			@autoreleasepool {
				@try {
					[Engine stop];
					for (auto node : Nodes) {
						[Engine disconnectNodeOutput:node];
						[Engine detachNode:node];
					}
					Nodes.clear();
					Buffer = nil;
					Format = nil;
					Engine = nil;
				} @catch (NSException*) {
					std::terminate();
				}
			}
		}

		mutable OfflineSoundSystemBoundary Boundary;
		std::shared_ptr<OfflineRenderState> State;
		AVAudioEngine* Engine = nil;
		AVAudioFormat* Format = nil;
		AVAudioPCMBuffer* Buffer = nil;
		std::vector<AVAudioSourceNode*> Nodes;
	};

	OfflineSoundMastering::OfflineSoundMastering(BufferedSoundCues& actual_sources, std::span<const BufferedSoundCueId> actual_cues, OfflineSoundMasteringDescription description) : m_Implementation(std::make_unique<Implementation>(actual_sources, actual_cues, description)) {
	}

	OfflineSoundMastering::~OfflineSoundMastering() = default;

	std::size_t OfflineSoundMastering::Render(std::span<float> output) {
		auto& state = *m_Implementation;
		state.RequireOwner();
		const auto channels = state.State->Description.Channels;
		if (output.empty() || output.size() % channels != 0 || output.size() / channels > state.State->Description.MaximumFrames) {
			throw std::runtime_error("offline output exceeds its actual nonempty frame extent");
		}
		@autoreleasepool {
			return state.Boundary.Invoke(OfflineSoundSystemOperation::Render, [&]() -> std::size_t {
				NSError* error = nil;
				const auto requested = static_cast<AVAudioFrameCount>(output.size() / channels);
				const auto result = [state.Engine renderOffline:requested toBuffer:state.Buffer error:&error];
				if (state.State->SourceFailure) {
					std::rethrow_exception(state.State->SourceFailure);
				}
				if (state.State->Failure.load() != OfflineCallbackFailure::None || result != AVAudioEngineManualRenderingStatusSuccess) {
					throw AudioFailure("actual offline source/mastering render", error);
				}
				const auto frames = state.Buffer.frameLength;
				const auto planes = state.Buffer.floatChannelData;
				if (frames != requested || planes == nullptr) {
					throw std::runtime_error("actual offline output differs from its requested Float32 frame extent");
				}
				for (std::size_t frame = 0; frame < frames; ++frame) {
					for (std::size_t channel = 0; channel < channels; ++channel) {
						output[frame * channels + channel] = planes[channel][frame];
					}
				}

				return frames;
			});
		}
	}

	OfflineSoundMasteringDescription OfflineSoundMastering::Description() const {
		m_Implementation->RequireOwner();
		return m_Implementation->Boundary.Invoke(OfflineSoundSystemOperation::Format, [&]() -> OfflineSoundMasteringDescription {
			const auto format = m_Implementation->Engine.manualRenderingFormat;
			OfflineSoundMasteringDescription result;
			result.SamplesPerSecond = static_cast<std::uint32_t>(format.sampleRate);
			result.Channels = static_cast<std::uint8_t>(format.channelCount);
			result.MaximumFrames = m_Implementation->Engine.manualRenderingMaximumFrameCount;

			return result;
		});
	}

	std::uint64_t OfflineSoundMastering::RenderedFrames() const {
		m_Implementation->RequireOwner();
		return m_Implementation->Boundary.Invoke(OfflineSoundSystemOperation::Timeline, [&]() -> std::uint64_t {
			const auto frames = m_Implementation->Engine.manualRenderingSampleTime;
			if (frames < 0) {
				throw std::runtime_error("actual offline engine returned a negative render timeline");
			}

			return static_cast<std::uint64_t>(frames);
		});
	}

	void OfflineSoundMastering::Close() noexcept {
		m_Implementation->Close();
	}
} // namespace sgre::audio
