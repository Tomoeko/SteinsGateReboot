#include "sgre/scene/emote_velvety_frame.hpp"

#include "sgre/scene/motion_player_commands.hpp"
#include "sgre/scene/motion_root_scene.hpp"
#include "sgre/scene/motion_scene_player.hpp"

#include <bit>
#include <cmath>
#include <stdexcept>
#include <utility>
#include <variant>

namespace sgre::scene {
	static float FrameNumber(const resource::PsbValue& value) {
		float number = 0;
		if (const auto* real = std::get_if<double>(&value.Data)) {
			number = static_cast<float>(*real);
		} else if (const auto* integer = std::get_if<std::int64_t>(&value.Data)) {
			number = static_cast<float>(*integer);
		} else if (const auto* boolean = std::get_if<bool>(&value.Data)) {
			number = *boolean ? 1.0f : 0.0f;
		} else {
			throw std::runtime_error("Emote Velvety global frame requires native numeric metadata");
		}
		if (!std::isfinite(number)) {
			throw std::runtime_error("Emote Velvety global frame requires finite metadata");
		}
		return number;
	}

	std::optional<EmoteVelvetyFrameDefinition> ReadEmoteVelvetyFrameDefinition(const resource::PsbDocument& document) {
		const auto* metadata = document.Root.Find("metadata");
		const auto* common = metadata == nullptr ? nullptr : metadata->Find("velvetyCommonParams");
		if (common == nullptr) {
			return std::nullopt;
		}
		const auto* ppm = common->Find("ppm");
		if (ppm == nullptr) {
			throw std::runtime_error("Emote Velvety global frame requires actual Common ppm");
		}
		EmoteVelvetyFrameDefinition definition;
		definition.PixelsPerMeter = FrameNumber(*ppm);
		if (const auto* reference = common->Find("velvetyReferenceLayerName")) {
			const auto* name = std::get_if<std::string>(&reference->Data);
			if (name == nullptr || name->size() > kMaximumMotionCommandNameBytes) {
				throw std::runtime_error("Emote Velvety reference layer requires a bounded native string");
			}
			definition.ReferenceLayer = name->substr(0, name->find('\0'));
		}
		return definition;
	}

	static resource::MotionMeshMatrix ComposeFrameMatrices(const resource::MotionMeshMatrix& outer, const resource::MotionMeshMatrix& inner) {
		resource::MotionMeshMatrix result;
		result.XX = inner.YX * outer.XY + inner.XX * outer.XX;
		result.XY = inner.YY * outer.XY + inner.XY * outer.XX;
		result.TranslationX = (inner.TranslationY * outer.XY + inner.TranslationX * outer.XX) + outer.TranslationX;
		result.YX = inner.YX * outer.YY + inner.XX * outer.YX;
		result.YY = inner.YY * outer.YY + inner.XY * outer.YX;
		result.TranslationY = (inner.TranslationY * outer.YY + inner.TranslationX * outer.YX) + outer.TranslationY;
		return result;
	}

	static resource::MotionPoint FramePoint(const resource::MotionPoint& point, const resource::MotionMeshMatrix& matrix) {
		return {(point.Y * matrix.XY + point.X * matrix.XX) + matrix.TranslationX, (point.Y * matrix.YY + point.X * matrix.YX) + matrix.TranslationY};
	}

	static resource::MotionMeshMatrix InverseFrameMatrix(const resource::MotionMeshMatrix& matrix) {
		const auto determinant = matrix.XX * matrix.YY - matrix.YX * matrix.XY;
		if (!std::isfinite(determinant) || determinant == 0) {
			throw std::runtime_error("Emote Velvety global frame requires an invertible actual conversion");
		}
		const auto factor = 1.0f / determinant;
		resource::MotionMeshMatrix inverse;
		inverse.XX = matrix.YY * factor;
		inverse.XY = -matrix.XY * factor;
		inverse.TranslationX = (matrix.TranslationY * matrix.XY - matrix.YY * matrix.TranslationX) * factor;
		inverse.YX = -matrix.YX * factor;
		inverse.YY = matrix.XX * factor;
		inverse.TranslationY = (matrix.YX * matrix.TranslationX - matrix.XX * matrix.TranslationY) * factor;
		return inverse;
	}

	static bool SameActualReference(const MotionSceneLayerReference& left, const MotionSceneLayerReference& right) {
		return left.Owner == right.Owner && left.RootSelectionSerial == right.RootSelectionSerial && left.ContextIdentity == right.ContextIdentity && left.ContextSelectionSerial == right.ContextSelectionSerial && left.NativeOrdinal == right.NativeOrdinal && left.NodeIdentifier == right.NodeIdentifier;
	}

