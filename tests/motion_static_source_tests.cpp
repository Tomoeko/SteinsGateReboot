#include "sgre/scene/motion_static_source.hpp"

#include <array>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

static void Require(bool condition, std::string_view context) {
	if (!condition) {
		throw std::runtime_error(std::string(context));
	}
}

template <typename Operation>
static void RequireFailure(Operation operation) {
	try {
		operation();
	} catch (const std::runtime_error&) {
		return;
	}

	throw std::runtime_error("static source binding accepted unsupported state");
}

static constexpr std::array<std::string_view, 4> kNames{"", "a", "b", "missing"};

static sgre::scene::PreparedMotionFrame Frame(std::uint8_t source, std::uint8_t icon, bool hidden = false) {
	sgre::scene::PreparedMotionFrame frame;
	frame.Hidden = hidden;
	frame.Attributes.Source = kNames[source];
	frame.Attributes.Icon = kNames[icon];

	return frame;
}

struct Sources {
	std::vector<sgre::resource::MotionSource> Values;
	std::array<sgre::scene::MotionSourceRegistration, 1> Registrations;
	std::array<sgre::scene::PreparedMotionFrame, 32> Frames;
	std::size_t TextureQueries = 0;
	bool FailTexture = false;

	Sources() {
		for (std::size_t index = 0; index < Frames.size(); ++index) {
			Frames[index] = Frame(static_cast<std::uint8_t>(index / 8), static_cast<std::uint8_t>((index / 2) % 4), index % 2 != 0);
		}

		for (std::size_t index = 0; index < 3; ++index) {
			sgre::resource::MotionSource source;
			source.Name = kNames[index];
			source.Texture.Width = 256;
			source.Texture.Height = 256;
			for (std::size_t icon_index = 0; icon_index < 3; ++icon_index) {
				sgre::resource::MotionIcon icon;
				icon.Name = kNames[icon_index];
				icon.Left = 8;
				icon.Top = 16;
				icon.Width = 32;
				icon.Height = 64;
				icon.OriginX = 3;
				icon.OriginY = 5;
				icon.AttributeFlags = 2;
				source.Icons.push_back(std::move(icon));
			}

			Values.push_back(std::move(source));
		}

		Registrations[0].Project.DocumentInstance = 1;
		Registrations[0].Project.RootOffset = 128;
		Registrations[0].Sources = Values;
	}

	const sgre::scene::PreparedMotionFrame& PreparedFrame(std::uint8_t source, std::uint8_t icon, bool hidden = false) const {
		Require(source < 4 && icon < 4, "fixture prepared frame index exceeds its owned store");

		return Frames[std::size_t(source) * 8 + std::size_t(icon) * 2 + (hidden ? 1 : 0)];
	}

	sgre::scene::MotionStaticSourceServices Services() {
		sgre::scene::MotionStaticSourceServices services;
		services.FindTexture = [this](const sgre::scene::ResolvedMotionIcon& binding) -> std::optional<sgre::scene::RegisteredMotionSource> {
			++TextureQueries;
			if (FailTexture) {
				throw std::runtime_error("synthetic actual texture service failure");
			}

			sgre::scene::RegisteredMotionSource source;
			source.Source = binding.Source;
			source.Slot.Index = 4;
			source.Slot.Generation = 1;
			source.Slot.Owner = this;
			source.Texture.Index = 4;
			source.Texture.Generation = 1;
			source.Texture.Owner = this;

			return source;
		};

		return services;
	}
};

