#pragma once

#include "operand_stack.hpp"

#include <cstddef>
#include <stdexcept>
#include <utility>
#include <vector>

namespace sgre::script {
	// Active register views borrow their context's stack by offset; suspended generators own snapshots.
	class FrameRegisters {
	public:
		void Resize(std::size_t count) {
			if (m_Stack) {
				throw std::runtime_error("active register views cannot resize independently of their stack");
			}

			m_Owned.resize(count);
			m_Count = count;
		}

		[[nodiscard]] std::size_t Size() const {
			return m_Count;
		}

		[[nodiscard]] bool Empty() const {
			return m_Count == 0;
		}

		[[nodiscard]] SquirrelValue& operator[](std::size_t index) {
			if (index >= m_Count) {
				throw std::runtime_error("register index exceeds its view");
			}

			return m_Stack ? m_Stack->At(m_Base + index) : m_Owned[index];
		}

		[[nodiscard]] const SquirrelValue& operator[](std::size_t index) const {
			if (index >= m_Count) {
				throw std::runtime_error("register index exceeds its view");
			}

			return m_Stack ? m_Stack->At(m_Base + index) : m_Owned[index];
		}

		[[nodiscard]] std::size_t Base() const {
			if (!m_Stack) {
				throw std::runtime_error("detached registers have no physical stack offset");
			}

			return m_Base;
		}

		void Bind(OperandStack& stack, std::size_t base, std::size_t parameter_count) {
			if (m_Stack || parameter_count > m_Count) {
				throw std::runtime_error("register view binding state is invalid");
			}

			for (std::size_t index = 0; index < parameter_count; ++index) {
				stack.Store(base + index, m_Owned[index]);
			}

			m_Base = base;
			m_Stack = &stack;
			m_Owned.clear();
		}

		void TransferToSnapshot(FrameRegisters& saved) {
			if (!m_Stack || saved.m_Stack) {
				throw std::runtime_error("generator transfer requires active and detached register views");
			}

			saved.Resize(m_Count);
			for (std::size_t index = 0; index < m_Count; ++index) {
				{
					SquirrelValue incoming(m_Stack->At(m_Base + index));
					SquirrelValue replaced(std::move(saved.m_Owned[index]));
					saved.m_Owned[index] = std::move(incoming);
				}

				m_Stack->Store(m_Base + index, {});
			}
		}

	private:
		std::vector<SquirrelValue> m_Owned;
		OperandStack* m_Stack = nullptr;
		std::size_t m_Base = 0;
		std::size_t m_Count = 0;
	};
} // namespace sgre::script
