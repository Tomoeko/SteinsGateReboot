#include "image_fixture.hpp"

static void TestSelectionAndSourceLifetime() {
	ImageFixture fixture;
	auto document = Document({Entry("first", 16, 8, {Tile(0, 0, 4, 4, 2), Tile(4, 0, 4, 4, 2), Tile(8, 0, 4, 4, 1)}), Entry("first", 99, 99, {}), Entry("empty", 0, 0, {})});
	const auto default_asset = fixture.Catalog.Prepare(document, fixture.Backing);
	const auto named_asset = fixture.Catalog.Prepare(document, fixture.Backing, "first");
	const auto missing = fixture.Catalog.Prepare(document, fixture.Backing, "FIRST");
	const auto empty = fixture.Catalog.Prepare(document, fixture.Backing, "empty");
	Require(default_asset->Valid && default_asset->Model.Width == 16 && default_asset->Model.Height == 8 && named_asset->Model.Width == 16, "empty/default and duplicate label choose the actual first entry");
	Require(default_asset->DocumentIdentity == named_asset->DocumentIdentity && !missing->Valid && missing->Model.Width == 0 && empty->Valid && empty->Model.Tiles.empty(), "document identity, missing label and valid empty source list remain distinct");
	sgre::scene::ImageTextureOwner first(fixture.Cache, default_asset);
	sgre::scene::ImageTextureOwner second(fixture.Cache, named_asset);
	const auto borrowed = first.Source(0);
	const auto shared = borrowed.Texture();
	Require(first.SourceCount() == 3 && fixture.Uploads == 2 && fixture.Cache.Get(shared).References == 4, "separate native wrappers retain each real repeated source reference");
	Require(first.AddressU(0) == 2 && first.AddressV(0) == 2 && fixture.Cache.Address(shared).Horizontal == 2 && fixture.Cache.Address(shared).Revision == 4, "Image initializes mirror addressing separately from filter state");
	first.SetAddress(1, 0);
	Require(second.AddressU(0) == 2 && fixture.Cache.Address(shared).Horizontal == 1 && fixture.Cache.Address(shared).Vertical == 0, "local wrapper address and latest shared physical address remain separate");
	const auto revision = fixture.Cache.Address(shared).Revision;
	first.SetAddress(1, 0);
	Require(fixture.Cache.Address(shared).Revision == revision + 2, "repeated address writes preserve each native invalidation event");
	first.Close();
	Require(!borrowed.IsLive() && fixture.Cache.Get(shared).References == 2 && fixture.Released.empty(), "borrowed wrapper expires while sibling cache references survive");
	second.Close();
	Require(fixture.Cache.Count() == 0 && fixture.Released == std::vector<std::uint32_t>{2, 1}, "last image owner releases source cache references in source order");
}

static void TestCatalogAndPreparationFailures() {
	ImageFixture fixture;
	const auto document = Document({Entry("good", 1, 1, {Tile(0, 0, 1, 1, 0)}), Integer(3)});
	const auto prepared = fixture.Catalog.Prepare(document, fixture.Backing);
	Require(prepared->Valid && prepared->Model.Width == 1, "unused malformed entries are not interpreted after native selection");
	bool malformed = false;
	try {
		static_cast<void>(fixture.Catalog.Prepare(document, fixture.Backing, "absent"));
	} catch (const std::runtime_error&) {
		malformed = true;
	}
	Require(malformed, "referenced malformed label rejects with a contextual preparation error");
	const auto sources = prepared->TileBounds;
	sgre::scene::PictureRegionSet regions(sources, 4);
	regions.Append({0, 0, 1, 1});
	Require(regions.Width() == 1 && regions.Regions().size() == 1, "actual selected image model supplies prepared picture source bounds");
	bool address = false;
	sgre::scene::ImageTextureOwner owner(fixture.Cache, prepared);
	try {
		owner.SetAddress(3, 0);
	} catch (const std::runtime_error&) {
		address = true;
	}
	Require(address && owner.AddressU(0) == 2, "unmeasured address index fails before changing source state");
}

static void TestRetirementFailureAndExpiredCatalog() {
	ImageFixture fixture;
	auto document = Document({Entry("sources", 3, 1, {Tile(0, 0, 1, 1, 0), Tile(1, 0, 1, 1, 1), Tile(2, 0, 1, 1, 2)})});
	auto asset = fixture.Catalog.Prepare(document, fixture.Backing);
	const std::weak_ptr<const sgre::resource::PsbDocument> weak_document = document;
	const std::weak_ptr<const sgre::scene::LoadedImageAsset> weak_asset = asset;
	sgre::scene::ImageTextureOwner owner(fixture.Cache, asset);
	const auto borrowed = owner.Source(1);
	document.reset();
	asset.reset();
	fixture.FailRelease = true;
	bool failure = false;
	try {
		owner.Close();
	} catch (const std::runtime_error&) {
		failure = true;
	}
	Require(failure && fixture.Cache.Count() == 0 && fixture.Released == std::vector<std::uint32_t>{0, 1, 2}, "cleanup failure still retires every actual source in order");
	Require(!borrowed.IsLive() && weak_asset.expired() && weak_document.expired(), "native source cleanup expires borrowed wrappers before immutable model/backing release");
	fixture.Catalog.Prune();
	Require(fixture.Catalog.DocumentCount() == 0, "catalog weak records do not retain retired Image documents");
	owner.Close();
}

