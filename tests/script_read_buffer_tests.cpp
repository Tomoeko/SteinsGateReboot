#include "script_buffer_fixture.hpp"

#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>

static void TestRawOwnerGateAndSerialPhases(const std::filesystem::path& directory) {
	const auto bytes = CompiledFixture();
	const auto path = directory / "wrapped.bin";
	WriteFixture(path, WrappedFixture(bytes, "script/item.nut.m"));
	sgre::resource::ArchiveMounts mounts;
	sgre::core::BackgroundCallManager background;
	sgre::scene::CommonProcessTasks driver;
	sgre::io::ScriptReadBuffer buffer;
	const auto task = sgre::io::AppendPreparedScriptBufferReader(driver, background, mounts, buffer, PreparedFixture(path));
	Require(driver.Parallel().Count() == 1 && !buffer.Observe().Ready && !buffer.CanClose(), "actual fresh reader must borrow the raw receiver/gate without a cache entry");
	RequireFailure([&]() {
		buffer.Close();
	});
	driver.Parallel().Update();
	DrainLane(background);
	Require(buffer.Completion() == sgre::io::FileReadCompletion::Succeeded && !buffer.Observe().Ready && !buffer.CanClose(), "per-request completion cannot substitute for shared reader readiness");
	driver.Parallel().Update();
	Require(driver.Parallel().Contains(task) && !buffer.Observe().Ready, "background retirement must not collapse one-shot finish into the same common phase");
	driver.Parallel().Update();
	const auto observed = buffer.Observe();
	Require(!driver.Parallel().Contains(task) && observed.Ready && observed.Completion == sgre::io::FileReadCompletion::Succeeded && observed.ByteCount == bytes.size() && std::vector<std::uint8_t>(observed.Bytes.begin(), observed.Bytes.end()) == bytes && buffer.CanClose(), "actual one-shot finish must publish its shared gate then retire the reader");
	buffer.Close();
	background.Close();
}

static void TestContainingParallelDoesNotBlockDriver(const std::filesystem::path& directory) {
	const auto path = directory / "parallel.bin";
	WriteFixture(path, WrappedFixture(CompiledFixture(), "script/item.nut.m"));
	sgre::resource::ArchiveMounts mounts;
	sgre::core::BackgroundCallManager background;
	sgre::scene::CommonProcessTasks driver;
	sgre::io::ScriptReadBuffer first;
	sgre::io::ScriptReadBuffer second;
	std::size_t process_calls = 0;
	const auto process = [&]() {
		++process_calls;
		if (process_calls == 1) {
			static_cast<void>(sgre::io::AppendPreparedScriptBufferReader(driver, background, mounts, first, PreparedFixture(path)));
			static_cast<void>(sgre::io::AppendPreparedScriptBufferReader(driver, background, mounts, second, PreparedFixture(path)));
		}
	};
	driver.Update(process);
	Require(driver.Serial().Count() == 0 && driver.Parallel().Count() == 2 && process_calls == 1, "actual parallel attachment must permit same-frame enqueue without blocking driver onProcess");
	DrainLane(background);
	driver.Update(process);
	Require(process_calls == 2 && driver.Parallel().Count() == 2 && !first.Observe().Ready && !second.Observe().Ready, "each reader serial retirement must preserve driver process traversal");
	driver.Update(process);
	Require(process_calls == 3 && driver.Parallel().Count() == 0 && first.Observe().Ready && second.Observe().Ready && first.Observe().Bytes.data() != second.Observe().Bytes.data(), "fresh driver parallel readers must own distinct raw outputs and retire after their own gates");
	first.Close();
	second.Close();
	background.Close();
}

