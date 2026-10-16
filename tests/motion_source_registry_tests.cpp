#include "sgre/scene/motion_source_registry.hpp"

#include "sgre/scene/motion_grid_draw.hpp"

#include <array>
#include <functional>
#include <iostream>
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
		Require(std::string_view(error.what()).find(context) != std::string_view::npos, "source registry error lost its context");

		return;
	}

	throw std::runtime_error("invalid source registry input was accepted");
}

static sgre::resource::MotionSource MakeSource(std::string name, std::span<const std::string_view> icons) {
	sgre::resource::MotionSource source;
	source.Name = std::move(name);
	for (const auto icon_name : icons) {
		sgre::resource::MotionIcon icon;
		icon.Name = icon_name;
		source.Icons.push_back(std::move(icon));
	}

	return source;
}

static sgre::scene::MotionSourceAlias MakeAlias(std::string key, std::string source, std::string icon) {
	sgre::scene::MotionSourceAlias alias;
	alias.Key = std::move(key);
	alias.Source = std::move(source);
	alias.Icon = std::move(icon);

	return alias;
}

static sgre::scene::MotionSourceRegistration MakeRegistration(std::uint64_t document, std::span<const sgre::resource::MotionSource> sources, std::span<const sgre::scene::MotionSourceAlias> aliases = {}) {
	sgre::scene::MotionSourceRegistration registration;
	registration.Project.DocumentInstance = document;
	registration.Project.RootOffset = 128;
	registration.Sources = sources;
	registration.Aliases = aliases;

	return registration;
}

static void TestProjectScopeAndOrder() {
	const std::array<std::string_view, 2> icons = {"icon", "Icon"};
	const std::array first_sources = {MakeSource("page", icons)};
	const std::array second_sources = {MakeSource("page", icons)};
	const std::array registrations = {MakeRegistration(11, first_sources), MakeRegistration(22, second_sources)};
	const sgre::scene::PreparedMotionSourceRegistry registry(registrations);
	const auto global = registry.Resolve("page", "icon");
	Require(global.has_value() && global->RegistrationIndex == 0 && global->Source == &first_sources[0] && !global->ProjectLocal, "global lookup did not retain registration order");
	const auto local = registry.Resolve("page", "icon", registrations[1].Project);
	Require(local.has_value() && local->Project == registrations[1].Project && local->Source == &second_sources[0] && local->Icon == &second_sources[0].Icons[0] && local->ProjectLocal, "lookup crossed requesting document scope");

	const auto uppercase = registry.Resolve("page", "Icon", registrations[1].Project);
	Require(uppercase.has_value() && uppercase->Icon == &second_sources[0].Icons[1], "icon lookup changed case");
	Require(!registry.Resolve("Page", "icon").has_value(), "source lookup changed case");
	Require(!registry.Resolve("page", "ICON").has_value(), "icon lookup folded case");
	Require(!registry.Resolve("page", "missing").has_value(), "missing icon received a fallback asset");

	auto different_root = registrations[1].Project;
	++different_root.RootOffset;
	const auto fallback = registry.Resolve("page", "icon", different_root);
	Require(fallback.has_value() && fallback->RegistrationIndex == 0 && !fallback->ProjectLocal, "root identity was omitted from project matching");
	const sgre::scene::PreparedMotionSourceRegistry empty({});
	Require(!empty.Resolve("page", "icon").has_value(), "empty registry resolved an icon");
}

