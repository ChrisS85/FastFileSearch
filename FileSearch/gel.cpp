#include "stxutif.h"
#include <codecvt>

#ifdef __MINGW32_VERSION
#ifndef _GLIBCXX_USE_WCHAR_T

namespace std
{
	//instantiate the codecvt facet id for the required types
	locale::id codecvt<wchar_t,char,mbstate_t>::id;
}

#endif //_GLIBCXX_USE_WCHAR_T
#endif //__MINGW32_VERSION


namespace gel
{namespace stdx
{
#if 0
	//instantiate the global locale
	std::locale utf8_locale(std::locale(), new std::codecvt_utf8<wchar_t>()); // VS2010 and further

	// convert wstring to UTF-8 string
	std::string wstring_to_utf8 (const std::wstring& str) {
		std::wstring_convert<std::codecvt_utf8<wchar_t>> myconv;
		return myconv.to_bytes(str);
	}
#else

	std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>, wchar_t> conversion;
	std::string wstring_to_utf8(const std::wstring& str) {
		return conversion.to_bytes(str);
	}

#endif

}}
