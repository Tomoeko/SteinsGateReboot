#include "sgre/script/image_bindings.hpp"

#include "image_fixture.hpp"

#include <optional>
#include <string_view>

using sgre::script::SquirrelValue;

template <typename Operation>
static void RequireFailure(Operation operation, std::string_view expected) {
	try {
		operation();
	} catch (const std::exception& error) {
		Require(std::string_view(error.what()).find(expected) != std::string_view::npos, error.what());

		return;
	}

	throw std::runtime_error("expected genuine Image failure: " + std::string(expected));
}

static sgre::script::SquirrelVmLimits ReclaimingLimits() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;

	return limits;
}

struct ImageBindingFixture {
	ImageFixture Resources;
	sgre::script::SquirrelVm Vm{ReclaimingLimits()};
	sgre::script::ScriptObjectBindings Objects{Vm};
	sgre::script::ScriptPsbBindings Psb{Vm, Objects};
	sgre::script::ScriptImageBindings Images;

	explicit ImageBindingFixture(std::size_t maximum = 8) : Images(Vm, Objects, Psb, Resources.Catalog, Resources.Cache, maximum) {
	}

	SquirrelValue MakeDocument() {
		return Psb.WrapDocument(Document({Entry("one", 16, 8, {Tile(0, 0, 4, 4, 0), Tile(4, 0, 4, 4, 0), Tile(8, 0, 4, 4, 1)}), Entry("one", 9, 9, {}), Entry("empty", 0, 0, {})}), Resources.Backing);
	}

	SquirrelValue MakeImage(const SquirrelValue& document, std::optional<std::string> label = {}) {
		std::vector<SquirrelValue> arguments{Vm.RootTable(), document};
		if (label) {
			arguments.emplace_back(*label);
		}

		return Vm.Invoke(Images.ImageClass(), arguments).Value;
	}
};

static SquirrelValue Call(sgre::script::SquirrelVm& vm, const SquirrelValue& receiver, std::string_view method, std::initializer_list<SquirrelValue> tail = {}) {
	std::vector<SquirrelValue> arguments{receiver};
	arguments.insert(arguments.end(), tail);

	return vm.Invoke(vm.GetSlot(receiver, std::string(method)), arguments).Value;
}

static void TestGenuineClassSourcesAndMissingLabel() {
	ImageBindingFixture fixture;
	const auto document = fixture.MakeDocument();
	const auto image = fixture.MakeImage(document);
	const auto missing = fixture.MakeImage(document, "ONE");
	const auto empty = fixture.MakeImage(document, "empty");
	Require(fixture.Vm.IsInstanceOf(image, fixture.Objects.ObjectClass()) && fixture.Vm.GetSlot(fixture.Vm.RootTable(), std::string("Image")) == fixture.Images.ImageClass(), "genuine Image derives from Object with its own native class identity");
	Require(Call(fixture.Vm, image, "getWidth") == SquirrelValue{std::int32_t(16)} && Call(fixture.Vm, image, "getHeight") == SquirrelValue{std::int32_t(8)} && Call(fixture.Vm, image, "getValid") == SquirrelValue{true}, "native captured getters publish selected image dimensions/validity");
	Require(Call(fixture.Vm, missing, "getValid") == SquirrelValue{false} && Call(fixture.Vm, missing, "getWidth") == SquirrelValue{std::int32_t(0)} && Call(fixture.Vm, empty, "getValid") == SquirrelValue{true}, "missing case-sensitive label differs from valid zero-source entry");
	const auto borrowed = fixture.Images.BorrowNativeImage(image);
	Require(borrowed.IsLive() && borrowed.Asset()->TileBounds.size() == 3 && fixture.Resources.Cache.Get(borrowed.Source(0).Texture()).References == 2, "native Image owns actual source wrappers and repeated cache references");
	Require(fixture.Vm.RetainedReferenceCount(image) == 0 && fixture.Vm.RetainedReferenceCount(document) == 3, "registry borrows Image instances and retains one actual PSBObject root per native Image");
	Require(fixture.Vm.ResolveWeakReference(Call(fixture.Vm, image, "weakref", {document})) == document, "native weakref observes the original last argument");
	const auto image_constants = fixture.Vm.GetSlot(fixture.Vm.ConstantTable(), std::string("IMAGE"));
	Require(fixture.Vm.GetSlot(image_constants, std::string("GRAYSCALE_METHOD_ITU")) == SquirrelValue{std::int32_t(2)}, "Image registration preserves its recovered constant table");
	RequireFailure([&] {
		static_cast<void>(Call(fixture.Vm, image, "negate"));
	},
	    "exclusive source-copy");
	fixture.Vm.Close();
	fixture.Vm.RethrowReleaseFailure();
	Require(!borrowed.IsLive() && fixture.Resources.Cache.Count() == 0, "explicit Close expires native Image/source borrows while services remain alive");
}

