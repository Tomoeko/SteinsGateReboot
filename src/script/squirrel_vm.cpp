#include "sgre/script/squirrel_vm.hpp"

#include "sgre/script/source_compiler.hpp"
#include "sgre/script/template_strings.hpp"

#include "frame_registers.hpp"
#include "operand_argument_scope.hpp"
#include "sgre/script/native_float.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <exception>
#include <limits>
#include <locale>
#include <memory>
#include <optional>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace sgre::script {
	static void AssignStoredValue(SquirrelValue& target, SquirrelValue incoming) {
		SquirrelValue previous(std::move(target));
		target = std::move(incoming);
	}

	static void ReleaseStoredValue(SquirrelValue& target) noexcept {
		SquirrelValue previous(std::move(target));
		target = {};
	}

	enum class VmCollectableKind {
		Object,
		Context
	};

	struct VmCollectableIdentity {
		VmCollectableKind Kind = VmCollectableKind::Object;
		std::uint32_t Index = 0;
		std::uint64_t Generation = 0;
		bool operator==(const VmCollectableIdentity&) const = default;
	};

	struct VmCollectableLinks {
		bool GarbageTracked = false;
		bool Marked = false;
		bool MarkQueued = false;
		std::optional<VmCollectableIdentity> GarbageNext;
		std::optional<VmCollectableIdentity> GarbagePrevious;
	};

	struct SquirrelObjectHandle::Lifetime : VmCollectableLinks {
		std::weak_ptr<void> Arena;
		const void* Owner = nullptr;
		std::uint32_t Index = 0;
		std::uint64_t Generation = 1;
		std::uint64_t StrongReferences = 0;
		bool Alive = true;
		bool ReleaseQueued = false;
		bool Finalizing = false;
		void* ReleaseContext = nullptr;
		void (*OnLastRelease)(void*, std::uint32_t, std::uint64_t) noexcept = nullptr;
	};

	SquirrelObjectHandle::SquirrelObjectHandle() = default;
	SquirrelObjectHandle::~SquirrelObjectHandle() {
		Release();
	}

	SquirrelObjectHandle::SquirrelObjectHandle(const SquirrelObjectHandle& other) : Index(other.Index), Generation(other.Generation), Owner(other.Owner), m_Lifetime(other.m_Lifetime) {
		Acquire();
	}

	SquirrelObjectHandle& SquirrelObjectHandle::operator=(const SquirrelObjectHandle& other) {
		if (this != &other) {
			SquirrelObjectHandle copy(other);
			*this = std::move(copy);
		}

		return *this;
	}

	SquirrelObjectHandle::SquirrelObjectHandle(SquirrelObjectHandle&& other) noexcept : Index(other.Index), Generation(other.Generation), Owner(other.Owner), m_Lifetime(std::move(other.m_Lifetime)), m_IsStrong(other.m_IsStrong) {
		other.Index = 0;
		other.Generation = 0;
		other.Owner = nullptr;
		other.m_IsStrong = false;
	}

	SquirrelObjectHandle& SquirrelObjectHandle::operator=(SquirrelObjectHandle&& other) noexcept {
		if (this != &other) {
			SquirrelObjectHandle previous(std::move(*this));
			Index = other.Index;
			Generation = other.Generation;
			Owner = other.Owner;
			m_Lifetime = std::move(other.m_Lifetime);
			m_IsStrong = other.m_IsStrong;
			other.Index = 0;
			other.Generation = 0;
			other.Owner = nullptr;
			other.m_IsStrong = false;
		}

		return *this;
	}

	bool SquirrelObjectHandle::operator==(const SquirrelObjectHandle& other) const {
		return Index == other.Index && Generation == other.Generation && Owner == other.Owner;
	}

	bool SquirrelObjectIdentity::operator==(const SquirrelObjectIdentity& other) const {
		return Index == other.Index && Generation == other.Generation && Owner == other.Owner && !m_LifetimeToken.owner_before(other.m_LifetimeToken) && !other.m_LifetimeToken.owner_before(m_LifetimeToken);
	}

	static constexpr std::size_t kMaxBorrowedReceiverDepth = 64;

	struct VmBorrowedReceiverIdentity {
		std::uint32_t Index = 0;
		std::uint64_t Generation = 0;
		bool operator==(const VmBorrowedReceiverIdentity&) const = default;
	};

	class VmBorrowedReceiverScope {
	public:
		VmBorrowedReceiverScope(std::vector<VmBorrowedReceiverIdentity>& scopes, VmBorrowedReceiverIdentity identity)
		    : m_Scopes(scopes) {
			m_Scopes.push_back(identity);
		}

		~VmBorrowedReceiverScope() {
			m_Scopes.pop_back();
		}
		VmBorrowedReceiverScope(const VmBorrowedReceiverScope&) = delete;
		VmBorrowedReceiverScope& operator=(const VmBorrowedReceiverScope&) = delete;

	private:
		std::vector<VmBorrowedReceiverIdentity>& m_Scopes;
	};

	void SquirrelObjectHandle::Acquire() {
		if (!m_Lifetime || !m_Lifetime->Alive || m_Lifetime->Arena.expired()) {
			return;
		}

		if (m_Lifetime->StrongReferences == std::numeric_limits<std::uint64_t>::max()) {
			throw std::runtime_error("Squirrel strong reference count exhausted");
		}

		++m_Lifetime->StrongReferences;
		m_IsStrong = true;
	}

	void SquirrelObjectHandle::Release() noexcept {
		if (!m_IsStrong || !m_Lifetime || !m_Lifetime->Alive || m_Lifetime->Arena.expired()) {
			return;
		}

		m_IsStrong = false;
		--m_Lifetime->StrongReferences;
		if (m_Lifetime->StrongReferences == 0 && m_Lifetime->OnLastRelease) {
			m_Lifetime->OnLastRelease(m_Lifetime->ReleaseContext, Index, Generation);
		}
	}

	static std::uint64_t StringHash(std::string_view text) {
		std::uint64_t hash = text.size();
		const auto step = (hash | 32) >> 5;
		std::size_t character = 0;
		for (std::uint64_t remaining = text.size(); remaining >= step; remaining -= step) {
			const auto signed_character = std::bit_cast<std::int8_t>(static_cast<std::uint8_t>(text[character++]));
			hash ^= (hash << 5) + (hash >> 2) + static_cast<std::uint16_t>(static_cast<std::int16_t>(signed_character));
		}

		return hash;
	}

	struct SquirrelString::Storage {
		struct Registry {
			std::unordered_map<std::uint64_t, std::vector<std::weak_ptr<Storage>>> Buckets;
			std::size_t Count = 0;
			std::size_t Bytes = 0;
			bool Alive = true;
		};

		explicit Storage(std::string bytes) : Bytes(std::move(bytes)) {
		}

		~Storage() {
			const auto registry = Owner.lock();
			if (!registry) {
				return;
			}

			const auto bucket = registry->Buckets.find(Hash);
			if (bucket != registry->Buckets.end()) {
				std::erase_if(bucket->second, [](const std::weak_ptr<Storage>& entry) {
					return entry.expired();
				});
				if (bucket->second.empty()) {
					registry->Buckets.erase(bucket);
				}
			}

			--registry->Count;
			registry->Bytes -= Bytes.size();
		}

		const std::string Bytes;
		std::shared_ptr<Storage> Canonical;
		std::weak_ptr<Registry> Owner;
		std::optional<SquirrelObjectIdentity> WeakReference;
		std::uint64_t Hash = 0;
		bool Adopted = false;
	};

	SquirrelString::SquirrelString(std::string text) : m_Storage(std::make_shared<Storage>(std::move(text))) {
	}

	SquirrelString::SquirrelString(const char* text) {
		if (!text) {
			throw std::runtime_error("Squirrel host string pointer is null");
		}

		m_Storage = std::make_shared<Storage>(std::string(text));
	}

	SquirrelString::SquirrelString(std::shared_ptr<Storage> storage) : m_Storage(std::move(storage)) {
	}

	SquirrelString::SquirrelString(const SquirrelString& other) : m_Storage(other.Record()) {
	}

	SquirrelString& SquirrelString::operator=(const SquirrelString& other) {
		if (this != &other) {
			SquirrelString copy(other);
			*this = std::move(copy);
		}

		return *this;
	}

	SquirrelString::SquirrelString(SquirrelString&&) noexcept = default;
	SquirrelString& SquirrelString::operator=(SquirrelString&&) noexcept = default;
	SquirrelString::~SquirrelString() = default;

	std::shared_ptr<SquirrelString::Storage> SquirrelString::Record() const {
		const auto record = m_Storage ? m_Storage : m_BorrowedStorage.lock();
		if (!record) {
			throw std::runtime_error("Squirrel string identity expired");
		}

		return record->Canonical ? record->Canonical : record;
	}

	const std::string& SquirrelString::Text() const {
		return Record()->Bytes;
	}

	bool SquirrelString::SharesIdentity(const SquirrelString& other) const {
		return Record() == other.Record();
	}

	bool SquirrelString::operator==(const SquirrelString& other) const {
		return Text() == other.Text();
	}

	const std::string& StringText(const SquirrelValue& value) {
		return std::get<SquirrelString>(value).Text();
	}

	const std::string* TryStringText(const SquirrelValue& value) {
		const auto* string = std::get_if<SquirrelString>(&value);

		return string ? &string->Text() : nullptr;
	}

	static constexpr std::uint8_t kDiscardRegister = 255;
	static constexpr std::uint64_t kMaxArraySortComparisons = 1024 * 1024;
	static constexpr std::array<std::string_view, 19> kMetaNames{"_add", "_sub", "_mul", "_div", "_unm", "_modulo", "_set", "_get", "_typeof", "_nexti", "_cmp", "_call", "_cloned", "_newslot", "_delslot", "_tostring", "_newmember", "_inherited", "_exist"};
	static constexpr std::array<std::string_view, 15> kSharedTypeNames{"null", "table", "array", "closure", "string", "userdata", "integer", "float", "userpointer", "function", "generator", "thread", "class", "instance", "bool"};
	static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);

	struct VmNativeSignatureRecord {
		SquirrelValueType Type;
		std::string_view Name;
		std::int32_t ParameterCheck;
		std::string_view Mask;
	};

	static constexpr VmNativeSignatureRecord kNativeSignatureRecords[] = {
	    {SquirrelValueType::Table, "len", 1, "t"},
	    {SquirrelValueType::Table, "rawget", 2, "t"},
	    {SquirrelValueType::Table, "rawset", 3, "t"},
	    {SquirrelValueType::Table, "rawdelete", 2, "t"},
	    {SquirrelValueType::Table, "rawin", 2, "t"},
	    {SquirrelValueType::Table, "weakref", 1, ""},
	    {SquirrelValueType::Table, "tostring", 1, "."},
	    {SquirrelValueType::Table, "tonumber", 1, "."},
	    {SquirrelValueType::Table, "clear", 1, "."},
	    {SquirrelValueType::Table, "find", 2, ".."},
	    {SquirrelValueType::Table, "findall", 2, ".."},
	    {SquirrelValueType::Table, "includes", 2, ".."},
	    {SquirrelValueType::Table, "includeCount", 2, ".."},
	    {SquirrelValueType::Table, "keys", 1, "t"},
	    {SquirrelValueType::Array, "len", 1, "a"},
	    {SquirrelValueType::Array, "append", 2, "a"},
	    {SquirrelValueType::Array, "extend", 2, "aa"},
	    {SquirrelValueType::Array, "push", 2, "a"},
	    {SquirrelValueType::Array, "pop", 1, "a"},
	    {SquirrelValueType::Array, "top", 1, "a"},
	    {SquirrelValueType::Array, "insert", 3, "an"},
	    {SquirrelValueType::Array, "remove", 2, "an"},
	    {SquirrelValueType::Array, "erase", 2, "an"},
	    {SquirrelValueType::Array, "removeValue", 2, "a.b"},
	    {SquirrelValueType::Array, "resize", -2, "an"},
	    {SquirrelValueType::Array, "reverse", 1, "a"},
	    {SquirrelValueType::Array, "sort", -1, "ac"},
	    {SquirrelValueType::Array, "slice", -1, "ann"},
	    {SquirrelValueType::Array, "splice", -1, "ann"},
	    {SquirrelValueType::Array, "join", 2, "as"},
	    {SquirrelValueType::Array, "weakref", 1, ""},
	    {SquirrelValueType::Array, "tostring", 1, "."},
	    {SquirrelValueType::Array, "tonumber", 1, "."},
	    {SquirrelValueType::Array, "clear", 1, "."},
	    {SquirrelValueType::Array, "find", 2, ".."},
	    {SquirrelValueType::Array, "findall", 2, ".."},
	    {SquirrelValueType::Array, "includes", 2, ".."},
	    {SquirrelValueType::Array, "includeCount", 2, ".."},
	    {SquirrelValueType::String, "len", 1, "s"},
	    {SquirrelValueType::String, "tointeger", 1, "s"},
	    {SquirrelValueType::String, "tofloat", 1, "s"},
	    {SquirrelValueType::String, "tostring", 1, "."},
	    {SquirrelValueType::String, "tonumber", 1, "."},
	    {SquirrelValueType::String, "slice", -1, "snn"},
	    {SquirrelValueType::String, "find", -2, "ssn"},
	    {SquirrelValueType::String, "rfind", -2, "ssn"},
	    {SquirrelValueType::String, "tolower", 1, "s"},
	    {SquirrelValueType::String, "toupper", 1, "s"},
	    {SquirrelValueType::String, "weakref", 1, ""},
	    {SquirrelValueType::String, "substr", -1, "snn"},
	    {SquirrelValueType::String, "charAt", -1, "sn"},
	    {SquirrelValueType::String, "replace", 3, "sss"},
	    {SquirrelValueType::String, "split", 2, "ss"},
	    {SquirrelValueType::String, "mbnext", -1, "sn"},
	    {SquirrelValueType::String, "mblen", 1, "s"},
	    {SquirrelValueType::String, "mbsubstr", -1, "snn"},
	    {SquirrelValueType::Integer, "tointeger", 1, "n|b"},
	    {SquirrelValueType::Integer, "tofloat", 1, "n|b"},
	    {SquirrelValueType::Integer, "tostring", 1, "."},
	    {SquirrelValueType::Integer, "tonumber", 1, "."},
	    {SquirrelValueType::Integer, "tochar", 1, "n|b"},
	    {SquirrelValueType::Integer, "weakref", 1, ""},
	    {SquirrelValueType::Generator, "getstatus", 1, "g"},
	    {SquirrelValueType::Generator, "weakref", 1, ""},
	    {SquirrelValueType::Generator, "tostring", 1, "."},
	    {SquirrelValueType::Generator, "tonumber", 1, "."},
	    {SquirrelValueType::Closure, "call", -1, "c"},
	    {SquirrelValueType::Closure, "pcall", -1, "c"},
	    {SquirrelValueType::Closure, "acall", 2, "ca"},
	    {SquirrelValueType::Closure, "pacall", 2, "ca"},
	    {SquirrelValueType::Closure, "weakref", 1, ""},
	    {SquirrelValueType::Closure, "tostring", 1, "."},
	    {SquirrelValueType::Closure, "tonumber", 1, "."},
	    {SquirrelValueType::Closure, "bindenv", 2, "c x|y|t"},
	    {SquirrelValueType::Closure, "getenv", 1, "c"},
	    {SquirrelValueType::Closure, "getinfos", 1, "c"},
	    {SquirrelValueType::Class, "getattributes", 2, "y."},
	    {SquirrelValueType::Class, "setattributes", 3, "y.."},
	    {SquirrelValueType::Class, "rawin", 2, "y"},
	    {SquirrelValueType::Class, "weakref", 1, ""},
	    {SquirrelValueType::Class, "tostring", 1, "."},
	    {SquirrelValueType::Class, "tonumber", 1, "."},
	    {SquirrelValueType::Class, "instance", 1, "y"},
	    {SquirrelValueType::Instance, "getclass", 1, "x"},
	    {SquirrelValueType::Instance, "rawin", 2, "x"},
	    {SquirrelValueType::Instance, "weakref", 1, ""},
	    {SquirrelValueType::Instance, "tostring", 1, "."},
	    {SquirrelValueType::Instance, "tonumber", 1, "."},
	    {SquirrelValueType::WeakReference, "ref", 1, "r"},
	    {SquirrelValueType::WeakReference, "weakref", 1, ""},
	    {SquirrelValueType::WeakReference, "tostring", 1, "."},
	    {SquirrelValueType::WeakReference, "tonumber", 1, "."},
	};

	static constexpr VmNativeSignatureRecord kNativeBaseSignatureRecords[] = {
	    {SquirrelValueType::NativeClosure, "seterrorhandler", 2, ""},
	    {SquirrelValueType::NativeClosure, "setdebughook", 2, ""},
	    {SquirrelValueType::NativeClosure, "enabledebuginfo", 2, ""},
	    {SquirrelValueType::NativeClosure, "getstackinfos", 2, ".n"},
	    {SquirrelValueType::NativeClosure, "getroottable", 1, ""},
	    {SquirrelValueType::NativeClosure, "setroottable", 2, ""},
	    {SquirrelValueType::NativeClosure, "getconsttable", 1, ""},
	    {SquirrelValueType::NativeClosure, "setconsttable", 2, ""},
	    {SquirrelValueType::NativeClosure, "setexceptionclass", 2, ""},
	    {SquirrelValueType::NativeClosure, "assert", 2, ""},
	    {SquirrelValueType::NativeClosure, "print", 2, ""},
	    {SquirrelValueType::NativeClosure, "errprint", 2, ""},
	    {SquirrelValueType::NativeClosure, "compilestring", -2, ".ss"},
	    {SquirrelValueType::NativeClosure, "strip", 2, ".s"},
	    {SquirrelValueType::NativeClosure, "lstrip", 2, ".s"},
	    {SquirrelValueType::NativeClosure, "rstrip", 2, ".s"},
	    {SquirrelValueType::NativeClosure, "split", 3, ".ss"},
	    {SquirrelValueType::NativeClosure, "newthread", 2, ".c"},
	    {SquirrelValueType::NativeClosure, "suspend", -1, ""},
	    {SquirrelValueType::NativeClosure, "array", -2, ".n"},
	    {SquirrelValueType::NativeClosure, "type", 2, ""},
	    {SquirrelValueType::NativeClosure, "dummy", 0, ""},
	    {SquirrelValueType::NativeClosure, "collectgarbage", 1, "."},
	    {SquirrelValueType::NativeClosure, "execstring", -1, ".so|x|y|t."},
	    {SquirrelValueType::NativeClosure, "evalstring", -1, "..o|x|y|t."},
	    {SquirrelValueType::NativeClosure, "template", -1, ".so|x|y|t"},
	};

	static std::int32_t NativeTypeMask(char token) {
		switch (token) {
		case 'o':
			return 1;
		case 'i':
			return 2;
		case 'f':
			return 4;
		case 'n':
			return 6;
		case 'b':
			return 8;
		case 's':
			return 16;
		case 't':
			return 32;
		case 'a':
			return 64;
		case 'u':
			return 128;
		case 'c':
			return 768;
		case 'g':
			return 1024;
		case 'p':
			return 2048;
		case 'v':
			return 4096;
		case 'y':
			return 16384;
		case 'x':
			return 32768;
		case 'r':
			return 65536;
		default:
			throw std::runtime_error("invalid native parameter mask token");
		}
	}

	static SquirrelNativeSignature NativeSignature(std::string_view name, std::int32_t parameter_check, std::string_view mask) {
		SquirrelNativeSignature signature;
		signature.Name = std::string(name);
		signature.ParameterCheck = parameter_check;
		std::size_t position = 0;
		while (position < mask.size()) {
			const auto token = mask[position++];
			if (token == ' ') {
				continue;
			}

			if (token == '.') {
				signature.TypeCheck.push_back(-1);
				continue;
			}

			auto bits = NativeTypeMask(token);
			while (position < mask.size() && mask[position] == '|') {
				++position;
				if (position == mask.size()) {
					throw std::runtime_error("native parameter mask has an unfinished union");
				}

				bits |= NativeTypeMask(mask[position++]);
			}

			signature.TypeCheck.push_back(bits);
		}

		return signature;
	}

	static SquirrelNativeSignature RecoveredDelegateSignature(SquirrelValueType type, std::string_view name) {
		const auto category = type == SquirrelValueType::Float || type == SquirrelValueType::Boolean ? SquirrelValueType::Integer : type == SquirrelValueType::NativeClosure ? SquirrelValueType::Closure
		                                                                                                                                                                     : type;
		for (const auto& record : kNativeSignatureRecords) {
			if (record.Type == category && record.Name == name) {
				return NativeSignature(name, record.ParameterCheck, record.Mask);
			}
		}

		throw std::runtime_error("delegate has no recovered native signature");
	}

	static std::size_t NativeMultibyteWidth(std::string_view text, std::size_t position) {
		if (position >= text.size() || text[position] == '\0') {
			return 0;
		}

		const auto lead = static_cast<std::uint8_t>(text[position]);
		if (lead < 128) {
			return 1;
		}

		if ((lead & 192) == 128) {
			std::size_t width = 1;
			while (width < text.size() - position && (static_cast<std::uint8_t>(text[position + width]) & 192) == 128) {
				++width;
			}

			return width;
		}

		if ((lead & 224) == 192) {
			return 2;
		}

		if ((lead & 240) == 224) {
			return 3;
		}

		if ((lead & 248) == 240) {
			return 4;
		}

		if ((lead & 254) == 248) {
			return 5;
		}

		return lead == 254 ? 6 : 1;
	}

	static std::int32_t NativeMultibyteLength(std::string_view text) {
		std::size_t position = 0;
		std::int32_t count = 0;
		while (position < text.size()) {
			const auto width = NativeMultibyteWidth(text, position);
			if (width == 0) {
				break;
			}

			position += width;
			++count;
		}

		return count;
	}

	static SquirrelValue LiteralValue(const SquirrelLiteral& literal) {
		return std::visit([](const auto& value) -> SquirrelValue {
			return value;
		},
		    literal);
	}

	static std::string_view LiteralText(const SquirrelLiteral& literal) {
		const auto* text = std::get_if<std::string>(&literal);

		return text ? std::string_view(*text) : std::string_view{};
	}

	static bool IsTrue(const SquirrelValue& value) {
		if (std::holds_alternative<std::monostate>(value)) {
			return false;
		}

		if (const auto* integer = std::get_if<std::int32_t>(&value)) {
			return *integer != 0;
		}

		if (const auto* number = std::get_if<float>(&value)) {
			return *number != 0.0f;
		}

		if (const auto* boolean = std::get_if<bool>(&value)) {
			return *boolean;
		}

		return true;
	}

	static bool IsNumber(const SquirrelValue& value) {
		return std::holds_alternative<std::int32_t>(value) || std::holds_alternative<float>(value);
	}

	static float NumberFloat(const SquirrelValue& value) {
		if (const auto* integer = std::get_if<std::int32_t>(&value)) {
			return static_cast<float>(*integer);
		}

		if (const auto* number = std::get_if<float>(&value)) {
			return *number;
		}

		throw std::runtime_error("expected a numeric value");
	}

	static std::int32_t IntegerValue(const SquirrelValue& value) {
		const auto* integer = std::get_if<std::int32_t>(&value);
		if (!integer) {
			throw std::runtime_error("expected an integer value");
		}

		return *integer;
	}

	static std::int32_t NumericIndex(const SquirrelValue& value) {
		if (const auto* integer = std::get_if<std::int32_t>(&value)) {
			return *integer;
		}

		const auto* number = std::get_if<float>(&value);
		if (!number) {
			throw std::runtime_error("index is not numeric");
		}

		// CVTTSS2SI returns the integer-indefinite value for an out-of-range or NaN operand.
		if (!std::isfinite(*number) || *number < -2147483648.0f || *number >= 2147483648.0f) {
			return std::numeric_limits<std::int32_t>::min();
		}

		return static_cast<std::int32_t>(*number);
	}

	std::optional<std::int32_t> TryNativeInteger(const SquirrelValue& value) {
		if (const auto* integer = std::get_if<std::int32_t>(&value)) {
			return *integer;
		}

		if (const auto* boolean = std::get_if<bool>(&value)) {
			return static_cast<std::int32_t>(*boolean);
		}

		if (const auto* number = std::get_if<float>(&value)) {
			if (!std::isfinite(*number) || *number < -2147483648.0f || *number >= 2147483648.0f) {
				return std::numeric_limits<std::int32_t>::min();
			}

			return static_cast<std::int32_t>(*number);
		}

		return {};
	}

	static bool EqualValues(const SquirrelValue& left, const SquirrelValue& right) {
		if (left.index() == right.index()) {
			if (const auto* number = std::get_if<float>(&left)) {
				return std::bit_cast<std::uint32_t>(*number) == std::bit_cast<std::uint32_t>(std::get<float>(right));
			}

			return left == right;
		}

		if (IsNumber(left) && IsNumber(right)) {
			return NumberFloat(left) == NumberFloat(right);
		}

		return false;
	}

	static bool EqualKeys(const SquirrelValue& left, const SquirrelValue& right) {
		return left.index() == right.index() && EqualValues(left, right);
	}

	static std::int32_t WrapInteger(std::uint32_t bits) {
		return std::bit_cast<std::int32_t>(bits);
	}

	struct VmSafetyFailure : std::runtime_error {
		explicit VmSafetyFailure(std::string message) : std::runtime_error(std::move(message)) {
		}
	};

	struct VmComparisonResultError : std::runtime_error {
		explicit VmComparisonResultError(std::string message) : std::runtime_error(std::move(message)) {
		}
	};

	struct VmThrownValue {
		SquirrelValue Value;
	};

	struct VmTrap {
		std::size_t Instruction = 0;
		std::size_t Register = 0;
		OperandFrameState StackState;
	};

	struct VmArraySortRange {
		std::int32_t First = 0;
		std::int32_t Last = 0;
	};

	enum class VmReturnKind {
		Value,
		Capture,
		Override,
		Order,
		Equality,
		IteratorKey,
		IteratorValue,
		IteratorYield,
		ClassInherited,
		NewMember,
		CompoundRead,
		CompoundArithmetic,
		CompoundSet,
		AssignLocal,
		Exists,
		SlotRead,
		SlotWrite,
		GetterOutput,
		PrepareCall
	};

	struct VmClassMember {
		SquirrelValue Receiver;
		SquirrelValue Key;
		SquirrelValue Value;
		SquirrelValue Attributes;
		bool IsStatic = false;
		bool HasAttributes = false;
	};

	struct VmCompoundSlot {
		SquirrelValue Receiver;
		SquirrelValue Key;
		SquirrelValue Operand;
		SquirrelValue Previous;
		SquirrelValue Computed;
		std::uint8_t Operation = '+';
		std::uint8_t ResultRegister = kDiscardRegister;
		bool PostIncrement = false;
	};

	struct VmGetterOutput {
		SquirrelValue* Value = nullptr;
		std::optional<std::size_t> PhysicalCell;
		bool Completed = false;
		bool Succeeded = false;
	};

	enum class VmDelegatedReadPhase {
		OwnSlot,
		DelegateResult,
		Metamethod,
		Default
	};

	struct VmDelegatedReadLevel {
		SquirrelValue Receiver;
		std::optional<SquirrelObjectHandle> DelegatedLocal;
		VmDelegatedReadPhase Phase = VmDelegatedReadPhase::OwnSlot;
	};

	struct VmReturnAction {
		VmReturnKind Kind = VmReturnKind::Value;
		std::size_t Register = kDiscardRegister;
		std::uint8_t Operation = 0;
		SquirrelValue Operand;
		std::optional<SquirrelValue> InputOverride;
		std::int32_t BranchDisplacement = 0;
		std::string FailureOverride;
		std::optional<VmClassMember> ClassMember;
		std::optional<VmCompoundSlot> Compound;
		std::size_t SecondaryRegister = 0;
		SquirrelValue Key;
		std::shared_ptr<const VmReturnAction> FollowingAction;
		bool UsesNativeTemporary = false;
		std::shared_ptr<VmGetterOutput> GetterOutput;
	};

	struct VmFrame {
		SquirrelObjectHandle Closure;
		FrameRegisters Registers;
		OperandFrameState StackState;
		std::optional<std::size_t> ArgumentCleanupTop;
		std::size_t PreparedParameters = 0;
		bool PreparedVariableCells = false;
		std::vector<SquirrelValue> VariableArguments;
		std::vector<VmTrap> Traps;
		std::size_t Instruction = 0;
		std::uint8_t ReturnRegister = kDiscardRegister;
		std::optional<SquirrelValue> ReturnOverride;
		std::optional<VmReturnAction> ReturnAction;
		std::optional<SquirrelObjectHandle> Generator;
		bool IsMetamethod = false;
		bool ExecuteRoot = false;
	};

	class VmOperandNativeScope {
	public:
		VmOperandNativeScope(OperandStack& stack, OperandFrameState previous, std::optional<std::size_t> cleanup_top, bool& physical_stack_active) : m_Stack(stack), m_Previous(previous), m_CleanupTop(cleanup_top), m_PhysicalStackActive(physical_stack_active) {
		}

		~VmOperandNativeScope() {
			Restore();
		}
		VmOperandNativeScope(const VmOperandNativeScope&) = delete;
		VmOperandNativeScope& operator=(const VmOperandNativeScope&) = delete;

		void Restore() {
			if (m_Restored) {
				return;
			}

			m_Restored = true;
			m_PhysicalStackActive = false;
			m_Stack.LeaveNativeFrame(m_Previous);
			if (m_CleanupTop) {
				m_Stack.SetTop(*m_CleanupTop - m_Stack.Base());
			}
		}

	private:
		OperandStack& m_Stack;
		OperandFrameState m_Previous;
		std::optional<std::size_t> m_CleanupTop;
		bool& m_PhysicalStackActive;
		bool m_Restored = false;
	};

	struct VmNativeFrame {
		SquirrelObjectHandle Closure;
		std::vector<SquirrelValue> Arguments;
		std::size_t ScriptDepth = 0;
		std::size_t PhysicalBase = 0;
		bool PhysicalStackActive = false;
	};

	static std::size_t MaximumOperandCells(std::size_t maximum_registers) {
		static constexpr std::size_t kInitialOperandCells = 1024;
		if (maximum_registers > (std::numeric_limits<std::size_t>::max() - kInitialOperandCells) / 3) {
			throw std::runtime_error("Squirrel physical operand bound overflows");
		}

		return kInitialOperandCells + maximum_registers * 3;
	}

	struct VmExecutionContext : VmCollectableLinks {
		explicit VmExecutionContext(std::size_t maximum_registers) : Stack(1024, MaximumOperandCells(maximum_registers)) {
		}

		OperandStack Stack;
		std::deque<VmFrame> Frames;
		std::deque<VmNativeFrame> NativeFrames;
		std::vector<SquirrelValue> TemporaryRoots;
		bool Initializing = false;
		SquirrelValue Result;
		SquirrelValue NativeTemporary;
		SquirrelValue RootTable;
		SquirrelValue ErrorHandler;
		SquirrelValue LastError;
		bool RaiseErrors = true;
		bool HandlingError = false;
		bool Suspended = false;
		std::uint8_t SuspendedRegister = kDiscardRegister;
		std::optional<VmReturnAction> SuspendedAction;
		std::size_t InvocationDepth = 0;
		std::size_t NativeCallDepth = 0;
		std::size_t InvocationResultPopDepth = 0;
		std::size_t CompletedResultObserverDepth = 0;
		std::size_t PropertyReadDepth = 0;
		std::size_t FinalizationDepth = 0;
	};

	class VmPropertyReadScope {
	public:
		explicit VmPropertyReadScope(VmExecutionContext& context)
		    : m_Context(context) {
			++m_Context.PropertyReadDepth;
		}
		~VmPropertyReadScope() {
			--m_Context.PropertyReadDepth;
		}
		VmPropertyReadScope(const VmPropertyReadScope&) = delete;
		VmPropertyReadScope& operator=(const VmPropertyReadScope&) = delete;

	private:
		VmExecutionContext& m_Context;
	};

	class VmCompletedResultObserverScope {
	public:
		explicit VmCompletedResultObserverScope(VmExecutionContext& target) : m_Target(target) {
			++m_Target.CompletedResultObserverDepth;
		}

		~VmCompletedResultObserverScope() {
			--m_Target.CompletedResultObserverDepth;
		}
		VmCompletedResultObserverScope(const VmCompletedResultObserverScope&) = delete;
		VmCompletedResultObserverScope& operator=(const VmCompletedResultObserverScope&) = delete;

	private:
		VmExecutionContext& m_Target;
	};

	class VmInvocationResultPopScope {
	public:
		VmInvocationResultPopScope(VmExecutionContext& target, VmExecutionContext& caller) : m_Target(target), m_Caller(caller) {
			++m_Target.InvocationResultPopDepth;
			++m_Caller.InvocationResultPopDepth;
		}

		~VmInvocationResultPopScope() {
			--m_Caller.InvocationResultPopDepth;
			--m_Target.InvocationResultPopDepth;
		}
		VmInvocationResultPopScope(const VmInvocationResultPopScope&) = delete;
		VmInvocationResultPopScope& operator=(const VmInvocationResultPopScope&) = delete;

	private:
		VmExecutionContext& m_Target;
		VmExecutionContext& m_Caller;
	};

	class VmReadContextScope {
	public:
		VmReadContextScope(std::uint32_t& active, std::uint32_t selected) : m_Active(active), m_Previous(active) {
			m_Active = selected;
		}

		~VmReadContextScope() {
			m_Active = m_Previous;
		}
		VmReadContextScope(const VmReadContextScope&) = delete;
		VmReadContextScope& operator=(const VmReadContextScope&) = delete;

	private:
		std::uint32_t& m_Active;
		std::uint32_t m_Previous = 0;
	};

	class VmGetterInvocationScope {
	public:
		VmGetterInvocationScope(bool& running, std::size_t& invocation_depth, VmExecutionContext& context, std::size_t& meta_depth) : m_Running(running), m_InvocationDepth(invocation_depth), m_Context(context), m_MetaDepth(meta_depth), m_PreviouslyRunning(running), m_HostCall(!running || (context.InvocationDepth == 0 && context.Frames.empty() && context.NativeFrames.empty())) {
			++m_MetaDepth;
			if (m_HostCall) {
				m_Running = true;
				++m_InvocationDepth;
				++m_Context.InvocationDepth;
			}
		}

		~VmGetterInvocationScope() {
			Restore();
		}

		void Restore() {
			if (!m_Active) {
				return;
			}

			m_Active = false;
			--m_MetaDepth;
			if (m_HostCall) {
				--m_Context.InvocationDepth;
				--m_InvocationDepth;
				m_Running = m_PreviouslyRunning;
			}
		}
		VmGetterInvocationScope(const VmGetterInvocationScope&) = delete;
		VmGetterInvocationScope& operator=(const VmGetterInvocationScope&) = delete;

	private:
		bool& m_Running;
		std::size_t& m_InvocationDepth;
		VmExecutionContext& m_Context;
		std::size_t& m_MetaDepth;
		bool m_PreviouslyRunning = false;
		bool m_HostCall = false;
		bool m_Active = true;
	};

	class VmTemporaryRootScope {
	public:
		VmTemporaryRootScope(VmExecutionContext& context, const SquirrelValue& value) : m_Context(context) {
			m_Context.TemporaryRoots.push_back(value);
		}

		~VmTemporaryRootScope() {
			SquirrelValue previous(std::move(m_Context.TemporaryRoots.back()));
			m_Context.TemporaryRoots.pop_back();
		}
		VmTemporaryRootScope(const VmTemporaryRootScope&) = delete;
		VmTemporaryRootScope& operator=(const VmTemporaryRootScope&) = delete;

	private:
		VmExecutionContext& m_Context;
	};

	class VmNativeCallScope {
	public:
		explicit VmNativeCallScope(VmExecutionContext& context) : m_Context(context) {
			++m_Context.NativeCallDepth;
		}

		~VmNativeCallScope() {
			Restore();
		}
		void Restore() {
			if (m_Active) {
				--m_Context.NativeCallDepth;
				m_Active = false;
			}
		}
		VmNativeCallScope(const VmNativeCallScope&) = delete;
		VmNativeCallScope& operator=(const VmNativeCallScope&) = delete;

	private:
		VmExecutionContext& m_Context;
		bool m_Active = true;
	};

	class VmNativeFrameScope {
	public:
		VmNativeFrameScope(VmExecutionContext& context, SquirrelObjectHandle closure, std::span<const SquirrelValue> arguments) : m_Context(context) {
			VmNativeFrame frame;
			frame.Closure = closure;
			frame.Arguments.assign(arguments.begin(), arguments.end());
			frame.ScriptDepth = context.Frames.size();
			context.NativeFrames.push_back(std::move(frame));
		}

		~VmNativeFrameScope() {
			Restore();
		}

		void Restore() {
			if (m_Active) {
				m_Active = false;
				m_Context.NativeFrames.pop_back();
			}
		}
		VmNativeFrameScope(const VmNativeFrameScope&) = delete;
		VmNativeFrameScope& operator=(const VmNativeFrameScope&) = delete;

	private:
		VmExecutionContext& m_Context;
		bool m_Active = true;
	};

	struct VmSlot {
		SquirrelValue Key;
		SquirrelValue Value;
		SquirrelValue Attributes;
		bool IsStatic = false;
		bool IsField = false;
		std::optional<std::uint32_t> ClassOrdinal;
		std::optional<std::size_t> Next;
	};

	struct VmBorrowedObject {
		std::uint32_t Index = 0;
		std::uint64_t Generation = 0;
		const void* Owner = nullptr;
	};

	static VmBorrowedObject BorrowedIdentity(const SquirrelObjectHandle& handle) {
		VmBorrowedObject identity;
		identity.Index = handle.Index;
		identity.Generation = handle.Generation;
		identity.Owner = handle.Owner;

		return identity;
	}

	struct VmReleaseCallbackScope {
		explicit VmReleaseCallbackScope(std::size_t& depth) : Depth(depth) {
			++Depth;
		}

		~VmReleaseCallbackScope() {
			--Depth;
		}

		std::size_t& Depth;
	};

	struct VmMarkFrame {
		std::uint32_t Index = 0;
		std::size_t Stage = 0;
		std::size_t Member = 0;
		std::size_t MemberPart = 0;
	};

	struct VmReleaseFrame {
		std::uint32_t Index = 0;
		std::size_t Stage = 0;
		std::size_t Member = 0;
		std::size_t MemberPart = 0;
		bool Collection = false;
		bool Resurrected = false;
	};

	struct VmFunctionPrototype {
		SquirrelValue SourceName;
		SquirrelValue Name;
		std::vector<SquirrelValue> Literals;
		std::vector<SquirrelValue> Metadata;
		std::vector<std::shared_ptr<const VmFunctionPrototype>> Children;
	};

	struct VmObject {
		SquirrelValueType Type = SquirrelValueType::Table;
		std::vector<VmSlot> Slots;
		std::vector<SquirrelValue> Elements;
		std::optional<SquirrelObjectHandle> Delegate;
		std::optional<SquirrelObjectHandle> ParentClass;
		std::optional<SquirrelObjectHandle> MemberIndex;
		std::array<SquirrelValue, kMetaNames.size()> MetaMethods;
		std::optional<SquirrelObjectHandle> InstanceClass;
		std::optional<SquirrelObjectHandle> BoundEnvironment;
		std::optional<VmBorrowedObject> WeakTarget;
		std::weak_ptr<void> WeakStringTarget;
		std::optional<VmBorrowedObject> WeakReference;
		std::optional<SquirrelInstanceHostIdentity> HostIdentity;
		std::shared_ptr<const BytecodeProgram> Program;
		std::shared_ptr<const VmFunctionPrototype> Prototype;
		SquirrelValue NativeSignatureName;
		const SquirrelFunction* Function = nullptr;
		std::vector<SquirrelValue> Captures;
		std::vector<SquirrelValue> Defaults;
		SquirrelValue Attributes;
		std::string NativeName;
		SquirrelNativeSignature Signature;
		SquirrelVm::NativeCallback Native;
		SquirrelVm::InstanceReleaseCallback ReleaseHook;
		std::vector<std::uint8_t> UserDataPayload;
		SquirrelVm::UserDataReleaseCallback UserDataHook;
		const void* UserDataTag = nullptr;
		const void* ClassTag = nullptr;
		std::optional<VmFrame> GeneratorFrame;
		std::optional<SquirrelObjectHandle> GeneratorClosure;
		bool GeneratorDead = false;
		bool GeneratorRunning = false;
		bool ClassLocked = false;
		std::size_t TableUsed = 0;
		std::size_t TableFree = 0;
		bool TablePointerKeys = false;
		bool ArrayClearing = false;
	};

	struct VmHostRootEntry {
		SquirrelValue Value;
		std::uint32_t References = 0;
	};

	struct VmHostRootRegistry {
		std::vector<VmHostRootEntry> Entries;
		std::vector<std::size_t> FreeEntries;
	};

	struct SquirrelHostRoot::Implementation {
		std::weak_ptr<VmHostRootRegistry> Owner;
		SquirrelValue BorrowedValue;
		std::optional<std::size_t> Entry;

		~Implementation() {
			const auto owner = Owner.lock();
			if (!owner || !Entry) {
				return;
			}

			auto& entry = owner->Entries[*Entry];
			if (entry.References == 0) {
				return;
			}

			--entry.References;
			if (entry.References == 0) {
				owner->FreeEntries.push_back(*Entry);
				ReleaseStoredValue(entry.Value);
			}
		}
	};

	class SquirrelVm::Implementation {
	public:
		Implementation(SquirrelVm& api, SquirrelVmLimits limits) : m_Api(api), m_Limits(limits) {
			if (limits.MaxObjects == 0 || limits.MaxObjects > std::numeric_limits<std::uint32_t>::max() || limits.MaxContexts == 0 || limits.MaxContexts > std::numeric_limits<std::uint32_t>::max() || limits.MaxFrames == 0 || limits.MaxRegisters == 0) {
				throw std::runtime_error("invalid Squirrel VM limits");
			}

			m_BorrowedReceiverScopes.reserve(kMaxBorrowedReceiverDepth);
			m_PendingReleases.reserve(m_Limits.MaxObjects);
			m_ReleaseFrames.reserve(m_Limits.MaxObjects);
			m_FreeObjects.reserve(m_Limits.MaxObjects);
			m_FreeContexts.reserve(m_Limits.MaxContexts);
			m_PendingMarks.reserve(m_Limits.MaxObjects);
			m_HostRoots->FreeEntries.reserve(m_Limits.MaxObjects);
			static_cast<void>(CreateContext());
			Active().RootTable = Allocate(SquirrelValueType::Table);
			m_ConstantTable = Allocate(SquirrelValueType::Table);
			for (const auto name : kSharedTypeNames) {
				m_SharedTypeNames.push_back(InternedValue(std::string(name)));
			}

			for (const auto name : kMetaNames) {
				m_SharedMetaNames.push_back(InternedValue(std::string(name)));
			}

			m_ConstructorName = InternedValue(std::string("constructor"));
		}

		~Implementation() {
			try {
				Close();
			} catch (...) {
				m_ReleaseFailure = std::current_exception();
				m_LifetimeOwner.reset();
			}
		}

		static void QueueLastRelease(void* context, std::uint32_t index, std::uint64_t generation) noexcept {
			auto& owner = *static_cast<Implementation*>(context);
			if (index >= owner.m_ObjectLifetimes.size()) {
				return;
			}

			const auto& record = owner.m_ObjectLifetimes[index];
			if (record->Generation == generation && record->Alive && !record->Finalizing && !record->ReleaseQueued && owner.m_Limits.ReclaimObjects) {
				record->ReleaseQueued = true;
				owner.m_PendingReleases.push_back(index);
				owner.DrainReleases();
			}
		}

		static bool IsCollectable(SquirrelValueType type) {
			return type != SquirrelValueType::WeakReference;
		}

		VmCollectableIdentity ObjectIdentity(std::uint32_t index) const noexcept {
			return {VmCollectableKind::Object, index, m_ObjectLifetimes[index]->Generation};
		}

		VmCollectableIdentity ContextIdentity(std::uint32_t index) const noexcept {
			return {VmCollectableKind::Context, index, m_ContextGenerations[index]};
		}

		bool IsLiveCollectable(const VmCollectableIdentity& identity) const noexcept {
			if (identity.Kind == VmCollectableKind::Object) {
				return identity.Index < m_ObjectLifetimes.size() && m_ObjectLifetimes[identity.Index]->Alive && m_ObjectLifetimes[identity.Index]->Generation == identity.Generation;
			}

			return identity.Index < m_Contexts.size() && m_Contexts[identity.Index] && m_ContextGenerations[identity.Index] == identity.Generation;
		}

		VmCollectableLinks& CollectableLinks(const VmCollectableIdentity& identity) const noexcept {
			if (identity.Kind == VmCollectableKind::Object) {
				return *m_ObjectLifetimes[identity.Index];
			}

			return *m_Contexts[identity.Index];
		}

		void LinkCollectable(const VmCollectableIdentity& identity) noexcept {
			auto& record = CollectableLinks(identity);
			record.GarbageTracked = true;
			record.GarbagePrevious.reset();
			record.GarbageNext = m_GarbageHead;
			if (m_GarbageHead) {
				CollectableLinks(*m_GarbageHead).GarbagePrevious = identity;
			}

			m_GarbageHead = identity;
		}

		void UnlinkCollectable(const VmCollectableIdentity& identity) noexcept {
			auto& record = CollectableLinks(identity);
			if (!record.GarbageTracked) {
				return;
			}

			if (record.GarbagePrevious) {
				CollectableLinks(*record.GarbagePrevious).GarbageNext = record.GarbageNext;
			} else {
				(record.Marked ? m_MarkedHead : m_GarbageHead) = record.GarbageNext;
			}

			if (record.GarbageNext) {
				CollectableLinks(*record.GarbageNext).GarbagePrevious = record.GarbagePrevious;
			}

			record.GarbageTracked = false;
			record.GarbagePrevious.reset();
			record.GarbageNext.reset();
		}

		void LinkGarbageObject(std::uint32_t index) noexcept {
			LinkCollectable(ObjectIdentity(index));
		}

		void UnlinkGarbageObject(std::uint32_t index) noexcept {
			UnlinkCollectable(ObjectIdentity(index));
		}

		static void ReleaseValue(SquirrelValue& target) noexcept {
			ReleaseStoredValue(target);
		}

		bool IsLiveIdentity(const VmBorrowedObject& identity) const noexcept {
			return identity.Owner == this && identity.Index < m_ObjectLifetimes.size() && m_ObjectLifetimes[identity.Index]->Alive && identity.Generation == m_ObjectLifetimes[identity.Index]->Generation;
		}

		void DeleteObject(std::uint32_t index) noexcept {
			auto& record = *m_ObjectLifetimes[index];
			auto& object = *m_Objects[index];
			if (object.WeakReference && IsLiveIdentity(*object.WeakReference)) {
				m_Objects[object.WeakReference->Index]->WeakTarget.reset();
			}

			if (object.Type == SquirrelValueType::WeakReference && object.WeakTarget && IsLiveIdentity(*object.WeakTarget)) {
				m_Objects[object.WeakTarget->Index]->WeakReference.reset();
			}

			if (const auto raw = object.WeakStringTarget.lock()) {
				const auto string = std::static_pointer_cast<SquirrelString::Storage>(raw);
				string->WeakReference.reset();
			}

			UnlinkGarbageObject(index);
			record.Alive = false;
			record.Finalizing = false;
			record.ReleaseQueued = false;
			m_UserDataBytes -= object.UserDataPayload.size();
			m_Objects[index].reset();
			m_FreeObjects.push_back(index);
			--m_LiveObjects;
		}

		void ReleaseDelegateReference(VmObject& object) noexcept {
			if (object.Delegate) {
				SquirrelValue previous = std::move(*object.Delegate);
				object.Delegate = std::get<SquirrelObjectHandle>(BorrowedValue(previous));
			}
		}

		bool ReleaseTableMember(VmObject& object, VmReleaseFrame& frame) noexcept {
			if (frame.Stage == 0) {
				frame.Stage = 1;
				if (!frame.Collection) {
					ReleaseDelegateReference(object);

					return true;
				}
			}

			if (frame.Stage == 1) {
				if (!frame.Collection) {
					object.Delegate.reset();
				}

				if (frame.Member < object.Slots.size()) {
					auto& slot = object.Slots[frame.Member];
					auto& value = frame.MemberPart == 0 ? slot.Key : slot.Value;
					if (frame.MemberPart++ != 0) {
						frame.MemberPart = 0;
						++frame.Member;
					}

					// Native finalization clears cells; ordinary destruction releases their raw references.
					if (frame.Collection) {
						ReleaseValue(value);
					} else {
						ReleaseRawValue(value);
					}

					return true;
				}

				frame.Stage = 2;
			}

			if (frame.Stage == 2) {
				frame.Stage = 3;
				if (frame.Collection) {
					ReleaseDelegateReference(object);

					return true;
				}
			}

			object.Delegate.reset();

			return false;
		}

		bool ReleaseClassMember(VmObject& object, VmReleaseFrame& frame) noexcept {
			if (frame.Stage == 0) {
				frame.Stage = 1;
				ReleaseValue(object.Attributes);

				return true;
			}

			if (frame.Stage == 1 || frame.Stage == 2) {
				const bool field = frame.Stage == 1;
				while (frame.Member < object.Slots.size() && object.Slots[frame.Member].IsField != field) {
					++frame.Member;
				}

				if (frame.Member < object.Slots.size()) {
					auto& slot = object.Slots[frame.Member];
					if (frame.MemberPart == 0) {
						frame.MemberPart = 1;
						ReleaseValue(slot.Attributes);
					} else {
						frame.MemberPart = 0;
						++frame.Member;
						ReleaseValue(slot.Value);
					}

					return true;
				}

				++frame.Stage;
				frame.Member = 0;

				return true;
			}

			if (frame.Stage == 3) {
				if (frame.Member < object.MetaMethods.size()) {
					ReleaseValue(object.MetaMethods[frame.Member++]);

					return true;
				}

				frame.Stage = 4;
			}

			if (frame.Stage == 4) {
				frame.Stage = 5;
				object.MemberIndex.reset();

				return true;
			}

			if (frame.Stage == 5) {
				frame.Stage = 6;
				object.ParentClass.reset();

				return true;
			}

			return false;
		}

		bool ReleaseUserDataMember(VmObject& object, VmReleaseFrame& frame) noexcept {
			if (frame.Stage == 0) {
				frame.Stage = 1;
				if (!frame.Collection) {
					auto hook = std::move(object.UserDataHook);
					object.UserDataHook = {};
					if (hook) {
						try {
							if (m_ReleaseCallbackDepth >= m_Limits.MaxFrames) {
								throw std::runtime_error("Squirrel native userdata release callback depth exceeds limit");
							}

							VmReleaseCallbackScope callback_scope(m_ReleaseCallbackDepth);
							const SquirrelValue receiver = Handle(frame.Index, true);
							hook(m_Api, receiver, object.UserDataPayload);
						} catch (...) {
							m_ReleaseFailure = std::current_exception();
						}

						object.UserDataHook = {};
					}

					return true;
				}
			}

			if (frame.Stage == 1) {
				frame.Stage = 2;
				ReleaseDelegateReference(object);

				return true;
			}

			object.Delegate.reset();

			return false;
		}

		bool ReleaseInstanceMember(VmObject& object, VmReleaseFrame& frame) noexcept {
			if (frame.Stage == 0) {
				frame.Stage = 1;
				auto hook = std::move(object.ReleaseHook);
				object.ReleaseHook = {};
				if (hook) {
					++m_ObjectLifetimes[frame.Index]->StrongReferences;
					try {
						if (m_ReleaseCallbackDepth >= m_Limits.MaxFrames) {
							throw std::runtime_error("Squirrel native release callback depth exceeds limit");
						}

						VmReleaseCallbackScope callback_scope(m_ReleaseCallbackDepth);
						const SquirrelValue receiver = Handle(frame.Index, true);
						hook(m_Api, receiver);
					} catch (...) {
						m_ReleaseFailure = std::current_exception();
					}

					object.ReleaseHook = {};
					--m_ObjectLifetimes[frame.Index]->StrongReferences;
					if (!frame.Collection && m_ObjectLifetimes[frame.Index]->StrongReferences != 0) {
						frame.Resurrected = true;

						return false;
					}
				}

				return true;
			}

			if (frame.Stage == 1) {
				frame.Stage = 2;
				object.InstanceClass.reset();

				return true;
			}

			if (frame.Member < object.Slots.size()) {
				ReleaseValue(object.Slots[frame.Member++].Value);

				return true;
			}

			return false;
		}

		bool ReleaseGeneratorMember(VmObject& object, VmReleaseFrame& frame) noexcept {
			if (frame.Stage == 0) {
				frame.Stage = 1;
				if (!frame.Collection && object.GeneratorFrame) {
					object.GeneratorFrame->Closure = {};

					return true;
				}
			}

			if (frame.Stage == 1) {
				if (!frame.Collection && object.GeneratorFrame && frame.Member < object.GeneratorFrame->VariableArguments.size()) {
					ReleaseRawValue(object.GeneratorFrame->VariableArguments[frame.Member++]);

					return true;
				}

				frame.Stage = 2;
				frame.Member = 0;
			}

			if (frame.Stage == 2) {
				if (object.GeneratorFrame && frame.Member < object.GeneratorFrame->Registers.Size()) {
					if (frame.Collection) {
						ReleaseValue(object.GeneratorFrame->Registers[frame.Member++]);
					} else {
						ReleaseRawValue(object.GeneratorFrame->Registers[frame.Member++]);
					}

					return true;
				}

				frame.Stage = 3;
			}

			if (frame.Stage == 3) {
				frame.Stage = 4;
				object.GeneratorClosure.reset();

				return true;
			}

			object.GeneratorDead = true;
			if (!frame.Collection) {
				object.GeneratorFrame.reset();
			}

			return false;
		}

		void KillGenerator(VmObject& generator) noexcept {
			generator.GeneratorDead = true;
			generator.GeneratorRunning = false;
			if (generator.GeneratorFrame) {
				for (std::size_t index = 0; index < generator.GeneratorFrame->Registers.Size(); ++index) {
					ReleaseStoredValue(generator.GeneratorFrame->Registers[index]);
				}
			}

			generator.GeneratorClosure.reset();
		}

		void ReleaseRawValue(SquirrelValue& value) noexcept {
			if (std::holds_alternative<SquirrelObjectHandle>(value) || std::holds_alternative<SquirrelString>(value)) {
				SquirrelValue previous(std::move(value));
				value = BorrowedValue(previous);
			}
		}

		bool ReleaseClosureMember(VmObject& object, VmReleaseFrame& frame) noexcept {
			if (frame.Stage == 0) {
				if (!frame.Collection && frame.Member < object.Defaults.size()) {
					ReleaseRawValue(object.Defaults[frame.Member++]);

					return true;
				}

				if (!frame.Collection) {
					object.Defaults.clear();
				}

				frame.Stage = 1;
				frame.Member = 0;
			}

			if (frame.Stage == 1) {
				if (frame.Member < object.Captures.size()) {
					ReleaseRawValue(object.Captures[frame.Member++]);

					return true;
				}

				object.Captures.clear();
				if (frame.Collection) {
					return false;
				}

				frame.Stage = 2;
			}

			if (frame.Stage == 2) {
				frame.Stage = 3;
				object.Prototype.reset();

				return true;
			}

			if (frame.Stage == 3) {
				frame.Stage = 4;
				object.BoundEnvironment.reset();

				return true;
			}

			return false;
		}

		bool ReleaseNativeClosureMember(VmObject& object, VmReleaseFrame& frame) noexcept {
			if (frame.Stage == 0) {
				frame.Stage = 1;
				if (!frame.Collection) {
					ReleaseValue(object.NativeSignatureName);

					return true;
				}
			}

			if (frame.Stage == 1) {
				frame.Stage = 2;
				if (!frame.Collection) {
					object.BoundEnvironment.reset();

					return true;
				}
			}

			if (frame.Member < object.Captures.size()) {
				ReleaseRawValue(object.Captures[frame.Member++]);

				return true;
			}

			object.Captures.clear();
			if (frame.Collection) {
				return false;
			}

			if (frame.Stage == 2) {
				frame.Stage = 3;
				object.Native = {};

				return true;
			}

			return false;
		}

		void ReleaseArrayElement(VmObject& object, std::size_t index) noexcept {
			if (std::holds_alternative<SquirrelObjectHandle>(object.Elements[index]) || std::holds_alternative<SquirrelString>(object.Elements[index])) {
				SquirrelValue previous(std::move(object.Elements[index]));
				object.Elements[index] = BorrowedValue(previous);
			}
		}

		SquirrelValue RemoveArray(const SquirrelValue& array, std::int32_t index) {
			auto& object = Object(array);
			if (object.Type != SquirrelValueType::Array) {
				throw std::runtime_error("Squirrel array remove requires an array");
			}

			const auto position = CheckedIndex(index, object.Elements.size());
			const auto value = ResolveStoredValue(object.Elements[position]);
			for (std::size_t following = position + 1; following < object.Elements.size(); ++following) {
				AssignStoredValue(object.Elements[following - 1], object.Elements[following]);
			}

			ReleaseStoredValue(object.Elements.back());
			object.Elements.pop_back();

			return value;
		}

		void ClearArray(const SquirrelValue& array) {
			auto& object = Object(array);
			if (object.Type != SquirrelValueType::Array || object.ArrayClearing) {
				throw std::runtime_error("Squirrel array clear requires an array and cannot reenter the same clear");
			}

			object.ArrayClearing = true;
			try {
				for (std::size_t index = 0; index < object.Elements.size(); ++index) {
					// Native2215d0 releases ascending raw slots before setting logical size to zero.
					ReleaseArrayElement(object, index);
				}

				object.Elements.clear();
			} catch (...) {
				object.ArrayClearing = false;
				throw;
			}

			object.ArrayClearing = false;
		}

		bool ReleaseNextMember(VmReleaseFrame& frame) noexcept {
			auto& object = *m_Objects[frame.Index];
			switch (object.Type) {
			case SquirrelValueType::Table:
				return ReleaseTableMember(object, frame);
			case SquirrelValueType::Class:
				return ReleaseClassMember(object, frame);
			case SquirrelValueType::Instance:
				return ReleaseInstanceMember(object, frame);
			case SquirrelValueType::UserData:
				return ReleaseUserDataMember(object, frame);
			case SquirrelValueType::Array:
				if (frame.Member < object.Elements.size()) {
					ReleaseArrayElement(object, frame.Member++);

					return true;
				}

				object.Elements.clear();

				return false;
			case SquirrelValueType::Closure:
				return ReleaseClosureMember(object, frame);
			case SquirrelValueType::NativeClosure:
				return ReleaseNativeClosureMember(object, frame);
			case SquirrelValueType::Generator:
				return ReleaseGeneratorMember(object, frame);
			default:
				return false;
			}
		}

		void DrainReleases() noexcept {
			if (m_DrainingReleases && m_ReleaseCallbackDepth == 0) {
				return;
			}

			const bool was_draining = m_DrainingReleases;
			const auto frame_floor = was_draining ? m_ReleaseFrames.size() : 0;
			m_DrainingReleases = true;
			while (!m_PendingReleases.empty() || m_ReleaseFrames.size() > frame_floor) {
				if (!m_PendingReleases.empty()) {
					const auto index = m_PendingReleases.back();
					m_PendingReleases.pop_back();
					auto& record = *m_ObjectLifetimes[index];
					record.ReleaseQueued = false;
					if (!record.Alive || record.Finalizing || record.StrongReferences != 0) {
						continue;
					}

					record.Finalizing = true;
					VmReleaseFrame frame;
					frame.Index = index;
					m_ReleaseFrames.push_back(frame);
				}

				auto& frame = m_ReleaseFrames.back();
				if (ReleaseNextMember(frame)) {
					continue;
				}

				const auto index = frame.Index;
				const bool resurrected = frame.Resurrected;
				const bool collection = frame.Collection;
				m_ReleaseFrames.pop_back();
				auto& record = *m_ObjectLifetimes[index];
				record.Finalizing = false;
				if (!collection && !resurrected && (record.StrongReferences == 0 || m_Objects[index]->Type == SquirrelValueType::UserData)) {
					DeleteObject(index);
				}
			}

			m_DrainingReleases = was_draining;
		}

		void DestroyRetiredContext(std::uint32_t index) noexcept {
			UnlinkCollectable(ContextIdentity(index));
			m_Contexts[index].reset();
			m_FreeContexts.push_back(index);
		}

		std::optional<VmCollectableIdentity> FinalizeContext(std::uint32_t index, bool unlink) noexcept {
			auto& context = *m_Contexts[index];
			++context.FinalizationDepth;
			const auto previous_closing_context = m_ClosingContext;
			m_ClosingContext = index;
			ClearContextForClose(context);
			const auto next = context.GarbageNext;
			if (unlink) {
				UnlinkCollectable(ContextIdentity(index));
			}

			m_ClosingContext = previous_closing_context;
			--context.FinalizationDepth;
			if (context.FinalizationDepth == 0 && m_RetiredContexts[index]) {
				DestroyRetiredContext(index);
			}

			return next;
		}

		std::optional<VmCollectableIdentity> FinalizeCollectable(const VmCollectableIdentity& identity) noexcept {
			if (identity.Kind == VmCollectableKind::Context) {
				return FinalizeContext(identity.Index, true);
			}

			auto& record = *m_ObjectLifetimes[identity.Index];
			++record.StrongReferences;
			record.Finalizing = true;
			VmReleaseFrame frame;
			frame.Index = identity.Index;
			frame.Collection = true;
			m_ReleaseFrames.push_back(frame);
			DrainReleases();
			// Native shared Close and GC read the next link after the actual finalizer returns.
			const auto next = record.GarbageNext;
			--record.StrongReferences;
			if (record.StrongReferences == 0) {
				QueueLastRelease(this, identity.Index, record.Generation);
			} else {
				UnlinkCollectable(identity);
			}

			return next;
		}

		void FinalizeRemainingObjects() noexcept {
			auto current = m_GarbageHead;
			while (current) {
				if (!IsLiveCollectable(*current)) {
					m_ReleaseFailure = std::make_exception_ptr(VmSafetyFailure("Squirrel close cursor expired during destructor callbacks"));

					break;
				}

				current = FinalizeCollectable(*current);
			}
		}

		void ClearContextForClose(VmExecutionContext& context) noexcept {
			ReleaseStoredValue(context.RootTable);
			ReleaseStoredValue(context.LastError);
			ReleaseStoredValue(context.ErrorHandler);
			ReleaseStoredValue(context.NativeTemporary);
			for (auto& frame : context.Frames) {
				frame.Closure = {};
			}

			for (std::size_t index = 0; index < context.Stack.AllocatedValues().size(); ++index) {
				ReleaseStoredValue(context.Stack.At(index));
			}

			context.Frames.clear();
			context.NativeFrames.clear();
			context.TemporaryRoots.clear();
			context.SuspendedAction.reset();
			context.Suspended = false;
			ReleaseStoredValue(context.Result);
		}

		void RequireCanClose() const {
			if (m_Closed) {
				return;
			}

			if (!m_BorrowedReceiverScopes.empty()) {
				throw std::runtime_error("Squirrel cannot close during borrowed receiver dispatch");
			}
			if (std::any_of(m_Contexts.begin(), m_Contexts.end(), [](const auto& context) {
				    return context && (context->CompletedResultObserverDepth != 0 || context->PropertyReadDepth != 0);
			    })) {
				throw std::runtime_error("Squirrel cannot close during completed result observation or property read");
			}
			if (m_Running || m_DrainingReleases || m_Collecting || m_Closing) {
				throw std::runtime_error("Squirrel cannot close during execution, release or collection");
			}
		}

		void Close() {
			if (m_Closed) {
				return;
			}

			RequireCanClose();

			m_Closing = true;
			m_Limits.ReclaimObjects = true;
			// Native222450 finalizes the bootstrap VM before traversing the actual shared collectable list.
			static_cast<void>(FinalizeContext(0, false));

			FinalizeRemainingObjects();
			ReleaseStoredValue(m_ConstructorName);
			ReleaseStoredValue(m_ConstantTable);
			for (auto& delegate : m_DefaultDelegates) {
				delegate.reset();
			}

			m_RetiringHostRoots = true;
			for (auto& entry : m_HostRoots->Entries) {
				entry.References = 0;
				ReleaseStoredValue(entry.Value);
			}

			m_ExceptionClass.reset();
			FinalizeRemainingObjects();
			m_DrainingReleases = true;
			for (std::size_t index = 0; index < m_Objects.size(); ++index) {
				if (m_ObjectLifetimes[index]->Alive) {
					DeleteObject(static_cast<std::uint32_t>(index));
				}
			}

			m_DrainingReleases = false;
			for (std::size_t index = 0; index < m_Contexts.size(); ++index) {
				m_RetiredContexts[index] = true;
				if (m_Contexts[index]) {
					UnlinkCollectable(ContextIdentity(static_cast<std::uint32_t>(index)));
					m_Contexts[index].reset();
				}
			}

			m_HostRoots.reset();
			m_SharedMetaNames.clear();
			m_SharedTypeNames.clear();
			m_StringPool->Alive = false;
			m_StringPool.reset();
			m_LifetimeOwner.reset();
			m_PendingReleases.clear();
			m_ReleaseFrames.clear();
			m_Closing = false;
			m_Closed = true;
		}

		void QueueMarkValue(const SquirrelValue& value) {
			const auto* handle = std::get_if<SquirrelObjectHandle>(&value);
			if (!handle) {
				return;
			}

			CheckValue(value);
			auto& record = *m_ObjectLifetimes[handle->Index];
			if (!IsCollectable(m_Objects[handle->Index]->Type) || record.Marked || record.MarkQueued) {
				return;
			}

			record.MarkQueued = true;
			VmMarkFrame frame;
			frame.Index = handle->Index;
			m_PendingMarks.push_back(frame);
		}

		void MarkRootValue(const SquirrelValue& value) {
			QueueMarkValue(value);
			DrainMarks();
		}

		void QueueMarkAction(const VmReturnAction& action) {
			MarkRootValue(action.Operand);
			MarkRootValue(action.Key);
			if (action.InputOverride) {
				MarkRootValue(*action.InputOverride);
			}

			if (action.ClassMember) {
				MarkRootValue(action.ClassMember->Receiver);
				MarkRootValue(action.ClassMember->Key);
				MarkRootValue(action.ClassMember->Value);
				MarkRootValue(action.ClassMember->Attributes);
			}

			if (action.Compound) {
				MarkRootValue(action.Compound->Receiver);
				MarkRootValue(action.Compound->Key);
				MarkRootValue(action.Compound->Operand);
				MarkRootValue(action.Compound->Previous);
				MarkRootValue(action.Compound->Computed);
			}

			const auto* following = action.FollowingAction.get();
			std::size_t depth = 0;
			while (following) {
				if (++depth > m_Limits.MaxFrames) {
					throw std::runtime_error("Squirrel collection continuation depth exceeds limit");
				}

				VmReturnAction current = *following;
				current.FollowingAction.reset();
				QueueMarkAction(current);
				following = following->FollowingAction.get();
			}
		}

		void MarkFrameContinuations(const VmFrame& frame) {
			if (frame.ReturnOverride) {
				MarkRootValue(*frame.ReturnOverride);
			}

			if (frame.ReturnAction) {
				QueueMarkAction(*frame.ReturnAction);
			}

			if (frame.Generator) {
				MarkRootValue(*frame.Generator);
			}
		}

		std::optional<SquirrelValue> NextTableMarkChild(const VmObject& object, VmMarkFrame& frame) const {
			if (frame.Stage == 0) {
				frame.Stage = 1;
				if (object.Delegate) {
					return *object.Delegate;
				}
			}

			if (frame.Member < object.Slots.size()) {
				const auto& slot = object.Slots[frame.Member];
				if (frame.MemberPart++ == 0) {
					return slot.Key;
				}

				frame.MemberPart = 0;
				++frame.Member;

				return slot.Value;
			}

			return {};
		}

		std::optional<SquirrelValue> NextClassMarkChild(const VmObject& object, VmMarkFrame& frame) const {
			for (;;) {
				switch (frame.Stage) {
				case 0:
					frame.Stage = 1;
					if (object.MemberIndex) {
						return *object.MemberIndex;
					}

					break;
				case 1:
					frame.Stage = 2;
					if (object.ParentClass) {
						return *object.ParentClass;
					}

					break;
				case 2:
					frame.Stage = 3;

					return object.Attributes;
				case 3:
				case 4:
					while (frame.Member < object.Slots.size() && object.Slots[frame.Member].IsField != (frame.Stage == 3)) {
						++frame.Member;
					}

					if (frame.Member < object.Slots.size()) {
						if (frame.MemberPart++ == 0) {
							return object.Slots[frame.Member].Value;
						}

						frame.MemberPart = 0;

						return object.Slots[frame.Member++].Attributes;
					}

					++frame.Stage;
					frame.Member = 0;
					break;
				case 5:
					if (frame.Member < object.MetaMethods.size()) {
						return object.MetaMethods[frame.Member++];
					}

					return {};
				default:
					throw VmSafetyFailure("Squirrel class mark cursor is invalid");
				}
			}
		}

		std::optional<SquirrelValue> NextClosureMarkChild(const VmObject& object, VmMarkFrame& frame) const {
			if (frame.Stage == 0) {
				if (frame.Member < object.Captures.size()) {
					return object.Captures[frame.Member++];
				}

				frame.Stage = 1;
				frame.Member = 0;
			}

			if (frame.Member < object.Defaults.size()) {
				return object.Defaults[frame.Member++];
			}

			return {};
		}

		std::optional<SquirrelValue> NextActionMarkChild(const VmReturnAction& action, std::size_t ordinal) const {
			const auto* current = &action;
			for (std::size_t depth = 0; current; ++depth) {
				if (depth >= m_Limits.MaxFrames) {
					throw VmSafetyFailure("Squirrel collection continuation depth exceeds limit");
				}

				if (ordinal < 12) {
					switch (ordinal) {
					case 0:
						return current->Operand;
					case 1:
						return current->Key;
					case 2:
						return current->InputOverride.value_or(SquirrelValue{});
					case 3:
						return current->ClassMember ? current->ClassMember->Receiver : SquirrelValue{};
					case 4:
						return current->ClassMember ? current->ClassMember->Key : SquirrelValue{};
					case 5:
						return current->ClassMember ? current->ClassMember->Value : SquirrelValue{};
					case 6:
						return current->ClassMember ? current->ClassMember->Attributes : SquirrelValue{};
					case 7:
						return current->Compound ? current->Compound->Receiver : SquirrelValue{};
					case 8:
						return current->Compound ? current->Compound->Key : SquirrelValue{};
					case 9:
						return current->Compound ? current->Compound->Operand : SquirrelValue{};
					case 10:
						return current->Compound ? current->Compound->Previous : SquirrelValue{};
					case 11:
						return current->Compound ? current->Compound->Computed : SquirrelValue{};
					}
				}

				ordinal -= 12;
				current = current->FollowingAction.get();
			}

			return {};
		}

		std::optional<SquirrelValue> NextGeneratorMarkChild(const VmObject& object, VmMarkFrame& frame) const {
			for (;;) {
				switch (frame.Stage) {
				case 0:
					if (object.GeneratorFrame && frame.Member < object.GeneratorFrame->Registers.Size()) {
						return object.GeneratorFrame->Registers[frame.Member++];
					}

					frame.Stage = 1;
					frame.Member = 0;
					break;
				case 1:
					if (object.GeneratorFrame && frame.Member < object.GeneratorFrame->VariableArguments.size()) {
						return object.GeneratorFrame->VariableArguments[frame.Member++];
					}

					frame.Stage = 2;
					frame.Member = 0;
					break;
				case 2:
					frame.Stage = 3;
					frame.Member = 0;
					break;
				case 3:
					frame.Stage = 4;
					if (object.GeneratorFrame) {
						return object.GeneratorClosure ? SquirrelValue(*object.GeneratorClosure) : SquirrelValue{};
					}

					break;
				case 4:
					frame.Stage = 5;
					if (object.GeneratorFrame && object.GeneratorFrame->ReturnOverride) {
						return *object.GeneratorFrame->ReturnOverride;
					}

					break;
				case 5:
					if (object.GeneratorFrame && object.GeneratorFrame->ReturnAction) {
						return NextActionMarkChild(*object.GeneratorFrame->ReturnAction, frame.Member++);
					}

					return {};
				default:
					throw VmSafetyFailure("Squirrel generator mark cursor is invalid");
				}
			}
		}

		std::optional<SquirrelValue> NextObjectMarkChild(const VmObject& object, VmMarkFrame& frame) const {
			switch (object.Type) {
			case SquirrelValueType::Table:
				return NextTableMarkChild(object, frame);
			case SquirrelValueType::Class:
				return NextClassMarkChild(object, frame);
			case SquirrelValueType::Closure:
			case SquirrelValueType::NativeClosure:
				return NextClosureMarkChild(object, frame);
			case SquirrelValueType::Generator:
				return NextGeneratorMarkChild(object, frame);
			case SquirrelValueType::Instance:
				if (frame.Stage++ == 0 && object.InstanceClass) {
					return *object.InstanceClass;
				}

				if (frame.Member < object.Slots.size()) {
					return object.Slots[frame.Member++].Value;
				}

				return {};
			case SquirrelValueType::UserData:
				if (frame.Stage++ == 0 && object.Delegate) {
					return *object.Delegate;
				}

				return {};
			case SquirrelValueType::Array:
				if (frame.Member < object.Elements.size()) {
					return object.Elements[frame.Member++];
				}

				return {};
			default:
				throw VmSafetyFailure("Squirrel marker requires a collectible object");
			}
		}

		void MoveToMarkedList(const VmCollectableIdentity& identity) noexcept {
			UnlinkCollectable(identity);
			auto& record = CollectableLinks(identity);
			record.Marked = true;
			record.GarbageTracked = true;
			record.GarbageNext = m_MarkedHead;
			if (m_MarkedHead) {
				CollectableLinks(*m_MarkedHead).GarbagePrevious = identity;
			}

			m_MarkedHead = identity;
		}

		void DrainMarks() {
			while (!m_PendingMarks.empty()) {
				auto& frame = m_PendingMarks.back();
				const auto index = frame.Index;
				if (const auto child = NextObjectMarkChild(*m_Objects[index], frame)) {
					QueueMarkValue(*child);

					continue;
				}

				m_PendingMarks.pop_back();
				MoveToMarkedList(ObjectIdentity(index));
				m_ObjectLifetimes[index]->MarkQueued = false;
			}
		}

		void MarkContext(std::uint32_t index) {
			auto& context = *m_Contexts[index];
			if (context.Marked || context.MarkQueued) {
				return;
			}

			context.MarkQueued = true;
			// Native24e3a0 marks error state before root/temporary/stack values and call closures.
			MarkRootValue(context.LastError);
			MarkRootValue(context.ErrorHandler);
			MarkRootValue(context.RootTable);
			MarkRootValue(context.NativeTemporary);
			for (const auto& temporary : context.TemporaryRoots) {
				MarkRootValue(temporary);
			}

			MarkRootValue(context.Result);
			for (const auto& value : context.Stack.AllocatedValues()) {
				MarkRootValue(value);
			}

			for (const auto& frame : context.Frames) {
				for (const auto& value : frame.VariableArguments) {
					MarkRootValue(value);
				}
			}

			for (std::size_t depth = 0; depth <= context.Frames.size(); ++depth) {
				for (const auto& native_frame : context.NativeFrames) {
					if (std::min(native_frame.ScriptDepth, context.Frames.size()) == depth) {
						MarkRootValue(native_frame.Closure);
					}
				}

				if (depth < context.Frames.size()) {
					MarkRootValue(context.Frames[depth].Closure);
				}
			}

			for (const auto& frame : context.Frames) {
				MarkFrameContinuations(frame);
			}

			if (context.SuspendedAction) {
				QueueMarkAction(*context.SuspendedAction);
			}

			// Native24e3a0 transfers the VM itself after its actual owned descendants.
			MoveToMarkedList(ContextIdentity(index));
			context.MarkQueued = false;
		}

		void MarkRoots() {
			// Native250120 always starts the shared state's global VM, independently of the caller.
			MarkContext(0);
			for (const auto& entry : m_HostRoots->Entries) {
				if (entry.References != 0) {
					MarkRootValue(entry.Value);
				}
			}

			MarkRootValue(m_ConstantTable);
			for (const auto& delegate : m_DefaultDelegates) {
				if (delegate) {
					MarkRootValue(*delegate);
				}
			}

			// Explicit facade contexts own their roots until ReleaseContext; SQThread-value tracing remains separate.
			for (std::size_t index = 1; index < m_Contexts.size(); ++index) {
				if (m_Contexts[index]) {
					MarkContext(static_cast<std::uint32_t>(index));
				}
			}

			DrainMarks();
		}

		std::uint32_t CollectGarbage() {
			if (!m_Limits.ReclaimObjects || m_Collecting || m_DrainingReleases || m_Closing || m_Closed) {
				throw std::runtime_error("Squirrel collection is disabled or reentrant");
			}

			m_Collecting = true;
			m_CollectionAllocations = 0;
			try {
				MarkRoots();
			} catch (...) {
				RestoreMarkedObjects();
				m_Collecting = false;
				throw;
			}

			std::uint32_t count = 0;
			auto current = m_GarbageHead;
			while (current) {
				if (!IsLiveCollectable(*current)) {
					m_ReleaseFailure = std::make_exception_ptr(VmSafetyFailure("Squirrel collector cursor expired during destructor callbacks"));

					break;
				}

				current = FinalizeCollectable(*current);
				++count;
			}

			// Native250120 restores only the marked chain, excluding newly prepended finalizer allocations.
			while (m_GarbageHead) {
				UnlinkCollectable(*m_GarbageHead);
			}

			RestoreMarkedObjects();
			m_Collecting = false;

			return count;
		}

		void RestoreMarkedObjects() noexcept {
			for (const auto& frame : m_PendingMarks) {
				m_ObjectLifetimes[frame.Index]->MarkQueued = false;
			}

			m_PendingMarks.clear();
			for (const auto& context : m_Contexts) {
				if (context) {
					context->MarkQueued = false;
				}
			}

			if (m_MarkedHead) {
				auto last = *m_MarkedHead;
				while (CollectableLinks(last).GarbageNext) {
					CollectableLinks(last).Marked = false;
					last = *CollectableLinks(last).GarbageNext;
				}

				CollectableLinks(last).Marked = false;
				CollectableLinks(last).GarbageNext = m_GarbageHead;
				if (m_GarbageHead) {
					CollectableLinks(*m_GarbageHead).GarbagePrevious = last;
				}

				m_GarbageHead = m_MarkedHead;
				m_MarkedHead.reset();
			}
		}

		SquirrelObjectHandle Handle(std::uint32_t index, bool borrowed = false) const {
			if (index >= m_ObjectLifetimes.size() || !m_ObjectLifetimes[index]->Alive) {
				throw std::runtime_error("Squirrel object lifetime expired");
			}

			SquirrelObjectHandle handle;
			handle.Index = index;
			handle.Generation = m_ObjectLifetimes[index]->Generation;
			handle.Owner = this;
			handle.m_Lifetime = m_ObjectLifetimes[index];
			if (!borrowed) {
				handle.Acquire();
			}

			return handle;
		}

		SquirrelObjectHandle Handle(const VmBorrowedObject& identity) const {
			if (identity.Owner != this || identity.Index >= m_ObjectLifetimes.size() || identity.Generation != m_ObjectLifetimes[identity.Index]->Generation || !m_ObjectLifetimes[identity.Index]->Alive) {
				throw std::runtime_error("Squirrel borrowed object lifetime expired");
			}

			return Handle(identity.Index);
		}

		SquirrelValue BorrowedValue(const SquirrelValue& value) const {
			CheckValue(value);
			if (const auto* handle = std::get_if<SquirrelObjectHandle>(&value)) {
				return Handle(handle->Index, true);
			}

			if (const auto* text = std::get_if<SquirrelString>(&value)) {
				SquirrelString borrowed(text->Record());
				borrowed.m_BorrowedStorage = borrowed.m_Storage;
				borrowed.m_Storage.reset();

				return borrowed;
			}

			return value;
		}

		SquirrelContextHandle ContextHandle(std::uint32_t index) const {
			SquirrelContextHandle handle;
			handle.Index = index;
			handle.Generation = m_ContextGenerations[index];
			handle.Owner = this;

			return handle;
		}

		VmExecutionContext& Context(SquirrelContextHandle handle) const {
			if (handle.Owner != this || handle.Index >= m_Contexts.size() || !m_Contexts[handle.Index] || m_RetiredContexts[handle.Index] || handle.Generation != m_ContextGenerations[handle.Index]) {
				throw std::runtime_error("Squirrel execution context handle is invalid");
			}

			return *m_Contexts[handle.Index];
		}

		VmExecutionContext& Active() const {
			return *m_Contexts[m_ActiveContext];
		}

		SquirrelContextHandle FinishContextCreation(std::uint32_t index, const VmExecutionContext* source) {
			// Native2530c0 links the context before25a030 installs its copied roots and globals.
			LinkCollectable(ContextIdentity(index));
			if (source) {
				m_Contexts[index]->RootTable = source->RootTable;
				m_Contexts[index]->ErrorHandler = source->ErrorHandler;
			}

			const auto previous_context = m_ActiveContext;
			m_ActiveContext = index;
			Active().Initializing = true;
			try {
				if (m_BaseLibraryInstalled) {
					InstallBaseGlobals();
				}
			} catch (...) {
				m_ActiveContext = previous_context;
				m_RetiredContexts[index] = true;
				static_cast<void>(FinalizeContext(index, true));

				throw;
			}

			Active().Initializing = false;
			m_ActiveContext = previous_context;

			return ContextHandle(index);
		}

		SquirrelContextHandle CreateContext(std::optional<SquirrelContextHandle> creator = {}) {
			if (m_Closed) {
				throw std::runtime_error("Squirrel VM is closed");
			}

			const auto* source = creator ? &Context(*creator) : (m_Contexts.empty() ? nullptr : &Active());
			if (!m_FreeContexts.empty()) {
				const auto index = m_FreeContexts.back();
				if (m_ContextGenerations[index] == std::numeric_limits<std::uint64_t>::max()) {
					throw std::runtime_error("Squirrel context generation exhausted");
				}

				auto context = std::make_unique<VmExecutionContext>(m_Limits.MaxRegisters);
				m_FreeContexts.pop_back();
				m_Contexts[index] = std::move(context);
				m_RetiredContexts[index] = false;
				++m_ContextGenerations[index];

				return FinishContextCreation(index, source);
			}

			if (m_Contexts.size() >= m_Limits.MaxContexts) {
				throw std::runtime_error("Squirrel execution context limit exceeded");
			}

			const auto index = static_cast<std::uint32_t>(m_Contexts.size());
			auto context = std::make_unique<VmExecutionContext>(m_Limits.MaxRegisters);
			m_Contexts.push_back(std::move(context));
			m_ContextGenerations.push_back(1);
			m_RetiredContexts.push_back(false);

			return FinishContextCreation(index, source);
		}

		void ResetContext(SquirrelContextHandle handle) {
			auto& context = Context(handle);
			if (context.FinalizationDepth != 0 || context.InvocationDepth != 0 || context.InvocationResultPopDepth != 0 || context.PropertyReadDepth != 0 || !context.NativeFrames.empty() || context.Initializing || !context.TemporaryRoots.empty()) {
				throw std::runtime_error("Squirrel cannot reset an executing context");
			}
			for (const auto& frame : context.Frames) {
				if (frame.Generator) {
					auto& generator = Object(*frame.Generator);
					KillGenerator(generator);
				}
			}

			VmExecutionContext cleared(m_Limits.MaxRegisters);
			cleared.RootTable = context.RootTable;
			cleared.ErrorHandler = context.ErrorHandler;
			cleared.LastError = context.LastError;
			static_cast<VmCollectableLinks&>(cleared) = static_cast<const VmCollectableLinks&>(context);
			context = std::move(cleared);
		}

		void ReleaseContext(SquirrelContextHandle handle) {
			if (handle.Index == 0) {
				throw std::runtime_error("Squirrel cannot release the root context");
			}

			auto& context = Context(handle);
			if (context.FinalizationDepth != 0) {
				m_RetiredContexts[handle.Index] = true;

				return;
			}

			ResetContext(handle);
			m_RetiredContexts[handle.Index] = true;
			static_cast<void>(FinalizeContext(handle.Index, true));
		}

		std::optional<std::size_t> RetainHostValue(const SquirrelValue& value) {
			if (m_Closed || m_RetiringHostRoots) {
				throw std::runtime_error("Squirrel cannot retain host roots after shutdown reference retirement begins");
			}

			CheckValue(value);
			if (!std::holds_alternative<SquirrelString>(value) && !std::holds_alternative<SquirrelObjectHandle>(value)) {
				return {};
			}

			auto& entries = m_HostRoots->Entries;
			for (std::size_t index = 0; index < entries.size(); ++index) {
				auto& entry = entries[index];
				if (entry.References != 0 && EqualKeys(entry.Value, value)) {
					if (entry.References == std::numeric_limits<std::uint32_t>::max()) {
						throw std::runtime_error("Squirrel host root reference count exhausted");
					}

					++entry.References;

					return index;
				}
			}

			VmHostRootEntry entry;
			AssignStoredValue(entry.Value, value);
			entry.References = 1;
			if (!m_HostRoots->FreeEntries.empty()) {
				// Native252e80 puts the released physical node at the free-chain head.
				const auto index = m_HostRoots->FreeEntries.back();
				m_HostRoots->FreeEntries.pop_back();
				entries[index] = std::move(entry);

				return index;
			}

			if (entries.size() >= m_Limits.MaxObjects) {
				throw std::runtime_error("Squirrel VM host root limit exceeded");
			}

			entries.push_back(std::move(entry));

			return entries.size() - 1;
		}

		std::uint32_t RetainedReferenceCount(const SquirrelValue& value) const {
			if (m_Closed) {
				throw std::runtime_error("Squirrel VM is closed");
			}

			CheckValue(value);
			for (const auto& entry : m_HostRoots->Entries) {
				if (entry.References != 0 && EqualKeys(entry.Value, value)) {
					return entry.References;
				}
			}

			return 0;
		}

		SquirrelObjectHandle Allocate(SquirrelValueType type) {
			if (m_Closed) {
				throw std::runtime_error("Squirrel VM is closed");
			}

			if (m_Collecting && m_CollectionAllocations >= m_Limits.MaxObjects) {
				throw VmSafetyFailure("Squirrel collection allocation work exceeds limit");
			}

			if (m_Closing && m_CloseAllocations >= m_Limits.MaxObjects) {
				throw VmSafetyFailure("Squirrel shutdown allocation work exceeds limit");
			}

			if (m_LiveObjects >= m_Limits.MaxObjects) {
				throw std::runtime_error("Squirrel VM object limit exceeded");
			}

			auto object = std::make_unique<VmObject>();
			object->Type = type;
			if (type == SquirrelValueType::Table) {
				InitializeTable(*object, 4);
			}

			const auto index = m_FreeObjects.empty() ? static_cast<std::uint32_t>(m_Objects.size()) : m_FreeObjects.back();
			auto lifetime = std::make_shared<SquirrelObjectHandle::Lifetime>();
			lifetime->Arena = m_LifetimeOwner;
			lifetime->Owner = this;
			lifetime->Index = index;
			lifetime->ReleaseContext = this;
			lifetime->OnLastRelease = QueueLastRelease;
			if (!m_FreeObjects.empty()) {
				if (m_ObjectLifetimes[index]->Generation == std::numeric_limits<std::uint64_t>::max()) {
					throw std::runtime_error("Squirrel object generation exhausted");
				}

				lifetime->Generation = m_ObjectLifetimes[index]->Generation + 1;
				m_ObjectLifetimes[index] = std::move(lifetime);
				m_Objects[index] = std::move(object);
				m_FreeObjects.pop_back();
			} else {
				m_ObjectLifetimes.push_back(std::move(lifetime));
				try {
					m_Objects.push_back(std::move(object));
				} catch (...) {
					m_ObjectLifetimes.pop_back();
					throw;
				}
			}

			++m_LiveObjects;
			if (m_Collecting) {
				++m_CollectionAllocations;
			}

			if (m_Closing) {
				++m_CloseAllocations;
			}

			if (IsCollectable(type)) {
				LinkGarbageObject(index);
			}

			return Handle(index);
		}

		VmObject& Object(const SquirrelObjectHandle& handle) const {
			if (handle.Owner != this || handle.Index >= m_Objects.size() || !handle.m_Lifetime || handle.m_Lifetime != m_ObjectLifetimes[handle.Index] || handle.Generation != m_ObjectLifetimes[handle.Index]->Generation || !m_ObjectLifetimes[handle.Index]->Alive) {
				throw std::runtime_error("Squirrel object handle does not belong to this VM");
			}

			return *m_Objects[handle.Index];
		}

		VmObject& Object(const SquirrelValue& value) const {
			const auto* handle = std::get_if<SquirrelObjectHandle>(&value);
			if (!handle) {
				throw std::runtime_error("Squirrel value is not an object");
			}

			return Object(*handle);
		}

		SquirrelValueType Type(const SquirrelValue& value) const {
			switch (value.index()) {
			case 0:
				return SquirrelValueType::Null;
			case 1:
				return SquirrelValueType::Integer;
			case 2:
				return SquirrelValueType::Float;
			case 3:
				return SquirrelValueType::Boolean;
			case 4:
				CheckValue(value);

				return SquirrelValueType::String;
			default:
				return Object(value).Type;
			}
		}

		static std::string TypeName(SquirrelValueType type) {
			switch (type) {
			case SquirrelValueType::Null:
				return "null";
			case SquirrelValueType::Integer:
				return "integer";
			case SquirrelValueType::Float:
				return "float";
			case SquirrelValueType::Boolean:
				return "bool";
			case SquirrelValueType::String:
				return "string";
			case SquirrelValueType::Table:
				return "table";
			case SquirrelValueType::Array:
				return "array";
			case SquirrelValueType::Closure:
			case SquirrelValueType::NativeClosure:
				return "function";
			case SquirrelValueType::Class:
				return "class";
			case SquirrelValueType::Instance:
				return "instance";
			case SquirrelValueType::Generator:
				return "generator";
			case SquirrelValueType::WeakReference:
				return "weakref";
			case SquirrelValueType::UserData:
				return "userdata";
			}

			throw std::runtime_error("invalid Squirrel value type");
		}

		std::shared_ptr<SquirrelString::Storage> InternString(const SquirrelString& text) const {
			const auto record = text.Record();
			if (record->Adopted) {
				const auto owner = record->Owner.lock();
				if (!owner || !owner->Alive) {
					throw std::runtime_error("Squirrel string owner expired");
				}

				if (owner != m_StringPool) {
					throw std::runtime_error("Squirrel string does not belong to this VM");
				}

				return record;
			}

			if (m_Closed || !m_StringPool->Alive) {
				throw std::runtime_error("Squirrel VM is closed");
			}

			const auto hash = StringHash(record->Bytes);
			if (const auto bucket = m_StringPool->Buckets.find(hash); bucket != m_StringPool->Buckets.end()) {
				for (const auto& entry : bucket->second) {
					const auto existing = entry.lock();
					if (existing && existing->Bytes == record->Bytes) {
						record->Canonical = existing;

						return existing;
					}
				}
			}

			if (m_StringPool->Count >= m_Limits.MaxInternedStrings || record->Bytes.size() > m_Limits.MaxInternedStringBytes - m_StringPool->Bytes) {
				throw VmSafetyFailure("Squirrel aggregate interned string limit exceeded");
			}

			m_StringPool->Buckets[hash].push_back(record);
			record->Hash = hash;
			record->Owner = m_StringPool;
			record->Adopted = true;
			++m_StringPool->Count;
			m_StringPool->Bytes += record->Bytes.size();

			return record;
		}

		SquirrelValue InternedValue(SquirrelValue value) const {
			CheckValue(value);
			if (const auto* text = std::get_if<SquirrelString>(&value)) {
				return SquirrelString(InternString(*text));
			}

			return value;
		}

		std::shared_ptr<const VmFunctionPrototype> CreatePrototype(const SquirrelFunction& function, std::size_t depth, std::size_t& count) const {
			if (depth >= m_Limits.MaxFrames || count >= m_Limits.MaxObjects) {
				throw VmSafetyFailure("Squirrel prototype ownership graph exceeds limit");
			}

			++count;
			auto prototype = std::make_shared<VmFunctionPrototype>();
			prototype->SourceName = InternedValue(LiteralValue(function.SourceName));
			prototype->Name = InternedValue(LiteralValue(function.Name));
			CheckValue(prototype->SourceName);
			CheckValue(prototype->Name);
			for (const auto& literal : function.Literals) {
				prototype->Literals.push_back(InternedValue(LiteralValue(literal)));
				CheckValue(prototype->Literals.back());
			}

			const auto retain_metadata = [&](const SquirrelLiteral& literal) {
				prototype->Metadata.push_back(InternedValue(LiteralValue(literal)));
				CheckValue(prototype->Metadata.back());
			};
			for (const auto& parameter : function.Parameters) {
				retain_metadata(parameter);
			}

			for (const auto& outer : function.OuterBindings) {
				retain_metadata(outer.Source);
				retain_metadata(outer.Name);
			}

			for (const auto& local : function.LocalVariables) {
				retain_metadata(local.Name);
			}

			for (const auto& child : function.Functions) {
				prototype->Children.push_back(CreatePrototype(child, depth + 1, count));
			}

			return prototype;
		}

		void CheckValue(const SquirrelValue& value) const {
			if (const auto* handle = std::get_if<SquirrelObjectHandle>(&value)) {
				static_cast<void>(Object(*handle));
			}

			if (const auto* text = std::get_if<SquirrelString>(&value)) {
				if (text->Text().size() > m_Limits.MaxStringBytes) {
					throw std::runtime_error("Squirrel string limit exceeded");
				}

				static_cast<void>(InternString(*text));
			}
		}

		void InitializeTable(VmObject& object, std::size_t requested) const {
			std::size_t capacity = 4;
			while (capacity < requested) {
				if (capacity > m_Limits.MaxContainerSlots / 2) {
					throw std::runtime_error("Squirrel table allocation exceeds slot limit");
				}

				capacity *= 2;
			}

			if (capacity > m_Limits.MaxContainerSlots) {
				throw std::runtime_error("Squirrel table allocation exceeds slot limit");
			}

			object.Slots.clear();
			object.Slots.resize(capacity);
			object.TableFree = capacity - 1;
			object.TableUsed = 0;
		}

		void ClearTable(VmObject& object) {
			const auto capacity = object.Slots.size();
			const auto* storage = object.Slots.data();
			for (std::size_t index = 0; index < capacity; ++index) {
				if (object.Slots.size() != capacity || object.Slots.data() != storage) {
					throw std::runtime_error("Squirrel table clear callback invalidated its native node cursor");
				}

				ReleaseValue(object.Slots[index].Key);
				if (object.Slots.size() != capacity || object.Slots.data() != storage) {
					throw std::runtime_error("Squirrel table clear callback invalidated its native node cursor");
				}

				ReleaseValue(object.Slots[index].Value);
			}

			object.TableUsed = 0;
			object.TablePointerKeys = false;
			RehashTable(object, true);
		}

		static std::uint64_t HashKey(const SquirrelValue& key) {
			if (const auto* integer = std::get_if<std::int32_t>(&key)) {
				return static_cast<std::uint64_t>(static_cast<std::int64_t>(*integer));
			}

			if (std::holds_alternative<float>(key)) {
				return static_cast<std::uint64_t>(static_cast<std::int64_t>(NumericIndex(key)));
			}

			if (const auto* boolean = std::get_if<bool>(&key)) {
				return *boolean ? 1 : 0;
			}

			if (const auto* text = TryStringText(key)) {
				return StringHash(*text);
			}

			return std::get<SquirrelObjectHandle>(key).Index;
		}

		void RehashTable(VmObject& object, bool force) {
			const auto capacity = object.Slots.size();
			std::size_t next_capacity = capacity;
			if (object.TableUsed >= capacity - capacity / 4) {
				if (capacity > m_Limits.MaxContainerSlots / 2) {
					throw std::runtime_error("Squirrel table growth exceeds slot limit");
				}

				next_capacity = capacity * 2;
			} else if (object.TableUsed <= capacity / 4 && capacity > 4) {
				next_capacity = capacity / 2;
			} else if (!force) {
				return;
			}

			const auto previous = std::move(object.Slots);
			InitializeTable(object, next_capacity);
			for (const auto& slot : previous) {
				if (!std::holds_alternative<std::monostate>(slot.Key)) {
					InsertTableSlot(object, slot.Key, slot.Value);
				}
			}
		}

		void InsertTableSlot(VmObject& object, const SquirrelValue& key, const SquirrelValue& value) {
			if (auto* existing = FindOwnSlot(object, key)) {
				AssignStoredValue(existing->Value, value);

				return;
			}

			std::size_t destination = static_cast<std::size_t>(HashKey(key) & (object.Slots.size() - 1));
			if (!std::holds_alternative<std::monostate>(object.Slots[destination].Key)) {
				const auto free = object.TableFree;
				const auto displaced_home = static_cast<std::size_t>(HashKey(object.Slots[destination].Key) & (object.Slots.size() - 1));
				if (destination > free && displaced_home != destination) {
					std::size_t predecessor = displaced_home;
					for (std::size_t depth = 0; object.Slots[predecessor].Next != destination; ++depth) {
						if (!object.Slots[predecessor].Next || depth >= object.Slots.size()) {
							throw std::runtime_error("Squirrel corrupt table chain");
						}

						predecessor = *object.Slots[predecessor].Next;
					}

					object.Slots[predecessor].Next = free;
					object.Slots[free] = std::move(object.Slots[destination]);
					object.Slots[destination] = {};
				} else {
					object.Slots[free].Next = object.Slots[destination].Next;
					object.Slots[destination].Next = free;
					destination = free;
				}
			}

			AssignStoredValue(object.Slots[destination].Key, key);
			object.TablePointerKeys = object.TablePointerKeys || std::holds_alternative<SquirrelObjectHandle>(key);
			while (!std::holds_alternative<std::monostate>(object.Slots[object.TableFree].Key) || object.Slots[object.TableFree].Next) {
				if (object.TableFree == 0) {
					RehashTable(object, true);
					InsertTableSlot(object, key, value);

					return;
				}

				--object.TableFree;
			}

			AssignStoredValue(object.Slots[destination].Value, value);
			++object.TableUsed;
		}

		VmSlot* FindOwnSlot(VmObject& object, const SquirrelValue& key) const {
			if ((object.Type == SquirrelValueType::Class && !object.MemberIndex) || (object.Type == SquirrelValueType::Instance && (!object.InstanceClass || !Object(*object.InstanceClass).MemberIndex))) {
				return nullptr;
			}

			if (std::holds_alternative<std::monostate>(key)) {
				return nullptr;
			}

			const auto slot = std::find_if(object.Slots.begin(), object.Slots.end(), [&](const VmSlot& candidate) {
				return EqualKeys(candidate.Key, key);
			});

			return slot == object.Slots.end() ? nullptr : &*slot;
		}

		VmSlot* FindSlot(VmObject& object, const SquirrelValue& key, std::size_t depth = 0) const {
			if (depth > m_Limits.MaxObjects) {
				throw std::runtime_error("Squirrel delegate cycle");
			}

			if (auto* slot = FindOwnSlot(object, key)) {
				return slot;
			}

			if (object.InstanceClass) {
				return FindSlot(Object(*object.InstanceClass), key, depth + 1);
			}

			if (object.Delegate) {
				return FindSlot(Object(*object.Delegate), key, depth + 1);
			}

			return nullptr;
		}

		std::size_t CheckedIndex(const SquirrelValue& key, std::size_t size) const {
			const auto index = NumericIndex(key);
			if (index < 0 || static_cast<std::size_t>(index) >= size) {
				throw std::runtime_error("Squirrel index exceeds container");
			}

			return static_cast<std::size_t>(index);
		}

		std::size_t StringIndex(const SquirrelValue& key, std::size_t size) const {
			const auto index = NumericIndex(key);
			const auto magnitude = index < 0 ? -static_cast<std::int64_t>(index) : index;
			if (static_cast<std::uint64_t>(magnitude) >= size) {
				throw std::runtime_error("Squirrel string index exceeds container");
			}

			return index < 0 ? size - static_cast<std::size_t>(magnitude) : static_cast<std::size_t>(index);
		}

		std::optional<SquirrelValue> FindUserDataDefaultSlot(const VmObject& object, const SquirrelValue& key) const {
			if (object.Type != SquirrelValueType::UserData || !object.Delegate) {
				return {};
			}

			const auto methods = m_DefaultDelegates[static_cast<std::size_t>(SquirrelValueType::Table)];
			if (methods) {
				if (const auto* slot = FindOwnSlot(Object(*methods), key)) {
					return slot->Value;
				}
			}

			std::optional<SquirrelObjectHandle> delegate = object.Delegate;
			for (std::size_t depth = 0; delegate; ++depth) {
				if (depth >= m_Limits.MaxObjects) {
					throw VmSafetyFailure("Squirrel userdata delegate traversal exceeds object limit");
				}

				const auto& current = Object(*delegate);
				if (current.Delegate) {
					const auto* getter = FindOwnSlot(Object(*current.Delegate), std::string("_get"));
					if (getter && !std::holds_alternative<std::monostate>(getter->Value)) {
						throw VmSafetyFailure("Squirrel userdata delegated table getter continuation is not recovered");
					}
				}

				delegate = current.Delegate;
			}

			return {};
		}

		bool HasDelegatedTableDefault(const SquirrelValue& value, const SquirrelValue& key) const {
			const auto* handle = std::get_if<SquirrelObjectHandle>(&value);
			if (!handle) {
				return false;
			}

			const auto& object = Object(*handle);
			if ((object.Type != SquirrelValueType::Table && object.Type != SquirrelValueType::UserData) || !object.Delegate) {
				return false;
			}

			const auto methods = m_DefaultDelegates[static_cast<std::size_t>(SquirrelValueType::Table)];

			return methods && FindOwnSlot(Object(*methods), key) != nullptr;
		}

		SquirrelValue GetSlot(const SquirrelValue& value, const SquirrelValue& key) const {
			CheckValue(value);
			CheckValue(key);
			if (const auto* text = TryStringText(value); text && IsNumber(key)) {
				return static_cast<std::int32_t>(std::bit_cast<std::int8_t>(static_cast<std::uint8_t>((*text)[StringIndex(key, text->size())])));
			}

			if (const auto* handle = std::get_if<SquirrelObjectHandle>(&value)) {
				auto& object = Object(*handle);
				if (object.Type == SquirrelValueType::Array && IsNumber(key)) {
					return ResolveStoredValue(object.Elements[CheckedIndex(key, object.Elements.size())]);
				}

				if (auto* slot = FindSlot(object, key)) {
					const bool method = !slot->IsField && (object.Type == SquirrelValueType::Class || (object.Type == SquirrelValueType::Instance && !FindOwnSlot(object, key)));

					return method ? slot->Value : ResolveStoredValue(slot->Value);
				}

				if (const auto fallback = FindUserDataDefaultSlot(object, key)) {
					return *fallback;
				}
			}

			const auto delegate = m_DefaultDelegates[static_cast<std::size_t>(Type(value))];
			if (delegate) {
				if (const auto* slot = FindOwnSlot(Object(*delegate), key)) {
					return slot->Value;
				}
			}

			throw std::runtime_error("Squirrel slot does not exist");
		}

		SquirrelValue ResolveStoredValue(const SquirrelValue& value) const {
			if (const auto* handle = std::get_if<SquirrelObjectHandle>(&value); handle && Object(*handle).Type == SquirrelValueType::WeakReference) {
				return m_Api.ResolveWeakReference(value);
			}

			return value;
		}

		bool HasSlot(const SquirrelValue& value, const SquirrelValue& key) const {
			CheckValue(value);
			CheckValue(key);
			if (const auto* text = TryStringText(value); text && IsNumber(key)) {
				const auto index = NumericIndex(key);

				return (index < 0 ? -static_cast<std::int64_t>(index) : index) < static_cast<std::int64_t>(text->size());
			}

			const auto* handle = std::get_if<SquirrelObjectHandle>(&value);
			if (!handle) {
				return false;
			}

			auto& object = Object(*handle);
			if (object.Type == SquirrelValueType::Array && IsNumber(key)) {
				const auto index = NumericIndex(key);

				return index >= 0 && static_cast<std::size_t>(index) < object.Elements.size();
			}

			return FindSlot(object, key) != nullptr || FindUserDataDefaultSlot(object, key).has_value();
		}

		std::optional<SquirrelValue> FindRawSlot(const SquirrelValue& value, const SquirrelValue& key) const {
			CheckValue(key);
			auto& object = Object(value);
			if (object.Type == SquirrelValueType::Array) {
				if (!IsNumber(key)) {
					return {};
				}

				const auto index = NumericIndex(key);
				if (index < 0 || static_cast<std::size_t>(index) >= object.Elements.size()) {
					return {};
				}

				return ResolveStoredValue(object.Elements[static_cast<std::size_t>(index)]);
			}

			if (object.Type != SquirrelValueType::Table && object.Type != SquirrelValueType::Class && object.Type != SquirrelValueType::Instance) {
				throw std::runtime_error("Squirrel rawget requires array/table/instance or class");
			}

			if (const auto* slot = FindOwnSlot(object, key)) {
				return object.Type == SquirrelValueType::Class && !slot->IsField ? slot->Value : ResolveStoredValue(slot->Value);
			}

			if (object.InstanceClass) {
				if (const auto* slot = FindOwnSlot(Object(*object.InstanceClass), key)) {
					return slot->IsField ? ResolveStoredValue(slot->Value) : slot->Value;
				}
			}

			return {};
		}

		bool TrySetClassMeta(VmObject& object, const SquirrelValue& key, const SquirrelValue& content) {
			const auto* name = TryStringText(key);
			if (!name || (Type(content) != SquirrelValueType::Closure && Type(content) != SquirrelValueType::NativeClosure)) {
				return false;
			}

			const auto position = std::find(kMetaNames.begin(), kMetaNames.end(), *name);
			if (position == kMetaNames.end()) {
				return false;
			}

			AssignStoredValue(object.MetaMethods[static_cast<std::size_t>(position - kMetaNames.begin())], content);

			return true;
		}

		void AppendClassMember(VmObject& object, const SquirrelValue& key, const SquirrelValue& content, bool is_static, bool is_field) {
			if (object.Slots.size() >= m_Limits.MaxContainerSlots) {
				throw std::runtime_error("Squirrel class tuple limit exceeded");
			}

			const auto ordinal = static_cast<std::size_t>(std::count_if(object.Slots.begin(), object.Slots.end(), [is_field](const VmSlot& slot) {
				return slot.IsField == is_field;
			}));
			if (ordinal > 0x00ffffff) {
				throw VmSafetyFailure("Squirrel class member ordinal exceeds native encoding");
			}

			VmSlot slot;
			AssignStoredValue(slot.Key, BorrowedValue(key));
			AssignStoredValue(slot.Value, content);
			slot.IsStatic = is_static;
			slot.IsField = is_field;
			slot.ClassOrdinal = static_cast<std::uint32_t>(ordinal);
			object.Slots.push_back(std::move(slot));
			if (!object.MemberIndex) {
				throw std::runtime_error("Squirrel class member index is missing");
			}

			const auto encoded = static_cast<std::uint32_t>(ordinal) | (is_field ? std::uint32_t{0x02000000} : std::uint32_t{0x01000000});
			InsertTableSlot(Object(*object.MemberIndex), key, std::bit_cast<std::int32_t>(encoded));
		}

		void NewSlot(const SquirrelValue& value, const SquirrelValue& key, const SquirrelValue& content, bool is_static) {
			CheckValue(key);
			CheckValue(content);
			if (std::holds_alternative<std::monostate>(key)) {
				throw std::runtime_error("Squirrel null cannot be a slot key");
			}

			auto& object = Object(value);
			if (object.Type != SquirrelValueType::Table && object.Type != SquirrelValueType::Class) {
				throw std::runtime_error("Squirrel newslot requires a table or class");
			}

			if (object.Type == SquirrelValueType::Table) {
				InsertTableSlot(object, key, content);

				return;
			}

			if (object.ClassLocked) {
				throw std::runtime_error("Squirrel class is locked");
			}

			auto* existing = FindOwnSlot(object, key);
			if ((!existing || !existing->IsField) && TrySetClassMeta(object, key, content)) {
				return;
			}

			const bool is_field = !is_static && Type(content) != SquirrelValueType::Closure && Type(content) != SquirrelValueType::NativeClosure;
			if (existing) {
				if (existing->IsField || !is_field) {
					AssignStoredValue(existing->Value, content);

					return;
				}

				// Native 0x140264c40 retains the previous method tuple when its name is redirected to a new field.
				existing->Key = {};
			}

			AppendClassMember(object, key, content, is_static, is_field);
		}

		void SetSlot(const SquirrelValue& value, const SquirrelValue& key, const SquirrelValue& content) {
			CheckValue(key);
			CheckValue(content);
			auto& object = Object(value);
			if (object.Type == SquirrelValueType::Array) {
				AssignStoredValue(object.Elements[CheckedIndex(key, object.Elements.size())], content);

				return;
			}

			if (object.Type == SquirrelValueType::Class) {
				throw std::runtime_error("Squirrel class slot assignment is unsupported by the native setter");
			}

			if (object.Type == SquirrelValueType::UserData) {
				throw std::runtime_error("Squirrel userdata assignment requires its _set metamethod");
			}

			if (auto* slot = object.Type == SquirrelValueType::Instance ? FindOwnSlot(object, key) : FindSlot(object, key)) {
				AssignStoredValue(slot->Value, content);

				return;
			}

			throw std::runtime_error("Squirrel assignment to a missing slot");
		}

		SquirrelValue DeleteSlot(const SquirrelValue& value, const SquirrelValue& key) {
			auto& object = Object(value);
			if (object.Type != SquirrelValueType::Table) {
				throw std::runtime_error("Squirrel deleteslot requires a table");
			}

			const auto slot = std::find_if(object.Slots.begin(), object.Slots.end(), [&](const VmSlot& candidate) {
				return EqualKeys(candidate.Key, key);
			});
			if (slot == object.Slots.end()) {
				throw std::runtime_error("Squirrel deletion of a missing slot");
			}

			const auto result = slot->Value;
			slot->Key = {};
			AssignStoredValue(slot->Value, {});
			--object.TableUsed;
			RehashTable(object, false);

			return result;
		}

		void Append(const SquirrelValue& array, const SquirrelValue& value) {
			CheckValue(value);
			auto& object = Object(array);
			if (object.Type != SquirrelValueType::Array) {
				throw std::runtime_error("Squirrel append requires an array");
			}

			if (object.Elements.size() >= m_Limits.MaxContainerSlots) {
				throw std::runtime_error("Squirrel array slot limit exceeded");
			}

			object.Elements.push_back(value);
		}

		std::size_t Length(const SquirrelValue& value) const {
			CheckValue(value);
			if (const auto* text = TryStringText(value)) {
				return text->size();
			}

			const auto& object = Object(value);
			if (object.Type == SquirrelValueType::Array) {
				return object.Elements.size();
			}

			if (object.Type == SquirrelValueType::Table) {
				return object.TableUsed;
			}

			if (object.Type == SquirrelValueType::UserData) {
				return object.UserDataPayload.size();
			}

			throw std::runtime_error("Squirrel value has no length");
		}

		const SquirrelFunction& Function(const VmFrame& frame) const {
			const auto& closure = Object(frame.Closure);
			if (!closure.Function) {
				throw std::runtime_error("Squirrel closure lacks a prototype");
			}

			return *closure.Function;
		}

		SquirrelValue& Register(VmFrame& frame, std::int64_t index) const {
			if (index < 0 || static_cast<std::uint64_t>(index) >= frame.Registers.Size()) {
				throw std::runtime_error("Squirrel register exceeds frame");
			}

			return frame.Registers[static_cast<std::size_t>(index)];
		}

		SquirrelValue Literal(const VmFrame& frame, std::int32_t index) const {
			const auto& literals = Function(frame).Literals;
			if (index < 0 || static_cast<std::size_t>(index) >= literals.size()) {
				throw std::runtime_error("Squirrel literal index exceeds prototype");
			}

			return Object(frame.Closure).Prototype->Literals[static_cast<std::size_t>(index)];
		}

		std::size_t BranchTarget(const VmFrame& frame, std::int32_t displacement) const {
			const auto target = static_cast<std::int64_t>(frame.Instruction) + displacement;
			if (target < 0 || static_cast<std::uint64_t>(target) >= Function(frame).Instructions.size()) {
				throw std::runtime_error("Squirrel branch target exceeds prototype");
			}

			return static_cast<std::size_t>(target);
		}

		void Jump(VmFrame& frame, std::int32_t displacement) const {
			frame.Instruction = BranchTarget(frame, displacement);
		}

		VmFrame MakeFrame(SquirrelObjectHandle handle, std::span<const SquirrelValue> arguments, std::uint8_t return_register) const {
			const auto& closure = Object(handle);
			const auto& function = *closure.Function;
			if (function.StackSize > m_Limits.MaxRegisters || function.Parameters.size() > function.StackSize) {
				throw std::runtime_error("Squirrel frame stack exceeds limit or parameter extent");
			}

			const auto required = function.Parameters.size();
			if (arguments.size() < required && required - arguments.size() > closure.Defaults.size()) {
				throw std::runtime_error("Squirrel wrong number of parameters");
			}

			if (arguments.size() > required && !function.HasVariableParameters) {
				throw std::runtime_error("Squirrel wrong number of parameters");
			}

			VmFrame frame;
			frame.Closure = handle;
			frame.ReturnRegister = return_register;
			frame.Registers.Resize(function.StackSize);
			frame.PreparedParameters = required;
			frame.PreparedVariableCells = true;
			const auto copied = std::min(arguments.size(), required);
			for (std::size_t index = 0; index < copied; ++index) {
				CheckValue(arguments[index]);
				AssignStoredValue(frame.Registers[index], arguments[index]);
			}

			for (std::size_t index = copied; index < required; ++index) {
				AssignStoredValue(frame.Registers[index], closure.Defaults[closure.Defaults.size() - (required - index)]);
			}

			if (arguments.size() > required) {
				frame.VariableArguments.assign(arguments.begin() + static_cast<std::ptrdiff_t>(required), arguments.end());
			}

			if (closure.BoundEnvironment) {
				if (frame.Registers.Empty()) {
					throw std::runtime_error("Squirrel bound closure lacks receiver register");
				}

				AssignStoredValue(frame.Registers[0], m_Api.ResolveWeakReference(*closure.BoundEnvironment));
			}

			return frame;
		}

		void PushFrame(VmFrame frame, std::size_t base, std::optional<std::size_t> argument_cleanup = {}) {
			if (Active().Frames.size() >= m_Limits.MaxFrames) {
				throw std::runtime_error("Squirrel call depth exceeds limit");
			}

			std::size_t total = frame.Registers.Size();
			for (const auto& active : Active().Frames) {
				if (active.Registers.Size() > m_Limits.MaxRegisters - total) {
					throw std::runtime_error("Squirrel aggregate register limit exceeded");
				}

				total += active.Registers.Size();
			}

			frame.StackState = Active().Stack.EnterScriptFrame(base, frame.Registers.Size());
			if (frame.PreparedVariableCells) {
				for (std::size_t index = 0; index < frame.VariableArguments.size(); ++index) {
					Active().Stack.Store(base + frame.PreparedParameters + index, {});
				}
			}

			frame.Registers.Bind(Active().Stack, base, frame.PreparedParameters);
			frame.ArgumentCleanupTop = argument_cleanup;
			Active().Frames.push_back(std::move(frame));
		}

		SquirrelValue MakeClosure(const VmFrame& frame, std::int32_t child_index) {
			const auto& parent = Object(frame.Closure);
			const auto& children = Function(frame).Functions;
			if (child_index < 0 || static_cast<std::size_t>(child_index) >= children.size()) {
				throw std::runtime_error("Squirrel child prototype index exceeds parent");
			}

			const auto handle = Allocate(SquirrelValueType::Closure);
			auto& closure = Object(handle);
			closure.Program = parent.Program;
			closure.Prototype = parent.Prototype->Children[static_cast<std::size_t>(child_index)];
			closure.Function = &children[static_cast<std::size_t>(child_index)];
			for (const auto& binding : closure.Function->OuterBindings) {
				if (binding.Type == 0) {
					const auto source = IntegerValue(LiteralValue(binding.Source));
					if (source < 0 || static_cast<std::size_t>(source) >= frame.Registers.Size()) {
						throw std::runtime_error("Squirrel local capture exceeds parent frame");
					}

					closure.Captures.push_back(frame.Registers[static_cast<std::size_t>(source)]);
				} else if (binding.Type == 1) {
					if (frame.Registers.Empty()) {
						throw std::runtime_error("Squirrel global capture lacks receiver");
					}

					closure.Captures.push_back(ReadSlot(frame, frame.Registers[0], LiteralValue(binding.Source)));
				} else if (binding.Type == 2) {
					const auto source = IntegerValue(LiteralValue(binding.Source));
					if (source < 0 || static_cast<std::size_t>(source) >= parent.Captures.size()) {
						throw std::runtime_error("Squirrel outer capture exceeds parent closure");
					}

					closure.Captures.push_back(parent.Captures[static_cast<std::size_t>(source)]);
				} else {
					throw std::runtime_error("Squirrel unknown outer binding kind");
				}
			}

			for (const auto source : closure.Function->DefaultParameters) {
				if (source >= frame.Registers.Size()) {
					throw std::runtime_error("Squirrel default capture exceeds parent frame");
				}

				closure.Defaults.push_back(frame.Registers[source]);
			}

			return handle;
		}

		std::optional<SquirrelValue> FindDefaultSlot(const SquirrelValue& value, const SquirrelValue& key) const {
			const auto delegate = m_DefaultDelegates[static_cast<std::size_t>(Type(value))];
			if (delegate) {
				if (const auto* slot = FindOwnSlot(Object(*delegate), key)) {
					return ResolveStoredValue(slot->Value);
				}
			}

			return {};
		}

		std::string MissingIndexError(const SquirrelValue& key) const {
			std::string description;
			if (const auto* text = TryStringText(key)) {
				description = text->substr(0, std::min<std::size_t>(50, text->find('\0')));
			} else if (const auto* integer = std::get_if<std::int32_t>(&key)) {
				description = std::to_string(*integer);
			} else if (const auto* number = std::get_if<float>(&key)) {
				description = FormatNativeFloat(*number, 'g', 14);
			} else {
				description = TypeName(Type(key));
			}

			return "the index '" + description + "' does not exist";
		}

		std::optional<SquirrelValue> FindReadFallback(const VmFrame& frame, const SquirrelValue& value, const SquirrelValue& key) const {
			if (Type(value) == SquirrelValueType::Array && IsNumber(key)) {
				return {};
			}

			if (Type(value) != SquirrelValueType::Class) {
				if (const auto result = FindDefaultSlot(value, key)) {
					return result;
				}
			}

			if (!frame.Registers.Empty() && EqualValues(frame.Registers[0], value)) {
				if (Type(Active().RootTable) != SquirrelValueType::Table) {
					throw VmSafetyFailure("Squirrel root lookup requires a nonnull table");
				}

				return FindRawSlot(Active().RootTable, key);
			}

			return {};
		}

		SquirrelValue ReadSlot(const VmFrame& frame, const SquirrelValue& value, const SquirrelValue& key) const {
			if (HasSlot(value, key)) {
				return GetSlot(value, key);
			}

			if (const auto fallback = FindReadFallback(frame, value, key)) {
				return *fallback;
			}

			throw std::runtime_error(MissingIndexError(key));
		}

		SquirrelValue Arithmetic(std::uint8_t operation, const SquirrelValue& left, const SquirrelValue& right) const {
			if (operation == '+' && (std::holds_alternative<SquirrelString>(left) || std::holds_alternative<SquirrelString>(right))) {
				const auto left_text = m_Api.ToString(left);
				const auto right_text = m_Api.ToString(right);
				if (left_text.size() > m_Limits.MaxStringBytes || right_text.size() > m_Limits.MaxStringBytes - left_text.size()) {
					throw std::runtime_error("Squirrel string concatenation exceeds limit");
				}

				return left_text + right_text;
			}

			if (!IsNumber(left) || !IsNumber(right)) {
				throw std::runtime_error("Squirrel arithmetic on unsupported values: " + TypeName(Type(left)) + " and " + TypeName(Type(right)));
			}

			if (const auto* integer_left = std::get_if<std::int32_t>(&left); integer_left && std::holds_alternative<std::int32_t>(right)) {
				const auto integer_right = std::get<std::int32_t>(right);
				const auto bits_left = std::bit_cast<std::uint32_t>(*integer_left);
				const auto bits_right = std::bit_cast<std::uint32_t>(integer_right);
				switch (operation) {
				case '+':
					return WrapInteger(bits_left + bits_right);
				case '-':
					return WrapInteger(bits_left - bits_right);
				case '*':
					return WrapInteger(bits_left * bits_right);
				case '/':
				case '%':
					return DivideInteger(*integer_left, integer_right, operation);
				default:
					throw std::runtime_error("Squirrel unknown arithmetic operation");
				}
			}

			const auto number_left = NumberFloat(left);
			const auto number_right = NumberFloat(right);
			switch (operation) {
			case '+':
				return FloatArithmetic(operation, number_left, number_right);
			case '-':
				return FloatArithmetic(operation, number_left, number_right);
			case '*':
				return FloatArithmetic(operation, number_left, number_right);
			case '/':
				return FloatArithmetic(operation, number_left, number_right);
			case '%':
				return FloatArithmetic(operation, number_left, number_right);
			default:
				throw std::runtime_error("Squirrel unknown arithmetic operation");
			}
		}

		static float FloatArithmetic(std::uint8_t operation, float left, float right) {
			const auto left_bits = std::bit_cast<std::uint32_t>(left);
			const auto right_bits = std::bit_cast<std::uint32_t>(right);
			const bool left_nan = (left_bits & 0x7fffffff) > 0x7f800000;
			const bool right_nan = (right_bits & 0x7fffffff) > 0x7f800000;
			if (left_nan || right_nan) {
				const bool right_first = operation == '+' || operation == '*';
				const auto selected = right_first ? (right_nan ? right_bits : left_bits) : (left_nan ? left_bits : right_bits);

				return std::bit_cast<float>(selected | 0x00400000);
			}

			float result = 0;
			switch (operation) {
			case '+':
				result = left + right;
				break;
			case '-':
				result = left - right;
				break;
			case '*':
				result = left * right;
				break;
			case '/':
				result = left / right;
				break;
			case '%':
				result = static_cast<float>(std::fmod(static_cast<double>(left), static_cast<double>(right)));
				break;
			default:
				throw std::runtime_error("Squirrel unknown floating arithmetic operation");
			}

			// SSE scalar invalid operations produce the negative indefinite NaN, unlike Apple Silicon's default NaN.
			return std::isnan(result) ? std::bit_cast<float>(std::uint32_t{0xffc00000}) : result;
		}

		static std::int32_t DivideInteger(std::int32_t left, std::int32_t right, std::uint8_t operation) {
			if (right == 0) {
				throw std::runtime_error(operation == '/' ? "division by zero" : "modulo by zero");
			}

			// Native IDIV faults on this pair. A recoverable error keeps malformed scripts from invoking host undefined behavior.
			if (left == std::numeric_limits<std::int32_t>::min() && right == -1) {
				throw std::runtime_error("native integer division overflow");
			}

			return operation == '/' ? left / right : left % right;
		}

		static SquirrelValue Bitwise(std::uint8_t operation, const SquirrelValue& left, const SquirrelValue& right) {
			const auto signed_left = IntegerValue(left);
			const auto bits_left = std::bit_cast<std::uint32_t>(signed_left);
			const auto bits_right = std::bit_cast<std::uint32_t>(IntegerValue(right));
			const auto shift = bits_right & 31;
			switch (operation) {
			case 0:
				return WrapInteger(bits_left & bits_right);
			case 2:
				return WrapInteger(bits_left | bits_right);
			case 3:
				return WrapInteger(bits_left ^ bits_right);
			case 4:
				return WrapInteger(bits_left << shift);
			case 5:
				return WrapInteger((bits_left >> shift) | (signed_left < 0 && shift != 0 ? (~std::uint32_t{0} << (32 - shift)) : 0));
			case 6:
				return WrapInteger(bits_left >> shift);
			default:
				throw std::runtime_error("Squirrel unknown bitwise operation");
			}
		}

		std::string ComparisonError(const SquirrelValue& left, const SquirrelValue& right) const {
			return "comparsion between '" + TypeName(Type(left)) + "' and '" + TypeName(Type(right)) + "'";
		}

		std::int32_t RawOrder(const SquirrelValue& left, const SquirrelValue& right) const {
			if (EqualKeys(left, right)) {
				return 0;
			}

			if (std::holds_alternative<std::monostate>(left)) {
				return -1;
			}

			if (std::holds_alternative<std::monostate>(right)) {
				return 1;
			}

			if (const auto* integer = std::get_if<std::int32_t>(&left); integer && std::holds_alternative<std::int32_t>(right)) {
				return WrapInteger(std::bit_cast<std::uint32_t>(*integer) - std::bit_cast<std::uint32_t>(std::get<std::int32_t>(right)));
			}

			if (IsNumber(left) && IsNumber(right)) {
				const auto number_left = NumberFloat(left);
				const auto number_right = NumberFloat(right);

				return EqualValues(left, right) ? 0 : (number_left < number_right ? -1 : 1);
			}

			if (const auto* boolean = std::get_if<bool>(&left); boolean && std::holds_alternative<bool>(right)) {
				return *boolean ? 1 : -1;
			}

			if (const auto* text = TryStringText(left); text && std::holds_alternative<SquirrelString>(right)) {
				const auto& other = StringText(right);
				for (std::size_t index = 0;; ++index) {
					const auto left_byte = index == text->size() ? 0 : static_cast<std::uint8_t>((*text)[index]);
					const auto right_byte = index == other.size() ? 0 : static_cast<std::uint8_t>(other[index]);
					if (left_byte != right_byte || left_byte == 0) {
						return static_cast<std::int32_t>(left_byte) - right_byte;
					}
				}
			}

			if (const auto* object_left = std::get_if<SquirrelObjectHandle>(&left); object_left && Type(left) == Type(right)) {
				static_assert(sizeof(std::uintptr_t) == sizeof(VmObject*));
				const auto address_left = std::bit_cast<std::uintptr_t>(&Object(*object_left));
				const auto address_right = std::bit_cast<std::uintptr_t>(&Object(std::get<SquirrelObjectHandle>(right)));

				return address_left < address_right ? -1 : 1;
			}

			throw std::runtime_error(ComparisonError(left, right));
		}

		bool Compare(std::uint8_t operation, const SquirrelValue& left, const SquirrelValue& right) const {
			return Relation(operation, RawOrder(left, right));
		}

		static bool Relation(std::uint8_t operation, std::int32_t order) {
			switch (operation) {
			case 0:
				return order > 0;
			case 2:
				return order >= 0;
			case 3:
				return order < 0;
			case 4:
				return order <= 0;
			default:
				throw std::runtime_error("Squirrel unknown ordering operation");
			}
		}

		SquirrelValue& GetterOutputCell(const VmGetterOutput& output) {
			return output.PhysicalCell ? Active().Stack.At(*output.PhysicalCell) : *output.Value;
		}

		void ApplyReturn(const VmReturnAction& action, const SquirrelValue& value) {
			if (action.Kind == VmReturnKind::GetterOutput) {
				if (!action.UsesNativeTemporary) {
					AssignStoredValue(GetterOutputCell(*action.GetterOutput), action.InputOverride ? *action.InputOverride : value);
				}

				action.GetterOutput->Succeeded = true;
				action.GetterOutput->Completed = true;

				return;
			}

			if (action.Kind == VmReturnKind::PrepareCall) {
				auto& frame = Active().Frames.back();
				AssignStoredValue(Register(frame, static_cast<std::int64_t>(action.SecondaryRegister)), action.Operand);
				WriteReturn(action.Register, value);

				return;
			}

			if (action.Kind == VmReturnKind::SlotRead) {
				if (action.FollowingAction) {
					ApplyReturn(*action.FollowingAction, value);
				} else {
					WriteReturn(action.Register, value);
				}

				return;
			}

			if (action.Kind == VmReturnKind::CompoundRead) {
				FinishCompoundRead(*action.Compound, value);

				return;
			}

			if (action.Kind == VmReturnKind::CompoundArithmetic) {
				FinishCompoundArithmetic(*action.Compound, value);

				return;
			}

			if (action.Kind == VmReturnKind::CompoundSet) {
				const auto& compound = *action.Compound;
				WriteReturn(compound.ResultRegister, compound.PostIncrement ? compound.Previous : compound.Computed);

				return;
			}

			if (action.Kind == VmReturnKind::AssignLocal) {
				auto& frame = Active().Frames.back();
				AssignStoredValue(Register(frame, static_cast<std::int64_t>(action.SecondaryRegister)), value);
				AssignStoredValue(Register(frame, static_cast<std::int64_t>(action.Register)), action.Operation == 39 ? action.Operand : value);

				return;
			}

			if (action.Kind == VmReturnKind::Exists) {
				WriteReturn(action.Register, true);

				return;
			}

			if (action.Kind == VmReturnKind::ClassInherited) {
				AssignStoredValue(Object(action.ClassMember->Receiver).Attributes, action.ClassMember->Attributes);

				return;
			}

			if (action.Kind == VmReturnKind::NewMember) {
				return;
			}

			SquirrelValue result = action.InputOverride ? *action.InputOverride : value;
			if (action.Kind == VmReturnKind::IteratorKey) {
				FinishIteratorKey(action, result);

				return;
			}

			if (action.Kind == VmReturnKind::IteratorValue || action.Kind == VmReturnKind::IteratorYield) {
				AssignStoredValue(Register(Active().Frames.back(), static_cast<std::int64_t>(action.Register)), result);
				if (action.Kind == VmReturnKind::IteratorValue) {
					Jump(Active().Frames.back(), 1);
				}

				return;
			}

			if (action.Kind == VmReturnKind::Override) {
				result = action.Operand;
			} else if (action.Kind == VmReturnKind::Order) {
				if (!std::holds_alternative<std::int32_t>(result)) {
					throw VmComparisonResultError(ComparisonError(action.Operand, action.Key));
				}

				result = Relation(action.Operation, std::get<std::int32_t>(result));
			} else if (action.Kind == VmReturnKind::Equality) {
				const bool equal = IntegerValue(result) == 0;
				result = action.Operation == 16 ? !equal : equal;
			}

			if (action.Kind == VmReturnKind::Capture) {
				AssignStoredValue(Active().Result, result);
			} else {
				WriteReturn(action.Register, result);
			}
		}

		std::optional<SquirrelValue> MetaMethod(const SquirrelValue& value, std::string_view name) const {
			const auto* handle = std::get_if<SquirrelObjectHandle>(&value);
			if (!handle) {
				return {};
			}

			const auto& object = Object(*handle);
			std::optional<SquirrelObjectHandle> methods;
			if (object.Type == SquirrelValueType::Table || object.Type == SquirrelValueType::UserData) {
				methods = object.Delegate;
			} else if (object.Type == SquirrelValueType::Instance) {
				return ClassMeta(Object(*object.InstanceClass), name);
			}

			if (methods) {
				if (const auto* slot = FindOwnSlot(Object(*methods), std::string(name)); slot && !std::holds_alternative<std::monostate>(slot->Value)) {
					return ResolveStoredValue(slot->Value);
				}
			}

			return {};
		}

		static std::optional<SquirrelValue> ClassMeta(const VmObject& object, std::string_view name) {
			const auto position = std::find(kMetaNames.begin(), kMetaNames.end(), name);
			if (position != kMetaNames.end()) {
				const auto& value = object.MetaMethods[static_cast<std::size_t>(position - kMetaNames.begin())];
				if (!std::holds_alternative<std::monostate>(value)) {
					return value;
				}
			}

			return {};
		}

		void CallMeta(const SquirrelValue& method, std::span<const SquirrelValue> arguments, std::uint8_t destination, std::optional<VmReturnAction> action = {}) {
			const auto type = Type(method);
			if (type != SquirrelValueType::Closure && type != SquirrelValueType::NativeClosure && type != SquirrelValueType::Class) {
				throw std::runtime_error("Squirrel metamethod is not callable");
			}

			const auto depth = Active().Frames.size();
			++m_MetaCallDepth;
			try {
				Call(method, arguments, destination, false, std::move(action));
			} catch (...) {
				--m_MetaCallDepth;
				throw;
			}

			--m_MetaCallDepth;
			if (Active().Frames.size() > depth) {
				Active().Frames.back().IsMetamethod = true;
			}
		}

		void WriteReturn(std::size_t destination, const SquirrelValue& value) {
			if (Active().Frames.empty()) {
				AssignStoredValue(Active().Result, value);
			} else if (destination != kDiscardRegister) {
				AssignStoredValue(Register(Active().Frames.back(), static_cast<std::int64_t>(destination)), value);
			}
		}

		void CopyInstanceFields(VmObject& instance, VmObject& source) {
			for (const auto& slot : source.Slots) {
				if (slot.IsField) {
					if (auto* existing = FindOwnSlot(instance, slot.Key)) {
						existing->Value = slot.Value;
					} else {
						if (instance.Slots.size() >= m_Limits.MaxContainerSlots) {
							throw std::runtime_error("Squirrel instance field limit exceeded");
						}

						VmSlot field;
						field.Key = BorrowedValue(slot.Key);
						field.Value = slot.Value;
						field.IsField = true;
						field.ClassOrdinal = slot.ClassOrdinal;
						instance.Slots.push_back(std::move(field));
					}
				}
			}
		}

		SquirrelObjectHandle CreateClass(const SquirrelValue& base, const SquirrelValue& attributes) {
			CheckValue(attributes);
			std::optional<SquirrelObjectHandle> parent;
			if (!std::holds_alternative<std::monostate>(base)) {
				if (Type(base) != SquirrelValueType::Class) {
					throw std::runtime_error("Squirrel inheritance requires a class");
				}

				parent = std::get<SquirrelObjectHandle>(base);
			}

			const auto handle = Allocate(SquirrelValueType::Class);
			auto& object = Object(handle);
			object.ParentClass = parent;
			if (parent) {
				object.Slots = Object(*parent).Slots;
				for (auto& slot : object.Slots) {
					slot.Key = BorrowedValue(slot.Key);
				}

				object.MetaMethods = Object(*parent).MetaMethods;
				object.MemberIndex = std::get<SquirrelObjectHandle>(Clone(*Object(*parent).MemberIndex));
			} else {
				object.MemberIndex = Allocate(SquirrelValueType::Table);
			}

			AssignStoredValue(object.Attributes, attributes);

			return handle;
		}

		SquirrelObjectHandle CreateUserData(std::span<const std::uint8_t> payload) {
			if (payload.size() > m_Limits.MaxUserDataBytes - m_UserDataBytes) {
				throw std::runtime_error("Squirrel userdata payload byte limit exceeded");
			}

			std::vector<std::uint8_t> owned(payload.begin(), payload.end());
			const auto handle = Allocate(SquirrelValueType::UserData);
			Object(handle).UserDataPayload = std::move(owned);
			m_UserDataBytes += payload.size();

			return handle;
		}

		VmObject& UserData(const SquirrelValue& value) const {
			auto& object = Object(value);
			if (object.Type != SquirrelValueType::UserData) {
				throw std::runtime_error("Squirrel userdata operation requires userdata");
			}

			return object;
		}

		SquirrelObjectHandle CreateInstance(const SquirrelValue& class_value) {
			if (Type(class_value) != SquirrelValueType::Class) {
				throw std::runtime_error("Squirrel instance creation requires a class");
			}

			const auto class_handle = std::get<SquirrelObjectHandle>(class_value);
			const auto instance = Allocate(SquirrelValueType::Instance);
			auto& created = Object(instance);
			created.InstanceClass = class_handle;
			CopyInstanceFields(created, Object(class_handle));
			std::optional<SquirrelObjectHandle> current = class_handle;
			for (std::size_t depth = 0; current; ++depth) {
				if (depth >= m_Limits.MaxObjects) {
					throw std::runtime_error("Squirrel class inheritance cycle");
				}

				auto& object = Object(*current);
				object.ClassLocked = true;
				current = object.ParentClass;
			}

			return instance;
		}

		std::int32_t ValueTypeMask(const SquirrelValue& value) const {
			switch (Type(value)) {
			case SquirrelValueType::Null:
				return 1;
			case SquirrelValueType::Integer:
				return 2;
			case SquirrelValueType::Float:
				return 4;
			case SquirrelValueType::Boolean:
				return 8;
			case SquirrelValueType::String:
				return 16;
			case SquirrelValueType::Table:
				return 32;
			case SquirrelValueType::Array:
				return 64;
			case SquirrelValueType::Closure:
				return 256;
			case SquirrelValueType::NativeClosure:
				return 512;
			case SquirrelValueType::Generator:
				return 1024;
			case SquirrelValueType::Class:
				return 16384;
			case SquirrelValueType::Instance:
				return 32768;
			case SquirrelValueType::WeakReference:
				return 65536;
			case SquirrelValueType::UserData:
				return 128;
			}

			throw std::runtime_error("native parameter type mask requires a recovered value type");
		}

		void CheckNativeParameters(const SquirrelNativeSignature& signature, std::span<const SquirrelValue> arguments) const {
			const auto count = static_cast<std::int64_t>(arguments.size());
			const auto expected = static_cast<std::int64_t>(signature.ParameterCheck);
			if ((expected > 0 && count != expected) || (expected < 0 && count < -expected)) {
				throw std::runtime_error("wrong number of parameters");
			}

			for (std::size_t index = 0; index < arguments.size() && index < signature.TypeCheck.size(); ++index) {
				const auto mask = signature.TypeCheck[index];
				if (mask != -1 && (mask & ValueTypeMask(arguments[index])) == 0) {
					throw std::runtime_error("native parameter " + std::to_string(index + 1) + " type mismatch: " + TypeName(Type(arguments[index])));
				}
			}
		}

		std::optional<std::pair<SquirrelValue, SquirrelValue>> SearchIteratorValue(const SquirrelValue& iterable, SquirrelValue& cursor) {
			const auto type = Type(iterable);
			if (type == SquirrelValueType::Instance) {
				const auto method = MetaMethod(iterable, "_nexti");
				if (!method) {
					throw std::runtime_error("native search instance has no _nexti hook");
				}

				const std::array<SquirrelValue, 2> arguments{iterable, cursor};
				cursor = m_Api.InvokeInContext(m_Api.CurrentContext(), *method, arguments, m_InstructionRemaining, false).Value;
				if (std::holds_alternative<std::monostate>(cursor)) {
					return {};
				}

				return std::pair<SquirrelValue, SquirrelValue>{cursor, m_Api.ReadSlot(iterable, cursor, m_InstructionRemaining)};
			}

			if (type == SquirrelValueType::Generator) {
				throw std::runtime_error("native search generator resumption requires a recovered nested resume protocol");
			}

			const auto index = std::holds_alternative<std::monostate>(cursor) ? 0 : IntegerValue(cursor);
			if (index < 0) {
				throw std::runtime_error("native search has a negative iterator cursor");
			}

			if (const auto* text = TryStringText(iterable)) {
				if (static_cast<std::size_t>(index) >= text->size()) {
					return {};
				}

				cursor = index + 1;
				const auto value = static_cast<std::int32_t>(std::bit_cast<std::int8_t>(static_cast<std::uint8_t>((*text)[static_cast<std::size_t>(index)])));

				return std::pair<SquirrelValue, SquirrelValue>{index, value};
			}

			const auto& object = Object(iterable);
			if (type == SquirrelValueType::Table || type == SquirrelValueType::Class) {
				const auto& members = type == SquirrelValueType::Table ? object : Object(*object.MemberIndex);
				if (members.TablePointerKeys) {
					throw std::runtime_error("Squirrel object-key table iteration requires native allocator provenance");
				}

				std::size_t position = static_cast<std::size_t>(index);
				while (position < members.Slots.size() && std::holds_alternative<std::monostate>(members.Slots[position].Key)) {
					++position;
				}

				if (position == members.Slots.size()) {
					return {};
				}

				cursor = static_cast<std::int32_t>(position + 1);
				const auto key = members.Slots[position].Key;
				const auto value = type == SquirrelValueType::Table ? ResolveStoredValue(members.Slots[position].Value) : GetSlot(iterable, key);

				return std::pair<SquirrelValue, SquirrelValue>{key, value};
			}

			if (type != SquirrelValueType::Array) {
				throw std::runtime_error("native search cannot iterate " + TypeName(type));
			}

			if (static_cast<std::size_t>(index) >= object.Elements.size()) {
				return {};
			}

			cursor = index + 1;

			return std::pair<SquirrelValue, SquirrelValue>{index, ResolveStoredValue(object.Elements[static_cast<std::size_t>(index)])};
		}

		SquirrelValue SearchValues(const SquirrelValue& iterable, const SquirrelValue& query, std::string_view operation) {
			const auto matches = operation == "findall" ? m_Api.CreateArray() : SquirrelValue{};
			SquirrelValue cursor;
			std::int32_t count = 0;
			for (std::size_t work = 0; work <= m_Limits.MaxContainerSlots; ++work) {
				const auto element = SearchIteratorValue(iterable, cursor);
				if (!element) {
					return operation == "findall" ? matches : operation == "includes" ? SquirrelValue(false)
					    : operation == "includeCount"                                 ? SquirrelValue(count)
					                                                                  : SquirrelValue{};
				}

				if (m_Api.CompareValues(element->second, query, m_InstructionRemaining) != 0) {
					continue;
				}

				if (operation == "find") {
					return element->first;
				}

				if (operation == "includes") {
					return true;
				}

				if (operation == "findall") {
					Append(matches, element->first);
				} else {
					++count;
				}
			}

			throw std::runtime_error("native container search exceeds work limit");
		}

		SquirrelValue ClosureInformation(const SquirrelValue& value) {
			const auto& closure = Object(value);
			const auto information = m_Api.CreateTable();
			if (closure.Type == SquirrelValueType::NativeClosure) {
				NewSlot(information, std::string("native"), true, false);
				NewSlot(information, std::string("name"), closure.Signature.Name ? SquirrelValue(*closure.Signature.Name) : SquirrelValue{}, false);
				NewSlot(information, std::string("paramscheck"), closure.Signature.ParameterCheck, false);
				SquirrelValue types;
				if (!closure.Signature.TypeCheck.empty()) {
					types = m_Api.CreateArray();
					for (const auto mask : closure.Signature.TypeCheck) {
						Append(types, mask);
					}
				}

				NewSlot(information, std::string("typecheck"), types, false);

				return information;
			}

			if (closure.Type != SquirrelValueType::Closure) {
				throw std::runtime_error("native closure reflection requires a callable closure");
			}

			const auto& function = *closure.Function;
			const auto parameters = m_Api.CreateArray();
			for (const auto& parameter : function.Parameters) {
				Append(parameters, LiteralValue(parameter));
			}

			if (function.HasVariableParameters) {
				Append(parameters, std::string("..."));
			}

			NewSlot(information, std::string("native"), false, false);
			NewSlot(information, std::string("name"), LiteralValue(function.Name), false);
			NewSlot(information, std::string("src"), LiteralValue(function.SourceName), false);
			NewSlot(information, std::string("parameters"), parameters, false);
			NewSlot(information, std::string("varargs"), function.HasVariableParameters, false);

			return information;
		}

		// Execute and CallNative share the original per-VM 100-level counter; host work guards remain separate.
		static bool NativeCallDepthExceeded(const VmExecutionContext& context) {
			static constexpr std::size_t kNativeCallLimit = 100;
			const auto meta_frames = static_cast<std::size_t>(std::count_if(context.Frames.begin(), context.Frames.end(), [](const VmFrame& frame) {
				return frame.IsMetamethod;
			}));

			return context.NativeCallDepth >= kNativeCallLimit || meta_frames >= kNativeCallLimit - context.NativeCallDepth;
		}

		static void FailNativeCallDepth(VmExecutionContext& context) {
			const std::string error = "Native stack overflow";
			AssignStoredValue(context.LastError, error);

			throw SquirrelVmException(error, error);
		}

		OperandStack& NativeStack() const {
			auto& context = Active();
			if (context.NativeFrames.empty() || !context.NativeFrames.back().PhysicalStackActive || context.NativeFrames.back().PhysicalBase != context.Stack.Base()) {
				throw VmSafetyFailure("Squirrel native stack requires an active native frame");
			}

			return context.Stack;
		}

		SquirrelNativeResult ReadNativeResult(SquirrelNativeResult result, bool temporary_output) {
			if (Active().CompletedResultObserverDepth != 0 && (result.Suspend || result.NativeStatus == -666)) {
				throw VmSafetyFailure("Squirrel completed result observer cannot accept suspension");
			}

			if (!result.NativeStatus) {
				if (temporary_output) {
					AssignStoredValue(Active().NativeTemporary, result.Value);
					result.Value = {};
				}

				return result;
			}

			if (result.Suspend) {
				throw VmSafetyFailure("Squirrel explicit native status cannot use compatibility suspension");
			}

			if (result.PushReturnedValue) {
				m_Api.PushNativeValue(result.Value);
			}

			const auto status = *result.NativeStatus;
			if (status < 0 && status != -666) {
				const auto error = Active().LastError;

				throw SquirrelVmException(error, "Squirrel native callback returned failure");
			}

			SquirrelNativeResult returned;
			if (status != 0) {
				auto& stack = NativeStack();
				if (stack.Top() == stack.Base()) {
					throw VmSafetyFailure("Squirrel native return requires a current stack value");
				}

				if (temporary_output) {
					AssignStoredValue(Active().NativeTemporary, stack.At(stack.Top() - 1));
				} else {
					returned.Value = stack.At(stack.Top() - 1);
				}

				stack.Store(stack.Top() - 1, {});
			} else if (temporary_output) {
				AssignStoredValue(Active().NativeTemporary, {});
			}

			returned.Suspend = status == -666;
			returned.NativeStatus = result.NativeStatus;

			return returned;
		}

		void Call(const SquirrelValue& callable, std::span<const SquirrelValue> arguments, std::uint8_t destination, bool tail_call = false, std::optional<VmReturnAction> action = {}, std::optional<std::size_t> physical_base = {}) {
			if (arguments.size() > m_Limits.MaxRegisters) {
				throw std::runtime_error("Squirrel call arguments exceed register limit");
			}

			for (const auto& value : arguments) {
				CheckValue(value);
			}

			const auto* handle = std::get_if<SquirrelObjectHandle>(&callable);
			if (!handle) {
				throw std::runtime_error("Squirrel attempt to call " + TypeName(Type(callable)));
			}

			auto& object = Object(*handle);
			if (object.Type == SquirrelValueType::NativeClosure) {
				if (NativeCallDepthExceeded(Active())) {
					FailNativeCallDepth(Active());
				}

				if (Active().NativeFrames.size() >= m_Limits.MaxFrames) {
					throw VmSafetyFailure("Squirrel native call depth exceeds limit");
				}

				CheckNativeParameters(object.Signature, arguments);
				if (object.Captures.size() > m_Limits.MaxRegisters - arguments.size()) {
					throw VmSafetyFailure("Squirrel native captures and arguments exceed register limit");
				}

				if (object.BoundEnvironment && arguments.empty()) {
					throw std::runtime_error("Squirrel bound native closure lacks receiver argument");
				}

				if (arguments.size() > m_Limits.MaxRegisters) {
					throw std::runtime_error("Squirrel native argument count exceeds limit");
				}

				VmNativeFrameScope native_frame(Active(), *handle, arguments);
				const auto previous_top = Active().Stack.Top();
				OperandArgumentScope argument_owner(Active().Stack, !physical_base);
				const auto argument_base = physical_base.value_or(previous_top + 1);
				if (!physical_base) {
					Active().Stack.EnsurePushExtent(arguments.size() + 1);
					Active().Stack.Push(callable);
					for (const auto& argument : arguments) {
						Active().Stack.Push(argument);
					}
				} else {
					for (std::size_t index = 0; index < arguments.size(); ++index) {
						Active().Stack.Store(argument_base + index, arguments[index]);
					}
				}

				const auto stack_state = Active().Stack.EnterNativeFrame(argument_base, arguments.size());
				Active().NativeFrames.back().PhysicalBase = argument_base;
				Active().NativeFrames.back().PhysicalStackActive = true;
				VmNativeCallScope native_call(Active());
				VmOperandNativeScope operand_frame(Active().Stack, stack_state, physical_base ? std::nullopt : std::optional<std::size_t>(previous_top), Active().NativeFrames.back().PhysicalStackActive);
				argument_owner.TransferToAcceptedFrame();
				// Native254850 appends captures after checking only the original user arguments.
				std::vector<SquirrelValue> visible_arguments;
				if (!object.Captures.empty()) {
					Active().Stack.EnsurePushExtent(object.Captures.size());
					for (const auto& capture : object.Captures) {
						Active().Stack.Push(capture);
					}
				}

				// Native receiver binding follows every capture overwrite and its release callbacks.
				if (object.BoundEnvironment) {
					Active().Stack.Store(argument_base, m_Api.ResolveWeakReference(*object.BoundEnvironment));
				}

				if (!object.Captures.empty() || object.BoundEnvironment) {
					const auto visible_count = Active().Stack.Top() - argument_base;
					visible_arguments.reserve(visible_count);
					for (std::size_t index = 0; index < visible_count; ++index) {
						visible_arguments.push_back(Active().Stack.At(argument_base + index));
					}

					arguments = visible_arguments;
				}

				bool recovered = false;
				const auto result = [&] {
					try {
						const auto callback_result = object.Native(m_Api, arguments);
						// Native254b17 retires callback depth before output replacement, retaining the call record.
						native_call.Restore();

						auto read = ReadNativeResult(callback_result, action && action->UsesNativeTemporary);
						if (action && action->Kind == VmReturnKind::GetterOutput && !action->UsesNativeTemporary) {
							AssignStoredValue(GetterOutputCell(*action->GetterOutput), read.Value);
						}

						return read;
					} catch (const VmSafetyFailure&) {
						throw;
					} catch (const std::runtime_error& error) {
						AssignStoredValue(Active().LastError, RuntimeErrorValue(error));
						native_call.Restore();
						// Original failed CallNative returns before getter default recovery replaces output.
						operand_frame.Restore();
						native_frame.Restore();
						if (action && RecoverFailedAction(*action)) {
							recovered = true;

							return SquirrelNativeResult{};
						}

						if (action && !action->FailureOverride.empty()) {
							throw std::runtime_error(action->FailureOverride);
						}

						throw;
					}
				}();
				if (recovered) {
					return;
				}

				CheckValue(result.Value);
				operand_frame.Restore();
				if (result.Suspend && (!result.NativeStatus || (physical_base && !action))) {
					if (!result.NativeStatus && (Active().Frames.empty() || m_InvocationDepth > 1 || m_MetaCallDepth != 0 || std::any_of(Active().Frames.begin(), Active().Frames.end(), [](const VmFrame& frame) {
						    return frame.IsMetamethod;
					    }))) {
						throw std::runtime_error("cannot suspend through native calls/metamethods");
					}

					Active().Suspended = true;
					Active().SuspendedRegister = destination;
					Active().SuspendedAction = action;
					AssignStoredValue(Active().Result, result.Value);
				} else if (action) {
					ApplyReturn(*action, action->UsesNativeTemporary ? Active().NativeTemporary : result.Value);
				} else {
					WriteReturn(destination, result.Value);
				}

				if (action && action->Kind == VmReturnKind::GetterOutput) {
					native_frame.Restore();
				}

				return;
			}

			if (object.Type == SquirrelValueType::Class) {
				if (action && action->Kind == VmReturnKind::GetterOutput) {
					CallReadClassGetter(callable, arguments, *action, physical_base);

					return;
				}

				const auto instance = CreateInstance(callable);
				SquirrelValue method;
				if (physical_base) {
					if (const auto index = ClassMemberIndex(callable, std::string("constructor"))) {
						AssignStoredValue(Active().NativeTemporary, *index);
						AssignStoredValue(Active().NativeTemporary, IndexedClassMember(callable, Active().NativeTemporary));
					} else {
						AssignStoredValue(Active().NativeTemporary, {});
					}

					method = Active().NativeTemporary;
				} else if (HasSlot(callable, std::string("constructor"))) {
					method = GetSlot(callable, std::string("constructor"));
				}

				if (Type(method) != SquirrelValueType::Null) {
					std::vector<SquirrelValue> constructor_arguments(arguments.begin(), arguments.end());
					if (constructor_arguments.empty()) {
						throw std::runtime_error("Squirrel constructor lacks a receiver argument");
					}

					constructor_arguments[0] = instance;
					auto constructor_action = action.value_or(VmReturnAction{});
					constructor_action.Register = destination;
					constructor_action.InputOverride = instance;
					Call(method, constructor_arguments, destination, false, constructor_action, physical_base);
				} else if (action) {
					ApplyReturn(*action, instance);
				} else {
					WriteReturn(destination, instance);
				}

				return;
			}

			if (object.Type != SquirrelValueType::Closure) {
				if (const auto method = MetaMethod(callable, "_call")) {
					std::vector<SquirrelValue> expanded;
					expanded.reserve(arguments.size() + 1);
					expanded.push_back(callable);
					expanded.insert(expanded.end(), arguments.begin(), arguments.end());
					CallMeta(*method, expanded, destination, std::move(action));

					return;
				}

				throw std::runtime_error("Squirrel attempt to call unsupported " + TypeName(object.Type));
			}

			const bool generic_read = action && action->Kind == VmReturnKind::GetterOutput;
			if (!physical_base || generic_read) {
				AssignStoredValue(Active().NativeTemporary, callable);
			}

			auto frame = MakeFrame(*handle, arguments, destination);
			frame.ExecuteRoot = !physical_base || generic_read;

			const auto previous_top = Active().Stack.Top();
			OperandArgumentScope argument_owner(Active().Stack, !physical_base);
			const auto argument_base = physical_base.value_or(previous_top + 1);
			if (!physical_base) {
				Active().Stack.EnsurePushExtent(arguments.size() + 1);
				Active().Stack.Push(callable);
				for (const auto& argument : arguments) {
					Active().Stack.Push(argument);
				}
			}

			frame.ReturnAction = action;
			if (object.Function->IsGenerator) {
				const auto generator = Allocate(SquirrelValueType::Generator);
				Object(generator).GeneratorClosure = *handle;
				Object(generator).GeneratorRunning = true;
				frame.Generator = generator;
				PushFrame(std::move(frame), argument_base, physical_base ? std::nullopt : std::optional<std::size_t>(previous_top));
				argument_owner.TransferToAcceptedFrame();
				ReturnFrame(generator, true, true);

				return;
			}

			if (tail_call && !Active().Frames.empty()) {
				if (!Active().Frames.back().Traps.empty()) {
					throw std::runtime_error("Squirrel tail-call active-trap reuse is not recovered");
				}

				// Native255b23/25abb0 drops old variadic cells in reverse before the reused frame binds its callee.
				while (!Active().Frames.back().VariableArguments.empty()) {
					auto retired = std::move(Active().Frames.back().VariableArguments.back());
					Active().Frames.back().VariableArguments.pop_back();
					ReleaseStoredValue(retired);
				}

				frame.ReturnRegister = Active().Frames.back().ReturnRegister;
				frame.ReturnOverride = Active().Frames.back().ReturnOverride;
				frame.ReturnAction = Active().Frames.back().ReturnAction;
				frame.Generator = Active().Frames.back().Generator;
				frame.IsMetamethod = Active().Frames.back().IsMetamethod;
				frame.ExecuteRoot = Active().Frames.back().ExecuteRoot;
				const auto base = Active().Frames.back().Registers.Base();
				frame.StackState = Active().Frames.back().StackState;
				frame.ArgumentCleanupTop = Active().Frames.back().ArgumentCleanupTop;
				static_cast<void>(Active().Stack.EnterScriptFrame(base, frame.Registers.Size()));
				for (std::size_t index = 0; index < frame.VariableArguments.size(); ++index) {
					Active().Stack.Store(base + frame.PreparedParameters + index, {});
				}

				frame.Registers.Bind(Active().Stack, base, frame.PreparedParameters);
				Active().Frames.back() = std::move(frame);
				argument_owner.TransferToAcceptedFrame();
			} else {
				PushFrame(std::move(frame), argument_base, physical_base ? std::nullopt : std::optional<std::size_t>(previous_top));
				argument_owner.TransferToAcceptedFrame();
			}
		}

		void CallInstruction(VmFrame& frame, const SquirrelInstruction& instruction) {
			const auto callable = Register(frame, instruction.Argument1);
			AssignStoredValue(Active().NativeTemporary, callable);
			const auto start = static_cast<std::size_t>(instruction.Argument2);
			const auto count = static_cast<std::size_t>(instruction.Argument3);
			if (start > frame.Registers.Size() || count > frame.Registers.Size() - start) {
				throw std::runtime_error("Squirrel call arguments exceed frame");
			}

			std::vector<SquirrelValue> arguments;
			arguments.reserve(count);
			for (std::size_t index = 0; index < count; ++index) {
				arguments.push_back(frame.Registers[start + index]);
			}

			Call(callable, arguments, instruction.Argument0, instruction.Opcode == 5, {}, frame.Registers.Base() + start);
		}

		std::optional<SquirrelValue> ClassMemberIndex(const SquirrelValue& receiver, const SquirrelValue& key) const {
			const auto type = Type(receiver);
			if (type != SquirrelValueType::Class && type != SquirrelValueType::Instance) {
				return {};
			}

			const auto& object = Object(receiver);
			const auto& class_value = type == SquirrelValueType::Class ? object : Object(*object.InstanceClass);
			if (!class_value.MemberIndex) {
				return {};
			}

			if (const auto* slot = FindOwnSlot(Object(*class_value.MemberIndex), key)) {
				return slot->Value;
			}

			return {};
		}

		SquirrelValue IndexedClassMember(const SquirrelValue& receiver, const SquirrelValue& encoded) const {
			const auto* integer = std::get_if<std::int32_t>(&encoded);
			if (!integer) {
				throw VmSafetyFailure("Squirrel class lookup temporary is not a bounded encoded integer");
			}

			const auto bits = std::bit_cast<std::uint32_t>(*integer);
			const auto ordinal = bits & 0x00ffffff;
			const bool field = (bits & 0x02000000) != 0;
			const auto& object = Object(receiver);
			const auto& storage = object.Type == SquirrelValueType::Instance && !field ? Object(*object.InstanceClass) : object;
			const auto position = std::find_if(storage.Slots.begin(), storage.Slots.end(), [ordinal, field](const VmSlot& slot) {
				return slot.IsField == field && slot.ClassOrdinal == ordinal;
			});
			if (position == storage.Slots.end()) {
				throw VmSafetyFailure("Squirrel class lookup temporary index exceeds owned tuples");
			}

			return field ? ResolveStoredValue(position->Value) : position->Value;
		}

		void CallReadClassGetter(const SquirrelValue& callable, std::span<const SquirrelValue> arguments, const VmReturnAction& action, std::optional<std::size_t> physical_base) {
			if (!physical_base || arguments.empty() || m_GetterClassDepth >= m_Limits.MaxFrames) {
				throw VmSafetyFailure("Squirrel Class-valued getter exceeds its bounded generic call extent");
			}

			++m_GetterClassDepth;
			try {
				const auto instance = CreateInstance(callable);
				AssignStoredValue(GetterOutputCell(*action.GetterOutput), instance);
				SquirrelValue method;
				if (HasSlot(callable, std::string("constructor"))) {
					method = GetSlot(callable, std::string("constructor"));
				}

				if (Type(method) == SquirrelValueType::Null) {
					action.GetterOutput->Completed = true;
					action.GetterOutput->Succeeded = true;
				} else {
					// GenericClass writes the outer instance before assigning the constructor receiver.
					Active().Stack.Store(*physical_base, instance);
					const auto type = Type(method);
					if (type != SquirrelValueType::Closure && type != SquirrelValueType::NativeClosure && type != SquirrelValueType::Class) {
						action.GetterOutput->Completed = true;
						action.GetterOutput->Succeeded = false;
					} else {
						std::vector<SquirrelValue> constructor_arguments(arguments.begin(), arguments.end());
						constructor_arguments[0] = instance;
						SquirrelValue unused_output;
						const auto constructor_output = std::make_shared<VmGetterOutput>();
						constructor_output->Value = &unused_output;
						VmReturnAction constructor_action;
						constructor_action.Kind = VmReturnKind::GetterOutput;
						constructor_action.GetterOutput = constructor_output;
						const auto frame_floor = Active().Frames.size();
						Call(method, constructor_arguments, kDiscardRegister, false, constructor_action, physical_base);
						if (Active().Frames.size() > frame_floor) {
							static_cast<void>(Run(m_InstructionRemaining, frame_floor));
						}

						if (!constructor_output->Completed || Active().Frames.size() != frame_floor || Active().Suspended) {
							throw VmSafetyFailure("Squirrel Class-valued getter constructor suspension is not recovered");
						}

						// The unused constructor result retires while MetaCall still owns its arguments.
						ReleaseStoredValue(unused_output);
						action.GetterOutput->Succeeded = constructor_output->Succeeded;
						action.GetterOutput->Completed = true;
					}
				}
			} catch (...) {
				--m_GetterClassDepth;
				throw;
			}

			--m_GetterClassDepth;
		}

		bool InvokeReadGetter(const SquirrelValue& receiver, const SquirrelValue& key, SquirrelValue& output, bool temporary_output, std::optional<std::size_t> physical_cell = {}) {
			const auto frame_floor = Active().Frames.size();
			const auto previous_base = Active().Stack.Base();
			const auto previous_top = Active().Stack.Top();
			const bool previous_suspended = Active().Suspended;
			OperandArgumentScope argument_owner(Active().Stack, true);
			Active().Stack.EnsurePushExtent(2);
			Active().Stack.Push(receiver);
			Active().Stack.Push(key);
			// Original MetaCall resolves its local method only after both argument pushes.
			auto method = MetaMethod(receiver, "_get");
			const auto capture = std::make_shared<VmGetterOutput>();
			capture->Value = temporary_output ? &Active().NativeTemporary : &output;
			capture->PhysicalCell = physical_cell;
			const auto cleanup = [&] {
				while (Active().Frames.size() > frame_floor) {
					const auto& frame = Active().Frames.back();
					if (frame.Generator) {
						KillGenerator(Object(*frame.Generator));
					}

					const auto stack_state = frame.StackState;
					Active().Frames.pop_back();
					Active().Stack.LeaveScriptFrame(stack_state);
				}

				Active().Suspended = previous_suspended;
				if (!previous_suspended) {
					Active().SuspendedAction.reset();
				}

				OperandFrameState previous;
				previous.PreviousBase = previous_base;
				previous.PreviousTop = Active().Stack.Top();
				Active().Stack.LeaveNativeFrame(previous);
				Active().Stack.SetTop(previous_top - previous_base);
				argument_owner.TransferToAcceptedFrame();
			};
			if (!method || (Type(*method) != SquirrelValueType::Closure && Type(*method) != SquirrelValueType::NativeClosure && Type(*method) != SquirrelValueType::Class)) {
				cleanup();
				method.reset();

				return false;
			}

			if (previous_suspended) {
				cleanup();
				method.reset();
				throw VmSafetyFailure("Squirrel delegated getter call from a suspended context is not recovered");
			}

			if ((!m_Running || (Active().InvocationDepth == 0 && Active().Frames.empty() && Active().NativeFrames.empty())) && m_InvocationDepth >= m_Limits.MaxFrames) {
				cleanup();
				method.reset();
				throw VmSafetyFailure("Squirrel native invocation depth limit exceeded");
			}

			VmGetterInvocationScope invocation(m_Running, m_InvocationDepth, Active(), m_MetaCallDepth);
			VmReturnAction action;
			action.Kind = VmReturnKind::GetterOutput;
			action.GetterOutput = capture;
			action.UsesNativeTemporary = temporary_output;
			try {
				const std::array<SquirrelValue, 2> arguments{receiver, key};
				Call(*method, arguments, kDiscardRegister, false, action, previous_top);
				if (Active().Frames.size() > frame_floor) {
					static_cast<void>(Run(m_InstructionRemaining, frame_floor));
				}
			} catch (const VmSafetyFailure&) {
				invocation.Restore();
				cleanup();
				method.reset();
				throw;
			} catch (const std::runtime_error& error) {
				AssignStoredValue(Active().LastError, RuntimeErrorValue(error));
				capture->Completed = true;
				capture->Succeeded = false;
			} catch (...) {
				invocation.Restore();
				cleanup();
				method.reset();
				throw;
			}

			invocation.Restore();
			const bool suspended = Active().Suspended;
			const bool completed = capture->Completed && Active().Frames.size() == frame_floor;
			cleanup();
			method.reset();
			if (!completed || suspended) {
				throw VmSafetyFailure("Squirrel delegated getter suspension is not recovered");
			}

			return capture->Succeeded;
		}

		void StoreReadOutput(SquirrelValue& output, const SquirrelValue& value, bool temporary_output, std::optional<std::size_t> physical_cell = {}) {
			if (physical_cell) {
				Active().Stack.Store(*physical_cell, value);

				return;
			}

			AssignStoredValue(temporary_output ? Active().NativeTemporary : output, value);
		}

		bool ReadDelegatedSlot(const SquirrelValue& receiver, const SquirrelValue& key, SquirrelValue& output, bool temporary_output, std::optional<std::size_t> physical_cell = {}, bool raw = false) {
			CheckValue(receiver);
			CheckValue(key);
			const auto current_key = [&] {
				return physical_cell ? BorrowedValue(Active().Stack.At(*physical_cell)) : BorrowedValue(key);
			};
			std::vector<VmDelegatedReadLevel> levels;
			VmDelegatedReadLevel first;
			first.Receiver = BorrowedValue(receiver);
			levels.push_back(std::move(first));
			bool succeeded = false;
			while (!levels.empty()) {
				auto& level = levels.back();
				auto& object = Object(level.Receiver);
				if (level.Phase == VmDelegatedReadPhase::OwnSlot) {
					if (object.Type == SquirrelValueType::Table) {
						if (const auto* slot = FindOwnSlot(object, current_key())) {
							StoreReadOutput(output, ResolveStoredValue(slot->Value), temporary_output, physical_cell);
							succeeded = true;
							levels.pop_back();

							continue;
						}
					}

					if (object.Delegate) {
						if (levels.size() >= m_Limits.MaxObjects) {
							throw VmSafetyFailure("Squirrel delegated getter traversal exceeds object limit");
						}

						// Native259580 owns this local independently of the parent's current delegate.
						level.DelegatedLocal = *object.Delegate;
						level.Phase = VmDelegatedReadPhase::DelegateResult;
						VmDelegatedReadLevel child;
						child.Receiver = BorrowedValue(SquirrelValue(*level.DelegatedLocal));
						levels.push_back(std::move(child));

						continue;
					}

					level.Phase = VmDelegatedReadPhase::Default;
				}

				if (level.Phase == VmDelegatedReadPhase::DelegateResult) {
					level.DelegatedLocal.reset();
					if (succeeded) {
						levels.pop_back();

						continue;
					}

					level.Phase = raw ? VmDelegatedReadPhase::Default : VmDelegatedReadPhase::Metamethod;
				}

				if (level.Phase == VmDelegatedReadPhase::Metamethod) {
					// Delegate retirement may replace this object's delegate; resolve the getter only now.
					succeeded = InvokeReadGetter(level.Receiver, current_key(), output, temporary_output, physical_cell);
					if (succeeded) {
						levels.pop_back();

						continue;
					}

					level.Phase = VmDelegatedReadPhase::Default;
				}

				if (!raw && object.Type == SquirrelValueType::Table) {
					if (const auto value = FindDefaultSlot(level.Receiver, current_key())) {
						StoreReadOutput(output, *value, temporary_output, physical_cell);
						succeeded = true;
						levels.pop_back();

						continue;
					}
				}

				succeeded = false;
				levels.pop_back();
			}

			return succeeded;
		}

		bool ReadAliasedProperty(std::size_t receiver_cell, std::size_t key_cell, bool raw) {
			const auto receiver = BorrowedValue(Active().Stack.At(receiver_cell));
			const auto key = BorrowedValue(Active().Stack.At(key_cell));
			const auto type = Type(receiver);
			SquirrelValue unused_output;
			if (type == SquirrelValueType::Table || type == SquirrelValueType::UserData) {
				return ReadDelegatedSlot(receiver, key, unused_output, false, key_cell, raw);
			}

			if (type == SquirrelValueType::Class || type == SquirrelValueType::Instance) {
				if (const auto index = ClassMemberIndex(receiver, key)) {
					Active().Stack.Store(key_cell, *index);
					const auto value = IndexedClassMember(receiver, Active().Stack.At(key_cell));
					Active().Stack.Store(key_cell, value);

					return true;
				}

				if (raw || type == SquirrelValueType::Class) {
					return false;
				}

				if (InvokeReadGetter(receiver, key, unused_output, false, key_cell)) {
					return true;
				}
			} else if (raw) {
				return false;
			}

			// Property keys are strings, so array/string numeric indexing is not this operation's domain.
			if (const auto value = FindDefaultSlot(receiver, BorrowedValue(Active().Stack.At(key_cell)))) {
				Active().Stack.Store(key_cell, *value);

				return true;
			}

			return false;
		}

		SquirrelValue ReadDelegatedHost(const SquirrelValue& receiver, const SquirrelValue& key, std::uint64_t instruction_budget) {
			if (!m_Running) {
				m_InstructionRemaining = instruction_budget;
			}

			SquirrelValue output;
			if (ReadDelegatedSlot(receiver, key, output, false)) {
				return output;
			}

			AssignStoredValue(Active().LastError, std::string("the index doesn't exist"));
			throw SquirrelVmException(std::string("the index doesn't exist"), "the index doesn't exist");
		}

		void ReadSlotInstruction(VmFrame& frame, const SquirrelValue& receiver, const SquirrelValue& key, std::uint8_t destination, std::optional<VmReturnAction> action = {}, bool temporary_output = false) {
			const auto receiver_type = Type(receiver);
			if (receiver_type == SquirrelValueType::Table || receiver_type == SquirrelValueType::UserData) {
				SquirrelValue output;
				if (!ReadDelegatedSlot(receiver, key, output, temporary_output)) {
					const auto fallback = FindReadFallback(frame, receiver, key);
					if (!fallback) {
						throw std::runtime_error(action && !action->FailureOverride.empty() ? action->FailureOverride : MissingIndexError(key));
					}

					StoreReadOutput(output, *fallback, temporary_output);
				}

				const auto& value = temporary_output ? Active().NativeTemporary : output;
				if (action) {
					ApplyReturn(*action, value);
				} else {
					AssignStoredValue(Register(frame, destination), value);
				}

				return;
			}

			if (HasSlot(receiver, key) || HasDelegatedTableDefault(receiver, key)) {
				SquirrelValue read;
				if (const auto index = temporary_output ? ClassMemberIndex(receiver, key) : std::optional<SquirrelValue>{}) {
					AssignStoredValue(Active().NativeTemporary, *index);
					read = IndexedClassMember(receiver, Active().NativeTemporary);
				} else {
					read = GetSlot(receiver, key);
				}

				if (temporary_output) {
					AssignStoredValue(Active().NativeTemporary, read);
				}

				const auto& value = temporary_output ? Active().NativeTemporary : read;
				if (action) {
					ApplyReturn(*action, value);
				} else {
					AssignStoredValue(Register(frame, destination), value);
				}

				return;
			}

			if (const auto method = MetaMethod(receiver, "_get")) {
				VmReturnAction read_action;
				read_action.Kind = VmReturnKind::SlotRead;
				read_action.UsesNativeTemporary = temporary_output;
				read_action.Register = destination;
				read_action.Operand = receiver;
				read_action.Key = key;
				read_action.FailureOverride = action && !action->FailureOverride.empty() ? action->FailureOverride : MissingIndexError(key);
				if (action) {
					// The getter frame and failure path share the immutable caller continuation.
					read_action.FollowingAction = std::make_shared<const VmReturnAction>(std::move(*action));
				}

				const std::array<SquirrelValue, 2> arguments{receiver, key};
				try {
					CallMeta(*method, arguments, destination, read_action);
				} catch (const VmSafetyFailure&) {
					throw;
				} catch (const std::runtime_error&) {
					if (!RecoverFailedAction(read_action)) {
						throw std::runtime_error(read_action.FailureOverride);
					}
				}

				return;
			}

			const auto fallback = FindReadFallback(frame, receiver, key);
			if (!fallback) {
				throw std::runtime_error(MissingIndexError(key));
			}

			if (temporary_output) {
				AssignStoredValue(Active().NativeTemporary, *fallback);
			}

			const auto& value = temporary_output ? Active().NativeTemporary : *fallback;
			if (action) {
				ApplyReturn(*action, value);
			} else {
				AssignStoredValue(Register(frame, destination), value);
			}
		}

		bool TryAssignRootSlot(const VmFrame& frame, const SquirrelValue& receiver, const SquirrelValue& key, const SquirrelValue& value) {
			const auto type = Type(receiver);
			if ((type != SquirrelValueType::Table && type != SquirrelValueType::Instance && type != SquirrelValueType::UserData) || frame.Registers.Empty() || !EqualKeys(frame.Registers[0], receiver)) {
				return false;
			}

			if (Type(Active().RootTable) != SquirrelValueType::Table) {
				throw VmSafetyFailure("Squirrel root assignment requires a nonnull table");
			}

			// Native 0x14025b2c1 retries the current root only after a failed receiver assignment.
			// Its raw table setter 0x14025d1d0 neither follows delegates nor creates a slot.
			auto* slot = FindOwnSlot(Object(Active().RootTable), key);
			if (!slot) {
				return false;
			}

			AssignStoredValue(slot->Value, value);

			return true;
		}

		void SlotInstruction(VmFrame& frame, const SquirrelInstruction& instruction) {
			const auto receiver = Register(frame, instruction.Argument1);
			const auto key = Register(frame, instruction.Argument2);
			if (instruction.Opcode == 12) {
				if (const auto method = MetaMethod(receiver, "_delslot")) {
					const std::array<SquirrelValue, 2> arguments{receiver, key};
					CallMeta(*method, arguments, instruction.Argument0);
				} else {
					AssignStoredValue(Register(frame, instruction.Argument0), DeleteSlot(receiver, key));
				}

				return;
			}

			const auto value = Register(frame, instruction.Argument3);
			const auto& object = Object(receiver);
			if (object.Type == SquirrelValueType::UserData && instruction.Opcode == 11) {
				NewSlot(receiver, key, value, false);
			}

			const bool existing = instruction.Opcode == 11 ? FindOwnSlot(Object(receiver), key) != nullptr : CanAssignRaw(receiver, key);
			if (!existing) {
				if (const auto method = MetaMethod(receiver, instruction.Opcode == 11 ? "_newslot" : "_set")) {
					const std::array<SquirrelValue, 3> arguments{receiver, key, value};
					VmReturnAction action;
					action.Kind = instruction.Opcode == 11 ? VmReturnKind::Override : VmReturnKind::SlotWrite;
					action.Register = instruction.Argument0;
					AssignStoredValue(action.Operand, instruction.Opcode == 11 ? value : receiver);
					if (action.Kind == VmReturnKind::SlotWrite) {
						AssignStoredValue(action.Key, key);
						action.InputOverride = value;
						action.FailureOverride = MissingIndexError(key);
					}

					try {
						CallMeta(*method, arguments, instruction.Argument0, action);
					} catch (const VmSafetyFailure&) {
						throw;
					} catch (const std::runtime_error&) {
						if (action.Kind != VmReturnKind::SlotWrite) {
							throw;
						}

						if (!RecoverFailedAction(action)) {
							throw std::runtime_error(action.FailureOverride);
						}
					}

					return;
				}
			}

			if (instruction.Opcode == 11) {
				NewSlot(receiver, key, value, false);
			} else if (existing || !TryAssignRootSlot(frame, receiver, key, value)) {
				SetSlot(receiver, key, value);
			}

			AssignStoredValue(Register(frame, instruction.Argument0), value);
		}

		void NegateInstruction(VmFrame& frame, const SquirrelInstruction& instruction) {
			const auto value = Register(frame, instruction.Argument1);
			if (!IsNumber(value)) {
				if (const auto method = MetaMethod(value, "_unm")) {
					const std::array<SquirrelValue, 1> arguments{value};
					VmReturnAction action;
					action.Register = instruction.Argument0;
					action.UsesNativeTemporary = true;
					CallMeta(*method, arguments, instruction.Argument0, action);

					return;
				}
			}

			AssignStoredValue(Register(frame, instruction.Argument0), Negate(value));
		}

		void TypeInstruction(VmFrame& frame, const SquirrelInstruction& instruction) {
			const auto value = Register(frame, instruction.Argument1);
			if (const auto method = MetaMethod(value, "_typeof")) {
				const std::array<SquirrelValue, 1> arguments{value};
				CallMeta(*method, arguments, instruction.Argument0);
			} else {
				AssignStoredValue(Register(frame, instruction.Argument0), TypeName(Type(value)));
			}
		}

		void CloneInstruction(VmFrame& frame, const SquirrelInstruction& instruction) {
			const auto original = Register(frame, instruction.Argument1);
			const auto copy = Clone(original);
			if (const auto method = MetaMethod(copy, "_cloned")) {
				const std::array<SquirrelValue, 2> arguments{copy, original};
				VmReturnAction action;
				action.Kind = VmReturnKind::Override;
				action.Register = instruction.Argument0;
				AssignStoredValue(action.Operand, copy);
				CallMeta(*method, arguments, instruction.Argument0, action);
			} else {
				AssignStoredValue(Register(frame, instruction.Argument0), copy);
			}
		}

		void EvaluateArithmetic(const SquirrelValue& left, const SquirrelValue& right, std::uint8_t operation, std::uint8_t destination, std::optional<VmReturnAction> action = {}) {
			if (IsNumber(left) && IsNumber(right)) {
				const auto result = Arithmetic(operation, left, right);
				if (action && action->UsesNativeTemporary) {
					AssignStoredValue(Active().NativeTemporary, result);
				}

				if (action) {
					ApplyReturn(*action, action->UsesNativeTemporary ? Active().NativeTemporary : result);
				} else {
					WriteReturn(destination, result);
				}

				return;
			}

			if (operation == '+' && (std::holds_alternative<SquirrelString>(left) || std::holds_alternative<SquirrelString>(right))) {
				const auto result = Arithmetic(operation, left, right);
				if (action && action->UsesNativeTemporary) {
					AssignStoredValue(Active().NativeTemporary, result);
				}

				if (action) {
					ApplyReturn(*action, action->UsesNativeTemporary ? Active().NativeTemporary : result);
				} else {
					WriteReturn(destination, result);
				}

				return;
			}

			std::string_view name;
			switch (operation) {
			case '+':
				name = "_add";
				break;
			case '-':
				name = "_sub";
				break;
			case '*':
				name = "_mul";
				break;
			case '/':
				name = "_div";
				break;
			case '%':
				name = "_modulo";
				break;
			default:
				throw std::runtime_error("Squirrel unknown arithmetic operation");
			}

			if (const auto method = MetaMethod(left, name)) {
				const std::array<SquirrelValue, 2> arguments{left, right};
				CallMeta(*method, arguments, destination, std::move(action));

				return;
			}

			const auto result = Arithmetic(operation, left, right);
			if (action && action->UsesNativeTemporary) {
				AssignStoredValue(Active().NativeTemporary, result);
			}

			if (action) {
				ApplyReturn(*action, action->UsesNativeTemporary ? Active().NativeTemporary : result);
			} else {
				WriteReturn(destination, result);
			}
		}

		void ArithmeticInstruction(VmFrame& frame, const SquirrelInstruction& instruction) {
			const auto left = Register(frame, instruction.Argument2);
			const auto right = Register(frame, instruction.Argument1);
			VmReturnAction action;
			action.Register = instruction.Argument0;
			action.UsesNativeTemporary = true;
			EvaluateArithmetic(left, right, instruction.Argument3, instruction.Argument0, action);
		}

		void CompareInstruction(VmFrame& frame, const SquirrelInstruction& instruction) {
			const auto left = Register(frame, instruction.Argument2);
			const auto right = instruction.Opcode == 40 || instruction.Argument3 == 0 ? Register(frame, instruction.Argument1) : Literal(frame, instruction.Argument1);
			if (((instruction.Opcode == 40 && Type(left) == Type(right) && !EqualKeys(left, right)) || (instruction.Opcode != 40 && Type(left) == SquirrelValueType::Instance)) && MetaMethod(left, "_cmp")) {
				const std::array<SquirrelValue, 2> arguments{left, right};
				VmReturnAction action;
				action.Kind = instruction.Opcode == 40 ? VmReturnKind::Order : VmReturnKind::Equality;
				action.Register = instruction.Argument0;
				action.Operation = instruction.Opcode == 40 ? instruction.Argument3 : instruction.Opcode;
				if (instruction.Opcode == 40) {
					action.Operand = left;
					action.Key = right;
					try {
						CallMeta(*MetaMethod(left, "_cmp"), arguments, instruction.Argument0, action);
					} catch (const VmComparisonResultError&) {
						throw;
					} catch (const VmSafetyFailure&) {
						throw;
					} catch (const std::runtime_error&) {
						static_cast<void>(RecoverFailedAction(action));
					}
				} else {
					CallMeta(*MetaMethod(left, "_cmp"), arguments, instruction.Argument0, action);
				}

				return;
			}

			if (instruction.Opcode == 40) {
				AssignStoredValue(Register(frame, instruction.Argument0), Compare(instruction.Argument3, left, right));
			} else {
				const bool equal = EqualValues(left, right);
				AssignStoredValue(Register(frame, instruction.Argument0), instruction.Opcode == 16 ? !equal : equal);
			}
		}

		void PrepareCall(VmFrame& frame, const SquirrelInstruction& instruction) {
			const auto receiver = Register(frame, instruction.Argument2);
			const auto key = instruction.Opcode == 8 ? Literal(frame, instruction.Argument1) : Register(frame, instruction.Argument1);
			VmReturnAction action;
			action.Kind = VmReturnKind::PrepareCall;
			action.Register = instruction.Argument0;
			action.SecondaryRegister = instruction.Argument3;
			action.UsesNativeTemporary = true;
			if (Type(receiver) == SquirrelValueType::Class && !HasSlot(receiver, key)) {
				if (const auto root_value = FindReadFallback(frame, receiver, key)) {
					action.Operand = Register(frame, 0);
					AssignStoredValue(Active().NativeTemporary, *root_value);
					ApplyReturn(action, Active().NativeTemporary);

					return;
				}

				if (const auto default_value = FindDefaultSlot(receiver, key)) {
					action.Operand = receiver;
					AssignStoredValue(Active().NativeTemporary, *default_value);
					ApplyReturn(action, Active().NativeTemporary);

					return;
				}
			}

			action.Operand = Type(receiver) == SquirrelValueType::Class && HasSlot(receiver, key) ? Register(frame, 0) : receiver;
			ReadSlotInstruction(frame, receiver, key, instruction.Argument0, action, true);
		}

		void SaveGeneratorFrame(VmObject& generator, VmFrame& active) {
			if (!generator.GeneratorFrame) {
				generator.GeneratorFrame.emplace();
			}

			auto& saved = *generator.GeneratorFrame;
			saved.Closure = active.Closure;
			saved.Traps = active.Traps;
			saved.Instruction = active.Instruction;
			saved.ReturnRegister = active.ReturnRegister;
			saved.ReturnOverride = active.ReturnOverride;
			saved.ReturnAction = active.ReturnAction;
			saved.IsMetamethod = active.IsMetamethod;
			saved.ExecuteRoot = active.ExecuteRoot;
			saved.PreparedParameters = active.Registers.Size();
			saved.PreparedVariableCells = false;
			saved.Generator.reset();
			active.Registers.TransferToSnapshot(saved.Registers);
			saved.VariableArguments.assign(active.VariableArguments.rbegin(), active.VariableArguments.rend());
		}

		void ReturnFrame(const SquirrelValue& value, bool yielding = false, bool initial_generator = false) {
			if (yielding && !Active().Frames.back().Generator) {
				throw std::runtime_error("Squirrel yield requires a generator");
			}

			const SquirrelValue returned = value;
			if (yielding && !initial_generator) {
				AssignStoredValue(Active().NativeTemporary, returned);
			}

			std::optional<SquirrelObjectHandle> generator_pin;
			if (Active().Frames.back().Generator) {
				generator_pin = *Active().Frames.back().Generator;
				auto& generator = Object(*generator_pin);
				if (yielding) {
					SaveGeneratorFrame(generator, Active().Frames.back());
					generator.GeneratorRunning = false;
				} else {
					KillGenerator(generator);
				}
			}

			auto frame = std::move(Active().Frames.back());
			Active().Frames.pop_back();
			const auto destination = frame.ReturnRegister;
			const auto action = frame.ReturnAction;
			const auto override = frame.ReturnOverride;
			const auto argument_cleanup = frame.ArgumentCleanupTop;
			const auto old_top = Active().Stack.RestoreScriptFrame(frame.StackState);
			for (std::size_t index = frame.VariableArguments.size(); index > 0; --index) {
				ReleaseStoredValue(frame.VariableArguments[index - 1]);
			}

			frame.Closure = {};
			if (frame.ExecuteRoot && !yielding) {
				AssignStoredValue(Active().NativeTemporary, returned);
			}

			if (!action) {
				WriteReturn(destination, override ? *override : returned);
			}

			Active().Stack.CleanupReturnedCells(old_top);
			if (argument_cleanup) {
				Active().Stack.SetTop(*argument_cleanup - Active().Stack.Base());
			}

			if (action) {
				if (initial_generator && action->UsesNativeTemporary) {
					AssignStoredValue(Active().NativeTemporary, returned);
				}

				ApplyReturn(*action, action->UsesNativeTemporary ? Active().NativeTemporary : (override ? *override : returned));
			}
		}

		void ResumeGenerator(const SquirrelValue& value, std::uint8_t destination, std::optional<VmReturnAction> action = {}) {
			if (!std::holds_alternative<SquirrelObjectHandle>(value)) {
				throw std::runtime_error("Squirrel resume requires a generator");
			}

			const auto handle = std::get<SquirrelObjectHandle>(value);
			auto& generator = Object(handle);
			if (generator.Type != SquirrelValueType::Generator || generator.GeneratorDead || generator.GeneratorRunning || !generator.GeneratorFrame) {
				throw std::runtime_error("Squirrel generator cannot be resumed");
			}

			for (const auto& trap : generator.GeneratorFrame->Traps) {
				if (trap.StackState.PreviousBase != Active().Stack.Top()) {
					throw std::runtime_error("Squirrel generator trap relocation across different resumer extents is not recovered");
				}
			}

			auto frame = *generator.GeneratorFrame;
			frame.VariableArguments.assign(generator.GeneratorFrame->VariableArguments.rbegin(), generator.GeneratorFrame->VariableArguments.rend());
			if (!generator.GeneratorFrame->Registers.Empty()) {
				ReleaseStoredValue(generator.GeneratorFrame->Registers[0]);
			}

			for (std::size_t index = generator.GeneratorFrame->VariableArguments.size(); index > 0; --index) {
				ReleaseStoredValue(generator.GeneratorFrame->VariableArguments[index - 1]);
			}

			generator.GeneratorFrame->VariableArguments.clear();
			frame.ReturnRegister = destination;
			frame.ReturnAction = std::move(action);
			frame.Generator = handle;
			frame.ExecuteRoot = false;
			frame.PreparedParameters = frame.Registers.Size();
			frame.PreparedVariableCells = false;
			PushFrame(std::move(frame), Active().Stack.Top());
			generator.GeneratorRunning = true;
		}

		SquirrelValue WrapException(const SquirrelValue& value) {
			if (!m_ExceptionClass || !std::holds_alternative<SquirrelString>(value)) {
				return value;
			}

			const auto instance = CreateInstance(*m_ExceptionClass);
			if (const auto* constructor = FindSlot(Object(*m_ExceptionClass), std::string("constructor"))) {
				const auto method = constructor->Value;
				const std::array<SquirrelValue, 2> arguments{instance, value};
				try {
					static_cast<void>(m_Api.InvokeInContext(ContextHandle(m_ActiveContext), method, arguments, m_InstructionRemaining, false));
				} catch (const VmSafetyFailure&) {
					throw;
				} catch (const std::runtime_error&) {
					// Native exception construction retains the created instance even if its constructor call fails.
				}
			}

			return instance;
		}

		static bool HasFailureRecovery(const VmReturnAction& action) {
			return action.Kind == VmReturnKind::GetterOutput || action.Kind == VmReturnKind::ClassInherited || action.Kind == VmReturnKind::NewMember || action.Kind == VmReturnKind::CompoundSet || action.Kind == VmReturnKind::Exists || action.Kind == VmReturnKind::Order;
		}

		static SquirrelValue RuntimeErrorValue(const std::runtime_error& error) {
			if (const auto* script_error = dynamic_cast<const SquirrelVmException*>(&error)) {
				return script_error->ThrownValue();
			}

			return std::string(error.what());
		}

		void NotifyError(const SquirrelValue& value) {
			if (Active().HandlingError || std::holds_alternative<std::monostate>(Active().ErrorHandler)) {
				return;
			}

			const auto handler = Active().ErrorHandler;
			const std::array<SquirrelValue, 2> arguments{Active().RootTable, value};
			Active().HandlingError = true;
			try {
				static_cast<void>(m_Api.InvokeInContext(ContextHandle(m_ActiveContext), handler, arguments, m_InstructionRemaining, false));
			} catch (const VmSafetyFailure&) {
				throw;
			} catch (const std::runtime_error&) {
				// Error-handler failure does not replace the original error passed to the caller.
			} catch (...) {
				Active().HandlingError = false;
				throw;
			}

			Active().HandlingError = false;
		}

		void Propagate(const SquirrelValue& value, std::size_t frame_floor, std::string_view diagnostic = {}) {
			const auto failing_top = Active().Stack.Top();
			std::optional<std::size_t> argument_cleanup;
			const auto cleanup = [&] {
				Active().Stack.CleanupReturnedCells(failing_top);
				if (argument_cleanup) {
					Active().Stack.SetTop(*argument_cleanup - Active().Stack.Base());
				}
			};
			AssignStoredValue(Active().LastError, value);
			SquirrelValue propagated = WrapException(value);
			bool notify = Active().RaiseErrors;
			for (std::size_t index = frame_floor; index < Active().Frames.size(); ++index) {
				const auto& frame = Active().Frames[index];
				if (!frame.Traps.empty() || (frame.ReturnAction && HasFailureRecovery(*frame.ReturnAction))) {
					notify = false;
					break;
				}
			}

			while (Active().Frames.size() > frame_floor) {
				auto& frame = Active().Frames.back();
				if (notify && !frame.IsMetamethod) {
					NotifyError(propagated);
					notify = false;
				}

				auto& current = Active().Frames.back();
				if (!current.Traps.empty()) {
					const auto trap = current.Traps.back();
					current.Traps.pop_back();
					Active().Stack.LeaveNativeFrame(trap.StackState);
					AssignStoredValue(Register(current, static_cast<std::int64_t>(trap.Register)), propagated);
					current.Instruction = trap.Instruction;
					cleanup();

					return;
				}

				if (current.Generator) {
					auto& generator = Object(*current.Generator);
					KillGenerator(generator);
				}

				const auto action = current.ReturnAction;
				Active().Stack.LeaveNativeFrame(current.StackState);
				for (std::size_t index = current.VariableArguments.size(); index > 0; --index) {
					ReleaseStoredValue(current.VariableArguments[index - 1]);
				}

				current.Closure = {};
				if (current.ArgumentCleanupTop) {
					argument_cleanup = current.ArgumentCleanupTop;
				}

				Active().Frames.pop_back();
				if (action && (HasFailureRecovery(*action) || action->Kind == VmReturnKind::SlotRead || action->Kind == VmReturnKind::SlotWrite)) {
					cleanup();
					if (RecoverFailedAction(*action)) {
						return;
					}
				}

				if (action && !action->FailureOverride.empty()) {
					propagated = WrapException(action->FailureOverride);
				}
			}

			cleanup();
			AssignStoredValue(Active().LastError, propagated);
			const auto* reason = TryStringText(propagated);
			throw SquirrelVmException(propagated, "unhandled Squirrel exception: " + (reason ? *reason : TypeName(Type(propagated))) + (diagnostic.empty() ? std::string{} : "; " + std::string(diagnostic)));
		}

		SquirrelValue Parent(const SquirrelValue& value) const {
			const auto& object = Object(value);
			if (object.Type == SquirrelValueType::Class) {
				return object.ParentClass ? SquirrelValue(*object.ParentClass) : SquirrelValue{};
			}

			if (object.Type == SquirrelValueType::Table) {
				return object.Delegate ? SquirrelValue(*object.Delegate) : SquirrelValue{};
			}

			throw std::runtime_error("Squirrel value has no parent slot");
		}

		void SetDelegate(const SquirrelValue& value, const SquirrelValue& delegate) {
			auto& object = Object(value);
			if (object.Type != SquirrelValueType::Table) {
				throw std::runtime_error("Squirrel delegation requires a table");
			}

			SetDelegable(value, object, delegate);
		}

		void SetDelegable(const SquirrelValue& value, VmObject& object, const SquirrelValue& delegate) {
			if (std::holds_alternative<std::monostate>(delegate)) {
				ReleaseDelegateReference(object);
				object.Delegate.reset();

				return;
			}

			if (Type(delegate) != SquirrelValueType::Table) {
				throw std::runtime_error("Squirrel delegate must be a table or null");
			}

			const auto handle = std::get<SquirrelObjectHandle>(delegate);
			std::optional<SquirrelObjectHandle> current = handle;
			for (std::size_t depth = 0; current; ++depth) {
				if (*current == std::get<SquirrelObjectHandle>(value) || depth >= m_Limits.MaxObjects) {
					throw std::runtime_error("Squirrel delegate cycle detected");
				}

				current = Object(*current).Delegate;
			}

			ReleaseDelegateReference(object);
			object.Delegate = handle;
		}

		SquirrelValue Clone(const SquirrelValue& value) {
			const auto& object = Object(value);
			if (object.Type != SquirrelValueType::Table && object.Type != SquirrelValueType::Array && object.Type != SquirrelValueType::Instance) {
				throw std::runtime_error("Squirrel clone requires a table, array or instance");
			}

			const auto copy = Allocate(object.Type);
			if (object.Type == SquirrelValueType::Table) {
				auto& cloned = Object(copy);
				InitializeTable(cloned, object.Slots.size());
				for (const auto& slot : object.Slots) {
					if (!std::holds_alternative<std::monostate>(slot.Key)) {
						InsertTableSlot(cloned, slot.Key, slot.Value);
					}
				}

				cloned.Delegate = object.Delegate;
			} else {
				Object(copy) = object;
				if (object.Type == SquirrelValueType::Instance) {
					for (auto& slot : Object(copy).Slots) {
						slot.Key = BorrowedValue(slot.Key);
						ReleaseStoredValue(slot.Attributes);
					}

					Object(copy).ReleaseHook = {};
				}
			}
			Object(copy).HostIdentity.reset();
			Object(copy).WeakReference.reset();

			return copy;
		}

		void CompoundSlot(VmFrame& frame, const SquirrelInstruction& instruction) {
			const auto packed = std::bit_cast<std::uint32_t>(instruction.Argument1);
			VmCompoundSlot compound;
			compound.Receiver = Register(frame, instruction.Opcode == 34 ? packed >> 16 : packed);
			AssignStoredValue(compound.Key, Register(frame, instruction.Argument2));
			AssignStoredValue(compound.Operand, instruction.Opcode == 34 ? Register(frame, packed & 65535) : SquirrelValue(static_cast<std::int32_t>(std::bit_cast<std::int8_t>(instruction.Argument3))));
			compound.Operation = instruction.Opcode == 34 ? instruction.Argument3 : '+';
			compound.ResultRegister = instruction.Argument0;
			compound.PostIncrement = instruction.Opcode == 38;
			VmReturnAction action;
			action.Kind = VmReturnKind::CompoundRead;
			action.Compound = compound;
			ReadSlotInstruction(frame, compound.Receiver, compound.Key, kDiscardRegister, action);
		}

		void FinishCompoundRead(VmCompoundSlot compound, const SquirrelValue& previous) {
			AssignStoredValue(compound.Previous, previous);
			VmReturnAction action;
			action.Kind = VmReturnKind::CompoundArithmetic;
			action.Compound = compound;
			EvaluateArithmetic(previous, compound.Operand, compound.Operation, kDiscardRegister, action);
		}

		bool CanAssignRaw(const SquirrelValue& receiver, const SquirrelValue& key) const {
			if (Type(receiver) == SquirrelValueType::Instance) {
				return FindOwnSlot(Object(receiver), key) != nullptr;
			}

			return (Type(receiver) == SquirrelValueType::Array || Type(receiver) == SquirrelValueType::Table) && HasSlot(receiver, key);
		}

		void FinishCompoundArithmetic(VmCompoundSlot compound, const SquirrelValue& value) {
			AssignStoredValue(compound.Computed, value);
			WriteReturn(compound.ResultRegister, value);
			VmReturnAction action;
			action.Kind = VmReturnKind::CompoundSet;
			action.Compound = compound;
			if (!CanAssignRaw(compound.Receiver, compound.Key)) {
				if (const auto method = MetaMethod(compound.Receiver, "_set")) {
					const std::array<SquirrelValue, 3> arguments{compound.Receiver, compound.Key, value};
					CallMeta(*method, arguments, kDiscardRegister, action);

					return;
				}
			}

			try {
				if (EqualKeys(compound.Receiver, Register(Active().Frames.back(), 0)) && !CanAssignRaw(compound.Receiver, compound.Key) && HasSlot(Active().RootTable, compound.Key)) {
					SetSlot(Active().RootTable, compound.Key, value);
				} else {
					SetSlot(compound.Receiver, compound.Key, value);
				}
			} catch (const VmSafetyFailure&) {
				throw;
			} catch (const std::runtime_error&) {
				// Native compound helper 0x1402554b0 ignores the setter's failed-call status.
			}

			ApplyReturn(action, {});
		}

		void CompoundLocal(VmFrame& frame, const SquirrelInstruction& instruction) {
			const auto changed = instruction.Argument1;
			const auto destination = instruction.Argument0;
			const auto previous = Register(frame, changed);
			const auto operand = instruction.Opcode == 35 ? Register(frame, instruction.Argument2) : SquirrelValue(static_cast<std::int32_t>(std::bit_cast<std::int8_t>(instruction.Argument3)));
			VmReturnAction action;
			action.Kind = VmReturnKind::AssignLocal;
			action.Register = static_cast<std::size_t>(destination);
			action.SecondaryRegister = static_cast<std::size_t>(changed);
			action.Operation = instruction.Opcode;
			AssignStoredValue(action.Operand, previous);
			EvaluateArithmetic(previous, operand, instruction.Opcode == 35 ? instruction.Argument3 : '+', kDiscardRegister, action);
		}

		SquirrelValue Negate(const SquirrelValue& value) const {
			if (const auto* number = std::get_if<float>(&value)) {
				return std::bit_cast<float>(std::bit_cast<std::uint32_t>(*number) ^ 0x80000000);
			}

			return Arithmetic('-', std::int32_t{0}, value);
		}

		bool InstanceClassHasTypeTag(const SquirrelValue& value, const void* type_tag) const {
			const auto& instance = Object(value);
			if (instance.Type != SquirrelValueType::Instance || !instance.InstanceClass) {
				throw std::runtime_error("Squirrel class-tag lookup requires a live class instance");
			}
			if (!type_tag) {
				return true;
			}

			auto current = instance.InstanceClass;
			for (std::size_t depth = 0; current; ++depth) {
				if (depth >= m_Limits.MaxObjects) {
					throw std::runtime_error("Squirrel class-tag ancestry exceeds object limit");
				}

				const auto& class_value = Object(*current);
				if (class_value.ClassTag == type_tag) {
					return true;
				}

				current = class_value.ParentClass;
			}

			return false;
		}

		bool InstanceOf(const SquirrelValue& value, const SquirrelValue& base) const {
			// Original opcode42 returns false for a non-class right operand, including optional renderers.
			if (Type(base) != SquirrelValueType::Class) {
				return false;
			}

			if (Type(value) != SquirrelValueType::Instance) {
				return false;
			}

			auto current = Object(value).InstanceClass;
			for (std::size_t depth = 0; current; ++depth) {
				if (*current == std::get<SquirrelObjectHandle>(base)) {
					return true;
				}

				if (depth >= m_Limits.MaxObjects) {
					throw std::runtime_error("Squirrel class inheritance cycle");
				}

				current = Object(*current).ParentClass;
			}

			return false;
		}

		void NewClass(VmFrame& frame, const SquirrelInstruction& instruction) {
			const auto base = instruction.Argument1 == -1 ? SquirrelValue{} : Register(frame, instruction.Argument1);
			const auto attributes = instruction.Argument2 == kDiscardRegister ? SquirrelValue{} : Register(frame, instruction.Argument2);
			const auto created = CreateClass(base, {});
			AssignStoredValue(Register(frame, instruction.Argument0), created);
			if (const auto method = ClassMeta(Object(created), "_inherited")) {
				VmReturnAction action;
				action.Kind = VmReturnKind::ClassInherited;
				action.ClassMember.emplace();
				action.ClassMember->Receiver = created;
				action.ClassMember->Attributes = attributes;
				const std::array<SquirrelValue, 2> arguments{created, attributes};
				CallMeta(*method, arguments, kDiscardRegister, action);
			} else {
				AssignStoredValue(Object(created).Attributes, attributes);
			}
		}

		void NewAttributedSlot(VmFrame& frame, const SquirrelInstruction& instruction) {
			VmClassMember member;
			member.Receiver = Register(frame, instruction.Argument1);
			AssignStoredValue(member.Key, Register(frame, instruction.Argument2));
			AssignStoredValue(member.Value, Register(frame, instruction.Argument3));
			member.IsStatic = (instruction.Argument0 & 2) != 0;
			member.HasAttributes = (instruction.Argument0 & 1) != 0;
			if (member.HasAttributes) {
				AssignStoredValue(member.Attributes, Register(frame, static_cast<std::int32_t>(instruction.Argument2) - 1));
			}

			if (Type(member.Receiver) == SquirrelValueType::Class) {
				if (const auto method = ClassMeta(Object(member.Receiver), "_newmember")) {
					VmReturnAction action;
					action.Kind = VmReturnKind::NewMember;
					action.UsesNativeTemporary = true;
					action.ClassMember = member;
					const std::array<SquirrelValue, 4> arguments{member.Receiver, member.Key, member.Value, member.Attributes};
					CallMeta(*method, arguments, kDiscardRegister, action);

					return;
				}
			}

			InsertClassMember(member);
		}

		void InsertClassMember(const VmClassMember& member) {
			NewSlot(member.Receiver, member.Key, member.Value, member.IsStatic);
			if (member.HasAttributes) {
				if (auto* slot = FindOwnSlot(Object(member.Receiver), member.Key)) {
					AssignStoredValue(slot->Attributes, member.Attributes);
				}
			}
		}

		bool RecoverFailedAction(const VmReturnAction& action) {
			if (action.Kind == VmReturnKind::GetterOutput) {
				action.GetterOutput->Completed = true;
				action.GetterOutput->Succeeded = false;

				return true;
			}

			if (action.Kind == VmReturnKind::Order) {
				ApplyReturn(action, RawOrder(action.Operand, action.Key));

				return true;
			}

			if (action.Kind == VmReturnKind::SlotRead && !Active().Frames.empty()) {
				if (const auto value = FindReadFallback(Active().Frames.back(), action.Operand, action.Key)) {
					if (action.UsesNativeTemporary) {
						AssignStoredValue(Active().NativeTemporary, *value);
					}

					ApplyReturn(action, action.UsesNativeTemporary ? Active().NativeTemporary : *value);

					return true;
				}
			}

			if (action.Kind == VmReturnKind::SlotWrite && !Active().Frames.empty()) {
				if (TryAssignRootSlot(Active().Frames.back(), action.Operand, action.Key, *action.InputOverride)) {
					ApplyReturn(action, {});

					return true;
				}
			}

			if (action.Kind == VmReturnKind::CompoundSet) {
				ApplyReturn(action, {});

				return true;
			}

			if (action.Kind == VmReturnKind::Exists) {
				WriteReturn(action.Register, false);

				return true;
			}

			if (action.Kind == VmReturnKind::ClassInherited) {
				ApplyReturn(action, {});

				return true;
			}

			if (action.Kind == VmReturnKind::NewMember) {
				InsertClassMember(*action.ClassMember);

				return true;
			}

			return false;
		}

		void ExistsInstruction(VmFrame& frame, const SquirrelInstruction& instruction) {
			const auto receiver = Register(frame, instruction.Argument1);
			const auto key = Register(frame, instruction.Argument2);
			if (HasSlot(receiver, key)) {
				AssignStoredValue(Register(frame, instruction.Argument0), true);

				return;
			}

			if (const auto method = MetaMethod(receiver, "_exist")) {
				VmReturnAction action;
				action.Kind = VmReturnKind::Exists;
				action.Register = instruction.Argument0;
				const std::array<SquirrelValue, 2> arguments{receiver, key};
				CallMeta(*method, arguments, instruction.Argument0, action);
			} else {
				AssignStoredValue(Register(frame, instruction.Argument0), false);
			}
		}

		void FinishIteratorKey(const VmReturnAction& action, const SquirrelValue& key) {
			auto& frame = Active().Frames.back();
			AssignStoredValue(Register(frame, static_cast<std::int64_t>(action.Register)), key);
			AssignStoredValue(Register(frame, static_cast<std::int64_t>(action.Register + 2)), key);
			if (std::holds_alternative<std::monostate>(key)) {
				Jump(frame, action.BranchDisplacement);

				return;
			}

			VmReturnAction value_action;
			value_action.Kind = VmReturnKind::IteratorValue;
			value_action.Register = action.Register + 1;
			value_action.FailureOverride = "_nexti returned an invalid idx";
			try {
				ReadSlotInstruction(frame, action.Operand, key, kDiscardRegister, value_action);
			} catch (const VmSafetyFailure&) {
				throw;
			} catch (const std::runtime_error&) {
				throw std::runtime_error(value_action.FailureOverride);
			}
		}

		void ForEach(VmFrame& frame, const SquirrelInstruction& instruction) {
			const auto iterable = Register(frame, instruction.Argument0);
			const auto start = static_cast<std::size_t>(instruction.Argument2);
			const auto cursor = Register(frame, static_cast<std::int64_t>(start + 2));
			if (Type(iterable) == SquirrelValueType::Instance) {
				const auto method = MetaMethod(iterable, "_nexti");
				if (!method) {
					throw std::runtime_error("Squirrel cannot iterate instance without _nexti");
				}

				VmReturnAction action;
				action.Kind = VmReturnKind::IteratorKey;
				action.Register = start;
				AssignStoredValue(action.Operand, iterable);
				action.BranchDisplacement = instruction.Argument1;
				action.FailureOverride = "_nexti failed";
				const std::array<SquirrelValue, 2> arguments{iterable, cursor};
				CallMeta(*method, arguments, kDiscardRegister, action);

				return;
			}

			if (Type(iterable) == SquirrelValueType::Generator) {
				const auto& generator = Object(iterable);
				if (generator.GeneratorDead) {
					Jump(frame, instruction.Argument1);

					return;
				}

				if (generator.GeneratorRunning) {
					throw std::runtime_error("Squirrel cannot iterate an active generator");
				}

				const auto index = std::holds_alternative<std::int32_t>(cursor) ? WrapInteger(std::bit_cast<std::uint32_t>(std::get<std::int32_t>(cursor)) + 1) : 0;
				AssignStoredValue(Register(frame, static_cast<std::int64_t>(start)), index);
				AssignStoredValue(Register(frame, static_cast<std::int64_t>(start + 2)), index);
				VmReturnAction action;
				action.Kind = VmReturnKind::IteratorYield;
				action.Register = start + 1;
				ResumeGenerator(iterable, kDiscardRegister, action);

				return;
			}

			const auto index = std::holds_alternative<std::monostate>(cursor) ? 0 : IntegerValue(cursor);
			if (index < 0) {
				throw std::runtime_error("Squirrel negative iterator cursor");
			}

			if (const auto* text = TryStringText(iterable)) {
				if (static_cast<std::size_t>(index) >= text->size()) {
					Jump(frame, instruction.Argument1);

					return;
				}

				AssignStoredValue(Register(frame, static_cast<std::int64_t>(start)), index);
				AssignStoredValue(Register(frame, static_cast<std::int64_t>(start + 1)), static_cast<std::int32_t>(std::bit_cast<std::int8_t>(static_cast<std::uint8_t>((*text)[static_cast<std::size_t>(index)]))));
			} else {
				const auto& object = Object(iterable);
				if (object.Type == SquirrelValueType::Table || object.Type == SquirrelValueType::Class) {
					const auto& members = object.Type == SquirrelValueType::Table ? object : Object(*object.MemberIndex);
					if (members.TablePointerKeys) {
						throw std::runtime_error("Squirrel object-key table iteration requires native allocator provenance");
					}

					std::size_t position = static_cast<std::size_t>(index);
					while (position < members.Slots.size() && std::holds_alternative<std::monostate>(members.Slots[position].Key)) {
						++position;
					}

					if (position == members.Slots.size()) {
						Jump(frame, instruction.Argument1);

						return;
					}

					const auto& key = members.Slots[position].Key;
					AssignStoredValue(Register(frame, static_cast<std::int64_t>(start)), key);
					AssignStoredValue(Register(frame, static_cast<std::int64_t>(start + 1)), object.Type == SquirrelValueType::Class ? GetSlot(iterable, key) : ResolveStoredValue(members.Slots[position].Value));
					AssignStoredValue(Register(frame, static_cast<std::int64_t>(start + 2)), static_cast<std::int32_t>(position + 1));
					Jump(frame, 1);

					return;
				}

				if (object.Type != SquirrelValueType::Array) {
					throw std::runtime_error("Squirrel cannot iterate " + TypeName(object.Type));
				}

				if (static_cast<std::size_t>(index) >= object.Elements.size()) {
					Jump(frame, instruction.Argument1);

					return;
				}

				AssignStoredValue(Register(frame, static_cast<std::int64_t>(start)), index);
				AssignStoredValue(Register(frame, static_cast<std::int64_t>(start + 1)), ResolveStoredValue(object.Elements[static_cast<std::size_t>(index)]));
			}

			AssignStoredValue(Register(frame, static_cast<std::int64_t>(start + 2)), WrapInteger(std::bit_cast<std::uint32_t>(index) + 1));
			Jump(frame, 1);
		}

		std::int32_t CompareArraySort(const SquirrelValue& array, const SquirrelValue& value, const SquirrelValue& pivot, const std::optional<SquirrelValue>& comparator, std::size_t initial_size, std::uint64_t& comparisons) {
			if (comparisons >= kMaxArraySortComparisons) {
				throw std::runtime_error("native array sort comparison-work limit exceeded");
			}

			++comparisons;
			std::int32_t order = 0;
			if (comparator) {
				const std::array<SquirrelValue, 3> arguments{Active().RootTable, value, pivot};
				SquirrelRunResult result;
				try {
					result = m_Api.InvokeInContext(ContextHandle(m_ActiveContext), *comparator, arguments, m_InstructionRemaining, false);
				} catch (const SquirrelVmException& error) {
					if (std::holds_alternative<std::monostate>(error.ThrownValue())) {
						throw;
					}

					throw std::runtime_error("compare func failed");
				} catch (const VmSafetyFailure&) {
					throw;
				} catch (const std::runtime_error&) {
					throw std::runtime_error("compare func failed");
				}

				const auto converted = TryNativeInteger(result.Value);
				if (!converted) {
					throw std::runtime_error("native sort comparator returned a value with an uninitialized integer comparison result");
				}

				order = *converted;
			} else {
				order = m_Api.CompareValues(value, pivot);
			}

			if (Object(array).Elements.size() < initial_size) {
				throw std::runtime_error("native sort comparator shortened its array below the captured sort range");
			}

			return order;
		}

		void SortArray(const SquirrelValue& array, const std::optional<SquirrelValue>& comparator) {
			const auto initial_size = Object(array).Elements.size();
			if (initial_size <= 1) {
				return;
			}

			if (initial_size > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())) {
				throw std::runtime_error("native sort array exceeds its signed 32-bit range");
			}

			VmArraySortRange initial;
			initial.Last = static_cast<std::int32_t>(initial_size - 1);
			std::vector<VmArraySortRange> pending{initial};
			std::uint64_t comparisons = 0;
			while (!pending.empty()) {
				const auto range = pending.back();
				pending.pop_back();
				const auto pivot = Object(array).Elements[static_cast<std::size_t>(range.First)];
				auto left = range.First;
				auto right = range.Last + 1;
				while (true) {
					++left;
					while (left <= range.Last) {
						const auto value = Object(array).Elements[static_cast<std::size_t>(left)];
						if (CompareArraySort(array, value, pivot, comparator, initial_size, comparisons) > 0) {
							break;
						}

						++left;
					}

					while (true) {
						--right;
						if (right < 0) {
							throw std::runtime_error("Invalid qsort, probably compare function defect");
						}

						const auto value = Object(array).Elements[static_cast<std::size_t>(right)];
						if (CompareArraySort(array, value, pivot, comparator, initial_size, comparisons) <= 0) {
							break;
						}
					}

					if (left >= right) {
						break;
					}

					auto& elements = Object(array).Elements;
					std::swap(elements[static_cast<std::size_t>(left)], elements[static_cast<std::size_t>(right)]);
				}

				auto& elements = Object(array).Elements;
				std::swap(elements[static_cast<std::size_t>(range.First)], elements[static_cast<std::size_t>(right)]);
				if (right + 1 < range.Last) {
					VmArraySortRange next;
					next.First = right + 1;
					next.Last = range.Last;
					pending.push_back(next);
				}

				if (range.First < right - 1) {
					VmArraySortRange next;
					next.First = range.First;
					next.Last = right - 1;
					pending.push_back(next);
				}
			}
		}

		static void RequireArguments(std::span<const SquirrelValue> arguments, std::size_t minimum, std::size_t maximum) {
			if (arguments.size() < minimum || arguments.size() > maximum) {
				throw std::runtime_error("Squirrel wrong number of native parameters");
			}
		}

		static SquirrelNativeResult NativeValue(SquirrelValue value) {
			SquirrelNativeResult result;
			AssignStoredValue(result.Value, std::move(value));

			return result;
		}

		std::string PointerStringValue(const SquirrelValue& value) const {
			std::uint64_t payload = 0;
			if (const auto* handle = std::get_if<SquirrelObjectHandle>(&value)) {
				// This diagnostic uses the stable owned allocation identity, never a serialized address.
				static_assert(sizeof(std::uintptr_t) == sizeof(VmObject*));
				static_assert(sizeof(std::uintptr_t) <= sizeof(payload));
				payload = std::bit_cast<std::uintptr_t>(&Object(*handle));
			}

			std::string digits(16, '0');
			static constexpr std::string_view kHexDigits = "0123456789ABCDEF";
			for (std::size_t index = digits.size(); index != 0; --index) {
				digits[index - 1] = kHexDigits[static_cast<std::size_t>(payload & 15)];
				payload >>= 4;
			}

			return "(" + TypeName(Type(value)) + " : 0x" + digits + ")";
		}

		std::string StringValue(const SquirrelValue& value) const {
			if (std::holds_alternative<std::monostate>(value)) {
				return PointerStringValue(value);
			}

			if (const auto* text = TryStringText(value)) {
				return *text;
			}

			if (const auto* boolean = std::get_if<bool>(&value)) {
				return *boolean ? "true" : "false";
			}

			if (const auto* integer = std::get_if<std::int32_t>(&value)) {
				return std::to_string(*integer);
			}

			if (const auto* number = std::get_if<float>(&value)) {
				return FormatNativeFloat(*number);
			}

			return PointerStringValue(value);
		}

		static bool AsciiSpace(char character) {
			return character == ' ' || character == '\t' || character == '\n' || character == '\r' || character == '\f' || character == '\v';
		}

		static SquirrelValue ParseNumber(std::string_view text) {
			text = text.substr(0, text.find('\0'));
			std::uint32_t base = 10;
			if (text.starts_with("0x") || text.starts_with("0X")) {
				base = 16;
				text.remove_prefix(2);
			} else if (text.starts_with("0b") || text.starts_with("0B")) {
				base = 2;
				text.remove_prefix(2);
			}

			const bool floating = base == 10 && text.find('.') != std::string_view::npos;
			while (!text.empty() && AsciiSpace(text.front())) {
				text.remove_prefix(1);
			}

			if (floating) {
				if (!text.empty() && text.front() == '+') {
					text.remove_prefix(1);
				}

				double number = 0;
				std::istringstream input{std::string(text)};
				input.imbue(std::locale::classic());
				if (!(input >> number)) {
					throw std::runtime_error("Squirrel cannot convert the string");
				}

				return static_cast<float>(number);
			}

			bool negative = false;
			if (!text.empty() && (text.front() == '-' || text.front() == '+')) {
				negative = text.front() == '-';
				text.remove_prefix(1);
			}

			std::uint64_t number = 0;
			std::size_t digits = 0;
			for (const auto character : text) {
				const auto digit = character >= '0' && character <= '9' ? static_cast<std::uint32_t>(character - '0') : (character >= 'a' && character <= 'f' ? static_cast<std::uint32_t>(character - 'a' + 10) : (character >= 'A' && character <= 'F' ? static_cast<std::uint32_t>(character - 'A' + 10) : 255));
				if (digit >= base) {
					break;
				}

				if (number > (std::numeric_limits<std::uint32_t>::max() - digit) / base) {
					throw std::runtime_error("Squirrel numeric string overflow contract is not recovered");
				}

				number = number * base + digit;
				++digits;
			}

			if (digits == 0) {
				throw std::runtime_error("Squirrel cannot convert the string");
			}

			const auto bits = static_cast<std::uint32_t>(number);

			return WrapInteger(negative ? 0 - bits : bits);
		}

		SquirrelValue ConvertNumber(const SquirrelValue& value, std::string_view operation) const {
			SquirrelValue number;
			if (std::holds_alternative<SquirrelString>(value)) {
				try {
					number = ParseNumber(StringText(value));
				} catch (const VmSafetyFailure&) {
					throw;
				} catch (const std::runtime_error& error) {
					if (operation != "tonumber" || std::string_view(error.what()) != "Squirrel cannot convert the string") {
						throw;
					}

					return std::int32_t{0};
				}
			} else if (const auto* boolean = std::get_if<bool>(&value)) {
				number = static_cast<std::int32_t>(*boolean);
			} else if (IsNumber(value)) {
				number = value;
			} else {
				return operation == "tonumber" ? SquirrelValue(std::int32_t{0}) : SquirrelValue{};
			}

			if (operation == "tofloat") {
				return NumberFloat(number);
			}

			if (operation == "tointeger") {
				return NumericIndex(number);
			}

			return number;
		}

		void AddDelegate(SquirrelValueType type, std::string name, NativeCallback callback) {
			auto& delegate = m_DefaultDelegates[static_cast<std::size_t>(type)];
			if (!delegate) {
				delegate = Allocate(SquirrelValueType::Table);
			}

			const auto method = m_Api.CreateNativeClosure(name, std::move(callback), RecoveredDelegateSignature(type, name));
			NewSlot(*delegate, std::move(name), method, false);
		}

		void RegisterBaseSlot(const SquirrelValue& root, const SquirrelValue& key, const SquirrelValue& value);
		void RegisterBaseNative(const SquirrelValue& root, std::string name, NativeCallback callback);
		void InstallBaseGlobals();
		void InstallBaseLibrary();
		void InstallAdditionalDelegates();
		SquirrelValue StackInformation(std::int32_t level);
		void Step(VmFrame& frame, const SquirrelInstruction& instruction);

		void RequireCallAdmission(VmExecutionContext& state, const SquirrelValue& callable) {
			if (state.Suspended || (!m_Running && !state.Frames.empty())) {
				throw std::runtime_error("Squirrel context already has an active invocation");
			}

			const auto callable_type = Type(callable);
			if ((callable_type == SquirrelValueType::Closure || callable_type == SquirrelValueType::NativeClosure) && NativeCallDepthExceeded(state)) {
				FailNativeCallDepth(state);
			}
			if (m_InvocationDepth >= m_Limits.MaxFrames) {
				throw VmSafetyFailure("Squirrel native invocation depth limit exceeded");
			}
		}

		SquirrelRunResult Run(std::uint64_t instruction_budget, std::size_t frame_floor = 0) {
			std::optional<VmNativeCallScope> execute_scope;
			if (Active().Frames.size() > frame_floor) {
				if (NativeCallDepthExceeded(Active())) {
					FailNativeCallDepth(Active());
				}

				execute_scope.emplace(Active());
			}

			std::uint64_t instructions = 0;
			while (Active().Frames.size() > frame_floor && !Active().Suspended) {
				if (instructions >= instruction_budget || m_InstructionRemaining == 0) {
					throw VmSafetyFailure("Squirrel instruction budget exceeded");
				}

				auto& frame = Active().Frames.back();
				const auto program_pin = Object(frame.Closure).Program;
				if (!program_pin) {
					throw VmSafetyFailure("Squirrel active function lacks its bytecode owner");
				}

				const auto& function = Function(frame);
				if (frame.Instruction >= function.Instructions.size()) {
					throw std::runtime_error("Squirrel instruction pointer exceeds prototype");
				}

				const auto offset = frame.Instruction;
				const auto instruction = function.Instructions[frame.Instruction++];
				++instructions;
				--m_InstructionRemaining;
				if (m_Trace) {
					SquirrelTraceEvent event;
					event.Source = LiteralText(function.SourceName);
					event.Function = LiteralText(function.Name);
					event.Instruction = offset;
					event.Operation = instruction;
					event.Context = ContextHandle(m_ActiveContext);
					m_Trace(event);
				}

				try {
					Step(frame, instruction);
				} catch (const VmSafetyFailure&) {
					throw;
				} catch (const VmThrownValue& thrown) {
					Propagate(thrown.Value, frame_floor);
				} catch (const SquirrelVmException& error) {
					Propagate(error.ThrownValue(), frame_floor);
				} catch (const std::runtime_error& error) {
					Propagate(std::string(error.what()), frame_floor, std::string(error.what()) + " in " + std::string(LiteralText(function.SourceName)) + ":" + std::string(LiteralText(function.Name)) + " at instruction " + std::to_string(offset));
				}
			}

			SquirrelRunResult result;
			result.State = Active().Suspended ? SquirrelRunState::Suspended : SquirrelRunState::Completed;
			AssignStoredValue(result.Value, Active().Result);
			result.Instructions = instructions;
			result.Context = ContextHandle(m_ActiveContext);

			return result;
		}

		SquirrelVm& m_Api;
		SquirrelVmLimits m_Limits;
		std::shared_ptr<std::uint8_t> m_LifetimeOwner = std::make_shared<std::uint8_t>(0);
		std::vector<std::shared_ptr<SquirrelObjectHandle::Lifetime>> m_ObjectLifetimes;
		std::vector<std::uint32_t> m_PendingReleases;
		std::vector<std::uint32_t> m_FreeObjects;
		std::vector<VmReleaseFrame> m_ReleaseFrames;
		std::optional<VmCollectableIdentity> m_GarbageHead;
		std::optional<VmCollectableIdentity> m_MarkedHead;
		std::vector<VmMarkFrame> m_PendingMarks;
		bool m_Collecting = false;
		bool m_Closing = false;
		bool m_RetiringHostRoots = false;
		bool m_Closed = false;
		std::vector<VmBorrowedReceiverIdentity> m_BorrowedReceiverScopes;
		std::exception_ptr m_ReleaseFailure;
		std::size_t m_LiveObjects = 0;
		std::size_t m_UserDataBytes = 0;
		bool m_DrainingReleases = false;
		std::size_t m_ReleaseCallbackDepth = 0;
		std::size_t m_CloseAllocations = 0;
		std::size_t m_CollectionAllocations = 0;
		std::vector<std::unique_ptr<VmObject>> m_Objects;
		// The pool indexes strings weakly; the immutable values alone own their byte storage.
		std::shared_ptr<SquirrelString::Storage::Registry> m_StringPool = std::make_shared<SquirrelString::Storage::Registry>();
		std::vector<SquirrelValue> m_SharedTypeNames;
		std::vector<SquirrelValue> m_SharedMetaNames;
		SquirrelValue m_ConstructorName;
		// Scoped host roots observe this registry weakly and never extend VM lifetime.
		std::shared_ptr<VmHostRootRegistry> m_HostRoots = std::make_shared<VmHostRootRegistry>();
		SquirrelValue m_ConstantTable;
		std::optional<SquirrelObjectHandle> m_ExceptionClass;
		std::vector<std::unique_ptr<VmExecutionContext>> m_Contexts;
		std::vector<std::uint64_t> m_ContextGenerations;
		std::vector<bool> m_RetiredContexts;
		std::optional<std::uint32_t> m_ClosingContext;
		std::vector<std::uint32_t> m_FreeContexts;
		std::uint32_t m_ActiveContext = 0;
		bool m_Running = false;
		std::size_t m_InvocationDepth = 0;
		std::size_t m_GetterClassDepth = 0;
		std::uint64_t m_InstructionRemaining = 0;
		std::size_t m_MetaCallDepth = 0;
		TraceCallback m_Trace;
		std::array<std::optional<SquirrelObjectHandle>, static_cast<std::size_t>(SquirrelValueType::UserData) + 1> m_DefaultDelegates;
		bool m_BaseLibraryInstalled = false;
	};

	void SquirrelVm::Implementation::RegisterBaseSlot(const SquirrelValue& root, const SquirrelValue& key, const SquirrelValue& value) {
		if (Type(root) != SquirrelValueType::Table) {
			return;
		}

		if (!FindOwnSlot(Object(root), key)) {
			if (const auto method = MetaMethod(root, "_newslot")) {
				const std::array<SquirrelValue, 3> arguments{root, key, value};
				++m_MetaCallDepth;
				bool success = false;
				try {
					static_cast<void>(m_Api.InvokeInContext(ContextHandle(m_ActiveContext), *method, arguments, m_Running ? m_InstructionRemaining : 1000000, false));
					success = true;
				} catch (const SquirrelVmException&) {
					// Table newslot falls back to insertion after a failed metamethod call.
				} catch (...) {
					--m_MetaCallDepth;

					throw;
				}

				--m_MetaCallDepth;
				if (success && Object(root).Delegate) {
					return;
				}
			}
		}

		NewSlot(root, key, value, false);
	}

	void SquirrelVm::Implementation::RegisterBaseNative(const SquirrelValue& root, std::string name, NativeCallback callback) {
		const auto record = std::find_if(std::begin(kNativeBaseSignatureRecords), std::end(kNativeBaseSignatureRecords), [&](const VmNativeSignatureRecord& candidate) {
			return name == candidate.Name;
		});
		if (record == std::end(kNativeBaseSignatureRecords)) {
			throw std::runtime_error("Squirrel base registration has no recovered signature");
		}

		const auto closure = m_Api.CreateNativeClosure(name, std::move(callback), NativeSignature(record->Name, record->ParameterCheck, record->Mask));
		RegisterBaseSlot(root, name, closure);
	}

	void SquirrelVm::Implementation::InstallBaseGlobals() {
		const auto root = Active().RootTable;
		// The original registrar pushes this snapshot once and keeps it on its VM stack throughout registration.
		VmTemporaryRootScope root_stack(Active(), root);

		RegisterBaseNative(root, "seterrorhandler", [this](SquirrelVm&, std::span<const SquirrelValue> arguments) {
			RequireArguments(arguments, 2, 2);
			const auto type = Type(arguments[1]);
			if (type == SquirrelValueType::Closure || type == SquirrelValueType::NativeClosure || type == SquirrelValueType::Null) {
				AssignStoredValue(Active().ErrorHandler, arguments[1]);
			}

			return NativeValue({});
		});
		RegisterBaseNative(root, "getstackinfos", [this](SquirrelVm&, std::span<const SquirrelValue> arguments) {
			RequireArguments(arguments, 2, 2);
			const auto level = TryNativeInteger(arguments[1]);
			if (!level) {
				throw std::runtime_error("getstackinfos requires a numeric stack level");
			}

			return NativeValue(StackInformation(*level));
		});

		RegisterBaseNative(root, "getroottable", [](SquirrelVm& vm, std::span<const SquirrelValue> arguments) {
			RequireArguments(arguments, 1, 1);

			return NativeValue(vm.RootTable());
		});
		RegisterBaseNative(root, "setroottable", [](SquirrelVm& vm, std::span<const SquirrelValue> arguments) {
			RequireArguments(arguments, 2, 2);
			vm.SetRootTable(arguments[1]);

			// The native setter returns its argument cell after the API has popped and cleared it.
			return NativeValue({});
		});
		RegisterBaseNative(root, "getconsttable", [](SquirrelVm& vm, std::span<const SquirrelValue> arguments) {
			RequireArguments(arguments, 1, 1);

			return NativeValue(vm.ConstantTable());
		});
		RegisterBaseNative(root, "setconsttable", [](SquirrelVm& vm, std::span<const SquirrelValue> arguments) {
			RequireArguments(arguments, 2, 2);
			vm.SetConstantTable(arguments[1]);

			// The native setter pops and clears its second argument before returning that same stack cell.
			return NativeValue({});
		});
		RegisterBaseNative(root, "setexceptionclass", [this](SquirrelVm&, std::span<const SquirrelValue> arguments) {
			RequireArguments(arguments, 2, 2);
			if (Type(arguments[1]) != SquirrelValueType::Class) {
				throw std::runtime_error("ivalid type");
			}

			m_ExceptionClass = std::get<SquirrelObjectHandle>(arguments[1]);

			return NativeValue(arguments[1]);
		});
		RegisterBaseNative(root, "assert", [](SquirrelVm&, std::span<const SquirrelValue> arguments) {
			RequireArguments(arguments, 2, 2);
			if (!IsTrue(arguments[1])) {
				throw std::runtime_error("assertion failed");
			}

			return NativeValue({});
		});
		RegisterBaseNative(root, "suspend", [this](SquirrelVm&, std::span<const SquirrelValue> arguments) {
			RequireArguments(arguments, 1, m_Limits.MaxRegisters);
			return m_Api.RequestNativeSuspension();
		});
		RegisterBaseNative(root, "array", [this](SquirrelVm& vm, std::span<const SquirrelValue> arguments) {
			RequireArguments(arguments, 2, 3);
			const auto size = NumericIndex(arguments[1]);
			if (size < 0 || static_cast<std::size_t>(size) > m_Limits.MaxContainerSlots) {
				throw std::runtime_error("Squirrel array size exceeds limit");
			}

			std::vector<SquirrelValue> values(static_cast<std::size_t>(size), arguments.size() > 2 ? arguments[2] : SquirrelValue{});

			return NativeValue(vm.CreateArray(values));
		});
		RegisterBaseNative(root, "type", [](SquirrelVm& vm, std::span<const SquirrelValue> arguments) {
			RequireArguments(arguments, 2, 2);

			return NativeValue(vm.TypeName(arguments[1]));
		});
		RegisterBaseNative(root, "compilestring", CompileSquirrelString);
		RegisterBaseNative(root, "template", ExpandSquirrelTemplate);
		RegisterBaseNative(root, "collectgarbage", [](SquirrelVm& vm, std::span<const SquirrelValue> arguments) {
			RequireArguments(arguments, 1, 1);

			// Native 0x1402a04c0 pushes the actual shared-state collector count as a 32-bit integer.
			return NativeValue(WrapInteger(vm.CollectGarbage()));
		});
		RegisterBaseNative(root, "dummy", [](SquirrelVm&, std::span<const SquirrelValue>) {
			return NativeValue({});
		});
		if (Type(root) == SquirrelValueType::Table) {
			RegisterBaseSlot(root, std::string("_version_"), std::string("Squirrel 2.2.4 stable"));
			RegisterBaseSlot(root, std::string("_charsize_"), std::int32_t{1});
			RegisterBaseSlot(root, std::string("_intsize_"), std::int32_t{4});
			RegisterBaseSlot(root, std::string("_floatsize_"), std::int32_t{4});
		}
	}

	void SquirrelVm::Implementation::InstallBaseLibrary() {
		if (m_BaseLibraryInstalled) {
			return;
		}

		InstallBaseGlobals();

		// Root setup 0x140226020 installs string globals; child 0x14025a030 repeats only base 0x1402a50e0.
		const auto root = Active().RootTable;
		VmTemporaryRootScope root_stack(Active(), root);
		for (const auto* name : {"strip", "lstrip", "rstrip"}) {
			RegisterBaseNative(root, name, [name](SquirrelVm&, std::span<const SquirrelValue> arguments) {
				RequireArguments(arguments, 2, 2);
				const auto& source = StringText(arguments[1]);
				const auto operation = std::string_view(name);
				// Native 0x140588110's initial CRT table classifies only these six signed-byte values as space.
				const auto is_space = [](char byte) {
					return byte == ' ' || (byte >= '\t' && byte <= '\r');
				};
				std::size_t first = 0;
				if (operation != "rstrip") {
					while (first < source.size() && is_space(source[first])) {
						++first;
					}
				}

				if (operation == "lstrip") {
					const auto terminator = source.find('\0', first);
					const auto end = terminator == std::string::npos ? source.size() : terminator;

					return SquirrelNativeResult::ReturnValue(source.substr(first, end - first));
				}

				// The native stored-length backward scan stops before classifying the first stored byte.
				auto end = source.size();
				while (end > 1 && is_space(source[end - 1])) {
					--end;
				}

				if (end < first) {
					// All-space strip passes a negative native length; strlen at its final terminator returns zero.
					return SquirrelNativeResult::ReturnValue(std::string{});
				}

				return SquirrelNativeResult::ReturnValue(source.substr(first, end - first));
			});
		}

		RegisterBaseNative(root, "split", [this](SquirrelVm& vm, std::span<const SquirrelValue> arguments) {
			RequireArguments(arguments, 3, 3);
			const auto& stored_source = StringText(arguments[1]);
			const auto& separators = StringText(arguments[2]);
			if (separators.empty()) {
				throw std::runtime_error("empty separators string");
			}

			// Global 0x140265a70 uses the linked CRT byte-set tokenizer; the string delegate is separate.
			std::array<bool, 256> separator_bytes{};
			for (const auto byte : separators) {
				if (byte == '\0') {
					break;
				}

				separator_bytes[static_cast<unsigned char>(byte)] = true;
			}

			const auto source = std::string_view(stored_source).substr(0, stored_source.find('\0'));
			const auto result = vm.CreateArray();
			std::size_t position = 0;
			while (position < source.size()) {
				while (position < source.size() && separator_bytes[static_cast<unsigned char>(source[position])]) {
					++position;
				}

				const auto first = position;
				while (position < source.size() && !separator_bytes[static_cast<unsigned char>(source[position])]) {
					++position;
				}

				if (first != position) {
					Append(result, std::string(source.substr(first, position - first)));
				}
			}

			return SquirrelNativeResult::ReturnValue(result);
		});

		for (const auto type : {SquirrelValueType::String, SquirrelValueType::Integer, SquirrelValueType::Float, SquirrelValueType::Boolean}) {
			for (const auto* name : {"tointeger", "tofloat", "tonumber"}) {
				AddDelegate(type, name, [this, name](SquirrelVm&, std::span<const SquirrelValue> arguments) {
					RequireArguments(arguments, 1, 1);

					return NativeValue(ConvertNumber(arguments[0], name));
				});
			}

			AddDelegate(type, "tostring", [this](SquirrelVm&, std::span<const SquirrelValue> arguments) {
				RequireArguments(arguments, 1, 1);

				return NativeValue(StringValue(arguments[0]));
			});
		}

		for (const auto type : {SquirrelValueType::Integer, SquirrelValueType::Float, SquirrelValueType::Boolean}) {
			AddDelegate(type, "tochar", [](SquirrelVm&, std::span<const SquirrelValue> arguments) {
				RequireArguments(arguments, 1, 1);
				const auto number = std::holds_alternative<bool>(arguments[0]) ? static_cast<std::int32_t>(std::get<bool>(arguments[0])) : NumericIndex(arguments[0]);

				return NativeValue(std::string(1, static_cast<char>(std::bit_cast<std::uint32_t>(number) & 255)));
			});
		}

		for (const auto type : {SquirrelValueType::Table, SquirrelValueType::Array, SquirrelValueType::String}) {
			AddDelegate(type, "len", [](SquirrelVm& vm, std::span<const SquirrelValue> arguments) {
				RequireArguments(arguments, 1, 1);

				return NativeValue(static_cast<std::int32_t>(vm.Length(arguments[0])));
			});
		}

		for (const auto type : {SquirrelValueType::Table, SquirrelValueType::Array}) {
			AddDelegate(type, "clear", [this](SquirrelVm&, std::span<const SquirrelValue> arguments) {
				RequireArguments(arguments, 1, 1);
				auto& object = Object(arguments[0]);
				if (object.Type == SquirrelValueType::Table) {
					ClearTable(object);
				} else {
					ClearArray(arguments[0]);
				}

				return SquirrelNativeResult::NoValues();
			});
		}

		AddDelegate(SquirrelValueType::Table, "rawget", [this](SquirrelVm&, std::span<const SquirrelValue> arguments) {
			RequireArguments(arguments, 2, 2);
			const auto* slot = FindOwnSlot(Object(arguments[0]), arguments[1]);
			if (!slot) {
				throw std::runtime_error("Squirrel raw slot does not exist");
			}

			return NativeValue(ResolveStoredValue(slot->Value));
		});
		AddDelegate(SquirrelValueType::Table, "rawset", [this](SquirrelVm&, std::span<const SquirrelValue> arguments) {
			RequireArguments(arguments, 3, 3);
			NewSlot(arguments[0], arguments[1], arguments[2], false);

			return NativeValue({});
		});
		AddDelegate(SquirrelValueType::Table, "rawdelete", [this](SquirrelVm&, std::span<const SquirrelValue> arguments) {
			RequireArguments(arguments, 2, 2);

			return NativeValue(DeleteSlot(arguments[0], arguments[1]));
		});
		AddDelegate(SquirrelValueType::Table, "rawin", [this](SquirrelVm&, std::span<const SquirrelValue> arguments) {
			RequireArguments(arguments, 2, 2);

			return NativeValue(FindOwnSlot(Object(arguments[0]), arguments[1]) != nullptr);
		});

		for (const auto* name : {"append", "push"}) {
			AddDelegate(SquirrelValueType::Array, name, [this](SquirrelVm&, std::span<const SquirrelValue> arguments) {
				RequireArguments(arguments, 2, 2);
				Append(arguments[0], arguments[1]);

				return NativeValue({});
			});
		}

		AddDelegate(SquirrelValueType::Array, "join", [this](SquirrelVm&, std::span<const SquirrelValue> arguments) {
			RequireArguments(arguments, 2, 2);
			const auto source = arguments[0];
			const auto& stored_delimiter = StringText(arguments[1]);
			const auto delimiter = std::string_view(stored_delimiter).substr(0, stored_delimiter.find('\0'));
			const auto initial_count = Object(source).Elements.size();
			const auto native_limit = static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max());
			if (initial_count > native_limit || delimiter.size() >= native_limit) {
				throw std::runtime_error("Squirrel string limit exceeded by array join");
			}

			const auto output_limit = std::min(m_Limits.MaxStringBytes, native_limit - 1 - delimiter.size());
			std::string result;
			const auto append = [&](std::string_view piece) {
				if (piece.size() > output_limit - result.size()) {
					throw std::runtime_error("Squirrel string limit exceeded by array join");
				}

				result.append(piece);
			};
			// Native2A2BE0 captures the initial count, then rereads each slot and its C-prefix bytes.
			for (std::size_t index = 0; index < initial_count; ++index) {
				if (index >= Object(source).Elements.size()) {
					continue;
				}

				const auto element = ResolveStoredValue(Object(source).Elements[index]);
				const auto* text = TryStringText(element);
				if (!text) {
					throw std::runtime_error("Array.join nonstring conversion requires recovered shared scratch ownership");
				}

				if (!result.empty()) {
					append(delimiter);
				}

				append(std::string_view(*text).substr(0, text->find('\0')));
			}

			return SquirrelNativeResult::ReturnValue(std::move(result));
		});
		AddDelegate(SquirrelValueType::Array, "pop", [this](SquirrelVm&, std::span<const SquirrelValue> arguments) {
			RequireArguments(arguments, 1, 1);
			auto& elements = Object(arguments[0]).Elements;
			if (elements.empty()) {
				throw std::runtime_error("Squirrel empty array");
			}

			const auto value = elements.back();
			elements.pop_back();

			return NativeValue(value);
		});
		AddDelegate(SquirrelValueType::Array, "top", [this](SquirrelVm&, std::span<const SquirrelValue> arguments) {
			RequireArguments(arguments, 1, 1);
			const auto& elements = Object(arguments[0]).Elements;
			if (elements.empty()) {
				throw std::runtime_error("Squirrel empty array");
			}

			return NativeValue(elements.back());
		});
		AddDelegate(SquirrelValueType::Array, "reverse", [this](SquirrelVm&, std::span<const SquirrelValue> arguments) {
			RequireArguments(arguments, 1, 1);
			auto& elements = Object(arguments[0]).Elements;
			std::reverse(elements.begin(), elements.end());

			return NativeValue({});
		});
		AddDelegate(SquirrelValueType::Array, "insert", [this](SquirrelVm&, std::span<const SquirrelValue> arguments) {
			RequireArguments(arguments, 3, 3);
			const auto index = NumericIndex(arguments[1]);
			auto& elements = Object(arguments[0]).Elements;
			if (index < 0 || static_cast<std::size_t>(index) > elements.size()) {
				throw std::runtime_error("index out of range");
			}

			if (elements.size() >= m_Limits.MaxContainerSlots) {
				throw std::runtime_error("Squirrel array insert exceeds container limit");
			}

			elements.insert(elements.begin() + index, arguments[2]);

			return NativeValue({});
		});
		AddDelegate(SquirrelValueType::Array, "sort", [this](SquirrelVm&, std::span<const SquirrelValue> arguments) {
			RequireArguments(arguments, 1, m_Limits.MaxRegisters);
			SortArray(arguments[0], arguments.size() > 1 ? std::optional<SquirrelValue>(arguments[1]) : std::optional<SquirrelValue>{});

			return NativeValue({});
		});
		AddDelegate(SquirrelValueType::Array, "resize", [this](SquirrelVm&, std::span<const SquirrelValue> arguments) {
			RequireArguments(arguments, 2, 3);
			const auto size = NumericIndex(arguments[1]);
			if (size < 0 || static_cast<std::size_t>(size) > m_Limits.MaxContainerSlots) {
				throw std::runtime_error("Squirrel resize exceeds slot limit");
			}

			Object(arguments[0]).Elements.resize(static_cast<std::size_t>(size), arguments.size() == 3 ? arguments[2] : SquirrelValue{});

			return NativeValue({});
		});
		AddDelegate(SquirrelValueType::String, "slice", [](SquirrelVm&, std::span<const SquirrelValue> arguments) {
			RequireArguments(arguments, 1, 3);
			const auto& text = StringText(arguments[0]);
			const auto size = static_cast<std::int64_t>(text.size());
			const auto first = arguments.size() > 1 && IsNumber(arguments[1]) ? NumericIndex(arguments[1]) : 0;
			const auto last = arguments.size() > 2 ? (IsNumber(arguments[2]) ? NumericIndex(arguments[2]) : 0) : size;
			const auto start = first < 0 ? size + first : first;
			const auto end = last < 0 ? size + last : last;
			if (start < 0 || end < start || end > size) {
				throw std::runtime_error("Squirrel slice out of range");
			}

			return NativeValue(text.substr(static_cast<std::size_t>(start), static_cast<std::size_t>(end - start)));
		});
		InstallAdditionalDelegates();

		m_BaseLibraryInstalled = true;
	}

	void SquirrelVm::Implementation::InstallAdditionalDelegates() {
		for (const auto type : {SquirrelValueType::Table, SquirrelValueType::Array}) {
			for (const auto* name : {"find", "findall", "includes", "includeCount"}) {
				AddDelegate(type, name, [this, name](SquirrelVm&, std::span<const SquirrelValue> arguments) {
					RequireArguments(arguments, 2, 2);

					return NativeValue(SearchValues(arguments[0], arguments[1], name));
				});
			}
		}

		for (const auto type : {SquirrelValueType::Closure, SquirrelValueType::NativeClosure}) {
			AddDelegate(type, "getinfos", [this](SquirrelVm&, std::span<const SquirrelValue> arguments) {
				RequireArguments(arguments, 1, 1);

				return NativeValue(ClosureInformation(arguments[0]));
			});
		}

		AddDelegate(SquirrelValueType::Array, "removeValue", [this](SquirrelVm& vm, std::span<const SquirrelValue> arguments) {
			RequireArguments(arguments, 2, m_Limits.MaxRegisters);
			std::size_t remaining = Object(arguments[0]).Elements.size();
			std::size_t index = 0;
			const auto all = arguments.size() > 2 ? TryNativeInteger(arguments[2]).value_or(0) != 0 : true;
			while (index < remaining) {
				const auto& source = Object(arguments[0]).Elements;
				const auto element = index < source.size() ? ResolveStoredValue(source[index]) : SquirrelValue{};
				if (vm.CompareValues(arguments[1], element, m_InstructionRemaining) != 0) {
					++index;
					continue;
				}

				auto& current = Object(arguments[0]).Elements;
				if (index < current.size()) {
					current.erase(current.begin() + static_cast<std::ptrdiff_t>(index));
				}

				--remaining;
				if (!all) {
					break;
				}
			}

			return NativeValue({});
		});
		AddDelegate(SquirrelValueType::String, "replace", [this](SquirrelVm&, std::span<const SquirrelValue> arguments) {
			RequireArguments(arguments, 3, 3);
			const auto& source = StringText(arguments[0]);
			const auto& needle = StringText(arguments[1]);
			const auto& replacement = StringText(arguments[2]);
			if (needle.empty()) {
				return NativeValue(source);
			}

			if (source.find('\0') != std::string::npos || needle.find('\0') != std::string::npos) {
				throw std::runtime_error("native string replacement with embedded NUL has no recovered bounded result");
			}

			std::string result;
			std::size_t position = 0;
			while (position < source.size()) {
				const auto found = source.find(needle, position);
				const auto next = found == std::string::npos ? source.size() : found;
				const auto prefix = next - position;
				if (prefix > m_Limits.MaxStringBytes - result.size()) {
					throw std::runtime_error("native replacement exceeds string limit");
				}

				result.append(source, position, prefix);
				if (found == std::string::npos) {
					break;
				}

				if (replacement.size() > m_Limits.MaxStringBytes - result.size()) {
					throw std::runtime_error("native replacement exceeds string limit");
				}

				result.append(replacement);
				position = next + needle.size();
			}

			return NativeValue(std::move(result));
		});
		AddDelegate(SquirrelValueType::String, "split", [this](SquirrelVm& vm, std::span<const SquirrelValue> arguments) {
			RequireArguments(arguments, 2, 2);
			const auto& stored = StringText(arguments[0]);
			const auto source = std::string_view(stored).substr(0, stored.find('\0'));
			const auto& delimiter = StringText(arguments[1]);
			if (delimiter.empty() || delimiter.find('\0') != std::string::npos) {
				throw std::runtime_error("native split delimiter is empty or has no recovered bounded advance");
			}

			const auto result = vm.CreateArray();
			std::size_t position = 0;
			while (true) {
				const auto found = source.find(delimiter, position);
				const auto next = found == std::string_view::npos ? source.size() : found;
				Append(result, std::string(source.substr(position, next - position)));
				if (found == std::string_view::npos) {
					break;
				}

				position = next + delimiter.size();
			}

			return NativeValue(result);
		});
		AddDelegate(SquirrelValueType::String, "mbnext", [this](SquirrelVm&, std::span<const SquirrelValue> arguments) {
			RequireArguments(arguments, 1, m_Limits.MaxRegisters);
			const auto& source = StringText(arguments[0]);
			const auto position = arguments.size() > 1 ? NumericIndex(arguments[1]) : 0;
			if (position < 0) {
				throw std::runtime_error("native multibyte cursor would read before its string");
			}

			return NativeValue(static_cast<std::int32_t>(NativeMultibyteWidth(source, static_cast<std::size_t>(position))));
		});
		AddDelegate(SquirrelValueType::String, "mblen", [](SquirrelVm&, std::span<const SquirrelValue> arguments) {
			RequireArguments(arguments, 1, 1);

			return NativeValue(NativeMultibyteLength(StringText(arguments[0])));
		});
		AddDelegate(SquirrelValueType::String, "mbsubstr", [this](SquirrelVm&, std::span<const SquirrelValue> arguments) {
			RequireArguments(arguments, 1, m_Limits.MaxRegisters);
			const auto& source = StringText(arguments[0]);
			const auto length = NativeMultibyteLength(source);
			const auto first = arguments.size() > 1 ? NumericIndex(arguments[1]) : 0;
			const auto start = first < 0 ? static_cast<std::int64_t>(length) + first : first;
			const auto end = arguments.size() > 2 ? start + NumericIndex(arguments[2]) : length;
			if (end < start || end > length) {
				throw std::runtime_error("native multibyte substring range exceeds string");
			}

			std::int64_t character = 0;
			std::size_t position = 0;
			while (character < start) {
				const auto width = NativeMultibyteWidth(source, position);
				if (width == 0) {
					break;
				}

				position += width;
				++character;
			}

			const auto byte_start = position;
			while (character < end) {
				const auto width = NativeMultibyteWidth(source, position);
				if (width == 0) {
					break;
				}

				position += width;
				++character;
			}

			if (byte_start > source.size() || position > source.size()) {
				throw std::runtime_error("native multibyte substring would exceed bounded string storage");
			}

			return NativeValue(source.substr(byte_start, position - byte_start));
		});
		for (std::size_t index = static_cast<std::size_t>(SquirrelValueType::Integer); index <= static_cast<std::size_t>(SquirrelValueType::WeakReference); ++index) {
			AddDelegate(static_cast<SquirrelValueType>(index), "weakref", [](SquirrelVm& vm, std::span<const SquirrelValue> arguments) {
				RequireArguments(arguments, 1, 1);

				return NativeValue(vm.CreateWeakReference(arguments[0]));
			});
		}

		AddDelegate(SquirrelValueType::WeakReference, "ref", [](SquirrelVm& vm, std::span<const SquirrelValue> arguments) {
			RequireArguments(arguments, 1, 1);

			return NativeValue(vm.ResolveWeakReference(arguments[0]));
		});
		for (const auto type : {SquirrelValueType::Table, SquirrelValueType::Array, SquirrelValueType::Class, SquirrelValueType::Instance, SquirrelValueType::Closure, SquirrelValueType::NativeClosure, SquirrelValueType::Generator}) {
			AddDelegate(type, "tonumber", [this](SquirrelVm&, std::span<const SquirrelValue> arguments) {
				RequireArguments(arguments, 1, 1);

				return NativeValue(ConvertNumber(arguments[0], "tonumber"));
			});
			AddDelegate(type, "tostring", [](SquirrelVm& vm, std::span<const SquirrelValue> arguments) {
				RequireArguments(arguments, 1, 1);

				return NativeValue(vm.ToString(arguments[0]));
			});
		}

		for (const auto type : {SquirrelValueType::Class, SquirrelValueType::Instance}) {
			AddDelegate(type, "rawin", [this](SquirrelVm&, std::span<const SquirrelValue> arguments) {
				RequireArguments(arguments, 2, 2);

				return NativeValue(FindRawSlot(arguments[0], arguments[1]).has_value());
			});
		}

		AddDelegate(SquirrelValueType::Class, "getattributes", [this](SquirrelVm&, std::span<const SquirrelValue> arguments) {
			RequireArguments(arguments, 2, 2);
			auto& object = Object(arguments[0]);
			if (std::holds_alternative<std::monostate>(arguments[1])) {
				return NativeValue(object.Attributes);
			}

			const auto* slot = FindOwnSlot(object, arguments[1]);
			if (!slot) {
				throw std::runtime_error("wrong index");
			}

			return NativeValue(slot->Attributes);
		});
		AddDelegate(SquirrelValueType::Class, "setattributes", [this](SquirrelVm&, std::span<const SquirrelValue> arguments) {
			RequireArguments(arguments, 3, 3);
			auto& object = Object(arguments[0]);
			SquirrelValue* attributes = &object.Attributes;
			if (!std::holds_alternative<std::monostate>(arguments[1])) {
				auto* slot = FindOwnSlot(object, arguments[1]);
				if (!slot) {
					throw std::runtime_error("wrong index");
				}

				attributes = &slot->Attributes;
			}

			const auto previous = *attributes;
			*attributes = arguments[2];

			return NativeValue(previous);
		});
		AddDelegate(SquirrelValueType::Class, "instance", [](SquirrelVm& vm, std::span<const SquirrelValue> arguments) {
			RequireArguments(arguments, 1, 1);

			return NativeValue(vm.CreateInstance(arguments[0]));
		});
		AddDelegate(SquirrelValueType::Instance, "getclass", [this](SquirrelVm&, std::span<const SquirrelValue> arguments) {
			RequireArguments(arguments, 1, 1);

			return NativeValue(*Object(arguments[0]).InstanceClass);
		});
		AddDelegate(SquirrelValueType::Generator, "getstatus", [this](SquirrelVm&, std::span<const SquirrelValue> arguments) {
			RequireArguments(arguments, 1, 1);
			const auto& generator = Object(arguments[0]);

			return NativeValue(std::string(generator.GeneratorDead ? "dead" : (generator.GeneratorRunning ? "running" : "suspended")));
		});

		for (const auto type : {SquirrelValueType::Closure, SquirrelValueType::NativeClosure}) {
			for (const auto* name : {"call", "pcall"}) {
				AddDelegate(type, name, [this, name](SquirrelVm& vm, std::span<const SquirrelValue> arguments) {
					RequireArguments(arguments, 1, m_Limits.MaxRegisters);

					return NativeValue(vm.InvokeInContext(vm.CurrentContext(), arguments[0], arguments.subspan(1), m_InstructionRemaining, std::string_view(name) == "call").Value);
				});
			}

			for (const auto* name : {"acall", "pacall"}) {
				AddDelegate(type, name, [this, name](SquirrelVm& vm, std::span<const SquirrelValue> arguments) {
					RequireArguments(arguments, 2, 2);
					if (Type(arguments[1]) != SquirrelValueType::Array) {
						throw std::runtime_error("closure acall requires an argument array");
					}

					const auto values = Object(arguments[1]).Elements;

					return NativeValue(vm.InvokeInContext(vm.CurrentContext(), arguments[0], values, m_InstructionRemaining, std::string_view(name) == "acall").Value);
				});
			}

			AddDelegate(type, "bindenv", [](SquirrelVm& vm, std::span<const SquirrelValue> arguments) {
				RequireArguments(arguments, 2, 2);

				return NativeValue(vm.BindEnvironment(arguments[0], arguments[1]));
			});
			AddDelegate(type, "getenv", [this](SquirrelVm&, std::span<const SquirrelValue> arguments) {
				RequireArguments(arguments, 1, 1);
				const auto environment = Object(arguments[0]).BoundEnvironment;

				return NativeValue(environment ? SquirrelValue(*environment) : SquirrelValue{});
			});
		}

		AddDelegate(SquirrelValueType::Table, "keys", [this](SquirrelVm& vm, std::span<const SquirrelValue> arguments) {
			RequireArguments(arguments, 1, 1);
			const auto& table = Object(arguments[0]);
			if (table.TablePointerKeys) {
				throw std::runtime_error("Squirrel object-key table iteration requires native allocator provenance");
			}

			std::vector<SquirrelValue> keys;
			keys.reserve(table.TableUsed);
			for (const auto& slot : table.Slots) {
				if (!std::holds_alternative<std::monostate>(slot.Key)) {
					keys.push_back(slot.Key);
				}
			}

			return NativeValue(vm.CreateArray(keys));
		});
		AddDelegate(SquirrelValueType::Array, "extend", [this](SquirrelVm&, std::span<const SquirrelValue> arguments) {
			RequireArguments(arguments, 2, 2);
			if (Type(arguments[1]) != SquirrelValueType::Array) {
				throw std::runtime_error("array extend requires an array");
			}

			const auto count = Object(arguments[1]).Elements.size();
			const auto previous = Object(arguments[0]).Elements.size();
			if (previous > m_Limits.MaxContainerSlots || count > m_Limits.MaxContainerSlots - previous) {
				throw std::runtime_error("Squirrel array slot limit exceeded");
			}

			for (std::size_t index = 0; index < count; ++index) {
				const auto value = Object(arguments[1]).Elements[index];
				Append(arguments[0], value);
			}

			return NativeValue({});
		});
		for (const auto* name : {"remove", "erase"}) {
			AddDelegate(SquirrelValueType::Array, name, [this](SquirrelVm&, std::span<const SquirrelValue> arguments) {
				RequireArguments(arguments, 2, 2);
				return NativeValue(RemoveArray(arguments[0], NumericIndex(arguments[1])));
			});
		}

		AddDelegate(SquirrelValueType::Array, "slice", [this](SquirrelVm& vm, std::span<const SquirrelValue> arguments) {
			RequireArguments(arguments, 1, 3);
			const auto& elements = Object(arguments[0]).Elements;
			const auto size = static_cast<std::int64_t>(elements.size());
			const auto first = arguments.size() > 1 ? NumericIndex(arguments[1]) : 0;
			const auto last = arguments.size() > 2 ? NumericIndex(arguments[2]) : size;
			const auto start = first < 0 ? size + first : first;
			const auto end = last < 0 ? size + last : last;
			if (end < start || end > size || static_cast<std::uint64_t>(end - start) > m_Limits.MaxContainerSlots) {
				throw std::runtime_error("Squirrel array slice out of range or exceeds limit");
			}

			std::vector<SquirrelValue> values;
			values.reserve(static_cast<std::size_t>(end - start));
			for (auto index = start; index < end; ++index) {
				values.push_back(index < 0 ? SquirrelValue{} : ResolveStoredValue(elements[static_cast<std::size_t>(index)]));
			}

			return NativeValue(vm.CreateArray(values));
		});

		for (const bool reverse : {false, true}) {
			AddDelegate(SquirrelValueType::String, reverse ? "rfind" : "find", [reverse](SquirrelVm&, std::span<const SquirrelValue> arguments) {
				RequireArguments(arguments, 2, 3);
				const auto& text = StringText(arguments[0]);
				const auto& needle = StringText(arguments[1]);
				const auto start = arguments.size() == 3 ? NumericIndex(arguments[2]) : 0;
				if (start < 0 || static_cast<std::size_t>(start) >= text.size()) {
					return NativeValue({});
				}

				const auto suffix = std::string_view(text).substr(static_cast<std::size_t>(start));
				const auto source = suffix.substr(0, suffix.find('\0'));
				const auto search = std::string_view(needle).substr(0, needle.find('\0'));
				const auto position = reverse ? source.rfind(search) : source.find(search);

				return NativeValue(position == std::string_view::npos ? SquirrelValue{} : SquirrelValue(static_cast<std::int32_t>(static_cast<std::size_t>(start) + position)));
			});
		}

		for (const bool character : {false, true}) {
			AddDelegate(SquirrelValueType::String, character ? "charAt" : "substr", [character](SquirrelVm&, std::span<const SquirrelValue> arguments) {
				RequireArguments(arguments, 1, character ? 2 : 3);
				const auto& text = StringText(arguments[0]);
				const auto size = static_cast<std::int64_t>(text.size());
				const auto first = arguments.size() > 1 ? NumericIndex(arguments[1]) : 0;
				const auto start = first < 0 ? size + first : first;
				const auto end = character ? start + 1 : (arguments.size() > 2 ? start + NumericIndex(arguments[2]) : size);
				if (start < 0 || end < start || end > size) {
					throw std::runtime_error("Squirrel string slice out of range");
				}

				return NativeValue(text.substr(static_cast<std::size_t>(start), static_cast<std::size_t>(end - start)));
			});
		}

		for (const bool upper : {false, true}) {
			AddDelegate(SquirrelValueType::String, upper ? "toupper" : "tolower", [upper](SquirrelVm&, std::span<const SquirrelValue> arguments) {
				RequireArguments(arguments, 1, 1);
				auto text = StringText(arguments[0]);
				for (auto& character : text) {
					if (upper && character >= 'a' && character <= 'z') {
						character = static_cast<char>(character - 'a' + 'A');
					} else if (!upper && character >= 'A' && character <= 'Z') {
						character = static_cast<char>(character - 'A' + 'a');
					}
				}

				return NativeValue(text);
			});
		}
	}

	SquirrelValue SquirrelVm::Implementation::StackInformation(std::int32_t level) {
		if (level < 0) {
			return {};
		}

		std::vector<std::variant<std::size_t, SquirrelObjectHandle>> stack;
		std::size_t native_index = 0;
		for (std::size_t script_index = 0; script_index <= Active().Frames.size(); ++script_index) {
			while (native_index < Active().NativeFrames.size() && Active().NativeFrames[native_index].ScriptDepth == script_index) {
				stack.emplace_back(Active().NativeFrames[native_index++].Closure);
			}

			if (script_index < Active().Frames.size()) {
				stack.emplace_back(script_index);
			}
		}

		if (static_cast<std::size_t>(level) >= stack.size()) {
			return {};
		}

		const auto selected = stack[stack.size() - 1 - static_cast<std::size_t>(level)];
		const auto information = m_Api.CreateTable();
		const auto locals = m_Api.CreateTable();
		std::string function_name = "unknown";
		std::string source_name = "NATIVE";
		std::int32_t line = -1;
		if (const auto* native = std::get_if<SquirrelObjectHandle>(&selected)) {
			function_name = Object(*native).Signature.Name.value_or("unknown");
		} else {
			auto& frame = Active().Frames[std::get<std::size_t>(selected)];
			const auto& function = Function(frame);
			const auto& closure = Object(frame.Closure);
			if (const auto* name = std::get_if<std::string>(&function.Name)) {
				function_name = *name;
			}

			source_name = "unknown";
			if (const auto* source = std::get_if<std::string>(&function.SourceName)) {
				source_name = *source;
			}

			line = function.LineInformation.empty() ? 0 : WrapInteger(function.LineInformation.front().Line);
			for (std::size_t index = 1; index < function.LineInformation.size(); ++index) {
				if (function.LineInformation[index].Instruction >= frame.Instruction) {
					break;
				}

				line = WrapInteger(function.LineInformation[index].Line);
			}

			for (std::size_t index = 0; index < function.OuterBindings.size(); ++index) {
				NewSlot(locals, LiteralValue(function.OuterBindings[index].Name), closure.Captures[index], false);
			}

			const auto instruction = frame.Instruction == 0 ? 0 : frame.Instruction - 1;
			for (const auto& local : function.LocalVariables) {
				if (local.StartInstruction <= instruction && local.EndInstruction >= instruction) {
					NewSlot(locals, LiteralValue(local.Name), Register(frame, local.Position), false);
				}
			}
		}

		NewSlot(information, std::string("func"), function_name, false);
		NewSlot(information, std::string("src"), source_name, false);
		NewSlot(information, std::string("line"), line, false);
		NewSlot(information, std::string("locals"), locals, false);

		return information;
	}

	void SquirrelVm::Implementation::Step(VmFrame& frame, const SquirrelInstruction& instruction) {
		switch (instruction.Opcode) {
		case 0:
			break;
		case 1:
			AssignStoredValue(Register(frame, instruction.Argument0), Literal(frame, instruction.Argument1));
			break;
		case 2:
			AssignStoredValue(Register(frame, instruction.Argument0), instruction.Argument1);
			break;
		case 3:
			AssignStoredValue(Register(frame, instruction.Argument0), std::bit_cast<float>(instruction.Argument1));
			break;
		case 4:
			AssignStoredValue(Register(frame, instruction.Argument0), Literal(frame, instruction.Argument1));
			AssignStoredValue(Register(frame, instruction.Argument2), Literal(frame, instruction.Argument3));
			break;
		case 5:
		case 6:
			CallInstruction(frame, instruction);
			break;
		case 7:
		case 8:
			PrepareCall(frame, instruction);
			break;
		case 9:
			ReadSlotInstruction(frame, Register(frame, instruction.Argument2), Literal(frame, instruction.Argument1), instruction.Argument0, {}, true);
			break;
		case 10:
			AssignStoredValue(Register(frame, instruction.Argument0), Register(frame, instruction.Argument1));
			break;
		case 11:
			SlotInstruction(frame, instruction);
			break;
		case 12:
			SlotInstruction(frame, instruction);
			break;
		case 13:
			SlotInstruction(frame, instruction);
			break;
		case 14:
			ReadSlotInstruction(frame, Register(frame, instruction.Argument1), Register(frame, instruction.Argument2), instruction.Argument0, {}, true);
			break;
		case 15:
			CompareInstruction(frame, instruction);
			break;
		case 16:
			CompareInstruction(frame, instruction);
			break;
		case 17:
			ArithmeticInstruction(frame, instruction);
			break;
		case 18:
			AssignStoredValue(Register(frame, instruction.Argument0), Bitwise(instruction.Argument3, Register(frame, instruction.Argument2), Register(frame, instruction.Argument1)));
			break;
		case 19:
			ReturnFrame(instruction.Argument0 == kDiscardRegister ? SquirrelValue{} : Register(frame, instruction.Argument1));
			break;
		case 20:
			if (instruction.Argument1 < 0 || static_cast<std::size_t>(instruction.Argument1) > frame.Registers.Size() || instruction.Argument0 > frame.Registers.Size() - static_cast<std::size_t>(instruction.Argument1)) {
				throw std::runtime_error("Squirrel null load exceeds frame");
			}

			for (std::int32_t index = 0; index < instruction.Argument1; ++index) {
				AssignStoredValue(frame.Registers[instruction.Argument0 + static_cast<std::size_t>(index)], {});
			}
			break;
		case 21:
			AssignStoredValue(Register(frame, instruction.Argument0), Active().RootTable);
			break;
		case 22:
			AssignStoredValue(Register(frame, instruction.Argument0), instruction.Argument1 != 0);
			break;
		case 23:
			AssignStoredValue(Register(frame, instruction.Argument0), Register(frame, instruction.Argument1));
			AssignStoredValue(Register(frame, instruction.Argument2), Register(frame, instruction.Argument3));
			break;
		case 24:
			Jump(frame, instruction.Argument1);
			break;
		case 25:
			if (IsTrue(Register(frame, instruction.Argument0))) {
				Jump(frame, instruction.Argument1);
			}

			break;
		case 26:
			if (!IsTrue(Register(frame, instruction.Argument0))) {
				Jump(frame, instruction.Argument1);
			}

			break;
		case 27:
			if (instruction.Argument1 < 0 || static_cast<std::size_t>(instruction.Argument1) >= Object(frame.Closure).Captures.size()) {
				throw std::runtime_error("Squirrel capture index exceeds closure");
			}

			AssignStoredValue(Register(frame, instruction.Argument0), Object(frame.Closure).Captures[static_cast<std::size_t>(instruction.Argument1)]);
			break;
		case 28:
			AssignStoredValue(Register(frame, instruction.Argument0), static_cast<std::int32_t>(frame.VariableArguments.size()));
			break;
		case 29:
			AssignStoredValue(Register(frame, instruction.Argument0), frame.VariableArguments[CheckedIndex(Register(frame, instruction.Argument1), frame.VariableArguments.size())]);
			break;
		case 30:
			AssignStoredValue(Register(frame, instruction.Argument0), Allocate(SquirrelValueType::Table));
			if (instruction.Argument1 > 4) {
				InitializeTable(Object(Register(frame, instruction.Argument0)), static_cast<std::size_t>(instruction.Argument1));
			}
			break;
		case 31:
			AssignStoredValue(Register(frame, instruction.Argument0), Allocate(SquirrelValueType::Array));
			break;
		case 32:
			Append(Register(frame, instruction.Argument0), instruction.Argument3 != 0 ? Literal(frame, instruction.Argument1) : Register(frame, instruction.Argument1));
			break;
		case 33:
			AssignStoredValue(Register(frame, instruction.Argument0), Parent(Register(frame, instruction.Argument1)));
			break;
		case 34:
			CompoundSlot(frame, instruction);
			break;
		case 35:
			CompoundLocal(frame, instruction);
			break;
		case 36:
		case 38:
			CompoundSlot(frame, instruction);
			break;
		case 37:
			CompoundLocal(frame, instruction);
			break;
		case 39:
			CompoundLocal(frame, instruction);
			break;
		case 40:
			CompareInstruction(frame, instruction);
			break;
		case 41:
			ExistsInstruction(frame, instruction);
			break;
		case 42:
			AssignStoredValue(Register(frame, instruction.Argument0), InstanceOf(Register(frame, instruction.Argument2), Register(frame, instruction.Argument1)));
			break;
		case 43:
			if (!IsTrue(Register(frame, instruction.Argument2))) {
				AssignStoredValue(Register(frame, instruction.Argument0), Register(frame, instruction.Argument2));
				Jump(frame, instruction.Argument1);
			}

			break;
		case 44:
			if (IsTrue(Register(frame, instruction.Argument2))) {
				AssignStoredValue(Register(frame, instruction.Argument0), Register(frame, instruction.Argument2));
				Jump(frame, instruction.Argument1);
			}

			break;
		case 45:
			NegateInstruction(frame, instruction);
			break;
		case 46:
			AssignStoredValue(Register(frame, instruction.Argument0), !IsTrue(Register(frame, instruction.Argument1)));
			break;
		case 47:
			AssignStoredValue(Register(frame, instruction.Argument0), WrapInteger(~std::bit_cast<std::uint32_t>(IntegerValue(Register(frame, instruction.Argument1)))));
			break;
		case 48:
			AssignStoredValue(Register(frame, instruction.Argument0), MakeClosure(frame, instruction.Argument1));
			break;
		case 49:
			ReturnFrame(instruction.Argument1 == kDiscardRegister ? SquirrelValue{} : Register(frame, instruction.Argument1), true);
			break;
		case 50:
			ResumeGenerator(Register(frame, instruction.Argument1), instruction.Argument0);
			break;
		case 51:
			ForEach(frame, instruction);
			break;
		case 52:
			if (Object(Register(frame, instruction.Argument0)).GeneratorDead) {
				Jump(frame, instruction.Argument1 - 1);
			}

			break;
		case 53:
			SetDelegate(Register(frame, instruction.Argument1), Register(frame, instruction.Argument2));
			AssignStoredValue(Register(frame, instruction.Argument0), Register(frame, instruction.Argument1));
			break;
		case 54:
			CloneInstruction(frame, instruction);
			break;
		case 55:
			TypeInstruction(frame, instruction);
			break;
		case 56:
			if (frame.Traps.size() >= m_Limits.MaxTraps) {
				throw std::runtime_error("Squirrel exception trap limit exceeded");
			}

			frame.Traps.push_back(VmTrap{BranchTarget(frame, instruction.Argument1), instruction.Argument0, OperandFrameState{Active().Stack.Base(), Active().Stack.Top()}});
			break;
		case 57:
			if (instruction.Argument0 > frame.Traps.size()) {
				throw std::runtime_error("Squirrel exception trap underflow");
			}

			frame.Traps.resize(frame.Traps.size() - instruction.Argument0);
			break;
		case 58:
			throw VmThrownValue{Register(frame, instruction.Argument0)};
		case 59:
			NewClass(frame, instruction);
			break;
		case 60:
			NewAttributedSlot(frame, instruction);
			break;
		default:
			throw std::runtime_error("Squirrel unknown opcode " + std::to_string(instruction.Opcode));
		}
	}

	SquirrelNativeResult SquirrelNativeResult::NoValues() {
		return StackStatus(0);
	}

	SquirrelNativeResult SquirrelNativeResult::ReturnValue(SquirrelValue value) {
		SquirrelNativeResult result;
		result.Value = std::move(value);
		result.NativeStatus = 1;
		result.PushReturnedValue = true;

		return result;
	}

	SquirrelNativeResult SquirrelNativeResult::StackStatus(std::int32_t status) {
		SquirrelNativeResult result;
		result.NativeStatus = status;

		return result;
	}

	SquirrelVm::SquirrelVm(SquirrelVmLimits limits) : m_Implementation(std::make_unique<Implementation>(*this, limits)) {
	}

	SquirrelVm::~SquirrelVm() {
		try {
			m_Implementation->Close();
		} catch (...) {
			m_Implementation->m_ReleaseFailure = std::current_exception();
			m_Implementation->m_LifetimeOwner.reset();
		}
	}

	void SquirrelVm::RequireCanClose() const {
		m_Implementation->RequireCanClose();
	}

	void SquirrelVm::Close() {
		m_Implementation->Close();
	}

	bool SquirrelVm::IsClosed() const {
		return m_Implementation->m_Closed;
	}

	SquirrelValue SquirrelVm::RootTable() const {
		return m_Implementation->m_Closed ? SquirrelValue{} : m_Implementation->Active().RootTable;
	}

	void SquirrelVm::SetRootTable(const SquirrelValue& table) {
		SetRootTableInContext(CurrentContext(), table);
	}

	SquirrelValue SquirrelVm::RootTableInContext(SquirrelContextHandle context) const {
		return m_Implementation->Context(context).RootTable;
	}

	void SquirrelVm::SetRootTableInContext(SquirrelContextHandle context, const SquirrelValue& table) {
		auto& state = m_Implementation->Context(context);
		const auto type = m_Implementation->Type(table);
		if (type != SquirrelValueType::Table && type != SquirrelValueType::Null) {
			const std::string error = "ivalid type";
			AssignStoredValue(state.LastError, error);

			throw SquirrelVmException(error, error);
		}

		AssignStoredValue(state.RootTable, table);
	}

	SquirrelValue SquirrelVm::ConstantTable() const {
		return m_Implementation->m_ConstantTable;
	}

	void SquirrelVm::SetConstantTable(const SquirrelValue& table) {
		if (m_Implementation->Type(table) != SquirrelValueType::Table) {
			const std::string error = "ivalid type, expected table";

			throw SquirrelVmException(error, error);
		}

		m_Implementation->m_ConstantTable = std::get<SquirrelObjectHandle>(table);
	}

	SquirrelValue SquirrelVm::CreateString(std::string text) {
		return m_Implementation->InternedValue(std::move(text));
	}

	SquirrelValue SquirrelVm::CreateTable() {
		return m_Implementation->Allocate(SquirrelValueType::Table);
	}

	SquirrelValue SquirrelVm::CreateUserData(std::span<const std::uint8_t> payload) {
		return m_Implementation->CreateUserData(payload);
	}

	std::span<std::uint8_t> SquirrelVm::UserDataBytes(const SquirrelValue& userdata) {
		return m_Implementation->UserData(userdata).UserDataPayload;
	}

	void SquirrelVm::SetUserDataReleaseHook(const SquirrelValue& userdata, UserDataReleaseCallback callback) {
		m_Implementation->UserData(userdata).UserDataHook = std::move(callback);
	}

	void SquirrelVm::SetUserDataTypeTag(const SquirrelValue& userdata, const void* type_tag) {
		m_Implementation->UserData(userdata).UserDataTag = type_tag;
	}

	const void* SquirrelVm::UserDataTypeTag(const SquirrelValue& userdata) const {
		return m_Implementation->UserData(userdata).UserDataTag;
	}

	void SquirrelVm::SetUserDataDelegate(const SquirrelValue& userdata, const SquirrelValue& delegate) {
		auto& object = m_Implementation->UserData(userdata);
		m_Implementation->SetDelegable(userdata, object, delegate);
	}

	SquirrelValue SquirrelVm::CreateArray(std::span<const SquirrelValue> values) {
		const auto array = m_Implementation->Allocate(SquirrelValueType::Array);
		for (const auto& value : values) {
			m_Implementation->Append(array, value);
		}

		return array;
	}

	void SquirrelVm::AppendArray(const SquirrelValue& array, const SquirrelValue& value) {
		m_Implementation->Append(array, value);
	}

	SquirrelValue SquirrelVm::RemoveArray(const SquirrelValue& array, std::int32_t index) {
		return m_Implementation->RemoveArray(array, index);
	}

	void SquirrelVm::ClearArray(const SquirrelValue& array) {
		m_Implementation->ClearArray(array);
	}

	SquirrelValue SquirrelVm::CreateClosure(BytecodeProgram program) {
		const auto handle = m_Implementation->Allocate(SquirrelValueType::Closure);
		auto& closure = m_Implementation->Object(handle);
		closure.Program = std::make_shared<const BytecodeProgram>(std::move(program));
		closure.Function = &closure.Program->MainFunction;
		std::size_t prototype_count = 0;
		closure.Prototype = m_Implementation->CreatePrototype(*closure.Function, 0, prototype_count);
		if (!closure.Function->OuterBindings.empty() || !closure.Function->DefaultParameters.empty()) {
			throw std::runtime_error("Squirrel root closure cannot capture absent parent frame");
		}

		return handle;
	}

	SquirrelValue SquirrelVm::CreateClosureInContext(SquirrelContextHandle context, BytecodeProgram program) {
		if (ContextState(context) != SquirrelContextState::Idle) {
			throw std::runtime_error("compiled closure preparation requires an idle receiving context");
		}

		auto& state = *m_Implementation;
		const auto previous_context = state.m_ActiveContext;
		state.m_ActiveContext = context.Index;
		try {
			auto closure = CreateClosure(std::move(program));
			state.m_ActiveContext = previous_context;

			return closure;
		} catch (...) {
			state.m_ActiveContext = previous_context;
			throw;
		}
	}

	SquirrelValue SquirrelVm::CreateClass(const SquirrelValue& base, const SquirrelValue& attributes) {
		return m_Implementation->CreateClass(base, attributes);
	}

	void SquirrelVm::SetClassTypeTag(const SquirrelValue& class_value, const void* type_tag) {
		auto& object = m_Implementation->Object(class_value);
		if (object.Type != SquirrelValueType::Class) {
			throw std::runtime_error("Squirrel class type tag requires a class");
		}

		object.ClassTag = type_tag;
	}

	const void* SquirrelVm::ClassTypeTag(const SquirrelValue& class_value) const {
		const auto& object = m_Implementation->Object(class_value);
		if (object.Type != SquirrelValueType::Class) {
			throw std::runtime_error("Squirrel class type tag requires a class");
		}

		return object.ClassTag;
	}

	const void* SquirrelVm::InstanceClassTypeTag(const SquirrelValue& value) const {
		const auto& instance = m_Implementation->Object(value);
		if (instance.Type != SquirrelValueType::Instance || !instance.InstanceClass) {
			throw std::runtime_error("Squirrel class-tag lookup requires a live class instance");
		}

		return m_Implementation->Object(*instance.InstanceClass).ClassTag;
	}

	bool SquirrelVm::InstanceClassHasTypeTag(const SquirrelValue& instance, const void* type_tag) const {
		return m_Implementation->InstanceClassHasTypeTag(instance, type_tag);
	}

	SquirrelValue SquirrelVm::CreateInstance(const SquirrelValue& class_value) {
		return m_Implementation->CreateInstance(class_value);
	}

	SquirrelObjectIdentity SquirrelVm::BorrowObjectIdentity(const SquirrelValue& value) const {
		static_cast<void>(m_Implementation->Object(value));
		const auto& handle = std::get<SquirrelObjectHandle>(value);
		SquirrelObjectIdentity identity;
		identity.Index = handle.Index;
		identity.Generation = handle.Generation;
		identity.Owner = handle.Owner;
		identity.m_LifetimeToken = handle.m_Lifetime;

		return identity;
	}

	std::optional<SquirrelValue> SquirrelVm::ResolveObjectIdentity(const SquirrelObjectIdentity& identity) const {
		if (identity.Owner == nullptr) {
			return {};
		}

		if (identity.Owner != m_Implementation.get()) {
			throw std::runtime_error("Squirrel object identity does not belong to this VM");
		}

		const auto lifetime = identity.m_LifetimeToken.lock();
		if (identity.Index >= m_Implementation->m_ObjectLifetimes.size() || !lifetime || lifetime != m_Implementation->m_ObjectLifetimes[identity.Index] || !m_Implementation->m_ObjectLifetimes[identity.Index]->Alive || identity.Generation != m_Implementation->m_ObjectLifetimes[identity.Index]->Generation) {
			return {};
		}

		return m_Implementation->Handle(identity.Index);
	}

	std::uint32_t SquirrelVm::CollectGarbage() {
		return m_Implementation->CollectGarbage();
	}

	void SquirrelVm::DispatchBorrowedReceiver(const SquirrelObjectIdentity& identity, const std::function<void(const SquirrelValue&)>& operation) {
		auto& state = *m_Implementation;
		if (state.m_Closed || state.m_Closing || state.m_RetiringHostRoots) {
			throw std::runtime_error("Squirrel borrowed receiver dispatch requires a live VM");
		}
		if (state.m_DrainingReleases || state.m_Collecting) {
			throw std::runtime_error("Squirrel borrowed receiver dispatch cannot run during retirement");
		}
		if (!operation) {
			throw std::runtime_error("Squirrel borrowed receiver dispatch requires a concrete operation");
		}
		if (identity.Owner != m_Implementation.get()) {
			throw std::runtime_error("Squirrel borrowed receiver identity does not belong to this VM");
		}

		const auto lifetime = identity.m_LifetimeToken.lock();
		if (identity.Index >= state.m_ObjectLifetimes.size() || !lifetime || lifetime != state.m_ObjectLifetimes[identity.Index] || !state.m_ObjectLifetimes[identity.Index]->Alive || identity.Generation != state.m_ObjectLifetimes[identity.Index]->Generation) {
			throw std::runtime_error("Squirrel borrowed receiver identity has retired");
		}
		if (state.m_ObjectLifetimes[identity.Index]->Finalizing || state.m_ObjectLifetimes[identity.Index]->ReleaseQueued) {
			throw std::runtime_error("Squirrel borrowed receiver identity is retiring");
		}
		if (state.m_Objects[identity.Index]->Type != SquirrelValueType::Instance) {
			throw std::runtime_error("Squirrel borrowed receiver dispatch requires an instance identity");
		}

		// The callback may retire the registry record that supplied the identity reference.
		const auto index = identity.Index;
		const auto generation = identity.Generation;
		const VmBorrowedReceiverIdentity scope_identity{index, generation};
		if (std::find(state.m_BorrowedReceiverScopes.begin(), state.m_BorrowedReceiverScopes.end(), scope_identity) != state.m_BorrowedReceiverScopes.end()) {
			throw std::runtime_error("Squirrel borrowed receiver dispatch cannot reenter the same receiver");
		}
		if (state.m_BorrowedReceiverScopes.size() == kMaxBorrowedReceiverDepth) {
			throw std::runtime_error("Squirrel borrowed receiver dispatch depth exceeds 64 scopes");
		}

		const VmBorrowedReceiverScope scope(state.m_BorrowedReceiverScopes, scope_identity);
		const SquirrelValue receiver = state.Handle(index, true);
		operation(receiver);
		// A borrow does not preserve an instance through last-reference loss or collection.
		if (!state.m_ObjectLifetimes[index]->Alive || state.m_ObjectLifetimes[index] != lifetime || state.m_ObjectLifetimes[index]->Generation != generation) {
			throw std::runtime_error("Squirrel borrowed receiver retired during dispatch");
		}
	}

	void SquirrelVm::SetInstanceReleaseHook(const SquirrelValue& instance, InstanceReleaseCallback callback) {
		auto& object = m_Implementation->Object(instance);
		if (object.Type != SquirrelValueType::Instance) {
			throw std::runtime_error("Squirrel instance release hook requires an instance");
		}

		object.ReleaseHook = std::move(callback);
	}

	void SquirrelVm::RethrowReleaseFailure() {
		const auto failure = std::exchange(m_Implementation->m_ReleaseFailure, {});
		if (failure) {
			std::rethrow_exception(failure);
		}
	}

	void SquirrelVm::SetInstanceHostIdentity(const SquirrelValue& instance, SquirrelInstanceHostIdentity identity) {
		auto& object = m_Implementation->Object(instance);
		if (object.Type != SquirrelValueType::Instance || !identity.TypeTag) {
			throw std::runtime_error("Squirrel host identity requires an instance and a non-null type tag");
		}

		if (object.HostIdentity && object.HostIdentity->TypeTag != identity.TypeTag) {
			throw std::runtime_error("Squirrel instance host type tag is already assigned");
		}

		object.HostIdentity = identity;
	}

	SquirrelInstanceHostIdentity SquirrelVm::InstanceHostIdentity(const SquirrelValue& instance, const void* type_tag) const {
		const auto& object = m_Implementation->Object(instance);
		if (object.Type != SquirrelValueType::Instance || !object.HostIdentity || !type_tag || object.HostIdentity->TypeTag != type_tag) {
			throw std::runtime_error("Squirrel instance host type tag does not match");
		}

		return *object.HostIdentity;
	}

	SquirrelValue SquirrelVm::CreateNativeClosure(std::string name, NativeCallback callback, SquirrelNativeSignature signature) {
		return CreateNativeClosureWithCaptures(std::move(name), std::move(callback), {}, std::move(signature));
	}

	SquirrelValue SquirrelVm::CreateNativeClosureWithCaptures(std::string name, NativeCallback callback, std::span<const SquirrelValue> pushed_captures, SquirrelNativeSignature signature) {
		if (pushed_captures.size() > m_Implementation->m_Limits.MaxRegisters) {
			throw VmSafetyFailure("Squirrel native capture count exceeds register limit");
		}

		for (const auto& capture : pushed_captures) {
			m_Implementation->CheckValue(capture);
		}

		if (!callback) {
			throw std::runtime_error("Squirrel native callback is empty");
		}

		const auto handle = m_Implementation->Allocate(SquirrelValueType::NativeClosure);
		auto& closure = m_Implementation->Object(handle);
		closure.NativeName = std::move(name);
		closure.Native = std::move(callback);
		closure.Captures.assign(pushed_captures.rbegin(), pushed_captures.rend());
		SetNativeSignature(handle, std::move(signature));

		return handle;
	}

	std::size_t SquirrelVm::NativeStackSize() const {
		const auto& stack = m_Implementation->NativeStack();

		return stack.Top() - stack.Base();
	}

	SquirrelValue SquirrelVm::NativeStackValue(std::int32_t index) const {
		const auto& stack = m_Implementation->NativeStack();
		const auto count = stack.Top() - stack.Base();
		const auto offset = index > 0 ? static_cast<std::int64_t>(index) - 1 : static_cast<std::int64_t>(count) + index;
		if (index == 0 || offset < 0 || static_cast<std::uint64_t>(offset) >= count) {
			throw VmSafetyFailure("Squirrel native stack index exceeds its logical extent");
		}

		return stack.At(stack.Base() + static_cast<std::size_t>(offset));
	}

	SquirrelValue SquirrelVm::NativeAllocatedStackValue(std::int32_t index) const {
		const auto& stack = m_Implementation->NativeStack();
		const auto allocated = stack.AllocatedValues().size();
		std::size_t position = 0;
		if (index > 0) {
			const auto offset = static_cast<std::size_t>(index - 1);
			if (stack.Base() >= allocated || offset >= allocated - stack.Base()) {
				throw VmSafetyFailure("Squirrel native stack index exceeds its allocated extent");
			}

			position = stack.Base() + offset;
		} else {
			const auto magnitude = static_cast<std::uint64_t>(-static_cast<std::int64_t>(index));
			if (index == 0 || magnitude > stack.Top()) {
				throw VmSafetyFailure("Squirrel native stack index exceeds its allocated extent");
			}

			position = stack.Top() - static_cast<std::size_t>(magnitude);
		}

		return stack.At(position);
	}

	void SquirrelVm::PushNativeValue(const SquirrelValue& value) {
		m_Implementation->CheckValue(value);
		auto& stack = m_Implementation->NativeStack();
		if (stack.Top() - stack.Base() >= m_Implementation->m_Limits.MaxRegisters) {
			throw VmSafetyFailure("Squirrel native stack size exceeds its register limit");
		}

		stack.EnsurePushExtent(1);
		stack.Push(value);
	}

	void SquirrelVm::SetNativeStackSize(std::size_t count) {
		if (count > m_Implementation->m_Limits.MaxRegisters) {
			throw VmSafetyFailure("Squirrel native stack size exceeds its register limit");
		}

		m_Implementation->NativeStack().SetTop(count);
	}

	void SquirrelVm::SetNativeSignature(const SquirrelValue& callable, SquirrelNativeSignature signature) {
		auto& object = m_Implementation->Object(callable);
		if (object.Type != SquirrelValueType::NativeClosure || signature.TypeCheck.size() > m_Implementation->m_Limits.MaxRegisters || (signature.Name && signature.Name->size() > m_Implementation->m_Limits.MaxStringBytes)) {
			throw std::runtime_error("native signature requires a native closure and bounded metadata");
		}

		if (signature.ParameterCheck == -99999) {
			signature.ParameterCheck = static_cast<std::int32_t>(signature.TypeCheck.size());
		}

		object.Signature = std::move(signature);
		object.NativeSignatureName = object.Signature.Name ? CreateString(*object.Signature.Name) : SquirrelValue{};
	}

	SquirrelValue SquirrelVm::CreateWeakReference(const SquirrelValue& value) {
		m_Implementation->CheckValue(value);
		const auto* target = std::get_if<SquirrelObjectHandle>(&value);
		if (!target) {
			if (const auto* text = std::get_if<SquirrelString>(&value)) {
				const auto record = m_Implementation->InternString(*text);
				if (record->WeakReference) {
					if (const auto cached = ResolveObjectIdentity(*record->WeakReference)) {
						return *cached;
					}
				}

				const auto weak = m_Implementation->Allocate(SquirrelValueType::WeakReference);
				m_Implementation->Object(weak).WeakStringTarget = record;
				record->WeakReference = BorrowObjectIdentity(weak);

				return weak;
			}

			return value;
		}

		auto& object = m_Implementation->Object(*target);
		if (!object.WeakReference) {
			const auto weak = m_Implementation->Allocate(SquirrelValueType::WeakReference);
			m_Implementation->Object(weak).WeakTarget = BorrowedIdentity(*target);
			object.WeakReference = BorrowedIdentity(weak);

			return weak;
		}

		return m_Implementation->Handle(*object.WeakReference);
	}

	SquirrelValue SquirrelVm::ResolveWeakReference(const SquirrelValue& value) const {
		const auto& weak = m_Implementation->Object(value);
		if (weak.Type != SquirrelValueType::WeakReference) {
			throw std::runtime_error("Squirrel ref requires a weak reference");
		}

		if (weak.WeakTarget) {
			return m_Implementation->Handle(*weak.WeakTarget);
		}

		if (const auto raw = weak.WeakStringTarget.lock()) {
			const auto record = std::static_pointer_cast<SquirrelString::Storage>(raw);
			const auto owner = record->Owner.lock();
			if (owner && owner->Alive) {
				return SquirrelString(record);
			}
		}

		return {};
	}

	void SquirrelVm::RegisterNative(std::string name, NativeCallback callback) {
		const auto closure = CreateNativeClosure(name, std::move(callback));
		NewSlot(RootTable(), std::move(name), closure);
	}

	SquirrelValue SquirrelVm::BindEnvironment(const SquirrelValue& callable, const SquirrelValue& environment) {
		const auto type = Type(callable);
		if (type != SquirrelValueType::Closure && type != SquirrelValueType::NativeClosure) {
			throw std::runtime_error("the target is not a closure");
		}

		const auto environment_type = Type(environment);
		if (environment_type != SquirrelValueType::Table && environment_type != SquirrelValueType::Class && environment_type != SquirrelValueType::Instance) {
			throw std::runtime_error("invalid environment");
		}

		const auto handle = m_Implementation->Allocate(type);
		auto& clone = m_Implementation->Object(handle);
		clone = m_Implementation->Object(callable);
		clone.WeakReference.reset();
		clone.BoundEnvironment = std::get<SquirrelObjectHandle>(CreateWeakReference(environment));

		return handle;
	}

	bool SquirrelVm::HasBoundEnvironment(const SquirrelValue& callable) const {
		const auto type = Type(callable);
		return (type == SquirrelValueType::Closure || type == SquirrelValueType::NativeClosure) && m_Implementation->Object(callable).BoundEnvironment.has_value();
	}

	void SquirrelVm::InstallRecoveredBaseLibrary() {
		m_Implementation->InstallBaseLibrary();
	}

	void SquirrelVm::NewSlot(const SquirrelValue& object, const SquirrelValue& key, const SquirrelValue& value, bool is_static) {
		m_Implementation->NewSlot(object, key, value, is_static);
	}

	void SquirrelVm::SetSlot(const SquirrelValue& object, const SquirrelValue& key, const SquirrelValue& value) {
		m_Implementation->SetSlot(object, key, value);
	}

	SquirrelHostRoot SquirrelVm::Retain(const SquirrelValue& value) {
		auto reference = std::make_unique<SquirrelHostRoot::Implementation>();
		reference->BorrowedValue = m_Implementation->BorrowedValue(value);
		reference->Owner = m_Implementation->m_HostRoots;
		reference->Entry = m_Implementation->RetainHostValue(value);

		return SquirrelHostRoot(std::move(reference));
	}

	std::uint32_t SquirrelVm::RetainedReferenceCount(const SquirrelValue& value) const {
		return m_Implementation->RetainedReferenceCount(value);
	}

	SquirrelHostRoot::SquirrelHostRoot() = default;
	SquirrelHostRoot::~SquirrelHostRoot() = default;
	SquirrelHostRoot::SquirrelHostRoot(SquirrelHostRoot&&) noexcept = default;
	SquirrelHostRoot& SquirrelHostRoot::operator=(SquirrelHostRoot&&) noexcept = default;

	SquirrelHostRoot::SquirrelHostRoot(std::unique_ptr<Implementation> implementation) : m_Implementation(std::move(implementation)) {
	}

	const SquirrelValue& SquirrelHostRoot::Value() const {
		if (!m_Implementation) {
			throw std::runtime_error("Squirrel host root is empty");
		}

		if (m_Implementation->Owner.expired()) {
			throw std::runtime_error("Squirrel host root owner expired");
		}

		return m_Implementation->BorrowedValue;
	}

	SquirrelValue SquirrelVm::GetSlot(const SquirrelValue& object, const SquirrelValue& key) const {
		return m_Implementation->GetSlot(object, key);
	}

	std::optional<SquirrelValue> SquirrelVm::FindRawSlot(const SquirrelValue& object, const SquirrelValue& key) const {
		return m_Implementation->FindRawSlot(object, key);
	}

	SquirrelValue SquirrelVm::ReadSlot(const SquirrelValue& object, const SquirrelValue& key, std::uint64_t instruction_budget) {
		return ReadSlotInContext(CurrentContext(), object, key, instruction_budget);
	}

	SquirrelValue SquirrelVm::ReadSlotInContext(SquirrelContextHandle context, const SquirrelValue& object, const SquirrelValue& key, std::uint64_t instruction_budget) {
		static_cast<void>(m_Implementation->Context(context));
		VmReadContextScope activation(m_Implementation->m_ActiveContext, context.Index);

		const auto type = Type(object);
		if (type == SquirrelValueType::Table || type == SquirrelValueType::UserData) {
			return m_Implementation->ReadDelegatedHost(object, key, instruction_budget);
		}

		if (HasSlot(object, key) || m_Implementation->HasDelegatedTableDefault(object, key)) {
			return GetSlot(object, key);
		}

		if (const auto method = m_Implementation->MetaMethod(object, "_get")) {
			const std::array<SquirrelValue, 2> arguments{object, key};
			SquirrelRunResult result;
			++m_Implementation->m_MetaCallDepth;
			try {
				result = InvokeInContext(CurrentContext(), *method, arguments, instruction_budget, false);
			} catch (const VmSafetyFailure&) {
				--m_Implementation->m_MetaCallDepth;
				throw;
			} catch (const std::runtime_error&) {
				--m_Implementation->m_MetaCallDepth;
				if (const auto fallback = m_Implementation->FindDefaultSlot(object, key)) {
					return *fallback;
				}

				AssignStoredValue(m_Implementation->Active().LastError, std::string("the index doesn't exist"));
				throw SquirrelVmException(std::string("the index doesn't exist"), "the index doesn't exist");
			} catch (...) {
				--m_Implementation->m_MetaCallDepth;
				throw;
			}

			--m_Implementation->m_MetaCallDepth;

			return result.Value;
		}

		if (Type(object) != SquirrelValueType::Class && !(Type(object) == SquirrelValueType::Array && IsNumber(key))) {
			if (const auto fallback = m_Implementation->FindDefaultSlot(object, key)) {
				return *fallback;
			}
		}

		// Native222a80 replaces the failed lookup's error instead of exposing _get failure.
		AssignStoredValue(m_Implementation->Active().LastError, std::string("the index doesn't exist"));
		throw SquirrelVmException(std::string("the index doesn't exist"), "the index doesn't exist");
	}

	SquirrelPropertyReadResult SquirrelVm::ReadPropertyInContext(SquirrelContextHandle context, const SquirrelValue& object, std::string_view key, SquirrelPropertyReadKind kind, std::uint64_t instruction_budget) {
		auto& owner = *m_Implementation;
		auto& state = owner.Context(context);
		if (owner.m_Closed || state.Suspended || state.PropertyReadDepth >= owner.m_Limits.MaxFrames || key.size() > owner.m_Limits.MaxStringBytes) {
			throw std::runtime_error("Squirrel property read requires a live nonsuspended bounded context");
		}
		if (kind != SquirrelPropertyReadKind::RawPresence && kind != SquirrelPropertyReadKind::Integer && kind != SquirrelPropertyReadKind::Number && kind != SquirrelPropertyReadKind::Boolean && kind != SquirrelPropertyReadKind::String) {
			throw std::runtime_error("Squirrel property read conversion kind is invalid");
		}

		owner.CheckValue(object);
		const auto receiver = owner.BorrowedValue(object);
		std::string key_text(key);
		VmReadContextScope activation(owner.m_ActiveContext, context.Index);
		VmPropertyReadScope property_scope(state);
		OperandArgumentScope operands(state.Stack, true);
		const auto previous_base = state.Stack.Base();
		const auto previous_top = state.Stack.Top();
		state.Stack.EnsurePushExtent(2);
		state.Stack.Push(owner.ResolveStoredValue(receiver));
		state.Stack.Push(CreateString(std::move(key_text)));
		if (!owner.m_Running) {
			owner.m_InstructionRemaining = instruction_budget;
		}

		SquirrelPropertyReadResult result;
		const bool raw = kind == SquirrelPropertyReadKind::RawPresence;
		result.Present = owner.ReadAliasedProperty(previous_top, previous_top + 1, raw);
		if (result.Present && !raw) {
			const auto& value = state.Stack.At(previous_top + 1);
			switch (kind) {
			case SquirrelPropertyReadKind::Integer:
				result.Integer = TryNativeInteger(value).value_or(0);
				break;
			case SquirrelPropertyReadKind::Number:
				if (const auto* number = std::get_if<float>(&value)) {
					result.Number = *number;
				} else if (const auto integer = TryNativeInteger(value)) {
					result.Number = static_cast<float>(*integer);
				}
				break;
			case SquirrelPropertyReadKind::Boolean:
				result.Boolean = TryNativeInteger(value).value_or(0) != 0;
				break;
			case SquirrelPropertyReadKind::String:
				if (const auto* text = TryStringText(value)) {
					result.String = *text;
				}
				break;
			case SquirrelPropertyReadKind::RawPresence:
				break;
			}
		}

		// Native2229d0 always pops the key; 222a80 pops it on failure before replacing LastError.
		state.Stack.SetTop(previous_top + 1 - previous_base);
		if (!result.Present && !raw) {
			AssignStoredValue(state.LastError, std::string("the index doesn't exist"));
		}
		state.Stack.SetTop(previous_top - previous_base);
		operands.TransferToAcceptedFrame();

		return result;
	}

	void SquirrelVm::AssignSlot(const SquirrelValue& object, const SquirrelValue& key, const SquirrelValue& value, std::uint64_t instruction_budget) {
		const bool existing = m_Implementation->CanAssignRaw(object, key);
		if (!existing) {
			if (const auto method = m_Implementation->MetaMethod(object, "_set")) {
				const std::array<SquirrelValue, 3> arguments{object, key, value};
				++m_Implementation->m_MetaCallDepth;
				try {
					static_cast<void>(InvokeInContext(CurrentContext(), *method, arguments, instruction_budget, false));
				} catch (...) {
					--m_Implementation->m_MetaCallDepth;
					throw;
				}

				--m_Implementation->m_MetaCallDepth;
				return;
			}
		}

		SetSlot(object, key, value);
	}

	bool SquirrelVm::IsInstanceOf(const SquirrelValue& instance, const SquirrelValue& base) const {
		return m_Implementation->InstanceOf(instance, base);
	}

	bool SquirrelVm::HasSlot(const SquirrelValue& object, const SquirrelValue& key) const {
		return m_Implementation->HasSlot(object, key);
	}

	std::size_t SquirrelVm::Length(const SquirrelValue& object) const {
		return m_Implementation->Length(object);
	}

	SquirrelValueType SquirrelVm::Type(const SquirrelValue& value) const {
		return m_Implementation->Type(value);
	}

	std::string SquirrelVm::TypeName(const SquirrelValue& value) const {
		return Implementation::TypeName(Type(value));
	}

	std::string SquirrelVm::ToString(const SquirrelValue& value, std::uint64_t instruction_budget) {
		m_Implementation->CheckValue(value);
		if (const auto method = m_Implementation->MetaMethod(value, "_tostring")) {
			const std::array<SquirrelValue, 1> arguments{value};
			SquirrelRunResult result;
			++m_Implementation->m_MetaCallDepth;
			try {
				result = InvokeInContext(CurrentContext(), *method, arguments, instruction_budget, false);
			} catch (const VmSafetyFailure&) {
				throw;
			} catch (const std::runtime_error&) {
				--m_Implementation->m_MetaCallDepth;
				return m_Implementation->PointerStringValue(value);
			} catch (...) {
				--m_Implementation->m_MetaCallDepth;
				throw;
			}

			--m_Implementation->m_MetaCallDepth;
			if (const auto* text = TryStringText(result.Value)) {
				return *text;
			}

			return m_Implementation->PointerStringValue(value);
		}

		return m_Implementation->StringValue(value);
	}

	std::int32_t SquirrelVm::CompareValues(const SquirrelValue& left, const SquirrelValue& right, std::uint64_t instruction_budget) {
		m_Implementation->CheckValue(left);
		m_Implementation->CheckValue(right);
		if (Type(left) == Type(right) && !EqualKeys(left, right)) {
			if (const auto method = m_Implementation->MetaMethod(left, "_cmp")) {
				const std::array<SquirrelValue, 2> arguments{left, right};
				SquirrelRunResult result;
				++m_Implementation->m_MetaCallDepth;
				try {
					result = InvokeInContext(CurrentContext(), *method, arguments, instruction_budget, false);
				} catch (const VmSafetyFailure&) {
					throw;
				} catch (const std::runtime_error&) {
					--m_Implementation->m_MetaCallDepth;

					return m_Implementation->RawOrder(left, right);
				} catch (...) {
					--m_Implementation->m_MetaCallDepth;

					throw;
				}

				--m_Implementation->m_MetaCallDepth;
				if (!std::holds_alternative<std::int32_t>(result.Value)) {
					const auto error = m_Implementation->ComparisonError(left, right);
					AssignStoredValue(m_Implementation->Active().LastError, error);

					throw SquirrelVmException(error, error);
				}

				return std::get<std::int32_t>(result.Value);
			}
		}

		try {
			return m_Implementation->RawOrder(left, right);
		} catch (const VmSafetyFailure&) {
			throw;
		} catch (const std::runtime_error& error) {
			const auto value = Implementation::RuntimeErrorValue(error);
			AssignStoredValue(m_Implementation->Active().LastError, value);

			throw SquirrelVmException(value, error.what());
		}
	}

	void SquirrelVm::SetTraceCallback(TraceCallback callback) {
		m_Implementation->m_Trace = std::move(callback);
	}

	SquirrelRunResult SquirrelVm::Invoke(const SquirrelValue& callable, std::span<const SquirrelValue> arguments, std::uint64_t instruction_budget) {
		return InvokeInContext(RootContext(), callable, arguments, instruction_budget);
	}

	SquirrelRunResult SquirrelVm::Resume(const SquirrelValue& value, std::uint64_t instruction_budget) {
		return ResumeInContext(RootContext(), value, instruction_budget);
	}

	SquirrelRunResult SquirrelVm::InvokeInContext(SquirrelContextHandle context, const SquirrelValue& callable, std::span<const SquirrelValue> arguments, std::uint64_t instruction_budget, bool raise_error) {
		return CallInContext(context, callable, arguments, instruction_budget, raise_error, true);
	}

	SquirrelRunResult SquirrelVm::InvokeWithoutResultInContext(SquirrelContextHandle context, const SquirrelValue& callable, std::span<const SquirrelValue> arguments, std::uint64_t instruction_budget, bool raise_error) {
		return CallInContext(context, callable, arguments, instruction_budget, raise_error, false);
	}

	SquirrelRunResult SquirrelVm::InvokeThenDiscardResultInContext(SquirrelContextHandle context, const SquirrelValue& callable, std::span<const SquirrelValue> arguments, std::uint64_t instruction_budget, bool raise_error) {
		auto& state = m_Implementation->Context(context);
		const VmInvocationResultPopScope pop_scope(state, m_Implementation->Active());

		return CallInContext(context, callable, arguments, instruction_budget, raise_error, false, true);
	}

	SquirrelRunResult SquirrelVm::InvokeObserveCompletedResultInContext(SquirrelContextHandle context, const SquirrelValue& callable, std::span<const SquirrelValue> arguments, const std::function<void(const SquirrelValue&)>& observer, std::uint64_t instruction_budget, bool raise_error) {
		if (IsClosed() || m_Implementation->m_Closing || m_Implementation->m_RetiringHostRoots || instruction_budget == 0 || !observer) {
			throw std::runtime_error("Squirrel completed result observer requires a live VM, positive budget and observer");
		}

		auto& state = m_Implementation->Context(context);
		m_Implementation->RequireCallAdmission(state, callable);
		if (arguments.size() > m_Implementation->m_Limits.MaxRegisters) {
			throw VmSafetyFailure("Squirrel completed result observer arguments exceed register limit");
		}
		for (const auto& argument : arguments) {
			m_Implementation->CheckValue(argument);
		}
		if (state.CompletedResultObserverDepth >= m_Implementation->m_Limits.MaxFrames) {
			throw VmSafetyFailure("Squirrel completed result observer depth exceeds limit");
		}

		const auto previous_result = state.Result;
		const VmInvocationResultPopScope pop_scope(state, m_Implementation->Active());
		const VmCompletedResultObserverScope observer_scope(state);
		SquirrelRunResult result;
		try {
			result = CallInContext(context, callable, arguments, instruction_budget, raise_error, true);
		} catch (...) {
			const VmReadContextScope receiving_context(m_Implementation->m_ActiveContext, context.Index);
			AssignStoredValue(state.Result, previous_result);

			throw;
		}

		const VmReadContextScope receiving_context(m_Implementation->m_ActiveContext, context.Index);
		AssignStoredValue(state.Result, previous_result);
		if (result.State != SquirrelRunState::Completed) {
			throw VmSafetyFailure("Squirrel completed result observer invocation did not complete");
		}

		try {
			observer(result.Value);
		} catch (...) {
			ReleaseStoredValue(result.Value);

			throw;
		}

		ReleaseStoredValue(result.Value);

		return result;
	}

	SquirrelRunResult SquirrelVm::CallInContext(SquirrelContextHandle context, const SquirrelValue& callable, std::span<const SquirrelValue> arguments, std::uint64_t instruction_budget, bool raise_error, bool return_value, bool discard_after_unwind) {
		auto& state = m_Implementation->Context(context);
		const bool nested = m_Implementation->m_Running;
		m_Implementation->RequireCallAdmission(state, callable);

		const auto previous_context = m_Implementation->m_ActiveContext;
		const auto frame_floor = state.Frames.size();
		const auto previous_result = nested || return_value ? state.Result : SquirrelValue{};
		const auto previous_raise_error = state.RaiseErrors;
		if (!nested) {
			m_Implementation->m_InstructionRemaining = instruction_budget;
		}

		const auto starting_budget = m_Implementation->m_InstructionRemaining;
		m_Implementation->m_ActiveContext = context.Index;
		m_Implementation->m_Running = true;
		++m_Implementation->m_InvocationDepth;
		++state.InvocationDepth;
		AssignStoredValue(state.Result, {});
		state.RaiseErrors = raise_error;
		try {
			VmReturnAction action;
			action.Kind = VmReturnKind::Capture;
			try {
				m_Implementation->Call(callable, arguments, kDiscardRegister, false, action);
			} catch (const VmSafetyFailure&) {
				throw;
			} catch (const std::runtime_error& error) {
				const auto value = Implementation::RuntimeErrorValue(error);
				AssignStoredValue(state.LastError, value);

				throw SquirrelVmException(value, error.what());
			}

			auto result = m_Implementation->Run(instruction_budget, frame_floor);
			result.Instructions = starting_budget - m_Implementation->m_InstructionRemaining;
			if (!return_value && !discard_after_unwind) {
				AssignStoredValue(result.Value, {});
				if (!nested) {
					AssignStoredValue(state.Result, {});
				}
			}

			if (nested) {
				AssignStoredValue(state.Result, previous_result);
			}

			if (nested || result.State == SquirrelRunState::Completed) {
				state.RaiseErrors = previous_raise_error;
			}

			--m_Implementation->m_InvocationDepth;
			--state.InvocationDepth;
			m_Implementation->m_Running = nested;
			if (!return_value && discard_after_unwind) {
				if (!nested) {
					AssignStoredValue(state.Result, {});
				}

				// Clear the facade owner before the returned owner; release-hook reentry may publish its own output.
				AssignStoredValue(result.Value, {});
			}

			m_Implementation->m_ActiveContext = previous_context;

			return result;
		} catch (...) {
			while (state.Frames.size() > frame_floor) {
				const auto& frame = state.Frames.back();
				if (frame.Generator) {
					auto& generator = m_Implementation->Object(*frame.Generator);
					m_Implementation->KillGenerator(generator);
				}

				const auto stack_state = frame.StackState;
				const auto argument_cleanup = frame.ArgumentCleanupTop;
				state.Frames.pop_back();
				state.Stack.LeaveScriptFrame(stack_state);
				if (argument_cleanup) {
					state.Stack.SetTop(*argument_cleanup - state.Stack.Base());
				}
			}

			if (nested) {
				AssignStoredValue(state.Result, previous_result);
			} else if (!return_value && !discard_after_unwind) {
				AssignStoredValue(state.Result, {});
			}

			state.RaiseErrors = previous_raise_error;

			--m_Implementation->m_InvocationDepth;
			--state.InvocationDepth;
			m_Implementation->m_Running = nested;
			if (!nested && !return_value && discard_after_unwind) {
				AssignStoredValue(state.Result, {});
			}

			m_Implementation->m_ActiveContext = previous_context;
			throw;
		}
	}

	SquirrelRunResult SquirrelVm::ResumeInContext(SquirrelContextHandle context, const SquirrelValue& value, std::uint64_t instruction_budget) {
		auto& state = m_Implementation->Context(context);
		if (!state.Suspended || state.InvocationDepth != 0 || !state.NativeFrames.empty()) {
			throw std::runtime_error("Squirrel context is not suspended or cannot be resumed through native calls");
		}

		if (m_Implementation->m_InvocationDepth >= m_Implementation->m_Limits.MaxFrames) {
			throw VmSafetyFailure("Squirrel native invocation depth limit exceeded");
		}

		m_Implementation->CheckValue(value);
		const bool nested = m_Implementation->m_Running;
		const auto previous_context = m_Implementation->m_ActiveContext;
		if (!nested) {
			m_Implementation->m_InstructionRemaining = instruction_budget;
		}

		const auto starting_budget = m_Implementation->m_InstructionRemaining;
		m_Implementation->m_ActiveContext = context.Index;
		m_Implementation->m_Running = true;
		++m_Implementation->m_InvocationDepth;
		++state.InvocationDepth;
		state.Suspended = false;
		try {
			if (state.SuspendedAction) {
				const auto action = *state.SuspendedAction;
				state.SuspendedAction.reset();
				m_Implementation->ApplyReturn(action, value);
			} else {
				m_Implementation->WriteReturn(state.SuspendedRegister, value);
			}

			auto result = m_Implementation->Run(instruction_budget);
			result.Instructions = starting_budget - m_Implementation->m_InstructionRemaining;
			--m_Implementation->m_InvocationDepth;
			--state.InvocationDepth;
			m_Implementation->m_Running = nested;
			m_Implementation->m_ActiveContext = previous_context;

			return result;
		} catch (...) {
			--m_Implementation->m_InvocationDepth;
			--state.InvocationDepth;
			m_Implementation->m_Running = nested;
			m_Implementation->m_ActiveContext = previous_context;
			throw;
		}
	}

	bool SquirrelVm::IsSuspended() const {
		return m_Implementation->Active().Suspended;
	}

	SquirrelNativeResult SquirrelVm::RequestNativeSuspension() {
		if (IsClosed()) {
			throw std::runtime_error("Squirrel VM is closed");
		}

		const auto& context = m_Implementation->Active();
		if (context.Suspended) {
			throw std::runtime_error("cannot suspend an already suspended vm");
		}

		const auto meta_frames = static_cast<std::size_t>(std::count_if(context.Frames.begin(), context.Frames.end(), [](const VmFrame& frame) {
			return frame.IsMetamethod;
		}));
		if (context.NativeCallDepth > 2 || meta_frames != 2 - context.NativeCallDepth) {
			throw std::runtime_error("cannot suspend through native calls/metamethods");
		}

		return SquirrelNativeResult::StackStatus(-666);
	}

	void SquirrelVm::ResetExecution() {
		ResetContext(RootContext());
	}

	SquirrelContextHandle SquirrelVm::RootContext() const {
		return m_Implementation->ContextHandle(0);
	}

	SquirrelValue SquirrelVm::LastError() const {
		return m_Implementation->m_Closed ? SquirrelValue{} : m_Implementation->Active().LastError;
	}

	SquirrelValue SquirrelVm::LastErrorInContext(SquirrelContextHandle context) const {
		return m_Implementation->Context(context).LastError;
	}

	void SquirrelVm::SetLastError(const SquirrelValue& value) {
		SetLastErrorInContext(CurrentContext(), value);
	}

	void SquirrelVm::SetLastErrorInContext(SquirrelContextHandle context, const SquirrelValue& value) {
		auto& state = m_Implementation->Context(context);
		AssignStoredValue(state.LastError, m_Implementation->InternedValue(value));
	}

	SquirrelContextHandle SquirrelVm::CurrentContext() const {
		return m_Implementation->ContextHandle(m_Implementation->m_ActiveContext);
	}

	SquirrelContextHandle SquirrelVm::CreateContext() {
		return m_Implementation->CreateContext();
	}

	SquirrelContextHandle SquirrelVm::CreateContextFrom(SquirrelContextHandle creator) {
		return m_Implementation->CreateContext(creator);
	}

	SquirrelContextState SquirrelVm::ContextState(SquirrelContextHandle context) const {
		const auto& state = m_Implementation->Context(context);
		if (state.Suspended) {
			return SquirrelContextState::Suspended;
		}

		return state.Frames.empty() && state.InvocationDepth == 0 ? SquirrelContextState::Idle : SquirrelContextState::Running;
	}

	SquirrelContextObservation SquirrelVm::ObserveContext(SquirrelContextHandle context, std::size_t max_frames, std::size_t max_registers) const {
		const auto& owner = *m_Implementation;
		if (owner.m_Closed || owner.m_Running || owner.m_Closing || owner.m_Collecting || owner.m_DrainingReleases || owner.m_RetiringHostRoots || max_frames == 0 || max_frames > kSquirrelObservationFrames || max_registers > kSquirrelObservationRegisters) {
			throw std::runtime_error("Squirrel context observation requires its live serialized endpoint and bounded copy limits");
		}

		const auto& state = owner.Context(context);
		SquirrelContextObservation result;
		if (state.Suspended) {
			result.State = SquirrelContextState::Suspended;
		} else if (!state.Frames.empty() || state.InvocationDepth != 0) {
			result.State = SquirrelContextState::Running;
		}
		result.FrameCount = state.Frames.size();
		result.FramesTruncated = state.Frames.size() > max_frames;
		result.NativeFrameCount = state.NativeFrames.size();
		result.HasSuspendedAction = state.SuspendedAction.has_value();
		result.SuspendedRegister = state.SuspendedRegister;
		const auto first = state.Frames.size() > max_frames ? state.Frames.size() - max_frames : 0;
		result.Frames.reserve(state.Frames.size() - first);
		for (std::size_t index = first; index < state.Frames.size(); ++index) {
			const auto& frame = state.Frames[index];
			const auto& function = owner.Function(frame);
			SquirrelObservedFrame observed;
			const auto source = LiteralText(function.SourceName);
			const auto name = LiteralText(function.Name);
			observed.Source = std::string(source.substr(0, kSquirrelObservationTextBytes));
			observed.Function = std::string(name.substr(0, kSquirrelObservationTextBytes));
			observed.SourceTruncated = source.size() > kSquirrelObservationTextBytes;
			observed.FunctionTruncated = name.size() > kSquirrelObservationTextBytes;
			observed.NextInstruction = frame.Instruction;
			if (frame.Instruction != 0 && frame.Instruction <= function.Instructions.size()) {
				observed.PreviousOperation = function.Instructions[frame.Instruction - 1];
				for (const auto& line : function.LineInformation) {
					if (line.Instruction <= frame.Instruction - 1) {
						observed.SourceLine = line.Line;
					}
				}
			}
			observed.RegisterCount = frame.Registers.Size();
			observed.RegistersTruncated = frame.Registers.Size() > max_registers;
			const auto register_count = std::min(frame.Registers.Size(), max_registers);
			observed.Registers.reserve(register_count);
			for (std::size_t register_index = 0; register_index < register_count; ++register_index) {
				const auto& value = frame.Registers[register_index];
				SquirrelObservedRegister cell;
				cell.Index = register_index;
				cell.Value.Type = owner.Type(value);
				if (const auto* integer = std::get_if<std::int32_t>(&value)) {
					cell.Value.Integer = *integer;
				} else if (const auto* number = std::get_if<float>(&value)) {
					cell.Value.FloatBits = std::bit_cast<std::uint32_t>(*number);
				} else if (const auto* boolean = std::get_if<bool>(&value)) {
					cell.Value.Boolean = *boolean;
				} else if (const auto* text = TryStringText(value)) {
					cell.Value.Text = text->substr(0, kSquirrelObservationTextBytes);
					cell.Value.TextTruncated = text->size() > kSquirrelObservationTextBytes;
				} else if (const auto* handle = std::get_if<SquirrelObjectHandle>(&value)) {
					cell.Value.ObjectIndex = handle->Index;
					cell.Value.ObjectGeneration = handle->Generation;
				}
				observed.Registers.push_back(std::move(cell));
			}
			result.Frames.push_back(std::move(observed));
		}

		return result;
	}

	void SquirrelVm::ResetContext(SquirrelContextHandle context) {
		m_Implementation->ResetContext(context);
	}

	void SquirrelVm::ReleaseContext(SquirrelContextHandle context) {
		m_Implementation->ReleaseContext(context);
	}

	SquirrelVmException::SquirrelVmException(SquirrelValue value, std::string message) : std::runtime_error(std::move(message)), m_Value(std::move(value)) {
	}

	const SquirrelValue& SquirrelVmException::ThrownValue() const {
		return m_Value;
	}
} // namespace sgre::script