static void TestAliasTraversal() {
	const std::array<std::string_view, 1> icons = {"i0"};
	const std::array first_sources = {MakeSource("p0", icons)};
	const std::array second_sources = {MakeSource("p2", icons)};
	const std::array second_aliases = {MakeAlias("p0/i0", "p1", "i0"), MakeAlias("p1/i0", "p2", "i0")};
	const std::array registrations = {MakeRegistration(1, first_sources), MakeRegistration(2, second_sources, second_aliases)};
	const sgre::scene::PreparedMotionSourceRegistry registry(registrations);
	// Original 0x1405172e0 rewrites p0->p1 locally, then p1->p2 on its fallback visit.
	const auto twice = registry.Resolve("p0", "i0", registrations[1].Project);
	Require(twice.has_value() && twice->Source == &second_sources[0] && !twice->ProjectLocal, "failed local alias rewrite did not survive fallback");
	const auto once = registry.Resolve("p1", "i0", registrations[1].Project);
	Require(once.has_value() && once->Source == &second_sources[0] && once->ProjectLocal, "local alias was not applied before source lookup");
	const auto unscoped = registry.Resolve("p0", "i0");
	Require(unscoped.has_value() && unscoped->Source == &first_sources[0], "global lookup skipped an earlier successful registration");

	const std::array first_aliases = {MakeAlias("p0/i0", "p1", "i0")};
	const std::array third_aliases = {MakeAlias("p1/i0", "p2", "i0")};
	const std::array global_registrations = {MakeRegistration(10, {}, first_aliases), MakeRegistration(20, second_sources, third_aliases)};
	const sgre::scene::PreparedMotionSourceRegistry global_registry(global_registrations);
	const auto chained = global_registry.Resolve("p0", "i0");
	Require(chained.has_value() && chained->RegistrationIndex == 1, "alias rewrite from an empty registration was lost");

	const std::array single_registration = {MakeRegistration(30, second_sources, second_aliases)};
	const sgre::scene::PreparedMotionSourceRegistry single(single_registration);
	Require(!single.Resolve("p0", "i0").has_value(), "one visit recursively followed multiple aliases");
}

struct NativeRegistrationFixture {
	std::uint8_t Source = 0;
	std::uint8_t IconMask = 0;
	std::array<std::uint8_t, 9> Aliases{};
};

struct NativeLookupFixture {
	std::array<NativeRegistrationFixture, 4> Registrations;
	std::uint8_t Request = 0;
	std::uint8_t Source = 0;
	std::uint8_t Icon = 0;
	std::uint8_t ExpectedRegistration = 0;
	std::uint8_t ExpectedIcon = 0;
	bool ExpectedLocal = false;
};

