#include "archive_psb_fixture.hpp"
#include "script_buffer_fixture.hpp"

#include "sgre/io/file_read_processor.hpp"
#include "sgre/script/object_bindings.hpp"
#include "sgre/script/thread_bindings.hpp"

#include <algorithm>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <optional>
#include <utility>

using sgre::script::ScriptScheduler;
using sgre::script::ScriptThreadStatus;
using sgre::script::SquirrelValue;
using sgre::script::SquirrelVm;

class ActualReaderFence {
public:
	explicit ActualReaderFence(sgre::scene::CommonProcessTasks& driver) : m_Driver(driver) {
	}
	~ActualReaderFence() noexcept {
		try {
			m_Driver.ClearParallel();
		} catch (...) {
			std::terminate();
		}
	}
	ActualReaderFence(const ActualReaderFence&) = delete;
	ActualReaderFence& operator=(const ActualReaderFence&) = delete;

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

static void MountActualScript(const std::filesystem::path& directory, sgre::resource::ResourceCache& cache, sgre::resource::ArchiveMounts& mounts, sgre::core::BackgroundCallManager& background, std::span<const std::uint8_t> decoded) {
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
	input.BodyFilename = PreparedFixture(body_file).Region.Filename;
	input.IndexRead.Region.Filename = index_file;
	input.IndexRead.Region.MaxBytes = 4096;
	input.IndexRead.OriginalFilename = "fixture_info.psb.m";
	input.IndexRead.Seed = "synthetic-seed";
	input.IndexRead.MaxDecodedBytes = 4096;
	input.IndexRead.MaxMetadataBytes = 4096;
	sgre::io::FileReadProcessor processor(cache, mounts, background);
	processor.QueueReadMount(std::move(input));
	processor.Update();
	DrainLane(background);
	processor.Update();
	processor.Update();
	processor.Update();
	Require(mounts.MountCount() == 1 && mounts.RetainedPsbObjectCount() == 1 && !mounts.HasInFlightReadMount(), "actual synthetic mount receiving owner failed to register");
	processor.Close();
}

static void FinishReaders(sgre::scene::CommonProcessTasks& driver, sgre::core::BackgroundCallManager& background) {
	driver.Parallel().Update();
	DrainLane(background);
	driver.Parallel().Update();
	driver.Parallel().Update();
	Require(driver.Parallel().Count() == 0, "actual independent script readers did not retire from containing Parallel");
}

static void TestPendingContextAndRealCompiledRun(const std::filesystem::path& directory) {
	SquirrelVm vm(ReclaimingLimits());
	sgre::resource::ResourceCache cache(0);
	sgre::resource::ArchiveMounts mounts;
	sgre::core::BackgroundCallManager background;
	sgre::scene::CommonProcessTasks driver;
	MountActualScript(directory, cache, mounts, background, CompiledFixture());
	sgre::script::ScriptObjectBindings objects(vm);
	ScriptScheduler scheduler(vm);
	sgre::script::ScriptThreadFileServices services{cache, driver, background, mounts, "synthetic-seed"};
	sgre::script::ScriptThreadBindings bindings(vm, scheduler, services, objects);
	ActualReaderFence fence(driver);
	const auto filename = vm.CreateString("script/item");
	const auto before = vm.RetainedReferenceCount(filename);
	const auto instance = vm.Retain(bindings.Spawn(filename));
	const auto handle = bindings.ThreadHandle(instance.Value());
	Require(driver.Parallel().Count() == 1 && driver.Serial().Count() == 0, "typed Thread reader must attach to containing driver Parallel only");
	static_cast<void>(scheduler.Advance(1));
	Require(!scheduler.Info(handle).Context && scheduler.Info(handle).HasScriptBuffer && scheduler.Info(handle).Status == ScriptThreadStatus::LoadingFile, "pending file must own +70 with no child context");
	RequireFailure([&]() {
		bindings.ReleaseInstance(instance.Value());
	});
	RequireFailure([&]() {
		scheduler.ShutdownNativeThreads();
	});
	Require(vm.RetainedReferenceCount(filename) == before + 1 && objects.HasInstance(instance.Value()) && vm.Length(scheduler.NativeThreadList()) == 1, "pending cleanup preflight must reject before filename/base/registration changes");
	FinishReaders(driver, background);
	scheduler.SetExecutionStopped(handle, true);
	static_cast<void>(scheduler.Advance(1));
	const auto loaded = scheduler.Info(handle);
	Require(loaded.Context && vm.ContextState(*loaded.Context) == sgre::script::SquirrelContextState::Idle && loaded.Status == ScriptThreadStatus::Running && !loaded.HasScriptBuffer, "actual child and closure must exist after +70 retires and before Running dispatch");
	Require(std::get<sgre::script::SquirrelString>(loaded.Filename).SharesIdentity(std::get<sgre::script::SquirrelString>(filename)) && vm.RetainedReferenceCount(filename) == before + 1, "load handoff must preserve the singular canonical +58 field");
	static_cast<void>(vm.CollectGarbage());
	scheduler.SetExecutionStopped(handle, false);
	const auto result = scheduler.Advance(1);
	Require(result.Completed.size() == 1 && std::get<std::int32_t>(scheduler.Info(handle).Result) == 42 && vm.RetainedReferenceCount(filename) == before + 1, "real compiled invocation must survive raw byte retirement and keep filename until owner retirement");
	bindings.ReleaseInstance(instance.Value());
	Require(vm.RetainedReferenceCount(filename) == before, "actual derived owner release must finally drop its filename field");
	background.Close();
}

static void TestManagerPinAndBaseContext(const std::filesystem::path& directory) {
	const auto raw = directory / "raw.bin";
	WriteFixture(raw, CompiledFixture());
	SquirrelVm vm(ReclaimingLimits());
	sgre::resource::ResourceCache cache(0);
	const auto resource = ProduceActualActiveBinary(cache, "script/item", raw);
	sgre::resource::ArchiveMounts mounts;
	sgre::core::BackgroundCallManager background;
	sgre::scene::CommonProcessTasks driver;
	ScriptScheduler scheduler(vm);
	const auto filename = vm.CreateString("script/item");
	const auto handle = scheduler.ForkFile(filename, cache, {});
	ActualReaderFence fence(driver);
	Require(!scheduler.AppendFileRead(handle, driver, background, mounts, "synthetic-seed") && driver.Parallel().Count() == 0 && cache.Info(resource).References == 2, "actual Active-only hit must add one separate pin and no reader");
	const auto base_root = vm.RootTableInContext(vm.RootContext());
	const auto caller = vm.CreateContext();
	vm.SetRootTableInContext(caller, vm.CreateTable());
	const auto advance = vm.CreateNativeClosure("independent nested readiness", [&](SquirrelVm& active, std::span<const SquirrelValue>) {
		Require(active.CurrentContext() == caller, "fixture must run inside a distinct caller context");
		scheduler.SetExecutionStopped(handle, true);
		static_cast<void>(scheduler.Advance(1));
		const auto info = scheduler.Info(handle);
		Require(info.Context && active.RootTableInContext(*info.Context) == base_root && active.CurrentContext() == caller && !info.HasScriptBuffer && cache.Info(resource).References == 1, "child must derive from actual base before load and restore caller after +70 pin release");

		return sgre::script::SquirrelNativeResult::NoValues();
	});
	static_cast<void>(vm.InvokeInContext(caller, advance, std::array<SquirrelValue, 1>{vm.RootTableInContext(caller)}));
	cache.Release(resource);
	Require(!cache.Contains(resource), "typed callable must own prototype without retaining cache bytes");
	scheduler.SetExecutionStopped(handle, false);
	static_cast<void>(scheduler.Advance(1));
	Require(std::get<std::int32_t>(scheduler.Info(handle).Result) == 42, "loaded typed closure must execute after actual cache eviction");
	scheduler.Stop(handle);
	scheduler.Release(handle);
	vm.ReleaseContext(caller);
	background.Close();
}

static void TestReadFailureAfterChild(const std::filesystem::path& directory) {
	SquirrelVm vm(ReclaimingLimits());
	sgre::resource::ResourceCache cache(0);
	sgre::resource::ArchiveMounts mounts;
	sgre::core::BackgroundCallManager background;
	sgre::scene::CommonProcessTasks driver;
	MountActualScript(directory, cache, mounts, background, CompiledFixture());
	std::filesystem::remove(directory / "body.bin");
	sgre::script::ScriptObjectBindings objects(vm);
	ScriptScheduler scheduler(vm);
	sgre::script::ScriptThreadBindings bindings(vm, scheduler, sgre::script::ScriptThreadFileServices{cache, driver, background, mounts, "synthetic-seed"}, objects);
	ActualReaderFence fence(driver);
	const auto filename = vm.CreateString("script/item");
	const auto instance = vm.Retain(bindings.Spawn(filename));
	const auto handle = bindings.ThreadHandle(instance.Value());
	const auto listener = vm.Retain(vm.CreateInstance(objects.ObjectClass()));
	objects.AttachInstance(listener.Value());
	bool notified = false;
	objects.Subscribe(instance.Value(), listener.Value(), [&](const SquirrelValue&) {
		const auto info = scheduler.Info(handle);
		Require(info.Status == ScriptThreadStatus::LoadingFile && info.Context && !info.HasScriptBuffer && !std::holds_alternative<std::monostate>(info.Filename) && !std::holds_alternative<std::monostate>(vm.LastErrorInContext(*info.Context)), "loader failure notification must see child diagnostic and retired +70, with filename and LoadingFile still owned");
		notified = true;

		return false;
	});
	FinishReaders(driver, background);
	static_cast<void>(scheduler.Advance(1));
	const auto failed = scheduler.Info(handle);
	Require(notified && failed.Status == ScriptThreadStatus::None && failed.NativeCallResult == -1 && !failed.Error.empty() && !failed.HasScriptBuffer && failed.Context, "typed loading failure must complete real notify/wait/arguments/None cleanup after buffer retirement");
	scheduler.ShutdownNativeThreads();
	Require(!std::holds_alternative<std::monostate>(scheduler.Info(handle).Filename) && vm.ContextState(*failed.Context) == sgre::script::SquirrelContextState::Idle, "already-loaded failure filename/context must survive native driver cleanup");
	vm.Close();
	vm.RethrowReleaseFailure();
	background.Close();
}

static void TestPendingShutdownOrderAndLoadedRetention(const std::filesystem::path& directory) {
	SquirrelVm vm(ReclaimingLimits());
	sgre::resource::ResourceCache cache(0);
	sgre::resource::ArchiveMounts mounts;
	sgre::core::BackgroundCallManager background;
	sgre::scene::CommonProcessTasks driver;
	MountActualScript(directory, cache, mounts, background, CompiledFixture());
	sgre::script::ScriptObjectBindings objects(vm);
	ScriptScheduler scheduler(vm);
	sgre::script::ScriptThreadBindings bindings(vm, scheduler, sgre::script::ScriptThreadFileServices{cache, driver, background, mounts, "synthetic-seed"}, objects);
	ActualReaderFence fence(driver);
	const auto listener = vm.Retain(vm.CreateInstance(objects.ObjectClass()));
	objects.AttachInstance(listener.Value());
	std::array<sgre::script::SquirrelHostRoot, 2> instances;
	std::array<sgre::script::ScriptThreadHandle, 2> handles;
	std::vector<std::size_t> order;
	const auto filename = vm.CreateString("script/item");
	const auto before = vm.RetainedReferenceCount(filename);
	for (std::size_t index = 0; index < 2; ++index) {
		instances[index] = vm.Retain(bindings.Spawn(filename));
		handles[index] = bindings.ThreadHandle(instances[index].Value());
		objects.Subscribe(instances[index].Value(), listener.Value(), [&, index](const SquirrelValue&) {
			const auto info = scheduler.Info(handles[index]);
			Require(info.Status == ScriptThreadStatus::LoadingFile && info.HasScriptBuffer && vm.RetainedReferenceCount(filename) == before + 2 - index && vm.Length(scheduler.NativeThreadList()) == 2, "native NotifyAll must precede buffer/filename retirement and retain array until all rows");
			order.push_back(index);

			return false;
		});
	}
	Require(driver.Parallel().Count() == 2, "each typed pending Thread must own a distinct fresh reader");
	RequireFailure([&]() {
		scheduler.ShutdownNativeThreads();
	});
	Require(order.empty(), "pending receiver preflight must reject before any native notification");
	driver.ClearParallel();
	scheduler.ShutdownNativeThreads();
	Require(order == std::vector<std::size_t>{0, 1} && vm.RetainedReferenceCount(filename) == before && std::holds_alternative<std::monostate>(scheduler.NativeThreadList()), "actual fenced pending buffers must drop filename in native order before final array owner release");
	for (const auto handle : handles) {
		Require(scheduler.Info(handle).Status == ScriptThreadStatus::None && !scheduler.Info(handle).HasScriptBuffer && std::holds_alternative<std::monostate>(scheduler.Info(handle).Filename), "pending shutdown row retains a native load field");
	}
	vm.Close();
	vm.RethrowReleaseFailure();
	background.Close();
}

static void TestLoadedDriverFilenameRetention(const std::filesystem::path& directory) {
	const auto raw = directory / "raw.bin";
	WriteFixture(raw, CompiledFixture());
	SquirrelVm vm(ReclaimingLimits());
	sgre::resource::ResourceCache cache(0);
	const auto resource = ProduceActualActiveBinary(cache, "script/item", raw);
	sgre::resource::ArchiveMounts mounts;
	sgre::core::BackgroundCallManager background;
	sgre::scene::CommonProcessTasks driver;
	sgre::script::ScriptObjectBindings objects(vm);
	ScriptScheduler scheduler(vm);
	sgre::script::ScriptThreadBindings bindings(vm, scheduler, sgre::script::ScriptThreadFileServices{cache, driver, background, mounts, "synthetic-seed"}, objects);
	ActualReaderFence fence(driver);
	const auto filename = vm.CreateString("script/item");
	const auto before = vm.RetainedReferenceCount(filename);
	const auto instance = vm.Retain(bindings.Spawn(filename));
	const auto handle = bindings.ThreadHandle(instance.Value());
	scheduler.SetExecutionStopped(handle, true);
	static_cast<void>(scheduler.Advance(1));
	const auto child = scheduler.Info(handle).Context;
	Require(child && !scheduler.Info(handle).HasScriptBuffer && cache.Info(resource).References == 1, "loaded typed row must release +70 before driver cleanup");
	RequireFailure([&]() {
		scheduler.ShutdownNativeFunctionThreads();
	});
	scheduler.ShutdownNativeThreads();
	Require(vm.RetainedReferenceCount(filename) == before + 1 && scheduler.Info(handle).Filename == filename && scheduler.Info(handle).Context == child && scheduler.Info(handle).Status == ScriptThreadStatus::None && vm.ContextState(*child) == sgre::script::SquirrelContextState::Idle, "native conditional cleanup must keep loaded +58/+78 through array retirement");
	bindings.ReleaseInstance(instance.Value());
	Require(vm.RetainedReferenceCount(filename) == before, "native derived Thread retirement must release surviving +58 exactly once");
	cache.Release(resource);
	background.Close();
}

static void TestClosureContextValidationAndRestore() {
	SquirrelVm vm;
	SquirrelVm other;
	const auto child = vm.CreateContext();
	const auto base = vm.CurrentContext();
	const auto program = sgre::script::ReadSquirrelBytecode(CompiledFixture());
	RequireFailure([&]() {
		static_cast<void>(vm.CreateClosureInContext(other.RootContext(), program));
	});
	Require(vm.CurrentContext() == base, "foreign context rejection changed active context");
	auto malformed = program;
	sgre::script::SquirrelOuterBinding outer;
	outer.Source = std::int32_t{0};
	malformed.MainFunction.OuterBindings.push_back(outer);
	RequireFailure([&]() {
		static_cast<void>(vm.CreateClosureInContext(child, malformed));
	});
	Require(vm.CurrentContext() == base, "prototype failure leaked the child active context");
	const auto closure = vm.CreateClosureInContext(child, program);
	Require(vm.CurrentContext() == base && std::get<std::int32_t>(vm.InvokeInContext(child, closure, std::array<SquirrelValue, 1>{vm.RootTableInContext(child)}).Value) == 42, "checked child prototype must be real and restore context after preparation");
	const auto callback = vm.CreateNativeClosure("context-reentry", [&](SquirrelVm& active, std::span<const SquirrelValue>) {
		RequireFailure([&]() {
			static_cast<void>(active.CreateClosureInContext(child, program));
		});

		return sgre::script::SquirrelNativeResult::NoValues();
	});
	static_cast<void>(vm.InvokeInContext(child, callback, std::array<SquirrelValue, 1>{vm.RootTableInContext(child)}));
	vm.ReleaseContext(child);
	RequireFailure([&]() {
		static_cast<void>(vm.CreateClosureInContext(child, program));
	});
	Require(vm.CurrentContext() == base, "retired context rejection changed caller selection");
}

static void TestChildAllocationAndSourceRejection(const std::filesystem::path& directory) {
	for (const bool child_allocation_failure : {false, true}) {
		const auto raw = directory / "raw.bin";
		const auto bytes = child_allocation_failure ? CompiledFixture() : std::vector<std::uint8_t>{'r', 'e', 't', 'u', 'r', 'n'};
		WriteFixture(raw, bytes);
		auto limits = ReclaimingLimits();
		if (child_allocation_failure) {
			limits.MaxContexts = 1;
		}
		SquirrelVm vm(limits);
		sgre::resource::ResourceCache cache(0);
		const auto resource = ProduceActualActiveBinary(cache, "script/item", raw);
		ScriptScheduler scheduler(vm);
		const auto filename = vm.CreateString("script/item");
		const auto handle = scheduler.ForkFile(filename, cache, {});
		Require(cache.Info(resource).References == 2, "ready buffer did not own its separate Active pin");
		static_cast<void>(scheduler.Advance(1));
		const auto info = scheduler.Info(handle);
		Require(info.Status == ScriptThreadStatus::None && info.NativeCallResult == -1 && !info.HasScriptBuffer && info.Filename == filename && cache.Info(resource).References == 1 && !info.Error.empty(), "authored child-allocation/unsupported-source failure must retire ready buffer and preserve filename without fake Running");
		Require(info.Context.has_value() != child_allocation_failure, "child-before-loader error boundary changed");
		scheduler.Stop(handle);
		scheduler.Release(handle);
		cache.Release(resource);
	}
}

static void PendingThreadDestruction(const std::filesystem::path& directory) {
	SquirrelVm vm;
	sgre::resource::ResourceCache cache(0);
	sgre::resource::ArchiveMounts mounts;
	sgre::core::BackgroundCallManager background;
	sgre::scene::CommonProcessTasks driver;
	MountActualScript(directory, cache, mounts, background, CompiledFixture());
	sgre::script::ScriptObjectBindings objects(vm);
	ScriptScheduler scheduler(vm);
	sgre::script::ScriptThreadBindings bindings(vm, scheduler, sgre::script::ScriptThreadFileServices{cache, driver, background, mounts, "synthetic-seed"}, objects);
	const auto instance = vm.Retain(bindings.Spawn(vm.CreateString("script/item")));
	const auto handle = bindings.ThreadHandle(instance.Value());
	Require(driver.Parallel().Count() == 1 && scheduler.Info(handle).HasScriptBuffer, "death fixture lacks actual pending Thread/read receiver ownership");
	RequireFailure([&]() {
		scheduler.RequireCanStop(handle);
	});
	// Arm only after assertions: an accidental fixture failure must not count as intended destruction.
	std::set_terminate([]() {
		std::_Exit(0);
	});
}

static void TestGenericPollShutdownStillRejected() {
	SquirrelVm vm;
	ScriptScheduler scheduler(vm);
	sgre::script::ScriptObjectBindings objects(vm);
	sgre::script::ScriptThreadBindings bindings(vm, scheduler, [](std::string_view) {
		return ScriptScheduler::LoadPoll{[]() -> std::optional<SquirrelValue> {
			return std::nullopt;
		}};
	},
	    objects);
	const auto instance = vm.Retain(bindings.Spawn(std::string("unsupported/generic.nut")));
	RequireFailure([&]() {
		scheduler.ShutdownNativeThreads();
	});
	Require(vm.Length(scheduler.NativeThreadList()) == 1 && scheduler.Info(bindings.ThreadHandle(instance.Value())).Status == ScriptThreadStatus::LoadingFile, "typed shutdown must keep generic owner unsupported without partial mutation");
}

int main(int argc, char** argv) {
	try {
		if (argc == 3 && std::string_view(argv[1]) == "--pending-thread-destruction") {
			const std::filesystem::path death_directory(argv[2]);
			std::filesystem::create_directories(death_directory);
			PendingThreadDestruction(death_directory);

			return 1;
		}
		if (argc != 2) {
			throw std::runtime_error("typed Thread fixtures require one ignored directory");
		}
		const std::filesystem::path directory(argv[1]);
		std::filesystem::create_directories(directory);
		TestPendingContextAndRealCompiledRun(directory);
		TestManagerPinAndBaseContext(directory);
		TestReadFailureAfterChild(directory);
		TestPendingShutdownOrderAndLoadedRetention(directory);
		TestLoadedDriverFilenameRetention(directory);
		TestClosureContextValidationAndRestore();
		TestGenericPollShutdownStillRejected();
		TestChildAllocationAndSourceRejection(directory);
		for (const auto& name : {"index.bin", "body.bin", "raw.bin"}) {
			std::filesystem::remove(directory / name);
		}
		std::cout << "eight typed Thread compiled-file/context/buffer/filename/driver cleanup groups pass\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
