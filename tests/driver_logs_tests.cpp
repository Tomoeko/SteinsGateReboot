#include "sgre/script/driver_logs.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

template <class Operation>
static void RequireError(Operation operation, std::string_view expected) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(expected) != std::string_view::npos, error.what());

		return;
	}

	throw std::runtime_error("expected driver logger boundary rejection");
}

static void TestNativeInitializationAndBoundaries() {
	sgre::script::ScriptDriverLogger log;
	Require(log.Cursor() == 0, "native logger cursor initializes to zero");
	for (std::size_t index = 0; index != log.kSlots; ++index) {
		const auto buffer = log.Buffer(index);
		Require(buffer.size() == 256 && std::all_of(buffer.begin(), buffer.end(), [](char value) {
			return value == 0;
		}),
		    "all 100 native buffers initialize with 256 zero bytes");
	}
	RequireError([&] {
		static_cast<void>(log.Buffer(100));
	},
	    "100-buffer history");
	log.Append({});
	log.Append(std::string_view("ab\0tail", 7));
	Require(log.Cursor() == 2 && log.MessageBytes(0).empty() && log.MessageBytes(1) == "ab", "empty output consumes one native slot and embedded NUL ends native strncpy input");
	const auto buffer = log.Buffer(1);
	Require(std::all_of(buffer.begin() + 2, buffer.end(), [](char value) {
		return value == 0;
	}),
	    "native short-string copy pads the complete bounded tail with zero");
}

static void TestNativeTruncationPaddingAndUtf8Bytes() {
	sgre::script::ScriptDriverLogger log;
	const std::string long_message(400, 'x');
	log.Append(long_message);
	Require(log.MessageBytes(0) == std::string_view(long_message).substr(0, 255) && log.Buffer(0)[255] == 0, "native copy truncates at 255 bytes while untouched final byte remains constructor zero");
	for (std::size_t index = 1; index != log.kSlots; ++index) {
		log.Append("other");
	}
	log.Append("short");
	Require(log.Cursor() == 1 && log.MessageBytes(0) == "short" && std::all_of(log.Buffer(0).begin() + 5, log.Buffer(0).end(), [](char value) {
		return value == 0;
	}),
	    "native wrap overwrite replaces stale long contents with strncpy padding");
	std::string utf8(254, 'a');
	utf8 += "\xc3\xa9";
	log.Append(utf8);
	Require(log.MessageBytes(1).size() == 255 && static_cast<unsigned char>(log.Buffer(1)[254]) == 0xc3 && log.Buffer(1)[255] == 0, "native byte truncation preserves a split UTF-8 prefix rather than inventing text normalization");
	for (std::size_t index = 2; index != log.kSlots; ++index) {
		log.Append("tail");
	}
	const auto borrowed = log.MessageBytes(0);
	log.Append(borrowed);
	Require(log.MessageBytes(0) == "short", "authored aliased input preserves the native formatter's separate-scratch value before replacing storage");
}

static void TestNativePhysicalRingOrder() {
	sgre::script::ScriptDriverLogger log;
	for (std::size_t event = 0; event != 205; ++event) {
		log.Append(std::to_string(event));
	}
	Require(log.Cursor() == 5, "205 appends preserve native modulo100 cursor");
	for (std::size_t index = 0; index != log.kSlots; ++index) {
		const auto last_event = index < 5 ? index + 200 : index + 100;
		Require(log.MessageBytes(index) == std::to_string(last_event), "physical native slots contain their last writer; no invented chronological list or count is stored");
	}
}

static void TestRequiredReportingAndNativeLookupOrder() {
	RequireError([] {
		sgre::script::ScriptDriverLogs logs({});
	},
	    "actual error-reporting service");
	std::vector<std::string> reported;
	sgre::script::ScriptDriverLogs* active = nullptr;
	sgre::script::ScriptDriverLogs logs([&](std::string_view text) {
		reported.emplace_back(text);
		if (!active->IsInitialized()) {
			active->Initialize();
		} else {
			Require(active->Error().Cursor() == 1, "real error-report service runs before current error history advances");
		}
	});
	active = &logs;
	logs.EmitFormatted("early ordinary output", false);
	Require(!logs.IsInitialized(), "native print history is discarded before deferred logger setup");
	logs.EmitFormatted("initialize during actual report", true);
	Require(logs.IsInitialized() && logs.Error().MessageBytes(0) == reported[0], "error output consults current logger after the actual reporting boundary returns");
	logs.EmitFormatted(std::string_view("error\0hidden", 12), true);
	Require(reported == std::vector<std::string>{"initialize during actual report", "error"} && logs.Error().Cursor() == 2 && logs.Print().Cursor() == 0, "native print and error histories remain independent and reports consume full visible text");
	RequireError([&] {
		logs.Initialize();
	},
	    "one native setup");
	logs.Close();
	RequireError([&] {
		logs.EmitFormatted("closed", false);
	},
	    "routing has retired");
}

static void TestGenuineFormatterRoutingAndNoScriptRoots() {
	std::vector<std::string> reported;
	sgre::script::ScriptDriverLogs logs([&](std::string_view text) {
		reported.emplace_back(text);
	});
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	sgre::script::SquirrelVm vm(limits);
	logs.Initialize();
	sgre::script::InstallNativePrinting(vm, logs.OutputCallback());
	const auto root = vm.RootTable();
	const auto printf = vm.GetSlot(root, std::string("printf"));
	static_cast<void>(vm.Invoke(printf, std::array<sgre::script::SquirrelValue, 4>{root, std::string("%03d|%s"), std::int32_t{42}, std::string("tail")}));
	vm.ResetExecution();
	Require(logs.Print().MessageBytes(0) == "042|tail", "actual existing first-party formatter output reaches native driver storage");
	const auto errprint = vm.GetSlot(root, std::string("errprint"));
	const std::string message(300, 'e');
	static_cast<void>(vm.Invoke(errprint, std::array<sgre::script::SquirrelValue, 2>{root, message}));
	vm.ResetExecution();
	Require(reported == std::vector<std::string>{message} && logs.Error().MessageBytes(0).size() == 255, "real error report receives the full formatted string while only native history truncates");
	sgre::script::SquirrelValue instance = vm.CreateInstance(vm.CreateClass());
	const auto weak = vm.CreateWeakReference(instance);
	const auto print = vm.GetSlot(root, std::string("print"));
	static_cast<void>(vm.Invoke(print, std::array<sgre::script::SquirrelValue, 2>{root, instance}));
	vm.ResetExecution();
	instance = {};
	Require(std::holds_alternative<std::monostate>(vm.ResolveWeakReference(weak)), "driver log bytes and output callback retain no Squirrel object instance");
	vm.Close();
	vm.RethrowReleaseFailure();
	logs.Close();
}

int main() {
	try {
		TestNativeInitializationAndBoundaries();
		TestNativeTruncationPaddingAndUtf8Bytes();
		TestNativePhysicalRingOrder();
		TestRequiredReportingAndNativeLookupOrder();
		TestGenuineFormatterRoutingAndNoScriptRoots();
		std::cout << "5 actual SQDriver logger groups passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
