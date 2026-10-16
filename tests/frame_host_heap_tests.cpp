#include "sgre/render/frame_host_heap.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace sgre::render {

	class FrameHostHeapFixtureBridge {
	public:
		// Only the original synthetic wrap domain seeds this private fresh owner; no production counter setter exists.
		static void WrappedCounter(FrameHostHeap& heap) {
			heap.SetWrappedCounterForFixture();
		}
	};

} // namespace sgre::render

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

template <typename Operation>
static void RequireError(Operation operation, std::string_view message) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(message) != std::string_view::npos, error.what());

		return;
	}
	throw std::runtime_error("expected rejection: " + std::string(message));
}

static std::uint64_t Word(const std::vector<std::uint8_t>& bytes, std::size_t& offset, std::size_t count) {
	Require(count <= 8 && offset <= bytes.size() && count <= bytes.size() - offset, "bounded host heap native fixture word");
	std::uint64_t value = 0;
	for (std::size_t index = 0; index < count; ++index) {
		value |= static_cast<std::uint64_t>(bytes[offset++]) << (index * 8);
	}

	return value;
}

static void TestNative(const std::string& path) {
	std::ifstream input(path, std::ios::binary | std::ios::ate);
	Require(input && input.tellg() == 6920, "HHP1 exact native bytes");
	std::vector<std::uint8_t> bytes(6920);
	input.seekg(0);
	input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
	Require(input && std::string_view(reinterpret_cast<const char*>(bytes.data()), 4) == "HHP1", "HHP1 native header");
	std::size_t offset = 4;
	Require(Word(bytes, offset, 4) == 192, "HHP1 exact original192 cycles");
	std::unique_ptr<sgre::render::FrameHostHeap> heap;
	std::size_t compared = 0;
	for (std::size_t index = 0; index < 192; ++index) {
		const auto owner = Word(bytes, offset, 4);
		const auto epoch = Word(bytes, offset, 4);
		const auto physical_count = Word(bytes, offset, 4);
		const auto expected_counter = Word(bytes, offset, 8);
		const auto expected_physical = Word(bytes, offset, 4);
		const auto expected_used = Word(bytes, offset, 8);
		const auto expected_pages = Word(bytes, offset, 4);
		Require(owner < 32 && physical_count >= 1 && physical_count <= 4 && epoch <= 8, "HHP1 native input domain");
		if (epoch == 0) {
			heap = std::make_unique<sgre::render::FrameHostHeap>(static_cast<std::size_t>(physical_count));
			if ((owner & 1) != 0) {
				sgre::render::FrameHostHeapFixtureBridge::WrappedCounter(*heap);
			}
		}
		Require(heap != nullptr, "HHP1 genuine constructed owner");
		heap->Begin();
		Require(heap->FrameCounter() == expected_counter && heap->PhysicalFrame() == expected_physical && heap->ResourceEntryByte() == 0 && heap->CurrentPageCount() == 0 && heap->CaptureUsedBytes() == 0, "native actual Begin/current/release phase");
		constexpr std::array<std::size_t, 4> requests{1, 7, 17, 65535};
		constexpr std::array<std::size_t, 4> alignments{4, 16, 64, 4};
		for (std::size_t item = 0; item < requests.size(); ++item) {
			const auto allocation = heap->Allocate(requests[item], alignments[item]);
			auto actual = allocation.Bytes();
			Require(actual.size() == requests[item], "actual host storage count");
			actual.front() = static_cast<std::byte>(item + 1);
			actual.back() = static_cast<std::byte>(item + 1);
		}
		heap->End();
		Require(heap->ResourceEntryByte() == 1 && heap->CaptureUsedBytes() == expected_used && heap->CachedUsedBytes() == expected_used && heap->CurrentPageCount() == expected_pages, "native End/used high-water/cache/list count");
		++compared;
	}
	Require(offset == bytes.size() && compared == 192, "HHP1 exact no trailing observations");
	std::cout << "Original32 owners/192 cycles expected-only comparisons; genuine owned host allocations PASS\n";
}

static void TestZeroBeforeBegin(const std::string& path) {
	std::ifstream input(path, std::ios::binary | std::ios::ate);
	Require(input && input.tellg() == 248, "HZR1 exact new native discriminator bytes");
	std::vector<std::uint8_t> bytes(248);
	input.seekg(0);
	input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
	Require(input && std::string_view(reinterpret_cast<const char*>(bytes.data()), 4) == "HZR1", "HZR1 native header");
	std::size_t offset = 4;
	Require(Word(bytes, offset, 4) == 5, "HZR1 exact five original observations");
	sgre::render::FrameHostHeap heap(2);
	std::byte* base = nullptr;
	for (std::size_t index = 0; index < 5; ++index) {
		const auto count = Word(bytes, offset, 8);
		const auto expected_frame = Word(bytes, offset, 8);
		const auto expected_offset = Word(bytes, offset, 8);
		const auto expected_used = Word(bytes, offset, 8);
		const auto expected_pages = Word(bytes, offset, 8);
		const auto expected_entry = Word(bytes, offset, 8);
		if (index == 3) {
			heap.End();
		}
		if (index == 4) {
			heap.Begin();
			base = nullptr;
		}
		const auto allocation = heap.Allocate(static_cast<std::size_t>(count));
		auto actual = allocation.Bytes();
		if (base == nullptr) {
			base = actual.data();
		}
		Require(actual.data() != nullptr && actual.size() == count && static_cast<std::size_t>(actual.data() - base) == expected_offset, "native zero-sized pointer/relative high-water alignment");
		Require(heap.FrameCounter() == expected_frame && heap.CaptureUsedBytes() == expected_used && heap.CurrentPageCount() == expected_pages && heap.ResourceEntryByte() == expected_entry, "native initialized before-Begin/after-End host allocation state");
	}
	Require(offset == bytes.size(), "HZR1 no trailing original observations");
}

