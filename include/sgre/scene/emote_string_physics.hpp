#pragma once

#include "sgre/resource/psb.hpp"
#include "sgre/scene/emote_tress_model.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sgre::scene {
	struct EmoteStringLine {
		float TopX = 0;
		float TopY = 0;
		float BottomX = 0;
		float BottomY = 1;
	};

	struct EmoteStringDefinition {
		std::string Uid;
		bool HorizontalBones = false;
		std::uint8_t LongitudinalDegree = 4;
		std::uint32_t Substeps = 1;
		float Speed = 1;
		float Gravity = 0;
		float DefaultRotation = 0;
		float Straightness = 0;
		float Roughness = 0;
		float Mass = 0;
		float SizeHorizontal = 0;
		float SizeVertical = 0;
		float OriginX = 0.5f;
		float OriginY = 0.5f;
		float SplitLeft = 0;
		float SplitRight = 0;
		float ParticlesPerMeter = 0;
		float ControlRadius = 0;
		EmoteTressRigidSide RigidSide = EmoteTressRigidSide::None;
		float FrictionTop = 0;
		float FrictionBottom = 0;
		float Elastic = 0;
		std::array<float, 3> CoordinateScale{1, 1, 1};
		float CompressWiden = 0;
		bool AdhocSimulation = false;
		bool MassTiltedDistribution = false;
		std::uint32_t CollisionHardness = 0;
		float CollisionRepulsion = 0;
		std::vector<EmoteStringLine> Tresses;
	};

	// Native2ec840 physical entity inputs. Owns names and authored line geometry.
	// Parent-mesh/fix correction, speed-variable routing and collider resolution belong to the containing UID driver.
	[[nodiscard]] std::vector<EmoteStringDefinition> ReadEmoteStringPhysicsDefinitions(const resource::PsbDocument& document);

	// Native309af0/30da60 physical String entity. Each real authored tress owns its actual two-sided model and spline.
	// Physical samples are the native intermediate before inverse basis and parent/fix correction, not a drawable override.
	class EmoteStringPhysics {
	public:
		explicit EmoteStringPhysics(EmoteStringDefinition definition);
		~EmoteStringPhysics() noexcept;
		EmoteStringPhysics(const EmoteStringPhysics&) = delete;
		EmoteStringPhysics& operator=(const EmoteStringPhysics&) = delete;
		// Native assignment copies mutable values while retaining this owner's resources and bindings.
		void RequireCanAssignState(const EmoteStringPhysics& source) const;
		void AssignState(const EmoteStringPhysics& source);
		void SetPosture(const EmotePhysicalPosture& posture);
		void Initialize();
		void ClearForcesAndMeasureBounds();
		void AddForce(std::size_t tress, bool first_side, std::size_t particle, const std::array<float, 3>& force);
		// Every collider is retained by the containing driver through this physical instance's retirement.
		void AttachCollider(EmoteCollider& collider, std::uint32_t tress_mask);
		void Update(float elapsed_ticks);
		// Samples use transverse inner cubic and longitudinal outer degree before native horizontal-bone rotation.
		[[nodiscard]] std::span<const std::array<float, 2>> PreparePhysicalSamples();
		[[nodiscard]] std::size_t TressCount() const noexcept;
		[[nodiscard]] std::string_view Uid() const noexcept;
		// Borrows this physical owner, including its immutable authored geometry.
		[[nodiscard]] const EmoteStringDefinition& Definition() const noexcept;
		[[nodiscard]] std::span<const float> LongitudinalKnots() const noexcept;
		[[nodiscard]] bool HorizontalBones() const noexcept;
		[[nodiscard]] std::uint8_t EncodedDegree() const noexcept;

	private:
		friend class EmoteStringControl;
		friend class EmoteStringMeshOutput;
		void UpdateForUnorderedComparison(float elapsed_ticks);
		void UpdateImpl(float elapsed_ticks, bool unordered_comparison);
		[[nodiscard]] std::span<const std::array<float, 2>> PrepareSamplesForUnorderedComparison();
		[[nodiscard]] std::span<const std::array<float, 2>> PrepareSamplesImpl(bool unordered_comparison);
		class Implementation;
		std::unique_ptr<Implementation> m_Implementation;
	};
} // namespace sgre::scene
