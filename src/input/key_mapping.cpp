#include "sgre/input/key_mapping.hpp"

#include <algorithm>
#include <array>
#include <stdexcept>

namespace sgre::input {

	// Readable compatibility names recovered from native tables 0x14096ad40/0x14096c0d0.
	static constexpr std::array<NamedPhysicalKey, 312> kPhysicalNames{{
	    {"DIK_ESCAPE", 0x1, KeyboardCodeSet::Scan},
	    {"DIK_1", 0x2, KeyboardCodeSet::Scan},
	    {"DIK_2", 0x3, KeyboardCodeSet::Scan},
	    {"DIK_3", 0x4, KeyboardCodeSet::Scan},
	    {"DIK_4", 0x5, KeyboardCodeSet::Scan},
	    {"DIK_5", 0x6, KeyboardCodeSet::Scan},
	    {"DIK_6", 0x7, KeyboardCodeSet::Scan},
	    {"DIK_7", 0x8, KeyboardCodeSet::Scan},
	    {"DIK_8", 0x9, KeyboardCodeSet::Scan},
	    {"DIK_9", 0xa, KeyboardCodeSet::Scan},
	    {"DIK_0", 0xb, KeyboardCodeSet::Scan},
	    {"DIK_MINUS", 0xc, KeyboardCodeSet::Scan},
	    {"DIK_EQUALS", 0xd, KeyboardCodeSet::Scan},
	    {"DIK_BACK", 0xe, KeyboardCodeSet::Scan},
	    {"DIK_TAB", 0xf, KeyboardCodeSet::Scan},
	    {"DIK_Q", 0x10, KeyboardCodeSet::Scan},
	    {"DIK_W", 0x11, KeyboardCodeSet::Scan},
	    {"DIK_E", 0x12, KeyboardCodeSet::Scan},
	    {"DIK_R", 0x13, KeyboardCodeSet::Scan},
	    {"DIK_T", 0x14, KeyboardCodeSet::Scan},
	    {"DIK_Y", 0x15, KeyboardCodeSet::Scan},
	    {"DIK_U", 0x16, KeyboardCodeSet::Scan},
	    {"DIK_I", 0x17, KeyboardCodeSet::Scan},
	    {"DIK_O", 0x18, KeyboardCodeSet::Scan},
	    {"DIK_P", 0x19, KeyboardCodeSet::Scan},
	    {"DIK_LBRACKET", 0x1a, KeyboardCodeSet::Scan},
	    {"DIK_RBRACKET", 0x1b, KeyboardCodeSet::Scan},
	    {"DIK_RETURN", 0x1c, KeyboardCodeSet::Scan},
	    {"DIK_LCONTROL", 0x1d, KeyboardCodeSet::Scan},
	    {"DIK_A", 0x1e, KeyboardCodeSet::Scan},
	    {"DIK_S", 0x1f, KeyboardCodeSet::Scan},
	    {"DIK_D", 0x20, KeyboardCodeSet::Scan},
	    {"DIK_F", 0x21, KeyboardCodeSet::Scan},
	    {"DIK_G", 0x22, KeyboardCodeSet::Scan},
	    {"DIK_H", 0x23, KeyboardCodeSet::Scan},
	    {"DIK_J", 0x24, KeyboardCodeSet::Scan},
	    {"DIK_K", 0x25, KeyboardCodeSet::Scan},
	    {"DIK_L", 0x26, KeyboardCodeSet::Scan},
	    {"DIK_SEMICOLON", 0x27, KeyboardCodeSet::Scan},
	    {"DIK_APOSTROPHE", 0x28, KeyboardCodeSet::Scan},
	    {"DIK_GRAVE", 0x29, KeyboardCodeSet::Scan},
	    {"DIK_LSHIFT", 0x2a, KeyboardCodeSet::Scan},
	    {"DIK_BACKSLASH", 0x2b, KeyboardCodeSet::Scan},
	    {"DIK_Z", 0x2c, KeyboardCodeSet::Scan},
	    {"DIK_X", 0x2d, KeyboardCodeSet::Scan},
	    {"DIK_C", 0x2e, KeyboardCodeSet::Scan},
	    {"DIK_V", 0x2f, KeyboardCodeSet::Scan},
	    {"DIK_B", 0x30, KeyboardCodeSet::Scan},
	    {"DIK_N", 0x31, KeyboardCodeSet::Scan},
	    {"DIK_M", 0x32, KeyboardCodeSet::Scan},
	    {"DIK_COMMA", 0x33, KeyboardCodeSet::Scan},
	    {"DIK_PERIOD", 0x34, KeyboardCodeSet::Scan},
	    {"DIK_SLASH", 0x35, KeyboardCodeSet::Scan},
	    {"DIK_RSHIFT", 0x36, KeyboardCodeSet::Scan},
	    {"DIK_MULTIPLY", 0x37, KeyboardCodeSet::Scan},
	    {"DIK_LMENU", 0x38, KeyboardCodeSet::Scan},
	    {"DIK_SPACE", 0x39, KeyboardCodeSet::Scan},
	    {"DIK_CAPITAL", 0x3a, KeyboardCodeSet::Scan},
	    {"DIK_F1", 0x3b, KeyboardCodeSet::Scan},
	    {"DIK_F2", 0x3c, KeyboardCodeSet::Scan},
	    {"DIK_F3", 0x3d, KeyboardCodeSet::Scan},
	    {"DIK_F4", 0x3e, KeyboardCodeSet::Scan},
	    {"DIK_F5", 0x3f, KeyboardCodeSet::Scan},
	    {"DIK_F6", 0x40, KeyboardCodeSet::Scan},
	    {"DIK_F7", 0x41, KeyboardCodeSet::Scan},
	    {"DIK_F8", 0x42, KeyboardCodeSet::Scan},
	    {"DIK_F9", 0x43, KeyboardCodeSet::Scan},
	    {"DIK_F10", 0x44, KeyboardCodeSet::Scan},
	    {"DIK_NUMLOCK", 0x45, KeyboardCodeSet::Scan},
	    {"DIK_SCROLL", 0x46, KeyboardCodeSet::Scan},
	    {"DIK_NUMPAD7", 0x47, KeyboardCodeSet::Scan},
	    {"DIK_NUMPAD8", 0x48, KeyboardCodeSet::Scan},
	    {"DIK_NUMPAD9", 0x49, KeyboardCodeSet::Scan},
	    {"DIK_SUBTRACT", 0x4a, KeyboardCodeSet::Scan},
	    {"DIK_NUMPAD4", 0x4b, KeyboardCodeSet::Scan},
	    {"DIK_NUMPAD5", 0x4c, KeyboardCodeSet::Scan},
	    {"DIK_NUMPAD6", 0x4d, KeyboardCodeSet::Scan},
	    {"DIK_ADD", 0x4e, KeyboardCodeSet::Scan},
	    {"DIK_NUMPAD1", 0x4f, KeyboardCodeSet::Scan},
	    {"DIK_NUMPAD2", 0x50, KeyboardCodeSet::Scan},
	    {"DIK_NUMPAD3", 0x51, KeyboardCodeSet::Scan},
	    {"DIK_NUMPAD0", 0x52, KeyboardCodeSet::Scan},
	    {"DIK_DECIMAL", 0x53, KeyboardCodeSet::Scan},
	    {"DIK_F11", 0x57, KeyboardCodeSet::Scan},
	    {"DIK_F12", 0x58, KeyboardCodeSet::Scan},
	    {"DIK_F13", 0x64, KeyboardCodeSet::Scan},
	    {"DIK_F14", 0x65, KeyboardCodeSet::Scan},
	    {"DIK_F15", 0x66, KeyboardCodeSet::Scan},
	    {"DIK_KANA", 0x70, KeyboardCodeSet::Scan},
	    {"DIK_CONVERT", 0x79, KeyboardCodeSet::Scan},
	    {"DIK_NOCONVERT", 0x7b, KeyboardCodeSet::Scan},
	    {"DIK_YEN", 0x7d, KeyboardCodeSet::Scan},
	    {"DIK_NUMPADEQUALS", 0x8d, KeyboardCodeSet::Scan},
	    {"DIK_CIRCUMFLEX", 0x90, KeyboardCodeSet::Scan},
	    {"DIK_AT", 0x91, KeyboardCodeSet::Scan},
	    {"DIK_COLON", 0x92, KeyboardCodeSet::Scan},
	    {"DIK_UNDERLINE", 0x93, KeyboardCodeSet::Scan},
	    {"DIK_KANJI", 0x94, KeyboardCodeSet::Scan},
	    {"DIK_STOP", 0x95, KeyboardCodeSet::Scan},
	    {"DIK_AX", 0x96, KeyboardCodeSet::Scan},
	    {"DIK_UNLABELED", 0x97, KeyboardCodeSet::Scan},
	    {"DIK_NUMPADENTER", 0x9c, KeyboardCodeSet::Scan},
	    {"DIK_RCONTROL", 0x9d, KeyboardCodeSet::Scan},
	    {"DIK_NUMPADCOMMA", 0xb3, KeyboardCodeSet::Scan},
	    {"DIK_DIVIDE", 0xb5, KeyboardCodeSet::Scan},
	    {"DIK_SYSRQ", 0xb7, KeyboardCodeSet::Scan},
	    {"DIK_RMENU", 0xb8, KeyboardCodeSet::Scan},
	    {"DIK_PAUSE", 0xc5, KeyboardCodeSet::Scan},
	    {"DIK_HOME", 0xc7, KeyboardCodeSet::Scan},
	    {"DIK_UP", 0xc8, KeyboardCodeSet::Scan},
	    {"DIK_PRIOR", 0xc9, KeyboardCodeSet::Scan},
	    {"DIK_LEFT", 0xcb, KeyboardCodeSet::Scan},
	    {"DIK_RIGHT", 0xcd, KeyboardCodeSet::Scan},
	    {"DIK_END", 0xcf, KeyboardCodeSet::Scan},
	    {"DIK_DOWN", 0xd0, KeyboardCodeSet::Scan},
	    {"DIK_NEXT", 0xd1, KeyboardCodeSet::Scan},
	    {"DIK_INSERT", 0xd2, KeyboardCodeSet::Scan},
	    {"DIK_DELETE", 0xd3, KeyboardCodeSet::Scan},
	    {"DIK_LWIN", 0xdb, KeyboardCodeSet::Scan},
	    {"DIK_RWIN", 0xdc, KeyboardCodeSet::Scan},
	    {"DIK_APPS", 0xdd, KeyboardCodeSet::Scan},
	    {"DIK_POWER", 0xde, KeyboardCodeSet::Scan},
	    {"DIK_SLEEP", 0xdf, KeyboardCodeSet::Scan},
	    {"VK_LBUTTON", 0x1, KeyboardCodeSet::Virtual},
	    {"VK_RBUTTON", 0x2, KeyboardCodeSet::Virtual},
	    {"VK_CANCEL", 0x3, KeyboardCodeSet::Virtual},
	    {"VK_MBUTTON", 0x4, KeyboardCodeSet::Virtual},
	    {"VK_XBUTTON1", 0x5, KeyboardCodeSet::Virtual},
	    {"VK_XBUTTON2", 0x6, KeyboardCodeSet::Virtual},
	    {"VK_BACK", 0x8, KeyboardCodeSet::Virtual},
	    {"VK_TAB", 0x9, KeyboardCodeSet::Virtual},
	    {"VK_CLEAR", 0xc, KeyboardCodeSet::Virtual},
	    {"VK_RETURN", 0xd, KeyboardCodeSet::Virtual},
	    {"VK_SHIFT", 0x10, KeyboardCodeSet::Virtual},
	    {"VK_CONTROL", 0x11, KeyboardCodeSet::Virtual},
	    {"VK_MENU", 0x12, KeyboardCodeSet::Virtual},
	    {"VK_PAUSE", 0x13, KeyboardCodeSet::Virtual},
	    {"VK_CAPITAL", 0x14, KeyboardCodeSet::Virtual},
	    {"VK_KANA", 0x15, KeyboardCodeSet::Virtual},
	    {"VK_HANGEUL", 0x15, KeyboardCodeSet::Virtual},
	    {"VK_HANGUL", 0x15, KeyboardCodeSet::Virtual},
	    {"VK_JUNJA", 0x17, KeyboardCodeSet::Virtual},
	    {"VK_FINAL", 0x18, KeyboardCodeSet::Virtual},
	    {"VK_HANJA", 0x19, KeyboardCodeSet::Virtual},
	    {"VK_KANJI", 0x19, KeyboardCodeSet::Virtual},
	    {"VK_ESCAPE", 0x1b, KeyboardCodeSet::Virtual},
	    {"VK_CONVERT", 0x1c, KeyboardCodeSet::Virtual},
	    {"VK_NONCONVERT", 0x1d, KeyboardCodeSet::Virtual},
	    {"VK_ACCEPT", 0x1e, KeyboardCodeSet::Virtual},
	    {"VK_MODECHANGE", 0x1f, KeyboardCodeSet::Virtual},
	    {"VK_SPACE", 0x20, KeyboardCodeSet::Virtual},
	    {"VK_PRIOR", 0x21, KeyboardCodeSet::Virtual},
	    {"VK_NEXT", 0x22, KeyboardCodeSet::Virtual},
	    {"VK_END", 0x23, KeyboardCodeSet::Virtual},
	    {"VK_HOME", 0x24, KeyboardCodeSet::Virtual},
	    {"VK_LEFT", 0x25, KeyboardCodeSet::Virtual},
	    {"VK_UP", 0x26, KeyboardCodeSet::Virtual},
	    {"VK_RIGHT", 0x27, KeyboardCodeSet::Virtual},
	    {"VK_DOWN", 0x28, KeyboardCodeSet::Virtual},
	    {"VK_SELECT", 0x29, KeyboardCodeSet::Virtual},
	    {"VK_PRINT", 0x2a, KeyboardCodeSet::Virtual},
	    {"VK_EXECUTE", 0x2b, KeyboardCodeSet::Virtual},
	    {"VK_SNAPSHOT", 0x2c, KeyboardCodeSet::Virtual},
	    {"VK_INSERT", 0x2d, KeyboardCodeSet::Virtual},
	    {"VK_DELETE", 0x2e, KeyboardCodeSet::Virtual},
	    {"VK_HELP", 0x2f, KeyboardCodeSet::Virtual},
	    {"VK_0", 0x30, KeyboardCodeSet::Virtual},
	    {"VK_1", 0x31, KeyboardCodeSet::Virtual},
	    {"VK_2", 0x32, KeyboardCodeSet::Virtual},
	    {"VK_3", 0x33, KeyboardCodeSet::Virtual},
	    {"VK_4", 0x34, KeyboardCodeSet::Virtual},
	    {"VK_5", 0x35, KeyboardCodeSet::Virtual},
	    {"VK_6", 0x36, KeyboardCodeSet::Virtual},
	    {"VK_7", 0x37, KeyboardCodeSet::Virtual},
	    {"VK_8", 0x38, KeyboardCodeSet::Virtual},
	    {"VK_9", 0x39, KeyboardCodeSet::Virtual},
	    {"VK_A", 0x41, KeyboardCodeSet::Virtual},
	    {"VK_B", 0x42, KeyboardCodeSet::Virtual},
	    {"VK_C", 0x43, KeyboardCodeSet::Virtual},
	    {"VK_D", 0x44, KeyboardCodeSet::Virtual},
	    {"VK_E", 0x45, KeyboardCodeSet::Virtual},
	    {"VK_F", 0x46, KeyboardCodeSet::Virtual},
	    {"VK_G", 0x47, KeyboardCodeSet::Virtual},
	    {"VK_H", 0x48, KeyboardCodeSet::Virtual},
	    {"VK_I", 0x49, KeyboardCodeSet::Virtual},
	    {"VK_J", 0x4a, KeyboardCodeSet::Virtual},
	    {"VK_K", 0x4b, KeyboardCodeSet::Virtual},
	    {"VK_L", 0x4c, KeyboardCodeSet::Virtual},
	    {"VK_M", 0x4d, KeyboardCodeSet::Virtual},
	    {"VK_N", 0x4e, KeyboardCodeSet::Virtual},
	    {"VK_O", 0x4f, KeyboardCodeSet::Virtual},
	    {"VK_P", 0x50, KeyboardCodeSet::Virtual},
	    {"VK_Q", 0x51, KeyboardCodeSet::Virtual},
	    {"VK_R", 0x52, KeyboardCodeSet::Virtual},
	    {"VK_S", 0x53, KeyboardCodeSet::Virtual},
	    {"VK_T", 0x54, KeyboardCodeSet::Virtual},
	    {"VK_U", 0x55, KeyboardCodeSet::Virtual},
	    {"VK_V", 0x56, KeyboardCodeSet::Virtual},
	    {"VK_W", 0x57, KeyboardCodeSet::Virtual},
	    {"VK_X", 0x58, KeyboardCodeSet::Virtual},
	    {"VK_Y", 0x59, KeyboardCodeSet::Virtual},
	    {"VK_Z", 0x5a, KeyboardCodeSet::Virtual},
	    {"VK_LWIN", 0x5b, KeyboardCodeSet::Virtual},
	    {"VK_RWIN", 0x5c, KeyboardCodeSet::Virtual},
	    {"VK_APPS", 0x5d, KeyboardCodeSet::Virtual},
	    {"VK_SLEEP", 0x5f, KeyboardCodeSet::Virtual},
	    {"VK_NUMPAD0", 0x60, KeyboardCodeSet::Virtual},
	    {"VK_NUMPAD1", 0x61, KeyboardCodeSet::Virtual},
	    {"VK_NUMPAD2", 0x62, KeyboardCodeSet::Virtual},
	    {"VK_NUMPAD3", 0x63, KeyboardCodeSet::Virtual},
	    {"VK_NUMPAD4", 0x64, KeyboardCodeSet::Virtual},
	    {"VK_NUMPAD5", 0x65, KeyboardCodeSet::Virtual},
	    {"VK_NUMPAD6", 0x66, KeyboardCodeSet::Virtual},
	    {"VK_NUMPAD7", 0x67, KeyboardCodeSet::Virtual},
	    {"VK_NUMPAD8", 0x68, KeyboardCodeSet::Virtual},
	    {"VK_NUMPAD9", 0x69, KeyboardCodeSet::Virtual},
	    {"VK_MULTIPLY", 0x6a, KeyboardCodeSet::Virtual},
	    {"VK_ADD", 0x6b, KeyboardCodeSet::Virtual},
	    {"VK_SEPARATOR", 0x6c, KeyboardCodeSet::Virtual},
	    {"VK_SUBTRACT", 0x6d, KeyboardCodeSet::Virtual},
	    {"VK_DECIMAL", 0x6e, KeyboardCodeSet::Virtual},
	    {"VK_DIVIDE", 0x6f, KeyboardCodeSet::Virtual},
	    {"VK_F1", 0x70, KeyboardCodeSet::Virtual},
	    {"VK_F2", 0x71, KeyboardCodeSet::Virtual},
	    {"VK_F3", 0x72, KeyboardCodeSet::Virtual},
	    {"VK_F4", 0x73, KeyboardCodeSet::Virtual},
	    {"VK_F5", 0x74, KeyboardCodeSet::Virtual},
	    {"VK_F6", 0x75, KeyboardCodeSet::Virtual},
	    {"VK_F7", 0x76, KeyboardCodeSet::Virtual},
	    {"VK_F8", 0x77, KeyboardCodeSet::Virtual},
	    {"VK_F9", 0x78, KeyboardCodeSet::Virtual},
	    {"VK_F10", 0x79, KeyboardCodeSet::Virtual},
	    {"VK_F11", 0x7a, KeyboardCodeSet::Virtual},
	    {"VK_F12", 0x7b, KeyboardCodeSet::Virtual},
	    {"VK_F13", 0x7c, KeyboardCodeSet::Virtual},
	    {"VK_F14", 0x7d, KeyboardCodeSet::Virtual},
	    {"VK_F15", 0x7e, KeyboardCodeSet::Virtual},
	    {"VK_F16", 0x7f, KeyboardCodeSet::Virtual},
	    {"VK_F17", 0x80, KeyboardCodeSet::Virtual},
	    {"VK_F18", 0x81, KeyboardCodeSet::Virtual},
	    {"VK_F19", 0x82, KeyboardCodeSet::Virtual},
	    {"VK_F20", 0x83, KeyboardCodeSet::Virtual},
	    {"VK_F21", 0x84, KeyboardCodeSet::Virtual},
	    {"VK_F22", 0x85, KeyboardCodeSet::Virtual},
	    {"VK_F23", 0x86, KeyboardCodeSet::Virtual},
	    {"VK_F24", 0x87, KeyboardCodeSet::Virtual},
	    {"VK_NUMLOCK", 0x90, KeyboardCodeSet::Virtual},
	    {"VK_SCROLL", 0x91, KeyboardCodeSet::Virtual},
	    {"VK_LSHIFT", 0xa0, KeyboardCodeSet::Virtual},
	    {"VK_RSHIFT", 0xa1, KeyboardCodeSet::Virtual},
	    {"VK_LCONTROL", 0xa2, KeyboardCodeSet::Virtual},
	    {"VK_RCONTROL", 0xa3, KeyboardCodeSet::Virtual},
	    {"VK_LMENU", 0xa4, KeyboardCodeSet::Virtual},
	    {"VK_RMENU", 0xa5, KeyboardCodeSet::Virtual},
	    {"VK_BROWSER_BACK", 0xa6, KeyboardCodeSet::Virtual},
	    {"VK_BROWSER_FORWARD", 0xa7, KeyboardCodeSet::Virtual},
	    {"VK_BROWSER_REFRESH", 0xa8, KeyboardCodeSet::Virtual},
	    {"VK_BROWSER_STOP", 0xa9, KeyboardCodeSet::Virtual},
	    {"VK_BROWSER_SEARCH", 0xaa, KeyboardCodeSet::Virtual},
	    {"VK_BROWSER_FAVORITES", 0xab, KeyboardCodeSet::Virtual},
	    {"VK_BROWSER_HOME", 0xac, KeyboardCodeSet::Virtual},
	    {"VK_VOLUME_MUTE", 0xad, KeyboardCodeSet::Virtual},
	    {"VK_VOLUME_DOWN", 0xae, KeyboardCodeSet::Virtual},
	    {"VK_VOLUME_UP", 0xaf, KeyboardCodeSet::Virtual},
	    {"VK_MEDIA_NEXT_TRACK", 0xb0, KeyboardCodeSet::Virtual},
	    {"VK_MEDIA_PREV_TRACK", 0xb1, KeyboardCodeSet::Virtual},
	    {"VK_MEDIA_STOP", 0xb2, KeyboardCodeSet::Virtual},
	    {"VK_MEDIA_PLAY_PAUSE", 0xb3, KeyboardCodeSet::Virtual},
	    {"VK_LAUNCH_MAIL", 0xb4, KeyboardCodeSet::Virtual},
	    {"VK_LAUNCH_MEDIA_SELECT", 0xb5, KeyboardCodeSet::Virtual},
	    {"VK_LAUNCH_APP1", 0xb6, KeyboardCodeSet::Virtual},
	    {"VK_LAUNCH_APP2", 0xb7, KeyboardCodeSet::Virtual},
	    {"VK_OEM_1", 0xba, KeyboardCodeSet::Virtual},
	    {"VK_OEM_PLUS", 0xbb, KeyboardCodeSet::Virtual},
	    {"VK_OEM_COMMA", 0xbc, KeyboardCodeSet::Virtual},
	    {"VK_OEM_MINUS", 0xbd, KeyboardCodeSet::Virtual},
	    {"VK_OEM_PERIOD", 0xbe, KeyboardCodeSet::Virtual},
	    {"VK_OEM_2", 0xbf, KeyboardCodeSet::Virtual},
	    {"VK_OEM_3", 0xc0, KeyboardCodeSet::Virtual},
	    {"VK_OEM_4", 0xdb, KeyboardCodeSet::Virtual},
	    {"VK_OEM_5", 0xdc, KeyboardCodeSet::Virtual},
	    {"VK_OEM_6", 0xdd, KeyboardCodeSet::Virtual},
	    {"VK_OEM_7", 0xde, KeyboardCodeSet::Virtual},
	    {"VK_OEM_8", 0xdf, KeyboardCodeSet::Virtual},
	    {"VK_OEM_102", 0xe2, KeyboardCodeSet::Virtual},
	    {"VK_ICO_HELP", 0xe3, KeyboardCodeSet::Virtual},
	    {"VK_ICO_00", 0xe4, KeyboardCodeSet::Virtual},
	    {"VK_PROCESSKEY", 0xe5, KeyboardCodeSet::Virtual},
	    {"VK_ICO_CLEAR", 0xe6, KeyboardCodeSet::Virtual},
	    {"VK_PACKET", 0xe7, KeyboardCodeSet::Virtual},
	    {"VK_OEM_RESET", 0xe9, KeyboardCodeSet::Virtual},
	    {"VK_OEM_JUMP", 0xea, KeyboardCodeSet::Virtual},
	    {"VK_OEM_PA1", 0xeb, KeyboardCodeSet::Virtual},
	    {"VK_OEM_PA2", 0xec, KeyboardCodeSet::Virtual},
	    {"VK_OEM_PA3", 0xed, KeyboardCodeSet::Virtual},
	    {"VK_OEM_WSCTRL", 0xee, KeyboardCodeSet::Virtual},
	    {"VK_OEM_CUSEL", 0xef, KeyboardCodeSet::Virtual},
	    {"VK_OEM_ATTN", 0xf0, KeyboardCodeSet::Virtual},
	    {"VK_OEM_FINISH", 0xf1, KeyboardCodeSet::Virtual},
	    {"VK_OEM_COPY", 0xf2, KeyboardCodeSet::Virtual},
	    {"VK_OEM_AUTO", 0xf3, KeyboardCodeSet::Virtual},
	    {"VK_OEM_ENLW", 0xf4, KeyboardCodeSet::Virtual},
	    {"VK_OEM_BACKTAB", 0xf5, KeyboardCodeSet::Virtual},
	    {"VK_ATTN", 0xf6, KeyboardCodeSet::Virtual},
	    {"VK_CRSEL", 0xf7, KeyboardCodeSet::Virtual},
	    {"VK_EXSEL", 0xf8, KeyboardCodeSet::Virtual},
	    {"VK_EREOF", 0xf9, KeyboardCodeSet::Virtual},
	    {"VK_PLAY", 0xfa, KeyboardCodeSet::Virtual},
	    {"VK_ZOOM", 0xfb, KeyboardCodeSet::Virtual},
	    {"VK_NONAME", 0xfc, KeyboardCodeSet::Virtual},
	    {"VK_PA1", 0xfd, KeyboardCodeSet::Virtual},
	    {"VK_OEM_CLEAR", 0xfe, KeyboardCodeSet::Virtual},
	}};

