#include "sgre/media/webm_playback.hpp"

#include "sgre/media/movie_audio.hpp"
#include "sgre/media/movie_rgb32.hpp"
#include "sgre/media/webm.hpp"

#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <exception>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>

namespace sgre::media {

	class WebmMoviePlayback final : public MoviePlaybackStream {
	public:
		WebmMoviePlayback(const std::filesystem::path& path, std::string_view seed, WebmMoviePlaybackLimits limits, MovieSilentAudioRegistry* actual_audio = nullptr)
		    : m_Limits(limits), m_Reader(std::make_unique<WebmReader>(path, seed)) {
			if (limits.QueuedVideoFrames == 0 || limits.QueuedVideoFrames > 8 || limits.QueuedAudioBlocks == 0 || limits.QueuedAudioBlocks > 32 || limits.MaximumFrameBytes == 0 || limits.MaximumFrameBytes > 256 * 1024 * 1024) {
				throw std::runtime_error("WebM movie worker requires bounded video queue and frame allocation limits");
			}
			for (const auto& track : m_Reader->Tracks()) {
				if (track.Type == 1 && track.Codec == "V_VP9" && !m_Description.HasVideo) {
					m_Video = track;
					m_Description.HasVideo = true;
					m_Description.Width = track.Width;
					m_Description.Height = track.Height;
				} else if (track.Type == 2 && track.Codec == "A_OPUS" && !m_Description.HasAudio) {
					if (actual_audio == nullptr) {
						throw std::runtime_error("WebM movie audio requires its actual silent Opus output service: " + path.filename().string());
					}
					m_Audio = track;
					m_Description.HasAudio = true;
				} else {
					throw std::runtime_error("WebM movie worker encountered an unsupported or duplicate track: " + track.Codec);
				}
			}
			const auto duration = m_Reader->DurationNanoseconds() / 100.0;
			if (!m_Description.HasVideo || m_Description.Width == 0 || m_Description.Height == 0 || m_Video.Alpha || !std::isfinite(duration) || duration < 0 || static_cast<long double>(duration) >= static_cast<long double>(std::numeric_limits<std::int64_t>::max())) {
				throw std::runtime_error("WebM movie requires actual VP9 dimensions, bounded duration and the recovered color-video track");
			}
			m_Description.Duration100ns = static_cast<std::int64_t>(duration);
			m_Description.CanSeek = true;
			if (m_Description.HasAudio) {
				m_AudioReader = std::make_unique<WebmReader>(path, seed);
				MoviePcmDescription pcm;
				pcm.Channels = m_Audio.Channels;
				m_AudioOutput = actual_audio->Open(pcm);
			}
			try {
				m_Worker = std::thread([this] {
					Run();
				});
				if (m_Description.HasAudio) {
					m_AudioWorker = std::thread([this] {
						RunAudio();
					});
				}
			} catch (...) {
				Close();
				throw;
			}
		}

		~WebmMoviePlayback() override {
			Close();
		}

		const MovieStreamDescription& Description() const noexcept override {
			return m_Description;
		}

		MovieVideoRead PollVideo() override {
			const std::lock_guard<std::mutex> lock(m_Mutex);
			RequireOpen();
			if (m_Error) {
				std::rethrow_exception(m_Error);
			}
			MovieVideoRead result;
			if (!m_Queue.empty()) {
				result.Status = MovieVideoReadStatus::Sample;
				result.Sample = std::move(m_Queue.front());
				m_Queue.pop_front();
				m_Condition.notify_all();
			} else if (m_Ended) {
				result.Status = MovieVideoReadStatus::EndOfStream;
			}

			return result;
		}

