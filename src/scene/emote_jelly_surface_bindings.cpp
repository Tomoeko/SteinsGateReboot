#include "sgre/scene/emote_jelly_surface_bindings.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <list>
#include <stdexcept>
#include <vector>

namespace sgre::scene {

	static constexpr std::size_t kPointsPerJellyRing = 16;
	static constexpr std::size_t kMaximumJellyParticles = 256;
	static constexpr std::size_t kMaximumJellySamples = 65536;
	static constexpr std::size_t kMaximumJellyCandidates = 1024 * 1024;
	static constexpr std::size_t kMaximumJellySamplingWork = 64 * 1024 * 1024;
	static constexpr std::array<std::size_t, 5> kJellyTreeOffsets{0, 1, 5, 21, 85};

	struct JellyCell {
		std::array<resource::MotionPoint, 4> Polygon;
		resource::MotionMeshMatrix Normalization;
		float Height = 0;
		float FarWidth = 0;
		float NearWidth = 0;
		std::uint32_t Section = 0;
		std::uint32_t Direction = 0;
	};

	struct JellyBounds {
		float Left = 0;
		float Top = 0;
		float Right = 0;
		float Bottom = 0;
	};

	struct JellyTreeEntry {
		JellyBounds Bounds;
		std::size_t Cell = 0;
	};

	struct JellySamplingWork {
		std::size_t Remaining = kMaximumJellySamplingWork;

		void Consume(std::size_t amount = 1) {
			if (amount > Remaining) {
				throw std::runtime_error("Emote Jelly surface construction exceeds its bounded geometry work");
			}

			Remaining -= amount;
		}
	};

	static void RequireFinitePoint(const resource::MotionPoint& point) {
		if (!std::isfinite(point.X) || !std::isfinite(point.Y)) {
			throw std::runtime_error("Emote Jelly surface requires finite actual initial ring geometry");
		}
	}

	static resource::MotionPoint MapJellyPoint(const resource::MotionMeshMatrix& matrix, const resource::MotionPoint& point) {
		resource::MotionPoint result;
		result.X = (point.Y * matrix.XY + point.X * matrix.XX) + matrix.TranslationX;
		result.Y = (point.Y * matrix.YY + point.X * matrix.YX) + matrix.TranslationY;
		RequireFinitePoint(result);

		return result;
	}

	static resource::MotionMeshMatrix ComposeJellyMatrices(const resource::MotionMeshMatrix& outer, const resource::MotionMeshMatrix& inner) {
		resource::MotionMeshMatrix result;
		result.XX = outer.XY * inner.YX + outer.XX * inner.XX;
		result.XY = outer.XY * inner.YY + outer.XX * inner.XY;
		result.YX = outer.YY * inner.YX + outer.YX * inner.XX;
		result.YY = outer.YY * inner.YY + outer.YX * inner.XY;
		result.TranslationX = (inner.TranslationY * outer.XY + inner.TranslationX * outer.XX) + outer.TranslationX;
		result.TranslationY = (inner.TranslationY * outer.YY + inner.TranslationX * outer.YX) + outer.TranslationY;

		return result;
	}

	static JellyBounds FindJellyBounds(std::span<const resource::MotionPoint> points) {
		JellyBounds result;
		result.Left = std::numeric_limits<float>::max();
		result.Top = std::numeric_limits<float>::max();
		result.Right = -std::numeric_limits<float>::max();
		result.Bottom = -std::numeric_limits<float>::max();
		for (const auto& point : points) {
			result.Left = std::min(point.X, result.Left);
			result.Top = std::min(point.Y, result.Top);
			result.Right = std::max(point.X, result.Right);
			result.Bottom = std::max(point.Y, result.Bottom);
		}

		return result;
	}

