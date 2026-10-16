#include "sgre/scene/motion_layer_identifiers.hpp"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

template <typename Operation>
static void RequireFailure(Operation operation, std::string_view context) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(context) != std::string_view::npos, error.what());

		return;
	}

	throw std::runtime_error("invalid layer identifier ownership accepted");
}

static void Defaults() {
	sgre::scene::MotionLayerIdentifierPool pool(3);
	Require(pool.NextIdentifier() == 1 && pool.LiveCount() == 0 && pool.RetiredCount() == 0, "native manager starts at1 withan empty retired stack");
	const auto first = pool.Acquire();
	const auto second = pool.Acquire();
	const auto third = pool.Acquire();
	Require(first.Value() == 1 && second.Value() == 2 && third.Value() == 3, "fresh selected identifiers must preserve actual manager numbering");
	RequireFailure([&] {
		static_cast<void>(pool.Acquire());
	},
	    "capacity");
	pool.Retire(first);
	pool.Retire(third);
	const auto reused_third = pool.Acquire();
	const auto reused_first = pool.Acquire();
	Require(reused_third.Value() == 3 && reused_first.Value() == 1 && pool.NextIdentifier() == 4, "retired node IDs reuse last-in-first-out without incrementing the next counter");
	Require(!pool.Contains(first) && !pool.Contains(third) && pool.Contains(second) && pool.Contains(reused_first) && pool.Contains(reused_third), "authored generation guard rejects retired/previous ownership while preserving actual current IDs");
	RequireFailure([&] {
		pool.Retire(first);
	},
	    "expired");
	sgre::scene::MotionLayerIdentifierPool other(3);
	const auto other_first = other.Acquire();
	Require(!pool.Contains(other_first), "equal numeric identifiers from different actual managers are distinct borrowed owners");
	RequireFailure([&] {
		pool.Retire(other_first);
	},
	    "another manager");
	pool.Retire(reused_first);
	RequireFailure([&] {
		pool.Retire(reused_first);
	},
	    "expired");
	RequireFailure([] {
		sgre::scene::MotionLayerIdentifierPool rejected(0);
	},
	    "positive");
	RequireFailure([] {
		sgre::scene::MotionLayerIdentifierPool rejected(1000001);
	},
	    "bounded");
}

class Reader {
public:
	explicit Reader(std::istream& stream) : m_Stream(stream) {
	}

	std::uint8_t Byte() {
		const auto value = m_Stream.get();
		if (value == std::char_traits<char>::eof()) {
			throw std::runtime_error("short native node-ID fixture");
		}

		return static_cast<std::uint8_t>(value);
	}

	std::uint32_t Word() {
		std::uint32_t result = 0;
		for (std::uint32_t shift = 0; shift != 32; shift += 8) {
			result |= static_cast<std::uint32_t>(Byte()) << shift;
		}

		return result;
	}

private:
	std::istream& m_Stream;
};

static void Native(std::string_view filename) {
	std::ifstream stream(std::string(filename), std::ios::binary);
	Reader reader(stream);
	Require(reader.Word() == 0x31494e4d, "wrong node-ID fixture magic");
	const auto cases = reader.Word();
	const auto steps = reader.Word();
	Require(cases == 64 && steps == 64, "node-ID fixture requires64 by64 transitions");
	for (std::uint32_t instance = 0; instance < cases; ++instance) {
		sgre::scene::MotionLayerIdentifierPool pool(64);
		std::vector<sgre::scene::MotionLayerIdentifier> live;
		for (std::uint32_t step = 0; step < steps; ++step) {
			const bool acquire = reader.Byte() != 0;
			const auto expected_value = reader.Word();
			const auto expected_next = reader.Word();
			const auto expected_retired = reader.Word();
			if (acquire) {
				const auto identifier = pool.Acquire();
				Require(identifier.Value() == expected_value, "native fresh/reused identifier differs");
				live.push_back(identifier);
			} else {
				const auto found = std::find_if(live.begin(), live.end(), [expected_value](const auto identifier) {
					return identifier.Value() == expected_value;
				});
				Require(found != live.end(), "native retirement has no current actual selected owner");
				pool.Retire(*found);
				live.erase(found);
			}

			Require(pool.NextIdentifier() == expected_next && pool.RetiredCount() == expected_retired && pool.LiveCount() == live.size(), "native manager counter/retired/live state differs");
		}
	}

	Require(stream.get() == std::char_traits<char>::eof(), "trailing node-ID fixture bytes");
	std::cout << "native layer identifiers4096 PASS\n";
}

int main(int argc, char** argv) {
	try {
		Defaults();
		if (argc == 2) {
			Native(argv[1]);
		} else if (argc != 1) {
			throw std::runtime_error("usage: motion_layer_identifier_tests [native-fixture]");
		}

		std::cout << "motion layer identifiers PASS\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
