#include "sgre/audio/buffered_sound_sources.hpp"

#include "sgre/audio/buffered_sound_output.hpp"
#include "sgre/audio/wave_decoder.hpp"
#include "sgre/audio/wma_decoder.hpp"

#include <algorithm>
#include <array>
#include <exception>
#include <limits>
#include <stdexcept>
#include <thread>
#include <utility>

namespace sgre::audio {

	struct BufferedSampleCallback {
		bool Active = true;
	};

	struct BufferedSampleBuffer {
		DecodedAudio Audio;
		std::size_t Cursor = 0;
		std::size_t LoopBegin = 0;
		std::size_t LoopEnd = 0;
		std::uint32_t LoopRepeats = 0;
		std::uint64_t ConsumedFrames = 0;
		bool Queued = true;
		bool Current = false;

		[[nodiscard]] std::size_t Frames() const {
			return Audio.Samples.size() / Audio.Channels;
		}
	};

	struct BufferedSampleVoice {
		// Member destruction also retires source/sample storage before callback storage.
		std::unique_ptr<BufferedSampleCallback> Callback;
		std::unique_ptr<BufferedSampleBuffer> Buffer;
		std::unique_ptr<WmaStreamSource> Stream;
		std::size_t StreamBytes = 0;
		std::uint32_t StreamRepeats = 0;
		bool StreamCurrent = false;
		bool StreamOutputAttached = false;
		std::optional<SoundMix> StreamMix;
		std::optional<bool> PendingRunning;
		bool Running = false;
	};

	static std::size_t BufferedSourceIndex(SoundVoiceAddress address) {
		if (address.Type == SoundVoiceType::Buffered32 && address.Slot < 32) {
			return address.Slot;
		}
		if (address.Type == SoundVoiceType::Buffered8 && address.Slot < 8) {
			return 32 + address.Slot;
		}

		throw std::runtime_error("decoded buffered source address requires a real pool0/1 slot");
	}

	static SoundVoiceAddress BufferedSourceAddress(std::size_t index) {
		SoundVoiceAddress address;
		address.Type = index < 32 ? SoundVoiceType::Buffered32 : SoundVoiceType::Buffered8;
		address.Slot = static_cast<std::uint8_t>(index < 32 ? index : index - 32);

		return address;
	}

	static DecodedAudio DecodeBufferedSamples(const BufferedSoundSourceData& data, std::optional<std::uint16_t> verified_wma_encode_options, std::size_t limit) {
		if (data.Description.Format.Encoding == resource::WaveEncoding::Pcm || data.Description.Format.Encoding == resource::WaveEncoding::MicrosoftAdpcm) {
			return DecodeWaveAudio(data.Description.Format, data.EncodedBytes, limit);
		}
		if (data.Description.Format.Encoding == resource::WaveEncoding::WmaStandard) {
			if (!verified_wma_encode_options || *verified_wma_encode_options != 31) {
				throw std::runtime_error("decoded WMA source requires the explicitly verified options31 profile");
			}

			WmaDecodeLimits limits;
			limits.MaxDecodedBytes = limit;

			return DecodeWmaStandardAudio(data.Description.Format, data.EncodedBytes, data.Description.DecodedPacketCumulativeBytes, *verified_wma_encode_options, limits);
		}

		throw std::runtime_error("decoded buffered source does not admit WMA Professional or unrecovered encodings");
	}

	static void PrepareBufferLoop(BufferedSampleBuffer& buffer, const SoundSubmission& submission) {
		if (submission.LoopRepeats > 255) {
			throw std::runtime_error("decoded buffered source loop count exceeds the native finite/infinite field domain");
		}

		buffer.LoopRepeats = submission.LoopRepeats;
		if (buffer.LoopRepeats == 0) {
			if (submission.LoopBegin != 0 || submission.LoopLength != 0) {
				throw std::runtime_error("decoded zero-repeat source requires zero native loop fields");
			}

			return;
		}

		const auto frames = buffer.Frames();
		buffer.LoopBegin = submission.LoopBegin;
		if (submission.LoopLength == 0) {
			if (buffer.LoopBegin != 0) {
				throw std::runtime_error("decoded whole-buffer loop requires zero loop begin");
			}

			buffer.LoopEnd = frames;
		} else {
			if (buffer.LoopBegin >= frames || submission.LoopLength > frames - buffer.LoopBegin) {
				throw std::runtime_error("decoded source loop region exceeds its actual sample buffer");
			}

			buffer.LoopEnd = buffer.LoopBegin + submission.LoopLength;
		}
	}