	static std::vector<resource::MotionPoint> BuildJellyHull(std::span<const resource::MotionPoint> points, JellySamplingWork& work) {
		const auto minimum = std::min_element(points.begin(), points.end(), [](const auto& first, const auto& second) {
			return first.X < second.X;
		});
		const auto maximum = std::max_element(points.begin(), points.end(), [](const auto& first, const auto& second) {
			return first.X < second.X;
		});
		if (minimum == maximum) {
			throw std::runtime_error("Emote Jelly surface requires a nondegenerate initial ring hull");
		}

		const auto minimum_index = static_cast<std::size_t>(minimum - points.begin());
		const auto maximum_index = static_cast<std::size_t>(maximum - points.begin());
		std::list<std::size_t> pending;
		for (std::size_t index = 0; index < points.size(); ++index) {
			if (index != minimum_index && index != maximum_index) {
				pending.push_back(index);
			}
		}

		std::list<std::size_t> hull{minimum_index, maximum_index};
		while (!pending.empty()) {
			const auto previous_count = pending.size();
			for (auto current = hull.begin(); current != hull.end(); ++current) {
				const auto previous = current == hull.begin() ? std::prev(hull.end()) : std::prev(current);
				const auto& first = points[*previous];
				const auto& second = points[*current];
				const float dy = second.Y - first.Y;
				const float negative_dx = -(second.X - first.X);
				const float squared_length = negative_dx * negative_dx + dy * dy;
				if (!(squared_length > 0) || !std::isfinite(squared_length)) {
					throw std::runtime_error("Emote Jelly hull edge exceeds its finite native distance domain");
				}

				float largest = -std::numeric_limits<float>::max();
				auto selected = pending.end();
				for (auto candidate = pending.begin(); candidate != pending.end(); ++candidate) {
					work.Consume();
					const auto& point = points[*candidate];
					const float side = ((dy * point.X + negative_dx * point.Y) + first.Y * second.X) - first.X * second.Y;
					if (side >= 0) {
						const float distance = side * side / squared_length;
						if (!std::isfinite(distance)) {
							throw std::runtime_error("Emote Jelly hull distance exceeds finite native arithmetic");
						}
						if (distance > largest) {
							largest = distance;
							selected = candidate;
						}
					}
				}

				if (selected == pending.end()) {
					continue;
				}

				const auto selected_index = *selected;
				const auto& third = points[selected_index];
				pending.erase(selected);
				current = hull.insert(current, selected_index);
				for (auto candidate = pending.begin(); candidate != pending.end();) {
					work.Consume();
					const auto& point = points[*candidate];
					const float first_side = (first.X - point.X) * (third.Y - first.Y) - (first.Y - point.Y) * (third.X - first.X);
					const float second_side = (second.Y - third.Y) * (third.X - point.X) - (second.X - third.X) * (third.Y - point.Y);
					const float third_side = (first.Y - second.Y) * (second.X - point.X) - (first.X - second.X) * (second.Y - point.Y);
					if (first_side < 0 || second_side < 0 || third_side < 0) {
						++candidate;
					} else {
						candidate = pending.erase(candidate);
					}
				}
			}

			if (pending.size() == previous_count) {
				break;
			}
		}

		std::vector<resource::MotionPoint> result;
		result.reserve(hull.size());
		for (const auto index : hull) {
			result.push_back(points[index]);
		}

		return result;
	}

	static bool JellyPolygonContains(std::span<const resource::MotionPoint> polygon, const resource::MotionPoint& point, JellySamplingWork& work) {
		bool inside = false;
		work.Consume(polygon.size());
		for (std::size_t index = 0; index < polygon.size(); ++index) {
			const auto& first = polygon[index];
			const auto& following = polygon[(index + 1) % polygon.size()];
			if ((first.Y > point.Y) != (following.Y > point.Y)) {
				const float crossing = ((following.X - first.X) * (point.Y - first.Y)) / (following.Y - first.Y);
				if (crossing > point.X - first.X) {
					inside = !inside;
				}
			}
		}

		return inside;
	}

	static bool PositiveJellyCell(const std::array<resource::MotionPoint, 4>& points) {
		float area = 0;
		auto previous = points.back();
		for (const auto& point : points) {
			area = area + (point.Y * previous.X - previous.Y * point.X);
			previous = point;
		}
		if (!std::isfinite(area)) {
			throw std::runtime_error("Emote Jelly cell exceeds finite native signed area");
		}

		return area > 0;
	}

