#include "sgre/scene/motion_projects.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

static constexpr std::array<std::string_view, 3> kCharacters{"a", "A", "あ"};
static constexpr std::array<std::string_view, 3> kTimelines{"idle", "Idle", "other"};

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

static sgre::scene::MotionProjectIdentity Identity(std::uint64_t document, std::uint32_t root = 128) {
	sgre::scene::MotionProjectIdentity identity;
	identity.DocumentInstance = document;
	identity.RootOffset = root;

	return identity;
}

static std::shared_ptr<const sgre::resource::MotionResource> MakeResource(const std::array<std::uint8_t, 3>& masks) {
	auto resource = std::make_shared<sgre::resource::MotionResource>();
	for (std::size_t character_index = 0; character_index < masks.size(); ++character_index) {
		if (masks[character_index] == 0) {
			continue;
		}

		sgre::resource::MotionObject character;
		character.Name = kCharacters[character_index];
		for (std::size_t timeline_index = 0; timeline_index < kTimelines.size(); ++timeline_index) {
			if ((masks[character_index] & (std::uint8_t{1} << timeline_index)) == 0) {
				continue;
			}

			sgre::resource::MotionTimeline timeline;
			timeline.Name = kTimelines[timeline_index];
			character.Timelines.push_back(std::move(timeline));
		}

		resource->Objects.push_back(std::move(character));
	}

	return resource;
}

static void RequireSelection(const std::optional<sgre::scene::ResolvedMotionTimeline>& result, sgre::scene::MotionProjectId project, sgre::scene::MotionProjectIdentity identity, std::string_view character, std::string_view timeline) {
	Require(result && result->Project == project && result->Identity == identity && result->Character && result->Timeline && result->Character->Name == character && result->Timeline->Name == timeline, "motion timeline selection differs from native project/name identity");
}

static void TestDocumentPreferenceAndFallback() {
	sgre::scene::MotionProjectRegistry registry;
	const auto first = registry.Register(Identity(11, 128), MakeResource({1, 0, 0}));
	const auto second = registry.Register(Identity(22, 256), MakeResource({2, 1, 1}));
	const auto third = registry.Register(Identity(33, 384), MakeResource({5, 0, 0}));
	RequireSelection(registry.ResolveTimeline("a", "idle"), first, Identity(11, 128), "a", "idle");
	RequireSelection(registry.ResolveTimeline("a", "idle", Identity(33, 384)), third, Identity(33, 384), "a", "idle");
	RequireSelection(registry.ResolveTimeline("a", "idle", Identity(22, 256)), first, Identity(11, 128), "a", "idle");
	RequireSelection(registry.ResolveTimeline("a", "Idle", Identity(11, 128)), second, Identity(22, 256), "a", "Idle");
	RequireSelection(registry.ResolveTimeline("a", "idle", Identity(33, 385)), first, Identity(11, 128), "a", "idle");
	RequireSelection(registry.ResolveTimeline("a", "idle", Identity(99, 384)), first, Identity(11, 128), "a", "idle");
	RequireSelection(registry.ResolveTimeline("a", "idle", Identity(0, 384)), first, Identity(11, 128), "a", "idle");
	Require(!registry.ResolveTimeline("missing", "idle") && !registry.ResolveTimeline("a", "missing") && !registry.ResolveTimeline("a", "IDLE"), "missing and case-different motion keys must not select a fallback timeline");
	RequireSelection(registry.ResolveTimeline("A", "idle"), second, Identity(22, 256), "A", "idle");
	RequireSelection(registry.ResolveTimeline("あ", "idle"), second, Identity(22, 256), "あ", "idle");
	RequireSelection(registry.ResolveTimeline(std::string_view("a\0suffix", 8), std::string_view("idle\0suffix", 11), Identity(33, 384)), third, Identity(33, 384), "a", "idle");
}

static void TestLiveCharacterMapAndBorrowing() {
	sgre::scene::MotionProjectRegistry registry;
	auto resource = MakeResource({1, 0, 0});
	const std::weak_ptr<const sgre::resource::MotionResource> weak_resource = resource;
	const auto project = registry.Register(Identity(1), resource);
	resource.reset();
	const auto duplicate = registry.Register(Identity(1, 999), MakeResource({4, 0, 0}));
	Require(duplicate == project, "duplicate native document registration changed its project");
	RequireSelection(registry.ResolveTimeline("a", "idle", Identity(1)), project, Identity(1), "a", "idle");
	Require(!registry.ResolveTimeline("a", "other"), "duplicate registration replaced the original immutable model");
	Require(registry.RenameCharacter(project, "a", "renamed"), "native character rename failed");
	const auto renamed = registry.ResolveTimeline("renamed", "idle", Identity(1));
	RequireSelection(renamed, project, Identity(1), "a", "idle");
	Require(!registry.ResolveTimeline("a", "idle"), "selection ignored the live renamed character lookup key");
	Require(registry.Unregister(project) && !weak_resource.expired(), "intermediate unregistration retired the registered model");
	Require(registry.RemoveCharacter(project, "renamed") && !registry.ResolveTimeline("renamed", "idle"), "selection ignored live character removal");
	Require(registry.Unregister(project) && weak_resource.expired(), "borrowed timeline result added a hidden strong model root");
	Require(!registry.ResolveTimeline("renamed", "idle", Identity(1)), "retired native registration still participated in selection");
}