	class BufferedSoundSources::Implementation final : public SoundVoiceOperations {
	public:
		explicit Implementation(BufferedSoundSourceLimits limits) : Limits(limits), OwnerThread(std::this_thread::get_id()) {
			if (limits.MaxSourceDecodedBytes == 0 || limits.MaxSourceDecodedBytes > 256 * 1024 * 1024 || limits.MaxTotalDecodedBytes == 0 || limits.MaxTotalDecodedBytes > 512 * 1024 * 1024) {
				throw std::runtime_error("decoded buffered sources require bounded positive sample-storage limits");
			}
		}

		BufferedSoundOutput* Output = nullptr;
		BufferedSoundSourceLimits Limits;
		const std::thread::id OwnerThread;
		std::unique_ptr<WmaStreamWorker> StreamWorker;
		std::array<BufferedSampleVoice, 40> Voices;
		SoundVoiceSchedule Schedule;
		std::size_t SampleBytes = 0;
		bool Closed = false;

		void RequireOwner() const {
			if (Closed || std::this_thread::get_id() != OwnerThread) {
				throw std::runtime_error("decoded buffered source operation requires its live serialized owner");
			}
		}

		void BufferEnded(SoundVoiceAddress address, BufferedSampleVoice& voice) {
			voice.Buffer->Queued = false;
			voice.Buffer->Current = false;
			voice.Callback->Active = false;
			Schedule.BufferEnded(address);
		}

		void Retire(SoundVoiceAddress address) {
			auto& voice = Voices[BufferedSourceIndex(address)];
			PollStream(address, voice);
			if (voice.Stream && !voice.Stream->CanRetire()) {
				throw std::runtime_error("streaming source reuse requires actual callback completion and decoder quiescence");
			}
			if (Output != nullptr) {
				Output->RetireSource(address);
			}
			voice.Running = false;
			if (voice.Stream) {
				voice.Stream->Close();
				voice.Stream.reset();
				SampleBytes -= voice.StreamBytes;
				voice.StreamBytes = 0;
				voice.StreamRepeats = 0;
				voice.StreamCurrent = false;
				voice.StreamOutputAttached = false;
				voice.StreamMix.reset();
			}
			if (voice.Buffer) {
				BufferEnded(address, voice);
				SampleBytes -= voice.Buffer->Audio.Samples.size() * sizeof(float);
				voice.Buffer.reset();
			}
			// Native392580 destroys source before callback; no borrowed archive bytes or queued callback survive here.
			voice.Callback.reset();
			voice.PendingRunning.reset();
			Schedule.Remove(address);
		}

		void PollStream(SoundVoiceAddress address, BufferedSampleVoice& voice) {
			if (voice.Stream) {
				voice.Stream->Poll();
				if (!voice.Stream->Inspect().CallbackActive) {
					voice.Callback->Active = false;
					Schedule.BufferEnded(address);
				}
			}
		}

		void Start(SoundVoiceAddress address, std::uint32_t operation_set) override {
			if (operation_set == 0) {
				throw std::runtime_error("decoded source scheduled start requires the nonzero operation set");
			}

			Voices[BufferedSourceIndex(address)].PendingRunning = true;
		}

		void Stop(SoundVoiceAddress address, std::uint32_t operation_set) override {
			if (operation_set == 0) {
				throw std::runtime_error("decoded source scheduled stop requires the nonzero operation set");
			}

			Voices[BufferedSourceIndex(address)].PendingRunning = false;
		}

		void CommitAll() override {
			for (auto& voice : Voices) {
				if (voice.PendingRunning) {
					voice.Running = *voice.PendingRunning;
					if (voice.Stream) {
						if (voice.Running) {
							voice.Stream->Resume();
						} else {
							voice.Stream->Pause();
						}
					}
					voice.PendingRunning.reset();
				}
			}
		}

