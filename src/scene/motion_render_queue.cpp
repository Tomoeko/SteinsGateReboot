#include "sgre/scene/motion_render_queue.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace sgre::scene {

	static constexpr std::size_t kMaxQueueCommands = 1024 * 1024;
	static constexpr std::size_t kInsertionCommandLimit = 32;
	static constexpr std::size_t kMedianDistanceLimit = 40;

	bool MotionRenderOrderBefore(const MotionRenderOrder& left, const MotionRenderOrder& right) noexcept {
		if (left.Primary != right.Primary) {
			return left.Primary < right.Primary;
		}

		if (left.Secondary != right.Secondary) {
			return left.Secondary < right.Secondary;
		}

		return left.Ordinal < right.Ordinal;
	}

	static bool Before(MotionRenderOrderReference left, MotionRenderOrderReference right) noexcept {
		return MotionRenderOrderBefore(left.get(), right.get());
	}

	static void SortMedian(std::span<MotionRenderOrderReference> commands, std::size_t first, std::size_t middle, std::size_t last) {
		if (Before(commands[middle], commands[first])) {
			std::swap(commands[middle], commands[first]);
		}

		if (Before(commands[last], commands[middle])) {
			std::swap(commands[last], commands[middle]);
			if (Before(commands[middle], commands[first])) {
				std::swap(commands[middle], commands[first]);
			}
		}
	}

	static std::pair<std::size_t, std::size_t> Partition(std::span<MotionRenderOrderReference> commands, std::size_t first, std::size_t last) {
		const auto middle = first + (last - first) / 2;
		const auto final = last - 1;
		if (final - first <= kMedianDistanceLimit) {
			SortMedian(commands, first, middle, final);
		} else {
			const auto step = (last - first) / 8;
			SortMedian(commands, first, first + step, first + step * 2);
			SortMedian(commands, middle - step, middle, middle + step);
			SortMedian(commands, final - step * 2, final - step, final);
			SortMedian(commands, first + step, middle, final - step);
		}

		auto equal_begin = middle;
		auto equal_end = middle + 1;
		while (equal_begin > first && !Before(commands[equal_begin - 1], commands[equal_begin]) && !Before(commands[equal_begin], commands[equal_begin - 1])) {
			--equal_begin;
		}

		while (equal_end < last && !Before(commands[equal_end], commands[equal_begin]) && !Before(commands[equal_begin], commands[equal_end])) {
			++equal_end;
		}

		auto greater = equal_end;
		auto lesser = equal_begin;
		for (;;) {
			while (greater < last) {
				if (!Before(commands[equal_begin], commands[greater])) {
					if (Before(commands[greater], commands[equal_begin])) {
						break;
					}

					if (equal_end != greater) {
						std::swap(commands[equal_end], commands[greater]);
					}

					++equal_end;
				}

				++greater;
			}

			while (lesser > first) {
				const auto previous = lesser - 1;
				if (!Before(commands[previous], commands[equal_begin])) {
					if (Before(commands[equal_begin], commands[previous])) {
						break;
					}

					--equal_begin;
					if (equal_begin != previous) {
						std::swap(commands[equal_begin], commands[previous]);
					}
				}

				--lesser;
			}

			if (lesser == first && greater == last) {
				return {equal_begin, equal_end};
			}

			if (lesser == first) {
				if (equal_end != greater) {
					std::swap(commands[equal_begin], commands[equal_end]);
				}

				++equal_end;
				std::swap(commands[equal_begin], commands[greater]);
				++equal_begin;
				++greater;
			} else if (greater == last) {
				--lesser;
				--equal_begin;
				if (lesser != equal_begin) {
					std::swap(commands[lesser], commands[equal_begin]);
				}

				--equal_end;
				std::swap(commands[equal_begin], commands[equal_end]);
			} else {
				--lesser;
				std::swap(commands[greater], commands[lesser]);
				++greater;
			}
		}
	}

	static void AdjustHeap(std::span<MotionRenderOrderReference> commands, std::size_t hole, std::size_t count, MotionRenderOrderReference value) {
		const auto initial = hole;
		const auto last_parent = (count - 1) / 2;
		while (hole < last_parent) {
			auto child = hole * 2 + 2;
			if (Before(commands[child], commands[child - 1])) {
				--child;
			}

			commands[hole] = commands[child];
			hole = child;
		}

		if (hole == last_parent && count % 2 == 0) {
			commands[hole] = commands[count - 1];
			hole = count - 1;
		}

		while (hole > initial) {
			const auto parent = (hole - 1) / 2;
			if (!Before(commands[parent], value)) {
				break;
			}

			commands[hole] = commands[parent];
			hole = parent;
		}

		commands[hole] = value;
	}

	static void HeapSort(std::span<MotionRenderOrderReference> commands) {
		for (auto parent = commands.size() / 2; parent > 0;) {
			--parent;
			AdjustHeap(commands, parent, commands.size(), commands[parent]);
		}

		for (auto count = commands.size(); count > 1; --count) {
			const auto value = commands[count - 1];
			commands[count - 1] = commands[0];
			AdjustHeap(commands, 0, count - 1, value);
		}
	}

	static void InsertionSort(std::span<MotionRenderOrderReference> commands) {
		for (std::size_t index = 1; index < commands.size(); ++index) {
			const auto value = commands[index];
			if (Before(value, commands[0])) {
				// Native front insertion crosses unordered intermediate values without comparing them.
				for (auto position = index; position > 0; --position) {
					commands[position] = commands[position - 1];
				}

				commands[0] = value;
			} else {
				auto position = index;
				while (position > 0 && Before(value, commands[position - 1])) {
					commands[position] = commands[position - 1];
					--position;
				}

				commands[position] = value;
			}
		}
	}

	static void NativeSort(std::span<MotionRenderOrderReference> commands, std::size_t ideal) {
		// Original 0x140501b00 uses an explicit algorithm; NaN comparisons are not a std::sort ordering.
		while (commands.size() > kInsertionCommandLimit) {
			if (ideal == 0) {
				HeapSort(commands);

				return;
			}

			const auto [equal_begin, equal_end] = Partition(commands, 0, commands.size());
			ideal = (ideal >> 1) + (ideal >> 2);
			if (equal_begin < commands.size() - equal_end) {
				NativeSort(commands.first(equal_begin), ideal);
				commands = commands.subspan(equal_end);
			} else {
				NativeSort(commands.subspan(equal_end), ideal);
				commands = commands.first(equal_begin);
			}
		}

		InsertionSort(commands);
	}

	MotionRenderQueue::MotionRenderQueue(std::size_t capacity) : m_Capacity(capacity) {
		if (capacity == 0 || capacity > kMaxQueueCommands) {
			throw std::runtime_error("motion render queue capacity must fit a positive native ordinal range");
		}

		m_Commands.reserve(capacity);
	}

	void MotionRenderQueue::Append(MotionRenderOrder& command) {
		if (m_Flushing || m_Commands.size() >= m_Capacity) {
			throw std::runtime_error("motion render queue append is forbidden during submission or beyond capacity");
		}

		m_Commands.emplace_back(command);
	}

	std::size_t MotionRenderQueue::Size() const noexcept {
		return m_Commands.size();
	}

	std::size_t MotionRenderQueue::Capacity() const noexcept {
		return m_Capacity;
	}

	void MotionRenderQueue::Clear() {
		if (m_Flushing) {
			throw std::runtime_error("motion render queue cannot clear during submission");
		}

		m_Commands.clear();
	}

	void MotionRenderQueue::Flush(const Submission& submit) {
		if (m_Flushing) {
			throw std::runtime_error("motion render queue cannot flush recursively");
		}

		if (m_Commands.empty()) {
			return;
		}

		if (!submit) {
			throw std::runtime_error("motion render queue requires a submission callback");
		}

		for (std::size_t index = 0; index < m_Commands.size(); ++index) {
			m_Commands[index].get().Ordinal = static_cast<std::uint32_t>(index);
		}

		NativeSort(m_Commands, m_Commands.size());
		m_Flushing = true;
		try {
			submit(m_Commands);
		} catch (...) {
			m_Flushing = false;

			throw;
		}

		m_Flushing = false;
		m_Commands.clear();
	}

} // namespace sgre::scene
