#include "sgre/audio/buffered_sound_source_data.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
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

template <typename Operation>
static void RequireFailure(Operation operation, std::string_view expected) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(expected) != std::string_view::npos, error.what());

		return;
	}

	throw std::runtime_error("buffered source fixture expected failure: " + std::string(expected));
}

static sgre::resource::PsbValue Integer(std::int64_t number) {
	sgre::resource::PsbValue result;
	result.Data = number;

	return result;
}

static sgre::resource::PsbValue Number(double number) {
	sgre::resource::PsbValue result;
	result.Data = number;

	return result;
}

static sgre::resource::PsbValue Object(sgre::resource::PsbObject fields) {
	sgre::resource::PsbValue result;
	result.Data = std::move(fields);

	return result;
}

static sgre::resource::PsbValue Array(sgre::resource::PsbArray elements) {
	sgre::resource::PsbValue result;
	result.Data = std::move(elements);

	return result;
}

static sgre::resource::PsbValue Chunk(std::uint32_t index, bool extra = false) {
	sgre::resource::PsbChunkReference reference;
	reference.Index = index;
	reference.Extra = extra;
	sgre::resource::PsbValue result;
	result.Data = reference;

	return result;
}

static void AppendWord(std::vector<std::uint8_t>& bytes, std::uint16_t word) {
	bytes.push_back(static_cast<std::uint8_t>(word));
	bytes.push_back(static_cast<std::uint8_t>(word >> 8));
}

static void AppendDword(std::vector<std::uint8_t>& bytes, std::uint32_t word) {
	for (std::size_t shift = 0; shift < 32; shift += 8) {
		bytes.push_back(static_cast<std::uint8_t>(word >> shift));
	}
}

struct SourceFixture {
	sgre::resource::PsbDocument Document;
	std::vector<std::uint8_t> Bytes;

	explicit SourceFixture(std::uint16_t tag, bool extra_chunks = false) {
		AppendWord(Bytes, tag);
		AppendWord(Bytes, 1);
		AppendDword(Bytes, 48000);
		AppendDword(Bytes, 96000);
		AppendWord(Bytes, 4);
		AppendWord(Bytes, 16);
		AppendWord(Bytes, tag == 2 ? 2 : 0);
		if (tag == 2) {
			AppendWord(Bytes, 64);
		}

		sgre::resource::PsbChunkRange format;
		format.Size = Bytes.size();
		const auto format_reference = Chunk(0, extra_chunks);
		if (extra_chunks) {
			Document.ExtraChunks.push_back(format);
		} else {
			Document.Chunks.push_back(format);
		}

		sgre::resource::PsbChunkRange data;
		data.Offset = Bytes.size();
		data.Size = 8;
		const auto data_reference = Chunk(static_cast<std::uint32_t>(Document.Chunks.size()));
		Document.Chunks.push_back(data);
		Bytes.insert(Bytes.end(), {0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0});
		sgre::resource::PsbObject architecture{{"fmt", format_reference}, {"data", data_reference}, {"nosrc", Integer(1)}, {"background", Number(1.5)}};
		if (tag == 0x161 || tag == 0x162) {
			sgre::resource::PsbChunkRange table;
			table.Offset = Bytes.size();
			table.Size = 8;
			architecture.emplace_back("dpds", Chunk(static_cast<std::uint32_t>(Document.ExtraChunks.size()), true));
			Document.ExtraChunks.push_back(table);
			AppendDword(Bytes, 8);
			AppendDword(Bytes, 16);
		}

		const auto channel = Object({{"archData", Object(std::move(architecture))}, {"pan", Array({Number(0.25), Number(0.75)})}});
		const auto voice = Object({{"device", Integer(1)}, {"channelList", Array({channel})}});
		const auto deferred = Object({{"device", Integer(0)}, {"channelList", Array({Object({{"archData", Object({{"fmt", Integer(0)}})}})})}});
		Document.Root = Object({{"voice", Object({{"selected", voice}, {"deferred", deferred}})}});
		Document.Header.Version = extra_chunks || !Document.ExtraChunks.empty() ? 4 : 3;
		Document.SerializedSize = Bytes.size();
	}

	[[nodiscard]] sgre::resource::PsbObject& Architecture() {
		auto& voices = std::get<sgre::resource::PsbObject>(std::get<sgre::resource::PsbObject>(Document.Root.Data).front().second.Data);
		auto& channels = std::get<sgre::resource::PsbArray>(std::get<sgre::resource::PsbObject>(voices.front().second.Data).back().second.Data);

		return std::get<sgre::resource::PsbObject>(std::get<sgre::resource::PsbObject>(channels.front().Data).front().second.Data);
	}

