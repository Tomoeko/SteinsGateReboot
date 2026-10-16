#include "sgre/app/launch_arguments.hpp"
#include "sgre/app/launch_configuration.hpp"
#include "sgre/audio/offline_buffered_sound_pool.hpp"
#include "sgre/audio/sound_resource_control.hpp"
#include "sgre/core/application_event_control.hpp"
#include "sgre/core/background_calls.hpp"
#include "sgre/input/function_keys.hpp"
#include "sgre/io/file_reader.hpp"
#include "sgre/media/movie_audio.hpp"
#include "sgre/media/movie_pcm_delivery.hpp"
#include "sgre/media/webm_playback.hpp"
#include "sgre/platform/application_pump.hpp"
#include "sgre/platform/backup_crypto.hpp"
#include "sgre/platform/emote_entropy.hpp"
#include "sgre/platform/host_locale.hpp"
#include "sgre/platform/host_performance_counter.hpp"
#include "sgre/platform/host_time.hpp"
#include "sgre/platform/macos_pcm_output.hpp"
#include "sgre/platform/motion_seed_source.hpp"
#include "sgre/render/color_pass.hpp"
#include "sgre/render/graphics_display_frame.hpp"
#include "sgre/render/sprite_graphics_targets.hpp"
#include "sgre/resource/archive_mounts.hpp"
#include "sgre/resource/resident_font.hpp"
#include "sgre/scenario/scenario_search.hpp"
#include "sgre/scene/bitmap_font_preparation_service.hpp"
#include "sgre/scene/emote_wind.hpp"
#include "sgre/scene/image_assets.hpp"
#include "sgre/scene/image_preparation_service.hpp"
#include "sgre/scene/motion_assets.hpp"
#include "sgre/scene/motion_preparation_service.hpp"
#include "sgre/scene/motion_scene_draw_context.hpp"
#include "sgre/script/affine_layer_bindings.hpp"
#include "sgre/script/affine_matrix_bindings.hpp"
#include "sgre/script/application_finish_bindings.hpp"
#include "sgre/script/backup_bindings.hpp"
#include "sgre/script/capture_bindings.hpp"
#include "sgre/script/captured_bindings.hpp"
#include "sgre/script/console_bindings.hpp"
#include "sgre/script/driver_phases.hpp"
#include "sgre/script/emote_player_bindings.hpp"
#include "sgre/script/fill_rectangle_bindings.hpp"
#include "sgre/script/font_bindings.hpp"
#include "sgre/script/game_bindings.hpp"
#include "sgre/script/image_bindings.hpp"
#include "sgre/script/indicator_bindings.hpp"
#include "sgre/script/input_bindings.hpp"
#include "sgre/script/layer_draw_bindings.hpp"
#include "sgre/script/layer_movie_bindings.hpp"
#include "sgre/script/layer_picture_bindings.hpp"
#include "sgre/script/layer_proxy_bindings.hpp"
#include "sgre/script/layer_raw_texture_bindings.hpp"
#include "sgre/script/motion_bindings.hpp"
#include "sgre/script/motion_resource_owner.hpp"
#include "sgre/script/native_math.hpp"
#include "sgre/script/native_texture_bindings.hpp"
#include "sgre/script/original_scene_seek.hpp"
#include "sgre/script/packed_color_bindings.hpp"
#include "sgre/script/prepared_driver_close.hpp"
#include "sgre/script/random_bindings.hpp"
#include "sgre/script/raw_image_bindings.hpp"
#include "sgre/script/raw_texture_bindings.hpp"
#include "sgre/script/regexp_bindings.hpp"
#include "sgre/script/resource_file_service.hpp"
#include "sgre/script/screen_bindings.hpp"
#include "sgre/script/screen_projection_bindings.hpp"
#include "sgre/script/sound_bindings.hpp"
#include "sgre/script/struct_bindings.hpp"
#include "sgre/script/system_bindings.hpp"
#include "sgre/script/text_render_bindings.hpp"
#include "sgre/script/thread_bindings.hpp"
#include "sgre/script/vector_font_bindings.hpp"

#import <Foundation/Foundation.h>

#include <mach-o/dyld.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <poll.h>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

namespace sgre::platform {

	struct OriginalStartupOwners {
		core::LooperFrame& Looper;
		core::ThreadTaskFrame& Workers;
		render::GraphicsDisplayFrame& Display;
		render::SpriteGraphicsTargets& Graphics;
		scene::MutableTextureBackend& RawTextures;
		scene::EmoteWindRandom& WindRandom;
		core::BackgroundCallManager& Background;
		resource::ArchiveMounts& Mounts;
		audio::SoundResourceControl& Sound;
		const MacMotionSeedSource& Seeds;
		const HostPerformanceCounter& Counter;
		media::MovieSilentAudioRegistry& MovieAudio;
		std::shared_ptr<scene::BitmapFontPreparationService> FontPreparation;
		std::shared_ptr<scene::MotionPreparationService> MotionPreparation;
		std::shared_ptr<scene::ImagePreparationService> ImagePreparation;
		std::function<bool()> InitializeSaveDirectory;
		std::filesystem::path SaveDirectory;
		input::FunctionKeyState& FunctionKeys;
		std::function<std::uint8_t()> SoundOutputChannels;
		MacApplicationPump& Platform;
		bool DebugStartInGame = false;
		bool Headless = true;
		std::optional<script::OriginalSceneSeekTarget> SceneSeek;
	};

	static std::unique_ptr<script::SquirrelVm> CreateOriginalStartupVm() {
		script::SquirrelVmLimits limits;
		limits.ReclaimObjects = true;
		auto vm = std::make_unique<script::SquirrelVm>(limits);
		vm->InstallRecoveredBaseLibrary();
		static_cast<void>(script::InstallRecoveredRegexpLibrary(*vm));
		script::InstallGameResourceProfile(*vm);

		return vm;
	}

	static script::ScriptStructBindingLimits OriginalStructLimits() {
		script::ScriptStructBindingLimits limits;
		// Original definition tables exceed the generic16MiB default; backing remains lazy and root bounded.
		limits.Storage.MaxRootBytes = 160 * 1024 * 1024;

		return limits;
	}

	static std::int64_t ReadMovieClock100ns(const HostPerformanceCounter& actual_counter) {
		constexpr std::uint64_t kTicksPerSecond = 10000000;
		const auto frequency = actual_counter.Frequency();
		if (frequency <= 0) {
			throw std::runtime_error("movie clock requires the actual positive host performance frequency");
		}
		const auto counter = actual_counter.ReadRawCounter();
		const auto divisor = static_cast<std::uint64_t>(frequency);
		const auto whole = counter / divisor;
		const auto remainder = counter % divisor;
		const auto maximum = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
		if (whole > maximum / kTicksPerSecond || remainder > maximum / kTicksPerSecond) {
			throw std::runtime_error("movie clock exceeds the recovered signed100ns projection");
		}
		const auto integral = whole * kTicksPerSecond;
		const auto fraction = remainder * kTicksPerSecond / divisor;
		if (fraction > maximum - integral) {
			throw std::runtime_error("movie clock exceeds the recovered signed100ns result");
		}

		return static_cast<std::int64_t>(integral + fraction);
	}

	class PrivateStartupStdin {
	public:
		explicit PrivateStartupStdin(bool enabled) {
			if (!enabled) {
				return;
			}
			m_OriginalFlags = fcntl(STDIN_FILENO, F_GETFL);
			if (m_OriginalFlags < 0) {
				std::cerr << "private stdin disabled: unavailable descriptor errno=" << errno << '\n';
				return;
			}
			if ((m_OriginalFlags & O_NONBLOCK) == 0) {
				if (fcntl(STDIN_FILENO, F_SETFL, m_OriginalFlags | O_NONBLOCK) < 0) {
					std::cerr << "private stdin disabled: nonblocking setup errno=" << errno << '\n';
					return;
				}
				m_RestoreFlags = true;
			}
			m_Active = true;
		}

		~PrivateStartupStdin() noexcept {
			Disable();
		}
		PrivateStartupStdin(const PrivateStartupStdin&) = delete;
		PrivateStartupStdin& operator=(const PrivateStartupStdin&) = delete;

		// This owner is the only stdin reader; fd0 is borrowed and never closed.
		[[nodiscard]] std::size_t Read(std::span<char> output) {
			if (!m_Active || output.empty()) {
				return 0;
			}
			pollfd descriptor{STDIN_FILENO, POLLIN, 0};
			const auto ready = poll(&descriptor, 1, 0);
			if (ready < 0) {
				if (errno != EINTR) {
					std::cerr << "private stdin poll disabled errno=" << errno << '\n';
					Disable();
				}
				return 0;
			}
			if (ready == 0) {
				return 0;
			}
			if ((descriptor.revents & POLLNVAL) != 0) {
				Disable();
				return 0;
			}
			const auto amount = std::min(output.size(), kMaximumBytes - m_BytesRead);
			const auto result = read(STDIN_FILENO, output.data(), amount);
			if (result < 0) {
				if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) {
					std::cerr << "private stdin read disabled errno=" << errno << '\n';
					Disable();
				}
				return 0;
			}
			if (result == 0) {
				std::cerr << "private stdin EOF; accepted commands remain queued\n";
				Disable();
				return 0;
			}
			m_BytesRead += static_cast<std::size_t>(result);
			if (m_BytesRead == kMaximumBytes) {
				std::cerr << "private stdin byte bound reached\n";
				Disable();
			}

			return static_cast<std::size_t>(result);
		}

		void Disable() noexcept {
			m_Active = false;
			if (m_RestoreFlags) {
				static_cast<void>(fcntl(STDIN_FILENO, F_SETFL, m_OriginalFlags));
				m_RestoreFlags = false;
			}
		}

