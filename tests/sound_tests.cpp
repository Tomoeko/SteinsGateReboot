#include "sgre/resource/sound.hpp"

#include <array>
#include <iostream>
#include <stdexcept>
#include <utility>

using Bytes = std::vector<std::uint8_t>;

static void Require(bool condition, const char* message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

static sgre::resource::PsbValue Text(std::string text) {
	sgre::resource::PsbValue value;
	value.Data = std::move(text);

	return value;
}

static sgre::resource::PsbValue Integer(std::int64_t integer) {
	sgre::resource::PsbValue value;
	value.Data = integer;

	return value;
}

static sgre::resource::PsbValue Number(double number) {
	sgre::resource::PsbValue value;
	value.Data = number;

	return value;
}

static sgre::resource::PsbValue Object(sgre::resource::PsbObject object) {
	sgre::resource::PsbValue value;
	value.Data = std::move(object);

	return value;
}

static sgre::resource::PsbValue Array(sgre::resource::PsbArray array) {
	sgre::resource::PsbValue value;
	value.Data = std::move(array);

	return value;
}

static sgre::resource::PsbValue Chunk(std::uint32_t index) {
	sgre::resource::PsbChunkReference reference;
	reference.Index = index;
	sgre::resource::PsbValue value;
	value.Data = reference;

	return value;
}

static Bytes FormatBytes() {
	return {0x61, 1, 1, 0, 0x44, 0xac, 0, 0, 0xa0, 0x0f, 0, 0, 4, 0, 16, 0, 0, 0};
}

static void TestWaveHeader() {
	const auto bytes = FormatBytes();
	const auto format = sgre::resource::ReadWaveFormat(bytes);
	Require(format.Encoding == sgre::resource::WaveEncoding::WmaStandard && format.Channels == 1 && format.SamplesPerSecond == 44100 && format.AverageBytesPerSecond == 4000 && format.BlockAlignment == 4 && format.BitsPerSample == 16 && format.Extra.empty(), "independent WAVEFORMATEX vector differs");
	for (std::size_t size = 0; size < bytes.size(); ++size) {
		bool rejected = false;
		try {
			static_cast<void>(sgre::resource::ReadWaveFormat(std::span<const std::uint8_t>(bytes).first(size)));
		} catch (const std::runtime_error&) {
			rejected = true;
		}

		Require(rejected, "truncated compressed WAVEFORMATEX was accepted");
	}
}

static sgre::resource::PsbDocument Document() {
	sgre::resource::PsbValue version;
	version.Data = 1.02;
	const auto data = Object({{"fmt", Chunk(0)}, {"dpds", Chunk(1)}, {"data", Chunk(2)}, {"loop", Array({Integer(1), Integer(2)})}});
	const auto entry = Object({{"loop", Integer(0)}, {"device", Integer(1)}, {"quality", Integer(1)}, {"type", Integer(1)}, {"channelList", Array({Object({{"archData", data}})})}});
	sgre::resource::PsbDocument document;
	document.Root = Object({{"id", Text("sound_archive")}, {"spec", Text("win")}, {"version", version}, {"voice", Object({{"synthetic", entry}})}});
	for (const auto& [offset, size] : std::vector<std::pair<std::uint64_t, std::uint64_t>>{{0, 18}, {18, 8}, {26, 8}}) {
		sgre::resource::PsbChunkRange range;
		range.Offset = offset;
		range.Size = size;
		document.Chunks.push_back(range);
	}

	return document;
}

static void TestArchiveAndTables() {
	auto bytes = FormatBytes();
	bytes.insert(bytes.end(), {8, 0, 0, 0, 16, 0, 0, 0});
	bytes.resize(34, 0);
	const auto document = Document();
	const auto archive = sgre::resource::ReadSoundArchive(document, bytes);
	Require(archive.Entries.size() == 1 && archive.Entries.front().Name == "synthetic", "sound entry identity differs");
	const auto& channel = archive.Entries.front().Channels.front();
	Require(channel.DecodedPacketCumulativeBytes == std::vector<std::uint32_t>{8, 16} && channel.EncodedData.Offset == 26 && channel.EncodedData.Size == 8 && channel.Loop && channel.Loop->Begin == 1 && channel.Loop->Length == 2, "sound packet/loop metadata differs");
	Require(archive.Entries.front().Volume == 1.0f && archive.Entries.front().PitchOctaves == 0.0f && archive.Entries.front().Priority == 0.0f && archive.Entries.front().Group.empty() && archive.Entries.front().Uniqueness.empty() && channel.Pan == std::array<float, 8>{1, 1, 0, 0, 0, 0, 0, 0}, "native absent-field sound defaults differ");

	bytes[22] = 4;
	bool rejected = false;
	try {
		static_cast<void>(sgre::resource::ReadSoundArchive(document, bytes));
	} catch (const std::runtime_error&) {
		rejected = true;
	}

	Require(rejected, "decreasing decoded-packet table was accepted");
	bytes[22] = 16;
	bytes.pop_back();
	rejected = false;
	try {
		static_cast<void>(sgre::resource::ReadSoundArchive(document, bytes));
	} catch (const std::runtime_error&) {
		rejected = true;
	}

	Require(rejected, "out-of-bounds audio data was accepted");
}

static void TestPlaybackMetadata() {
	auto bytes = FormatBytes();
	bytes.insert(bytes.end(), {8, 0, 0, 0, 16, 0, 0, 0});
	bytes.resize(34, 0);
	auto document = Document();
	auto& voices = std::get<sgre::resource::PsbObject>(std::get<sgre::resource::PsbObject>(document.Root.Data).back().second.Data);
	auto& entry = std::get<sgre::resource::PsbObject>(voices.front().second.Data);
	entry.emplace_back("volume", Number(-0.75));
	entry.emplace_back("pitch", Number(1.25));
	entry.emplace_back("priority", Integer(9));
	entry.emplace_back("group", Text("SyntheticGroup"));
	entry.emplace_back("uniq", Text("CaseSensitiveKey"));
	auto& channels = std::get<sgre::resource::PsbArray>(entry[4].second.Data);
	auto& channel = std::get<sgre::resource::PsbObject>(channels.front().Data);
	channel.emplace_back("pan", Array({Number(-0.5), Number(2.0), Text("unused native tail")}));
	const auto archive = sgre::resource::ReadSoundArchive(document, bytes);
	const auto& result = archive.Entries.front();
	Require(result.Volume == -0.75f && result.PitchOctaves == 1.25f && result.Priority == 9.0f && result.Group == "SyntheticGroup" && result.Uniqueness == "CaseSensitiveKey", "sound playback metadata differs or names were normalized");
	Require(result.Channels.front().Pan == std::array<float, 8>{-0.5f, 2.0f, 0, 0, 0, 0, 0, 0}, "native two-coefficient channel pan differs");
	channel.back().second = Array({Number(1.0)});
	bool rejected = false;
	try {
		static_cast<void>(sgre::resource::ReadSoundArchive(document, bytes));
	} catch (const std::runtime_error&) {
		rejected = true;
	}

	Require(rejected, "short sound channel pan was accepted");
	channel.pop_back();
	entry.back().second = Integer(1);
	rejected = false;
	try {
		static_cast<void>(sgre::resource::ReadSoundArchive(document, bytes));
	} catch (const std::runtime_error&) {
		rejected = true;
	}

	Require(rejected, "numeric sound uniqueness key was accepted");
}

int main() {
	try {
		TestWaveHeader();
		TestArchiveAndTables();
		TestPlaybackMetadata();
		std::cout << "sound metadata tests passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
