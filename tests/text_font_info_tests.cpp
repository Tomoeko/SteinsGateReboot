#include "sgre/script/text_font_info.hpp"

#include "font_info_fixture.hpp"
#include "vector_font_fixture.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

static void RequireFailure(const std::function<void()>& operation, std::string_view context) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(context) != std::string_view::npos, error.what());

		return;
	}

	throw std::runtime_error("FontInfo fixture expected failure");
}

static sgre::script::SquirrelValue Construct(sgre::script::SquirrelVm& vm, const sgre::script::SquirrelValue& class_value, std::vector<sgre::script::SquirrelValue> arguments = {}) {
	arguments.insert(arguments.begin(), vm.RootTable());

	return vm.Invoke(class_value, arguments).Value;
}

static void Complete(sgre::script::SquirrelVm& vm, const sgre::script::SquirrelValue& source) {
	const std::array<sgre::script::SquirrelValue, 1> receiver{source};
	Require(!std::get<bool>(vm.Invoke(vm.GetSlot(source, std::string("getLoading")), receiver).Value), "FontInfo source did not complete");
}

static void TestActualBitmapInterfaceSizeFilteringAndRootOrder() {
	sgre::script::SquirrelVm vm;
	sgre::script::ScriptObjectBindings objects(vm);
	sgre::script::ScriptPsbBindings psb(vm, objects);
	sgre::script::ScriptVectorFontBindings vectors(vm, objects, [](auto, auto) {
		return []() -> std::optional<sgre::script::ScriptResourceAsset> {
			return {};
		};
	},
	    [](auto, auto) {
	    });
	const auto source = psb.WrapDocument(font_info_test::Document(), font_info_test::Backing());
	std::uint32_t uploads = 0;
	std::size_t releases = 0;
	sgre::scene::TextureCache cache([&](auto, const auto&) {
		return sgre::render::SpriteTextureHandle{++uploads, 1, &uploads};
	},
	    [&](auto) {
		    Require(vm.RetainedReferenceCount(source) >= 1, "FontInfo source root must survive private bitmap/GPU interface retirement");
		    ++releases;
	    });
	sgre::scene::BitmapFontAssetCatalog catalog(cache);
	sgre::text::TextFontSelectionKey key;
	key.RequestedSize = 24;
	key.FaceIndex = 7;
	key.Style = sgre::text::TextFontStyle::Bold;
	sgre::script::ScriptTextFontInfo first(vm, objects, psb, vectors, catalog, cache, source, key);
	sgre::script::ScriptTextFontInfo second(vm, objects, psb, vectors, catalog, cache, source, key);
	Require(first.Kind() == sgre::script::TextFontProviderKind::Bitmap && first.Source() == source && first.Key().RequestedSize == 24 && first.Key().Style == sgre::text::TextFontStyle::Bold && vm.RetainedReferenceCount(source) == 2, "FontInfo must retain its actual supplied PSBObject and immutable request key");
	Require(first.BitmapScale().Width == 1 && first.BitmapScale().Height == 1 && first.BitmapProvider() != second.BitmapProvider() && uploads == 2, "FontInfo construction must preserve fresh scale-one interfaces with shared physical sources");
	const auto first_wrapper = first.BitmapProvider()->Source(0);
	const auto second_wrapper = second.BitmapProvider()->Source(0);
	Require(!first_wrapper.SameIdentity(second_wrapper) && first_wrapper.Texture() == second_wrapper.Texture(), "FontInfo source wrappers must remain provider-scoped");
	first.SetSize(24);
	const auto metrics = first.MeasureBitmapCodeUnit(u'A');
	Require(first.BitmapScale().Width == 4 && first.BitmapScale().Height == 4 && metrics.Width == 20 && metrics.Height == 24 && second.BitmapScale().Width == 1, "native PSB interface size divides requested size by constructor-cached maximum height");
	Require(first.MeasureBitmapCodeUnit(u'Z').Width == 0 && first.MeasureBitmapCodeUnit(u'Z').Height == 0, "native missing bitmap glyph must preserve zero measurement");
	first.SetFiltering(true);
	Require(first.BitmapProvider()->Minification(0) == 1 && first.BitmapProvider()->Magnification(1) == 1 && second.BitmapProvider()->Minification(0) == 0, "native MyPSBFont byte filtering must update both words without rewriting sibling local state");
	first.Close();
	Require(vm.RetainedReferenceCount(source) == 1 && !first_wrapper.IsLive() && second_wrapper.IsLive() && releases == 0, "intermediate FontInfo retirement must release only its private interface and root");
	second.Close();
	Require(vm.RetainedReferenceCount(source) == 0 && !second_wrapper.IsLive() && cache.Count() == 0 && releases == 2, "last FontInfo interface must retire before its supplied root");
	RequireFailure([&] {
		static_cast<void>(first.Source());
	},
	    "retired");
	vm.Close();
}