		bool CurrentBufferContextPresent(SoundVoiceAddress address) override {
			static_cast<void>(BufferedSourceIndex(address));
			// Native buffered XAUDIO2_BUFFER.pContext is null, even with queued/current real samples.
			return false;
		}

		void Flush(SoundVoiceAddress address) override {
			auto& voice = Voices[BufferedSourceIndex(address)];
			if (voice.Stream && (!voice.Running || !voice.StreamCurrent)) {
				voice.Stream->RequestStop();
				PollStream(address, voice);
			}
			if (voice.Buffer && voice.Buffer->Queued && (!voice.Running || !voice.Buffer->Current)) {
				voice.Buffer->Cursor = voice.Buffer->Frames();
				BufferEnded(address, voice);
			}
		}
	};

	BufferedSoundSources::BufferedSoundSources(BufferedSoundSourceLimits limits) : m_Implementation(std::make_unique<Implementation>(limits)) {
	}

	BufferedSoundSources::~BufferedSoundSources() {
		try {
			Close();
		} catch (...) {
			std::terminate();
		}
	}

	void BufferedSoundSources::ConnectOutput(BufferedSoundOutput& actual_output) {
		auto& state = *m_Implementation;
		state.RequireOwner();
		if (state.Output != nullptr || std::any_of(state.Voices.begin(), state.Voices.end(), [](const auto& voice) {
			    return voice.Buffer != nullptr || voice.Callback != nullptr;
		    })) {
			throw std::runtime_error("buffered source output requires its fresh actual decoded pool before preparation");
		}
		actual_output.ConnectSourceOwner(*this);
		state.Output = &actual_output;
	}

	void BufferedSoundSources::DisconnectOutput(BufferedSoundOutput& actual_output) {
		auto& state = *m_Implementation;
		state.RequireOwner();
		if (state.Output != &actual_output) {
			throw std::runtime_error("buffered output disconnect requires its exact connected owner");
		}
		for (std::size_t index = 0; index < state.Voices.size(); ++index) {
			actual_output.RetireSource(BufferedSourceAddress(index));
			state.Voices[index].StreamOutputAttached = false;
		}
		state.Output = nullptr;
		actual_output.ReleaseSourceOwner(*this);
	}

	void BufferedSoundSources::ApplyOutputMix(SoundVoiceAddress address, const SoundMix& actual_mix) {
		auto& state = *m_Implementation;
		state.RequireOwner();
		auto& voice = state.Voices[BufferedSourceIndex(address)];
		const auto source_channels = voice.Buffer ? voice.Buffer->Audio.Channels : voice.Stream ? voice.Stream->Inspect().Channels
		                                                                                        : 0;
		if (source_channels == 0 || actual_mix.SourceChannels != source_channels || actual_mix.DestinationChannels == 0 || actual_mix.DestinationChannels > 8) {
			throw std::runtime_error("buffered output matrix requires its actual decoded source channel count");
		}
		if (voice.Stream) {
			voice.StreamMix = actual_mix;
		}
		if (state.Output != nullptr && (!voice.Stream || voice.StreamOutputAttached)) {
			state.Output->ApplySourceMix(address, actual_mix);
		}
	}

