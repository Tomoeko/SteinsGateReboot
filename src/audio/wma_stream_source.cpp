#include "sgre/audio/wma_stream_source.hpp"

#include "sgre/audio/wma_packet.hpp"
#include "sgre/audio/wma_stream_decoder.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <exception>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <utility>
#include <vector>

namespace sgre::audio {

	static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
	static_assert(std::atomic<std::uint32_t>::is_always_lock_free);
	static_assert(std::atomic<bool>::is_always_lock_free);
	static constexpr std::size_t kMaximumStreamSources = 40;
	static constexpr std::size_t kMaximumRingFrames = 262144;

	struct WmaStreamSource::State {
		resource::WaveFormat Format;
		std::span<const std::uint8_t> Encoded;
		std::vector<std::uint32_t> Ledger;
		std::vector<float> Ring;
		const std::size_t Capacity;
		const std::size_t Prefill;
		const std::uint16_t Options;
		std::uint32_t Repeats;
		std::unique_ptr<WmaStandardStreamDecoder> Decoder;
		std::span<const float> Pending;
		std::size_t PendingOffset = 0;
		std::size_t Packet = 0;
		std::uint64_t Written = 0;
		std::exception_ptr Failure;
		std::atomic<std::uint64_t> Published = 0;
		std::atomic<std::uint64_t> Consumed = 0;
		std::atomic<std::uint32_t> DecodedPackets = 0;
		std::atomic<bool> Cancel = false;
		std::atomic<bool> Quiescent = false;
		std::atomic<bool> InputComplete = false;
		std::atomic<bool> Failed = false;

		State(const BufferedSoundSourceData& data, std::uint16_t options, WmaStreamSourceLimits limits)
		    : Format(data.Description.Format), Encoded(data.EncodedBytes), Ledger(data.Description.DecodedPacketCumulativeBytes), Ring(limits.BufferFrames * Format.Channels), Capacity(limits.BufferFrames), Prefill(limits.PrefillFrames), Options(options), Repeats(data.Submission.LoopRepeats) {
		}

		void Finish(bool input_complete) noexcept {
			Pending = {};
			Decoder.reset();
			InputComplete.store(input_complete, std::memory_order_relaxed);
			// No subsequent worker step reads encoded/ledger storage or writes ring samples after this release.
			Quiescent.store(true, std::memory_order_release);
		}

		bool Step() noexcept {
			if (Quiescent.load(std::memory_order_relaxed)) {
				return false;
			}
			if (Cancel.load(std::memory_order_acquire)) {
				Finish(false);
				return true;
			}

			try {
				if (Decoder && PendingOffset == Pending.size() && Decoder->Complete()) {
					Pending = {};
					PendingOffset = 0;
					if (Repeats == 0) {
						Finish(true);
						return true;
					}
					if (Repeats != 255) {
						--Repeats;
					}
					Decoder.reset();
					Packet = 0;
					return true;
				}
				const auto consumed = Consumed.load(std::memory_order_acquire);
				if (Written < consumed || Written - consumed > Capacity) {
					throw std::runtime_error("WMA stream ring cursor ordering differs");
				}
				const auto available = Capacity - static_cast<std::size_t>(Written - consumed);
				if (available == 0) {
					return false;
				}
				if (!Decoder) {
					Decoder = std::make_unique<WmaStandardStreamDecoder>(Format, Options, static_cast<std::uint32_t>(Ledger.size()), Ledger.back());
				}
				if (PendingOffset == Pending.size()) {
					Pending = {};
					PendingOffset = 0;
					Pending = Decoder->DecodePacket(Encoded.subspan(Packet * Format.BlockAlignment, Format.BlockAlignment), Ledger[Packet]);
					++Packet;
					DecodedPackets.store(static_cast<std::uint32_t>(Packet), std::memory_order_relaxed);
				}
				if (Cancel.load(std::memory_order_acquire)) {
					Finish(false);
					return true;
				}
				const auto frames = std::min(available, (Pending.size() - PendingOffset) / Format.Channels);
				if (frames == 0) {
					return true;
				}
				if (frames > std::numeric_limits<std::uint64_t>::max() - Written) {
					throw std::runtime_error("WMA stream produced-frame counter exceeds its explicit range");
				}
				const auto start = static_cast<std::size_t>(Written % Capacity);
				const auto first = std::min(frames, Capacity - start);
				std::copy_n(Pending.begin() + static_cast<std::ptrdiff_t>(PendingOffset), first * Format.Channels, Ring.begin() + static_cast<std::ptrdiff_t>(start * Format.Channels));
				std::copy_n(Pending.begin() + static_cast<std::ptrdiff_t>(PendingOffset + first * Format.Channels), (frames - first) * Format.Channels, Ring.begin());
				PendingOffset += frames * Format.Channels;
				Written += frames;
				Published.store(Written, std::memory_order_release);
				return true;
			} catch (...) {
				Failure = std::current_exception();
				Failed.store(true, std::memory_order_relaxed);
				Finish(false);
				return true;
			}
		}
	};

