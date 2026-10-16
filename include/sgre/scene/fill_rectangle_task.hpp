#pragma once

#include "sgre/scene/fill_rectangle_packet.hpp"

#include <cstdint>
#include <functional>

namespace sgre::scene {
	struct FillRectangleState {
		resource::MotionPoint Coordinate;
		resource::MotionPoint Size;
		std::uint32_t Color = 0;
		std::uint32_t Opacity = 255;
		// Native stores this property, but Draw04d980 never reads it:4f5740 selects blend0.
		std::uint32_t BlendMode = 0;
		bool Closed = false;
	};

	class FillRectangleTask {
	public:
		using DrawRectangle = std::function<void(const FillRectangleDrawPacket&)>;
		FillRectangleTask() = default;
		FillRectangleTask(const FillRectangleTask&) = delete;
		FillRectangleTask& operator=(const FillRectangleTask&) = delete;
		[[nodiscard]] FillRectangleState State() const noexcept;
		void SetCoordinate(resource::MotionPoint coordinate);
		void SetLeft(float left);
		void SetTop(float top);
		void SetSize(resource::MotionPoint size);
		void SetWidth(float width);
		void SetHeight(float height);
		void SetColor(std::uint32_t rgba);
		void SetOpacity(std::uint32_t opacity);
		void SetBlendMode(std::uint32_t mode);
		// Native04d980: four logical XYs and low-byte alpha multiplication; no texture or update work.
		void Draw(const DrawRectangle& actual_drawing);
		[[nodiscard]] bool CanClose() const noexcept;
		void Close();

	private:
		void RequireIdle() const;
		FillRectangleState m_State;
		bool m_Drawing = false;
	};
} // namespace sgre::scene