static void TestScriptSourcePsbAndObjectRetirementOrder() {
	ImageBindingFixture fixture;
	std::vector<std::uint32_t> events;
	auto document = fixture.MakeDocument();
	const auto weak_document = fixture.Vm.CreateWeakReference(document);
	const auto listener = fixture.Vm.Invoke(fixture.Objects.ObjectClass(), std::array<SquirrelValue, 1>{fixture.Vm.RootTable()}).Value;
	fixture.Objects.Subscribe(document, listener, [&](const auto&) {
		Require(fixture.Resources.Cache.Count() == 0, "last strong PSBObject root retires after all image sources");
		events.push_back(3);

		return true;
	});
	const auto subclass = fixture.Vm.CreateClass(fixture.Images.ImageClass());
	fixture.Vm.NewSlot(subclass, std::string("destructor"), fixture.Vm.CreateNativeClosure("image script destructor", [&](auto&, auto) {
		events.push_back(1);

		return sgre::script::SquirrelNativeResult::NoValues();
	}));
	auto image = fixture.Vm.Invoke(subclass, std::array<SquirrelValue, 2>{fixture.Vm.RootTable(), document}).Value;
	const auto identity = fixture.Vm.BorrowObjectIdentity(image);
	const auto borrowed = fixture.Images.BorrowNativeImage(image);
	fixture.Objects.Subscribe(image, listener, [&](const auto&) {
		events.push_back(4);

		return true;
	});
	fixture.Resources.OnDrop = [&] {
		Require(!std::holds_alternative<std::monostate>(fixture.Vm.ResolveWeakReference(weak_document)), "actual PSB script root remains live throughout GPU retirement");
		events.push_back(2);
	};
	document = {};
	static_cast<void>(Call(fixture.Vm, image, "getValid"));
	// Retire the fixture caller's temporary owners before measuring its final native Image release.
	fixture.Vm.ResetContext(fixture.Vm.RootContext());
	image = {};
	fixture.Vm.RethrowReleaseFailure();
	if (events != std::vector<std::uint32_t>{1, 2, 2, 3, 4}) {
		std::cerr << "private Image retirement events:";
		for (const auto event : events) {
			std::cerr << ' ' << event;
		}
		std::cerr << '\n';
	}
	Require(events == std::vector<std::uint32_t>{1, 2, 2, 3, 4}, "script destructor precedes ordered sources, retained PSBObject and final Object notification");
	Require(!borrowed.IsLive() && !fixture.Vm.ResolveObjectIdentity(identity) && std::holds_alternative<std::monostate>(fixture.Vm.ResolveWeakReference(weak_document)), "borrowed identities do not keep script/native Image or PSB targets alive");
	fixture.Vm.Close();
	fixture.Vm.RethrowReleaseFailure();
}

static void TestDiagnosticCleanupAndGenerationReuse() {
	ImageBindingFixture fixture(1);
	const auto document = fixture.MakeDocument();
	auto image = fixture.MakeImage(document);
	const auto old_identity = fixture.Vm.BorrowObjectIdentity(image);
	const auto borrowed = fixture.Images.BorrowNativeImage(image);
	fixture.Resources.FailRelease = true;
	RequireFailure([&] {
		fixture.Images.ReleaseInstance(image);
	},
	    "retirement failure");
	Require(!borrowed.IsLive() && fixture.Resources.Cache.Count() == 0 && !fixture.Objects.HasInstance(image) && fixture.Vm.RetainedReferenceCount(document) == 0, "cleanup diagnostic still removes all source, root and native registry ownership");
	RequireFailure([&] {
		static_cast<void>(fixture.Images.BorrowNativeImage(image));
	},
	    "live native identity");
	fixture.Vm.ResetContext(fixture.Vm.RootContext());
	image = {};
	fixture.Vm.RethrowReleaseFailure();
	Require(!fixture.Vm.ResolveObjectIdentity(old_identity), "released script Image owner expires before native registry capacity is reused");
	const auto replacement = fixture.MakeImage(document);
	const auto replacement_identity = fixture.Vm.BorrowObjectIdentity(replacement);
	Require(replacement_identity.Index == old_identity.Index && replacement_identity.Generation != old_identity.Generation && fixture.Images.BorrowNativeImage(replacement).IsLive(), "replacement Image reuses the retired arena slot with a fresh generation and released one-wrapper capacity");
	fixture.Vm.Close();
	fixture.Vm.RethrowReleaseFailure();
}

static void TestNotificationPreflightAndCloseAfterClassFinalize() {
	ImageBindingFixture fixture;
	const auto document = fixture.MakeDocument();
	const auto subclass = fixture.Vm.CreateClass(fixture.Images.ImageClass());
	const auto image = fixture.Vm.Invoke(subclass, std::array<SquirrelValue, 2>{fixture.Vm.RootTable(), document}).Value;
	const auto borrowed = fixture.Images.BorrowNativeImage(image);
	const auto listener = fixture.Vm.Invoke(fixture.Objects.ObjectClass(), std::array<SquirrelValue, 1>{fixture.Vm.RootTable()}).Value;
	fixture.Objects.Subscribe(image, listener, [&](const auto&) {
		RequireFailure([&] {
			fixture.Images.ReleaseInstance(image);
		},
		    "completion notification");
		Require(borrowed.IsLive() && fixture.Resources.Cache.Count() == 2, "rejected notification release preserves original sources and payload");

		return true;
	});
	fixture.Objects.Notify(image);
	fixture.Vm.Close();
	fixture.Vm.RethrowReleaseFailure();
	Require(!borrowed.IsLive() && fixture.Resources.Cache.Count() == 0, "registered native identity remains sufficient for cleanup after Class ancestry retirement");
}

int main() {
	try {
		TestGenuineClassSourcesAndMissingLabel();
		TestScriptSourcePsbAndObjectRetirementOrder();
		TestDiagnosticCleanupAndGenerationReuse();
		TestNotificationPreflightAndCloseAfterClassFinalize();
		std::cout << "Image binding tests passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