	class WmaStreamWorker::Implementation {
	public:
		explicit Implementation(WmaStreamSourceLimits limits) : Limits(limits) {
			if (limits.BufferFrames == 0 || limits.BufferFrames > kMaximumRingFrames || limits.PrefillFrames == 0 || limits.PrefillFrames > limits.BufferFrames || limits.MaxSources == 0 || limits.MaxSources > kMaximumStreamSources) {
				throw std::runtime_error("WMA streaming requires bounded positive source/ring/prefill capacities");
			}
			Worker = std::thread([this] {
				Run();
			});
		}

		~Implementation() noexcept {
			try {
				StopAndJoin();
			} catch (...) {
				std::terminate();
			}
		}

		const WmaStreamSourceLimits Limits;
		const std::thread::id Owner = std::this_thread::get_id();
		std::mutex Mutex;
		std::condition_variable Wake;
		std::array<std::shared_ptr<WmaStreamSource::State>, kMaximumStreamSources> Sources;
		std::atomic<bool> Stopping = false;
		bool Closed = false;
		// The destructor joins explicitly before real jobs or synchronization members can be destroyed.
		std::thread Worker;

		void StopAndJoin() {
			Stopping.store(true, std::memory_order_release);
			Wake.notify_one();
			if (Worker.joinable()) {
				Worker.join();
			}
		}

		void RequireOwner() const {
			if (Closed || std::this_thread::get_id() != Owner) {
				throw std::runtime_error("WMA stream worker requires its live serialized source owner");
			}
		}

		void Run() noexcept {
			while (!Stopping.load(std::memory_order_acquire)) {
				std::array<std::shared_ptr<WmaStreamSource::State>, kMaximumStreamSources> sources;
				{
					const std::lock_guard lock(Mutex);
					sources = Sources;
				}
				bool progress = false;
				for (const auto& source : sources) {
					if (source) {
						progress = source->Step() || progress;
					}
				}
				if (!progress) {
					std::unique_lock lock(Mutex);
					// Authored host polling bounds a full-ring wait without any callback-side synchronization.
					Wake.wait_for(lock, std::chrono::milliseconds(2));
				}
			}
		}
	};

	WmaStreamWorker::WmaStreamWorker(WmaStreamSourceLimits limits) : m_Implementation(std::make_unique<Implementation>(limits)) {
	}

	WmaStreamWorker::~WmaStreamWorker() noexcept {
		try {
			Close();
		} catch (...) {
			std::terminate();
		}
	}

	std::unique_ptr<WmaStreamSource> WmaStreamWorker::Create(const BufferedSoundSourceData& data, std::uint16_t verified_encode_options) {
		auto& state = *m_Implementation;
		state.RequireOwner();
		const auto& format = data.Description.Format;
		if ((data.Pool != SoundVoiceType::Buffered32 && data.Pool != SoundVoiceType::Buffered8) || format.Encoding != resource::WaveEncoding::WmaStandard || verified_encode_options != 31 || format.Channels == 0 || format.Channels > 2 || !data.Submission.EndOfStream || data.Submission.EncodedBytes != data.EncodedBytes.size() || data.Description.EncodedData.Size != data.EncodedBytes.size() || data.Submission.LoopBegin != 0 || data.Submission.LoopLength != 0 || data.Submission.LoopRepeats > 255) {
			throw std::runtime_error("WMA stream requires actual options31 compressed EOS data with recovered whole-buffer loop fields");
		}
		const auto headers = ReadWmaPacketHeaders(format, data.EncodedBytes, data.Description.DecodedPacketCumulativeBytes);
		if (headers.empty() || data.Description.DecodedPacketCumulativeBytes.back() == 0) {
			throw std::runtime_error("WMA stream requires real nonempty decoded source frames");
		}
		std::unique_ptr<WmaStreamSource> result;
		{
			const std::lock_guard lock(state.Mutex);
			const auto last = state.Sources.begin() + static_cast<std::ptrdiff_t>(state.Limits.MaxSources);
			const auto free = std::find(state.Sources.begin(), last, nullptr);
			if (free == last) {
				throw std::runtime_error("WMA stream worker source capacity is exhausted");
			}
			auto job = std::make_shared<WmaStreamSource::State>(data, verified_encode_options, state.Limits);
			result = std::make_unique<WmaStreamSource>(WmaStreamSource::Admission{}, *this, job);
			*free = std::move(job);
		}
		state.Wake.notify_one();

		return result;
	}

