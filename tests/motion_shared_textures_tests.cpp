#include "sgre/scene/motion_shared_textures.hpp"

#include <array>
#include <bit>
#include <cstdint>
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

template <typename Operation>
static void RequireFailure(Operation operation, std::string_view context) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(context) != std::string_view::npos, error.what());

		return;
	}

	throw std::runtime_error("expected shared texture failure was not reported");
}

static sgre::resource::ImageTexture Description(std::uint32_t offset) {
	sgre::resource::ImageTexture description;
	description.SerializedValueOffset = offset;
	description.Format = sgre::resource::PixelFormat::Alpha8;
	description.Width = 1;
	description.Height = 1;
	description.TruncatedWidth = 1;
	description.TruncatedHeight = 1;
	description.PixelRange.Size = 1;

	return description;
}

struct SyntheticTextures {
	std::uint32_t Uploads = 0;
	std::vector<std::uint32_t> Retired;
	sgre::scene::TextureCache Cache;
	const std::array<std::uint8_t, 1> Bytes{128};

	SyntheticTextures() : Cache([&](auto, const auto&) {
		                      return sgre::render::SpriteTextureHandle{++Uploads, 1, &Uploads};
	                      },
	                          [&](auto handle) {
		                          Retired.push_back(handle.Index);
	                          }) {
	}
};

static void TestExactOwnersAndFirstDimensions() {
	SyntheticTextures fixture;
	sgre::scene::MotionTextureWrapper first(fixture.Cache, 1, Description(20), fixture.Bytes);
	sgre::scene::MotionTextureWrapper second(fixture.Cache, 1, Description(20), fixture.Bytes);
	sgre::scene::MotionSharedTextures table;
	const auto a = table.Register(first.Borrow(), {120, 40});
	const auto b = table.Register(first.Borrow(), {900, 700});
	const auto c = table.Register(second.Borrow(), {300, 80});
	Require(a == b && a.Index == 0 && c.Index == 1 && a.EncodedIndex() == -1 && c.EncodedIndex() == -2, "shared registration must deduplicate exact wrappers and complement the row index");
	Require(first.Texture() == second.Texture() && !first.Borrow().SameIdentity(second.Borrow()), "distinct wrapper owners sharing one physical texture must remain distinct identities");
	const auto original = table.GetIndex(-1);
	Require(original.References == 2 && original.Dimensions.X == 120 && original.Dimensions.Y == 40, "matching owner registration must preserve its first dimensions");
	Require(table.GetIndex(-2).Dimensions.X == 300 && fixture.Cache.Get(first.Texture()).References == 2 && fixture.Uploads == 1, "shared rows must neither replace another wrapper's dimensions nor add a GPU cache reference");
	first.SetFiltering(1, 2);
	static_cast<void>(table.Register(first.Borrow(), {20, 10}));
	Require(first.Minification() == 1 && first.Magnification() == 2 && table.RegisteredCount() == 2, "shared registration must not apply local-slot filtering side effects");
	table.Close();
	Require(fixture.Retired.empty() && first.Borrow().IsLive() && second.Borrow().IsLive(), "closing the table must not dispose borrowed wrapper or GPU owners");
}

static void TestReplacementReleaseBeforeLookup() {
	SyntheticTextures fixture;
	sgre::scene::MotionTextureWrapper owner(fixture.Cache, 1, Description(30), fixture.Bytes);
	sgre::scene::MotionSharedTextures table;
	std::optional current{table.Register(owner.Borrow(), {100, 80})};
	const auto old = *current;
	table.Replace(current, owner.Borrow(), {220, 160});
	Require(current->Index == old.Index && current->Generation != old.Generation && !table.Contains(old), "sole-owner replacement must release and reuse the row with a fresh generation");
	Require(table.Get(*current).Dimensions.X == 220 && table.Get(*current).References == 1, "sole-owner replacement must record the new dimensions after removing the old row");
	const auto other_image = table.Register(owner.Borrow(), {400, 300});
	const auto retained = *current;
	table.Replace(current, owner.Borrow(), {800, 700});
	Require(*current == retained && *current == other_image && table.Get(*current).Dimensions.X == 220 && table.Get(*current).References == 2, "replacement with another surviving registration must retain the original row and dimensions");
	table.Release(other_image);
	Require(table.Contains(*current) && table.Get(*current).References == 1, "releasing one shared image must leave its sibling registration live");
	table.Release(*current);
	Require(table.RegisteredCount() == 0 && !table.Contains(retained), "the final image registration must clear the borrowed row");
}

