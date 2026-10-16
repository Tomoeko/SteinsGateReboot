#include "sgre/script/sound_bindings.hpp"

#include "sgre/audio/buffered_sound_output.hpp"
#include "voice_control_fixture.hpp"

#include <cmath>
#include <memory>
#include <numbers>
#include <optional>
#include <variant>

class PlaybackOutput final : public sgre::audio::BufferedSoundOutput {
public:
	std::size_t Attached = 0;
	std::size_t MixChanges = 0;

private:
	void ConnectSourceOwner(sgre::audio::BufferedSoundSources&) override {
	}
	void ReleaseSourceOwner(sgre::audio::BufferedSoundSources&) noexcept override {
	}
	void AttachSource(sgre::audio::SoundVoiceAddress, sgre::audio::BufferedSoundSources&) override {
		++Attached;
	}
	void RetireSource(sgre::audio::SoundVoiceAddress) noexcept override {
	}
	void ApplySourceMix(sgre::audio::SoundVoiceAddress, const sgre::audio::SoundMix&) override {
		++MixChanges;
	}
};

static const std::array<CueVector, 2> kPlaybackCueVectors{CueVector{}, CueVector{"long-cue-name-20bytes"}};

class PlaybackFixture {
public:
	explicit PlaybackFixture(const std::filesystem::path& directory, std::span<const CueVector> vectors = kPlaybackCueVectors)
	    : Runtime(directory, vectors), Control(Runtime.Cache, Runtime.Mounts, Runtime.Processor, "win", "synthetic-seed", false), Bindings(Vm, Control) {
		Control.ConnectOutput(Output);
		Runtime.LoadArchive("beta", vectors);
		Require(Control.Load("configuration.psb"), "playVoice requires the actual configuration load owner");
		for (std::size_t visit = 0; visit != 8 && !Control.GetLoaded("configuration.psb"); ++visit) {
			Control.AdvancePostLoop();
		}
		for (const auto name : {"alpha", "beta"}) {
			Require(Control.LoadArchive(name), "playVoice requires actual archive declarations");
			for (std::size_t visit = 0; visit != 8 && !Control.GetArchiveLoaded(name); ++visit) {
				Control.AdvancePostLoop();
			}
			Require(Control.GetArchiveLoaded(name), "playVoice archive must finish through its genuine workers/tasks");
		}
		Control.SetLoudnessVolumeEnabled(false);
	}

	~PlaybackFixture() noexcept {
		try {
			Vm.Close();
			Control.Close();
		} catch (...) {
			std::terminate();
		}
	}

	void Connect(std::optional<sgre::script::SoundBufferedDecoderProfile> verified_decoder_profile = std::nullopt) {
		Bindings.ConfigureBufferedPlayback([this] {
			++GeometryReads;

			return std::uint8_t{2};
		},
		    verified_decoder_profile);
	}

	[[nodiscard]] sgre::script::SquirrelValue Invoke(std::initializer_list<sgre::script::SquirrelValue> values) {
		std::vector<sgre::script::SquirrelValue> arguments{Bindings.SoundClass()};
		arguments.insert(arguments.end(), values.begin(), values.end());

		return Vm.Invoke(Vm.GetSlot(Bindings.SoundClass(), "playVoice"), arguments).Value;
	}

	[[nodiscard]] sgre::audio::BufferedSoundCueId Id(const sgre::script::SquirrelValue& value) {
		Require(std::holds_alternative<std::int32_t>(value) && std::get<std::int32_t>(value) >= 0, "playVoice must return its real newly published logical cue ID");

		return sgre::audio::BufferedSoundCueId{static_cast<std::uint32_t>(std::get<std::int32_t>(value))};
	}

	ActualCueRuntime Runtime;
	PlaybackOutput Output;
	sgre::audio::SoundResourceControl Control;
	sgre::script::SquirrelVm Vm;
	sgre::script::ScriptSoundBindings Bindings;
	std::size_t GeometryReads = 0;
};

