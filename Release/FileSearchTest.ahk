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
Gui, Add, Edit, w100 vQueryString, .exe
Gui, Add, Button, gButton Default, Search
Gui, Add, Edit, w500 h500 vResults Multi, Index time: %tmIndex% seconds
Gui, Add, Button, gLoad, Load Index
Gui, Add, Button, gSave, Save Index
Gui, Show
GoSub Button
return

Button:
Gui, Submit, NoHide
tmSearch := A_TickCount
if(StrLen(QueryString) > 2)
	results := Query(QueryString)
else
	results := ""
tmSearch := (A_TickCount - tmSearch ) / 1000
GuiControl,, Results, Index time: %tmIndex% seconds`nSearch time: %tmSearch% seconds`n%results%
return

Load:
if(DriveIndex)
	DllCall(DllPath "\DeleteIndex", "PTR", DriveIndex)
tmLoad := A_TickCount
DriveIndex := DllCall(DllPath "\LoadIndexFromDisk", "str", A_ScriptDir "\" Drive "Index.dat", "PTR")
tmLoad := A_TickCount - tmLoad
GuiControl,, Results, Load time: %tmLoad%
return

Save:
tmSave := A_TickCount
Path := A_ScriptDir "\" Drive "Index.dat"
result := DllCall(DllPath "\SaveIndexToDisk", "PTR", DriveIndex, wstr, Path, "UINT")
tmSave := A_TickCount - tmSave
GuiControl,, Results, Save time: %tmSave% %result% e%ErrorLevel%
return

Query(String)
{
	global DriveIndex, DllPath
	pResult := DllCall(DllPath "\Search", "PTR", DriveIndex,  wstr, String, PTR)
	strResult := StrGet(presult + 0)
	;DllCall(DllPath "FreeResultsBuffer", "PTR", pResult)
	return strResult
}

GuiClose:
DllCall(DllPath "\DeleteIndex", "PTR", DriveIndex)
DllCall("FreeLibrary", "PTR", hModule)
ExitApp