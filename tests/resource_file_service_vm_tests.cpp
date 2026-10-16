#include "resource_file_fixture.hpp"

#include "sgre/script/binary_bindings.hpp"

static sgre::script::SquirrelValue Call(sgre::script::SquirrelVm& vm, const sgre::script::SquirrelValue& instance, const std::string& name, const sgre::script::SquirrelValue& argument) {
	const std::array<sgre::script::SquirrelValue, 2> arguments{instance, argument};

	return vm.Invoke(vm.GetSlot(instance, name), arguments).Value;
}

static void TestActualPsbProxyAndManagerPins(const std::filesystem::path& directory) {
	ResourceCache cache(0);
	ArchiveMounts mounts;
	BackgroundCallManager background;
	FileReadProcessor processor(cache, mounts, background);
	MountActual(processor, mounts, background, directory / "index.bin", directory / "body.bin", Wrap(PayloadPsb(), "asset/item.m"));
	ScriptResourceFileService service(cache, mounts, processor, "synthetic-seed");
	{
		sgre::script::SquirrelVmLimits limits;
		limits.ReclaimObjects = true;
		sgre::script::SquirrelVm vm(limits);
		vm.InstallRecoveredBaseLibrary();
		sgre::script::ScriptObjectBindings objects(vm);
		sgre::script::ScriptPsbBindings psb(vm, objects);
		sgre::script::ScriptBinaryBindings binary(vm, objects);
		std::vector<std::string> errors;
		sgre::script::ScriptResourceBindings resources(vm, objects, psb, binary, service.RequestService(), [&](std::string_view name, std::string_view message) {
			errors.push_back(std::string(name) + ": " + std::string(message));
		});
		const std::array<sgre::script::SquirrelValue, 1> root_receiver{vm.RootTable()};
		auto instance = vm.Invoke(resources.ResourceClass(), root_receiver).Value;
		static_cast<void>(Call(vm, instance, "load", std::string("asset/item")));
		const auto handle = *cache.FindNamedResource("asset/item");
		Require(std::get<bool>(vm.ReadSlot(instance, std::string("loading"))) && cache.Info(handle).References == 1, "actual Resource must own its one pending native manager target");
		processor.Update();
		DrainLane(background);
		Require(!std::get<bool>(vm.ReadSlot(instance, std::string("waiting"))) && processor.Tasks().Serial().Count() == 1 && cache.Info(handle).References == 1, "actual script polling must import real receiving data before serial background retirement");
		auto first = Call(vm, instance, "find", std::string("asset/item"));
		auto second = Call(vm, instance, "find", std::string("asset/item"));
		Require(first != second && vm.IsInstanceOf(first, psb.ObjectClass()) && cache.Info(handle).References == 3, "actual PSBObject factory must acquire one separate manager reference for each find");
		auto proxy = vm.ReadSlot(first, std::string("root"));
		vm.NewSlot(vm.RootTable(), std::string("actual-resource-proxy"), proxy);
		const std::array<sgre::script::SquirrelValue, 1> receiver{instance};
		static_cast<void>(vm.Invoke(vm.GetSlot(instance, std::string("unload")), receiver));
		Require(cache.Info(handle).References == 2, "native unload must retire only its target reference, preserving returned PSBObjects");
		instance = {};
		second = {};
		first = {};
		vm.RethrowReleaseFailure();
		Require(cache.Info(handle).References == 1, "PSBValue document root must preserve exactly one actual find pin");
		cache.ClearCache();
		static_cast<void>(vm.CollectGarbage());
		Require(cache.Info(handle).References == 1 && std::get<bool>(vm.ReadSlot(proxy, std::int32_t(0))) && errors.empty(), "real proxy collection/rooted traversal must preserve the source-owned immutable decoded data");
		vm.SetSlot(vm.RootTable(), std::string("actual-resource-proxy"), {});
		proxy = {};
		vm.RethrowReleaseFailure();
		Require(!cache.Contains(handle) && service.CanClose(), "last actual native proxy must release its shared document/backing find reference");
	}
	service.Close();
	processor.Close();
	background.Close();
}

