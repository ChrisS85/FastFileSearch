/**********************************************************************************
Module name: CDriveIndex.cpp
Written by: Christian Sander
Credits for original code this is based on: Jeffrey Cooperstein & Jeffrey Richter
**********************************************************************************/

#include "stdafx.h"
#include "CDriveIndex.h"
#include <algorithm>
#include <iostream>
#include <fstream>



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



// Exported function to search in the index of a drive.
// Returns a string that contains the filepaths of the results,
// separated by newlines for easier processing in non-C++ languages.
WCHAR* _stdcall Search(CDriveIndex *di, WCHAR *szQuery)
{
	if(dynamic_cast<CDriveIndex*>(di) && szQuery)
	{
		vector<SearchResultFile> results;
		wstring result;
		di->Find(&wstring(szQuery), &results, true, true);
		for(unsigned int i = 0; i != results.size(); i++)
			result += (i == 0 ? TEXT("") : TEXT("\n")) + results[i].Path + results[i].Filename;
		WCHAR * szOutput = new WCHAR[result.length() + 1];
		ZeroMemory(szOutput, (result.length() + 1) * sizeof(szOutput[0]));
		_snwprintf(szOutput, result.length(), TEXT("%s"), result.c_str());
		return szOutput;
	}
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
BOOL CDriveIndex::Query(PUSN_JOURNAL_DATA pUsnJournalData)
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
	if (m_hVol != INVALID_HANDLE_VALUE) DebugBreak();
	m_cDrive = cDrive;
	ClearLastResult();
	BOOL fOk = FALSE;
	__try {
		// Open a handle to the volume
		m_hVol = Open(m_cDrive, GENERIC_WRITE | GENERIC_READ);
		if (INVALID_HANDLE_VALUE == m_hVol) __leave;
		fOk = TRUE;
	}
	__finally {
		if (!fOk) CleanUp();
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
		Filter = MakeAddress(szName);
	i.Filter = Filter;
	rgFiles.insert(rgFiles.end(), i);
	return(TRUE);
}



// Adds a directory to the database
BOOL CDriveIndex::AddDir(DWORDLONG Index, wstring *szName, DWORDLONG ParentIndex, DWORDLONG Filter)
{
	IndexedFile i;
	i.Index = Index;
	if(!Filter)
		Filter = MakeAddress(szName);
	i.Filter = Filter;
	rgDirectories.insert(rgDirectories.end(), i);
	return(TRUE);
}



// Calculates a 64bit value that is used to filter out many files before comparing their filenames
// This method gives a huge speed boost.
DWORDLONG CDriveIndex::MakeAddress(wstring *szName)
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
void CDriveIndex::Find(wstring *strQuery, vector<SearchResultFile> *rgszResults, BOOL bSort, BOOL bEnhancedSearch)
{
	if(strQuery->length() == 0)
	{
		// Store this query
		LastResult.Query = wstring(TEXT(""));
		LastResult.Results = vector<SearchResultFile>();
		return;
	}
	wstring strQueryLower(*strQuery);
	for(unsigned int j = 0; j != strQueryLower.length(); j++)
		strQueryLower[j] = tolower(strQueryLower[j]);
	const WCHAR *szQuery = strQueryLower.c_str();
	
	
	//finds all files that contain pszQuery in their names
	DWORDLONG QueryFilter = MakeAddress(&strQueryLower);
	DWORDLONG QueryLength = (QueryFilter & 0xE000000000000000ui64) >> 61ui64; //Bits 61-63 for storing lengths up to 8
	QueryFilter = QueryFilter & 0x1FFFFFFFFFFFFFFFui64; //All but the last 3 bits

	if(strQueryLower.compare(LastResult.Query) == 0 && LastResult.Results.size() > 0)
		for(SearchResultFile *srf = &LastResult.Results[0]; srf <= &LastResult.Results.back(); srf++)
			rgszResults->insert(rgszResults->end(), *srf);
	else if(strQueryLower.find(LastResult.Query) != -1 && LastResult.Results.size() > 0)
	{
		vector<SearchResultFile> results;
		for(SearchResultFile *srf = &LastResult.Results[0]; srf <= &LastResult.Results.back(); srf++)
		{
			DWORDLONG Length = (srf->Filter & 0xE000000000000000ui64) >> 61ui64; //Bits 61-63 for storing lengths up to 8
			DWORDLONG Filter = srf->Filter & 0x1FFFFFFFFFFFFFFFui64; //All but the last 3 bits
			if((Filter & QueryFilter) == QueryFilter && QueryLength <= Length)
			{
				if(bEnhancedSearch)
					srf->MatchQuality = FuzzySearch(srf->Filename, *strQuery);
				else
				{
					wstring szLower(srf->Filename);
					for(unsigned int j = 0; j != szLower.length(); j++)
						szLower[j] = tolower(szLower[j]);
					srf->MatchQuality = szLower.find(szQuery) != -1;
				}
				if(srf->MatchQuality > 0.6f)
				{
					rgszResults->insert(rgszResults->end(), *srf);
					results.insert(results.end(), *srf);
				}
			}
		}
		LastResult.Results = results;
	}
	else
	{
		LastResult.Results = vector<SearchResultFile>();
		for(IndexedFile *i = &rgFiles[0]; i <= &rgFiles.back(); i++)
		{
			DWORDLONG Length = (i->Filter & 0xE000000000000000ui64) >> 61ui64; //Bits 61-63 for storing lengths up to 8
			DWORDLONG Filter = i->Filter & 0x1FFFFFFFFFFFFFFFui64; //All but the last 3 bits
			if((Filter & QueryFilter) == QueryFilter && QueryLength <= Length)
			{
				USNEntry file = FRNToName(i->Index);
				float MatchQuality;
				if(bEnhancedSearch)
					MatchQuality = FuzzySearch(file.Name, *strQuery);
				else
				{
					wstring szLower(file.Name);
					for(unsigned int j = 0; j != szLower.length(); j++)
						szLower[j] = tolower(szLower[j]);
					MatchQuality = szLower.find(szQuery) != -1;
				}

				if(MatchQuality > 0.6f)
				{
					SearchResultFile srf;
					srf.Filename = file.Name;
					srf.Path.reserve(MAX_PATH);
					Get(i->Index, &srf.Path);

					//split path
					WCHAR szDrive[_MAX_DRIVE];
					WCHAR szPath[_MAX_PATH];
					WCHAR szName[_MAX_FNAME];
					WCHAR szExt[_MAX_EXT];
					_wsplitpath(srf.Path.c_str(), szDrive, szPath, szName, szExt);
					srf.Path = wstring(szDrive) + wstring(szPath);
					srf.Filter = i->Filter;
					srf.MatchQuality = MatchQuality;
					rgszResults->insert(rgszResults->end(), srf);
				}
			}
		}
		for(IndexedFile *i = &rgDirectories[0]; i <= &rgDirectories.back(); i++)
		{
			DWORDLONG Length = (i->Filter & 0xE000000000000000ui64) >> 61ui64; //Bits 61-63 for storing lengths up to 8
			DWORDLONG Filter = i->Filter & 0x1FFFFFFFFFFFFFFFui64; //All but the last 3 bits
			if((Filter & QueryFilter) == QueryFilter && QueryLength <= Length)
			{
				USNEntry file = FRNToName(i->Index);
				float MatchQuality;
				if(bEnhancedSearch)
					MatchQuality = FuzzySearch(file.Name, *strQuery);
				else
				{
					wstring szLower(file.Name);
					for(unsigned int j = 0; j != szLower.length(); j++)
						szLower[j] = tolower(szLower[j]);
					MatchQuality = szLower.find(szQuery) != -1;
				}

				if(MatchQuality > 0.6f)
				{
					SearchResultFile srf;
					srf.Filename = file.Name;
					srf.Path.reserve(MAX_PATH);
					Get(i->Index, &srf.Path);

					//split path
					WCHAR szDrive[_MAX_DRIVE];
					WCHAR szPath[_MAX_PATH];
					WCHAR szName[_MAX_FNAME];
					WCHAR szExt[_MAX_EXT];
					_wsplitpath(srf.Path.c_str(), szDrive, szPath, szName, szExt);
					srf.Path = wstring(szDrive) + wstring(szPath);
					srf.Filter = i->Filter;
					srf.MatchQuality = MatchQuality;
					rgszResults->insert(rgszResults->end(), srf);
				}
			}
		}
		for(unsigned int i = 0; i != rgszResults->size(); i++)
			LastResult.Results.insert(LastResult.Results.end(), (*rgszResults)[i]);
	}
	if(bSort)
		sort(rgszResults->begin(), rgszResults->end());
	// Store this query
	LastResult.Query = wstring(strQueryLower);
}



// Clears the database
BOOL CDriveIndex::Empty()
{
	rgFiles.clear();
	rgDirectories.clear();
	return(TRUE);
}



// Constructs a path for a file
BOOL CDriveIndex::Get(DWORDLONG Index, wstring *sz)
{
	*sz = TEXT("");
	int n = 0;
	do {
		INT64 Offset;
		if(n == 0)
			Offset = FindOffsetByIndex(Index);
		else
			Offset = FindDirOffsetByIndex(Index);
		if(Offset == -1)
			return FALSE;
		USNEntry file = FRNToName(Index);
		*sz = file.Name + ((n != 0) ? TEXT("\\") : TEXT("")) + *sz;
		Index = file.ParentIndex;
		n++;
	} while (Index != 0);
	return(TRUE);
}



// Constructs a path for a directory
BOOL CDriveIndex::GetDir(DWORDLONG Index, wstring *sz)
{
	*sz = TEXT("");
	do {
		INT64 Offset = FindDirOffsetByIndex(Index);
		if(Offset == -1)
			return FALSE;
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
	vector<IndexedFile>::difference_type pos;
	IndexedFile i;
	i.Index = Index;
	pos = distance(rgDirectories.begin(), lower_bound(rgDirectories.begin(), rgDirectories.end(), i));
	return (INT64) (pos == rgDirectories.size() ? -1 : pos); // this is valid because the number of files doesn't exceed the range of INT64
}



// Enumerate the MFT for all entries. Store the file reference numbers of
// any directories in the database.
void CDriveIndex::PopulateIndex()
{
	Empty();
	USN_JOURNAL_DATA ujd;
	Query(&ujd);

	// Get the FRN of the root directory
	// This had BETTER work, or we can't do anything

	TCHAR szRoot[_MAX_PATH];
	wsprintf(szRoot, TEXT("%c:\\"), m_cDrive);
	HANDLE hDir = CreateFile(szRoot, 0, FILE_SHARE_READ | FILE_SHARE_WRITE,
		NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);

	BY_HANDLE_FILE_INFORMATION fi;
	GetFileInformationByHandle(hDir, &fi);
	CloseHandle(hDir);
	DWORDLONG IndexRoot = (((DWORDLONG) fi.nFileIndexHigh) << 32) 
		| fi.nFileIndexLow;
	wsprintf(szRoot, TEXT("%c:"), m_cDrive);
	AddDir(IndexRoot, &wstring(szRoot), 0);
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
	unsigned int numDirs = 0;
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

	rgFiles.reserve(num);
	rgDirectories.reserve(numDirs);
	med.StartFileReferenceNumber = 0;
	while (DeviceIoControl(m_hVol, FSCTL_ENUM_USN_DATA, &med, sizeof(med),
		pData, sizeof(pData), &cb, NULL) != FALSE) {

			PUSN_RECORD pRecord = (PUSN_RECORD) &pData[sizeof(USN)];
			while ((PBYTE) pRecord < (pData + cb)) {
				wstring sz((LPCWSTR) ((PBYTE) pRecord + pRecord->FileNameOffset), pRecord->FileNameLength / sizeof(WCHAR));
				if ((pRecord->FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
					AddDir(pRecord->FileReferenceNumber, &sz, pRecord->ParentFileReferenceNumber);
				else
					Add(pRecord->FileReferenceNumber, &sz, pRecord->ParentFileReferenceNumber);
				pRecord = (PUSN_RECORD) ((PBYTE) pRecord + pRecord->RecordLength);
			}
			med.StartFileReferenceNumber = * (DWORDLONG *) pData;
	}
	rgFiles.shrink_to_fit();
	rgDirectories.shrink_to_fit();
	sort(rgFiles.begin(), rgFiles.end());
	sort(rgDirectories.begin(), rgDirectories.end());
}



// Resolve FRN to filename by enumerating USN journal with StartFileReferenceNumber=FRN
USNEntry CDriveIndex::FRNToName(DWORDLONG FRN)
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
BOOL CDriveIndex::SaveToDisk(wstring &strPath)
{
	ofstream::pos_type size;
	ofstream file (strPath.c_str(), ios::out|ios::binary|ios::trunc);
	if (file.is_open())
	{
		//Drive character
		file.write((char*) &m_cDrive, sizeof(m_cDrive));

		//Drive FileReferenceNumber
		file.write((char*) &m_dwDriveFRN, sizeof(m_dwDriveFRN));

		vector<IndexedFile>::size_type size = rgFiles.size();
		//Number of files
		file.write((char*) &size, sizeof(rgFiles.size()));
		//indexed files
		file.write((char*) &(rgFiles[0]), sizeof(IndexedFile) * rgFiles.size());

		size = rgDirectories.size();
		//Number of directories
		file.write((char*) &size, sizeof(rgDirectories.size()));
		//indexed directories
		file.write((char*) &(rgDirectories[0]), sizeof(IndexedFile) * rgDirectories.size());
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
				IndexedFile i;
				file.read((char*) &i, sizeof(IndexedFile));
				rgDirectories.insert(rgDirectories.end(), i);
			}
		}
		file.close();
	}
	return;
}



// Returns the number of files and folders on this drive
DriveInfo CDriveIndex::GetInfo()
{
	DriveInfo di;
 	di.NumFiles = (DWORDLONG) rgFiles.size();
	di.NumDirectories = (DWORDLONG) rgDirectories.size();
	return di;
}




//Performs a fuzzy search for shorter in longer.
//return values range from 0.0 = identical to 1.0 = completely different. 0.4 seems appropriate
float FuzzySearch(wstring &longer, wstring &shorter, BOOL UseFuzzySearch)
{
	if(longer.compare(shorter) == 0)
		return 1.0f;

	//Note: All string lengths are shorter than MAX_PATH, so an uint is perfectly fitted.
	unsigned int lenl = (unsigned int) longer.length();
	unsigned int lens = (unsigned int) shorter.length();

	if(lens > lenl)
		return 0.0f;

	//Check if the shorter string is a substring of the longer string
	unsigned int Contained = (unsigned int) longer.find(shorter);
	if(Contained != wstring::npos)
		return Contained == 0 ? 1.0f : 0.8f;

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
	//Calculate fuzzy string difference
	if(UseFuzzySearch)
	{
		float max = 0.0f;
		for(unsigned int i = 0; i != lenl - lens; i++)
			max = max(max, 1 - StringDifference(shorter, longer.substr(i, lens)));
		return max;
	}
	return 0;
}


//By Toralf:
//basic idea for SIFT3 code by Siderite Zackwehdex 
//http://siderite.blogspot.com/2007/04/super-fast-and-accurate-string-distance.html 
//took idea to normalize it to longest string from Brad Wood 
//http://www.bradwood.com/string_compare/ 
//Own work: 
// - when character only differ in case, LSC is a 0.8 match for this character 
// - modified code for speed, might lead to different results compared to original code 
// - optimized for speed (30% faster then original SIFT3 and 13.3 times faster than basic Levenshtein distance) 
//http://www.autohotkey.com/forum/topic59407.html 
// returns a float: between "0.0 = identical" and "1.0 = nothing in common" 
float StringDifference(wstring &string1, wstring &string2, unsigned int maxOffset)
{
	if(string1.compare(TEXT("")) == 0 || string2.compare(TEXT("")) == 0)
		return (string1.compare(string2) == 0 ? 0.0f : 1.0f);
	unsigned int l1 = (unsigned int) string1.length();
	unsigned int l2 = (unsigned int) string2.length();
	wstring string1lower(string1);
	wstring string2lower(string2);
	for(unsigned int i = 0; i != l1; i++)
		string1lower[i] = tolower(string1[i]);
	for(unsigned int i = 0; i != l2; i++)
		string2lower[i] = tolower(string2[i]);
	if(string1lower.compare(string2lower) == 0)
		return (string1.compare(string2) ? 0.0f : 0.2f/(float)l1); //either identical or (assumption:) "only one" char with different case
	unsigned int ni = 0;
	unsigned int mi = 0;
	float lcs = 0;
	while((ni <= l1) && (mi <= l2))
	{
		if(string1[ni] == string2[mi])
			lcs++;
		else if(tolower(string1[ni]) == tolower(string2[mi]))
			lcs += 0.8f;
		else
		{
			for(unsigned int i = 0; i != maxOffset; i++)
			{
				unsigned int oi = ni + i;
				unsigned int pi = mi + i;
				if((tolower(string1[oi]) == tolower(string2[mi])) && (oi <= l1))
				{
					ni = oi;
					lcs += (string1[oi] == string2[mi] ? 1.0f : 0.8f);
					break;
				} 
				if((string1[ni] == string2[pi]) && (pi <= l2))
				{ 
					mi = pi;
					lcs += (string1[ni] == string2[pi] ? 1.0f : 0.8f);
					break;
				}
			} 
		}
		ni++;
		mi++;
	} 
	return (((float)l1 + (float)l2)/2.0f - lcs) / (float) max(l1, l2);
}