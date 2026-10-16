#pragma once

#include "sgre/scene/emote_emission_definition.hpp"
#include "sgre/scene/emote_velvety_posture.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

namespace sgre::scene {
	class EmoteVariableControls;
	class MotionPlayerCommands;
	class EmoteEmissionOwner;

	class EmoteEmissionGroup {
		class Implementation;

	public:
		explicit EmoteEmissionGroup(std::unique_ptr<Implementation> implementation);
		~EmoteEmissionGroup() noexcept;
		EmoteEmissionGroup(const EmoteEmissionGroup&) = delete;
		EmoteEmissionGroup& operator=(const EmoteEmissionGroup&) = delete;
		// Native31ab20 selects this actual pool for one physical entity before its point/ellipse queries.
		void PrepareBounds(float left, float top, float right, float bottom, float extra_radius);
		[[nodiscard]] std::array<float, 3> SamplePoint(const resource::MotionPoint& point, float radius) const;
		[[nodiscard]] std::array<float, 3> SampleEllipse(const EmoteCollisionEllipse& ellipse) const;
		// Borrows completed pool output until the next actual emission phase.
		[[nodiscard]] std::span<const EmoteEmissionParticle> Particles() const;
		[[nodiscard]] std::uint64_t PreparedFrameSerial() const;

	private:
		friend class EmoteEmissionOwner;
		std::unique_ptr<Implementation> m_Implementation;
	};

	class EmoteEmissionOwner {
	public:
		// Metadata, actual primary Commands and actual scalar-variable owner enclose every group lease.
		// A null variable owner is admitted only when acquired groups have no speed/trigger variable bindings.
		// The entropy word is the separate native2e9280 rand_s input, not the eye stream or a default seed.
		EmoteEmissionOwner(const MotionPlayerCommands& actual_primary, const resource::PsbDocument& document, EmoteVariableControls* actual_variables, std::uint32_t actual_entropy_word);
		~EmoteEmissionOwner() noexcept;
		EmoteEmissionOwner(const EmoteEmissionOwner&) = delete;
		EmoteEmissionOwner& operator=(const EmoteEmissionOwner&) = delete;
		[[nodiscard]] std::shared_ptr<EmoteEmissionGroup> Acquire(std::string_view uid);
		// After genuine plain Progress/source/collider poses, before the ordered physical-family updates.
		void Prepare(const EmoteVelvetyInputFrame& actual_frame, float elapsed_ticks);
		void Close();
		[[nodiscard]] bool UsesCommands(const MotionPlayerCommands& actual_primary) const noexcept;
		[[nodiscard]] std::uint64_t PreparedFrameSerial() const;
		[[nodiscard]] std::size_t GroupCount() const noexcept;
		[[nodiscard]] EmoteEmissionRandomState RandomState() const;

	private:
		class Implementation;
		std::unique_ptr<Implementation> m_Implementation;
	};
} // namespace sgre::scene
