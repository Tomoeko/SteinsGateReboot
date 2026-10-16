#include "sgre/scene/emote_scene_player.hpp"

#include "sgre/scene/emote_color_transition.hpp"
#include "sgre/scene/emote_coordinate_transition.hpp"
#include "sgre/scene/emote_rotation_transition.hpp"
#include "sgre/scene/emote_scale_transition.hpp"
#include "sgre/scene/emote_timeline_scheduler.hpp"
#include "sgre/scene/emote_variable_controls.hpp"
#include "sgre/scene/motion_assets.hpp"
#include "sgre/scene/motion_manager_runtime.hpp"
#include "sgre/scene/motion_root_scene.hpp"
#include "sgre/scene/motion_scene_player.hpp"

#include <cmath>
#include <exception>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <variant>

namespace sgre::scene {
	static const resource::PsbValue& EmoteOwnerField(const resource::PsbValue& value, std::string_view name) {
		if (const auto* result = value.Find(name)) {
			return *result;
		}
		throw std::runtime_error("containing Emote requires actual metadata " + std::string(name));
	}

	static std::string_view EmoteOwnerName(const resource::PsbValue& value) {
		const auto* text = std::get_if<std::string>(&value.Data);
		if (text == nullptr || text->size() > kMaximumMotionCommandNameBytes) {
			throw std::runtime_error("containing Emote base selection requires bounded actual strings");
		}
		return std::string_view(*text).substr(0, text->find('\0'));
	}

	static float EmoteOwnerNumber(const resource::PsbValue& value) {
		float result = 0;
		if (const auto* real = std::get_if<double>(&value.Data)) {
			result = static_cast<float>(*real);
		} else if (const auto* integer = std::get_if<std::int64_t>(&value.Data)) {
			result = static_cast<float>(*integer);
		} else if (const auto* boolean = std::get_if<bool>(&value.Data)) {
			result = *boolean ? 1.0f : 0.0f;
		} else {
			throw std::runtime_error("containing Emote initialization requires actual numeric metadata");
		}
		if (!std::isfinite(result)) {
			throw std::runtime_error("containing Emote initialization exceeds finite metadata domain");
		}
		return result;
	}

	class EmoteScenePlayer::Implementation {
	public:
		class RuntimeBorrow {
		public:
			explicit RuntimeBorrow(MotionManagerRuntime& runtime) : Runtime(runtime) {
				Runtime.AcquirePlayer();
			}
			~RuntimeBorrow() noexcept {
				Release();
			}
			void Release() noexcept {
				if (Live) {
					Runtime.ReleasePlayer();
					Live = false;
				}
			}
			MotionManagerRuntime& Runtime;
			bool Live = true;
		};

