#include "sgre/script/debug_scene_entry.hpp"

#include <array>
#include <cstddef>
#include <span>
#include <stdexcept>
#include <utility>
#include <variant>

namespace sgre::script {

	void InstallOriginalDebugSceneEntry(SquirrelVm& vm, ScriptDebugSceneEntry entry) {
		constexpr std::size_t kMaximumStorageBytes = 4096;
		if (vm.IsClosed() || entry.Storage.empty() || entry.Storage.size() > kMaximumStorageBytes || entry.Storage.find('\0') != std::string::npos || entry.TargetLine < 0) {
			throw std::runtime_error("original debug scene entry requires its live VM, exact bounded storage and nonnegative source line");
		}

		const auto root = vm.RootTable();
		if (!vm.HasSlot(root, std::string("Application"))) {
			throw std::runtime_error("original debug scene entry precedes the original Application class publication");
		}
		if (vm.HasSlot(root, std::string("MyApplication")) || (vm.HasSlot(root, std::string("app")) && !std::holds_alternative<std::monostate>(vm.GetSlot(root, std::string("app"))))) {
			throw std::runtime_error("original debug scene entry must precede the actual application derivation and construction");
		}

		const auto application = vm.GetSlot(root, std::string("Application"));
		if (vm.Type(application) != SquirrelValueType::Class || !vm.HasSlot(application, std::string("getMainScene")) || vm.Type(vm.GetSlot(application, std::string("getMainScene"))) != SquirrelValueType::Closure) {
			throw std::runtime_error("original debug scene entry requires the original script getMainScene extension hook");
		}

		const std::array<SquirrelValue, 1> captures{application};
		const auto provider = vm.CreateNativeClosureWithCaptures("debug.Application.getMainScene", [entry = std::move(entry)](SquirrelVm& active, std::span<const SquirrelValue> arguments) {
			const auto application_class = active.NativeStackValue(-1);
			if (arguments.size() != 2 || active.Type(arguments.front()) != SquirrelValueType::Instance || !active.IsInstanceOf(arguments.front(), application_class)) {
				throw std::runtime_error("original debug scene provider requires its actual Application-derived receiver");
			}

			const auto scene = active.CreateTable();
			const auto scene_root = active.Retain(scene);
			active.NewSlot(scene, std::string("storage"), active.CreateString(entry.Storage));
			active.NewSlot(scene, std::string("target"), entry.TargetLine);

			return SquirrelNativeResult::ReturnValue(scene);
		},
		    captures);
		vm.NewSlot(application, std::string("getMainScene"), provider);
	}

} // namespace sgre::script
