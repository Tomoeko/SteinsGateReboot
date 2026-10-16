#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <string_view>

namespace sgre::script {
	struct NativeRegexpLimits {
		std::size_t MaxPatternBytes = 65536;
		std::size_t MaxTextBytes = 16 * 1024 * 1024;
		std::size_t MaxNodes = 65536;
		std::size_t MaxDepth = 256;
		std::size_t MaxMatchSteps = 10000000;
	};

	struct NativeRegexpRange {
		std::size_t Begin = 0;
		std::size_t End = 0;
		bool operator==(const NativeRegexpRange&) const = default;
	};

	struct NativeRegexpCapture {
		std::optional<std::size_t> Begin;
		std::size_t Length = 0;
	};

	// Byte-oriented native 2abda0/2ab850 grammar and matching order. Inputs are borrowed only during a call.
	class NativeRegexp {
	public:
		explicit NativeRegexp(std::string_view pattern, NativeRegexpLimits limits = {});
		~NativeRegexp();
		NativeRegexp(NativeRegexp&&) noexcept;
		NativeRegexp& operator=(NativeRegexp&&) noexcept;
		NativeRegexp(const NativeRegexp&) = delete;
		NativeRegexp& operator=(const NativeRegexp&) = delete;
		[[nodiscard]] bool Match(std::string_view text);
		[[nodiscard]] std::optional<NativeRegexpRange> Search(std::string_view text, std::size_t start = 0);
		[[nodiscard]] std::size_t SubexpressionCount() const;
		[[nodiscard]] std::span<const NativeRegexpCapture> Captures() const;

	private:
		class Implementation;
		std::unique_ptr<Implementation> m_Implementation;
	};
} // namespace sgre::script