	[[nodiscard]] std::unique_ptr<sgre::io::PublishedPsbFile> Publish() {
		return std::make_unique<sgre::io::PublishedPsbFile>(std::move(Bytes), std::move(Document));
	}
};

static void TestExactResourceBorrowsAndDeferredParsing() {
	SourceFixture fixture(1);
	const auto file = fixture.Publish();
	const auto definitions = sgre::resource::ReadSoundArchiveDefinitions(file->Document());
	const auto& voice = definitions.at("selected");
	Require(&voice.Channels.front().Document.get() == &file->Document(), "native channel value handle must retain the exact archive document owner");
	const auto source = sgre::audio::ReadBufferedSoundSourceData(*file, voice, 0, 0);
	Require(source.Pool == sgre::audio::SoundVoiceType::Buffered8 && source.Description.Format.Encoding == sgre::resource::WaveEncoding::Pcm && source.Description.Format.Channels == 1 && source.Description.Format.SamplesPerSecond == 48000, "buffered source must use the deferred definition's actual pool and independent WAVEFORMATEX vector");
	Require(source.FormatBytes.data() == file->Bytes().data() && source.EncodedBytes.data() == file->Bytes().data() + 18 && source.EncodedBytes.size() == 8 && source.DecodedPacketBytes.empty(), "fmt/data source spans must borrow actual backing without payload copies");
	Require(source.Description.Pan == std::array<float, 8>{0.25f, 0.75f, 0, 0, 0, 0, 0, 0} && source.Description.Background && source.Submission.DisableRateConversion && source.Submission.LoopRepeats == 0 && source.Submission.EncodedBytes == 8, "native source pan/nosrc/count decisions differ");
	Require(definitions.size() == 2, "unused malformed fmt must remain deferred through archive definition parsing");
	RequireFailure([&] {
		static_cast<void>(sgre::audio::ReadBufferedSoundSourceData(*file, definitions.at("deferred"), 0, 1));
	},
	    "chunk for fmt");

	SourceFixture other_fixture(1);
	const auto other = other_fixture.Publish();
	RequireFailure([&] {
		static_cast<void>(sgre::audio::ReadBufferedSoundSourceData(*other, voice, 0, 1));
	},
	    "exact archive document");
}

static void TestAdpcmAndLoopConversion() {
	SourceFixture fixture(2, true);
	fixture.Architecture().emplace_back("loop", Array({Number(77.9), Integer(130), Object({})}));
	const auto file = fixture.Publish();
	const auto definitions = sgre::resource::ReadSoundArchiveDefinitions(file->Document());
	const auto source = sgre::audio::ReadBufferedSoundSourceData(*file, definitions.at("selected"), 0, 0);
	Require(source.FormatBytes.data() == file->Bytes().data() && source.Description.Format.Extra == std::vector<std::uint8_t>{64, 0} && source.Description.Loop->Begin == 77 && source.Description.Loop->Length == 130, "extra fmt and first-two sample loop numeric conversion differ");
	Require(source.Submission.LoopBegin == 64 && source.Submission.LoopLength == 128 && source.Submission.LoopRepeats == 255, "native ADPCM loop quantization/infinite repeat differs");
	Require(sgre::audio::ReadBufferedSoundSourceData(*file, definitions.at("selected"), 0, 3).Submission.LoopRepeats == 2, "positive play count must retain count minus one");
}

static void TestWmaTablesAndBorrowedPacketBytes() {
	for (const auto tag : std::array<std::uint16_t, 2>{0x161, 0x162}) {
		SourceFixture fixture(tag, true);
		const auto file = fixture.Publish();
		const auto definitions = sgre::resource::ReadSoundArchiveDefinitions(file->Document());
		const auto source = sgre::audio::ReadBufferedSoundSourceData(*file, definitions.at("selected"), 0, -1);
		Require(source.DecodedPacketBytes.data() == file->Bytes().data() + 26 && source.DecodedPacketBytes.size() == 8 && source.Description.DecodedPacketCumulativeBytes == std::vector<std::uint32_t>{8, 16} && source.Submission.WmaPackets == 2 && source.Submission.LoopRepeats == 255, "native WMA dpds pointer/count and preserved cumulative words differ");
	}

	SourceFixture decreasing(0x161);
	decreasing.Bytes[30] = 4;
	const auto file = decreasing.Publish();
	const auto definitions = sgre::resource::ReadSoundArchiveDefinitions(file->Document());
	RequireFailure([&] {
		static_cast<void>(sgre::audio::ReadBufferedSoundSourceData(*file, definitions.at("selected"), 0, 1));
	},
	    "table decreases");
}

