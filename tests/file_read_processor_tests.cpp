#include "sgre/io/file_read_processor.hpp"

#include "archive_psb_fixture.hpp"
#include "sgre/resource/resource_cipher.hpp"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using ByteVector = std::vector<std::uint8_t>;
using sgre::core::BackgroundCallLane;
using sgre::core::BackgroundCallManager;
using sgre::core::BackgroundCallTask;
using sgre::io::FileReadProcessor;
using sgre::resource::ArchiveMounts;
using sgre::resource::ArchiveReadMountInput;
using sgre::resource::ResourceCache;

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

	throw std::runtime_error("actual file-reader fixture expected preflight failure");
}

static void WriteFile(const std::filesystem::path& path, const ByteVector& bytes) {
	std::ofstream output(path, std::ios::binary | std::ios::trunc);
	for (const auto byte : bytes) {
		output.put(static_cast<char>(byte));
	}

	Require(static_cast<bool>(output), "actual file-reader fixture write failed");
}

static ByteVector WrappedIndex() {
	const auto size = static_cast<std::uint32_t>(kArchivePsbFixture.size());
	// Independent RFC8878 single-segment/raw-last block, two-byte content size encoded minus256.
	ByteVector frame{0x28, 0xb5, 0x2f, 0xfd, 0x60, static_cast<std::uint8_t>(size - 256), static_cast<std::uint8_t>((size - 256) >> 8)};
	const auto block = (size << 3) | 1;
	frame.push_back(static_cast<std::uint8_t>(block));
	frame.push_back(static_cast<std::uint8_t>(block >> 8));
	frame.push_back(static_cast<std::uint8_t>(block >> 16));
	frame.insert(frame.end(), kArchivePsbFixture.begin(), kArchivePsbFixture.end());
	sgre::resource::ResourceCipher cipher("test_info.psb.m", "synthetic-seed", 131);
	cipher.Transform(frame);
	ByteVector result{'m', 'z', 's', 0};
	for (std::uint32_t index = 0; index < 4; ++index) {
		result.push_back(static_cast<std::uint8_t>(size >> (index * 8)));
	}

	result.insert(result.end(), frame.begin(), frame.end());

	return result;
}

static ArchiveReadMountInput Input(const std::filesystem::path& path, std::string prefix, std::string body = "actual-body.bin") {
	ArchiveReadMountInput input;
	input.Prefix = std::move(prefix);
	input.IndexIdentity = "test_info.psb.m";
	input.BodyIdentity = std::move(body);
	input.BodyBytes = 4;
	input.IndexRead.Region.Filename = path;
	input.IndexRead.Region.MaxBytes = 4096;
	input.IndexRead.OriginalFilename = "test_info.psb.m";
	input.IndexRead.Seed = "synthetic-seed";
	input.IndexRead.MaxDecodedBytes = 4096;
	input.IndexRead.MaxMetadataBytes = 4096;

	return input;
}

static sgre::io::PreparedRawFileRead RawRegion(const std::filesystem::path& path) {
	sgre::io::PreparedRawFileRead input;
	input.Filename = path;
	input.MaxBytes = 4096;

	return input;
}

static void FinishThreeNestedVisits(FileReadProcessor& processor, ArchiveMounts& mounts) {
	processor.Update();
	Require(mounts.HasInFlightReadMount() && mounts.RetainedPsbObjectCount() == 0 && processor.Tasks().Serial().Count() == 1, "background retirement visit must not fabricate immediate mount publication");
	processor.Update();
	Require(mounts.HasInFlightReadMount() && mounts.RetainedPsbObjectCount() == 0 && processor.Tasks().Serial().Count() == 1, "one-shot reader retirement must precede real registration and retain its receiving output");
	processor.Update();
	Require(!mounts.HasInFlightReadMount() && mounts.RetainedPsbObjectCount() != 0 && processor.Tasks().Serial().Count() == 0, "real mount publication/retirement must follow both nested serial-empty gates");
}

static void DrainLane(BackgroundCallManager& background) {
	std::promise<void> barrier;
	auto done = barrier.get_future();
	BackgroundCallTask after(background, BackgroundCallLane::BelowNormal, [&]() {
		barrier.set_value();
	});
	after.Update();
	background.PostLoop();
	done.get();
	after.Close();
}

