#pragma once

#include <IOKit/hid/IOHIDManager.h>

#include <array>
#include <cstddef>
#include <exception>
#include <thread>

namespace sgre::platform {

	// This inventory opens no device input stream. It observes actual HID controller collections
	// so the keyboard-only backend cannot silently replace a connected controller with zeros.
	class HidControllerPresence {
	public:
		HidControllerPresence();
		~HidControllerPresence();
		HidControllerPresence(const HidControllerPresence&) = delete;
		HidControllerPresence& operator=(const HidControllerPresence&) = delete;
		[[nodiscard]] std::size_t Count() const;
		void RequireKeyboardOnly() const;
		void Close() noexcept;

	private:
		static void Matched(void* context, IOReturn result, void* sender, IOHIDDeviceRef device) noexcept;
		static void Removed(void* context, IOReturn result, void* sender, IOHIDDeviceRef device) noexcept;
		static void InitialDevice(const void* device, void* context) noexcept;
		void Record(IOHIDDeviceRef device, IOReturn result, bool added) noexcept;
		void RequireLive() const;
		IOHIDManagerRef m_Manager = nullptr;
		std::array<IOHIDDeviceRef, 64> m_Devices{};
		std::size_t m_Count = 0;
		std::exception_ptr m_Failure;
		const std::thread::id m_Thread = std::this_thread::get_id();
		bool m_Scheduled = false;
		bool m_Closed = false;
	};

} // namespace sgre::platform
