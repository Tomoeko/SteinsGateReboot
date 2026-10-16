#include "sgre/script/text_font_owner.hpp"

#include "font_info_fixture.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

template <typename Operation>
static void RequireFailure(Operation operation, std::string_view message) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(message) != std::string_view::npos, error.what());

		return;
	}

	throw std::runtime_error("actual text font owner fixture expected failure");
}

static sgre::text::TextFontSelectionKey Key(float size, std::uint32_t face, sgre::text::TextFontStyle style = sgre::text::TextFontStyle::Regular) {
	sgre::text::TextFontSelectionKey key;
	key.RequestedSize = size;
	key.FaceIndex = face;
	key.Style = style;

	return key;
}

struct Fixture {
	sgre::script::SquirrelVm Vm;
	sgre::script::ScriptObjectBindings Objects{Vm};
	sgre::script::ScriptPsbBindings Psb{Vm, Objects};
	sgre::script::ScriptVectorFontBindings Vectors{Vm, Objects, [](auto, auto) {
		                                               return []() -> std::optional<sgre::script::ScriptResourceAsset> {
			                                               return {};
		                                               };
	                                               },
	    [](auto, auto) {
	    }};
	sgre::script::SquirrelValue Source = Psb.WrapDocument(font_info_test::Document(), font_info_test::Backing());
	std::uint32_t Uploads = 0;
	std::vector<std::size_t> RootCountsOnRelease;
	sgre::scene::TextureCache Textures{[this](auto, const auto&) {
		                                   return sgre::render::SpriteTextureHandle{++Uploads, 1, &Uploads};
	                                   },
	    [this](auto) {
		    RootCountsOnRelease.push_back(Vm.RetainedReferenceCount(Source));
	    }};
	sgre::scene::BitmapFontAssetCatalog Fonts{Textures};
	sgre::script::SquirrelHostRoot CachedSelf;
	std::unique_ptr<sgre::script::ScriptTextFontOwner> Owner;

	explicit Fixture(sgre::script::SquirrelVmLimits limits = {}) : Vm(limits) {
	}

	~Fixture() {
		Owner.reset();
		CachedSelf = {};
		Vm.Close();
	}

	sgre::script::SquirrelValue Prepare(const sgre::script::SquirrelValue& class_value, std::size_t max_fonts = 65536) {
		const auto receiver = Vm.CreateInstance(class_value);
		Objects.AttachInstance(receiver);
		CachedSelf = Vm.Retain(Vm.CreateWeakReference(receiver));
		Owner = std::make_unique<sgre::script::ScriptTextFontOwner>(Vm, Objects, Psb, Vectors, Fonts, Textures, CachedSelf, max_fonts);

		return receiver;
	}
};