static void TestBindingAndGeometry() {
	Sources sources;
	sgre::scene::PreparedMotionSourceRegistry registry(sources.Registrations);
	sgre::scene::MotionStaticSourceBinding binding(registry, sources.Registrations[0].Project, sources.Services());
	Require(!binding.State().Ready && !binding.Bind(sources.PreparedFrame(0, 0)), "initial empty pair must preserve native not-ready state");
	Require(binding.Bind(sources.PreparedFrame(1, 2)) && binding.State().Ready && sources.TextureQueries == 1, "changed registered source pair must resolve its actual texture");
	Require(!binding.Bind(sources.PreparedFrame(2, 1, true)) && binding.State().Source == "a" && binding.State().Icon == "b", "hidden selected frame must preserve previous source binding");
	Require(!binding.Bind(sources.PreparedFrame(1, 2)) && sources.TextureQueries == 1, "equal source contents must preserve current binding without texture lookup");
	sgre::scene::PreparedMotionLayer layer;
	sgre::scene::MotionHierarchyState state;
	state.Attributes.Hidden = false;
	state.Attributes.Source = "a";
	state.Attributes.Icon = "b";
	const auto command = binding.BuildSprite(layer, state, 1);
	Require(command.has_value() && command->Slot.Index == 4 && command->Texture.Index == 4 && command->Sprite.Source == "a" && command->Sprite.Icon == "b", "static publication value must carry actual resolved resource and texture identities");
	Require(command->Sprite.Vertices[0].Position.X == -3 && command->Sprite.Vertices[0].Position.Y == -5 && command->Sprite.Vertices[3].Position.X == 29 && command->Sprite.Vertices[3].Position.Y == 59, "static bound source must supply actual icon origins and geometry dimensions");
	Require(command->Sprite.Vertices[0].Uv.X == 8.0f / 256 && command->Sprite.Vertices[0].Uv.Y == 16.0f / 256 && command->Sprite.Vertices[3].Uv.X == 40.0f / 256 && command->Sprite.Vertices[3].Uv.Y == 80.0f / 256, "static source must supply actual atlas rectangle and physical dimensions");
	state.Attributes.Hidden = true;
	Require(!binding.BuildSprite(layer, state, 1), "inactive evaluated layer must not publish a bound sprite");
	Require(binding.Bind(sources.PreparedFrame(3, 2)) && !binding.State().Ready && !binding.Bind(sources.PreparedFrame(3, 2)), "failed source names must be cached without unchanged retries");
	RequireFailure([&] {
		sgre::scene::MotionStaticSourceBinding rejected(registry, sources.Registrations[0].Project, {});
	});
	RequireFailure([&] {
		auto blank = Frame(1, 1);
		blank.Attributes.Source = "blank";
		static_cast<void>(binding.Bind(blank));
	});
	sources.FailTexture = true;
	RequireFailure([&] {
		static_cast<void>(binding.Bind(sources.PreparedFrame(2, 2)));
	});
	Require(!binding.State().Ready && !binding.State().Binding && !binding.State().Texture, "failed actual texture service must invalidate borrowed publication state");
	sources.FailTexture = false;
	Require(binding.Bind(sources.PreparedFrame(1, 1)) && binding.State().Ready, "binding guard restores after required lookup failure");
}

static void TestRetainedMeshDescriptor() {
	Sources sources;
	auto& icon = sources.Values[1].Icons[2];
	sgre::resource::PsbValue vertices;
	sgre::resource::PsbChunkReference chunk;
	chunk.Index = 3;
	vertices.Data = chunk;
	sgre::resource::PsbObject mesh;
	mesh.emplace_back("vertices", vertices);
	icon.Mesh.SerializedTag = 33;
	icon.Mesh.SerializedOffset = 512;
	icon.Mesh.Data = std::move(mesh);
	sgre::scene::PreparedMotionSourceRegistry registry(sources.Registrations);
	sgre::scene::MotionStaticSourceBinding binding(registry, sources.Registrations[0].Project, sources.Services());
	Require(binding.Bind(sources.PreparedFrame(1, 2)) && binding.State().Ready, "retained mesh source resolves its actual registered descriptor");
	Require(binding.State().Binding->Icon == &icon, "ordinary source keeps the original icon owner rather than replacing its mesh descriptor");
	sgre::scene::PreparedMotionLayer layer;
	sgre::scene::MotionHierarchyState state;
	state.Attributes.Hidden = false;
	state.Attributes.Source = "a";
	state.Attributes.Icon = "b";
	state.Attributes.UsesDefaultColor = false;
	const auto command = binding.BuildSprite(layer, state, 0);
	Require(command && command->Sprite.Vertices[0].Position.X == -3 && command->Sprite.Vertices[0].Position.Y == -5 && command->Sprite.Vertices[3].Position.X == 29 && command->Sprite.Vertices[3].Position.Y == 59, "undeformed icon mesh retains the native four rectangle positions");
	Require(command->Sprite.Indices == std::array<std::uint16_t, 4>{0, 2, 1, 3} && command->Sprite.TextureAttributes == 2 && command->Sprite.Vertices[0].Uv.X == 8.0f / 256 && command->Sprite.Vertices[3].Uv.Y == 80.0f / 256, "undeformed stored mesh keeps ordinary native strip, attributes and atlas sampling");
	const auto* retained = icon.Mesh.Find("vertices");
	Require(retained && std::get<sgre::resource::PsbChunkReference>(retained->Data).Index == 3 && icon.Mesh.SerializedTag == 33 && icon.Mesh.SerializedOffset == 512, "ordinary sprite publication preserves the original nonempty mesh atom and serialized identity");
	layer.MeshTransform = 1;
	RequireFailure([&] {
		static_cast<void>(binding.BuildSprite(layer, state, 0));
	});
	layer.MeshTransform = 0;
	layer.Type = 2;
	RequireFailure([&] {
		static_cast<void>(binding.BuildSprite(layer, state, 0));
	});
	layer.Type = 0;
	RequireFailure([&] {
		static_cast<void>(sgre::scene::BuildMotionSpriteCommand(layer, state, *binding.State().Binding, 0, true));
	});
}

