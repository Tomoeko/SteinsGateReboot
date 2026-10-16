#include "sgre/media/webm.hpp"
#include "sgre/resource/resource_cipher.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
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
	Require(payload.size() < 16383, "fixture exceeds EBML size encoder");
	if (payload.size() < 127) {
		id.push_back(static_cast<std::uint8_t>(0x80 | payload.size()));
	} else {
		id.push_back(static_cast<std::uint8_t>(0x40 | (payload.size() >> 8)));
		id.push_back(static_cast<std::uint8_t>(payload.size()));
	}

	Append(id, payload);

	return id;
}

static Bytes Movie(const Bytes& blocks, bool unknown_cluster = false) {
	const auto header = Element({0x1a, 0x45, 0xdf, 0xa3}, Element({0x42, 0x82}, {'w', 'e', 'b', 'm'}));
	Bytes track = Element({0xd7}, {1});
	Append(track, Element({0x83}, {1}));
	Append(track, Element({0x86}, {'V', '_', 'V', 'P', '9'}));
	Bytes video = Element({0xb0}, {4});
	Append(video, Element({0xba}, {4}));
	Append(track, Element({0xe0}, video));
	Bytes segment = Element({0x16, 0x54, 0xae, 0x6b}, Element({0xae}, track));
	Bytes cluster = Element({0xe7}, {5});
	Append(cluster, blocks);
	if (unknown_cluster) {
		Append(segment, {0x1f, 0x43, 0xb6, 0x75, 0xff});
		Append(segment, cluster);
	} else {
		Append(segment, Element({0x1f, 0x43, 0xb6, 0x75}, cluster));
	}

	Bytes result = header;
	Append(result, Element({0x18, 0x53, 0x80, 0x67}, segment));

	return result;
}

class Fixture {
public:
	explicit Fixture(Bytes bytes, bool encrypted = false) {
		const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
		m_Path = std::filesystem::current_path() / ("webm-fixture-" + std::to_string(suffix));
		if (encrypted) {
			const sgre::resource::ResourceCipher cipher(m_Path.filename().string(), "synthetic-seed", 4096);
			cipher.Transform(bytes);
		}

		std::ofstream output(m_Path, std::ios::binary);
		output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
		if (!output) {
			throw std::runtime_error("cannot write bounded WebM fixture");
		}
	}

	~Fixture() {
		std::error_code error;
		std::filesystem::remove(m_Path, error);
	}

	Fixture(const Fixture&) = delete;
	Fixture& operator=(const Fixture&) = delete;

	const std::filesystem::path& Path() const {
		return m_Path;
	}

private:
	std::filesystem::path m_Path;
};

static void CheckSimple(sgre::media::WebmReader& reader) {
	Require(reader.Tracks().size() == 1, "track count differs");
	const auto& track = reader.Tracks().front();
	Require(track.Number == 1 && track.Codec == "V_VP9" && track.Width == 4 && track.Height == 4, "track identity/dimensions differ");
	const auto packet = reader.NextPacket();
	Require(packet && packet->TimestampNanoseconds == 3000000 && packet->KeyFrame, "signed relative timestamp or key flag differs");
	Require(packet->Frames == std::vector<Bytes>{{0xaa, 0xbb}}, "block payload differs");
	Require(!reader.NextPacket(), "reader found an invented packet");
	reader.Rewind();
	Require(reader.NextPacket()->Frames == packet->Frames, "rewind changed payload");
}

static void TestSimpleAndEncrypted() {
	const auto bytes = Movie(Element({0xa3}, {0x81, 0xff, 0xfe, 0x80, 0xaa, 0xbb}));
	const Fixture fixture(bytes);
	sgre::media::WebmReader reader(fixture.Path());
	CheckSimple(reader);
	const Fixture encrypted(bytes, true);
	sgre::media::WebmReader decrypted(encrypted.Path(), "synthetic-seed");
	CheckSimple(decrypted);
	const Fixture unknown(Movie(Element({0xa3}, {0x81, 0xff, 0xfe, 0x80, 0xaa, 0xbb}), true));
	sgre::media::WebmReader unknown_reader(unknown.Path());
	CheckSimple(unknown_reader);
}

