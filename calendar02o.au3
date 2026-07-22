; * ============================================================================
; * CSV Calendar with Automated Network Sync
; *
; * THIS WORK IS NOT FIT FOR ANY FUNCTION OR PURPOSE, COMES WITH NO WARRANTY,
; * AND IS BEING RELEASED INTO THE PUBLIC DOMAIN.
; *
; * ============================================================================ 
#include <GUIConstantsEx.au3>
#include <WindowsConstants.au3>
#include <ScrollBarConstants.au3>
#include <FontConstants.au3>
#include <EditConstants.au3>
#include <ComboConstants.au3>
#include <WinAPIConstants.au3>
#include <WinAPIGdi.au3>
#include <WinAPIGdiDC.au3>
#include <WinAPISysWin.au3>
#include <WinAPIMisc.au3>
#include <Array.au3>
#include <Date.au3>
#include <Misc.au3>
#include <WinAPIRes.au3>
#include <FileConstants.au3>
#include <MsgBoxConstants.au3>
#include <WinAPIFiles.au3>
#include <WinAPIDlg.au3>
#include <File.au3>

Opt("GUIOnEventMode", 0)
Opt("MouseCoordMode", 2) ; Client coordinates
Opt("TCPTimeout", 100)   ; Aggressive TCP timeout to prevent UI/script hanging

Global $fTimezoneOffset = 0.0 ; Adjusts visual event placement by hours

; --- Zoom & Canvas Configuration ---
Global $fZoom = 1.0           ; 1.0 = 1 pixel per minute
Global $iCanvasWidth  = 1100  ; Minimum horizontal width
Global $iCanvasHeight = 1440  ; 24 hours * 60 minutes * Zoom
Global Const $iTimeColWidth = 65
Global Const $iHeaderH    = 50 ; Height reserved at top of main window for native buttons
Global Const $iSubHeaderH = 30 ; Height reserved at top of canvas for column titles

Global $iScrollX = 0, $iScrollY = 300
Global $iClientW = 1050, $iClientH = 700
Global $hCanvas = 0  ; Initialize canvas handle to prevent issues in handlers

; --- Network & Sync Configuration Variables ---
Global $iNetPort = 9876
Global $iNetSyncIntervalMs = 180000
Global $iNetLogging = 0
Global $iNetDeleteThreshold = 100
Global $aServers[8] = [0, "0", "0", "0", "0", "0", "0", "0"] ; Index 0 is unused, 1-7 for Server1-Server7

; --- View & Date State ---
; ViewMode: 1 = 1 Day, 2 = 4 Day, 3 = Week, 4 = 4 Person, 5 = 7 Person, 6 = Month, 7 = Upcoming Schedule
Global $iViewMode = 1 
Global $sCurrentDate = _NowCalcDate() ; YYYY/MM/DD format
Global $aPeople[] = ["Alice", "Bob", "Charlie", "David", "Eve", "Frank", "Grace"]

; --- Event Data Structure: [ID, Title, StartMin, DurationMin, RGBColor, DateStr, PersonIdx, Version, LastModifiedBy] ---
Global $aEvents[0][9]

; --- File I/O Configuration ---
Local $sBaseName = StringRegExpReplace(@ScriptName, "\.[^.]*$", "")
Global $sCSVFile = @ScriptDir & "\" & $sBaseName & ".csv"
Global $sINIFile = @ScriptDir & "\" & $sBaseName & ".ini"
Global $sLogFile = @ScriptDir & "\" & $sBaseName & ".log"

; --- Automatically create default INI file if it doesn't exist ---
If Not FileExists($sINIFile) Then
	IniWrite($sINIFile, "Window", "Node", "1")
    IniWrite($sINIFile, "Network", "Port", "9876")
    IniWrite($sINIFile, "Network", "SyncIntervalMs", "180000") ; Default: 3 minutes (180000 ms)
    IniWrite($sINIFile, "Network", "Logging", "0") ; 0 = Off, 1 = On
    IniWrite($sINIFile, "Network", "DeleteThreshold", "100") ; Default: 100 sync cycles
    For $i = 1 To 7
        IniWrite($sINIFile, "Servers", "Server" & $i, "0")
    Next
EndIf

; Load settings and people from INI file
_LoadINI()
_LoadCSV()

; --- Sync Tracking Variables ---
Global $iSyncCycles = 0 ; Track number of sync cycles
Global $sIPs = GetIPAddresses()
; --- Automatically determine this node's ID based on matching local IP to the server list ---
Global $iServerNum = 1 ; If IP address is 0 or no match is found, assume 1
For $i = 1 To 7
    If $aServers[$i] <> "0" And  StringInStr($aServers[$i],$sIPs) Then
        $iServerNum = $i
		if IniRead($sINIFile, "Window", "Node","1")<>$iServerNum Then IniWrite($sINIFile, "Window", "Node", $iServerNum)
        ExitLoop
    EndIf
Next
Global $iMyNodeID = $iServerNum

TCPStartup()

; --- Server Setup (Listens on all local interfaces, allows backlog of 7 clients) ---
Global $hServerListen = TCPListen("0.0.0.0", $iNetPort, 7)
If $hServerListen = -1 Then
    _Log("Failed to start TCP Server listener on port " & $iNetPort)
EndIf

_Log("Service started. Node ID: " & $iMyNodeID & " ¦ Listening on port: " & $iNetPort)

; --- Client Timer Setup ---
Global $iSyncTimer = TimerInit()

; --- Interaction State Variables ---
Global $iDragMode = 0       ; 0 = None, 1 = Move/Copy, 2 = Resize Top, 3 = Resize Bottom
Global $iDragIndex = -1
Global $iDragOffsetY = 0
Global $iOrigStart = 0, $iOrigDuration = 0
Global $bCopyTriggered = False ; Prevents multiple duplicates during a single Ctrl+Drag
Global $iEditingIndex = -1     ; Track index of event currently being text-edited
Global $iSelectedForDelete = -1 ; Track last selected event for deletion

; ==============================================================================
; CREATE MAIN GUI & NATIVE CONTROLS
; ==============================================================================
Local $iWinX = Int(IniRead($sINIFile, "Window", "X", -1))
Local $iWinY = Int(IniRead($sINIFile, "Window", "Y", -1))

Global $hMainGUI = GUICreate("CSV Calendar", $iClientW, $iClientH, $iWinX, $iWinY, _
        BitOR($WS_OVERLAPPEDWINDOW, $WS_CLIPCHILDREN), $WS_EX_COMPOSITED)

; Zoom Buttons
Global $idBtnZoomIn  = GUICtrlCreateButton("+", 10, 12, 28, 26)
Global $idBtnZoomOut = GUICtrlCreateButton("-", 42, 12, 28, 26)
GUICtrlSetFont($idBtnZoomIn, 11, 800)
GUICtrlSetFont($idBtnZoomOut, 11, 800)

; Navigation Buttons (View Span)
Global $idBtnPrev = GUICtrlCreateButton("< Prev", 80, 12, 60, 26)
Global $idBtnNext = GUICtrlCreateButton("Next >", 145, 12, 60, 26)

; Navigation Buttons (Single Day increment)
Global $idBtnPrevDay = GUICtrlCreateButton("< Day", 215, 12, 50, 26)
Global $idBtnNextDay = GUICtrlCreateButton("Day >", 270, 12, 50, 26)

; Print & Export Buttons
Global $idBtnPrint  = GUICtrlCreateButton("Print", 330, 12, 55, 26)
Global $idBtnExport = GUICtrlCreateButton("Export", 390, 12, 55, 26)

; View Selection Dropdown
Global $idComboView = GUICtrlCreateCombo("", 455, 13, 140, 25, $CBS_DROPDOWNLIST)
GUICtrlSetData($idComboView, "1 Day View|4 Day View|Week View|4 Person View|7 Person View|Month View|Upcoming Schedule", "1 Day View")

; Delete Button
Global $idBtnDelete = GUICtrlCreateButton("Delete", 605, 12, 55, 26)

; Prominent Date Title Label
Global $idLblDateTitle = GUICtrlCreateLabel("", 670, 10, 365, 32)
GUICtrlSetFont($idLblDateTitle, 15, 700, 0, "Segoe UI")
GUICtrlSetColor($idLblDateTitle, 0x202124)

; ==============================================================================
; DEDICATED CHILD CANVAS GUI
; ==============================================================================
Global $hCanvas = GUICreate("", $iClientW, $iClientH - $iHeaderH, 0, $iHeaderH, _
        BitOR($WS_CHILD, $WS_VISIBLE, $WS_VSCROLL, $WS_HSCROLL), 0, $hMainGUI)

; In-Place Multiline Text Editor Overlay
Global $idInPlaceEdit = GUICtrlCreateEdit("", -500, -500, 100, 100, _
        BitOR($ES_MULTILINE, $ES_WANTRETURN, $ES_AUTOVSCROLL, $WS_BORDER))
GUICtrlSetState($idInPlaceEdit, $GUI_HIDE)
GUICtrlSetFont($idInPlaceEdit, 10, 600, 0, "Segoe UI")
GUICtrlSetBkColor($idInPlaceEdit, 0xFFFFFF)

; --- Register Win32 Message Handlers ---
GUIRegisterMsg($WM_PAINT, "WM_PAINT")
GUIRegisterMsg($WM_SIZE, "WM_SIZE")
GUIRegisterMsg($WM_ERASEBKGND, "WM_ERASEBKGND")
GUIRegisterMsg($WM_VSCROLL, "WM_VSCROLL")
GUIRegisterMsg($WM_HSCROLL, "WM_HSCROLL")
GUIRegisterMsg($WM_MOUSEWHEEL, "WM_MOUSEWHEEL")
GUIRegisterMsg($WM_LBUTTONDOWN, "WM_LBUTTONDOWN")
GUIRegisterMsg($WM_LBUTTONUP, "WM_LBUTTONUP")
GUIRegisterMsg($WM_MOUSEMOVE, "WM_MOUSEMOVE")
GUIRegisterMsg($WM_LBUTTONDBLCLK, "WM_LBUTTONDBLCLK")

_UpdateDateTitle()
_UpdateScrollBars()
GUISetState(@SW_SHOW, $hMainGUI)

; Force initial redraw after GUI is shown to ensure proper client dimensions
$iClientW = _WinAPI_GetWindowWidth($hMainGUI)
$iClientH = _WinAPI_GetWindowHeight($hMainGUI)
_WinAPI_InvalidateRect($hCanvas, 0, True)

; --- Main Event Loop ---
While 1
    ; --- Handle Inbound Server Connections ---
    If $hServerListen <> -1 Then
        Local $hClient = TCPAccept($hServerListen)
        If $hClient <> -1 Then
            _Log("Server: Incoming connection accepted.")
            _HandleServerClient($hClient, $sCSVFile)
        EndIf
    EndIf

    ; --- Handle Client Periodic Sync (Sequentially checks all specified servers) ---
    If TimerDiff($iSyncTimer) >= $iNetSyncIntervalMs Then
        $iSyncTimer = TimerInit() ; Reset timer
        _CloseInPlaceEdit(True)   ; Save any ongoing edits before background sync
        If _RunAllClientSyncs($sINIFile, $iNetPort, $sCSVFile, $iMyNodeID) Then
            ReDim $aEvents[0][9]
            _LoadCSV()
            _UpdateScrollBars()
            _WinAPI_InvalidateRect($hMainGUI, 0, True)
            _WinAPI_InvalidateRect($hCanvas, 0, True)
            _WinAPI_RedrawWindow($hCanvas, 0, 0, BitOR($RDW_INVALIDATE, $RDW_UPDATENOW, $RDW_ERASE, $RDW_ALLCHILDREN))
        EndIf
        
        ; --- Check Delete Threshold ---
        $iSyncCycles += 1
        If $iSyncCycles >= $iNetDeleteThreshold Then
            If _ProcessDeleteThreshold($sCSVFile) Then
                ReDim $aEvents[0][9]
                _LoadCSV()
                _UpdateScrollBars()
                _WinAPI_InvalidateRect($hCanvas, 0, True)
            EndIf
            $iSyncCycles = 0 ; Reset counter after threshold check
        EndIf
    EndIf

    Switch GUIGetMsg()
        Case $GUI_EVENT_CLOSE
            _SaveINI()
            TCPShutdown()
            Exit

        Case $idBtnZoomIn
            _SetZoom($fZoom + 0.2)

        Case $idBtnZoomOut
            _SetZoom($fZoom - 0.2)
            
        Case $idBtnDelete
            If $iSelectedForDelete <> -1 And $iSelectedForDelete < UBound($aEvents) Then
                $aEvents[$iSelectedForDelete][4] = 2 ; Color 2 = Deleted mark
                _MarkEventModified($iSelectedForDelete)
                If $iEditingIndex == $iSelectedForDelete Then _CloseInPlaceEdit(False)
                $iSelectedForDelete = -1
                _SaveCSV()
                _UpdateScrollBars()
                _WinAPI_InvalidateRect($hCanvas, 0, True)
            EndIf

        Case $idBtnPrevDay
            $iSelectedForDelete = -1
            _CloseInPlaceEdit(True)
            $sCurrentDate = _DateAdd('d', -1, $sCurrentDate)
            _UpdateDateTitle()
            _WinAPI_InvalidateRect($hMainGUI, 0, True)
            _WinAPI_InvalidateRect($hCanvas, 0, True)
            _WinAPI_RedrawWindow($hCanvas, 0, 0, BitOR($RDW_INVALIDATE, $RDW_UPDATENOW, $RDW_ERASE, $RDW_ALLCHILDREN))
  

        Case $idBtnNextDay
            $iSelectedForDelete = -1
            _CloseInPlaceEdit(True)
            $sCurrentDate = _DateAdd('d', 1, $sCurrentDate)
            _UpdateDateTitle()
            _WinAPI_InvalidateRect($hMainGUI, 0, True)
            _WinAPI_InvalidateRect($hCanvas, 0, True)
            _WinAPI_RedrawWindow($hCanvas, 0, 0, BitOR($RDW_INVALIDATE, $RDW_UPDATENOW, $RDW_ERASE, $RDW_ALLCHILDREN))
  

        Case $idBtnPrev
            $iSelectedForDelete = -1
            _CloseInPlaceEdit(True)
            Switch $iViewMode
                Case 1, 4, 5
                    $sCurrentDate = _DateAdd('d', -1, $sCurrentDate)
                Case 2
                    $sCurrentDate = _DateAdd('d', -4, $sCurrentDate)
                Case 3
                    $sCurrentDate = _DateAdd('d', -7, $sCurrentDate)
                Case 6, 7
                    $sCurrentDate = _DateAdd('M', -1, $sCurrentDate)
            EndSwitch
            _UpdateDateTitle()
            _UpdateScrollBars()
            _WinAPI_InvalidateRect($hMainGUI, 0, True)
            _WinAPI_InvalidateRect($hCanvas, 0, True)
            _WinAPI_RedrawWindow($hCanvas, 0, 0, BitOR($RDW_INVALIDATE, $RDW_UPDATENOW, $RDW_ERASE, $RDW_ALLCHILDREN))
  

        Case $idBtnNext
            $iSelectedForDelete = -1
            _CloseInPlaceEdit(True)
            Switch $iViewMode
                Case 1, 4, 5
                    $sCurrentDate = _DateAdd('d', 1, $sCurrentDate)
                Case 2
                    $sCurrentDate = _DateAdd('d', 4, $sCurrentDate)
                Case 3
                    $sCurrentDate = _DateAdd('d', 7, $sCurrentDate)
                Case 6, 7
                    $sCurrentDate = _DateAdd('M', 1, $sCurrentDate)
            EndSwitch
            _UpdateDateTitle()
            _UpdateScrollBars()
            _WinAPI_InvalidateRect($hMainGUI, 0, True)
            _WinAPI_InvalidateRect($hCanvas, 0, True)
            _WinAPI_RedrawWindow($hCanvas, 0, 0, BitOR($RDW_INVALIDATE, $RDW_UPDATENOW, $RDW_ERASE, $RDW_ALLCHILDREN))
  
            
        Case $idBtnPrint
            _PrintSchedule()

        Case $idBtnExport
            _ExportUpcomingSchedule()

        Case $idComboView
            $iSelectedForDelete = -1
            _CloseInPlaceEdit(True)
            Local $sSel = GUICtrlRead($idComboView)
            Switch $sSel
                Case "1 Day View"
                    $iViewMode = 1
                Case "4 Day View"
                    $iViewMode = 2
                Case "Week View"
                    $iViewMode = 3
                Case "4 Person View"
                    $iViewMode = 4
                Case "7 Person View"
                    $iViewMode = 5
                Case "Month View"
                    $iViewMode = 6
                Case "Upcoming Schedule"
                    $iViewMode = 7
            EndSwitch
			sleep(100)
            _UpdateDateTitle()
            _UpdateScrollBars()
            _WinAPI_InvalidateRect($hMainGUI, 0, True)
            _WinAPI_InvalidateRect($hCanvas, 0, True)
            _WinAPI_RedrawWindow($hCanvas, 0, 0, BitOR($RDW_INVALIDATE, $RDW_UPDATENOW, $RDW_ERASE, $RDW_ALLCHILDREN))
            GUICtrlSetState($idLblDateTitle, $GUI_FOCUS)
    EndSwitch