static void TestNativeNestedGatesAndUnmount(const std::filesystem::path& index, const std::filesystem::path& body) {
	ResourceCache cache(64);
	ArchiveMounts mounts;
	BackgroundCallManager background;
	FileReadProcessor processor(cache, mounts, background);
	const auto raw = cache.Acquire("raw");
	static_cast<void>(processor.QueuePreparedRaw(raw, "raw", RawRegion(body)));
	processor.QueueReadMount(Input(index, "p/"));
	std::promise<void> entered;
	auto active = entered.get_future();
	std::promise<void> release;
	auto permitted = release.get_future();
	BackgroundCallTask blocker(background, BackgroundCallLane::BelowNormal, [&]() {
		entered.set_value();
		permitted.get();
	});
	blocker.Update();
	processor.Update();
	Require(mounts.QueuedReadMountCount() == 0 && mounts.HasInFlightReadMount() && processor.Tasks().Serial().Count() == 1 && processor.PendingRawCount() == 1, "actual parent must claim a mount owner before ordinary pending raw dispatch");
	processor.Draw(73);
	processor.QueueReadMount(Input(index, "p/", "future-body.bin"));
	Require(mounts.QueuedReadMountCount() == 1, "future metadata request must remain in the genuine global pending list");
	mounts.Unmount("p/");
	Require(mounts.QueuedReadMountCount() == 0 && mounts.HasInFlightReadMount() && !mounts.Resolve("p/item"), "Unmount must remove pending metadata but preserve the swapped receiving-owner batch");
	background.PostLoop();
	active.get();
	std::promise<void> after_read;
	auto complete = after_read.get_future();
	BackgroundCallTask after(background, BackgroundCallLane::BelowNormal, [&]() {
		after_read.set_value();
	});
	after.Update();
	release.set_value();
	complete.get();
	blocker.Close();
	after.Close();
	FinishThreeNestedVisits(processor, mounts);
	const auto restored = mounts.Resolve("p/item");
	Require(restored && restored->BodyIdentity == "actual-body.bin" && restored->Entry.Size == 4 && processor.PendingRawCount() == 1, "in-flight native batch must register again after Unmount, without reading the removed future row");
	Require(cache.Info(raw).Completion == sgre::resource::ResourceCompletion::Pending, "ordinary raw request must still wait until the following parent onProcess visit");
	const auto* identity = &mounts.RetainedPsbObject(0);
	mounts.Unmount("p/");
	Require(mounts.MountCount() == 0 && mounts.RetainedPsbObjectCount() == 1 && &mounts.RetainedPsbObject(0) == identity && identity->Bytes().size() == kArchivePsbFixture.size(), "Unmount/temporary owner retirement must preserve the actual PSB data/document owner");
	processor.Update();
	Require(processor.Tasks().Serial().Count() == 1 && processor.PendingRawCount() == 1, "ordinary raw job must enqueue once after mount retirement without an implicit wake");
	DrainLane(background);
	processor.Update();
	cache.Advance();
	Require(cache.Bytes(raw).size() == 4 && cache.Bytes(raw)[0] == 11 && processor.PendingRawCount() == 0, "ordinary raw phase must use the same concrete requests/cache owner after real mount phases");
	processor.Close();
	Require(cache.CanRetireReadDestinations() && mounts.RetainedPsbObjectCount() == 1, "processor close detaches its actual raw owner while retaining enclosing archive outputs");
	background.Close();
}

static void TestQueuedAndSwappedClose(const std::filesystem::path& index) {
	ResourceCache cache(0);
	ArchiveMounts mounts;
	BackgroundCallManager background;
	FileReadProcessor processor(cache, mounts, background);
	processor.QueueReadMount(Input(index, "cancel/"));
	mounts.Unmount("cancel/");
	processor.Update();
	Require(!mounts.HasInFlightReadMount() && processor.Tasks().Serial().Count() == 0, "pre-claim Unmount must cancel an actual pending metadata record");
	processor.QueueReadMount(Input(index, "swapped/"));
	processor.Update();
	Require(mounts.HasInFlightReadMount() && processor.CanClose(), "enclosing owner should admit authored cancellation/fence of its queued nested child");
	processor.Close();
	Require(!mounts.HasInFlightReadMount() && mounts.RetainedPsbObjectCount() == 0 && cache.CanRetireReadDestinations(), "authored queued child close must remove/fence actual request borrows before swapped receiving rows retire");
	background.Close();
}

