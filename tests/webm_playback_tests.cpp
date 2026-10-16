#include "sgre/media/webm_playback.hpp"

#include "sgre/media/webm.hpp"
#include "sgre/resource/resource_cipher.hpp"

#include <array>
#include <bit>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <stdexcept>
#include <thread>
#include <vector>

using Bytes = std::vector<std::uint8_t>;

static void Require(bool condition, const char* message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

static void Append(Bytes& destination, const Bytes& source) {
	destination.insert(destination.end(), source.begin(), source.end());
}

static Bytes Element(Bytes id, const Bytes& payload) {
	Require(payload.size() < 16383, "synthetic movie element exceeds its bounded encoder");
	if (payload.size() < 127) {
		id.push_back(static_cast<std::uint8_t>(0x80 | payload.size()));
	} else {
		id.push_back(static_cast<std::uint8_t>(0x40 | (payload.size() >> 8)));
		id.push_back(static_cast<std::uint8_t>(payload.size()));
	}
	Append(id, payload);

	return id;
}

static Bytes Movie(bool audio, bool invalid_packet) {
	// Independent64x64 VP9 limited-range black; this is not an extracted original asset.
	static constexpr std::array<std::uint8_t, 31> kBlack{0x82, 0x49, 0x83, 0x42, 0x00, 0x03, 0xf0, 0x03, 0xf6, 0x00, 0x38, 0x24, 0x1c, 0x18, 0x4a, 0x00, 0x00, 0x30, 0x60, 0x00, 0x00, 0x67, 0x3f, 0xff, 0xff, 0x56, 0x45, 0x64, 0xee, 0xcc, 0x00};
	Bytes track = Element({0xd7}, {1});
	Append(track, Element({0x83}, {1}));
	Append(track, Element({0x86}, {'V', '_', 'V', 'P', '9'}));
	Bytes video = Element({0xb0}, {64});
	Append(video, Element({0xba}, {64}));
	Append(track, Element({0xe0}, video));
	Bytes tracks = Element({0xae}, track);
	if (audio) {
		Bytes sound = Element({0xd7}, {2});
		Append(sound, Element({0x83}, {2}));
		Append(sound, Element({0x86}, {'A', '_', 'O', 'P', 'U', 'S'}));
		Append(tracks, Element({0xae}, sound));
	}
	const auto duration = std::bit_cast<std::uint32_t>(40.0f);
	Bytes info = Element({0x44, 0x89}, {static_cast<std::uint8_t>(duration >> 24), static_cast<std::uint8_t>(duration >> 16), static_cast<std::uint8_t>(duration >> 8), static_cast<std::uint8_t>(duration)});
	Bytes segment = Element({0x15, 0x49, 0xa9, 0x66}, info);
	Append(segment, Element({0x16, 0x54, 0xae, 0x6b}, tracks));
	Bytes cluster = Element({0xe7}, {0});
	for (std::uint8_t index = 0; index < 4; ++index) {
		Bytes block{0x81, 0, static_cast<std::uint8_t>(index * 10), 0x80};
		if (invalid_packet) {
			block.push_back(0);
		} else {
			block.insert(block.end(), kBlack.begin(), kBlack.end());
		}
		Append(cluster, Element({0xa3}, block));
	}
	Append(segment, Element({0x1f, 0x43, 0xb6, 0x75}, cluster));
	Bytes result = Element({0x1a, 0x45, 0xdf, 0xa3}, Element({0x42, 0x82}, {'w', 'e', 'b', 'm'}));
	Append(result, Element({0x18, 0x53, 0x80, 0x67}, segment));

	return result;
}

class MovieFixture {
public:
	explicit MovieFixture(Bytes bytes) {
		m_Path = std::filesystem::current_path() / ("movie-worker-fixture-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".webm");
		sgre::resource::ResourceCipher cipher(m_Path.filename().string(), "synthetic-seed", 4096);
		cipher.Transform(bytes);
		std::ofstream output(m_Path, std::ios::binary);
		output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
		Require(static_cast<bool>(output), "cannot write bounded encrypted movie fixture");
	}
	~MovieFixture() {
		std::error_code error;
		std::filesystem::remove(m_Path, error);
	}
	MovieFixture(const MovieFixture&) = delete;
	MovieFixture& operator=(const MovieFixture&) = delete;
	[[nodiscard]] const std::filesystem::path& Path() const noexcept {
		return m_Path;
	}

private:
	std::filesystem::path m_Path;
};

static sgre::media::MovieVideoRead WaitVideo(sgre::media::MoviePlaybackStream& stream) {
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
	while (std::chrono::steady_clock::now() < deadline) {
		auto result = stream.PollVideo();
		if (result.Status != sgre::media::MovieVideoReadStatus::Pending) {
			return result;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	throw std::logic_error("movie decoder worker did not produce a bounded sample/EOF/error");
}

static void TestActualWorker() {
	const MovieFixture fixture(Movie(false, false));
	sgre::media::WebmMoviePlaybackLimits limits;
	limits.QueuedVideoFrames = 1;
	auto stream = sgre::media::OpenWebmMoviePlayback(fixture.Path(), "synthetic-seed", limits);
	const auto description = stream->Description();
	Require(description.HasVideo && !description.HasAudio && description.Width == 64 && description.Height == 64 && description.Duration100ns == 400000, "actual worker lost its original track/duration identity");
	for (std::int64_t index = 0; index < 4; ++index) {
		const auto result = WaitVideo(*stream);
		Require(result.Status == sgre::media::MovieVideoReadStatus::Sample && result.Sample.Frame && result.Sample.Rgb32 && result.Sample.Timestamp100ns == index * 100000, "actual worker lost a decoded frame or signed source timestamp");
		Require(result.Sample.Rgb32->Width == 64 && result.Sample.Rgb32->Height == 64 && result.Sample.Rgb32->Bytes.size() == 16384, "actual worker did not publish converted owned RGB32");
		for (std::size_t offset = 0; offset < result.Sample.Rgb32->Bytes.size(); offset += 4) {
			Require(result.Sample.Rgb32->Bytes[offset] == 0 && result.Sample.Rgb32->Bytes[offset + 1] == 0 && result.Sample.Rgb32->Bytes[offset + 2] == 0 && result.Sample.Rgb32->Bytes[offset + 3] == 255, "actual VP9-to-RGB32 worker differs from independent black pixels");
		}
	}
	Require(WaitVideo(*stream).Status == sgre::media::MovieVideoReadStatus::EndOfStream, "actual decoder worker omitted EOF");
	Require(stream->PumpAudio(0).InputEnded && !stream->PumpAudio(0).Position100ns, "no-audio movie invented an output clock");
	stream->Close();
	stream->Close();

	auto paused = sgre::media::OpenWebmMoviePlayback(fixture.Path(), "synthetic-seed", limits);
	paused->SetPaused(true);
	paused->SetVolume(0.5f);
	paused->Close();
	Require(paused->StopRequested(), "paused actual worker did not join and close");
}

static void TestErrors() {
	const MovieFixture audio(Movie(true, false));
	bool rejected = false;
	try {
		static_cast<void>(sgre::media::OpenWebmMoviePlayback(audio.Path(), "synthetic-seed"));
	} catch (const std::runtime_error& error) {
		rejected = std::string_view(error.what()).find("Opus") != std::string_view::npos;
	}
	Require(rejected, "audio-bearing movie must not silently discard its unsupported audio track");
	const MovieFixture malformed(Movie(false, true));
	auto stream = sgre::media::OpenWebmMoviePlayback(malformed.Path(), "synthetic-seed");
	rejected = false;
	try {
		static_cast<void>(WaitVideo(*stream));
	} catch (const std::runtime_error&) {
		rejected = true;
	}
	stream->Close();
	Require(rejected, "movie worker swallowed its actual decoder failure");
}

static void TestLocalMovies(const std::filesystem::path& movie_root, const std::filesystem::path& seed_path) {
	std::ifstream input(seed_path);
	std::string seed;
	std::getline(input, seed);
	Require(input.good() || input.eof(), "cannot read supplied local seed");
	std::size_t videos = 0;
	std::size_t audio_gates = 0;
	std::size_t frames = 0;
	for (const auto& entry : std::filesystem::directory_iterator(movie_root)) {
		if (entry.path().extension() != ".webm") {
			continue;
		}
		sgre::media::WebmReader reader(entry.path(), seed);
		bool audio = false;
		for (const auto& track : reader.Tracks()) {
			audio = audio || track.Type == 2;
		}
		if (audio) {
			bool rejected = false;
			try {
				static_cast<void>(sgre::media::OpenWebmMoviePlayback(entry.path(), seed));
			} catch (const std::runtime_error& error) {
				rejected = std::string_view(error.what()).find("Opus") != std::string_view::npos;
			}
			Require(rejected, "local audio movie did not retain its explicit Opus gap");
			++audio_gates;
			continue;
		}
		auto stream = sgre::media::OpenWebmMoviePlayback(entry.path(), seed);
		for (std::size_t phase = 0; phase < 2; ++phase) {
			const auto result = WaitVideo(*stream);
			Require(result.Status == sgre::media::MovieVideoReadStatus::Sample && result.Sample.Rgb32 && result.Sample.Frame, "original movie worker did not decode its actual sample");
			++frames;
		}
		stream->SetPaused(true);
		stream->Close();
		++videos;
	}
	std::cout << "actual local movies video_only=" << videos << " decoded_samples=" << frames << " explicit_audio_gates=" << audio_gates << "; no original-pixel or whole-movie timing claim\n";
	Require(videos == 60 && audio_gates == 25, "local movie corpus differs from the supplied85-file inventory");
}

int main(int argc, char** argv) {
	try {
		if (argc == 3) {
			TestLocalMovies(argv[1], argv[2]);
		} else {
			Require(argc == 1, "movie worker test expects no arguments or actual movie directory and seed path");
			TestActualWorker();
			TestErrors();
			std::cout << "actual WebM/VP9/RGB32 worker/EOF/close/error tests passed\n";
		}

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
