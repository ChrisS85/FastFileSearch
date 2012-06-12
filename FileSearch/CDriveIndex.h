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
	wstring Path;
	DWORDLONG Filter;
	SearchResultFile(wstring aPath, DWORDLONG aFilter)
	{
		Path = aPath;
		Filter = aFilter;
	}
};
struct SearchResult
{
	wstring Query;
	vector<SearchResultFile> Results;
};
class CDriveIndex {
public:
	CDriveIndex();
	CDriveIndex(wstring &strPath);
	~CDriveIndex();
	BOOL Init(WCHAR cDrive);
	void Find(wstring *pszQuery, vector<wstring> *rgszResults);
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
	DWORDLONG MakeAddress(wstring *szName);
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



//Exported functions
CDriveIndex* _stdcall CreateIndex(WCHAR Drive);
void _stdcall DeleteIndex(CDriveIndex *di);
WCHAR* _stdcall Search(CDriveIndex *di, WCHAR *szQuery);
void _stdcall FreeResultsBuffer(WCHAR *szResults);
BOOL _stdcall SaveIndexToDisk(CDriveIndex *di, WCHAR *szPath);
CDriveIndex* _stdcall LoadIndexFromDisk(WCHAR *szPath);
void _stdcall GetDriveInfo(CDriveIndex *di, DriveInfo *driveInfo);