	void BufferedSoundSources::Prepare(SoundVoiceAddress address, const BufferedSoundSourceData& data, std::optional<std::uint16_t> verified_wma_encode_options) {
		auto& state = *m_Implementation;
		state.RequireOwner();
		const auto index = BufferedSourceIndex(address);
		if (data.Pool != address.Type || data.Submission.EncodedBytes != data.EncodedBytes.size() || data.Description.EncodedData.Size != data.EncodedBytes.size() || !data.Submission.EndOfStream) {
			throw std::runtime_error("decoded source preparation requires the matching actual buffered data/submission");
		}

		state.Retire(address);
		const auto available = std::min(state.Limits.MaxSourceDecodedBytes, state.Limits.MaxTotalDecodedBytes - state.SampleBytes);
		if (available == 0) {
			throw std::runtime_error("decoded source aggregate sample-storage budget is exhausted");
		}

		auto audio = DecodeBufferedSamples(data, verified_wma_encode_options, available);
		if (audio.Samples.empty() || audio.Channels == 0 || audio.Samples.size() % audio.Channels != 0) {
			throw std::runtime_error("decoded source requires a real nonempty complete-frame sample buffer");
		}

		auto buffer = std::make_unique<BufferedSampleBuffer>();
		buffer->Audio = std::move(audio);
		PrepareBufferLoop(*buffer, data.Submission);
		auto callback = std::make_unique<BufferedSampleCallback>();
		state.SampleBytes += buffer->Audio.Samples.size() * sizeof(float);
		auto& voice = state.Voices[index];
		voice.Callback = std::move(callback);
		voice.Buffer = std::move(buffer);
		state.Schedule.Register(address);
		try {
			if (state.Output != nullptr) {
				state.Output->AttachSource(address, *this);
			}
		} catch (...) {
			state.Retire(address);
			throw;
		}
	}

	void BufferedSoundSources::RequestPause(SoundVoiceAddress address) {
		m_Implementation->RequireOwner();
		static_cast<void>(BufferedSourceIndex(address));
		m_Implementation->Schedule.RequestPause(address);
	}

	void BufferedSoundSources::PrepareWmaStreaming(SoundVoiceAddress address, const BufferedSoundSourceData& data, std::uint16_t verified_encode_options) {
		auto& state = *m_Implementation;
		state.RequireOwner();
		const auto index = BufferedSourceIndex(address);
		if (data.Pool != address.Type) {
			throw std::runtime_error("WMA streaming requires its exact actual source pool slot");
		}
		if (state.Output != nullptr) {
			state.Output->RequireWmaStreaming(*this, address);
		}
		state.Retire(address);
		const auto available = std::min(state.Limits.MaxSourceDecodedBytes, state.Limits.MaxTotalDecodedBytes - state.SampleBytes);
		const auto channels = data.Description.Format.Channels;
		if (channels == 0 || state.Limits.Streaming.BufferFrames > available / (static_cast<std::size_t>(channels) * sizeof(float))) {
			throw std::runtime_error("WMA streaming ring exceeds the actual source sample-storage budget");
		}
		if (!state.StreamWorker) {
			state.StreamWorker = std::make_unique<WmaStreamWorker>(state.Limits.Streaming);
		}
		auto callback = std::make_unique<BufferedSampleCallback>();
		auto stream = state.StreamWorker->Create(data, verified_encode_options);
		stream->Pause();
		auto& voice = state.Voices[index];
		voice.Callback = std::move(callback);
		voice.Stream = std::move(stream);
		voice.StreamRepeats = data.Submission.LoopRepeats;
		voice.StreamBytes = state.Limits.Streaming.BufferFrames * channels * sizeof(float);
		state.SampleBytes += voice.StreamBytes;
		state.Schedule.Register(address);
	}

	void BufferedSoundSources::PollWmaStreams() {
		auto& state = *m_Implementation;
		state.RequireOwner();
		for (std::size_t index = 0; index < state.Voices.size(); ++index) {
			state.PollStream(BufferedSourceAddress(index), state.Voices[index]);
		}
	}

	BufferedSoundStreamPreparation BufferedSoundSources::CompleteWmaStreamPreparation(SoundVoiceAddress address) {
		auto& state = *m_Implementation;
		state.RequireOwner();
		auto& voice = state.Voices[BufferedSourceIndex(address)];
		if (!voice.Stream) {
			throw std::runtime_error("stream preparation completion requires the actual incremental source owner");
		}
		state.PollStream(address, voice);
		voice.Stream->RethrowWorkerFailure();
		const auto info = voice.Stream->Inspect();
		if (info.StopRequested || !info.CallbackActive) {
			return BufferedSoundStreamPreparation::Stopped;
		}
		if (!info.Prefilled) {
			return BufferedSoundStreamPreparation::Prefilling;
		}
		if (state.Output != nullptr && !voice.StreamOutputAttached) {
			if (!voice.StreamMix) {
				throw std::runtime_error("stream output preparation requires its actual retained source matrix");
			}
			state.Output->RequireWmaStreaming(*this, address);
			try {
				state.Output->AttachSource(address, *this);
				state.Output->ApplySourceMix(address, *voice.StreamMix);
			} catch (...) {
				state.Output->RetireSource(address);
				throw;
			}
			voice.StreamOutputAttached = true;
		}

		return BufferedSoundStreamPreparation::Prepared;
	}