static void TestCompletedWorkerCloseRetainsOutput(const std::filesystem::path& index) {
	ResourceCache cache(0);
	ArchiveMounts mounts;
	BackgroundCallManager background;
	FileReadProcessor processor(cache, mounts, background);
	processor.QueueReadMount(Input(index, "unregistered/"));
	processor.Update();
	DrainLane(background);
	Require(mounts.RetainedPsbObjectCount() == 0 && mounts.HasInFlightReadMount(), "actual produced receiver must wait for nested owner registration/fencing");
	processor.Close();
	Require(!mounts.HasInFlightReadMount() && mounts.MountCount() == 0 && mounts.RetainedPsbObjectCount() == 1 && mounts.RetainedPsbObject(0).Document().Root.Find("file_info"), "authored forced close must retain actual produced PSB data even before registration");
	background.Close();
}

static void TestSharedActualArchiveOwner(const std::filesystem::path& index, const std::filesystem::path& body) {
	ResourceCache first_cache(0);
	ResourceCache second_cache(0);
	ArchiveMounts mounts;
	BackgroundCallManager background;
	FileReadProcessor first(first_cache, mounts, background);
	FileReadProcessor second(second_cache, mounts, background);
	first.QueueReadMount(Input(index, "shared/"));
	first.Update();
	const auto raw = second_cache.Acquire("second-raw");
	static_cast<void>(second.QueuePreparedRaw(raw, "second-raw", RawRegion(body)));
	second.Update();
	Require(second.Tasks().Serial().Count() == 0 && second.PendingRawCount() == 1, "another actual processor must respect the shared global in-flight mount gate");
	DrainLane(background);
	FinishThreeNestedVisits(first, mounts);
	second.Update();
	DrainLane(background);
	second.Update();
	second_cache.Advance();
	Require(second_cache.Bytes(raw)[3] == 44, "second actual reader must resume real raw production after global mount owner retirement");
	second.Close();
	first.Close();
	background.Close();
}

static void TestModeCapacityAndConstructorPreflight(const std::filesystem::path& index) {
	ResourceCache cache(0);
	sgre::resource::ArchiveMountLimits limits;
	limits.MaxPreparedArchives = 1;
	limits.MaxPendingMounts = 1;
	limits.MaxMounts = 1;
	ArchiveMounts mounts(limits);
	BackgroundCallManager background;
	FileReadProcessor processor(cache, mounts, background);
	RequireFailure([&]() {
		mounts.Advance();
	});
	RequireFailure([&]() {
		mounts.QueueMount("p/", "prepared", "body");
	});
	RequireFailure([&]() {
		mounts.Prepare("index", "body", {});
	});
	processor.QueueReadMount(Input(index, "one/"));
	RequireFailure([&]() {
		processor.QueueReadMount(Input(index, "overflow/"));
	});
	Require(mounts.QueuedReadMountCount() == 1, "capacity rejection must preserve the one actual pending mount record");
	mounts.Unmount("one/");
	Require(mounts.QueuedReadMountCount() == 0, "bounded cancelled admission must remain an unassigned receiving record");
	processor.Close();
	RequireFailure([&]() {
		processor.QueueReadMount(Input(index, "closed/"));
	});
	ArchiveMounts host_model;
	host_model.Prepare("prepared-index", "prepared-body", {});
	ResourceCache unused_cache(0);
	RequireFailure([&]() {
		FileReadProcessor rejected(unused_cache, host_model, background);
	});
	Require(unused_cache.CanRetireReadDestinations(), "rejected host-model attachment must unwind its concrete raw cache relationship before receiver retirement");
	background.Close();
}

static void TestIndexOnlyGate() {
	const auto document = sgre::resource::ReadPsbMetadata(kArchivePsbFixture, kArchivePsbFixture.size());
	sgre::io::PublishedPsbFile file(ByteVector(kArchivePsbFixture.begin(), kArchivePsbFixture.end()), document);
	Require(sgre::io::PrepareIndexOnlyMount(file, 4).Entries.size() == 1, "observed archive root must admit real index-only preparation");
	RequireFailure([&]() {
		static_cast<void>(sgre::io::PrepareIndexOnlyMount(file, 3));
	});
	for (const bool null_spec : {true, false}) {
		auto with_spec = document;
		sgre::resource::PsbValue spec;
		if (!null_spec) {
			spec.Data = std::string("other-target");
		}

		std::get<sgre::resource::PsbObject>(with_spec.Root.Data).emplace_back("spec", std::move(spec));
		sgre::io::PublishedPsbFile unsupported(ByteVector(kArchivePsbFixture.begin(), kArchivePsbFixture.end()), std::move(with_spec));
		RequireFailure([&]() {
			static_cast<void>(sgre::io::PrepareIndexOnlyMount(unsupported, 4));
		});
	}

	auto non_object = document;
	non_object.Root.Data = sgre::resource::PsbArray{};
	sgre::io::PublishedPsbFile unsupported(ByteVector(kArchivePsbFixture.begin(), kArchivePsbFixture.end()), std::move(non_object));
	RequireFailure([&]() {
		static_cast<void>(sgre::io::PrepareIndexOnlyMount(unsupported, 4));
	});
}