		class State {
		public:
			State(MotionManagerRuntime& runtime, MotionProjectId project, std::function<std::uint32_t()> entropy, std::function<void(const MotionPlayerEvent&)> listener, EmoteWindRandom* wind_random)
			    : Runtime(runtime), Asset(runtime.ReadRegisteredAsset(project)), Random(runtime.NextUnitFloat()), Commands(runtime.Manager(), [this](const auto& selected, auto) {
				      Plain->Rebuild(selected);
			      },
			                                                                                                          [this] {
				                                                                                                          Plain->CaptureJoin();
			                                                                                                          }) {
				if (!entropy || !listener || !runtime.SceneAssets().ContainsPrepared(Asset) || !runtime.Manager().Contains(project) || runtime.Manager().Identity(project) != Asset.Identity || &runtime.Manager().Resource(project) != Asset.Model.get()) {
					throw std::runtime_error("containing Emote requires actual registered asset, shared manager and entropy/listener services");
				}
				const auto& metadata = EmoteOwnerField(Asset.Document->Root, "metadata");
				const auto& base = EmoteOwnerField(metadata, "base");
				auto program = ReadEmoteVariableProgram(*Asset.Document);
				if (!program.UnsupportedFamilies.empty()) {
					throw std::runtime_error("containing Emote requires recovered " + program.UnsupportedFamilies.front());
				}
				Scale = EmoteOwnerNumber(EmoteOwnerField(metadata, "scale"));
				if (!(Scale > 0) || !std::isfinite(1.0f / Scale)) {
					throw std::runtime_error("containing Emote scale requires its actual finite positive metadata calibration");
				}
				Wind.emplace(Scale, wind_random);
				Plain = std::make_unique<MotionScenePlayer>(runtime, Commands, Root, Render, std::move(listener), &Random);
				Commands.SetCharacter(EmoteOwnerName(EmoteOwnerField(base, "chara")));
				Commands.Play(EmoteOwnerName(EmoteOwnerField(base, "motion")));
				if (!Commands.SelectedProject() || Commands.SelectedProject()->Project != project || Plain->Scene() == nullptr) {
					throw std::runtime_error("containing Emote metadata.base resolved outside its actual registered document");
				}
				Global.Mirror = EmoteOwnerNumber(EmoteOwnerField(metadata, "mirror")) != 0;
				Root.SetFlip(Global.Mirror, false);
				// Native2c6e50 evaluates the real plain owner once at0 before creating channel/physics owners.
				Plain->Progress(0);
				Variables = std::make_unique<EmoteVariableControls>(Commands, [this] {
					RequireSceneMutable();
				},
				    std::move(program), entropy, [this](float step) {
					    const auto coordinate = PrepareEmoteDefaultCoordinatePublication(Coordinate.Advance(step));
					    const auto color = Color.Advance(step);
					    const auto scale = GlobalScale.Advance(step);
					    const auto rotation = RotationControl.Advance(step);
					    // Native51ee00 compares ColorFilter, rather than the previously published modulation color.
					    if (color != Render.ColorFilter) {
						    Root.TransferModulationColor(color);
					    }
					    Root.SetCoordinate({coordinate[0], coordinate[1]});
					    Root.SetZoom(scale, scale);
					    Root.SetAngleRadians(rotation);
					    // Native2d2f70 advances the optional wind after2cc510 for each identical property substep.
					    Wind->Advance(step);
				    });
				if (!Variables->MissingFamilies().empty()) {
					throw std::runtime_error("containing Emote requires recovered " + Variables->MissingFamilies().front());
				}
				Timelines = std::make_unique<EmoteTimelineScheduler>(*Variables, *Asset.Document, [this] {
					RequireSceneMutable();
				});
				Physics = std::make_unique<EmoteVelvetyOwner>(Root, Commands, *Plain, *Asset.Document, *Variables, entropy);
			}

			void RequireSceneMutable() const {
				if (Thread != std::this_thread::get_id() || !Plain || !Plain->CanMutate() || !Runtime.ResourceBorrowsLive()) {
					throw std::runtime_error("containing Emote requires its live unqueued actual plain/registration owners");
				}
			}
			void RequireMutable() const {
				RequireSceneMutable();
				if (Operating || !Valid) {
					throw std::runtime_error("containing Emote cannot reenter an active or failed native phase");
				}
			}
			bool CanClose() const noexcept {
				return Thread == std::this_thread::get_id() && !Operating && (!Plain || Plain->CanMutate());
			}
			void Progress(float elapsed) {
				RequireMutable();
				if (!std::isfinite(elapsed) || elapsed < 0 || elapsed > 4096 * 1.5f) {
					throw std::runtime_error("containing Emote requires recovered bounded nonnegative native delta");
				}
				Operating = true;
				try {
					ProgressNative(elapsed);
					Operating = false;
				} catch (...) {
					Operating = false;
					Valid = false;
					throw;
				}
			}
			void ProgressNative(float elapsed) {
				Timelines->Update(elapsed);
				Variables->UpdateWithTimelines(elapsed, *Timelines);
				Plain->Progress(elapsed);
				// Native2d33b8 follows plain composition/publication and does not run at an exactly zero delta.
				if (elapsed != 0) {
					// Native2ca710 reads the separate extended angular owner at+2c8, whose admitted default is zero.
					Physics->Update(elapsed, Global);
				}
			}

