#include "sgre/scene/motion_textures.hpp"

#include <array>
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

	throw std::runtime_error("expected motion texture failure was not reported");
}

static sgre::resource::ImageTexture Description(std::uint32_t offset) {
	sgre::resource::ImageTexture texture;
	texture.SerializedValueOffset = offset;
	texture.Format = sgre::resource::PixelFormat::Alpha8;
	texture.Width = 1;
	texture.Height = 1;
	texture.TruncatedWidth = 1;
	texture.TruncatedHeight = 1;
	texture.PixelRange.Size = 1;

	return texture;
}

static void TestSlotsAndSharedSamplers() {
	std::uint32_t uploads = 0;
	std::vector<std::uint32_t> retired;
	sgre::scene::TextureCache cache([&](auto, const auto&) {
		return sgre::render::SpriteTextureHandle{++uploads, 1, &uploads};
	},
	    [&](const auto texture) {
		    retired.push_back(texture.Index);
	    });
	sgre::scene::MotionTextureSlots first(cache, 32);
	sgre::scene::MotionTextureSlots second(cache, 32);
	const std::array<std::uint8_t, 1> bytes{128};
	std::array<sgre::scene::MotionTextureSlot, 9> handles;
	for (std::size_t index = 0; index < handles.size(); ++index) {
		handles[index] = first.Import(17, Description(static_cast<std::uint32_t>(100 + index)), bytes);
		Require(handles[index].Index == index, "native motion registration must choose the first empty slot");
	}

	Require(first.SlotCount() == 16 && first.RegisteredCount() == 9 && uploads == 9, "a full eight-slot native vector must double at the ninth registration");
	const auto old = handles[3];
	first.Release(old);
	const auto replacement = first.Import(17, Description(200), bytes);
	Require(replacement.Index == 3 && replacement.Generation != old.Generation && !first.Contains(old), "slot reuse must retain the native index and reject stale authored generations");
	RequireFailure([&] {
		first.Release(old);
	},
	    "stale");
	Require(!first.ReleaseIndex(12), "native release of an empty in-range slot must leave it empty");
	const auto duplicate = second.Import(17, Description(100), {});
	const auto shared = first.Get(handles[0]).Texture;
	Require(second.Get(duplicate).Texture == shared && cache.Get(shared).References == 2, "distinct owned motion wrappers must share exactly one physical cache upload");
	first.SetFiltering(1, 2);
	second.SetFiltering(3, 4);
	Require(first.Get(handles[0]).Minification == 1 && second.Get(duplicate).Minification == 3 && cache.Get(shared).Minification == 3 && cache.Get(shared).Magnification == 4, "wrapper-local sampler fields and the latest shared physical sampler must remain distinct");
	first.Release(handles[0]);
	Require(cache.Contains(shared) && cache.Get(shared).References == 1, "one owned wrapper release must preserve another manager's physical texture reference");
	second.Release(duplicate);
	Require(!cache.Contains(shared), "final owned wrapper release must retire the shared physical texture");
}

static void TestBorrowedWrapperIdentity() {
	std::uint32_t uploads = 0;
	std::vector<std::uint32_t> retired;
	sgre::scene::TextureCache cache([&](auto, const auto&) {
		return sgre::render::SpriteTextureHandle{++uploads, 1, &uploads};
	},
	    [&](const auto texture) {
		    retired.push_back(texture.Index);
	    });
	sgre::scene::MotionTextureSlots first(cache);
	sgre::scene::MotionTextureSlots second(cache);
	const std::array<std::uint8_t, 1> bytes{128};
	auto owner = std::make_unique<sgre::scene::MotionTextureWrapper>(cache, 10, Description(100), bytes);
	const auto handle = owner->Texture();
	const auto borrowed = owner->Borrow();
	const auto a = first.RegisterBorrowed(borrowed);
	const auto b = first.RegisterBorrowed(borrowed);
	const auto c = second.RegisterBorrowed(borrowed);
	Require(first.Get(a).Borrowed && cache.Get(handle).References == 1 && uploads == 1, "borrowed wrapper registration must not acquire cache or wrapper ownership");
	first.SetFiltering(1, 2);
	Require(owner->Minification() == 1 && second.Get(c).Minification == 1 && first.Get(b).Magnification == 2, "all borrowed slots must observe mutation of the exact same wrapper");
	first.Release(a);
	second.Close();
	Require(retired.empty() && owner->Texture() == handle && borrowed.IsLive(), "borrowed slot disposal must preserve its external wrapper and GPU owner");
	owner.reset();
	Require(retired == std::vector<std::uint32_t>{1} && !borrowed.IsLive() && !first.Contains(b), "borrowed handles must expire without extending their wrapper lifetime");
	RequireFailure([&] {
		static_cast<void>(first.Get(b));
	},
	    "expired");
	Require(first.ReleaseIndex(b.Index) && first.RegisteredCount() == 0, "an expired borrowed slot must still release its non-owning registration safely");
}

static void TestBoundsAndDisposalOrder() {
	std::uint32_t uploads = 0;
	std::vector<std::uint32_t> retired;
	bool fail_release = false;
	sgre::scene::MotionTextureSlots* observed = nullptr;
	sgre::scene::TextureCache cache([&](auto, const auto&) {
		return sgre::render::SpriteTextureHandle{++uploads, 1, &uploads};
	},
	    [&](const auto texture) {
		    retired.push_back(texture.Index);
		    if (observed) {
			    Require(observed->RegisteredCount() != 0, "native GPU retirement must precede clearing the owning wrapper slot");
			    RequireFailure([&] {
				    static_cast<void>(observed->ReleaseIndex(0));
			    },
			        "outside release");
		    }
		    if (fail_release) {
			    throw std::runtime_error("GPU disposal fixture failure");
		    }
	    });
	RequireFailure([&] {
		sgre::scene::MotionTextureSlots invalid(cache, 12);
	},
	    "power of two");
	sgre::scene::MotionTextureSlots slots(cache, 8);
	const std::array<std::uint8_t, 1> bytes{128};
	std::array<sgre::scene::MotionTextureSlot, 8> handles;
	for (std::size_t index = 0; index < handles.size(); ++index) {
		handles[index] = slots.Import(22, Description(static_cast<std::uint32_t>(100 + index)), bytes);
	}

	RequireFailure([&] {
		static_cast<void>(slots.Import(22, Description(999), bytes));
	},
	    "capacity");
	Require(uploads == 9 && retired == std::vector<std::uint32_t>{9} && cache.Count() == 8 && slots.RegisteredCount() == 8, "failed slot publication must retire its newly imported wrapper without leaking the cache reference");
	RequireFailure([&] {
		static_cast<void>(slots.ReleaseIndex(8));
	},
	    "extent");
	observed = &slots;
	fail_release = true;
	RequireFailure([&] {
		slots.Close();
	},
	    "GPU disposal fixture failure");
	Require(slots.RegisteredCount() == 0 && cache.Count() == 0 && retired == std::vector<std::uint32_t>{9, 1, 2, 3, 4, 5, 6, 7, 8}, "safety close must retire every owned wrapper despite a platform cleanup failure");
	slots.Close();
	RequireFailure([&] {
		static_cast<void>(slots.Import(22, Description(100), bytes));
	},
	    "live preparation owner");
	observed = nullptr;
}

int main() {
	try {
		TestSlotsAndSharedSamplers();
		TestBorrowedWrapperIdentity();
		TestBoundsAndDisposalOrder();
		std::cout << "Motion texture ownership fixtures passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
