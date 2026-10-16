#include "sgre/app/display_path.hpp"

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

static void Require(bool condition, const char* reason) {
	if (!condition) {
		throw std::runtime_error(reason);
	}
}

int main() {
	try {
		const auto root = std::filesystem::current_path().root_path();
		const auto home = root / "users" / "fixture";
		const auto game = home / "Game's files" / "installation";
		Require(sgre::app::DisplayUserPath(game, home) == "~/Game's files/installation", "display path must omit the user home");
		Require(sgre::app::DisplayUserPath(home, home) == "~", "exact home must abbreviate without an extra component");
		Require(sgre::app::QuotePosixUserPathArgument(home / "Documents" / "game", home) == "~/Documents/game", "ordinary home paths must remain readable without redundant quotes");
		Require(sgre::app::QuotePosixUserPathArgument(game, home) == R"(~/'Game'\''s files/installation')", "shell home expansion and apostrophe quoting must preserve one argument");
		Require(sgre::app::QuotePosixUserPathArgument("~/literal", home) == "'~/literal'", "a literal relative tilde must not gain home expansion");
		const auto other = root / "users" / "fixture-extra" / "game";
		Require(sgre::app::DisplayUserPath(other, home).find("~") == std::string::npos, "a common username prefix must not match a different home");
		Require(sgre::app::DisplayUserPath(game, {}).find("~") == std::string::npos && sgre::app::DisplayUserPath(game, root).find("~") == std::string::npos, "missing or root home must not abbreviate arbitrary absolute paths");
		const auto encoded = home.generic_u8string();
		const std::string home_text(encoded.begin(), encoded.end());
		Require(sgre::app::RedactUserHome("cannot read " + home_text + "/Files/config.json", home) == "cannot read ~/Files/config.json", "inspector failure details must redact the home directory");
		std::cout << "display paths and private shell commands passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
