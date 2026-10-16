#include "sgre/scene/motion_stencil_mask_owner.hpp"

#include <bit>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

template <typename Value>
static sgre::resource::PsbValue Atom(Value value) {
	sgre::resource::PsbValue atom;
	atom.Data = std::move(value);

	return atom;
}

class Reader {
public:
	explicit Reader(std::istream& stream) : m_Stream(stream) {
	}
	std::uint8_t Byte() {
		const auto value = m_Stream.get();
		Require(value != std::char_traits<char>::eof(), "short native wrv fixture");

		return static_cast<std::uint8_t>(value);
	}
	std::uint32_t Word() {
		std::uint32_t value = 0;
		for (std::uint32_t shift = 0; shift < 32; shift += 8) {
			value |= static_cast<std::uint32_t>(Byte()) << shift;
		}

		return value;
	}
	std::uint64_t WideWord() {
		const auto low = Word();
		const auto high = Word();

		return static_cast<std::uint64_t>(low) | (static_cast<std::uint64_t>(high) << 32);
	}

private:
	std::istream& m_Stream;
};

static sgre::resource::PsbValue Value(std::uint32_t tag, std::uint64_t bits) {
	sgre::resource::PsbValue value;
	Require(tag <= 255, "native scalar tag exceeds byte domain");
	value.SerializedTag = static_cast<std::uint8_t>(tag);
	if (tag == 2 || tag == 3) {
		value.Data = tag == 2;
	} else if (tag >= 4 && tag <= 12) {
		value.Data = std::bit_cast<std::int64_t>(bits);
	} else if (tag == 0x1d) {
		value.Data = 0.0;
	} else if (tag == 0x1e) {
		Require(bits <= 0xffffffffu, "native float32 fixture bits exceed field width");
		value.Data = static_cast<double>(std::bit_cast<float>(static_cast<std::uint32_t>(bits)));
	} else if (tag == 0x1f) {
		value.Data = std::bit_cast<double>(bits);
	} else {
		throw std::runtime_error("unsupported native wrv fixture tag");
	}

	return value;
}

static sgre::resource::MotionLayer Layer(const sgre::resource::PsbValue& wrv) {
	sgre::resource::MotionLayer layer;
	layer.Label = "stencil";
	layer.Type = 12;
	layer.TransformOrder = {0, 3, 2, 1};
	layer.Properties.emplace_back("stencilType", Atom(std::int64_t{5}));
	sgre::resource::MotionFrame current;
	current.Type = 2;
	const sgre::resource::PsbObject stc = {{"wrv", wrv}, {"wcc", Atom(std::monostate{})}, {"wrt", Atom(0.25)}, {"wsf", Atom(0.5)}};
	current.Content = Atom(sgre::resource::PsbObject{{"mask", Atom(std::int64_t{0x20000000})}, {"src", Atom(std::string("c"))}, {"icon", Atom(std::string("i"))}, {"stc", Atom(stc)}});
	layer.Frames.push_back(current);
	sgre::resource::MotionFrame terminal;
	terminal.Time = 4;
	layer.Frames.push_back(terminal);

	return layer;
}

static void Compare(const sgre::resource::PsbValue& wrv, bool expected) {
	const auto layer = Layer(wrv);
	const sgre::resource::MotionResource resource;
	const auto prepared = sgre::scene::PrepareMotionLayer(layer, resource);
	sgre::scene::MotionStencilMaskOwner owner(layer, prepared);
	owner.PrepareFrame(0, 0);
	owner.PrepareFrame(1, 1);
	Require(owner.Control(0).Enabled && owner.Control(0).ReverseWrite == expected, "native full-value wrv control conversion differs");
	Require(owner.Evaluate(resource, 0, 0, true, true), "genuine initial selected stencil control must evaluate");
	Require(owner.Summary().Enabled && owner.Summary().ReverseWrite == expected, "selected stencil summary must retain its actual normalized reversal flag");
}

static void TestBoundaries() {
	Compare(Atom(std::int64_t{256}), true);
	Compare(Atom(std::int64_t{-256}), true);
	Compare(Atom(std::int64_t{4294967296}), true);
	Compare(Atom(std::int64_t{0}), false);
	Compare(Atom(1e-300), true);
	Compare(Atom(-1e-300), true);
	Compare(Atom(-0.0), false);
	const auto layer = Layer(Atom(std::string("invalid")));
	const sgre::resource::MotionResource resource;
	const auto prepared = sgre::scene::PrepareMotionLayer(layer, resource);
	try {
		sgre::scene::MotionStencilMaskOwner rejected(layer, prepared);
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find("boolean/numeric wrv") != std::string_view::npos, error.what());

		return;
	}

	throw std::runtime_error("unsupported stencil boolean atom was accepted");
}

static void Replay(const char* path) {
	std::ifstream stream(path, std::ios::binary);
	Require(stream.is_open(), "cannot read native wrv fixture");
	Reader reader(stream);
	Require(reader.Byte() == 'M' && reader.Byte() == 'W' && reader.Byte() == 'B' && reader.Byte() == '1', "wrong native wrv fixture magic");
	const auto count = reader.Word();
	Require(count > 0 && count <= 256, "native wrv fixture count exceeds bound");
	for (std::uint32_t index = 0; index < count; ++index) {
		const auto tag = reader.Word();
		const auto bits = reader.WideWord();
		const auto accessor = reader.Word();
		const auto decoder = reader.Word();
		Require(accessor <= 1 && decoder == accessor, "native wrv fixture accessor and control decoder disagree");
		try {
			Compare(Value(tag, bits), accessor != 0);
		} catch (const std::exception& error) {
			throw std::runtime_error("native wrv vector " + std::to_string(index) + ": " + error.what());
		}
	}
	Require(stream.peek() == std::char_traits<char>::eof(), "unexpected trailing native wrv fixture bytes");
	std::cout << count << " original boolean/numeric accessor and complete wrv control decoders matched\n";
}

int main(int argc, char** argv) {
	try {
		Require(argc == 1 || argc == 2, "usage: stencil_boolean_tests [native fixture]");
		TestBoundaries();
		if (argc == 2) {
			Replay(argv[1]);
		}
		std::cout << "full integer and original floating stencil boolean boundaries PASS\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