	static void PrepareJellyCell(JellyCell& cell) {
		const auto& first = cell.Polygon[0];
		const auto& second = cell.Polygon[1];
		const float dx = second.X - first.X;
		const float dy = second.Y - first.Y;
		const float inverse_length = 1.0f / std::sqrt(dx * dx + dy * dy);
		const float cosine = inverse_length * dx;
		const float sine = inverse_length * dy;
		resource::MotionMeshMatrix rotation;
		rotation.XX = sine * 0.0f + cosine;
		rotation.XY = cosine * 0.0f + sine;
		rotation.TranslationX = ((-first.X * cosine) + (-first.Y * sine)) + 0.0f;
		rotation.YX = cosine * 0.0f + -sine;
		rotation.YY = -sine * 0.0f + cosine;
		rotation.TranslationY = ((-first.X * -sine) + (-first.Y * cosine)) + 0.0f;
		std::array<resource::MotionPoint, 4> transformed;
		for (std::size_t index = 0; index < transformed.size(); ++index) {
			transformed[index] = MapJellyPoint(rotation, cell.Polygon[index]);
		}

		resource::MotionMeshMatrix shear;
		shear.XY = -transformed[3].X / transformed[3].Y;
		cell.Normalization = ComposeJellyMatrices(shear, rotation);
		for (auto& point : transformed) {
			point = MapJellyPoint(shear, point);
		}

		cell.Height = transformed[3].Y;
		cell.FarWidth = transformed[2].X;
		cell.NearWidth = transformed[1].X;
		if (cell.Height == 0 || !std::isfinite(cell.Height) || !std::isfinite(cell.FarWidth) || !std::isfinite(cell.NearWidth)) {
			throw std::runtime_error("Emote Jelly cell requires a finite native trapezoid inverse");
		}
	}

	static std::array<JellyBounds, 4> SplitJellyCellBounds(const JellyBounds& bounds) {
		const float width = bounds.Right - bounds.Left;
		const float height = bounds.Bottom - bounds.Top;
		if (width > height + height) {
			// Native3128* adds Top to the X quarter cuts as well. Preserve that observed branch.
			const float first = width * 0.25f + bounds.Top;
			const float second = width * 0.5f + bounds.Top;
			const float third = (width * 3.0f) * 0.25f + bounds.Top;

			return {{{bounds.Left, bounds.Top, first, bounds.Bottom}, {first, bounds.Top, second, bounds.Bottom}, {second, bounds.Top, third, bounds.Bottom}, {third, bounds.Top, bounds.Right, bounds.Bottom}}};
		}
		if (height <= width + width) {
			const float middle_x = width * 0.5f + bounds.Left;
			const float middle_y = height * 0.5f + bounds.Top;

			return {{{bounds.Left, bounds.Top, middle_x, middle_y}, {middle_x, bounds.Top, bounds.Right, middle_y}, {bounds.Left, middle_y, middle_x, bounds.Bottom}, {middle_x, middle_y, bounds.Right, bounds.Bottom}}};
		}

		const float first = height * 0.25f + bounds.Top;
		const float second = height * 0.5f + bounds.Top;
		const float third = (height * 3.0f) * 0.25f + bounds.Top;

		return {{{bounds.Left, bounds.Top, bounds.Right, first}, {bounds.Left, first, bounds.Right, second}, {bounds.Left, second, bounds.Right, third}, {bounds.Left, third, bounds.Right, bounds.Bottom}}};
	}

	static std::uint32_t JellyMortonCode(const JellyBounds& bounds, const resource::MotionPoint& point) {
		const float x = (point.X - bounds.Left) / ((bounds.Right - bounds.Left) / 16.0f);
		const float y = (point.Y - bounds.Top) / ((bounds.Bottom - bounds.Top) / 16.0f);
		if (!std::isfinite(x) || !std::isfinite(y)) {
			throw std::runtime_error("Emote Jelly spatial lookup requires finite native cell coordinates");
		}

		const auto cell_x = static_cast<std::uint32_t>(std::clamp(x, 0.0f, 15.0f));
		const auto cell_y = static_cast<std::uint32_t>(std::clamp(y, 0.0f, 15.0f));
		std::uint32_t result = 0;
		for (std::uint32_t bit = 0; bit < 4; ++bit) {
			result |= ((cell_x >> bit) & 1U) << (bit * 2);
			result |= ((cell_y >> bit) & 1U) << (bit * 2 + 1);
		}

		return result;
	}