	void BufferedSoundSources::ReleaseWmaStreamInputBorrow(SoundVoiceAddress address) {
		auto& state = *m_Implementation;
		state.RequireOwner();
		auto& voice = state.Voices[BufferedSourceIndex(address)];
		if (voice.Stream) {
			state.PollStream(address, voice);
			voice.Stream->ReleaseInputBorrow();
		}
	}

	void BufferedSoundSources::RethrowWmaStreamFailure(SoundVoiceAddress address) const {
		const auto& state = *m_Implementation;
		state.RequireOwner();
		const auto& voice = state.Voices[BufferedSourceIndex(address)];
		if (voice.Stream) {
			voice.Stream->RethrowWorkerFailure();
		}
	}

	void BufferedSoundSources::RequestResume(SoundVoiceAddress address) {
		m_Implementation->RequireOwner();
		static_cast<void>(BufferedSourceIndex(address));
		m_Implementation->Schedule.RequestResume(address);
	}

	void BufferedSoundSources::RequestStop(SoundVoiceAddress address) {
		m_Implementation->RequireOwner();
		static_cast<void>(BufferedSourceIndex(address));
		m_Implementation->Schedule.RequestStop(address);
	}

	void BufferedSoundSources::Commit() {
		m_Implementation->RequireOwner();
		m_Implementation->Schedule.Commit(*m_Implementation);
	}

	std::size_t BufferedSoundSources::ConsumeFrames(SoundVoiceAddress address, std::span<float> destination) {
		auto& state = *m_Implementation;
		state.RequireOwner();
		auto& voice = state.Voices[BufferedSourceIndex(address)];
		if (voice.Stream) {
			throw std::runtime_error("WMA streaming consumption requires the typed prefill/starvation/terminal result");
		}
		if (!voice.Buffer) {
			throw std::runtime_error("decoded source consumption requires its actual prepared sample owner");
		}

		auto& buffer = *voice.Buffer;
		const auto channels = buffer.Audio.Channels;
		if (destination.size() % channels != 0) {
			throw std::runtime_error("decoded source output must contain complete interleaved frames");
		}

		if (!voice.Running || !buffer.Queued) {
			return 0;
		}

		std::size_t written = 0;
		const auto requested = destination.size() / channels;
		while (written < requested) {
			const auto endpoint = buffer.LoopRepeats != 0 ? buffer.LoopEnd : buffer.Frames();
			const auto frames = std::min(requested - written, endpoint - buffer.Cursor);
			if (frames > std::numeric_limits<std::uint64_t>::max() - buffer.ConsumedFrames) {
				throw std::runtime_error("decoded source consumed-frame counter exceeds its explicit range");
			}

			std::copy_n(buffer.Audio.Samples.begin() + static_cast<std::ptrdiff_t>(buffer.Cursor * channels), frames * channels, destination.begin() + static_cast<std::ptrdiff_t>(written * channels));
			buffer.Cursor += frames;
			buffer.ConsumedFrames += frames;
			buffer.Current = true;
			written += frames;
			if (buffer.LoopRepeats != 0 && buffer.Cursor == buffer.LoopEnd) {
				if (buffer.LoopRepeats != 255) {
					--buffer.LoopRepeats;
				}
				buffer.Cursor = buffer.LoopBegin;
			} else if (buffer.Cursor == buffer.Frames()) {
				state.BufferEnded(address, voice);
				break;
			}
		}

		return written;
	}