static void TestDispatchAndActualSource(const std::filesystem::path& directory) {
	PlaybackFixture fixture(directory);
	RequireFailure([&] {
		static_cast<void>(fixture.Invoke({"tone"}));
	},
	    "actual containing buffered output geometry");
	fixture.Connect();
	RequireFailure([&] {
		fixture.Connect();
	},
	    "one actual live output");
	const auto options = fixture.Vm.CreateTable();
	const auto one = fixture.Id(fixture.Invoke({"tone"}));
	const auto two = fixture.Id(fixture.Invoke({"beta", "tone"}));
	const auto three = fixture.Id(fixture.Invoke({"tone", options}));
	const auto four = fixture.Id(fixture.Invoke({"tone", options, std::int32_t{-2}}));
	const auto five = fixture.Id(fixture.Invoke({"beta", "tone", options}));
	const auto six = fixture.Id(fixture.Invoke({"beta", "tone", options, std::int32_t{7}}));
	Require(fixture.GeometryReads == 6 && fixture.Output.Attached == 6, "all six original overloads must prepare actual source/output slots");
	for (const auto id : {one, two, three, four, five, six}) {
		const auto info = fixture.Control.BufferedCues().Inspect(id);
		Require(info.Channels == 1 && info.CallbackActive && fixture.Control.BufferedCues().IsPlaying(id), "returned ID must own genuine decoded callback samples");
	}
	Require(fixture.Control.BufferedCues().Inspect(one).ArchiveName == "alpha" && fixture.Control.BufferedCues().Inspect(two).ArchiveName == "beta", "empty archive uses real sorted search while explicit archive is preserved");
	const auto missing = fixture.Invoke({"missing"});
	Require(std::get<std::int32_t>(missing) == -1 && fixture.Output.Attached == 6, "missing native definition returns -1 without creating a source");
	const auto nul_name = fixture.Id(fixture.Invoke({sgre::script::SquirrelString(std::string("tone\0suffix", 11))}));
	Require(fixture.Control.BufferedCues().Inspect(nul_name).CueName == "tone", "native name lookup uses the C-string prefix");
	std::array<float, 4> pcm{};
	Require(fixture.Control.BufferedCues().ConsumeChannelFrames(one, 0, pcm) == 4 && pcm == std::array<float, 4>{0.25f, 0.5f, -0.5f, 0.0f}, "script playback must own the independent decoded PCM, not an inferred playing flag");
	Require(!fixture.Control.BufferedCues().IsPlaying(one), "actual consumption must complete its source callback");
}

static void TestOptionsAndResolvedNames(const std::filesystem::path& directory) {
	PlaybackFixture fixture(directory);
	fixture.Connect();
	fixture.Control.SetMasterVolume(0.5f);
	fixture.Control.SetGroupVolume("custom", 0.25f);
	const auto options = fixture.Vm.CreateTable();
	fixture.Vm.NewSlot(options, "volume", 0.5f);
	fixture.Vm.NewSlot(options, "pitch", 0.25f);
	fixture.Vm.NewSlot(options, "priority", std::int32_t{4});
	fixture.Vm.NewSlot(options, "group", "custom");
	fixture.Vm.NewSlot(options, "uniq", "single");
	fixture.Vm.NewSlot(options, "hidMask", std::int32_t{1});
	const auto id = fixture.Id(fixture.Invoke({"alpha", "tone", options, std::int32_t{1}}));
	auto channel = fixture.Control.BufferedCues().InspectChannel(id, 0);
	Require(channel.Mix && channel.Mix->Matrix[0] == 0.0078125f && !channel.Mix->FrequencyRatio, "voice/resource/group/master factors must reach the retained actual output matrix");
	fixture.Control.SetGroupVolume("custom", 0.5f);
	channel = fixture.Control.BufferedCues().InspectChannel(id, 0);
	Require(channel.Mix->Matrix[0] == 0.015625f, "active group setter must use the resolved requested group, not the resource's empty default");
	fixture.Vm.SetSlot(options, "priority", std::int32_t{3});
	Require(std::get<std::int32_t>(fixture.Invoke({"alpha", "tone", options})) == -1 && fixture.Control.BufferedCues().IsPlaying(id), "requested uniqueness/priority must reject a real lower-priority competing cue");
	fixture.Vm.SetSlot(options, "priority", std::int32_t{5});
	const auto replacement = fixture.Id(fixture.Invoke({"alpha", "tone", options}));
	Require(replacement.Value != id.Value && fixture.Control.BufferedCues().IsPlaying(replacement), "higher-priority uniqueness must replace the real old source/cue");
	RequireFailure([&] {
		static_cast<void>(fixture.Control.BufferedCues().Inspect(id));
	},
	    "no actual active ID");
	fixture.Vm.SetSlot(options, "group", "later");
	fixture.Control.SetGroupVolume("custom", 0.125f);
	Require(fixture.Control.BufferedCues().InspectChannel(replacement, 0).Mix->Matrix[0] == 0.00390625f, "cue must own resolved group bytes after the source option table changes");
	fixture.Vm.SetSlot(options, "uniq", "other");
	fixture.Vm.SetSlot(options, "volume", true);
	const auto ignored_boolean = fixture.Id(fixture.Invoke({"tone", options}));
	Require(fixture.Control.BufferedCues().InspectChannel(ignored_boolean, 0).Mix->Matrix[0] == 0.0625f, "native Boolean volume is ignored, retaining constructor unity");
}