	static void InsertJellyTreeEntry(std::array<std::vector<JellyTreeEntry>, 341>& nodes, const JellyBounds& body, const JellyBounds& bounds, std::size_t cell_index) {
		const std::uint32_t first = JellyMortonCode(body, {bounds.Left, bounds.Top});
		const std::uint32_t last = JellyMortonCode(body, {bounds.Right, bounds.Bottom});
		std::uint32_t difference = first ^ last;
		std::uint32_t depth = 4;
		std::uint32_t index = 3;
		while (difference != 0) {
			if ((difference & 3U) != 0) {
				depth = index;
			}

			difference >>= 2;
			if (index != 0) {
				--index;
			}
		}

		const auto code = first >> ((4 - depth) * 2);
		nodes[kJellyTreeOffsets[depth] + code].push_back({bounds, cell_index});
	}

	static std::vector<JellyCell> PrepareJellyCells(std::span<const resource::MotionPoint> rings, const JellyBounds& body, std::array<std::vector<JellyTreeEntry>, 341>& nodes, JellySamplingWork& work) {
		std::vector<JellyCell> cells;
		const auto section_count = rings.size() / kPointsPerJellyRing - 1;
		cells.reserve(section_count * kPointsPerJellyRing);
		for (std::size_t section = 0; section < section_count; ++section) {
			for (std::size_t direction = 0; direction < kPointsPerJellyRing; ++direction) {
				work.Consume();
				const auto following = (direction + 1) % kPointsPerJellyRing;
				JellyCell cell;
				cell.Polygon = {rings[section * kPointsPerJellyRing + direction], rings[section * kPointsPerJellyRing + following], rings[(section + 1) * kPointsPerJellyRing + following], rings[(section + 1) * kPointsPerJellyRing + direction]};
				if (!PositiveJellyCell(cell.Polygon)) {
					continue;
				}

				cell.Section = static_cast<std::uint32_t>(section);
				cell.Direction = static_cast<std::uint32_t>(direction);
				PrepareJellyCell(cell);
				for (const auto& bounds : SplitJellyCellBounds(FindJellyBounds(cell.Polygon))) {
					InsertJellyTreeEntry(nodes, body, bounds, cells.size());
				}

				cells.push_back(cell);
			}
		}

		return cells;
	}

	static const JellyCell* FindJellyCell(const std::vector<JellyCell>& cells, const std::array<std::vector<JellyTreeEntry>, 341>& nodes, const JellyBounds& body, const resource::MotionPoint& point, JellySamplingWork& work) {
		const auto code = JellyMortonCode(body, point);
		for (std::uint32_t reverse_depth = 0; reverse_depth <= 4; ++reverse_depth) {
			const auto depth = 4 - reverse_depth;
			const auto& entries = nodes[kJellyTreeOffsets[depth] + (code >> (reverse_depth * 2))];
			// Native315450 prepends each real entry. Leaf-to-root lookup keeps that last-in-first order.
			for (auto entry = entries.rbegin(); entry != entries.rend(); ++entry) {
				work.Consume();
				const auto& bounds = entry->Bounds;
				if (bounds.Left < point.X && point.X < bounds.Right && bounds.Top < point.Y && point.Y < bounds.Bottom && JellyPolygonContains(cells[entry->Cell].Polygon, point, work)) {
					return &cells[entry->Cell];
				}
			}
		}

		return nullptr;
	}

	static std::int32_t BoundedJellyLatticeIndex(float value, bool ceiling) {
		const float rounded = ceiling ? std::ceil(value) : std::floor(value);
		if (!std::isfinite(rounded) || rounded < -1048576.0f || rounded > 1048576.0f) {
			throw std::runtime_error("Emote Jelly surface lattice index exceeds its bounded native domain");
		}

		return static_cast<std::int32_t>(rounded);
	}

