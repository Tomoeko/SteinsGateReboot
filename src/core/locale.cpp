#include "sgre/core/locale.hpp"

#include <array>

namespace sgre::core {

	struct OriginalLocaleRecord {
		std::uint32_t Identifier;
		std::string_view Language;
		std::string_view Country;
	};

	// Original 0x1408f7b20: 156 ordered LCID/language/country records, selected by 0x1405ab4b0.
	static constexpr std::array<OriginalLocaleRecord, 156> kOriginalLocaleCatalog{{
	    {0x00000436u, "af", "ZA"},
	    {0x0000041cu, "sq", "AL"},
	    {0x00001401u, "ar", "DZ"},
	    {0x00003c01u, "ar", "BH"},
	    {0x00000c01u, "ar", "EG"},
	    {0x00000801u, "ar", "IQ"},
	    {0x00002c01u, "ar", "JO"},
	    {0x00003401u, "ar", "KW"},
	    {0x00003001u, "ar", "LB"},
	    {0x00001001u, "ar", "LY"},
	    {0x00001801u, "ar", "MA"},
	    {0x00002001u, "ar", "OM"},
	    {0x00004001u, "ar", "QA"},
	    {0x00000401u, "ar", "SA"},
	    {0x00002801u, "ar", "SY"},
	    {0x00001c01u, "ar", "TN"},
	    {0x00003801u, "ar", "AE"},
	    {0x00002401u, "ar", "YE"},
	    {0x0000042bu, "hy", "AM"},
	    {0x0000042du, "eu", "ES"},
	    {0x00000423u, "be", "BY"},
	    {0x00000445u, "bn", "IN"},
	    {0x00000402u, "bg", "BG"},
	    {0x00000403u, "ca", "ES"},
	    {0x00000c04u, "zh", "HK"},
	    {0x00001404u, "zh", "MO"},
	    {0x00000804u, "zh", "CN"},
	    {0x00000004u, "zh", "TW"},
	    {0x00001004u, "zh", "SG"},
	    {0x00000404u, "zh", "TW"},
	    {0x00007c04u, "zh", "TW"},
	    {0x0000101au, "hr", "BA"},
	    {0x0000041au, "hr", "HR"},
	    {0x00000405u, "cs", "CZ"},
	    {0x00000406u, "da", "DK"},
	    {0x00000813u, "nl", "BE"},
	    {0x00000413u, "nl", "NL"},
	    {0x00000c09u, "en", "AU"},
	    {0x00002809u, "en", "BZ"},
	    {0x00001009u, "en", "CA"},
	    {0x00002409u, "en", "CB"},
	    {0x00001809u, "en", "IE"},
	    {0x00002009u, "en", "JM"},
	    {0x00001409u, "en", "NZ"},
	    {0x00003409u, "en", "PH"},
	    {0x00001c09u, "en", "ZA"},
	    {0x00002c09u, "en", "TT"},
	    {0x00000809u, "en", "GB"},
	    {0x00000409u, "en", "US"},
	    {0x00003009u, "en", "ZW"},
	    {0x00000425u, "et", "EE"},
	    {0x00000438u, "fo", "FO"},
	    {0x0000040bu, "fi", "FI"},
	    {0x0000080cu, "fr", "BE"},
	    {0x00000c0cu, "fr", "CA"},
	    {0x0000040cu, "fr", "FR"},
	    {0x0000140cu, "fr", "LU"},
	    {0x0000180cu, "fr", "MC"},
	    {0x0000100cu, "fr", "CH"},
	    {0x00000456u, "gl", "ES"},
	    {0x00000437u, "ka", "GE"},
	    {0x00000c07u, "de", "AT"},
	    {0x00000407u, "de", "DE"},
	    {0x00001407u, "de", "LI"},
	    {0x00001007u, "de", "LU"},
	    {0x00000807u, "de", "CH"},
	    {0x00000408u, "el", "GR"},
	    {0x00000447u, "gu", "IN"},
	    {0x0000040du, "he", "IL"},
	    {0x00000439u, "hi", "IN"},
	    {0x0000040eu, "hu", "HU"},
	    {0x0000040fu, "is", "IS"},
	    {0x00000421u, "id", "ID"},
	    {0x00000410u, "it", "IT"},
	    {0x00000810u, "it", "CH"},
	    {0x00000411u, "ja", "JP"},
	    {0x0000044bu, "kn", "IN"},
	    {0x0000043fu, "kk", "KZ"},
	    {0x00000441u, "sw", "KE"},
	    {0x00000412u, "ko", "KR"},
	    {0x00000440u, "ky", "KG"},
	    {0x00000426u, "lv", "LV"},
	    {0x00000427u, "lt", "LT"},
	    {0x0000042fu, "mk", "MK"},
	    {0x0000083eu, "ms", "BN"},
	    {0x0000043eu, "ms", "MY"},
	    {0x0000044cu, "ml", "IN"},
	    {0x0000043au, "mt", "MT"},
	    {0x00000481u, "mi", "NZ"},
	    {0x0000044eu, "mr", "IN"},
	    {0x00000450u, "mn", "MN"},
	    {0x0000046cu, "ns", "ZA"},
	    {0x00000414u, "nb", "NO"},
	    {0x00000814u, "nn", "NO"},
	    {0x00000429u, "fa", "IR"},
	    {0x00000415u, "pl", "PL"},
	    {0x00000416u, "pt", "BR"},
	    {0x00000816u, "pt", "PT"},
	    {0x00000446u, "pa", "IN"},
	    {0x0000046bu, "qu", "BO"},
	    {0x0000086bu, "qu", "EC"},
	    {0x00000c6bu, "qu", "PE"},
	    {0x00000418u, "ro", "RO"},
	    {0x00000419u, "ru", "RU"},
	    {0x0000243bu, "sm", "FI"},
	    {0x0000103bu, "sm", "NO"},
	    {0x0000143bu, "sm", "SE"},
	    {0x00000c3bu, "se", "FI"},
	    {0x0000043bu, "se", "NO"},
	    {0x0000083bu, "se", "SE"},
	    {0x0000203bu, "sm", "FI"},
	    {0x0000183bu, "sm", "NO"},
	    {0x00001c3bu, "sm", "SE"},
	    {0x0000044fu, "sa", "IN"},
	    {0x00001c1au, "sr", "BA"},
	    {0x00000c1au, "sr", "SP"},
	    {0x0000181au, "sr", "BA"},
	    {0x0000081au, "sr", "SP"},
	    {0x0000041bu, "sk", "SK"},
	    {0x00000424u, "sl", "SI"},
	    {0x00002c0au, "es", "AR"},
	    {0x0000400au, "es", "BO"},
	    {0x0000340au, "es", "CL"},
	    {0x0000240au, "es", "CO"},
	    {0x0000140au, "es", "CR"},
	    {0x00001c0au, "es", "DO"},
	    {0x0000300au, "es", "EC"},
	    {0x0000440au, "es", "SV"},
	    {0x0000100au, "es", "GT"},
	    {0x0000480au, "es", "HN"},
	    {0x0000080au, "es", "MX"},
	    {0x00004c0au, "es", "NI"},
	    {0x0000180au, "es", "PA"},
	    {0x00003c0au, "es", "PY"},
	    {0x0000280au, "es", "PE"},
	    {0x0000500au, "es", "PR"},
	    {0x00000c0au, "es", "ES"},
	    {0x0000380au, "es", "UY"},
	    {0x0000200au, "es", "VE"},
	    {0x0000081du, "sv", "FI"},
	    {0x0000041du, "sv", "SE"},
	    {0x0000045au, "sy", "SY"},
	    {0x00000449u, "ta", "IN"},
	    {0x00000444u, "tt", "RU"},
	    {0x0000044au, "te", "IN"},
	    {0x0000041eu, "th", "TH"},
	    {0x00000432u, "tn", "ZA"},
	    {0x0000041fu, "tr", "TR"},
	    {0x00000422u, "uk", "UA"},
	    {0x00000420u, "ur", "PK"},
	    {0x00000843u, "uz", "UZ"},
	    {0x00000443u, "uz", "UZ"},
	    {0x0000042au, "vi", "VN"},
	    {0x00000452u, "cy", "GB"},
	    {0x00000434u, "xh", "ZA"},
	    {0x00000435u, "zu", "ZA"},
	}};

	OriginalLocale ResolveOriginalWindowsLocale(std::uint32_t locale_identifier) {
		for (const auto& record : kOriginalLocaleCatalog) {
			if (record.Identifier == locale_identifier) {
				return {record.Language, record.Country};
			}
		}

		return {"ja", "JP"};
	}

	OriginalLocale ResolveOriginalHostLocale(std::string_view language, std::string_view country) {
		for (const auto& record : kOriginalLocaleCatalog) {
			if (record.Language == language && record.Country == country) {
				return {record.Language, record.Country};
			}
		}

		return {"ja", "JP"};
	}

	std::uint32_t PackOriginalLocalePrefix(std::string_view code) {
		if (code.size() < 2) {
			return 0;
		}

		return (static_cast<std::uint32_t>(static_cast<std::uint8_t>(code[0])) << 8) + static_cast<std::uint8_t>(code[1]);
	}

} // namespace sgre::core
