#include "sgre/platform/host_locale.hpp"

#import <Foundation/Foundation.h>

namespace sgre::platform {

	HostLocale ReadHostLocale() {
		@autoreleasepool {
			NSLocale* locale = [NSLocale currentLocale];
			const char* language = [locale.languageCode UTF8String];
			const char* country = [locale.countryCode UTF8String];
			HostLocale result;
			if (language) {
				result.Language = language;
			}
			if (country) {
				result.Country = country;
			}

			return result;
		}
	}

} // namespace sgre::platform
