#include "archive_psb_fixture.hpp"
#include "script_buffer_fixture.hpp"

#include "sgre/io/file_read_processor.hpp"
#include "sgre/script/thread_bindings.hpp"

#include <algorithm>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <utility>

using sgre::script::ScriptThreadStatus;
using sgre::script::SquirrelValue;

class ActualScriptReaderFence {
public:
	explicit ActualScriptReaderFence(sgre::scene::CommonProcessTasks& driver) : m_Driver(driver) {
	}
	~ActualScriptReaderFence() noexcept {
		try {
			m_Driver.ClearParallel();
		} catch (...) {
			std::terminate();
		}
	}

private:
	sgre::scene::CommonProcessTasks& m_Driver;
};

static sgre::script::SquirrelVmLimits ReclaimingLimits() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;

	return limits;
}

static void PutWord(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t word) {
	for (std::size_t index = 0; index < 4; ++index) {
		bytes[offset + index] = static_cast<std::uint8_t>(word >> (8 * index));
	}
}

static std::uint32_t Word(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
	std::uint32_t value = 0;
	for (std::size_t index = 0; index < 4; ++index) {
		value |= static_cast<std::uint32_t>(bytes[offset + index]) << (8 * index);
	}

	return value;
}

static void InsertIndexBytes(std::vector<std::uint8_t>& bytes, std::size_t at, std::span<const std::uint8_t> inserted) {
	bytes.insert(bytes.begin() + static_cast<std::ptrdiff_t>(at), inserted.begin(), inserted.end());
	for (std::size_t field = 8; field < 40; field += 4) {
		const auto offset = Word(bytes, field);
		if (offset >= at) {
			PutWord(bytes, field, offset + static_cast<std::uint32_t>(inserted.size()));
		}
	}
}

static std::vector<std::uint8_t> ScriptIndex(std::size_t body_bytes) {
	Require(body_bytes < 32768, "independent script body exceeds signed two-byte PSB atom");
	std::vector<std::uint8_t> bytes(kArchivePsbFixture.begin(), kArchivePsbFixture.end());
	// Extend the independent one-entry schema with '.m' expiration and a checked body extent.
	InsertIndexBytes(bytes, 385, std::array<std::uint8_t, 3>{'.', 'm', 0});
	bytes[374] = 2;
	InsertIndexBytes(bytes, 377, std::array<std::uint8_t, 1>{8});
	bytes[371] = 6;
	bytes[372] = static_cast<std::uint8_t>(body_bytes);
	InsertIndexBytes(bytes, 373, std::array<std::uint8_t, 1>{static_cast<std::uint8_t>(body_bytes >> 8)});
	bytes[353] = 1;
	bytes[346] = 11;
	InsertIndexBytes(bytes, 355, std::array<std::uint8_t, 3>{0, 0x15, 1});
	std::uint32_t first = 1;
	std::uint32_t second = 0;
	for (std::size_t index = 8; index < 40; ++index) {
		first = (first + bytes[index]) % 65521;
		second = (second + first) % 65521;
	}
	PutWord(bytes, 40, (second << 16) | first);
	const auto document = sgre::resource::ReadPsbMetadata(bytes, bytes.size());
	const auto index = sgre::resource::ReadArchiveIndex(document, body_bytes);
	Require(index.Entries.size() == 1 && index.Entries[0].Name == "item" && index.Entries[0].Size == body_bytes && index.ExpireSuffixes == std::vector<std::string>{".m"}, "independent concrete script index did not decode its schema");

	return bytes;
}