	struct NamedTarget {
		std::string_view Name;
		std::uint32_t LogicalButtons;
		std::uint32_t ControllerButtons;
	};

	static constexpr std::array<NamedTarget, 25> kTargets{{
	    {"a", 0x1, 0x1000},
	    {"b", 0x2, 0x2000},
	    {"select", 0x4, 0x20},
	    {"start", 0x8, 0x10},
	    {"right", 0x10, 0x8},
	    {"left", 0x20, 0x4},
	    {"up", 0x40, 0x1},
	    {"down", 0x80, 0x2},
	    {"r", 0x100, 0x200},
	    {"l", 0x200, 0x100},
	    {"x", 0x400, 0x4000},
	    {"y", 0x800, 0x8000},
	    {"c", 0x4000, 0x0},
	    {"z", 0x8000, 0x0},
	    {"l1", 0x200, 0x100},
	    {"r1", 0x100, 0x200},
	    {"l2", 0x10000, 0x0},
	    {"r2", 0x20000, 0x0},
	    {"l3", 0x40000, 0x40},
	    {"r3", 0x80000, 0x80},
	    {"back", 0x100000, 0x0},
	    {"r_right", 0x200000, 0x0},
	    {"r_left", 0x400000, 0x0},
	    {"r_up", 0x800000, 0x0},
	    {"r_down", 0x1000000, 0x0},
	}};