static void TestPanAndRejectedInputs(const std::filesystem::path& directory) {
	PlaybackFixture fixture(directory);
	fixture.Connect();
	const auto options = fixture.Vm.CreateTable();
	const auto pan = fixture.Vm.CreateTable();
	fixture.Vm.NewSlot(pan, "l", 0.5f);
	fixture.Vm.NewSlot(pan, "r", 0.25f);
	fixture.Vm.NewSlot(options, "pan", pan);
	const auto id = fixture.Id(fixture.Invoke({"tone", options}));
	const auto mix = fixture.Control.BufferedCues().InspectChannel(id, 0).Mix;
	Require(mix && mix->Matrix[0] == 0.0625f && mix->Matrix[1] == 0.09375f, "native pan fields must multiply the actual channel pan without replacing it");
	const auto before = fixture.GeometryReads;
	for (const auto& values : std::vector<std::vector<sgre::script::SquirrelValue>>{{}, {std::int32_t{1}}, {"tone", std::int32_t{1}}, {"tone", options, 1.0f}, {"alpha", "tone", options, true}}) {
		std::vector<sgre::script::SquirrelValue> arguments{fixture.Bindings.SoundClass()};
		arguments.insert(arguments.end(), values.begin(), values.end());
		RequireFailure([&] {
			static_cast<void>(fixture.Vm.Invoke(fixture.Vm.GetSlot(fixture.Bindings.SoundClass(), "playVoice"), arguments));
		},
		    "invalid argument");
	}
	fixture.Vm.NewSlot(options, "pitch", "1.0");
	RequireFailure([&] {
		static_cast<void>(fixture.Invoke({"tone", options}));
	},
	    "requires a number");
	fixture.Vm.SetSlot(options, "pitch", 0.0f);
	fixture.Vm.NewSlot(options, "hidMask", std::int32_t{2});
	RequireFailure([&] {
		static_cast<void>(fixture.Invoke({"tone", options}));
	},
	    "requires recovered routing");
	Require(fixture.GeometryReads == before && fixture.Control.BufferedCues().ActiveCount() == 1, "rejected arguments/options must fail before output or cue publication");
}