WEnd

; ==============================================================================
; EVENT DATA HELPERS
; ==============================================================================
Func _GDI_CreatePen($iStyle, $iWidth, $iRGB)
    Return _WinAPI_CreatePen($iStyle, $iWidth, _RGB2BGR($iRGB))
EndFunc

Func _RGB2BGR($iColor)
    Return BitOR(BitShift(BitAND($iColor, 0x0000FF), -16), BitAND($iColor, 0x00FF00), BitShift(BitAND($iColor, 0xFF0000), 16))
EndFunc

Func _GDI_RoundRect($hDC, $iLeft, $iTop, $iRight, $iBottom, $iWidth, $iHeight)
    Return DllCall("gdi32.dll", "bool", "RoundRect", "handle", $hDC, "int", $iLeft, "int", $iTop, "int", $iRight, "int", $iBottom, "int", $iWidth, "int", $iHeight)[0]
EndFunc

Func _GDI_Ellipse($hDC, $iLeft, $iTop, $iRight, $iBottom)
    Return DllCall("gdi32.dll", "bool", "Ellipse", "handle", $hDC, "int", $iLeft, "int", $iTop, "int", $iRight, "int", $iBottom)[0]
EndFunc

Func _GDI_CreateSolidBrush($iRGB)
    Return _WinAPI_CreateSolidBrush(_RGB2BGR($iRGB))
EndFunc

Func _GDI_SetTextColor($hDC, $iRGB)
    Return _WinAPI_SetTextColor($hDC, _RGB2BGR($iRGB))
EndFunc

Func _AddEvent($sTitle, $iStartMin, $iDuration, $iColor, $sDate, $iPersonIdx, $sID = "", $iVersion = 1, $iLastModifiedBy = -1)
    If $sID == "" Then
        Local $iEpoch = _DateDiff('s', "1970/01/01 00:00:00", _NowCalc())
        $sID = $iEpoch & StringFormat("%04d", Random(0, 9999, 1))
    EndIf
    If $iLastModifiedBy == -1 Then $iLastModifiedBy = $iServerNum

    Local $iRows = UBound($aEvents)
    ReDim $aEvents[$iRows + 1][9]
    $aEvents[$iRows][0] = $sID
    $aEvents[$iRows][1] = $sTitle
    $aEvents[$iRows][2] = $iStartMin
    $aEvents[$iRows][3] = $iDuration
    $aEvents[$iRows][4] = $iColor
    $aEvents[$iRows][5] = $sDate
    $aEvents[$iRows][6] = $iPersonIdx
    $aEvents[$iRows][7] = $iVersion
    $aEvents[$iRows][8] = $iLastModifiedBy
    Return $iRows
EndFunc

Func _MarkEventModified($iIdx)
    If $iIdx >= 0 And $iIdx < UBound($aEvents) Then
        $aEvents[$iIdx][7] = Int($aEvents[$iIdx][7]) + 1
        $aEvents[$iIdx][8] = $iServerNum
    EndIf
EndFunc

; ==============================================================================
; FILE I/O MANAGEMENT (CSV, INI, NETWORK SYNC & EXPORT)
; ==============================================================================
Func _LoadINI()
    $iClientW = Int(IniRead($sINIFile, "Window", "Width", 1050))
    $iClientH = Int(IniRead($sINIFile, "Window", "Height", 700))
  $fTimezoneOffset = Number(IniRead($sINIFile, "Settings", "TimezoneOffset", "0.0")) ; Added Timezone Offset
    
    ; Load Network Settings
    $iNetPort = Int(IniRead($sINIFile, "Network", "Port", 9876))
    $iNetSyncIntervalMs = Int(IniRead($sINIFile, "Network", "SyncIntervalMs", 180000))
    $iNetLogging = Int(IniRead($sINIFile, "Network", "Logging", 0))
    $iNetDeleteThreshold = Int(IniRead($sINIFile, "Network", "DeleteThreshold", 100))
    
    ; Load Server Settings
    For $i = 1 To 7
        $aServers[$i] = IniRead($sINIFile, "Servers", "Server" & $i, "0")
    Next
    
    Local $sPeopleStr = IniRead($sINIFile, "People", "Names", "")
    If $sPeopleStr <> "" Then
        Local $aTmp = StringSplit($sPeopleStr, "¦", 2)
        If UBound($aTmp) > 0 Then
            ReDim $aPeople[UBound($aTmp)]
            For $i = 0 To UBound($aTmp) - 1
                $aPeople[$i] = StringStripWS($aTmp[$i], 3)
            Next
        EndIf
    EndIf
EndFunc

Func _SaveINI()
    Local $aPos = WinGetPos($hMainGUI)
    Local $aClient = WinGetClientSize($hMainGUI)
    If IsArray($aPos) And IsArray($aClient) Then
        IniWrite($sINIFile, "Window", "X", $aPos[0])
        IniWrite($sINIFile, "Window", "Y", $aPos[1])
        IniWrite($sINIFile, "Window", "Width", $aClient[0])
        IniWrite($sINIFile, "Window", "Height", $aClient[1])
    EndIf

    IniWrite($sINIFile, "Settings", "TimezoneOffset", $fTimezoneOffset) ; Save Timezone Offset

    ; Save Network Settings
    IniWrite($sINIFile, "Network", "Port", $iNetPort)
    IniWrite($sINIFile, "Network", "SyncIntervalMs", $iNetSyncIntervalMs)
    IniWrite($sINIFile, "Network", "Logging", $iNetLogging)
    IniWrite($sINIFile, "Network", "DeleteThreshold", $iNetDeleteThreshold)
    
    ; Save Server Settings
    For $i = 1 To 7
        IniWrite($sINIFile, "Servers", "Server" & $i, $aServers[$i])
    Next

    Local $sPeopleStr = ""
    For $i = 0 To UBound($aPeople) - 1
        $sPeopleStr &= $aPeople[$i] & ($i < UBound($aPeople) - 1 ? "¦" : "")
    Next
    IniWrite($sINIFile, "People", "Names", $sPeopleStr)
EndFunc

Func _LoadCSV()
    If Not FileExists($sCSVFile) Then Return
    Local $aLines = FileReadToArray($sCSVFile)
    If @error Then Return
    For $i = 1 To UBound($aLines) - 1 ; Skip header
        Local $aParts = StringSplit($aLines[$i], "¦", 2)
        If UBound($aParts) >= 9 Then
            Local $sTitle = StringReplace($aParts[1], "%2C", "¦")
            $sTitle = StringReplace($sTitle, "%0A", @CRLF)
            _AddEvent($sTitle, Int($aParts[2]), Int($aParts[3]), Int($aParts[4]), $aParts[5], Int($aParts[6]), $aParts[0], Int($aParts[7]), Int($aParts[8]))
        ElseIf UBound($aParts) >= 6 Then ; Legacy format fallback
            Local $sTitle = StringReplace($aParts[0], "%2C", "¦")
            $sTitle = StringReplace($sTitle, "%0A", @CRLF)
            _AddEvent($sTitle, Int($aParts[1]), Int($aParts[2]), Int($aParts[3]), $aParts[4], Int($aParts[5]))
        EndIf
    Next
EndFunc

Func _SaveCSV()
    Local $hFile = FileOpen($sCSVFile, 2) ; Overwrite mode
    FileWriteLine($hFile, "ID,Title,StartMin,Duration,Color,Date,PersonIdx,Version,LastModifiedBy")
    For $i = 0 To UBound($aEvents) - 1
        Local $sTitle = StringReplace($aEvents[$i][1], "¦", "%2C")
        $sTitle = StringReplace($sTitle, @CRLF, "%0A")
        $sTitle = StringReplace($sTitle, @CR, "%0A")
        $sTitle = StringReplace($sTitle, @LF, "%0A")
        FileWriteLine($hFile, $aEvents[$i][0] & "¦" & $sTitle & "¦" & $aEvents[$i][2] & "¦" & $aEvents[$i][3] & "¦" & $aEvents[$i][4] & "¦" & $aEvents[$i][5] & "¦" & $aEvents[$i][6] & "¦" & $aEvents[$i][7] & "¦" & $aEvents[$i][8])
    Next
    FileClose($hFile)
EndFunc

Func _Log($sMessage)
    If $iNetLogging <> 1 Then Return
    Local $hFile = FileOpen($sLogFile, 1) ; 1 = Append mode
    If $hFile <> -1 Then
        Local $sTimestamp = @YEAR & "-" & @MON & "-" & @MDAY & " " & @HOUR & ":" & @MIN & ":" & @SEC
        FileWriteLine($hFile, "[" & $sTimestamp & "] " & $sMessage)
        FileClose($hFile)
    EndIf
EndFunc

Func _IsIP($sIP)
    Return StringRegExp($sIP, '^\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3}$', 0)
EndFunc

;Func _IsLocalIP($sIP)
 ;   If $sIP = "127.0.0.1" Or $sIP = "localhost" Then Return True
 ;   If $sIP = @IPAddress1 Or $sIP = @IPAddress2 Or $sIP = @IPAddress3 Or $sIP = @IPAddress4 Then Return True
 ;   Return False
;EndFunc

Func _HandleServerClient($hClient, $sCsvFile)
    ; Receive client sync payload with a 10-second inactivity timeout and [EOF] marker check
    Local $sData = ""
    Local $hTimer = TimerInit()
    Do
        Local $sChunk = TCPRecv($hClient, 4096)
        If @error Then
            _Log("Server: Connection lost while receiving data.")
            ExitLoop
        EndIf
        
        If $sChunk <> "" Then
            $sData &= $sChunk
            $hTimer = TimerInit() ; Reset inactivity timer on data receipt
        EndIf
        
        If TimerDiff($hTimer) > 10000 Then ; 10 seconds of inactivity
            _Log("Server: Disconnecting client - 10 seconds of inactivity reached.")
            ExitLoop
        EndIf
        Sleep(10)
    Until StringInStr($sData, "[EOF]") ; Wait until full transmission

    ; Strip the marker out before parsing
    $sData = StringReplace($sData, "[EOF]", "")

    If $sData = "" Then
        _Log("Server: Received empty payload. Closing connection.")
        TCPCloseSocket($hClient)
        Return
    EndIf

    ; Parse incoming client packet: Format -> "CLIENT_SYNC¦<NodeID>\n[CSV rows]" OR "CLIENT_SYNC_ALL¦<NodeID>"
    Local $aPacket = StringSplit(StringStripCR($sData), @LF, 2)
    If UBound($aPacket) < 1 Then
        TCPCloseSocket($hClient)
        Return
    EndIf

    Local $aHeaderInfo = StringSplit($aPacket[0], "¦")
    Local $sCommand = ""
    Local $iClientNodeID = 0
    If $aHeaderInfo[0] >= 1 Then $sCommand = $aHeaderInfo[1]
    If $aHeaderInfo[0] >= 2 Then $iClientNodeID = Number($aHeaderInfo[2])

    _Log("Server: Processing command '" & $sCommand & "' from Node " & $iClientNodeID)

    Local $sHeader = "ID" & "¦" & "Title" & "¦" & "StartMin" & "¦" & "Duration" & "¦" & "Color" & "¦" & "Date" & "¦" & "PersonIdx" & "¦" & "Version" & "¦" & "LastModifiedBy"
    Local $sResponse = $sHeader & @CRLF

    If Not FileExists($sCsvFile) Then
        TCPSend($hClient, $sResponse & "[EOF]")
        TCPCloseSocket($hClient)
        _Log("Server: Local CSV missing. Sent blank header and closed connection.")
        Return
    EndIf

    Local $aServerLines
    _FileReadToArray($sCsvFile, $aServerLines)
    If @error Or UBound($aServerLines) <= 1 Then
        TCPSend($hClient, $sResponse & "[EOF]")
        TCPCloseSocket($hClient)
        _Log("Server: Local CSV empty. Sent blank header and closed connection.")
        Return
    EndIf

    ; If client requested ALL entries (local CSV was empty), send everything
    If $sCommand = "CLIENT_SYNC_ALL" Then
        For $j = 2 To UBound($aServerLines) - 1
            Local $sSrvLine = StringStripWS($aServerLines[$j], 3)
            If $sSrvLine <> "" Then
                $sResponse &= $sSrvLine & @CRLF
            EndIf
        Next
        TCPSend($hClient, $sResponse & "[EOF]")
        TCPCloseSocket($hClient)
        _Log("Server: Sent full database and closed connection immediately.")
        Return
    EndIf

    ; Otherwise, perform standard delta filtering
    Local $oClientVersions = ObjCreate("Scripting.Dictionary")
    
    For $i = 1 To UBound($aPacket) - 1
        Local $sLine = StringStripWS($aPacket[$i], 3)
        If $sLine = "" Or StringInStr($sLine, "ID" & "¦") Then ContinueLoop
        Local $aFields = StringSplit($sLine, "¦")
        If $aFields[0] >= 9 Then
            Local $sID = $aFields[1]
            Local $iVer = Number($aFields[8])
            $oClientVersions.Item($sID) = $iVer
        EndIf
    Next

    Local $iSentCount = 0
    For $j = 2 To UBound($aServerLines) - 1
        Local $sSrvLine = StringStripWS($aServerLines[$j], 3)
        If $sSrvLine = "" Then ContinueLoop
        Local $aSrvFields = StringSplit($sSrvLine, "¦")
        If $aSrvFields[0] < 9 Then ContinueLoop

        Local $sSrvID = $aSrvFields[1]
        Local $iSrvVersion = Number($aSrvFields[8])
        Local $iSrvModBy = Number($aSrvFields[9])

        ; RULE 1: Do not send data originally generated/modified by the requesting client itself
        If $iSrvModBy = $iClientNodeID Then ContinueLoop

        ; RULE 2: Only send if client doesn't have the ID, or server version is newer
        Local $bShouldSend = False
        If Not $oClientVersions.Exists($sSrvID) Then
            $bShouldSend = True
        Else
            If $iSrvVersion > $oClientVersions.Item($sSrvID) Then
                $bShouldSend = True
            EndIf
        EndIf

        If $bShouldSend Then
            $sResponse &= $sSrvLine & @CRLF
            $iSentCount += 1
        EndIf
    Next

    TCPSend($hClient, $sResponse & "[EOF]")
    TCPCloseSocket($hClient)
    _Log("Server: Sent " & $iSentCount & " delta row(s) and closed connection immediately.")
EndFunc

Func _RunAllClientSyncs($sIniFile, $iTargetPort, $sCsvFile, $iMyNodeID)
    Local $bUpdated = False
    For $i = 1 To 7
        Local $sTargetIP = IniRead($sIniFile, "Servers", "Server" & $i, "0")

        If $sTargetIP = "0" Or $sTargetIP = "" Or Not _IsIP($sTargetIP) Then ContinueLoop
        If StringInStr($sIPs,$sTargetIP) Then ContinueLoop

        Local $iSocket = TCPConnect($sTargetIP, $iTargetPort)
        If $iSocket = -1 Then ContinueLoop
        
        _Log("Client: Connected to server " & $sTargetIP)

        ; Check if local CSV exists and actually contains data rows
        Local $bHasData = False
        Local $aLocalLines[1]
        If FileExists($sCsvFile) Then
            _FileReadToArray($sCsvFile, $aLocalLines)
            If Not @error And UBound($aLocalLines) > 1 Then
                For $j = 2 To UBound($aLocalLines) - 1
                    If StringStripWS($aLocalLines[$j], 3) <> "" Then
                        $bHasData = True
                        ExitLoop
                    EndIf
                Next
            EndIf
        EndIf

        ; Construct sync payload: Ask for everything if empty, otherwise send local delta map
        Local $sPayload = ""
        If Not $bHasData Then
            $sPayload = "CLIENT_SYNC_ALL¦" & $iMyNodeID & @LF
            _Log("Client: Requesting full sync from " & $sTargetIP)
        Else
            $sPayload = "CLIENT_SYNC¦" & $iMyNodeID & @LF
            For $j = 1 To UBound($aLocalLines) - 1
                If $aLocalLines[$j] <> "" Then
                    $sPayload &= $aLocalLines[$j] & @LF
                EndIf
            Next
            _Log("Client: Sending delta payload to " & $sTargetIP)
        EndIf

        ; Send payload with explicitly appended [EOF] marker
        TCPSend($iSocket, $sPayload & "[EOF]")

        ; Receive filtered delta or full response safely with 10s timeout protection
        Local $sReceivedData = ""
        Local $hRecvTimer = TimerInit()
        Do
            Local $sChunk = TCPRecv($iSocket, 4096)
            If @error Then ExitLoop
            If $sChunk <> "" Then 
                $sReceivedData &= $sChunk
                $hRecvTimer = TimerInit()
            EndIf
            If TimerDiff($hRecvTimer) > 10000 Then
                _Log("Client: Server response timed out after 10s.")
                ExitLoop
            EndIf
            Sleep(10)
        Until StringInStr($sReceivedData, "[EOF]")
        
        ; Strip marker
        $sReceivedData = StringReplace($sReceivedData, "[EOF]", "")

        TCPCloseSocket($iSocket)
        _Log("Client: Disconnected from " & $sTargetIP)

        ; Merge received data into local CSV
        If $sReceivedData <> "" Then
            If _MergeCsvData($sCsvFile, $sReceivedData) Then $bUpdated = True
        EndIf
    Next
    Return $bUpdated
