#include "sgre/scene/motion_render_queue.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using sgre::scene::MotionRenderOrder;
using sgre::scene::MotionRenderQueue;

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

	throw std::runtime_error("expected failure: " + std::string(expected));
}

static MotionRenderOrder Order(float primary, float secondary, std::uint32_t ordinal, std::uint64_t identity) {
	MotionRenderOrder order;
	order.Primary = primary;
	order.Secondary = secondary;
	order.Ordinal = ordinal;
	order.CommandIdentity = identity;

	return order;
}

static void TestComparisonDomain() {
	const auto low = Order(-1, 99, 100, 1);
	const auto high = Order(0, -99, 0, 2);
	Require(sgre::scene::MotionRenderOrderBefore(low, high) && !sgre::scene::MotionRenderOrderBefore(high, low), "primary render key did not dominate secondary/ordinal");
	const auto first = Order(-0.0f, 0.0f, 0, 3);
	const auto last = Order(0.0f, -0.0f, std::numeric_limits<std::uint32_t>::max(), 4);
	Require(sgre::scene::MotionRenderOrderBefore(first, last), "signed-zero equality or unsigned ordinal comparison differs");
	const auto nan = std::bit_cast<float>(std::uint32_t{0x7fc00001});
	const auto nan_primary = Order(nan, -99, 0, 5);
	Require(!sgre::scene::MotionRenderOrderBefore(nan_primary, high) && !sgre::scene::MotionRenderOrderBefore(high, nan_primary), "unordered primary must not fall back to secondary/ordinal");
	const auto nan_secondary = Order(0, nan, 0, 6);
	Require(!sgre::scene::MotionRenderOrderBefore(nan_secondary, last) && !sgre::scene::MotionRenderOrderBefore(last, nan_secondary), "unordered secondary must not fall back to ordinal");
}

static void TestQueuePhasesAndAliases() {
	std::array<MotionRenderOrder, 4> commands{Order(1, 0, 99, 1), Order(0, 2, 99, 2), Order(0, 1, 99, 3), Order(0, 1, 99, 4)};
	MotionRenderQueue queue(4);
	for (auto& command : commands) {
		queue.Append(command);
	}

	std::vector<std::uint64_t> identities;
	queue.Flush([&](const auto ordered) {
		Require(queue.Size() == 4 && queue.Capacity() == 4, "native queue was reset before submission observed it");
		for (const auto command : ordered) {
			identities.push_back(command.get().CommandIdentity);
		}

		RequireFailure([&] {
			queue.Append(commands[0]);
		},
		    "during submission");
		RequireFailure([&] {
			queue.Clear();
		},
		    "during submission");
		RequireFailure([&] {
			queue.Flush([](const auto) {
			});
		},
		    "recursively");
	});
	Require(identities == std::vector<std::uint64_t>{3, 4, 2, 1} && queue.Size() == 0 && commands[0].Ordinal == 0 && commands[3].Ordinal == 3, "sort/tie ordinals or post-submission reset differs");
	queue.Append(commands[2]);
	queue.Append(commands[2]);
	queue.Append(commands[3]);
	queue.Flush([&](const auto ordered) {
		Require(ordered.size() == 3 && commands[2].Ordinal == 1 && commands[3].Ordinal == 2 && &ordered[0].get() == &ordered[1].get(), "duplicate command references lost last-write ordinals or alias identity");
	});
	std::size_t submissions = 0;
	queue.Flush([&](const auto) {
		++submissions;
	});
	Require(submissions == 0, "empty native queue invoked submission");
}

static void TestUnorderedFrontInsertion() {
	const auto nan = std::bit_cast<float>(std::uint32_t{0x7fc00001});
	std::array<MotionRenderOrder, 3> commands{Order(-3, 0, 0, 1), Order(-3, nan, 0, 2), Order(-3, -3, 0, 3)};
	MotionRenderQueue queue(3);
	for (auto& command : commands) {
		queue.Append(command);
	}

	std::vector<std::uint64_t> identities;
	queue.Flush([&](const auto ordered) {
		for (const auto command : ordered) {
			identities.push_back(command.get().CommandIdentity);
		}
	});
	Require(identities == std::vector<std::uint64_t>{3, 1, 2}, "native front insertion failed to cross unordered intermediate values");
}

