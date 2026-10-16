#include "sgre/script/motion_resource_owner.hpp"

#include "sgre/script/object_bindings.hpp"
#include "sgre/script/psb_bindings.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <memory>
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
static void RequireFailure(Operation operation, std::string_view context) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(context) != std::string_view::npos, error.what());

		return;
	}

	throw std::runtime_error("expected native motion resource owner failure was not reported");
}

template <typename Value>
static sgre::resource::PsbValue Atom(Value value) {
	sgre::resource::PsbValue atom;
	atom.Data = std::move(value);

	return atom;
}

static std::shared_ptr<sgre::resource::PsbDocument> Document() {
	using sgre::resource::PsbArray;
	using sgre::resource::PsbObject;
	sgre::resource::PsbChunkReference chunk;
	auto texture = Atom(PsbObject{{"type", Atom(std::string("A8"))}, {"width", Atom(std::int64_t{1})}, {"height", Atom(std::int64_t{1})}, {"truncated_width", Atom(std::int64_t{1})}, {"truncated_height", Atom(std::int64_t{1})}, {"pixel", Atom(chunk)}});
	texture.SerializedOffset = 64;
	auto source = Atom(PsbObject{{"texture", std::move(texture)}, {"icon", Atom(PsbObject{})}});
	auto document = std::make_shared<sgre::resource::PsbDocument>();
	document->Header.Version = 4;
	document->Header.RootOffset = 44;
	document->Header.DataOffset = 127;
	document->SerializedSize = 128;
	document->Chunks.push_back({127, 1});
	document->Root = Atom(PsbObject{{"id", Atom(std::string("motion"))}, {"spec", Atom(std::string("win"))}, {"version", Atom(3.03)}, {"label", Atom(std::string("Synthetic"))}, {"easing", Atom(PsbArray{})}, {"source", Atom(PsbObject{{"source", std::move(source)}})}, {"object", Atom(PsbObject{})}, {"sourceIconRenameMap", Atom(PsbObject{{"alias", Atom(PsbArray{Atom(std::string("source")), Atom(std::string("icon"))})}})}});
	document->Root.SerializedOffset = document->Header.RootOffset;

	return document;
}

static std::shared_ptr<std::vector<std::uint8_t>> Backing() {
	auto bytes = std::make_shared<std::vector<std::uint8_t>>(128);
	bytes->back() = 128;

	return bytes;
}

static void TestPairedRegistrationAndScriptOwnership() {
	sgre::script::SquirrelVmLimits vm_limits;
	vm_limits.ReclaimObjects = true;
	sgre::script::SquirrelVm vm(vm_limits);
	vm.InstallRecoveredBaseLibrary();
	sgre::script::ScriptObjectBindings objects(vm);
	sgre::script::ScriptPsbBindings psb(vm, objects);
	std::vector<std::string> events;
	const auto destructor = vm.CreateNativeClosure("PSB destructor fixture", [&](auto&, auto) {
		events.push_back("psb");

		return sgre::script::SquirrelNativeResult{};
	});
	vm.NewSlot(psb.ObjectClass(), std::string("destructor"), destructor);
	std::uint32_t uploads = 0;
	std::weak_ptr<const sgre::scene::LoadedMotionAsset> weak_asset;
	sgre::scene::TextureCache cache([&](auto, const auto&) {
		return sgre::render::SpriteTextureHandle{++uploads, 1, &uploads};
	},
	    [&](auto) {
		    Require(!weak_asset.expired(), "portable loaded data must survive native PSB root release through both manager cleanups");
		    events.push_back("gpu");
	    });
	sgre::scene::MotionAssetCatalog catalog(cache);
	sgre::script::ScriptMotionResourceOwner owner(vm, psb, catalog, cache);
	const auto document = Document();
	const auto backing = Backing();
	auto first = psb.WrapDocument(document, backing);
	auto second = psb.WrapDocument(document, backing);
	const auto weak_first = vm.CreateWeakReference(first);
	const auto project = owner.Register(first);
	weak_asset = owner.Asset(project);
	const auto duplicate = owner.Register(second);
	Require(project == duplicate && owner.Count() == 1 && owner.ReferenceCount(project) == 2 && vm.RetainedReferenceCount(first) == 1 && vm.RetainedReferenceCount(second) == 0, "native paired registration must retain only the first PSBObject once while incrementing logical counts");
	Require(owner.RegisteredPsbOwner(project) == first && owner.Primary().ReferenceCount(project) == 2 && owner.Secondary().ReferenceCount(project) == 2 && uploads == 1, "both motion managers must preserve the same project and one physical texture upload");
	const auto texture = owner.Primary().FindSource(project, "source")->Texture;
	Require(cache.Get(texture).References == 2, "duplicate project registration must not acquire additional source wrapper/GPU references");
	second = {};
	events.clear();
	first = {};
	Require(events.empty() && !std::holds_alternative<std::monostate>(vm.ResolveWeakReference(weak_first)), "the native resource root must retain its first PSBObject after scoped caller values expire");
	owner.Unregister(project);
	Require(owner.ReferenceCount(project) == 1 && events.empty() && cache.Get(texture).References == 2, "intermediate resource unregistration must retain the script owner and both source managers");
	owner.Unregister(project);
	Require(events == std::vector<std::string>{"psb", "gpu"} && owner.Count() == 0 && owner.Primary().Count() == 0 && owner.Secondary().Count() == 0 && weak_asset.expired(), "explicit final release must drop the native script owner before manager cleanup, retaining portable data until both complete");
	Require(std::holds_alternative<std::monostate>(vm.ResolveWeakReference(weak_first)), "final resource root release must expire the script document target");
	RequireFailure([&] {
		owner.Unregister(project);
	},
	    "invalid resource id");
	owner.Close();
	vm.RethrowReleaseFailure();
	vm.Close();
}