static void TestActualVectorBorrowAndIndependentInterfaceSizes() {
	sgre::script::SquirrelVm vm;
	sgre::script::ScriptObjectBindings objects(vm);
	sgre::script::ScriptPsbBindings psb(vm, objects);
	bool completed = false;
	const auto bytes = std::make_shared<const std::vector<std::uint8_t>>(vector_font_test::MakeFont());
	sgre::script::ScriptVectorFontBindings vectors(vm, objects, [&, bytes](auto, auto) {
		return [&, bytes]() -> std::optional<sgre::script::ScriptResourceAsset> {
			if (!completed) {
				return {};
			}

			sgre::script::ScriptResourceAsset asset;
			asset.Kind = sgre::script::ScriptResourceDataKind::Binary;
			asset.Backing = bytes;

			return asset;
		};
	},
	    [](auto, auto) {
	    });
	sgre::scene::TextureCache cache([](auto, const auto&) -> sgre::render::SpriteTextureHandle {
		throw std::runtime_error("vector FontInfo must not allocate a bitmap source texture");
	},
	    [](auto) {
	    });
	sgre::scene::BitmapFontAssetCatalog catalog(cache);
	const auto source = Construct(vm, vectors.VectorFontClass(), {std::string("font.otf")});
	sgre::script::ScriptTextFontInfo unavailable(vm, objects, psb, vectors, catalog, cache, source, {});
	Require(unavailable.Kind() == sgre::script::TextFontProviderKind::Unavailable && vm.RetainedReferenceCount(source) == 1 && !vectors.State(source).ProviderPrepared, "pending FontInfo source must remain rooted with no private interface or premature preparation");
	completed = true;
	Complete(vm, source);
	sgre::script::ScriptTextFontInfo first(vm, objects, psb, vectors, catalog, cache, source, {});
	sgre::script::ScriptTextFontInfo second(vm, objects, psb, vectors, catalog, cache, source, {});
	Require(first.Kind() == sgre::script::TextFontProviderKind::Vector && second.Kind() == sgre::script::TextFontProviderKind::Vector && unavailable.Kind() == sgre::script::TextFontProviderKind::Unavailable && vm.RetainedReferenceCount(source) == 3, "FontInfo must borrow one loaded core without retroactively replacing an unavailable interface");
	Require(first.VectorSizeWord() == 0xffffffff && vectors.Provider(source).SizeState().Word == 0xffffffff, "TextRender interface and core must begin with separate native size sentinels");
	first.SetSize(24.9f);
	second.SetSize(12);
	Require(first.VectorSizeWord() == 24 && second.VectorSizeWord() == 12 && vectors.Provider(source).SizeState().Word == 0xffffffff, "interface selection must not update its shared core until actual glyph preparation");
	Require(first.PrepareVectorGlyph(0).PixelSize == 24 && vectors.Provider(source).SizeState().Word == 24 && second.PrepareVectorGlyph(0).PixelSize == 12 && first.PrepareVectorGlyph(0).PixelSize == 24, "separate TextRender interfaces must reapply their requested size before using the same borrowed core");
	first.SetFiltering(true);
	Require(cache.Count() == 0, "vector FontInterface filtering is the native no-op");
	first.SetSize(4294967296.0f);
	Require(first.VectorSizeWord() == 0 && first.PrepareVectorGlyph(0).PixelSize == 1, "interface lowword and shared-core float-rounding/minimum conversion differs");
	first.Close();
	second.Close();
	unavailable.Close();
	Require(vm.RetainedReferenceCount(source) == 0 && objects.HasInstance(source), "retiring TextRender interfaces must drop roots without deleting a still-retained script VectorFont owner");
	const auto weak_source = vm.CreateWeakReference(source);
	sgre::script::ScriptTextFontInfo weak(vm, objects, psb, vectors, catalog, cache, weak_source, {});
	weak.SetSize(18);
	Require(weak.Kind() == sgre::script::TextFontProviderKind::Vector && weak.Source() == weak_source && weak.PrepareVectorGlyph(0).PixelSize == 18 && vm.RetainedReferenceCount(source) == 0, "live weak VectorFont result must borrow its actual core without retaining the target");
	vectors.ReleaseInstance(source);
	RequireFailure([&] {
		static_cast<void>(weak.Kind());
	},
	    "retired during");
	weak.Close();
	vm.Close();
}

