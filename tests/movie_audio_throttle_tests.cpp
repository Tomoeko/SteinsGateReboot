#include "sgre/scene/movie_layer_task.hpp"

#include "movie_audio_throttle_native_vectors.hpp"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

struct ThrottleObservations {
	const NativeMovieThrottlePhase* Input = nullptr;
	std::uint32_t VideoPolls = 0;
	std::uint32_t ClockReads = 0;
	std::uint32_t Draws = 0;
	std::uint32_t LastDrawnSerial = 0;
	std::uint32_t Closed = 0;
	std::uint32_t Retired = 0;
};

class NativeBoundaryMovieStream final : public sgre::media::MoviePlaybackStream {
public:
	NativeBoundaryMovieStream(ThrottleObservations& observations, bool has_audio, std::int64_t duration100ns)
	    : m_Observations(observations) {
		m_Description.Width = m_Description.Height = 64;
		m_Description.HasVideo = true;
		m_Description.HasAudio = has_audio;
		m_Description.Duration100ns = duration100ns;
	}
	const sgre::media::MovieStreamDescription& Description() const noexcept override {
		return m_Description;
	}
	sgre::media::MovieVideoRead PollVideo() override {
		++m_Observations.VideoPolls;
		const auto& input = *m_Observations.Input;
		sgre::media::MovieVideoRead result;
		if (input.ReadKind == 1) {
			result.Status = sgre::media::MovieVideoReadStatus::EndOfStream;
		} else if (input.ReadKind == 2) {
			result.Status = sgre::media::MovieVideoReadStatus::Sample;
			auto pixels = std::make_shared<sgre::media::MovieRgb32Frame>();
			pixels->Width = pixels->Height = pixels->PaddedWidth = pixels->PaddedHeight = 64;
			pixels->RowBytes = 256;
			pixels->Bytes.resize(16384);
			result.Sample.Rgb32 = std::move(pixels);
			result.Sample.Aperture.Width = result.Sample.Aperture.Height = 64;
			result.Sample.Timestamp100ns = input.SampleTime100ns;
		}

		return result;
	}
	sgre::media::MovieAudioProgress PumpAudio(std::int64_t now100ns) override {
		Require(m_Description.HasAudio, "no-audio task must not manufacture a batch or output clock");
		const auto& input = *m_Observations.Input;
		Require(now100ns == input.Clock100ns, "audio snapshot uses the same actual Draw clock");
		sgre::media::MovieAudioProgress result;
		result.InputEnded = input.AudioInputEnded;
		result.AssembledBlocks = input.AssembledBlocks;

		return result;
	}
	bool StopRequested() const noexcept override {
		return false;
	}
	void SetPaused(bool) override {
	}
	void SetVolume(float) override {
	}
	void Close() noexcept override {
		if (!m_Closed) {
			++m_Observations.Closed;
			m_Closed = true;
		}
	}

private:
	ThrottleObservations& m_Observations;
	sgre::media::MovieStreamDescription m_Description;
	bool m_Closed = false;
};

static void TestOriginalBatchAcknowledgement() {
	for (std::size_t case_index = 0; case_index < kNativeMovieThrottleCases.size(); ++case_index) {
		const auto& native = kNativeMovieThrottleCases[case_index];
		ThrottleObservations observations;
		sgre::media::MoviePlaybackServices services;
		services.Open = [&](std::string_view storage) {
			Require(storage == "exact-source", "throttle cannot substitute the real stream identity");
			return std::make_unique<NativeBoundaryMovieStream>(observations, native.HasAudio, native.Duration100ns);
		};
		services.ReadClock100ns = [&] {
			++observations.ClockReads;
			return observations.Input->Clock100ns;
		};
		services.Draw = [&](const sgre::media::MovieDrawFrame& frame) {
			Require(frame.Sample.Rgb32 != nullptr, "draw must retain the actual current decoded frame");
			++observations.Draws;
			observations.LastDrawnSerial = static_cast<std::uint32_t>(frame.FrameSerial);
		};
		services.CanRetireDrawOwner = [](const void*) {
			return true;
		};
		services.RetireDrawOwner = [&](const void*) {
			++observations.Retired;
		};
		sgre::scene::MovieLayerTask task(std::move(services));
		for (std::size_t phase_index = 0; phase_index < native.Phases.size(); ++phase_index) {
			const auto& phase = native.Phases[phase_index];
			observations.Input = &phase;
			switch (phase.Action) {
			case 0:
				break;
			case 1:
				task.Play("exact-source");
				break;
			case 2:
				task.Pause();
				break;
			case 3:
				task.Unpause();
				break;
			case 4:
				task.Stop();
				break;
			default:
				throw std::runtime_error("unknown native movie fixture operation");
			}
			task.Draw();
			const auto state = task.State();
			if (static_cast<std::uint32_t>(state.Status) != phase.Status || observations.VideoPolls != phase.VideoPolls || observations.ClockReads != phase.ClockReads || observations.Draws != phase.Draws || observations.LastDrawnSerial != phase.LastDrawnSerial || std::bit_cast<std::uint64_t>(state.PositionSeconds) != phase.PositionSecondsBits) {
				throw std::runtime_error("original movie audio throttle differs at case=" + std::to_string(case_index) + " phase=" + std::to_string(phase_index) + " polls=" + std::to_string(observations.VideoPolls) + "/" + std::to_string(phase.VideoPolls));
			}
		}
		task.Close();
		Require(observations.Closed == 2 && observations.Retired == 1, "both genuine stream instances and actual draw owner retire once");
	}
}

int main() {
	try {
		TestOriginalBatchAcknowledgement();
		std::cout << "movie audio throttle tests passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