EndFunc

Func _MergeCsvData($sCsvFile, $sNewCsvContent)
    Local $aNewLines = StringSplit(StringStripCR($sNewCsvContent), @LF)
    If UBound($aNewLines) <= 2 Then Return False

    Local $aLocalLines[1]
    If FileExists($sCsvFile) Then
        _FileReadToArray($sCsvFile, $aLocalLines)
    Else
        Local $hNewFile = FileOpen($sCsvFile, 2)
        If $hNewFile <> -1 Then
            FileWriteLine($hNewFile, $aNewLines[1])
            FileClose($hNewFile)
        EndIf
        _FileReadToArray($sCsvFile, $aLocalLines)
    EndIf

    Local $iMergedCount = 0
    For $i = 2 To UBound($aNewLines) - 1
        Local $sLine = StringStripWS($aNewLines[$i], 3)
        If $sLine = "" Then ContinueLoop
        Local $aIncomingFields = StringSplit($sLine, "¦")
        If $aIncomingFields[0] < 9 Then ContinueLoop

        Local $sIncID = $aIncomingFields[1]
        Local $iIncVersion = Number($aIncomingFields[8])
        Local $iIncModBy = Number($aIncomingFields[9])

        Local $bFound = False
        For $j = 2 To UBound($aLocalLines) - 1
            Local $aLocalFields = StringSplit($aLocalLines[$j], "¦")
            If $aLocalFields[0] >= 9 And $aLocalFields[1] = $sIncID Then
                $bFound = True
                Local $iLocalVersion = Number($aLocalFields[8])
                Local $iLocalModBy = Number($aLocalFields[9])

                ; Version comparison logic with Node ID tie-breaker
                If $iIncVersion > $iLocalVersion Then
                    $aLocalLines[$j] = $sLine
                    $iMergedCount += 1
                ElseIf $iIncVersion = $iLocalVersion Then
                    If $iIncModBy > $iLocalModBy Then
                        $aLocalLines[$j] = $sLine
                        $iMergedCount += 1
                    EndIf
                EndIf
                ExitLoop
            EndIf
        Next

        If Not $bFound Then
            _ArrayAdd($aLocalLines, $sLine)
            $iMergedCount += 1
        EndIf
    Next

    If $iMergedCount > 0 Then
        Local $hFile = FileOpen($sCsvFile, 2)
        If $hFile <> -1 Then
            For $j = 1 To UBound($aLocalLines) - 1
                If $aLocalLines[$j] <> "" Then
                    FileWriteLine($hFile, $aLocalLines[$j])
                EndIf
            Next
            FileClose($hFile)
        EndIf
        _Log("Client: Successfully merged " & $iMergedCount & " new/updated row(s).")
        Return True
    EndIf
    Return False
EndFunc

Func _ProcessDeleteThreshold($sCsvFile)
    If Not FileExists($sCsvFile) Then Return False

    Local $aLines
    _FileReadToArray($sCsvFile, $aLines)
    If @error Or UBound($aLines) <= 1 Then Return False

    Local $iDeletedCount = 0
    Local $sNewContent = $aLines[1] & @CRLF ; Initialize content with the header

    ; Process from row 2 downwards
    For $i = 2 To UBound($aLines) - 1
        Local $sLine = StringStripWS($aLines[$i], 3)
        If $sLine = "" Then ContinueLoop

        Local $aFields = StringSplit($sLine, "¦")
        
        ; Verify the line has enough fields and check if Color (Index 5) is 2
        If $aFields[0] >= 5 And StringStripWS($aFields[5], 3) = "2" Then
            $iDeletedCount += 1
            ContinueLoop ; Skip appending this line (effectively deleting it)
        EndIf

        $sNewContent &= $sLine & @CRLF
    Next

    ; Only rewrite the file if events were actually deleted to save I/O overhead
    If $iDeletedCount > 0 Then
        Local $hFile = FileOpen($sCsvFile, 2) ; 2 = Overwrite mode
        If $hFile <> -1 Then
            FileWrite($hFile, $sNewContent)
            FileClose($hFile)
            _Log("Threshold reached. Cleaned up " & $iDeletedCount & " event(s) with Color 2.")
            Return True
        EndIf
    EndIf
    Return False
EndFunc

Func _ExportUpcomingSchedule()
    Local $sSavePath = FileSaveDialog("Export Upcoming Schedule", @DesktopDir, "Text Files (*.txt)", BitOR($FO_OVERWRITE, $FD_PATHMUSTEXIST), "Upcoming_Schedule.txt", $hMainGUI)
    If @error Or $sSavePath == "" Then Return
    If StringRight($sSavePath, 4) <> ".txt" Then $sSavePath &= ".txt"

    Local $aUpIdx = _GetSortedUpcomingIndices()

    Local $hFile = FileOpen($sSavePath, BitOR($FO_OVERWRITE, $FO_UTF8))
    If $hFile = -1 Then
        MsgBox(16, "Export Error", "Could not open file for writing.", 0, $hMainGUI)
        Return
    EndIf

    FileWriteLine($hFile, "========================================")
    FileWriteLine($hFile, "           UPCOMING SCHEDULE            ")
    FileWriteLine($hFile, "========================================" & @CRLF)

    If UBound($aUpIdx) == 0 Then
        FileWriteLine($hFile, "No upcoming events found.")
    Else
        For $i = 0 To UBound($aUpIdx) - 1
            Local $e = $aUpIdx[$i]
            Local $sDate = _FormatDateTitle($aEvents[$e][5])
            Local $iDisplayMin = $aEvents[$e][2] + ($fTimezoneOffset * 60) ; Incorporate Offset
            Local $sTime = _MinToTimeString($iDisplayMin) & " - " & _MinToTimeString($iDisplayMin + $aEvents[$e][3])
            Local $sPerson = ""
            If $aEvents[$e][6] < UBound($aPeople) Then $sPerson = $aPeople[$aEvents[$e][6]]

            FileWriteLine($hFile, $aEvents[$e][1])
            FileWriteLine($hFile, $sDate & "   •   " & $sTime & "   •   " & $sPerson)
            FileWriteLine($hFile, "----------------------------------------")
        Next
    EndIf

    FileClose($hFile)
    MsgBox(64, "Export Successful", "Upcoming schedule exported successfully to:" & @CRLF & $sSavePath, 0, $hMainGUI)
EndFunc
; ==============================================================================
; IN-PLACE TEXT EDITING MANAGEMENT
; ==============================================================================
Func _OpenInPlaceEdit($iEventIdx)
    _CloseInPlaceEdit(True) ; Save any previous edit
    $iEditingIndex = $iEventIdx

    Local $aRect = _GetEventScreenRect($iEventIdx)
    If Not @error Then
        Local $iW = $aRect[2] - $aRect[0]
        Local $iH = $aRect[3] - $aRect[1]
        If $iH < 45 Then $iH = 50 ; Ensure enough height for text wrapping
        If $iW < 110 Then $iW = 120
        
        GUICtrlSetData($idInPlaceEdit, $aEvents[$iEditingIndex][1])
        GUICtrlSetPos($idInPlaceEdit, $aRect[0], $aRect[1], $iW, $iH)
        GUICtrlSetState($idInPlaceEdit, $GUI_SHOW)
        GUICtrlSetState($idInPlaceEdit, $GUI_FOCUS)
    EndIf
EndFunc

Func _CloseInPlaceEdit($bSave = True)
    If $iEditingIndex <> -1 Then
        If $bSave Then 
            Local $sNewTitle = GUICtrlRead($idInPlaceEdit)
            If $sNewTitle <> $aEvents[$iEditingIndex][1] Then
                $aEvents[$iEditingIndex][1] = $sNewTitle
                _MarkEventModified($iEditingIndex)
                _SaveCSV()
            EndIf
        EndIf
        GUICtrlSetState($idInPlaceEdit, $GUI_HIDE)
        GUICtrlSetPos($idInPlaceEdit, -500, -500, 10, 10)
        $iEditingIndex = -1
        _WinAPI_InvalidateRect($hCanvas, 0, True) 
    EndIf
EndFunc

; ==============================================================================
; ZOOM & SCROLLING MANAGEMENT
; ==============================================================================
Func _SetZoom($fNewZoom)
    If $iViewMode >= 6 Then Return ; Zoom disabled in Month & Upcoming View
    If $fNewZoom < 0.6 Then $fNewZoom = 0.6
    If $fNewZoom > 2.5 Then $fNewZoom = 2.5
    If $fNewZoom == $fZoom Then Return

    _CloseInPlaceEdit(True)
    Local $iVisibleH = $iClientH - $iHeaderH - $iSubHeaderH
    Local $iCenterMin = ($iScrollY + ($iVisibleH / 2)) / $fZoom
    $fZoom = $fNewZoom
    
    _UpdateScrollBars()
    $iScrollY = Round(($iCenterMin * $fZoom) - ($iVisibleH / 2))
    
    _WinAPI_InvalidateRect($hCanvas, 0, True)
EndFunc

Func _UpdateScrollBars()
    Local $iEffectiveWidth = ($iClientW > $iCanvasWidth) ? $iClientW : $iCanvasWidth
    Local $iVisibleH = $iClientH - $iHeaderH - $iSubHeaderH

    Local $tSI = DllStructCreate("uint cbSize;uint fMask;int nMin;int nMax;uint nPage;int nPos;int nTrackPos")
    DllStructSetData($tSI, "cbSize", DllStructGetSize($tSI))
    DllStructSetData($tSI, "fMask", BitOR(0x1, 0x2, 0x4))
    
    If $iViewMode == 6 Then
        $iScrollY = 0
        DllStructSetData($tSI, "nMin", 0)
        DllStructSetData($tSI, "nMax", 0)
        DllStructSetData($tSI, "nPage", $iVisibleH)
        DllStructSetData($tSI, "nPos", 0)
        DllCall("user32.dll", "int", "SetScrollInfo", "hwnd", $hCanvas, "int", $SB_VERT, "struct*", $tSI, "bool", True)
    Else
        If $iViewMode == 7 Then
            Local $iCount = 0
            For $i = 0 To UBound($aEvents) - 1
                If $aEvents[$i][5] >= $sCurrentDate And $aEvents[$i][4] <> 2 Then $iCount += 1
            Next
            $iCanvasHeight = ($iCount * 115) + 40
        Else
            $iCanvasHeight = Round(1440 * $fZoom)
        EndIf
        
        Local $iMaxScrollY = $iCanvasHeight - $iVisibleH
        If $iMaxScrollY < 0 Then $iMaxScrollY = 0
        If $iScrollY > $iMaxScrollY Then $iScrollY = $iMaxScrollY
        If $iScrollY < 0 Then $iScrollY = 0

        DllStructSetData($tSI, "nMin", 0)
        DllStructSetData($tSI, "nMax", $iCanvasHeight)
        DllStructSetData($tSI, "nPage", $iVisibleH)
        DllStructSetData($tSI, "nPos", $iScrollY)
        DllCall("user32.dll", "int", "SetScrollInfo", "hwnd", $hCanvas, "int", $SB_VERT, "struct*", $tSI, "bool", True)
    EndIf

    ; Horizontal Scroll
    Local $iMaxScrollX = $iEffectiveWidth - $iClientW
    If $iMaxScrollX < 0 Then $iMaxScrollX = 0
    If $iScrollX > $iMaxScrollX Then $iScrollX = $iMaxScrollX
    If $iScrollX < 0 Then $iScrollX = 0

    DllStructSetData($tSI, "nMax", $iEffectiveWidth)
    DllStructSetData($tSI, "nPage", $iClientW)
    DllStructSetData($tSI, "nPos", $iScrollX)
    DllCall("user32.dll", "int", "SetScrollInfo", "hwnd", $hCanvas, "int", $SB_HORZ, "struct*", $tSI, "bool", True)
EndFunc

Func _GetScrollTrackPos($hWnd, $nBar)
    Local $tSI = DllStructCreate("uint cbSize;uint fMask;int nMin;int nMax;uint nPage;int nPos;int nTrackPos")
    DllStructSetData($tSI, "cbSize", DllStructGetSize($tSI))
    DllStructSetData($tSI, "fMask", 0x10)
    DllCall("user32.dll", "bool", "GetScrollInfo", "hwnd", $hWnd, "int", $nBar, "struct*", $tSI)
    Return DllStructGetData($tSI, "nTrackPos")
EndFunc

; ==============================================================================
; GDI RENDERING ENGINE
; ==============================================================================
Func _DrawCalendar($hDC)
    Local $iCanvasW = $iClientW
    Local $iCanvasH = $iClientH - $iHeaderH

    Local $hMemDC  = _WinAPI_CreateCompatibleDC($hDC)
    Local $hBitmap = _WinAPI_CreateCompatibleBitmap($hDC, $iCanvasW, $iCanvasH)
    Local $hOldBmp = _WinAPI_SelectObject($hMemDC, $hBitmap)

    ; Canvas Background
    Local $hBgBrush = _WinAPI_CreateSolidBrush(0xFFFFFF)
    Local $tClientRect = _WinAPI_CreateRectEx(0, 0, $iCanvasW, $iCanvasH)
    _WinAPI_FillRect($hMemDC, DllStructGetPtr($tClientRect), $hBgBrush)
    _WinAPI_DeleteObject($hBgBrush)

    If $iViewMode == 6 Then
        _DrawMonthView($hMemDC, $iCanvasW, $iCanvasH)
    ElseIf $iViewMode == 7 Then
        _DrawUpcomingView($hMemDC, $iCanvasW, $iCanvasH)
    Else
        _DrawTimelineView($hMemDC, $iCanvasW, $iCanvasH)
    EndIf

    _WinAPI_BitBlt($hDC, 0, 0, $iCanvasW, $iCanvasH, $hMemDC, 0, 0, $SRCCOPY)
    
    _WinAPI_SelectObject($hMemDC, $hOldBmp)
    _WinAPI_DeleteObject($hBitmap)
    _WinAPI_DeleteDC($hMemDC)
EndFunc

