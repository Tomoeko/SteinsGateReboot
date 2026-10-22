#include "sgre/scenario/scenario_search.hpp"

#include "sgre/app/launch_configuration.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>

static void Require(bool condition, std::string_view reason) {
	if (!condition) {
		throw std::runtime_error(std::string(reason));
	}
}

static void Reject(const auto& operation, std::string_view expected) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(expected) != std::string_view::npos, "scenario rejection lost its precise cause");
		return;
	}

	throw std::runtime_error("scenario malformed input unexpectedly succeeded");
}

static sgre::resource::PsbValue Value(auto data) {
	sgre::resource::PsbValue result;
	result.Data = std::move(data);

	return result;
}

static sgre::resource::PsbValue Number(std::int64_t data) {
	return Value(data);
}

static sgre::resource::PsbValue String(std::string data) {
	return Value(std::move(data));
}

static sgre::resource::PsbValue Snapshot(std::int32_t point, std::int32_t line) {
	return Value(sgre::resource::PsbArray{Number(point), Value(sgre::resource::PsbObject{}), Number(0), Number(0), Number(line)});
}

static sgre::resource::PsbValue Variant(std::string name, std::string text) {
	return Value(sgre::resource::PsbArray{String(std::move(name)), String(std::move(text)), Number(0)});
}

static sgre::resource::PsbValue TextRecord(std::string name, std::string original, std::string translation) {
	return Value(sgre::resource::PsbArray{String(std::move(name)), Value(sgre::resource::PsbArray{Variant("", std::move(original)), Variant("Translated speaker", std::move(translation)), Variant("", "Traditional fixture"), Variant("", "Simplified fixture")}), Value(sgre::resource::PsbArray{}), Number(0), Value(sgre::resource::PsbObject{})});
}

static sgre::resource::PsbValue Scene(std::string label, std::int32_t first_line, sgre::resource::PsbArray lines, sgre::resource::PsbArray texts = {}) {
	return Value(sgre::resource::PsbObject{{"version", Value(1.0)}, {"label", String(std::move(label))}, {"firstLine", Number(first_line)}, {"lines", Value(std::move(lines))}, {"texts", Value(std::move(texts))}});
}

static sgre::resource::PsbDocument Document() {
	sgre::resource::PsbDocument result;
	result.Root = Value(sgre::resource::PsbObject{
	    {"name", String("fixture.ks")},
	    {"languages", Value(sgre::resource::PsbArray{String("en"), String("tc"), String("sc")})},
	    {"scenes", Value(sgre::resource::PsbArray{Scene("*opening", 10, {Snapshot(4, 10), Number(1), Snapshot(7, 20), Number(2)}, {TextRecord("Speaker", "Original alpha", "Translated beta"), TextRecord("Narrator", "Original gamma", "Translated delta")}), Scene("*later", 30, {Snapshot(8, 30), Snapshot(9, 40)})})},
	    {"llmap", Value(sgre::resource::PsbArray{Value(sgre::resource::PsbObject{{"*opening", Value(sgre::resource::PsbArray{Number(12), Number(22)})}}), Value(sgre::resource::PsbObject{}), Value(sgre::resource::PsbObject{})})}});

	return result;
}

static sgre::resource::PsbValue& Member(sgre::resource::PsbValue& value, std::string_view key) {
	for (auto& field : std::get<sgre::resource::PsbObject>(value.Data)) {
		if (field.first == key) {
			return field.second;
		}
	}

	throw std::runtime_error("synthetic scenario member is absent");
}

static sgre::resource::PsbArray& Scenes(sgre::resource::PsbDocument& document) {
	return std::get<sgre::resource::PsbArray>(Member(document.Root, "scenes").Data);
}

