#include "sgre/audio/offline_buffered_sound_pool.hpp"

#include "offline_sound_boundary.hpp"
#include "sgre/audio/sound_resource_control.hpp"

#import <AVFAudio/AVFAudio.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <exception>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace sgre::audio {
	static constexpr std::size_t kBufferedNodeCapacity = 40;
	static constexpr std::uint32_t kOfflinePoolMaximumFrames = 2048;
	// System SRC may request more source frames than one mastering slice, including filter lookahead.
	static constexpr AVAudioFrameCount kMaximumSourceCallbackFrames = 65536;

	static std::size_t SourceNodeIndex(SoundVoiceAddress address) {
		if (address.Type == SoundVoiceType::Buffered32 && address.Slot < 32) {
			return address.Slot;
		}
		if (address.Type == SoundVoiceType::Buffered8 && address.Slot < 8) {
			return 32 + address.Slot;
		}

		throw std::runtime_error("offline sound node requires its actual pool0/1 address");
	}

	static std::runtime_error PoolAudioFailure(const char* operation, NSError* error) {
		const char* detail = error == nil ? nullptr : error.localizedDescription.UTF8String;

		return std::runtime_error(std::string(operation) + (detail == nullptr ? " failed" : ": " + std::string(detail)));
	}

	enum class PoolCallbackFailure {
		None,
		ForeignThread,
		OutputExtent,
		SourceException
	};

	struct OfflinePoolState {
		BufferedSoundSources* Sources = nullptr;
		OfflineSoundMasteringDescription Description;
		const std::thread::id OwnerThread = std::this_thread::get_id();
		std::atomic<bool> Closed = false;
		std::atomic<PoolCallbackFailure> Failure = PoolCallbackFailure::None;
		// The serialized owner alone accesses this cell; foreign callbacks never touch Sources or SourceFailure.
		std::exception_ptr SourceFailure;
		bool Collecting = false;
	};

	struct OfflinePoolSource {
		std::weak_ptr<OfflinePoolState> Pool;
		SoundVoiceAddress Address;
		std::uint16_t SourceChannels = 0;
		std::uint32_t SamplesPerSecond = 0;
		bool Streaming = false;
		OfflineSoundSourceRender Observation;
		std::optional<SoundMix> Mix;
		std::vector<float> SourceWorkspace;
		std::vector<float> OutputWorkspace;
		std::atomic<bool> Closed = false;
	};

	static OSStatus CallbackFailure(const std::shared_ptr<OfflinePoolState>& pool, PoolCallbackFailure failure) noexcept {
		auto expected = PoolCallbackFailure::None;
		pool->Failure.compare_exchange_strong(expected, failure);

		return kAudio_ParamError;
	}

	static OSStatus RenderPoolSource(const std::shared_ptr<OfflinePoolSource>& source, BOOL* silence, AVAudioFrameCount frames, AudioBufferList* output) noexcept {
		if (!source || source->Closed.load()) {
			return kAudio_ParamError;
		}
		const auto pool = source->Pool.lock();
		if (!pool || pool->Closed.load()) {
			return kAudio_ParamError;
		}
		if (std::this_thread::get_id() != pool->OwnerThread) {
			return CallbackFailure(pool, PoolCallbackFailure::ForeignThread);
		}
		const auto channels = pool->Description.Channels;
		if (silence == nullptr || output == nullptr || frames > kMaximumSourceCallbackFrames || output->mNumberBuffers != channels) {
			return CallbackFailure(pool, PoolCallbackFailure::OutputExtent);
		}
		for (std::size_t channel = 0; channel < channels; ++channel) {
			if (output->mBuffers[channel].mNumberChannels != 1 || output->mBuffers[channel].mData == nullptr || output->mBuffers[channel].mDataByteSize < static_cast<std::size_t>(frames) * sizeof(float)) {
				return CallbackFailure(pool, PoolCallbackFailure::OutputExtent);
			}
		}
		try {
			if (pool->Sources == nullptr || !source->Mix) {
				if (source->Streaming) {
					return CallbackFailure(pool, PoolCallbackFailure::SourceException);
				}
				throw std::runtime_error("offline source callback requires its actual connected decoded owner and committed matrix");
			}
			bool all_silent = true;
			std::size_t offset = 0;
			while (offset < frames) {
				// Scratch remains the declared mastering size; the callback's frame count is in source-rate units.
				const auto count = std::min<std::size_t>(pool->Description.MaximumFrames, frames - offset);
				auto mixed = std::span(source->OutputWorkspace).first(count * channels);
				auto input = std::span(source->SourceWorkspace).first(count * source->SourceChannels);
				std::fill(mixed.begin(), mixed.end(), 0);
				std::size_t consumed = 0;
				OfflineSoundRenderStatus status = OfflineSoundRenderStatus::Rendered;
				if (source->Streaming) {
					const auto read = pool->Sources->ConsumeWmaStreamFrames(source->Address, input);
					if (read.Status == WmaStreamReadStatus::Rejected || read.Frames > count || source->Mix->SourceChannels != source->SourceChannels || source->Mix->DestinationChannels != channels) {
						return CallbackFailure(pool, PoolCallbackFailure::SourceException);
					}
					consumed = read.Frames;
					source->Observation.LastReadStatus = read.Status;
					source->Observation.InputComplete = read.InputComplete;
					if (read.Status == WmaStreamReadStatus::Failed) {
						status = OfflineSoundRenderStatus::SourceFailed;
					} else if (read.Status == WmaStreamReadStatus::Prefilling) {
						status = OfflineSoundRenderStatus::Prefilling;
					} else if (read.Status == WmaStreamReadStatus::Starved || (read.Status == WmaStreamReadStatus::Samples && consumed < count && !read.InputComplete)) {
						status = OfflineSoundRenderStatus::Starved;
					}
				} else {
					consumed = pool->Sources->ConsumeFrames(source->Address, input);
				}
				if (pool->Collecting) {
					auto& observation = source->Observation;
					observation.CallbackObserved = true;
					if (count > std::numeric_limits<std::uint64_t>::max() - observation.RequestedFrames) {
						return CallbackFailure(pool, PoolCallbackFailure::OutputExtent);
					}
					observation.RequestedFrames += count;
					observation.ConsumedFrames += consumed;
					observation.SilencedFrames += count - consumed;
					if (static_cast<std::uint8_t>(status) > static_cast<std::uint8_t>(observation.Status)) {
						observation.Status = status;
					}
				}
				AccumulateSoundMix(*source->Mix, input.first(consumed * source->SourceChannels), mixed.first(consumed * channels));
				for (std::size_t channel = 0; channel < channels; ++channel) {
					// This exact foreign format supplies noninterleaved Float32 planes; portable samples remain interleaved.
					auto* plane = static_cast<float*>(output->mBuffers[channel].mData);
					for (std::size_t frame = 0; frame < count; ++frame) {
						plane[offset + frame] = mixed[frame * channels + channel];
					}
				}
				all_silent = all_silent && consumed == 0;
				offset += count;
			}
			*silence = all_silent;

			return noErr;
		} catch (...) {
			if (!pool->SourceFailure) {
				pool->SourceFailure = std::current_exception();
			}

			return CallbackFailure(pool, PoolCallbackFailure::SourceException);
		}
	}

	struct OfflinePoolNode {
		std::shared_ptr<OfflinePoolSource> Source;
		AVAudioSourceNode* Node = nil;
	};

	class OfflineBufferedSoundPool::Implementation {
	public:
		explicit Implementation(OfflineSoundMasteringDescription description) {
			if (description.Channels == 0 || description.Channels > 8 || description.SamplesPerSecond < 8000 || description.SamplesPerSecond > 192000 || description.MaximumFrames == 0 || description.MaximumFrames > kOfflinePoolMaximumFrames) {
				throw std::runtime_error("offline buffered pool requires explicit bounded PCM mastering geometry");
			}
			State = std::make_shared<OfflinePoolState>();
			State->Description = description;
			try {
				CreateGraph();
			} catch (...) {
				ReleaseGraph();
				throw;
			}
		}

		~Implementation() {
			if (State->Sources != nullptr) {
				std::terminate();
			}
			ReleaseGraph();
		}

		void RequireSerializedOwner() const {
			if (State->Closed.load() || std::this_thread::get_id() != State->OwnerThread) {
				throw std::runtime_error("offline buffered pool requires its actual live serialized system owner");
			}
		}

		void RequireOwner() const {
			RequireSerializedOwner();
			Boundary.Invoke(OfflineSoundSystemOperation::EngineState, [&] {
				if (!Engine.isInManualRenderingMode || Engine.manualRenderingMode != AVAudioEngineManualRenderingModeOffline || !Engine.isRunning) {
					throw std::runtime_error("offline buffered pool requires its actual running manual offline graph");
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
					// SDK contract: enabling manual mode precedes all mixer/output access that could configure hardware.
					if (Format == nil || Engine == nil || ![Engine enableManualRenderingMode:AVAudioEngineManualRenderingModeOffline format:Format maximumFrameCount:description.MaximumFrames error:&error]) {
						throw PoolAudioFailure("dynamic manual offline engine configuration", error);
					}
					const auto actual = Engine.manualRenderingFormat;
					if (actual == nil || actual.commonFormat != AVAudioPCMFormatFloat32 || actual.isInterleaved || actual.channelCount != description.Channels || actual.sampleRate != description.SamplesPerSecond || Engine.manualRenderingMaximumFrameCount != description.MaximumFrames) {
						throw std::runtime_error("actual offline pool format differs from its explicit PCM description");
					}
					Buffer = [[AVAudioPCMBuffer alloc] initWithPCMFormat:Format frameCapacity:description.MaximumFrames];
					if (Buffer == nil) {
						throw std::runtime_error("actual offline pool output allocation failed");
					}
					[Engine connect:Engine.mainMixerNode to:Engine.outputNode format:Format];
					Engine.mainMixerNode.outputVolume = 1;
					if (![Engine startAndReturnError:&error]) {
						throw PoolAudioFailure("dynamic manual offline engine start", error);
					}
					RequireOwner();
				});
			}
		}

		void RetireNode(std::size_t index) noexcept {
			if (std::this_thread::get_id() != State->OwnerThread) {
				std::terminate();
			}
			auto& slot = Nodes[index];
			if (slot.Source) {
				slot.Source->Closed.store(true);
			}
			@autoreleasepool {
				@try {
					if (slot.Node != nil) {
						// Allocation can precede a failed attach. Retire only an actual engine-owned node.
						if (slot.Node.engine == Engine) {
							[Engine disconnectNodeOutput:slot.Node];
							[Engine detachNode:slot.Node];
						}
						slot.Node = nil;
					}
				} @catch (NSException*) {
					std::terminate();
				}
			}
			slot.Source.reset();
		}

		void ReleaseGraph() noexcept {
			if (std::this_thread::get_id() != State->OwnerThread) {
				std::terminate();
			}
			if (State->Closed.exchange(true)) {
				return;
			}
			@autoreleasepool {
				@try {
					[Engine stop];
					for (std::size_t index = 0; index < Nodes.size(); ++index) {
						RetireNode(index);
					}
					Buffer = nil;
					Format = nil;
					Engine = nil;
				} @catch (NSException*) {
					std::terminate();
				}
			}
		}

		mutable OfflineSoundSystemBoundary Boundary;
		std::shared_ptr<OfflinePoolState> State;
		std::array<OfflinePoolNode, kBufferedNodeCapacity> Nodes;
		AVAudioEngine* Engine = nil;
		AVAudioFormat* Format = nil;
		AVAudioPCMBuffer* Buffer = nil;
	};

	OfflineBufferedSoundPool::OfflineBufferedSoundPool(BufferedSoundSources& actual_sources, OfflineSoundMasteringDescription description) : m_Implementation(std::make_unique<Implementation>(description)) {
		actual_sources.ConnectOutput(*this);
	}

	OfflineBufferedSoundPool::OfflineBufferedSoundPool(BufferedSoundCues& actual_controller, OfflineSoundMasteringDescription description) : m_Implementation(std::make_unique<Implementation>(description)) {
		actual_controller.ConnectOutput(*this);
	}

	OfflineBufferedSoundPool::OfflineBufferedSoundPool(SoundResourceControl& actual_controller, OfflineSoundMasteringDescription description) : m_Implementation(std::make_unique<Implementation>(description)) {
		actual_controller.ConnectOutput(*this);
	}

	OfflineBufferedSoundPool::~OfflineBufferedSoundPool() {
		Close();
	}

	void OfflineBufferedSoundPool::ConnectSourceOwner(BufferedSoundSources& actual_sources) {
		auto& owner = *m_Implementation;
		owner.RequireOwner();
		if (owner.State->Sources != nullptr) {
			throw std::runtime_error("offline pool is already attached to its actual decoded source owner");
		}
		owner.State->Sources = &actual_sources;
	}

	void OfflineBufferedSoundPool::ReleaseSourceOwner(BufferedSoundSources& actual_sources) noexcept {
		auto& owner = *m_Implementation;
		if (std::this_thread::get_id() != owner.State->OwnerThread || owner.State->Sources != &actual_sources || std::any_of(owner.Nodes.begin(), owner.Nodes.end(), [](const auto& slot) {
			    return slot.Node != nil || slot.Source != nullptr;
		    })) {
			std::terminate();
		}
		owner.State->Sources = nullptr;
	}

	void OfflineBufferedSoundPool::AttachSource(SoundVoiceAddress address, BufferedSoundSources& actual_sources) {
		auto& owner = *m_Implementation;
		owner.RequireOwner();
		const auto index = SourceNodeIndex(address);
		const auto source_info = actual_sources.Inspect(address);
		const auto info = source_info.Samples;
		if (owner.State->Sources != &actual_sources || owner.Nodes[index].Node != nil || owner.Nodes[index].Source || !info.Queued || !info.CallbackActive || info.BufferFrames == 0 || info.Channels == 0 || info.Channels > 8 || (info.SamplesPerSecond == 0 || info.SamplesPerSecond > 192000)) {
			throw std::runtime_error("offline attachment requires its actual unused decoded slot and bounded original source rate");
		}
		auto source = std::make_shared<OfflinePoolSource>();
		source->Pool = owner.State;
		source->Address = address;
		source->SourceChannels = info.Channels;
		source->SamplesPerSecond = info.SamplesPerSecond;
		source->Streaming = source_info.Streaming.has_value();
		if (source->Streaming && (!source_info.Streaming->Prefilled || source_info.Streaming->StopRequested || source_info.Streaming->Failed)) {
			throw std::runtime_error("offline streamed attachment requires its actual nonfailed prefetched PCM owner");
		}
		source->SourceWorkspace.resize(static_cast<std::size_t>(owner.State->Description.MaximumFrames) * info.Channels);
		source->OutputWorkspace.resize(static_cast<std::size_t>(owner.State->Description.MaximumFrames) * owner.State->Description.Channels);
		owner.Nodes[index].Source = source;
		@autoreleasepool {
			owner.Boundary.Invoke(OfflineSoundSystemOperation::SourceAttachment, [&] {
				// Native3921a0 passes the original fmt to CreateSourceVoice. Keep the same source-time units here.
				// The already-offline mixer converts this PCM into the explicit mastering rate without a device.
				AVAudioFormat* source_format = [[AVAudioFormat alloc] initWithCommonFormat:AVAudioPCMFormatFloat32 sampleRate:info.SamplesPerSecond channels:owner.State->Description.Channels interleaved:NO];
				if (source_format == nil) {
					throw std::runtime_error("actual offline source-rate PCM format allocation failed");
				}
				const std::weak_ptr<OfflinePoolSource> weak = source;
				AVAudioSourceNode* node = [[AVAudioSourceNode alloc] initWithFormat:source_format
				                                                        renderBlock:^OSStatus(BOOL* silence, const AudioTimeStamp*, AVAudioFrameCount frames, AudioBufferList* output) {
					                                                      return RenderPoolSource(weak.lock(), silence, frames, output);
				                                                        }];
				if (node == nil) {
					throw std::runtime_error("actual offline source node allocation failed");
				}
				owner.Nodes[index].Node = node;
				[owner.Engine attachNode:node];
				[owner.Engine connect:node to:owner.Engine.mainMixerNode format:source_format];
				const auto connected = [node outputFormatForBus:0];
				if (connected == nil || connected.commonFormat != AVAudioPCMFormatFloat32 || connected.isInterleaved || connected.sampleRate != info.SamplesPerSecond || connected.channelCount != owner.State->Description.Channels) {
					throw std::runtime_error("actual offline source connection changed its original source-rate PCM format");
				}
				node.volume = 1;
				node.pan = 0;
			});
		}
	}

	void OfflineBufferedSoundPool::RetireSource(SoundVoiceAddress address) noexcept {
		try {
			m_Implementation->RetireNode(SourceNodeIndex(address));
		} catch (...) {
			std::terminate();
		}
	}

	void OfflineBufferedSoundPool::ApplySourceMix(SoundVoiceAddress address, const SoundMix& actual_mix) {
		auto& owner = *m_Implementation;
		owner.RequireOwner();
		auto& slot = owner.Nodes[SourceNodeIndex(address)];
		if (!slot.Source || slot.Node == nil || actual_mix.SourceChannels != slot.Source->SourceChannels || actual_mix.DestinationChannels != owner.State->Description.Channels || (actual_mix.FrequencyRatio && *actual_mix.FrequencyRatio != 1)) {
			throw std::runtime_error("offline source mix requires its actual matrix geometry and unit unchanged frequency ratio");
		}
		slot.Source->Mix = actual_mix;
	}

	void OfflineBufferedSoundPool::RequireWmaStreaming(const BufferedSoundSources& actual_sources, SoundVoiceAddress address) const {
		auto& owner = *m_Implementation;
		owner.RequireOwner();
		static_cast<void>(SourceNodeIndex(address));
		if (owner.State->Sources != &actual_sources) {
			throw std::runtime_error("offline stream requires its exact reciprocal decoded source owner");
		}
	}

	std::size_t OfflineBufferedSoundPool::Render(std::span<float> output) {
		auto& owner = *m_Implementation;
		owner.RequireOwner();
		if (std::any_of(owner.Nodes.begin(), owner.Nodes.end(), [](const auto& slot) {
			    return slot.Source && slot.Source->Streaming;
		    })) {
			throw std::runtime_error("streamed offline sources require typed render/starvation accounting");
		}
		const auto channels = owner.State->Description.Channels;
		if (output.empty() || output.size() % channels != 0 || output.size() / channels > owner.State->Description.MaximumFrames) {
			throw std::runtime_error("offline pool output exceeds its actual nonempty complete-frame extent");
		}
		@autoreleasepool {
			return owner.Boundary.Invoke(OfflineSoundSystemOperation::Render, [&]() -> std::size_t {
				NSError* error = nil;
				const auto requested = static_cast<AVAudioFrameCount>(output.size() / channels);
				const auto result = [owner.Engine renderOffline:requested toBuffer:owner.Buffer error:&error];
				if (owner.State->SourceFailure) {
					std::rethrow_exception(owner.State->SourceFailure);
				}
				if (owner.State->Failure.load() != PoolCallbackFailure::None || result != AVAudioEngineManualRenderingStatusSuccess) {
					throw PoolAudioFailure("actual dynamic offline source render", error);
				}
				const auto frames = owner.Buffer.frameLength;
				const auto planes = owner.Buffer.floatChannelData;
				if (frames != requested || planes == nullptr) {
					throw std::runtime_error("actual offline pool returned a different Float32 frame extent");
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

	OfflineSoundRenderResult OfflineBufferedSoundPool::RenderTyped(std::span<float> output) {
		auto& owner = *m_Implementation;
		owner.RequireOwner();
		const auto channels = owner.State->Description.Channels;
		if (owner.State->Collecting || output.empty() || output.size() % channels != 0 || output.size() / channels > owner.State->Description.MaximumFrames) {
			throw std::runtime_error("typed offline render requires its bounded nonrecursive actual mastering span");
		}
		for (auto& slot : owner.Nodes) {
			if (slot.Source) {
				slot.Source->Observation = OfflineSoundSourceRender{};
				slot.Source->Observation.Source = slot.Source->Address;
				slot.Source->Observation.SamplesPerSecond = slot.Source->SamplesPerSecond;
				slot.Source->Observation.Streaming = slot.Source->Streaming;
			}
		}
		owner.State->Collecting = true;
		try {
			@autoreleasepool {
				const auto rendered = owner.Boundary.Invoke(OfflineSoundSystemOperation::Render, [&]() -> OfflineSoundRenderResult {
					OfflineSoundRenderResult result;
					const auto before = owner.Engine.manualRenderingSampleTime;
					if (before < 0) {
						throw std::runtime_error("typed offline graph returned a negative actual master clock");
					}
					result.MasterFrameBefore = static_cast<std::uint64_t>(before);
					NSError* error = nil;
					const auto requested = static_cast<AVAudioFrameCount>(output.size() / channels);
					const auto status = [owner.Engine renderOffline:requested toBuffer:owner.Buffer error:&error];
					if (owner.State->SourceFailure) {
						std::rethrow_exception(owner.State->SourceFailure);
					}
					if (owner.State->Failure.load() != PoolCallbackFailure::None || status != AVAudioEngineManualRenderingStatusSuccess) {
						throw PoolAudioFailure("actual typed offline source render", error);
					}
					const auto frames = owner.Buffer.frameLength;
					const auto planes = owner.Buffer.floatChannelData;
					const auto after = owner.Engine.manualRenderingSampleTime;
					if (frames != requested || planes == nullptr || after < before) {
						throw std::runtime_error("typed offline graph returned a different actual output/master clock extent");
					}
					result.MasterFrames = frames;
					result.MasterFrameAfter = static_cast<std::uint64_t>(after);
					for (const auto& slot : owner.Nodes) {
						if (slot.Source) {
							const auto& observation = slot.Source->Observation;
							result.Sources[result.SourceCount++] = observation;
							if (static_cast<std::uint8_t>(observation.Status) > static_cast<std::uint8_t>(result.Status)) {
								result.Status = observation.Status;
							}
						}
					}
					for (std::size_t frame = 0; frame < frames; ++frame) {
						for (std::size_t channel = 0; channel < channels; ++channel) {
							output[frame * channels + channel] = planes[channel][frame];
						}
					}

					return result;
				});
				owner.State->Collecting = false;
				return rendered;
			}
		} catch (...) {
			owner.State->Collecting = false;
			throw;
		}
	}

	OfflineSoundMasteringDescription OfflineBufferedSoundPool::Description() const {
		auto& owner = *m_Implementation;
		owner.RequireOwner();
		return owner.Boundary.Invoke(OfflineSoundSystemOperation::Format, [&]() -> OfflineSoundMasteringDescription {
			const auto format = owner.Engine.manualRenderingFormat;
			OfflineSoundMasteringDescription result;
			result.SamplesPerSecond = static_cast<std::uint32_t>(format.sampleRate);
			result.Channels = static_cast<std::uint8_t>(format.channelCount);
			result.MaximumFrames = owner.Engine.manualRenderingMaximumFrameCount;

			return result;
		});
	}

	std::uint64_t OfflineBufferedSoundPool::RenderedFrames() const {
		auto& owner = *m_Implementation;
		owner.RequireOwner();
		return owner.Boundary.Invoke(OfflineSoundSystemOperation::Timeline, [&]() -> std::uint64_t {
			const auto frames = owner.Engine.manualRenderingSampleTime;
			if (frames < 0) {
				throw std::runtime_error("actual offline pool returned a negative frame timeline");
			}

			return static_cast<std::uint64_t>(frames);
		});
	}

	std::size_t OfflineBufferedSoundPool::SourceNodeCount() const {
		auto& owner = *m_Implementation;
		owner.RequireOwner();
		return owner.Boundary.Invoke(OfflineSoundSystemOperation::EngineState, [&]() -> std::size_t {
			std::size_t count = 0;
			for (const auto& slot : owner.Nodes) {
				if (slot.Node != nil) {
					if (!slot.Source || slot.Node.engine != owner.Engine) {
						throw std::runtime_error("actual source node no longer belongs to its connected offline pool");
					}
					++count;
				}
			}

			return count;
		});
	}

	void OfflineBufferedSoundPool::Close() noexcept {
		auto& owner = *m_Implementation;
		if (std::this_thread::get_id() != owner.State->OwnerThread) {
			std::terminate();
		}
		if (owner.State->Closed.load()) {
			return;
		}
		try {
			if (owner.State->Sources != nullptr) {
				// Physical callbacks retire first. CPU queued/remaining frames and completion remain untouched.
				owner.State->Sources->DisconnectOutput(*this);
			}
		} catch (...) {
			std::terminate();
		}
		owner.ReleaseGraph();
	}
} // namespace sgre::audio