		MovieAudioProgress PumpAudio(std::int64_t now100ns) override {
			const std::lock_guard<std::mutex> lock(m_Mutex);
			RequireOpen();
			if (m_Error) {
				std::rethrow_exception(m_Error);
			}
			if (!m_AudioOutput) {
				MovieAudioProgress result;
				result.InputEnded = true;

				return result;
			}
			while (!m_AudioQueue.empty()) {
				if (!m_AudioOutput->TryQueue(m_AudioQueue.front())) {
					break;
				}
				m_AudioQueue.pop_front();
				m_Condition.notify_all();
			}
			if (m_AudioEnded && m_AudioQueue.empty()) {
				m_AudioOutput->MarkInputEnded();
			}

			return m_AudioOutput->Snapshot(now100ns).Progress;
		}

		bool StopRequested() const noexcept override {
			const std::lock_guard<std::mutex> lock(m_Mutex);

			return m_Closing;
		}

		void SetPaused(bool paused) override {
			const std::lock_guard<std::mutex> lock(m_Mutex);
			RequireOpen();
			m_Paused = paused;
			if (m_AudioOutput) {
				m_AudioOutput->SetPaused(paused);
			}
			m_Condition.notify_all();
		}

		void SetVolume(float volume) override {
			const std::lock_guard<std::mutex> lock(m_Mutex);
			RequireOpen();
			if (m_AudioOutput) {
				m_AudioOutput->SetVolume(volume);
			}
		}

		void Close() noexcept override {
			{
				const std::lock_guard<std::mutex> lock(m_Mutex);
				m_Closing = true;
				m_Condition.notify_all();
			}
			try {
				if (m_Worker.joinable()) {
					m_Worker.join();
				}
				if (m_AudioWorker.joinable()) {
					m_AudioWorker.join();
				}
			} catch (...) {
				std::terminate();
			}
			const std::lock_guard<std::mutex> lock(m_Mutex);
			m_Queue.clear();
			m_AudioQueue.clear();
			m_Reader.reset();
			m_AudioReader.reset();
			if (m_AudioOutput) {
				m_AudioOutput->Close();
				m_AudioOutput.reset();
			}
		}

	private:
		void RequireOpen() const {
			if (m_Closing) {
				throw std::runtime_error("WebM movie stream is closed");
			}
		}

		bool WaitForCapacity() {
			std::unique_lock<std::mutex> lock(m_Mutex);
			m_Condition.wait(lock, [this] {
				return m_Closing || (!m_Paused && m_Queue.size() < m_Limits.QueuedVideoFrames);
			});

			return !m_Closing;
		}

		bool Publish(Nv12VideoFrame frame) {
			if (!WaitForCapacity()) {
				return false;
			}
			MovieVideoSample sample;
			sample.Rgb32 = std::make_shared<const MovieRgb32Frame>(ConvertMovieRgb32Frame(frame, m_Limits.MaximumFrameBytes));
			sample.Aperture.Width = frame.Width;
			sample.Aperture.Height = frame.Height;
			sample.Timestamp100ns = frame.TimestampNanoseconds / 100;
			sample.Frame = std::make_shared<const Nv12VideoFrame>(std::move(frame));
			const std::lock_guard<std::mutex> lock(m_Mutex);
			if (m_Closing) {
				return false;
			}
			m_Queue.push_back(std::move(sample));

			return true;
		}

		static std::int64_t FrameTimestamp(const WebmPacket& packet, std::size_t index, std::uint64_t default_duration) {
			if (index == 0) {
				return packet.TimestampNanoseconds;
			}
			if (default_duration == 0 || default_duration > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) / index) {
				throw std::runtime_error("laced movie frames require actual bounded default-duration timing");
			}
			const auto offset = static_cast<std::int64_t>(default_duration * index);
			if (packet.TimestampNanoseconds > std::numeric_limits<std::int64_t>::max() - offset) {
				throw std::runtime_error("laced movie timestamp exceeds signed nanosecond storage");
			}

			return packet.TimestampNanoseconds + offset;
		}