	private:
		static constexpr std::size_t kMaximumBytes = 4096;
		std::size_t m_BytesRead = 0;
		int m_OriginalFlags = 0;
		bool m_RestoreFlags = false;
		bool m_Active = false;
	};

	class PrivateCaptureDescriptor {
	public:
		explicit PrivateCaptureDescriptor(int descriptor) : m_Descriptor(descriptor) {
		}
		~PrivateCaptureDescriptor() noexcept {
			if (m_Descriptor >= 0) {
				static_cast<void>(close(m_Descriptor));
			}
		}
		PrivateCaptureDescriptor(const PrivateCaptureDescriptor&) = delete;
		PrivateCaptureDescriptor& operator=(const PrivateCaptureDescriptor&) = delete;

		void Write(std::span<const std::uint8_t> bytes) {
			while (!bytes.empty()) {
				const auto result = write(m_Descriptor, bytes.data(), bytes.size());
				if (result < 0 && errno == EINTR) {
					continue;
				}
				if (result <= 0) {
					throw std::runtime_error("private numbered frame write failed errno=" + std::to_string(errno));
				}
				bytes = bytes.subspan(static_cast<std::size_t>(result));
			}
		}

		void Finish() {
			const auto descriptor = std::exchange(m_Descriptor, -1);
			if (close(descriptor) != 0) {
				throw std::runtime_error("private numbered frame close failed errno=" + std::to_string(errno));
			}
		}

	private:
		int m_Descriptor = -1;
	};

	class OriginalScriptStartup {
	public:
		// The concrete hidden application owns these actual loop/graphics/Sound services through Close.
		OriginalScriptStartup(OriginalStartupOwners owners, std::filesystem::path original_installation, std::string archive_seed)
		    : m_Owners(std::move(owners)), m_Original(std::move(original_installation)), m_ArchiveSeed(std::move(archive_seed)), m_Logs(std::bind_front(&OriginalScriptStartup::ReportError, this)), m_Scheduler(m_Vm), m_Objects(m_Vm), m_Layers(m_Vm, m_Objects, m_Tree), m_Psb(m_Vm, m_Objects), m_Binary(m_Vm, m_Objects), m_Matrices(m_Vm), m_RawImages(m_Vm, m_Objects, m_Binary), m_Math(m_Vm), m_Random(m_Vm, m_Objects, m_Owners.Seeds.SeedBuffer().Words()), m_Inputs(m_Vm, m_Objects, m_KeyMappings), m_System(m_Vm, m_Owners.Looper.Clock(), ReadHostLocalDateTime), m_Sound(m_Vm, m_Owners.Sound), m_Finish(m_Vm), m_Threads(m_Vm, m_Scheduler, std::bind_front(&OriginalScriptStartup::ResolveThreadFiles, this), m_Objects), m_Fonts(m_Vm, m_Objects, m_Psb), m_Structs(m_Vm, m_Objects, m_Psb, m_Binary, OriginalStructLimits()), m_Resources(m_Vm, m_Objects, m_Psb, m_Binary, std::bind_front(&OriginalScriptStartup::RequestResource, this), ResourceError()), m_Phases(m_Vm, m_Scheduler, "script/main.nut", {}, DriverServices()) {
			if (!m_Owners.ImagePreparation || !m_Owners.ImagePreparation->IsOwnerThread() || m_Owners.ImagePreparation->PendingCount() != 0 || !m_Owners.MotionPreparation || !m_Owners.MotionPreparation->IsOwnerThread() || !m_Owners.FontPreparation || !m_Owners.FontPreparation->IsOwnerThread() || m_Owners.FontPreparation->PendingCount() != 0 || m_ArchiveSeed.empty() || m_ArchiveSeed.find('\0') != std::string::npos) {
				throw std::runtime_error("original startup requires its actual serialized font/cache owner and original archive seed");
			}

			script::InstallNativePrinting(m_Vm, m_Logs.OutputCallback());
			m_System.ConfigureLooperTime(m_Owners.Looper);
			// Original04b340 concatenates only disk root zero;SPEC_TXT selects wind3d11data here.
			const auto disk_root = (m_Original / "wind3d11data").generic_string() + "/";
			m_System.ConfigureDiskFileExistence([disk_root](std::string_view filename) {
				std::string relative(filename);
				std::replace(relative.begin(), relative.end(), '\\', '/');
				const auto path_utf8 = disk_root + relative;
				const auto physical = std::filesystem::path(std::u8string(path_utf8.begin(), path_utf8.end()));
				std::error_code error;
				const auto status = std::filesystem::status(physical, error);

				return !error && std::filesystem::exists(status) && !std::filesystem::is_directory(status);
			});
			if (m_Owners.DebugStartInGame) {
				if (!m_Owners.SceneSeek) {
					throw std::runtime_error("stateful gameplay seek requires an actual validated scenario snapshot");
				}
				m_SceneSeek = std::make_unique<script::OriginalSceneSeek>(m_Vm, *m_Owners.SceneSeek);
			}
			m_Inputs.ConfigureFrameOwners(m_Owners.Looper, m_Controller.Parallel(), [this](const auto&, const auto& mappings) {
				auto actual_frame = m_Owners.Platform.ReadHubFrame(mappings);
				if (!m_Owners.Headless) {
					return actual_frame;
				}

				input::KeyboardSample replay;
				replay.Available = true;
				const bool pressed = m_CommandReturnPressed;
				if (pressed) {
					replay.ScanKeys[0x1c] = 0x80;
					replay.VirtualKeys[0x0d] = 0x80;
				}
				auto frame = input::ResolveKeyboardOnlyFrame(replay, mappings.Convert(replay));
				frame.CursorExtent = actual_frame.CursorExtent;
				m_LastPrivateHubUpdate = m_UpdateCount;
				m_PrivateHubSampled = true;
				if ((m_UpdateCount == 3000 || m_UpdateCount == 3004 || m_UpdateCount == 3600 || m_UpdateCount == 3604) && m_LastInputReplayLog != m_UpdateCount) {
					std::cerr << "privateInputReplay update=" << m_UpdateCount << " return=" << pressed << " resolvedButtons=" << frame.Devices[0].Buttons << '\n';
					m_LastInputReplayLog = m_UpdateCount;
				}
				return frame;
			},
			    [this] {
				    if (!m_Owners.Headless) {
					    return m_Owners.Platform.ReadMouseFrame();
				    }

				    return ReadPrivateMouseFrame();
			    });
			m_Sound.ConfigureBufferedPlayback(m_Owners.SoundOutputChannels, script::SoundBufferedDecoderProfile::VerifiedWmaOptions31, script::SoundBufferedPlaybackMode::IncrementalWma);
			m_Inputs.ConfigureFunctionKeys([this] {
				return m_Owners.FunctionKeys.Snapshot();
			});
			m_Inputs.ConfigureKeyboardText([this] {
				return m_Owners.FunctionKeys.TextSnapshot();
			});
			PrepareMountInputs();
			m_System.ConfigureArchiveReadMounts(m_Owners.Mounts, [this](auto prefix, auto index, auto body) {
				const auto found = m_MountInputs.find({std::string(index), std::string(body)});
				if (found == m_MountInputs.end()) {
					throw std::runtime_error("original System.mount requested an unprepared physical archive identity");
				}

				auto input = found->second;
				input.Prefix = prefix;

				return input;
			});
			const auto primary = m_Owners.Graphics.PrimaryTarget();
			const auto extent = primary->Description();
			m_System.ConfigureRenderTarget(extent.Width, extent.Height);
			m_System.ConfigureMaskMode([this](auto mode) {
				m_Owners.Graphics.SetMaskMode(mode);
			},
			    [this] {
				    return m_Owners.Graphics.MaskMode();
			    });
			const auto locale = ReadHostLocale();
			m_System.ConfigureLocale(core::ResolveOriginalHostLocale(locale.Language, locale.Country));
			auto& textures = m_Owners.FontPreparation->Cache();
			m_BitmapFonts = std::make_unique<scene::BitmapFontAssetCatalog>(textures);
			m_ImageAssets = std::make_unique<scene::ImageAssetCatalog>(textures);
			m_MotionAssets = std::make_unique<scene::MotionAssetCatalog>(textures);
			m_Layers.ConfigureMotionOwners(m_Psb, *m_MotionAssets, textures);
			script::RegisterPackedColorFunctions(m_Vm);
			m_Raw = std::make_unique<script::ScriptRawTextureBindings>(m_Vm, m_Objects, m_Layers, m_RawImages, m_Binary, m_Tree, m_Owners.RawTextures, [this]() -> scene::ParallelTaskSet& {
				return PreparedScene().Group(scene::DefaultDriverTaskGroup::Priority);
			});
			m_Affine = std::make_unique<script::ScriptAffineLayerBindings>(m_Vm, m_Objects, m_Layers, m_Matrices);
			m_Screens = std::make_unique<scene::ScreenRegistry>(m_Tree, [this](const scene::ScreenDescription& description) {
				auto target_description = m_Owners.Graphics.CurrentTarget()->Description();
				target_description.Width = description.Width;
				target_description.Height = description.Height;
				scene::ScreenTargetAllocation target;
				target.Renderer = m_Owners.Graphics.AllocateTarget(target_description);
				const auto actual = target.Renderer->Description();
				target.Width = actual.Width;
				target.Height = actual.Height;

				return target;
			});
			m_ScreenBindings = std::make_unique<script::ScriptScreenBindings>(m_Vm, m_Objects, m_Layers, *m_Screens);
			m_ScreenProjections = std::make_unique<script::ScriptScreenProjectionBindings>(m_Vm, m_Layers, *m_ScreenBindings, *m_Screens, [this](auto texture, const auto& packet) {
				m_Owners.Display.Cycle().DrawScreenProjection(texture, packet);
			});
			m_Images = std::make_unique<script::ScriptImageBindings>(m_Vm, m_Objects, m_Psb, *m_ImageAssets, textures);
			m_NativeTextures = std::make_unique<script::ScriptNativeTextureBindings>(m_Vm, m_Objects, *m_ScreenBindings, *m_Screens, *m_Raw, *m_Images, textures, *m_Owners.Graphics.PrimaryTarget());
			m_Owners.Display.Cycle().PrepareFillRectangle();
			m_FillRectangles = std::make_unique<script::ScriptFillRectangleBindings>(m_Vm, m_Objects, m_Layers);
			m_FillRectangles->ConfigureFrameOwners(m_Owners.Display.Cycle().CameraScope(), [this](const auto& packet) {
				m_Owners.Display.Cycle().DrawFillRectangle(packet);
			});
			m_Captures = std::make_unique<script::ScriptCaptureBindings>(m_Vm, m_Objects, m_Layers);
			m_Captures->ConfigureFrameOwners(m_Owners.Display.Cycle().CaptureBackend(), [this](const auto& task) {
				m_Owners.Display.Cycle().DrawCapture(task);
			});
			m_Owners.Display.Cycle().PrepareCaptured();
			m_Captured = std::make_unique<script::ScriptCapturedBindings>(m_Vm, m_Objects, m_Layers, *m_Captures, [this](auto& texture, const auto& packet) {
				m_Owners.Display.Cycle().DrawCaptured(texture, packet);
			});
			m_Captured->ConfigureAnimationClock(m_Owners.Looper.Clock());
			m_Captured->ConfigureOpacityMaps(*m_Images, [this](auto& texture, auto image, const auto& packet) {
				m_Owners.Display.Cycle().DrawCapturedOpacityMap(texture, image, packet);
			});
			m_DrawObjects = std::make_unique<script::ScriptDrawObjectBindings>(m_Vm, m_Objects);
			m_LayerDraw = std::make_unique<script::ScriptLayerDrawBindings>(m_Vm, m_Objects, m_Layers, *m_DrawObjects);
			const auto layer_operations = LayerOperations(false);
			script::ScriptLayerProxyDrawServices proxy_draw;
			proxy_draw.State = layer_operations.State;
			proxy_draw.Draw = layer_operations.Draw;
			m_LayerProxy = std::make_unique<script::ScriptLayerProxyBindings>(m_Vm, m_Objects, m_Layers, m_Tree, std::move(proxy_draw));
			m_Owners.Display.Cycle().PreparePictures();
			m_LayerPicture = std::make_unique<script::ScriptLayerPictureBindings>(m_Vm, m_Objects, m_Layers, *m_Images, m_Tree);
			m_LayerPicture->ConfigureFrameOwners(m_Owners.Display.Cycle().CameraScope(), [this](const auto& frame) {
				m_Owners.Display.Cycle().DrawPicture(frame);
				if (m_PictureDraws < 8) {
					std::cerr << "actualPictureDraw received coordinate=" << frame.State.Coordinate.X << ',' << frame.State.Coordinate.Y << " extent=" << frame.State.Width << ',' << frame.State.Height << " opacity=" << frame.State.Opacity << " blend=" << frame.State.BlendMode << " regions=" << frame.Regions.size() << '\n';
				}
				++m_PictureDraws;
			});
			m_LayerRaw = std::make_unique<script::ScriptLayerRawTextureBindings>(m_Vm, m_Objects, m_Layers, m_RawImages, m_Psb, m_Math, m_Tree, m_Owners.RawTextures);
			m_Owners.Display.Cycle().PrepareMovies();
			m_Movies = std::make_unique<script::ScriptLayerMovieBindings>(m_Vm, m_Objects, m_Layers, m_Tree);
			media::MoviePlaybackServices movies;
			movies.Open = [this](auto storage) {
				return OpenMovie(storage);
			};
			movies.ReadClock100ns = [this] {
				return ReadMovieClock100ns(m_Owners.Counter);
			};
			movies.Draw = [this](const auto& frame) {
				m_Owners.Display.Cycle().DrawMovie(frame);
				if (m_MovieDraws < 8) {
					std::cerr << "actualMovieDraw received owner=" << frame.OwnerIdentity << " serial=" << frame.FrameSerial << " sample100ns=" << frame.Sample.Timestamp100ns << " extent=" << frame.Extent[0] << ',' << frame.Extent[1] << '\n';
				}
				if (m_MovieTimingRows < 128 && (m_MovieDraws < 8 || m_MovieDraws % 30 == 0)) {
					++m_MovieTimingRows;
					try {
						std::cerr << "actualMovieDrawTiming owner=" << frame.OwnerIdentity << " sequence=" << m_MovieTimingRows << " draw=" << m_MovieDraws << " serial=" << frame.FrameSerial << " sample100ns=" << frame.Sample.Timestamp100ns << " wall100ns=" << ReadMovieClock100ns(m_Owners.Counter) << " movieRendered=" << m_Owners.MovieAudio.RenderedFrames() << '\n';
					} catch (...) {
					}
				}
				++m_MovieDraws;
			};
			movies.CanRetireDrawOwner = [this](const auto* owner) {
				return m_Owners.Display.Cycle().CanRetireMovie(owner);
			};
			movies.RetireDrawOwner = [this](const auto* owner) {
				m_Owners.Display.Cycle().RetireMovie(owner);
			};
			m_Movies->ConfigureFrameOwners(m_Owners.Display.Cycle().CameraScope(), std::move(movies));
			m_Owners.Display.Cycle().PrepareTextGlyphs();
			m_Text = std::make_unique<script::ScriptTextRenderBindings>(m_Vm, m_Objects, m_Layers, m_Tree, m_Owners.RawTextures);
			m_Text->ConfigureFrameOwners(m_Owners.Display.Cycle().CameraScope());
			m_Text->ConfigureGlyphDrawing([this](auto& atlas, auto rectangles, const auto& clip) {
				m_Owners.Display.Cycle().DrawTextGlyphs(atlas, rectangles, clip);
			});
			m_Indicators = std::make_unique<script::ScriptIndicatorBindings>(m_Vm, m_Objects, m_Layers, m_Tree, m_Psb, *m_BitmapFonts, textures, &m_Fonts);
			script::ScriptConsoleServices console;
			console.ReadFrameDelta = [this] {
				const auto& clock = m_Owners.Looper.Clock();

				return clock.VariableFrame() ? std::max(clock.VariableStep(), 1.0f) : 1.0f;
			};
			console.Draw = [this](const auto& state, const auto& placement, const auto& font, auto argument) {
				m_Owners.Display.Cycle().DrawBitmapConsole(state, placement, font, argument);
			};
			m_Console = std::make_unique<script::ScriptConsoleBindings>(m_Vm, m_Objects, m_Layers, m_Tree, m_Psb, *m_BitmapFonts, textures, *m_Indicators, std::move(console), &m_Fonts);
			m_Backup = std::make_unique<script::ScriptBackupBindings>(m_Vm, m_Objects, m_Layers, m_Structs, m_Owners.Background, m_Owners.InitializeSaveDirectory);
			save::BackupFileServices backup_files;
			backup_files.Directory = m_Owners.SaveDirectory;
			backup_files.Decrypt = DecryptBackupBytes;
			backup_files.Encrypt = EncryptBackupBytes;
			backup_files.ReadTimestamp = ReadHostUnixSeconds;
			m_Backup->ConfigureFiles(std::move(backup_files));
			m_Vectors = std::make_unique<script::ScriptVectorFontBindings>(
			    m_Vm, m_Objects, [this](auto name, auto mode) {
				    return RequestResource(name, mode);
			    },
			    ResourceError());
			m_Text->ConfigureFontOwners(m_Psb, *m_Vectors, *m_BitmapFonts, textures);
			script::ScriptMotionBindingServices motion;
			motion.ResolveRuntime = [this](const auto& receiver, auto source) -> scene::MotionManagerRuntime& {
				const auto& resources = m_Layers.MotionResources(receiver);
				const auto& manager = source == script::MotionLayerSource::Primary ? resources.Primary() : resources.Secondary();

				return m_Layers.MotionRuntime(receiver, manager);
			};
			motion.ReadAsynchronousMode = [this] {
				return m_AsynchronousMode;
			};
			motion.WriteAsynchronousMode = [this](bool asynchronous) {
				m_AsynchronousMode = asynchronous;
			};
			script::ScriptEmoteBindingServices emote;
			emote.Motion = motion;
			emote.ReadEntropyWord = ReadEmoteEntropyWord;
			emote.WindRandom = &m_Owners.WindRandom;
			m_Motion = std::make_unique<script::ScriptMotionBindings>(m_Vm, m_Objects, m_Layers, std::move(motion));
			m_Motion->ConfigureAlternateTextures(m_NativeTextures->NativeTextureClass(), [this](const auto& instance) -> const scene::NativeTextureReference& {
				return m_NativeTextures->BorrowNativeTexture(instance).Reference();
			});
			m_Emote = std::make_unique<script::ScriptEmoteBindings>(m_Vm, m_Objects, m_Layers, m_Psb, std::move(emote));
		}

		~OriginalScriptStartup() noexcept {
			try {
				Close();
			} catch (const std::exception& error) {
				std::cerr << "original script retirement: " << error.what() << '\n';
				if (!m_Closed) {
					std::terminate();
				}
			} catch (...) {
				if (!m_Closed) {
					std::terminate();
				}
			}
		}

		OriginalScriptStartup(const OriginalScriptStartup&) = delete;
		OriginalScriptStartup& operator=(const OriginalScriptStartup&) = delete;

		[[nodiscard]] bool QueuePrivateReturnPulse() {
			RequireOpen();
			if (m_PendingReturnPulses == kMaximumPendingReturnPulses) {
				return false;
			}
			++m_PendingReturnPulses;

			return true;
		}

		[[nodiscard]] bool QueuePrivateMouseMove() {
			RequireOpen();
			if (!m_Owners.Headless || m_PendingMouseMoves == kMaximumPendingMouseMoves) {
				return false;
			}
			++m_PendingMouseMoves;

			return true;
		}

		void Update() {
			RequireOpen();
			BeginPrivateReturnUpdate();
			// Real macOS upload preparation services jobs queued by actual resource workers.
			static_cast<void>(m_Owners.FontPreparation->PreparePending());
			m_Owners.FontPreparation->RetirePending();
			static_cast<void>(m_Owners.MotionPreparation->PreparePending());
			m_Owners.MotionPreparation->RetirePending();
			static_cast<void>(m_Owners.ImagePreparation->PreparePending());
			m_Owners.ImagePreparation->RetirePending();
			if (m_Prepared) {
				PreparedScene().Update([this] {
					m_Phases.OnProcess();
					InstallSceneSeekHooks();
				});
			} else {
				m_Controller.Update([this] {
					m_Phases.OnProcess();
					InstallSceneSeekHooks();
				});
			}
			if (m_SceneSeek && !m_Vm.IsClosed()) {
				m_SceneSeek->Poll();
				ReportSceneSeek();
				const auto& seek = m_SceneSeek->Observation();
				if (seek.Phase == script::OriginalSceneSeekPhase::Failed || seek.Phase == script::OriginalSceneSeekPhase::Cancelled) {
					throw std::runtime_error("original stateful seek ended before releasing presentation: " + seek.Reason);
				}
			}
			CompletePrivateReturnUpdate();
			++m_UpdateCount;
		}

		[[nodiscard]] bool SeekHoldsPresentation() const noexcept {
			if (!m_SceneSeek) {
				return false;
			}
			const auto phase = m_SceneSeek->Observation().Phase;

			return phase != script::OriginalSceneSeekPhase::Arrived && phase != script::OriginalSceneSeekPhase::Blocked;
		}

		void Draw(std::uint32_t argument) {
			RequireOpen();
			m_Owners.Display.BeginRootDraw();
			if (m_Prepared) {
				PreparedScene().Draw(argument);
			}
			++m_DrawCount;
		}

		void AdvanceReceivingRetirement() {
			RequireOpen();
			if (m_Manager && !m_Manager->IsClosed()) {
				// Only the concrete receiving/cache tasks advance here; no script, input, scene or frame callback runs.
				m_Manager->Update();
			}
		}

		[[nodiscard]] audio::SoundReadServices ResolveSoundFiles() {
			auto& manager = Manager();

			return {manager.Cache(), m_Owners.Mounts, manager.Processor()};
		}

		void ConfigureApplicationClose(core::ApplicationEventControl& actual_events) {
			RequireOpen();
			m_System.ConfigureApplicationClose(actual_events);
		}

		void ConfigureProcessTermination(script::ScriptProcessTerminationServices services) {
			RequireOpen();
			if (m_Owners.Headless) {
				throw std::runtime_error("headless original observation must not configure a process termination service");
			}

			m_System.ConfigureProcessTermination(std::move(services));
		}

		[[nodiscard]] bool EvaluateFinishRequest(std::uint32_t reason) {
			RequireOpen();

			return m_Finish.EvaluateCloseRequest(reason);
		}

		[[nodiscard]] bool CanClose() const noexcept {
			try {
				if (m_Prepared) {
					m_Prepared->RequireCanClose();
				}

				return !m_Screens || m_Screens->CanClose();
			} catch (...) {
				return false;
			}
		}

		void Close() {
			if (m_Closed) {
				return;
			}
			if (m_SceneSeek) {
				m_SceneSeek->Cancel();
				m_SceneSeek.reset();
			}
			std::cerr << "actualMovieDraw total=" << m_MovieDraws << '\n';
			std::cerr << "actualPictureDraw total=" << m_PictureDraws << '\n';
			if (m_Prepared) {
				try {
					m_Prepared->Close();
				} catch (...) {
					if (m_Prepared->Phase() != script::PreparedDriverClosePhase::Closed) {
						throw;
					}
					if (!m_CloseFailure) {
						m_CloseFailure = std::current_exception();
					}
				}
			} else {
				m_Scheduler.ShutdownNativeThreads();
				m_Vm.Close();
				m_Controller.Clear();
				m_Logs.Close();
			}
			if (m_Screens) {
				m_Screens->Close();
			}
			if (m_Files) {
				m_Files->Close();
			}
			// The enclosing Sound owner still borrows Manager.Cache until its own later Close.
			m_Closed = true;
			if (const auto failure = std::exchange(m_CloseFailure, {})) {
				std::rethrow_exception(failure);
			}
		}

		[[nodiscard]] bool IsClosed() const noexcept {
			return m_Closed;
		}

		[[nodiscard]] script::ScriptDriverPhase Phase() const noexcept {
			return m_Phases.Phase();
		}

		[[nodiscard]] std::string_view LastNativeError() const noexcept {
			return m_LastNativeError;
		}

		[[nodiscard]] const script::ScriptScheduler& Scheduler() const noexcept {
			return m_Scheduler;
		}

		void WriteProgress(std::ostream& output) const {
			output << "driverUpdates=" << m_UpdateCount << " driverDraws=" << m_DrawCount << " phase=" << static_cast<std::uint32_t>(m_Phases.Phase()) << " phaseFailed=" << m_Phases.Failed() << " startup=";
			const auto startup = m_Phases.StartupSucceeded();
			if (startup) {
				output << (*startup ? "succeeded" : "failed");
			} else {
				output << "not-invoked";
			}
			output << " movieDraws=" << m_MovieDraws << " pictureDraws=" << m_PictureDraws << " mountQueued=" << m_Owners.Mounts.QueuedReadMountCount() << " mountInFlight=" << m_Owners.Mounts.HasInFlightReadMount() << " mounts=" << m_Owners.Mounts.MountCount() << " fontPending=" << m_Owners.FontPreparation->PendingCount() << " fontDocuments=" << m_Owners.FontPreparation->LiveDocumentCount() << " imagePending=" << m_Owners.ImagePreparation->PendingCount() << " imageDocuments=" << m_Owners.ImagePreparation->LiveDocumentCount();
			if (m_Manager && !m_Manager->IsClosed()) {
				const auto resources = m_Manager->Cache().State();
				output << " resourcesLoading=" << resources.LoadingEntryNum << " resourcesActive=" << resources.ActiveEntryNum << " resourcesCached=" << resources.CacheEntryNum << " resourceReadQueued=" << m_Manager->Processor().PendingRawCount() << " readerSerial=" << m_Manager->Processor().Tasks().Serial().Count();
			}
			const auto threads = m_Scheduler.ActiveThreads();
			std::array<std::size_t, 6> statuses{};
			for (const auto thread : threads) {
				const auto status = static_cast<std::size_t>(m_Scheduler.Info(thread).Status);
				if (status < statuses.size()) {
					++statuses[status];
				}
			}
			output << " threads=" << threads.size() << " threadStates(none,file,function,stopped,running,waiting)=";
			for (const auto count : statuses) {
				output << count << ',';
			}
			output << " schedulerTick=" << m_Scheduler.CurrentTick() << " schedulerDelta=" << m_Scheduler.DiffTick() << " nativeError=" << m_LastNativeError;
			if (m_Logs.IsInitialized()) {
				const auto& normal = m_Logs.Print();
				const auto& error = m_Logs.Error();
				const auto normal_slot = (normal.Cursor() + script::ScriptDriverLogger::kSlots - 1) % script::ScriptDriverLogger::kSlots;
				const auto error_slot = (error.Cursor() + script::ScriptDriverLogger::kSlots - 1) % script::ScriptDriverLogger::kSlots;
				output << " latestPrint=" << normal.MessageBytes(normal_slot) << " latestError=" << error.MessageBytes(error_slot);
			}
			output << '\n';
			for (std::size_t index = 0; index < std::min<std::size_t>(threads.size(), 8); ++index) {
				const auto observed = m_Scheduler.ObserveWait(threads[index]);
				const auto info = m_Scheduler.Info(threads[index]);
				output << "thread[" << threads[index].Index << "] status=" << static_cast<std::uint32_t>(observed.Status) << " stopped=" << observed.ExecutionStopped << " elapsed=" << observed.ElapsedTicks << " timeout=" << observed.TimeoutTicks << " child=" << observed.WaitingChild.has_value() << " notifications=" << observed.NotificationCount << " file=" << observed.Filename << " hasBuffer=" << observed.HasScriptBuffer << " bufferReady=" << observed.BufferReady << " bufferCanClose=" << observed.BufferCanClose << " completion=";
				if (observed.BufferCompletion) {
					output << static_cast<std::uint32_t>(*observed.BufferCompletion);
				} else {
					output << "none";
				}
				output << " nativeResult=" << info.NativeCallResult << " error=" << info.Error.substr(0, 256) << '\n';
				if (observed.WaitingChild) {
					output << "  waitsChild=" << observed.WaitingChild->Index << ':' << observed.WaitingChild->Generation << '\n';
				}
				if (!observed.Context) {
					continue;
				}
				const auto context = m_Vm.ObserveContext(*observed.Context, 8, script::kSquirrelObservationRegisters);
				output << "  context=" << observed.Context->Index << ':' << observed.Context->Generation << " state=" << static_cast<std::uint32_t>(context.State) << " frames=" << context.FrameCount << " truncated=" << context.FramesTruncated << " nativeFrames=" << context.NativeFrameCount << " suspendedAction=" << context.HasSuspendedAction << " suspendedRegister=" << static_cast<std::uint32_t>(context.SuspendedRegister) << '\n';
				for (const auto& frame : context.Frames) {
					output << "  frame source=" << std::quoted(frame.Source) << " function=" << std::quoted(frame.Function) << " nextPC=" << frame.NextInstruction << " previousLine=" << frame.SourceLine << " registers=" << frame.RegisterCount << " truncated=" << frame.RegistersTruncated;
					if (frame.PreviousOperation) {
						const auto& operation = *frame.PreviousOperation;
						output << " previousOpcode=" << static_cast<std::uint32_t>(operation.Opcode) << " arguments=" << static_cast<std::uint32_t>(operation.Argument0) << ',' << operation.Argument1 << ',' << static_cast<std::uint32_t>(operation.Argument2) << ',' << static_cast<std::uint32_t>(operation.Argument3);
					}
					output << '\n';
					for (const auto& cell : frame.Registers) {
						const auto& value = cell.Value;
						output << "    R" << cell.Index << " type=" << static_cast<std::uint32_t>(value.Type);
						if (value.Integer) {
							output << " integer=" << *value.Integer;
						} else if (value.FloatBits) {
							output << " floatBits=" << *value.FloatBits;
						} else if (value.Boolean) {
							output << " boolean=" << *value.Boolean;
						} else if (value.Type == script::SquirrelValueType::String) {
							output << " text=" << std::quoted(value.Text) << " truncated=" << value.TextTruncated;
						} else if (value.ObjectIndex) {
							output << " object=" << *value.ObjectIndex << ':' << value.ObjectGeneration;
						}
						output << '\n';
						if (value.Type != script::SquirrelValueType::String || value.TextTruncated || !m_Manager || m_Manager->IsClosed()) {
							continue;
						}
						const auto resource = m_Manager->Cache().FindNamedResource(value.Text);
						if (resource) {
							const auto resource_info = m_Manager->Cache().Info(*resource);
							const auto receiving = m_Manager->Cache().ObserveReceiving(*resource);
							output << "    actualResource name=" << std::quoted(value.Text) << " residency=" << static_cast<std::uint32_t>(resource_info.Residency) << " completion=" << static_cast<std::uint32_t>(resource_info.Completion) << " references=" << resource_info.References << " chargedBytes=" << resource_info.ChargedBytes << " unsupported=" << resource_info.UnsupportedProduction << " error=" << std::quoted(resource_info.Error);
							if (receiving.Receiving) {
								output << " receivingPhase=" << static_cast<std::uint32_t>(receiving.Receiving->Phase) << " receivingCompletion=" << static_cast<std::uint32_t>(receiving.Receiving->Completion) << " receivingCanRetire=" << receiving.Receiving->CanRetire;
							}
							output << '\n';
						}
						if (m_Owners.Sound.GetLoading(value.Text) || m_Owners.Sound.GetLoaded(value.Text)) {
							const auto sound = m_Owners.Sound.ConfigurationInfo(value.Text);
							output << "    actualSoundConfiguration name=" << std::quoted(value.Text) << " loading=" << m_Owners.Sound.GetLoading(value.Text) << " loaded=" << m_Owners.Sound.GetLoaded(value.Text) << " completionLatched=" << sound.CompletionLatched << " diagnostic=" << static_cast<std::uint32_t>(sound.Diagnostic) << " error=" << std::quoted(sound.Error) << '\n';
						}
					}
				}
			}
		}

	private:
		void InstallSceneSeekHooks() {
			if (m_SceneSeek && !m_Vm.IsClosed()) {
				m_SceneSeek->InstallAvailableHooks();
			}
		}

		void ReportSceneSeek() {
			const auto& state = m_SceneSeek->Observation();
			if (m_ReportedSeekPhase && *m_ReportedSeekPhase == state.Phase && m_ReportedMovieRequests == state.MovieRequests && m_ReportedMovieConfirmations == state.MovieConfirmations) {
				return;
			}
			m_ReportedSeekPhase = state.Phase;
			m_ReportedMovieRequests = state.MovieRequests;
			m_ReportedMovieConfirmations = state.MovieConfirmations;
			if (m_SeekReportRows == 256) {
				return;
			}
			++m_SeekReportRows;
			std::cerr << "originalStatefulSeek phase=" << static_cast<std::uint32_t>(state.Phase) << " storage=" << std::quoted(state.Storage) << " line=" << state.SourceLine << " point=" << state.Point << " snapshots=" << state.Snapshots << " movieRequests=" << state.MovieRequests << " movieConfirmations=" << state.MovieConfirmations << " reason=" << std::quoted(state.Reason) << " source=" << std::quoted(state.Source) << " function=" << std::quoted(state.Function) << " scriptLine=" << state.ScriptLine << '\n';
		}

		void ReportError(std::string_view error) {
			if (error.size() > 8192) {
				throw std::runtime_error("original native error report exceeds its retained diagnostic byte bound");
			}

			m_LastNativeError.assign(error);
		}

		void RequireOpen() const {
			if (m_Closed || m_Vm.IsClosed()) {
				throw std::runtime_error("original startup driver requires its actual live VM/application owner");
			}
		}

		[[nodiscard]] resource::ResourceManagerProcess& Manager() {
			RequireOpen();
			if (!m_Manager || m_Manager->IsClosed()) {
				throw std::runtime_error("original request precedes actual deferred ResourceManager setup or follows its retirement");
			}

			return *m_Manager;
		}

		[[nodiscard]] scene::DefaultDriverScene& PreparedScene() {
			RequireOpen();
			if (!m_Prepared) {
				throw std::runtime_error("original request requires the actual post-Setup SQDriver scene/Priority owner");
			}

			return m_Prepared->Scene();
		}

		[[nodiscard]] script::ScriptThreadFileServices ResolveThreadFiles() {
			return {Manager().Cache(), m_Controller, m_Owners.Background, m_Owners.Mounts, m_ArchiveSeed};
		}

		[[nodiscard]] script::ScriptResourceBindings::ErrorSink ResourceError() {
			return [this](auto name, auto error) {
				m_Logs.EmitFormatted(std::string(name) + ": " + std::string(error), true);
			};
		}

		[[nodiscard]] script::ScriptResourceBindings::LoadPoll RequestResource(std::string_view name, script::ScriptResourceRequestMode mode) {
			static_cast<void>(Manager());
			if (!m_Files) {
				throw std::runtime_error("original Resource request has no actual same-cache file receiving owner");
			}
			const auto request = m_Files->RequestService();

			return [this, request, name = std::string(name), mode, poll = script::ScriptResourceBindings::LoadPoll{}]() mutable -> std::optional<script::ScriptResourceAsset> {
				if (!poll) {
					const auto filename = script::ResourceReadFilename(name, mode);
					if (!m_Owners.Mounts.Resolve(filename) && (m_Owners.Mounts.QueuedReadMountCount() != 0 || m_Owners.Mounts.HasInFlightReadMount())) {
						return std::nullopt;
					}
					poll = request(name, mode);
				}

				return poll();
			};
		}

		[[nodiscard]] std::unique_ptr<media::MoviePlaybackStream> OpenMovie(std::string_view storage) {
			if (m_MovieStorageRequests < 16) {
				constexpr std::size_t kDiagnosticBytes = 4096;
				std::cerr << "actualMovieStorage request=" << m_MovieStorageRequests << " bytes=" << storage.size() << " storage=" << std::quoted(std::string(storage.substr(0, kDiagnosticBytes))) << " truncated=" << (storage.size() > kDiagnosticBytes) << '\n';
				++m_MovieStorageRequests;
			}

			return media::OpenWebmMoviePlayback(ResolveMovieStorage(storage), m_ArchiveSeed, m_Owners.MovieAudio);
		}

		[[nodiscard]] std::filesystem::path ResolveMovieStorage(std::string_view storage) const {
			if (m_Owners.Mounts.Resolve(storage)) {
				throw std::runtime_error("LayerMovie requires its actual mounted archive-region stream; the current WebM worker owns a physical file");
			}

			const auto normalized = resource::NormalizeResourcePath(storage);
			if (normalized.empty() || normalized.front() == '/' || normalized.find(':') != std::string::npos) {
				throw std::runtime_error("LayerMovie requires its recovered relative native disk identity; absolute paths and schemes remain unrecovered");
			}

			// Original SPEC_TXT has M_SPEC=win/RSC_PATH=wind3d11data/ and no root overrides.
			// Native4db430 searches the registered roots in reverse order, after4d6230 normalization only.
			for (const auto root : {"windata", "wind3d11data"}) {
				const auto filename = m_Original / root / std::filesystem::path(normalized);
				std::error_code error;
				if (std::filesystem::is_regular_file(filename, error)) {
					return filename;
				}
			}

			throw std::runtime_error("LayerMovie exact storage does not exist under its original registered disk roots: " + normalized);
		}

		void PrepareMountInputs() {
			const auto data = m_Original / "wind3d11data";
			for (const auto name : {"config", "font", "image", "map", "motion", "scenario", "script", "sound", "voice"}) {
				const auto index = std::string(name) + "_info.psb.m";
				const auto body = std::string(name) + "_body.bin";
				const auto bytes = std::filesystem::file_size(data / index);
				if (bytes == 0 || bytes > 16 * 1024 * 1024 || bytes > std::numeric_limits<std::uint32_t>::max()) {
					throw std::runtime_error("original archive index exceeds prepared reader physical/metadata range");
				}
				resource::ArchiveReadMountInput input;
				input.IndexIdentity = index;
				input.BodyIdentity = body;
				input.BodyFilename = data / body;
				input.BodyBytes = std::filesystem::file_size(input.BodyFilename);
				input.IndexRead.Region.Filename = data / index;
				input.IndexRead.Region.Length = static_cast<std::uint32_t>(bytes);
				input.IndexRead.Region.MaxBytes = 16 * 1024 * 1024;
				input.IndexRead.OriginalFilename = index;
				input.IndexRead.Seed = m_ArchiveSeed;
				input.IndexRead.MaxDecodedBytes = 16 * 1024 * 1024;
				input.IndexRead.MaxMetadataBytes = 16 * 1024 * 1024;
				m_MountInputs.emplace(std::make_pair(index, body), std::move(input));
			}
		}

		[[nodiscard]] scene::LayerManagerFrameOperations LayerOperations(bool root) {
			scene::LayerManagerFrameOperations operations;
			operations.State = [this](auto handle) {
				if (m_Tree.State(handle).Kind == scene::LayerKind::RawTexture) {
					return m_Raw->ProcessState(handle);
				}

				return m_Layers.ProcessState(handle);
			};
			operations.Update = [this](auto handle) {
				if (m_Tree.State(handle).Kind == scene::LayerKind::Base) {
					m_Layers.DispatchBaseFrame(handle, [](const auto&, auto& frame) {
						frame.Update();
					});
					return;
				}
				if (m_Tree.State(handle).Kind == scene::LayerKind::RawTexture) {
					m_Raw->Update(handle);
					return;
				}
				if (m_Tree.State(handle).Kind == scene::LayerKind::Folder) {
					m_Layers.DispatchFolderFrame(handle, [](const auto&, auto& frame) {
						frame.Update();
					});
					return;
				}
				m_Layers.DispatchMotionFrame(handle, [this](const auto& receiver, auto& frame) {
					if (m_Vm.IsInstanceOf(receiver, m_Affine->AffineLayerClass())) {
						m_Affine->Process(receiver, [&frame] {
							frame.Update();
						});
					} else {
						frame.Update();
					}
				});
			};
			operations.Draw = [this](auto handle, auto argument) {
				if (m_LayerProxy && m_LayerProxy->Owns(handle)) {
					m_LayerProxy->Draw(handle, argument);
					return;
				}

				if (m_Tree.State(handle).Kind == scene::LayerKind::Base) {
					m_Layers.DispatchBaseFrame(handle, [argument](const auto&, auto& frame) {
						frame.Draw(argument);
					});
					return;
				}
				if (m_Tree.State(handle).Kind == scene::LayerKind::RawTexture) {
					m_Raw->Draw(handle, argument);
					return;
				}
				if (m_Tree.State(handle).Kind == scene::LayerKind::Folder) {
					m_Layers.DispatchFolderFrame(handle, [argument](const auto&, auto& frame) {
						frame.Draw(argument);
					});
					return;
				}
				m_Layers.DispatchMotionFrame(handle, [this, argument](const auto& receiver, auto& frame) {
					if (m_Vm.IsInstanceOf(receiver, m_Affine->AffineLayerClass())) {
						m_Affine->Draw(receiver, argument, [&frame](auto forwarded) {
							frame.Draw(forwarded);
						});
					} else {
						frame.Draw(argument);
					}
				});
			};
			if (root) {
				operations.PrepareDraw = [this] {
					m_Owners.Display.Cycle().ResetRootCamera();
				};
			}

			return operations;
		}

		std::span<const scene::CachedTextureHandle> MotionTextureTable(const scene::MotionResourceManager& manager) {
			m_MotionTextureValid.fill(false);
			m_MotionTextures.fill(scene::CachedTextureHandle{});
			const auto projects = manager.WriteRegistrationOrder(m_MotionProjects);
			std::size_t table_size = 0;
			const void* slot_owner = nullptr;
			for (std::size_t project = 0; project < projects; ++project) {
				const auto names = manager.WriteSourceNames(m_MotionProjects[project], m_MotionSourceNames);
				for (std::size_t name = 0; name < names; ++name) {
					const auto source = manager.FindSource(m_MotionProjects[project], m_MotionSourceNames[name]);
					if (!source || source->Slot.Index >= m_MotionTextures.size() || (slot_owner != nullptr && slot_owner != source->Slot.Owner)) {
						throw std::runtime_error("original Motion draw requires its exact bounded source-manager texture slots");
					}
					const auto index = static_cast<std::size_t>(source->Slot.Index);
					if (m_MotionTextureValid[index] && m_MotionTextures[index] != source->Texture) {
						throw std::runtime_error("original Motion slot changed its exact registered texture identity");
					}
					slot_owner = source->Slot.Owner;
					m_MotionTextures[index] = source->Texture;
					m_MotionTextureValid[index] = true;
					table_size = std::max(table_size, index + 1);
				}
			}

			return std::span<const scene::CachedTextureHandle>(m_MotionTextures).first(table_size);
		}

		void DrawMotionSource(const script::SquirrelValue& receiver, script::MotionLayerSource source) {
			const auto& resources = m_Layers.MotionResources(receiver);
			const auto& manager = source == script::MotionLayerSource::Primary ? resources.Primary() : resources.Secondary();
			auto& runtime = m_Layers.MotionRuntime(receiver, manager);

			scene::MotionFrameScratch scratch;
			scratch.Batch.Positions = m_Positions;
			scratch.Batch.Uvs = m_Uvs;
			scratch.Batch.Colors = m_Colors;
			scratch.Batch.Indices = m_Indices;
			scratch.MaskOperations = m_MaskOperations;
			scratch.BlendPasses = m_BlendPasses;
			if (runtime.HasSceneServices()) {
				m_Owners.Display.Cycle().DrawScene(runtime.SceneContext(), MotionTextureTable(manager), scratch);
			} else {
				if (runtime.Context().Size() != 0) {
					throw std::runtime_error("original legacy Motion draw requires its separate prepared ordinary texture table");
				}
				m_Owners.Display.Cycle().Draw(runtime.Context(), scratch);
			}
		}

		[[nodiscard]] script::ScriptDriverPhaseServices DriverServices() {
			script::ScriptDriverPhaseServices services;
			services.Setup = [this] {
				Setup();
			};
			services.ReadFrameStep = [this] {
				const auto& clock = m_Owners.Looper.Clock();

				return clock.VariableFrame() ? std::max(clock.VariableStep(), 1.0f) : 1.0f;
			};

			return services;
		}

		void Setup() {
			if (m_Manager || m_Prepared || m_Owners.Display.Cycle().CameraScope().Depth() != 1 || m_Owners.Display.Cycle().CameraScope().IsDrawing()) {
				throw std::runtime_error("original Setup requires first actual Common onProcess after Display Pre/default camera");
			}
			m_Manager = std::make_unique<resource::ResourceManagerProcess>(m_Owners.Mounts, m_Owners.Background);
			m_System.ConfigureResourceCache(m_Manager->Cache());
			m_Files = std::make_unique<script::ScriptResourceFileService>(m_Manager->Cache(), m_Owners.Mounts, m_Manager->Processor(), m_ArchiveSeed, script::ResourceFileServiceLimits{}, m_Owners.FontPreparation, m_Owners.MotionPreparation, m_Owners.ImagePreparation);
			m_ResidentFont = resource::ReadResidentMeterFont(m_Original / "sgre_steam.exe");
			m_ResidentFontAsset = m_BitmapFonts->Prepare(m_ResidentFont.Document, m_ResidentFont.Backing);
			const auto viewport = m_Owners.Graphics.PrimaryTarget()->Description();
			m_Meter = std::make_unique<scene::ResourceMeterProcess>(m_Owners.Looper.Hooks(), m_Owners.FontPreparation->Cache(), m_ResidentFontAsset, scene::ResourceMeterViewport{viewport.Width, viewport.Height});
			script::PreparedDriverCloseOwners owners{m_Controller, m_Tree, *m_Manager, *m_Meter, m_Scheduler, m_Vm, m_Objects, m_Layers, m_Logs};
			m_Prepared = std::make_unique<script::PreparedDriverClose>(owners, LayerOperations(true));
			m_Affine->AttachPriorityTask(PreparedScene().Group(scene::DefaultDriverTaskGroup::Priority));
			scene::ScreenDriverServices screens;
			screens.ResolveTasks = [this]() -> scene::ParallelTaskSet& {
				return PreparedScene().Group(scene::DefaultDriverTaskGroup::BeforeLayersSecond);
			};
			screens.Layers = LayerOperations(true);
			screens.Draw = [this](auto, auto& target, auto& manager, const auto& state, auto) {
				m_Owners.Display.Cycle().DrawScreen(target, manager, state);
			};
			m_Screens->ConfigureDriver(std::move(screens));
			m_Raw->ConfigureFrameOwners(m_Owners.Display.Cycle().CameraScope(), [this](const auto&, auto& task, const auto& packet, auto) {
				m_Owners.Display.Cycle().DrawRawTexture(task, packet);
			});
			script::ScriptLayerFrameServices frames;
			frames.Motion.State = [this](const auto& receiver) {
				return m_Layers.MotionFrameState(receiver);
			};
			frames.Motion.SecondaryDraw = [this](const auto& receiver, auto) {
				DrawMotionSource(receiver, script::MotionLayerSource::Secondary);
			};
			frames.Motion.PrimaryDraw = [this](const auto& receiver, auto) {
				DrawMotionSource(receiver, script::MotionLayerSource::Primary);
			};
			frames.Folder.State = [this](const auto& receiver) {
				return m_Layers.FolderFrameState(receiver);
			};
			frames.Folder.Children = LayerOperations(false);
			m_Layers.ConfigureFrameOwners(m_Owners.Display.Cycle().CameraScope(), m_Owners.Looper.Clock(), m_Owners.Seeds.SeedBuffer().Words(), std::move(frames));
			m_Logs.Initialize();
			// Native048330 prequeues this exact script mount; the original script itself requests the remaining mounts.
			auto initial = m_MountInputs.at({"script_info.psb.m", "script_body.bin"});
			initial.Prefix = "script/";
			m_Manager->Processor().QueueReadMount(std::move(initial));
		}

		OriginalStartupOwners m_Owners;
		std::filesystem::path m_Original;
		std::string m_ArchiveSeed;
		std::unique_ptr<script::SquirrelVm> m_VmOwner = CreateOriginalStartupVm();
		script::SquirrelVm& m_Vm = *m_VmOwner;
		std::string m_LastNativeError;
		script::ScriptDriverLogs m_Logs;
		script::ScriptScheduler m_Scheduler;
		script::ScriptObjectBindings m_Objects;
		scene::LayerTree m_Tree;
		script::ScriptLayerBindings m_Layers;
		script::ScriptPsbBindings m_Psb;
		script::ScriptBinaryBindings m_Binary;
		script::ScriptAffineMatrixBindings m_Matrices;
		script::ScriptRawImageBindings m_RawImages;
		script::ScriptMathBindings m_Math;
		script::ScriptRandomBindings m_Random;
		input::KeyMappings m_KeyMappings;
		script::ScriptInputBindings m_Inputs;
		std::unique_ptr<script::OriginalSceneSeek> m_SceneSeek;
		std::optional<script::OriginalSceneSeekPhase> m_ReportedSeekPhase;
		std::size_t m_ReportedMovieRequests = 0;
		std::size_t m_ReportedMovieConfirmations = 0;
		std::size_t m_SeekReportRows = 0;
		script::ScriptSystemBindings m_System;
		script::ScriptSoundBindings m_Sound;
		script::ScriptApplicationFinishBindings m_Finish;
		scene::CommonProcessTasks m_Controller;
		std::size_t m_LastInputReplayLog = 0;
		std::uint32_t m_MovieStorageRequests = 0;
		std::uint64_t m_MovieDraws = 0;
		std::size_t m_MovieTimingRows = 0;
		std::uint64_t m_PictureDraws = 0;
		std::unique_ptr<resource::ResourceManagerProcess> m_Manager;
		std::unique_ptr<script::ScriptResourceFileService> m_Files;
		script::ScriptThreadBindings m_Threads;
		script::ScriptFontBindings m_Fonts;
		script::ScriptStructBindings m_Structs;
		script::ScriptResourceBindings m_Resources;
		std::unique_ptr<scene::BitmapFontAssetCatalog> m_BitmapFonts;
		std::unique_ptr<scene::ImageAssetCatalog> m_ImageAssets;
		std::unique_ptr<scene::MotionAssetCatalog> m_MotionAssets;
		std::unique_ptr<scene::ScreenRegistry> m_Screens;
		std::unique_ptr<script::ScriptScreenBindings> m_ScreenBindings;
		std::unique_ptr<script::ScriptScreenProjectionBindings> m_ScreenProjections;
		std::unique_ptr<script::ScriptRawTextureBindings> m_Raw;
		std::unique_ptr<script::ScriptAffineLayerBindings> m_Affine;
		std::unique_ptr<script::ScriptImageBindings> m_Images;
		std::unique_ptr<script::ScriptNativeTextureBindings> m_NativeTextures;
		std::unique_ptr<script::ScriptFillRectangleBindings> m_FillRectangles;
		std::unique_ptr<script::ScriptCaptureBindings> m_Captures;
		std::unique_ptr<script::ScriptCapturedBindings> m_Captured;
		std::unique_ptr<script::ScriptDrawObjectBindings> m_DrawObjects;
		std::unique_ptr<script::ScriptLayerDrawBindings> m_LayerDraw;
		std::unique_ptr<script::ScriptLayerProxyBindings> m_LayerProxy;
		std::unique_ptr<script::ScriptLayerPictureBindings> m_LayerPicture;
		std::unique_ptr<script::ScriptLayerRawTextureBindings> m_LayerRaw;
		std::unique_ptr<script::ScriptLayerMovieBindings> m_Movies;
		std::unique_ptr<script::ScriptTextRenderBindings> m_Text;
		std::unique_ptr<script::ScriptIndicatorBindings> m_Indicators;
		std::unique_ptr<script::ScriptConsoleBindings> m_Console;
		std::unique_ptr<script::ScriptBackupBindings> m_Backup;
		std::unique_ptr<script::ScriptVectorFontBindings> m_Vectors;
		std::unique_ptr<script::ScriptMotionBindings> m_Motion;
		std::unique_ptr<script::ScriptEmoteBindings> m_Emote;
		resource::ResidentMeterFontInput m_ResidentFont;
		std::shared_ptr<const scene::LoadedBitmapFontAsset> m_ResidentFontAsset;
		std::unique_ptr<scene::ResourceMeterProcess> m_Meter;
		std::unique_ptr<script::PreparedDriverClose> m_Prepared;
		std::map<std::pair<std::string, std::string>, resource::ArchiveReadMountInput> m_MountInputs;
		script::ScriptDriverPhases m_Phases;
		std::array<scene::MotionProjectId, 4096> m_MotionProjects;
		std::array<std::string_view, 1024> m_MotionSourceNames;
		std::array<scene::CachedTextureHandle, 1024> m_MotionTextures;
		std::array<bool, 1024> m_MotionTextureValid{};
		std::array<resource::MotionPoint, 16384> m_Positions;
		std::array<resource::MotionPoint, 16384> m_Uvs;
		std::array<std::uint32_t, 16384> m_Colors;
		std::array<std::uint32_t, 24576> m_Indices;
		std::array<scene::MotionMaskOperation, 4096> m_MaskOperations;
		std::array<scene::MotionMaskedBlendPass, 20> m_BlendPasses;

		[[nodiscard]] input::MouseSample ReadPrivateMouseFrame() {
			if (!m_PrivateMouseEnabled && m_PendingMouseMoves == 0) {
				return m_Owners.Platform.ReadMouseFrame();
			}
			if (!m_PrivateMouseSampled || m_LastPrivateMouseUpdate != m_UpdateCount) {
				bool moved = false;
				if (m_PrivateMouseOffsetNext) {
					m_PrivateMouseX = 800;
					m_PrivateMouseY = 450;
					m_PrivateMouseOffsetNext = false;
					moved = true;
				} else if (m_PendingMouseMoves != 0) {
					--m_PendingMouseMoves;
					m_PrivateMouseEnabled = true;
					m_PrivateMouseX = 640;
					m_PrivateMouseY = 360;
					m_PrivateMouseOffsetNext = true;
					moved = true;
				}
				m_LastPrivateMouseUpdate = m_UpdateCount;
				m_PrivateMouseSampled = true;
				if (moved && m_PrivateMouseRows < kMaximumPrivateMouseRows) {
					++m_PrivateMouseRows;
					std::cerr << "privateMouseReplay sequence=" << m_PrivateMouseRows << " update=" << m_UpdateCount << " clientX=" << m_PrivateMouseX << " clientY=" << m_PrivateMouseY << " offsetNext=" << m_PrivateMouseOffsetNext << " pending=" << m_PendingMouseMoves << '\n';
				}
			}

			// Feed the existing MouseInput quantization/edges in VSync; never move the OS pointer.
			input::MouseSample sample;
			sample.Client = {0, 0, 1280, 720};
			sample.ClientX = m_PrivateMouseX;
			sample.ClientY = m_PrivateMouseY;
			sample.TargetWindowMatches = true;
			sample.EnterTracking = true;

			return sample;
		}

		void BeginPrivateReturnUpdate() {
			// InputHub samples in VSync after Update; retain this latch through the completed update.
			m_CommandReturnPressed = m_CommandReturnUpdates != 0;
			if (m_CommandReturnUpdates != 0 || m_CommandReleaseUpdates != 0 || m_PendingReturnPulses == 0) {
				return;
			}
			if (!m_PrivateHubSampled || m_LastPrivateHubUpdate != m_UpdateCount) {
				return;
			}
			--m_PendingReturnPulses;
			m_CommandReturnUpdates = kCommandReturnUpdates;
			m_CommandReturnPressed = true;
			std::cerr << "private stdin Return begin update=" << m_UpdateCount << " pending=" << m_PendingReturnPulses << '\n';
		}

		void CompletePrivateReturnUpdate() {
			if (m_CommandReturnUpdates != 0) {
				--m_CommandReturnUpdates;
				if (m_CommandReturnUpdates == 0) {
					m_CommandReleaseUpdates = 1;
					std::cerr << "private stdin Return release after update=" << m_UpdateCount << '\n';
				}
			} else if (m_CommandReleaseUpdates != 0) {
				--m_CommandReleaseUpdates;
			}
		}

		static constexpr std::uint32_t kMaximumPendingMouseMoves = 16;
		static constexpr std::uint32_t kMaximumPrivateMouseRows = 64;
		std::uint32_t m_PendingMouseMoves = 0;
		std::uint32_t m_PrivateMouseRows = 0;
		std::int32_t m_PrivateMouseX = 640;
		std::int32_t m_PrivateMouseY = 360;
		std::uint64_t m_LastPrivateMouseUpdate = 0;
		bool m_PrivateMouseEnabled = false;
		bool m_PrivateMouseSampled = false;
		bool m_PrivateMouseOffsetNext = false;
		static constexpr std::uint32_t kMaximumPendingReturnPulses = 16;
		static constexpr std::uint32_t kCommandReturnUpdates = 4;
		std::uint32_t m_PendingReturnPulses = 0;
		std::uint32_t m_CommandReturnUpdates = 0;
		std::uint32_t m_CommandReleaseUpdates = 0;
		bool m_CommandReturnPressed = false;
		bool m_PrivateHubSampled = false;
		std::uint64_t m_LastPrivateHubUpdate = 0;
		std::uint64_t m_UpdateCount = 0;
		std::uint64_t m_DrawCount = 0;
		// Native image a97278 starts zero; actual reader and writer share this mutable application state.
		bool m_AsynchronousMode = false;
		std::exception_ptr m_CloseFailure;
		bool m_Closed = false;
	};

} // namespace sgre::platform

