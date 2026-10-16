#pragma once

#include "sgre/script/squirrel_vm.hpp"

#include <cstddef>
#include <limits>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace sgre::script {
	struct OperandFrameState {
		std::size_t PreviousBase = 0;
		std::size_t PreviousTop = 0;
	};

	class OperandStack {
	public:
		OperandStack(std::size_t initial_cells, std::size_t maximum_cells) : m_MaximumCells(maximum_cells) {
			if (initial_cells == 0 || initial_cells > maximum_cells) {
				throw std::runtime_error("operand stack initial allocation exceeds its bounds");
			}

			m_Cells.resize(initial_cells);
		}

		[[nodiscard]] std::size_t Base() const {
			return m_Base;
		}

		[[nodiscard]] std::size_t Top() const {
			return m_Top;
		}

		// These views borrow movable storage and must not survive a nested call or stack growth.
		[[nodiscard]] std::span<const SquirrelValue> AllocatedValues() const {
			return m_Cells;
		}

		[[nodiscard]] const SquirrelValue& At(std::size_t index) const {
			if (index >= m_Cells.size()) {
				throw std::runtime_error("operand cell exceeds allocated storage");
			}

			return m_Cells[index];
		}

		[[nodiscard]] SquirrelValue& At(std::size_t index) {
			if (index >= m_Cells.size()) {
				throw std::runtime_error("operand cell exceeds allocated storage");
			}

			return m_Cells[index];
		}

		void EnsurePushExtent(std::size_t count) {
			if (count > m_MaximumCells - m_Top) {
				throw std::runtime_error("operand argument extent exceeds its bounds");
			}

			if (count > m_Cells.size() - m_Top) {
				Grow(count - (m_Cells.size() - m_Top));
			}
		}

		void Store(std::size_t index, const SquirrelValue& value) {
			if (index >= m_Cells.size()) {
				throw std::runtime_error("operand cell exceeds allocated storage");
			}

			SquirrelValue incoming(value);
			SquirrelValue previous(std::move(m_Cells[index]));
			m_Cells[index] = std::move(incoming);
		}

		void Push(const SquirrelValue& value) {
			if (m_Top >= m_Cells.size()) {
				throw std::runtime_error("operand push exceeds allocated storage");
			}

			const auto index = m_Top++;
			Store(index, value);
		}

		void SetTop(std::size_t logical_count) {
			if (logical_count > m_Cells.size() - m_Base) {
				throw std::runtime_error("operand logical extent exceeds allocated storage");
			}

			const auto desired = m_Base + logical_count;
			while (m_Top > desired) {
				Store(--m_Top, {});
			}

			while (m_Top < desired) {
				Push({});
			}
		}

		[[nodiscard]] OperandFrameState EnterScriptFrame(std::size_t base, std::size_t register_count) {
			if (base > m_Top || base > m_MaximumCells || register_count > m_MaximumCells - base || register_count > m_MaximumCells / 2) {
				throw std::runtime_error("script operand extent exceeds its bounds");
			}

			const auto top = base + register_count;
			const auto margin = register_count * 2;
			if (margin > std::numeric_limits<std::size_t>::max() - top) {
				throw std::runtime_error("script operand growth margin exceeds its bounds");
			}

			if (top + margin > m_Cells.size()) {
				Grow(margin);
			}

			OperandFrameState previous;
			previous.PreviousBase = m_Base;
			previous.PreviousTop = m_Top;
			m_Base = base;
			m_Top = top;

			return previous;
		}

		[[nodiscard]] OperandFrameState EnterNativeFrame(std::size_t base, std::size_t argument_count) {
			static constexpr std::size_t kNativeHeadroom = 10;
			static constexpr std::size_t kNativeGrowth = 20;
			if (base > m_Top || argument_count > m_Top - base || kNativeHeadroom > std::numeric_limits<std::size_t>::max() - m_Top) {
				throw std::runtime_error("native operand extent exceeds its bounds");
			}

			if (m_Top + kNativeHeadroom > m_Cells.size()) {
				Grow(kNativeGrowth);
			}

			OperandFrameState previous;
			previous.PreviousBase = m_Base;
			previous.PreviousTop = m_Top;
			m_Base = base;
			m_Top = base + argument_count;

			return previous;
		}

		void LeaveNativeFrame(const OperandFrameState& state) {
			RestoreFrame(state);
		}

		[[nodiscard]] std::size_t RestoreScriptFrame(const OperandFrameState& state) {
			const auto old_top = m_Top;
			RestoreFrame(state);

			return old_top;
		}

		void LeaveScriptFrame(const OperandFrameState& state) {
			CleanupReturnedCells(RestoreScriptFrame(state));
		}

		void CleanupReturnedCells(std::size_t old_top) {
			if (old_top < m_Top) {
				return;
			}

			// Native254d30 includes the old one-past-top cell, then releases down to restored top.
			if (old_top >= m_Cells.size()) {
				throw std::runtime_error("script return cleanup exceeds allocated storage");
			}

			for (std::size_t index = old_top;; --index) {
				Store(index, {});
				if (index == m_Top) {
					break;
				}
			}
		}

	private:
		void Grow(std::size_t extra_cells) {
			if (extra_cells > m_MaximumCells - m_Cells.size()) {
				throw std::runtime_error("operand stack allocation exceeds its bounds");
			}

			m_Cells.resize(m_Cells.size() + extra_cells);
		}

		void RestoreFrame(const OperandFrameState& state) {
			if (state.PreviousBase > state.PreviousTop || state.PreviousTop > m_Cells.size()) {
				throw std::runtime_error("saved operand frame extent is invalid");
			}

			m_Base = state.PreviousBase;
			m_Top = state.PreviousTop;
		}

		std::vector<SquirrelValue> m_Cells;
		std::size_t m_Base = 0;
		std::size_t m_Top = 0;
		std::size_t m_MaximumCells = 0;
	};
} // namespace sgre::script