	static constexpr std::array<KeyConversion, 19> kDefaultScan{{
	    {0xc8, 0x40, 0x1},
	    {0xd0, 0x80, 0x2},
	    {0xcb, 0x20, 0x4},
	    {0xcd, 0x10, 0x8},
	    {0x2c, 0x1, 0x1000},
	    {0x2d, 0x2, 0x2000},
	    {0x1e, 0x400, 0x4000},
	    {0x1f, 0x800, 0x8000},
	    {0x2e, 0x200, 0x100},
	    {0x20, 0x100, 0x200},
	    {0x10, 0x10000, 0x0},
	    {0x11, 0x20000, 0x0},
	    {0x12, 0x40000, 0x40},
	    {0x13, 0x80000, 0x80},
	    {0xf, 0x8, 0x10},
	    {0x6, 0x4, 0x20},
	    {0x39, 0x1, 0x1000},
	    {0x9c, 0x1, 0x1000},
	    {0x1, 0x100000, 0x0},
	}};

	static constexpr std::array<KeyConversion, 3> kDefaultVirtual{{
	    {0xd, 0x1, 0x1000},
	    {0x2, 0x2, 0x2000},
	    {0x11, 0x100, 0x200},
	}};

	static std::string_view NativeName(std::string_view name) {
		return name.substr(0, name.find('\0'));
	}