static void TestActualNormalRubyCallbackAndPrivateInterfaces() {
	Fixture fixture;
	auto& vm = fixture.Vm;
	const auto class_value = vm.CreateClass(fixture.Objects.ObjectClass());
	std::size_t calls = 0;
	sgre::script::SquirrelValue receiver;
	vm.NewSlot(class_value, std::string("findFont"), vm.CreateNativeClosure("actual_find_font", [&](auto&, std::span<const sgre::script::SquirrelValue> arguments) {
		Require(arguments.size() == 4 && arguments[0] == receiver, "findFont must receive the actual cached weak-self receiver and three supplied arguments");
		Require(std::get<float>(arguments[1]) == 24 && sgre::script::StringText(arguments[2]) == "face", "native requested size and terminated face string must reach the real callback");
		Require(std::get<std::int32_t>(arguments[3]) == (calls == 0 ? 1 : 0), "normal style must be passed exactly and ruby style must use its separate zero key");
		++calls;

		return sgre::script::SquirrelNativeResult::ReturnValue(fixture.Source);
	}));
	receiver = fixture.Prepare(class_value);
	const auto first = fixture.Owner->Select(sgre::text::TextFontFamily::Normal, Key(24, 7, sgre::text::TextFontStyle::Bold), std::string("face\0ignored", 12), false);
	Require(first.CallbackInvoked && !first.CallbackFailed && first.Font && first.Font->Kind() == sgre::script::TextFontProviderKind::Bitmap && fixture.Owner->Current(sgre::text::TextFontFamily::Normal) == 0, "successful real normal findFont must own and select its private FontInfo");
	Require(first.Font->BitmapScale().Height == 4 && vm.RetainedReferenceCount(fixture.Source) == 1 && vm.RetainedReferenceCount(receiver) == 0, "size assignment follows private provider creation while weak self does not retain the wrapper");
	const auto hit = fixture.Owner->Select(sgre::text::TextFontFamily::Normal, Key(24, 7), "different name with same intern identity", true);
	Require(!hit.CallbackInvoked && hit.Font == first.Font && calls == 1 && hit.Font->BitmapProvider()->Minification(0) == 1, "normal regular wildcard hit must reuse current interface then set size/filter without calling findFont");
	const auto ruby = fixture.Owner->Select(sgre::text::TextFontFamily::Ruby, Key(24, 7, sgre::text::TextFontStyle::Italic), "face", false);
	Require(ruby.CallbackInvoked && ruby.Font != first.Font && ruby.Font->BitmapProvider() != first.Font->BitmapProvider() && ruby.Font->Key().Style == sgre::text::TextFontStyle::Regular && calls == 2, "ruby must own a distinct fresh interface even when callback returns the same source");
	Require(vm.RetainedReferenceCount(fixture.Source) == 2 && fixture.Uploads == 2, "both families retain the same actual source independently while sharing physical source textures");
	fixture.Owner->Clear();
	Require(fixture.Owner->Count(sgre::text::TextFontFamily::Normal) == 0 && fixture.Owner->Count(sgre::text::TextFontFamily::Ruby) == 0 && fixture.Textures.Count() == 0 && vm.RetainedReferenceCount(fixture.Source) == 0, "normal then ruby clear must retire every private provider and source root");
	Require(fixture.RootCountsOnRelease == std::vector<std::size_t>{1, 1}, "last provider GPU retirement must precede releasing its source root");
}

static void TestCallbackFailureAndPreviousCurrentVisibility() {
	Fixture fixture;
	auto& vm = fixture.Vm;
	const auto class_value = vm.CreateClass(fixture.Objects.ObjectClass());
	std::size_t calls = 0;
	vm.NewSlot(class_value, std::string("findFont"), vm.CreateNativeClosure("actual_find_font_failure", [&](auto&, auto arguments) {
		if (++calls == 2) {
			Require(fixture.Owner->Current(sgre::text::TextFontFamily::Normal) == 0 && fixture.Owner->Count(sgre::text::TextFontFamily::Normal) == 1, "native callback must observe the previous current interface until its result commits");
			Require(std::get<float>(arguments[1]) == 12, "real miss callback received the wrong size");
			throw std::runtime_error("actual callback error" + std::string(5000, 'x'));
		}

		return sgre::script::SquirrelNativeResult::ReturnValue(fixture.Source);
	}));
	const auto receiver = fixture.Prepare(class_value);
	const auto first = fixture.Owner->Select(sgre::text::TextFontFamily::Normal, Key(24, 7), "face", false);
	const auto failed = fixture.Owner->Select(sgre::text::TextFontFamily::Normal, Key(12, 7), "face", false);
	Require(failed.CallbackInvoked && failed.CallbackFailed && !failed.Font && failed.Diagnostic.find("actual callback error") != std::string::npos && failed.Diagnostic.size() == 4096 && !fixture.Owner->Current(sgre::text::TextFontFamily::Normal), "failed findFont must clear only current and preserve its bounded diagnostic");
	Require(fixture.Owner->Count(sgre::text::TextFontFamily::Normal) == 1 && vm.RetainedReferenceCount(fixture.Source) == 1, "failure must preserve earlier retained interfaces without inventing fallback");
	const auto retained = fixture.Owner->Select(sgre::text::TextFontFamily::Normal, Key(24, 7), "face", true);
	Require(retained.Font == first.Font && !retained.CallbackInvoked && calls == 2, "later compatible request must find an earlier retained entry after failure cleared current");
	fixture.Owner->Close();
	RequireFailure([&] {
		static_cast<void>(fixture.Owner->Count(sgre::text::TextFontFamily::Normal));
	},
	    "retired");
	static_cast<void>(receiver);
}