	static EmoteJellyRingSample BindJellySample(const JellyCell* cell, const resource::MotionPoint& physical, const EmoteJellySurfaceSamplingDefinition& definition, std::uint32_t last_section, std::int32_t row, std::int32_t column) {
		EmoteJellyRingSample result;
		result.Section = last_section;
		result.Direction = 0;
		result.Circumference = 0;
		result.Longitudinal = 1;
		if (cell != nullptr) {
			result.Section = cell->Section;
			result.Direction = cell->Direction;
			const auto local = MapJellyPoint(cell->Normalization, physical);
			result.Longitudinal = local.Y / cell->Height;
			const float width = (1.0f - result.Longitudinal) * cell->NearWidth + result.Longitudinal * cell->FarWidth;
			result.Circumference = local.X / width;
		}

		result.Rest.X = (physical.X * (1.0f / definition.Size.X) + physical.Y * 0.0f) + definition.Origin.X;
		result.Rest.Y = (physical.Y * (1.0f / definition.Size.Y) + physical.X * 0.0f) + definition.Origin.Y;
		const float row_value = static_cast<float>(row);
		const float column_value = static_cast<float>(column);
		result.Order = (row_value * row_value + column_value * column_value) * (definition.ReverseOrder ? -1.0f : 1.0f);
		RequireFinitePoint(result.Rest);
		if (!std::isfinite(result.Longitudinal) || !std::isfinite(result.Circumference) || !std::isfinite(result.Order)) {
			throw std::runtime_error("Emote Jelly surface binding exceeds finite native interpolation");
		}

		return result;
	}