namespace sgre::platform {

	struct OriginalStartupRun {
		std::filesystem::path Installation;
		std::filesystem::path SpriteShader;
		std::filesystem::path PresentationShader;
		std::filesystem::path SaveDirectory;
		std::string ArchiveSeed;
		render::SpritePassDescription Target;
		audio::OfflineSoundMasteringDescription Offline;
		std::size_t MaximumIterations = 100000;
		std::uint64_t MaximumMilliseconds = 20000;
		bool DebugStartInGame = false;
		bool Headless = true;
		std::optional<script::OriginalSceneSeekTarget> SceneSeek;
		render::SpritePresentationDescription Presentation;
		std::filesystem::path ConfigurationWorkspace;
		std::optional<app::LaunchConfiguration> Configuration;
	};

	class HiddenOriginalApplication {
	public:
		explicit HiddenOriginalApplication(OriginalStartupRun run)
		    : m_Stdin(run.Headless), m_Run(std::move(run)), m_Graphics(m_Run.Target, m_Run.SpriteShader), m_Primary(m_Graphics.PrimaryTarget()), m_Surface(*m_Primary, m_Run.PresentationShader, m_Run.Presentation), m_Platform(m_Events, m_Surface), m_Clock(m_Events, m_Platform), m_Workers([this] {
			      return m_Counter.ReadRawCounter();
		      },
		                                                                                                                                                                                                                                                                                  4, 1024),
		      m_Sound([this] {
			      if (!m_Driver) {
				      throw std::runtime_error("original Sound request precedes actual containing script driver and ResourceManager setup");
			      }

			      return m_Driver->ResolveSoundFiles();
		      },
		          "win", m_Run.ArchiveSeed, false),
		      m_Root(RootCallbacks()), m_Looper(m_Workers, m_Root, m_Clock) {
			try {
				if ((m_Run.Headless && (m_Run.MaximumIterations == 0 || m_Run.MaximumMilliseconds == 0)) || m_Run.MaximumIterations > 100000 || m_Run.MaximumMilliseconds > 900000) {
					throw std::runtime_error("headless startup requires positive bounded iterations and wall time; explicit diagnostic limits must retain their bounds");
				}

				const auto graphics_limits = m_Primary->Description();
				scene::TextureCacheLimits cache_limits;
				cache_limits.MaxTextures = graphics_limits.MaxTextures;
				cache_limits.MaxTextureBytes = graphics_limits.MaxTextureBytes;
				scene::TextureCache::UploadBc7 upload_bc7;
				if (m_Primary->SupportsBc7Textures()) {
					upload_bc7 = [this](const resource::Bc7TextureView& image) {
						return m_Primary->UploadBc7Texture(image);
					};
				}
				m_FontPreparation = std::make_shared<scene::BitmapFontPreparationService>([this](auto format, const auto& image) {
					return m_Primary->UploadTexture(format, image);
				},
				    [this](auto texture) {
					    m_Primary->ReleaseTexture(texture);
				    },
				    cache_limits, scene::BitmapFontPreparationLimits{}, std::move(upload_bc7));
				m_MotionPreparation = std::make_shared<scene::MotionPreparationService>(m_FontPreparation->Cache());
				m_ImagePreparation = std::make_shared<scene::ImagePreparationService>(m_FontPreparation->Cache());
				m_Display = std::make_unique<render::GraphicsDisplayFrame>(m_Looper, m_Graphics, m_Surface, m_Workers, m_FontPreparation->Cache(), render::RenderFrameCounters{}, render::RenderFrameCounters{});
				m_Platform.BindPresentation(*m_Display, m_Clock);
				m_PresentationBound = true;
				// Prepare actual VS4 pipelines/backend while Idle, before the first Display Pre or Raw wrapper.
				auto& raw_textures = m_Display->Cycle().RawTextureBackend();
				m_Seeds = std::make_unique<MacMotionSeedSource>(m_Looper, m_Workers, m_Counter);
				RegisterLooperHooks();
				if (m_Run.Offline.SamplesPerSecond != 48000) {
					throw std::runtime_error("original Movie silent integration requires the measured48kHz host sample budget");
				}
				m_Sound.ConfigureVolumeAnimations(m_Looper.Clock());
				if (!m_Run.Headless && m_Run.Offline.Channels != 2) {
					throw std::runtime_error("visible Sound/Movie output currently requires the actual48k stereo mastering configuration");
				}
				m_Output = std::make_unique<audio::OfflineBufferedSoundPool>(m_Sound, m_Run.Offline);
				m_MovieAudio = std::make_unique<media::MovieSilentAudioRegistry>([this] {
					return ReadMovieClock100ns(m_Counter);
				},
				    m_Sound);
				m_AudioScratch.resize(static_cast<std::size_t>(m_Run.Offline.MaximumFrames) * m_Run.Offline.Channels);
				if (!m_Run.Headless) {
					MacPcmOutputDescription output_description;
					output_description.SamplesPerSecond = m_Run.Offline.SamplesPerSecond;
					output_description.Channels = m_Run.Offline.Channels;
					output_description.DeliveryMode = MacPcmDeliveryMode::CallbackEvents;
					m_HardwareAudio = std::make_unique<MacPcmOutput>(output_description);
					m_MovieDelivery = std::make_unique<media::MoviePcmDelivery>(*m_MovieAudio, *m_Output, *m_HardwareAudio);
				}
				m_Auxiliary = std::make_unique<core::ApplicationAuxiliaryRegistration>(
				    m_Clock, [this](bool suspended) {
					    m_Sound.InvokeAuxiliaryPause(suspended);
				    },
				    [this] {
					    m_Platform.RequireLive();
					    if (!m_Output || m_Sound.IsClosed()) {
						    throw std::runtime_error("original Sound auxiliary requires its actual live offline output/controller owners");
					    }
				    });
				OriginalStartupOwners owners{m_Looper, m_Workers, *m_Display, m_Graphics, raw_textures, m_WindRandom, m_Background, m_Mounts, m_Sound, *m_Seeds, m_Counter, *m_MovieAudio, m_FontPreparation, m_MotionPreparation, m_ImagePreparation, std::bind_front(&HiddenOriginalApplication::InitializeSaveDirectory, this), m_Run.SaveDirectory, m_FunctionKeys, [this] {
					                             if (!m_Output) {
						                             throw std::runtime_error("Sound playback requires the actual hidden offline output owner");
					                             }

					                             return m_Output->Description().Channels;
				                             },
				    m_Platform, m_Run.DebugStartInGame, m_Run.Headless, m_Run.SceneSeek};
				m_Driver = std::make_unique<OriginalScriptStartup>(std::move(owners), m_Run.Installation, m_Run.ArchiveSeed);
				m_Driver->ConfigureApplicationClose(m_Events);
				InstallFinishCloseReceiver();
				if (!m_Run.Headless) {
					script::ScriptProcessTerminationServices termination;
					termination.RequireLive = [this] {
						m_Platform.RequireLive();
						if (m_Closed || m_Run.Headless || !m_Driver || m_Driver->IsClosed()) {
							throw std::runtime_error("normal original process termination requires its actual live platform and script driver");
						}
					};
					termination.TerminateProcess = [] {
						// Match the original process boundary; automatic stack owners deliberately do not unwind.
						std::exit(0);
					};
					m_Driver->ConfigureProcessTermination(std::move(termination));
				}
			} catch (...) {
				const auto failure = std::current_exception();
				Close();
				std::rethrow_exception(failure);
			}
		}

