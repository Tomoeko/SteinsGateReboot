#include "sgre/core/locale.hpp"

#include "sgre/script/system_bindings.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

static void TestOriginalLocaleCatalogAndPacking() {
	const auto english = sgre::core::ResolveOriginalWindowsLocale(0x409);
	const auto japanese = sgre::core::ResolveOriginalWindowsLocale(0x411);
	const auto chinese = sgre::core::ResolveOriginalWindowsLocale(0x804);
	const auto unknown = sgre::core::ResolveOriginalWindowsLocale(0xffffffff);
	Require(english.Language == "en" && english.Country == "US" && japanese.Language == "ja" && japanese.Country == "JP" && chinese.Language == "zh" && chinese.Country == "CN", "original locale identifier lookup must preserve language/country codes");
	Require(unknown.Language == "ja" && unknown.Country == "JP", "unmatched native Windows locale must retain the boot ja/JP default");
	const auto host = sgre::core::ResolveOriginalHostLocale("en", "US");
	const auto unknown_host = sgre::core::ResolveOriginalHostLocale("xx", "ZZ");
	const auto case_mismatch = sgre::core::ResolveOriginalHostLocale("EN", "us");
	Require(host.Language == english.Language && host.Country == english.Country && unknown_host.Language == "ja" && case_mismatch.Country == "JP", "host locale matching must be exact and use the original fallback without case normalization");
	Require(sgre::core::PackOriginalLocalePrefix("en") == 0x656e && sgre::core::PackOriginalLocalePrefix("JP") == 0x4a50 && sgre::core::PackOriginalLocalePrefix("kok") == 0x6b6f, "native locale integer fields must pack the first two unsigned bytes in display order");
	Require(sgre::core::PackOriginalLocalePrefix("") == 0 && sgre::core::PackOriginalLocalePrefix("a") == 0 && sgre::core::PackOriginalLocalePrefix(std::string("\xff\x80", 2)) == 0xff80, "short or high-bit locale prefix conversion must avoid signed-character assumptions");
}

static void TestNativeLocaleSystemMethods() {
	sgre::script::SquirrelVm vm;
	sgre::core::FrameClock clock;
	sgre::script::ScriptSystemBindings system(vm, clock);
	const auto class_value = system.SystemClass();
	const std::array<sgre::script::SquirrelValue, 1> receiver{class_value};
	const auto call = [&](std::string_view name) {
		return vm.Invoke(vm.GetSlot(class_value, std::string(name)), receiver).Value;
	};
	bool rejected = false;
	try {
		static_cast<void>(call("getLanguageString"));
	} catch (const std::runtime_error& error) {
		rejected = std::string_view(error.what()).find("has not been initialized") != std::string_view::npos;
	}

	Require(rejected, "native language service must diagnose missing bootstrap initialization");
	system.ConfigureLocale(sgre::core::ResolveOriginalWindowsLocale(0x409));
	Require(sgre::script::StringText(call("getLanguageString")) == "en" && sgre::script::StringText(call("getCountryString")) == "US", "native System methods must return immutable original string codes");
	Require(std::get<std::int32_t>(call("getLanguageInt")) == 0x656e && std::get<std::int32_t>(call("getCountryInt")) == 0x5553, "native System locale integer methods must return original packed code words");
	std::string language = "ja";
	std::string country = "JP";
	system.ConfigureLocale({language, country});
	language.clear();
	country.clear();
	Require(sgre::script::StringText(call("getLanguageString")) == "ja" && sgre::script::StringText(call("getCountryString")) == "JP", "configured locale must borrow stable catalog storage rather than caller temporaries");
	vm.Close();
	vm.RethrowReleaseFailure();
}

int main() {
	try {
		TestOriginalLocaleCatalogAndPacking();
		TestNativeLocaleSystemMethods();
		std::cout << "Original locale catalog and native System locale fixtures passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
