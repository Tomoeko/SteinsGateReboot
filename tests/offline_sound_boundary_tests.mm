#include "../src/platform/macos/offline_sound_boundary.hpp"

#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <string>

static void RequireBoundary(bool condition, const char* message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

void TestOfflineSoundSystemBoundaryFailures() {
	using Boundary = sgre::audio::OfflineSoundSystemBoundary;
	using Operation = sgre::audio::OfflineSoundSystemOperation;
	using Kind = sgre::audio::OfflineSoundSystemFailureKind;
	Boundary objective_c;
	std::size_t calls = 0;
	for (std::size_t attempt = 0; attempt < 2; ++attempt) {
		try {
			objective_c.Invoke(attempt == 0 ? Operation::Render : Operation::Format, [&] {
				++calls;
				@throw [NSException exceptionWithName:@"OfflineBoundaryFixture" reason:@"actual Objective-C fixture exception" userInfo:nil];
			});
			throw std::runtime_error("actual Objective-C exception was accepted as successful system output");
		} catch (const std::runtime_error& error) {
			RequireBoundary(std::string(error.what()) == "manual offline render exception: actual Objective-C fixture exception", "actual Objective-C exception did not translate or preserve the first operation detail");
		}
	}
	RequireBoundary(calls == 1 && objective_c.Operation() == Operation::Render && objective_c.Kind() == Kind::ObjectiveC, "first foreign failure retried its operation or lost its actual cause");
	Boundary cpp;
	for (std::size_t attempt = 0; attempt < 2; ++attempt) {
		try {
			cpp.Invoke(attempt == 0 ? Operation::Timeline : Operation::EngineState, [&] {
				++calls;
				throw std::runtime_error("actual C++ fixture failure");
			});
			throw std::runtime_error("actual C++ failure was accepted as successful system output");
		} catch (const std::runtime_error& error) {
			RequireBoundary(std::string(error.what()) == "actual C++ fixture failure", "actual C++ failure did not retain the first error");
		}
	}
	RequireBoundary(calls == 2 && cpp.Operation() == Operation::Timeline && cpp.Kind() == Kind::Cpp, "first C++ failure retried its operation or lost its actual cause");
	Boundary nested;
	try {
		nested.Invoke(Operation::GraphCreation, [&] {
			nested.Invoke(Operation::EngineState, [] {
				@throw [NSException exceptionWithName:@"NestedOfflineBoundaryFixture" reason:nil userInfo:nil];
			});
		});
		throw std::runtime_error("nested foreign operation did not reject its actual exception");
	} catch (const std::runtime_error& error) {
		RequireBoundary(std::string(error.what()) == "manual offline engine state exception: unknown system exception", "nested failure lost its first operation or nil-reason diagnostic");
	}
	RequireBoundary(nested.Operation() == Operation::EngineState && nested.Kind() == Kind::ObjectiveC, "containing graph operation overwrote its actual inner first failure");
	Boundary swallowed;
	try {
		swallowed.Invoke(Operation::GraphCreation, [&] {
			try {
				swallowed.Invoke(Operation::Render, [] {
					throw std::runtime_error("first swallowed C++ failure");
				});
			} catch (const std::runtime_error&) {
				// Exercise the containing operation continuing after its inner first cause was retained.
			}
			@
			throw [NSException exceptionWithName:@"LaterOfflineBoundaryFixture" reason:@"later foreign failure" userInfo:nil];
		});
		throw std::runtime_error("later foreign failure escaped operation translation");
	} catch (const std::runtime_error& error) {
		RequireBoundary(std::string(error.what()) == "manual offline graph creation exception: later foreign failure", "later real Foundation exception did not translate at its containing operation");
	}
	RequireBoundary(swallowed.Operation() == Operation::Render && swallowed.Kind() == Kind::Cpp, "later foreign failure changed the kind or operation of the first retained C++ failure");
	try {
		swallowed.RequireOperational();
		throw std::runtime_error("retained first failure was accepted as successful state");
	} catch (const std::runtime_error& error) {
		RequireBoundary(std::string(error.what()) == "first swallowed C++ failure", "later foreign failure replaced the first retained C++ exception");
	}
}

#if defined(SGRE_OFFLINE_SOUND_BOUNDARY_STANDALONE)
int main() {
	try {
		TestOfflineSoundSystemBoundaryFailures();
		std::cout << "Actual Foundation and C++ first-failure boundary checks passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
#endif
