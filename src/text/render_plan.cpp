#include "sgre/text/render_plan.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>

namespace sgre::text {

	static constexpr std::size_t kMaximumPreparedEntries = 1024 * 1024;

	static void AddPreparedCount(std::size_t& count, std::size_t added, const char* context) {
		if (added > kMaximumPreparedEntries - count) {
			throw std::runtime_error(context);
		}

		count += added;
	}

	static TextBounds ElementBounds(const TextRenderElement& element) {
		const auto& position = element.Geometry.Position;
		TextBounds bounds;
		bounds.Left = position.X;
		bounds.Top = position.Y;
		bounds.Right = position.X + element.Geometry.Metrics.Width;
		bounds.Bottom = position.Y + element.Geometry.Metrics.Height;
		if (!std::isfinite(bounds.Left) || !std::isfinite(bounds.Top) || !std::isfinite(bounds.Right) || !std::isfinite(bounds.Bottom)) {
			throw std::runtime_error("text render element bounds are nonfinite");
		}

		return bounds;
	}

	static void ValidateLink(const TextLinkState& link, std::size_t& count) {
		AddPreparedCount(count, link.Elements.size(), "text retained link elements exceed the preparation limit");
		AddPreparedCount(count, link.RunBounds.size(), "text retained link bounds exceed the preparation limit");
		AddPreparedCount(count, link.Text.size(), "text retained link units exceed the preparation limit");
		for (const auto& bounds : link.RunBounds) {
			if (!std::isfinite(bounds.Left) || !std::isfinite(bounds.Top) || !std::isfinite(bounds.Right) || !std::isfinite(bounds.Bottom)) {
				throw std::runtime_error("text retained link bounds are nonfinite");
			}
		}
	}

	static float IntegralBoundaryTime(float time) {
		if (!std::isfinite(time) || double(time) < double(std::numeric_limits<std::int32_t>::min()) || double(time) > double(std::numeric_limits<std::int32_t>::max())) {
			throw std::runtime_error("text reveal boundary exceeds its signed integer range");
		}

		return static_cast<float>(static_cast<std::int32_t>(time));
	}

	static std::size_t ValidatePreparation(std::span<const TextRenderLine> lines, std::span<const TextLinkState> links, std::span<const TextRevealBoundary> boundaries, std::uint32_t segment_count) {
		std::size_t count = 0;
		AddPreparedCount(count, lines.size(), "text render lines exceed the preparation limit");
		AddPreparedCount(count, links.size(), "text render links exceed the preparation limit");
		AddPreparedCount(count, boundaries.size(), "text reveal boundaries exceed the preparation limit");
		for (const auto& link : links) {
			ValidateLink(link, count);
		}

		std::size_t element_count = 0;
		for (const auto& line : lines) {
			AddPreparedCount(count, line.Elements.size(), "text render elements exceed the preparation limit");
			element_count += line.Elements.size();
			for (const auto& element : line.Elements) {
				AddPreparedCount(count, element.Text.size(), "text render units exceed the preparation limit");
				static_cast<void>(ElementBounds(element));
				if (!std::isfinite(element.RevealTime)) {
					throw std::runtime_error("text render reveal time is nonfinite");
				}
				if (element.LinkIndex >= 0 && static_cast<std::size_t>(element.LinkIndex) >= links.size()) {
					throw std::runtime_error("text render link index exceeds its owner");
				}
			}
		}

		if (segment_count > 1 && !boundaries.empty()) {
			for (const auto& boundary : boundaries.first(boundaries.size() - 1)) {
				static_cast<void>(IntegralBoundaryTime(boundary.Time));
			}
		}

		return element_count;
	}

	static void ExpandRunBounds(TextBounds& bounds, const TextBounds& element) {
		if (element.Left < bounds.Left) {
			bounds.Left = element.Left;
		}
		if (element.Top < bounds.Top) {
			bounds.Top = element.Top;
		}
		if (element.Right > bounds.Right) {
			bounds.Right = element.Right;
		}
		if (element.Bottom > bounds.Bottom) {
			bounds.Bottom = element.Bottom;
		}
	}

	static void AppendLinkElement(TextLinkState& link, const TextRenderElement& element, std::int32_t run) {
		link.Elements.push_back(element.ElementId);
		link.Text.append(element.Text);
		const auto bounds = ElementBounds(element);
		if (link.RunBounds.empty() || link.LastRun != run) {
			link.RunBounds.push_back(bounds);
			link.LastRun = run;
		} else {
			ExpandRunBounds(link.RunBounds.back(), bounds);
		}
	}

	std::vector<TextRevealBoundary> PrepareTextKeyWaits(std::span<const TextRevealBoundary> boundaries, float time_scale) {
		if (boundaries.size() > kMaximumPreparedEntries) {
			throw std::runtime_error("text key-wait boundaries exceed the retained preparation limit");
		}

		std::vector<TextRevealBoundary> result;
		if (boundaries.size() > 1) {
			result.assign(boundaries.begin(), boundaries.end() - 1);
			for (auto& boundary : result) {
				boundary.Time *= time_scale;
			}
		}

		return result;
	}

	std::uint32_t CalculateTextShownElementCount(std::span<const float> finalized_times, std::int32_t elapsed_time, float time_scale) {
		if (finalized_times.size() > kMaximumPreparedEntries) {
			throw std::runtime_error("text reveal query exceeds its finalized preparation limit");
		}

		const auto time = static_cast<float>(elapsed_time);
		for (std::size_t index = finalized_times.size(); index != 0; --index) {
			const auto threshold = finalized_times[index - 1] * time_scale;
			if (time >= threshold) {
				return static_cast<std::uint32_t>(index);
			}
		}

		return 0;
	}

	TextRenderPlan PrepareTextRenderPlan(std::span<const TextRenderLine> lines, std::span<const TextLinkState> links, std::span<const TextRevealBoundary> reveal_boundaries, std::uint32_t render_call_count) {
		const auto count = ValidatePreparation(lines, links, reveal_boundaries, render_call_count);
		TextRenderPlan result;
		result.Links.assign(links.begin(), links.end());
		result.RevealBoundaries.assign(reveal_boundaries.begin(), reveal_boundaries.end());
		std::vector<const TextRenderElement*> order;
		order.reserve(count);
		std::int32_t run = 0;
		std::int32_t previous_link = -1;
		for (const auto& line : lines) {
			for (const auto& element : line.Elements) {
				order.push_back(&element);
				if (element.LinkIndex != previous_link) {
					++run;
				}
				if (element.LinkIndex >= 0) {
					AppendLinkElement(result.Links[static_cast<std::size_t>(element.LinkIndex)], element, run);
				}
				previous_link = element.LinkIndex;
			}
			++run;
		}

		if (render_call_count > 1) {
			std::stable_sort(order.begin(), order.end(), [](const auto* first, const auto* second) {
				return first->RevealTime < second->RevealTime;
			});
			std::size_t first_element = 0;
			if (!result.RevealBoundaries.empty()) {
				for (auto& boundary : std::span(result.RevealBoundaries).first(result.RevealBoundaries.size() - 1)) {
					const auto time = IntegralBoundaryTime(boundary.Time);
					while (first_element < order.size() && order[first_element]->RevealTime < time) {
						++first_element;
					}
					boundary.FirstElement = static_cast<std::uint32_t>(first_element);
				}
			}
		}

		result.RenderOrder.reserve(count);
		result.RevealTimes.reserve(count);
		for (const auto* element : order) {
			result.RenderOrder.push_back(element->ElementId);
			result.RevealTimes.push_back(element->RevealTime);
		}

		return result;
	}

} // namespace sgre::text