static void TestPendingOwnerSurvivesProcessor(const std::filesystem::path& index) {
	ResourceCache cache(0);
	ArchiveMounts mounts;
	BackgroundCallManager background;
	{
		FileReadProcessor first(cache, mounts, background);
		first.QueueReadMount(Input(index, "later/"));
		first.Close();
	}
	Require(mounts.QueuedReadMountCount() == 1 && !mounts.HasInFlightReadMount(), "global pending rows must survive a processor which never claimed them");
	FileReadProcessor second(cache, mounts, background);
	second.Update();
	DrainLane(background);
	FinishThreeNestedVisits(second, mounts);
	second.Close();
	Require(mounts.Resolve("later/item") && mounts.RetainedPsbObjectCount() == 1 && mounts.RetainedPsbObject(0).Bytes().size() == kArchivePsbFixture.size(), "actual enclosing receiving owner must preserve both pending admission and published output beyond processor scopes");
	background.Close();
}

static void FatalIndexDestruction(const std::filesystem::path& directory) {
	const auto missing = directory / "missing-original-index.bin";
	Require(!std::filesystem::exists(missing), "fatal index fixture requires an absent physical input");
	ResourceCache cache(0);
	ArchiveMounts mounts;
	BackgroundCallManager background;
	FileReadProcessor processor(cache, mounts, background);
	processor.QueueReadMount(Input(missing, "fatal/"));
	processor.Update();
	DrainLane(background);
	Require(!processor.CanClose() && !processor.IsClosed() && mounts.HasInFlightReadMount() && mounts.RetainedPsbObjectCount() == 0, "null-status fatal index read must retain its actual receiving/serial owner and reject fabricated completion");
	RequireFailure([&]() {
		processor.Close();
	});
	Require(!processor.IsClosed() && mounts.HasInFlightReadMount() && !cache.CanRetireReadDestinations(), "fatal owner preflight must reject before raw detachment and batch retirement");
	std::set_terminate([]() {
		std::_Exit(0);
	});
}

static void ReceivingOwnerDestruction() {
	ResourceCache cache(0);
	auto mounts = std::make_unique<ArchiveMounts>();
	BackgroundCallManager background;
	FileReadProcessor processor(cache, *mounts, background);
	Require(processor.CanClose() && !cache.CanRetireReadDestinations(), "actual live processor must own its concrete cache relationship before receiving-owner destruction");
	std::set_terminate([]() {
		std::_Exit(0);
	});
	mounts.reset();
	std::_Exit(1);
}

int main(int argc, char** argv) {
	try {
		if (argc != 2 && argc != 3) {
			throw std::runtime_error("actual file read processor fixture requires one ignored directory");
		}

		const auto decoded = sgre::resource::ReadPsbMetadata(kArchivePsbFixture, kArchivePsbFixture.size());
		const auto archive = sgre::resource::ReadArchiveIndex(decoded, 4);
		Require(archive.Entries.size() == 1 && archive.Entries[0].Name == "item", "independent serialized archive fixture does not encode its stated receiving schema");

		const std::filesystem::path directory(argv[1]);
		std::filesystem::create_directories(directory);
		if (argc == 3) {
			const std::string mode(argv[2]);
			if (mode == "--fatal-index-destruction") {
				FatalIndexDestruction(directory);

				return 1;
			}
			if (mode == "--receiving-owner-destruction") {
				ReceivingOwnerDestruction();

				return 1;
			}

			throw std::runtime_error("unknown actual file-reader lifetime fixture mode");
		}
		const auto index = directory / "prepared-index.bin";
		const auto body = directory / "actual-raw-body.bin";
		WriteFile(index, WrappedIndex());
		WriteFile(body, {11, 22, 33, 44});
		TestIndexOnlyGate();
		TestNativeNestedGatesAndUnmount(index, body);
		TestQueuedAndSwappedClose(index);
		TestCompletedWorkerCloseRetainsOutput(index);
		TestSharedActualArchiveOwner(index, body);
		TestModeCapacityAndConstructorPreflight(index);
		TestPendingOwnerSurvivesProcessor(index);
		std::filesystem::remove(index);
		std::filesystem::remove(body);
		std::cout << "seven actual file-reader common/serial/receiving-owner groups pass\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}

	return 0;
}