class ImageProjectionReader {
public:
	explicit ImageProjectionReader(const char* filename) : m_Input(filename, std::ios::binary) {
		Require(static_cast<bool>(m_Input), "image projection could not be opened");
	}

	std::uint32_t Word() {
		std::array<char, 4> bytes{};
		m_Input.read(bytes.data(), bytes.size());
		Require(static_cast<bool>(m_Input), "image projection is truncated");

		return static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[0])) | static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[1])) << 8 | static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[2])) << 16 | static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[3])) << 24;
	}

	void Compare(const std::vector<std::uint32_t>& values) {
		Require(Word() == values.size(), "image projection field count differs");
		for (const auto value : values) {
			Require(Word() == value, "image projection state differs");
		}
	}

	void Finish() {
		Require(m_Input.peek() == std::char_traits<char>::eof(), "image projection has trailing bytes");
	}

private:
	std::ifstream m_Input;
};

static std::vector<std::uint32_t> SourceSnapshot(const sgre::scene::ImageTextureOwner& owner) {
	const auto& asset = owner.Asset();
	std::vector<std::uint32_t> result{asset.Model.Width, asset.Model.Height, static_cast<std::uint32_t>(asset.Valid), static_cast<std::uint32_t>(owner.SourceCount())};
	for (std::size_t index = 0; index < owner.SourceCount(); ++index) {
		for (const auto value : asset.TileBounds[index].Bounds) {
			result.push_back(std::bit_cast<std::uint32_t>(value));
		}
		result.push_back(*asset.Model.Tiles[index].Texture.SerializedValueOffset - 0x100u);
		result.push_back(owner.AddressU(index));
		result.push_back(owner.AddressV(index));
	}

	return result;
}

static std::vector<std::uint32_t> CacheSnapshot(const sgre::scene::TextureCache& cache) {
	std::array<sgre::scene::CachedTextureHandle, 4> ordered{};
	const auto count = cache.WriteRegistrationOrder(ordered);
	std::vector<std::uint32_t> result{static_cast<std::uint32_t>(count)};
	static constexpr std::array<std::uint32_t, 3> native_address{3, 1, 2};
	for (std::size_t index = 0; index < count; ++index) {
		const auto texture = cache.Get(ordered[index]);
		const auto address = cache.Address(ordered[index]);
		result.insert(result.end(), {texture.Backend.Index, texture.References, native_address.at(address.Horizontal), native_address.at(address.Vertical), static_cast<std::uint32_t>(address.Revision != 0)});
	}

	return result;
}

static void TestNativeProjection(const char* filename) {
	ImageProjectionReader input(filename);
	Require(input.Word() == 1000, "image projection retains its exact bounded case count");
	static constexpr std::array<const char*, 4> labels{"alpha", "beta", "alpha", "delta"};
	static constexpr std::array<const char*, 6> requests{"", "alpha", "beta", "delta", "ALPHA", "missing"};
	for (std::uint32_t case_id = 0; case_id < 1000; ++case_id) {
		ImageFixture fixture;
		sgre::resource::PsbArray entries;
		const auto entry_count = input.Word();
		Require(entry_count <= 4, "native image entry count is outside projection bounds");
		for (std::uint32_t index = 0; index < entry_count; ++index) {
			const auto width = input.Word();
			const auto height = input.Word();
			const auto tile_count = input.Word();
			Require(width <= 4096 && height <= 4096 && tile_count <= 4, "native image dimensions/source count exceed projection bounds");
			sgre::resource::PsbArray tiles;
			for (std::uint32_t tile = 0; tile < tile_count; ++tile) {
				const auto left = input.Word();
				const auto top = input.Word();
				const auto tile_width = input.Word();
				const auto tile_height = input.Word();
				const auto identifier = input.Word();
				tiles.push_back(Tile(left, top, tile_width, tile_height, identifier));
			}
			entries.push_back(Entry(labels[index], width, height, std::move(tiles)));
		}

		const std::array<std::uint32_t, 2> requested{input.Word(), input.Word()};
		const std::array<std::uint32_t, 2> close_order{input.Word(), input.Word()};
		const auto document = Document(std::move(entries));
		std::array<std::unique_ptr<sgre::scene::ImageTextureOwner>, 2> owners;
		for (std::size_t index = 0; index < owners.size(); ++index) {
			owners[index] = std::make_unique<sgre::scene::ImageTextureOwner>(fixture.Cache, fixture.Catalog.Prepare(document, fixture.Backing, requests.at(requested[index])));
			input.Compare(SourceSnapshot(*owners[index]));
			input.Compare(CacheSnapshot(fixture.Cache));
		}
		for (const auto index : close_order) {
			owners.at(index)->Close();
			input.Compare(CacheSnapshot(fixture.Cache));
			input.Compare(fixture.Released);
		}
		Require(fixture.Cache.Count() == 0, "both native image owners retire all their shared texture references");
	}
	input.Finish();
}

int main(int argc, char** argv) {
	try {
		TestSelectionAndSourceLifetime();
		TestCatalogAndPreparationFailures();
		TestRetirementFailureAndExpiredCatalog();
		if (argc == 2) {
			TestNativeProjection(argv[1]);
		}
		std::cout << "image asset tests passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