static void TestLaces() {
	const std::vector<Bytes> laces{{2, 2, 3, 'a', 'b', 'c', 'd', 'e', 'f'}, {2, 'a', 'b', 'c', 'd', 'e', 'f'}, {2, 0x82, 0xc0, 'a', 'b', 'c', 'd', 'e', 'f'}};
	for (std::size_t mode = 1; mode <= 3; ++mode) {
		Bytes block{0x81, 0, 0, static_cast<std::uint8_t>(0x80 | (mode << 1))};
		Append(block, laces[mode - 1]);
		const Fixture fixture(Movie(Element({0xa3}, block)));
		sgre::media::WebmReader reader(fixture.Path());
		const auto packet = reader.NextPacket();
		Require(packet && packet->Frames.size() == 3, "lace frame count differs");
		if (mode == 2) {
			Require(packet->Frames == std::vector<Bytes>{{'a', 'b'}, {'c', 'd'}, {'e', 'f'}}, "fixed lace differs");
		} else {
			Require(packet->Frames == std::vector<Bytes>{{'a', 'b'}, {'c', 'd', 'e'}, {'f'}}, "variable lace differs");
		}
	}
}

static void TestGroup() {
	Bytes group = Element({0xa1}, {0x81, 0, 0, 0, 7});
	Append(group, Element({0xfb}, {0xff}));
	Append(group, Element({0x9b}, {2}));
	Append(group, Element({0x75, 0xa2}, {0xff}));
	Append(group, Element({0x75, 0xa1}, Element({0xa6}, Element({0xa5}, {9, 8}))));
	const Fixture fixture(Movie(Element({0xa0}, group)));
	sgre::media::WebmReader reader(fixture.Path());
	const auto packet = reader.NextPacket();
	Require(packet && !packet->KeyFrame && packet->DurationNanoseconds == 2000000 && packet->DiscardPaddingNanoseconds == -1, "BlockGroup duration/reference/discard padding differs");
	Require(packet->Additions.size() == 1 && packet->Additions.front().Id == 1 && packet->Additions.front().Bytes == Bytes{9, 8}, "block addition differs");
}

static void ExpectRejected(const Bytes& bytes, sgre::media::WebmLimits limits = {}) {
	const Fixture fixture(bytes);
	bool rejected = false;
	try {
		sgre::media::WebmReader reader(fixture.Path(), limits);
		while (reader.NextPacket()) {
		}
	} catch (const std::runtime_error&) {
		rejected = true;
	}

	Require(rejected, "malformed or excessive WebM was accepted");
}

static void TestMalformed() {
	const auto valid = Movie(Element({0xa3}, {0x81, 0, 0, 0x80, 1, 2}));
	for (std::size_t length = 0; length < valid.size(); ++length) {
		ExpectRejected(Bytes(valid.begin(), valid.begin() + static_cast<std::ptrdiff_t>(length)));
	}

	ExpectRejected(Movie(Element({0xa3}, {0x82, 0, 0, 0x80, 1})));
	ExpectRejected(Movie(Element({0xa3}, {0x81, 0, 0, 0x84, 2, 1, 2, 3, 4})));
	ExpectRejected(Movie(Element({0xa3}, {0x81, 0, 0, 0x82, 2, 255})));
	ExpectRejected(Movie(Element({0xa3}, {0x81, 0, 0, 0x86, 2, 0x80, 0x80})));
	sgre::media::WebmLimits limits;
	limits.MaxPacketBytes = 4;
	ExpectRejected(valid, limits);
	limits = {};
	limits.MaxElements = 1;
	ExpectRejected(valid, limits);
}

int main() {
	try {
		TestSimpleAndEncrypted();
		TestLaces();
		TestGroup();
		TestMalformed();
		std::cout << "WebM tests passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