static void TestMultilingualOwnedResults() {
	sgre::scenario::ScenarioSearchResults retained;
	{
		const auto document = Document();
		sgre::scenario::ScenarioSearchRequest request;
		request.Query = "beta";
		retained = sgre::scenario::SearchScenarioDocument(document, request);
		Require(retained.Matches.size() == 1 && !retained.Truncated && !retained.Cancelled && retained.EntriesRead == 1, "selected translation query did not return its exact record");
		const auto& match = retained.Matches.front();
		Require(match.Storage == "fixture.ks" && match.SceneLabel == "*opening" && match.SourceLine == 10 && match.TextId == 1 && match.TranslatedSourceLine == 12, "search lost original and translated source identities");
		Require(match.Character == "Translated speaker" && match.OriginalText == "Original alpha" && match.Translation == "Translated beta" && match.Language == "en", "language tuple fields were not recovered independently");
		Require(match.JumpTarget == "fixture.ks:10" && match.JumpLine == 10 && match.SnapshotLine == 10, "jump command substituted a text index or translated line");
		for (const auto& query : {"alpha", "Translated speaker", "fixture.ks", "*opening"}) {
			request.Query = query;
			Require(!sgre::scenario::SearchScenarioDocument(document, request).Matches.empty(), "query omitted a documented search field");
		}
		request.Language = "ja";
		request.Query = "alpha";
		const auto japanese = sgre::scenario::SearchScenarioDocument(document, request);
		Require(japanese.Matches.front().Character == "Speaker" && japanese.Matches.front().Translation == "Original alpha" && !japanese.Matches.front().TranslatedSourceLine, "base Japanese variant or name fallback was lost");
		request.Language = "tc";
		request.Query = "Traditional";
		Require(sgre::scenario::SearchScenarioDocument(document, request).Matches.size() == 2, "declared Traditional variant was not selected");
	}

	Require(retained.Matches.front().OriginalText == "Original alpha" && retained.Matches.front().JumpTarget == "fixture.ks:10", "search values borrowed retired PSB storage");
}

static void TestBrowseLimitAndCancellation() {
	const auto document = Document();
	sgre::scenario::ScenarioSearchRequest request;
	request.Limit = 1;
	const auto first = sgre::scenario::SearchScenarioDocument(document, request);
	Require(first.Matches.size() == 1 && first.Truncated && first.TextRowsRead == 2, "browse did not distinguish a real additional match");
	request.Query = "beta";
	const auto only = sgre::scenario::SearchScenarioDocument(document, request);
	Require(only.Matches.size() == 1 && !only.Truncated, "an exact single match was falsely truncated");
	request.Query = "BETA";
	Require(sgre::scenario::SearchScenarioDocument(document, request).Matches.empty(), "UTF-8 byte search silently folded case");
	request.Cancelled = [] {
		return true;
	};
	const auto before = sgre::scenario::SearchScenarioDocument(document, request);
	Require(before.Cancelled && before.EntriesRead == 0 && before.Matches.empty(), "initial cancellation still traversed metadata");
	std::size_t calls = 0;
	request.Query.clear();
	request.Limit = 50;
	request.Cancelled = [&calls] {
		return ++calls == 4;
	};
	const auto during = sgre::scenario::SearchScenarioDocument(document, request);
	Require(during.Cancelled && during.Matches.size() == 1 && during.TextRowsRead == 1, "record cancellation did not preserve only completed owned results");
}

static void TestOriginalSnapshotSelection() {
	auto document = Document();
	auto& scenes = Scenes(document);
	Member(scenes[0], "lines") = Value(sgre::resource::PsbArray{Snapshot(4, 10), Snapshot(5, 25), Snapshot(6, 15), Snapshot(7, 28)});
	const auto zero = sgre::scenario::ValidateScenarioJump(document, "FIXTURE.ks", 0);
	Require(zero.Storage == "FIXTURE.ks" && zero.CanonicalStorage == "fixture.ks" && zero.SourceLine == 0 && zero.SnapshotLine == 10 && zero.Point == 4 && zero.SnapshotIndex == 0, "validation changed requested spelling or rejected original line-zero selection");
	const auto repeated = sgre::scenario::ValidateScenarioJump(document, "fixture.ks", 16);
	Require(repeated.SceneLabel == "*opening" && repeated.SnapshotLine == 25 && repeated.Point == 5 && repeated.SnapshotIndex == 1, "decreasing snapshots changed the first original qualifying record");
	const auto later = sgre::scenario::ValidateScenarioJump(document, "fixture.ks", 30);
	Require(later.SceneLabel == "*later" && later.SnapshotLine == 30 && later.Point == 8, "scene boundary did not use next firstLine strictly");
	Reject([&] {
		static_cast<void>(sgre::scenario::ValidateScenarioJump(document, "fixture.ks", 29));
	},
	    "no following original snapshot");
	Reject([&] {
		static_cast<void>(sgre::scenario::ValidateScenarioJump(document, "other.ks", 10));
	},
	    "actual parsed resource identity");
	Reject([&] {
		static_cast<void>(sgre::scenario::ValidateScenarioJump(document, "fixture.ks", -1));
	},
	    "nonnegative");
	for (const auto& storage : {"../fixture.ks", "x/fixture.ks", "x\\fixture.ks", "x:10.ks", "fixture.KS"}) {
		Reject([&] {
			static_cast<void>(sgre::scenario::ValidateScenarioJump(document, storage, 10));
		},
		    "basename");
	}
}