	EmoteVelvetyFrameOwner::EmoteVelvetyFrameOwner(const MotionRootScene& actual_root, const MotionPlayerCommands& actual_commands, const MotionScenePlayer& actual_plain, EmoteVelvetyFrameDefinition definition, float actual_view_height) : m_Root(actual_root), m_Commands(actual_commands), m_Plain(actual_plain), m_Definition(std::move(definition)) {
		if (!std::isfinite(m_Definition.PixelsPerMeter) || !(m_Definition.PixelsPerMeter > 0) || !std::isfinite(m_Definition.ReferenceSpeedLimit) || m_Definition.ReferenceSpeedLimit < 0 || !std::isfinite(actual_view_height) || !(actual_view_height > 0) || m_Definition.ReferenceLayer.size() > kMaximumMotionCommandNameBytes) {
			throw std::runtime_error("Emote Velvety global frame requires actual bounded units, view height and reference policy");
		}
		const auto tangent = std::tan(std::bit_cast<float>(std::uint32_t{0x3f060a92}));
		m_DepthScale = -(actual_view_height / 100.0f) / (tangent + tangent);
	}

	EmoteVelvetyInputFrame EmoteVelvetyFrameOwner::Prepare(float elapsed_ticks, const EmoteVelvetyGlobalControls& actual_controls) {
		m_Prepared = false;
		const auto* scene = m_Plain.Scene();
		if (!m_Valid || !m_Plain.CanMutate() || !m_Plain.ResourcesLive() || (scene == nullptr && !m_Cleared) || !m_Root.Frame().Composed || !std::isfinite(elapsed_ticks) || !std::isfinite(actual_controls.Rotation)) {
			throw std::runtime_error("Emote Velvety global frame requires its actual completed live idle plain player");
		}
		const auto& root = m_Root.Overrides();
		if (!std::isfinite(root.ZoomX) || !(root.ZoomX > 0)) {
			throw std::runtime_error("Emote Velvety global frame requires finite positive actual plain root zoom");
		}
		m_Valid = false;
		const auto units = (1.0f / root.ZoomX) / m_Definition.PixelsPerMeter;
		resource::MotionMeshMatrix rotation;
		rotation.XX = std::cos(actual_controls.Rotation);
		rotation.XY = -std::sin(actual_controls.Rotation);
		rotation.YX = -rotation.XY;
		rotation.YY = rotation.XX;
		resource::MotionMeshMatrix subtract_root;
		subtract_root.TranslationX = -root.Coordinate[0];
		subtract_root.TranslationY = -root.Coordinate[1];
		resource::MotionMeshMatrix add_root;
		add_root.TranslationX = root.Coordinate[0];
		add_root.TranslationY = root.Coordinate[1];
		auto conversion = ComposeFrameMatrices(add_root, ComposeFrameMatrices(rotation, subtract_root));
		resource::MotionMeshMatrix scale;
		scale.XX = units;
		scale.YY = units;
		if ((actual_controls.Flags & 0x200u) != 0) {
			conversion = ComposeFrameMatrices(scale, conversion);
		}
		if ((actual_controls.Flags & 0x100u) != 0) {
			conversion = ComposeFrameMatrices(ComposeFrameMatrices(add_root, ComposeFrameMatrices(scale, subtract_root)), conversion);
		}
		resource::MotionPoint reference;
		if (scene == nullptr) {
			if (!m_ClearedReference || !m_HasReference) {
				throw std::runtime_error("Emote Velvety post-clear update requires an initialized native reference history; cold missing-anchor state remains unsupported");
			}
			reference = *m_ClearedReference;
		} else {
			if (!m_Lookup || scene != m_Scene || m_Commands.SelectionSerial() != m_SelectionSerial || scene->GraphGeneration() != m_GraphGeneration) {
				const auto selected_reference = scene->FindLayer(m_Definition.ReferenceLayer);
				if (!m_Reference || !selected_reference || !SameActualReference(*m_Reference, *selected_reference)) {
					m_HasReference = false;
				}
				m_Reference = selected_reference;
				m_Scene = scene;
				m_SelectionSerial = m_Commands.SelectionSerial();
				m_GraphGeneration = scene->GraphGeneration();
				m_Lookup = true;
			}
			if (!m_Reference) {
				throw std::runtime_error("Emote Velvety reference layer absent: native missing-anchor retained-position initialization remains unsupported");
			}
			const auto numeric = scene->LayerCoordinate(*m_Reference);
			reference = {numeric[0], numeric[1]};
			m_Cleared = false;
			m_ClearedReference.reset();
		}
		auto current = FramePoint(reference, conversion);
		if (actual_controls.Mirror) {
			current.X = -current.X;
		}
		if (!m_HasReference) {
			m_PrecedingReference = current;
			m_AccumulatedReference = current;
			m_HasReference = true;
		} else {
			resource::MotionPoint delta{current.X - m_PrecedingReference.X, current.Y - m_PrecedingReference.Y};
			m_PrecedingReference = current;
			const auto limit = m_Definition.ReferenceSpeedLimit;
			if (limit != 0 && elapsed_ticks != 0) {
				const auto seconds = elapsed_ticks / 60.0f;
				const auto x_velocity = delta.X / seconds;
				const auto y_velocity = delta.Y / seconds;
				const auto speed = std::sqrt(y_velocity * y_velocity + x_velocity * x_velocity);
				if (speed > 0) {
					const auto coefficient = ((1.0f - std::exp((speed / limit) * -1.60000002384185791015625f)) * limit) / speed;
					delta.X = delta.X * coefficient;
					delta.Y = delta.Y * coefficient;
				}
			}
			m_AccumulatedReference.X = delta.X + m_AccumulatedReference.X;
			m_AccumulatedReference.Y = delta.Y + m_AccumulatedReference.Y;
		}
		const auto preceding_x = conversion.TranslationX;
		const auto preceding_y = conversion.TranslationY;
		conversion.TranslationX = (preceding_y * 0.0f + preceding_x) + (m_AccumulatedReference.X - m_PrecedingReference.X);
		conversion.TranslationY = (preceding_x * 0.0f + preceding_y) + (m_AccumulatedReference.Y - m_PrecedingReference.Y);
		for (const auto value : {conversion.XX, conversion.XY, conversion.TranslationX, conversion.YX, conversion.YY, conversion.TranslationY}) {
			if (!std::isfinite(value)) {
				throw std::runtime_error("Emote Velvety global conversion exceeds finite actual frame state");
			}
		}
		m_Inverse = InverseFrameMatrix(conversion);
		EmoteVelvetyInputFrame result;
		result.WorldToPhysics = conversion;
		result.DepthScale = m_DepthScale;
		result.Mirror = actual_controls.Mirror;
		result.ResetRotationReference = m_First;
		m_First = false;
		m_Prepared = true;
		m_Valid = true;
		return result;
	}

