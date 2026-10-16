#include "sgre/scene/motion_seed_buffer.hpp"

#include "sgre/io/file_reader.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

static void TestInitialBytesAndBorrow() {
	sgre::scene::MotionSeedBuffer seed(0x123456789abcdef0u);
	const auto words = seed.Words();
	Require(words.front() == 0xdef0u && std::all_of(words.begin() + 1, words.end(), [](auto word) {
		return word == 0;
	}),
	    "native initialization preserves only the first low counter halfword and zeroes every other byte");
	Require(seed.HalfwordIndex() == 0 && seed.Rotation() == 0, "original hook index and rotation start at zero");
	const auto* identity = words.data();
	const std::array<std::uint16_t, 3> zero{};
	seed.MixHookInput(0x10000u, zero);
	Require(seed.Words().data() == identity && words.front() == 0xdef0u && seed.HalfwordIndex() == 1, "the actual stable seed buffer ignores upper counter bits and preserves borrowed word identity");
}

static void TestIndependentFoldAndRotationVectors() {
	sgre::scene::MotionSeedBuffer seed(1);
	const std::array<std::uint16_t, 3> first{1, 2, 3};
	seed.MixHookInput(0x10u, first);
	Require(seed.Words().front() == 0x11u && seed.HalfwordIndex() == 1, "four independent halfwords fold into the original low word lane");
	const std::array<std::uint16_t, 3> second{1, 2, 4};
	seed.MixHookInput(0xffffu, second);
	Require(seed.Words().front() == 0xfff80011u && seed.HalfwordIndex() == 2, "odd indices select the original high halfword without modifying the low lane");
	const std::array<std::uint16_t, 3> zero{};
	for (std::size_t index = 2; index < 1248; ++index) {
		seed.MixHookInput(0, zero);
	}
	Require(seed.HalfwordIndex() == 0 && seed.Rotation() == 1, "a complete halfword pass wraps the index and increments rotation");
	seed.MixHookInput(1, zero);
	Require(seed.Words().front() == 0xfff88011u && seed.HalfwordIndex() == 1, "the first rotated bit moves to the original halfword's high bit");
	for (std::size_t index = 1; index < 15 * 1248; ++index) {
		seed.MixHookInput(0, zero);
	}
	Require(seed.HalfwordIndex() == 0 && seed.Rotation() == 0 && seed.Words().front() == 0xfff88011u, "sixteen complete passes wrap rotation while zero samples preserve accumulated seed bytes");
	Require(std::all_of(seed.Words().begin() + 1, seed.Words().end(), [](auto word) {
		return word == 0;
	}),
	    "mixing does not modify unrelated word lanes");
}

static void TestRealRandomSnapshotRelationship() {
	sgre::scene::MotionSeedBuffer seed(17);
	sgre::scene::MotionSharedRandom before(seed.Words());
	const auto original_state = before.State();
	const std::array<std::uint16_t, 3> observed{0x1234u, 0xabcdu, 0x55aau};
	seed.MixHookInput(0x5678u, observed);
	sgre::scene::MotionSharedRandom after(seed.Words());
	Require(before.State() == original_state && after.State() != original_state, "actual shared random construction copies its contemporary seed instead of retargeting a prior stream");
}

class FixtureReader {
public:
	explicit FixtureReader(std::span<const std::uint8_t> bytes) : m_Bytes(bytes) {
	}

	std::uint64_t ReadUnsigned(std::size_t width) {
		if (width == 0 || width > 8 || m_Offset > m_Bytes.size() || width > m_Bytes.size() - m_Offset) {
			throw std::runtime_error("seed native fixture field exceeds its bounded input");
		}

		std::uint64_t result = 0;
		for (std::size_t index = 0; index < width; ++index) {
			result |= static_cast<std::uint64_t>(m_Bytes[m_Offset++]) << (index * 8);
		}

		return result;
	}

	[[nodiscard]] bool Complete() const noexcept {
		return m_Offset == m_Bytes.size();
	}

private:
	std::span<const std::uint8_t> m_Bytes;
	std::size_t m_Offset = 0;
};

static void TestNativeFixture(const char* path) {
	sgre::io::FileReader file(path);
	Require(file.Size() >= 12 && file.Size() <= 2 * 1024 * 1024, "native seed fixture must be a bounded nonempty record set");
	const auto bytes = file.Read(0, static_cast<std::size_t>(file.Size()));
	FixtureReader reader(bytes);
	Require(reader.ReadUnsigned(4) == 0x4642534du, "native seed fixture magic does not match MSBF");
	const auto families = reader.ReadUnsigned(4);
	const auto steps = reader.ReadUnsigned(4);
	Require(families > 0 && families <= 8 && steps > 0 && steps <= 25000 && families * steps <= 100000, "native seed fixture count exceeds its comparison budget");
	Require(bytes.size() == 12 + families * (8 + steps * 26 + 624 * 4), "native seed fixture has incomplete or trailing serialized records");
	for (std::uint64_t family = 0; family < families; ++family) {
		sgre::scene::MotionSeedBuffer seed(reader.ReadUnsigned(8));
		for (std::uint64_t step = 0; step < steps; ++step) {
			const auto counter = reader.ReadUnsigned(8);
			std::array<std::uint16_t, 3> caller_halfwords{};
			for (auto& halfword : caller_halfwords) {
				halfword = static_cast<std::uint16_t>(reader.ReadUnsigned(2));
			}
			const auto expected_word = reader.ReadUnsigned(4);
			const auto expected_index = reader.ReadUnsigned(4);
			const auto expected_rotation = reader.ReadUnsigned(4);
			const auto changed_word = seed.HalfwordIndex() / 2;
			seed.MixHookInput(counter, caller_halfwords);
			Require(seed.Words()[changed_word] == expected_word && seed.HalfwordIndex() == expected_index && seed.Rotation() == expected_rotation, "seed hook word/index/rotation differs from the admitted original instructions");
		}
		for (const auto word : seed.Words()) {
			Require(word == reader.ReadUnsigned(4), "complete final seed buffer differs from original hook output");
		}
	}
	Require(reader.Complete(), "native seed fixture contains unconsumed input");
	std::cout << families * steps << " original seed-hook word/index/rotation records match\n";
}

int main(int argc, char** argv) {
	try {
		Require(argc == 1 || argc == 2, "usage: motion_seed_buffer_tests [native-seed-hook.bin]");
		TestInitialBytesAndBorrow();
		TestIndependentFoldAndRotationVectors();
		TestRealRandomSnapshotRelationship();
		if (argc == 2) {
			TestNativeFixture(argv[1]);
		}
		std::cout << "Motion seed-buffer groups PASS\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
