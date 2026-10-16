#include "sgre/scene/motion_blank_source.hpp"
#include "sgre/scene/motion_static_source.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

template <typename Operation>
static void RequireFailure(Operation operation) {
	try {
		operation();
	} catch (const std::runtime_error&) {
		return;
	}

	throw std::runtime_error("blank descriptor accepted unsupported or malformed input");
}

static std::uint32_t Word(std::istream& stream) {
	std::uint32_t word = 0;
	for (std::uint32_t index = 0; index < 4; ++index) {
		const auto byte = stream.get();
		Require(byte != std::char_traits<char>::eof(), "short bounded blank rectangle fixture");
		word |= static_cast<std::uint32_t>(static_cast<unsigned char>(byte)) << (index * 8);
	}

	return word;
}

static void Replay(const char* path) {
	std::ifstream input(path, std::ios::binary);
	Require(input.is_open() && Word(input) == 0x3152424d && Word(input) == 11, "wrong bounded blank descriptor native fixture");
	for (std::size_t index = 0; index < 11; ++index) {
		const auto bytes = Word(input);
		Require(bytes > 0 && bytes <= 256, "native blank fixture name exceeds its bounded input");
		std::string icon(bytes, '\0');
		input.read(icon.data(), static_cast<std::streamsize>(icon.size()));
		Require(input.good(), "short bounded native blank descriptor name");
		const auto rectangle = sgre::scene::ReadMotionBlankSourceRectangle(icon);
		Require(static_cast<std::uint32_t>(rectangle.Width) == Word(input) && static_cast<std::uint32_t>(rectangle.Height) == Word(input), "blank signed dimensions differ from original atoi producer");
		Require(std::bit_cast<std::uint32_t>(rectangle.OriginX) == Word(input) && std::bit_cast<std::uint32_t>(rectangle.OriginY) == Word(input), "blank origins differ from original strtod then float32 producer");
		Require(std::bit_cast<std::uint32_t>(rectangle.Resolution) == Word(input), "blank resolution differs from native one");
	}
	Require(input.peek() == std::char_traits<char>::eof(), "extra blank descriptor native fixture bytes");
}

static void TestBindingOwnership() {
	sgre::scene::MotionSourceRegistration registration;
	registration.Project.DocumentInstance = 1;
	registration.Project.RootOffset = 128;
	const std::array registrations{registration};
	const sgre::scene::PreparedMotionSourceRegistry registry(registrations);
	std::size_t texture_queries = 0;
	sgre::scene::MotionStaticSourceServices services;
	services.FindTexture = [&](const sgre::scene::ResolvedMotionIcon&) -> std::optional<sgre::scene::RegisteredMotionSource> {
		++texture_queries;
		throw std::runtime_error("blank descriptor entered unobserved registered texture service");
	};
	sgre::scene::MotionStaticSourceBinding binding(registry, registration.Project, services);
	const std::array<std::string, 4> icons{"32:32:16:16", "32:32:16:16", "1941:5500:970:2750", "16:8:3"};
	std::array<sgre::scene::PreparedMotionFrame, 4> frames;
	for (std::size_t index = 0; index < frames.size(); ++index) {
		frames[index].Hidden = false;
		frames[index].Attributes.Source = "blank";
		frames[index].Attributes.Icon = icons[index];
	}
	Require(binding.Bind(frames[0]) && binding.State().Ready && binding.State().Blank && !binding.State().Binding && !binding.State().Texture, "blank branch did not own its real descriptor without atlas/texture binding");
	const auto* original_name = binding.State().Icon.data();
	Require(binding.State().Blank->Width == 32 && binding.State().Blank->OriginX == 16 && !binding.Bind(frames[1]) && binding.State().Icon.data() == original_name, "equal distinct blank names replaced their actual retained cache");
	frames[2].Hidden = true;
	Require(!binding.Bind(frames[2]) && binding.State().Blank->Height == 32 && binding.State().Icon.data() == original_name, "hidden changed blank frame replaced the retained source descriptor");
	frames[2].Hidden = false;
	Require(binding.Bind(frames[2]) && binding.State().Blank->Width == 1941 && binding.State().Blank->Height == 5500, "visible changed blank frame did not decode its current descriptor");
	sgre::scene::PreparedMotionLayer layer;
	sgre::scene::MotionHierarchyState numeric;
	numeric.Attributes.Hidden = false;
	numeric.Attributes.Opacity = 255;
	numeric.ModulationColor = 0xffffffff;
	Require(!binding.BuildSprite(layer, numeric, 1), "blank source invented a textured or untextured draw command");
	sgre::scene::PreparedMotionFrame missing;
	missing.Hidden = false;
	missing.Attributes.Source = "missing";
	missing.Attributes.Icon = "tile";
	Require(binding.Bind(missing) && !binding.State().Ready && !binding.State().Blank && !binding.State().Binding && !binding.State().Texture, "ordinary failed resolution retained a stale blank descriptor");
	Require(!binding.Bind(missing) && binding.Bind(frames[0]) && binding.State().Ready && binding.State().Blank, "blank cache did not preserve failed names and restore a genuine changed descriptor");
	RequireFailure([&] {
		static_cast<void>(binding.Bind(frames[3]));
	});
	Require(!binding.State().Ready && !binding.State().Blank && !binding.State().Binding && !binding.State().Texture && texture_queries == 0, "malformed blank source exposed partial readiness or entered registered texture service");
}

static void TestBoundedSyntax() {
	const std::array<std::string_view, 12> unsupported{"", "32:32:16", "32:32:16:16:0", "32::16:16", "2147483648:2:0:0", "2:3:inf:0", "2:3:nan:0", "2:3:0x1p2:0", "2:3:1e309:0", "2:3:1e40:0", "2:3:+-1:0", "2:3:1px:0"};
	for (const auto input : unsupported) {
		RequireFailure([&] {
			static_cast<void>(sgre::scene::ReadMotionBlankSourceRectangle(input));
		});
	}
	const std::string oversized(65537, '1');
	RequireFailure([&] {
		static_cast<void>(sgre::scene::ReadMotionBlankSourceRectangle(oversized));
	});
	const std::string nul_terminated("32:32:16:16\0ignored", 19);
	const auto retained = sgre::scene::ReadMotionBlankSourceRectangle(nul_terminated);
	Require(retained.Width == 32 && retained.Height == 32 && retained.OriginX == 16 && retained.OriginY == 16, "blank source changed native C-string termination");
}

int main(int argc, char** argv) {
	try {
		Require(argc == 2, "usage: motion_blank_source_tests bounded_native_fixture");
		Replay(argv[1]);
		TestBindingOwnership();
		TestBoundedSyntax();
		std::cout << "PASS 11 native blank rectangles, cache owner transitions, suppression and bounded syntax\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
