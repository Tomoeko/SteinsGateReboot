#include "sgre/scene/motion_random.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

template <typename Operation>
static void RequireFailure(Operation operation) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find("actual manager") != std::string_view::npos, error.what());

		return;
	}

	throw std::runtime_error("unrepresentable manager random sample was accepted");
}

static void TestKnownSharedAndPlayerVectors() {
	const std::array<std::uint32_t, sgre::scene::kMotionSharedSeedWords> seed{};
	sgre::scene::MotionSharedRandom shared(seed);
	Require(shared.State()[0] == 0x80000000, "native shared initialization installs the actual leading state word");
	const auto sample = shared.NextUnitFloat();
	Require(std::bit_cast<std::uint32_t>(sample) == 1062244606, "zero runtime-seed corpus has the retained native first manager sample");
	sgre::scene::MotionPlayerRandom player(sample);
	Require(player.State() == std::array<std::uint32_t, 4>{123456789, 362436069, 521288629, 3499163136}, "actual child scalar conversion seeds its independent native four-word state");
	const std::array<std::array<std::uint32_t, 3>, 4> expected{{{2182931416, 1064483438, 1064658116}, {3124160863, 1052037172, 1032830288}, {298777618, 1059285576, 1058241604}, {2180230998, 1057046414, 1065111946}}};
	for (const auto& row : expected) {
		Require(shared.NextWord() == row[0] && std::bit_cast<std::uint32_t>(shared.NextUnitFloat()) == row[1] && std::bit_cast<std::uint32_t>(player.NextUnitFloat()) == row[2], "retained native shared/player streams remain distinct");
	}

	const auto shared_state = shared.State();
	for (std::size_t index = 0; index < 32; ++index) {
		const auto unit = player.NextUnitFloat();
		Require(unit >= 0 && unit < 1, "actual player sample stays in its native unit interval");
	}

	Require(shared.State() == shared_state, "player generation does not consume or mutate its manager stream");
	auto clone = player;
	Require(clone.State() == player.State(), "native cloned player state copies the actual four words");
	Require(std::bit_cast<std::uint32_t>(clone.NextUnitFloat()) == std::bit_cast<std::uint32_t>(player.NextUnitFloat()), "copied player state resumes the same stream");
	const auto before = player.State();
	static_cast<void>(clone.NextUnitFloat());
	Require(player.State() == before && clone.State() != before, "copied player state has independent subsequent ownership");
}

static void TestManagerSampleDomain() {
	sgre::scene::MotionPlayerRandom zero(0);
	sgre::scene::MotionPlayerRandom largest(1.0f - 0x1p-23f);
	Require(zero.State()[3] == 0 && largest.State()[3] == 4294966784u, "native child seed conversion covers both manager unit boundaries without signed narrowing");
	RequireFailure([] {
		sgre::scene::MotionPlayerRandom rejected(-0.0f);
	});
	RequireFailure([] {
		sgre::scene::MotionPlayerRandom rejected(-1);
	});
	RequireFailure([] {
		sgre::scene::MotionPlayerRandom rejected(1);
	});
	RequireFailure([] {
		sgre::scene::MotionPlayerRandom rejected(0x1p-24f);
	});
	RequireFailure([] {
		sgre::scene::MotionPlayerRandom rejected(std::numeric_limits<float>::infinity());
	});
	RequireFailure([] {
		sgre::scene::MotionPlayerRandom rejected(std::numeric_limits<float>::quiet_NaN());
	});
}

static std::uint32_t ReadWord(std::span<const std::uint8_t> bytes, std::size_t offset) {
	if (offset > bytes.size() || 4 > bytes.size() - offset) {
		throw std::runtime_error("truncated native random word");
	}

	std::uint32_t result = 0;
	for (std::size_t index = 0; index < 4; ++index) {
		result |= static_cast<std::uint32_t>(bytes[offset + index]) << (index * 8);
	}

	return result;
}

template <std::size_t Size>
static std::array<std::uint8_t, Size> ReadRecord(std::ifstream& input) {
	std::array<char, Size> raw{};
	input.read(raw.data(), static_cast<std::streamsize>(raw.size()));
	Require(input.gcount() == static_cast<std::streamsize>(raw.size()), "truncated native random record");
	std::array<std::uint8_t, Size> result{};
	for (std::size_t index = 0; index < Size; ++index) {
		result[index] = static_cast<std::uint8_t>(static_cast<unsigned char>(raw[index]));
	}

	return result;
}

static void ReplayNative(const char* path) {
	std::ifstream input(path, std::ios::binary);
	Require(input.is_open(), "cannot open native random fixture");
	const auto header = ReadRecord<12>(input);
	Require(header[0] == 'M' && header[1] == 'R' && header[2] == 'N' && header[3] == 'G', "invalid native random signature");
	const auto cases = ReadWord(header, 4);
	const auto steps = ReadWord(header, 8);
	Require(cases > 0 && cases <= 16 && steps > 0 && steps <= 768, "native random work exceeds its bound");
	for (std::uint32_t case_index = 0; case_index < cases; ++case_index) {
		std::array<std::uint32_t, sgre::scene::kMotionSharedSeedWords> seed{};
		const auto seed_record = ReadRecord<624 * 4>(input);
		for (std::size_t index = 0; index < seed.size(); ++index) {
			seed[index] = ReadWord(seed_record, index * 4);
		}

		sgre::scene::MotionSharedRandom shared(seed);
		const auto initial = ReadRecord<624 * 4>(input);
		for (std::size_t index = 0; index < seed.size(); ++index) {
			Require(shared.State()[index] == ReadWord(initial, index * 4), "native initialized shared state word differs");
		}

		const auto child_seed = ReadRecord<20>(input);
		const auto manager_sample = shared.NextUnitFloat();
		Require(std::bit_cast<std::uint32_t>(manager_sample) == ReadWord(child_seed, 0), "native initial manager seed sample differs");
		sgre::scene::MotionPlayerRandom player(manager_sample);
		for (std::size_t index = 0; index < 4; ++index) {
			Require(player.State()[index] == ReadWord(child_seed, 4 + index * 4), "actual native child scalar seed conversion word differs");
		}

		for (std::uint32_t step = 0; step < steps; ++step) {
			try {
				const auto row = ReadRecord<28>(input);
				Require(shared.NextWord() == ReadWord(row, 0), "actual shared MT word differs");
				Require(std::bit_cast<std::uint32_t>(shared.NextUnitFloat()) == ReadWord(row, 4), "actual shared unit float word differs");
				const auto unit = player.NextUnitFloat();
				for (std::size_t index = 0; index < 4; ++index) {
					Require(player.State()[index] == ReadWord(row, 8 + index * 4), "actual player random state word differs");
				}

				Require(std::bit_cast<std::uint32_t>(unit) == ReadWord(row, 24), "actual player unit float word differs");
			} catch (const std::exception& error) {
				throw std::runtime_error("native random case " + std::to_string(case_index) + " step " + std::to_string(step) + ": " + error.what());
			}
		}
	}

	Require(input.peek() == std::char_traits<char>::eof(), "native random fixture has trailing data");
}

int main(int argc, char** argv) {
	try {
		Require(argc == 1 || argc == 2, "usage: motion_random_tests [native-fixture]");
		TestKnownSharedAndPlayerVectors();
		TestManagerSampleDomain();
		if (argc == 2) {
			ReplayNative(argv[1]);
		}

		std::cout << "motion random tests passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