static std::vector<std::uint8_t> WrappedIndex(std::span<const std::uint8_t> bytes) {
	Require(bytes.size() >= 256 && bytes.size() < 65536, "index raw Zstandard fixture size exceeds two-byte content field");
	const auto encoded_size = static_cast<std::uint32_t>(bytes.size() - 256);
	std::vector<std::uint8_t> frame{0x28, 0xb5, 0x2f, 0xfd, 0x60, static_cast<std::uint8_t>(encoded_size), static_cast<std::uint8_t>(encoded_size >> 8)};
	const auto block = static_cast<std::uint32_t>(bytes.size() << 3) | 1;
	frame.push_back(static_cast<std::uint8_t>(block));
	frame.push_back(static_cast<std::uint8_t>(block >> 8));
	frame.push_back(static_cast<std::uint8_t>(block >> 16));
	frame.insert(frame.end(), bytes.begin(), bytes.end());
	sgre::resource::ResourceCipher cipher("fixture_info.psb.m", "synthetic-seed", 131);
	cipher.Transform(frame);
	std::vector<std::uint8_t> wrapped{'m', 'z', 's', 0};
	AppendWord(wrapped, static_cast<std::uint32_t>(bytes.size()));
	wrapped.insert(wrapped.end(), frame.begin(), frame.end());

	return wrapped;
}

static void QueueActualScriptMount(const std::filesystem::path& directory, sgre::io::FileReadProcessor& processor, std::span<const std::uint8_t> decoded) {
	std::filesystem::create_directories(directory);
	const auto body = WrappedFixture(decoded, "script/item.m");
	const auto body_file = directory / "body.bin";
	const auto index_file = directory / "index.bin";
	WriteFixture(body_file, body);
	WriteFixture(index_file, WrappedIndex(ScriptIndex(body.size())));
	sgre::resource::ArchiveReadMountInput input;
	input.Prefix = "script/";
	input.IndexIdentity = "fixture_info.psb.m";
	input.BodyIdentity = "fixture_body.bin";
	input.BodyBytes = body.size();
	input.BodyFilename = body_file;
	input.IndexRead.Region.Filename = index_file;
	input.IndexRead.Region.MaxBytes = 4096;
	input.IndexRead.OriginalFilename = "fixture_info.psb.m";
	input.IndexRead.Seed = "synthetic-seed";
	input.IndexRead.MaxDecodedBytes = 4096;
	input.IndexRead.MaxMetadataBytes = 4096;
	processor.QueueReadMount(std::move(input));
}

static void PublishActualMount(sgre::io::FileReadProcessor& processor, sgre::resource::ArchiveMounts& mounts, sgre::core::BackgroundCallManager& background) {
	processor.Update();
	Require(mounts.HasInFlightReadMount(), "containing processor did not claim the actual queued index");
	DrainLane(background);
	for (std::size_t visit = 0; visit < 8 && mounts.HasInFlightReadMount(); ++visit) {
		processor.Update();
	}
	Require(!mounts.HasInFlightReadMount() && mounts.QueuedReadMountCount() == 0 && mounts.MountCount() == 1 && mounts.RetainedPsbObjectCount() == 1, "actual index receiving publication did not finish");
}

static void FinishActualReaders(sgre::scene::CommonProcessTasks& driver, sgre::core::BackgroundCallManager& background) {
	driver.Parallel().Update();
	DrainLane(background);
	driver.Parallel().Update();
	driver.Parallel().Update();
	Require(driver.Parallel().Count() == 0, "fresh typed script readers did not retire from their actual Driver Parallel owner");
}

