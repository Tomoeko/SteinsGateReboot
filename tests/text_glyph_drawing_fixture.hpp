#pragma once

#include "sgre/scene/motion_assets.hpp"
#include "sgre/script/layer_bindings.hpp"
#include "sgre/script/text_render_bindings.hpp"

#include "vector_font_fixture.hpp"

#include <array>
#include <bit>
#include <functional>
#include <initializer_list>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace text_drawing_test {

	inline sgre::script::SquirrelVmLimits VmLimits() {
		sgre::script::SquirrelVmLimits result;
		result.ReclaimObjects = true;
		return result;
	}

	struct Fixture {
		sgre::script::SquirrelVm Vm{VmLimits()};
		sgre::script::ScriptObjectBindings Objects{Vm};
		sgre::script::ScriptPsbBindings Psb{Vm, Objects};
		std::shared_ptr<const std::vector<std::uint8_t>> FontBytes = std::make_shared<const std::vector<std::uint8_t>>(vector_font_test::MakeFont());
		sgre::script::ScriptVectorFontBindings Vectors;
		sgre::scene::BitmapFontAssetCatalog Fonts;
		sgre::scene::MotionAssetCatalog Motions;
		sgre::scene::LayerTree Tree{16};
		sgre::script::ScriptLayerBindings Layers{Vm, Objects, Tree};
		sgre::script::ScriptTextRenderBindings Text;
		std::vector<sgre::script::SquirrelHostRoot> Roots;
		sgre::script::SquirrelValue Source;
		sgre::script::SquirrelValue Layer;
		sgre::script::SquirrelValue Instance;
		std::function<void()> Finding;
		std::size_t FindCalls = 0;

		Fixture(sgre::scene::TextureCache& cache, sgre::scene::MutableTextureBackend& backend, sgre::script::ScriptTextRenderBindings::VectorGlyphDrawing draw, sgre::scene::LayerCameraDrawScope* camera = nullptr)
		    : Vectors(Vm, Objects, [this](auto, auto) {
			      return [this]() -> std::optional<sgre::script::ScriptResourceAsset> {
				      sgre::script::ScriptResourceAsset asset;
				      asset.Kind = sgre::script::ScriptResourceDataKind::Binary;
				      asset.Backing = FontBytes;
				      return asset;
			      };
		      },
		          [](auto, auto) {
		          }),
		      Fonts(cache), Motions(cache), Text(Vm, Objects, Layers, Tree, backend) {
			Layers.ConfigureMotionOwners(Psb, Motions, cache);
			Text.ConfigureFontOwners(Psb, Vectors, Fonts, cache);
			Text.ConfigureGlyphDrawing(std::move(draw));
			if (camera) {
				Text.ConfigureFrameOwners(*camera);
			}
			Source = Construct(Vectors.VectorFontClass(), {std::string("authored-rectangle.otf")});
			const std::array<sgre::script::SquirrelValue, 1> source_receiver{Source};
			if (std::get<bool>(Vm.Invoke(Vm.GetSlot(Source, std::string("getLoading")), source_receiver).Value)) {
				throw std::runtime_error("authored CFF provider failed its actual resource completion");
			}
			const auto derived = Keep(Vm.CreateClass(Text.TextRenderClass()));
			Vm.NewSlot(derived, std::string("findFont"), Vm.CreateNativeClosure("glyph_drawing_actual_findFont", [this](auto&, auto) {
				++FindCalls;
				if (Finding) {
					Finding();
				}
				return sgre::script::SquirrelNativeResult::ReturnValue(Source);
			}));
			Layer = Construct(Layers.LayerClass());
			Instance = Construct(derived, {Layer});
			Call("setDefault", {Properties({{"fontsize", std::int32_t{8}}, {"rubysize", std::int32_t{4}}, {"linesize", std::int32_t{8}}, {"linespacing", std::int32_t{0}}, {"shadow", false}, {"color", std::bit_cast<std::int32_t>(std::uint32_t{0xff204080})}})});
			Call("resetFont");
			Call("resetStyle");
			Call("setVisible", {true});
		}

		~Fixture() {
			Vm.Close();
		}

		sgre::script::SquirrelValue Keep(const sgre::script::SquirrelValue& value) {
			Roots.push_back(Vm.Retain(value));
			return value;
		}

		sgre::script::SquirrelValue Construct(const sgre::script::SquirrelValue& cls, std::vector<sgre::script::SquirrelValue> arguments = {}) {
			arguments.insert(arguments.begin(), Vm.RootTable());
			return Keep(Vm.Invoke(cls, arguments).Value);
		}

		sgre::script::SquirrelValue Properties(std::initializer_list<std::pair<std::string_view, sgre::script::SquirrelValue>> fields) {
			const auto table = Keep(Vm.CreateTable());
			for (const auto& [name, value] : fields) {
				Vm.NewSlot(table, std::string(name), value);
			}
			return table;
		}

		sgre::script::SquirrelValue Call(std::string_view method, std::vector<sgre::script::SquirrelValue> arguments = {}) {
			arguments.insert(arguments.begin(), Instance);
			return Vm.Invoke(Vm.GetSlot(Instance, std::string(method)), arguments).Value;
		}

		void Render(std::string value = "BB") {
			if (!std::get<bool>(Call("render", {std::move(value), std::int32_t{0}, std::int32_t{0}, std::int32_t{0}, false}))) {
				throw std::runtime_error("authored Text.render unexpectedly overflowed");
			}
			Call("done");
		}

		void Draw() {
			Layers.MotionTasks(Layer).Group(sgre::scene::MotionTaskGroup::First).Draw(0);
		}
	};

} // namespace text_drawing_test
