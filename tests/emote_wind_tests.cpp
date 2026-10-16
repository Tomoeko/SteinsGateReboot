#include "sgre/scene/emote_wind.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

static void Require(bool value, std::string_view message) {
	if (!value) {
		throw std::runtime_error(std::string(message));
	}
}
template <typename Operation>
static void RequireFailure(Operation operation, std::string_view message) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(message) != std::string_view::npos, error.what());
		return;
	}
	throw std::runtime_error("wind accepted unsupported state");
}
static void TestLazySeedBoundariesAndReentry() {
	const std::array<std::int64_t, 7> seconds{-1, 0, 1, 1700000000, 0xffffffff, 0x793582aff, 0x793582b00};
	// Native582368/2d4840 expected initial fourth words, observed after the first shift into z.
	const std::array<std::uint32_t, 7> expected{0xffffffff, 0, 1, 1700000000, 0xffffffff, 0x93582aff, 0xffffffff};
	for (std::size_t index = 0; index != seconds.size(); ++index) {
		std::size_t reads = 0;
		sgre::scene::EmoteWindRandom random([&] {
			++reads;
			return seconds[index];
		});
		Require(!random.State() && reads == 0, "wind stream must remain unseeded until first actual trial");
		const auto unit = random.NextUnitFloat();
		Require(reads == 1 && random.State()->at(2) == expected[index] && unit >= 0 && unit < 1, "lazy native seed conversion must preserve low32 and upper-bound error mapping");
		static_cast<void>(random.NextUnitFloat());
		Require(reads == 1, "shared wind stream must read application time once");
	}
	sgre::scene::EmoteWindRandom* active = nullptr;
	sgre::scene::EmoteWindRandom random([&] {
		RequireFailure([&] {
			static_cast<void>(active->NextUnitFloat());
		},
		    "reenter");
		return std::int64_t{1700000000};
	});
	active = &random;
	static_cast<void>(random.NextUnitFloat());
	Require(random.State().has_value(), "reentry rejection cannot corrupt successful lazy seed publication");
}
static void TestOwnerFiniteBoundsAndPreservation() {
	sgre::scene::EmoteWindRandom random([] {
		return std::int64_t{1700000000};
	});
	sgre::scene::EmoteWindOwner owner(2, &random);
	owner.Start({0, 100, 1, .5f, 1});
	owner.Advance(8);
	auto before = owner.Snapshot();
	Require(before.Control && before.Control->Bands[0].Active, "real finite trial sequence must emit a band before ownership assertions");
	owner.Start({0, 100, 2, -1, 2});
	const auto reused = owner.Snapshot();
	Require(reused.Control->Bands[0].Active && reused.Control->Bands[0].Position == before.Control->Bands[0].Position && reused.Control->Bands[0].Strength == before.Control->Bands[0].Strength && reused.Control->BirthClock == 0, "same authored endpoints reuse active bands but reset trial cadence");
	owner.Stop();
	Require(!owner.Snapshot().Control && owner.Snapshot().Parameters.Minimum == -1, "stop deletes controller but preserves authored property words");
	owner.Start({1, 100, 2, -1, 2});
	Require(!owner.Snapshot().Control->Bands[0].Active, "changed endpoints create fresh band ownership");
	const auto current = owner.Snapshot();
	RequireFailure([&] {
		owner.Start({0, 8, std::numeric_limits<float>::infinity(), 1, 1});
	},
	    "finite");
	Require(owner.Snapshot().Parameters.Start == current.Parameters.Start, "finite start preflight must leave completed controller unchanged");
	RequireFailure([&] {
		owner.Advance(65536);
	},
	    "bounded");
	RequireFailure([&] {
		owner.Advance(-1);
	},
	    "nonnegative");
	RequireFailure([&] {
		static_cast<void>(owner.Sample(std::numeric_limits<float>::quiet_NaN()));
	},
	    "finite");
	sgre::scene::EmoteWindOwner absent(1, nullptr);
	absent.Stop();
	RequireFailure([&] {
		absent.Start({0, 8, 1, 1, 1});
	},
	    "shared lazy random");
	Require(!absent.Snapshot().Control && absent.Snapshot().Parameters.Goal == 0, "missing service failure must not commit a controller");
}