static void TestPendingMountAndActualCompiledHandoff(const std::filesystem::path& directory) {
	sgre::script::SquirrelVm vm(ReclaimingLimits());
	sgre::resource::ResourceCache cache(0);
	sgre::resource::ArchiveMounts mounts;
	sgre::core::BackgroundCallManager background;
	sgre::io::FileReadProcessor processor(cache, mounts, background);
	sgre::scene::CommonProcessTasks driver;
	sgre::script::ScriptObjectBindings objects(vm);
	sgre::script::ScriptScheduler scheduler(vm);
	sgre::script::ScriptThreadBindings bindings(vm, scheduler, sgre::script::ScriptThreadFileServices{cache, driver, background, mounts, "synthetic-seed"}, objects);
	ActualScriptReaderFence fence(driver);
	QueueActualScriptMount(directory, processor, CompiledFixture());
	const auto filename = vm.CreateString("script/item");
	std::array<sgre::script::SquirrelHostRoot, 2> instances;
	std::array<sgre::script::ScriptThreadHandle, 2> handles;
	for (std::size_t index = 0; index < handles.size(); ++index) {
		instances[index] = vm.Retain(bindings.Spawn(filename));
		handles[index] = bindings.ThreadHandle(instances[index].Value());
	}
	Require(driver.Parallel().Count() == 2 && cache.State().LoadingEntryNum == 0 && !mounts.Resolve("script/item.m"), "typed request did not retain two actual fresh readers without a cache entry or early mapping");
	driver.Parallel().Update();
	static_cast<void>(scheduler.Advance(1));
	for (const auto handle : handles) {
		const auto info = scheduler.Info(handle);
		Require(info.Status == ScriptThreadStatus::LoadingFile && info.HasScriptBuffer && !info.Context && info.Filename == filename, "queued mount was selected or compiled before its actual index publication");
	}
	processor.Update();
	Require(mounts.HasInFlightReadMount(), "actual index worker was not admitted ahead of pending scripts");
	DrainLane(background);
	driver.Parallel().Update();
	static_cast<void>(scheduler.Advance(1));
	Require(!mounts.Resolve("script/item.m") && scheduler.Info(handles[0]).HasScriptBuffer && !scheduler.Info(handles[0]).Context, "worker completion bypassed the actual containing mount publication gate");
	for (std::size_t visit = 0; visit < 8 && mounts.HasInFlightReadMount(); ++visit) {
		processor.Update();
	}
	Require(mounts.MountCount() == 1 && !mounts.HasInFlightReadMount(), "containing mount reception did not publish the actual fixture index");
	FinishActualReaders(driver, background);
	const auto tick = scheduler.Advance(1);
	Require(tick.Instructions == 4, "two genuine compiled-file children did not execute the independent two-instruction programs");
	for (const auto handle : handles) {
		const auto info = scheduler.Info(handle);
		Require(info.Status == ScriptThreadStatus::None && info.Context && !info.HasScriptBuffer && info.Filename == filename && std::get<std::int32_t>(info.Result) == 42 && info.Error.empty(), "actual file-owned compiled handoff did not preserve filename and retire its raw buffer before execution");
	}
	scheduler.ShutdownNativeThreads();
	vm.Close();
	vm.RethrowReleaseFailure();
	processor.Close();
	background.Close();
}

static void TestLoadedNativeShutdownOrder(const std::filesystem::path& directory) {
	sgre::script::SquirrelVm vm(ReclaimingLimits());
	sgre::resource::ResourceCache cache(0);
	sgre::resource::ArchiveMounts mounts;
	sgre::core::BackgroundCallManager background;
	sgre::io::FileReadProcessor processor(cache, mounts, background);
	sgre::scene::CommonProcessTasks driver;
	sgre::script::ScriptObjectBindings objects(vm);
	sgre::script::ScriptScheduler scheduler(vm);
	sgre::script::ScriptThreadBindings bindings(vm, scheduler, sgre::script::ScriptThreadFileServices{cache, driver, background, mounts, "synthetic-seed"}, objects);
	ActualScriptReaderFence fence(driver);
	QueueActualScriptMount(directory, processor, CompiledFixture());
	PublishActualMount(processor, mounts, background);
	const auto filename = vm.CreateString("script/item");
	const auto listener = vm.Retain(vm.CreateInstance(objects.ObjectClass()));
	objects.AttachInstance(listener.Value());
	std::array<sgre::script::SquirrelHostRoot, 2> instances;
	std::array<sgre::script::ScriptThreadHandle, 2> handles;
	std::vector<std::size_t> notifications;
	for (std::size_t index = 0; index < handles.size(); ++index) {
		instances[index] = vm.Retain(bindings.Spawn(filename));
		handles[index] = bindings.ThreadHandle(instances[index].Value());
		scheduler.SetExecutionStopped(handles[index], true);
		objects.Subscribe(instances[index].Value(), listener.Value(), [&, index](const SquirrelValue&) {
			const auto info = scheduler.Info(handles[index]);
			Require(info.Status == ScriptThreadStatus::Running && !info.HasScriptBuffer && info.Filename == filename && vm.Length(scheduler.NativeThreadList()) == 2, "native notification did not precede state and array retirement while preserving loaded filename");
			notifications.push_back(index);

			return false;
		});
	}
	const auto array_alias = vm.Retain(scheduler.NativeThreadList());
	FinishActualReaders(driver, background);
	static_cast<void>(scheduler.Advance(1));
	scheduler.RequireCanShutdownNativeThreads();
	scheduler.ShutdownNativeThreads();
	Require(notifications == std::vector<std::size_t>{0, 1} && vm.Length(array_alias.Value()) == 0 && std::holds_alternative<std::monostate>(scheduler.NativeThreadList()), "actual typed native shutdown did not visit before retiring the same aliased array");
	for (const auto handle : handles) {
		const auto info = scheduler.Info(handle);
		Require(info.Status == ScriptThreadStatus::None && info.Filename == filename && info.Context && vm.ContextState(*info.Context) == sgre::script::SquirrelContextState::Idle, "native loaded row lost its actual filename/context before VM Close");
	}
	vm.Close();
	vm.RethrowReleaseFailure();
	processor.Close();
	background.Close();
}

