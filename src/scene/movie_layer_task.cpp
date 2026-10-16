#include "sgre/scene/movie_layer_task.hpp"

#include <cmath>
#include <exception>
#include <limits>
#include <stdexcept>
#include <utility>

namespace sgre::scene {

	class MovieOperation {
	public:
		explicit MovieOperation(bool& operating)
		    : m_Operating(operating) {
			if (operating) {
				throw std::runtime_error("movie task cannot reenter its actual stream/frame operation");
			}
			operating = true;
		}
		~MovieOperation() {
			m_Operating = false;
		}

	private:
		bool& m_Operating;
	};

	static double MovieSeconds(std::int64_t time100ns) {
		// Native stores double(float(100ns / 10000000.0)), including the duration comparison.
		return static_cast<double>(static_cast<float>(static_cast<double>(time100ns) / 10000000.0));
	}

	static void RequireMovieDescription(const media::MovieStreamDescription& description) {
		if (description.Duration100ns < 0 || (!description.HasVideo && !description.HasAudio) || (description.HasVideo && (description.Width == 0 || description.Height == 0))) {
			throw std::runtime_error("movie stream requires actual nonnegative timing and its declared video/audio track dimensions");
		}
	}

	static void RequireMovieSample(const media::MovieVideoSample& sample) {
		if (!sample.Rgb32 || sample.Rgb32->Width == 0 || sample.Rgb32->Height == 0 || sample.Aperture.Width == 0 || sample.Aperture.Height == 0 || !std::isfinite(sample.Aperture.Left) || !std::isfinite(sample.Aperture.Top)) {
			throw std::runtime_error("movie draw requires the real decoded sample and actual finite aperture");
		}
	}

	MovieLayerTask::MovieLayerTask(media::MoviePlaybackServices services)
	    : m_Services(std::move(services)) {
		if (!m_Services.Open || !m_Services.ReadClock100ns || !m_Services.Draw || !m_Services.CanRetireDrawOwner || !m_Services.RetireDrawOwner) {
			throw std::runtime_error("movie task requires its real stream, monotonic clock and frame receiver ownership services");
		}
	}

	MovieLayerTask::~MovieLayerTask() {
		try {
			Close();
		} catch (...) {
			std::terminate();
		}
	}

	MovieLayerState MovieLayerTask::State() const noexcept {
		return m_State;
	}

	void MovieLayerTask::RequireIdle() const {
		if (m_State.Closed || m_Operating) {
			throw std::runtime_error("movie task requires its live idle actual owner");
		}
	}

	void MovieLayerTask::StopStream() noexcept {
		if (m_Stream) {
			m_Stream->Close();
			m_Stream.reset();
		}
		m_State.Status = MoviePlaybackStatus::Stopped;
		m_State.PositionSeconds = 0;
		m_Current.reset();
		m_ClockBase100ns.reset();
		m_VideoInputEnded = false;
		m_VideoFinished = false;
		m_AudioFinished = false;
		m_AudioAssembledBlocks = 0;
		m_AudioAcknowledgedBlocks = 0;
	}

	void MovieLayerTask::Play(std::string_view storage) {
		RequireIdle();
		if (!m_Services.CanRetireDrawOwner(this)) {
			throw std::runtime_error("movie replacement requires retirement of its actual queued frame borrow");
		}
		MovieOperation operation(m_Operating);
		StopStream();
		auto stream = m_Services.Open(storage);
		if (!stream) {
			return;
		}
		try {
			const auto description = stream->Description();
			RequireMovieDescription(description);
			stream->SetVolume(m_State.Volume);
			m_Description = description;
			m_DurationSeconds = MovieSeconds(description.Duration100ns);
			m_State.Width = description.Width;
			m_State.Height = description.Height;
			m_State.Extent = {static_cast<float>(description.Width), static_cast<float>(description.Height)};
			m_State.Position = {-m_State.Extent[0] * 0.5f, -m_State.Extent[1] * 0.5f};
			m_VideoFinished = !description.HasVideo;
			m_AudioFinished = !description.HasAudio;
			m_Stream = std::move(stream);
			m_State.Status = MoviePlaybackStatus::Playing;
		} catch (...) {
			stream->Close();
			throw;
		}
	}

	void MovieLayerTask::Stop() {
		RequireIdle();
		MovieOperation operation(m_Operating);
		StopStream();
	}

	void MovieLayerTask::Pause() {
		RequireIdle();
		if (m_State.Status == MoviePlaybackStatus::Playing) {
			MovieOperation operation(m_Operating);
			m_Stream->SetPaused(true);
			m_State.Status = MoviePlaybackStatus::Paused;
		}
	}

	void MovieLayerTask::Unpause() {
		RequireIdle();
		if (m_State.Status == MoviePlaybackStatus::Paused) {
			MovieOperation operation(m_Operating);
			m_Stream->SetPaused(false);
			m_State.Status = MoviePlaybackStatus::Playing;
		}
	}

	void MovieLayerTask::SetOpacity(std::uint32_t opacity) {
		RequireIdle();
		m_State.Opacity = opacity;
	}

	void MovieLayerTask::SetFiltering(std::uint32_t horizontal, std::uint32_t vertical) {
		RequireIdle();
		m_State.FilteringX = horizontal;
		m_State.FilteringY = vertical;
	}

