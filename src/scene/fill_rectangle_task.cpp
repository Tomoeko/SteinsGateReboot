#include "sgre/scene/fill_rectangle_task.hpp"

#include <cmath>
#include <stdexcept>

namespace sgre::scene {
	static void RequireFillCoordinate(float value) {
		if (!std::isfinite(value)) {
			throw std::runtime_error("FillRect coordinate exceeds its finite supported domain");
		}
	}

	void FillRectangleTask::RequireIdle() const {
		if (m_State.Closed || m_Drawing) {
			throw std::runtime_error("FillRect requires its live idle actual task owner");
		}
	}

	FillRectangleState FillRectangleTask::State() const noexcept {
		return m_State;
	}

	void FillRectangleTask::SetCoordinate(resource::MotionPoint coordinate) {
		RequireIdle();
		RequireFillCoordinate(coordinate.X);
		RequireFillCoordinate(coordinate.Y);
		m_State.Coordinate = coordinate;
	}

	void FillRectangleTask::SetLeft(float left) {
		SetCoordinate({left, m_State.Coordinate.Y});
	}

	void FillRectangleTask::SetTop(float top) {
		SetCoordinate({m_State.Coordinate.X, top});
	}

	void FillRectangleTask::SetSize(resource::MotionPoint size) {
		RequireIdle();
		RequireFillCoordinate(size.X);
		RequireFillCoordinate(size.Y);
		m_State.Size = size;
	}

	void FillRectangleTask::SetWidth(float width) {
		SetSize({width, m_State.Size.Y});
	}

	void FillRectangleTask::SetHeight(float height) {
		SetSize({m_State.Size.X, height});
	}

	void FillRectangleTask::SetColor(std::uint32_t rgba) {
		RequireIdle();
		m_State.Color = rgba;
	}

	void FillRectangleTask::SetOpacity(std::uint32_t opacity) {
		RequireIdle();
		// Native04da40 zero-extends DL before storing the DWORD.
		m_State.Opacity = opacity & 255u;
	}

	void FillRectangleTask::SetBlendMode(std::uint32_t mode) {
		RequireIdle();
		m_State.BlendMode = mode;
	}

	void FillRectangleTask::Draw(const DrawRectangle& actual_drawing) {
		RequireIdle();
		if (m_State.Color == 0) {
			return;
		}
		if (!actual_drawing) {
			throw std::runtime_error("FillRect requires its actual untextured rectangle drawing service");
		}
		const float right = m_State.Coordinate.X + m_State.Size.X;
		const float bottom = m_State.Coordinate.Y + m_State.Size.Y;
		RequireFillCoordinate(right);
		RequireFillCoordinate(bottom);
		const std::uint32_t alpha_product = (m_State.Color & 255u) * m_State.Opacity;
		const auto alpha = (alpha_product / 255u) & 255u;
		const auto rgba = (m_State.Color & 0xffffff00u) | alpha;
		FillRectangleDrawPacket packet;
		packet.Vertices = {{{m_State.Coordinate.X, m_State.Coordinate.Y}, {m_State.Coordinate.X, bottom}, {right, m_State.Coordinate.Y}, {right, bottom}}};
		packet.Color = (rgba >> 8) | (rgba << 24);
		m_Drawing = true;
		try {
			actual_drawing(packet);
		} catch (...) {
			m_Drawing = false;
			throw;
		}
		m_Drawing = false;
	}

	bool FillRectangleTask::CanClose() const noexcept {
		return !m_Drawing;
	}

	void FillRectangleTask::Close() {
		if (m_Drawing) {
			throw std::runtime_error("FillRect cannot retire its current draw packet borrow");
		}
		m_State.Closed = true;
	}
} // namespace sgre::scene
