#include "sgre/scene/motion_selected_layer_sources.hpp"

#include "sgre/scene/motion_selected_layer_nodes.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

static void Require(bool condition, std::string_view text) {
	if (!condition) {
		throw std::runtime_error(std::string(text));
	}
}

template <typename Operation>
static void RequireFailure(Operation operation, std::string_view message) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(message) != std::string_view::npos, error.what());

		return;
	}

	throw std::runtime_error("foreign or expired selected source owner was accepted");
}

template <typename Value>
static sgre::resource::PsbValue Atom(Value value) {
	sgre::resource::PsbValue result;
	result.Data = std::move(value);

	return result;
}

static std::shared_ptr<sgre::resource::PsbDocument> Document(std::size_t registration, std::size_t case_index) {
	const std::array<std::pair<std::size_t, bool>, 9> sequence = {{{0, false}, {1, false}, {1, false}, {2, false}, {2, false}, {3, true}, {3, false}, {4, false}, {0, false}}};
	std::array<std::pair<std::string, std::string>, 5> pairs = {{{"p2", "i2"}, {"p0", "i0"}, {"missing", "i0"}, {"p1", "i1"}, {case_index == 1 ? "P0" : "p0", "i0"}}};
	sgre::resource::PsbArray frames;
	for (std::size_t index = 0; index < sequence.size(); ++index) {
		const auto [pair, hidden] = sequence[index];
		const auto& names = pairs[pair];
		const auto content = Atom(sgre::resource::PsbObject{{"mask", Atom(std::int64_t{0})}, {"src", Atom(names.first)}, {"icon", Atom(names.second)}});
		frames.push_back(Atom(sgre::resource::PsbObject{{"time", Atom(static_cast<double>(index))}, {"type", Atom(hidden ? std::int64_t{0} : std::int64_t{2})}, {"content", content}}));
	}
	frames.push_back(Atom(sgre::resource::PsbObject{{"time", Atom(9.0)}, {"type", Atom(std::int64_t{0})}}));
	sgre::resource::PsbChunkReference chunk;
	auto texture = Atom(sgre::resource::PsbObject{{"type", Atom(std::string("A8"))}, {"width", Atom(std::int64_t{32})}, {"height", Atom(std::int64_t{32})}, {"truncated_width", Atom(std::int64_t{32})}, {"truncated_height", Atom(std::int64_t{32})}, {"pixel", Atom(chunk)}});
	texture.SerializedOffset = 64;
	const auto icon = Atom(sgre::resource::PsbObject{{"left", Atom(8.0)}, {"top", Atom(16.0)}, {"width", Atom(16.0)}, {"height", Atom(8.0)}, {"originX", Atom(3.0)}, {"originY", Atom(5.0)}, {"attr", Atom(std::int64_t{2})}});
	const auto source = Atom(sgre::resource::PsbObject{{"texture", texture}, {"icon", Atom(sgre::resource::PsbObject{{"i" + std::to_string(registration), icon}})}});
	const auto layer = Atom(sgre::resource::PsbObject{{"label", Atom(std::string("sprite"))}, {"type", Atom(std::int64_t{0})}, {"inheritMask", Atom(std::int64_t{0})}, {"coordinate", Atom(std::int64_t{0})}, {"frameList", Atom(std::move(frames))}, {"transformOrder", Atom(sgre::resource::PsbArray{Atom(std::int64_t{0}), Atom(std::int64_t{3}), Atom(std::int64_t{2}), Atom(std::int64_t{1})})}, {"children", Atom(sgre::resource::PsbArray{})}, {"stencilType", Atom(std::int64_t{0})}, {"meshTransform", Atom(std::int64_t{0})}, {"objTriPriority", Atom(std::int64_t{0})}, {"parameterize", Atom(std::monostate{})}, {"joinTarget", Atom(false)}, {"groundCorrection", Atom(false)}});
	const auto priority = Atom(sgre::resource::PsbObject{{"time", Atom(0.0)}, {"type", Atom(std::int64_t{1})}, {"content", Atom(sgre::resource::PsbArray{Atom(std::int64_t{0})})}});
	const auto terminal = Atom(sgre::resource::PsbObject{{"time", Atom(9.0)}, {"type", Atom(std::int64_t{1})}, {"content", Atom(std::monostate{})}});
	const auto timeline = Atom(sgre::resource::PsbObject{{"type", Atom(std::int64_t{0})}, {"bounds", Atom(sgre::resource::PsbObject{{"left", Atom(0.0)}, {"top", Atom(0.0)}, {"right", Atom(16.0)}, {"bottom", Atom(8.0)}})}, {"lastTime", Atom(9.0)}, {"loopTime", Atom(-1.0)}, {"layer", Atom(sgre::resource::PsbArray{layer})}, {"priority", Atom(sgre::resource::PsbArray{priority, terminal})}, {"tag", Atom(sgre::resource::PsbArray{})}, {"parameter", Atom(std::monostate{})}, {"variable", Atom(std::monostate{})}, {"parameterize", Atom(std::monostate{})}});
	sgre::resource::PsbObject aliases;
	if (registration == 2) {
		aliases.emplace_back("p0/i0", Atom(sgre::resource::PsbArray{Atom(std::string("p1")), Atom(std::string("i1"))}));
	} else if (registration == 0) {
		aliases.emplace_back("p1/i1", Atom(sgre::resource::PsbArray{Atom(std::string("p0")), Atom(std::string("i0"))}));
	}
	auto document = std::make_shared<sgre::resource::PsbDocument>();
	document->Header.Version = 4;
	document->Header.RootOffset = 44;
	document->Header.DataOffset = 128;
	document->SerializedSize = 1152;
	document->Chunks = {{128, 1024}};
	document->Root = Atom(sgre::resource::PsbObject{{"id", Atom(std::string("motion"))}, {"spec", Atom(std::string("win"))}, {"version", Atom(3.03)}, {"label", Atom(std::string("Registered source fixture"))}, {"easing", Atom(sgre::resource::PsbArray{})}, {"source", Atom(sgre::resource::PsbObject{{"p" + std::to_string(registration), source}})}, {"object", Atom(sgre::resource::PsbObject{{"character" + std::to_string(registration), Atom(sgre::resource::PsbObject{{"type", Atom(std::int64_t{0})}, {"motion", Atom(sgre::resource::PsbObject{{"main", timeline}})}})}})}, {"sourceIconRenameMap", Atom(std::move(aliases))}});
	document->Root.SerializedOffset = 44;

	return document;
}

