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
#include <unordered_map>
#include "tinyxml2.h"

using namespace std;

#define NO_WHERE 0
#define IN_FILES 1
#define IN_DIRECTORIES 2
struct HashMapEntry
{
	DWORDLONG ParentFRN;
	unsigned int iOffset;
};
struct IndexedFile
{
	DWORDLONG Index;
	//DWORDLONG ParentIndex;
	DWORDLONG Filter;
	bool operator<(const IndexedFile& i)
	{
		return Index < i.Index;
	}
	IndexedFile()
	{
		Index = 0;
		Filter = 0;
	}
};
struct IndexedDirectory
{
	DWORDLONG Index;
	//DWORDLONG ParentIndex;
	DWORDLONG Filter;
	unsigned int nFiles;
	bool operator<(const IndexedDirectory& i)
	{
		return Index < i.Index;
	}
	IndexedDirectory()
	{
		Index = 0;
		Filter = 0;
		nFiles = 0;
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
	USNEntry()
	{
		ParentIndex = 0;
		Name = wstring();
	}
};

struct DriveInfo
{
	DWORDLONG NumFiles;
	DWORDLONG NumDirectories;
	DriveInfo()
	{
		NumFiles = 0;
		NumDirectories = 0;
	}
};

struct SearchResultFile
{
	wstring Filename;
	wstring Path;
	DWORDLONG Filter;
	float MatchQuality;
	SearchResultFile()
	{
		Filename = wstring();
		Path = wstring();
		Filter = 0;
		MatchQuality = 0.0f;
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
	wstring SearchPath;
	vector<SearchResultFile> Results;
	int iOffset; //0 when finished
	unsigned int SearchEndedWhere;
	int maxResults;
	SearchResult()
	{
		Query = wstring();
		SearchPath = wstring();
		Results = vector<SearchResultFile>();
		iOffset = 0;
		SearchEndedWhere = NO_WHERE;
		maxResults = -1;
	}
};
class CDriveIndex {
public:
	enum ExportFormat {ExportFormatAdcXml, ExportFormatAdcXml_LZ4};

public:
	CDriveIndex();
	CDriveIndex(wstring &strPath);
	~CDriveIndex();
	BOOL Init(WCHAR cDrive);
	int Find(wstring *strQuery, wstring *strPath, vector<SearchResultFile> *rgsrfResults, BOOL bSort = true, BOOL bEnhancedSearch = true, int maxResults = -1);
	void PopulateIndex();
	BOOL SaveToDisk(wstring &strPath) const;
	BOOL ExportToFileListing(wstring &strPath, int format) const;
	DriveInfo GetInfo() const;

protected:
	BOOL Empty();
	HANDLE Open(WCHAR cDriveLetter, DWORD dwAccess);
	BOOL Create(DWORDLONG MaximumSize, DWORDLONG AllocationDelta);
	BOOL Query(PUSN_JOURNAL_DATA pUsnJournalData) const;
	void FindRecursively(wstring &strQuery, const WCHAR* &szQueryLower, DWORDLONG QueryFilter, DWORDLONG QueryLength, wstring * strQueryPath, vector<SearchResultFile> &rgsrfResults, BOOL bEnhancedSearch, int maxResults, int &nResults);
	template <class T>
	void FindInJournal(wstring &strQuery, const WCHAR* &szQueryLower, DWORDLONG QueryFilter, DWORDLONG QueryLength, wstring * strQueryPath, vector<T> &rgJournalIndex, vector<SearchResultFile> &rgsrfResults, unsigned int  iOffset, BOOL bEnhancedSearch, int maxResults, int &nResults);
	void FindInPreviousResults(wstring &strQuery, const WCHAR* &szQueryLower, DWORDLONG QueryFilter, DWORDLONG QueryLength, wstring * strQueryPath, vector<SearchResultFile> &rgsrfResults, unsigned int  iOffset, BOOL bEnhancedSearch, int maxResults, int &nResults);
	void attach(vector<tinyxml2::XMLHandle> &dirHandles, unordered_map<DWORDLONG, vector<tinyxml2::XMLHandle>::size_type> &umDirFrnToHandle, int NodeType, DWORDLONG Index) const;
	
	INT64 FindOffsetByIndex(DWORDLONG Index);
	INT64 FindDirOffsetByIndex(DWORDLONG Index);
	DWORDLONG MakeFilter(wstring *szName);
	USNEntry FRNToName(DWORDLONG FRN) const;
	void CleanUp();
	BOOL Add(DWORDLONG Index, wstring *szName, DWORDLONG ParentIndex, DWORDLONG Address = 0);
	BOOL AddDir(DWORDLONG Index, wstring *szName, DWORDLONG ParentIndex, DWORDLONG Address = 0);
	BOOL Get(DWORDLONG Index, wstring *sz) const;
	BOOL GetDir(DWORDLONG Index, wstring *sz) const;
	//unsigned int GetParentDirectory(DWORDLONG Index);
	void ClearLastResult();
	// Members used to enumerate journal records
	HANDLE					m_hVol;			// handle to volume
	WCHAR					m_cDrive;		// drive letter of volume
	DWORDLONG				m_dwDriveFRN;	// drive FileReferenceNumber

	//Database containers
	vector<IndexedFile> rgFiles;
	vector<IndexedDirectory> rgDirectories;
	SearchResult LastResult;
};
float FuzzySearch(wstring &longer, wstring &shorter);
DWORDLONG PathToFRN(wstring* strPath);

//Exported functions
CDriveIndex* _stdcall CreateIndex(WCHAR Drive);
void _stdcall DeleteIndex(CDriveIndex *di);
WCHAR* _stdcall Search(CDriveIndex *di, WCHAR *szQuery, WCHAR *szPath, BOOL bSort, BOOL bEnhancedSearch, int maxResults, BOOL *bFoundAll);
void _stdcall FreeResultsBuffer(WCHAR *szResults);
BOOL _stdcall SaveIndexToDisk(CDriveIndex *di, WCHAR *szPath);
CDriveIndex* _stdcall LoadIndexFromDisk(WCHAR *szPath);
void _stdcall GetDriveInfo(CDriveIndex *di, DriveInfo *driveInfo);
BOOL _stdcall SaveIndexToDisk(CDriveIndex *di, WCHAR *szPath);
BOOL _stdcall ExportIndex(CDriveIndex *di, WCHAR *szPath, int format);
