#pragma once

#import <Foundation/Foundation.h>

#include <exception>
#include <stdexcept>
#include <string>
#include <utility>

namespace sgre::audio {
	enum class OfflineSoundSystemOperation {
		None,
		GraphCreation,
		EngineState,
		Render,
		Format,
		Timeline,
		SourceAttachment
	};

	enum class OfflineSoundSystemFailureKind {
		None,
		ObjectiveC,
		Cpp
	};

	static const char* OfflineSoundOperationName(OfflineSoundSystemOperation operation) {
		switch (operation) {
		case OfflineSoundSystemOperation::GraphCreation:
			return "graph creation";
		case OfflineSoundSystemOperation::EngineState:
			return "engine state";
		case OfflineSoundSystemOperation::Render:
			return "render";
		case OfflineSoundSystemOperation::Format:
			return "format";
		case OfflineSoundSystemOperation::Timeline:
			return "timeline";
		case OfflineSoundSystemOperation::SourceAttachment:
			return "source attachment";
		case OfflineSoundSystemOperation::None:
			return "none";
		}

		return "unknown";
	}

	static std::runtime_error OfflineSoundObjectiveCFailure(OfflineSoundSystemOperation operation, NSException* exception) {
		std::string reason = "unknown system exception";
		@try {
			const char* text = exception.reason.UTF8String;
			if (text != nullptr) {
				reason = text;
			}
		} @catch (NSException*) {
			// Obtaining a diagnostic must not let a second foreign exception escape the boundary.
		}

		return std::runtime_error(std::string("manual offline ") + OfflineSoundOperationName(operation) + " exception: " + reason);
	}

	// Serialized owner state. Preserve the first fault and partial actual sample consumption until Close.
	class OfflineSoundSystemBoundary {
	public:
		template <typename Work>
		decltype(auto) Invoke(OfflineSoundSystemOperation operation, Work&& work) {
			RequireOperational();
			try {
				@try {
					return std::forward<Work>(work)();
				} @catch (NSException* exception) {
					if (!m_Exception) {
						m_Kind = OfflineSoundSystemFailureKind::ObjectiveC;
					}
					throw OfflineSoundObjectiveCFailure(operation, exception);
				}
			} catch (...) {
				if (!m_Exception) {
					m_Operation = operation;
					if (m_Kind == OfflineSoundSystemFailureKind::None) {
						m_Kind = OfflineSoundSystemFailureKind::Cpp;
					}
					m_Exception = std::current_exception();
				}
				throw;
			}
		}

		void RequireOperational() const {
			if (m_Exception) {
				std::rethrow_exception(m_Exception);
			}
		}

		[[nodiscard]] OfflineSoundSystemOperation Operation() const noexcept {
			return m_Operation;
		}

		[[nodiscard]] OfflineSoundSystemFailureKind Kind() const noexcept {
			return m_Kind;
		}

	private:
		OfflineSoundSystemOperation m_Operation = OfflineSoundSystemOperation::None;
		OfflineSoundSystemFailureKind m_Kind = OfflineSoundSystemFailureKind::None;
		std::exception_ptr m_Exception;
	};
} // namespace sgre::audio