Func _DrawTimelineView($hMemDC, $iCanvasW, $iCanvasH)
    Local $iEffectiveWidth = ($iCanvasW > $iCanvasWidth) ? $iCanvasW : $iCanvasWidth
    Local $iColCount = _GetColCount()
    Local $iDayColWidth = Floor(($iEffectiveWidth - $iTimeColWidth) / $iColCount)

    _WinAPI_SetBkMode($hMemDC, $TRANSPARENT)
    Local $hFontHour   = _WinAPI_CreateFont(13, 0, 0, 0, 400, False, False, False, 0, 0, 0, 0, 0, "Segoe UI")
    Local $hFontEvent  = _WinAPI_CreateFont(13, 0, 0, 0, 600, False, False, False, 0, 0, 0, 0, 0, "Segoe UI")
    Local $hFontHeader = _WinAPI_CreateFont(13, 0, 0, 0, 700, False, False, False, 0, 0, 0, 0, 0, "Segoe UI")
    Local $hOldFont    = _WinAPI_SelectObject($hMemDC, $hFontHour)

    ; Draw Time Grid & Labels
    Local $hPenHour = _GDI_CreatePen(0, 1, 0xE0E0E0)
    Local $hPenHalf = _GDI_CreatePen(2, 1, 0xF0F0F0)

    For $iHour = 0 To 24
        Local $iY = Round(($iHour * 60) * $fZoom) - $iScrollY + $iSubHeaderH
        If $iY >= $iSubHeaderH - 10 And $iY <= $iCanvasH Then
            _WinAPI_SelectObject($hMemDC, $hPenHour)
            _WinAPI_MoveTo($hMemDC, $iTimeColWidth - $iScrollX, $iY)
            _WinAPI_LineTo($hMemDC, $iEffectiveWidth - $iScrollX, $iY)

            If $iHour > 0 And $iHour < 24 Then
                Local $sTimeLabel = ($iHour > 12 ? $iHour - 12 : ($iHour == 0 ? 12 : $iHour)) & ($iHour >= 12 ? " PM" : " AM")
                If $iHour == 12 Then $sTimeLabel = "12 PM"
                _GDI_SetTextColor($hMemDC, 0x70757A)
                Local $tRect = _WinAPI_CreateRectEx(0, $iY - 10, $iTimeColWidth - 8 - $iScrollX, 20)
                _WinAPI_DrawText($hMemDC, $sTimeLabel, $tRect, BitOR($DT_RIGHT, $DT_TOP, $DT_SINGLELINE))
            EndIf

            If $iHour < 24 Then
                Local $iHalfY = Round(($iHour * 60 + 30) * $fZoom) - $iScrollY + $iSubHeaderH
                _WinAPI_SelectObject($hMemDC, $hPenHalf)
                _WinAPI_MoveTo($hMemDC, $iTimeColWidth - $iScrollX, $iHalfY)
                _WinAPI_LineTo($hMemDC, $iEffectiveWidth - $iScrollX, $iHalfY)
            EndIf
        EndIf
    Next

    ; Vertical Column Dividers
    _WinAPI_SelectObject($hMemDC, $hPenHour)
    _WinAPI_MoveTo($hMemDC, $iTimeColWidth - $iScrollX, $iSubHeaderH)
    _WinAPI_LineTo($hMemDC, $iTimeColWidth - $iScrollX, $iCanvasH)
    
    For $c = 1 To $iColCount
        Local $iColX = $iTimeColWidth + ($c * $iDayColWidth) - $iScrollX
        _WinAPI_MoveTo($hMemDC, $iColX, $iSubHeaderH)
        _WinAPI_LineTo($hMemDC, $iColX, $iCanvasH)
    Next

    ; Draw Event Blocks
    _WinAPI_SelectObject($hMemDC, $hFontEvent)
    For $i = 0 To UBound($aEvents) - 1
        If $aEvents[$i][4] == 2 Then ContinueLoop 
        
        Local $iColIdx = _GetEventColumnIndex($i)
        If $iColIdx < 0 Or $iColIdx >= $iColCount Then ContinueLoop

        ; Apply timezone offset to display coordinates
        Local $iStartMin = $aEvents[$i][2] + ($fTimezoneOffset * 60)
        Local $iDuration = $aEvents[$i][3]
        Local $iColor    = $aEvents[$i][4]

        Local $iBoxTop    = Round($iStartMin * $fZoom) - $iScrollY + $iSubHeaderH
        Local $iBoxBottom = Round(($iStartMin + $iDuration) * $fZoom) - $iScrollY + $iSubHeaderH
        Local $iBoxLeft   = $iTimeColWidth + ($iColIdx * $iDayColWidth) - $iScrollX + 4
        Local $iBoxRight  = $iBoxLeft + $iDayColWidth - 8

        If $iBoxBottom > $iSubHeaderH And $iBoxTop < $iCanvasH Then
            Local $hBrushEvent = _GDI_CreateSolidBrush($iColor)
            Local $hPenEvent   = _GDI_CreatePen(0, 1, _DarkenColor($iColor, 20))
            
            _WinAPI_SelectObject($hMemDC, $hBrushEvent)
            _WinAPI_SelectObject($hMemDC, $hPenEvent)
            _GDI_RoundRect($hMemDC, $iBoxLeft, $iBoxTop, $iBoxRight, $iBoxBottom, 8, 8)

            If $i <> $iEditingIndex Then 
                _GDI_SetTextColor($hMemDC, 0xFFFFFF)
                Local $iBoxHeight = $iBoxBottom - $iBoxTop
                Local $tTextRect  = _WinAPI_CreateRectEx($iBoxLeft + 8, $iBoxTop + 4, ($iBoxRight - $iBoxLeft) - 12, $iBoxHeight - 6)
                
                Local $sEventTime = _MinToTimeString($iStartMin) & " - " & _MinToTimeString($iStartMin + $iDuration)
                Local $sDisplayText = $aEvents[$i][1] & @CRLF & $sEventTime
                
                If $iBoxHeight > 18 Then
                    _WinAPI_DrawText($hMemDC, $sDisplayText, $tTextRect, BitOR($DT_LEFT, $DT_TOP, $DT_WORDBREAK, $DT_END_ELLIPSIS))
                EndIf
            EndIf

            If ($iBoxBottom - $iBoxTop) > 24 Then
                Local $hPenGrip = _GDI_CreatePen(0, 1, _DarkenColor($iColor, 35))
                _WinAPI_SelectObject($hMemDC, $hPenGrip)
                Local $iMidX = $iBoxLeft + (($iBoxRight - $iBoxLeft) / 2)
                _WinAPI_MoveTo($hMemDC, $iMidX - 12, $iBoxTop + 3)
                _WinAPI_LineTo($hMemDC, $iMidX + 12, $iBoxTop + 3)
                _WinAPI_MoveTo($hMemDC, $iMidX - 12, $iBoxBottom - 3)
                _WinAPI_LineTo($hMemDC, $iMidX + 12, $iBoxBottom - 3)
                _WinAPI_DeleteObject($hPenGrip)
            EndIf

            _WinAPI_DeleteObject($hBrushEvent)
            _WinAPI_DeleteObject($hPenEvent)
        EndIf
    Next

    ; Draw Red Current Time Indicator
    Local $sTodayStr = _NowCalcDate()
    Local $iRedCol = -1

    If _IsPeopleView() And $sCurrentDate == $sTodayStr Then
        $iRedCol = -2
    ElseIf Not _IsPeopleView() Then
        Local $iDiffToday = _DateDiff('d', $sCurrentDate, $sTodayStr)
        If $iDiffToday >= 0 And $iDiffToday < $iColCount Then $iRedCol = $iDiffToday
    EndIf

    If $iRedCol <> -1 Then
        Local $iCurrentMin = (@HOUR * 60) + @MIN
        Local $iNowY = Round($iCurrentMin * $fZoom) - $iScrollY + $iSubHeaderH
        If $iNowY >= $iSubHeaderH And $iNowY <= $iCanvasH Then
            Local $hPenRed   = _GDI_CreatePen(0, 2, 0xEA4335)
            Local $hBrushRed = _GDI_CreateSolidBrush(0xEA4335)
            _WinAPI_SelectObject($hMemDC, $hPenRed)
            _WinAPI_SelectObject($hMemDC, $hBrushRed)
            
            Local $iRedLeft  = ($iRedCol == -2) ? ($iTimeColWidth - $iScrollX) : ($iTimeColWidth + ($iRedCol * $iDayColWidth) - $iScrollX)
            Local $iRedRight = ($iRedCol == -2) ? ($iEffectiveWidth - $iScrollX) : ($iRedLeft + $iDayColWidth)

            _WinAPI_MoveTo($hMemDC, $iRedLeft, $iNowY)
            _WinAPI_LineTo($hMemDC, $iRedRight, $iNowY)
            _GDI_Ellipse($hMemDC, $iRedLeft - 5, $iNowY - 5, $iRedLeft + 5, $iNowY + 5)
            
            _WinAPI_DeleteObject($hPenRed)
            _WinAPI_DeleteObject($hBrushRed)
        EndIf
    EndIf

    ; Draw Fixed Top Sub-Headers
    Local $hHeaderBrush = _WinAPI_CreateSolidBrush(0xF8F9FA)
    Local $tHeaderRect  = _WinAPI_CreateRectEx(0, 0, $iCanvasW, $iSubHeaderH)
    _WinAPI_FillRect($hMemDC, DllStructGetPtr($tHeaderRect), $hHeaderBrush)
    _WinAPI_DeleteObject($hHeaderBrush)

    Local $hPenBorder = _GDI_CreatePen(0, 1, 0xDADCE0)
    _WinAPI_SelectObject($hMemDC, $hPenBorder)
    _WinAPI_MoveTo($hMemDC, 0, $iSubHeaderH - 1)
    _WinAPI_LineTo($hMemDC, $iCanvasW, $iSubHeaderH - 1)

    _WinAPI_SelectObject($hMemDC, $hFontHeader)
    _GDI_SetTextColor($hMemDC, 0x3C4043)

    For $c = 0 To $iColCount - 1
        Local $iColLeft = $iTimeColWidth + ($c * $iDayColWidth) - $iScrollX
        Local $tColRect = _WinAPI_CreateRectEx($iColLeft, 6, $iDayColWidth, 25)
        Local $sHeaderText = ""

        If _IsPeopleView() Then
            If $c < UBound($aPeople) Then
                $sHeaderText = $aPeople[$c]
            Else
                $sHeaderText = "Person " & ($c + 1)
            EndIf
        Else
            Local $sColDate = _DateAdd('d', $c, $sCurrentDate)
            $sHeaderText = _FormatDayHeader($sColDate)
        EndIf

        _WinAPI_DrawText($hMemDC, $sHeaderText, $tColRect, BitOR($DT_CENTER, $DT_TOP, $DT_SINGLELINE, $DT_END_ELLIPSIS))
        If $c > 0 Then
            _WinAPI_MoveTo($hMemDC, $iColLeft, 0)
            _WinAPI_LineTo($hMemDC, $iColLeft, $iSubHeaderH)
        EndIf
    Next

    _WinAPI_SelectObject($hMemDC, $hOldFont)
    _WinAPI_DeleteObject($hFontHour)
    _WinAPI_DeleteObject($hFontEvent)
    _WinAPI_DeleteObject($hFontHeader)
    _WinAPI_DeleteObject($hPenHour)
    _WinAPI_DeleteObject($hPenHalf)
    _WinAPI_DeleteObject($hPenBorder)
EndFunc

Func _DrawMonthView($hMemDC, $iCanvasW, $iCanvasH)
    _WinAPI_SetBkMode($hMemDC, $TRANSPARENT)
    Local $hFontDay    = _WinAPI_CreateFont(12, 0, 0, 0, 600, False, False, False, 0, 0, 0, 0, 0, "Segoe UI")
    Local $hFontEvent  = _WinAPI_CreateFont(9, 0, 0, 0, 600, False, False, False, 0, 0, 0, 0, 0, "Segoe UI")
    Local $hFontHeader = _WinAPI_CreateFont(13, 0, 0, 0, 700, False, False, False, 0, 0, 0, 0, 0, "Segoe UI")
    Local $hOldFont    = _WinAPI_SelectObject($hMemDC, $hFontDay)

    Local $iYear = Int(StringLeft($sCurrentDate, 4))
    Local $iMonth = Int(StringMid($sCurrentDate, 6, 2))
    Local $iDaysInMonth = _DateDaysInMonth($iYear, $iMonth)
    Local $iStartDayOfWeek = _DateToDayOfWeek($iYear, $iMonth, 1)

    Local $iColW = $iCanvasW / 7
    Local $iRowH = ($iCanvasH - $iSubHeaderH) / 6

    Local $hHeaderBrush = _WinAPI_CreateSolidBrush(0xF8F9FA)
    Local $tHeaderRect  = _WinAPI_CreateRectEx(0, 0, $iCanvasW, $iSubHeaderH)
    _WinAPI_FillRect($hMemDC, DllStructGetPtr($tHeaderRect), $hHeaderBrush)
    _WinAPI_DeleteObject($hHeaderBrush)

    Local $aDaysOfWeek[7] = ["Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"]
    _WinAPI_SelectObject($hMemDC, $hFontHeader)
    _GDI_SetTextColor($hMemDC, 0x3C4043)
    For $c = 0 To 6
        Local $tColRect = _WinAPI_CreateRectEx($c * $iColW, 6, $iColW, 25)
        _WinAPI_DrawText($hMemDC, $aDaysOfWeek[$c], $tColRect, BitOR($DT_CENTER, $DT_TOP, $DT_SINGLELINE))
    Next

    Local $hPenBorder = _GDI_CreatePen(0, 1, 0xDADCE0)
    Local $hBrushToday = _WinAPI_CreateSolidBrush(0xFEF7E0) 
    Local $hBrushGrey  = _WinAPI_CreateSolidBrush(0xF1F3F4) 
    _WinAPI_SelectObject($hMemDC, $hPenBorder)

    Local $sTodayStr = _NowCalcDate()

    For $row = 0 To 5
        For $col = 0 To 6
            Local $iCellIdx = ($row * 7) + $col + 1
            Local $iDayNum = $iCellIdx - $iStartDayOfWeek + 1
            Local $iX1 = $col * $iColW
            Local $iY1 = $iSubHeaderH + ($row * $iRowH)
            Local $iX2 = $iX1 + $iColW
            Local $iY2 = $iY1 + $iRowH

            Local $tCellRect = _WinAPI_CreateRect($iX1, $iY1, $iX2, $iY2)

            If $iDayNum >= 1 And $iDayNum <= $iDaysInMonth Then
                Local $sCellDate = StringFormat("%04d/%02d/%02d", $iYear, $iMonth, $iDayNum)
                If $sCellDate == $sTodayStr Then _WinAPI_FillRect($hMemDC, DllStructGetPtr($tCellRect), $hBrushToday)

                _WinAPI_SelectObject($hMemDC, $hFontDay)
                _GDI_SetTextColor($hMemDC, ($sCellDate == $sTodayStr) ? 0x1A73E8 : 0x3C4043)
                Local $tNumRect = _WinAPI_CreateRect($iX1, $iY1 + 4, $iX2 - 6, $iY1 + 22)
                _WinAPI_DrawText($hMemDC, String($iDayNum), $tNumRect, BitOR($DT_RIGHT, $DT_TOP, $DT_SINGLELINE))

                Local $aDayEventIdx[0]
                For $e = 0 To UBound($aEvents) - 1
                    If $aEvents[$e][5] == $sCellDate And $aEvents[$e][4] <> 2 Then _ArrayAdd($aDayEventIdx, $e)
                Next
                
                For $ei = 0 To UBound($aDayEventIdx) - 2
                    For $ej = $ei + 1 To UBound($aDayEventIdx) - 1
                        Local $idxI = $aDayEventIdx[$ei], $idxJ = $aDayEventIdx[$ej]
                        If $aEvents[$idxJ][2] < $aEvents[$idxI][2] Then
                            $aDayEventIdx[$ei] = $idxJ
                            $aDayEventIdx[$ej] = $idxI
                        EndIf
                    Next
                Next
                
                Local $iDayEvents = UBound($aDayEventIdx)
                Local $iMaxVisible = Floor(($iRowH - 22) / 15)
                If $iDayEvents > $iMaxVisible Then $iMaxVisible -= 1
                
                For $p = 0 To $iDayEvents - 1
                    If $p < $iMaxVisible Then
                        Local $eIdx = $aDayEventIdx[$p]
                        Local $iPillTop = $iY1 + 22 + ($p * 15)
                        Local $iPillBottom = $iPillTop + 13
                        Local $iPillLeft = $iX1 + 4
                        Local $iPillRight = $iX2 - 4

                        Local $iColor = $aEvents[$eIdx][4]
                        
                        Local $hBrushEv = _GDI_CreateSolidBrush($iColor)
                        Local $hPenEv   = _GDI_CreatePen(0, 1, _DarkenColor($iColor, 15))
                        
                        _WinAPI_SelectObject($hMemDC, $hPenEv)
                        _WinAPI_SelectObject($hMemDC, $hBrushEv)
                        
                        _GDI_RoundRect($hMemDC, $iPillLeft, $iPillTop, $iPillRight, $iPillBottom, 4, 4)

                        If $eIdx <> $iEditingIndex Then
                            _GDI_SetTextColor($hMemDC, _ContrastColor($iColor))
                            Local $tEvRect = _WinAPI_CreateRect($iPillLeft + 5, $iPillTop + 2, $iPillRight - 5, $iPillBottom - 2)
                            _WinAPI_DrawText($hMemDC, $aEvents[$eIdx][1], $tEvRect, BitOR($DT_LEFT, $DT_TOP, $DT_SINGLELINE, $DT_END_ELLIPSIS))
                        EndIf

                        _WinAPI_DeleteObject($hPenEv)
                        _WinAPI_DeleteObject($hBrushEv)
                    ElseIf $p == $iMaxVisible And $iDayEvents > $iMaxVisible Then
                        _GDI_SetTextColor($hMemDC, 0x5F6368)
                        Local $tMoreRect = _WinAPI_CreateRect($iX1 + 6, $iY1 + 22 + ($p * 15), $iX2 - 6, $iY2)
                        _WinAPI_DrawText($hMemDC, "+" & ($iDayEvents - $iMaxVisible) & " more", $tMoreRect, BitOR($DT_LEFT, $DT_TOP, $DT_SINGLELINE))
                    EndIf
                Next
            Else
                _WinAPI_FillRect($hMemDC, DllStructGetPtr($tCellRect), $hBrushGrey)
            EndIf

            _WinAPI_SelectObject($hMemDC, $hPenBorder)
            _WinAPI_MoveTo($hMemDC, $iX1, $iY1)
            _WinAPI_LineTo($hMemDC, $iX2, $iY1)
            _WinAPI_MoveTo($hMemDC, $iX1, $iY1)
            _WinAPI_LineTo($hMemDC, $iX1, $iY2)
        Next
    Next

    _WinAPI_SelectObject($hMemDC, $hOldFont)
    _WinAPI_DeleteObject($hFontDay)
    _WinAPI_DeleteObject($hFontEvent)
    _WinAPI_DeleteObject($hFontHeader)
    _WinAPI_DeleteObject($hPenBorder)
    _WinAPI_DeleteObject($hBrushToday)
    _WinAPI_DeleteObject($hBrushGrey)