	static bool EqualFolded(std::string_view value, std::string_view reference, bool upper) {
		value = NativeName(value);
		if (value.size() != reference.size()) {
			return false;
		}

		for (std::size_t index = 0; index < value.size(); ++index) {
			auto character = static_cast<std::uint8_t>(value[index]);
			if (upper && character >= 'a' && character <= 'z') {
				character = static_cast<std::uint8_t>(character - ('a' - 'A'));
			}

			if (!upper && character >= 'A' && character <= 'Z') {
				character = static_cast<std::uint8_t>(character + ('a' - 'A'));
			}

			if (character != static_cast<std::uint8_t>(reference[index])) {
				return false;
			}
		}

		return true;
	}

	static const NamedTarget* FindTarget(std::string_view name) {
		const auto found = std::find_if(kTargets.begin(), kTargets.end(), [&](const auto& target) {
			return EqualFolded(name, target.Name, false);
		});

		return found == kTargets.end() ? nullptr : &*found;
	}

	std::span<const NamedPhysicalKey> PhysicalKeyNames() {
		return kPhysicalNames;
	}

	KeyMappings::KeyMappings(std::size_t maximum_conversions) : m_MaximumConversions(maximum_conversions) {
		if (maximum_conversions < kDefaultScan.size() + kDefaultVirtual.size() || maximum_conversions > 65536) {
			throw std::runtime_error("input key conversion capacity is outside its bounded range");
		}

		m_Scan.reserve(maximum_conversions);
		m_Virtual.reserve(maximum_conversions);
		SetDefault();
	}

