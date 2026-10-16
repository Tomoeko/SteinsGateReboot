#include "sgre/scene/graphics_model_stack.hpp"

#include <stdexcept>

namespace sgre::scene {

	GraphicsModelStack::GraphicsModelStack(std::size_t capacity) : m_Capacity(capacity) {
		if (capacity == 0 || capacity > 1024) {
			throw std::runtime_error("graphics model stack requires capacity 1..1024");
		}

		m_Models.reserve(capacity);
	}

	void GraphicsModelStack::Reset() {
		m_Models.clear();
		m_Models.push_back(MotionScissorMatrix{});
	}

	void GraphicsModelStack::Push() {
		if (m_Models.size() == m_Capacity) {
			throw std::runtime_error("graphics model stack exceeds its allocated capacity");
		}

		const auto model = m_Models.empty() ? MotionScissorMatrix{} : m_Models.back();
		m_Models.push_back(model);
	}

	void GraphicsModelStack::Pop() {
		if (m_Models.empty()) {
			throw std::runtime_error("graphics model stack cannot pop an empty owner");
		}

		m_Models.pop_back();
	}

	void GraphicsModelStack::SetCurrent(const MotionScissorMatrix& model) {
		if (m_Models.empty()) {
			throw std::runtime_error("graphics model stack requires a current slot");
		}

		m_Models.back() = model;
	}

	const MotionScissorMatrix& GraphicsModelStack::Current() const {
		if (m_Models.empty()) {
			throw std::runtime_error("graphics model stack requires a current slot");
		}

		return m_Models.back();
	}

	std::size_t GraphicsModelStack::Depth() const noexcept {
		return m_Models.size();
	}

} // namespace sgre::scene