static std::uint32_t Word(std::istream& stream) {
	std::uint32_t word = 0;
	for (std::uint32_t shift = 0; shift < 32; shift += 8) {
		const auto value = stream.get();
		Require(value != std::char_traits<char>::eof(), "short registered source fixture");
		word |= static_cast<std::uint32_t>(static_cast<std::uint8_t>(value)) << shift;
	}

	return word;
}

static void Replay(const char* path) {
	std::ifstream stream(path, std::ios::binary);
	Require(stream.is_open() && Word(stream) == 0x3153524d, "wrong registered source fixture");
	const auto cases = Word(stream);
	const auto steps = Word(stream);
	Require(cases == 2 && steps == 9, "registered source fixture exceeds fixed bounds");
	for (std::size_t case_index = 0; case_index < cases; ++case_index) {
		std::array<std::size_t, 3> backend_owners{};
		std::size_t uploaded = 0;
		sgre::scene::TextureCache cache([&](auto, const auto&) {
			Require(uploaded < backend_owners.size(), "registered fixture imported extra textures");
			sgre::render::SpriteTextureHandle handle;
			handle.Owner = &backend_owners[uploaded++];
			handle.Generation = 1;

			return handle;
		},
		    [](auto) {
		    });
		sgre::scene::MotionAssetCatalog catalog(cache);
		sgre::scene::MotionResourceManager manager(cache);
		std::array<std::shared_ptr<const sgre::scene::LoadedMotionAsset>, 3> assets;
		std::array<sgre::scene::MotionProjectId, 3> projects;
		for (std::size_t index = 0; index < assets.size(); ++index) {
			assets[index] = catalog.Prepare(Document(index, case_index), std::make_shared<const std::vector<std::uint8_t>>(1152, 128));
			projects[index] = manager.Register(assets[index]->Identity, assets[index]->Model, *assets[index]->Backing);
		}
		sgre::scene::MotionPlayerCommands commands(manager);
		commands.SetCharacter("character2");
		commands.Play("main");
		const auto& timeline = *commands.CachedTimeline();
		const auto prepared = sgre::scene::PrepareMotionTimeline(timeline, *assets[2]->Model);
		sgre::scene::MotionRootScene root;
		const sgre::scene::MotionPlayerRenderControls render;
		sgre::scene::MotionLayerIdentifierPool identifiers;
		sgre::scene::MotionSelectedLayerNodes selected(commands, root, render, timeline, prepared, identifiers, {}, {});
		RequireFailure([&] {
			sgre::scene::MotionSelectedLayerSources foreign(selected, catalog, [&](auto) -> const sgre::scene::LoadedMotionAsset& {
				return *assets[0];
			});
		},
		    "exact registered loaded asset");

		const auto copied_asset = *assets[2];
		Require(!catalog.ContainsPrepared(copied_asset), "an unbound copied asset must not satisfy catalog provenance");
		RequireFailure([&] {
			sgre::scene::MotionSelectedLayerSources copied(selected, catalog, [&](auto project) -> const sgre::scene::LoadedMotionAsset& {
				return project == projects[2] ? copied_asset : *assets[static_cast<std::size_t>(project.Value)];
			});
		},
		    "exact registered loaded asset");
		const auto read_asset = [&](auto project) -> const sgre::scene::LoadedMotionAsset& {
			for (std::size_t index = 0; index < projects.size(); ++index) {
				if (project == projects[index]) {
					return *assets[index];
				}
			}
			throw std::runtime_error("requested asset is not registered in this actual owner");
		};
		const auto refs_before = assets[2].use_count();
		const auto model_refs_before = assets[2]->Model.use_count();
		sgre::scene::MotionSelectedLayerSources sources(selected, catalog, read_asset);
		Require(assets[2].use_count() == refs_before && assets[2]->Model.use_count() == model_refs_before && sources.RequestingProject() == projects[2], "selected source borrow must preserve exact requester without asset retain");
		RequireFailure([&] {
			sgre::scene::MotionSelectedLayerSources duplicate(selected, catalog, read_asset);
		},
		    "exclusive actual registered asset");
		sgre::scene::MotionLocalFrames local(selected, *assets[2]->Model, sources.FrameServices([](auto, auto) {
			throw std::runtime_error("action-free registered source fixture queued an action");
		}));
		RequireFailure([&] {
			static_cast<void>(sources.State(0));
		},
		    "completed current local cursor");
		std::array<std::pair<std::string, std::string>, 5> pairs = {{{"p2", "i2"}, {"p0", "i0"}, {"missing", "i0"}, {"p1", "i1"}, {case_index == 1 ? "P0" : "p0", "i0"}}};
		for (std::size_t step = 0; step < steps; ++step) {
			const auto pair = Word(stream);
			const bool hidden = Word(stream) != 0;
			const auto lookups = Word(stream);
			const bool ready = Word(stream) != 0;
			const auto handle = Word(stream);
			const auto resolved = Word(stream);
			const auto cached = Word(stream);
			Require(Word(stream) == (ready ? 1u : 0u) && pair < pairs.size() && cached < pairs.size() && lookups <= 1, "native registered source observation exceeds scope");
			local.Select(sgre::scene::MotionFrameTraversal::Seek, static_cast<float>(step));
			const auto& state = sources.State(0);
			Require(prepared.Layers[0].Frames[step].Hidden == hidden && state.Ready == ready && state.Source == pairs[cached].first && state.Icon == pairs[cached].second, "actual selected registered source cache differs from native");
			if (ready) {
				Require(state.Binding && state.Texture && sources.ResolvedProject(0) == projects.at(handle - 1) && state.Binding->Source == &assets.at(handle - 1)->Model->Sources[0] && state.Texture->Source == state.Binding->Source, "actual resolved source/texture registration differs from native");
				if (resolved != 0xffffffffu) {
					Require(resolved + 1 == handle, "native resolved document and texture handle disagree");
				}
			} else {
				Require(!state.Binding && !state.Texture && !sources.ResolvedProject(0), "failed native source lookup must not create readiness");
			}
		}
		Require(manager.Unregister(projects[1]), "fixture must actually retire an alternate source registration");
		RequireFailure([&] {
			static_cast<void>(sources.State(0));
		},
		    "registration order changed");
		RequireFailure([&] {
			local.Select(sgre::scene::MotionFrameTraversal::Seek, 8.0f);
		},
		    "registration order changed");

		commands.SetCharacter("character0");
		commands.Play("main");
		RequireFailure([&] {
			static_cast<void>(sources.State(0));
		},
		    "exact live requesting project");
		commands.SetCharacter("character2");
		commands.Play("main");
		Require(selected.Nodes().empty(), "successful reselection must not revive the previous selected tree");
		RequireFailure([&] {
			static_cast<void>(sources.State(0));
		},
		    "actual registered character/timeline");
		RequireFailure([&] {
			local.Select(sgre::scene::MotionFrameTraversal::Seek, 8.0f);
		},
		    "actual live selected raw/prepared owner");
		selected.Retire();
		RequireFailure([&] {
			static_cast<void>(sources.RequestingProject());
		},
		    "exact live requesting project");
	}
	Require(stream.peek() == std::char_traits<char>::eof(), "registered source fixture has trailing bytes");
	std::cout << cases * steps << " actual selected registered source bindings matched\n";
}

int main(int argc, char** argv) {
	try {
		Require(argc == 2, "usage: motion_selected_layer_sources_tests native_fixture");
		Replay(argv[1]);

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
