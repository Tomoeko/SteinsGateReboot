#include "sgre/audio/sound_playback.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>

namespace sgre::audio {

	SoundSubmission PlanSoundSubmission(const resource::SoundChannel& channel, std::int32_t requested_play_count) {
		if (channel.EncodedData.Size > std::numeric_limits<std::uint32_t>::max() || channel.DecodedPacketCumulativeBytes.size() > std::numeric_limits<std::uint32_t>::max()) {
			throw std::runtime_error("sound submission byte or packet count exceeds its native field");
		}

		SoundSubmission submission;
		submission.EncodedBytes = static_cast<std::uint32_t>(channel.EncodedData.Size);
		submission.DisableRateConversion = channel.DisableSampleRateConversion && channel.Format.SamplesPerSecond == 48000;
		submission.LoopRepeats = requested_play_count < 0 ? 255 : requested_play_count == 0 ? 0
		                                                                                    : static_cast<std::uint32_t>(requested_play_count - 1);
		if (channel.Loop) {
			submission.LoopBegin = channel.Loop->Begin;
			submission.LoopLength = channel.Loop->Length;
			submission.LoopRepeats = requested_play_count <= 0 ? 255 : static_cast<std::uint32_t>(requested_play_count - 1);
			if (channel.Format.Encoding == resource::WaveEncoding::MicrosoftAdpcm) {
				if (channel.Format.Extra.size() < 2) {
					throw std::runtime_error("ADPCM sound loop lacks its samples-per-block field");
				}

				const std::uint32_t samples_per_block = channel.Format.Extra[0] | (static_cast<std::uint32_t>(channel.Format.Extra[1]) << 8);
				if (samples_per_block == 0) {
					throw std::runtime_error("ADPCM sound loop has zero samples per block");
				}

				submission.LoopBegin = submission.LoopBegin / samples_per_block * samples_per_block;
				submission.LoopLength = submission.LoopLength / samples_per_block * samples_per_block;
			}
		}

		if (channel.Format.Encoding == resource::WaveEncoding::WmaStandard || channel.Format.Encoding == resource::WaveEncoding::WmaProfessional) {
			submission.WmaPackets = static_cast<std::uint32_t>(channel.DecodedPacketCumulativeBytes.size());
		}

		return submission;
	}

	SoundMixParameters::SoundMixParameters() {
		SourcePan[0].Destinations[1] = 0.0f;
		SourcePan[1].Destinations[0] = 0.0f;
	}

	SoundMix EvaluateSoundMix(const SoundMixParameters& parameters, std::uint8_t source_channels, std::uint8_t destination_channels, bool disable_rate_conversion) {
		if (source_channels == 0 || source_channels > 8 || destination_channels == 0 || destination_channels > 8) {
			throw std::runtime_error("sound matrix channel count is outside the recovered eight-channel bound");
		}

		SoundMix mix;
		mix.SourceChannels = source_channels;
		mix.DestinationChannels = destination_channels;
		// Preserve the original single-precision gain multiplications and their order.
		float volume = parameters.VoiceVolume * parameters.ResourceVolume;
		volume *= parameters.GroupVolume;
		volume *= parameters.ArchiveVolume;
		volume *= parameters.MasterVolume;
		volume *= parameters.MuteVolume;
		if (!parameters.UseSourcePan) {
			mix.Matrix[0] = volume * (parameters.Pan.Destinations[0] * parameters.ResourcePan[0]);
			mix.Matrix[static_cast<std::size_t>(source_channels) * 2 - 1] = volume * (parameters.Pan.Destinations[1] * parameters.ResourcePan[1]);
		} else if (source_channels == 1 && parameters.FoldStereoPanForMono) {
			mix.Matrix[0] = ((parameters.SourcePan[1].Destinations[0] * parameters.ResourcePan[0]) + (parameters.SourcePan[0].Destinations[0] * parameters.ResourcePan[0])) * volume;
			mix.Matrix[1] = ((parameters.SourcePan[1].Destinations[1] * parameters.ResourcePan[1]) + (parameters.SourcePan[0].Destinations[1] * parameters.ResourcePan[1])) * volume;
		} else {
			for (std::size_t source = 0; source < source_channels; ++source) {
				for (std::size_t destination = 0; destination < destination_channels; ++destination) {
					mix.Matrix[destination * source_channels + source] = (parameters.SourcePan[source].Destinations[destination] * parameters.ResourcePan[destination]) * volume;
				}
			}
		}

		if (!disable_rate_conversion) {
			mix.FrequencyRatio = std::pow(2.0f, parameters.ResourcePitchOctaves + parameters.VoicePitchOctaves);
		}

		return mix;
	}

	void AccumulateSoundMix(const SoundMix& mix, std::span<const float> input, std::span<float> output) {
		if (mix.SourceChannels == 0 || mix.SourceChannels > 8 || mix.DestinationChannels == 0 || mix.DestinationChannels > 8 || input.size() % mix.SourceChannels != 0 || output.size() % mix.DestinationChannels != 0 || input.size() / mix.SourceChannels != output.size() / mix.DestinationChannels) {
			throw std::runtime_error("sound mix buffers or matrix channel counts disagree");
		}

		const auto frames = input.size() / mix.SourceChannels;
		for (std::size_t frame = 0; frame < frames; ++frame) {
			for (std::size_t destination = 0; destination < mix.DestinationChannels; ++destination) {
				float sum = 0.0f;
				for (std::size_t source = 0; source < mix.SourceChannels; ++source) {
					sum += input[frame * mix.SourceChannels + source] * mix.Matrix[destination * mix.SourceChannels + source];
				}

				output[frame * mix.DestinationChannels + destination] += sum;
			}
		}
	}

} // namespace sgre::audio