template <std::size_t Size>
static std::array<std::uint8_t, Size> ReadBytes(std::ifstream& stream) {
	std::array<char, Size> raw;
	if (!stream.read(raw.data(), static_cast<std::streamsize>(raw.size()))) {
		throw std::runtime_error("motion native selection fixture is truncated");
	}

	std::array<std::uint8_t, Size> result;
	for (std::size_t index = 0; index < Size; ++index) {
		result[index] = static_cast<std::uint8_t>(static_cast<unsigned char>(raw[index]));
	}

	return result;
}

static void ReplayNativeVectors(std::string_view path) {
	std::ifstream stream(std::string(path), std::ios::binary);
	Require(static_cast<bool>(stream), "motion native selection fixture cannot be opened");
	const auto header = ReadBytes<8>(stream);
	Require(header[0] == 'M' && header[1] == 'T' && header[2] == 'S' && header[3] == '1', "motion native selection fixture has an invalid tag");
	const auto count = static_cast<std::uint32_t>(header[4]) | (static_cast<std::uint32_t>(header[5]) << 8) | (static_cast<std::uint32_t>(header[6]) << 16) | (static_cast<std::uint32_t>(header[7]) << 24);
	Require(count > 0 && count <= 4096, "motion native selection fixture count exceeds its replay limit");
	for (std::uint32_t case_index = 0; case_index < count; ++case_index) {
		const auto row = ReadBytes<20>(stream);
		Require(row[0] <= 4 && row[1] <= 6 && row[2] <= 3 && row[3] <= 3 && row[4] <= 1 && row[5] <= 1 && row[18] <= row[0] && row[19] <= 3, "motion native selection fixture has an invalid selector");
		sgre::scene::MotionProjectRegistry registry;
		std::array<sgre::scene::MotionProjectId, 4> projects;
		for (std::uint8_t project_index = 0; project_index < row[0]; ++project_index) {
			std::array<std::uint8_t, 3> masks;
			for (std::size_t index = 0; index < masks.size(); ++index) {
				masks[index] = row[6 + project_index * 3 + index];
				Require(masks[index] <= 7, "motion native selection fixture has an invalid timeline mask");
			}

			projects[project_index] = registry.Register(Identity(project_index + 1, 128 + project_index * 16), MakeResource(masks));
		}

		std::optional<sgre::scene::MotionProjectIdentity> preference;
		if (row[1] >= 1 && row[1] <= row[0]) {
			preference = Identity(row[1], 128 + (row[1] - 1) * 16);
		} else if (row[1] == 5) {
			preference = Identity(1000, 128);
		} else if (row[1] == 6 && row[0] != 0) {
			preference = Identity(1, 129);
		}

		std::string character(row[2] < kCharacters.size() ? kCharacters[row[2]] : "missing");
		std::string timeline(row[3] < kTimelines.size() ? kTimelines[row[3]] : "missing");
		if (row[4]) {
			character.append("\0suffix", 7);
		}

		if (row[5]) {
			timeline.append("\0suffix", 7);
		}

		const auto selected = registry.ResolveTimeline(character, timeline, preference);
		if (row[18] == 0) {
			Require(!selected && row[19] == 0, "portable motion selection differs from the native missing result");
			continue;
		}

		Require(row[2] < kCharacters.size() && row[19] >= 1, "motion native selection fixture has inconsistent result names");
		RequireSelection(selected, projects[row[18] - 1], Identity(row[18], 128 + (row[18] - 1) * 16), kCharacters[row[2]], kTimelines[row[19] - 1]);
	}

	Require(stream.peek() == std::char_traits<char>::eof(), "motion native selection fixture has trailing bytes");
}

int main(int argc, char** argv) {
	try {
		Require(argc <= 2, "motion timeline selection test accepts one optional native fixture path");
		TestDocumentPreferenceAndFallback();
		TestLiveCharacterMapAndBorrowing();
		if (argc == 2) {
			ReplayNativeVectors(argv[1]);
		}

		std::cout << "motion document/root preference, live character keys and borrowed timeline selection passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
