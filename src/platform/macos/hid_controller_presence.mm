#include "hid_controller_presence.hpp"

#import <Foundation/Foundation.h>
#include <IOKit/hid/IOHIDKeys.h>
#include <IOKit/hid/IOHIDUsageTables.h>

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace sgre::platform {

	template <class T>
	class InputCfOwner {
	public:
		explicit InputCfOwner(T value) : m_Value(value) {
			if (value == nullptr) {
				throw std::runtime_error("macOS input inventory could not create its CoreFoundation owner");
			}
		}
		~InputCfOwner() {
			CFRelease(m_Value);
		}
		InputCfOwner(const InputCfOwner&) = delete;
		InputCfOwner& operator=(const InputCfOwner&) = delete;
		[[nodiscard]] T Get() const noexcept {
			return m_Value;
		}

	private:
		T m_Value;
	};

	HidControllerPresence::HidControllerPresence() {
		if (![NSThread isMainThread]) {
			throw std::runtime_error("macOS input inventory requires the actual application main thread");
		}
		m_Manager = IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDManagerOptionNone);
		if (m_Manager == nullptr) {
			throw std::runtime_error("macOS input inventory could not create the actual HID manager");
		}
		try {
			const InputCfOwner matching(CFArrayCreateMutable(kCFAllocatorDefault, 3, &kCFTypeArrayCallBacks));
			const std::int32_t page = kHIDPage_GenericDesktop;
			const InputCfOwner page_number(CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &page));
			for (const std::int32_t usage : {kHIDUsage_GD_Joystick, kHIDUsage_GD_GamePad, kHIDUsage_GD_MultiAxisController}) {
				const InputCfOwner usage_number(CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &usage));
				std::array<const void*, 2> keys{CFSTR(kIOHIDDeviceUsagePageKey), CFSTR(kIOHIDDeviceUsageKey)};
				std::array<const void*, 2> values{page_number.Get(), usage_number.Get()};
				const InputCfOwner criteria(CFDictionaryCreate(kCFAllocatorDefault, keys.data(), values.data(), static_cast<CFIndex>(keys.size()), &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks));
				CFArrayAppendValue(matching.Get(), criteria.Get());
			}
			IOHIDManagerSetDeviceMatchingMultiple(m_Manager, matching.Get());
			IOHIDManagerRegisterDeviceMatchingCallback(m_Manager, Matched, this);
			IOHIDManagerRegisterDeviceRemovalCallback(m_Manager, Removed, this);
			IOHIDManagerScheduleWithRunLoop(m_Manager, CFRunLoopGetMain(), kCFRunLoopDefaultMode);
			m_Scheduled = true;
			// Device enumeration does not require opening or seizing the actual devices.
			CFSetRef initial = IOHIDManagerCopyDevices(m_Manager);
			if (initial != nullptr) {
				CFSetApplyFunction(initial, InitialDevice, this);
				CFRelease(initial);
			}
			RequireLive();
		} catch (...) {
			Close();
			throw;
		}
	}

	HidControllerPresence::~HidControllerPresence() {
		Close();
	}

	void HidControllerPresence::RequireLive() const {
		if (std::this_thread::get_id() != m_Thread || m_Closed || m_Manager == nullptr) {
			throw std::runtime_error("macOS input inventory requires its live actual main-thread owner");
		}
		if (m_Failure) {
			std::rethrow_exception(m_Failure);
		}
	}

	void HidControllerPresence::Record(IOHIDDeviceRef device, IOReturn result, bool added) noexcept {
		if (m_Closed) {
			return;
		}
		try {
			RequireLive();
			if (result != kIOReturnSuccess || device == nullptr) {
				throw std::runtime_error("macOS actual HID inventory callback failed: " + std::to_string(result));
			}
			const auto first = m_Devices.begin();
			const auto end = first + m_Count;
			const auto found = std::find(first, end, device);
			if (added) {
				if (found == end) {
					if (m_Count == m_Devices.size()) {
						throw std::runtime_error("macOS actual controller inventory exceeds checked collection bound");
					}
					m_Devices[m_Count++] = device;
				}
			} else if (found != end) {
				*found = m_Devices[--m_Count];
				m_Devices[m_Count] = nullptr;
			}
		} catch (...) {
			// No authored exception may cross an IOHID C callback.
			m_Failure = std::current_exception();
		}
	}

	void HidControllerPresence::Matched(void* context, IOReturn result, void*, IOHIDDeviceRef device) noexcept {
		static_cast<HidControllerPresence*>(context)->Record(device, result, true);
	}

	void HidControllerPresence::Removed(void* context, IOReturn result, void*, IOHIDDeviceRef device) noexcept {
		static_cast<HidControllerPresence*>(context)->Record(device, result, false);
	}

	void HidControllerPresence::InitialDevice(const void* device, void* context) noexcept {
		static_cast<HidControllerPresence*>(context)->Record(static_cast<IOHIDDeviceRef>(const_cast<void*>(device)), kIOReturnSuccess, true);
	}

	std::size_t HidControllerPresence::Count() const {
		RequireLive();
		return m_Count;
	}

	void HidControllerPresence::RequireKeyboardOnly() const {
		if (Count() != 0) {
			throw std::runtime_error("macOS InputHub requires recovered actual gamepad samples for the connected HID controller domain");
		}
	}

	void HidControllerPresence::Close() noexcept {
		if (m_Closed) {
			return;
		}
		if (std::this_thread::get_id() != m_Thread) {
			std::terminate();
		}
		m_Closed = true;
		if (m_Manager != nullptr) {
			IOHIDManagerRegisterDeviceMatchingCallback(m_Manager, nullptr, nullptr);
			IOHIDManagerRegisterDeviceRemovalCallback(m_Manager, nullptr, nullptr);
			if (m_Scheduled) {
				IOHIDManagerUnscheduleFromRunLoop(m_Manager, CFRunLoopGetMain(), kCFRunLoopDefaultMode);
			}
			CFRelease(m_Manager);
			m_Manager = nullptr;
		}
		m_Devices = {};
		m_Count = 0;
	}

} // namespace sgre::platform
