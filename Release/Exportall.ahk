#SingleInstance off
#NoEnv
#Warn
FileEncoding, UTF-8-RAW

; Name of function in DLL to export a drive's index
DllExportFuncname := "ExportIndex"

RunAsAdmin() {
	global
	params :=
	Loop, %0%  ; For each parameter:
		params .= A_Space . %A_Index%
	local ShellExecute
	ShellExecute := A_IsUnicode ? "shell32\ShellExecute":"shell32\ShellExecuteA"
	if not A_IsAdmin
	{
		A_IsCompiled
		  ? DllCall(ShellExecute, uint, 0, str, "RunAs", str, A_ScriptFullPath, str, params , str, A_WorkingDir, int, 1)
		  : DllCall(ShellExecute, uint, 0, str, "RunAs", str, A_AhkPath, str, """" . A_ScriptFullPath . """" . A_Space . params, str, A_WorkingDir, int, 1)
		ExitApp
	}
}

ToHex(num)
{
	if num is not integer
		return num
	oldFmt := A_FormatInteger
	SetFormat, integer, hex
	num := num + 0
	SetFormat, integer,% oldFmt
	return num
}
;returns positive hex value of last error
GetLastError()
{
	return ToHex(A_LastError < 0 ? A_LastError & 0xFFFFFFFF : A_LastError)
}

ErrorFormat(error_id)
{
	VarSetCapacity(msg,500+500*A_IsUnicode,0)
	if !len := DllCall("FormatMessage"
				,"UInt",FORMAT_MESSAGE_FROM_SYSTEM := 0x00001000 | FORMAT_MESSAGE_IGNORE_INSERTS := 0x00000200		;dwflags
				,"Ptr",0		;lpSource
				,"UInt",error_id	;dwMessageId
				,"UInt",0			;dwLanguageId
				,"Ptr",&msg			;lpBuffer
				,"UInt",500)			;nSize
		return
	return 	strget(&msg,len)
}

ExitWithMessage(msg) {
	MsgBox, 16, % "[Autohotkey message, will auto-close in 30 seconds]", % msg, 30
	ExitApp
}

Class NtfsFastProc {
	Description := "Quickly process NTFS filesystem metadata on a local disk."
	LoggedInitError := ""

	Class ErrorModeHelper {
		__New() {
			; prevent error mode dialogs from hanging the application
			this.oldErrMode := DllCall("SetErrorMode", "UInt", SEM_FAILCRITICALERRORS := 0x0001)
		}
		__Delete() {
			DllCall("SetErrorMode", "UInt", this.oldErrMode)
		}
	}

	__New(providerDllPath) {
		global DllExportFuncname
		this.DllPath := providerDllPath
		foo := new NtfsFastProc.ErrorModeHelper
		this.hModule := DllCall("LoadLibrary", "Str", providerDllPath, "PTR")
		if !this.hModule
		{
			this.LoggedInitError := "Failed to load provider dll: " providerDllPath "`n`nMake sure it is a valid executable and that all runtime dependencies are satisfied.`n`nMessage: " ErrorFormat(GetLastError())
			return this
		}
		this.QueryProc := DllCall("GetProcAddress", "PTR", this.hModule, "AStr", DllExportFuncname, "PTR")
		if !this.QueryProc
		{
			this.LoggedInitError := "Could not find a '" DllExportFuncname "' export symbol in provider dll: " providerDllPath "`n`nIt may not be a supported dll or the file is corrupt.`n`nMessage: " ErrorFormat(GetLastError())
			return this
		}
	}

	__Delete() {
		if this.hModule
		{
			DllCall("FreeLibrary", "PTR", this.hModule)
			this.hModule := 0
		}
	}

	BuildFileDatabase(LoadExisting = true) {
		NTFSDrives := this.GetIndexingDrives()
		for index, Drive in NTFSDrives
		{
			ExportPath := A_ScriptDir "\" Drive "_export.xmlz4"
			DriveIndex := DllCall(this.DllPath "\CreateIndex", "ushort", NumGet(Drive, "ushort"), "PTR")
			if (DriveIndex)
			{
				hSrResult := DllCall(this.QueryProc, "PTR", DriveIndex, "wstr", ExportPath, "int", ExportFormatAdcXml_LZ4 := 1, PTR)
				SoundPlay, *64
			}
		}
	}

	GetIndexingDrives() {
		return ["J"]
	}
}




RunAsAdmin()

DllPath := A_ScriptDir "\FileSearch.dll"

fastProc := new NtfsFastProc(DllPath)
fastProc.LoggedInitError <> "" ? ExitWithMessage(fastProc.loggedInitError) :

fastProc.BuildFileDatabase()