	void KeyMappings::Clear() {
		m_Scan.clear();
		m_Virtual.clear();
	}

	void KeyMappings::SetDefault() {
		m_Scan.assign(kDefaultScan.begin(), kDefaultScan.end());
		m_Virtual.assign(kDefaultVirtual.begin(), kDefaultVirtual.end());
	}

	bool KeyMappings::Add(std::string_view source, std::string_view logical_target, std::string_view controller_target) {
		const auto found = std::find_if(kPhysicalNames.begin(), kPhysicalNames.end(), [&](const auto& key) {
			return EqualFolded(source, key.Name, true);
		});
		if (found == kPhysicalNames.end()) {
			return false;
		}

		if (m_Scan.size() + m_Virtual.size() == m_MaximumConversions) {
			throw std::runtime_error("input key conversion capacity is exhausted");
		}

		KeyConversion conversion;
		conversion.Source = found->Code;
		if (const auto* target = FindTarget(logical_target)) {
			conversion.LogicalButtons = target->LogicalButtons;
		}

		if (const auto* target = FindTarget(controller_target)) {
			conversion.ControllerButtons = target->ControllerButtons;
		}

		auto& conversions = found->CodeSet == KeyboardCodeSet::Scan ? m_Scan : m_Virtual;
		conversions.push_back(conversion);

		return true;
	}