			MotionManagerRuntime& Runtime;
			const LoadedMotionAsset& Asset;
			MotionPlayerRandom Random;
			MotionPlayerCommands Commands;
			MotionRootScene Root;
			MotionPlayerRenderControls Render;
			std::unique_ptr<MotionScenePlayer> Plain;
			EmoteCoordinateTransition Coordinate;
			EmoteColorTransition Color;
			EmoteScaleTransition GlobalScale;
			EmoteRotationTransition RotationControl;
			std::optional<EmoteWindOwner> Wind;
			std::unique_ptr<EmoteVariableControls> Variables;
			std::unique_ptr<EmoteTimelineScheduler> Timelines;
			// Physics/listeners and leases retire before variables, selected nodes and the shared runtime borrow.
			std::unique_ptr<EmoteVelvetyOwner> Physics;
			EmoteVelvetyGlobalControls Global;
			const std::thread::id Thread = std::this_thread::get_id();
			float Scale = 1;
			float BustScale = 1;
			float HairScale = 1;
			float PartsScale = 1;
			bool Operating = false;
			bool Valid = true;
		};

		Implementation(MotionManagerRuntime& runtime, MotionProjectId project, std::function<std::uint32_t()> entropy, std::function<void(const MotionPlayerEvent&)> listener, EmoteWindRandom* wind_random)
		    : Borrow(runtime), Owner(std::make_unique<State>(runtime, project, std::move(entropy), std::move(listener), wind_random)) {
		}
		State& Require() const {
			if (!Owner) {
				throw std::runtime_error("containing Emote player has closed");
			}
			return *Owner;
		}
		RuntimeBorrow Borrow;
		std::unique_ptr<State> Owner;
	};

