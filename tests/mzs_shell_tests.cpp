#include "sgre/resource/mzs_shell.hpp"

#include <array>
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

static void Expect(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

static std::uint8_t HexDigit(char character) {
	if (character >= '0' && character <= '9') {
		return static_cast<std::uint8_t>(character - '0');
	}

	if (character >= 'a' && character <= 'f') {
		return static_cast<std::uint8_t>(character - 'a' + 10);
	}

	throw std::runtime_error("Invalid test-vector digit");
}

static std::vector<std::uint8_t> FromHex(std::string_view hexadecimal) {
	Expect(hexadecimal.size() % 2 == 0, "Invalid test-vector length");
	std::vector<std::uint8_t> bytes(hexadecimal.size() / 2);

	for (std::size_t index = 0; index < bytes.size(); ++index) {
		bytes[index] = static_cast<std::uint8_t>((HexDigit(hexadecimal[index * 2]) << 4) | HexDigit(hexadecimal[index * 2 + 1]));
	}

	return bytes;
}

static void ExpectDecodeFailure(std::span<const std::uint8_t> bytes, std::string_view filename, std::string_view seed, std::size_t period, std::size_t max_decoded_size, std::string_view message) {
	try {
		static_cast<void>(sgre::resource::DecodeMzsShell(bytes, filename, seed, period, max_decoded_size));
	} catch (const std::runtime_error&) {
		return;
	}

	throw std::runtime_error(std::string(message));
}

static void TestRfcMd5ShellVectors() {
	// Independent wrappers use the seven RFC 1321 messages and MT19937 init_by_array.
	static constexpr std::array<std::string_view, 7> kSeeds{"", "a", "abc", "message digest", "abcdefghijklmnopqrstuvwxyz", "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789", "12345678901234567890123456789012345678901234567890123456789012345678901234567890"};
	static constexpr std::array<std::string_view, 7> kWrappers{"6d7a730000100000fdaef39702f4104192d42fe7aa312fb3", "6d7a7300001000005f845f7aaed24539176d136965f0c01a", "6d7a730000100000690ca16f5f99d3b76f0658e36c4de240", "6d7a73000010000034ab9a9a5ddaf3538477d8e61024842d", "6d7a730000100000e046a9c3480357fc31860178ad1e5ba4", "6d7a730000100000147864aeebdf1df5fb7d3454c125fb89", "6d7a730000100000625924086535a435add5d9cf54145a29"};
	const std::vector<std::uint8_t> expected = FromHex("28b52ffd000102030405060708090a0b");

	for (std::size_t index = 0; index < kSeeds.size(); ++index) {
		const std::vector<std::uint8_t> wrapper = FromHex(kWrappers[index]);
		const sgre::resource::MzsPayload payload = sgre::resource::DecodeMzsShell(wrapper, "", kSeeds[index], 131, 4096);
		Expect(payload.DecodedSize == 4096, "MZS decoded-size field changed");
		Expect(payload.CompressedBytes == expected, "RFC MD5 shell vector mismatch");
		Expect(wrapper == FromHex(kWrappers[index]), "MZS decoder changed its input bytes");
	}
}

static void TestFilenameNormalizationAndPeriod() {
	const std::vector<std::uint8_t> wrapper = FromHex("6d7a7300403020103074f8b6aec9dbb69db1f1c97e25b4906d08b5022a49599439c657c88ce2871ec9a06b216733bd8647abad9aabeb5c17a3cf587676fb93d729d833cf70b67d0253d5e82e8483269199d4678961af4fc796ad9910182c2992fa46115867c0c57e2765fea505dd13286309b7e240b45a012eac5d6afc0c5f926f11d62cd0c5a4049903f3674156c92d4c5c331e3c7e44fda03315ee952a9fa9ccde11ba4bd8450f67009b4a1dd49ce4b63a03c4262217286edb922052c7ebf57e1452aa55bc42f333fa87d02817d30706a1141a59e804e22ac8421530068d9ba9ae1779cb9ed5e44542fba4d84118865894ade084386fc331dd84ad31c2f77f89d817ec9c59a1534023811a7e8c1ac2d34ea8cfd1bc93bffbc37823ae8a7316af182c4f539e37c85dc28ae4bd24f79e511b6135b78c49a5");
	std::vector<std::uint8_t> expected = FromHex("28b52ffd");

	for (std::size_t index = 0; index < 300; ++index) {
		expected.push_back(static_cast<std::uint8_t>(index));
	}

	static constexpr std::array<std::string_view, 5> kFilenames{"test.mzs", "TEST.MZS", "/different/directory/Test.Mzs", "C:\\different\\directory\\TEST.MZS", "dir\\Sub/TEST.MZS"};

	for (const std::string_view filename : kFilenames) {
		const sgre::resource::MzsPayload payload = sgre::resource::DecodeMzsShell(wrapper, filename, "synthetic-seed", 131, 0x10203040);
		Expect(payload.DecodedSize == 0x10203040, "MZS size is not little endian");
		Expect(payload.CompressedBytes == expected, "MZS basename normalization or period repetition mismatch");
	}

	const std::vector<std::uint8_t> short_expected = FromHex("28b52ffd000102030405060708090a0b");
	const std::vector<std::uint8_t> period_one = FromHex("6d7a73001000000030ad37e518191a1b1c1d1e1f10111213");
	const std::vector<std::uint8_t> period_five = FromHex("6d7a7300100000003074f8b6ae19c3d44fab1ec6df42a413");
	const std::vector<std::uint8_t> period_large = FromHex("6d7a7300100000003074f8b6aec9dbb69db1f1c97e25b490");
	Expect(sgre::resource::DecodeMzsShell(period_one, "test.mzs", "synthetic-seed", 1, 16).CompressedBytes == short_expected, "One-byte MZS period mismatch");
	Expect(sgre::resource::DecodeMzsShell(period_five, "test.mzs", "synthetic-seed", 5, 16).CompressedBytes == short_expected, "Partial-word MZS period mismatch");
	Expect(sgre::resource::DecodeMzsShell(period_large, "test.mzs", "synthetic-seed", 64, 16).CompressedBytes == short_expected, "MZS period longer than payload mismatch");
	Expect(sgre::resource::DecodeMzsShell(period_large, "test.mzs", "synthetic-seed", std::numeric_limits<std::size_t>::max(), 16).CompressedBytes == short_expected, "Unbounded caller period allocated beyond the payload");
}

static void TestMalformedShells() {
	const std::vector<std::uint8_t> wrapper = FromHex("6d7a7300100000003074f8b6aec9dbb69db1f1c97e25b490");

	for (std::size_t size = 0; size < 12; ++size) {
		ExpectDecodeFailure(std::span<const std::uint8_t>(wrapper).first(size), "test.mzs", "synthetic-seed", 131, 16, "Truncated MZS wrapper was accepted");
	}

	for (std::size_t index = 0; index < 4; ++index) {
		std::vector<std::uint8_t> invalid = wrapper;
		invalid[index] ^= 1;
		ExpectDecodeFailure(invalid, "test.mzs", "synthetic-seed", 131, 16, "Invalid MZS magic was accepted");
	}

	ExpectDecodeFailure(wrapper, "test.mzs", "synthetic-seed", 0, 16, "Zero MZS period was accepted");
	ExpectDecodeFailure(wrapper, "test.mzs", "synthetic-seed", 131, 15, "MZS decoded-size limit was ignored");
	ExpectDecodeFailure(wrapper, "other.mzs", "synthetic-seed", 131, 16, "Incorrect MZS filename was accepted");
	ExpectDecodeFailure(wrapper, "test.mzs", "incorrect-seed", 131, 16, "Incorrect MZS seed was accepted");

	std::vector<std::uint8_t> zero_size = wrapper;
	zero_size[4] = 0;
	Expect(sgre::resource::DecodeMzsShell(zero_size, "test.mzs", "synthetic-seed", 131, 0).DecodedSize == 0, "Zero decoded-size MZS wrapper was rejected");

	std::vector<std::uint8_t> corrupted = wrapper;
	corrupted[8] ^= 1;
	ExpectDecodeFailure(corrupted, "test.mzs", "synthetic-seed", 131, 16, "Invalid recovered Zstandard magic was accepted");
}

int main() {
	try {
		TestRfcMd5ShellVectors();
		TestFilenameNormalizationAndPeriod();
		TestMalformedShells();
	} catch (const std::exception& error) {
		std::fprintf(stderr, "MZS shell tests failed: %s\n", error.what());
		return 1;
	}

	std::puts("MZS shell tests passed");

	return 0;
}