static void TestActualBinaryAndFailedTarget(const std::filesystem::path& directory) {
	ResourceCache cache(0);
	ArchiveMounts mounts;
	BackgroundCallManager background;
	FileReadProcessor processor(cache, mounts, background);
	const auto body = directory / "body.bin";
	MountActual(processor, mounts, background, directory / "index.bin", body, Wrap(PayloadPsb(), "asset/item.m"));
	ScriptResourceFileService service(cache, mounts, processor, "synthetic-seed");
	{
		sgre::script::SquirrelVmLimits limits;
		limits.ReclaimObjects = true;
		sgre::script::SquirrelVm vm(limits);
		vm.InstallRecoveredBaseLibrary();
		sgre::script::ScriptObjectBindings objects(vm);
		sgre::script::ScriptPsbBindings psb(vm, objects);
		sgre::script::ScriptBinaryBindings binary(vm, objects);
		std::vector<std::string> errors;
		sgre::script::ScriptResourceBindings resources(vm, objects, psb, binary, service.RequestService(), [&](std::string_view name, std::string_view message) {
			errors.push_back(std::string(name) + ": " + std::string(message));
		});
		const std::array<sgre::script::SquirrelValue, 1> receiver{vm.RootTable()};
		auto instance = vm.Invoke(resources.ResourceClass(), receiver).Value;
		static_cast<void>(Call(vm, instance, "loadBinary", std::string("asset/item")));
		const auto handle = *cache.FindNamedResource("asset/item");
		processor.Update();
		DrainLane(background);
		Require(!std::get<bool>(vm.ReadSlot(instance, std::string("loading"))), "actual wrapped binary target remained pending after real publication");
		auto data = Call(vm, instance, "find", std::string("asset/item"));
		Require(vm.IsInstanceOf(data, binary.BinaryClass()) && cache.Info(handle).References == 2 && std::get<std::int32_t>(vm.Invoke(vm.GetSlot(data, std::string("size")), std::array<sgre::script::SquirrelValue, 1>{data}).Value) == 76, "real Binary factory did not preserve decoded length and its separate pin");
		instance = {};
		vm.RethrowReleaseFailure();
		Require(cache.Info(handle).References == 1, "reclaimed Resource must release its real binary target reference");
		data = {};
		vm.RethrowReleaseFailure();
		Require(!cache.Contains(handle), "last Binary release must retire its actual cache generation");
		processor.Update();
		std::filesystem::remove(body);
		instance = vm.Invoke(resources.ResourceClass(), receiver).Value;
		static_cast<void>(Call(vm, instance, "loadRawBinary", std::string("asset/item")));
		const auto failed = *cache.FindNamedResource("asset/item");
		processor.Update();
		DrainLane(background);
		Require(!std::get<bool>(vm.ReadSlot(instance, std::string("loading"))) && cache.Info(failed).References == 1 && errors.size() == 1, "real failed script target must report its diagnostic and preserve target ownership");
		Require(std::holds_alternative<std::monostate>(Call(vm, instance, "find", std::string("asset/item"))), "actual failed target cannot fabricate Binary/PSB data");
		instance = {};
		vm.RethrowReleaseFailure();
		Require(!cache.Contains(failed) && service.CanClose(), "failed Resource finalization must release the actual active reference");
	}
	service.Close();
	processor.Close();
	background.Close();
}

int main(int argc, char** argv) {
	try {
		if (argc != 2) {
			throw std::runtime_error("actual resource-file VM fixture requires one ignored directory");
		}

		const std::filesystem::path directory(argv[1]);
		std::filesystem::create_directories(directory);
		TestActualPsbProxyAndManagerPins(directory);
		TestActualBinaryAndFailedTarget(directory);
		std::filesystem::remove(directory / "index.bin");
		std::filesystem::remove(directory / "body.bin");
		std::cout << "two actual resource-file VM/proxy/pin/finalization groups pass\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}

	return 0;
}
