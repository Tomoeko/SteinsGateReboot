#include "sgre/text/blob_font_face.hpp"

#include <array>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

static void WriteBig(std::vector<std::uint8_t>& bytes, std::size_t field, std::uint32_t value, std::size_t width = 2) {
	for (std::size_t index = 0; index < width; ++index) {
		bytes.at(field + width - index - 1) = static_cast<std::uint8_t>(value & 0xffU);
		value >>= 8;
	}
}

static std::vector<std::uint8_t> MakeDirectory(std::uint32_t count, bool descending = false) {
	std::vector<std::uint8_t> bytes(12 + std::size_t(count) * 17);
	WriteBig(bytes, 0, 0x10000, 4);
	WriteBig(bytes, 4, count);
	for (std::uint32_t index = 0; index < count; ++index) {
		const auto entry = 12 + std::size_t(index) * 16;
		const auto number = descending ? count - index - 1 : index;
		WriteBig(bytes, entry, 0x02000000U + number, 4);
		WriteBig(bytes, entry + 8, static_cast<std::uint32_t>(12 + std::size_t(count) * 16 + index), 4);
		WriteBig(bytes, entry + 12, 1, 4);
		bytes[12 + std::size_t(count) * 16 + index] = static_cast<std::uint8_t>(index);
	}

	return bytes;
}

static std::vector<std::uint8_t> MakeCollection() {
	std::vector<std::uint8_t> bytes(64);
	WriteBig(bytes, 0, 0x74746366, 4);
	WriteBig(bytes, 4, 0x10000, 4);
	WriteBig(bytes, 8, 2, 4);
	WriteBig(bytes, 12, 20, 4);
	WriteBig(bytes, 16, 63, 4);
	WriteBig(bytes, 20, 0x4f54544f, 4);
	WriteBig(bytes, 24, 1);
	WriteBig(bytes, 32, 0x47444546, 4);
	WriteBig(bytes, 40, 60, 4);
	WriteBig(bytes, 44, 0xffffffff, 4);
	bytes[60] = 1;
	bytes[61] = 2;
	bytes[62] = 3;
	bytes[63] = 4;

	return bytes;
}

static void TestPinnedViewsAndFreezeOrder() {
	auto pin = std::make_shared<const std::vector<std::uint8_t>>(MakeDirectory(1));
	const std::weak_ptr<const std::vector<std::uint8_t>> observer = pin;
	auto face = std::make_unique<sgre::text::BlobFontFace>(pin, 0xffffffff);
	Require(face->WholeFontBytes().data() == pin->data(), "clean parent keeps exact raw resource bytes without copying");
	pin.reset();
	Require(face->Status() == sgre::text::FontFaceBlobStatus::Accepted && face->FaceIndex() == 0xffffffff && !face->ParentIsImmutable(), "single face preserves selection index and mutable logical parent flag");
	auto whole = face->ReferenceTable(0);
	Require(whole.Length() == 29 && !whole.IsImmutable() && !face->ParentIsImmutable(), "tag0 shares parent without freezing");
	auto table = face->ReferenceTable(0x02000000);
	Require(table.Length() == 1 && table.Bytes()[0] == 0 && !table.IsImmutable() && face->ParentIsImmutable() && whole.IsImmutable(), "ordinary table query freezes parent but creates a mutable child descriptor");
	face.reset();
	Require(!observer.expired() && table.Bytes()[0] == 0 && whole.Length() == 29, "real table parent pin survives face destruction");
	whole = {};
	Require(!observer.expired(), "child retains parent after parent-reference view is released");
	table = {};
	Require(observer.expired(), "last child release retires immutable font pin");
}

static void TestCollectionAdmissionAndClampedOffsets() {
	const auto bytes = MakeCollection();
	auto pin = std::make_shared<const std::vector<std::uint8_t>>(bytes);
	const std::weak_ptr<const std::vector<std::uint8_t>> observer = pin;
	sgre::text::BlobFontFace repaired(pin);
	pin.reset();
	Require(repaired.Status() == sgre::text::FontFaceBlobStatus::Repaired && observer.expired() && repaired.WholeFontBytes()[19] == 0 && bytes[19] == 63, "collection repair owns new parent bytes and releases original pin");
	const auto table = repaired.ReferenceTable(0x47444546);
	Require(table.Length() == 4 && table.Bytes()[0] == 1 && table.Bytes()[3] == 4, "native subblob clamps length to whole-parent remaining bytes");
	sgre::text::BlobFontFace erased(std::make_shared<const std::vector<std::uint8_t>>(bytes), 1);
	Require(erased.ReferenceTable(0x47444546).Length() == 0, "erased selected face offset resolves empty directory");
	const sgre::text::BlobFontFace immutable(std::make_shared<const std::vector<std::uint8_t>>(bytes), 0, true);
	Require(immutable.Status() == sgre::text::FontFaceBlobStatus::Rejected && immutable.WholeFontBytes().empty() && immutable.ParentIsImmutable(), "immutable collection denies needed offset repair");
	auto truncated = MakeDirectory(1);
	truncated.resize(27);
	const sgre::text::BlobFontFace rejected(std::make_shared<const std::vector<std::uint8_t>>(truncated));
	Require(rejected.Status() == sgre::text::FontFaceBlobStatus::Rejected, "truncated directory is replaced with empty parent");
	std::vector<std::uint8_t> unknown = {1, 2, 3, 4};
	sgre::text::BlobFontFace admitted(std::make_shared<const std::vector<std::uint8_t>>(unknown));
	Require(admitted.Status() == sgre::text::FontFaceBlobStatus::Accepted && admitted.ReferenceTable(0).Length() == 4 && admitted.ReferenceTable(0x47444546).Length() == 0, "unknown native signature admits bounded bytes but selects no directory");
}