static void TestNativeScalarNormalizationAndRateGate() {
	const std::array<std::pair<std::int64_t, bool>, 8> integers{{{0, false}, {1, true}, {-1, true}, {256, true}, {-256, true}, {257, true}, {4294967296, true}, {std::numeric_limits<std::int64_t>::min(), true}}};
	for (const auto& [integer, expected] : integers) {
		SourceFixture fixture(1);
		fixture.Architecture()[2].second = Integer(integer);
		const auto file = fixture.Publish();
		const auto definitions = sgre::resource::ReadSoundArchiveDefinitions(file->Document());
		Require(sgre::audio::ReadBufferedSoundSourceData(*file, definitions.at("selected"), 0, 1).Submission.DisableRateConversion == expected, "native flag accessor must normalize the full signed integer to a boolean");
	}

	SourceFixture lower_rate(1);
	lower_rate.Bytes[4] = 0x44;
	lower_rate.Bytes[5] = 0xac;
	const auto file = lower_rate.Publish();
	const auto definitions = sgre::resource::ReadSoundArchiveDefinitions(file->Document());
	Require(!sgre::audio::ReadBufferedSoundSourceData(*file, definitions.at("selected"), 0, 1).Submission.DisableRateConversion, "nosrc must not disable conversion at 44100Hz");
}

class ScalarReader {
public:
	explicit ScalarReader(const char* path) {
		std::ifstream input(path, std::ios::binary | std::ios::ate);
		Require(static_cast<bool>(input), "cannot open original accessor scalar corpus");
		const auto size = input.tellg();
		Require(size >= 8 && size <= 8192, "original accessor scalar corpus exceeds its bounded extent");
		m_Bytes.resize(static_cast<std::size_t>(size));
		input.seekg(0);
		input.read(m_Bytes.data(), static_cast<std::streamsize>(m_Bytes.size()));
		Require(static_cast<bool>(input) && std::string_view(m_Bytes.data(), 4) == "MWB1", "original accessor scalar corpus read/magic failed");
	}

	[[nodiscard]] std::uint64_t Word(std::size_t width) {
		Require(width <= 8 && m_Position <= m_Bytes.size() && width <= m_Bytes.size() - m_Position, "original accessor scalar corpus is truncated");
		std::uint64_t value = 0;
		for (std::size_t byte = 0; byte < width; ++byte) {
			value |= static_cast<std::uint64_t>(static_cast<std::uint8_t>(m_Bytes[m_Position++])) << (byte * 8);
		}

		return value;
	}

	[[nodiscard]] bool Complete() const {
		return m_Position == m_Bytes.size();
	}

private:
	std::vector<char> m_Bytes;
	std::size_t m_Position = 4;
};

static void ReplayOriginalScalarCorpus(const char* path) {
	ScalarReader reader(path);
	Require(reader.Word(4) == 154, "original accessor scalar corpus vector count changed");
	for (std::size_t vector = 0; vector < 154; ++vector) {
		const auto tag = reader.Word(4);
		const auto bits = reader.Word(8);
		const auto accessor = reader.Word(4);
		const auto control = reader.Word(4);
		Require(accessor <= 1 && accessor == control, "original accessor and full control-decoder result differ");
		sgre::resource::PsbValue scalar;
		if (tag == 2 || tag == 3) {
			scalar.Data = tag == 2;
		} else if (tag >= 4 && tag <= 12) {
			scalar.Data = std::bit_cast<std::int64_t>(bits);
		} else if (tag == 0x1d) {
			scalar.Data = 0.0;
		} else if (tag == 0x1e) {
			scalar.Data = static_cast<double>(std::bit_cast<float>(static_cast<std::uint32_t>(bits)));
		} else {
			Require(tag == 0x1f, "original accessor scalar corpus has an unsupported atom tag");
			scalar.Data = std::bit_cast<double>(bits);
		}

		SourceFixture fixture(1);
		fixture.Architecture()[2].second = scalar;
		fixture.Architecture()[3].second = scalar;
		const auto file = fixture.Publish();
		const auto definitions = sgre::resource::ReadSoundArchiveDefinitions(file->Document());
		const auto source = sgre::audio::ReadBufferedSoundSourceData(*file, definitions.at("selected"), 0, 1);
		Require(source.Description.DisableSampleRateConversion == (accessor != 0) && source.Submission.DisableRateConversion == (accessor != 0) && source.Description.Background == (accessor != 0), "buffered source flags differ from actual original accessor scalar vector " + std::to_string(vector));
	}

	Require(reader.Complete(), "original accessor scalar corpus has trailing bytes");
	std::cout << "buffered sound source flags: original154 scalar vectors passed\n";
}

