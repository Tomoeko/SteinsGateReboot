#include "sgre/render/presentation_rectangle.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

template <typename Work>
static void RequireError(Work work) {
	try {
		work();
	} catch (const std::runtime_error&) {
		return;
	}
	throw std::runtime_error("expected invalid presentation geometry rejection");
}

static void TestFitAndInput() {
	using sgre::render::FitPresentationRectangle;
	using sgre::render::MapPresentationPoint;
	for (const auto width : {1u, 720u, 1280u, 1921u, 3840u, 32768u, std::numeric_limits<std::uint32_t>::max()}) {
		for (const auto height : {1u, 720u, 1081u, 2160u, 32768u, std::numeric_limits<std::uint32_t>::max()}) {
			const auto rectangle = FitPresentationRectangle(1280, 720, width, height);
			Require(rectangle.Left >= 0 && rectangle.Top >= 0 && rectangle.Width <= width && rectangle.Height <= height, "aspect-fit rectangle escaped its destination");
			Require(std::abs(rectangle.Width / rectangle.Height - 1280.0 / 720.0) < 1e-12, "presentation changed the logical aspect ratio");
			const auto center_x = rectangle.Left + rectangle.Width * 0.5;
			const auto center_y = rectangle.Top + rectangle.Height * 0.5;
			const auto center = MapPresentationPoint(rectangle, 1280, 720, center_x, center_y);
			// Extreme aspect ratios put a narrow viewport at a large origin. Its representable input
			// coordinate has an ULP that scales into logical pixels; this is not a native output tolerance.
			const auto x_ulp = std::nextafter(center_x, std::numeric_limits<double>::infinity()) - center_x;
			const auto y_ulp = std::nextafter(center_y, std::numeric_limits<double>::infinity()) - center_y;
			const bool ordinary_window_extent = width <= 32768 && height <= 32768;
			const auto x_bound = ordinary_window_extent ? 1e-9 : std::max(1e-9, x_ulp / rectangle.Width * 1280);
			const auto y_bound = ordinary_window_extent ? 1e-9 : std::max(1e-9, y_ulp / rectangle.Height * 720);
			Require(center.Inside && std::abs(center.X - 640) < x_bound && std::abs(center.Y - 360) < y_bound, "inverse presentation mapping changed the scene center beyond input representability");
			const auto top_left = MapPresentationPoint(rectangle, 1280, 720, rectangle.Left, rectangle.Top);
			Require(top_left.Inside && top_left.X == 0 && top_left.Y == 0, "top-left pixel boundary is not included");
			const auto right = MapPresentationPoint(rectangle, 1280, 720, rectangle.Left + rectangle.Width, rectangle.Top + rectangle.Height * 0.5);
			Require(!right.Inside, "right edge must remain outside native client input");
			const auto bottom = MapPresentationPoint(rectangle, 1280, 720, rectangle.Left + rectangle.Width * 0.5, rectangle.Top + rectangle.Height);
			Require(!bottom.Inside, "bottom edge must remain outside native client input");
		}
	}
	const auto bars = FitPresentationRectangle(1280, 720, 1000, 1000);
	Require(bars.Left == 0 && bars.Top == 218.75 && bars.Width == 1000 && bars.Height == 562.5, "square window did not produce centered letterboxing");
	Require(!MapPresentationPoint(bars, 1280, 720, 500, 100).Inside && !MapPresentationPoint(bars, 1280, 720, 500, 900).Inside, "letterbox bars reached game input");
	const auto pillar = FitPresentationRectangle(1280, 720, 2560, 720);
	Require(pillar.Left == 640 && pillar.Top == 0 && pillar.Width == 1280 && pillar.Height == 720, "wide window did not produce centered pillarboxing");
	Require(!MapPresentationPoint(pillar, 1280, 720, 100, 360).Inside, "pillarbox bar reached game input");
	RequireError([] {
		static_cast<void>(FitPresentationRectangle(0, 720, 1280, 720));
	});
	RequireError([] {
		static_cast<void>(FitPresentationRectangle(1280, 720, 0, 720));
	});
	RequireError([&] {
		static_cast<void>(MapPresentationPoint(bars, 1280, 720, std::numeric_limits<double>::quiet_NaN(), 0));
	});
	RequireError([] {
		static_cast<void>(MapPresentationPoint({0, 0, std::numeric_limits<double>::infinity(), 1}, 1, 1, 0, 0));
	});
}

int main() {
	try {
		TestFitAndInput();
		std::cout << "presentation aspect-fit and inverse input PASS\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
