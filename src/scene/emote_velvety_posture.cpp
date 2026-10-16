#include "sgre/scene/emote_velvety_posture.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <stdexcept>

namespace sgre::scene {
	static constexpr float kNativeVelvetyPi = 3.1415927410125732421875f;
	static constexpr float kNativeVelvetyTwoPi = 6.283185482025146484375f;

	static void RequirePoint(const resource::MotionPoint& point) {
		if (!std::isfinite(point.X) || !std::isfinite(point.Y)) {
			throw std::runtime_error("Emote Velvety posture requires finite actual point inputs");
		}
	}

	static resource::MotionPoint ToPhysics(const resource::MotionPoint& point, const resource::MotionMeshMatrix& transform) {
		resource::MotionPoint result;
		result.X = (point.Y * transform.XY + point.X * transform.XX) + transform.TranslationX;
		result.Y = (point.Y * transform.YY + point.X * transform.YX) + transform.TranslationY;
		RequirePoint(result);
		return result;
	}

	static float NormalizeRotation(float angle) {
		if (!std::isfinite(angle) || std::abs(angle) > 65536.0f) {
			throw std::runtime_error("Emote Velvety rotation exceeds bounded native normalization work");
		}
		angle = angle + kNativeVelvetyPi;
		while (angle < 0) {
			angle = angle + kNativeVelvetyTwoPi;
		}
		while (angle >= kNativeVelvetyTwoPi) {
			angle = angle - kNativeVelvetyTwoPi;
		}
		return angle - kNativeVelvetyPi;
	}

	EmoteVelvetyPostureReader::EmoteVelvetyPostureReader(float width, float height, float origin_x, float origin_y, float pixels_per_meter, std::span<const resource::MotionPoint> fixes) {
		if (!(width > 0) || !(height > 0) || !(pixels_per_meter > 0) || fixes.size() > m_LocalFixes.size()) {
			throw std::runtime_error("Emote Velvety posture requires actual positive dimensions, ppm and at most three fixes");
		}
		for (const auto value : {width, height, origin_x, origin_y, pixels_per_meter}) {
			if (!std::isfinite(value)) {
				throw std::runtime_error("Emote Velvety posture requires finite actual metadata");
			}
		}
		m_LocalOrigin.X = ((origin_x - 0.5f) * width) * pixels_per_meter;
		m_LocalOrigin.Y = ((origin_y - 0.5f) * height) * pixels_per_meter;
		RequirePoint(m_LocalOrigin);
		m_FixCount = fixes.size();
		for (std::size_t index = 0; index != fixes.size(); ++index) {
			RequirePoint(fixes[index]);
			m_LocalFixes[index].X = ((fixes[index].X - 0.5f) * width) * pixels_per_meter;
			m_LocalFixes[index].Y = ((fixes[index].Y - 0.5f) * height) * pixels_per_meter;
			RequirePoint(m_LocalFixes[index]);
		}
	}

	void EmoteVelvetyPostureReader::SetLocalPixelOrigin(resource::MotionPoint origin) {
		if (!std::isfinite(origin.X) || !std::isfinite(origin.Y)) {
			throw std::runtime_error("Emote local pixel origin requires actual finite coordinates");
		}
		m_LocalOrigin = origin;
	}

