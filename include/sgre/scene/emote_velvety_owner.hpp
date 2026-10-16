#pragma once

#include "sgre/resource/psb.hpp"
#include "sgre/scene/emote_velvety_frame.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>

namespace sgre::scene {
	class EmoteVariableControls;
	class MotionPlayerCommands;
	class MotionRootScene;
	class MotionScenePlayer;

	struct EmoteVelvetyOwnerCounts {
		std::size_t Bloom = 0;
		std::size_t String = 0;
		std::size_t Catenary = 0;
		std::size_t Jelly = 0;
		std::size_t Stretch = 0;
		std::size_t Synchronize = 0;
		std::size_t Parts = 0;
		std::size_t AttachedPartsControls = 0;
		std::size_t Updated = 0;
		std::size_t MeshWrites = 0;
	};

	class EmoteVelvetyOwner {
	public:
		// The same genuine plain/Root/Commands, registered document and scalar owner enclose this driver.
		// Entropy supplies the separate native2e9280 stream once when Common metadata is present.
		EmoteVelvetyOwner(const MotionRootScene& actual_root, const MotionPlayerCommands& actual_primary, const MotionScenePlayer& actual_plain, const resource::PsbDocument& document, EmoteVariableControls& actual_variables, const std::function<std::uint32_t()>& read_entropy_word);
		~EmoteVelvetyOwner() noexcept;
		EmoteVelvetyOwner(const EmoteVelvetyOwner&) = delete;
		EmoteVelvetyOwner& operator=(const EmoteVelvetyOwner&) = delete;
		// Runs only after the containing plain Progress; it never evaluates Motion a second time.
		void Update(float elapsed_ticks, const EmoteVelvetyGlobalControls& actual_controls);
		// Runs the genuine plain clear while preserving only the measured warm numeric-cell lifetime.
		void ClearPlain(MotionScenePlayer& actual_plain);
		[[nodiscard]] bool Enabled() const noexcept;
		[[nodiscard]] EmoteVelvetyOwnerCounts Counts() const;
		[[nodiscard]] std::uint64_t FrameSerial() const;
		// Copies the already completed owned driver conversion; it never prepares or advances a frame.
		[[nodiscard]] resource::MotionMeshMatrix PhysicsToWorld() const;
		[[nodiscard]] float CharacterHeight() const;
		void Close();

	private:
		class Implementation;
		std::unique_ptr<Implementation> m_Implementation;
	};
} // namespace sgre::scene
