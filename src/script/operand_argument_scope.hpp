#pragma once

#include "operand_stack.hpp"

#include <cstddef>
#include <optional>

namespace sgre::script {
	// A host call owns its appended arguments until an accepted frame takes over their cleanup.
	class OperandArgumentScope {
	public:
		OperandArgumentScope(OperandStack& stack, bool owns_arguments) : m_Stack(stack), m_Base(stack.Base()), m_Top(stack.Top()), m_OwnsArguments(owns_arguments) {
		}

		~OperandArgumentScope() {
			if (!m_OwnsArguments) {
				return;
			}

			OperandFrameState restore;
			restore.PreviousBase = m_Base;
			restore.PreviousTop = m_Stack.Top();
			m_Stack.LeaveNativeFrame(restore);
			m_Stack.SetTop(m_Top - m_Base);
		}
		OperandArgumentScope(const OperandArgumentScope&) = delete;
		OperandArgumentScope& operator=(const OperandArgumentScope&) = delete;

		void TransferToAcceptedFrame() {
			m_OwnsArguments = false;
		}

	private:
		OperandStack& m_Stack;
		std::size_t m_Base = 0;
		std::size_t m_Top = 0;
		bool m_OwnsArguments = false;
	};
} // namespace sgre::script