static void TestSharedSceneBoundary() {
	auto document = Document();
	Member(Scenes(document)[0], "lines") = Value(sgre::resource::PsbArray{Snapshot(4, 10), Number(1), Snapshot(7, 30), Number(2)});
	sgre::scenario::ScenarioSearchRequest request;
	request.Query = "delta";
	const auto found = sgre::scenario::SearchScenarioDocument(document, request);
	Require(found.Matches.size() == 1, "boundary text disappeared from catalog search");
	const auto& match = found.Matches.front();
	Require(match.SourceLine == 30 && match.JumpLine == 29 && match.JumpTarget == "fixture.ks:29" && match.SceneLabel == "*opening", "boundary search selected the next scene or changed original provenance");
	const auto jump = sgre::scenario::ValidateScenarioJump(document, match.Storage, match.JumpLine);
	Require(jump.SceneLabel == match.SceneLabel && jump.Point == 7 && jump.SnapshotLine == 30, "boundary search target cannot restore its containing scene");
}

static void TestTextReferencesAndSchemaFailures() {
	sgre::scenario::ScenarioSearchRequest request;
	{
		auto document = Document();
		auto& lines = std::get<sgre::resource::PsbArray>(Member(Scenes(document)[0], "lines").Data);
		lines[1] = Number(0);
		Reject([&] {
			static_cast<void>(sgre::scenario::SearchScenarioDocument(document, request));
		},
		    "one-based");
		lines[1] = Number(3);
		Reject([&] {
			static_cast<void>(sgre::scenario::SearchScenarioDocument(document, request));
		},
		    "one-based");
		lines = {Number(1)};
		Reject([&] {
			static_cast<void>(sgre::scenario::SearchScenarioDocument(document, request));
		},
		    "preceding original position");
	}
	{
		auto document = Document();
		Member(Scenes(document)[0], "version") = Number(0);
		Reject([&] {
			static_cast<void>(sgre::scenario::SearchScenarioDocument(document, request));
		},
		    "version-one");
		Member(Scenes(document)[0], "version") = Value(1.0);
		Member(Scenes(document)[1], "firstLine") = Number(1);
		Reject([&] {
			static_cast<void>(sgre::scenario::SearchScenarioDocument(document, request));
		},
		    "source order");
	}
	{
		auto document = Document();
		auto& texts = std::get<sgre::resource::PsbArray>(Member(Scenes(document)[0], "texts").Data);
		auto& tuple = std::get<sgre::resource::PsbArray>(texts[0].Data);
		auto& variants = std::get<sgre::resource::PsbArray>(tuple[1].Data);
		variants.pop_back();
		Reject([&] {
			static_cast<void>(sgre::scenario::SearchScenarioDocument(document, request));
		},
		    "language count");
	}
	request.Language = "unknown";
	Reject([&] {
		static_cast<void>(sgre::scenario::SearchScenarioDocument(Document(), request));
	},
	    "not declared");
	request.Language = "en";
	request.Query = std::string("a\0b", 3);
	Reject([&] {
		static_cast<void>(sgre::scenario::SearchScenarioDocument(Document(), request));
	},
	    "contains NUL");
}

static void TestBoundedWorkAndResults() {
	const auto document = Document();
	sgre::scenario::ScenarioSearchRequest request;
	request.Query = "beta";
	sgre::scenario::ScenarioSearchLimits limits;
	limits.MaxQueryBytes = 2;
	Reject([&] {
		static_cast<void>(sgre::scenario::SearchScenarioDocument(document, request, limits));
	},
	    "query exceeds");
	limits = {};
	limits.MaxScenes = 1;
	Reject([&] {
		static_cast<void>(sgre::scenario::SearchScenarioDocument(document, request, limits));
	},
	    "scene limit");
	limits = {};
	limits.MaxRecords = 2;
	Reject([&] {
		static_cast<void>(sgre::scenario::SearchScenarioDocument(document, request, limits));
	},
	    "record limit");
	limits = {};
	limits.MaxTextBytes = 3;
	Reject([&] {
		static_cast<void>(sgre::scenario::SearchScenarioDocument(document, request, limits));
	},
	    "bounded string");
	limits = {};
	limits.MaxResultBytes = 3;
	Reject([&] {
		static_cast<void>(sgre::scenario::SearchScenarioDocument(document, request, limits));
	},
	    "owned result byte limit");
	limits = {};
	limits.MaxDecodedTotalBytes = 3;
	Reject([&] {
		static_cast<void>(sgre::scenario::SearchScenarioDocument(document, request, limits));
	},
	    "compared-text byte limit");
	limits = {};
	limits.MaxResults = 1;
	Reject([&] {
		static_cast<void>(sgre::scenario::SearchScenarioDocument(document, request, limits));
	},
	    "result limit");
}