static void TestOriginalLookupVectors() {
	// Original 0x1405172e0 produced these synthetic four-document outcomes.
	// Alias slot source*3+icon stores target_source*3+target_icon, or 255 for no rewrite.
	// Request 4 has no project; request 5 shares document 1 but has a different root.
	const std::array<NativeLookupFixture, 6> fixtures = {{
	    {{{
	         {2, 1, {255, 5, 255, 0, 255, 255, 255, 255, 3}},
	         {2, 2, {255, 255, 255, 255, 255, 255, 7, 1, 255}},
	         {2, 3, {7, 3, 255, 255, 255, 255, 255, 255, 255}},
	         {1, 0, {255, 0, 255, 255, 255, 255, 255, 7, 7}},
	     }},
	        0, 1, 0, 3, 1, false},
	    {{{
	         {0, 4, {255, 255, 255, 255, 255, 255, 8, 255, 255}},
	         {0, 2, {255, 255, 255, 255, 255, 255, 255, 255, 255}},
	         {2, 5, {255, 255, 255, 255, 255, 255, 8, 255, 255}},
	         {0, 5, {255, 6, 255, 255, 255, 7, 255, 255, 255}},
	     }},
	        3, 0, 0, 4, 0, true},
	    {{{
	         {0, 1, {255, 255, 255, 255, 255, 255, 4, 5, 255}},
	         {0, 6, {255, 255, 6, 255, 255, 2, 255, 7, 255}},
	         {0, 5, {255, 8, 0, 255, 5, 255, 255, 255, 255}},
	         {0, 0, {255, 255, 6, 255, 0, 255, 255, 255, 8}},
	     }},
	        0, 1, 0, 0, 255, false},
	    {{{
	         {1, 0, {8, 3, 255, 255, 255, 255, 255, 255, 255}},
	         {2, 2, {255, 255, 255, 255, 255, 255, 255, 255, 255}},
	         {1, 5, {255, 255, 255, 2, 255, 5, 255, 255, 7}},
	         {0, 7, {255, 255, 255, 255, 255, 1, 255, 255, 255}},
	     }},
	        1, 0, 2, 4, 2, false},
	    {{{
	         {0, 7, {255, 255, 255, 6, 255, 255, 255, 255, 255}},
	         {2, 5, {3, 255, 8, 255, 255, 5, 255, 255, 255}},
	         {0, 0, {255, 255, 255, 255, 7, 255, 255, 2, 255}},
	         {2, 2, {255, 255, 255, 255, 255, 255, 255, 255, 255}},
	     }},
	        2, 1, 0, 2, 0, false},
	    {{{
	         {0, 3, {255, 255, 255, 255, 255, 255, 255, 255, 255}},
	         {2, 0, {1, 255, 255, 255, 7, 255, 255, 255, 255}},
	         {2, 2, {255, 255, 3, 1, 255, 7, 255, 255, 255}},
	         {2, 1, {255, 255, 255, 3, 0, 2, 255, 255, 255}},
	     }},
	        0, 1, 2, 3, 1, false},
	}};
	const std::array<std::string, 3> source_names = {"p0", "p1", "p2"};
	const std::array<std::string, 3> icon_names = {"i0", "i1", "i2"};
	for (const auto& fixture : fixtures) {
		std::array<std::vector<sgre::resource::MotionSource>, 4> sources;
		std::array<std::vector<sgre::scene::MotionSourceAlias>, 4> aliases;
		std::array<sgre::scene::MotionSourceRegistration, 4> registrations;
		for (std::size_t index = 0; index < fixture.Registrations.size(); ++index) {
			const auto& original = fixture.Registrations[index];
			sgre::resource::MotionSource source;
			source.Name = source_names[original.Source];
			for (std::size_t icon_index = 0; icon_index < icon_names.size(); ++icon_index) {
				if ((original.IconMask & (1 << icon_index)) != 0) {
					sgre::resource::MotionIcon icon;
					icon.Name = icon_names[icon_index];
					source.Icons.push_back(std::move(icon));
				}
			}

			sources[index].push_back(std::move(source));
			for (std::size_t key = 0; key < original.Aliases.size(); ++key) {
				const auto target = original.Aliases[key];
				if (target != 255) {
					aliases[index].push_back(MakeAlias(source_names[key / 3] + "/" + icon_names[key % 3], source_names[target / 3], icon_names[target % 3]));
				}
			}

			registrations[index] = MakeRegistration(index + 1, sources[index], aliases[index]);
		}

		std::optional<sgre::scene::MotionProjectIdentity> request;
		if (fixture.Request < registrations.size()) {
			request = registrations[fixture.Request].Project;
		} else if (fixture.Request == 5) {
			request = registrations[0].Project;
			++request->RootOffset;
		}

		const sgre::scene::PreparedMotionSourceRegistry registry(registrations);
		const auto result = registry.Resolve(source_names[fixture.Source], icon_names[fixture.Icon], request);
		const std::size_t selected_registration = result ? result->RegistrationIndex + 1 : 0;
		Require(selected_registration == fixture.ExpectedRegistration, "source selection differs from original instructions");
		if (result) {
			Require(result->Icon->Name == icon_names[fixture.ExpectedIcon] && result->ProjectLocal == fixture.ExpectedLocal, "icon or local selection differs from original instructions");
		}
	}
}

static void TestExactAliasKeys() {
	const std::array<std::string_view, 1> icons = {"target"};
	const std::array sources = {MakeSource("destination", icons)};
	const std::array aliases = {MakeAlias("a/b/c", "destination", "target"), MakeAlias("\xc3\xa9/item", "destination", "target"), MakeAlias("/", "destination", "target")};
	const std::array registrations = {MakeRegistration(1, sources, aliases)};
	const sgre::scene::PreparedMotionSourceRegistry registry(registrations);
	Require(registry.Resolve("a/b", "c").has_value() && registry.Resolve("a", "b/c").has_value(), "alias key was split or normalized");
	Require(registry.Resolve("\xc3\xa9", "item").has_value(), "alias lookup changed UTF-8 bytes");
	Require(registry.Resolve("", "").has_value(), "empty names changed native concatenation semantics");
	Require(!registry.Resolve("a\\b", "c").has_value(), "lookup normalized path separators");
}

