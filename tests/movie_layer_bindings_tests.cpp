#include "sgre/script/layer_movie_bindings.hpp"

#include "sgre/scene/layer_camera_draw.hpp"
#include "sgre/script/layer_bindings.hpp"
#include "sgre/script/psb_bindings.hpp"

#include "movie_clock_native_vectors.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

template <class Operation>
static void RequireFailure(Operation operation, std::string_view expected) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(expected) != std::string_view::npos, error.what());
		return;
	}
	throw std::runtime_error("movie accepted an unsupported owner/phase: " + std::string(expected));
}

struct StreamObservations {
	std::size_t Polls = 0;
	std::size_t ClockReads = 0;
	std::size_t Closed = 0;
	std::size_t Retired = 0;
	std::int64_t Now100ns = 0;
	bool Queued = false;
	bool StopRequested = false;
	std::vector<float> Volumes;
	std::vector<bool> Pauses;
	std::vector<sgre::media::MovieDrawFrame> Packets;
	std::function<void()> DuringDraw;
};

class FixtureMovieStream final : public sgre::media::MoviePlaybackStream {
public:
	FixtureMovieStream(StreamObservations& observations, std::int64_t duration, std::vector<sgre::media::MovieVideoSample> samples = {})
	    : m_Observations(observations), m_Samples(std::move(samples)) {
		m_Description.Width = 64;
		m_Description.Height = 64;
		m_Description.Duration100ns = duration;
		m_Description.HasVideo = true;
	}
	~FixtureMovieStream() override {
		Close();
	}
	const sgre::media::MovieStreamDescription& Description() const noexcept override {
		return m_Description;
	}
	sgre::media::MovieVideoRead PollVideo() override {
		++m_Observations.Polls;
		sgre::media::MovieVideoRead result;
		if (m_Next == m_Samples.size()) {
			result.Status = sgre::media::MovieVideoReadStatus::EndOfStream;
		} else {
			result.Status = sgre::media::MovieVideoReadStatus::Sample;
			result.Sample = m_Samples[m_Next++];
		}
		return result;
	}
	sgre::media::MovieAudioProgress PumpAudio(std::int64_t) override {
		throw std::runtime_error("no-audio movie must not manufacture an audio clock");
	}
	bool StopRequested() const noexcept override {
		return m_Observations.StopRequested;
	}
	void SetPaused(bool paused) override {
		m_Observations.Pauses.push_back(paused);
	}
	void SetVolume(float volume) override {
		m_Observations.Volumes.push_back(volume);
	}
	void Close() noexcept override {
		if (!m_Closed) {
			m_Samples.clear();
			++m_Observations.Closed;
			m_Closed = true;
		}
	}

private:
	StreamObservations& m_Observations;
	sgre::media::MovieStreamDescription m_Description;
	std::vector<sgre::media::MovieVideoSample> m_Samples;
	std::size_t m_Next = 0;
	bool m_Closed = false;
};

static sgre::media::MovieVideoSample Sample(std::int64_t time) {
	auto rgb = std::make_shared<sgre::media::MovieRgb32Frame>();
	rgb->Width = rgb->PaddedWidth = 64;
	rgb->Height = rgb->PaddedHeight = 64;
	rgb->RowBytes = 256;
	rgb->Bytes.resize(16384, 127);
	sgre::media::MovieVideoSample sample;
	sample.Rgb32 = std::move(rgb);
	sample.Timestamp100ns = time;
	sample.Aperture.Width = 64;
	sample.Aperture.Height = 64;
	return sample;
}

static sgre::media::MoviePlaybackServices Services(StreamObservations& observations, std::function<std::unique_ptr<sgre::media::MoviePlaybackStream>(std::string_view)> open) {
	sgre::media::MoviePlaybackServices services;
	services.Open = std::move(open);
	services.ReadClock100ns = [&observations] {
		++observations.ClockReads;
		return observations.Now100ns;
	};
	services.Draw = [&observations](const auto& packet) {
		if (observations.DuringDraw) {
			observations.DuringDraw();
		}
		observations.Packets.push_back(packet);
	};
	services.CanRetireDrawOwner = [&observations](const void*) {
		return !observations.Queued;
	};
	services.RetireDrawOwner = [&observations](const void*) {
		++observations.Retired;
	};
	return services;
}