	bool WmaStreamWorker::CanClose() const {
		const auto& state = *m_Implementation;
		if (std::this_thread::get_id() != state.Owner) {
			return false;
		}
		const std::lock_guard lock(m_Implementation->Mutex);

		return std::none_of(state.Sources.begin(), state.Sources.end(), [](const auto& source) {
			return source != nullptr;
		});
	}

	void WmaStreamWorker::Close() {
		auto& state = *m_Implementation;
		if (!CanClose()) {
			throw std::runtime_error("WMA stream worker shutdown requires every genuine source to retire first");
		}
		if (state.Closed) {
			return;
		}
		state.StopAndJoin();
		state.Closed = true;
	}

	void WmaStreamWorker::Retire(WmaStreamSource& source) {
		auto& state = *m_Implementation;
		state.RequireOwner();
		const std::lock_guard lock(state.Mutex);
		const auto found = std::find(state.Sources.begin(), state.Sources.end(), source.m_State);
		if (found == state.Sources.end()) {
			throw std::runtime_error("WMA stream retirement requires this exact registered source owner");
		}
		found->reset();
	}

	WmaStreamSource::WmaStreamSource(Admission, WmaStreamWorker& actual_worker, std::shared_ptr<State> state) : m_Worker(actual_worker), m_State(std::move(state)) {
	}

	WmaStreamSource::~WmaStreamSource() noexcept {
		try {
			Close();
		} catch (...) {
			std::terminate();
		}
	}

	void WmaStreamSource::RequireOwner() const {
		if (std::this_thread::get_id() != m_OwnerThread || m_Closed) {
			throw std::runtime_error("WMA stream source requires its live serialized consume/control owner");
		}
	}

	void WmaStreamSource::ObserveCompletion() noexcept {
		const bool quiescent = m_State->Quiescent.load(std::memory_order_acquire);
		// Acquire quiescence before the final cursor, so an earlier cursor cannot fake complete consumption.
		const auto published = m_State->Published.load(std::memory_order_acquire);
		if (quiescent && (m_StopRequested || m_State->Failed.load(std::memory_order_relaxed))) {
			m_ReadFrames = published;
			m_State->Consumed.store(m_ReadFrames, std::memory_order_release);
			m_CallbackActive = false;
			return;
		}
		if (published - m_ReadFrames >= m_State->Prefill || (quiescent && m_State->InputComplete.load(std::memory_order_relaxed))) {
			m_Prefilled = true;
		}
		if (quiescent && m_State->InputComplete.load(std::memory_order_relaxed) && m_ReadFrames == published) {
			m_CallbackActive = false;
		}
	}

	void WmaStreamSource::Poll() {
		RequireOwner();
		ObserveCompletion();
	}

	void WmaStreamSource::Pause() {
		RequireOwner();
		m_Running = false;
	}

	void WmaStreamSource::Resume() {
		RequireOwner();
		if (!m_StopRequested) {
			m_Running = true;
		}
	}

	void WmaStreamSource::RequestStop() {
		RequireOwner();
		m_StopRequested = true;
		m_Running = false;
		m_State->Cancel.store(true, std::memory_order_release);
		ObserveCompletion();
	}