		~HiddenOriginalApplication() noexcept {
			try {
				Close();
			} catch (const std::exception& error) {
				std::cerr << "original application retirement: " << error.what() << '\n';
				if (!m_Closed) {
					std::terminate();
				}
			} catch (...) {
				if (!m_Closed) {
					std::terminate();
				}
			}
		}

		void CaptureLastFrame(bool numbered = false) {
			const auto submission = m_Display->Cycle().LastSubmission();
			if (!submission || submission->Owner == nullptr || submission->Identity == 0) {
				throw std::runtime_error("no actual completed frame is available");
			}
			const auto primary = m_Graphics.PrimaryTarget();
			if (submission->Owner != primary.get()) {
				throw std::runtime_error("capture frame does not belong to the actual primary target");
			}
			const auto pixels = primary->Capture(submission->Identity);
			if (numbered) {
				WriteNumberedCapture(pixels, submission->Identity);
				return;
			}
			const auto destination = m_Run.SaveDirectory.parent_path() / "original-startup-frame-capture" / "latest-frame.ppm";
			std::filesystem::create_directories(destination.parent_path());
			std::ofstream output(destination, std::ios::binary | std::ios::trunc);
			output << "P6\n"
			       << pixels.Width << ' ' << pixels.Height << "\n255\n";
			for (std::size_t offset = 0; offset < pixels.Rgba.size(); offset += 4) {
				output.write(reinterpret_cast<const char*>(pixels.Rgba.data() + offset), 3);
			}
			output.close();
			if (!output) {
				throw std::runtime_error("private actual-frame capture could not be written");
			}
			std::cerr << "actual primary frame captured width=" << pixels.Width << " height=" << pixels.Height << " submission=" << submission->Identity << '\n';
		}

