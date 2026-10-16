#include "sgre/script/system_bindings.hpp"

#include "archive_psb_fixture.hpp"
#include "sgre/io/file_read_processor.hpp"
#include "sgre/resource/resource_cipher.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <span>
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
		Require(std::string_view(error.what()).find(expected) != std::string_view::npos, "archive mount failure lost its specific boundary");

		return;
	}

	throw std::runtime_error("archive mount fixture expected a failure");
}

static std::vector<std::uint8_t> WrappedIndex() {
	const auto size = static_cast<std::uint32_t>(kArchivePsbFixture.size());
	std::vector<std::uint8_t> frame{0x28, 0xb5, 0x2f, 0xfd, 0x60, static_cast<std::uint8_t>(size - 256), static_cast<std::uint8_t>((size - 256) >> 8)};
	const auto block = (size << 3) | 1;
	for (std::uint32_t index = 0; index < 3; ++index) {
		frame.push_back(static_cast<std::uint8_t>(block >> (index * 8)));
	}
	frame.insert(frame.end(), kArchivePsbFixture.begin(), kArchivePsbFixture.end());
	sgre::resource::ResourceCipher cipher("test_info.psb.m", "synthetic-seed", 131);
	cipher.Transform(frame);
	std::vector<std::uint8_t> result{'m', 'z', 's', 0};
	for (std::uint32_t index = 0; index < 4; ++index) {
		result.push_back(static_cast<std::uint8_t>(size >> (index * 8)));
	}
	result.insert(result.end(), frame.begin(), frame.end());

	return result;
}

static void WriteOwnedFile(const std::filesystem::path& path, std::span<const std::uint8_t> bytes) {
	Require(!std::filesystem::exists(path), "synthetic mount file already exists; preserve it for inspection");
	std::ofstream output(path, std::ios::binary);
	output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
	Require(static_cast<bool>(output), "synthetic mount file write failed");
}

static sgre::resource::ArchiveReadMountInput Input(const std::filesystem::path& index, const std::filesystem::path& body, std::string_view prefix, std::string_view info_name, std::string_view body_name) {
	sgre::resource::ArchiveReadMountInput input;
	input.Prefix = prefix;
	input.IndexIdentity = info_name;
	input.BodyIdentity = body_name;
	input.BodyFilename = body;
	input.BodyBytes = 4;
	input.IndexRead.Region.Filename = index;
	input.IndexRead.Region.MaxBytes = 4096;
	input.IndexRead.OriginalFilename = info_name;
	input.IndexRead.Seed = "synthetic-seed";
	input.IndexRead.MaxDecodedBytes = 4096;
	input.IndexRead.MaxMetadataBytes = 4096;

	return input;
}

static sgre::script::SquirrelValue Mount(sgre::script::SquirrelVm& vm, sgre::script::ScriptSystemBindings& system, const sgre::script::SquirrelValue& prefix, const sgre::script::SquirrelValue& flag = false) {
	const std::array<sgre::script::SquirrelValue, 5> arguments{system.SystemClass(), prefix, std::string("test_info.psb.m"), std::string("body.bin"), flag};

	return vm.Invoke(vm.GetSlot(system.SystemClass(), std::string("mountArchive")), arguments).Value;
}

static void DrainReadLane(sgre::core::BackgroundCallManager& background) {
	std::promise<void> barrier;
	auto completed = barrier.get_future();
	sgre::core::BackgroundCallTask task(background, sgre::core::BackgroundCallLane::BelowNormal, [&] {
		barrier.set_value();
	});
	task.Update();
	background.PostLoop();
	completed.get();
	task.Close();
}