static void TestOriginalClockAndEndState() {
	for (const auto& native : kNativeMovieClockCases) {
		StreamObservations observations;
		sgre::scene::MovieLayerTask player(Services(observations, [&](std::string_view storage) {
			Require(storage == "exact-source", "movie source name must cross the real file service unchanged");
			return std::make_unique<FixtureMovieStream>(observations, native.Duration100ns);
		}));
		player.Play("exact-source");
		for (std::size_t phase = 0; phase < native.Phases.size(); ++phase) {
			if (native.Pause && phase == 1) {
				player.Pause();
			}
			if (native.Pause && phase == 2) {
				player.Unpause();
			}
			const auto& expected = native.Phases[phase];
			observations.Now100ns = expected.Time100ns;
			player.Draw();
			const auto state = player.State();
			Require(static_cast<std::uint32_t>(state.Status) == expected.Status && observations.Polls == expected.Polls && observations.ClockReads == expected.ClockReads && std::bit_cast<std::uint64_t>(state.PositionSeconds) == expected.PositionSecondsBits, "original movie Draw/EOF/pause/rounded duration state differs");
		}
		Require(observations.Packets.empty() && observations.Closed == 1, "EOF never manufactures a frame and real stop closes its source exactly once");
		player.Close();
		Require(observations.Retired == 1, "actual physical retirement closes its per-player frame receiver once");
	}
}

static sgre::script::SquirrelVmLimits VmLimits() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	return limits;
}

static sgre::scene::LayerCameraFrameState FrameState() {
	sgre::scene::LayerCameraFrameState frame;
	frame.Viewport = {1280, 720};
	frame.RawScissor = {0, 0, 1280, 720};
	frame.RoundedScissor = frame.RawScissor;
	frame.AppliedRoundedScissor = frame.RawScissor;
	return frame;
}

struct MovieFixture {
	StreamObservations Observations;
	sgre::scene::TextureCache Cache{[](auto, const auto&) -> sgre::render::SpriteTextureHandle {
		                                throw std::runtime_error("movie fixture must not allocate a substitute atlas");
	                                },
	    [](auto) {
		    throw std::runtime_error("movie fixture owns no atlas");
	    }};
	sgre::script::SquirrelVm Vm{VmLimits()};
	sgre::script::ScriptObjectBindings Objects{Vm};
	sgre::script::ScriptPsbBindings Psb{Vm, Objects};
	sgre::scene::MotionAssetCatalog Assets{Cache};
	sgre::scene::LayerTree Tree;
	sgre::script::ScriptLayerBindings Layers{Vm, Objects, Tree};
	std::array<sgre::scene::MotionScissorMatrix, 1> Models{};
	sgre::scene::LayerCameraDrawScope Camera{FrameState(), Models, [](const auto&) {
	                                         }};
	sgre::script::ScriptLayerMovieBindings Movies{Vm, Objects, Layers, Tree};
	std::vector<std::string> Opened;