		void Run() {
			if (!m_Run.Headless) {
				if (!m_Driver->SeekHoldsPresentation()) {
					m_Surface.ShowWindow();
					m_WindowShown = true;
				}
				AdvanceHardwareAudio();
				m_HardwareAudio->Start();
			}
			const auto started = m_Counter.ReadMilliseconds();
			m_LastAudioClock = std::chrono::steady_clock::now();
			bool active = true;
			try {
				while (active && (m_Run.MaximumIterations == 0 || m_Iterations < m_Run.MaximumIterations) && (m_Run.MaximumMilliseconds == 0 || m_Counter.ReadMilliseconds() - started < m_Run.MaximumMilliseconds)) {
					if (m_Run.Headless) {
						PollPrivateCommands();
					}
					active = m_Display->RunIteration();
					++m_Iterations;
					if (active && !m_Run.Headless && !m_WindowShown && m_SeekPresentationDrawReady && m_Driver->LastNativeError().empty()) {
						// Present only after the released boundary has completed its genuine root Draw/submit iteration.
						m_Surface.ShowWindow();
						m_WindowShown = true;
					}
					if (m_Run.Headless) {
						CapturePrivateCommand();
					}
				}
				if (m_Run.Headless) {
					try {
						CaptureLastFrame();
					} catch (const std::exception& capture_error) {
						std::cerr << "private frame capture: " << capture_error.what() << '\n';
					}
				}
				if (active) {
					throw std::runtime_error("original startup observation reached its actual OS-loop iteration/wall bound; whole game remains active");
				}
				if (!m_Driver->LastNativeError().empty()) {
					throw std::runtime_error(std::string(m_Driver->LastNativeError()));
				}
				if (!m_Run.Headless) {
					RememberWindowPreferences();
				}
			} catch (const std::exception& error) {
				WriteSilentAudioProgress(std::cerr);
				std::cerr << "actual OS progress: elapsedMilliseconds=" << m_Counter.ReadMilliseconds() - started << " rootUpdates=" << m_RootUpdates << " rootDraws=" << m_RootDraws << " rootRetirements=" << m_RootRetirements << '\n';
				m_Driver->WriteProgress(std::cerr);
				std::cerr << "original startup boundary: " << error.what() << "; actualIterations=" << m_Iterations << "; phase=" << static_cast<std::uint32_t>(m_Driver->Phase()) << "; retainedNativeError=" << m_Driver->LastNativeError() << '\n';
				throw;
			}
			std::cout << "original startup OS loop completed iterations=" << m_Iterations << " updates=" << m_RootUpdates << " draws=" << m_RootDraws << " rootRetirements=" << m_RootRetirements << "; headless=" << m_Run.Headless << (m_Run.Headless ? "; offline discarded audio" : "; default-device PCM output") << "; no whole-game completion claim\n";
		}