	std::vector<EmoteJellyRingSample> BuildEmoteJellySurfaceBindings(const EmoteJellySurfaceSamplingDefinition& definition, std::span<const resource::MotionPoint> initial_rings) {
		if (initial_rings.size() % kPointsPerJellyRing != 0 || initial_rings.size() < 3 * kPointsPerJellyRing || initial_rings.size() > (kMaximumJellyParticles + 1) * kPointsPerJellyRing) {
			throw std::runtime_error("Emote Jelly surface requires actual N+1 rings with16 points and bounded particle count");
		}
		if (definition.EncodedDegree != 0x33 && definition.EncodedDegree != 0x34 && definition.EncodedDegree != 0x43) {
			throw std::runtime_error("Emote Jelly surface requires the constructor-selected cubic/quartic basis pair");
		}

		RequireFinitePoint(definition.Size);
		RequireFinitePoint(definition.Origin);
		RequireFinitePoint(definition.ProjectionDirection);
		if (!(definition.Size.X > 0) || !(definition.Size.Y > 0) || !(definition.SampleDensity > 0) || !std::isfinite(definition.SampleDensity)) {
			throw std::runtime_error("Emote Jelly surface requires positive finite physical dimensions and sample density");
		}
		for (const auto& point : initial_rings) {
			RequireFinitePoint(point);
		}

		const auto tip = initial_rings[initial_rings.size() - kPointsPerJellyRing];
		for (const auto& point : initial_rings.last(kPointsPerJellyRing)) {
			if (point.X != tip.X || point.Y != tip.Y) {
				throw std::runtime_error("Emote Jelly surface requires the actual collapsed additional tip ring");
			}
		}

		JellyBounds body;
		body.Left = -definition.Origin.X * definition.Size.X;
		body.Top = -definition.Origin.Y * definition.Size.Y;
		body.Right = definition.Size.X + body.Left;
		body.Bottom = definition.Size.Y + body.Top;
		if (!(body.Right > body.Left) || !(body.Bottom > body.Top) || !std::isfinite(body.Left) || !std::isfinite(body.Top) || !std::isfinite(body.Right) || !std::isfinite(body.Bottom)) {
			throw std::runtime_error("Emote Jelly surface body bounds exceed finite native geometry");
		}

		JellySamplingWork work;
		const auto hull = BuildJellyHull(initial_rings.first(initial_rings.size() - kPointsPerJellyRing), work);
		std::array<std::vector<JellyTreeEntry>, 341> nodes;
		const auto cells = PrepareJellyCells(initial_rings, body, nodes, work);
		const float angle = std::atan2(definition.ProjectionDirection.Y, definition.ProjectionDirection.X);
		const float sine = std::sin(angle);
		const float cosine = std::cos(angle);
		const float determinant = cosine * cosine - -sine * sine;
		const float inverse_determinant = 1.0f / determinant;
		resource::MotionMeshMatrix to_sampling;
		to_sampling.XX = inverse_determinant * cosine;
		to_sampling.XY = sine * inverse_determinant;
		to_sampling.TranslationX = (-sine * 0.0f - cosine * 0.0f) * inverse_determinant;
		to_sampling.YX = -sine * inverse_determinant;
		to_sampling.YY = inverse_determinant * cosine;
		to_sampling.TranslationY = (sine * 0.0f - cosine * 0.0f) * inverse_determinant;
		std::vector<resource::MotionPoint> sampling_hull;
		sampling_hull.reserve(hull.size());
		for (const auto& point : hull) {
			sampling_hull.push_back(MapJellyPoint(to_sampling, point));
		}

		const auto sampling_bounds = FindJellyBounds(sampling_hull);
		const auto sampling_origin = MapJellyPoint(to_sampling, tip);
		const float width = static_cast<float>(definition.EncodedDegree & 15);
		const float height = static_cast<float>(definition.EncodedDegree >> 4);
		const float step = std::min((definition.Size.X / width) / definition.SampleDensity, (definition.Size.Y / height) / definition.SampleDensity);
		const float interval = (std::sqrt(3.0f) * step) * 0.5f;
		if (!(step > 0) || !(interval > 0) || !std::isfinite(step) || !std::isfinite(interval)) {
			throw std::runtime_error("Emote Jelly surface sampling interval exceeds finite native geometry");
		}

		const auto first_row = BoundedJellyLatticeIndex((sampling_bounds.Left - sampling_origin.X) / interval, false);
		const auto last_row = BoundedJellyLatticeIndex((sampling_bounds.Right - sampling_origin.X) / interval, true);
		const auto first_column = BoundedJellyLatticeIndex((sampling_bounds.Top - sampling_origin.Y) / step, false) - 1;
		const auto last_column = BoundedJellyLatticeIndex((sampling_bounds.Bottom - sampling_origin.Y) / step, true) + 1;
		const auto row_count = static_cast<std::size_t>(last_row - first_row + 1);
		const auto column_count = static_cast<std::size_t>(last_column - first_column + 1);
		if (column_count == 0 || row_count > kMaximumJellyCandidates / column_count) {
			throw std::runtime_error("Emote Jelly surface exceeds its bounded actual lattice candidates");
		}

		const auto last_section = static_cast<std::uint32_t>(initial_rings.size() / kPointsPerJellyRing - 2);
		std::vector<EmoteJellyRingSample> result;
		for (auto row = first_row; row <= last_row; ++row) {
			const float shift = row % 2 != 0 ? -step * 0.5f : 0.0f;
			const float x = (static_cast<float>(row) * interval + sampling_origin.X) + 0.0f;
			for (auto column = first_column; column <= last_column; ++column) {
				work.Consume();
				const float y = (static_cast<float>(column) * step + sampling_origin.Y) + shift;
				const resource::MotionPoint sampling{x, y};
				if (!(sampling_bounds.Left <= x && x < sampling_bounds.Right && sampling_bounds.Top <= y && y < sampling_bounds.Bottom) || !JellyPolygonContains(sampling_hull, sampling, work)) {
					continue;
				}

				resource::MotionPoint physical;
				physical.X = (x * cosine + y * -sine) + 0.0f;
				physical.Y = (x * sine + y * cosine) + 0.0f;
				if (!(body.Left <= physical.X && physical.X < body.Right && body.Top <= physical.Y && physical.Y < body.Bottom)) {
					continue;
				}

				const JellyCell* cell = nullptr;
				if (row != 0 || column != 0) {
					cell = FindJellyCell(cells, nodes, body, physical, work);
					if (cell == nullptr) {
						continue;
					}
				}
				if (result.size() == kMaximumJellySamples) {
					throw std::runtime_error("Emote Jelly surface exceeds its bounded actual sample bindings");
				}

				result.push_back(BindJellySample(cell, physical, definition, last_section, row, column));
			}
		}

		return result;
	}

} // namespace sgre::scene
