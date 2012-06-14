/******************************************************************************
Module name: CDriveIndex.cpp
Written by: Christian Sander
Credits for original code this is based on: Jeffrey Cooperstein & Jeffrey Richter
******************************************************************************/

#pragma once

#include <vector>
#include <string>
#include <Windows.h>
#include <WinIoCtl.h>
#include <stdio.h>
#include <sstream>
using namespace std;

#define NO_WHERE 0
#define IN_FILES 1
#define IN_DIRECTORIES 2
struct IndexedFile
{
	DWORDLONG Index;
	//DWORDLONG ParentIndex;
	DWORDLONG Filter;
	bool operator<(const IndexedFile& i)
	{
		return Index < i.Index;
	}
};
struct USNEntry
{
	DWORDLONG ParentIndex;
	wstring Name;
	USNEntry(wstring aName, DWORDLONG aParentIndex)
	{
		Name = aName;
		ParentIndex = aParentIndex;
	}
};

struct DriveInfo
{
	DWORDLONG NumFiles;
	DWORDLONG NumDirectories;
};

struct SearchResultFile
{
	wstring Filename;
	wstring Path;
	DWORDLONG Filter;
	float MatchQuality;
	SearchResultFile()
	{
	}
	SearchResultFile(const SearchResultFile &srf)
	{
		Filename = srf.Filename;
		Path = srf.Path;
		Filter = srf.Filter;
		MatchQuality = srf.MatchQuality;
	}
	SearchResultFile(wstring aPath, wstring aFilename, DWORDLONG aFilter, float aMatchQuality = 1)
	{
		Filename = aFilename;
		Path = aPath;
		Filter = aFilter;
		MatchQuality = aMatchQuality;
	}
	bool operator<(const SearchResultFile& i)
	{
		return MatchQuality == i.MatchQuality ? Path + Filename < i.Path + i.Filename : MatchQuality > i.MatchQuality;
	}
};
struct SearchResult
{
	wstring Query;
	vector<SearchResultFile> Results;
	int iOffset; //-1 when finished
	unsigned int SearchEndedWhere;
	int maxResults;
};
class CDriveIndex {
public:
	CDriveIndex();
	CDriveIndex(wstring &strPath);
	~CDriveIndex();
	BOOL Init(WCHAR cDrive);
	int Find(wstring *strQuery, wstring *strPath, vector<SearchResultFile> *rgsrfResults, BOOL bSort = true, BOOL bEnhancedSearch = true, int maxResults = -1);
	void FindInJournal(wstring &strQuery, const WCHAR* &szQueryLower, DWORDLONG QueryFilter, DWORDLONG QueryLength, wstring * strQueryPath, vector<IndexedFile> &rgJournalIndex, vector<SearchResultFile> &rgsrfResults, unsigned int  iOffset, BOOL bEnhancedSearch, int maxResults, int &nResults);
	void FindInPreviousResults(wstring &strQuery, const WCHAR* &szQueryLower, DWORDLONG QueryFilter, DWORDLONG QueryLength, wstring * strQueryPath, vector<SearchResultFile> &rgsrfResults, unsigned int  iOffset, BOOL bEnhancedSearch, int maxResults, int &nResults);
	void PopulateIndex();
	BOOL SaveToDisk(wstring &strPath);
	DriveInfo GetInfo();

protected:
	BOOL Empty();
	HANDLE Open(WCHAR cDriveLetter, DWORD dwAccess);
	BOOL Create(DWORDLONG MaximumSize, DWORDLONG AllocationDelta);
	BOOL Query(PUSN_JOURNAL_DATA pUsnJournalData);
	INT64 FindOffsetByIndex(DWORDLONG Index);
	INT64 FindDirOffsetByIndex(DWORDLONG Index);
	DWORDLONG MakeFilter(wstring *szName);
	USNEntry FRNToName(DWORDLONG FRN);
	void CleanUp();
	BOOL Add(DWORDLONG Index, wstring *szName, DWORDLONG ParentIndex, DWORDLONG Address = 0);
	BOOL AddDir(DWORDLONG Index, wstring *szName, DWORDLONG ParentIndex, DWORDLONG Address = 0);
	BOOL Get(DWORDLONG Index, wstring *sz);
	BOOL GetDir(DWORDLONG Index, wstring *sz);
	void ClearLastResult();
	// Members used to enumerate journal records
	HANDLE					m_hVol;			// handle to volume
	WCHAR					m_cDrive;		// drive letter of volume
	DWORDLONG				m_dwDriveFRN;	// drive FileReferenceNumber

	//Database containers
	vector<IndexedFile> rgFiles;
	vector<IndexedFile> rgDirectories;

	SearchResult LastResult;
};
float FuzzySearch(wstring &longer, wstring &shorter);

//Exported functions
CDriveIndex* _stdcall CreateIndex(WCHAR Drive);
void _stdcall DeleteIndex(CDriveIndex *di);
WCHAR* _stdcall Search(CDriveIndex *di, WCHAR *szQuery, WCHAR *szPath, BOOL bSort, BOOL bEnhancedSearch, int maxResults, BOOL *bFoundAll);
void _stdcall FreeResultsBuffer(WCHAR *szResults);
BOOL _stdcall SaveIndexToDisk(CDriveIndex *di, WCHAR *szPath);
CDriveIndex* _stdcall LoadIndexFromDisk(WCHAR *szPath);
void _stdcall GetDriveInfo(CDriveIndex *di, DriveInfo *driveInfo);