	EmoteScenePlayer::EmoteScenePlayer(MotionManagerRuntime& runtime, MotionProjectId project, std::function<std::uint32_t()> entropy, std::function<void(const MotionPlayerEvent&)> listener, EmoteWindRandom* wind_random)
	    : m_Implementation(std::make_unique<Implementation>(runtime, project, std::move(entropy), std::move(listener), wind_random)) {
	}
	EmoteScenePlayer::~EmoteScenePlayer() noexcept {
		if (!CanClose()) {
			std::terminate();
		}
	}
	void EmoteScenePlayer::Update() {
		auto& state = m_Implementation->Require();
		state.RequireMutable();
		state.Progress(state.Runtime.ReadFrameDelta());
	}
	void EmoteScenePlayer::Progress(float elapsed_ticks) {
		m_Implementation->Require().Progress(elapsed_ticks);
	}
	void EmoteScenePlayer::Draw() {
		auto& state = m_Implementation->Require();
		state.RequireMutable();
		state.Plain->Draw();
		// Accepted metadata has no acquired particle players; active visual emission is rejected at construction.
	}
	void EmoteScenePlayer::SetVisible(bool visible) {
		auto& state = m_Implementation->Require();
		state.RequireMutable();
		state.Root.SetVisible(visible);
	}
	void EmoteScenePlayer::SetCoordinate(const std::array<float, 2>& coordinate, float duration, float easing) {
		auto& state = m_Implementation->Require();
		state.RequireMutable();
		auto next = state.Coordinate;
		next.Set(coordinate, duration, easing);
		state.Variables->MarkPropertiesDirty();
		state.Coordinate = next;
	}
	std::array<float, 2> EmoteScenePlayer::Coordinate() const {
		auto& state = m_Implementation->Require();
		state.RequireMutable();
		return state.Coordinate.Current();
	}
	void EmoteScenePlayer::SetScale(float scale, float duration, float easing) {
		auto& state = m_Implementation->Require();
		state.RequireMutable();
		auto next = state.GlobalScale;
		next.Set(scale, duration, easing);
		state.Variables->MarkPropertiesDirty();
		state.GlobalScale = next;
	}
	float EmoteScenePlayer::Scale() const {
		auto& state = m_Implementation->Require();
		state.RequireMutable();
		return state.GlobalScale.Current();
	}
	void EmoteScenePlayer::SetRotation(float radians, float duration, float easing) {
		auto& state = m_Implementation->Require();
		state.RequireMutable();
		auto next = state.RotationControl;
		next.Set(radians, duration, easing);
		state.Variables->MarkPropertiesDirty();
		state.RotationControl = next;
	}
	float EmoteScenePlayer::Rotation() const {
		auto& state = m_Implementation->Require();
		state.RequireMutable();
		// Native50a590 multiplies by tau before dividing; AngleRadians uses a different operation order.
		const float numerator = state.Root.AngleDegrees() * 6.28318548f;
		return numerator / 360.0f;
	}
	void EmoteScenePlayer::SetMeshDivisionRatio(float ratio) {
		auto& state = m_Implementation->Require();
		state.RequireMutable();
		if (!std::isfinite(ratio) || ratio < 0) {
			throw std::runtime_error("Emote mesh division requires a finite nonnegative ratio");
		}

		// Native51f150 stores the policy without marking property/numeric geometry dirty.
		state.Commands.SetMeshDivisionRatio(ratio);
	}
	float EmoteScenePlayer::MeshDivisionRatio() const {
		auto& state = m_Implementation->Require();
		state.RequireMutable();
		return state.Commands.MeshDivisionRatio();
	}
	void EmoteScenePlayer::SetBustScale(float scale) {
		auto& state = m_Implementation->Require();
		state.RequireMutable();
		state.BustScale = scale;
	}
	float EmoteScenePlayer::BustScale() const {
		auto& state = m_Implementation->Require();
		state.RequireMutable();
		return state.BustScale;
	}
	void EmoteScenePlayer::SetHairScale(float scale) {
		auto& state = m_Implementation->Require();
		state.RequireMutable();
		state.HairScale = scale;
	}
	float EmoteScenePlayer::HairScale() const {
		auto& state = m_Implementation->Require();
		state.RequireMutable();
		return state.HairScale;
	}
	void EmoteScenePlayer::SetPartsScale(float scale) {
		auto& state = m_Implementation->Require();
		state.RequireMutable();
		state.PartsScale = scale;
	}
	float EmoteScenePlayer::PartsScale() const {
		auto& state = m_Implementation->Require();
		state.RequireMutable();
		return state.PartsScale;
	}
	void EmoteScenePlayer::StartWind(EmoteWindParameters parameters) {
		auto& state = m_Implementation->Require();
		state.RequireMutable();
		state.Wind->Start(parameters);
	}
	void EmoteScenePlayer::StopWind() {
		auto& state = m_Implementation->Require();
		state.RequireMutable();
		state.Wind->Stop();
	}
	EmoteWindSnapshot EmoteScenePlayer::WindState() const {
		auto& state = m_Implementation->Require();
		state.RequireMutable();
		return state.Wind->Snapshot();
	}
	void EmoteScenePlayer::SetColor(std::uint32_t color, float duration, float easing) {
		auto& state = m_Implementation->Require();
		state.RequireMutable();
		auto next = state.Color;
		next.Set(color, duration, easing);
		state.Variables->MarkPropertiesDirty();
		state.Color = next;
	}
	std::uint32_t EmoteScenePlayer::Color() const {
		auto& state = m_Implementation->Require();
		state.RequireMutable();
		return state.Root.ModulationColor();
	}
	void EmoteScenePlayer::Clear() {
		auto& state = m_Implementation->Require();
		state.RequireMutable();
		state.Operating = true;
		try {
			state.Physics->ClearPlain(*state.Plain);
			state.Operating = false;
		} catch (...) {
			state.Operating = false;
			state.Valid = false;
			throw;
		}
	}
	bool EmoteScenePlayer::Animating() const {
		auto& state = m_Implementation->Require();
		state.RequireMutable();
		// Native2c9520 excludes color, Wind and physical simulation. Extended rotation remains constructor-zero in the admitted setter domain.
		return state.Coordinate.Changing() || state.GlobalScale.Changing() || state.RotationControl.Changing() || state.Timelines->Animating();
	}