	WmaStreamReadResult WmaStreamSource::ConsumeFrames(std::span<float> destination) noexcept {
		WmaStreamReadResult result;
		if (std::this_thread::get_id() != m_OwnerThread || m_Closed || destination.size() % m_State->Format.Channels != 0) {
			return result;
		}
		ObserveCompletion();
		result.InputComplete = m_State->Quiescent.load(std::memory_order_acquire) && m_State->InputComplete.load(std::memory_order_relaxed);
		if (m_State->Quiescent.load(std::memory_order_acquire) && m_State->Failed.load(std::memory_order_relaxed)) {
			result.Status = WmaStreamReadStatus::Failed;
		} else if (m_StopRequested) {
			result.Status = m_CallbackActive ? WmaStreamReadStatus::StopPending : WmaStreamReadStatus::Cancelled;
		} else if (!m_CallbackActive) {
			result.Status = WmaStreamReadStatus::Finished;
		} else if (!m_Running) {
			result.Status = WmaStreamReadStatus::Paused;
		} else if (!m_Prefilled) {
			result.Status = WmaStreamReadStatus::Prefilling;
		} else {
			if (destination.empty()) {
				result.Status = WmaStreamReadStatus::Samples;
				return result;
			}
			const auto published = m_State->Published.load(std::memory_order_acquire);
			const auto frames = std::min(destination.size() / m_State->Format.Channels, static_cast<std::size_t>(published - m_ReadFrames));
			if (frames > std::numeric_limits<std::uint64_t>::max() - m_ConsumedFrames) {
				return result;
			}
			const auto start = static_cast<std::size_t>(m_ReadFrames % m_State->Capacity);
			const auto first = std::min(frames, m_State->Capacity - start);
			std::copy_n(m_State->Ring.begin() + static_cast<std::ptrdiff_t>(start * m_State->Format.Channels), first * m_State->Format.Channels, destination.begin());
			std::copy_n(m_State->Ring.begin(), (frames - first) * m_State->Format.Channels, destination.begin() + static_cast<std::ptrdiff_t>(first * m_State->Format.Channels));
			m_ReadFrames += frames;
			m_ConsumedFrames += frames;
			m_State->Consumed.store(m_ReadFrames, std::memory_order_release);
			result.Frames = frames;
			result.Status = frames != 0 ? WmaStreamReadStatus::Samples : WmaStreamReadStatus::Starved;
			ObserveCompletion();
			result.InputComplete = m_State->Quiescent.load(std::memory_order_acquire) && m_State->InputComplete.load(std::memory_order_relaxed);
		}

		return result;
	}

	WmaStreamSourceInfo WmaStreamSource::Inspect() const {
		RequireOwner();
		WmaStreamSourceInfo info;
		info.WorkerQuiescent = m_State->Quiescent.load(std::memory_order_acquire);
		info.SamplesPerSecond = m_State->Format.SamplesPerSecond;
		info.Channels = m_State->Format.Channels;
		info.FramesPerPlay = m_State->Ledger.back() / (sizeof(std::int16_t) * info.Channels);
		info.ConsumedFrames = m_ConsumedFrames;
		info.QueuedFrames = static_cast<std::size_t>(m_State->Published.load(std::memory_order_acquire) - m_ReadFrames);
		info.DecodedPackets = m_State->DecodedPackets.load(std::memory_order_relaxed);
		info.Prefilled = m_Prefilled;
		info.Running = m_Running;
		info.InputComplete = info.WorkerQuiescent && m_State->InputComplete.load(std::memory_order_relaxed);
		info.CallbackActive = m_CallbackActive;
		info.StopRequested = m_StopRequested;
		info.Failed = info.WorkerQuiescent && m_State->Failed.load(std::memory_order_relaxed);
		info.InputBorrowActive = !m_State->Encoded.empty();

		return info;
	}

	void WmaStreamSource::RethrowWorkerFailure() const {
		RequireOwner();
		if (m_State->Quiescent.load(std::memory_order_acquire) && m_State->Failure) {
			std::rethrow_exception(m_State->Failure);
		}
	}

	bool WmaStreamSource::CanRetire() const noexcept {
		return std::this_thread::get_id() == m_OwnerThread && (m_Closed || (!m_CallbackActive && m_State->Quiescent.load(std::memory_order_acquire)));
	}

	void WmaStreamSource::ReleaseInputBorrow() {
		RequireOwner();
		if (!CanRetire()) {
			throw std::runtime_error("WMA encoded input release requires actual callback completion and decoder quiescence");
		}
		// Quiescent is immutable; every later worker visit returns before reading this span.
		m_State->Encoded = {};
	}

	void WmaStreamSource::Close() {
		if (!CanRetire()) {
			throw std::runtime_error("WMA stream retirement requires actual consume/flush completion and decoder quiescence");
		}
		if (m_Closed) {
			return;
		}
		ReleaseInputBorrow();
		m_Worker.Retire(*this);
		m_Closed = true;
	}

} // namespace sgre::audio
