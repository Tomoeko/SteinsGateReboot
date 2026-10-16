#include "sgre/scene/motion_resources.hpp"

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

	throw std::runtime_error("expected motion resource failure was not reported");
}

static std::shared_ptr<sgre::resource::MotionResource> Model(std::span<const std::string_view> names) {
	auto model = std::make_shared<sgre::resource::MotionResource>();
	std::uint32_t offset = 100;
	for (const auto name : names) {
		sgre::resource::MotionSource source;
		source.Name = name;
		source.Texture.SerializedValueOffset = offset++;
		source.Texture.Format = sgre::resource::PixelFormat::Alpha8;
		source.Texture.Width = 1;
		source.Texture.Height = 1;
		source.Texture.TruncatedWidth = 1;
		source.Texture.TruncatedHeight = 1;
		source.Texture.PixelRange.Size = 1;
		model->Sources.push_back(std::move(source));
	}

	sgre::resource::MotionObject character;
	character.Name = "character";
	model->Objects.push_back(std::move(character));

	return model;
}

static void TestProjectSourcesAndFinalization() {
	std::uint32_t uploads = 0;
	std::vector<std::uint32_t> retired;
	sgre::scene::MotionResourceManager* observed = nullptr;
	std::weak_ptr<const sgre::resource::MotionResource> weak_model;
	sgre::scene::MotionProjectId id;
	bool inspect_release = false;
	sgre::scene::TextureCache cache([&](auto, const auto&) {
		Require(observed->Count() == 0, "native source import must precede publication of a new project");

		return sgre::render::SpriteTextureHandle{++uploads, 1, &uploads};
	},
	    [&](const auto texture) {
		    if (inspect_release) {
			    Require(observed->Contains(id) && !weak_model.expired(), "native texture disposal must occur before project/model retirement");
			    RequireFailure([&] {
				    observed->SetFiltering(1, 1);
			    },
			        "outside a resource callback");
		    }
		    retired.push_back(texture.Index);
	    });
	sgre::scene::MotionResourceManager manager(cache);
	observed = &manager;
	const std::array<std::string_view, 3> names{"z", "a", "m"};
	auto model = Model(names);
	weak_model = model;
	const auto* original_character = &model->Objects[0];
	const std::array<std::uint8_t, 1> bytes{128};
	sgre::scene::MotionProjectIdentity identity{7, 123};
	id = manager.Register(identity, model, bytes);
	Require(id.Value == 0 && uploads == 3 && manager.ReferenceCount(id) == 1, "new motion project must import each source exactly once and start at identifier zero");
	Require(cache.Get(manager.FindSource(id, "z")->Texture).Backend.Index == 1 && cache.Get(manager.FindSource(id, "a")->Texture).Backend.Index == 2, "source import must preserve the serialized resource order rather than sorted lookup order");
	std::array<std::string_view, 3> output;
	Require(manager.WriteSourceNames(id, output) == 3 && output == std::array<std::string_view, 3>{"a", "m", "z"}, "prepared source lookup must retain the native byte-key map order");
	Require(manager.FindSource(id, std::string_view("a\0ignored", 9))->Source->Name == "a" && !manager.FindSource(id, "absent"), "source lookup must preserve native C-string termination and missing-name behavior");
	identity.RootOffset = 999;
	const auto duplicate = manager.Register(identity, Model({}), {});
	Require(duplicate == id && uploads == 3 && manager.ReferenceCount(id) == 2 && manager.FindCharacter(id, "character") == original_character, "duplicate document registration must preserve first root/model and avoid reimporting source textures");
	model.reset();
	Require(!weak_model.expired() && manager.Unregister(id) && retired.empty() && manager.ReferenceCount(id) == 1, "intermediate project release must retain all source wrappers and the immutable model");
	inspect_release = true;
	Require(manager.Unregister(id) && retired == std::vector<std::uint32_t>{2, 3, 1} && weak_model.expired() && cache.Count() == 0, "final source disposal must follow sorted native map order before retiring the project/model");
	Require(!manager.Unregister(id) && manager.Count() == 0 && !manager.FindSource(id, "a"), "final project removal must retire its source lookup and preserve missing-project behavior");
	inspect_release = false;
	const auto replacement = manager.Register(identity, Model({}), {});
	Require(replacement.Value == 1, "project identifiers must increase without recycling a retired document's old identifier");
}