	EmoteVelvetyPosture EmoteVelvetyPostureReader::Read(const MotionLayerUidReference& actual_layer, const EmoteVelvetyInputFrame& frame, const EmoteVelvetyPartPosture* actual_parent) {
		m_HasRead = false;
		if (!actual_layer.Active()) {
			throw std::runtime_error("Emote Velvety posture requires the actual active selected UID occurrence");
		}
		for (const auto value : {frame.WorldToPhysics.XX, frame.WorldToPhysics.XY, frame.WorldToPhysics.TranslationX, frame.WorldToPhysics.YX, frame.WorldToPhysics.YY, frame.WorldToPhysics.TranslationY, frame.DepthScale}) {
			if (!std::isfinite(value)) {
				throw std::runtime_error("Emote Velvety posture requires its actual finite driver conversion");
			}
		}
		EmoteVelvetyPosture result;
		result.CorrectionAngle = NormalizeRotation(actual_layer.RotationRadians());
		result.Physical.Rotation = result.CorrectionAngle;
		std::array<resource::MotionPoint, 8> local{};
		local[0] = m_LocalOrigin;
		const auto fit_count = actual_parent == nullptr ? std::size_t{5} : std::size_t{1};
		if (actual_parent == nullptr) {
			local[1] = {m_LocalOrigin.X - 1.0f, m_LocalOrigin.Y + 0.0f};
			local[2] = {m_LocalOrigin.X + 1.0f, m_LocalOrigin.Y + 0.0f};
			local[3] = {m_LocalOrigin.X + 0.0f, m_LocalOrigin.Y - 1.0f};
			local[4] = {m_LocalOrigin.X + 0.0f, m_LocalOrigin.Y + 1.0f};
		}
		std::copy_n(m_LocalFixes.begin(), m_FixCount, local.begin() + fit_count);
		auto world = local;
		actual_layer.TransformPoints(std::span(world).first(fit_count + m_FixCount));
		if (frame.Mirror) {
			for (auto& point : std::span(world).first(fit_count + m_FixCount)) {
				point.X = -point.X;
			}
		}
		const auto origin = ToPhysics(world[0], frame.WorldToPhysics);
		result.Physical.Coordinate[0] = origin.X;
		result.Physical.Coordinate[1] = origin.Y;
		for (std::size_t index = 0; index != m_FixCount; ++index) {
			m_WorldFixes[index] = ToPhysics(world[fit_count + index], frame.WorldToPhysics);
		}
		if (actual_parent != nullptr) {
			result.Physical.Rotation = actual_parent->Rotation;
			result.CorrectionScale = actual_parent->Scale;
		} else {
			float covariance_xy = 0;
			float variance_x = 0;
			float variance_y = 0;
			float cross_xx = 0;
			float cross_xy = 0;
			float cross_yx = 0;
			float cross_yy = 0;
			for (std::size_t index = 1; index != fit_count; ++index) {
				const auto x = local[index].X - local[0].X;
				const auto y = local[index].Y - local[0].Y;
				const auto mapped_x = world[index].X - world[0].X;
				const auto mapped_y = world[index].Y - world[0].Y;
				covariance_xy = covariance_xy + y * x;
				variance_x = variance_x + x * x;
				variance_y = variance_y + y * y;
				cross_xx = cross_xx + mapped_x * x;
				cross_xy = cross_xy + mapped_x * y;
				cross_yx = cross_yx + mapped_y * x;
				cross_yy = cross_yy + mapped_y * y;
			}
			const auto determinant = variance_y * variance_x - covariance_xy * covariance_xy;
			if (std::abs(determinant) >= std::bit_cast<float>(std::uint32_t{0x322bcc77})) {
				const auto inverse_xy = -covariance_xy / determinant;
				const auto inverse_y = variance_y / determinant;
				const auto inverse_x = variance_x / determinant;
				resource::MotionMeshMatrix fit;
				fit.XX = inverse_xy * cross_xy + inverse_y * cross_xx;
				fit.XY = inverse_x * cross_xy + inverse_xy * cross_xx;
				fit.YX = inverse_y * cross_yx + inverse_xy * cross_yy;
				fit.YY = inverse_xy * cross_yx + inverse_x * cross_yy;
				if (frame.ResetRotationReference) {
					m_RotationReference = {};
				}
				if (const auto decomposed = DecomposeMotionAffine(fit, m_RotationReference)) {
					m_RotationReference = decomposed->Rotation;
					result.Physical.Rotation = NormalizeRotation(decomposed->Rotation.Radians);
					result.CorrectionScale = actual_layer.DeformationScale();
				}
			}
		}
		if (!std::isfinite(result.CorrectionScale) || !(result.CorrectionScale > 0)) {
			throw std::runtime_error("Emote Velvety posture requires a finite positive actual inherited deformation scale");
		}
		if (!std::isfinite(result.Physical.Rotation) || std::abs(result.Physical.Rotation) > 65536.0f) {
			throw std::runtime_error("Emote Velvety posture requires the actual finite bounded part rotation");
		}
		result.Physical.Coordinate[2] = (1.0f / result.CorrectionScale - 1.0f) * frame.DepthScale;
		result.Physical.Cosine = std::cos(result.Physical.Rotation);
		result.Physical.Sine = std::sin(result.Physical.Rotation);
		if (!std::isfinite(result.Physical.Coordinate[2])) {
			throw std::runtime_error("Emote Velvety posture exceeds finite actual physical depth");
		}
		m_HasRead = true;
		return result;
	}

	std::span<const resource::MotionPoint> EmoteVelvetyPostureReader::WorldFixes() const {
		if (!m_HasRead) {
			throw std::runtime_error("Emote Velvety world fixes require a completed actual UID posture read");
		}
		return std::span(m_WorldFixes).first(m_FixCount);
	}
} // namespace sgre::scene