	MovieFixture() {
		Vm.InstallRecoveredBaseLibrary();
		Layers.ConfigureMotionOwners(Psb, Assets, Cache);
		Movies.ConfigureFrameOwners(Camera, Services(Observations, [this](std::string_view storage) {
			Opened.emplace_back(storage);
			return std::make_unique<FixtureMovieStream>(Observations, 1000000, std::vector<sgre::media::MovieVideoSample>{Sample(0), Sample(333333), Sample(666666)});
		}));
	}
	~MovieFixture() {
		try {
			Vm.Close();
		} catch (...) {
		}
	}
	sgre::script::SquirrelValue Parent() {
		return Vm.Invoke(Layers.LayerClass(), std::array<sgre::script::SquirrelValue, 1>{Vm.RootTable()}).Value;
	}
	sgre::script::SquirrelValue Movie(const sgre::script::SquirrelValue& parent) {
		return Vm.Invoke(Movies.LayerMovieClass(), std::array<sgre::script::SquirrelValue, 3>{Vm.RootTable(), parent, std::string("ignored extra")}).Value;
	}
	sgre::script::SquirrelValue Call(const sgre::script::SquirrelValue& receiver, const char* method, std::initializer_list<sgre::script::SquirrelValue> parameters = {}) {
		std::vector<sgre::script::SquirrelValue> arguments{receiver};
		arguments.insert(arguments.end(), parameters);
		return Vm.Invoke(Vm.GetSlot(receiver, std::string(method)), arguments).Value;
	}
	void Draw(const sgre::script::SquirrelValue& parent) {
		Layers.MotionTasks(parent).Group(sgre::scene::MotionTaskGroup::First).Draw(0);
	}
	void Close() {
		Vm.Close();
		Vm.RethrowReleaseFailure();
	}
};

static void TestActualWrapperAndFrameOwnership() {
	MovieFixture fixture;
	const auto parent = fixture.Parent();
	auto parent_root = fixture.Vm.Retain(parent);
	fixture.Layers.SetSmoothing(parent, true);
	const auto movie = fixture.Movie(parent);
	auto movie_root = fixture.Vm.Retain(movie);
	Require(fixture.Vm.IsInstanceOf(movie, fixture.Objects.ObjectClass()) && !fixture.Vm.IsInstanceOf(movie, fixture.Layers.LayerBaseClass()), "LayerMovie is its real direct Object-derived wrapper");
	Require(!std::get<bool>(fixture.Call(movie, "getVisible")) && std::get<bool>(fixture.Call(movie, "getPlayStart")) && !std::get<bool>(fixture.Call(movie, "getPlaying")), "native movie starts hidden/stopped while getPlayStart tests only attachment");
	fixture.Call(movie, "setOpacity", {std::int32_t{-257}});
	fixture.Call(movie, "setVolume", {0.5f});
	fixture.Call(movie, "setUseAlpha", {true});
	fixture.Call(movie, "play", {std::string("movie/exact.webm"), std::int32_t{7}});
	Require(fixture.Opened == std::vector<std::string>{"movie/exact.webm"} && std::get<std::int32_t>(fixture.Call(movie, "getOpacity")) == -257 && std::get<float>(fixture.Call(movie, "getVolume")) == 0.5f, "original storage and full opacity word survive typed dispatch with ignored extras");
	fixture.Draw(parent);
	Require(fixture.Observations.ClockReads == 0 && fixture.Observations.Packets.empty(), "Play does not bypass the actual initially paused draw task");
	fixture.Call(movie, "setVisible", {true});
	fixture.Observations.Now100ns = 1000000000;
	fixture.Observations.DuringDraw = [&] {
		RequireFailure([&] {
			fixture.Movies.ReleaseInstance(movie);
		},
		    "queued frame/player");
	};
	fixture.Draw(parent);
	fixture.Observations.DuringDraw = {};
	const auto& first = fixture.Observations.Packets.back();
	Require(first.FrameSerial == 1 && first.Opacity == 0xfffffeffu && first.FilteringX == 1 && first.FilteringY == 1 && first.UseAlpha && first.Position == std::array<float, 2>{-32, -32} && first.Extent == std::array<float, 2>{64, 64}, "actual sample dimensions center the full-height native frame with unchanged filtering/alpha/raw opacity");
	fixture.Observations.Now100ns += 100000;
	fixture.Draw(parent);
	Require(fixture.Observations.Packets.back().FrameSerial == 2, "native compares retained sample PTS before fetching the next sample");
	fixture.Observations.Now100ns += 50000;
	fixture.Draw(parent);
	Require(fixture.Observations.Packets.back().FrameSerial == 2 && fixture.Observations.Polls == 2, "future retained sample does not consume another worker frame");
	fixture.Call(movie, "pause");
	fixture.Observations.Now100ns += 400000;
	fixture.Draw(parent);
	Require(fixture.Observations.Packets.back().FrameSerial == 2 && fixture.Observations.Polls == 2 && std::get<bool>(fixture.Call(movie, "getPlaying")), "paused movies redraw retained pixels and remain playing without decoder consumption");
	fixture.Call(movie, "unpause");
	fixture.Observations.Now100ns += 150000;
	fixture.Draw(parent);
	Require(fixture.Observations.Packets.back().FrameSerial == 3, "unpause retains the native original clock baseline");
	fixture.Layers.SetSmoothing(parent, false);
	fixture.Draw(parent);
	Require(fixture.Observations.Packets.back().FilteringX == 0, "actual parent view notifications reach the real player filtering words");
	fixture.Vm.CollectGarbage();
	Require(std::get<bool>(fixture.Call(movie, "getPlaying")), "explicit script roots preserve movie and parent across the actual collector");
	const auto retained = std::weak_ptr<const sgre::media::MovieRgb32Frame>(fixture.Observations.Packets.front().Sample.Rgb32);
	fixture.Observations.Queued = true;
	RequireFailure([&] {
		fixture.Movies.ReleaseInstance(movie);
	},
	    "queued frame/player");
	RequireFailure([&] {
		fixture.Layers.ReleaseInstance(parent);
	},
	    "task groups");
	Require(fixture.Tree.Contains(fixture.Layers.Handle(parent)), "queued source retirement rejects before any actual parent mutation");
	fixture.Observations.Queued = false;
	fixture.Movies.ReleaseInstance(movie);
	Require(fixture.Observations.Closed == 1 && fixture.Observations.Retired == 0 && !retained.expired(), "wrapper release stops source before deferred physical retirement while queued packet values own samples");
	fixture.Layers.MotionTasks(parent).Group(sgre::scene::MotionTaskGroup::First).Update();
	Require(fixture.Observations.Retired == 1, "actual First traversal retires the physical movie receiver");
	fixture.Observations.Packets.clear();
	Require(retained.expired(), "frame pixels retire after their last real consumer releases ownership");
	movie_root = {};
	parent_root = {};
	fixture.Close();
}