EndFunc

Func _DrawUpcomingView($hMemDC, $iCanvasW, $iCanvasH)
    _WinAPI_SetBkMode($hMemDC, $TRANSPARENT)
    Local $hFontTitle = _WinAPI_CreateFont(22, 0, 0, 0, 700, False, False, False, 0, 0, 0, 0, 0, "Segoe UI") 
    Local $hFontText  = _WinAPI_CreateFont(16, 0, 0, 0, 400, False, False, False, 0, 0, 0, 0, 0, "Segoe UI") 
    Local $hOldFont   = _WinAPI_SelectObject($hMemDC, $hFontTitle)
    Local $iEffectiveW = ($iCanvasW > $iCanvasWidth) ? $iCanvasW : $iCanvasW
    
    Local $aUpIdx = _GetSortedUpcomingIndices()
    
    Local $iY = 20 - $iScrollY
    For $i = 0 To UBound($aUpIdx) - 1
        Local $e = $aUpIdx[$i]
        
        If $iY + 100 > 0 And $iY < $iCanvasH Then
            Local $sDate = _FormatDateTitle($aEvents[$e][5])
            Local $iDisplayMin = $aEvents[$e][2] + ($fTimezoneOffset * 60) ; Applied offset
            Local $sTime = _MinToTimeString($iDisplayMin) & " - " & _MinToTimeString($iDisplayMin + $aEvents[$e][3])
            Local $sPerson = ""
            If $aEvents[$e][6] < UBound($aPeople) Then $sPerson = $aPeople[$aEvents[$e][6]]
            
            Local $iCardLeft = 20 - $iScrollX
            Local $iCardRight = $iEffectiveW - 20 - $iScrollX
            Local $iCardTop = $iY
            Local $iCardBottom = $iY + 95
            
            Local $hBrushEv = _GDI_CreateSolidBrush($aEvents[$e][4])
            Local $tColorRect = _WinAPI_CreateRect($iCardLeft, $iCardTop, $iCardLeft + 15, $iCardBottom)
            _WinAPI_FillRect($hMemDC, DllStructGetPtr($tColorRect), $hBrushEv)
            _WinAPI_DeleteObject($hBrushEv)
            
            Local $hBrushBg = _WinAPI_CreateSolidBrush(0xF8F9FA)
            Local $tBgRect = _WinAPI_CreateRect($iCardLeft + 15, $iCardTop, $iCardRight, $iCardBottom)
            _WinAPI_FillRect($hMemDC, DllStructGetPtr($tBgRect), $hBrushBg)
            _WinAPI_DeleteObject($hBrushBg)
            
            Local $hPenLine = _GDI_CreatePen(0, 1, 0xDADCE0)
            _WinAPI_SelectObject($hMemDC, $hPenLine)
            _WinAPI_MoveTo($hMemDC, $iCardLeft, $iCardTop)
            _WinAPI_LineTo($hMemDC, $iCardRight, $iCardTop)
            _WinAPI_LineTo($hMemDC, $iCardRight, $iCardBottom)
            _WinAPI_LineTo($hMemDC, $iCardLeft, $iCardBottom)
            _WinAPI_LineTo($hMemDC, $iCardLeft, $iCardTop)
            _WinAPI_DeleteObject($hPenLine)
            
            If $e <> $iEditingIndex Then
                _WinAPI_SelectObject($hMemDC, $hFontTitle)
                _GDI_SetTextColor($hMemDC, 0x202124)
                Local $tTitle = _WinAPI_CreateRect($iCardLeft + 35, $iY + 16, $iCardRight - 15, $iY + 50)
                _WinAPI_DrawText($hMemDC, $aEvents[$e][1], $tTitle, BitOR($DT_LEFT, $DT_TOP, $DT_SINGLELINE, $DT_END_ELLIPSIS))
            EndIf

            _WinAPI_SelectObject($hMemDC, $hFontText)
            _GDI_SetTextColor($hMemDC, 0x5F6368)
            Local $tDesc = _WinAPI_CreateRect($iCardLeft + 35, $iY + 56, $iCardRight - 15, $iY + 86)
            _WinAPI_DrawText($hMemDC, $sDate & "   •   " & $sTime & "   •   " & $sPerson, $tDesc, BitOR($DT_LEFT, $DT_TOP, $DT_SINGLELINE, $DT_END_ELLIPSIS))
        EndIf
        
        $iY += 115
    Next
    
    _WinAPI_SelectObject($hMemDC, $hOldFont)
    _WinAPI_DeleteObject($hFontTitle)
    _WinAPI_DeleteObject($hFontText)
EndFunc

; ==============================================================================
; WIN32 MESSAGE HANDLERS & HIT-TESTING
; ==============================================================================
Func WM_PAINT($hWnd, $iMsg, $wParam, $lParam)
    If $hWnd == $hCanvas Then
        Local $tPaint = DllStructCreate("hwnd hdc;int fErase;int rcPaint[4];int fRestore;int fIncUpdate;byte rgbReserved[32]")
        Local $hDC = _WinAPI_BeginPaint($hWnd, $tPaint)
        _DrawCalendar($hDC)
        _WinAPI_EndPaint($hWnd, $tPaint)
    EndIf
    Return $GUI_RUNDEFMSG
EndFunc

Func WM_ERASEBKGND($hWnd, $iMsg, $wParam, $lParam)
    If $hWnd == $hCanvas Then Return 1
    Return $GUI_RUNDEFMSG
EndFunc

Func WM_SIZE($hWnd, $iMsg, $wParam, $lParam)
    If $hWnd == $hMainGUI Then
        $iClientW = BitAND($lParam, 0xFFFF)
        $iClientH = BitShift($lParam, 16)
        
        If $hCanvas <> 0 Then
            _WinAPI_MoveWindow($hCanvas, 0, $iHeaderH, $iClientW, $iClientH - $iHeaderH, True)
        EndIf
        
        _UpdateScrollBars()
        _WinAPI_InvalidateRect($hCanvas, 0, True)
    EndIf
    Return $GUI_RUNDEFMSG
EndFunc

Func WM_LBUTTONDOWN($hWnd, $iMsg, $wParam, $lParam)
    If $hWnd <> $hCanvas Then Return $GUI_RUNDEFMSG
    _CloseInPlaceEdit(True)

    If BitShift($lParam, 16) < $iSubHeaderH And $iViewMode <> 7 Then Return $GUI_RUNDEFMSG 

    Local $iMouseX = BitAND($lParam, 0xFFFF)
    Local $iMouseY = BitShift($lParam, 16)

    If $iViewMode == 7 Then 
        Local $aUpIdx = _GetSortedUpcomingIndices()
        Local $iEffectiveW = ($iClientW > $iCanvasWidth ? $iClientW : $iCanvasWidth)
        For $i = 0 To UBound($aUpIdx) - 1
            Local $e = $aUpIdx[$i]
            Local $iCardTop = 20 - $iScrollY + ($i * 115) + 16
            Local $iCardBottom = $iCardTop + 34
            Local $iCardLeft = 35 - $iScrollX
            Local $iCardRight = $iEffectiveW - 35
            
            If $iMouseX >= $iCardLeft And $iMouseX <= $iCardRight And $iMouseY >= $iCardTop And $iMouseY <= $iCardBottom Then
                $iSelectedForDelete = $e
                ExitLoop
            EndIf
        Next
        Return $GUI_RUNDEFMSG 
    EndIf

    For $i = UBound($aEvents) - 1 To 0 Step -1
        Local $aRect = _GetEventScreenRect($i)
        If @error Then ContinueLoop

        If $iMouseX >= $aRect[0] And $iMouseX <= $aRect[2] And $iMouseY >= $aRect[1] And $iMouseY <= $aRect[3] Then
            $iSelectedForDelete = $i
            $iDragIndex = $i
            $iOrigStart = $aEvents[$i][2]
            $iOrigDuration = $aEvents[$i][3]
            $bCopyTriggered = False

            If $iViewMode >= 6 Then
                $iDragMode = 1
            Else
                If ($iMouseY - $aRect[1]) <= 6 Then
                    $iDragMode = 2
                ElseIf ($aRect[3] - $iMouseY) <= 6 Then
                    $iDragMode = 3
                Else
                    $iDragMode = 1
                    $iDragOffsetY = $iMouseY - $aRect[1]
                EndIf
            EndIf
            ExitLoop
        EndIf
    Next
    Return $GUI_RUNDEFMSG
EndFunc

Func WM_MOUSEMOVE($hWnd, $iMsg, $wParam, $lParam)
    If $hWnd <> $hCanvas Then Return $GUI_RUNDEFMSG

    Local $iMouseX = BitAND($lParam, 0xFFFF) + $iScrollX
    Local $iMouseY = BitShift($lParam, 16) + $iScrollY - $iSubHeaderH

    If $iDragMode > 0 And $iDragIndex <> -1 And $iViewMode <> 7 Then
        If Not $bCopyTriggered And _IsPressed("11") Then
            _AddEvent($aEvents[$iDragIndex][1], $aEvents[$iDragIndex][2], $aEvents[$iDragIndex][3], _
                      $aEvents[$iDragIndex][4], $aEvents[$iDragIndex][5], $aEvents[$iDragIndex][6])
            $iDragIndex = UBound($aEvents) - 1 
            $bCopyTriggered = True
        EndIf

        If $iViewMode == 6 Then
            Local $sHoverDate = _GetDateFromMonthXY(BitAND($lParam, 0xFFFF), BitShift($lParam, 16))
            If $sHoverDate <> "" Then $aEvents[$iDragIndex][5] = $sHoverDate
        Else
            Local $iCurrentMin = Round($iMouseY / $fZoom)
            $iCurrentMin = Round($iCurrentMin / 15) * 15 

            Switch $iDragMode
                Case 1 
                    Local $iNewStart = Round(($iMouseY - $iDragOffsetY) / $fZoom)
                    $iNewStart = Round($iNewStart / 15) * 15
                    $iNewStart = $iNewStart - ($fTimezoneOffset * 60) ; Remove the visual offset to store the base time

                    If $iNewStart < 0 Then $iNewStart = 0
                    If ($iNewStart + $aEvents[$iDragIndex][3]) > 1440 Then $iNewStart = 1440 - $aEvents[$iDragIndex][3]
                    $aEvents[$iDragIndex][2] = $iNewStart
                    
                    If $iMouseX > $iTimeColWidth Then
                        Local $iColWidth = (($iClientW > $iCanvasWidth ? $iClientW : $iCanvasWidth) - $iTimeColWidth) / _GetColCount()
                        Local $iNewCol = Floor(($iMouseX - $iTimeColWidth) / $iColWidth)
                        If $iNewCol >= 0 And $iNewCol < _GetColCount() Then
                            If _IsPeopleView() Then
                                $aEvents[$iDragIndex][6] = $iNewCol
                            Else
                                $aEvents[$iDragIndex][5] = _DateAdd('d', $iNewCol, $sCurrentDate)
                            EndIf
                        EndIf
                    EndIf

                Case 2
                    If $iCurrentMin < 0 Then $iCurrentMin = 0
                    Local $iEndMin = $iOrigStart + $iOrigDuration
                    Local $iAdjustedMin = $iCurrentMin - ($fTimezoneOffset * 60) ; Remove visual offset
                    If ($iEndMin - $iAdjustedMin) >= 15 Then
                        $aEvents[$iDragIndex][2] = $iAdjustedMin
                        $aEvents[$iDragIndex][3] = $iEndMin - $iAdjustedMin
                    EndIf

                Case 3
                    If $iCurrentMin > 1440 Then $iCurrentMin = 1440
                    Local $iAdjustedMin = $iCurrentMin - ($fTimezoneOffset * 60) ; Remove visual offset
                    If ($iAdjustedMin - $iOrigStart) >= 15 Then
                        $aEvents[$iDragIndex][3] = $iAdjustedMin - $iOrigStart
                    EndIf
            EndSwitch
        EndIf

        _WinAPI_InvalidateRect($hCanvas, 0, True)
        Return $GUI_RUNDEFMSG
    EndIf
    
    If $iViewMode == 7 Then Return $GUI_RUNDEFMSG

    Local $bOnHandle = False
    Local $iRawX = BitAND($lParam, 0xFFFF)
    Local $iRawY = BitShift($lParam, 16)

    For $i = UBound($aEvents) - 1 To 0 Step -1
        Local $aRect = _GetEventScreenRect($i)
        If @error Then ContinueLoop

        If $iRawX >= $aRect[0] And $iRawX <= $aRect[2] And $iRawY >= $aRect[1] And $iRawY <= $aRect[3] Then
            If $iViewMode <= 5 And (($iRawY - $aRect[1]) <= 6 Or ($aRect[3] - $iRawY) <= 6) Then
                _WinAPI_SetCursor(_WinAPI_LoadCursor(0, 32645))
                $bOnHandle = True
            EndIf
            ExitLoop
        EndIf
    Next
    If Not $bOnHandle Then _WinAPI_SetCursor(_WinAPI_LoadCursor(0, 32512))

    Return $GUI_RUNDEFMSG
EndFunc

Func WM_LBUTTONUP($hWnd, $iMsg, $wParam, $lParam)
    If $iDragMode > 0 Then 
        If $iDragIndex <> -1 And $iDragIndex < UBound($aEvents) Then
            _MarkEventModified($iDragIndex)
        EndIf
        _SaveCSV()
    EndIf
    $iDragMode = 0
    $iDragIndex = -1
    Return $GUI_RUNDEFMSG
EndFunc

Func WM_LBUTTONDBLCLK($hWnd, $iMsg, $wParam, $lParam)
    If $hWnd <> $hCanvas Then Return $GUI_RUNDEFMSG
    If BitShift($lParam, 16) < $iSubHeaderH And $iViewMode <> 7 Then Return $GUI_RUNDEFMSG

    Local $iMouseX = BitAND($lParam, 0xFFFF)
    Local $iMouseY = BitShift($lParam, 16)

    If $iViewMode == 7 Then
        Local $aUpIdx = _GetSortedUpcomingIndices()
        Local $iEffectiveW = ($iClientW > $iCanvasWidth ? $iClientW : $iCanvasWidth)
        For $i = 0 To UBound($aUpIdx) - 1
            Local $e = $aUpIdx[$i]
            Local $iCardTop = 20 - $iScrollY + ($i * 115) + 16
            Local $iCardBottom = $iCardTop + 34
            Local $iCardLeft = 35 - $iScrollX
            Local $iCardRight = $iEffectiveW - 35
            
            If $iMouseX >= $iCardLeft And $iMouseX <= $iCardRight And $iMouseY >= $iCardTop And $iMouseY <= $iCardBottom Then
                If _IsPressed("10") Then ; Shift key handling
                    $aEvents[$e][4] = 2
                    _MarkEventModified($e)
                    $iSelectedForDelete = -1
                    _SaveCSV()
                    _UpdateScrollBars()
                    _WinAPI_InvalidateRect($hCanvas, 0, True)
                Else
                    $iSelectedForDelete = $e
                    _OpenInPlaceEdit($e)
                EndIf
                Return $GUI_RUNDEFMSG
            EndIf
        Next
        Return $GUI_RUNDEFMSG
    EndIf

    For $i = UBound($aEvents) - 1 To 0 Step -1
        Local $aRect = _GetEventScreenRect($i)
        If Not @error And $iMouseX >= $aRect[0] And $iMouseX <= $aRect[2] And $iMouseY >= $aRect[1] And $iMouseY <= $aRect[3] Then
            If _IsPressed("10") Then ; Shift key handling
                $aEvents[$i][4] = 2
                _MarkEventModified($i)
                $iSelectedForDelete = -1
                _SaveCSV()
                _UpdateScrollBars()
                _WinAPI_InvalidateRect($hCanvas, 0, True)
            Else
                $iSelectedForDelete = $i
                _OpenInPlaceEdit($i)
            EndIf
            Return $GUI_RUNDEFMSG
        EndIf
    Next

    Local $aColors[5] = [0x039BE5, 0x33B679, 0x8E24AA, 0xF4511E, 0xE67C73]
    Local $iColor = $aColors[Mod(UBound($aEvents), 5)]

    If $iViewMode == 6 Then
        Local $sClickedDate = _GetDateFromMonthXY($iMouseX, $iMouseY)
        If $sClickedDate <> "" Then
            _AddEvent("New Event", 540, 60, $iColor, $sClickedDate, 0)
            _SaveCSV()
            _OpenInPlaceEdit(UBound($aEvents) - 1)
        EndIf
    ElseIf $iViewMode == 7 Then
        Return $GUI_RUNDEFMSG 
    ElseIf ($iMouseX + $iScrollX) > $iTimeColWidth Then
        Local $iEffectiveW = ($iClientW > $iCanvasWidth ? $iClientW : $iCanvasWidth)
        Local $iDayColW = ($iEffectiveW - $iTimeColWidth) / _GetColCount()
        Local $iClickedCol = Floor(($iMouseX + $iScrollX - $iTimeColWidth) / $iDayColW)
        If $iClickedCol >= _GetColCount() Then $iClickedCol = _GetColCount() - 1

        Local $iStartMin = Round((($iMouseY + $iScrollY - $iSubHeaderH) / $fZoom) / 30) * 30
        $iStartMin = $iStartMin - ($fTimezoneOffset * 60) ; Remove the visual offset so created events represent actual base time
        If $iStartMin > (1440 - 60) Then $iStartMin = 1440 - 60
        
        Local $sEvDate = _IsPeopleView() ? $sCurrentDate : _DateAdd('d', $iClickedCol, $sCurrentDate)
        Local $iPersonIdx = _IsPeopleView() ? $iClickedCol : 0

        _AddEvent("New Event", $iStartMin, 60, $iColor, $sEvDate, $iPersonIdx)
        _SaveCSV()
        _OpenInPlaceEdit(UBound($aEvents) - 1)
    EndIf

    Return $GUI_RUNDEFMSG
