#include "sgre/render/sprite_frame_encoder.hpp"

#include "sprite_presentation_bridge.hpp"

#include <cstdint>
#include <stdexcept>
#include <string>

static void Require(bool condition, const char* message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

template <typename Operation>
static void RequireLiveRejection(Operation operation) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string(error.what()).find("quiescent successful live encoding") != std::string::npos, error.what());

		return;
	}

	throw std::runtime_error("actual presentation source accepted uncommitted live encoding");
}

void TestActualPresentationFence(sgre::render::SpritePassRenderer& renderer, sgre::render::SpriteFrameEncoder& encoder, std::uint64_t accepted_submission) {
	const auto source = sgre::render::SpritePassPresentationBridge::CreateSource(renderer);
	Require(source.CurrentSubmission() == accepted_submission, "actual presentation source did not derive the accepted live submission");
	const auto frame = source.RequireLatest(accepted_submission);
	Require(frame.Target != nil && frame.Producer != nil, "accepted presentation source has no real GPU owners");
	encoder.Begin({}, sgre::render::SpritePassLoadAction::Preserve);
	RequireLiveRejection([&] {
		static_cast<void>(source.CurrentSubmission());
	});
	RequireLiveRejection([&] {
		static_cast<void>(source.RequireLatest(accepted_submission));
	});
	encoder.Abort();
	Require(source.CurrentSubmission() == accepted_submission && source.RequireLatest(accepted_submission).Target == frame.Target, "abandoned encoder changed the accepted presentation source");
}
