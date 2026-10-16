#pragma once

#include "sgre/scene/motion_scissor.hpp"

#include <cstddef>
#include <vector>

namespace sgre::scene {

	class GraphicsModelStack {
	public:
		explicit GraphicsModelStack(std::size_t capacity);
		GraphicsModelStack(const GraphicsModelStack&) = delete;
		GraphicsModelStack& operator=(const GraphicsModelStack&) = delete;
		GraphicsModelStack(GraphicsModelStack&&) = delete;
		GraphicsModelStack& operator=(GraphicsModelStack&&) = delete;
		// The native virtual-screen task copies the current model, or identity when empty.
		// Native preloop replaces every prior slot with one identity; all prior model borrows end.
		void Reset();
		void Push();
		void Pop();
		void SetCurrent(const MotionScissorMatrix& model);
		// This borrow ends when its slot is popped or the containing stack is destroyed.
		[[nodiscard]] const MotionScissorMatrix& Current() const;
		[[nodiscard]] std::size_t Depth() const noexcept;

	private:
		std::vector<MotionScissorMatrix> m_Models;
		std::size_t m_Capacity;
	};

} // namespace sgre::scene
