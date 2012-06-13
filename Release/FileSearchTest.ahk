#SingleInstance off
if(!A_IsAdmin)
{
	run *runas %A_ScriptFullPath%
	ExitApp
}
Drive := "C"
DllPath := A_ScriptDir "\FileSearch.dll"
hModule := DllCall("LoadLibrary", "Str", DllPath, "PTR")
tmIndex := A_TickCount
DriveIndex := DllCall(DllPath "\CreateIndex", ushort, NumGet(Drive, "ushort"), "PTR")
tmIndex := (A_TickCount - tmIndex) / 1000
VarSetCapacity(DriveInfo, 16, 0)
DllCall(DllPath "\GetDriveInfo", "PTR", DriveIndex, "PTR", &DriveInfo, "UINT")
NumFiles := NumGet(DriveInfo, 0, "uint64")
NumDirectories := NumGet(DriveInfo, 8, "uint64")
Gui, Add, Edit, w100 gButton vQueryString, .exe
Gui, Add, Button, gButton Default, Search
Gui, Add, CheckBox, vLimitResults, Limit Results to 1000
Gui, Add, Edit, w500 h500 vResults Multi, Index time: %tmIndex% seconds`n%NumFiles% files total, %NumDirectories% directories total
Gui, Add, Button, gLoad, Load Index
Gui, Add, Button, gSave, Save Index
Gui, Show
GoSub Button
return

Button:
Gui, Submit, NoHide
tmSearch := A_TickCount
SearchString := QueryString
if(StrLen(QueryString) > 2)
	results := Query(QueryString, LimitResults ? 1000 : -1)
else
	results := ""
tmSearch := (A_TickCount - tmSearch ) / 1000
GuiControl,, Results, Index time: %tmIndex% seconds`n%NumFiles% files total, %NumDirectories% directories total`nSearch time for "%QueryString%": %tmSearch% seconds`n%results%

;It may happen that the search takes long enough to swallow this g-label notification while typing. To fix this we simply run it again if the query string was changed.
Gui, Submit, NoHide
if(SearchString != QueryString)
	GoSub Button
return

Load:
if(DriveIndex)
	DllCall(DllPath "\DeleteIndex", "PTR", DriveIndex)
tmLoad := A_TickCount
DriveIndex := DllCall(DllPath "\LoadIndexFromDisk", "str", A_ScriptDir "\" Drive "Index.dat", "PTR")
tmLoad := A_TickCount - tmLoad
DllCall(DllPath "\GetDriveInfo", "PTR", DriveIndex, "PTR", &DriveInfo, "UINT")
NumFiles := NumGet(DriveInfo, 0, "uint64")
NumDirectories := NumGet(DriveInfo, 8, "uint64")
GuiControl,, Results, Load time: %tmLoad% seconds`n%NumFiles% files total, %NumDirectories% directories total
return

Save:
tmSave := A_TickCount
Path := A_ScriptDir "\" Drive "Index.dat"
result := DllCall(DllPath "\SaveIndexToDisk", "PTR", DriveIndex, wstr, Path, "UINT")
tmSave := A_TickCount - tmSave
GuiControl,, Results, Save time: %tmSave% seconds`n%NumFiles% files total, %NumDirectories% directories total
return

Query(String, LimitResults)
{
	global DriveIndex, DllPath
	pResult := DllCall(DllPath "\Search", "PTR", DriveIndex,  "wstr", String, "int", true, "int", true, "int", LimitResults, "int*", nResults, PTR)
	strResult := StrGet(presult + 0) (nResults = -1 ? "`nThere were more results..." : "")
	;DllCall(DllPath "FreeResultsBuffer", "PTR", pResult)
	return strResult
}

GuiClose:
DllCall(DllPath "\DeleteIndex", "PTR", DriveIndex)
DllCall("FreeLibrary", "PTR", hModule)
ExitApp