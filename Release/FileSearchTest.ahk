#SingleInstance off
if(!A_IsAdmin)
{
	run *runas %A_ScriptFullPath%
	ExitApp
}
Drive := "D"
DllPath := A_ScriptDir "\FileSearch.dll"
hModule := DllCall("LoadLibrary", "Str", DllPath, "PTR")
tmIndex := A_TickCount
DriveIndex := DllCall(DllPath "\CreateIndex", ushort, NumGet(Drive, "ushort"), "PTR")
tmIndex := (A_TickCount - tmIndex) / 1000
VarSetCapacity(DriveInfo, 16, 0)
DllCall(DllPath "\GetDriveInfo", "PTR", DriveIndex, "PTR", &DriveInfo, "UINT")
NumFiles := NumGet(DriveInfo, 0, "uint64")
NumDirectories := NumGet(DriveInfo, 8, "uint64")
Gui, Add, Edit, section w200 gButton vQueryString, .exe
Gui, Add, Text,, In this path:
Gui, Add, Edit, x+10 w200 vQueryPath,
Gui, Add, Button, xs+0 gButton Default, Search
Gui, Add, CheckBox, xs+0 vLimitResults, Limit Results to 1000
Gui, Add, Edit, xs+0 w500 h500 vResults Multi, Index time: %tmIndex% seconds`n%NumFiles% files total, %NumDirectories% directories total
Gui, Add, Button, xs+0 gLoad, Load Index
Gui, Add, Button, xs+0 gSave, Save Index
Gui, Show
GoSub Button
return

Button:
Gui, Submit, NoHide
tmSearch := A_TickCount
SearchString := QueryString
if(StrLen(QueryString) > 2)
	results := Query(QueryString, QueryPath, LimitResults ? 1000 : -1, nResults)
else
{
	results := ""
	nResults := 0
}
tmSearch := (A_TickCount - tmSearch ) / 1000
GuiControl,, Results, Index time: %tmIndex% seconds`n%NumFiles% files total, %NumDirectories% directories total`nSearch time for "%QueryString%": %tmSearch% seconds`n%nResults% Result(s)`n%results%

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

Query(String, QueryPath, LimitResults, ByRef nResults)
{
	global DriveIndex, DllPath
	pResult := DllCall(DllPath "\Search", "PTR", DriveIndex,  "wstr", String, "wstr", QueryPath, "int", true, "int", true, "int", LimitResults, "int*", nResults, PTR)
	strResult := StrGet(presult + 0) (nResults = -1 ? "`nThere were more results..." : "")
	DllCall(DllPath "FreeResultsBuffer", "PTR", pResult)
	return strResult
}

GuiClose:
DllCall(DllPath "\DeleteIndex", "PTR", DriveIndex)
DllCall("FreeLibrary", "PTR", hModule)
ExitApp