static void TestGrowthBoundsAndGenerations() {
	SyntheticTextures fixture;
	sgre::scene::MotionSharedTextures table(16);
	std::array<std::unique_ptr<sgre::scene::MotionTextureWrapper>, 9> owners;
	std::array<sgre::scene::MotionSharedTextureSlot, 9> slots;
	for (std::size_t index = 0; index < owners.size(); ++index) {
		owners[index] = std::make_unique<sgre::scene::MotionTextureWrapper>(fixture.Cache, 1, Description(static_cast<std::uint32_t>(100 + index)), fixture.Bytes);
		slots[index] = table.Register(owners[index]->Borrow(), {static_cast<float>(index + 1), 10});
		Require(slots[index].Index == index, "native registration must choose the first empty shared row");
	}

	Require(table.SlotCount() == 16 && table.RegisteredCount() == 9, "shared table extent must double from eight to sixteen at the ninth exact owner");
	const auto old = slots[2];
	table.Release(old);
	const auto reused = table.Register(owners[2]->Borrow(), {80, 20});
	Require(reused.Index == old.Index && reused.Generation != old.Generation && table.GetIndex(old.EncodedIndex()).Dimensions.X == 80, "native raw negative IDs observe row reuse while authored handles reject stale generations");
	RequireFailure([&] {
		table.Release(old);
	},
	    "stale");
	RequireFailure([&] {
		static_cast<void>(table.GetIndex(0));
	},
	    "negative");
	RequireFailure([&] {
		static_cast<void>(table.GetIndex(-17));
	},
	    "extent");
	RequireFailure([&] {
		static_cast<void>(table.GetIndex(std::bit_cast<std::int32_t>(std::uint32_t{0x80000000})));
	},
	    "extent");
	sgre::scene::MotionSharedTextures other;
	Require(!other.Contains(reused), "shared handles from another table must not resolve");
	RequireFailure([&] {
		static_cast<void>(other.Get(reused));
	},
	    "another owner");
	RequireFailure([&] {
		sgre::scene::MotionSharedTextures invalid(12);
	},
	    "power of two");
	sgre::scene::MotionSharedTextures bounded(8);
	for (std::size_t index = 0; index < 8; ++index) {
		static_cast<void>(bounded.Register(owners[index]->Borrow(), {1, 1}));
	}

	RequireFailure([&] {
		static_cast<void>(bounded.Register(owners[8]->Borrow(), {1, 1}));
	},
	    "capacity");
	Require(bounded.RegisteredCount() == 8 && fixture.Retired.empty(), "capacity failure must preserve existing rows and external texture owners");
}

static void TestExpiredAndClosedOwners() {
	SyntheticTextures fixture;
	sgre::scene::MotionSharedTextures table;
	auto owner = std::make_unique<sgre::scene::MotionTextureWrapper>(fixture.Cache, 1, Description(40), fixture.Bytes);
	const auto borrowed = owner->Borrow();
	const auto slot = table.Register(borrowed, {40, 20});
	Require(borrowed.SameIdentity(owner->Borrow()) && borrowed.Texture() == owner->Texture(), "borrowed access must resolve the exact live wrapper and current texture");
	owner.reset();
	Require(!borrowed.IsLive() && !borrowed.SameIdentity(borrowed) && !table.Contains(slot) && fixture.Retired == std::vector<std::uint32_t>{1}, "shared registration and borrowed queries must not retain an expired owner");
	RequireFailure([&] {
		static_cast<void>(borrowed.Texture());
	},
	    "expired");
	RequireFailure([&] {
		static_cast<void>(table.GetIndex(slot.EncodedIndex()));
	},
	    "expired");
	table.Release(slot);
	Require(table.RegisteredCount() == 0 && !table.At(slot.Index), "expired wrapper registration must still release its count without accessing the dead GPU owner");
	RequireFailure([&] {
		static_cast<void>(table.Register({}, {1, 1}));
	},
	    "live borrowed");
	sgre::scene::MotionTextureWrapper closed(fixture.Cache, 1, Description(41), fixture.Bytes);
	const auto closed_borrow = closed.Borrow();
	closed.Close();
	RequireFailure([&] {
		static_cast<void>(closed_borrow.Texture());
	},
	    "expired");
}

static void TestRawDimensionsAndFailedReplacement() {
	SyntheticTextures fixture;
	sgre::scene::MotionTextureWrapper owner(fixture.Cache, 1, Description(50), fixture.Bytes);
	sgre::scene::MotionSharedTextures table;
	const sgre::resource::MotionPoint dimensions{std::bit_cast<float>(std::uint32_t{0x80000000}), std::bit_cast<float>(std::uint32_t{0x7fc12345})};
	std::optional current{table.Register(owner.Borrow(), dimensions)};
	const auto observed = table.Get(*current).Dimensions;
	Require(std::bit_cast<std::uint32_t>(observed.X) == 0x80000000 && std::bit_cast<std::uint32_t>(observed.Y) == 0x7fc12345, "shared dimension storage must preserve signed zero and NaN payload bits without normalization");
	RequireFailure([&] {
		table.Replace(current, {}, {1, 1});
	},
	    "live borrowed");
	Require(!current && table.RegisteredCount() == 0 && owner.Borrow().IsLive(), "replacement rejection must leave the previous registration released without disposing its borrowed owner");
	const auto last = table.Register(owner.Borrow(), {10, 20});
	table.Close();
	table.Close();
	Require(!table.Contains(last) && table.RegisteredCount() == 0 && owner.Borrow().IsLive(), "table close must expire all shared handles and preserve external owners");
	RequireFailure([&] {
		static_cast<void>(table.Register(owner.Borrow(), {10, 20}));
	},
	    "closed");
	RequireFailure([&] {
		static_cast<void>(table.Get(last));
	},
	    "stale");
}

int main() {
	try {
		TestExactOwnersAndFirstDimensions();
		TestReplacementReleaseBeforeLookup();
		TestGrowthBoundsAndGenerations();
		TestExpiredAndClosedOwners();
		TestRawDimensionsAndFailedReplacement();
		std::cout << "Shared motion texture fixtures passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