static void TestTwoManagersAndCharacterMaps() {
	std::uint32_t uploads = 0;
	std::vector<std::uint32_t> retired;
	sgre::scene::TextureCache cache([&](auto, const auto&) {
		return sgre::render::SpriteTextureHandle{++uploads, 1, &uploads};
	},
	    [&](const auto texture) {
		    retired.push_back(texture.Index);
	    });
	sgre::scene::MotionResourceManager first(cache);
	sgre::scene::MotionResourceManager second(cache);
	const std::array<std::string_view, 1> names{"source"};
	auto model = Model(names);
	const auto* original = &model->Objects[0];
	const std::array<std::uint8_t, 1> bytes{128};
	const sgre::scene::MotionProjectIdentity identity{12, 33};
	const auto a = first.Register(identity, model, bytes);
	const auto b = second.Register(identity, model, {});
	const auto shared = first.FindSource(a, "source")->Texture;
	Require(first.FindSource(a, "source")->Texture == second.FindSource(b, "source")->Texture && uploads == 1 && cache.Get(shared).References == 2, "two native motion managers must own separate source wrappers sharing one GPU upload");
	Require(first.RenameCharacter(a, "character", "renamed") && first.FindCharacter(a, "renamed") == original && second.FindCharacter(b, "character") == original && original->Name == "character", "per-manager character renames must preserve the immutable model and the other manager's dictionary");
	Require(first.RemoveCharacter(a, "missing") && first.RemoveCharacter(a, "renamed") && !first.FindCharacter(a, "renamed"), "native character removal must succeed for a live project even when its key is absent");
	first.Close();
	Require(retired.empty() && cache.Get(shared).References == 1 && first.Count() == 0, "closing one manager must preserve the second manager's source and physical texture");
	Require(second.Unregister(b) && retired == std::vector<std::uint32_t>{1}, "final source wrapper retirement must destroy one shared GPU texture exactly once");
}

static void TestPreparationFailureAndCleanup() {
	std::uint32_t uploads = 0;
	std::vector<std::uint32_t> retired;
	bool fail_release = false;
	sgre::scene::TextureCacheLimits cache_limits;
	cache_limits.MaxTextures = 2;
	sgre::scene::TextureCache cache([&](auto, const auto&) {
		return sgre::render::SpriteTextureHandle{++uploads, 1, &uploads};
	},
	    [&](const auto texture) {
		    retired.push_back(texture.Index);
		    if (fail_release) {
			    throw std::runtime_error("source GPU cleanup fixture failure");
		    }
	    },
	    cache_limits);
	sgre::scene::MotionResourceManager manager(cache);
	const std::array<std::string_view, 3> too_many{"third", "first", "second"};
	const std::array<std::uint8_t, 1> bytes{128};
	const sgre::scene::MotionProjectIdentity identity{3, 4};
	RequireFailure([&] {
		static_cast<void>(manager.Register(identity, Model(too_many), bytes));
	},
	    "capacity");
	Require(manager.Count() == 0 && cache.Count() == 0 && retired == std::vector<std::uint32_t>{2, 1}, "failed source preparation must retire each imported wrapper without publishing a partial project");
	const std::array<std::string_view, 2> names{"z", "a"};
	const auto id = manager.Register(identity, Model(names), bytes);
	fail_release = true;
	RequireFailure([&] {
		static_cast<void>(manager.Unregister(id));
	},
	    "source GPU cleanup fixture failure");
	Require(manager.Count() == 0 && cache.Count() == 0 && retired == std::vector<std::uint32_t>{2, 1, 4, 3}, "final unregistration must attempt all source owners and retire the project despite platform cleanup failures");
	fail_release = false;
	const std::array<std::string_view, 2> duplicate{"same", "same"};
	RequireFailure([&] {
		static_cast<void>(manager.Register(identity, Model(duplicate), bytes));
	},
	    "duplicate native source");
	Require(manager.Count() == 0 && cache.Count() == 0, "ambiguous native source names must fail without leaving prepared owners");
	manager.Close();
	RequireFailure([&] {
		static_cast<void>(manager.Register(identity, Model({}), {}));
	},
	    "live preparation owner");
}

int main() {
	try {
		TestProjectSourcesAndFinalization();
		TestTwoManagersAndCharacterMaps();
		TestPreparationFailureAndCleanup();
		std::cout << "Motion resource ownership fixtures passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