static void TestResolvedDrawBinding() {
	const std::array<std::string_view, 1> names = {"actual_icon"};
	std::array sources = {MakeSource("actual_page", names)};
	sources[0].Texture.Width = 8;
	sources[0].Texture.Height = 8;
	sources[0].Icons[0].Width = 2;
	sources[0].Icons[0].Height = 2;
	const std::array aliases = {MakeAlias("requested_page/requested_icon", "actual_page", "actual_icon")};
	const std::array registrations = {MakeRegistration(71, sources, aliases)};
	const sgre::scene::PreparedMotionSourceRegistry registry(registrations);
	const auto binding = registry.Resolve("requested_page", "requested_icon", registrations[0].Project);
	Require(binding.has_value(), "draw fixture source did not resolve");
	sgre::scene::MotionHierarchyState state;
	state.Attributes.Hidden = false;
	state.Attributes.Source = "requested_page";
	state.Attributes.Icon = "requested_icon";
	state.Attributes.Colors.fill(0x808080ff);
	const auto material = sgre::scene::EvaluateMotionSpriteMaterial(state, *binding);
	Require(material.has_value() && material->Source == "actual_page" && material->Icon == "actual_icon", "material discarded resolved alias binding");
	RequireFailure([&] {
		static_cast<void>(sgre::scene::EvaluateMotionSpriteMaterial(state, sources[0], sources[0].Icons[0]));
	},
	    "identity");
	sgre::scene::PreparedMotionLayer layer;
	const auto quad = sgre::scene::BuildMotionSpriteCommand(layer, state, *binding, 0, false);
	Require(quad.has_value() && quad->Source == "actual_page" && quad->Vertices[3].Position.X == 2 && quad->Vertices[3].Uv.X == 0.25f, "resolved quad did not use the bound rectangle and texture");

	state.Attributes.Deformation = sgre::scene::IdentityMotionBezierPatch();
	layer.MeshTransform = 1;
	sgre::scene::MotionMeshSubdivision subdivision;
	subdivision.X = 2;
	subdivision.Y = 2;
	std::array<sgre::resource::MotionPoint, 9> positions;
	std::array<sgre::scene::MotionSpriteVertex, 9> vertices;
	std::array<std::uint32_t, 24> indices;
	sgre::scene::MotionGridBuffers buffers;
	buffers.Positions = positions;
	buffers.Vertices = vertices;
	buffers.Indices = indices;
	const auto grid = sgre::scene::BuildOwnMotionGridCommand(layer, state, *binding, subdivision, buffers, 0, false);
	Require(grid.has_value() && grid->Material.Source == "actual_page" && grid->Vertices.back().Position.Y == 2 && grid->Vertices.back().Uv.Y == 0.25f, "resolved grid did not use its bound source");
	const auto packed = sgre::scene::PackMotionGridCommand(layer, state, *binding, subdivision, buffers);
	Require(packed.has_value() && packed->Material.Icon == "actual_icon", "pre-evaluated geometry lost resolved icon binding");
	Require(state.Attributes.Source == "requested_page" && state.Attributes.Icon == "requested_icon", "draw binding overwrote original frame identities");

	const sgre::scene::ResolvedMotionIcon missing;
	RequireFailure([&] {
		static_cast<void>(sgre::scene::EvaluateMotionSpriteMaterial(state, missing));
	},
	    "resolved source binding");
}

static sgre::resource::PsbValue StringValue(std::string text) {
	sgre::resource::PsbValue value;
	value.Data = std::move(text);

	return value;
}