static void TestActualQueuedPublication(const std::filesystem::path& index, const std::filesystem::path& body) {
	sgre::resource::ResourceCache cache(64);
	sgre::resource::ArchiveMounts mounts;
	sgre::core::BackgroundCallManager background;
	sgre::io::FileReadProcessor processor(cache, mounts, background);
	sgre::script::SquirrelVm vm;
	sgre::core::FrameClock clock;
	sgre::script::ScriptSystemBindings system(vm, clock);
	std::size_t resolutions = 0;
	system.ConfigureArchiveReadMounts(mounts, [&](auto prefix, auto info_name, auto body_name) {
		++resolutions;
		Require(prefix == "p/" && info_name == "test_info.psb.m" && body_name == "body.bin", "native string arguments changed before physical resolution");

		return Input(index, body, prefix, info_name, body_name);
	});
	Require(std::get<std::int32_t>(Mount(vm, system, std::string("p/\0ignored", 10), 0.5f)) == 0, "native mount return must be integer zero");
	Require(resolutions == 1 && mounts.QueuedReadMountCount() == 1 && mounts.MountCount() == 0, "actual queued mount was replaced with immediate host publication");
	processor.Update();
	Require(mounts.HasInFlightReadMount() && mounts.MountCount() == 0, "actual nested reader was not dispatched");
	DrainReadLane(background);
	processor.Update();
	Require(mounts.MountCount() == 0, "worker retirement prematurely published its mount");
	processor.Update();
	Require(mounts.MountCount() == 0, "one-shot reader retirement prematurely published its mount");
	processor.Update();
	const auto resolved = mounts.Resolve("p/item");
	Require(resolved && resolved->BodyFilename == body && resolved->BodyBytes == 4 && resolved->Entry.Offset == 0 && resolved->Entry.Size == 4, "mounted lookup lost its actual physical body mapping or checked range");
	Require(mounts.RetainedPsbObjectCount() == 1 && !mounts.HasInFlightReadMount(), "actual index output did not survive temporary reader retirement");
	processor.Close();
	vm.Close();
	vm.RethrowReleaseFailure();
	background.Close();
	std::cout << "actual script mount, nested worker publication and physical mapping PASS\n";
}

static void TestAdmissionFailures(const std::filesystem::path& index, const std::filesystem::path& body) {
	sgre::resource::ResourceCache cache(64);
	sgre::resource::ArchiveMounts mounts;
	sgre::core::BackgroundCallManager background;
	sgre::io::FileReadProcessor processor(cache, mounts, background);
	sgre::script::SquirrelVm vm;
	sgre::core::FrameClock clock;
	sgre::script::ScriptSystemBindings system(vm, clock);
	std::size_t resolutions = 0;
	system.ConfigureArchiveReadMounts(mounts, [&](auto prefix, auto info_name, auto body_name) {
		++resolutions;
		auto input = Input(index, body, prefix, info_name, body_name);
		input.BodyIdentity = "foreign.bin";

		return input;
	});
	RequireFailure([&] {
		static_cast<void>(Mount(vm, system, std::string("p/")));
	},
	    "differs from its actual script request");
	RequireFailure([&] {
		static_cast<void>(Mount(vm, system, std::string("p/"), 1.5f));
	},
	    "alternate physical read mode");
	RequireFailure([&] {
		static_cast<void>(Mount(vm, system, std::string("p/"), std::string("false")));
	},
	    "invalid argument 4.");
	RequireFailure([&] {
		static_cast<void>(Mount(vm, system, std::int32_t{1}));
	},
	    "invalid argument 1.");
	RequireFailure([&] {
		system.ConfigureArchiveReadMounts(mounts, {});
	},
	    "requires a prepared-input resolver");
	Require(resolutions == 1 && mounts.QueuedReadMountCount() == 0 && mounts.MountCount() == 0, "admission failure mutated actual mount reception or invoked unavailable mode resolution");
	processor.Close();
	vm.Close();
	vm.RethrowReleaseFailure();
	background.Close();
	std::cout << "exact request identity, native argument conversion and unavailable mode rejection PASS\n";
}

int main(int argc, char** argv) {
	try {
		Require(argc == 2, "system archive read mount fixture requires an ignored directory");
		const std::filesystem::path directory(argv[1]);
		std::filesystem::create_directories(directory);
		const auto index = directory / "system-owned-index.bin";
		const auto body = directory / "system-owned-body.bin";
		WriteOwnedFile(index, WrappedIndex());
		const std::array<std::uint8_t, 4> bytes{11, 22, 33, 44};
		WriteOwnedFile(body, bytes);
		TestActualQueuedPublication(index, body);
		TestAdmissionFailures(index, body);
		std::filesystem::remove(index);
		std::filesystem::remove(body);

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