static void TestOptionDelegates(const std::filesystem::path& directory) {
	PlaybackFixture fixture(directory);
	fixture.Connect();
	const auto options = fixture.Vm.CreateTable();
	const auto parent = fixture.Vm.CreateTable();
	fixture.Vm.NewSlot(parent, "volume", 0.25f);
	std::size_t getters = 0;
	fixture.Vm.NewSlot(parent, "_get", fixture.Vm.CreateNativeClosure("unexpected_sound_option_getter", [&](auto&, auto) {
		++getters;

		return sgre::script::SquirrelNativeResult::ReturnValue(99.0f);
	}));
	sgre::script::BytecodeProgram program;
	program.MainFunction.Parameters = {std::string("this"), std::string("target"), std::string("parent")};
	program.MainFunction.StackSize = 4;
	sgre::script::SquirrelInstruction delegate;
	delegate.Opcode = 53;
	delegate.Argument0 = 3;
	delegate.Argument1 = 1;
	delegate.Argument2 = 2;
	sgre::script::SquirrelInstruction finish;
	finish.Opcode = 19;
	finish.Argument0 = 255;
	program.MainFunction.Instructions = {delegate, finish};
	const std::array<sgre::script::SquirrelValue, 3> arguments{fixture.Vm.RootTable(), options, parent};
	static_cast<void>(fixture.Vm.Invoke(fixture.Vm.CreateClosure(std::move(program)), arguments));
	const auto id = fixture.Id(fixture.Invoke({"tone", options}));
	Require(fixture.Control.BufferedCues().InspectChannel(id, 0).Mix->Matrix[0] == 0.03125f && getters == 0, "raw option existence must follow concrete delegate slots without invoking _get for absent fields");
}

class WmaPolicyBitFixture {
public:
	void Append(std::uint32_t value, std::uint8_t width) {
		for (std::uint8_t index = width; index != 0; --index) {
			if (m_Bits % 8 == 0) {
				m_Bytes.push_back(0);
			}

			m_Bytes.back() |= static_cast<std::uint8_t>(((value >> (index - 1)) & 1) << (7 - m_Bits % 8));
			++m_Bits;
		}
	}

	[[nodiscard]] Bytes Packet() const {
		auto bytes = m_Bytes;
		bytes.resize(1487);

		return bytes;
	}

private:
	Bytes m_Bytes;
	std::size_t m_Bits = 0;
};

static Bytes IndependentWmaPolicyPacket() {
	// The existing decoder's independent six-frame, frequency-zero impulse fixture; no generated decoder output.
	WmaPolicyBitFixture bits;
	bits.Append(0, 4);
	bits.Append(6, 4);
	bits.Append(0, 12);
	bits.Append(0, 3);
	bits.Append(0, 3);
	for (std::size_t frame = 0; frame < 6; ++frame) {
		bits.Append(0, 3);
		bits.Append(1, 1);
		bits.Append(63, 7);
		for (std::size_t band = 0; band < 25; ++band) {
			bits.Append(0, 1);
		}
		if (frame == 2) {
			bits.Append(0x3e6, 11);
			bits.Append(128, 9);
			bits.Append(0, 11);
			bits.Append(1, 1);
		}
		bits.Append(0xf6, 9);
	}

	return bits.Packet();
}

static void RequireUnchangedWmaInput(PlaybackFixture& fixture, std::span<const std::uint8_t> format, std::span<const std::uint8_t> encoded) {
	const auto& file = fixture.Control.ArchiveFile("alpha");
	const auto definitions = sgre::resource::ReadSoundArchiveDefinitions(file.Document());
	const auto data = sgre::audio::ReadBufferedSoundSourceData(file, definitions.at("other"), 0, 1);
	Require(data.FormatBytes.size() == 18 && std::equal(data.FormatBytes.begin(), data.FormatBytes.end(), format.begin(), format.end()) && data.Description.Format.Extra.empty(), "explicit decoder policy must preserve the actual 18-byte fmt without synthesizing an extension");
	Require(std::equal(data.EncodedBytes.begin(), data.EncodedBytes.end(), encoded.begin(), encoded.end()) && data.Description.DecodedPacketCumulativeBytes == std::vector<std::uint32_t>{16384}, "explicit policy must preserve original encoded bytes and the exact decoded packet ledger");
}

