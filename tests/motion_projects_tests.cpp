#include "sgre/scene/motion_projects.hpp"

#include <array>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

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

	throw std::runtime_error("motion registry preparation rejection was not reported");
}

static std::shared_ptr<const sgre::resource::MotionResource> MakeResource(std::span<const std::string_view> names) {
	auto resource = std::make_shared<sgre::resource::MotionResource>();
	for (const auto name : names) {
		sgre::resource::MotionObject object;
		object.Name = name;
		resource->Objects.push_back(std::move(object));
	}

	return resource;
}

static void TestRegistrationIdentityAndOrder() {
	sgre::scene::MotionProjectRegistry registry;
	sgre::scene::MotionProjectIdentity identity;
	identity.DocumentInstance = 11;
	identity.RootOffset = 128;
	const std::array<std::string_view, 4> names{"z", "あ", "B", "a"};
	auto resource = MakeResource(names);
	const std::weak_ptr<const sgre::resource::MotionResource> weak_resource = resource;
	const auto first = registry.Register(identity, resource);
	resource.reset();
	Require(first.Value == 0 && registry.ReferenceCount(first) == 1 && !weak_resource.expired(), "first native registration ID or resource ownership differs");
	identity.RootOffset = 256;
	const auto duplicate = registry.Register(identity, MakeResource({}));
	Require(duplicate == first && registry.ReferenceCount(first) == 2 && registry.Identity(first).RootOffset == 128, "registration must deduplicate the document while preserving its first root/resource");
	identity.DocumentInstance = 22;
	const auto second = registry.Register(identity, MakeResource({}));
	std::array<sgre::scene::MotionProjectId, 2> order;
	Require(second.Value == 1 && registry.WriteRegistrationOrder(order) == 2 && order[0] == first && order[1] == second, "duplicate registration must preserve original insertion order");
	std::array<std::string_view, 4> sorted;
	Require(registry.WriteCharacterNames(first, sorted) == 4 && sorted == std::array<std::string_view, 4>{"B", "a", "z", "あ"}, "character enumeration must follow the native byte-key map order");
	Require(registry.Unregister(first) && registry.ReferenceCount(first) == 1 && !weak_resource.expired(), "intermediate unregistration must preserve resource and native ID");
	Require(registry.Unregister(first) && !registry.Contains(first) && weak_resource.expired(), "final unregistration must release its owned resource");
	Require(registry.WriteRegistrationOrder(order) == 1 && order[0] == second && !registry.Unregister(first), "unregistration must preserve the remaining order and reject stale IDs");
	identity.DocumentInstance = 11;
	const auto replacement = registry.Register(identity, MakeResource({}));
	Require(replacement.Value == 2 && replacement != first, "removed native project IDs must not be recycled");
}

static void TestCharacterMutation() {
	sgre::scene::MotionProjectRegistry registry;
	sgre::scene::MotionProjectIdentity identity;
	identity.DocumentInstance = 1;
	const std::array<std::string_view, 3> names{"first", "last", "other"};
	const auto project = registry.Register(identity, MakeResource(names));
	const auto* original = registry.FindCharacter(project, "first");
	Require(original && !registry.RenameCharacter(project, "first", "first") && !registry.RenameCharacter(project, "first", "other") && !registry.RenameCharacter(project, "missing", "new"), "native rename must reject missing origins and every existing destination");
	Require(registry.RenameCharacter(project, "first", "new") && registry.FindCharacter(project, "new") == original && !registry.FindCharacter(project, "first") && original->Name == "first", "rename must move the lookup key without rewriting the original motion object");
	Require(registry.FindCharacter(project, std::string_view("new\0suffix", 10)) == original && !registry.FindCharacter(project, "New"), "native character lookup must preserve byte case and C-string termination");
	Require(registry.RemoveCharacter(project, "missing") && registry.RemoveCharacter(project, "new") && !registry.FindCharacter(project, "new"), "native removal must succeed for an existing project even when its character is absent");
	std::array<std::string_view, 3> remaining;
	Require(registry.WriteCharacterNames(project, remaining) == 2 && remaining[0] == "last" && remaining[1] == "other", "mutable character map order differs after removal");
	Require(!registry.RenameCharacter({}, "last", "new") && !registry.RemoveCharacter({}, "last") && registry.WriteCharacterNames({}, remaining) == 0, "missing project lookup/mutation differs");
}

static void TestPreparationSafety() {
	sgre::scene::MotionProjectLimits limits;
	limits.MaxProjects = 1;
	limits.MaxCharacters = 2;
	limits.MaxNameBytes = 3;
	sgre::scene::MotionProjectRegistry registry(limits);
	sgre::scene::MotionProjectIdentity identity;
	identity.DocumentInstance = 1;
	const std::array<std::string_view, 2> names{"a", "bc"};
	const auto project = registry.Register(identity, MakeResource(names));
	RequireFailure([&] {
		static_cast<void>(registry.RenameCharacter(project, "a", "long"));
	},
	    "name preparation limit");
	Require(registry.FindCharacter(project, "a") && !registry.FindCharacter(project, "long"), "failed rename must preserve the character map");
	RequireFailure([&] {
		static_cast<void>(registry.WriteCharacterNames(project, {}));
	},
	    "output is too small");
	identity.DocumentInstance = 2;
	RequireFailure([&] {
		static_cast<void>(registry.Register(identity, MakeResource({})));
	},
	    "capacity");
	Require(registry.Unregister(project), "bounded registry did not release its prepared storage");
	const std::array<std::string_view, 1> too_long{"long"};
	RequireFailure([&] {
		static_cast<void>(registry.Register(identity, MakeResource(too_long)));
	},
	    "preparation limit");
	const auto replacement = registry.Register(identity, MakeResource(names));
	Require(replacement.Value == 1 && registry.Count() == 1, "failed registration must not consume an identifier or prepared capacity");
	RequireFailure([&] {
		static_cast<void>(registry.Register({}, MakeResource({})));
	},
	    "loaded document");
}

int main() {
	try {
		TestRegistrationIdentityAndOrder();
		TestCharacterMutation();
		TestPreparationSafety();
		std::cout << "motion project ownership, native IDs/counts and character-map mutation passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