static void TestPendingFenceAndForeignMount(const std::filesystem::path& directory) {
	sgre::script::SquirrelVm vm(ReclaimingLimits());
	sgre::resource::ResourceCache cache(0);
	sgre::resource::ArchiveMounts mounts;
	sgre::resource::ArchiveMounts foreign;
	sgre::core::BackgroundCallManager background;
	sgre::io::FileReadProcessor processor(cache, mounts, background);
	sgre::scene::CommonProcessTasks driver;
	sgre::script::ScriptObjectBindings objects(vm);
	sgre::script::ScriptScheduler scheduler(vm);
	sgre::script::ScriptThreadBindings bindings(vm, scheduler, sgre::script::ScriptThreadFileServices{cache, driver, background, mounts, "synthetic-seed"}, objects);
	ActualScriptReaderFence fence(driver);
	QueueActualScriptMount(directory, processor, CompiledFixture());
	sgre::io::ScriptReadBuffer raw_receiver;
	RequireFailure([&]() {
		sgre::io::PreparedScriptBufferReader physical(raw_receiver, PreparedFixture(directory / "index.bin"), mounts, background);
	});
	Require(raw_receiver.CanClose(), "strict prepared physical reader acquired a borrow in the unsupported pending-mount domain");
	RequireFailure([&]() {
		static_cast<void>(sgre::io::AppendQueuedMountedScriptBufferReader(driver, background, mounts, raw_receiver, sgre::io::PrepareQueuedMountedScriptBufferRead(foreign, "script/item.m", "synthetic-seed")));
	});
	Require(raw_receiver.CanClose() && driver.Parallel().Count() == 0, "foreign source rejection acquired a raw receiver borrow or child");
	const auto filename = vm.CreateString("script/item");
	const auto instance = vm.Retain(bindings.Spawn(filename));
	const auto handle = bindings.ThreadHandle(instance.Value());
	RequireFailure([&]() {
		scheduler.ShutdownNativeThreads();
	});
	Require(scheduler.Info(handle).Status == ScriptThreadStatus::LoadingFile && scheduler.Info(handle).HasScriptBuffer && scheduler.Info(handle).Filename == filename, "pending shutdown rejection mutated typed ownership");
	driver.ClearParallel();
	scheduler.ShutdownNativeThreads();
	Require(scheduler.Info(handle).Status == ScriptThreadStatus::None && !scheduler.Info(handle).HasScriptBuffer && std::holds_alternative<std::monostate>(scheduler.Info(handle).Filename), "actual forced reader fence did not precede conditional native buffer/filename retirement");
	vm.Close();
	vm.RethrowReleaseFailure();
	processor.Close();
	background.Close();
}

