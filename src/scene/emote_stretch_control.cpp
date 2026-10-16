#include "sgre/scene/emote_stretch_control.hpp"

#include "sgre/scene/emote_mesh_fix_alignment.hpp"
#include "sgre/scene/emote_point_prediction.hpp"
#include "sgre/scene/motion_bezier_lattice.hpp"
#include "sgre/scene/motion_player_commands.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <exception>
#include <map>
#include <optional>
#include <stdexcept>
#include <thread>
#include <utility>
#include <variant>

namespace sgre::scene {
	static const resource::PsbValue& StretchField(const resource::PsbValue& object, std::string_view name) {
		if (const auto* value = object.Find(name)) {
			return *value;
		}
		throw std::runtime_error("Emote Stretch metadata requires actual field " + std::string(name));
	}

	static float StretchNumber(const resource::PsbValue& value) {
		float result = 0;
		if (const auto* integer = std::get_if<std::int64_t>(&value.Data)) {
			result = static_cast<float>(*integer);
		} else if (const auto* real = std::get_if<double>(&value.Data)) {
			result = static_cast<float>(*real);
		} else if (const auto* boolean = std::get_if<bool>(&value.Data)) {
			result = *boolean ? 1 : 0;
		} else {
			throw std::runtime_error("Emote Stretch metadata requires actual numeric scalars");
		}
		if (!std::isfinite(result)) {
			throw std::runtime_error("Emote Stretch scalar exceeds the finite native domain");
		}
		return result;
	}

