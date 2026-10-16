#pragma once

#include "sgre/resource/psb.hpp"
#include "sgre/scene/emote_velvety_posture.hpp"

#include <cstddef>
#include <memory>
#include <optional>
#include <string_view>

namespace sgre::scene {

	class EmoteStringControl;
	class EmoteJellyControl;
	class EmoteBloomControl;
	class EmoteCatenaryControl;
	class EmoteSynchronizeControl;
	class EmoteStretchControl;
	class MotionPlayerCommands;

	class EmotePartsTree {
	public:
		// Native2f5870 owns the real serialized tree; Commands and attached controls enclose this scheduler.
		// Concrete String/Jelly/Bloom/Catenary/Synchronize/Stretch lanes do not stand in for the other physical families or containing MEmote.
		EmotePartsTree(const MotionPlayerCommands& actual_primary, const resource::PsbValue& actual_tree);
		~EmotePartsTree() noexcept;
		EmotePartsTree(const EmotePartsTree&) = delete;
		EmotePartsTree& operator=(const EmotePartsTree&) = delete;
		// Native2ed980..2ed9e2 appends only to an exact indexed UID. An absent UID discards that callback.
		[[nodiscard]] bool AttachString(EmoteStringControl& actual_control);
		[[nodiscard]] bool AttachJelly(EmoteJellyControl& actual_control);
		[[nodiscard]] bool AttachBloom(EmoteBloomControl& actual_control);
		[[nodiscard]] bool AttachCatenary(EmoteCatenaryControl& actual_control);
		[[nodiscard]] bool AttachSynchronize(EmoteSynchronizeControl& actual_control);
		[[nodiscard]] bool AttachStretch(EmoteStretchControl& actual_control);
		// Native2f5bc0: callbacks before children, serialized sibling order, genuine parent retained posture.
		// Actual plain Progress, completed UID joins and global/collider preparation precede this operation.
		void UpdateStrings(float elapsed_ticks, const EmoteVelvetyInputFrame& actual_frame);
		void UpdateControls(float elapsed_ticks, const EmoteVelvetyInputFrame& actual_frame);
		[[nodiscard]] std::optional<EmoteVelvetyPartPosture> Posture(std::string_view uid) const;
		[[nodiscard]] std::size_t PartCount() const;
		[[nodiscard]] std::size_t StringCount() const;
		[[nodiscard]] std::size_t JellyCount() const;
		[[nodiscard]] std::size_t BloomCount() const;
		[[nodiscard]] std::size_t CatenaryCount() const;
		[[nodiscard]] std::size_t SynchronizeCount() const;
		[[nodiscard]] std::size_t StretchCount() const;
		void Close();

	private:
		class Implementation;
		std::unique_ptr<Implementation> m_Implementation;
	};

} // namespace sgre::scene