static void TestSafetyCloseKeepsRootsThroughGpuDisposal() {
	sgre::script::SquirrelVmLimits vm_limits;
	vm_limits.ReclaimObjects = true;
	sgre::script::SquirrelVm vm(vm_limits);
	vm.InstallRecoveredBaseLibrary();
	sgre::script::ScriptObjectBindings objects(vm);
	sgre::script::ScriptPsbBindings psb(vm, objects);
	std::vector<std::string> events;
	std::array<std::uint32_t, 2> indices{};
	const auto destructor = vm.CreateNativeClosure("ordered PSB destructor fixture", [&](auto&, const auto arguments) {
		const auto index = std::get<sgre::script::SquirrelObjectHandle>(arguments[0]).Index;
		events.push_back(index == indices[0] ? "psb0" : "psb1");

		return sgre::script::SquirrelNativeResult{};
	});
	vm.NewSlot(psb.ObjectClass(), std::string("destructor"), destructor);
	std::uint32_t uploads = 0;
	std::array<sgre::script::SquirrelValue, 2> weak_owners;
	sgre::scene::TextureCache cache([&](auto, const auto&) {
		return sgre::render::SpriteTextureHandle{++uploads, 1, &uploads};
	},
	    [&](const auto texture) {
		    for (const auto& weak : weak_owners) {
			    Require(!std::holds_alternative<std::monostate>(vm.ResolveWeakReference(weak)), "every PSB root must remain live throughout manager disposal during safety close");
		    }
		    events.push_back("gpu" + std::to_string(texture.Index));
	    });
	sgre::scene::MotionAssetCatalog catalog(cache);
	sgre::script::ScriptMotionResourceOwner owner(vm, psb, catalog, cache);
	for (std::size_t index = 0; index < indices.size(); ++index) {
		auto script_owner = psb.WrapDocument(Document(), Backing());
		indices[index] = std::get<sgre::script::SquirrelObjectHandle>(script_owner).Index;
		weak_owners[index] = vm.CreateWeakReference(script_owner);
		const auto project = owner.Register(script_owner);
		Require(project.Value == static_cast<std::int32_t>(index), "paired manager identifiers must preserve native monotonic order");
	}

	owner.Close();
	Require(events == std::vector<std::string>{"gpu1", "gpu2", "psb1", "psb0"}, "safety close must retire managers before descending-key native resource roots");
	owner.Close();
	vm.RethrowReleaseFailure();
	vm.Close();
}

int main() {
	try {
		TestPairedRegistrationAndScriptOwnership();
		TestSafetyCloseKeepsRootsThroughGpuDisposal();
		std::cout << "Script motion resource ownership fixtures passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