static void TestExplicitWmaPolicyAndConfigurationRollback(const std::filesystem::path& directory) {
	const Bytes format{0x61, 1, 1, 0, 0x44, 0xac, 0, 0, 0xa0, 0x0f, 0, 0, 0xcf, 5, 16, 0, 0, 0};
	const auto packet = IndependentWmaPolicyPacket();
	const std::array<std::uint32_t, 1> packets{16384};
	CueVector wma;
	wma.Name = "other";
	wma.FormatBytes = format;
	wma.EncodedBytes = packet;
	wma.DecodedPackets = packets;
	const std::array<CueVector, 2> vectors{wma, CueVector{}};
	const auto unset_directory = directory / "wma-unset";
	std::filesystem::create_directories(unset_directory);
	{
		PlaybackFixture fixture(unset_directory, vectors);
		RequireFailure([&] {
			fixture.Bindings.ConfigureBufferedPlayback({}, sgre::script::SoundBufferedDecoderProfile::VerifiedWmaOptions31);
		},
		    "one actual live output");
		RequireFailure([&] {
			fixture.Connect(static_cast<sgre::script::SoundBufferedDecoderProfile>(7));
		},
		    "decoder profile is not explicitly verified");
		Require(fixture.GeometryReads == 0 && fixture.Output.Attached == 0 && fixture.Control.BufferedCues().ActiveCount() == 0, "rejected service configuration must neither read output geometry nor commit policy/source state");
		fixture.Connect();
		const auto id = fixture.Id(fixture.Invoke({"other"}));
		const auto failed = fixture.Control.BufferedCues().InspectChannel(id, 0);
		Require(failed.Error.find("explicitly verified options31") != std::string_view::npos && !failed.State.Samples.CallbackActive && !failed.State.RecoveredSchedule.Exists && !failed.Mix && fixture.Output.Attached == 0, "absent policy must preserve explicit WMA source failure even when the native logical cue ID exists");
		RequireUnchangedWmaInput(fixture, format, packet);
		const auto pcm = fixture.Id(fixture.Invoke({"tone"}));
		Require(fixture.Control.BufferedCues().InspectChannel(pcm, 0).Error.empty() && fixture.Output.Attached == 1, "failed WMA preparation must retain ordinary PCM admission and actual output attachment");
	}

	const auto verified_directory = directory / "wma-verified";
	std::filesystem::create_directories(verified_directory);
	{
		PlaybackFixture fixture(verified_directory, vectors);
		fixture.Connect(sgre::script::SoundBufferedDecoderProfile::VerifiedWmaOptions31);
		RequireFailure([&] {
			fixture.Connect();
		},
		    "one actual live output");
		const auto id = fixture.Id(fixture.Invoke({"other"}));
		const auto prepared = fixture.Control.BufferedCues().InspectChannel(id, 0);
		Require(prepared.Error.empty() && prepared.Mix && prepared.State.Samples.CallbackActive && prepared.State.Samples.BufferFrames == 8192 && prepared.State.Samples.SamplesPerSecond == 44100 && prepared.State.Samples.Channels == 1 && fixture.Output.Attached == 1, "explicit supplied-content policy must reach real VM-to-cue WMA decoding and original source-rate output");
		RequireUnchangedWmaInput(fixture, format, packet);
		std::array<float, 512> output{};
		std::size_t consumed = 0;
		constexpr double coefficients = 2048;
		const float gain_argument = 64.0f * 0.05f;
		const double amplitude = static_cast<float>(std::pow(10.0, static_cast<double>(gain_argument))) * 128;
		while (fixture.Control.BufferedCues().IsPlaying(id)) {
			const auto frames = fixture.Control.BufferedCues().ConsumeChannelFrames(id, 0, output);
			Require(frames != 0 && frames <= 8192 - consumed, "verified source must make bounded progress through its actual decoded sample extent");
			for (std::size_t offset = 0; offset < frames; ++offset) {
				const auto index = consumed + offset;
				double expected = 0;
				if (index < 4096) {
					const double position = static_cast<double>(index) + 0.5;
					expected = -amplitude / 32768 * (2 / coefficients) * std::cos(std::numbers::pi / coefficients * (position + coefficients / 2) * 0.5) * std::sin(std::numbers::pi / (2 * coefficients) * position);
				}
				Require(std::abs(static_cast<double>(output[offset]) - expected) < 2e-9, "Sound.playVoice WMA policy did not publish the independent frequency-zero impulse samples");
			}
			consumed += frames;
		}
		Require(consumed == 8192 && fixture.Control.BufferedCues().InspectChannel(id, 0).State.Samples.ConsumedFrames == consumed, "configured WMA source must finish only after consuming every actual decoded frame");
		const auto pcm = fixture.Id(fixture.Invoke({"tone"}));
		Require(fixture.Control.BufferedCues().InspectChannel(pcm, 0).Error.empty() && fixture.Output.Attached == 2, "explicit WMA policy must leave ordinary PCM decoding unchanged");
	}
}