EndFunc

Func WM_MOUSEWHEEL($hWnd, $iMsg, $wParam, $lParam)
    If $iViewMode == 6 Then Return $GUI_RUNDEFMSG
    _CloseInPlaceEdit(True)
    Local $iDelta = BitShift($wParam, 16)
    $iScrollY -= ($iDelta > 0 ? 60 : -60)
    _UpdateScrollBars()
    _WinAPI_InvalidateRect($hCanvas, 0, True)
    Return $GUI_RUNDEFMSG
EndFunc

Func WM_VSCROLL($hWnd, $iMsg, $wParam, $lParam)
    If $hWnd == $hCanvas And $iViewMode <> 6 Then
        _CloseInPlaceEdit(True)
        Local $iReq = BitAND($wParam, 0xFFFF)
        Local $iVisibleH = $iClientH - $iHeaderH - $iSubHeaderH
        Switch $iReq
            Case 0
                $iScrollY -= 30
            Case 1
                $iScrollY += 30
            Case 2
                $iScrollY -= $iVisibleH
            Case 3
                $iScrollY += $iVisibleH
            Case 4, 5
                $iScrollY = _GetScrollTrackPos($hWnd, $SB_VERT)
        EndSwitch
        _UpdateScrollBars()
        _WinAPI_InvalidateRect($hCanvas, 0, True)
    EndIf
    Return $GUI_RUNDEFMSG
EndFunc

Func WM_HSCROLL($hWnd, $iMsg, $wParam, $lParam)
    If $hWnd == $hCanvas Then
        _CloseInPlaceEdit(True)
        Local $iReq = BitAND($wParam, 0xFFFF)
        Switch $iReq
            Case 0
                $iScrollX -= 30
            Case 1
                $iScrollX += 30
            Case 2
                $iScrollX -= $iClientW
            Case 3
                $iScrollX += $iClientW
            Case 4, 5
                $iScrollX = _GetScrollTrackPos($hWnd, $SB_HORZ)
        EndSwitch
        _UpdateScrollBars()
        _WinAPI_InvalidateRect($hCanvas, 0, True)
    EndIf
    Return $GUI_RUNDEFMSG
EndFunc

; ==============================================================================
; HELPER & CALCULATION UTILITIES
; ==============================================================================
Func _GetSortedUpcomingIndices()
    Local $iTotal = UBound($aEvents)
    Local $aResult[0]
    If $iTotal == 0 Then Return $aResult

    Local $aUpSort[$iTotal][2] ; [Original Index, Sort Key]
    Local $iCount = 0

    For $i = 0 To $iTotal - 1
        If $aEvents[$i][5] >= $sCurrentDate And $aEvents[$i][4] <> 2 Then
            $aUpSort[$iCount][0] = $i
            $aUpSort[$iCount][1] = $aEvents[$i][5] & StringFormat("%04d", $aEvents[$i][2])
            $iCount += 1
        EndIf
    Next

    If $iCount == 0 Then Return $aResult

    ReDim $aUpSort[$iCount][2]
    _ArraySort($aUpSort, 0, 0, 0, 1)

    ReDim $aResult[$iCount]
    For $i = 0 To $iCount - 1
        $aResult[$i] = $aUpSort[$i][0]
    Next

    Return $aResult
EndFunc

Func _GetEventScreenRect($iEventIdx)
    Local $aRect[4]
    If $iEventIdx < 0 Or $iEventIdx >= UBound($aEvents) Then Return SetError(1, 0, $aRect)
    If $aEvents[$iEventIdx][4] == 2 Then Return SetError(1, 0, $aRect)

    If $iViewMode <= 5 Then
        Local $iColIdx = _GetEventColumnIndex($iEventIdx)
        If $iColIdx < 0 Then Return SetError(1, 0, $aRect)
        
        Local $iEffectiveW = ($iClientW > $iCanvasWidth ? $iClientW : $iCanvasWidth)
        Local $iDayColW = ($iEffectiveW - $iTimeColWidth) / _GetColCount()

        Local $iDisplayMin = $aEvents[$iEventIdx][2] + ($fTimezoneOffset * 60) ; Display adjustment

        $aRect[0] = $iTimeColWidth + ($iColIdx * $iDayColW) - $iScrollX + 4
        $aRect[1] = Round($iDisplayMin * $fZoom) - $iScrollY + $iSubHeaderH 
        $aRect[2] = $aRect[0] + $iDayColW - 8
        $aRect[3] = Round(($iDisplayMin + $aEvents[$iEventIdx][3]) * $fZoom) - $iScrollY + $iSubHeaderH
        Return $aRect
        
    ElseIf $iViewMode == 6 Then
        Local $sEvDate = $aEvents[$iEventIdx][5]
        Local $iYear = Int(StringLeft($sEvDate, 4))
        Local $iMonth = Int(StringMid($sEvDate, 6, 2))
        Local $iDay = Int(StringRight($sEvDate, 2))
        
        If $iYear <> Int(StringLeft($sCurrentDate, 4)) Or $iMonth <> Int(StringMid($sCurrentDate, 6, 2)) Then Return SetError(1, 0, $aRect)
        
        Local $iStartDay = _DateToDayOfWeek($iYear, $iMonth, 1)
        Local $iCellIdx = $iDay + $iStartDay - 1
        Local $row = Floor(($iCellIdx - 1) / 7)
        Local $col = Mod(($iCellIdx - 1), 7)
        
        Local $iColW = $iClientW / 7
        Local $iRowH = ($iClientH - $iHeaderH - $iSubHeaderH) / 6
        
        Local $iDayEvents = 0
        Local $iMyPillIdx = -1
        Local $sCellDate = $aEvents[$iEventIdx][5]
        
        Local $aDayEventIdx[0]
        For $e = 0 To UBound($aEvents) - 1
            If $aEvents[$e][5] == $sCellDate And $aEvents[$e][4] <> 2 Then _ArrayAdd($aDayEventIdx, $e)
        Next
        
        For $ei = 0 To UBound($aDayEventIdx) - 2
            For $ej = $ei + 1 To UBound($aDayEventIdx) - 1
                Local $idxI = $aDayEventIdx[$ei], $idxJ = $aDayEventIdx[$ej]
                If $aEvents[$idxJ][2] < $aEvents[$idxI][2] Then
                    $aDayEventIdx[$ei] = $idxJ
                    $aDayEventIdx[$ej] = $idxI
                EndIf
            Next
        Next
        
        For $p = 0 To UBound($aDayEventIdx) - 1
            If $aDayEventIdx[$p] == $iEventIdx Then 
                $iMyPillIdx = $p
                ExitLoop
            EndIf
        Next
        
        Local $iMaxVisible = Floor(($iRowH - 22) / 15)
        If UBound($aDayEventIdx) > $iMaxVisible Then $iMaxVisible -= 1
        
        If $iMyPillIdx == -1 Or $iMyPillIdx >= $iMaxVisible Then Return SetError(1, 0, $aRect)
        
        $aRect[0] = ($col * $iColW) + 4
        $aRect[1] = $iSubHeaderH + ($row * $iRowH) + 22 + ($iMyPillIdx * 15)
        $aRect[2] = ($col * $iColW) + $iColW - 4
        $aRect[3] = $aRect[1] + 13
        Return $aRect
        
    ElseIf $iViewMode == 7 Then
        Local $iEffectiveW = ($iClientW > $iCanvasWidth ? $iClientW : $iCanvasWidth)
        Local $aUpIdx = _GetSortedUpcomingIndices()
        For $i = 0 To UBound($aUpIdx) - 1
            If $aUpIdx[$i] == $iEventIdx Then
                $aRect[0] = 35 - $iScrollX
                $aRect[1] = 20 - $iScrollY + ($i * 115) + 16
                $aRect[2] = $iEffectiveW - 35
                $aRect[3] = $aRect[1] + 34
                Return $aRect
            EndIf
        Next
    EndIf
    Return SetError(1, 0, $aRect)
EndFunc

Func _GetDateFromMonthXY($iX, $iY)
    If $iY < $iSubHeaderH Then Return ""
    
    Local $iColW = $iClientW / 7
    Local $iRowH = ($iClientH - $iHeaderH - $iSubHeaderH) / 6
    
    Local $col = Floor($iX / $iColW)
    Local $row = Floor(($iY - $iSubHeaderH) / $iRowH)
    
    Local $iYear = Int(StringLeft($sCurrentDate, 4))
    Local $iMonth = Int(StringMid($sCurrentDate, 6, 2))
    
    Local $iStartDay = _DateToDayOfWeek($iYear, $iMonth, 1)
    Local $iCellIdx = ($row * 7) + $col + 1
    Local $iDayNum = $iCellIdx - $iStartDay + 1
    
    If $iDayNum >= 1 And $iDayNum <= _DateDaysInMonth($iYear, $iMonth) Then
        Return StringFormat("%04d/%02d/%02d", $iYear, $iMonth, $iDayNum)
    EndIf
    Return ""
EndFunc

Func _GetEventsForCurrentView()
    Local $aResult[0]
    
    Switch $iViewMode
        Case 1
            For $i = 0 To UBound($aEvents) - 1
                If $aEvents[$i][5] == $sCurrentDate And $aEvents[$i][4] <> 2 Then _ArrayAdd($aResult, $i)
            Next
            
        Case 2, 4
            For $i = 0 To UBound($aEvents) - 1
                If $aEvents[$i][4] == 2 Then ContinueLoop
                Local $sDate = $aEvents[$i][5]
                For $d = 0 To 3
                    If $sDate == _DateAdd('d', $d, $sCurrentDate) Then 
                        _ArrayAdd($aResult, $i)
                        ExitLoop
                    EndIf
                Next
            Next
            
        Case 3, 5
            For $i = 0 To UBound($aEvents) - 1
                If $aEvents[$i][4] == 2 Then ContinueLoop
                Local $sDate = $aEvents[$i][5]
                For $d = 0 To 6
                    If $sDate == _DateAdd('d', $d, $sCurrentDate) Then 
                        _ArrayAdd($aResult, $i)
                        ExitLoop
                    EndIf
                Next
            Next
            
        Case 6
            Local $iYear = Int(StringLeft($sCurrentDate, 4))
            Local $iMonth = Int(StringMid($sCurrentDate, 6, 2))
            For $i = 0 To UBound($aEvents) - 1
                If $aEvents[$i][4] == 2 Then ContinueLoop
                Local $sDate = $aEvents[$i][5]
                Local $eYear = Int(StringLeft($sDate, 4))
                Local $eMonth = Int(StringMid($sDate, 6, 2))
                If $eYear == $iYear And $eMonth == $iMonth Then 
                    _ArrayAdd($aResult, $i)
                EndIf
            Next
            
        Case 7
            For $i = 0 To UBound($aEvents) - 1
                If $aEvents[$i][5] >= $sCurrentDate And $aEvents[$i][4] <> 2 Then _ArrayAdd($aResult, $i)
            Next
    EndSwitch
    
    For $i = 0 To UBound($aResult) - 2
        For $j = $i + 1 To UBound($aResult) - 1
            Local $idxI = $aResult[$i], $idxJ = $aResult[$j]
            If $aEvents[$idxJ][5] < $aEvents[$idxI][5] Or _
               ($aEvents[$idxJ][5] == $aEvents[$idxI][5] And $aEvents[$idxJ][2] < $aEvents[$idxI][2]) Then
                $aResult[$i] = $idxJ
                $aResult[$j] = $idxI
            EndIf
        Next
    Next
    
    Return $aResult
EndFunc

Func _GetColCount()
    Switch $iViewMode
        Case 1
            Return 1
        Case 2, 4
            Return 4
        Case 3, 5
            Return 7
    EndSwitch
    Return 1
EndFunc

Func _IsPeopleView()
    Return ($iViewMode == 4 Or $iViewMode == 5)
EndFunc

Func _GetEventColumnIndex($iEventIdx)
    Local $sEvDate    = $aEvents[$iEventIdx][5]
    Local $iPersonIdx = $aEvents[$iEventIdx][6]

    If _IsPeopleView() Then
        If $sEvDate == $sCurrentDate And $iPersonIdx < _GetColCount() Then Return $iPersonIdx
    Else
        Local $iDiff = _DateDiff('d', $sCurrentDate, $sEvDate)
        If $iDiff >= 0 And $iDiff < _GetColCount() Then Return $iDiff
    EndIf
    Return -1
EndFunc

Func _UpdateDateTitle()
    Local $sText = ""
    Switch $iViewMode
        Case 1
            $sText = _FormatDateTitle($sCurrentDate)
        Case 2
            $sText = _FormatDateTitle($sCurrentDate) & " - " & _FormatDateTitle(_DateAdd('d', 3, $sCurrentDate))
        Case 3
            $sText = _FormatDateTitle($sCurrentDate) & " - " & _FormatDateTitle(_DateAdd('d', 6, $sCurrentDate))
        Case 4
            $sText = _FormatDateTitle($sCurrentDate) & " (4 Person Team)"
        Case 5
            $sText = _FormatDateTitle($sCurrentDate) & " (7 Person Team)"
        Case 6
            Local $aMonths = ["January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"]
            $sText = $aMonths[Int(StringMid($sCurrentDate, 6, 2)) - 1] & " " & StringLeft($sCurrentDate, 4)
        Case 7
            $sText = "Upcoming Schedule"
    EndSwitch
    GUICtrlSetData($idLblDateTitle, $sText)
EndFunc