		void Close() {
			if (m_Closed) {
				return;
			}
			m_Looper.RequireIdleContainingPhase();
			if (m_CloseCallbacks) {
				m_CloseCallbacks->Close();
				m_CloseCallbacks.reset();
			}
			if (m_MovieDelivery) {
				try {
					m_MovieDelivery->Close();
				} catch (...) {
					if (!m_MovieDelivery->IsClosed()) {
						throw;
					}
					if (!m_CloseFailure) {
						m_CloseFailure = std::current_exception();
					}
				}
				m_MovieDelivery.reset();
			}
			if (m_HardwareAudio) {
				m_HardwareAudio->Close();
				m_HardwareAudio.reset();
			}
			if (m_ImagePreparation) {
				m_ImagePreparation->Stop();
			}
			if (m_MotionPreparation) {
				m_MotionPreparation->Stop();
			}
			if (m_FontPreparation) {
				// Cancellation publishes actual failed preparation and unblocks worker waits before their concrete fences.
				m_FontPreparation->Stop();
			}
			AwaitSoundRetirement();
			if (m_Driver && !m_Driver->CanClose()) {
				throw std::runtime_error("original application retains actual pending script/resource borrowers at retirement");
			}
			if (m_Driver) {
				try {
					m_Driver->Close();
				} catch (...) {
					if (!m_Driver->IsClosed()) {
						throw;
					}
					if (!m_CloseFailure) {
						m_CloseFailure = std::current_exception();
					}
				}
			}
			if (m_Output) {
				m_Output->Close();
				m_Output.reset();
			}
			if (m_Auxiliary) {
				m_Auxiliary->Close();
			}
			m_Sound.Close();
			// Driver keeps the concrete Manager/cache object alive until Sound releases its captured bundle.
			m_Driver.reset();
			if (m_MovieAudio) {
				m_MovieAudio->Close();
				m_MovieAudio.reset();
			}
			RetireLooperHooks();
			if (m_Seeds) {
				m_Seeds->Close();
			}
			if (m_Looper.HasTask()) {
				m_Looper.ReleaseTask();
			}
			if (m_PresentationBound) {
				m_Platform.UnbindPresentation();
				m_PresentationBound = false;
			}
			if (m_Display) {
				m_Display->Close();
				m_Display.reset();
			}
			m_Looper.Close();
			m_Clock.Close();
			m_Workers.Close();
			m_Background.Close();
			if (m_ImagePreparation) {
				m_ImagePreparation->RetirePending();
				m_ImagePreparation->Close();
			}
			if (m_MotionPreparation) {
				m_MotionPreparation->RetirePending();
				m_MotionPreparation->Close();
			}
			if (m_FontPreparation) {
				m_FontPreparation->RetirePending();
				m_FontPreparation->Close();
			}
			m_Platform.Close();
			m_Events.Close();
			m_Closed = true;
			if (const auto failure = std::exchange(m_CloseFailure, {})) {
				std::rethrow_exception(failure);
			}
		}

	private:
		void RememberWindowPreferences() {
			if (!m_Run.Configuration || m_Run.ConfigurationWorkspace.empty()) {
				return;
			}
			const auto geometry = m_Surface.Geometry();
			const auto width = std::round(geometry.WindowedWidthPoints);
			const auto height = std::round(geometry.WindowedHeightPoints);
			if (!std::isfinite(width) || !std::isfinite(height) || width < 1 || height < 1 || width > 16384 || height > 16384) {
				throw std::runtime_error("actual windowed content dimensions exceed managed configuration limits");
			}
			auto configuration = *m_Run.Configuration;
			if (geometry.WindowedSizeChanged) {
				configuration.WindowWidth = static_cast<std::uint32_t>(width);
				configuration.WindowHeight = static_cast<std::uint32_t>(height);
			}
			configuration.Fullscreen = geometry.Fullscreen;
			const app::LaunchConfigurationStore store(m_Run.ConfigurationWorkspace);
			store.Save(configuration);
			m_Run.Configuration = std::move(configuration);
		}

		void PollPrivateCommands() {
			std::array<char, 256> buffer{};
			const auto count = m_Stdin.Read(buffer);
			for (const auto command : std::span(buffer).first(count)) {
				if (command == ' ' || command == '\t' || command == '\r' || command == '\n') {
					continue;
				}
				if (m_PrivateCommands == kMaximumPrivateCommands) {
					m_Stdin.Disable();
					std::cerr << "private stdin command bound reached\n";
					break;
				}
				++m_PrivateCommands;
				if (command == 'r') {
					std::cerr << "private stdin Return queued=" << m_Driver->QueuePrivateReturnPulse() << '\n';
				} else if (command == 'm') {
					std::cerr << "private stdin mouse queued=" << m_Driver->QueuePrivateMouseMove() << '\n';
				} else if (command == 'c') {
					if (m_CaptureRequests < kMaximumCaptureRequests) {
						if (m_PendingCaptures == 0) {
							const auto submission = m_Display->Cycle().LastSubmission();
							m_LastCommandCaptureSubmission = submission ? submission->Identity : 0;
						}
						++m_CaptureRequests;
						++m_PendingCaptures;
						std::cerr << "private stdin capture queued=" << m_CaptureRequests << '\n';
					} else {
						std::cerr << "private stdin capture bound reached\n";
					}
				} else {
					std::cerr << "private stdin ignored command byte=" << static_cast<unsigned int>(static_cast<unsigned char>(command)) << '\n';
				}
			}
		}

		void CapturePrivateCommand() {
			if (m_PendingCaptures == 0) {
				return;
			}
			const auto submission = m_Display->Cycle().LastSubmission();
			if (!submission || submission->Owner == nullptr || submission->Identity == 0 || submission->Identity == m_LastCommandCaptureSubmission) {
				return;
			}
			--m_PendingCaptures;
			m_LastCommandCaptureSubmission = submission->Identity;
			try {
				CaptureLastFrame(true);
			} catch (const std::exception& error) {
				std::cerr << "private stdin capture request failed: " << error.what() << '\n';
			}
		}

		void WriteNumberedCapture(const render::ColorPassPixels& pixels, std::uint64_t submission) {
			constexpr std::size_t kMaximumCapturePixels = 16777216;
			if (pixels.Width == 0 || pixels.Height == 0 || pixels.Width > kMaximumCapturePixels / pixels.Height || pixels.Rgba.size() != std::size_t(pixels.Width) * pixels.Height * 4) {
				throw std::runtime_error("private numbered capture exceeds its bounded actual RGBA extent");
			}
			const auto directory = m_Run.SaveDirectory.parent_path() / "original-startup-frame-capture";
			std::filesystem::create_directories(directory);
			std::filesystem::path destination;
			int descriptor = -1;
			for (std::uint32_t attempt = 0; attempt < kMaximumCaptureFileAttempts; ++attempt) {
				destination = directory / ("frame-" + std::to_string(m_NextCaptureNumber++) + ".ppm");
				descriptor = open(destination.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
				if (descriptor >= 0) {
					break;
				}
				if (errno != EEXIST) {
					throw std::runtime_error("private numbered capture could not be created errno=" + std::to_string(errno));
				}
			}
			if (descriptor < 0) {
				throw std::runtime_error("private numbered capture exhausted its bounded unused filenames");
			}
			PrivateCaptureDescriptor output(descriptor);
			const auto header = "P6\n" + std::to_string(pixels.Width) + " " + std::to_string(pixels.Height) + "\n255\n";
			output.Write({reinterpret_cast<const std::uint8_t*>(header.data()), header.size()});
			std::vector<std::uint8_t> rgb(pixels.Rgba.size() / 4 * 3);
			for (std::size_t source = 0, target = 0; source < pixels.Rgba.size(); source += 4, target += 3) {
				std::copy_n(pixels.Rgba.data() + source, 3, rgb.data() + target);
			}
			output.Write(rgb);
			output.Finish();
			std::cerr << "private numbered primary capture path=" << destination << " width=" << pixels.Width << " height=" << pixels.Height << " submission=" << submission << '\n';
		}

		void InstallFinishCloseReceiver() {
			core::ApplicationEventCallbackOperations operations;
			// Only Close is registered; these slots retain their actual native null registrations.
			operations.Suspend = [] {
				throw std::runtime_error("unregistered original application suspend receiver was invoked");
			};
			operations.Resume = [] {
				throw std::runtime_error("unregistered original application resume receiver was invoked");
			};
			operations.Resize = [](auto) {
				throw std::runtime_error("unregistered original application resize receiver was invoked");
			};
			operations.Close = [this] {
				if (!m_Driver || m_Driver->IsClosed()) {
					throw std::runtime_error("original finish callback requires its actual live script driver");
				}

				return m_Driver->EvaluateFinishRequest(m_Events.State().CloseCode);
			};
			m_CloseCallbacks = std::make_unique<core::ApplicationEventCallbacks>(m_Events, std::move(operations));
			if (auto* previous = m_Events.ExchangeCallback(core::ApplicationEventSlot::Close, m_CloseCallbacks.get()); previous != nullptr) {
				static_cast<void>(m_Events.ExchangeCallback(core::ApplicationEventSlot::Close, previous));
				m_CloseCallbacks.reset();
				throw std::runtime_error("original finish callback requires the unused actual close registration");
			}
		}

		void ReportRootPhaseStall(std::string_view phase, std::chrono::steady_clock::time_point begin) {
			const auto end = std::chrono::steady_clock::now();
			if (m_Run.Headless && m_RootPhaseTimingRows < kMaximumRootPhaseTimingRows) {
				auto& samples = phase == "Update" ? m_RootUpdateSamples : m_RootDrawSamples;
				const auto count = phase == "Update" ? m_RootUpdates : m_RootDraws;
				samples[(count - 1) % samples.size()] = std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin).count();
				if (count % samples.size() == 0) {
					auto sorted = samples;
					std::sort(sorted.begin(), sorted.end());
					std::int64_t total = 0;
					for (const auto duration : sorted) {
						total += duration;
					}
					++m_RootPhaseTimingRows;
					std::cerr << "actualRootPhaseTiming sequence=" << m_RootPhaseTimingRows << " phase=" << phase << " first=" << count - samples.size() + 1 << " last=" << count << " samples=" << samples.size() << " totalNs=" << total << " minNs=" << sorted.front() << " medianNs=" << sorted[sorted.size() / 2] << " p95Ns=" << sorted[sorted.size() * 95 / 100] << " maxNs=" << sorted.back() << " wall100ns=" << ReadMovieClock100ns(m_Counter) << " rootUpdates=" << m_RootUpdates << " rootDraws=" << m_RootDraws << '\n';
				}
			}
			if (m_RootPhaseStallRows == kMaximumRootPhaseStallRows || end - begin <= std::chrono::milliseconds(100)) {
				return;
			}
			const auto begin_100ns = std::chrono::duration_cast<std::chrono::nanoseconds>(begin.time_since_epoch()).count() / 100;
			const auto end_100ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end.time_since_epoch()).count() / 100;
			++m_RootPhaseStallRows;
			std::cerr << "actualRootPhaseStall sequence=" << m_RootPhaseStallRows << " phase=" << phase << " steadyBegin100ns=" << begin_100ns << " steadyEnd100ns=" << end_100ns << " elapsed100ns=" << end_100ns - begin_100ns << " rootUpdates=" << m_RootUpdates << " rootDraws=" << m_RootDraws << " headless=" << m_Run.Headless << '\n';
		}