static void TestPartitionThresholds() {
	constexpr std::array<std::size_t, 6> counts{32, 33, 41, 42, 96, 4096};
	for (const auto count : counts) {
		std::vector<MotionRenderOrder> commands;
		commands.reserve(count);
		MotionRenderQueue queue(count);
		for (std::size_t index = 0; index < count; ++index) {
			const auto identity = count - index - 1;
			commands.push_back(Order(static_cast<float>(identity / 4), static_cast<float>(identity % 4), 0, identity));
			queue.Append(commands.back());
		}

		queue.Flush([&](const auto ordered) {
			Require(ordered.size() == count, "partition changed the command count");
			for (std::size_t index = 0; index < count; ++index) {
				Require(ordered[index].get().CommandIdentity == index, "partition threshold changed lexicographic render order");
			}
		});
	}
}

static void TestSubmissionFailureAndLimits() {
	MotionRenderQueue queue(1);
	queue.Flush({});
	auto command = Order(0, 0, 77, 1);
	queue.Append(command);
	RequireFailure([&] {
		queue.Append(command);
	},
	    "capacity");
	RequireFailure([&] {
		queue.Flush([](const auto) {
			throw std::runtime_error("fixture submit failure");
		});
	},
	    "fixture submit failure");
	Require(queue.Size() == 1 && command.Ordinal == 0, "failed native submission discarded its prepared queue or ordinal");
	queue.Clear();
	RequireFailure([&] {
		MotionRenderQueue invalid(0);
	},
	    "capacity");
	RequireFailure([&] {
		MotionRenderQueue invalid(1024 * 1024 + 1);
	},
	    "capacity");
}

static std::uint32_t ReadWord(std::ifstream& input) {
	std::array<char, 4> bytes{};
	input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
	Require(input.good(), "bounded motion queue oracle is truncated");

	return static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[0])) | (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[1])) << 8) | (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[2])) << 16) | (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[3])) << 24);
}

static void TestNativeReplay(const char* input_path, const char* output_path) {
	std::ifstream input(input_path, std::ios::binary);
	std::ifstream expected(output_path, std::ios::binary);
	Require(input.is_open() && expected.is_open(), "cannot open bounded motion queue oracle");
	MotionRenderQueue queue(96);
	for (std::size_t fixture = 0; fixture < 1000; ++fixture) {
		const auto unique_count = ReadWord(input);
		const auto count = ReadWord(input);
		Require(unique_count <= 96 && count <= 96 && unique_count <= count, "motion queue oracle count exceeds its bound");
		std::vector<MotionRenderOrder> commands;
		commands.reserve(unique_count);
		for (std::uint32_t index = 0; index < unique_count; ++index) {
			const auto primary = std::bit_cast<float>(ReadWord(input));
			const auto secondary = std::bit_cast<float>(ReadWord(input));
			commands.push_back(Order(primary, secondary, 0xffffffff, index));
		}

		for (std::uint32_t index = 0; index < count; ++index) {
			const auto identifier = ReadWord(input);
			Require(identifier < commands.size(), "motion queue oracle reference exceeds its owner vector");
			queue.Append(commands[identifier]);
		}

		Require(ReadWord(expected) == count, "native motion queue output count differs");
		queue.Flush([&](const auto ordered) {
			std::vector<std::uint64_t> actual;
			std::vector<std::uint64_t> native;
			for (const auto command : ordered) {
				actual.push_back(command.get().CommandIdentity);
				native.push_back(ReadWord(expected));
			}

			Require(actual == native, "native motion queue permutation differs at fixture " + std::to_string(fixture));
		});
		for (const auto& command : commands) {
			Require(command.Ordinal == ReadWord(expected), "native motion queue aliased ordinal differs");
		}
	}

	Require(input.peek() == std::ifstream::traits_type::eof() && expected.peek() == std::ifstream::traits_type::eof(), "motion queue oracle has trailing data");
}

int main(int argc, char** argv) {
	try {
		TestComparisonDomain();
		TestQueuePhasesAndAliases();
		TestUnorderedFrontInsertion();
		TestPartitionThresholds();
		TestSubmissionFailureAndLimits();
		Require(argc == 1 || argc == 3, "usage: motion_render_queue_tests [native_input native_output]");
		if (argc == 3) {
			TestNativeReplay(argv[1], argv[2]);
		}

		std::cout << "motion render queue fixtures passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