	void MovieLayerTask::SetVolume(float volume) {
		RequireIdle();
		MovieOperation operation(m_Operating);
		m_State.Volume = volume;
		if (m_Stream) {
			m_Stream->SetVolume(volume);
		}
	}

	void MovieLayerTask::SetUseAlpha(bool enabled) {
		RequireIdle();
		m_State.UseAlpha = enabled;
	}

	void MovieLayerTask::Update() {
		RequireIdle();
		MovieOperation operation(m_Operating);
		if (m_Stream && m_Stream->StopRequested()) {
			StopStream();
		}
	}

	void MovieLayerTask::ProcessVideo(std::int64_t position100ns) {
		if (m_VideoInputEnded) {
			m_State.PositionSeconds = MovieSeconds(position100ns);
			if (!(m_State.PositionSeconds < m_DurationSeconds)) {
				m_VideoFinished = true;
				m_State.PositionSeconds = m_DurationSeconds;
			}
		} else {
			m_State.PositionSeconds = MovieSeconds(m_Current ? m_Current->Timestamp100ns : 0);
		}
		if (m_VideoFinished || (m_Current && m_Current->Timestamp100ns > position100ns)) {
			return;
		}
		if (m_Current && m_Description.HasAudio && !m_AudioFinished && m_AudioAcknowledgedBlocks == m_AudioAssembledBlocks && m_AlternateDraw) {
			return;
		}
		const auto result = m_Stream->PollVideo();
		switch (result.Status) {
		case media::MovieVideoReadStatus::Pending:
			return;
		case media::MovieVideoReadStatus::EndOfStream:
			m_VideoInputEnded = true;
			return;
		case media::MovieVideoReadStatus::Sample:
			RequireMovieSample(result.Sample);
			if (m_FrameSerial == std::numeric_limits<std::uint64_t>::max()) {
				throw std::runtime_error("movie decoded frame serial is exhausted");
			}
			m_Current = result.Sample;
			++m_FrameSerial;
			if (m_AlternateDraw) {
				m_AudioAcknowledgedBlocks = m_AudioAssembledBlocks;
			}
			return;
		}
		throw std::runtime_error("movie worker supplied an unknown video read state");
	}

	void MovieLayerTask::DrawCurrent() {
		if (!m_Current) {
			return;
		}
		media::MovieDrawFrame request;
		request.OwnerIdentity = this;
		request.FrameSerial = m_FrameSerial;
		request.Sample = *m_Current;
		// Native38fa38 invokes072f40 on each actual sample draw, retaining full aperture height for alpha.
		m_State.Extent = {static_cast<float>(m_Current->Aperture.Width), static_cast<float>(m_Current->Aperture.Height)};
		m_State.Position = {-m_State.Extent[0] * 0.5f, -m_State.Extent[1] * 0.5f};
		request.Position = m_State.Position;
		request.Extent = m_State.Extent;
		request.Opacity = m_State.Opacity;
		request.FilteringX = m_State.FilteringX;
		request.FilteringY = m_State.FilteringY;
		request.UseAlpha = m_State.UseAlpha;
		m_Services.Draw(request);
	}

	void MovieLayerTask::Draw() {
		RequireIdle();
		MovieOperation operation(m_Operating);
		if (m_State.Status == MoviePlaybackStatus::Stopped) {
			m_AlternateDraw = !m_AlternateDraw;
			return;
		}
		const auto now100ns = m_Services.ReadClock100ns();
		if (now100ns < 0 || (m_ClockBase100ns && now100ns < *m_ClockBase100ns)) {
			throw std::runtime_error("movie drawing requires its actual monotonic nonnegative clock");
		}
		if (!m_ClockBase100ns) {
			m_ClockBase100ns = now100ns;
		}
		if (m_State.Status == MoviePlaybackStatus::Playing) {
			auto position100ns = now100ns - *m_ClockBase100ns;
			if (m_Description.HasAudio) {
				const auto audio = m_Stream->PumpAudio(now100ns);
				m_AudioFinished = audio.InputEnded;
				m_AudioAssembledBlocks = audio.AssembledBlocks;
				if (audio.Position100ns) {
					position100ns = *audio.Position100ns;
				}
			}
			if (m_Description.HasVideo) {
				ProcessVideo(position100ns);
			} else {
				m_State.PositionSeconds = MovieSeconds(position100ns);
			}
		}
		DrawCurrent();
		if (m_VideoFinished && m_AudioFinished) {
			// LayerMovie exposes no loop control; its constructor's loop byte is false.
			StopStream();
		}
		// Native toggles+101 after every Draw, including paused/stopped visits; Stop/Play do not reset it.
		m_AlternateDraw = !m_AlternateDraw;
	}

	bool MovieLayerTask::CanClose() const {
		return m_State.Closed || (!m_Operating && m_Services.CanRetireDrawOwner(this));
	}

	void MovieLayerTask::Close() {
		if (m_State.Closed) {
			return;
		}
		RequireIdle();
		if (!m_Services.CanRetireDrawOwner(this)) {
			throw std::runtime_error("movie retirement requires completion of its actual queued frame borrow");
		}
		MovieOperation operation(m_Operating);
		StopStream();
		m_Services.RetireDrawOwner(this);
		m_State.Closed = true;
	}

} // namespace sgre::scene
