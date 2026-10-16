#include "script_buffer_fixture.hpp"

#include "sgre/script/script_buffer_load.hpp"

#include <iostream>

static void FinishActualReader(sgre::scene::CommonProcessTasks& driver, sgre::core::BackgroundCallManager& background) {
	driver.Parallel().Update();
	DrainLane(background);
	driver.Parallel().Update();
	driver.Parallel().Update();
}

static void TestRawLoadFilenameAndClosure(const std::filesystem::path& directory) {
	const auto decoded = CompiledFixture();
	const auto path = directory / "wrapped.bin";
	WriteFixture(path, WrappedFixture(decoded, "script/item.nut.m"));
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	sgre::script::SquirrelVm vm(limits);
	vm.InstallRecoveredBaseLibrary();
	sgre::resource::ResourceCache cache(0);
	sgre::resource::ArchiveMounts mounts;
	sgre::core::BackgroundCallManager background;
	sgre::scene::CommonProcessTasks driver;
	const auto filename = vm.CreateString("script/item.nut");
	const auto before = vm.RetainedReferenceCount(filename);
	sgre::script::ScriptBufferLoad load(vm, filename, cache);
	Require(std::get<sgre::script::SquirrelString>(load.FilenameValue()).SharesIdentity(std::get<sgre::script::SquirrelString>(filename)) && vm.RetainedReferenceCount(filename) == before + 1 && load.ReadFilename() == "script/item.nut.m", "actual filename must retain one native field and preserve its identity/read suffix");
	static_cast<void>(sgre::io::AppendPreparedScriptBufferReader(driver, background, mounts, load.Buffer(), PreparedFixture(path)));
	RequireFailure([&]() {
		load.Close();
	});
	RequireFailure([&]() {
		static_cast<void>(load.LoadCompiledClosureAndReleaseBuffer());
	});
	Require(load.HasBuffer() && cache.State().LoadingEntryNum == 0 && vm.RetainedReferenceCount(filename) == before + 1, "pending raw miss must keep its direct receiving/name owners without a fake cache target");
	FinishActualReader(driver, background);
	static_cast<void>(vm.CollectGarbage());
	auto closure = load.LoadCompiledClosureAndReleaseBuffer();
	Require(!load.HasBuffer() && driver.Parallel().Count() == 0 && vm.RetainedReferenceCount(filename) == before + 1 && sgre::script::StringText(load.FilenameValue()) == "script/item.nut", "actual buffer must retire immediately after real compile, with filename still owned before execution");
	const std::array<sgre::script::SquirrelValue, 1> arguments{vm.RootTable()};
	Require(std::get<std::int32_t>(vm.Invoke(closure, arguments).Value) == 42, "real decoded closure must remain valid after raw buffer release");
	load.Close();
	Require(vm.RetainedReferenceCount(filename) == before, "explicit enclosing load retirement must release only its filename field");
	background.Close();
}

static void TestActualActivePinHandoff(const std::filesystem::path& directory) {
	const auto path = directory / "raw.bin";
	WriteFixture(path, CompiledFixture());
	sgre::script::SquirrelVm vm;
	sgre::resource::ResourceCache cache(0);
	const auto handle = ProduceActualActiveBinary(cache, "script/item.nut", path);
	sgre::resource::ArchiveMounts mounts;
	sgre::core::BackgroundCallManager background;
	sgre::scene::CommonProcessTasks driver;
	sgre::script::ScriptBufferLoad load(vm, vm.CreateString("script/item.nut"), cache);
	Require(cache.Info(handle).References == 2 && load.Buffer().Storage() == sgre::io::ScriptBufferStorage::ManagerBinaryPin && load.Buffer().Observe().Ready, "manager hit requires one independent actual raw pointer pin and immediate gate readiness");
	Require(!load.AppendMountedRead(driver, background, mounts, "synthetic-seed") && driver.Parallel().Count() == 0, "manager hit must not append a fake reader or perform mounted I/O");
	const auto closure = load.LoadCompiledClosureAndReleaseBuffer();
	Require(!load.HasBuffer() && cache.Info(handle).References == 1, "actual cached ScriptBuffer release must drop its one pin before execution");
	cache.Release(handle);
	Require(!cache.Contains(handle), "loader closure must not manufacture a raw-resource pin");
	const std::array<sgre::script::SquirrelValue, 1> arguments{vm.RootTable()};
	Require(std::get<std::int32_t>(vm.Invoke(closure, arguments).Value) == 42, "compiled closure must own its prototype beyond cache eviction");
	load.Close();
	background.Close();
}

static void TestReadyFailureReleasesOnlyBuffer(const std::filesystem::path& directory) {
	sgre::script::SquirrelVm vm;
	sgre::resource::ResourceCache cache(0);
	sgre::resource::ArchiveMounts mounts;
	sgre::core::BackgroundCallManager background;
	sgre::scene::CommonProcessTasks driver;
	const auto filename = vm.CreateString("script/item.nut");
	const auto before = vm.RetainedReferenceCount(filename);
	sgre::script::ScriptBufferLoad load(vm, filename, cache);
	Require(load.AppendMountedRead(driver, background, mounts, "synthetic-seed").has_value() && driver.Parallel().Count() == 1 && !load.Buffer().Observe().Ready, "logical miss must append a genuine pending reader without precompleted bytes");
	FinishActualReader(driver, background);
	Require(load.Buffer().Observe().Ready && load.Buffer().Observe().Completion == sgre::io::FileReadCompletion::ReadFailed && load.Buffer().Error().find("script/item.nut.m") != std::string::npos, "selected mapping error still requires separate real reader finish and exact original identity");
	RequireFailure([&]() {
		static_cast<void>(load.LoadCompiledClosureAndReleaseBuffer());
	});
	Require(!load.HasBuffer() && vm.RetainedReferenceCount(filename) == before + 1 && sgre::script::StringText(load.FilenameValue()) == "script/item.nut", "loader failure must release raw buffer but retain the actual filename field");
	load.Close();
	Require(vm.RetainedReferenceCount(filename) == before, "failure retirement must release the remaining filename exactly once");
	// Retain the existing checked physical missing-file failure independently of the queued mapped lane.
	sgre::script::ScriptBufferLoad physical_missing(vm, filename, cache);
	static_cast<void>(sgre::io::AppendPreparedScriptBufferReader(driver, background, mounts, physical_missing.Buffer(), PreparedFixture(directory / "missing.bin")));
	FinishActualReader(driver, background);
	Require(physical_missing.Buffer().Observe().Ready && physical_missing.Buffer().Observe().Completion == sgre::io::FileReadCompletion::ReadFailed, "prepared physical failure lost its distinct reader finish gate");
	RequireFailure([&]() {
		static_cast<void>(physical_missing.LoadCompiledClosureAndReleaseBuffer());
	});
	physical_missing.Close();
	Require(vm.RetainedReferenceCount(filename) == before, "physical failure retained its filename field after explicit retirement");
	background.Close();
}

int main(int argc, char** argv) {
	try {
		if (argc != 2) {
			throw std::runtime_error("script buffer VM fixture requires one ignored directory");
		}

		const std::filesystem::path directory(argv[1]);
		std::filesystem::create_directories(directory);
		TestRawLoadFilenameAndClosure(directory);
		TestActualActivePinHandoff(directory);
		TestReadyFailureReleasesOnlyBuffer(directory);
		std::filesystem::remove(directory / "raw.bin");
		std::filesystem::remove(directory / "wrapped.bin");
		std::cout << "three actual script-buffer filename/compile/cache-pin handoff groups pass\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}

	return 0;
}