	static std::int32_t StretchInteger(const resource::PsbValue& value) {
		if (const auto* integer = std::get_if<std::int64_t>(&value.Data)) {
			return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(*integer));
		}
		if (const auto* boolean = std::get_if<bool>(&value.Data)) {
			return *boolean ? 1 : 0;
		}
		if (const auto* real = std::get_if<double>(&value.Data); real != nullptr && std::isfinite(*real) && *real >= -2147483648.0 && *real < 2147483648.0) {
			return static_cast<std::int32_t>(*real);
		}
		throw std::runtime_error("Emote Stretch integer exceeds native finite conversion");
	}

	static bool StretchFlag(const resource::PsbValue& object, std::string_view name, bool fallback) {
		const auto* value = object.Find(name);
		if (value == nullptr) {
			return fallback;
		}
		if (const auto* real = std::get_if<double>(&value->Data)) {
			if (!std::isfinite(*real)) {
				throw std::runtime_error("Emote Stretch flag requires actual finite metadata");
			}
			return *real != 0;
		}
		return static_cast<std::uint8_t>(StretchInteger(*value)) != 0;
	}

	static std::string StretchName(const resource::PsbValue& object, std::string_view name, bool required, std::size_t& budget) {
		const auto* value = object.Find(name);
		if (value == nullptr && !required) {
			return {};
		}
		const auto* text = value == nullptr ? nullptr : std::get_if<std::string>(&value->Data);
		if (text == nullptr) {
			throw std::runtime_error("Emote Stretch requires actual UID strings");
		}
		const auto actual = std::string_view(*text).substr(0, text->find('\0'));
		if (actual.size() > kMaximumMotionCommandNameBytes || actual.size() >= 8 * 1024 * 1024 - budget) {
			throw std::runtime_error("Emote Stretch UID storage exceeds its bounded domain");
		}
		budget += actual.size() + 1;
		return std::string(actual);
	}

	std::vector<EmoteStretchDefinition> ReadEmoteStretchDefinitions(const resource::PsbDocument& document) {
		const auto& metadata = StretchField(document.Root, "metadata");
		if (metadata.Find("velvetyCommonParams") == nullptr) {
			return {};
		}
		const auto* controls = std::get_if<resource::PsbArray>(&StretchField(metadata, "velvetyStretchControl").Data);
		if (controls == nullptr || controls->size() > 4096) {
			throw std::runtime_error("Emote Stretch requires a bounded actual control array");
		}
		std::vector<EmoteStretchDefinition> result;
		result.reserve(controls->size());
		std::size_t budget = 0;
		for (const auto& control : *controls) {
			EmoteStretchDefinition definition;
			definition.Uid = StretchName(control, "uid", false, budget);
			definition.PixelsPerMeter = StretchNumber(StretchField(control, "ppm"));
			definition.Width = StretchNumber(StretchField(control, "sizeH"));
			definition.Height = StretchNumber(StretchField(control, "sizeV"));
			definition.OriginX = StretchNumber(StretchField(control, "originX"));
			definition.OriginY = StretchNumber(StretchField(control, "originY"));
			definition.CorrectScale = !StretchFlag(control, "inheritShape", true) && StretchFlag(control, "correctScale", false);
			definition.ReferenceParentMeshPosture = StretchFlag(control, "refParentMeshPosture", false);
			if (const auto* prediction = control.Find("coordPred")) {
				const auto number = StretchInteger(*prediction);
				if (number < 0 || number >= 4) {
					throw std::runtime_error("Emote Stretch prediction requires recovered degree0..3");
				}
				definition.PredictionDegree = static_cast<std::uint32_t>(number);
			}
			const auto* fixes = std::get_if<resource::PsbArray>(&StretchField(control, "fixes").Data);
			if (fixes == nullptr || fixes->size() > 3) {
				throw std::runtime_error("Emote Stretch supports the actual zero/one/two/three-point fix storage");
			}
			for (const auto& fix : *fixes) {
				definition.Fixes.push_back({StretchNumber(StretchField(fix, "x")), StretchNumber(StretchField(fix, "y"))});
			}
			result.push_back(std::move(definition));
		}
		return result;
	}
	static resource::MotionPoint StretchPoint(const resource::MotionPoint& point, const resource::MotionMeshMatrix& matrix) {
		return {(point.Y * matrix.XY + point.X * matrix.XX) + matrix.TranslationX, (point.Y * matrix.YY + point.X * matrix.YX) + matrix.TranslationY};
	}

	static void StretchTransform(resource::MotionBezierLattice& lattice, const resource::MotionMeshMatrix& matrix) {
		for (auto& point : lattice.Controls) {
			point = StretchPoint(point, matrix);
		}
	}

	static resource::MotionMeshMatrix StretchWorld(const EmoteStretchDefinition& definition, const EmotePhysicalPosture& posture) {
		const auto x = -definition.OriginX * definition.Width;
		const auto y = -definition.OriginY * definition.Height;
		resource::MotionMeshMatrix result;
		result.XX = definition.Width * posture.Cosine;
		result.XY = definition.Height * -posture.Sine;
		result.YX = definition.Width * posture.Sine;
		result.YY = definition.Height * posture.Cosine;
		result.TranslationX = (x * posture.Cosine + y * -posture.Sine) + posture.Coordinate[0];
		result.TranslationY = (y * posture.Cosine + x * posture.Sine) + posture.Coordinate[1];
		return result;
	}

	static resource::MotionMeshMatrix StretchInverse(const resource::MotionMeshMatrix& matrix) {
		const auto determinant = matrix.YY * matrix.XX - matrix.YX * matrix.XY;
		if (!std::isfinite(determinant) || determinant == 0) {
			throw std::runtime_error("Emote Stretch requires the actual finite invertible target posture");
		}
		const auto reciprocal = 1.0f / determinant;
		resource::MotionMeshMatrix result;
		result.XX = reciprocal * matrix.YY;
		result.XY = -matrix.XY * reciprocal;
		result.YX = -matrix.YX * reciprocal;
		result.YY = reciprocal * matrix.XX;
		result.TranslationX = (matrix.TranslationY * matrix.XY - matrix.TranslationX * matrix.YY) * reciprocal;
		result.TranslationY = (matrix.TranslationX * matrix.YX - matrix.TranslationY * matrix.XX) * reciprocal;
		return result;
	}

	static resource::MotionMeshMatrix StretchProduct(const resource::MotionMeshMatrix& first, const resource::MotionMeshMatrix& second) {
		resource::MotionMeshMatrix result;
		result.XX = first.XY * second.YX + first.XX * second.XX;
		result.XY = first.XY * second.YY + first.XX * second.XY;
		result.YX = first.YY * second.YX + first.YX * second.XX;
		result.YY = first.YY * second.YY + first.YX * second.XY;
		result.TranslationX = (second.TranslationY * first.XY + second.TranslationX * first.XX) + first.TranslationX;
		result.TranslationY = (second.TranslationY * first.YY + second.TranslationX * first.YX) + first.TranslationY;
		return result;
	}

	class EmoteStretchMeshOutput::Implementation {
	public:
		explicit Implementation(EmoteStretchDefinition definition)
		    : Definition(std::move(definition)) {
			for (const auto value : {Definition.Width, Definition.Height, Definition.OriginX, Definition.OriginY}) {
				if (!std::isfinite(value)) {
					throw std::runtime_error("Emote Stretch mesh requires finite actual definition values");
				}
			}
			if (!(Definition.Width > 0) || !(Definition.Height > 0) || Definition.Fixes.size() > 3) {
				throw std::runtime_error("Emote Stretch mesh requires positive dimensions and at most three actual fixes");
			}
			for (const auto& fix : Definition.Fixes) {
				if (!std::isfinite(fix.X) || !std::isfinite(fix.Y)) {
					throw std::runtime_error("Emote Stretch mesh requires finite actual fix coordinates");
				}
			}
			Output = IdentityMotionBezierLattice(0x11);
			if (Definition.Fixes.size() > 1) {
				MultipleFixes.emplace(Definition.Fixes, Definition.Width, Definition.Height);
			}
		}
		void RequireReady() const {
			if (Thread != std::this_thread::get_id() || !Valid) {
				throw std::runtime_error("Emote Stretch mesh requires its valid serialized actual owner");
			}
		}
		EmoteStretchDefinition Definition;
		resource::MotionBezierLattice Output;
		std::optional<EmoteMeshFixAlignment> MultipleFixes;
		const std::thread::id Thread = std::this_thread::get_id();
		bool Valid = true;
	};

	EmoteStretchMeshOutput::EmoteStretchMeshOutput(EmoteStretchDefinition definition)
	    : m_Implementation(std::make_unique<Implementation>(std::move(definition))) {
	}
	EmoteStretchMeshOutput::~EmoteStretchMeshOutput() noexcept = default;
	const resource::MotionBezierLattice& EmoteStretchMeshOutput::Prepare(const EmoteVelvetyPosture& actual_posture, std::span<const resource::MotionPoint> actual_world_targets) {
		auto& owner = *m_Implementation;
		owner.RequireReady();
		for (const auto value : {actual_posture.Physical.Coordinate[0], actual_posture.Physical.Coordinate[1], actual_posture.Physical.Coordinate[2], actual_posture.Physical.Rotation, actual_posture.Physical.Cosine, actual_posture.Physical.Sine, actual_posture.CorrectionAngle, actual_posture.CorrectionScale}) {
			if (!std::isfinite(value)) {
				throw std::runtime_error("Emote Stretch mesh requires the actual finite completed posture");
			}
		}
		if (!(actual_posture.CorrectionScale > 0) || actual_world_targets.size() != owner.Definition.Fixes.size()) {
			throw std::runtime_error("Emote Stretch mesh requires actual positive scale and every world-fix target");
		}
		for (const auto& target : actual_world_targets) {
			if (!std::isfinite(target.X) || !std::isfinite(target.Y)) {
				throw std::runtime_error("Emote Stretch mesh requires finite actual world-fix targets");
			}
		}
		try {
			auto& output = owner.Output;
			output.Controls[0] = {0, 0};
			output.Controls[1] = {1, 0};
			output.Controls[2] = {0, 1};
			output.Controls[3] = {1, 1};
			const auto world = StretchWorld(owner.Definition, actual_posture.Physical);
			const auto inverse = StretchInverse(world);
			if (owner.Definition.CorrectScale) {
				auto scaled = world;
				scaled.XX = world.XX * actual_posture.CorrectionScale;
				scaled.XY = world.XY * actual_posture.CorrectionScale;
				scaled.YX = world.YX * actual_posture.CorrectionScale;
				scaled.YY = world.YY * actual_posture.CorrectionScale;
				scaled.TranslationX = (world.TranslationX - actual_posture.Physical.Coordinate[0]) * actual_posture.CorrectionScale + actual_posture.Physical.Coordinate[0];
				scaled.TranslationY = (world.TranslationY - actual_posture.Physical.Coordinate[1]) * actual_posture.CorrectionScale + actual_posture.Physical.Coordinate[1];
				StretchTransform(output, scaled);
				if (owner.MultipleFixes) {
					owner.MultipleFixes->Normalize(output, actual_posture.Physical, inverse);
				} else {
					auto normalization = inverse;
					if (!owner.Definition.Fixes.empty()) {
						const auto actual = EvaluateMotionBezierLattice(output, owner.Definition.Fixes.front());
						const auto x = actual_posture.Physical.Coordinate[0] - actual.X;
						const auto y = actual_posture.Physical.Coordinate[1] - actual.Y;
						normalization.TranslationX = (inverse.XX * x + inverse.XY * y) + inverse.TranslationX;
						normalization.TranslationY = (inverse.YY * y + inverse.YX * x) + inverse.TranslationY;
					}
					StretchTransform(output, normalization);
				}
			}
			if (owner.MultipleFixes) {
				owner.MultipleFixes->Correct(output, actual_world_targets, inverse);
			} else if (!owner.Definition.Fixes.empty()) {
				auto pivot = world;
				const auto fix = StretchPoint(owner.Definition.Fixes.front(), pivot);
				pivot.TranslationX = (pivot.TranslationX - fix.X) + actual_world_targets.front().X;
				pivot.TranslationY = (pivot.TranslationY - fix.Y) + actual_world_targets.front().Y;
				StretchTransform(output, StretchProduct(inverse, pivot));
			}
			ValidateMotionBezierLattice(output);
			return output;
		} catch (...) {
			owner.Valid = false;
			throw;
		}
	}

	struct StretchInstance {
		StretchInstance(const MotionLayerUidReference& layer, const EmoteStretchDefinition& definition)
		    : Layer(layer), Prediction(definition.PredictionDegree, definition.Fixes.size()), Mesh(definition) {
		}
		MotionLayerUidReference Layer;
		EmotePointPrediction Prediction;
		EmoteStretchMeshOutput Mesh;
		bool Reset = true;
	};

	class EmoteStretchControl::Implementation final : public MotionLayerUidObserver {
	public:
		Implementation(const MotionPlayerCommands& primary, EmoteStretchDefinition definition)
		    : Commands(primary), Uids(primary.LayerUids()), Definition(std::move(definition)), Posture(Definition.Width, Definition.Height, Definition.OriginX, Definition.OriginY, Definition.PixelsPerMeter, Definition.Fixes) {
			if (&Uids.UltimatePlayer() != &primary || Definition.Uid.size() > kMaximumMotionCommandNameBytes || Definition.PredictionDegree > 3) {
				throw std::runtime_error("Emote Stretch requires the actual primary UID registry and bounded definition");
			}
			try {
				Uids.Subscribe(Definition.Uid, *this);
				Subscribed = true;
				RequireReady();
			} catch (...) {
				Close();
				throw;
			}
		}
		~Implementation() noexcept override {
			try {
				Close();
			} catch (...) {
				std::terminate();
			}
		}
		void RequireThread() const {
			if (Thread != std::this_thread::get_id()) {
				throw std::runtime_error("Emote Stretch requires its actual serialized owner thread");
			}
		}
		void RequireReady() const {
			RequireThread();
			if (Failure) {
				std::rethrow_exception(Failure);
			}
			if (!Subscribed || Operating || Closing || !Valid) {
				throw std::runtime_error("Emote Stretch requires its live idle actual occurrence owner");
			}
		}
		void LayerAdded(std::string_view uid, const MotionLayerUidMatch& match) noexcept override {
			try {
				RequireThread();
				if (uid != Definition.Uid || Operating || Closing || &match.Layer.Player().LayerUids().UltimatePlayer() != &Commands || Instances.contains(match.Occurrence) || Instances.size() >= 65536) {
					throw std::runtime_error("Emote Stretch Added requires its exact actual primary occurrence");
				}
				// Native2ef0d0 initializes real UID origin before allocating the entity.
				const resource::MotionPoint physical{(Definition.OriginX - 0.5f) * Definition.Width, (Definition.OriginY - 0.5f) * Definition.Height};
				const resource::MotionPoint pixel{physical.X * Definition.PixelsPerMeter, physical.Y * Definition.PixelsPerMeter};
				match.Layer.InitializeMeshOrigin(pixel, physical);
				Instances.emplace(match.Occurrence, std::make_unique<StretchInstance>(match.Layer, Definition));
			} catch (...) {
				if (!Failure) {
					Failure = std::current_exception();
				}
			}
		}
		void LayerRemoved(std::string_view, const MotionLayerUidMatch& match) noexcept override {
			const auto found = Instances.find(match.Occurrence);
			if (found != Instances.end()) {
				if (Operating || found->second->Layer != match.Layer) {
					std::terminate();
				}
				Instances.erase(found);
			}
		}
		void Close() {
			RequireThread();
			if (Operating || Closing) {
				throw std::runtime_error("Emote Stretch cannot retire during actual update or nested retirement");
			}
			Closing = true;
			try {
				if (Subscribed) {
					Uids.Unsubscribe(Definition.Uid, *this);
					Subscribed = false;
				}
				Instances.clear();
				Closing = false;
			} catch (...) {
				Closing = false;
				throw;
			}
		}
		void Update(float elapsed_ticks, const EmoteVelvetyInputFrame& frame, const EmoteVelvetyPartPosture* actual_parent) {
			RequireReady();
			if (!std::isfinite(elapsed_ticks) || !(elapsed_ticks > 0)) {
				throw std::runtime_error("Emote Stretch requires the actual positive finite physical frame delta");
			}
			LastUpdated = 0;
			LastWrites = 0;
			Operating = true;
			try {
				for (auto& [occurrence, instance] : Instances) {
					static_cast<void>(occurrence);
					if (!instance->Layer.Active()) {
						instance->Reset = true;
						continue;
					}
					auto pose = Posture.Read(instance->Layer, frame, Definition.ReferenceParentMeshPosture ? nullptr : actual_parent);
					Cache = {pose.Physical.Rotation, pose.CorrectionScale};
					// Stretch2f2b00 uses the raw selected angle, unlike the fitted PartsTree cache.
					pose.Physical.Coordinate[2] = 0;
					pose.Physical.Rotation = pose.CorrectionAngle;
					pose.Physical.Cosine = std::cos(pose.CorrectionAngle);
					pose.Physical.Sine = std::sin(pose.CorrectionAngle);
					std::array<resource::MotionPoint, 3> fixes{};
					const auto actual = Posture.WorldFixes();
					std::copy(actual.begin(), actual.end(), fixes.begin());
					auto targets = std::span(fixes).first(actual.size());
					resource::MotionMeshMatrix inverse;
					inverse.XX = pose.Physical.Cosine;
					inverse.XY = pose.Physical.Sine;
					inverse.YX = -pose.Physical.Sine;
					inverse.YY = pose.Physical.Cosine;
					inverse.TranslationX = -pose.Physical.Coordinate[1] * pose.Physical.Sine + -pose.Physical.Coordinate[0] * pose.Physical.Cosine;
					inverse.TranslationY = -pose.Physical.Coordinate[0] * -pose.Physical.Sine + -pose.Physical.Coordinate[1] * pose.Physical.Cosine;
					instance->Prediction.Advance(elapsed_ticks, instance->Reset, inverse, targets);
					instance->Reset = false;
					const auto& output = instance->Mesh.Prepare(pose, targets);
					if (instance->Layer.SetMesh(1, &output, true)) {
						++LastWrites;
					}
					++LastUpdated;
				}
				Operating = false;
			} catch (...) {
				Operating = false;
				Valid = false;
				throw;
			}
		}
		const MotionPlayerCommands& Commands;
		MotionPlayerLayerUids& Uids;
		EmoteStretchDefinition Definition;
		EmoteVelvetyPostureReader Posture;
		std::map<std::uint32_t, std::unique_ptr<StretchInstance>> Instances;
		std::exception_ptr Failure;
		const std::thread::id Thread = std::this_thread::get_id();
		EmoteVelvetyPartPosture Cache{0, 0};
		std::size_t LastUpdated = 0;
		std::size_t LastWrites = 0;
		bool Subscribed = false;
		bool Operating = false;
		bool Closing = false;
		bool Valid = true;
	};

	EmoteStretchControl::EmoteStretchControl(const MotionPlayerCommands& actual_primary, EmoteStretchDefinition definition)
	    : m_Implementation(std::make_unique<Implementation>(actual_primary, std::move(definition))) {
	}
	EmoteStretchControl::~EmoteStretchControl() noexcept = default;
	void EmoteStretchControl::RequireReady() const {
		m_Implementation->RequireReady();
	}
	void EmoteStretchControl::Update(float elapsed_ticks, const EmoteVelvetyInputFrame& actual_frame, const EmoteVelvetyPartPosture* actual_parent) {
		m_Implementation->Update(elapsed_ticks, actual_frame, actual_parent);
	}
	std::string_view EmoteStretchControl::Uid() const {
		m_Implementation->RequireReady();
		return m_Implementation->Definition.Uid;
	}
	bool EmoteStretchControl::UsesCommands(const MotionPlayerCommands& actual_commands) const noexcept {
		return &m_Implementation->Commands == &actual_commands;
	}
	std::size_t EmoteStretchControl::InstanceCount() const {
		m_Implementation->RequireReady();
		return m_Implementation->Instances.size();
	}
	std::size_t EmoteStretchControl::LastUpdatedCount() const {
		m_Implementation->RequireReady();
		return m_Implementation->LastUpdated;
	}
	std::size_t EmoteStretchControl::LastMeshWriteCount() const {
		m_Implementation->RequireReady();
		return m_Implementation->LastWrites;
	}
	EmoteVelvetyPartPosture EmoteStretchControl::PartPosture() const {
		m_Implementation->RequireReady();
		return m_Implementation->Cache;
	}
	void EmoteStretchControl::Close() {
		m_Implementation->Close();
	}
} // namespace sgre::scene