static void TestTextureLookupReentry() {
	Sources sources;
	sgre::scene::PreparedMotionSourceRegistry registry(sources.Registrations);
	auto services = sources.Services();
	const auto actual_lookup = services.FindTexture;
	sgre::scene::MotionStaticSourceBinding* current = nullptr;
	services.FindTexture = [&](const auto& source) {
		RequireFailure([&] {
			static_cast<void>(current->Bind(sources.PreparedFrame(2, 1)));
		});
		Require(current->State().Source == "a" && current->State().Icon == "b" && !current->State().Ready, "rejected reentry preserves the committed pending pair");

		return actual_lookup(source);
	};
	sgre::scene::MotionStaticSourceBinding binding(registry, sources.Registrations[0].Project, std::move(services));
	current = &binding;
	Require(binding.Bind(sources.PreparedFrame(1, 2)) && binding.State().Ready && sources.TextureQueries == 1, "outer actual texture lookup completes after rejected reentry");
}

static std::uint8_t ReadByte(std::istream& input) {
	const auto value = input.get();
	if (value == std::char_traits<char>::eof()) {
		throw std::runtime_error("native static source fixture is truncated");
	}

	return static_cast<std::uint8_t>(value);
}

static std::uint32_t ReadU32(std::istream& input) {
	std::uint32_t value = 0;
	for (std::uint32_t byte = 0; byte < 4; ++byte) {
		value |= static_cast<std::uint32_t>(ReadByte(input)) << (byte * 8);
	}

	return value;
}

static void ReplayNative(const char* path, bool tween_frames) {
	std::ifstream input(path, std::ios::binary);
	Require(input.good(), "native static source fixture is unavailable");
	for (const auto byte : std::array<std::uint8_t, 4>{'M', 'S', 'B', '1'}) {
		Require(ReadByte(input) == byte, "native static source fixture signature differs");
	}

	const auto cases = ReadU32(input);
	const auto steps = ReadU32(input);
	Require(cases > 0 && cases <= 4096 && steps > 0 && steps <= 4096, "native static source fixture count exceeds its bound");
	Sources sources;
	for (auto& frame : sources.Frames) {
		frame.Tween = tween_frames;
	}
	sgre::scene::PreparedMotionSourceRegistry registry(sources.Registrations);
	for (std::uint32_t case_index = 0; case_index < cases; ++case_index) {
		sgre::scene::MotionStaticSourceBinding binding(registry, sources.Registrations[0].Project, sources.Services());
		for (std::uint32_t step = 0; step < steps; ++step) {
			const auto source = ReadByte(input);
			const auto icon = ReadByte(input);
			const auto hidden = ReadByte(input) != 0;
			const auto cached_source = ReadByte(input);
			const auto cached_icon = ReadByte(input);
			const auto ready = ReadByte(input) != 0;
			const auto changed = ReadByte(input) != 0;
			Require(source < 4 && icon < 4 && cached_source < 4 && cached_icon < 4, "native source fixture name index exceeds its bound");
			Require(binding.Bind(sources.PreparedFrame(source, icon, hidden)) == changed, "native source content cache lookup gate differs");
			const auto& state = binding.State();
			Require(state.Source == kNames[cached_source] && state.Icon == kNames[cached_icon] && state.Ready == ready && state.Binding.has_value() == ready && state.Texture.has_value() == ready, "native cached source/icon/readiness differs");
		}
	}

	Require(input.peek() == std::char_traits<char>::eof(), "native static source fixture has trailing bytes");
	std::cout << "Replayed " << cases * steps << " native static source transitions\n";
}

int main(int argc, char** argv) {
	try {
		Require(argc == 1 || argc == 2, "usage: motion_static_source_tests [native-static-source-fixture]");
		TestBindingAndGeometry();
		TestRetainedMeshDescriptor();
		TestTextureLookupReentry();
		if (argc == 2) {
			ReplayNative(argv[1], false);
			ReplayNative(argv[1], true);
		}

		std::cout << "motion static source tests passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