static void TestGeneralMemberLookupAndWeakCallable() {
	Fixture fixture;
	auto& vm = fixture.Vm;
	const auto class_value = vm.CreateClass(fixture.Objects.ObjectClass());
	std::size_t getters = 0;
	std::size_t calls = 0;
	const auto callable = vm.CreateNativeClosure("actual_dynamic_find_font", [&](auto&, auto arguments) {
		Require(arguments.size() == 4 && std::get<std::int32_t>(arguments[3]) == 0, "dynamic genuine font member received a malformed call");
		++calls;

		return sgre::script::SquirrelNativeResult::ReturnValue(std::int32_t{17});
	});
	const auto weak_callable = vm.CreateWeakReference(callable);
	vm.NewSlot(class_value, std::string("_get"), vm.CreateNativeClosure("actual_font_member_lookup", [&](auto&, auto arguments) {
		Require(sgre::script::StringText(arguments[1]) == "findFont", "native general get must request its exact member name");
		++getters;

		return sgre::script::SquirrelNativeResult::ReturnValue(weak_callable);
	}));
	const auto receiver = fixture.Prepare(class_value);
	Require(!vm.HasSlot(receiver, std::string("findFont")), "fixture must exercise actual _get rather than an ordinary slot");
	const auto selected = fixture.Owner->Select(sgre::text::TextFontFamily::Ruby, Key(12, 9), "ruby", false);
	Require(selected.Font && selected.Font->Kind() == sgre::script::TextFontProviderKind::Unavailable && std::get<std::int32_t>(selected.Font->Source()) == 17 && getters == 1 && calls == 1, "actual _get plus weak callable must retain even an unavailable successful result");
	Require(fixture.Uploads == 0, "successful primitive font result must not fabricate a provider");
}

static void TestMissingCallbackDoesNotRetryRootOrRetainWrapper() {
	Fixture fixture;
	auto& vm = fixture.Vm;
	const auto class_value = vm.CreateClass(fixture.Objects.ObjectClass());
	std::size_t root_calls = 0;
	vm.NewSlot(vm.RootTable(), std::string("findFont"), vm.CreateNativeClosure("unrelated_root_font", [&](auto&, auto) {
		++root_calls;

		return sgre::script::SquirrelNativeResult::ReturnValue(fixture.Source);
	}));
	const auto receiver = fixture.Prepare(class_value);
	const auto failed = fixture.Owner->Select(sgre::text::TextFontFamily::Normal, Key(24, 0), "face", false);
	Require(failed.CallbackFailed && !failed.CallbackInvoked && !failed.Font && root_calls == 0 && fixture.Owner->Count(sgre::text::TextFontFamily::Normal) == 0, "native member lookup must not retry the VM root or invent a font on failure");
	static_cast<void>(receiver);
}

static void TestExpiredCachedSelfAndNonCallableMember() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	Fixture fixture(limits);
	auto& vm = fixture.Vm;
	const auto class_value = vm.CreateClass(fixture.Objects.ObjectClass());
	vm.NewSlot(class_value, std::string("findFont"), std::int32_t{19});
	auto receiver = fixture.Prepare(class_value);
	const auto unavailable = fixture.Owner->Select(sgre::text::TextFontFamily::Normal, Key(24, 0), "face", false);
	Require(unavailable.CallbackFailed && !unavailable.CallbackInvoked && unavailable.Diagnostic.find("not a callable") != std::string::npos, "non-callable actual member must fail without invoking a fabricated font");
	receiver = {};
	vm.RethrowReleaseFailure();
	Require(vm.Type(vm.ResolveWeakReference(fixture.CachedSelf.Value())) == sgre::script::SquirrelValueType::Null, "private font owner must borrow its real task's weak self without retaining the wrapper");
	const auto expired = fixture.Owner->Select(sgre::text::TextFontFamily::Normal, Key(12, 0), "face", false);
	Require(expired.CallbackFailed && !expired.CallbackInvoked && expired.Diagnostic.find("weak self has expired") != std::string::npos && fixture.Owner->Count(sgre::text::TextFontFamily::Normal) == 0, "expired native receiver must remain unavailable without invoking findFont");
}