	void EmoteScenePlayer::AssignState(const EmoteScenePlayer& source) {
		auto& destination = m_Implementation->Require();
		auto& origin = source.m_Implementation->Require();
		destination.RequireMutable();
		origin.RequireMutable();
		if (&destination == &origin) {
			return;
		}
		destination.Variables->RequireCanAssignState(*origin.Variables);
		destination.Timelines->RequireCanAssignState(*origin.Timelines);
		destination.Physics->RequireCanAssignState(*origin.Physics);
		destination.Operating = true;
		origin.Operating = true;
		try {
			// Native reconfigures the destination wind; bands and the shared random stream are not cloned.
			destination.Wind->AssignState(*origin.Wind);
			destination.BustScale = origin.BustScale;
			destination.HairScale = origin.HairScale;
			destination.PartsScale = origin.PartsScale;
			destination.Coordinate.AssignState(origin.Coordinate);
			destination.Color = origin.Color;
			destination.GlobalScale = origin.GlobalScale;
			destination.RotationControl = origin.RotationControl;
			destination.Variables->AssignState(*origin.Variables);
			destination.Timelines->AssignState(*origin.Timelines);
			if (destination.Physics->Enabled()) {
				// Native2E1FD0 first performs the genuine containing CalcLayerFrameInfo(0), without a shared-clock step.
				destination.ProgressNative(0);
			}
			destination.Physics->AssignState(*origin.Physics, destination.Global);
			origin.Operating = false;
			destination.Operating = false;
		} catch (...) {
			origin.Operating = false;
			destination.Operating = false;
			destination.Valid = false;
			throw;
		}
	}
	float EmoteScenePlayer::ReadVariable(std::string_view name) const {
		auto& state = m_Implementation->Require();
		state.RequireMutable();
		return state.Variables->Read(name);
	}
	void EmoteScenePlayer::SetVariable(std::string_view name, float value, float duration, float easing) {
		auto& state = m_Implementation->Require();
		state.RequireMutable();
		state.Variables->Set(name, value, duration, easing);
	}
	void EmoteScenePlayer::SetBlinkEnabled(bool enabled) {
		auto& state = m_Implementation->Require();
		state.RequireMutable();
		state.Variables->SetBlinkEnabled(enabled);
	}
	void EmoteScenePlayer::PlayTimeline(std::string_view name, std::uint32_t flags) {
		auto& state = m_Implementation->Require();
		state.RequireMutable();
		state.Timelines->Play(name, flags);
	}
	void EmoteScenePlayer::StopTimeline(std::string_view name) {
		auto& state = m_Implementation->Require();
		state.RequireMutable();
		state.Timelines->Stop(name);
	}
	void EmoteScenePlayer::SetTimelineBlendRatio(std::string_view name, float ratio, float duration, float easing) {
		auto& state = m_Implementation->Require();
		state.RequireMutable();
		state.Timelines->SetBlendRatio(name, ratio, duration, easing);
	}
	float EmoteScenePlayer::TimelineBlendRatio(std::string_view name) const {
		auto& state = m_Implementation->Require();
		state.RequireMutable();
		return state.Timelines->BlendRatio(name);
	}
	void EmoteScenePlayer::FadeInTimeline(std::string_view name, float duration, float easing) {
		auto& state = m_Implementation->Require();
		state.RequireMutable();
		state.Timelines->FadeIn(name, duration, easing);
	}
	void EmoteScenePlayer::FadeOutTimeline(std::string_view name, float duration, float easing) {
		auto& state = m_Implementation->Require();
		state.RequireMutable();
		state.Timelines->FadeOut(name, duration, easing);
	}
	bool EmoteScenePlayer::TimelinePlaying(std::string_view name) const {
		auto& state = m_Implementation->Require();
		state.RequireMutable();
		return state.Timelines->IsPlaying(name);
	}
	const MotionPlayerCommands& EmoteScenePlayer::Commands() const {
		return m_Implementation->Require().Commands;
	}
	const MotionRootScene& EmoteScenePlayer::Root() const {
		return m_Implementation->Require().Root;
	}
	const MotionScenePlayer& EmoteScenePlayer::PlainPlayer() const {
		return *m_Implementation->Require().Plain;
	}
	const EmoteVariableControls& EmoteScenePlayer::Variables() const {
		return *m_Implementation->Require().Variables;
	}
	EmoteVelvetyOwnerCounts EmoteScenePlayer::PhysicalCounts() const {
		return m_Implementation->Require().Physics->Counts();
	}
	resource::MotionMeshMatrix EmoteScenePlayer::PhysicsToWorld() const {
		auto& state = m_Implementation->Require();
		state.RequireMutable();

		return state.Physics->PhysicsToWorld();
	}
	float EmoteScenePlayer::MetadataScale() const {
		return m_Implementation->Require().Scale;
	}
	bool EmoteScenePlayer::CanClose() const noexcept {
		return !m_Implementation->Owner || m_Implementation->Owner->CanClose();
	}
	bool EmoteScenePlayer::IsClosed() const noexcept {
		return !m_Implementation->Owner;
	}
	void EmoteScenePlayer::Close() {
		if (!CanClose()) {
			throw std::runtime_error("containing Emote retirement requires completed update and actual draw-input release");
		}
		m_Implementation->Owner.reset();
		m_Implementation->Borrow.Release();
	}
} // namespace sgre::scene
