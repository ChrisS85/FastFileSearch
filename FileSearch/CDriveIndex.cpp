/**********************************************************************************
Module name: CDriveIndex.cpp
Written by: Christian Sander
Credits for original code this is based on: Jeffrey Cooperstein & Jeffrey Richter
**********************************************************************************/

#include "stdafx.h"
#include "CDriveIndex.h"
#include <algorithm>
#include <memory>
#include <hash_map>
#include <iostream>
#include <fstream>
#include <ctime>

#include "lz4.h"

#include "stxutif.h"
using namespace gel;

using namespace tinyxml2;

namespace {
template <typename T>
T swap_endian(T u)
{
    union
    {
        T u;
        unsigned char u8[sizeof(T)];
    } source, dest;

    source.u = u;

    for (size_t k = 0; k < sizeof(T); k++)
        dest.u8[k] = source.u8[sizeof(T) - k - 1];

    return dest.u;
}
};


// Exported function to create the index of a drive
CDriveIndex* _stdcall CreateIndex(WCHAR cDrive)
{
	CDriveIndex *di = new CDriveIndex();
	di->Init(cDrive);
	di->PopulateIndex();
	return di;
}



// Exported function to delete the index of a drive
void _stdcall DeleteIndex(CDriveIndex *di)
{
	if(dynamic_cast<CDriveIndex*>(di))
		delete di;
}