static void TestSuccessfulWeakProviderResultRemainsWeak() {
	Fixture fixture;
	auto& vm = fixture.Vm;
	const auto weak_source = vm.CreateWeakReference(fixture.Source);
	const auto class_value = vm.CreateClass(fixture.Objects.ObjectClass());
	vm.NewSlot(class_value, std::string("findFont"), vm.CreateNativeClosure("actual_weak_font_result", [&](auto&, auto) {
		return sgre::script::SquirrelNativeResult::ReturnValue(weak_source);
	}));
	const auto receiver = fixture.Prepare(class_value);
	const auto selected = fixture.Owner->Select(sgre::text::TextFontFamily::Normal, Key(24, 7), "face", false);
	Require(selected.Font && selected.Font->Kind() == sgre::script::TextFontProviderKind::Bitmap && selected.Font->Source() == weak_source && vm.RetainedReferenceCount(fixture.Source) == 0, "actual weak callback result must resolve its live provider without promoting the supplied root");
	fixture.Owner->Close();
	Require(fixture.Textures.Count() == 0, "actual weak-result private interface must release its physical source textures");
	static_cast<void>(receiver);
}

static void TestBoundedAndReentrantOwnerMutation() {
	Fixture fixture;
	auto& vm = fixture.Vm;
	const auto class_value = vm.CreateClass(fixture.Objects.ObjectClass());
	std::size_t calls = 0;
	vm.NewSlot(class_value, std::string("findFont"), vm.CreateNativeClosure("actual_reentrant_font", [&](auto&, auto) {
		++calls;
		RequireFailure([&] {
			fixture.Owner->Clear();
		},
		    "real findFont callback");
		RequireFailure([&] {
			fixture.Owner->Close();
		},
		    "real findFont callback");
		RequireFailure([&] {
			static_cast<void>(fixture.Owner->Select(sgre::text::TextFontFamily::Ruby, Key(12, 0), "ruby", false));
		},
		    "real findFont callback");

		return sgre::script::SquirrelNativeResult::ReturnValue(fixture.Source);
	}));
	const auto receiver = fixture.Prepare(class_value, 1);
	Require(fixture.Owner->Select(sgre::text::TextFontFamily::Normal, Key(24, 0), "face", false).Font != nullptr && calls == 1, "guarded callback must still be able to complete its actual font request");
	RequireFailure([&] {
		static_cast<void>(fixture.Owner->Select(sgre::text::TextFontFamily::Normal, Key(12, 0), "face", false));
	},
	    "bounded retained entries");
	Require(calls == 1 && fixture.Owner->Current(sgre::text::TextFontFamily::Normal) == 0, "safety limit must reject before calling or changing a valid retained current interface");
	static_cast<void>(receiver);
}

int main() {
	try {
		TestActualNormalRubyCallbackAndPrivateInterfaces();
		TestCallbackFailureAndPreviousCurrentVisibility();
		TestGeneralMemberLookupAndWeakCallable();
		TestMissingCallbackDoesNotRetryRootOrRetainWrapper();
		TestExpiredCachedSelfAndNonCallableMember();
		TestSuccessfulWeakProviderResultRemainsWeak();
		TestBoundedAndReentrantOwnerMutation();
		std::cout << "Actual TextRender findFont member call, normal/ruby interface ownership and failure retention passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