	ConvertedKeyboard KeyMappings::Convert(const KeyboardSample& sample) const {
		ConvertedKeyboard result;
		if (!sample.Available) {
			return result;
		}

		result.LogicalButtons = sample.BackPulse ? 0x100000 : 0;
		for (const auto& conversion : m_Scan) {
			const auto down = (sample.ScanKeys[conversion.Source] & 0x80) != 0;
			const auto alt_return = conversion.Source == 0x1c && ((sample.ScanKeys[0x38] | sample.ScanKeys[0xb8]) & 0x80) != 0;
			if (down && !alt_return) {
				result.LogicalButtons |= conversion.LogicalButtons;
				result.ControllerButtons |= conversion.ControllerButtons;
			}
		}

		for (const auto& conversion : m_Virtual) {
			const auto down = (sample.VirtualKeys[conversion.Source] & 0x80) != 0;
			const auto alt_return = conversion.Source == 0xd && (sample.VirtualKeys[0x12] & 0x80) != 0;
			if (down && !alt_return) {
				result.LogicalButtons |= conversion.LogicalButtons;
				result.ControllerButtons |= conversion.ControllerButtons;
			}
		}

		return result;
	}

	std::span<const KeyConversion> KeyMappings::ScanConversions() const {
		return m_Scan;
	}

	std::span<const KeyConversion> KeyMappings::VirtualConversions() const {
		return m_Virtual;
	}

} // namespace sgre::input