		void DecodePackets() {
			Vp9Description description;
			description.Width = m_Description.Width;
			description.Height = m_Description.Height;
			Vp9VideoDecoder decoder(description, m_Limits.MaximumFrameBytes);
			while (WaitForCapacity()) {
				const auto packet = m_Reader->NextPacket();
				if (!packet) {
					for (auto& frame : decoder.Drain()) {
						if (!Publish(std::move(frame))) {
							return;
						}
					}
					const std::lock_guard<std::mutex> lock(m_Mutex);
					m_Ended = true;

					return;
				}
				if (m_Description.HasAudio && packet->TrackNumber == m_Audio.Number) {
					continue;
				}
				if (packet->TrackNumber != m_Video.Number || packet->Invisible || !packet->Additions.empty()) {
					throw std::runtime_error("WebM movie packet requires its actual color-video track without unmeasured invisible/addition composition");
				}
				for (std::size_t index = 0; index < packet->Frames.size(); ++index) {
					for (auto& frame : decoder.Decode(packet->Frames[index], FrameTimestamp(*packet, index, m_Video.DefaultDurationNanoseconds))) {
						if (!Publish(std::move(frame))) {
							return;
						}
					}
				}
			}
		}

		bool WaitForAudioCapacity() {
			std::unique_lock<std::mutex> lock(m_Mutex);
			m_Condition.wait(lock, [this] {
				return m_Closing || (!m_Paused && m_AudioQueue.size() < m_Limits.QueuedAudioBlocks);
			});

			return !m_Closing;
		}

		void RunAudio() noexcept {
			try {
				MovieOpusDecoder decoder(m_Audio);
				while (WaitForAudioCapacity()) {
					const auto packet = m_AudioReader->NextPacket();
					if (!packet) {
						const std::lock_guard<std::mutex> lock(m_Mutex);
						m_AudioEnded = true;
						m_Condition.notify_all();

						return;
					}
					if (packet->TrackNumber == m_Video.Number) {
						continue;
					}
					auto block = decoder.Decode(*packet);
					if (block.Samples.empty()) {
						continue;
					}
					if (!WaitForAudioCapacity()) {
						return;
					}
					const std::lock_guard<std::mutex> lock(m_Mutex);
					if (m_Closing) {
						return;
					}
					m_AudioQueue.push_back(std::move(block));
				}
			} catch (...) {
				const std::lock_guard<std::mutex> lock(m_Mutex);
				m_Error = std::current_exception();
				m_AudioEnded = true;
				m_Condition.notify_all();
			}
		}

		void Run() noexcept {
			try {
				DecodePackets();
			} catch (...) {
				const std::lock_guard<std::mutex> lock(m_Mutex);
				m_Error = std::current_exception();
				m_Ended = true;
			}
		}

		WebmMoviePlaybackLimits m_Limits;
		std::unique_ptr<WebmReader> m_Reader;
		std::unique_ptr<WebmReader> m_AudioReader;
		std::unique_ptr<MovieAudioOutput> m_AudioOutput;
		WebmTrack m_Video;
		WebmTrack m_Audio;
		MovieStreamDescription m_Description;
		mutable std::mutex m_Mutex;
		std::condition_variable m_Condition;
		std::deque<MovieVideoSample> m_Queue;
		std::deque<MovieAudioBlock> m_AudioQueue;
		std::exception_ptr m_Error;
		bool m_Paused = false;
		bool m_Closing = false;
		bool m_Ended = false;
		bool m_AudioEnded = false;
		std::thread m_Worker;
		std::thread m_AudioWorker;
	};

	std::unique_ptr<MoviePlaybackStream> OpenWebmMoviePlayback(const std::filesystem::path& path, std::string_view seed, WebmMoviePlaybackLimits limits) {
		return std::make_unique<WebmMoviePlayback>(path, seed, limits);
	}

	std::unique_ptr<MoviePlaybackStream> OpenWebmMoviePlayback(const std::filesystem::path& path, std::string_view seed, MovieSilentAudioRegistry& actual_audio, WebmMoviePlaybackLimits limits) {
		return std::make_unique<WebmMoviePlayback>(path, seed, limits, &actual_audio);
	}

} // namespace sgre::media
