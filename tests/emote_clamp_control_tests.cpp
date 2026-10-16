#include "sgre/scene/emote_clamp_control.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

static void RequireNear(float actual, float expected, std::string_view message) {
	if (std::abs(actual - expected) > 0.000002f) {
		throw std::runtime_error(std::string(message));
	}
}

static void TestRadialDomain() {
	sgre::scene::EmoteClampTransform transform;
	transform.Kind = sgre::scene::EmoteClampKind::Radial;
	transform.Minimum = -1;
	transform.Maximum = 1;
	const auto corner = sgre::scene::ApplyEmoteClamp(transform, 1, 1);
	RequireNear(corner[0], std::sqrt(0.5f), "native radial clamp corner X differs");
	RequireNear(corner[1], std::sqrt(0.5f), "native radial clamp corner Y differs");
	const auto interior = sgre::scene::ApplyEmoteClamp(transform, 0.25f, -0.5f);
	RequireNear(interior[0], 0.25f, "interior radial X must remain unchanged");
	RequireNear(interior[1], -0.5f, "interior radial Y must remain unchanged");
	const auto axis = sgre::scene::ApplyEmoteClamp(transform, 2, 0);
	RequireNear(axis[0], 2, "native zero-axis branch must not invent radial clipping");
	RequireNear(axis[1], 0, "native zero-axis branch changed zero");
}

static void TestSmoothDomain() {
	sgre::scene::EmoteClampTransform transform;
	transform.Minimum = -1;
	transform.Maximum = 1;
	const auto corner = sgre::scene::ApplyEmoteClamp(transform, 1, 1);
	RequireNear(corner[0], std::sqrt(0.5f), "native smooth corner mapping differs");
	RequireNear(corner[1], std::sqrt(0.5f), "native smooth corner symmetry differs");
	const auto diagonal = sgre::scene::ApplyEmoteClamp(transform, 0.5f, 0.5f);
	RequireNear(diagonal[0], 0.5f, "native smooth diagonal must follow sine-scaled radius, not linear disk normalization");
	RequireNear(diagonal[1], 0.5f, "native smooth diagonal Y differs");
	transform.Minimum = 0;
	transform.Maximum = 100;
	const auto translated = sgre::scene::ApplyEmoteClamp(transform, 100, 100);
	RequireNear(translated[0], (std::sqrt(0.5f) + 1.0f) * 50.0f, "authored variable range conversion differs");
	RequireNear(translated[1], translated[0], "translated smooth clamp symmetry differs");
}

int main() {
	try {
		TestRadialDomain();
		TestSmoothDomain();
		std::cout << "Emote native type0 smooth/type1 radial pair clamp, exact axis bypass and actual variable-range conversion passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