static void TestAliasReaderAndValidation() {
	sgre::resource::PsbDocument document;
	Require(sgre::scene::ReadMotionSourceAliases(document).empty(), "absent alias map was not empty");
	sgre::resource::PsbValue target;
	target.Data = sgre::resource::PsbArray{StringValue("page"), StringValue("icon"), StringValue("ignored")};
	sgre::resource::PsbValue mapping;
	mapping.Data = sgre::resource::PsbObject{{"old/item", target}};
	document.Root.Data = sgre::resource::PsbObject{{"sourceIconRenameMap", mapping}};
	const auto aliases = sgre::scene::ReadMotionSourceAliases(document);
	Require(aliases.size() == 1 && aliases[0].Key == "old/item" && aliases[0].Source == "page" && aliases[0].Icon == "icon", "alias reader changed the first two array entries");

	std::get<sgre::resource::PsbArray>(target.Data).resize(1);
	mapping.Data = sgre::resource::PsbObject{{"old/item", target}};
	document.Root.Data = sgre::resource::PsbObject{{"sourceIconRenameMap", mapping}};
	RequireFailure([&] {
		static_cast<void>(sgre::scene::ReadMotionSourceAliases(document));
	},
	    "requires source and icon");
	document.Root.Data = sgre::resource::PsbObject{{"sourceIconRenameMap", StringValue("bad")}};
	RequireFailure([&] {
		static_cast<void>(sgre::scene::ReadMotionSourceAliases(document));
	},
	    "bounded object");

	const std::array<std::string_view, 1> icons = {"icon"};
	const std::array sources = {MakeSource("page", icons)};
	const std::array duplicate_projects = {MakeRegistration(1, sources), MakeRegistration(1, sources)};
	RequireFailure([&] {
		const sgre::scene::PreparedMotionSourceRegistry invalid(duplicate_projects);
	},
	    "registered once");
	const std::array zero_project = {MakeRegistration(0, sources)};
	RequireFailure([&] {
		const sgre::scene::PreparedMotionSourceRegistry invalid(zero_project);
	},
	    "nonzero");
	const std::array duplicate_sources = {sources[0], sources[0]};
	const std::array source_registration = {MakeRegistration(1, duplicate_sources)};
	RequireFailure([&] {
		const sgre::scene::PreparedMotionSourceRegistry invalid(source_registration);
	},
	    "duplicate source");
	const std::array<std::string_view, 2> repeated_icons = {"icon", "icon"};
	const std::array repeated_source = {MakeSource("page", repeated_icons)};
	const std::array icon_registration = {MakeRegistration(1, repeated_source)};
	RequireFailure([&] {
		const sgre::scene::PreparedMotionSourceRegistry invalid(icon_registration);
	},
	    "duplicate icon");
	const std::array repeated_aliases = {aliases[0], aliases[0]};
	const std::array alias_registration = {MakeRegistration(1, sources, repeated_aliases)};
	RequireFailure([&] {
		const sgre::scene::PreparedMotionSourceRegistry invalid(alias_registration);
	},
	    "duplicate alias");
	const std::array valid_registration = {MakeRegistration(1, sources)};
	const sgre::scene::PreparedMotionSourceRegistry valid(valid_registration);
	RequireFailure([&] {
		static_cast<void>(valid.Resolve(std::string_view("page\0tail", 9), "icon"));
	},
	    "embedded NUL");
	RequireFailure([&] {
		static_cast<void>(valid.Resolve(std::string(65537, 'a'), "icon"));
	},
	    "name limit");
}

int main() {
	try {
		TestProjectScopeAndOrder();
		TestAliasTraversal();
		TestOriginalLookupVectors();
		TestExactAliasKeys();
		TestResolvedDrawBinding();
		TestAliasReaderAndValidation();
		std::cout << "motion source registry tests passed\n";
	} catch (const std::exception& error) {
		std::cerr << "motion source registry tests: " << error.what() << '\n';

		return 1;
	}

	return 0;
}