		[[nodiscard]] scene::SerialTaskCallbacks RootCallbacks() {
			scene::SerialTaskCallbacks callbacks;
			callbacks.Update = [this](auto& root) {
				if (!m_Driver || !m_Display) {
					throw std::runtime_error("actual original root Update has no installed containing driver/Display");
				}
				const auto phase_begin = std::chrono::steady_clock::now();
				AdvanceSilentAudio();
				ObserveAudioTiming();
				m_Driver->Update();
				++m_RootUpdates;
				if (!m_Driver->LastNativeError().empty()) {
					RequestSoundRetirement();
					if (m_Driver->CanClose() && m_Sound.CanClose()) {
						root.SetStatus(scene::TaskStatus::Retire);
					}
				}
				ReportRootPhaseStall("Update", phase_begin);
			};
			callbacks.Draw = [this](auto&, auto argument) {
				const auto phase_begin = std::chrono::steady_clock::now();
				m_Driver->Draw(argument);
				++m_RootDraws;
				if (!m_Driver->SeekHoldsPresentation()) {
					m_SeekPresentationDrawReady = true;
				}
				ReportRootPhaseStall("Draw", phase_begin);
			};
			callbacks.CanRelease = [this] {
				return (!m_Driver || m_Driver->CanClose()) && m_Sound.CanClose();
			};
			callbacks.Release = [this] {
				// The actual containing owner retains Driver/cache through the final wait's live Sound auxiliary callback.
				// Close retires that graph after the OS loop returns, before its receiving owners disappear.
				++m_RootRetirements;
			};

			return callbacks;
		}

		[[nodiscard]] scene::ParallelTaskSet& HookGroup(std::size_t index) {
			return index == 2 ? m_Looper.Group(core::LooperFrameGroup::VSync) : m_Looper.Hooks().Group(scene::LooperHookPhase::PostLoop);
		}

		void RequestSoundRetirement() {
			if (!m_SoundCloseRequested && !m_Sound.IsClosed()) {
				m_Sound.RequestClose();
				m_SoundCloseRequested = true;
			}
		}

		void AwaitSoundRetirement() {
			m_Looper.RequireIdleContainingPhase();
			RequestSoundRetirement();
			const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
			while (!m_Sound.CanClose()) {
				if (std::chrono::steady_clock::now() >= deadline) {
					throw std::runtime_error("original Sound shutdown still retains actual worker/callback/resource borrowers after its bounded idle receiving poll");
				}
				if (m_Driver) {
					if (m_Driver->IsClosed()) {
						throw std::runtime_error("pending Sound shutdown outlived its actual Driver receiving owner");
					}
					m_Driver->AdvanceReceivingRetirement();
				}
				// Preserve actual postloop Sound-before-background order, without another Looper/script/sample-clock visit.
				m_Sound.AdvancePostLoop();
				m_Background.PostLoop();
				++m_SoundRetirementPolls;
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}
			if (m_SoundRetirementPolls != 0) {
				std::cerr << "original Sound retirement idle_receiving_polls=" << m_SoundRetirementPolls << "; no script/frame/sample-clock advances\n";
			}
		}

		void WriteSilentAudioProgress(std::ostream& output) const {
			output << (m_Run.Headless ? "silentAudio" : "deviceAudio") << " rendered_master_frames=" << m_Output->RenderedFrames() << " movie_rendered_frames=" << m_MovieAudio->RenderedFrames() << " clock_fraction=" << m_AudioFraction << " starved_renders=" << m_AudioStarvedRenders << " prefilling_renders=" << m_AudioPrefillingRenders << " last_master_before=" << m_LastAudioRender.MasterFrameBefore << " last_master_after=" << m_LastAudioRender.MasterFrameAfter << (m_Run.Headless ? "; actual typed manual offline output only, frame-quantized service, no hardware device\n" : "; actual owner-thread manual mix feeds the bounded device ring\n");
			for (std::size_t index = 0; index < m_LastAudioRender.SourceCount; ++index) {
				const auto& source = m_LastAudioRender.Sources[index];
				output << "silentAudio source_type=" << static_cast<std::uint32_t>(source.Source.Type) << " slot=" << static_cast<std::uint32_t>(source.Source.Slot) << " source_rate=" << source.SamplesPerSecond << " streaming=" << source.Streaming << " callback=" << source.CallbackObserved << " requested_source_frames=" << source.RequestedFrames << " consumed_source_frames=" << source.ConsumedFrames << " silenced_source_frames=" << source.SilencedFrames << " status=" << static_cast<std::uint32_t>(source.Status) << " last_read=" << static_cast<std::uint32_t>(source.LastReadStatus) << " input_complete=" << source.InputComplete << '\n';
			}
			if (m_HardwareAudio) {
				try {
					const auto snapshot = m_HardwareAudio->Snapshot();
					output << "deviceAudio submitted=" << snapshot.SubmittedFrames << " requested=" << snapshot.RequestedFrames << " copied=" << snapshot.CopiedFrames << " silent=" << snapshot.SilentFrames << " buffered=" << snapshot.BufferedFrames << " device=" << snapshot.DeviceIdentity << " device_rate=" << snapshot.DeviceSamplesPerSecond << " device_channels=" << snapshot.DeviceChannels << '\n';
				} catch (const std::exception& error) {
					output << "secondary deviceAudio diagnostic: " << error.what() << '\n';
				}
			}
		}

		void ObserveAudioTiming() noexcept {
			if (m_AudioTimingRows >= 128) {
				return;
			}
			const auto visit = m_AudioTimingCalls++;
			if (visit >= 8 && visit % 30 != 0) {
				return;
			}
			++m_AudioTimingRows;
			try {
				std::cerr << "actualHostAudioTiming sequence=" << m_AudioTimingRows << " visit=" << visit << " wall100ns=" << ReadMovieClock100ns(m_Counter) << " rootUpdates=" << m_RootUpdates << " rootDraws=" << m_RootDraws << " headless=" << m_Run.Headless << " masterFrames=" << m_Output->RenderedFrames() << " movieFrames=" << m_MovieAudio->RenderedFrames() << " sourceStarvedRenders=" << m_AudioStarvedRenders << " sourcePrefillingRenders=" << m_AudioPrefillingRenders;
				if (m_HardwareAudio) {
					const auto device = m_HardwareAudio->Snapshot();
					std::cerr << " deviceSubmitted=" << device.SubmittedFrames << " deviceRequested=" << device.RequestedFrames << " deviceCopied=" << device.CopiedFrames << " deviceSilent=" << device.SilentFrames << " deviceBuffered=" << device.BufferedFrames;
				}
				std::cerr << '\n';
			} catch (...) {
			}
		}

		void AdvanceSilentAudio() {
			if (m_HardwareAudio) {
				AdvanceHardwareAudio();
				return;
			}
			if (!m_Output || !m_MovieAudio) {
				throw std::runtime_error("silent observation clock requires the actual offline Sound and Movie outputs");
			}

			const auto now = std::chrono::steady_clock::now();
			const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(now - m_LastAudioClock).count();
			m_LastAudioClock = now;
			if (elapsed < 0) {
				throw std::runtime_error("silent observation clock moved backwards");
			}

			constexpr std::uint64_t kNanosecondsPerSecond = 1000000000;
			const auto nanoseconds = static_cast<std::uint64_t>(elapsed);
			const auto rate = m_Run.Offline.SamplesPerSecond;
			if (nanoseconds > (std::numeric_limits<std::uint64_t>::max() - m_AudioFraction) / rate) {
				throw std::runtime_error("silent observation clock exceeds its bounded sample accumulator");
			}

			const auto scaled = nanoseconds * rate + m_AudioFraction;
			auto remaining = scaled / kNanosecondsPerSecond;
			m_AudioFraction = scaled % kNanosecondsPerSecond;
			while (remaining != 0) {
				const auto frames = static_cast<std::size_t>(std::min<std::uint64_t>(remaining, m_Run.Offline.MaximumFrames));
				// Manual offline rendering advances real sources; discard samples without opening a hardware audio device.
				m_LastAudioRender = m_Output->RenderTyped(std::span(m_AudioScratch).first(frames * m_Run.Offline.Channels));
				if (m_LastAudioRender.MasterFrames != frames || m_LastAudioRender.SourceCount > m_LastAudioRender.Sources.size()) {
					throw std::runtime_error("silent observation typed output returned a different bounded master/source extent");
				}
				if (m_LastAudioRender.Status == audio::OfflineSoundRenderStatus::SourceFailed) {
					throw std::runtime_error("silent observation typed output reported actual source failure");
				}
				m_AudioStarvedRenders += m_LastAudioRender.Status == audio::OfflineSoundRenderStatus::Starved ? 1u : 0u;
				m_AudioPrefillingRenders += m_LastAudioRender.Status == audio::OfflineSoundRenderStatus::Prefilling ? 1u : 0u;
				// Submitted movie audio continues on the real host phase even if its LayerMovie is hidden/paused.
				m_MovieAudio->RenderSilent(static_cast<std::uint32_t>(frames));
				remaining -= frames;
			}
		}

		void AdvanceHardwareAudio() {
			if (m_Run.Headless || !m_HardwareAudio || !m_Output || !m_MovieAudio || !m_MovieDelivery) {
				throw std::runtime_error("audible PCM service requires its actual visible output, manual mixers and Movie delivery owner");
			}
			// Drain even when the existing ring is already at target: callback delivery drives Movie clocks.
			static_cast<void>(m_MovieDelivery->Poll());
			// One snapshot bounds this owner phase; never chase a concurrent device consumer indefinitely.
			auto remaining = m_HardwareAudio->FramesToTarget();
			while (remaining != 0) {
				const auto frames = std::min<std::size_t>(remaining, m_Run.Offline.MaximumFrames);
				m_LastAudioRender = m_MovieDelivery->RenderAndSubmit(static_cast<std::uint32_t>(frames), m_Driver->SeekHoldsPresentation());
				m_AudioStarvedRenders += m_LastAudioRender.Status == audio::OfflineSoundRenderStatus::Starved ? 1u : 0u;
				m_AudioPrefillingRenders += m_LastAudioRender.Status == audio::OfflineSoundRenderStatus::Prefilling ? 1u : 0u;
				remaining -= frames;
			}
		}

		void RegisterLooperHooks() {
			const core::LooperContainingMutation mutation(m_Looper, m_Workers);
			m_Mutation = &mutation;
			try {
				for (std::size_t index = 0; index < m_HookClocks.size(); ++index) {
					scene::ParallelTaskCallbacks callbacks;
					callbacks.Update = [this, index](auto) {
						if (index == 0) {
							m_Sound.AdvancePostLoop();
						} else if (index == 1) {
							m_Background.PostLoop();
						} else {
							// Native048ef0 belongs to actual post-wait VSync; getters only read retained state.
							m_FunctionKeys.Advance(m_Platform.ReadFunctionKeySample());
						}
					};
					callbacks.CanRelease = [this, index](auto clock) {
						if (!m_RetiringHooks || !m_Mutation || !m_Registered[index] || clock != m_HookClocks[index]) {
							return false;
						}
						m_Mutation->RequireOwner(m_Looper, m_Workers);

						return true;
					};
					callbacks.Release = [this, index](auto clock) {
						if (!m_RetiringHooks || !m_Mutation || clock != m_HookClocks[index]) {
							throw std::runtime_error("original Sound/background hook requires its exact containing retirement owner");
						}
						m_Mutation->RequireOwner(m_Looper, m_Workers);
						m_Registered[index] = false;
					};
					m_HookClocks[index] = HookGroup(index).Append(std::move(callbacks));
					m_Registered[index] = true;
				}
			} catch (...) {
				m_Mutation = nullptr;
				throw;
			}
			m_Mutation = nullptr;
		}

		void RetireLooperHooks() {
			const core::LooperContainingMutation mutation(m_Looper, m_Workers);
			m_Mutation = &mutation;
			m_RetiringHooks = true;
			try {
				for (std::size_t index = m_HookClocks.size(); index != 0; --index) {
					if (m_Registered[index - 1]) {
						auto& group = HookGroup(index - 1);
						if (!group.CanExtract(m_HookClocks[index - 1])) {
							throw std::runtime_error("original exact Sound/background hook remains borrowed");
						}
						auto task = group.Extract(m_HookClocks[index - 1]);
						task.Release();
					}
				}
			} catch (...) {
				m_Mutation = nullptr;
				m_RetiringHooks = false;
				throw;
			}
			m_Mutation = nullptr;
			m_RetiringHooks = false;
		}

		bool InitializeSaveDirectory() {
			std::lock_guard lock(m_SaveDirectoryMutex);
			if (m_SaveDirectoryInitialized) {
				return true;
			}

			std::error_code error;
			std::filesystem::create_directories(m_Run.SaveDirectory, error);
			if (error || !std::filesystem::is_directory(m_Run.SaveDirectory, error) || error) {
				return false;
			}

			// A uniquely created probe verifies the selected rewrite directory without replacing user data.
			auto probe = (m_Run.SaveDirectory / ".sgre-write-probe-XXXXXX").string();
			const auto descriptor = mkstemp(probe.data());
			if (descriptor < 0) {
				return false;
			}

			const std::uint8_t value = 0;
			const auto written = write(descriptor, &value, sizeof(value));
			const auto closed = close(descriptor);
			const auto removed = unlink(probe.c_str());
			m_SaveDirectoryInitialized = written == sizeof(value) && closed == 0 && removed == 0;

			return m_SaveDirectoryInitialized;
		}

		static constexpr std::uint32_t kMaximumRootPhaseTimingRows = 1024;
		std::uint32_t m_RootPhaseTimingRows = 0;
		std::array<std::int64_t, 120> m_RootUpdateSamples{};
		std::array<std::int64_t, 120> m_RootDrawSamples{};
		static constexpr std::uint32_t kMaximumRootPhaseStallRows = 64;
		std::uint32_t m_RootPhaseStallRows = 0;
		static constexpr std::uint32_t kMaximumPrivateCommands = 4096;
		static constexpr std::uint32_t kMaximumCaptureRequests = 16;
		static constexpr std::uint32_t kMaximumCaptureFileAttempts = 4096;
		PrivateStartupStdin m_Stdin;
		std::uint32_t m_PrivateCommands = 0;
		std::uint32_t m_CaptureRequests = 0;
		std::uint32_t m_PendingCaptures = 0;
		std::uint32_t m_NextCaptureNumber = 1;
		std::uint64_t m_LastCommandCaptureSubmission = 0;
		OriginalStartupRun m_Run;
		std::mutex m_SaveDirectoryMutex;
		bool m_SaveDirectoryInitialized = false;
		HostPerformanceCounter m_Counter;
		render::SpriteGraphicsTargets m_Graphics;
		std::shared_ptr<render::SpritePassRenderer> m_Primary;
		render::SpritePassPresentation m_Surface;
		core::ApplicationEventControl m_Events;
		MacApplicationPump m_Platform;
		core::ApplicationClockPump m_Clock;
		core::ThreadTaskFrame m_Workers;
		resource::ArchiveMounts m_Mounts;
		core::BackgroundCallManager m_Background;
		audio::SoundResourceControl m_Sound;
		scene::SerialTask m_Root;
		core::LooperFrame m_Looper;
		std::shared_ptr<scene::BitmapFontPreparationService> m_FontPreparation;
		std::shared_ptr<scene::MotionPreparationService> m_MotionPreparation;
		std::shared_ptr<scene::ImagePreparationService> m_ImagePreparation;
		std::unique_ptr<render::GraphicsDisplayFrame> m_Display;
		std::unique_ptr<MacMotionSeedSource> m_Seeds;
		std::unique_ptr<MacPcmOutput> m_HardwareAudio;
		std::unique_ptr<audio::OfflineBufferedSoundPool> m_Output;
		std::unique_ptr<media::MovieSilentAudioRegistry> m_MovieAudio;
		// Reverse destruction retires the output binding before all three actual enclosing audio owners.
		std::unique_ptr<media::MoviePcmDelivery> m_MovieDelivery;
		std::vector<float> m_AudioScratch;
		std::chrono::steady_clock::time_point m_LastAudioClock;
		std::uint64_t m_AudioFraction = 0;
		audio::OfflineSoundRenderResult m_LastAudioRender;
		std::size_t m_AudioTimingCalls = 0;
		std::size_t m_AudioTimingRows = 0;
		std::uint64_t m_AudioStarvedRenders = 0;
		std::uint64_t m_AudioPrefillingRenders = 0;
		bool m_SoundCloseRequested = false;
		std::size_t m_SoundRetirementPolls = 0;
		std::unique_ptr<core::ApplicationAuxiliaryRegistration> m_Auxiliary;
		input::FunctionKeyState m_FunctionKeys;
		// Native2d4840 seeds only at the first actual trial; this owner encloses all driver tasks.
		scene::EmoteWindRandom m_WindRandom{ReadHostUnixSeconds};
		std::unique_ptr<OriginalScriptStartup> m_Driver;
		std::unique_ptr<core::ApplicationEventCallbacks> m_CloseCallbacks;
		std::array<scene::ParallelTaskHandle, 3> m_HookClocks;
		std::array<bool, 3> m_Registered{};
		const core::LooperContainingMutation* m_Mutation = nullptr;
		std::size_t m_Iterations = 0;
		std::size_t m_RootUpdates = 0;
		std::size_t m_RootDraws = 0;
		bool m_WindowShown = false;
		bool m_SeekPresentationDrawReady = false;
		std::size_t m_RootRetirements = 0;
		bool m_PresentationBound = false;
		bool m_RetiringHooks = false;
		std::exception_ptr m_CloseFailure;
		bool m_Closed = false;
	};

} // namespace sgre::platform