static void TestSelectedMissingMapping() {
	sgre::script::SquirrelVm vm(ReclaimingLimits());
	sgre::resource::ResourceCache cache(0);
	sgre::resource::ArchiveMounts mounts;
	sgre::core::BackgroundCallManager background;
	sgre::io::FileReadProcessor processor(cache, mounts, background);
	sgre::scene::CommonProcessTasks driver;
	sgre::script::ScriptObjectBindings objects(vm);
	sgre::script::ScriptScheduler scheduler(vm);
	sgre::script::ScriptThreadBindings bindings(vm, scheduler, sgre::script::ScriptThreadFileServices{cache, driver, background, mounts, "synthetic-seed"}, objects);
	ActualScriptReaderFence fence(driver);
	const auto filename = vm.CreateString("script/missing");
	const auto instance = vm.Retain(bindings.Spawn(filename));
	const auto handle = bindings.ThreadHandle(instance.Value());
	Require(scheduler.Info(handle).Status == ScriptThreadStatus::LoadingFile && !scheduler.Info(handle).Context, "missing mapping was rejected before actual selected worker production");
	FinishActualReaders(driver, background);
	static_cast<void>(scheduler.Advance(1));
	const auto info = scheduler.Info(handle);
	Require(info.Status == ScriptThreadStatus::None && info.Context && !info.HasScriptBuffer && info.Filename == filename && info.Error.find("script/missing.m") != std::string::npos && info.Error.find("mounted resource was not found") != std::string::npos, "actual selected mapping failure did not pass through separate readiness and typed child/error retirement");
	scheduler.ShutdownNativeThreads();
	vm.Close();
	vm.RethrowReleaseFailure();
	processor.Close();
	background.Close();
}

static void TestActualActiveBinaryPin(const std::filesystem::path& directory) {
	std::filesystem::create_directories(directory);
	const auto raw_file = directory / "raw.bin";
	WriteFixture(raw_file, CompiledFixture());
	sgre::script::SquirrelVm vm(ReclaimingLimits());
	sgre::resource::ResourceCache cache(0);
	const auto resource = ProduceActualActiveBinary(cache, "script/item", raw_file);
	sgre::resource::ArchiveMounts mounts;
	sgre::core::BackgroundCallManager background;
	sgre::scene::CommonProcessTasks driver;
	sgre::script::ScriptObjectBindings objects(vm);
	sgre::script::ScriptScheduler scheduler(vm);
	sgre::script::ScriptThreadBindings bindings(vm, scheduler, sgre::script::ScriptThreadFileServices{cache, driver, background, mounts, "synthetic-seed"}, objects);
	ActualScriptReaderFence fence(driver);
	const auto instance = vm.Retain(bindings.Spawn(vm.CreateString("script/item")));
	const auto handle = bindings.ThreadHandle(instance.Value());
	Require(driver.Parallel().Count() == 0 && cache.Info(resource).References == 2, "genuine Active manager Binary hit did not own exactly one pin without a fresh reader");
	static_cast<void>(scheduler.Advance(1));
	Require(!scheduler.Info(handle).HasScriptBuffer && std::get<std::int32_t>(scheduler.Info(handle).Result) == 42 && cache.Info(resource).References == 1, "compiled handoff did not retire the actual Active binary pin");
	scheduler.ShutdownNativeThreads();
	vm.Close();
	vm.RethrowReleaseFailure();
	cache.Release(resource);
	background.Close();
}

int main(int argc, char** argv) {
	try {
		if (argc != 2) {
			throw std::runtime_error("queued typed script fixtures require one ignored output directory");
		}
		const std::filesystem::path directory(argv[1]);
		TestPendingMountAndActualCompiledHandoff(directory / "deferred");
		TestLoadedNativeShutdownOrder(directory / "loaded");
		TestPendingFenceAndForeignMount(directory / "pending");
		TestSelectedMissingMapping();
		TestActualActiveBinaryPin(directory / "active");
		std::cout << "5 queued typed script owner groups passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