; ==============================================================================
; MULTI-PAGE WIN32 GDI PRINT SYSTEM
; ==============================================================================
Func _PrintSchedule()
    Local $tPRINTDLG = DllStructCreate($tagPRINTDLG)
    DllStructSetData($tPRINTDLG, "Size", DllStructGetSize($tPRINTDLG))
    DllStructSetData($tPRINTDLG, "hwndOwner", $hMainGUI)
    DllStructSetData($tPRINTDLG, "Flags", BitOR(0x00000100, 0x00000004, 0x00040000))
    DllStructSetData($tPRINTDLG, "nFromPage", 1)
    DllStructSetData($tPRINTDLG, "nToPage", 9999) 
    DllStructSetData($tPRINTDLG, "nMinPage", 1)
    DllStructSetData($tPRINTDLG, "nMaxPage", 9999) 
    DllStructSetData($tPRINTDLG, "nCopies", 1)

    If Not _WinAPI_PrintDlg($tPRINTDLG) Then Return

    Local $hPrintDC = DllStructGetData($tPRINTDLG, "hDC")
    If Not $hPrintDC Then Return

    Local $iPageWidth  = _WinAPI_GetDeviceCaps($hPrintDC, 8)  ; HORZRES
    Local $iPageHeight = _WinAPI_GetDeviceCaps($hPrintDC, 10) ; VERTRES
    Local $iDPIX       = _WinAPI_GetDeviceCaps($hPrintDC, 88) ; LOGPIXELSX
    Local $iDPIY       = _WinAPI_GetDeviceCaps($hPrintDC, 90) ; LOGPIXELSY

    Local $iMarginX = Round($iDPIX * 0.25)
    Local $iMarginY = Round($iDPIY * 0.25)
    Local $iUsableW = $iPageWidth - (2 * $iMarginX)
    Local $iUsableH = $iPageHeight - (2 * $iMarginY)

    Local $tDOCINFO = DllStructCreate("int cbSize;ptr lpszDocName;ptr lpszOutput;ptr lpszDatatype;dword fwType")
    DllStructSetData($tDOCINFO, "cbSize", DllStructGetSize($tDOCINFO))
    Local $tDocName = DllStructCreate("wchar[64]")
    DllStructSetData($tDocName, 1, "Calendar Schedule Vector Print")
    DllStructSetData($tDOCINFO, "lpszDocName", DllStructGetPtr($tDocName))

    DllCall("gdi32.dll", "int", "StartDocW", "handle", $hPrintDC, "struct*", $tDOCINFO)

    Switch $iViewMode
        Case 1 To 5
            _PrintTimelineVector($hPrintDC, $iMarginX, $iMarginY, $iUsableW, $iUsableH, $iDPIX, $iDPIY)
        Case 6
            _PrintMonthVector($hPrintDC, $iMarginX, $iMarginY, $iUsableW, $iUsableH, $iDPIX, $iDPIY)
        Case 7
            _PrintUpcomingVector($hPrintDC, $iMarginX, $iMarginY, $iUsableW, $iUsableH, $iDPIX, $iDPIY)
    EndSwitch

    DllCall("gdi32.dll", "int", "EndDoc", "handle", $hPrintDC)
    _WinAPI_DeleteDC($hPrintDC)
EndFunc

Func _PrintTimelineVector($hPrintDC, $iMarginX, $iMarginY, $iUsableW, $iUsableH, $iDPIX, $iDPIY)
    Local $iTitleH     = Round($iDPIY * 0.45)
    Local $iSubHeaderH = Round($iDPIY * 0.35)
    Local $iGridTop    = $iMarginY + $iTitleH + $iSubHeaderH
    Local $iGridH      = $iUsableH - $iTitleH - $iSubHeaderH

    Local $iHourH = Round($iDPIY * 0.65)
    Local $iMinH  = $iHourH / 60

    Local $iMinutesPerPage = Floor(($iGridH / $iHourH) * 60)
    If $iMinutesPerPage < 120 Then $iMinutesPerPage = 120

    Local $iColCount = _GetColCount()
    Local $iTimeColW = Round($iDPIX * 0.75)
    Local $iDayColW  = ($iUsableW - $iTimeColW) / $iColCount

    Local $hFontTitle  = _WinAPI_CreateFont(-Round($iDPIY * 18 / 72), 0, 0, 0, 700, False, False, False, 0, 0, 0, 0, 0, "Segoe UI")
    Local $hFontHeader = _WinAPI_CreateFont(-Round($iDPIY * 11 / 72), 0, 0, 0, 700, False, False, False, 0, 0, 0, 0, 0, "Segoe UI")
    Local $hFontHour   = _WinAPI_CreateFont(-Round($iDPIY * 9 / 72),  0, 0, 0, 400, False, False, False, 0, 0, 0, 0, 0, "Segoe UI")
    Local $hFontEvent  = _WinAPI_CreateFont(-Round($iDPIY * 9 / 72),  0, 0, 0, 600, False, False, False, 0, 0, 0, 0, 0, "Segoe UI")

    Local $hPenGrid   = _GDI_CreatePen(0, 1, 0xE0E0E0)
    Local $hPenBorder = _GDI_CreatePen(0, 1, 0xDADCE0)

    Local $iStartMinPage = 0
    While $iStartMinPage < 1440
        Local $iEndMinPage = $iStartMinPage + $iMinutesPerPage
        If $iEndMinPage > 1440 Then $iEndMinPage = 1440

        DllCall("gdi32.dll", "int", "StartPage", "handle", $hPrintDC)
        _WinAPI_SetBkMode($hPrintDC, $TRANSPARENT)

        Local $sDateTitle = GUICtrlRead($idLblDateTitle)
        Local $hOldFont = _WinAPI_SelectObject($hPrintDC, $hFontTitle)
        _GDI_SetTextColor($hPrintDC, 0x202124)
        Local $tTitleRect = _WinAPI_CreateRect($iMarginX, $iMarginY, $iMarginX + $iUsableW, $iMarginY + $iTitleH)
        _WinAPI_DrawText($hPrintDC, $sDateTitle, $tTitleRect, BitOR($DT_LEFT, $DT_TOP, $DT_SINGLELINE))

        Local $iSubTop = $iMarginY + $iTitleH
        Local $hHeaderBrush = _WinAPI_CreateSolidBrush(0xF8F9FA)
        Local $tHeaderRect = _WinAPI_CreateRect($iMarginX, $iSubTop, $iMarginX + $iUsableW, $iSubTop + $iSubHeaderH)
        _WinAPI_FillRect($hPrintDC, DllStructGetPtr($tHeaderRect), $hHeaderBrush)
        _WinAPI_DeleteObject($hHeaderBrush)

        Local $hOldPen = _WinAPI_SelectObject($hPrintDC, $hPenBorder)
        _WinAPI_MoveTo($hPrintDC, $iMarginX, $iSubTop + $iSubHeaderH)
        _WinAPI_LineTo($hPrintDC, $iMarginX + $iUsableW, $iSubTop + $iSubHeaderH)

        _WinAPI_SelectObject($hPrintDC, $hFontHeader)
        _GDI_SetTextColor($hPrintDC, 0x3C4043)

        For $c = 0 To $iColCount - 1
            Local $iColLeft = $iMarginX + $iTimeColW + ($c * $iDayColW)
            Local $tColRect = _WinAPI_CreateRect($iColLeft, $iSubTop + Round($iDPIY * 0.08), $iColLeft + $iDayColW, $iSubTop + $iSubHeaderH)
            Local $sHeaderText = ""

            If _IsPeopleView() Then
                $sHeaderText = ($c < UBound($aPeople)) ? $aPeople[$c] : "Person " & ($c + 1)
            Else
                Local $sColDate = _DateAdd('d', $c, $sCurrentDate)
                $sHeaderText = _FormatDayHeader($sColDate)
            EndIf

            _WinAPI_DrawText($hPrintDC, $sHeaderText, $tColRect, BitOR($DT_CENTER, $DT_TOP, $DT_SINGLELINE, $DT_END_ELLIPSIS))

            _WinAPI_MoveTo($hPrintDC, $iColLeft, $iSubTop)
            _WinAPI_LineTo($hPrintDC, $iColLeft, $iGridTop + $iGridH)
        Next

        _WinAPI_SelectObject($hPrintDC, $hFontHour)
        _WinAPI_SelectObject($hPrintDC, $hPenGrid)

        Local $iStartHour = Floor($iStartMinPage / 60)
        Local $iEndHour   = Ceiling($iEndMinPage / 60)

        For $iHour = $iStartHour To $iEndHour
            Local $iHourMin = $iHour * 60
            Local $iY = $iGridTop + Round(($iHourMin - $iStartMinPage) * $iMinH)

            If $iY >= $iGridTop And $iY <= ($iGridTop + $iGridH) Then
                _WinAPI_MoveTo($hPrintDC, $iMarginX + $iTimeColW, $iY)
                _WinAPI_LineTo($hPrintDC, $iMarginX + $iUsableW, $iY)

                If $iHour >= 0 And $iHour <= 24 Then
                    Local $sTimeLabel = ($iHour > 12 ? $iHour - 12 : ($iHour == 0 ? 12 : $iHour)) & ($iHour >= 12 ? " PM" : " AM")
                    If $iHour == 12 Then $sTimeLabel = "12 PM"
                    If $iHour == 0 Or $iHour == 24 Then $sTimeLabel = "12 AM"

                    _GDI_SetTextColor($hPrintDC, 0x70757A)
                    Local $tRect = _WinAPI_CreateRect($iMarginX, $iY - Round($iDPIY * 0.08), $iMarginX + $iTimeColW - Round($iDPIX * 0.08), $iY + Round($iDPIY * 0.1))
                    _WinAPI_DrawText($hPrintDC, $sTimeLabel, $tRect, BitOR($DT_RIGHT, $DT_TOP, $DT_SINGLELINE))
                EndIf
            EndIf
        Next

        _WinAPI_SelectObject($hPrintDC, $hPenBorder)
        _WinAPI_MoveTo($hPrintDC, $iMarginX + $iTimeColW, $iSubTop)
        _WinAPI_LineTo($hPrintDC, $iMarginX + $iTimeColW, $iGridTop + $iGridH)

        _WinAPI_SelectObject($hPrintDC, $hFontEvent)
        For $i = 0 To UBound($aEvents) - 1
            If $aEvents[$i][4] == 2 Then ContinueLoop

            Local $iColIdx = _GetEventColumnIndex($i)
            If $iColIdx < 0 Or $iColIdx >= $iColCount Then ContinueLoop

            Local $iEvStart = $aEvents[$i][2] + ($fTimezoneOffset * 60)
            Local $iEvEnd   = $iEvStart + $aEvents[$i][3]

            If $iEvEnd > $iStartMinPage And $iEvStart < $iEndMinPage Then
                Local $iBoxTop    = $iGridTop + Round(($iEvStart - $iStartMinPage) * $iMinH)
                Local $iBoxBottom = $iGridTop + Round(($iEvEnd - $iStartMinPage) * $iMinH)

                If $iBoxTop < $iGridTop Then $iBoxTop = $iGridTop
                If $iBoxBottom > ($iGridTop + $iGridH) Then $iBoxBottom = $iGridTop + $iGridH

                If ($iBoxBottom - $iBoxTop) >= 4 Then
                    Local $iBoxLeft  = $iMarginX + $iTimeColW + ($iColIdx * $iDayColW) + 4
                    Local $iBoxRight = $iBoxLeft + $iDayColW - 8
                    Local $iColor    = $aEvents[$i][4]

                    Local $hBrushEv = _GDI_CreateSolidBrush($iColor)
                    Local $hPenEv   = _GDI_CreatePen(0, 1, _DarkenColor($iColor, 20))

                    _WinAPI_SelectObject($hPrintDC, $hBrushEv)
                    _WinAPI_SelectObject($hPrintDC, $hPenEv)
                    _GDI_RoundRect($hPrintDC, $iBoxLeft, $iBoxTop, $iBoxRight, $iBoxBottom, 8, 8)

                    _GDI_SetTextColor($hPrintDC, _ContrastColor($iColor))
                    Local $iBoxH = $iBoxBottom - $iBoxTop
                    If $iBoxH >= Round($iDPIY * 0.15) Then
                        Local $tTextRect = _WinAPI_CreateRect($iBoxLeft + 6, $iBoxTop + 4, $iBoxRight - 6, $iBoxBottom - 4)
                        Local $sEvText = $aEvents[$i][1] & @CRLF & _MinToTimeString($iEvStart) & " - " & _MinToTimeString($iEvEnd)
                        _WinAPI_DrawText($hPrintDC, $sEvText, $tTextRect, BitOR($DT_LEFT, $DT_TOP, $DT_WORDBREAK, $DT_END_ELLIPSIS))
                    EndIf

                    _WinAPI_DeleteObject($hBrushEv)
                    _WinAPI_DeleteObject($hPenEv)
                EndIf
            EndIf
        Next

        _WinAPI_SelectObject($hPrintDC, $hOldFont)
        _WinAPI_SelectObject($hPrintDC, $hOldPen)

        DllCall("gdi32.dll", "int", "EndPage", "handle", $hPrintDC)
        $iStartMinPage += $iMinutesPerPage
    WEnd

    _WinAPI_DeleteObject($hFontTitle)
    _WinAPI_DeleteObject($hFontHeader)
    _WinAPI_DeleteObject($hFontHour)
    _WinAPI_DeleteObject($hFontEvent)
    _WinAPI_DeleteObject($hPenGrid)
    _WinAPI_DeleteObject($hPenBorder)
EndFunc