static std::uint32_t StartupUnsignedArgument(std::string_view value) {
	std::uint32_t result = 0;
	const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
	if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() || result == 0) {
		throw std::runtime_error("startup requires positive explicit numeric target/offline parameters");
	}

	return result;
}

static bool LegacyStartupArguments(int argument_count, char** arguments) {
	if (argument_count != 12) {
		return false;
	}
	for (int index = 1; index < argument_count; ++index) {
		if (std::string_view(arguments[index]).starts_with("--")) {
			return false;
		}
	}

	return true;
}

static std::optional<std::filesystem::path> FindStartupWorkspace(std::filesystem::path candidate) {
	while (!candidate.empty()) {
		if (std::filesystem::is_regular_file(candidate / "shaders/metal/sprite_pass.metal") && std::filesystem::is_regular_file(candidate / "shaders/metal/sprite_presentation.metal")) {
			return std::filesystem::canonical(candidate);
		}
		const auto parent = candidate.parent_path();
		if (parent == candidate) {
			break;
		}
		candidate = parent;
	}

	return {};
}

static std::filesystem::path StartupExecutablePath() {
	std::vector<char> bytes(4096);
	std::uint32_t length = static_cast<std::uint32_t>(bytes.size());
	if (_NSGetExecutablePath(bytes.data(), &length) != 0) {
		if (length == 0 || length > 65536) {
			throw std::runtime_error("actual macOS executable path exceeds bounded launch discovery");
		}
		bytes.resize(length);
		if (_NSGetExecutablePath(bytes.data(), &length) != 0) {
			throw std::runtime_error("actual macOS executable path changed during launch discovery");
		}
	}
	const auto end = std::find(bytes.begin(), bytes.end(), '\0');
	if (end == bytes.end()) {
		throw std::runtime_error("actual macOS executable path lacks its bounded terminator");
	}

	return std::filesystem::canonical(std::filesystem::path(std::string(bytes.begin(), end)));
}

static std::filesystem::path ResolveStartupWorkspace() {
	std::error_code current_error;
	const auto current = std::filesystem::current_path(current_error);
	if (!current_error) {
		if (const auto workspace = FindStartupWorkspace(current)) {
			return *workspace;
		}
	}
	if (const auto workspace = FindStartupWorkspace(StartupExecutablePath().parent_path())) {
		return *workspace;
	}

	throw std::runtime_error("cannot locate rewrite shaders beside the current workspace or actual executable ancestors");
}

static void RequireStartupSaveOverride(const std::filesystem::path& save, const std::filesystem::path& installation, const std::filesystem::path& config_directory) {
	if (std::filesystem::exists(save) && !std::filesystem::is_directory(save)) {
		throw std::runtime_error("per-run save-root exists but is not a directory");
	}
	for (auto ancestor = save; !ancestor.empty(); ancestor = ancestor.parent_path()) {
		if (std::filesystem::exists(ancestor) && std::filesystem::equivalent(ancestor, installation)) {
			throw std::runtime_error("per-run save-root must stay outside the preserved installation");
		}
		if (std::filesystem::exists(ancestor) && std::filesystem::equivalent(ancestor, config_directory)) {
			const auto relative = save.lexically_relative(ancestor);
			auto first = relative.empty() ? std::string{} : relative.begin()->string();
			for (auto& letter : first) {
				if (letter >= 'A' && letter <= 'Z') {
					letter = static_cast<char>(letter - 'A' + 'a');
				}
			}
			if (first.empty() || first == "." || first == "config.json" || first == "config.json.last-good" || first == "config.json.corrupt" || first.starts_with(".sgre-config-write-")) {
				throw std::runtime_error("per-run save-root overlaps the managed config publication namespace");
			}
		}
		if (ancestor == ancestor.parent_path()) {
			break;
		}
	}
	for (auto ancestor = config_directory; !ancestor.empty(); ancestor = ancestor.parent_path()) {
		if (std::filesystem::exists(save) && std::filesystem::equivalent(ancestor, save)) {
			throw std::runtime_error("per-run save-root must not contain the managed config directory");
		}
		if (ancestor == ancestor.parent_path()) {
			break;
		}
	}
}

static sgre::platform::OriginalStartupRun ConfiguredStartupRun(const sgre::app::LaunchArguments& options, bool& validate_only) {
	const auto workspace = ResolveStartupWorkspace();
	const sgre::app::LaunchConfigurationStore store(workspace);
	auto loaded = store.LoadOrCreate(options.Installation);
	if (!loaded.RecoveryDiagnostic.empty()) {
		std::cerr << "launch config repaired: " << loaded.RecoveryDiagnostic << '\n';
	}
	const auto& configuration = loaded.Configuration;
	sgre::platform::OriginalStartupRun run;
	run.Installation = configuration.Installation;
	run.ArchiveSeed = std::move(loaded.ArchiveSeed);
	run.SpriteShader = configuration.SpriteShader;
	run.PresentationShader = configuration.PresentationShader;
	run.SaveDirectory = configuration.SaveDirectory;
	const sgre::app::LaunchConfiguration defaults;
	run.Target.Width = options.Headless ? defaults.Width : configuration.Width;
	run.Target.Height = options.Headless ? defaults.Height : configuration.Height;
	run.Offline.SamplesPerSecond = configuration.AudioSamplesPerSecond;
	run.Offline.Channels = static_cast<std::uint8_t>(configuration.AudioChannels);
	run.Offline.MaximumFrames = 64;
	run.Headless = options.Headless;
	run.MaximumIterations = options.MaximumIterations.value_or(options.Headless ? 100000u : 0u);
	run.MaximumMilliseconds = options.MaximumMilliseconds.value_or(options.Headless ? 900000u : 0u);
	if (options.SaveDirectory) {
		run.SaveDirectory = std::filesystem::weakly_canonical(std::filesystem::absolute(*options.SaveDirectory));
		RequireStartupSaveOverride(run.SaveDirectory, run.Installation, store.ConfigurationPath().parent_path());
	}
	run.DebugStartInGame = options.Jump.has_value() || options.DebugStartInGame.value_or(loaded.Configuration.DebugStartInGame);
	if (run.DebugStartInGame) {
		sgre::scenario::ScenarioSearchCatalog scenarios({run.Installation, run.ArchiveSeed});
		const auto target = scenarios.ValidateJump(options.Jump ? options.Jump->Storage : "reSG00_01.ks", options.Jump ? options.Jump->TargetLine : 135);
		sgre::script::OriginalSceneSeekTarget seek;
		seek.Storage = target.CanonicalStorage;
		seek.RequestedLine = target.SourceLine;
		seek.SnapshotLine = target.SnapshotLine;
		seek.Point = target.Point;
		run.SceneSeek = std::move(seek);
		std::cerr << "original stateful seek validated: storage=" << std::quoted(target.CanonicalStorage) << " line=" << target.SourceLine << " snapshot_line=" << target.SnapshotLine << " snapshot_index=" << target.SnapshotIndex << " point=" << target.Point << '\n';
	}
	loaded.Configuration = sgre::app::ApplyLaunchPreferences(std::move(loaded.Configuration), options);
	if (options.Fullscreen || options.WindowCentered) {
		store.Save(loaded.Configuration);
	}
	run.Presentation.VisibleWindow = !run.Headless;
	run.Presentation.WindowWidthPoints = loaded.Configuration.WindowWidth;
	run.Presentation.WindowHeightPoints = loaded.Configuration.WindowHeight;
	run.Presentation.WindowCentered = loaded.Configuration.WindowCentered;
	run.Presentation.Fullscreen = !run.Headless && loaded.Configuration.Fullscreen;
	run.ConfigurationWorkspace = workspace;
	run.Configuration = loaded.Configuration;
	validate_only = options.ValidateOnly;
	if (validate_only) {
		std::cout << "launch config valid: " << store.ConfigurationPath() << "; width=" << run.Target.Width << "; height=" << run.Target.Height << "; audio_rate=" << run.Offline.SamplesPerSecond << "; audio_channels=" << static_cast<std::uint32_t>(run.Offline.Channels) << "; debug_start_in_game=" << run.DebugStartInGame << '\n';
		if (run.SceneSeek) {
			std::cout << "launch stateful seek: storage=" << std::quoted(run.SceneSeek->Storage) << " line=" << run.SceneSeek->RequestedLine << " snapshot_line=" << run.SceneSeek->SnapshotLine << " point=" << run.SceneSeek->Point << '\n';
		}
	}

	return run;
}

static void WriteStartupBuildIdentity() {
#if defined(__has_feature)
#if __has_feature(address_sanitizer)
	constexpr std::string_view kAddressSanitizer = "enabled";
#else
	constexpr std::string_view kAddressSanitizer = "disabled";
#endif
#if __has_feature(undefined_behavior_sanitizer)
	constexpr std::string_view kUndefinedBehaviorSanitizer = "enabled";
#else
	constexpr std::string_view kUndefinedBehaviorSanitizer = "disabled";
#endif
#else
	constexpr std::string_view kAddressSanitizer = "unknown";
	constexpr std::string_view kUndefinedBehaviorSanitizer = "unknown";
#endif
#if defined(__OPTIMIZE__)
	constexpr std::string_view kOptimization = "enabled";
#else
	constexpr std::string_view kOptimization = "disabled";
#endif
#if defined(NDEBUG)
	constexpr std::string_view kAssertions = "disabled";
#else
	constexpr std::string_view kAssertions = "enabled";
#endif
#if defined(__clang_version__)
	constexpr std::string_view kCompiler = __clang_version__;
#elif defined(__VERSION__)
	constexpr std::string_view kCompiler = __VERSION__;
#else
	constexpr std::string_view kCompiler = "unknown";
#endif
#if defined(SGRE_PRIVATE_STARTUP_SOURCE_SHA256)
	constexpr std::string_view kSourceHash = SGRE_PRIVATE_STARTUP_SOURCE_SHA256;
	static_assert(kSourceHash.size() == 64);
#else
	constexpr std::string_view kSourceHash = "unprovided";
#endif

	// Compiler observations describe this host translation unit, not linked-library flags or measured speed.
	std::cerr << "actualStartupBuildIdentity scope=hostTranslationUnit hostRevision=mouse-phase-build-identity-v1 sourceSha256=" << kSourceHash << " addressSanitizer=" << kAddressSanitizer << " undefinedBehaviorSanitizer=" << kUndefinedBehaviorSanitizer << " optimization=" << kOptimization << " assertions=" << kAssertions << " compiler=\"" << kCompiler << "\"\n";
}

int main(int argc, char** argv) {
	@autoreleasepool {
		try {
			WriteStartupBuildIdentity();
			sgre::platform::OriginalStartupRun run;
			bool validate_only = false;
			if (LegacyStartupArguments(argc, argv)) {
				run.Installation = argv[1];
				sgre::io::FileReader seed_file(argv[2]);
				if (seed_file.Size() == 0 || seed_file.Size() > 65536) {
					throw std::runtime_error("original archive seed input exceeds its bounded text domain");
				}
				const auto seed_bytes = seed_file.Read(0, static_cast<std::size_t>(seed_file.Size()));
				run.ArchiveSeed.assign(seed_bytes.begin(), seed_bytes.end());
				while (!run.ArchiveSeed.empty() && (run.ArchiveSeed.back() == '\n' || run.ArchiveSeed.back() == '\r')) {
					run.ArchiveSeed.pop_back();
				}
				run.SpriteShader = argv[3];
				run.PresentationShader = argv[4];
				run.Target.Width = StartupUnsignedArgument(argv[5]);
				run.Target.Height = StartupUnsignedArgument(argv[6]);
				run.Offline.SamplesPerSecond = StartupUnsignedArgument(argv[7]);
				const auto channels = StartupUnsignedArgument(argv[8]);
				if (channels > 8) {
					throw std::runtime_error("startup offline channel count exceeds actual supported domain");
				}
				run.Offline.Channels = static_cast<std::uint8_t>(channels);
				run.Offline.MaximumFrames = 64;
				run.MaximumIterations = StartupUnsignedArgument(argv[9]);
				run.MaximumMilliseconds = StartupUnsignedArgument(argv[10]);
				run.SaveDirectory = std::filesystem::weakly_canonical(argv[11]);
				const auto installation = std::filesystem::weakly_canonical(run.Installation);
				const auto relative_save = run.SaveDirectory.lexically_relative(installation);
				if (run.SaveDirectory.empty() || relative_save.empty() || *relative_save.begin() != "..") {
					throw std::runtime_error("rewrite save directory must be separate from the preserved reference installation");
				}
			} else {
				std::vector<std::string_view> arguments;
				for (int index = 1; index < argc; ++index) {
					arguments.emplace_back(argv[index]);
				}
				const auto options = sgre::app::ParseLaunchArguments(arguments);
				if (options.Help) {
					std::cout << "usage: sgre-game [GAME_ROOT] [--skip-startup|--normal-start] [--jump SCENE.ks:LINE] [--fullscreen|--windowed] [--center-window|--no-center-window] [--headless] [--validate-config] [--maximum-milliseconds N] [--maximum-iterations N] [--save-root PATH]\n  --jump follows original gameplay to a validated source line for this run; authentic choices may require input.\n";

					return 0;
				}
				run = ConfiguredStartupRun(options, validate_only);
			}
			if (validate_only) {
				return 0;
			}
			// Explicit application policy; bounded render fixtures keep their positive API defaults.
			run.Target.MaxTextureBytes = 0;
			sgre::platform::HiddenOriginalApplication application(std::move(run));
			application.Run();
			application.Close();

			return 0;
		} catch (const std::exception& error) {
			std::cerr << "original startup failed: " << error.what() << '\n';

			return 1;
		}
	}
}
