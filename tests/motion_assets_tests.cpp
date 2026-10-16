#include "sgre/scene/motion_assets.hpp"
#include "sgre/scene/motion_resources.hpp"

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

	throw std::runtime_error("expected loaded motion asset failure was not reported");
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

static void TestLoadedIdentityAndWeakBookkeeping() {
	std::uint32_t uploads = 0;
	sgre::scene::TextureCache cache([&](auto, const auto&) {
		return sgre::render::SpriteTextureHandle{++uploads, 1, &uploads};
	},
	    [](auto) {
	    });
	sgre::scene::MotionAssetCatalog catalog(cache);
	auto document = Document();
	auto backing = Backing();
	auto first = catalog.Prepare(document, backing);
	const auto identity = first->Identity;
	Require(identity.DocumentInstance != 0 && identity.RootOffset == 44 && first->Document == document && first->Backing == backing, "prepared motion owner must retain its exact immutable document, bytes and root offset");
	Require(first->Model->Sources.size() == 1 && first->Model->Sources[0].Texture.SerializedValueOffset == 64 && first->Aliases.size() == 1 && first->Aliases[0].Key == "alias", "motion preparation must retain texture atom identity and decoded alias metadata");
	Require(catalog.Prepare(document, {}) == first && catalog.DocumentCount() == 1 && uploads == 0, "one live loaded document must reuse its prepared owner without upload or redecoding");
	std::weak_ptr<const sgre::scene::LoadedMotionAsset> weak_asset = first;
	first.reset();
	Require(weak_asset.expired() && catalog.DocumentCount() == 1, "catalog bookkeeping must not extend a prepared owner's lifetime");
	auto recreated = catalog.Prepare(document, backing);
	Require(recreated->Identity == identity, "repreparing a still-loaded document must preserve its document identity");
	auto equal_document = std::make_shared<sgre::resource::PsbDocument>(*document);
	auto distinct = catalog.Prepare(equal_document, backing);
	Require(distinct->Identity.DocumentInstance != identity.DocumentInstance, "equal bytes/models in separate loaded documents must retain distinct native identities");
	std::weak_ptr<const sgre::resource::PsbDocument> weak_document = document;
	std::weak_ptr<const std::vector<std::uint8_t>> weak_backing = backing;
	document.reset();
	backing.reset();
	equal_document.reset();
	Require(!weak_document.expired() && !weak_backing.expired(), "loaded owners must preserve document and backing across caller release");
	recreated.reset();
	distinct.reset();
	Require(weak_document.expired() && weak_backing.expired() && catalog.DocumentCount() == 0, "expired weak catalog records must retain neither models nor private backing");
	catalog.Prune();
	const auto next = catalog.Prepare(Document(), Backing());
	Require(next->Identity.DocumentInstance > identity.DocumentInstance, "expired loaded document IDs must never be recycled");
}

static void TestPreparedOwnerAndManagers() {
	std::uint32_t uploads = 0;
	std::vector<std::uint32_t> retired;
	sgre::scene::TextureCache cache([&](auto, const auto& image) {
		Require(image.Pixels == std::vector<std::uint8_t>{0, 0, 0, 128}, "prepared loaded bytes must feed the first-party A8 decoder");

		return sgre::render::SpriteTextureHandle{++uploads, 1, &uploads};
	},
	    [&](const auto texture) {
		    retired.push_back(texture.Index);
	    });
	sgre::scene::MotionAssetCatalog catalog(cache);
	const auto asset = catalog.Prepare(Document(), Backing());
	sgre::scene::MotionResourceManager first(cache);
	sgre::scene::MotionResourceManager second(cache);
	const auto a = first.Register(asset->Identity, asset->Model, *asset->Backing);
	const auto b = second.Register(asset->Identity, asset->Model, {});
	const auto texture = first.FindSource(a, "source")->Texture;
	Require(texture == second.FindSource(b, "source")->Texture && uploads == 1 && cache.Get(texture).References == 2, "the loaded owner must provide one shared identity to both native motion managers");
	first.Close();
	Require(retired.empty() && cache.Get(texture).References == 1, "prepared source upload must survive intermediate manager disposal");
	second.Close();
	Require(retired == std::vector<std::uint32_t>{1} && cache.Count() == 0, "the last manager must retire the prepared asset's uploaded texture once");
}

static void TestFailedPreparationAndLimits() {
	std::uint32_t uploads = 0;
	sgre::scene::TextureCache cache([&](auto, const auto&) {
		return sgre::render::SpriteTextureHandle{++uploads, 1, &uploads};
	},
	    [](auto) {
	    });
	sgre::scene::MotionAssetCatalog catalog(cache, 1);
	RequireFailure([&] {
		static_cast<void>(catalog.Prepare({}, Backing()));
	},
	    "loaded PSB document");
	RequireFailure([&] {
		static_cast<void>(catalog.Prepare(Document(), {}));
	},
	    "complete retained");
	auto invalid = Document();
	invalid->Header.Version = 9;
	RequireFailure([&] {
		static_cast<void>(catalog.Prepare(invalid, Backing()));
	},
	    "PSB version");
	Require(catalog.DocumentCount() == 0 && uploads == 0, "failed preparation must publish no document owner or physical upload");
	auto owner = catalog.Prepare(Document(), Backing());
	RequireFailure([&] {
		static_cast<void>(catalog.Prepare(Document(), Backing()));
	},
	    "capacity");
	const auto old_identity = owner->Identity.DocumentInstance;
	owner.reset();
	owner = catalog.Prepare(Document(), Backing());
	Require(owner->Identity.DocumentInstance > old_identity, "pruning expired weak documents must restore bounded capacity with a fresh identity");
	cache.Close();
	RequireFailure([&] {
		sgre::scene::MotionAssetCatalog another(cache);
		static_cast<void>(another.Prepare(Document(), Backing()));
	},
	    "live preparation owner");
}

int main() {
	try {
		TestLoadedIdentityAndWeakBookkeeping();
		TestPreparedOwnerAndManagers();
		TestFailedPreparationAndLimits();
		std::cout << "Loaded motion asset fixtures passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
