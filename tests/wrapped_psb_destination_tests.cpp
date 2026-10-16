#include "sgre/io/wrapped_psb_destination.hpp"

#include "sgre/resource/resource_cipher.hpp"

#include <array>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

using ByteVector = std::vector<std::uint8_t>;
using sgre::io::PreparedWrappedPsbRead;
using sgre::io::PublishedPsbFile;
using sgre::io::WrappedPsbFailurePhase;
using sgre::io::WrappedPsbReadDestination;
using sgre::io::WrappedPsbReadState;

static void Require(bool condition, const char* message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

template <typename Operation>
static void RequireFailure(Operation operation) {
	try {
		operation();
	} catch (const std::runtime_error&) {
		return;
	}

	throw std::runtime_error("wrapped PSB fixture expected an owner/publication failure");
}

static void WriteWord(ByteVector& bytes, std::size_t offset, std::uint32_t value) {
	for (std::uint32_t index = 0; index < 4; ++index) {
		bytes.at(offset + index) = static_cast<std::uint8_t>(value >> (index * 8));
	}
}

static ByteVector PsbFixture() {
	ByteVector bytes(44, 0);
	bytes[0] = 'P';
	bytes[1] = 'S';
	bytes[2] = 'B';
	bytes[4] = 3;
	WriteWord(bytes, 8, 44);
	WriteWord(bytes, 12, 44);
	// One-node empty key trie, root chunk reference0, empty strings, one four-byte chunk.
	const ByteVector metadata{13, 1, 13, 0, 13, 1, 13, 0, 13, 0, 13, 25, 0, 13, 0, 13, 13, 1, 13, 0, 13, 1, 13, 4};
	bytes.insert(bytes.end(), metadata.begin(), metadata.end());
	WriteWord(bytes, 36, 55);
	WriteWord(bytes, 16, 57);
	WriteWord(bytes, 20, 60);
	WriteWord(bytes, 24, 60);
	WriteWord(bytes, 28, 64);
	WriteWord(bytes, 32, 68);
	std::uint32_t first = 1;
	std::uint32_t second = 0;
	for (std::size_t index = 8; index < 40; ++index) {
		first += bytes[index];
		second += first;
	}

	WriteWord(bytes, 40, (second << 16) | first);
	bytes.insert(bytes.end(), {10, 20, 30, 40});

	return bytes;
}

static ByteVector Wrap(const ByteVector& psb) {
	Require(psb.size() < 256, "synthetic raw Zstandard fixture exceeds its one-byte size domain");
	ByteVector frame{0x28, 0xb5, 0x2f, 0xfd, 0x20, static_cast<std::uint8_t>(psb.size())};
	const auto block = static_cast<std::uint32_t>((psb.size() << 3) | 1);
	frame.push_back(static_cast<std::uint8_t>(block));
	frame.push_back(static_cast<std::uint8_t>(block >> 8));
	frame.push_back(static_cast<std::uint8_t>(block >> 16));
	frame.insert(frame.end(), psb.begin(), psb.end());
	sgre::resource::ResourceCipher cipher("nested/fixture.psb.m", "synthetic-seed", 131);
	cipher.Transform(frame);
	ByteVector wrapper{'m', 'z', 's', 0, 0, 0, 0, 0};
	WriteWord(wrapper, 4, static_cast<std::uint32_t>(psb.size()));
	wrapper.insert(wrapper.end(), frame.begin(), frame.end());

	return wrapper;
}

static void WriteFixture(const std::filesystem::path& path, const ByteVector& bytes) {
	std::ofstream output(path, std::ios::binary | std::ios::trunc);
	for (const auto byte : bytes) {
		output.put(static_cast<char>(byte));
	}

	Require(static_cast<bool>(output), "wrapped PSB synthetic file write failed");
}

static PreparedWrappedPsbRead Source(const std::filesystem::path& path) {
	PreparedWrappedPsbRead source;
	source.Region.Filename = path;
	source.Region.MaxBytes = 4096;
	source.OriginalFilename = "nested/fixture.psb.m";
	source.Seed = "synthetic-seed";
	source.MaxDecodedBytes = 4096;
	source.MaxMetadataBytes = 4096;

	return source;
}

static void TestRealProductionAndRetainedOutput(const std::filesystem::path& path) {
	const auto expected = PsbFixture();
	WriteFixture(path, Wrap(expected));
	std::unique_ptr<PublishedPsbFile> retained;
	const sgre::resource::PsbDocument* identity = nullptr;
	{
		WrappedPsbReadDestination receiving;
		const auto source = Source(path);
		receiving.MarkQueued();
		std::promise<bool> prepared;
		auto complete = prepared.get_future();
		std::promise<void> publish;
		auto permitted = publish.get_future();
		std::thread worker([&]() {
			receiving.Select();
			RequireFailure([&]() {
				static_cast<void>(receiving.TakePublishedObject());
			});
			prepared.set_value(receiving.ReadSelected(source));
			permitted.get();
			receiving.PublishSelected();
		});
		if (!complete.get()) {
			publish.set_value();
			worker.join();
			throw std::runtime_error(receiving.Error());
		}

		Require(receiving.State() == WrappedPsbReadState::Selected && !receiving.CanRetire(), "prepared object must remain unpublished while the real worker owns its selected receiver");
		RequireFailure([&]() {
			static_cast<void>(receiving.TakePublishedObject());
		});
		publish.set_value();
		worker.join();
		retained = receiving.TakePublishedObject();
		identity = &retained->Document();
		Require(std::vector<std::uint8_t>(retained->Bytes().begin(), retained->Bytes().end()) == expected, "actual decoded bytes differ from the independent PSB/chunk fixture");
		Require(receiving.CanRetire() && receiving.State() == WrappedPsbReadState::Succeeded, "published output must transfer its ownership without inventing native row status");
		RequireFailure([&]() {
			static_cast<void>(receiving.TakePublishedObject());
		});
	}

	Require(identity == &retained->Document() && retained->Document().Chunks.size() == 1 && retained->Document().Chunks[0].Offset == 68 && retained->Bytes()[71] == 40, "PSB object/data identity must survive temporary receiving-row retirement");
}

static void TestPhysicalReadBeforeNextSelectionAndCurrentObject(const std::filesystem::path& path) {
	WriteFixture(path, Wrap(PsbFixture()));
	const auto source = Source(path);
	WrappedPsbReadDestination current;
	WrappedPsbReadDestination following;
	current.MarkQueued();
	following.MarkQueued();
	std::promise<bool> physically_read;
	auto read = physically_read.get_future();
	std::promise<void> prepare;
	auto permitted = prepare.get_future();
	std::thread worker([&]() {
		current.Select();
		const bool read_ok = current.ReadPhysicalSelected(source);
		following.Select();
		physically_read.set_value(read_ok);
		permitted.get();
		static_cast<void>(current.PrepareSelectedObject(source));
		current.PublishSelected();
		static_cast<void>(following.ReadSelected(source));
		following.PublishSelected();
	});
	const bool read_ok = read.get();
	const bool both_selected = current.State() == WrappedPsbReadState::Selected && following.State() == WrappedPsbReadState::Selected;
	RequireFailure([&]() {
		static_cast<void>(current.TakePublishedObject());
	});
	prepare.set_value();
	worker.join();
	Require(read_ok && both_selected, "real following request must be selectable after current physical read and before wrapped object preparation");
	const auto first = current.TakePublishedObject();
	const auto next = following.TakePublishedObject();
	Require(&first->Document() != &next->Document() && first->Bytes().size() == next->Bytes().size(), "distinct native receiving requests must construct distinct stable PSB object identities");
}

static void TestPhysicalReadFailures(const std::filesystem::path& path) {
	for (const bool missing : {true, false}) {
		WrappedPsbReadDestination receiving;
		auto source = Source(missing ? path / "missing" : path);
		if (!missing) {
			source.Region.Offset = 1;
		}

		receiving.MarkQueued();
		receiving.Select();
		Require(!receiving.ReadSelected(source), "missing file or zero-length native full-size request at nonzero offset must fail physical production");
		receiving.PublishSelected();
		Require(receiving.FailurePhase() == WrappedPsbFailurePhase::PhysicalRead && !receiving.Error().empty(), "physical failure must preserve its real producer phase/diagnostic");
		RequireFailure([&]() {
			static_cast<void>(receiving.TakePublishedObject());
		});
	}
}

static void TestCheckedShellAndPsbFailures(const std::filesystem::path& path) {
	for (const bool bad_shell : {true, false}) {
		auto bytes = PsbFixture();
		if (!bad_shell) {
			bytes[40] ^= 1;
		}

		WriteFixture(path, Wrap(bytes));
		auto source = Source(path);
		if (bad_shell) {
			source.OriginalFilename = "wrong.psb.m";
		}

		WrappedPsbReadDestination receiving;
		receiving.MarkQueued();
		receiving.Select();
		Require(!receiving.ReadSelected(source), "wrong cipher identity or malformed PSB checksum must not publish an invented object");
		receiving.PublishSelected();
		Require(receiving.FailurePhase() == (bad_shell ? WrappedPsbFailurePhase::ShellDecode : WrappedPsbFailurePhase::PsbDecode), "checked failure lost its concrete shell/PSB boundary");
		Require(receiving.CanRetire() && !receiving.Error().empty(), "completed failed worker borrow must retain its bounded receiver diagnostic");
	}
}

static void TestQueuedOwnerBoundary() {
	WrappedPsbReadDestination receiving;
	Require(receiving.CanRetire(), "unassigned receiving cell should have no published worker borrow");
	receiving.MarkQueued();
	Require(!receiving.CanRetire(), "actual queued receiving cell must prevent owner retirement");
	RequireFailure([&]() {
		receiving.MarkQueued();
	});
	Require(receiving.AbandonQueued() && receiving.CanRetire() && receiving.State() == WrappedPsbReadState::Abandoned, "authored removed-queue teardown must abandon its actual queued borrow without native completion3");
	Require(!receiving.AbandonQueued(), "authored queue abandonment must occur once");
	RequireFailure([&]() {
		receiving.Select();
	});
}

int main(int argc, char** argv) {
	try {
		if (argc != 2) {
			throw std::runtime_error("wrapped PSB fixture requires one ignored directory");
		}

		const std::filesystem::path directory(argv[1]);
		std::filesystem::create_directories(directory);
		const auto path = directory / "physical-region.bin";
		TestRealProductionAndRetainedOutput(path);
		TestPhysicalReadBeforeNextSelectionAndCurrentObject(path);
		TestPhysicalReadFailures(path);
		TestCheckedShellAndPsbFailures(path);
		TestQueuedOwnerBoundary();
		std::filesystem::remove(path);
		std::cout << "five actual wrapped PSB producer/receiving-owner groups pass\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}

	return 0;
}