	void EmoteVelvetyFrameOwner::ClearPlain(MotionScenePlayer& actual_plain) {
		if (m_Thread != std::this_thread::get_id() || &actual_plain != &m_Plain || !m_Valid || !m_Plain.CanMutate() || !m_Plain.ResourcesLive()) {
			throw std::runtime_error("Emote Velvety clear requires its exact live idle unqueued actual plain owner");
		}
		auto retained = m_ClearedReference;
		const auto* scene = m_Plain.Scene();
		if (scene != nullptr && m_Cleared && m_HasReference) {
			throw std::runtime_error("Emote Velvety clear requires its unchanged actual cached reference; post-clear physical-cell reuse must first complete an actual frame");
		}
		if (scene != nullptr && m_Reference) {
			if (!m_Lookup || scene != m_Scene || m_Commands.SelectionSerial() != m_SelectionSerial || scene->GraphGeneration() != m_GraphGeneration) {
				throw std::runtime_error("Emote Velvety clear requires its unchanged actual cached reference; changed physical-cell reuse remains unsupported");
			}
			const auto numeric = scene->LayerCoordinate(*m_Reference);
			if (!std::isfinite(numeric[0]) || !std::isfinite(numeric[1])) {
				throw std::runtime_error("Emote Velvety clear requires finite actual reference coordinates");
			}
			retained = resource::MotionPoint{numeric[0], numeric[1]};
		}

		actual_plain.Clear();
		// No node/evaluator borrow survives Clear. Raw XY remains distinct from converted history.
		m_ClearedReference = retained;
		m_Reference.reset();
		m_Scene = nullptr;
		m_Cleared = true;
	}

	const resource::MotionMeshMatrix& EmoteVelvetyFrameOwner::PhysicsToWorld() const {
		if (!m_Prepared) {
			throw std::runtime_error("Emote Velvety inverse requires its completed actual global frame");
		}
		return m_Inverse;
	}
} // namespace sgre::scene