static void TestUnavailableSourcesAndFailureCompletingClose() {
	sgre::script::SquirrelVm vm;
	sgre::script::ScriptObjectBindings objects(vm);
	sgre::script::ScriptPsbBindings psb(vm, objects);
	sgre::script::ScriptVectorFontBindings vectors(vm, objects, [](auto, auto) {
		return []() -> std::optional<sgre::script::ScriptResourceAsset> {
			return {};
		};
	},
	    [](auto, auto) {
	    });
	std::uint32_t uploads = 0;
	std::uint32_t releases = 0;
	sgre::scene::TextureCache cache([&](auto, const auto&) {
		return sgre::render::SpriteTextureHandle{++uploads, 1, &uploads};
	},
	    [&](auto) {
		    ++releases;
		    throw std::runtime_error("FontInfo release fixture");
	    });
	sgre::scene::BitmapFontAssetCatalog catalog(cache);
	const auto ordinary = Construct(vm, objects.ObjectClass());
	sgre::script::ScriptTextFontInfo unknown(vm, objects, psb, vectors, catalog, cache, ordinary, {});
	Require(unknown.Kind() == sgre::script::TextFontProviderKind::Unavailable && unknown.Source() == ordinary && vm.RetainedReferenceCount(ordinary) == 1, "unrecognized successful findFont results must still retain their actual supplied root");
	unknown.SetSize(24);
	unknown.SetFiltering(true);
	Require(uploads == 0, "unavailable FontInfo must not synthesize a fallback provider");
	unknown.Close();
	sgre::script::ScriptTextFontInfo primitive(vm, objects, psb, vectors, catalog, cache, std::int32_t{17}, {});
	Require(primitive.Kind() == sgre::script::TextFontProviderKind::Unavailable && std::get<std::int32_t>(primitive.Source()) == 17, "native primitive result identity must be retained without inventing an interface");
	primitive.Close();
	const auto source = psb.WrapDocument(font_info_test::Document(), font_info_test::Backing());
	sgre::script::ScriptTextFontInfo bitmap(vm, objects, psb, vectors, catalog, cache, source, {});
	RequireFailure([&] {
		bitmap.Close();
	},
	    "FontInfo release fixture");
	Require(releases == 2 && cache.Count() == 0 && vm.RetainedReferenceCount(source) == 0, "failed private interface retirement must complete all source/root cleanup before reporting its first error");
	bitmap.Close();
	vm.Close();
}

static void TestScopeRollbackAndRetiredBorrowGuards() {
	sgre::script::SquirrelVm vm;
	sgre::script::ScriptObjectBindings objects(vm);
	sgre::script::ScriptPsbBindings psb(vm, objects);
	const auto font_bytes = std::make_shared<const std::vector<std::uint8_t>>(vector_font_test::MakeFont());
	sgre::script::ScriptVectorFontBindings vectors(vm, objects, [font_bytes](auto, auto) {
		return [font_bytes]() -> std::optional<sgre::script::ScriptResourceAsset> {
			sgre::script::ScriptResourceAsset asset;
			asset.Kind = sgre::script::ScriptResourceDataKind::Binary;
			asset.Backing = font_bytes;

			return asset;
		};
	},
	    [](auto, auto) {
	    });
	const auto bitmap_source = psb.WrapDocument(font_info_test::Document(), font_info_test::Backing());
	std::uint32_t uploads = 0;
	std::size_t releases = 0;
	sgre::scene::TextureCache failing_cache([&](auto, const auto&) {
		++uploads;
		if (uploads == 2) {
			throw std::runtime_error("FontInfo upload rollback fixture");
		}

		return sgre::render::SpriteTextureHandle{uploads, 1, &uploads};
	},
	    [&](auto) {
		    Require(vm.RetainedReferenceCount(bitmap_source) == 1, "failed FontInfo construction must retire its partial interface before its source root");
		    ++releases;
	    });
	sgre::scene::BitmapFontAssetCatalog failing_catalog(failing_cache);
	RequireFailure([&] {
		sgre::script::ScriptTextFontInfo owner(vm, objects, psb, vectors, failing_catalog, failing_cache, bitmap_source, {});
	},
	    "upload rollback fixture");
	Require(uploads == 2 && releases == 1 && failing_cache.Count() == 0 && vm.RetainedReferenceCount(bitmap_source) == 0, "failed FontInfo construction retained partial source textures or its supplied VM root");

	sgre::scene::TextureCache cache([](auto, const auto&) -> sgre::render::SpriteTextureHandle {
		throw std::runtime_error("vector FontInfo guard must not request a texture");
	},
	    [](auto) {
	    });
	sgre::scene::BitmapFontAssetCatalog catalog(cache);
	RequireFailure([&] {
		sgre::script::ScriptTextFontInfo owner(vm, objects, psb, vectors, failing_catalog, cache, bitmap_source, {});
	},
	    "one bitmap font catalog and texture cache");
	Require(vm.RetainedReferenceCount(bitmap_source) == 0, "FontInfo cache-scope failure retained its supplied root");
	const auto vector_source = Construct(vm, vectors.VectorFontClass(), {std::string("font.otf")});
	Complete(vm, vector_source);
	sgre::script::ScriptTextFontInfo retired(vm, objects, psb, vectors, catalog, cache, vector_source, {});
	retired.SetSize(24);
	vectors.ReleaseInstance(vector_source);
	Require(vm.RetainedReferenceCount(vector_source) == 1, "explicit source retirement must not silently release the consumer root");
	RequireFailure([&] {
		static_cast<void>(retired.PrepareVectorGlyph(0));
	},
	    "explicitly retired");
	retired.Close();
	Require(vm.RetainedReferenceCount(vector_source) == 0, "retired provider borrow retained its source root after Close");

	const auto second_source = Construct(vm, vectors.VectorFontClass(), {std::string("second.otf")});
	Complete(vm, second_source);
	sgre::script::ScriptTextFontInfo closed_vm(vm, objects, psb, vectors, catalog, cache, second_source, {});
	vm.Close();
	RequireFailure([&] {
		closed_vm.SetSize(12);
	},
	    "VM is closed");
	closed_vm.Close();
}

