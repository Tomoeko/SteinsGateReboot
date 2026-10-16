#include "sgre/text/encoding.hpp"

#include <array>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

static void RequireFailure(const std::function<void()>& operation, std::string_view context) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(context) != std::string_view::npos, "text encoding error lost its byte context");

		return;
	}

	throw std::runtime_error("invalid text encoding was accepted");
}

static void TestCodeUnitVectors() {
	Require(sgre::text::DecodeTextCodeUnits("").empty(), "empty UTF-8 text produced a code unit");
	Require(sgre::text::DecodeTextCodeUnits("A\n") == std::vector<std::uint16_t>({0x41, 0x0a}), "ASCII text changed");
	Require(sgre::text::DecodeTextCodeUnits(std::string_view("A\0B", 3)) == std::vector<std::uint16_t>({0x41, 0, 0x42}), "explicit-length text lost embedded NUL units");
	Require(sgre::text::DecodeTextCodeUnits("\x7f\xc2\x80\xdf\xbf\xe0\xa0\x80") == std::vector<std::uint16_t>({0x7f, 0x80, 0x7ff, 0x800}), "UTF-8 encoding-width boundaries changed");
	Require(sgre::text::DecodeTextCodeUnits("\xe3\x81\x82\xef\xbf\xbf") == std::vector<std::uint16_t>({0x3042, 0xffff}), "BMP units were normalized or truncated");
	Require(sgre::text::DecodeTextCodeUnits("\xf0\x90\x80\x80\xf4\x8f\xbf\xbf") == std::vector<std::uint16_t>({0xd800, 0xdc00, 0xdbff, 0xdfff}), "supplementary scalar boundaries changed");
	// Original 0x1404c7840 emits separate surrogate units for the final supplementary scalar.
	Require(sgre::text::DecodeTextCodeUnits("A\xe3\x81\x82\xf0\x9f\x99\x82") == std::vector<std::uint16_t>({0x41, 0x3042, 0xd83d, 0xde42}), "original mixed-string code units changed");
}

static void TestInvalidEncoding() {
	const std::array<std::string_view, 9> invalid = {"\x80", "\xc0\xaf", "\xe0\x80\x80", "\xed\xa0\x80", "\xf0\x80\x80\x80", "\xf4\x90\x80\x80", "\xf8\x88\x80\x80\x80", "\xfc\x84\x80\x80\x80\x80", "\xff"};
	for (const auto input : invalid) {
		RequireFailure([&] {
			static_cast<void>(sgre::text::DecodeTextCodeUnits(input));
		},
		    "text UTF-8 byte 0");
	}

	RequireFailure([] {
		static_cast<void>(sgre::text::DecodeTextCodeUnits("A\xe3\x81"));
	},
	    "byte 1: truncated");
	RequireFailure([] {
		static_cast<void>(sgre::text::DecodeTextCodeUnits("\xe3\x41\x82"));
	},
	    "byte 1: invalid continuation");
	RequireFailure([] {
		static_cast<void>(sgre::text::DecodeTextCodeUnits(std::string(1024 * 1024 + 1, 'A')));
	},
	    "text byte limit");
	const auto maximum = sgre::text::DecodeTextCodeUnits(std::string(1024 * 1024, 'A'));
	Require(maximum.size() == 1024 * 1024 && maximum.front() == 0x41 && maximum.back() == 0x41, "valid text byte limit was rejected");
}

int main() {
	try {
		TestCodeUnitVectors();
		TestInvalidEncoding();
		std::cout << "native text code-unit conversion and bounded UTF-8 validation passed\n";
	} catch (const std::exception& error) {
		std::cerr << "text encoding tests: " << error.what() << '\n';

		return 1;
	}

	return 0;
}
