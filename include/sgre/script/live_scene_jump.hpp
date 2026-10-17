#pragma once

#include "sgre/script/original_checkpoint.hpp"

#include <functional>
#include <memory>

namespace sgre::script {

	class LiveSceneJump {
	public:
		explicit LiveSceneJump(SquirrelVm& vm);
		~LiveSceneJump();
		LiveSceneJump(const LiveSceneJump&) = delete;
		LiveSceneJump& operator=(const LiveSceneJump&) = delete;
		void InstallAvailableHooks();
		[[nodiscard]] bool CanJump() const;
		void Queue(std::span<const std::uint8_t> checkpoint, const OriginalCheckpointLocation& location, std::string_view identity, std::function<void()> on_consumed = {});
		[[nodiscard]] bool Pending() const noexcept;

	private:
		class Implementation;
		std::shared_ptr<Implementation> m_Implementation;
	};

} // namespace sgre::script
