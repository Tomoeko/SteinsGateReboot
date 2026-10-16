#pragma once

#import <Foundation/Foundation.h>

#include <exception>
#include <stdexcept>
#include <string>
#include <utility>

namespace sgre::render {

	enum class PresentationSystemOperation {
		None,
		Creation,
		Observation,
		Policy,
		Transfer,
		Commit,
		Retirement
	};

	enum class PresentationSystemFailureKind {
		None,
		ObjectiveC,
		Cpp
	};

	static inline const char* PresentationOperationName(PresentationSystemOperation operation) {
		switch (operation) {
		case PresentationSystemOperation::Creation:
			return "creation";
		case PresentationSystemOperation::Observation:
			return "observation";
		case PresentationSystemOperation::Policy:
			return "policy";
		case PresentationSystemOperation::Transfer:
			return "transfer";
		case PresentationSystemOperation::Commit:
			return "commit";
		case PresentationSystemOperation::Retirement:
			return "retirement";
		case PresentationSystemOperation::None:
			return "none";
		}

		return "unknown";
	}

	static inline std::runtime_error PresentationObjectiveCFailure(PresentationSystemOperation operation, NSException* exception) {
		std::string reason = "unknown system exception";
		@try {
			const char* text = exception.reason.UTF8String;
			if (text != nullptr) {
				reason = text;
			}
		} @catch (NSException*) {
			// A second foreign diagnostic failure cannot replace the original operation.
		}

		return std::runtime_error(std::string("hidden presentation ") + PresentationOperationName(operation) + " exception: " + reason);
	}

	// Serialized platform owner; cleanup may run after a fault but never replaces its first cause.
	class PresentationSystemBoundary {
	public:
		template <typename Work>
		decltype(auto) Invoke(PresentationSystemOperation operation, Work&& work) {
			RequireOperational();
			try {
				return Observe(operation, std::forward<Work>(work));
			} catch (...) {
				Retain(operation, PresentationSystemFailureKind::Cpp);
				RequireOperational();
				throw;
			}
		}

		// C++ input rejection remains correctable; an actual foreign exception faults the owner.
		template <typename Work>
		decltype(auto) Observe(PresentationSystemOperation operation, Work&& work) {
			RequireOperational();
			@try {
				return std::forward<Work>(work)();
			} @catch (NSException* exception) {
				try {
					throw PresentationObjectiveCFailure(operation, exception);
				} catch (...) {
					Retain(operation, PresentationSystemFailureKind::ObjectiveC);
					RequireOperational();
					throw;
				}
			}
		}

		template <typename Work>
		bool Cleanup(PresentationSystemOperation operation, Work&& work) noexcept {
			try {
				@try {
					std::forward<Work>(work)();
				} @catch (NSException* exception) {
					try {
						throw PresentationObjectiveCFailure(operation, exception);
					} catch (...) {
						Retain(operation, PresentationSystemFailureKind::ObjectiveC);
						throw;
					}
				}
			} catch (...) {
				Retain(operation, PresentationSystemFailureKind::Cpp);
				m_CleanupFailed = true;

				return false;
			}

			return true;
		}

		void RequireOperational() const {
			if (m_Exception) {
				std::rethrow_exception(m_Exception);
			}
		}

		[[nodiscard]] PresentationSystemOperation Operation() const noexcept {
			return m_Operation;
		}

		[[nodiscard]] PresentationSystemFailureKind Kind() const noexcept {
			return m_Kind;
		}

		[[nodiscard]] bool CleanupFailed() const noexcept {
			return m_CleanupFailed;
		}

	private:
		void Retain(PresentationSystemOperation operation, PresentationSystemFailureKind kind) noexcept {
			if (!m_Exception) {
				m_Exception = std::current_exception();
				m_Operation = operation;
				m_Kind = kind;
			}
		}

		PresentationSystemOperation m_Operation = PresentationSystemOperation::None;
		PresentationSystemFailureKind m_Kind = PresentationSystemFailureKind::None;
		std::exception_ptr m_Exception;
		bool m_CleanupFailed = false;
	};

} // namespace sgre::render