Func _PrintMonthVector($hPrintDC, $iMarginX, $iMarginY, $iUsableW, $iUsableH, $iDPIX, $iDPIY)
    DllCall("gdi32.dll", "int", "StartPage", "handle", $hPrintDC)
    _WinAPI_SetBkMode($hPrintDC, $TRANSPARENT)

    Local $iTitleH     = Round($iDPIY * 0.45)
    Local $iSubHeaderH = Round($iDPIY * 0.3)
    Local $iGridH      = $iUsableH - $iTitleH - $iSubHeaderH

    Local $iYear  = Int(StringLeft($sCurrentDate, 4))
    Local $iMonth = Int(StringMid($sCurrentDate, 6, 2))
    Local $iDaysInMonth = _DateDaysInMonth($iYear, $iMonth)
    Local $iStartDayOfWeek = _DateToDayOfWeek($iYear, $iMonth, 1)

    Local $iColW = $iUsableW / 7
    Local $iRowH = $iGridH / 6

    Local $hFontTitle  = _WinAPI_CreateFont(-Round($iDPIY * 18 / 72), 0, 0, 0, 700, False, False, False, 0, 0, 0, 0, 0, "Segoe UI")
    Local $hFontHeader = _WinAPI_CreateFont(-Round($iDPIY * 11 / 72), 0, 0, 0, 700, False, False, False, 0, 0, 0, 0, 0, "Segoe UI")
    Local $hFontDay    = _WinAPI_CreateFont(-Round($iDPIY * 10 / 72), 0, 0, 0, 600, False, False, False, 0, 0, 0, 0, 0, "Segoe UI")
    Local $hFontEvent  = _WinAPI_CreateFont(-Round($iDPIY * 8 / 72),  0, 0, 0, 600, False, False, False, 0, 0, 0, 0, 0, "Segoe UI")

    Local $sDateTitle = GUICtrlRead($idLblDateTitle)
    Local $hOldFont = _WinAPI_SelectObject($hPrintDC, $hFontTitle)
    _GDI_SetTextColor($hPrintDC, 0x202124)
    Local $tTitleRect = _WinAPI_CreateRect($iMarginX, $iMarginY, $iMarginX + $iUsableW, $iMarginY + $iTitleH)
    _WinAPI_DrawText($hPrintDC, $sDateTitle, $tTitleRect, BitOR($DT_LEFT, $DT_TOP, $DT_SINGLELINE))

    Local $iSubTop = $iMarginY + $iTitleH
    Local $hHeaderBrush = _WinAPI_CreateSolidBrush(0xF8F9FA)
    Local $tHeaderRect = _WinAPI_CreateRect($iMarginX, $iSubTop, $iMarginX + $iUsableW, $iSubTop + $iSubHeaderH)
    _WinAPI_FillRect($hPrintDC, DllStructGetPtr($tHeaderRect), $hHeaderBrush)
    _WinAPI_DeleteObject($hHeaderBrush)

    Local $aDaysOfWeek[7] = ["Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"]
    _WinAPI_SelectObject($hPrintDC, $hFontHeader)
    _GDI_SetTextColor($hPrintDC, 0x3C4043)
    For $c = 0 To 6
        Local $tColRect = _WinAPI_CreateRect($iMarginX + ($c * $iColW), $iSubTop + Round($iDPIY * 0.05), $iMarginX + (($c + 1) * $iColW), $iSubTop + $iSubHeaderH)
        _WinAPI_DrawText($hPrintDC, $aDaysOfWeek[$c], $tColRect, BitOR($DT_CENTER, $DT_TOP, $DT_SINGLELINE))
    Next

    Local $iGridTop = $iSubTop + $iSubHeaderH
    Local $hPenBorder = _GDI_CreatePen(0, 1, 0xDADCE0)
    Local $hBrushGrey  = _WinAPI_CreateSolidBrush(0xF8F9FA)
    Local $hOldPen = _WinAPI_SelectObject($hPrintDC, $hPenBorder)

    Local $iPillH = Round($iDPIY * 0.16)
    Local $iPillGap = Round($iDPIY * 0.02)
    Local $iNumH = Round($iDPIY * 0.2)

    For $row = 0 To 5
        For $col = 0 To 6
            Local $iCellIdx = ($row * 7) + $col + 1
            Local $iDayNum = $iCellIdx - $iStartDayOfWeek + 1
            Local $iX1 = $iMarginX + ($col * $iColW)
            Local $iY1 = $iGridTop + ($row * $iRowH)
            Local $iX2 = $iX1 + $iColW
            Local $iY2 = $iY1 + $iRowH

            Local $tCellRect = _WinAPI_CreateRect($iX1, $iY1, $iX2, $iY2)

            If $iDayNum >= 1 And $iDayNum <= $iDaysInMonth Then
                Local $sCellDate = StringFormat("%04d/%02d/%02d", $iYear, $iMonth, $iDayNum)

                _WinAPI_SelectObject($hPrintDC, $hFontDay)
                _GDI_SetTextColor($hPrintDC, 0x3C4043)
                Local $tNumRect = _WinAPI_CreateRect($iX1, $iY1 + 4, $iX2 - Round($iDPIX * 0.05), $iY1 + $iNumH)
                _WinAPI_DrawText($hPrintDC, String($iDayNum), $tNumRect, BitOR($DT_RIGHT, $DT_TOP, $DT_SINGLELINE))

                Local $aDayEventIdx[0]
                For $e = 0 To UBound($aEvents) - 1
                    If $aEvents[$e][5] == $sCellDate And $aEvents[$e][4] <> 2 Then _ArrayAdd($aDayEventIdx, $e)
                Next

                For $ei = 0 To UBound($aDayEventIdx) - 2
                    For $ej = $ei + 1 To UBound($aDayEventIdx) - 1
                        Local $idxI = $aDayEventIdx[$ei], $idxJ = $aDayEventIdx[$ej]
                        If $aEvents[$idxJ][2] < $aEvents[$idxI][2] Then
                            $aDayEventIdx[$ei] = $idxJ
                            $aDayEventIdx[$ej] = $idxI
                        EndIf
                    Next
                Next

                Local $iDayEvents = UBound($aDayEventIdx)
                Local $iMaxVisible = Floor(($iRowH - $iNumH - 10) / ($iPillH + $iPillGap))
                If $iDayEvents > $iMaxVisible Then $iMaxVisible -= 1

                _WinAPI_SelectObject($hPrintDC, $hFontEvent)
                For $p = 0 To $iDayEvents - 1
                    If $p < $iMaxVisible Then
                        Local $eIdx = $aDayEventIdx[$p]
                        Local $iPillTop = $iY1 + $iNumH + ($p * ($iPillH + $iPillGap))
                        Local $iPillBottom = $iPillTop + $iPillH
                        Local $iPillLeft = $iX1 + 4
                        Local $iPillRight = $iX2 - 4

                        Local $iColor = $aEvents[$eIdx][4]
                        Local $hBrushEv = _GDI_CreateSolidBrush($iColor)
                        Local $hPenEv   = _GDI_CreatePen(0, 1, _DarkenColor($iColor, 15))

                        _WinAPI_SelectObject($hPrintDC, $hPenEv)
                        _WinAPI_SelectObject($hPrintDC, $hBrushEv)
                        _GDI_RoundRect($hPrintDC, $iPillLeft, $iPillTop, $iPillRight, $iPillBottom, 6, 6)

                        _GDI_SetTextColor($hPrintDC, _ContrastColor($iColor))
                        Local $tEvRect = _WinAPI_CreateRect($iPillLeft + 4, $iPillTop + 2, $iPillRight - 4, $iPillBottom)
                        _WinAPI_DrawText($hPrintDC, $aEvents[$eIdx][1], $tEvRect, BitOR($DT_LEFT, $DT_TOP, $DT_SINGLELINE, $DT_END_ELLIPSIS))

                        _WinAPI_DeleteObject($hPenEv)
                        _WinAPI_DeleteObject($hBrushEv)
                    ElseIf $p == $iMaxVisible And $iDayEvents > $iMaxVisible Then
                        _GDI_SetTextColor($hPrintDC, 0x5F6368)
                        Local $tMoreRect = _WinAPI_CreateRect($iX1 + 6, $iY1 + $iNumH + ($p * ($iPillH + $iPillGap)), $iX2 - 6, $iY2)
                        _WinAPI_DrawText($hPrintDC, "+" & ($iDayEvents - $iMaxVisible) & " more", $tMoreRect, BitOR($DT_LEFT, $DT_TOP, $DT_SINGLELINE))
                    EndIf
                Next
            Else
                _WinAPI_FillRect($hPrintDC, DllStructGetPtr($tCellRect), $hBrushGrey)
            EndIf

            _WinAPI_SelectObject($hPrintDC, $hPenBorder)
            _WinAPI_MoveTo($hPrintDC, $iX1, $iY1)
            _WinAPI_LineTo($hPrintDC, $iX2, $iY1)
            _WinAPI_LineTo($hPrintDC, $iX2, $iY2)
            _WinAPI_LineTo($hPrintDC, $iX1, $iY2)
            _WinAPI_LineTo($hPrintDC, $iX1, $iY1)
        Next
    Next

    _WinAPI_SelectObject($hPrintDC, $hOldFont)
    _WinAPI_SelectObject($hPrintDC, $hOldPen)
    _WinAPI_DeleteObject($hFontTitle)
    _WinAPI_DeleteObject($hFontHeader)
    _WinAPI_DeleteObject($hFontDay)
    _WinAPI_DeleteObject($hFontEvent)
    _WinAPI_DeleteObject($hPenBorder)
    _WinAPI_DeleteObject($hBrushGrey)

    DllCall("gdi32.dll", "int", "EndPage", "handle", $hPrintDC)
EndFunc

Func _PrintUpcomingVector($hPrintDC, $iMarginX, $iMarginY, $iUsableW, $iUsableH, $iDPIX, $iDPIY)
    Local $aUpIdx = _GetSortedUpcomingIndices()
    Local $iCount = UBound($aUpIdx)

    Local $iTitleH  = Round($iDPIY * 0.5)
    Local $iCardH   = Round($iDPIY * 0.9)
    Local $iCardGap = Round($iDPIY * 0.15)

    Local $hFontTitle = _WinAPI_CreateFont(-Round($iDPIY * 18 / 72), 0, 0, 0, 700, False, False, False, 0, 0, 0, 0, 0, "Segoe UI")
    Local $hFontItem  = _WinAPI_CreateFont(-Round($iDPIY * 14 / 72), 0, 0, 0, 700, False, False, False, 0, 0, 0, 0, 0, "Segoe UI")
    Local $hFontText  = _WinAPI_CreateFont(-Round($iDPIY * 11 / 72), 0, 0, 0, 400, False, False, False, 0, 0, 0, 0, 0, "Segoe UI")

    Local $iCurrentY = _StartPageHeader($hPrintDC, $hFontTitle, $iMarginX, $iMarginY, $iUsableW, $iTitleH)

    If $iCount == 0 Then
        Local $hOld = _WinAPI_SelectObject($hPrintDC, $hFontText)
        _GDI_SetTextColor($hPrintDC, 0x5F6368)
        Local $tRect = _WinAPI_CreateRect($iMarginX, $iCurrentY, $iMarginX + $iUsableW, $iCurrentY + $iCardH)
        _WinAPI_DrawText($hPrintDC, "No upcoming events found.", $tRect, BitOR($DT_LEFT, $DT_TOP, $DT_SINGLELINE))
        _WinAPI_SelectObject($hPrintDC, $hOld)
        DllCall("gdi32.dll", "int", "EndPage", "handle", $hPrintDC)
    Else
        Local $i = 0
        While $i < $iCount
            If ($iCurrentY + $iCardH) > ($iMarginY + $iUsableH) Then
                DllCall("gdi32.dll", "int", "EndPage", "handle", $hPrintDC)
                $iCurrentY = _StartPageHeader($hPrintDC, $hFontTitle, $iMarginX, $iMarginY, $iUsableW, $iTitleH)
            EndIf

            Local $e = $aUpIdx[$i]
            Local $sDate = _FormatDateTitle($aEvents[$e][5])
            Local $sTime = _MinToTimeString($aEvents[$e][2]) & " - " & _MinToTimeString($aEvents[$e][2] + $aEvents[$e][3])
            Local $sPerson = ""
            If $aEvents[$e][6] < UBound($aPeople) Then $sPerson = $aPeople[$aEvents[$e][6]]

            Local $iLeft = $iMarginX
            Local $iRight = $iMarginX + $iUsableW
            Local $iTop = $iCurrentY
            Local $iBottom = $iCurrentY + $iCardH

            Local $iColorBarW = Round($iDPIX * 0.12)
            Local $hBrushEv = _GDI_CreateSolidBrush($aEvents[$e][4])
            Local $tBarRect = _WinAPI_CreateRect($iLeft, $iTop, $iLeft + $iColorBarW, $iBottom)
            _WinAPI_FillRect($hPrintDC, DllStructGetPtr($tBarRect), $hBrushEv)
            _WinAPI_DeleteObject($hBrushEv)

            Local $hBrushBg = _WinAPI_CreateSolidBrush(0xF8F9FA)
            Local $tBgRect = _WinAPI_CreateRect($iLeft + $iColorBarW, $iTop, $iRight, $iBottom)
            _WinAPI_FillRect($hPrintDC, DllStructGetPtr($tBgRect), $hBrushBg)
            _WinAPI_DeleteObject($hBrushBg)

            Local $hPenLine = _GDI_CreatePen(0, 1, 0xDADCE0)
            Local $hOldPen = _WinAPI_SelectObject($hPrintDC, $hPenLine)
            _WinAPI_MoveTo($hPrintDC, $iLeft, $iTop)
            _WinAPI_LineTo($hPrintDC, $iRight, $iTop)
            _WinAPI_LineTo($hPrintDC, $iRight, $iBottom)
            _WinAPI_LineTo($hPrintDC, $iLeft, $iBottom)
            _WinAPI_LineTo($hPrintDC, $iLeft, $iTop)
            _WinAPI_SelectObject($hPrintDC, $hOldPen)
            _WinAPI_DeleteObject($hPenLine)

            Local $iPadX = Round($iDPIX * 0.2)
            Local $iTextLeft = $iLeft + $iColorBarW + $iPadX
            Local $hOldFont = _WinAPI_SelectObject($hPrintDC, $hFontItem)
            _GDI_SetTextColor($hPrintDC, 0x202124)
            Local $tTitleRect = _WinAPI_CreateRect($iTextLeft, $iTop + Round($iDPIY * 0.12), $iRight - $iPadX, $iTop + Round($iDPIY * 0.45))
            _WinAPI_DrawText($hPrintDC, $aEvents[$e][1], $tTitleRect, BitOR($DT_LEFT, $DT_TOP, $DT_SINGLELINE, $DT_END_ELLIPSIS))

            _WinAPI_SelectObject($hPrintDC, $hFontText)
            _GDI_SetTextColor($hPrintDC, 0x5F6368)
            Local $tDescRect = _WinAPI_CreateRect($iTextLeft, $iTop + Round($iDPIY * 0.5), $iRight - $iPadX, $iBottom - Round($iDPIY * 0.1))
            _WinAPI_DrawText($hPrintDC, $sDate & "   •   " & $sTime & "   •   " & $sPerson, $tDescRect, BitOR($DT_LEFT, $DT_TOP, $DT_SINGLELINE, $DT_END_ELLIPSIS))
            _WinAPI_SelectObject($hPrintDC, $hOldFont)

            $iCurrentY += $iCardH + $iCardGap
            $i += 1
        WEnd
        DllCall("gdi32.dll", "int", "EndPage", "handle", $hPrintDC)
    EndIf

    _WinAPI_DeleteObject($hFontTitle)
    _WinAPI_DeleteObject($hFontItem)
    _WinAPI_DeleteObject($hFontText)
EndFunc

Func _StartPageHeader($hPrintDC, $hFontTitle, $iMarginX, $iMarginY, $iUsableW, $iTitleH, $sTitleText = "Upcoming Schedule")
    DllCall("gdi32.dll", "int", "StartPage", "handle", $hPrintDC)
    _WinAPI_SetBkMode($hPrintDC, $TRANSPARENT)

    Local $hOld = _WinAPI_SelectObject($hPrintDC, $hFontTitle)
    _GDI_SetTextColor($hPrintDC, 0x202124)
    Local $tRect = _WinAPI_CreateRect($iMarginX, $iMarginY, $iMarginX + $iUsableW, $iMarginY + $iTitleH)
    _WinAPI_DrawText($hPrintDC, $sTitleText, $tRect, BitOR($DT_LEFT, $DT_TOP, $DT_SINGLELINE))
    _WinAPI_SelectObject($hPrintDC, $hOld)

    Return $iMarginY + $iTitleH
EndFunc

; ==============================================================================
; UTILITY FORMATTING FUNCTIONS
; ==============================================================================
Func _MinToTimeString($iMin)
    Local $iH = Floor($iMin / 60)
    Local $iM = Mod($iMin, 60)
    Local $sAmpm = "AM"
    If $iH >= 12 Then
        $sAmpm = "PM"
        If $iH > 12 Then $iH -= 12
    ElseIf $iH == 0 Then
        $iH = 12
    EndIf
    Return StringFormat("%d:%02d %s", $iH, $iM, $sAmpm)
EndFunc

Func _FormatDateTitle($sDateStr)
    Local $aMonths = ["Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"]
    Local $iY = Int(StringLeft($sDateStr, 4))
    Local $iM = Int(StringMid($sDateStr, 6, 2))
    Local $iD = Int(StringRight($sDateStr, 2))
    Return $aMonths[$iM - 1] & " " & $iD & ", " & $iY
EndFunc

Func _FormatDayHeader($sDate)
    Local $iYear = Int(StringLeft($sDate, 4))
    Local $iMonth = Int(StringMid($sDate, 6, 2))
    Local $iDay = Int(StringRight($sDate, 2))
    Local $aDaysOfWeek[7] = ["Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"]
    Local $iDayOfWeek = _DateToDayOfWeek($iYear, $iMonth, $iDay)
    Return $aDaysOfWeek[$iDayOfWeek - 1] & " " & StringFormat("%02d/%02d", $iMonth, $iDay)
EndFunc

Func _DarkenColor($iRGB, $iPercent)
    Local $iR = BitAND(BitShift($iRGB, 16), 0xFF)
    Local $iG = BitAND(BitShift($iRGB, 8), 0xFF)
    Local $iB = BitAND($iRGB, 0xFF)

    $iR = Int($iR * (100 - $iPercent) / 100)
    $iG = Int($iG * (100 - $iPercent) / 100)
    $iB = Int($iB * (100 - $iPercent) / 100)

    Return BitOR(BitShift($iR, -16), BitShift($iG, -8), $iB)
EndFunc

Func _ContrastColor($iRGB)
    Local $iR = BitAND(BitShift($iRGB, 16), 0xFF)
    Local $iG = BitAND(BitShift($iRGB, 8), 0xFF)
    Local $iB = BitAND($iRGB, 0xFF)
    Local $fLuma = (0.299 * $iR) + (0.587 * $iG) + (0.114 * $iB)
    Return ($fLuma > 128) ? 0x000000 : 0xFFFFFF
EndFunc

Func GetIPAddresses()
    Local $sAllIPs = ""

    ; First call to GetIpAddrTable to get the required buffer size
    Local $aRet = DllCall("iphlpapi.dll", "dword", "GetIpAddrTable", "ptr", 0, "dword*", 0, "int", 1)
    If @error Or $aRet[2] = 0 Then Return ""

    Local $iSize = $aRet[2]
    Local $tBuffer = DllStructCreate("byte[" & $iSize & "]")

    ; Second call to GetIpAddrTable to populate the buffer with network data
    $aRet = DllCall("iphlpapi.dll", "dword", "GetIpAddrTable", "ptr", DllStructGetPtr($tBuffer), "dword*", $iSize, "int", 1)
    If @error Or $aRet[0] <> 0 Then Return ""

    ; Extract the number of entries from the first 4 bytes
    Local $iNumEntries = DllStructGetData(DllStructCreate("dword", DllStructGetPtr($tBuffer)), 1)
    Local $iOffset = 4 ; Skip dwNumEntries header

    ; Loop through each MIB_IPADDRROW structure (24 bytes per entry)
    For $i = 0 To $iNumEntries - 1
        Local $tRow = DllStructCreate("dword;dword;dword;dword;dword;ulong", DllStructGetPtr($tBuffer) + $iOffset)
        Local $iIpVal = DllStructGetData($tRow, 1)
        
        $iOffset += 24 
        
        If $iIpVal = 0 Then ContinueLoop
        
        ; Convert numeric DWORD IP to standard dotted-quad string notation
        Local $sIP = BitAND($iIpVal, 0xFF) & "." & _
                     BitAND(BitShift($iIpVal, 8), 0xFF) & "." & _
                     BitAND(BitShift($iIpVal, 16), 0xFF) & "." & _
                     BitAND(BitShift($iIpVal, 24), 0xFF)
                     
        ; Exclude loopback address
        If $sIP <> "127.0.0.1" Then
            If $sAllIPs <> "" Then
                $sAllIPs &= '¦'
            EndIf
            $sAllIPs &= $sIP
        EndIf
    Next

    Return $sAllIPs
EndFunc