static void TestActualWeakSourceResolutionWithoutStrongRoot() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	sgre::script::SquirrelVm vm(limits);
	sgre::script::ScriptObjectBindings objects(vm);
	sgre::script::ScriptPsbBindings psb(vm, objects);
	sgre::script::ScriptVectorFontBindings vectors(vm, objects, [](auto, auto) {
		return []() -> std::optional<sgre::script::ScriptResourceAsset> {
			return {};
		};
	},
	    [](auto, auto) {
	    });
	std::uint32_t uploads = 0;
	sgre::scene::TextureCache cache([&](auto, const auto&) {
		return sgre::render::SpriteTextureHandle{++uploads, 1, &uploads};
	},
	    [](auto) {
	    });
	sgre::scene::BitmapFontAssetCatalog catalog(cache);
	auto source = psb.WrapDocument(font_info_test::Document(), font_info_test::Backing());
	const auto weak_source = vm.CreateWeakReference(source);
	sgre::script::ScriptTextFontInfo info(vm, objects, psb, vectors, catalog, cache, weak_source, {});
	Require(info.Kind() == sgre::script::TextFontProviderKind::Bitmap && info.Source() == weak_source && vm.RetainedReferenceCount(source) == 0 && uploads == 2, "native weak result must remain weakly rooted while its live target supplies the actual bitmap interface");
	info.SetSize(24);
	Require(info.MeasureBitmapCodeUnit(u'A').Height == 24, "resolved weak bitmap target must use the same fresh native provider contract");
	source = {};
	vm.RethrowReleaseFailure();
	Require(vm.Type(vm.ResolveWeakReference(weak_source)) == sgre::script::SquirrelValueType::Null, "FontInfo must not promote a supplied weak root into ownership of the target");
	RequireFailure([&] {
		static_cast<void>(info.Kind());
	},
	    "retired during");
	info.Close();
	Require(cache.Count() == 0, "retiring expired weak-source interface must still complete owned texture cleanup");
	sgre::script::ScriptTextFontInfo expired(vm, objects, psb, vectors, catalog, cache, weak_source, {});
	Require(expired.Kind() == sgre::script::TextFontProviderKind::Unavailable && expired.Source() == weak_source, "an already-expired weak result must be cached without a fabricated provider");
	expired.Close();
	vm.Close();
}

int main() {
	try {
		TestActualBitmapInterfaceSizeFilteringAndRootOrder();
		TestActualWeakSourceResolutionWithoutStrongRoot();
		TestActualVectorBorrowAndIndependentInterfaceSizes();
		TestUnavailableSourcesAndFailureCompletingClose();
		TestScopeRollbackAndRetiredBorrowGuards();
		std::cout << "FontInfo supplied roots, private interfaces, source availability and size/filter phases passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