static void TestStrictOwnersPoolsAndMalformedBacking() {
	SourceFixture fixture(1);
	const auto bytes = fixture.Bytes;
	const auto document = fixture.Document;
	for (std::size_t length = 0; length < bytes.size(); ++length) {
		const sgre::io::PublishedPsbFile file(std::vector<std::uint8_t>(bytes.begin(), bytes.begin() + length), document);
		const auto definitions = sgre::resource::ReadSoundArchiveDefinitions(file.Document());
		RequireFailure([&] {
			static_cast<void>(sgre::audio::ReadBufferedSoundSourceData(file, definitions.at("selected"), 0, 1));
		},
		    "complete actual");
	}

	const auto file = fixture.Publish();
	auto definitions = sgre::resource::ReadSoundArchiveDefinitions(file->Document());
	for (const auto device : std::array<std::int32_t, 3>{-1, 2, 3}) {
		definitions.at("selected").Device = device;
		RequireFailure([&] {
			static_cast<void>(sgre::audio::ReadBufferedSoundSourceData(*file, definitions.at("selected"), 0, 1));
		},
		    "pool0/1");
	}
	definitions.at("selected").Device = 0;
	RequireFailure([&] {
		static_cast<void>(sgre::audio::ReadBufferedSoundSourceData(*file, definitions.at("selected"), 1, 1));
	},
	    "pool0/1");

	SourceFixture huge_loop(1);
	huge_loop.Architecture().emplace_back("loop", Array({Number(std::numeric_limits<double>::infinity()), Integer(1)}));
	const auto invalid = huge_loop.Publish();
	const auto invalid_definitions = sgre::resource::ReadSoundArchiveDefinitions(invalid->Document());
	RequireFailure([&] {
		static_cast<void>(sgre::audio::ReadBufferedSoundSourceData(*invalid, invalid_definitions.at("selected"), 0, 1));
	},
	    "finite nonnegative");

	SourceFixture bad_range(1);
	bad_range.Document.Chunks.back().Offset = std::numeric_limits<std::uint64_t>::max();
	const auto ranged = bad_range.Publish();
	const auto ranged_definitions = sgre::resource::ReadSoundArchiveDefinitions(ranged->Document());
	RequireFailure([&] {
		static_cast<void>(sgre::audio::ReadBufferedSoundSourceData(*ranged, ranged_definitions.at("selected"), 0, 1));
	},
	    "actual archive backing for data");

	SourceFixture bad_index(1, true);
	bad_index.Architecture().front().second = Chunk(1, true);
	const auto indexed = bad_index.Publish();
	const auto indexed_definitions = sgre::resource::ReadSoundArchiveDefinitions(indexed->Document());
	RequireFailure([&] {
		static_cast<void>(sgre::audio::ReadBufferedSoundSourceData(*indexed, indexed_definitions.at("selected"), 0, 1));
	},
	    "actual document table for fmt");

	SourceFixture unsupported(0x163);
	const auto tagged = unsupported.Publish();
	const auto tagged_definitions = sgre::resource::ReadSoundArchiveDefinitions(tagged->Document());
	RequireFailure([&] {
		static_cast<void>(sgre::audio::ReadBufferedSoundSourceData(*tagged, tagged_definitions.at("selected"), 0, 1));
	},
	    "unsupported recovered WAVE format tag");
}

int main(int argc, char** argv) {
	try {
		TestExactResourceBorrowsAndDeferredParsing();
		TestAdpcmAndLoopConversion();
		TestWmaTablesAndBorrowedPacketBytes();
		TestNativeScalarNormalizationAndRateGate();
		TestStrictOwnersPoolsAndMalformedBacking();
		if (argc == 2) {
			ReplayOriginalScalarCorpus(argv[1]);
		} else {
			Require(argc == 1, "buffered source fixture accepts one optional original scalar corpus path");
		}
		std::cout << "buffered sound source data: 5 fixture groups passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
