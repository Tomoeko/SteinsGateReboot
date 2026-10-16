# C++ source style

This guide applies to authored runtime code, tools, tests, and shaders. It combines the strict source layout in `AGENTS_test.md` with useful comments, as requested. Follow `AGENTS.md` for development constraints.

## References and project choices

Use the [C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines) for focused operations, explicit interfaces, resource management, const correctness, and type safety. In particular, apply F.2/F.3, R.1/R.3/R.5/R.21, and I.2/I.13. Use the [Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html) as a readability and ownership reference. These references inform the design; the concrete project conventions below govern formatting. Google-specific indentation, naming, and exception restrictions are not adopted.

Use [ClangFormat's documented configuration](https://clang.llvm.org/docs/ClangFormatStyleOptions.html) to enforce `.clang-format`. Prefer simple modern C++20 over C-style manual cleanup or elaborate template machinery. Research unfamiliar language and format behavior in primary sources before relying on it.

## Layout

- Use tabs for indentation, displayed at four columns, UTF-8, CRLF, and a final newline. Never pad return types, assignments, case labels, fields, or parameters for visual alignment. Use normal grammatical spacing.
- Always put control-flow bodies in braces and on separate lines, including single-statement guards and `continue`, `return`, or `break` bodies. Do not compress functions or conditionals onto one line.
- Keep calls, chained expressions, assignment expressions, ternaries, and print statements on one physical line. There is no column limit. Declarations may wrap if the parameter list is enormous; prefer a cohesive parameter type when it has a real invariant.
- Separate logical phases with a blank line: after guards, between initialization and computation, between independent operations, after a try/catch, and before the final return of a nontrivial function.
- Do not put braces around a `case` body. Move local work into a helper or use a suitable init-statement. Never compromise scope safety merely to fit a switch.
- Use `.hpp` for C++ headers, `.cpp` for portable implementation, `.mm` for Apple interop, and `.metal` for Metal shaders. Keep each header self-contained with `#pragma once`, direct includes, and a narrow public surface. Include the corresponding header first, then project headers, then standard/system headers in consistent groups.
- Never write `using namespace`. Never use unnamed namespaces. Use `static` for file-local functions and constants. Keep internal helper types out of public headers.

## Names and functions

Use `sgre` with focused lowercase namespaces such as `sgre::resource`. Use PascalCase for types, functions, enum values, and public data fields. Use `m_Something` for private instance fields and `sm_Something` for static member fields. Use lowercase_with_underscores for local variables and parameters, and `kSomething` for named constants. Resource strings and original lookup identifiers retain their exact spelling.

Give each function one recognizable job. An orchestrator should make its phases easy to see. Use guard clauses and focused helpers for distinct operations, with names such as `ReadHeader`, `DecodeArchiveEntry`, and `AdvanceDialogue`. Avoid tiny wrappers that conceal an obvious expression, arbitrary function-length limits, and helpers named after implementation accidents.

Use standard algorithms when they improve clarity. A direct bounded loop is often best for serialized fields and state transitions. Use `auto` when the initializer makes the type evident, not when it hides ownership, units, or narrowing. Prefer `const` for unchanged values and borrowed input. Do not add `std::move` to return a local value when ordinary return semantics suffice.

## Ownership and errors

Prefer the rule of zero: standard containers and resource owners should supply destruction, copying, and moves. A unique OS/GPU owner must not accidentally copy. Return values rather than partially initialized output arguments. Use `explicit` for converting constructors and `[[nodiscard]]` where discarding a result loses essential work or validation.

Do not use naked owning pointers, manual `new`/`delete`, or `malloc`/`free` in portable C++. Wrap foreign resources promptly in an owner. `std::span` and `std::string_view` borrow; neither extends a lifetime. Do not return views into temporary or released data.

Do not use designated initializers. Default-initialize aggregate state and assign each required field explicitly. Use default member initializers for safe defaults. Put a blank line before the next operation or return.

Use `std::uint8_t`, `std::uint16_t`, `std::uint32_t`, and `std::uint64_t` (and signed counterparts) for serialized values and fixed-width computations. Use `std::size_t` for in-memory sizes and indices. Never spell integer types as `long`, `long long`, `unsigned long`, or `unsigned long long`. Keep offset spaces and units explicit; do not conflate file offsets with executable virtual addresses.

Check bounds with subtraction after validating the starting offset. Check multiplication before allocation. Bound decompressed sizes, graph depth, table counts, and total work. Reject invalid tags and unsupported variants with a precise error. Use `assert` only for programmer invariants, never for input validation.

The recovery tools use exceptions for malformed files and I/O failure, caught once at the CLI/test boundary. Add context without swallowing errors. Do not mark a fallible operation `noexcept`. Destructors and resource release operations must be nonthrowing.

```cpp
static std::span<const std::uint8_t> ReadRange(std::span<const std::uint8_t> bytes, std::size_t offset, std::size_t byte_count) {
	if (offset > bytes.size() || byte_count > bytes.size() - offset) {
		throw std::runtime_error("resource range exceeds input");
	}

	return bytes.subspan(offset, byte_count);
}
```

## Comments and provenance

Comments are allowed. Explain a constraint, invariant, owner lifetime, unusual ordering, or evidence source. Do not describe the next statement in prose, leave stale commented code, or label guessed behavior as recovered fact. Prefer a short link to the relevant recovery note over embedding large decompilation extracts.

```cpp
// The archive stream starts after the clear eight-byte MZS header.
DecodePayload(bytes.subspan(kShellHeaderSize));
```

For TODOs, name the missing behavior and the discriminating recovery check. Keep substantial gaps in `ROADMAP.md` and the corresponding recovery document. A comment is not a substitute for enforcing a precondition.

## Portability and review

Decode little-endian fields byte by byte or through a checked byte reader. Never dereference an unaligned typed pointer into a file, serialize a native C++ object representation, or use implementation-defined signed overflow. Avoid C-style casts and `reinterpret_cast` outside a documented foreign API boundary.

Keep Metal/AppKit/native audio/media types inside the macOS platform module. Portable resource readers and engine data must build independently of that module. Choose platform abstractions when a recovered behavior needs them; do not build empty future backends.

Review behavior, ownership, failure paths, bounded work, evidence, readability, and format compliance. Verify permitted builds with compiler warnings and targeted sanitizer checks for parsers. Measure changes before making optimization claims. Keep the original call order, resource identities, units, and output stable during refactoring.