	WmaStreamReadResult BufferedSoundSources::ConsumeWmaStreamFrames(SoundVoiceAddress address, std::span<float> destination) noexcept {
		auto& state = *m_Implementation;
		if (std::this_thread::get_id() != state.OwnerThread || state.Closed || !((address.Type == SoundVoiceType::Buffered32 && address.Slot < 32) || (address.Type == SoundVoiceType::Buffered8 && address.Slot < 8))) {
			return {};
		}
		const auto index = static_cast<std::size_t>(address.Slot) + (address.Type == SoundVoiceType::Buffered32 ? 0U : 32U);
		auto& voice = state.Voices[index];
		if (!voice.Stream) {
			return {};
		}
		const auto result = voice.Stream->ConsumeFrames(destination);
		voice.StreamCurrent = voice.StreamCurrent || result.Frames != 0;
		if (voice.Stream->CanRetire()) {
			voice.Callback->Active = false;
			state.Schedule.BufferEnded(address);
		}

		return result;
	}

	BufferedSoundSourceInfo BufferedSoundSources::Inspect(SoundVoiceAddress address) const {
		const auto& state = *m_Implementation;
		state.RequireOwner();
		const auto& voice = state.Voices[BufferedSourceIndex(address)];
		BufferedSoundSourceInfo info;
		info.RecoveredSchedule = state.Schedule.Inspect(address);
		if (voice.Buffer) {
			info.Samples.SamplesPerSecond = voice.Buffer->Audio.SamplesPerSecond;
			info.Samples.Channels = voice.Buffer->Audio.Channels;
			info.Samples.BufferFrames = voice.Buffer->Frames();
			info.Samples.CursorFrame = voice.Buffer->Cursor;
			info.Samples.RemainingBufferFrames = voice.Buffer->Queued ? voice.Buffer->Frames() - voice.Buffer->Cursor : 0;
			info.Samples.LoopRepeatsRemaining = voice.Buffer->LoopRepeats;
			info.Samples.ConsumedFrames = voice.Buffer->ConsumedFrames;
			info.Samples.Queued = voice.Buffer->Queued;
			info.Samples.Running = voice.Running;
			info.Samples.CallbackActive = voice.Callback->Active;
		}
		if (voice.Stream) {
			info.Streaming = voice.Stream->Inspect();
			info.StreamingOutputAttached = voice.StreamOutputAttached;
			const auto& stream = *info.Streaming;
			info.Samples.SamplesPerSecond = stream.SamplesPerSecond;
			info.Samples.Channels = stream.Channels;
			info.Samples.BufferFrames = static_cast<std::size_t>(stream.FramesPerPlay);
			info.Samples.CursorFrame = static_cast<std::size_t>(stream.ConsumedFrames % stream.FramesPerPlay);
			if (!stream.CallbackActive && stream.InputComplete) {
				info.Samples.CursorFrame = info.Samples.BufferFrames;
			}
			info.Samples.RemainingBufferFrames = stream.CallbackActive ? info.Samples.BufferFrames - info.Samples.CursorFrame : 0;
			info.Samples.LoopRepeatsRemaining = voice.StreamRepeats == 255 ? 255 : static_cast<std::uint32_t>(voice.StreamRepeats - std::min<std::uint64_t>(voice.StreamRepeats, stream.ConsumedFrames / stream.FramesPerPlay));
			info.Samples.ConsumedFrames = stream.ConsumedFrames;
			info.Samples.Queued = stream.QueuedFrames != 0;
			info.Samples.Running = voice.Running;
			info.Samples.CallbackActive = voice.Callback->Active;
		}

		return info;
	}

	std::size_t BufferedSoundSources::DecodedBytes() const {
		m_Implementation->RequireOwner();

		return m_Implementation->SampleBytes;
	}

	void BufferedSoundSources::Retire(SoundVoiceAddress address) {
		m_Implementation->RequireOwner();
		m_Implementation->Retire(address);
	}

	void BufferedSoundSources::Close() {
		auto& state = *m_Implementation;
		if (state.Closed) {
			return;
		}

		state.RequireOwner();
		for (std::size_t index = 0; index < state.Voices.size(); ++index) {
			state.Retire(BufferedSourceAddress(index));
		}
		if (state.Output != nullptr) {
			auto* output = state.Output;
			state.Output = nullptr;
			output->ReleaseSourceOwner(*this);
		}
		if (state.StreamWorker) {
			state.StreamWorker->Close();
		}
		state.Closed = true;
	}

} // namespace sgre::audio