static void TestOwnership() {
	RequireError([] {
		sgre::render::FrameHostHeap invalid(0);
	},
	    "positive bounded physical count");
	RequireError([] {
		sgre::render::FrameHostHeap invalid(65);
	},
	    "positive bounded physical count");
	sgre::render::FrameHostHeap heap(2);
	heap.Begin();
	const auto first = heap.Allocate(7);
	const auto second = heap.Allocate(17);
	const auto page = heap.Allocate(65536);
	Require(first.Bytes().size() == 7 && second.Bytes().data() - first.Bytes().data() == 16 && page.Bytes().size() == 65536, "default16 relative alignment and actual new page");
	first.Bytes().front() = std::byte{99};
	heap.Begin();
	Require(first.Bytes().front() == std::byte{99}, "another physical frame does not retire this borrowed page");
	heap.Begin();
	RequireError([&] {
		static_cast<void>(first.Bytes());
	},
	    "physical page has recycled");
	const auto replacement = heap.Allocate(7);
	Require(replacement.Bytes().size() == 7, "fresh physical page owns new allocation");
	RequireError([&] {
		static_cast<void>(first.Bytes());
	},
	    "physical page has recycled");
	const auto cached = heap.CaptureUsedBytes();
	heap.End();
	const auto after_end = heap.Allocate(1);
	Require(after_end.Bytes().size() == 1 && heap.ResourceEntryByte() == 1 && heap.CachedUsedBytes() == cached, "native allocation does not invent an entry-byte readiness gate or automatically refresh cache");
	const auto now_used = heap.CaptureUsedBytes();
	RequireError([&] {
		static_cast<void>(heap.Allocate(1, 3));
	},
	    "size/alignment domain");
	RequireError([&] {
		static_cast<void>(heap.Allocate(std::numeric_limits<std::size_t>::max()));
	},
	    "size/alignment domain");
	Require(heap.CaptureUsedBytes() == now_used, "invalid requests preserve actual page high-water");
	std::exception_ptr foreign_failure;
	std::thread foreign([&] {
		try {
			RequireError([&] {
				heap.Begin();
			},
			    "owner thread");
			RequireError([&] {
				heap.End();
			},
			    "owner thread");
		} catch (...) {
			foreign_failure = std::current_exception();
		}
	});
	foreign.join();
	if (foreign_failure) {
		std::rethrow_exception(foreign_failure);
	}
	heap.Close();
	RequireError([&] {
		static_cast<void>(replacement.Bytes());
	},
	    "owner has closed");
	RequireError([&] {
		static_cast<void>(heap.Allocate(1));
	},
	    "has closed");
	sgre::render::FrameHostAllocation expired;
	{
		auto owner = std::make_unique<sgre::render::FrameHostHeap>(1);
		expired = owner->Allocate(5);
	}
	RequireError([&] {
		static_cast<void>(expired.Bytes());
	},
	    "owner has retired");
	sgre::render::FrameHostHeapLimits limits;
	limits.MaximumBytes = 65536;
	limits.MaximumPages = 1;
	sgre::render::FrameHostHeap bounded(1, limits);
	static_cast<void>(bounded.Allocate(65536));
	RequireError([&] {
		static_cast<void>(bounded.Allocate(1));
	},
	    "owner budget");
	Require(bounded.CaptureUsedBytes() == 65536 && bounded.CurrentPageCount() == 1, "capacity rejection preserves real storage");
	bounded.Begin();
	Require(bounded.Allocate(0).Bytes().empty() && bounded.CurrentPageCount() == 1 && bounded.CaptureUsedBytes() == 0, "native zero count still owns a host page");
}

static void TestConcurrentAllocations() {
	sgre::render::FrameHostHeap heap(1);
	heap.Begin();
	std::array<std::thread, 4> workers;
	std::array<std::exception_ptr, 4> failures{};
	for (std::size_t index = 0; index < workers.size(); ++index) {
		workers[index] = std::thread([&, index] {
			try {
				for (std::size_t item = 0; item < 128; ++item) {
					const auto allocation = heap.Allocate(16);
					for (auto& byte : allocation.Bytes()) {
						byte = static_cast<std::byte>(index + 1);
					}
				}
			} catch (...) {
				failures[index] = std::current_exception();
			}
		});
	}
	for (auto& worker : workers) {
		worker.join();
	}
	for (const auto& failure : failures) {
		if (failure) {
			std::rethrow_exception(failure);
		}
	}
	heap.End();
	Require(heap.CaptureUsedBytes() == 8192 && heap.CurrentPageCount() == 1, "actual serialized nonoverlapping worker allocations");
	std::cout << "Borrow expiry/recycle/budget and512 concurrent allocations PASS; GPU upload/registration OPEN\n";
}

int main(int argc, char** argv) {
	try {
		Require(argc == 3, "original HHP1/HZR1 fixtures required");
		TestNative(argv[1]);
		TestZeroBeforeBegin(argv[2]);
		TestOwnership();
		TestConcurrentAllocations();
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}

	return 0;
}
