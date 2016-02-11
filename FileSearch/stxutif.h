#ifndef _UTIF_H_
#define _UTIF_H_ 1

#include <stdexcept>
#include <locale>
#include <iosfwd>
#include <algorithm>

#ifdef __MINGW32_VERSION
#ifndef _GLIBCXX_USE_WCHAR_T

namespace std
{
	/// declare a coecvt between wchar_t and char
	/*!
	 * If Mingw support for wchar_t is not activated, the following declarations are
	 * required to properly let the code to be translatable
	 */
	template<>
	class codecvt<wchar_t,char,mbstate_t>:
		public __codecvt_abstract_base<wchar_t,char,mbstate_t>
	{
	protected:
		explicit codecvt(size_t refs=0)
			:__codecvt_abstract_base<wchar_t,char,mbstate_t>(refs)
		{}
	public:
		static locale::id id;
	};

	// wide types for char dependent templates
	typedef basic_ios<wchar_t> 				wios;
	typedef basic_streambuf<wchar_t> 		wstreambuf;
	typedef basic_istream<wchar_t> 			wistream;
	typedef basic_ostream<wchar_t> 			wostream;
	typedef basic_iostream<wchar_t> 		wiostream;
	typedef basic_stringbuf<wchar_t> 		wstringbuf;
	typedef basic_istringstream<wchar_t>	wistringstream;
	typedef basic_ostringstream<wchar_t>	wostringstream;
	typedef basic_stringstream<wchar_t> 	wstringstream;
	typedef basic_filebuf<wchar_t> 			wfilebuf;
	typedef basic_ifstream<wchar_t> 		wifstream;
	typedef basic_ofstream<wchar_t> 		wofstream;
	typedef basic_fstream<wchar_t> 			wfstream;
}

#endif //_GLIBCXX_USE_WCHAR_T
#endif //__MINGW32_VERSION

namespace gel
{namespace stdx
{
	extern std::locale utf8_locale; ///< global locale with UTF-8 conversion capabilities.

	std::string wstring_to_utf8 (const std::wstring& str);
}}

#endif
