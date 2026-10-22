#include "sgre/debug/game_test_trace.hpp"

#include <algorithm>
#include <iostream>
#include <sstream>
#include <stdexcept>

static void Require(bool condition, std::string_view reason) {
	if (!condition) {
		throw std::runtime_error(std::string(reason));
	}
}

int main() {
	try {
		std::ostringstream output;
		sgre::debug::GameTestTrace trace(output);
		trace.Begin("identity", "parent");
		const sgre::script::OriginalCheckpointLocation location{"fixture.ks", "*scene", 20, 4, 7};
		trace.Snapshot(location, "digest");
		sgre::script::OriginalSceneSeekObservation seek;
		seek.Phase = sgre::script::OriginalSceneSeekPhase::Arrived;
		seek.Storage = location.Storage;
		seek.SourceLine = location.SourceLine;
		seek.Point = location.Point;
		seek.Reason = "fixture\nreason";
		trace.Seek(seek);
		trace.ScriptOutput(std::string_view("quote\"slash\\\0\n", 14), true);
		trace.Frame(3, 4);
		trace.Boundary("observation_stopped", "explicit stop");
		const auto text = output.str();
		Require(std::count(text.begin(), text.end(), '\n') == 6, "embedded control bytes split a JSON event");
		Require(text.find("\"sequence\":6,\"event\":\"boundary\"") != std::string::npos && text.find("\"record\":7") != std::string::npos && text.find("\"headless\":true,\"hardware_audio\":false,\"width\":1280,\"height\":720") != std::string::npos, "trace lost its sequence, source cursor or observation policy");
		Require(text.find("quote\\\"slash\\\\\\u0000\\u000a") != std::string::npos && text.find("fixture\\u000areason") != std::string::npos, "test JSON escaping changed embedded NUL/newline/backslash/quote behavior");
		std::ostringstream failed;
		failed.setstate(std::ios::badbit);
		sgre::debug::GameTestTrace broken(failed);
		bool rejected = false;
		try {
			broken.Frame(0, 0);
		} catch (const std::runtime_error&) {
			rejected = true;
		}
		Require(rejected, "failed evidence output silently passed");
		std::cout << "bounded JSON receipts, exact coordinates, hidden/silent policy and failed-output checks passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