static void TestWmaFailureAndBindingLifetime(const std::filesystem::path& directory) {
	const Bytes format{0x61, 1, 1, 0, 0x44, 0xac, 0, 0, 0xa0, 0x0f, 0, 0, 0xcf, 5, 16, 0, 0, 0};
	const auto packet = IndependentWmaPolicyPacket();
	const std::array<std::uint32_t, 1> packets{16384};
	CueVector wma;
	wma.Name = "other";
	wma.FormatBytes = format;
	wma.DecodedPackets = packets;
	auto malformed = packet;
	malformed[0] |= 0x10;
	wma.EncodedBytes = malformed;
	const auto failed_directory = directory / "wma-malformed";
	std::filesystem::create_directories(failed_directory);
	{
		const std::array<CueVector, 2> vectors{wma, CueVector{}};
		PlaybackFixture fixture(failed_directory, vectors);
		fixture.Connect(sgre::script::SoundBufferedDecoderProfile::VerifiedWmaOptions31);
		const auto id = fixture.Id(fixture.Invoke({"other"}));
		const auto failed = fixture.Control.BufferedCues().InspectChannel(id, 0);
		Require(failed.Error.find("sequence differs") != std::string_view::npos && !failed.State.Samples.CallbackActive && !failed.State.RecoveredSchedule.Exists && !failed.Mix && fixture.Output.Attached == 0, "verified policy must not replace malformed packet failure with nominal decoded/output readiness");
		const auto pcm = fixture.Id(fixture.Invoke({"tone"}));
		Require(fixture.Control.BufferedCues().InspectChannel(pcm, 0).Error.empty() && fixture.Output.Attached == 1, "malformed verified source must roll back its real source slot before later PCM preparation");
	}

	wma.EncodedBytes = packet;
	const std::array<CueVector, 1> vectors{wma};
	const auto lifetime_directory = directory / "wma-binding-lifetime";
	std::filesystem::create_directories(lifetime_directory);
	PlaybackFixture fixture(lifetime_directory, vectors);
	auto bindings = std::make_unique<sgre::script::ScriptSoundBindings>(fixture.Vm, fixture.Control);
	bindings->ConfigureBufferedPlayback([&] {
		++fixture.GeometryReads;

		return std::uint8_t{2};
	},
	    sgre::script::SoundBufferedDecoderProfile::VerifiedWmaOptions31);
	const auto sound = bindings->SoundClass();
	const auto play = fixture.Vm.GetSlot(sound, "playVoice");
	const auto retained = fixture.Vm.Retain(play);
	const std::array<sgre::script::SquirrelValue, 2> arguments{sound, "other"};
	const auto id = fixture.Id(fixture.Vm.Invoke(play, arguments).Value);
	bindings.reset();
	RequireFailure([&] {
		static_cast<void>(fixture.Vm.Invoke(play, arguments));
	},
	    "actual live VM/resource owner");
	std::array<float, 1> sample{};
	Require(fixture.GeometryReads == 1 && fixture.Output.Attached == 1 && fixture.Control.BufferedCues().ConsumeChannelFrames(id, 0, sample) == 1 && sample[0] != 0, "binding retirement must expire policy callbacks while the real cue owner retains independent decoded samples");
}

int main(int argc, char** argv) {
	try {
		Require(argc == 2, "playVoice fixture requires an owned output directory");
		const std::filesystem::path directory(argv[1]);
		std::filesystem::create_directories(directory);
		TestDispatchAndActualSource(directory);
		TestOptionsAndResolvedNames(directory);
		TestPanAndRejectedInputs(directory);
		TestOptionDelegates(directory);
		TestExplicitWmaPolicyAndConfigurationRollback(directory);
		TestWmaFailureAndBindingLifetime(directory);
		std::cout << "Sound.playVoice: native overloads, real cue/output, explicit WMA policy, unchanged fmt, rollback and lifetime passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