static void TestReadyFailureAndForcedFence(const std::filesystem::path& directory) {
	sgre::resource::ArchiveMounts mounts;
	sgre::core::BackgroundCallManager background;
	sgre::scene::CommonProcessTasks driver;
	sgre::io::ScriptReadBuffer failed;
	static_cast<void>(sgre::io::AppendPreparedScriptBufferReader(driver, background, mounts, failed, PreparedFixture(directory / "missing.bin")));
	driver.Parallel().Update();
	DrainLane(background);
	Require(failed.Completion() == sgre::io::FileReadCompletion::ReadFailed && !failed.Observe().Ready, "read2 must remain distinct from shared readiness");
	driver.Parallel().Update();
	driver.Parallel().Update();
	Require(failed.Observe().Ready && failed.Observe().Bytes.empty() && failed.Observe().ByteCount == 0 && !failed.Error().empty(), "actual ready failure must not fabricate a successful payload");
	failed.Close();
	sgre::io::ScriptReadBuffer queued;
	static_cast<void>(sgre::io::AppendPreparedScriptBufferReader(driver, background, mounts, queued, PreparedFixture(directory / "unstarted.bin")));
	Require(!queued.CanClose(), "unstarted physical reader still borrows its receiver");
	driver.ClearParallel();
	Require(queued.CanClose() && !queued.Observe().Ready && queued.Completion() == sgre::io::FileReadCompletion::Pending, "authored unstarted fence must release a borrow without inventing normal readiness/cancel status");
	queued.Close();
	background.Close();
}

static void TestActiveLookupIsNotCacheRevival(const std::filesystem::path& directory) {
	const auto bytes = CompiledFixture();
	const auto path = directory / "raw.bin";
	WriteFixture(path, bytes);
	sgre::resource::ResourceCache cache(4096);
	const auto handle = ProduceActualActiveBinary(cache, "script/item.nut", path);
	{
		auto pin = cache.TryPinActiveBinary("script/item.nut");
		Require(pin && pin->data() == cache.Bytes(handle).data() && cache.Info(handle).References == 2 && cache.Info(handle).Residency == sgre::resource::ResourceResidency::Active, "named Active hit must add one actual pointer pin without copying/promoting");
		pin.reset();
		sgre::io::ScriptReadBuffer buffer(cache, "script/item.nut");
		Require(buffer.Storage() == sgre::io::ScriptBufferStorage::ManagerBinaryPin && buffer.Observe().Ready && buffer.Observe().ByteCount == bytes.size(), "cached buffer must be immediately ready with its independent actual pin");
		cache.Release(handle);
		cache.ClearCache();
		Require(cache.Info(handle).References == 1 && buffer.Observe().Bytes[0] == 0xfa, "buffer pin must preserve data beyond original reference/clear");
	}
	Require(cache.Info(handle).Residency == sgre::resource::ResourceResidency::Cached && cache.Info(handle).References == 0 && !cache.TryPinActiveBinary("script/item.nut"), "ScriptBuffer hit must never revive the separate Cached map");
	const auto pending = cache.Acquire("script/loading.nut");
	Require(!cache.TryPinActiveBinary("script/loading.nut") && cache.Info(pending).References == 1 && cache.Info(pending).Residency == sgre::resource::ResourceResidency::Loading, "Loading lookup must neither pin nor manufacture completion");
	cache.Release(pending);
	cache.ClearCache();
}

static int VerifyPendingDestructor() {
	sgre::resource::ArchiveMounts mounts;
	sgre::core::BackgroundCallManager background;
	sgre::scene::CommonProcessTasks driver;
	auto buffer = std::make_unique<sgre::io::ScriptReadBuffer>();
	static_cast<void>(sgre::io::AppendPreparedScriptBufferReader(driver, background, mounts, *buffer, PreparedFixture("unstarted-fixture.bin")));
	Require(!buffer->CanClose() && driver.Parallel().Count() == 1, "pending buffer death check requires its real live reader borrow");
	std::set_terminate([]() {
		std::_Exit(0);
	});
	buffer.reset();

	return 1;
}

int main(int argc, char** argv) {
	try {
		if (argc == 2 && std::string_view(argv[1]) == "--pending-buffer-destruction") {
			return VerifyPendingDestructor();
		}
		if (argc != 2) {
			throw std::runtime_error("script buffer fixture requires one ignored directory");
		}

		const std::filesystem::path directory(argv[1]);
		std::filesystem::create_directories(directory);
		TestRawOwnerGateAndSerialPhases(directory);
		TestContainingParallelDoesNotBlockDriver(directory);
		TestReadyFailureAndForcedFence(directory);
		TestActiveLookupIsNotCacheRevival(directory);
		std::filesystem::remove(directory / "raw.bin");
		std::filesystem::remove(directory / "wrapped.bin");
		std::cout << "four independent raw script-buffer/reader-gate/Active-pin groups pass\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}

	return 0;
}