static void TestDetachedAndDeferredStop() {
	MovieFixture fixture;
	const auto parent = fixture.Parent();
	const auto movie = fixture.Movie(parent);
	fixture.Call(movie, "play", {std::string("movie/retire.webm")});
	fixture.Observations.StopRequested = true;
	fixture.Layers.MotionTasks(parent).Group(sgre::scene::MotionTaskGroup::First).Update();
	Require(!std::get<bool>(fixture.Call(movie, "getPlaying")) && fixture.Observations.Closed == 1, "actual Update consumes the worker's stop request without drawing or advancing a clock");
	fixture.Layers.ReleaseInstance(parent);
	Require(!std::get<bool>(fixture.Call(movie, "getVisible")) && !std::get<bool>(fixture.Call(movie, "getPlayStart")) && !std::get<bool>(fixture.Call(movie, "getPlaying")) && std::get<std::int32_t>(fixture.Call(movie, "getOpacity")) == 0 && std::get<float>(fixture.Call(movie, "getVolume")) == 0, "detached SQLayerMovie accessors preserve exact native defaults");
	fixture.Call(movie, "play", {std::string("must-not-open")});
	Require(fixture.Opened.size() == 1, "detached play cannot redirect into another player or reopen storage");
	fixture.Movies.ReleaseInstance(movie);
	fixture.Close();
}

int main() {
	try {
		TestOriginalClockAndEndState();
		TestActualWrapperAndFrameOwnership();
		TestDetachedAndDeferredStop();
		std::cout << "movie layer bindings tests passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