class NativeWords {
public:
	explicit NativeWords(const char* path) {
		const auto size = std::filesystem::file_size(path);
		Require(size >= 8 && size <= 1024 * 1024, "native wind fixture exceeds bounded file extent");
		Bytes.resize(static_cast<std::size_t>(size));
		std::ifstream input(path, std::ios::binary);
		Require(input.good(), "cannot open native wind expected fixture");
		input.read(Bytes.data(), static_cast<std::streamsize>(Bytes.size()));
		Require(input.gcount() == static_cast<std::streamsize>(Bytes.size()), "native wind expected fixture truncated");
		Require(Bytes[0] == 'E' && Bytes[1] == 'W' && Bytes[2] == 'N' && Bytes[3] == '1', "native wind expected magic mismatch");
		Position = 4;
	}
	std::uint32_t Word() {
		Require(Position <= Bytes.size() && 4 <= Bytes.size() - Position, "native wind expected field truncated");
		std::uint32_t result = 0;
		for (unsigned index = 0; index != 4; ++index) {
			result |= static_cast<std::uint32_t>(static_cast<unsigned char>(Bytes[Position++])) << (index * 8);
		}
		return result;
	}
	float Number() {
		return std::bit_cast<float>(Word());
	}
	void Equal(float actual, std::string_view field) {
		const auto expected = Word();
		if (std::bit_cast<std::uint32_t>(actual) != expected) {
			throw std::runtime_error("native wind " + std::string(field) + " word mismatch at byte " + std::to_string(Position - 4) + " expected " + std::to_string(expected) + " actual " + std::to_string(std::bit_cast<std::uint32_t>(actual)));
		}
	}
	bool End() const {
		return Position == Bytes.size();
	}

private:
	std::vector<char> Bytes;
	std::size_t Position = 0;
};
static void CheckNativeState(NativeWords& words, const sgre::scene::EmoteWindOwner& owner, const sgre::scene::EmoteWindRandom& random) {
	const auto state = owner.Snapshot();
	for (const auto value : {state.Parameters.Start, state.Parameters.Goal, state.Parameters.Speed, state.Parameters.Minimum, state.Parameters.Maximum}) {
		words.Equal(value, "authored");
	}
	const auto has_control = words.Word();
	Require(has_control <= 1 && state.Control.has_value() == (has_control != 0), "native wind controller ownership mismatch");
	if (state.Control) {
		const auto& control = *state.Control;
		for (const auto value : {control.Start, control.Goal, control.Minimum, control.Maximum, control.SignedVelocity, control.BirthClock}) {
			words.Equal(value, "control");
		}
		Require(words.Word() == static_cast<std::uint32_t>(control.Enabled), "native wind enabled byte mismatch");
		const auto active_count = words.Word();
		Require(active_count <= sgre::scene::kEmoteWindBands, "native wind active count exceeds actual slots");
		std::size_t observed = 0;
		for (std::size_t index = 0; index != control.Bands.size(); ++index) {
			const auto& band = control.Bands[index];
			if (band.Active) {
				Require(observed < active_count && words.Word() == index, "native first-free band ordering mismatch");
				words.Equal(band.Position, "active position");
				words.Equal(band.Strength, "active strength");
				++observed;
			}
		}
		Require(observed == active_count, "native active band count mismatch");
	}
	const auto rng_kind = words.Word();
	Require(rng_kind <= 2, "native wind RNG discriminator invalid");
	const auto rng = random.State();
	if (rng_kind != 2) {
		Require(rng.has_value() == (rng_kind == 1), "native lazy seed phase mismatch");
		if (rng) {
			for (const auto word : *rng) {
				Require(words.Word() == word, "native shared XorShift state word mismatch");
			}
		}
	}
}
static void ReplayNative(const char* path) {
	NativeWords words(path);
	const auto plans = words.Word();
	Require(plans == 15, "native wind replay requires all15 naturally constructed plans");
	std::size_t states = 0, samples = 0;
	for (std::size_t plan = 0; plan != plans; ++plan) {
		sgre::scene::EmoteWindRandom random([] {
			return std::int64_t{1700000000};
		});
		const auto count = words.Word();
		Require(count > 0 && count <= 2, "native wind owner count invalid");
		std::vector<std::unique_ptr<sgre::scene::EmoteWindOwner>> owners;
		for (std::size_t index = 0; index != count; ++index) {
			owners.push_back(std::make_unique<sgre::scene::EmoteWindOwner>(words.Number(), &random));
		}
		const auto steps = words.Word();
		Require(steps <= 32, "native wind phase count invalid");
		for (std::size_t step = 0; step != steps; ++step) {
			const auto opcode = words.Word();
			const auto index = words.Word();
			Require(index < owners.size(), "native wind owner index invalid");
			auto& owner = *owners[index];
			if (opcode == 0) {
				sgre::scene::EmoteWindParameters parameters;
				parameters.Start = words.Number();
				parameters.Goal = words.Number();
				parameters.Speed = words.Number();
				parameters.Minimum = words.Number();
				parameters.Maximum = words.Number();
				owner.Start(parameters);
			} else if (opcode == 1) {
				owner.Advance(words.Number());
			} else {
				Require(opcode == 2, "native wind phase opcode invalid");
				owner.Stop();
			}
			const auto compare = words.Word();
			Require(compare <= 1, "native wind phase comparison flag invalid");
			if (compare == 0) {
				continue;
			}
			CheckNativeState(words, owner, random);
			++states;
			const auto queries = words.Word();
			Require(queries <= 32, "native wind sample count invalid");
			for (std::size_t query = 0; query != queries; ++query) {
				const auto position = words.Number();
				words.Equal(owner.Sample(position), "sample");
				++samples;
			}
		}
	}
	Require(words.End() && states == 129 && samples == 560, "native wind replay omitted a retained natural state/sample or has trailing data");
	std::cout << "native wind exact129states/560samples passed; prepared capacity/overlap remains separate native proof\n";
}
int main(int argc, char** argv) {
	try {
		TestLazySeedBoundariesAndReentry();
		TestOwnerFiniteBoundsAndPreservation();
		if (argc == 3 && std::string_view(argv[1]) == "--native-wind") {
			ReplayNative(argv[2]);
		} else {
			Require(argc == 1, "expected optional --native-wind retained binary");
		}
		std::cout << "Emote wind owner tests passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
