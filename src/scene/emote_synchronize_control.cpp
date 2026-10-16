#include "sgre/scene/emote_synchronize_control.hpp"

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
	static const resource::PsbValue& SynchronizeField(const resource::PsbValue& object, std::string_view name) {
		if (const auto* value = object.Find(name)) {
			return *value;
		}
		throw std::runtime_error("Emote Synchronize metadata requires actual field " + std::string(name));
	}

	static float SynchronizeNumber(const resource::PsbValue& value) {
		float result = 0;
		if (const auto* integer = std::get_if<std::int64_t>(&value.Data)) {
			result = static_cast<float>(*integer);
		} else if (const auto* real = std::get_if<double>(&value.Data)) {
			result = static_cast<float>(*real);
		} else if (const auto* boolean = std::get_if<bool>(&value.Data)) {
			result = *boolean ? 1 : 0;
		} else {
			throw std::runtime_error("Emote Synchronize metadata requires actual numeric scalars");
		}
		if (!std::isfinite(result)) {
			throw std::runtime_error("Emote Synchronize scalar exceeds the finite native domain");
		}
		return result;
	}

	static std::int32_t SynchronizeInteger(const resource::PsbValue& value) {
		if (const auto* integer = std::get_if<std::int64_t>(&value.Data)) {
			return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(*integer));
		}
		if (const auto* boolean = std::get_if<bool>(&value.Data)) {
			return *boolean ? 1 : 0;
		}
		if (const auto* real = std::get_if<double>(&value.Data); real != nullptr && std::isfinite(*real) && *real >= -2147483648.0 && *real < 2147483648.0) {
			return static_cast<std::int32_t>(*real);
		}
		throw std::runtime_error("Emote Synchronize integer exceeds native finite conversion");
	}

	static bool SynchronizeFlag(const resource::PsbValue& object, std::string_view name, bool fallback) {
		const auto* value = object.Find(name);
		if (value == nullptr) {
			return fallback;
		}
		if (const auto* real = std::get_if<double>(&value->Data)) {
			if (!std::isfinite(*real)) {
				throw std::runtime_error("Emote Synchronize flag requires actual finite metadata");
			}
			return *real != 0;
		}
		return static_cast<std::uint8_t>(SynchronizeInteger(*value)) != 0;
	}

	static std::string SynchronizeName(const resource::PsbValue& object, std::string_view name, bool required, std::size_t& budget) {
		const auto* value = object.Find(name);
		if (value == nullptr && !required) {
			return {};
		}
		const auto* text = value == nullptr ? nullptr : std::get_if<std::string>(&value->Data);
		if (text == nullptr) {
			throw std::runtime_error("Emote Synchronize requires actual UID strings");
		}
		const auto actual = std::string_view(*text).substr(0, text->find('\0'));
		if (actual.size() > kMaximumMotionCommandNameBytes || actual.size() >= 8 * 1024 * 1024 - budget) {
			throw std::runtime_error("Emote Synchronize UID storage exceeds its bounded domain");
		}
		budget += actual.size() + 1;
		return std::string(actual);
	}

	std::vector<EmoteSynchronizeDefinition> ReadEmoteSynchronizeDefinitions(const resource::PsbDocument& document) {
		const auto& metadata = SynchronizeField(document.Root, "metadata");
		if (metadata.Find("velvetyCommonParams") == nullptr) {
			return {};
		}
		const auto* controls = std::get_if<resource::PsbArray>(&SynchronizeField(metadata, "velvetySynchronizeControl").Data);
		if (controls == nullptr || controls->size() > 4096) {
			throw std::runtime_error("Emote Synchronize requires a bounded actual control array");
		}
		std::vector<EmoteSynchronizeDefinition> result;
		result.reserve(controls->size());
		std::size_t budget = 0;
		for (const auto& control : *controls) {
			EmoteSynchronizeDefinition definition;
			definition.Uid = SynchronizeName(control, "uid", false, budget);
			definition.SourceUid = SynchronizeName(control, "sourceUid", true, budget);
			definition.PixelsPerMeter = SynchronizeNumber(SynchronizeField(control, "ppm"));
			definition.Width = SynchronizeNumber(SynchronizeField(control, "sizeH"));
			definition.Height = SynchronizeNumber(SynchronizeField(control, "sizeV"));
			definition.OriginX = SynchronizeNumber(SynchronizeField(control, "originX"));
			definition.OriginY = SynchronizeNumber(SynchronizeField(control, "originY"));
			definition.Weight = SynchronizeNumber(SynchronizeField(control, "weight"));
			definition.HorizontalWeight = SynchronizeNumber(SynchronizeField(control, "weightHorizontal"));
			definition.VerticalWeight = SynchronizeNumber(SynchronizeField(control, "weightVertical"));
			definition.HorizontalFlip = SynchronizeFlag(control, "flipHorizontal", false);
			definition.VerticalFlip = SynchronizeFlag(control, "flipVertical", false);
			const auto inherit = SynchronizeFlag(control, "inheritShape", true);
			definition.CorrectFix = !inherit && SynchronizeFlag(control, "correctFix", false);
			definition.CorrectScale = !inherit && SynchronizeFlag(control, "correctScale", false);
			definition.ReferenceParentMeshPosture = SynchronizeFlag(control, "refParentMeshPosture", false);
			if (const auto* prediction = control.Find("coordPred")) {
				const auto number = SynchronizeInteger(*prediction);
				if (number < 0 || number >= 4) {
					throw std::runtime_error("Emote Synchronize prediction requires recovered degree0..3");
				}
				definition.PredictionDegree = static_cast<std::uint32_t>(number);
			}
			const auto* fixes = std::get_if<resource::PsbArray>(&SynchronizeField(control, "fixes").Data);
			if (fixes == nullptr || fixes->size() > 3) {
				throw std::runtime_error("Emote Synchronize supports the actual zero/one/two/three-point fix storage");
			}
			for (const auto& fix : *fixes) {
				definition.Fixes.push_back({SynchronizeNumber(SynchronizeField(fix, "x")), SynchronizeNumber(SynchronizeField(fix, "y"))});
			}
			result.push_back(std::move(definition));
		}
		return result;
	}

	static resource::MotionPoint SynchronizePoint(const resource::MotionPoint& point, const resource::MotionMeshMatrix& matrix) {
		return {(point.Y * matrix.XY + point.X * matrix.XX) + matrix.TranslationX, (point.Y * matrix.YY + point.X * matrix.YX) + matrix.TranslationY};
	}

	static void SynchronizeTransform(resource::MotionBezierLattice& lattice, const resource::MotionMeshMatrix& matrix) {
		for (auto& point : lattice.Controls) {
			point = SynchronizePoint(point, matrix);
		}
	}

	static resource::MotionMeshMatrix SynchronizeWorld(const EmoteSynchronizeDefinition& definition, const EmotePhysicalPosture& posture, float origin_x, float origin_y) {
		const auto x = -origin_x * definition.Width;
		const auto y = -origin_y * definition.Height;
		resource::MotionMeshMatrix result;
		result.XX = definition.Width * posture.Cosine;
		result.XY = definition.Height * -posture.Sine;
		result.YX = definition.Width * posture.Sine;
		result.YY = definition.Height * posture.Cosine;
		result.TranslationX = (x * posture.Cosine + y * -posture.Sine) + posture.Coordinate[0];
		result.TranslationY = (y * posture.Cosine + x * posture.Sine) + posture.Coordinate[1];
		return result;
	}

	static resource::MotionMeshMatrix SynchronizeInverse(const resource::MotionMeshMatrix& matrix) {
		const auto determinant = matrix.YY * matrix.XX - matrix.YX * matrix.XY;
		if (!std::isfinite(determinant) || determinant == 0) {
			throw std::runtime_error("Emote Synchronize requires the actual finite invertible target posture");
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

	static resource::MotionMeshMatrix SynchronizeProduct(const resource::MotionMeshMatrix& first, const resource::MotionMeshMatrix& second) {
		resource::MotionMeshMatrix result;
		result.XX = first.XY * second.YX + first.XX * second.XX;
		result.XY = first.XY * second.YY + first.XX * second.XY;
		result.YX = first.YY * second.YX + first.YX * second.XX;
		result.YY = first.YY * second.YY + first.YX * second.XY;
		result.TranslationX = (second.TranslationY * first.XY + second.TranslationX * first.XX) + first.TranslationX;
		result.TranslationY = (second.TranslationY * first.YY + second.TranslationX * first.YX) + first.TranslationY;
		return result;
	}

	struct SynchronizeMeshState {
		explicit SynchronizeMeshState(const EmoteSynchronizeDefinition& definition)
		    : OriginX(definition.OriginX), OriginY(definition.OriginY), Physical{definition.OriginX - 0.5f, definition.OriginY - 0.5f} {
			Output.Controls.reserve(256);
			if (definition.Fixes.size() > 1) {
				MultipleFixes.emplace(definition.Fixes, definition.Width, definition.Height);
			}
		}
		resource::MotionBezierLattice Output;
		std::optional<EmoteMeshFixAlignment> MultipleFixes;
		float OriginX;
		float OriginY;
		resource::MotionPoint Physical;
	};

	static void PrepareSynchronizeMesh(SynchronizeMeshState& instance, const EmoteSynchronizeDefinition& definition, const resource::MotionBezierLattice& source, const EmoteVelvetyPosture& pose, std::span<const resource::MotionPoint> targets) {
		ValidateMotionBezierLattice(source);
		const auto width = static_cast<std::size_t>(source.EncodedDegree & 15) + 1;
		const auto height = static_cast<std::size_t>(source.EncodedDegree >> 4) + 1;
		const auto horizontal = definition.Weight * definition.HorizontalWeight;
		const auto vertical = definition.Weight * definition.VerticalWeight;
		if ((horizontal != 1 || vertical != 1) && (width == 1 || height == 1)) {
			throw std::runtime_error("Emote Synchronize weighted zero-axis identity operator remains unrecovered");
		}
		auto& output = instance.Output;
		output.EncodedDegree = source.EncodedDegree;
		output.Controls.assign(source.Controls.begin(), source.Controls.end());
		if (definition.HorizontalFlip) {
			for (std::size_t y = 0; y != height; ++y) {
				for (std::size_t x = 0; x != width / 2; ++x) {
					std::swap(output.Controls[y * width + x], output.Controls[y * width + width - x - 1]);
				}
			}
			for (auto& point : output.Controls) {
				point.X = 1.0f - point.X;
			}
		}
		if (definition.VerticalFlip) {
			for (std::size_t y = 0; y != height / 2; ++y) {
				for (std::size_t x = 0; x != width; ++x) {
					std::swap(output.Controls[y * width + x], output.Controls[(height - y - 1) * width + x]);
				}
			}
			for (auto& point : output.Controls) {
				point.Y = 1.0f - point.Y;
			}
		}
		if (horizontal != 1 || vertical != 1) {
			for (std::size_t y = 0; y != height; ++y) {
				for (std::size_t x = 0; x != width; ++x) {
					auto& point = output.Controls[y * width + x];
					const auto u = static_cast<float>(x) / static_cast<float>(width - 1);
					const auto v = static_cast<float>(y) / static_cast<float>(height - 1);
					point.X = (1.0f - horizontal) * u + horizontal * point.X;
					point.Y = (1.0f - vertical) * v + vertical * point.Y;
				}
			}
		}
		const auto world = SynchronizeWorld(definition, pose.Physical, instance.OriginX, instance.OriginY);
		const auto inverse = SynchronizeInverse(world);
		SynchronizeTransform(output, world);
		if (definition.CorrectScale) {
			for (auto& point : output.Controls) {
				point.X = (point.X - pose.Physical.Coordinate[0]) * pose.CorrectionScale + pose.Physical.Coordinate[0];
				point.Y = (point.Y - pose.Physical.Coordinate[1]) * pose.CorrectionScale + pose.Physical.Coordinate[1];
			}
		}
		if (instance.MultipleFixes) {
			instance.MultipleFixes->Normalize(output, pose.Physical, inverse);
		} else {
			auto normalization = inverse;
			if (!definition.Fixes.empty()) {
				const auto actual = EvaluateMotionBezierLattice(output, definition.Fixes.front());
				const auto x = pose.Physical.Coordinate[0] - actual.X;
				const auto y = pose.Physical.Coordinate[1] - actual.Y;
				normalization.TranslationX = (inverse.XX * x + inverse.XY * y) + inverse.TranslationX;
				normalization.TranslationY = (inverse.YY * y + inverse.YX * x) + inverse.TranslationY;
			}
			SynchronizeTransform(output, normalization);
		}
		if (definition.CorrectFix && !definition.Fixes.empty()) {
			auto corrected = pose.Physical;
			corrected.Rotation = pose.CorrectionAngle;
			corrected.Cosine = std::cos(corrected.Rotation);
			corrected.Sine = std::sin(corrected.Rotation);
			const auto corrected_world = SynchronizeWorld(definition, corrected, instance.OriginX, instance.OriginY);
			const auto corrected_inverse = SynchronizeInverse(corrected_world);
			if (instance.MultipleFixes) {
				instance.MultipleFixes->Correct(output, targets, corrected_inverse);
			} else {
				auto pivot = corrected_world;
				const auto fix = SynchronizePoint(definition.Fixes.front(), pivot);
				pivot.TranslationX = (pivot.TranslationX - fix.X) + targets.front().X;
				pivot.TranslationY = (pivot.TranslationY - fix.Y) + targets.front().Y;
				SynchronizeTransform(output, SynchronizeProduct(corrected_inverse, pivot));
			}
		}
		ValidateMotionBezierLattice(output);
	}

	class EmoteSynchronizeMeshOutput::Implementation {
	public:
		explicit Implementation(EmoteSynchronizeDefinition definition)
		    : Definition(std::move(definition)), State(Definition) {
			for (const auto value : {Definition.PixelsPerMeter, Definition.Width, Definition.Height, Definition.OriginX, Definition.OriginY, Definition.Weight, Definition.HorizontalWeight, Definition.VerticalWeight, Definition.Weight * Definition.HorizontalWeight, Definition.Weight * Definition.VerticalWeight}) {
				if (!std::isfinite(value)) {
					throw std::runtime_error("Emote Synchronize mesh requires finite actual definition values");
				}
			}
			if (!(Definition.PixelsPerMeter > 0) || !(Definition.Width > 0) || !(Definition.Height > 0) || Definition.Fixes.size() > 3) {
				throw std::runtime_error("Emote Synchronize mesh requires positive dimensions/ppm and at most three actual fixes");
			}
			for (const auto& fix : Definition.Fixes) {
				if (!std::isfinite(fix.X) || !std::isfinite(fix.Y)) {
					throw std::runtime_error("Emote Synchronize mesh requires finite actual fix coordinates");
				}
			}
		}
		void RequireReady() const {
			if (Thread != std::this_thread::get_id() || !Valid) {
				throw std::runtime_error("Emote Synchronize mesh requires its valid serialized actual owner");
			}
		}
		EmoteSynchronizeDefinition Definition;
		SynchronizeMeshState State;
		const std::thread::id Thread = std::this_thread::get_id();
		bool Valid = true;
	};

	EmoteSynchronizeMeshOutput::EmoteSynchronizeMeshOutput(EmoteSynchronizeDefinition definition)
	    : m_Implementation(std::make_unique<Implementation>(std::move(definition))) {
	}
	EmoteSynchronizeMeshOutput::~EmoteSynchronizeMeshOutput() noexcept = default;
	void EmoteSynchronizeMeshOutput::SetSourceOrigin(resource::MotionPoint actual_source_origin) {
		auto& owner = *m_Implementation;
		owner.RequireReady();
		if (!std::isfinite(actual_source_origin.X) || !std::isfinite(actual_source_origin.Y)) {
			throw std::runtime_error("Emote Synchronize requires its actual finite source origin");
		}
		if (owner.Definition.Fixes.empty()) {
			owner.State.Physical.X = owner.Definition.VerticalFlip ? -actual_source_origin.X : actual_source_origin.X;
			owner.State.Physical.Y = owner.Definition.HorizontalFlip ? -actual_source_origin.Y : actual_source_origin.Y;
			owner.State.OriginX = owner.State.Physical.X + 0.5f;
			owner.State.OriginY = owner.State.Physical.Y + 0.5f;
		}
	}
	resource::MotionPoint EmoteSynchronizeMeshOutput::PhysicalOrigin() const {
		m_Implementation->RequireReady();
		return m_Implementation->State.Physical;
	}
	resource::MotionPoint EmoteSynchronizeMeshOutput::PixelOrigin() const {
		const auto physical = PhysicalOrigin();
		const auto& definition = m_Implementation->Definition;
		const resource::MotionPoint result{(physical.X * definition.Width) * definition.PixelsPerMeter, (physical.Y * definition.Height) * definition.PixelsPerMeter};
		if (!std::isfinite(result.X) || !std::isfinite(result.Y)) {
			throw std::runtime_error("Emote Synchronize source-origin conversion exceeded finite pixel coordinates");
		}
		return result;
	}
	const resource::MotionBezierLattice& EmoteSynchronizeMeshOutput::Prepare(const resource::MotionBezierLattice& source, const EmoteVelvetyPosture& actual_posture, std::span<const resource::MotionPoint> world_fix_targets) {
		auto& owner = *m_Implementation;
		owner.RequireReady();
		for (const auto value : {actual_posture.Physical.Coordinate[0], actual_posture.Physical.Coordinate[1], actual_posture.Physical.Coordinate[2], actual_posture.Physical.Rotation, actual_posture.Physical.Cosine, actual_posture.Physical.Sine, actual_posture.CorrectionAngle, actual_posture.CorrectionScale}) {
			if (!std::isfinite(value)) {
				throw std::runtime_error("Emote Synchronize mesh requires the actual finite completed posture");
			}
		}
		if (!(actual_posture.CorrectionScale > 0) || (owner.Definition.CorrectFix && world_fix_targets.size() != owner.Definition.Fixes.size())) {
			throw std::runtime_error("Emote Synchronize mesh requires actual positive scale and every enabled world-fix target");
		}
		for (const auto& point : world_fix_targets) {
			if (!std::isfinite(point.X) || !std::isfinite(point.Y)) {
				throw std::runtime_error("Emote Synchronize mesh requires finite actual world-fix targets");
			}
		}
		try {
			PrepareSynchronizeMesh(owner.State, owner.Definition, source, actual_posture, world_fix_targets);
			return owner.State.Output;
		} catch (...) {
			owner.Valid = false;
			throw;
		}
	}
	void EmoteSynchronizeMeshOutput::Clear() {
		m_Implementation->RequireReady();
		m_Implementation->State.Output.EncodedDegree = 0;
		m_Implementation->State.Output.Controls.clear();
	}

	struct SynchronizeInstance {
		SynchronizeInstance(const MotionLayerUidReference& layer, const EmoteSynchronizeDefinition& definition)
		    : Layer(layer), Prediction(definition.PredictionDegree, definition.Fixes.size()), Mesh(definition) {
		}
		MotionLayerUidReference Layer;
		EmotePointPrediction Prediction;
		EmoteSynchronizeMeshOutput Mesh;
		bool Reset = true;
	};

	class EmoteSynchronizeControl::Implementation final : public MotionLayerUidObserver {
	public:
		class SourceObserver final : public MotionLayerUidObserver {
		public:
			explicit SourceObserver(Implementation& owner)
			    : Owner(owner) {
			}
			void LayerAdded(std::string_view uid, const MotionLayerUidMatch& match) noexcept override {
				try {
					Owner.RequireThread();
					if (uid != Owner.Definition.SourceUid || Owner.Operating || &match.Layer.Player().LayerUids().UltimatePlayer() != &Owner.Commands) {
						throw std::runtime_error("Emote Synchronize source Added requires the actual idle primary UID owner");
					}
					// Native2ee8a0 overwrites the retained source with each genuine Added occurrence.
					Owner.Source = match.Layer;
					Owner.SourcePresent = true;
				} catch (...) {
					Owner.RememberFailure();
				}
			}
			void LayerRemoved(std::string_view uid, const MotionLayerUidMatch&) noexcept override {
				if (uid != Owner.Definition.SourceUid || Owner.Operating) {
					std::terminate();
				}
				// Native2efac0 clears presence on removal; it does not select another surviving match.
				Owner.SourcePresent = false;
				Owner.Source.reset();
			}
			Implementation& Owner;
		};

		Implementation(const MotionPlayerCommands& primary, EmoteSynchronizeDefinition definition)
		    : Commands(primary), Uids(primary.LayerUids()), Definition(std::move(definition)), SourceListener(*this), Posture(Definition.Width, Definition.Height, Definition.OriginX, Definition.OriginY, Definition.PixelsPerMeter, Definition.Fixes) {
			if (&Uids.UltimatePlayer() != &primary || Definition.Uid.size() > kMaximumMotionCommandNameBytes || Definition.SourceUid.size() > kMaximumMotionCommandNameBytes || Definition.PredictionDegree > 3) {
				throw std::runtime_error("Emote Synchronize requires the actual primary UID registry and bounded definition");
			}
			for (const auto value : {Definition.Weight, Definition.HorizontalWeight, Definition.VerticalWeight, Definition.Weight * Definition.HorizontalWeight, Definition.Weight * Definition.VerticalWeight}) {
				if (!std::isfinite(value)) {
					throw std::runtime_error("Emote Synchronize requires finite actual combined weights");
				}
			}
			try {
				Uids.Subscribe(Definition.Uid, *this);
				TargetSubscribed = true;
				Uids.Subscribe(Definition.SourceUid, SourceListener);
				SourceSubscribed = true;
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

		void RememberFailure() noexcept {
			if (!Failure) {
				Failure = std::current_exception();
			}
		}
		void RequireThread() const {
			if (Thread != std::this_thread::get_id()) {
				throw std::runtime_error("Emote Synchronize requires its actual serialized owner thread");
			}
		}
		void RequireReady() const {
			RequireThread();
			if (Failure) {
				std::rethrow_exception(Failure);
			}
			if (!TargetSubscribed || !SourceSubscribed || Operating || Closing || !Valid) {
				throw std::runtime_error("Emote Synchronize requires its live idle actual source/target owner");
			}
		}
		void LayerAdded(std::string_view uid, const MotionLayerUidMatch& match) noexcept override {
			try {
				RequireThread();
				if (uid != Definition.Uid || Operating || Closing || &match.Layer.Player().LayerUids().UltimatePlayer() != &Commands || Instances.contains(match.Occurrence) || Instances.size() >= 65536) {
					throw std::runtime_error("Emote Synchronize target Added requires its exact actual primary occurrence");
				}
				Instances.emplace(match.Occurrence, std::make_unique<SynchronizeInstance>(match.Layer, Definition));
			} catch (...) {
				RememberFailure();
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
				throw std::runtime_error("Emote Synchronize cannot retire during actual update or nested retirement");
			}
			Closing = true;
			try {
				if (SourceSubscribed) {
					Uids.Unsubscribe(Definition.SourceUid, SourceListener);
					SourceSubscribed = false;
				}
				Source.reset();
				SourcePresent = false;
				if (TargetSubscribed) {
					Uids.Unsubscribe(Definition.Uid, *this);
					TargetSubscribed = false;
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
				throw std::runtime_error("Emote Synchronize requires the actual positive finite physical frame delta");
			}
			LastUpdated = 0;
			LastWrites = 0;
			if (!SourcePresent) {
				return;
			}
			if (!Source) {
				throw std::runtime_error("Emote Synchronize lost its present actual source identity");
			}
			Operating = true;
			try {
				const bool source_active = Source->Active();
				for (auto& [occurrence, instance] : Instances) {
					static_cast<void>(occurrence);
					if (!instance->Layer.Active()) {
						continue;
					}
					if (source_active) {
						instance->Mesh.SetSourceOrigin(Source->MeshPhysicalOrigin());
					}
					const auto physical_origin = instance->Mesh.PhysicalOrigin();
					const auto pixel_origin = instance->Mesh.PixelOrigin();
					Posture.SetLocalPixelOrigin(pixel_origin);
					const auto pose = Posture.Read(instance->Layer, frame, Definition.ReferenceParentMeshPosture ? nullptr : actual_parent);
					Cache = {pose.Physical.Rotation, pose.CorrectionScale};
					std::array<resource::MotionPoint, 3> fixes{};
					std::span<resource::MotionPoint> targets;
					if (Definition.CorrectFix && !Definition.Fixes.empty()) {
						targets = std::span(fixes).first(Definition.Fixes.size());
						const auto actual = Posture.WorldFixes();
						std::copy(actual.begin(), actual.end(), targets.begin());
						auto normalized = pose.Physical;
						normalized.Rotation = pose.CorrectionAngle;
						normalized.Cosine = std::cos(normalized.Rotation);
						normalized.Sine = std::sin(normalized.Rotation);
						resource::MotionMeshMatrix inverse;
						inverse.XX = normalized.Cosine;
						inverse.XY = normalized.Sine;
						inverse.YX = -normalized.Sine;
						inverse.YY = normalized.Cosine;
						inverse.TranslationX = -normalized.Coordinate[1] * normalized.Sine + -normalized.Coordinate[0] * normalized.Cosine;
						inverse.TranslationY = -normalized.Coordinate[0] * -normalized.Sine + -normalized.Coordinate[1] * normalized.Cosine;
						instance->Prediction.Advance(elapsed_ticks, instance->Reset, inverse, targets);
					}
					instance->Reset = false;
					const auto* source = source_active ? &Source->CurrentMesh() : nullptr;
					if (source == nullptr || source->EncodedDegree == 0) {
						if (instance->Layer.SetMesh(1, nullptr, true)) {
							++LastWrites;
						}
						instance->Mesh.Clear();
					} else {
						const auto& output = instance->Mesh.Prepare(*source, pose, targets);
						if (instance->Layer.SetMesh(1, &output, true)) {
							++LastWrites;
						}
						instance->Layer.InitializeMeshOrigin(pixel_origin, physical_origin);
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
		EmoteSynchronizeDefinition Definition;
		SourceObserver SourceListener;
		EmoteVelvetyPostureReader Posture;
		std::map<std::uint32_t, std::unique_ptr<SynchronizeInstance>> Instances;
		std::optional<MotionLayerUidReference> Source;
		std::exception_ptr Failure;
		const std::thread::id Thread = std::this_thread::get_id();
		EmoteVelvetyPartPosture Cache{0, 0};
		std::size_t LastUpdated = 0;
		std::size_t LastWrites = 0;
		bool SourcePresent = false;
		bool SourceSubscribed = false;
		bool TargetSubscribed = false;
		bool Operating = false;
		bool Closing = false;
		bool Valid = true;
	};

	EmoteSynchronizeControl::EmoteSynchronizeControl(const MotionPlayerCommands& actual_primary, EmoteSynchronizeDefinition definition)
	    : m_Implementation(std::make_unique<Implementation>(actual_primary, std::move(definition))) {
	}
	EmoteSynchronizeControl::~EmoteSynchronizeControl() noexcept = default;
	void EmoteSynchronizeControl::RequireCanSkip() const {
		m_Implementation->RequireReady();
	}
	void EmoteSynchronizeControl::Skip() {
		RequireCanSkip();
		for (auto& [occurrence, instance] : m_Implementation->Instances) {
			static_cast<void>(occurrence);
			instance->Reset = true;
		}
	}
	void EmoteSynchronizeControl::RequireReady() const {
		m_Implementation->RequireReady();
	}
	void EmoteSynchronizeControl::Update(float elapsed_ticks, const EmoteVelvetyInputFrame& actual_frame, const EmoteVelvetyPartPosture* actual_parent) {
		m_Implementation->Update(elapsed_ticks, actual_frame, actual_parent);
	}
	std::string_view EmoteSynchronizeControl::Uid() const {
		m_Implementation->RequireReady();
		return m_Implementation->Definition.Uid;
	}
	bool EmoteSynchronizeControl::UsesCommands(const MotionPlayerCommands& actual_commands) const noexcept {
		return &m_Implementation->Commands == &actual_commands;
	}
	std::size_t EmoteSynchronizeControl::InstanceCount() const {
		m_Implementation->RequireReady();
		return m_Implementation->Instances.size();
	}
	std::size_t EmoteSynchronizeControl::LastUpdatedCount() const {
		m_Implementation->RequireReady();
		return m_Implementation->LastUpdated;
	}
	std::size_t EmoteSynchronizeControl::LastMeshWriteCount() const {
		m_Implementation->RequireReady();
		return m_Implementation->LastWrites;
	}
	EmoteVelvetyPartPosture EmoteSynchronizeControl::PartPosture() const {
		m_Implementation->RequireReady();
		return m_Implementation->Cache;
	}
	void EmoteSynchronizeControl::Close() {
		m_Implementation->Close();
	}
	void EmoteSynchronizeControl::RequireCanAssignState(const EmoteSynchronizeControl& source) const {
		const auto& target = *m_Implementation;
		const auto& from = *source.m_Implementation;
		target.RequireReady();
		from.RequireReady();
		if (Uid() != source.Uid()) {
			throw std::runtime_error("Emote physical assignment requires an exact family UID match");
		}
		for (const auto& [occurrence, instance] : target.Instances) {
			const auto found = from.Instances.find(occurrence);
			if (found == from.Instances.end()) {
				continue;
			}
			instance->Layer.RequireCanAssignMeshFrom(found->second->Layer);
		}
	}

	void EmoteSynchronizeControl::AssignState(const EmoteSynchronizeControl& source) {
		RequireCanAssignState(source);
		if (this == &source) {
			return;
		}

		auto& target = *m_Implementation;
		const auto& from = *source.m_Implementation;
		target.Operating = true;
		try {
			target.Posture = from.Posture;
			target.Cache = from.Cache;
			// Native instance maps match occurrence keys; prediction history and destination leases stay local.
			for (auto& [occurrence, instance] : target.Instances) {
				const auto found = from.Instances.find(occurrence);
				if (found == from.Instances.end()) {
					continue;
				}
				instance->Layer.AssignMeshFrom(found->second->Layer);
			}
			target.Operating = false;
		} catch (...) {
			target.Operating = false;
			target.Valid = false;
			throw;
		}
	}

} // namespace sgre::scene
