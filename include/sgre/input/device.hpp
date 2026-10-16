#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace sgre::input {

	inline constexpr std::size_t kPhysicalKeyCount = 256;
	inline constexpr std::size_t kInputDeviceCount = 8;

	struct InputPoint {
		float X = 0;
		float Y = 0;
	};

	struct InputExtent {
		float Width = 0;
		float Height = 0;
	};

	enum class ControllerType : std::int32_t {
		Xbox = 0,
		Steam = 1,
		PlayStation4 = 2,
		PlayStation5 = 3,
		Switch = 4,
		Generic = 5,
		Unknown = 6
	};

	// The platform sampler resolves its physical devices and key mapping before publication.
	struct DeviceSample {
		std::uint32_t Buttons = 0;
		std::array<std::uint8_t, kPhysicalKeyCount> PhysicalKeys{};
		InputPoint LeftStick;
		InputPoint RightStick;
		float LeftTrigger = 0;
		float RightTrigger = 0;
		bool Connected = false;
	};

	struct RepeatSettings {
		bool Enabled = false;
		std::uint64_t InitialTicks = 0;
		std::uint64_t IntervalTicks = 0;
	};

	class InputDevice {
	public:
		// Native 0x1404f4200 consumes integer-truncated 60 Hz clock steps.
		void Advance(const DeviceSample& sample, std::int32_t elapsed_ticks, std::optional<InputExtent> cursor_extent = {});
		void SetButtonRepeat(std::uint32_t mask, std::uint64_t initial_ticks, std::uint64_t interval_ticks);
		void SetPhysicalRepeat(std::uint8_t key, const RepeatSettings& settings);
		[[nodiscard]] std::uint32_t Buttons() const;
		[[nodiscard]] std::uint32_t PreviousButtons() const;
		[[nodiscard]] std::uint32_t Pressed(std::uint32_t mask) const;
		[[nodiscard]] std::uint32_t Released(std::uint32_t mask) const;
		[[nodiscard]] bool ComboPressed(std::uint32_t mask) const;
		[[nodiscard]] std::int32_t StickDirection() const;
		[[nodiscard]] bool Connected() const;
		void SetControllerType(ControllerType type);
		[[nodiscard]] ControllerType Type() const;
		[[nodiscard]] InputPoint LeftStick() const;
		[[nodiscard]] InputPoint RightStick() const;
		[[nodiscard]] float LeftTrigger() const;
		[[nodiscard]] float RightTrigger() const;
		[[nodiscard]] InputPoint Cursor() const;
		[[nodiscard]] const std::array<std::uint8_t, kPhysicalKeyCount>& PhysicalKeys() const;
		[[nodiscard]] const std::array<std::uint8_t, kPhysicalKeyCount>& PreviousPhysicalKeys() const;
		[[nodiscard]] float ButtonRepeatRemaining() const;
		[[nodiscard]] float PhysicalRepeatRemaining(std::uint8_t key) const;

	private:
		struct PhysicalRepeat {
			RepeatSettings Settings;
			float RemainingTicks = 0;
		};

		std::uint32_t m_Buttons = 0;
		std::uint32_t m_PreviousButtons = 0;
		std::uint32_t m_RepeatMask = 0;
		std::uint64_t m_InitialTicks = 0;
		std::uint64_t m_IntervalTicks = 0;
		float m_RepeatRemaining = 0;
		std::array<std::uint8_t, kPhysicalKeyCount> m_PhysicalKeys{};
		std::array<std::uint8_t, kPhysicalKeyCount> m_PreviousPhysicalKeys{};
		std::array<PhysicalRepeat, kPhysicalKeyCount> m_PhysicalRepeat{};
		InputPoint m_LeftStick;
		InputPoint m_RightStick;
		InputPoint m_Cursor;
		float m_LeftTrigger = 0;
		float m_RightTrigger = 0;
		bool m_Connected = false;
		ControllerType m_ControllerType = ControllerType::Xbox;
	};

} // namespace sgre::input
