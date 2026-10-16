#include "sgre/resource/cff_geometry.hpp"

#include <cmath>
#include <functional>
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

static void RequireFailure(const std::function<void()>& operation, std::string_view context) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(context) != std::string_view::npos, "CFF geometry failure lost its context");

		return;
	}

	throw std::runtime_error("invalid CFF geometry was accepted");
}

static sgre::resource::CffOutlineCommand Command(sgre::resource::CffOutlineCommandKind kind, double x = 0, double y = 0) {
	sgre::resource::CffOutlineCommand command;
	command.Kind = kind;
	command.Points[0].X = x;
	command.Points[0].Y = y;

	return command;
}

static void TestNativeContourNormalization() {
	using Kind = sgre::resource::CffOutlineCommandKind;
	sgre::resource::CffOutline outline;
	outline.Commands = {Command(Kind::MoveTo), Command(Kind::LineTo, 100, 0), Command(Kind::LineTo, 100, 100), Command(Kind::LineTo), Command(Kind::ClosePath)};
	const auto triangle = sgre::resource::PrepareCffDesignOutline(outline);
	Require(triangle.Points.size() == 3 && triangle.ContourEnds == std::vector<std::uint16_t>{2}, "native closing endpoint was duplicated");
	Require(triangle.Points[0].X == 0 && triangle.Points[0].Y == 0 && triangle.Points[1].X == 100 && triangle.Points[1].Y == 0 && triangle.Points[2].X == 100 && triangle.Points[2].Y == 100, "native contour point order changed");

	outline.Commands = {Command(Kind::MoveTo, -3, 4), Command(Kind::LineTo, -3, 4), Command(Kind::ClosePath)};
	const auto degenerate = sgre::resource::PrepareCffDesignOutline(outline);
	Require(degenerate.Points.empty() && degenerate.ContourEnds.empty(), "native one-point contour was retained");
	outline.Commands = {Command(Kind::MoveTo), Command(Kind::ClosePath)};
	Require(sgre::resource::PrepareCffDesignOutline(outline).Points.empty(), "a move-only contour generated point storage");
	Require(sgre::resource::PrepareCffDesignOutline({}).Points.empty(), "empty CFF glyph generated point storage");
}

static void TestCubicTagsAndMultipleContours() {
	using Kind = sgre::resource::CffOutlineCommandKind;
	auto curve = Command(Kind::CubicTo, -10, 20);
	curve.Points[1].X = 30;
	curve.Points[1].Y = -40;
	curve.Points[2].X = 1;
	curve.Points[2].Y = 2;
	sgre::resource::CffOutline outline;
	outline.Commands = {Command(Kind::MoveTo, 1, 2), curve, Command(Kind::ClosePath), Command(Kind::MoveTo, 9, 10), Command(Kind::LineTo, 11, 12), Command(Kind::ClosePath)};
	const auto prepared = sgre::resource::PrepareCffDesignOutline(outline);
	Require(prepared.Points.size() == 5 && prepared.ContourEnds == std::vector<std::uint16_t>{2, 4}, "native cubic endpoint or contour order differs");
	Require(prepared.Points[0].Kind == sgre::resource::CffPointKind::OnCurve && prepared.Points[1].Kind == sgre::resource::CffPointKind::CubicControl && prepared.Points[2].Kind == sgre::resource::CffPointKind::CubicControl && prepared.Points[3].Kind == sgre::resource::CffPointKind::OnCurve, "native cubic point tags differ");
	Require(prepared.Points[1].X == -10 && prepared.Points[1].Y == 20 && prepared.Points[2].X == 30 && prepared.Points[2].Y == -40, "cubic controls were projected or reordered");

	outline.Commands = {Command(Kind::MoveTo), Command(Kind::LineTo), Command(Kind::LineTo), Command(Kind::ClosePath)};
	Require(sgre::resource::PrepareCffDesignOutline(outline).Points.size() == 2, "native closure removed more than one repeated endpoint");
}

static void TestUnsupportedAndMalformedGeometry() {
	using Kind = sgre::resource::CffOutlineCommandKind;
	for (const auto value : {0.5, std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(), 2097152.0, -2097153.0}) {
		sgre::resource::CffOutline outline;
		outline.Commands = {Command(Kind::MoveTo, value, 0), Command(Kind::ClosePath)};
		RequireFailure([&] {
			static_cast<void>(sgre::resource::PrepareCffDesignOutline(outline));
		},
		    value == 0.5 ? "fractional" : std::isfinite(value) ? "coordinate range"
		                                                       : "non-finite");
	}
	for (const auto& commands : std::vector<std::vector<sgre::resource::CffOutlineCommand>>{{Command(Kind::LineTo)}, {Command(Kind::ClosePath)}, {Command(Kind::MoveTo)}, {Command(Kind::MoveTo), Command(Kind::MoveTo)}, {Command(static_cast<Kind>(100))}}) {
		sgre::resource::CffOutline outline;
		outline.Commands = commands;
		RequireFailure([&] {
			static_cast<void>(sgre::resource::PrepareCffDesignOutline(outline));
		},
		    "CFF geometry");
	}

	sgre::resource::CffOutline excessive;
	excessive.Commands.resize(65537);
	RequireFailure([&] {
		static_cast<void>(sgre::resource::PrepareCffDesignOutline(excessive));
	},
	    "work limit");
	excessive.Commands.assign(32769, Command(Kind::LineTo));
	excessive.Commands.front() = Command(Kind::MoveTo);
	excessive.Commands.back() = Command(Kind::ClosePath);
	RequireFailure([&] {
		static_cast<void>(sgre::resource::PrepareCffDesignOutline(excessive));
	},
	    "point count");
}

int main() {
	try {
		TestNativeContourNormalization();
		TestCubicTagsAndMultipleContours();
		TestUnsupportedAndMalformedGeometry();
		std::cout << "CFF geometry tests passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << "CFF geometry test failure: " << error.what() << '\n';

		return 1;
	}
}