static void TestOriginalLookupBranchesAndWorkBound() {
	sgre::text::BlobFontFace small(std::make_shared<const std::vector<std::uint8_t>>(MakeDirectory(63)));
	Require(small.ReferenceTable(0x02000001).Bytes()[0] == 1, "under64 directory uses first matching linear search");
	sgre::text::BlobFontFace ascending(std::make_shared<const std::vector<std::uint8_t>>(MakeDirectory(64)));
	Require(ascending.ReferenceTable(0x02000001).Length() == 0 && ascending.ReferenceTable(0x0200001f).Bytes()[0] == 31, "original64-record descending branch is preserved even for ascending data");
	sgre::text::BlobFontFace descending(std::make_shared<const std::vector<std::uint8_t>>(MakeDirectory(64, true)));
	Require(descending.ReferenceTable(0x0200003f).Bytes()[0] == 0 && descending.ReferenceTable(0x02000001).Bytes()[0] == 62, "native descending table search returns its actual record");
	std::vector<std::uint8_t> repeated(12 + 1048576 * 4);
	WriteBig(repeated, 0, 0x74746366, 4);
	WriteBig(repeated, 4, 0x10000, 4);
	WriteBig(repeated, 8, 1048576, 4);
	bool bounded = false;
	try {
		const sgre::text::BlobFontFace unsupported(std::make_shared<const std::vector<std::uint8_t>>(std::move(repeated)));
	} catch (const std::runtime_error&) {
		bounded = true;
	}
	Require(bounded, "counted zero offsets obey explicit whole-font work limit");
}

static bool ReadWord(std::uint32_t& value, bool allow_end = false) {
	std::array<char, 4> bytes{};
	std::cin.read(bytes.data(), 4);
	if (allow_end && std::cin.eof() && std::cin.gcount() == 0) {
		return false;
	}
	Require(std::cin.gcount() == 4, "truncated font face proof word");
	value = 0;
	for (std::size_t index = 0; index < bytes.size(); ++index) {
		value |= std::uint32_t(static_cast<std::uint8_t>(bytes[index])) << (index * 8);
	}

	return true;
}

static void WriteWord(std::uint32_t value) {
	for (std::size_t index = 0; index < 4; ++index) {
		std::cout.put(static_cast<char>(value & 0xffU));
		value >>= 8;
	}
}

static void WriteBytes(std::span<const std::uint8_t> bytes) {
	if (bytes.size() > 4096) {
		WriteBytes(bytes.first(32));
		WriteBytes(bytes.last(32));

		return;
	}
	for (const auto value : bytes) {
		std::cout.put(static_cast<char>(value));
	}
	std::cout.flush();
}

static void RunNativeProtocol() {
	std::uint32_t length = 0;
	while (ReadWord(length, true)) {
		std::uint32_t index = 0;
		std::uint32_t immutable = 0;
		std::uint32_t tag_count = 0;
		ReadWord(index);
		ReadWord(immutable);
		ReadWord(tag_count);
		Require(length <= 64 * 1024 * 1024 && immutable <= 1 && tag_count <= 512, "invalid font face proof domain");
		std::vector<std::uint8_t> bytes(length);
		for (auto& value : bytes) {
			const auto character = std::cin.get();
			Require(character != std::char_traits<char>::eof(), "truncated font face proof bytes");
			value = static_cast<std::uint8_t>(character);
		}
		std::vector<std::uint32_t> tags(tag_count);
		for (auto& tag : tags) {
			ReadWord(tag);
		}
		auto pin = std::make_shared<const std::vector<std::uint8_t>>(std::move(bytes));
		const std::weak_ptr<const std::vector<std::uint8_t>> observer = pin;
		auto face = std::make_unique<sgre::text::BlobFontFace>(pin, index, immutable != 0);
		pin.reset();
		WriteWord(static_cast<std::uint32_t>(face->Status()));
		WriteWord(face->FaceIndex());
		WriteWord(face->ParentIsImmutable() ? 1 : 0);
		WriteWord(static_cast<std::uint32_t>(face->WholeFontBytes().size()));
		WriteBytes(face->WholeFontBytes());
		const auto whole_bytes = face->WholeFontBytes();
		std::vector<sgre::text::FontFaceTableBlob> tables;
		for (const auto tag : tags) {
			tables.push_back(face->ReferenceTable(tag));
			WriteWord(tables.back().Length());
			WriteWord(tables.back().IsImmutable() ? 1 : 0);
			WriteWord(face->ParentIsImmutable() ? 1 : 0);
			WriteWord(tables.back().Length() == 0 ? 0 : static_cast<std::uint32_t>(tables.back().Bytes().data() - whole_bytes.data()));
			WriteBytes(tables.back().Bytes());
		}
		face.reset();
		WriteWord(observer.expired() ? 0 : 1);
		for (const auto& table : tables) {
			WriteWord(table.Length());
			WriteWord(table.IsImmutable() ? 1 : 0);
			WriteWord(table.Length() == 0 ? 0 : static_cast<std::uint32_t>(table.Bytes().data() - whole_bytes.data()));
			WriteBytes(table.Bytes());
		}
		tables.clear();
		WriteWord(observer.expired() ? 0 : 1);
		std::cout.flush();
	}
}

int main(int argc, char** argv) {
	try {
		if (argc == 2 && std::string_view(argv[1]) == "--native") {
			RunNativeProtocol();

			return 0;
		}
		Require(argc == 1, "unsupported font face proof argument");
		TestPinnedViewsAndFreezeOrder();
		TestCollectionAdmissionAndClampedOffsets();
		TestOriginalLookupBranchesAndWorkBound();
		std::cout << "PASS 3 blob font face groups\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
