#include "sgre/platform/host_locale.hpp"
#include "sgre/resource/archive_mounts.hpp"
#include "sgre/resource/archive_reader.hpp"
#include "sgre/resource/resource_cache.hpp"
#include "sgre/scene/bitmap_font_assets.hpp"
#include "sgre/script/affine_layer_bindings.hpp"
#include "sgre/script/affine_matrix_bindings.hpp"
#include "sgre/script/draw_object_bindings.hpp"
#include "sgre/script/font_bindings.hpp"
#include "sgre/script/game_bindings.hpp"
#include "sgre/script/image_bindings.hpp"
#include "sgre/script/indicator_bindings.hpp"
#include "sgre/script/input_bindings.hpp"
#include "sgre/script/layer_bindings.hpp"
#include "sgre/script/layer_draw_bindings.hpp"
#include "sgre/script/layer_picture_bindings.hpp"
#include "sgre/script/layer_raw_texture_bindings.hpp"
#include "sgre/script/motion_bindings.hpp"
#include "sgre/script/motion_resource_owner.hpp"
#include "sgre/script/native_format.hpp"
#include "sgre/script/native_math.hpp"
#include "sgre/script/object_bindings.hpp"
#include "sgre/script/psb_bindings.hpp"
#include "sgre/script/raw_image_bindings.hpp"
#include "sgre/script/raw_texture_bindings.hpp"
#include "sgre/script/resource_bindings.hpp"
#include "sgre/script/screen_bindings.hpp"
#include "sgre/script/system_bindings.hpp"
#include "sgre/script/text_render_bindings.hpp"
#include "sgre/script/thread_bindings.hpp"

#ifdef SGRE_HAS_MACOS_RENDERER
#include "sgre/render/sprite_pass.hpp"
#endif

#include "motion_startup_observation.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