void CDriveIndex::attach(vector<XMLHandle> &dirHandles, unordered_map<DWORDLONG, vector<XMLHandle>::size_type> &umDirFrnToHandle, int NodeType, DWORDLONG Index) const
{
	if (umDirFrnToHandle.find(Index) != umDirFrnToHandle.end()) {  // already processed?
		return;
	}

	USNEntry file = FRNToName(Index);
	if (file.ParentIndex == 0) {  // No upper directory
		return;
	}
	if(!(file.ParentIndex == 0 && file.Name.length() == 2 && file.Name[1] == wchar_t(L':'))) // to exclude drive itself
	{
		SearchResultFile srf;
		srf.Filename = file.Name;
		srf.Path.reserve(MAX_PATH);
		// Obtain path into buffer, and split it
		Get(Index, &srf.Path);
		if(srf.Path.length() < 3) {
			return;
		}

		WCHAR szDrive[_MAX_DRIVE];
		WCHAR szPath[_MAX_PATH];
		WCHAR szName[_MAX_FNAME];
		WCHAR szExt[_MAX_EXT];
		_wsplitpath(srf.Path.c_str(), szDrive, szPath, szName, szExt);

		// Skip metadata files and other special directories
		int compareCount = sizeof(L"$RECYCLE.BIN") / sizeof(WCHAR) - 1;
		if (*szDrive == wchar_t(0) || srf.Path.compare(3, compareCount, L"$RECYCLE.BIN", compareCount) == 0) {
			return;
		}

		unordered_map<DWORDLONG, vector<XMLHandle>::size_type>::const_iterator it(umDirFrnToHandle.find(file.ParentIndex));
		if (it == umDirFrnToHandle.end()) {
			attach(dirHandles, umDirFrnToHandle, IN_DIRECTORIES, file.ParentIndex);
		}
		it = umDirFrnToHandle.find(file.ParentIndex);
		if (it == umDirFrnToHandle.end()) {
			return;  // unexpected
		}
		XMLHandle parentHandle(dirHandles[it->second]);
		
		XMLNode * parent = parentHandle.ToNode();
		if (!parent) {
			return;
		}
		
		XMLElement * element = parent->GetDocument()->NewElement( (NodeType == IN_DIRECTORIES) ? "Directory" : "File" );
		element->SetAttribute("Name", stdx::wstring_to_utf8(file.Name).c_str());

		if (NodeType == IN_DIRECTORIES) {
			vector<XMLHandle>::iterator insertedIt(dirHandles.insert(dirHandles.end(), XMLHandle(element)));
			umDirFrnToHandle[Index] = insertedIt - dirHandles.begin();
		}
		else {
			HANDLE hFile = CreateFile( srf.Path.c_str(), 0,
									FILE_SHARE_READ, 0, OPEN_EXISTING,
									FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, 0 );
			if (hFile != INVALID_HANDLE_VALUE) {
				BY_HANDLE_FILE_INFORMATION FileInfo = {0};
				GetFileInformationByHandle(hFile, &FileInfo);
				if (!(FileInfo.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
					ULARGE_INTEGER filesize = {FileInfo.nFileSizeLow, FileInfo.nFileSizeHigh};
					element->SetAttribute("Size", std::to_string(DWORDLONG(filesize.QuadPart)).c_str());;
				}
				CloseHandle(hFile);
			}
		}
		
		parent->InsertEndChild(element);
	}
}


// Exports database to an exchange format.
// format is ignored for now, pass 0 for the default format.
// returns number of processed entries and 0 if an error occured.
BOOL CDriveIndex::ExportToFileListing(wstring &strPath, int format) const
{
	if (format < ExportFormat::ExportFormatAdcXml || format > ExportFormat::ExportFormatAdcXml_LZ4) {
		return FALSE;
	}
	vector<SearchResultFile> results;
	XMLDocument doc;
	XMLPrinter printer;
	__time64_t tm;
	CHAR tmbuf[200];

	_time64(&tm);
	strftime(tmbuf, sizeof(tmbuf) / sizeof(tmbuf[0]), "%Y-%m-%dT%H:%M:%SZ", _gmtime64(&tm));

	stringstream emptyxml;
	emptyxml <<
		"<?xml version=\"1.0\" encoding=\"utf-8\" standalone=\"yes\"?>\n"
		"<FileListing Version=\"1\" CID=\"00FASTFILESEARCH00CDRIVEINDEXCLASSAPI00"
		"\" Base=\"" << stdx::wstring_to_utf8(wstring(&m_cDrive,(&m_cDrive)+1)) << ":" <<
		"\" Generator=\"FastFileSearch CDriveIndex C++ API"
		"\" GeneratedDate=\"" << tmbuf << "\"\n"
		" xmlns:adc=\"http://adc.sourceforge.net/ADC.html\"></FileListing>";

	XMLError err = doc.Parse(emptyxml.str().c_str());
	if (err != XML_SUCCESS) {
		return false;
	}
	XMLHandle docHandle( &doc );

	vector<XMLHandle> dirHandles;
	dirHandles.reserve(rgDirectories.size());
	unordered_map<DWORDLONG, vector<XMLHandle>::size_type> umDirFrnToHandle;

	vector<XMLHandle>::iterator insertedIt(dirHandles.insert(dirHandles.end(), XMLHandle(docHandle.FirstChildElement())));
	umDirFrnToHandle[m_dwDriveFRN] = insertedIt - dirHandles.begin();

	for(unsigned int j = 0; j != rgDirectories.size(); j++) {
		const IndexedDirectory* i = &rgDirectories[j];
		attach(dirHandles, umDirFrnToHandle, IN_DIRECTORIES, i->Index);
	}
	for(unsigned int j = 0; j != rgFiles.size(); j++) {
		const IndexedFile* i = &rgFiles[j];
		attach(dirHandles, umDirFrnToHandle, IN_FILES, i->Index);
	}
	doc.Print( &printer );

	std::ofstream file;  // closes at end of scope
	file.open(strPath.c_str(), ios::out|ios::binary|ios::trunc);
	if (file.is_open())
	{
		if (format == ExportFormat::ExportFormatAdcXml) {
			file.write(printer.CStr(), printer.CStrSize() - 1);
			return file.good();
		}
		else if (format == ExportFormat::ExportFormatAdcXml_LZ4) {
			int const uncompressed_size = printer.CStrSize() - 1;
			int max_compressed_size = LZ4_compressBound( uncompressed_size );
			std::unique_ptr<char[]> compressed( new char[max_compressed_size] );
			int const compressed_size = LZ4_compress( printer.CStr(), compressed.get(), uncompressed_size );
			int const final_compressed_size = ( compressed_size >= uncompressed_size ) ? uncompressed_size : compressed_size;

#if defined(BIG_ENDIAN)
				int value;
				value = swap_endian(uncompressed_size); file.write(reinterpret_cast<const char*>(&value), sizeof(int));
				value = swap_endian(final_compressed_size); file.write(reinterpret_cast<const char*>(&value), sizeof(int));
#else
				file.write(reinterpret_cast<const char*>(&uncompressed_size), sizeof(int));
				file.write(reinterpret_cast<const char*>(&final_compressed_size), sizeof(int));
#endif
				file.write(( compressed_size >= uncompressed_size ) ? printer.CStr() : compressed.get(), final_compressed_size);

				return true;
		}
	}

	
	return false;
}


// Exported function to search in the index of a drive.
// Returns a string that contains the filepaths of the results,
// separated by newlines for easier processing in non-C++ languages.
// nResults is -1 if more results than the limit were found and 0 if an error occured. In this case the return value is NULL.
WCHAR* _stdcall Search(CDriveIndex *di, WCHAR *szQuery, WCHAR *szPath, BOOL bSort, BOOL bEnhancedSearch, int maxResults, int *nResults)
{
	if(dynamic_cast<CDriveIndex*>(di) && szQuery)
	{
		vector<SearchResultFile> results;
		wstring result;
		int numResults = di->Find(&wstring(szQuery), szPath != NULL ? &wstring(szPath) : NULL, &results, bSort, bEnhancedSearch, maxResults);
		if(nResults != NULL)
			*nResults = numResults;
		for(unsigned int i = 0; i != results.size(); i++)
			result += (i == 0 ? TEXT("") : TEXT("\n")) + results[i].Path + results[i].Filename;
		WCHAR * szOutput = new WCHAR[result.length() + 1];
		ZeroMemory(szOutput, (result.length() + 1) * sizeof(szOutput[0]));
		_snwprintf(szOutput, result.length(), TEXT("%s"), result.c_str());
		return szOutput;
	}
	if(nResults != NULL)
		*nResults = 0;
	return NULL;
}



// Exported function to clear the memory of the string returned by Search().
// This needs to be called after every call to Search to avoid memory leaks.
void _stdcall FreeResultsBuffer(WCHAR *szResults)
{
	if(szResults)
		delete[] szResults;
}



// Exported function that loads the database from disk
CDriveIndex* _stdcall LoadIndexFromDisk(WCHAR *szPath)
{
	if(szPath)
		return new CDriveIndex(wstring(szPath));
	return NULL;
}



// Exported function that saves the database to disk
BOOL _stdcall SaveIndexToDisk(CDriveIndex *di, WCHAR *szPath)
{
	if(dynamic_cast<CDriveIndex*>(di) && szPath)
		return di->SaveToDisk(wstring(szPath));
	return false;
}


// Exported function that returns the number of files and directories
void _stdcall GetDriveInfo(CDriveIndex *di, DriveInfo *driveInfo)
{
	if(dynamic_cast<CDriveIndex*>(di))
		*driveInfo = di->GetInfo();
}



BOOL _stdcall ExportIndex(CDriveIndex *di, WCHAR *szPath, int format)
{
	if(dynamic_cast<CDriveIndex*>(di) && szPath)
		return di->ExportToFileListing(wstring(szPath), format);
	return false;
}

// Constructor
CDriveIndex::CDriveIndex()
{
	// Initialize member variables
	m_hVol = INVALID_HANDLE_VALUE;
}



// Destructor
CDriveIndex::~CDriveIndex()
{
	CleanUp();
}



// Cleanup function to free resources
void CDriveIndex::CleanUp()
{
	// Cleanup the memory and handles we were using
	if (m_hVol != INVALID_HANDLE_VALUE) 
		CloseHandle(m_hVol);
}



// This is a helper function that opens a handle to the volume specified
// by the cDriveLetter parameter.
HANDLE CDriveIndex::Open(TCHAR cDriveLetter, DWORD dwAccess)
{
	TCHAR szVolumePath[_MAX_PATH];
	wsprintf(szVolumePath, TEXT("\\\\.\\%c:"), cDriveLetter);
	HANDLE hCJ = CreateFile(szVolumePath, dwAccess, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
	return(hCJ);
}


// This function creates a journal on the volume. If a journal already
// exists this function will adjust the MaximumSize and AllocationDelta
// parameters of the journal
BOOL CDriveIndex::Create(DWORDLONG MaximumSize, DWORDLONG AllocationDelta)
{
	DWORD cb;
	CREATE_USN_JOURNAL_DATA cujd;
	cujd.MaximumSize = MaximumSize;
	cujd.AllocationDelta = AllocationDelta;
	BOOL fOk = DeviceIoControl(m_hVol, FSCTL_CREATE_USN_JOURNAL, 
		&cujd, sizeof(cujd), NULL, 0, &cb, NULL);
	return(fOk);
}

// Return statistics about the journal on the current volume
BOOL CDriveIndex::Query(PUSN_JOURNAL_DATA pUsnJournalData) const
{
	DWORD cb;
	BOOL fOk = DeviceIoControl(m_hVol, FSCTL_QUERY_USN_JOURNAL, NULL, 0, 
		pUsnJournalData, sizeof(*pUsnJournalData), &cb, NULL);
	return(fOk);
}

// Call this to initialize the structure. The cDrive parameter
// specifies the drive that this instance will access. The cbBuffer
// parameter specifies the size of the interal buffer used to read records
// from the journal. This should be large enough to hold several records
// (for example, 10 kilobytes will allow this class to buffer several
// dozen journal records at a time)
BOOL CDriveIndex::Init(WCHAR cDrive)
{
	// You should not call this function twice for one instance.
	if (m_hVol != INVALID_HANDLE_VALUE)
		DebugBreak();
	m_cDrive = cDrive;
	ClearLastResult();
	BOOL fOk = FALSE;
	__try {
		// Open a handle to the volume
		m_hVol = Open(m_cDrive, GENERIC_WRITE | GENERIC_READ);
		if (INVALID_HANDLE_VALUE == m_hVol)
			__leave;
		fOk = TRUE;
	}
	__finally {
		if (!fOk)
			CleanUp();
	}
	return(fOk);
}

void CDriveIndex::ClearLastResult()
{
	LastResult = SearchResult();
}

// Adds a file to the database
BOOL CDriveIndex::Add(DWORDLONG Index, wstring *szName, DWORDLONG ParentIndex, DWORDLONG Filter)
{
	IndexedFile i;
	i.Index = Index;
	if(!Filter)
		Filter = MakeFilter(szName);
	i.Filter = Filter;
	rgFiles.insert(rgFiles.end(), i);
	return(TRUE);
}



// Adds a directory to the database
BOOL CDriveIndex::AddDir(DWORDLONG Index, wstring *szName, DWORDLONG ParentIndex, DWORDLONG Filter)
{
	IndexedDirectory i;
	i.Index = Index;
	if(!Filter)
		Filter = MakeFilter(szName);
	i.Filter = Filter;
	i.nFiles = 0;
	rgDirectories.insert(rgDirectories.end(), i);
	return(TRUE);
}



// Calculates a 64bit value that is used to filter out many files before comparing their filenames
// This method gives a huge speed boost.
DWORDLONG CDriveIndex::MakeFilter(wstring *szName)
{
	/*
	Creates an address that is used to filter out strings that don't contain the queried characters
	Explanation of the meaning of the single bits:
	0-25 a-z
	26-35 0-9
	36 .
	37 space
	38 !#$&'()+,-~_
	39 2 same characters
	40 3 same characters
	The fields below indicate the presence of 2-character sequences. Based off http://en.wikipedia.org/wiki/Letter_frequency
	41 TH
	42 HE
	43 AN
	44 RE
	45 ER
	46 IN
	47 ON
	48 AT
	49 ND
	50 ST
	51 ES
	52 EN
	53 OF
	54 TE
	55 ED
	56 OR
	57 TI
	58 HI
	59 AS
	60 TO
	61-63 length (max. 8 characters. Queries are usually shorter than this)
	*/
	if(!(szName->length() > 0))
		return 0;
	DWORDLONG Address = 0;
	WCHAR c;
	wstring szlower(*szName);
	transform(szlower.begin(), szlower.end(), szlower.begin(), tolower);
	int counts[26] = {0}; //This array is used to check if characters occur two or three times in the string
	wstring::size_type l = szlower.length();
	for(unsigned int i = 0; i != l; i++)
	{
		c = szlower[i];
		if(c > 96 && c < 123) //a-z
		{
			Address |= 1ui64 << (DWORDLONG)((DWORDLONG)c - 97ui64);
			counts[c-97]++;
			if(i < l - 1)
			{
				if(c == L't' && szlower[i+1] == L'h') //th
					Address |= 1ui64 << 41;
				else if(c == L'h' && szlower[i+1] == L'e') //he
					Address |= 1ui64 << 41;
				else if(c == L'a' && szlower[i+1] == L'n') //an
					Address |= 1ui64 << 41;
				else if(c == L'r' && szlower[i+1] == L'e') //re
					Address |= 1ui64 << 41;
				else if(c == L'e' && szlower[i+1] == L'r') //er
					Address |= 1ui64 << 41;
				else if(c == L'i' && szlower[i+1] == L'n') //in
					Address |= 1ui64 << 41;
				else if(c == L'o' && szlower[i+1] == L'n') //on
					Address |= 1ui64 << 41;
				else if(c == L'a' && szlower[i+1] == L't') //at
					Address |= 1ui64 << 41;
				else if(c == L'n' && szlower[i+1] == L'd') //nd
					Address |= 1ui64 << 41;
				else if(c == L's' && szlower[i+1] == L't') //st
					Address |= 1ui64 << 41;
				else if(c == L'e' && szlower[i+1] == L's') //es
					Address |= 1ui64 << 41;
				else if(c == L'e' && szlower[i+1] == L'n') //en
					Address |= 1ui64 << 41;
				else if(c == L'o' && szlower[i+1] == L'f') //of
					Address |= 1ui64 << 41;
				else if(c == L't' && szlower[i+1] == L'e') //te
					Address |= 1ui64 << 41;
				else if(c == L'e' && szlower[i+1] == L'd') //ed
					Address |= 1ui64 << 41;
				else if(c == L'o' && szlower[i+1] == L'r') //or
					Address |= 1ui64 << 41;
				else if(c == L't' && szlower[i+1] == L'i') //ti
					Address |= 1ui64 << 41;
				else if(c == L'h' && szlower[i+1] == L'i') //hi
					Address |= 1ui64 << 41;
				else if(c == L'a' && szlower[i+1] == L's') //as
					Address |= 1ui64 << 41;
				else if(c == L't' && szlower[i+1] == L'o') //to
					Address |= 1ui64 << 41;
			}
		}
		else if(c >= L'0' && c <= '9') //0-9
			Address |= 1ui64 << (c - L'0' + 26ui64);
		else if(c == L'.') //.
			Address |= 1ui64 << 36;
		else if(c == L' ') // space
			Address |= 1ui64 << 37;
		else if(c == L'!' || c == L'#' || c == L'$' || c == L'&' || c == L'\'' || c == L'(' || c == L')' || c == L'+' || c == L',' || c == L'-' || c == L'~' || c == L'_')
			Address |= 1ui64 << 38; // !#$&'()+,-~_
	}
	for(unsigned int i = 0; i != 26; i++)
	{
		if(counts[i] == 2)
			Address |= 1ui64 << 39;
		else if(counts[i] > 2)
			Address |= 1ui64 << 40;
	}
	DWORDLONG length = (szlower.length() > 7 ? 7ui64 : (DWORDLONG)szlower.length()) & 0x00000007ui64; //3 bits for length -> 8 max
	Address |= length << 61ui64;
	return Address;
}



// Internal function for searching in the database.
// For projects in C++ which use this project it might be preferable to use this function
// to skip the wrapper.
// Returns: number of results, -1 if maxResults != -1 and not all results were found
int CDriveIndex::Find(wstring *strQuery, wstring *strQueryPath, vector<SearchResultFile> *rgsrfResults, BOOL bSort, BOOL bEnhancedSearch, int maxResults)
{
	//These variables are used to control the flow of execution in this function.

	//Indicates where results should be searched
	unsigned int SearchWhere = IN_FILES;
	//Offset for vector marked by SearchWhere
	unsigned int iOffset = 0;
	//Used to skip the search when the previous two properties should be carried over to the next search without actually using them now.
	BOOL bSkipSearch = false;

	//Number of results in this search. -1 if more than maximum number of results.
	int nResults = 0;

	//No query, just ignore this call
	if(strQuery->length() == 0)
	{
		// Store this query
		LastResult.Query = wstring(TEXT(""));
		LastResult.Results = vector<SearchResultFile>();
		return nResults;
	}

	if(strQueryPath != NULL)
	{
		//Check if the path actually matches the drive of this index
		WCHAR szDrive[_MAX_DRIVE];
		_wsplitpath(strQueryPath->c_str(), szDrive, NULL, NULL, NULL);
		for(unsigned int j = 0; j != _MAX_DRIVE; j++)
			szDrive[j] = toupper(szDrive[j]);
		if(wstring(szDrive).compare(wstring(1,toupper(m_cDrive))) == 0)
			return 0;
	}

	//Create lower query string for case-insensitive search
	wstring strQueryLower(*strQuery);
	for(unsigned int j = 0; j != strQueryLower.length(); j++)
		strQueryLower[j] = tolower(strQueryLower[j]);
	const WCHAR *szQueryLower = strQueryLower.c_str();
	
	//Create lower query path string for case-insensitive search
	wstring strQueryPathLower(strQueryPath != NULL ? *strQueryPath : TEXT(""));
	for(unsigned int j = 0; j != strQueryPathLower.length(); j++)
		strQueryPathLower[j] = tolower((*strQueryPath)[j]);
	wstring* pstrQueryPathLower = strQueryPath != NULL && strQueryPathLower.length() > 0 ? &strQueryPathLower : NULL;

	//If the query path is different from the last query so that the results are not valid anymore, the last query needs to be dropped
	if(!(strQueryPath != NULL && (LastResult.maxResults == -1 || LastResult.iOffset == 0) && (LastResult.SearchPath.length() == 0 || strQueryPathLower.find(LastResult.SearchPath) == 0)))
		LastResult = SearchResult();

	//Calculate Filter value and length of the current query which are compared with the cached ones to skip many of them
	DWORDLONG QueryFilter = MakeFilter(&strQueryLower);
	DWORDLONG QueryLength = (QueryFilter & 0xE000000000000000ui64) >> 61ui64; //Bits 61-63 for storing lengths up to 8
	QueryFilter = QueryFilter & 0x1FFFFFFFFFFFFFFFui64; //All but the last 3 bits

	//If the same query string as in the last query was used
	if(strQueryLower.compare(LastResult.Query) == 0 && LastResult.Results.size() > 0 && (LastResult.SearchEndedWhere == NO_WHERE && iOffset != 1)) // need proper condition here to skip
	{
		//Keep the position of the last result
		SearchWhere = LastResult.SearchEndedWhere;
		iOffset = LastResult.iOffset;
		bSkipSearch = true;
		for(int i = 0; i != LastResult.Results.size(); i++)
		{
			BOOL bFound = true;
			if(pstrQueryPathLower != NULL)
			{
				wstring strPathLower(LastResult.Results[i].Path);
				for(unsigned int j = 0; j != strPathLower.length(); j++)
					strPathLower[j] = tolower(LastResult.Results[i].Path[j]);
				bFound = strPathLower.find(strQueryPathLower) != -1;
			}
			if(bFound)
			{
				nResults++;
				//If the result limit has decreased and we have found all (shouldn't happen in common scenarios)
				if(maxResults != -1 && nResults > maxResults)
				{
					nResults = -1;
				
					//If we get here, the next incremental should start fresh, but only if it requires more results than this one.
					//To accomplish this we make this result contain no information about the origin of these results.
					SearchWhere = NO_WHERE;
					iOffset = 1;
					break;
				}
				rgsrfResults->insert(rgsrfResults->end(), LastResult.Results[i]);
			}
		}
		//if the last search was limited and didn't finish because it found enough files and we don't have the maximum number of results yet
		//we need to continue the search where the last one stopped.
		if(LastResult.maxResults != -1 && LastResult.SearchEndedWhere != NO_WHERE && (maxResults == -1 || nResults < maxResults))
			bSkipSearch = false;
	}
	//If this query is more specific than the previous one, it can use the results from the previous query
	else if(strQueryLower.find(LastResult.Query) != -1 && LastResult.Results.size() > 0)
	{
		bSkipSearch = true;
		//Keep the position of the last result
		SearchWhere = LastResult.SearchEndedWhere;
		iOffset = LastResult.iOffset;
		FindInPreviousResults(*strQuery, szQueryLower, QueryFilter, QueryLength, pstrQueryPathLower, *rgsrfResults, 0, bEnhancedSearch, maxResults, nResults);

		//if the last search was limited and didn't finish because it found enough files and we don't have the maximum number of results yet
		//we need to continue the search where the last one stopped.
		if(LastResult.maxResults != -1 && LastResult.SearchEndedWhere != NO_WHERE && (maxResults == -1 || nResults < maxResults))
			bSkipSearch = false;
	}
	DWORDLONG FRNPath;
	long long nFilesInDir = -1;
	if(strQueryPath != NULL && strQueryPath->length())
	{
		FRNPath = PathToFRN(strQueryPath);
		wstring strPath2;
		GetDir(FRNPath, &strPath2);
		int iOffset = (int) FindDirOffsetByIndex(FRNPath);
		if(iOffset != -1)
			nFilesInDir = rgDirectories[iOffset].nFiles;
	}
	if(SearchWhere == IN_FILES && iOffset == 0 && nFilesInDir != -1 && nFilesInDir < 10000 && !bSkipSearch)
	{
		FindRecursively(*strQuery, szQueryLower, QueryFilter, QueryLength, strQueryPath, *rgsrfResults, bEnhancedSearch, maxResults, nResults);
		SearchWhere = NO_WHERE;
	}
	else if(SearchWhere == IN_FILES && !bSkipSearch)
	{
		//Find in file index
		FindInJournal(*strQuery, szQueryLower, QueryFilter, QueryLength, (strQueryPath != NULL ? &strQueryPathLower : NULL), rgFiles, *rgsrfResults, iOffset, bEnhancedSearch, maxResults, nResults);
		//If we found the maximum number of results in the file index we stop here
		if(maxResults != -1 && nResults == -1)
			iOffset++; //Start with next entry on the next incremental search
		else //Search isn't limited or not all results found yet, continue in directory index
		{
			SearchWhere = IN_DIRECTORIES;
			iOffset = 0;
		}
	}

	if(SearchWhere == IN_DIRECTORIES && !bSkipSearch)
	{
		//Find in directory index
		FindInJournal(*strQuery, szQueryLower, QueryFilter, QueryLength, pstrQueryPathLower, rgDirectories, *rgsrfResults, iOffset, bEnhancedSearch, maxResults, nResults);
		//If we found the maximum number of results in the directory index we stop here
		if(maxResults != -1 && nResults == -1)
			iOffset++; //Start with next entry on the next incremental search
		else //Search isn't limited or less than the maximum number of results found
		{
			SearchWhere = NO_WHERE;
			iOffset = 0;
		}
	}

	//Sort by match quality and name
	if(bSort)
		sort(rgsrfResults->begin(), rgsrfResults->end());
	
	// Store this query
	LastResult.Query = wstring(strQueryLower);
	
	// Store search path
	LastResult.SearchPath = strQueryPathLower;

	//Clear old results, they will be replaced with the current ones
	LastResult.Results = vector<SearchResultFile>();

	//Store number of results (Needed for incremental search)
	LastResult.iOffset = iOffset;

	//Store if this search was limited
	LastResult.maxResults = maxResults;

	//Store where the current search ended due to file limit (or if it didn't);
	LastResult.SearchEndedWhere = SearchWhere;

	//Update last results
	for(unsigned int i = 0; i != rgsrfResults->size(); i++)
		LastResult.Results.insert(LastResult.Results.end(), (*rgsrfResults)[i]);

	return nResults;
}

void CDriveIndex::FindRecursively(wstring &strQuery, const WCHAR* &szQueryLower, DWORDLONG QueryFilter, DWORDLONG QueryLength, wstring* strQueryPath, vector<SearchResultFile> &rgsrfResults, BOOL bEnhancedSearch, int maxResults, int &nResults)
{
	WIN32_FIND_DATA ffd;
	size_t length_of_arg;
	HANDLE hFind = INVALID_HANDLE_VALUE;

	// Check that the input path plus 3 is not longer than MAX_PATH.
	// Three characters are for the "\*" plus NULL appended below.
	length_of_arg = strQueryPath->length();
	if (length_of_arg > (MAX_PATH - 3))
		return;

	// Prepare string for use with FindFile functions.  First, copy the
	// string to a buffer, then append '\*' to the directory name.
	wstring strPath = wstring(*strQueryPath);
	if((*strQueryPath)[strQueryPath->length() - 1] != L'\\')
		strPath += wstring(TEXT("\\*"));
	else
		strPath += wstring(TEXT("*"));

	const WCHAR* szDir = strPath.c_str();

	// Find the first file in the directory.
	hFind = FindFirstFile(szDir, &ffd);

	if (hFind == INVALID_HANDLE_VALUE)
		return;
	unsigned int nFiles = 0;
	// List all the files in the directory with some info about them.
	do
	{
		if(ffd.dwFileAttributes & FILE_ATTRIBUTE_VIRTUAL || ffd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
			continue;
		float MatchQuality;
		wstring strFilename(ffd.cFileName);
		if(strFilename.compare(TEXT(".")) == 0 || strFilename.compare(TEXT("..")) == 0)
			continue;
		nFiles++;
		if(bEnhancedSearch)
			MatchQuality = FuzzySearch(strFilename, strQuery);
		else
		{
			wstring szLower(strFilename);
			for(unsigned int j = 0; j != szLower.length(); j++)
				szLower[j] = tolower(szLower[j]);
			MatchQuality = szLower.find(strQuery) != -1;
		}

		if(MatchQuality > 0.6f)
		{
			nResults++;
			if(maxResults != -1 && nResults > maxResults)
			{
				nResults = -1;
				break;
			}
			SearchResultFile srf;
			srf.Filename = strFilename;
			srf.Path = *strQueryPath + TEXT("\\");
			srf.Filter = MAXULONG64;
			srf.MatchQuality = MatchQuality;
			rgsrfResults.insert(rgsrfResults.end(), srf);
		}

		if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
		{
			wstring strSubPath = wstring(*strQueryPath);
			if((*strQueryPath)[strQueryPath->length() - 1] != L'\\')
				strSubPath += L'\\';
			strSubPath += ffd.cFileName;
			FindRecursively(strQuery, szQueryLower, QueryFilter, QueryLength, &strSubPath, rgsrfResults, bEnhancedSearch, maxResults, nResults);
			if(nResults == -1)
				break;
		}
	}
	while (FindNextFile(hFind, &ffd) != 0);
	FindClose(hFind);
}

//T needs to be IndexedFile or IndexedDirectory
template <class T>
void CDriveIndex::FindInJournal(wstring &strQuery, const WCHAR* &szQueryLower, DWORDLONG QueryFilter, DWORDLONG QueryLength, wstring * strQueryPath, vector<T> &rgJournalIndex, vector<SearchResultFile> &rgsrfResults, unsigned int iOffset, BOOL bEnhancedSearch, int maxResults, int &nResults)
{
	for(unsigned int j = 0; j != rgJournalIndex.size(); j++)
	{
		IndexedFile* i = (IndexedFile*)&rgJournalIndex[j];
		DWORDLONG Length = (i->Filter & 0xE000000000000000ui64) >> 61ui64; //Bits 61-63 for storing lengths up to 8
		DWORDLONG Filter = i->Filter & 0x1FFFFFFFFFFFFFFFui64; //All but the last 3 bits
		if((Filter & QueryFilter) == QueryFilter && QueryLength <= Length)
		{
			USNEntry file = FRNToName(i->Index);
			float MatchQuality;
			if(bEnhancedSearch)
				MatchQuality = FuzzySearch(file.Name, strQuery);
			else
			{
				wstring szLower(file.Name);
				for(unsigned int j = 0; j != szLower.length(); j++)
					szLower[j] = tolower(szLower[j]);
				MatchQuality = szLower.find(strQuery) != -1;
			}

			if(MatchQuality > 0.6f)
			{
				nResults++;
				if(maxResults != -1 && nResults > maxResults)
				{
					nResults = -1;
					break;
				}
				SearchResultFile srf;
				srf.Filename = file.Name;
				srf.Path.reserve(MAX_PATH);
				Get(i->Index, &srf.Path);
				BOOL bFound = true;
				if(strQueryPath != NULL)
				{
					wstring strPathLower(srf.Path);
					for(unsigned int j = 0; j != strPathLower.length(); j++)
						strPathLower[j] = tolower(strPathLower[j]);
					bFound = strPathLower.find(*strQueryPath) != -1;
				}
				if(bFound)
				{
					//split path
					WCHAR szDrive[_MAX_DRIVE];
					WCHAR szPath[_MAX_PATH];
					WCHAR szName[_MAX_FNAME];
					WCHAR szExt[_MAX_EXT];
					_wsplitpath(srf.Path.c_str(), szDrive, szPath, szName, szExt);
					srf.Path = wstring(szDrive) + wstring(szPath);
					srf.Filter = i->Filter;
					srf.MatchQuality = MatchQuality;
					rgsrfResults.insert(rgsrfResults.end(), srf);
				}
			}
		}
	}
}
void CDriveIndex::FindInPreviousResults(wstring &strQuery, const WCHAR* &szQueryLower, DWORDLONG QueryFilter, DWORDLONG QueryLength, wstring * strQueryPath, vector<SearchResultFile> &rgsrfResults, unsigned int  iOffset, BOOL bEnhancedSearch, int maxResults, int &nResults)
{
	for(int i = 0; i != LastResult.Results.size() && (maxResults == -1 || i < maxResults); i++)
	{
		SearchResultFile *srf = & LastResult.Results[i];
		DWORDLONG Length = (srf->Filter & 0xE000000000000000ui64) >> 61ui64; //Bits 61-63 for storing lengths up to 8
		DWORDLONG Filter = srf->Filter & 0x1FFFFFFFFFFFFFFFui64; //All but the last 3 bits
		if((Filter & QueryFilter) == QueryFilter && QueryLength <= Length)
		{
			if(bEnhancedSearch)
				srf->MatchQuality = FuzzySearch(srf->Filename, strQuery);
			else
			{
				wstring szLower(srf->Filename);
				for(unsigned int j = 0; j != szLower.length(); j++)
					szLower[j] = tolower(szLower[j]);
				srf->MatchQuality = szLower.find(szQueryLower) != -1;
			}
			if(srf->MatchQuality > 0.6f)
			{
				BOOL bFound = true;
				if(strQueryPath != NULL)
				{
					wstring strPathLower(srf->Path);
					for(unsigned int j = 0; j != srf->Path.length(); j++)
						strPathLower[j] = tolower(srf->Path[j]);
					bFound = strPathLower.find(*strQueryPath) != -1;
				}
				if(bFound)
				{
					nResults++;
					if(maxResults != -1 && nResults > maxResults)
					{
						nResults = -1;
						break;
					}
					rgsrfResults.insert(rgsrfResults.end(), *srf);
				}
			}
		}
	}
}
	

// Clears the database
BOOL CDriveIndex::Empty()
{
	rgFiles.clear();
	rgDirectories.clear();
	return(TRUE);
}



// Constructs a path for a file
BOOL CDriveIndex::Get(DWORDLONG Index, wstring *sz) const
{
	*sz = TEXT("");
	int n = 0;
	do {
		USNEntry file = FRNToName(Index);
		*sz = file.Name + ((n != 0) ? TEXT("\\") : TEXT("")) + *sz;
		Index = file.ParentIndex;
		n++;
	} while (Index != 0);
	return(TRUE);
}



// Constructs a path for a directory
BOOL CDriveIndex::GetDir(DWORDLONG Index, wstring *sz) const
{
	*sz = TEXT("");
	do {
		USNEntry file = FRNToName(Index);
		*sz = file.Name + ((sz->length() != 0) ? TEXT("\\") : TEXT("")) + *sz;
		Index = file.ParentIndex;
	} while (Index != 0);
	return(TRUE);
}



//Finds the position of a file in the database by the FileReferenceNumber
INT64 CDriveIndex::FindOffsetByIndex(DWORDLONG Index) {

	vector<IndexedFile>::difference_type pos;
	IndexedFile i;
	i.Index = Index;
	pos = distance(rgFiles.begin(), lower_bound(rgFiles.begin(), rgFiles.end(), i));
	return (INT64) (pos == rgFiles.size() ? -1 : pos); // this is valid because the number of files doesn't exceed the range of INT64
}



//Finds the position of a directory in the database by the FileReferenceNumber
INT64 CDriveIndex::FindDirOffsetByIndex(DWORDLONG Index)
{
	vector<IndexedDirectory>::difference_type pos;
	IndexedDirectory i;
	i.Index = Index;
	pos = distance(rgDirectories.begin(), lower_bound(rgDirectories.begin(), rgDirectories.end(), i));
	return (INT64) (pos == rgDirectories.size() ? -1 : pos); // this is valid because the number of files doesn't exceed the range of INT64
}

DWORDLONG PathToFRN(wstring* strPath)
{
	HANDLE hDir = CreateFile(strPath->c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
	if(hDir == INVALID_HANDLE_VALUE)
		return 0;
	BY_HANDLE_FILE_INFORMATION fi;
	GetFileInformationByHandle(hDir, &fi);
	CloseHandle(hDir);
	return (((DWORDLONG) fi.nFileIndexHigh) << 32) | fi.nFileIndexLow;
}

// Enumerate the MFT for all entries. Store the file reference numbers of
// any directories in the database.
void CDriveIndex::PopulateIndex()
{
	Empty();
	
	vector<DWORDLONG> FileParents;
	vector<DWORDLONG> DirectoryParents;

	USN_JOURNAL_DATA ujd;
	Query(&ujd);

	// Get the FRN of the root directory
	// This had BETTER work, or we can't do anything

	WCHAR szRoot[_MAX_PATH];
	wsprintf(szRoot, TEXT("%c:\\"), m_cDrive);
	HANDLE hDir = CreateFile(szRoot, 0, FILE_SHARE_READ | FILE_SHARE_WRITE,
		NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);

	BY_HANDLE_FILE_INFORMATION fi;
	GetFileInformationByHandle(hDir, &fi);
	CloseHandle(hDir);
	DWORDLONG IndexRoot = (((DWORDLONG) fi.nFileIndexHigh) << 32) | fi.nFileIndexLow;
	wsprintf(szRoot, TEXT("%c:"), m_cDrive);
	AddDir(IndexRoot, &wstring(szRoot), 0);
	DirectoryParents.insert(DirectoryParents.end(), 0);
	m_dwDriveFRN = IndexRoot;

	MFT_ENUM_DATA med;
	med.StartFileReferenceNumber = 0;
	med.LowUsn = 0;
	med.HighUsn = ujd.NextUsn;

	// Process MFT in 64k chunks
	BYTE pData[sizeof(DWORDLONG) + 0x10000];
	DWORDLONG fnLast = 0;
	DWORD cb;
	unsigned int num = 0;
	unsigned int numDirs = 1;
	while (DeviceIoControl(m_hVol, FSCTL_ENUM_USN_DATA, &med, sizeof(med), pData, sizeof(pData), &cb, NULL) != FALSE) {

		PUSN_RECORD pRecord = (PUSN_RECORD) &pData[sizeof(USN)];
		while ((PBYTE) pRecord < (pData + cb)) {
			if ((pRecord->FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
				numDirs++;
			else
				num++;
			pRecord = (PUSN_RECORD) ((PBYTE) pRecord + pRecord->RecordLength);
		}
		med.StartFileReferenceNumber = * (DWORDLONG *) pData;
	}
	
	FileParents.reserve(num);
	DirectoryParents.reserve(numDirs);
	rgFiles.reserve(num);
	rgDirectories.reserve(numDirs);
	hash_map<DWORDLONG, HashMapEntry> hmFiles;
	hash_map<DWORDLONG, HashMapEntry> hmDirectories;
	hash_map<DWORDLONG, HashMapEntry>::iterator it;
	med.StartFileReferenceNumber = 0;
	while (DeviceIoControl(m_hVol, FSCTL_ENUM_USN_DATA, &med, sizeof(med), pData, sizeof(pData), &cb, NULL) != FALSE)
	{
		PUSN_RECORD pRecord = (PUSN_RECORD) &pData[sizeof(USN)];
		while ((PBYTE) pRecord < (pData + cb))
		{
			wstring sz((LPCWSTR) ((PBYTE) pRecord + pRecord->FileNameOffset), pRecord->FileNameLength / sizeof(WCHAR));
			if ((pRecord->FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
			{
				AddDir(pRecord->FileReferenceNumber, &sz, pRecord->ParentFileReferenceNumber);
				//DirectoryParents.insert(DirectoryParents.end(), pRecord->ParentFileReferenceNumber);
				HashMapEntry hme;
				hme.iOffset = rgDirectories.size() - 1;
				hme.ParentFRN = pRecord->ParentFileReferenceNumber;
				hmDirectories[pRecord->FileReferenceNumber] = hme;
			}
			else
			{
				Add(pRecord->FileReferenceNumber, &sz, pRecord->ParentFileReferenceNumber);
				HashMapEntry hme;
				hme.iOffset = rgFiles.size() - 1;
				hme.ParentFRN = pRecord->ParentFileReferenceNumber;
				//FileParents.insert(FileParents.end(), pRecord->ParentFileReferenceNumber);
				hmFiles[pRecord->FileReferenceNumber] = hme;
			}
			pRecord = (PUSN_RECORD) ((PBYTE) pRecord + pRecord->RecordLength);
		}
		med.StartFileReferenceNumber = * (DWORDLONG *) pData;
	}

	//Calculate files per directory. This takes most of the indexing time, but this information can be useful to reduce the time needed
	//for searching in directories with few files (less than 10k).
	for ( it=hmFiles.begin() ; it != hmFiles.end(); it++ )
	{
		HashMapEntry* hme = &hmDirectories[it->second.ParentFRN];
		do
		{
			rgDirectories[hme->iOffset].nFiles++;
			HashMapEntry* hme2 = &hmDirectories[it->second.ParentFRN];

			if(hme != hme2)
				hme = hme2;
			else // This must not happen, otherwise a directory is its own parent!
				break;
		} while(hme->ParentFRN != 0);
	}
	//for(unsigned int i = 0; i != FileParents.size(); i++)
	//{
	//	DWORDLONG dwIndex = FileParents[i];
	//	while(dwIndex != 0)
	//	{
	//		int iOffset = -1;
	//		for(unsigned int j = 0; j != rgDirectories.size(); j++)
	//			if(rgDirectories[j].Index == dwIndex)
	//			{
	//				iOffset = j;
	//				break;
	//			}
	//		if(iOffset == -1)
	//			break;
	//		rgDirectories[iOffset].nFiles++;
	//		DWORDLONG dwIndex2 = DirectoryParents[iOffset];

	//		if(dwIndex != dwIndex2)
	//			dwIndex = dwIndex2;
	//		else // This must not happen, otherwise a directory is its own parent!
	//			break;
	//	}
	//	//wstring strPath;
	//	//GetDir(dwIndex, &strPath);

	//	//do {
	//	//	//USNEntry file = FRNToName(dwIndex);
	//	//	int iOffset = -1;
	//	//	for(int j = 0; j != rgDirectories.size(); j++)
	//	//		if(rgDirectories[j].Index == dwIndex)
	//	//		{
	//	//				iOffset = j;
	//	//				break;
	//	//		}
	//	//	if(iOffset == -1)
	//	//		break;
	//	//	//USNEntry parent = FRNToName(file.ParentIndex);
	//	//	//USNEntry parent2 = FRNToName(rgDirectories[iOffset].Index);
	//	//	rgDirectories[iOffset].nFiles++;
	//	//	dwIndex = file.ParentIndex;
	//	//} while (dwIndex != 0);
	//}
	rgFiles.shrink_to_fit();
	rgDirectories.shrink_to_fit();
	sort(rgFiles.begin(), rgFiles.end());
	sort(rgDirectories.begin(), rgDirectories.end());
}

// Resolve FRN to filename by enumerating USN journal with StartFileReferenceNumber=FRN
USNEntry CDriveIndex::FRNToName(DWORDLONG FRN) const
{
	if(FRN == m_dwDriveFRN)
		return USNEntry(wstring(1, m_cDrive) + wstring(TEXT(":")), 0);
	USN_JOURNAL_DATA ujd;
	Query(&ujd);

	MFT_ENUM_DATA med;
	med.StartFileReferenceNumber = FRN;
	med.LowUsn = 0;
	med.HighUsn = ujd.NextUsn;

	// The structure only needs a single entry so it can be pretty small
	BYTE pData[sizeof(DWORDLONG) + 0x300];
	DWORD cb;
	while (DeviceIoControl(m_hVol, FSCTL_ENUM_USN_DATA, &med, sizeof(med), pData, sizeof(pData), &cb, NULL) != FALSE) {

		PUSN_RECORD pRecord = (PUSN_RECORD) &pData[sizeof(USN)];
		while ((PBYTE) pRecord < (pData + cb)) {
			if(pRecord->FileReferenceNumber == FRN)
				return USNEntry(wstring((LPCWSTR) ((PBYTE) pRecord + pRecord->FileNameOffset), pRecord->FileNameLength / sizeof(WCHAR)), pRecord->ParentFileReferenceNumber);
			pRecord = (PUSN_RECORD) ((PBYTE) pRecord + pRecord->RecordLength);
		}
		med.StartFileReferenceNumber = * (DWORDLONG *) pData;
	}
	return USNEntry(wstring(TEXT("")), 0);
}



// Saves the database to disk. The file can be used to create an instance of CDriveIndex.
BOOL CDriveIndex::SaveToDisk(wstring &strPath) const
{
	ofstream::pos_type size;
	ofstream file (strPath.c_str(), ios::out|ios::binary|ios::trunc);
	if (file.is_open())
	{
		//Drive character
		file.write((char*) &m_cDrive, sizeof(m_cDrive));

		//Drive FileReferenceNumber
		file.write((char*) &m_dwDriveFRN, sizeof(m_dwDriveFRN));

		unsigned int size = rgFiles.size();
		//Number of files
		file.write((char*) &size, sizeof(rgFiles.size()));
		//indexed files
		file.write((char*) &(rgFiles[0]), sizeof(IndexedFile) * rgFiles.size());

		size = rgDirectories.size();
		//Number of directories
		file.write((char*) &size, sizeof(rgDirectories.size()));
		//indexed directories
		file.write((char*) &(rgDirectories[0]), sizeof(IndexedDirectory) * rgDirectories.size());
		file.close();
		return true;
	}
	return false;
}



// Constructor for loading the index from a previously saved file
CDriveIndex::CDriveIndex(wstring &strPath)
{
	m_hVol = INVALID_HANDLE_VALUE;
	Empty();

	ifstream::pos_type size;

	ifstream file (strPath.c_str(), ios::in | ios::binary);
	if (file.is_open())
	{
		//Drive
		WCHAR cDrive;
		file.read((char*) &cDrive, sizeof(WCHAR));

		if(Init(cDrive))
		{
			// Drive FileReferenceNumber
			file.read((char*) &m_dwDriveFRN, sizeof(m_dwDriveFRN));
			
			//Number of files
			unsigned int numFiles = 0;
			file.read((char*) &numFiles, sizeof(numFiles));
			rgFiles.reserve(numFiles);

			//indexed files
			for(unsigned int j = 0; j != numFiles; j++)
			{
				IndexedFile i;
				file.read((char*) &i, sizeof(IndexedFile));
				rgFiles.insert(rgFiles.end(), i);
			}
		
			//Number of directories
			unsigned int numDirs = 0;
			file.read((char*) &numDirs, sizeof(numDirs));
			rgDirectories.reserve(numDirs);

			//indexed directories
			for(unsigned int j = 0; j != numDirs; j++)
			{
				IndexedDirectory i;
				file.read((char*) &i, sizeof(IndexedDirectory));
				rgDirectories.insert(rgDirectories.end(), i);
			}
		}
		file.close();
	}
	return;
}



// Returns the number of files and folders on this drive
DriveInfo CDriveIndex::GetInfo() const
{
	DriveInfo di;
 	di.NumFiles = (DWORDLONG) rgFiles.size();
	di.NumDirectories = (DWORDLONG) rgDirectories.size();
	return di;
}




//Performs a fuzzy search for shorter in longer.
//return values range from 0.0 = identical to 1.0 = completely different. 0.4 seems appropriate
float FuzzySearch(wstring &longer, wstring &shorter)
{
	//Note: All string lengths are shorter than MAX_PATH, so an uint is perfectly fitted.
	unsigned int lenl = (unsigned int) longer.length();
	unsigned int lens = (unsigned int) shorter.length();

	if(lens > lenl)
		return 0.0f;

	//Check if the shorter string is a substring of the longer string
	unsigned int Contained = (unsigned int) longer.find(shorter);
	if(Contained != wstring::npos)
		return Contained == 0 ? 1.0f : 0.8f;

	wstring longerlower(longer);
	wstring shorterlower(shorter);
	for(unsigned int i = 0; i != lenl; i++)
		longerlower[i] = tolower(longer[i]);
	for(unsigned int i = 0; i != lens; i++)
		shorterlower[i] = tolower(shorter[i]);

	//Check if the shorter string is a substring of the longer string
	Contained = (unsigned int) longerlower.find(shorterlower);
	if(Contained != wstring::npos)
		return Contained == 0 ? 0.9f : 0.7f;

	//Check if string can be matched by omitting characters
	if(lens < 5)
	{
		unsigned int pos = 0;
		unsigned int matched = 0;
		for(unsigned int i = 0; i != lens; i++)
		{
			WCHAR c = toupper(shorter[i]); //only look for capital letters in longer string, (e.g. match tc in TrueCrypt)
			for(unsigned int j = 0; j != lenl - pos; j++)
			{
				if(longer[pos + j] == c)
				{
					pos = j;
					matched++;
					break;
				}
				else
					continue;
			}
		}
		if(matched == lens)
			return 0.9f; //Slightly worse than direct matches
	}
	return 0;
}