class ConfigFixture {
public:
	ConfigFixture() {
		const auto token = std::chrono::steady_clock::now().time_since_epoch().count();
		Root = std::filesystem::temp_directory_path() / ("sgre-scenario-config-" + std::to_string(token));
		Require(std::filesystem::create_directory(Root), "scenario config fixture directory already exists");
		std::filesystem::create_directories(Root / "Files");
		std::filesystem::create_directory(Root / "installation");
	}
	~ConfigFixture() {
		std::error_code error;
		std::filesystem::remove_all(Root, error);
	}
	std::filesystem::path Root;
};

static void TestReadOnlyConfigurationResolution() {
	ConfigFixture fixture;
	sgre::app::LaunchConfiguration config;
	config.Installation = fixture.Root / "installation";
	config.SpriteShader = fixture.Root / "sprite.metal";
	config.PresentationShader = fixture.Root / "presentation.metal";
	config.SaveDirectory = fixture.Root / "saves";
	config.SeedFile = fixture.Root / "seed.txt";
	const auto serialized = sgre::app::SerializeLaunchConfiguration(config);
	{
		std::ofstream output(fixture.Root / "Files/config.json", std::ios::binary);
		output << serialized;
		Require(static_cast<bool>(output), "scenario config fixture could not be written");
	}
	{
		std::ofstream output(fixture.Root / "seed.txt", std::ios::binary);
		output << "synthetic-test-seed\r\n";
		Require(static_cast<bool>(output), "scenario seed fixture could not be written");
	}
	const auto location = sgre::scenario::ResolveScenarioSearchLocation(fixture.Root);
	Require(location.Installation == std::filesystem::canonical(fixture.Root / "installation") && location.ArchiveSeed == "synthetic-test-seed", "read-only existing config location/seed resolution differs");
	std::ifstream input(fixture.Root / "Files/config.json", std::ios::binary);
	const std::string after((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
	Require(after == serialized && !std::filesystem::exists(fixture.Root / "Files/config.last-good.json"), "inspector rewrote or repaired launch settings");
	std::size_t count = 0;
	for (const auto& entry : std::filesystem::directory_iterator(fixture.Root / "Files")) {
		static_cast<void>(entry);
		++count;
	}
	Require(count == 1, "inspector created a configuration backup/state file");
}

static void TestSingleVariantOriginalFallback() {
	auto document = Document();
	auto& texts = std::get<sgre::resource::PsbArray>(Member(Scenes(document)[0], "texts").Data);
	for (auto& text : texts) {
		auto& tuple = std::get<sgre::resource::PsbArray>(text.Data);
		std::get<sgre::resource::PsbArray>(tuple[1].Data).resize(1);
	}
	for (const auto language : {"ja", "en", "tc", "sc"}) {
		sgre::scenario::ScenarioSearchRequest request;
		request.Language = language;
		request.Query = "Original";
		const auto result = sgre::scenario::SearchScenarioDocument(document, request);
		Require(result.Matches.size() == 2 && result.Matches[0].OriginalText == "Original alpha" && result.Matches[0].Translation == "Original alpha" && result.Matches[0].Character == "Speaker", "singleton variants must retain original base text/name for every requested language");
		Require(result.Matches[0].SourceLine == 10 && result.Matches[0].JumpTarget == "fixture.ks:10" && !result.Matches[0].TranslatedSourceLine, "singleton fallback invented translated source metadata or changed original jump identity");
		request.Query = "beta";
		Require(sgre::scenario::SearchScenarioDocument(document, request).Matches.empty(), "singleton fallback searched an absent translated variant");
	}
	auto& tuple = std::get<sgre::resource::PsbArray>(texts[0].Data);
	std::get<sgre::resource::PsbArray>(tuple[1].Data).clear();
	Reject([&] {
		static_cast<void>(sgre::scenario::SearchScenarioDocument(document, {}));
	},
	    "language count");
}

static void TestCompleteTestInventory() {
	auto document = Document();
	auto& first = Member(Scenes(document)[0], "lines");
	first = Value(sgre::resource::PsbArray{Snapshot(1, 10), Snapshot(2, 20), Snapshot(3, 20), Snapshot(4, 15), Snapshot(5, 30), Snapshot(6, 100)});
	auto& object = std::get<sgre::resource::PsbObject>(Scenes(document)[0].Data);
	object.emplace_back("nexts", Value(sgre::resource::PsbArray{Value(sgre::resource::PsbObject{{"type", Number(0)}, {"storage", String("fixture.ks")}, {"target", String("*later")}, {"eval", String("fixtureCondition")}}), Value(sgre::resource::PsbObject{{"type", Number(1)}})}));
	const auto inventory = sgre::scenario::ReadScenarioTestDocument(document);
	Require(inventory.size() == 2 && inventory[0].Snapshots.size() == 6 && inventory[0].Exits.size() == 1 && inventory[0].TerminalExits == 1 && inventory[0].Exits[0].Condition == "fixtureCondition", "test inventory lost a scene, duplicate/decreasing record, predicate or terminal control");
	Require(inventory[0].Snapshots[0].JumpLine == 10 && inventory[0].Snapshots[1].JumpLine == 20 && !inventory[0].Snapshots[2].JumpLine && !inventory[0].Snapshots[3].JumpLine && inventory[0].Snapshots[4].JumpLine == 29 && !inventory[0].Snapshots[5].JumpLine, "test inventory fabricated addressability for repeated/decreasing lines or crossed a scene boundary");
	for (const auto& scene : inventory) {
		for (const auto& snapshot : scene.Snapshots) {
			if (snapshot.JumpLine) {
				const auto resolved = sgre::scenario::ValidateScenarioJump(document, scene.Storage, *snapshot.JumpLine);
				Require(resolved.SceneLabel == scene.Label && resolved.SnapshotLine == snapshot.Location.SourceLine && resolved.Point == snapshot.Location.Point && resolved.SnapshotIndex == static_cast<std::size_t>(snapshot.Location.Record), "test target does not resolve to its exact original record");
			}
		}
	}
	Scenes(document).clear();
	Reject([&] {
		static_cast<void>(sgre::scenario::ReadScenarioTestDocument(document));
	},
	    "scene count");
	for (std::int32_t seed = 0; seed < 32; ++seed) {
		auto generated = Document();
		auto& scenes = Scenes(generated);
		scenes.clear();
		for (std::int32_t scene_index = 0; scene_index < 3; ++scene_index) {
			sgre::resource::PsbArray lines;
			for (std::int32_t row = 0; row < 12; ++row) {
				lines.push_back(Snapshot(row, (seed * 7 + row * 13) % 75));
			}
			scenes.push_back(Scene("*generated" + std::to_string(scene_index), scene_index * 20, std::move(lines)));
		}
		const auto plan = sgre::scenario::ReadScenarioTestDocument(generated);
		for (std::int32_t requested = 0; requested < 90; ++requested) {
			const auto selected = requested < 20 ? 0u : requested < 40 ? 1u
			                                                           : 2u;
			const auto& rows = std::get<sgre::resource::PsbArray>(Member(scenes[selected], "lines").Data);
			std::optional<std::size_t> expected;
			for (std::size_t row = 0; row < rows.size(); ++row) {
				const auto& fields = std::get<sgre::resource::PsbArray>(rows[row].Data);
				if (std::get<std::int64_t>(fields[4].Data) >= requested) {
					expected = row;
					break;
				}
			}
			std::optional<std::size_t> actual;
			for (const auto& target : plan[selected].Snapshots) {
				if (target.JumpLine && *target.JumpFirstLine <= requested && requested <= *target.JumpLine) {
					Require(!actual, "inverse jump intervals overlap");
					actual = static_cast<std::size_t>(target.Location.Record);
				}
			}
			Require(actual == expected, "inverse test target inventory differs from independent original-order scan");
		}
	}
}

int main() {
	try {
		TestMultilingualOwnedResults();
		TestBrowseLimitAndCancellation();
		TestOriginalSnapshotSelection();
		TestSharedSceneBoundary();
		TestTextReferencesAndSchemaFailures();
		TestBoundedWorkAndResults();
		TestReadOnlyConfigurationResolution();
		TestSingleVariantOriginalFallback();
		TestCompleteTestInventory();
		std::cout << "9 scenario search groups passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