int main(int argc, char** argv) {
	const bool observe_motion = argc == 5 && std::string_view(argv[4]) == "--observe-motion-registration";
	const bool platform_requested = argc == 4 || observe_motion;
	try {
		if (argc != 3 && argc != 4 && !observe_motion) {
			throw std::runtime_error("local_script_startup_tests GAME_ROOT SEED_FILE [SPRITE_SHADER [--observe-motion-registration]]");
		}

		std::ifstream seed_input(argv[2]);
		std::string seed;
		std::getline(seed_input, seed);
		if (!seed.empty() && seed.back() == '\r') {
			seed.pop_back();
		}

		const auto data = std::filesystem::path(argv[1]) / "wind3d11data";
		sgre::resource::ArchiveReader scripts(data / "script_info.psb.m", data / "script_body.bin", seed);
		sgre::resource::ArchiveMounts mounts;
		for (const auto name : {"config", "font", "image", "map", "motion", "scenario", "script", "sound", "voice"}) {
			const auto index = std::string(name) + "_info.psb.m";
			const auto body = std::string(name) + "_body.bin";
			sgre::resource::ArchiveReader prepared(data / index, data / body, seed);
			mounts.Prepare(index, body, prepared.Index());
		}
		sgre::script::SquirrelVm vm;
		sgre::resource::ResourceCache resources(8 * 1024 * 1024);
		std::map<std::string, sgre::script::ScriptResourceAsset, std::less<>> config_assets;
		sgre::resource::ArchiveReader config(data / "config_info.psb.m", data / "config_body.bin", seed);
		for (const auto& entry : config.Index().Entries) {
			if (entry.Name == "license.txt.m") {
				// This original raw-text identity survives the PSB-only expired suffix list.
				continue;
			}

			const auto name = "config/" + entry.Name + ".psb";
			auto bytes = config.Read(entry.Name, name + ".m");
			const auto handle = resources.Acquire(name);
			const auto native_charge = static_cast<std::uint32_t>(bytes.size());
			resources.Complete(handle, std::move(bytes), native_charge);
			resources.Advance();
			const auto backing = resources.PinBytes(handle);
			resources.Release(handle);
			const auto header = sgre::resource::ReadPsbHeader(*backing);
			const auto document = std::make_shared<const sgre::resource::PsbDocument>(sgre::resource::ReadPsbMetadata(std::span<const std::uint8_t>(*backing).first(header.DataOffset), backing->size()));
			sgre::script::ScriptResourceAsset asset;
			asset.Document = document;
			asset.Backing = backing;
			asset.RetainBacking = [&resources, handle] {
				return resources.PinBytes(handle);
			};
			config_assets.emplace(name, std::move(asset));
		}
		vm.InstallRecoveredBaseLibrary();
		sgre::script::InstallGameResourceProfile(vm);
		std::string log;
		sgre::script::InstallNativePrinting(vm, [&](std::string_view text, bool) {
			if (text.size() > 16384 - log.size()) {
				throw std::runtime_error("startup log exceeds its fixture limit");
			}

			log.append(text);
		});
		std::map<std::string, sgre::script::SquirrelHostRoot, std::less<>> cache;
		for (const auto& entry : scripts.Index().Entries) {
			auto bytes = scripts.Read(entry.Name, entry.Name + ".nut.m");
			const auto name = "script/" + entry.Name + ".nut";
			const auto handle = resources.Acquire(name);
			const auto raw_byte_count = static_cast<std::uint32_t>(bytes.size());
			resources.Complete(handle, std::move(bytes), raw_byte_count);
			resources.Advance();
			cache.emplace(name, vm.Retain(vm.CreateClosure(sgre::script::ReadSquirrelBytecode(resources.Bytes(handle)))));
			resources.Release(handle);
		}

		sgre::script::ScriptScheduler scheduler(vm);
		sgre::script::ScriptObjectBindings objects(vm);
		sgre::script::ScriptAffineMatrixBindings matrices(vm);
		sgre::input::KeyMappings key_mappings;
		sgre::script::ScriptInputBindings inputs(vm, objects, key_mappings);
		sgre::scene::LayerTree layer_tree;
		sgre::script::ScriptPsbBindings psb(vm, objects);
		sgre::script::ScriptFontBindings font_bindings(vm, objects, psb);
		sgre::script::ScriptBinaryBindings binary(vm, objects);
		sgre::script::ScriptRawImageBindings raw_images(vm, objects, binary);
#ifdef SGRE_HAS_MACOS_RENDERER
		sgre::scene::ParallelTaskSet raw_driver;
		raw_driver.SetRetireWhenEmpty(false);
		std::unique_ptr<sgre::scene::MutableTextureBackend> raw_backend;
		std::unique_ptr<sgre::render::SpritePassRenderer> renderer;
		std::unique_ptr<sgre::scene::ScreenRegistry> screens;
		std::unique_ptr<sgre::scene::TextureCache> textures;
		std::unique_ptr<sgre::scene::MotionAssetCatalog> motion_assets;
		std::unique_ptr<sgre::scene::ImageAssetCatalog> image_assets;
		std::unique_ptr<sgre::scene::BitmapFontAssetCatalog> bitmap_fonts;
#endif
		sgre::script::ScriptLayerBindings layers(vm, objects, layer_tree);
		std::unique_ptr<sgre::script::ScriptIndicatorBindings> indicators;
		std::unique_ptr<sgre::script::ScriptRawTextureBindings> raw_bindings;
		std::unique_ptr<sgre::script::ScriptTextRenderBindings> text_render;
		std::unique_ptr<sgre::script::ScriptLayerRawTextureBindings> layer_raw_bindings;
		std::unique_ptr<sgre::script::ScriptImageBindings> images;
		std::unique_ptr<sgre::script::ScriptDrawObjectBindings> draw_objects;
		std::unique_ptr<sgre::script::ScriptLayerDrawBindings> layer_draw;
		std::unique_ptr<sgre::script::ScriptLayerPictureBindings> layer_picture;
		std::unique_ptr<sgre::script::ScriptAffineLayerBindings> affine_layers;
		sgre::script::ScriptResourceBindings resource_bindings(vm, objects, psb, [&](std::string_view name) {
			const auto original_name = std::string(name);

			return [&, original_name]() -> std::optional<sgre::script::ScriptResourceAsset> {
				if (!mounts.Resolve(original_name + ".m")) {
					return std::nullopt;
				}

				const auto found = config_assets.find(original_name);
				if (found == config_assets.end()) {
					throw std::runtime_error("startup fixture did not prepare original PSB request: " + original_name);
				}

				return found->second;
			};
		},
		    [](std::string_view name, std::string_view error) {
			    throw std::runtime_error(std::string(name) + ": " + std::string(error));
		    });
		sgre::script::ScriptMathBindings math(vm);
		sgre::core::FrameClock clock;
		sgre::script::ScriptSystemBindings::LocalDateTimeReader local_time;
#ifdef SGRE_HAS_MACOS_RENDERER
		if (platform_requested) {
			local_time = sgre::platform::ReadHostLocalDateTime;
		}
#endif
		sgre::script::ScriptSystemBindings system(vm, clock, local_time);
		system.ConfigureResourceCache(resources);
		system.ConfigureArchiveMounts(mounts);
#ifdef SGRE_HAS_MACOS_RENDERER
		std::unique_ptr<sgre::script::ScriptScreenBindings> screen_bindings;
		if (platform_requested) {
			sgre::render::SpritePassDescription description;
			description.Width = 64;
			description.Height = 32;
			renderer = std::make_unique<sgre::render::SpritePassRenderer>(description, argv[3]);
			const auto extent = renderer->Description();
			system.ConfigureRenderTarget(extent.Width, extent.Height);
			const auto host_locale = sgre::platform::ReadHostLocale();
			system.ConfigureLocale(sgre::core::ResolveOriginalHostLocale(host_locale.Language, host_locale.Country));
			screens = std::make_unique<sgre::scene::ScreenRegistry>(layer_tree, [&](const sgre::scene::ScreenDescription& screen) {
				sgre::render::SpritePassDescription target;
				target.Width = screen.Width;
				target.Height = screen.Height;
				sgre::scene::ScreenTargetAllocation allocation;
				allocation.Renderer = std::make_shared<sgre::render::SpritePassRenderer>(target, argv[3]);
				const auto actual = allocation.Renderer->Description();
				allocation.Width = actual.Width;
				allocation.Height = actual.Height;

				return allocation;
			});
			screen_bindings = std::make_unique<sgre::script::ScriptScreenBindings>(vm, objects, layers, *screens);
			raw_backend = renderer->CreateMutableTextureBackend();
			raw_bindings = std::make_unique<sgre::script::ScriptRawTextureBindings>(vm, objects, layers, raw_images, binary, layer_tree, *raw_backend, raw_driver);
			text_render = std::make_unique<sgre::script::ScriptTextRenderBindings>(vm, objects, layers, layer_tree, *raw_backend);
			layer_raw_bindings = std::make_unique<sgre::script::ScriptLayerRawTextureBindings>(vm, objects, layers, raw_images, psb, math, layer_tree, *raw_backend);
			textures = std::make_unique<sgre::scene::TextureCache>([&](auto format, const auto& image) {
				return renderer->UploadTexture(format, image);
			},
			    [&](auto texture) {
				    renderer->ReleaseTexture(texture);
			    });
			image_assets = std::make_unique<sgre::scene::ImageAssetCatalog>(*textures);
			images = std::make_unique<sgre::script::ScriptImageBindings>(vm, objects, psb, *image_assets, *textures);
			motion_assets = std::make_unique<sgre::scene::MotionAssetCatalog>(*textures);
			layers.ConfigureMotionOwners(psb, *motion_assets, *textures);
			affine_layers = std::make_unique<sgre::script::ScriptAffineLayerBindings>(vm, objects, layers, matrices);
			draw_objects = std::make_unique<sgre::script::ScriptDrawObjectBindings>(vm, objects);
			layer_draw = std::make_unique<sgre::script::ScriptLayerDrawBindings>(vm, objects, layers, *draw_objects);
			layer_picture = std::make_unique<sgre::script::ScriptLayerPictureBindings>(vm, objects, layers, *images, layer_tree);
			bitmap_fonts = std::make_unique<sgre::scene::BitmapFontAssetCatalog>(*textures);
			indicators = std::make_unique<sgre::script::ScriptIndicatorBindings>(vm, objects, layers, layer_tree, psb, *bitmap_fonts, *textures, &font_bindings);
		}
#else
		if (platform_requested) {
			throw std::runtime_error("platform startup requires the macOS renderer build");
		}
#endif
		std::unique_ptr<sgre::script::ScriptMotionBindings> motion;
		std::size_t motion_runtime_lookups = 0;
		std::size_t motion_mode_reads = 0;
		if (observe_motion) {
			sgre::script::ScriptMotionBindingServices services;
			services.ResolveRuntime = [&](const auto& receiver, auto source) -> sgre::scene::MotionManagerRuntime& {
				++motion_runtime_lookups;
				const auto& owner = layers.MotionResources(receiver);
				const auto& manager = source == sgre::script::MotionLayerSource::Primary ? owner.Primary() : owner.Secondary();
				try {
					return layers.MotionRuntime(receiver, manager);
				} catch (const std::runtime_error& error) {
					if (std::string_view(error.what()) == "actual requested Layer Motion runtime is unregistered") {
						throw std::runtime_error(std::string(sgre::test::kStartupMotionRuntimeUnavailable));
					}

					throw;
				}
			};
			services.ReadAsynchronousMode = [&]() -> bool {
				++motion_mode_reads;
				// Native image mode0 is proven; this fixture has no encompassing mutable application mode owner.
				throw std::runtime_error(std::string(sgre::test::kStartupMotionModeUnavailable));
			};
			motion = std::make_unique<sgre::script::ScriptMotionBindings>(vm, objects, layers, std::move(services));
		}

		std::size_t requests = 0;
		sgre::script::ScriptThreadBindings bindings(vm, scheduler, [&](std::string_view filename) {
			++requests;
			const auto found = cache.find(filename);
			if (found == cache.end()) {
				throw std::runtime_error("script request was not preloaded: " + std::string(filename));
			}

			const auto closure = found->second.Value();

			return [closure]() -> std::optional<sgre::script::SquirrelValue> {
				return closure;
			};
		},
		    objects);
		const auto main = bindings.Spawn(std::string("script/main.nut"));
		const auto handle = bindings.ThreadHandle(main);
		std::uint64_t instructions = 0;
		std::vector<std::string> failures;
		sgre::test::MotionStartupObservation motion_observation;
		for (std::size_t tick = 0; tick < (platform_requested ? 120 : 5); ++tick) {
			mounts.Advance();
#ifdef SGRE_HAS_MACOS_RENDERER
			raw_driver.Update();
#endif
			const auto update = scheduler.Advance(1);
			instructions += update.Instructions;
			for (const auto completed : update.Completed) {
				const auto failure = scheduler.Info(completed).Error;
				if (!failure.empty()) {
					if (observe_motion) {
						motion_observation.RecordFailure(failure);
					} else {
						failures.push_back(failure);
					}
				}
			}
		}

		const auto info = scheduler.Info(handle);
		if (observe_motion) {
			motion_observation.Requests = requests;
			motion_observation.Instructions = instructions;
			motion_observation.MainStatus = static_cast<std::uint32_t>(info.Status);
			motion_observation.MainFailure = sgre::test::ObserveStartupFailure(info.Error);
			motion_observation.RuntimeLookups = motion_runtime_lookups;
			motion_observation.ModeReads = motion_mode_reads;
			motion_observation.LogBytes = log.size();
			motion_observation.Layers = layer_tree.ChildCount();
			motion_observation.ActualClassRegistered = vm.GetSlot(vm.RootTable(), std::string("Motion")) == motion->MotionClass();
			sgre::test::WriteMotionStartupObservation(std::cout, motion_observation);
			motion->NativeOwners().Close();
			vm.Close();
			vm.RethrowReleaseFailure();
#ifdef SGRE_HAS_MACOS_RENDERER
			raw_driver.Clear();
#endif

			// Exit2 records an observation awaiting root acceptance; it never claims this unverified startup passed.
			return 2;
		}
		if (argc == 4) {
			if (requests != 27 || info.Status != sgre::script::ScriptThreadStatus::None || info.Error.find("main.nut:main at instruction 214") == std::string::npos || failures.size() != 4 || instructions == 0 || failures[0].find("basicpicture.nut:main at instruction 310") == std::string::npos || failures[0].find("the index 'Motion' does not exist") == std::string::npos || vm.GetSlot(vm.RootTable(), std::string("AffineMatrix")) != matrices.MatrixClass() || !images || vm.GetSlot(vm.RootTable(), std::string("Image")) != images->ImageClass() || !draw_objects || vm.GetSlot(vm.RootTable(), std::string("DrawObject")) != draw_objects->DrawObjectClass() || !layer_draw || vm.GetSlot(vm.RootTable(), std::string("LayerDraw")) != layer_draw->LayerDrawClass() || !affine_layers || vm.GetSlot(vm.RootTable(), std::string("AffineLayer")) != affine_layers->AffineLayerClass() || !layer_picture || vm.GetSlot(vm.RootTable(), std::string("LayerPicture")) != layer_picture->LayerPictureClass() || !layer_raw_bindings || vm.GetSlot(vm.RootTable(), std::string("LayerRawTex")) != layer_raw_bindings->LayerRawTextureClass() || !text_render || vm.GetSlot(vm.RootTable(), std::string("TextRender")) != text_render->TextRenderClass() || !raw_bindings || vm.GetSlot(vm.RootTable(), std::string("RawTex")) != raw_bindings->RawTextureClass() || !vm.HasSlot(vm.RootTable(), std::string("Screen")) || vm.GetSlot(vm.RootTable(), std::string("Layer")) != layers.LayerClass() || vm.GetSlot(vm.RootTable(), std::string("MotionLayerBase")) != layers.MotionLayerBaseClass() || vm.GetSlot(vm.RootTable(), std::string("Indicator")) != indicators->IndicatorClass() || vm.GetSlot(vm.RootTable(), std::string("LayerView")) != indicators->LayerViewClass() || layer_tree.ChildCount() == 0 || mounts.MountCount() != 7 || !mounts.Resolve("config/revision.psb.m") || resources.Limit() != 524288000 || !clock.VariableFrame() || log.empty()) {
				throw std::runtime_error("original platform startup did not initialize native scene/locale services and preserve the explicit unimplemented Motion dependency; requests=" + std::to_string(requests) + ", failures=" + std::to_string(failures.size()) + ", instructions=" + std::to_string(instructions) + ", status=" + std::to_string(static_cast<std::uint32_t>(info.Status)) + ", variable=" + std::to_string(clock.VariableFrame()) + ", limit=" + std::to_string(resources.Limit()) + ", mounts=" + std::to_string(mounts.MountCount()) + ", layers=" + std::to_string(layer_tree.ChildCount()) + ", log=" + std::to_string(log.size()) + ": " + (failures.empty() ? std::string{} : failures[0] + "; ") + info.Error);
			}

			std::cout << "all " << cache.size() << " scripts parsed from owned cache buffers; original startup configured services, mounted seven archives, loaded original PSB configuration, initialized folder/motion class and locale services, and loaded genuine Indicator/LayerView/TextRender text classes and loaded genuine LayerRawTex/DrawObject/LayerDraw/Image/LayerPicture/AffineLayer before reaching the unimplemented Motion class\n";

			vm.Close();
			vm.RethrowReleaseFailure();
#ifdef SGRE_HAS_MACOS_RENDERER
			raw_driver.Clear();
#endif

			return 0;
		}
		if (requests != 3 || info.Status != sgre::script::ScriptThreadStatus::None || info.Error.find("instruction 164") == std::string::npos || !clock.VariableFrame() || !objects.HasInstance(main) || log.empty()) {
			throw std::runtime_error("original startup did not complete child scheduling, enable its frame clock, print through the bounded host log, and stop at missing render-target bounds: " + info.Error);
		}

		std::cout << "all " << cache.size() << " scripts preloaded; original startup scheduling and printing executed " << instructions << " completed instructions and stopped at missing render-target bounds\n";

		vm.Close();
		vm.RethrowReleaseFailure();

		return 0;
	} catch (const std::exception& error) {
		if (observe_motion) {
			std::cerr << "Motion startup observation stopped before aggregate acceptance; ";
			sgre::test::WriteStartupFailureLocation(std::cerr, sgre::test::ObserveStartupFailure(error.what()));
			std::cerr << '\n';
		} else {
			std::cerr << error.what() << '\n';
		}

		return 1;
	}
}
