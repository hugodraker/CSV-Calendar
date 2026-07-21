;* ============================================================================
; * CSV Calendar
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
#include <WinAPIDlg.au3> ; Added for _WinAPI_PrintDlg and $tagPRINTDLG

Opt("GUIOnEventMode", 0)
Opt("MouseCoordMode", 2) ; Client coordinates

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

; --- View & Date State ---
; ViewMode: 1 = 1 Day, 2 = 4 Day, 3 = Week, 4 = 4 Person, 5 = 7 Person, 6 = Month, 7 = Upcoming Schedule
Global $iViewMode = 1 
Global $sCurrentDate = _NowCalcDate() ; YYYY/MM/DD format
Global $aPeople[] = ["Alice", "Bob", "Charlie", "David", "Eve", "Frank", "Grace"]

; --- Event Data Structure: [Title, StartMin, DurationMin, RGBColor, DateStr (YYYY/MM/DD), PersonIdx] ---
Global $aEvents[0][6]

; --- File I/O Configuration ---
Local $sBaseName = StringRegExpReplace(@ScriptName, "\.[^.]*$", "")
Global $sCSVFile = @ScriptDir & "\" & $sBaseName & ".csv"

; Load from CSV instead of dummy data
_LoadCSV()

; --- Interaction State Variables ---
Global $iDragMode = 0       ; 0 = None, 1 = Move/Copy, 2 = Resize Top, 3 = Resize Bottom
Global $iDragIndex = -1
Global $iDragOffsetY = 0
Global $iOrigStart = 0, $iOrigDuration = 0
Global $bCopyTriggered = False ; Prevents multiple duplicates during a single Ctrl+Drag
Global $iEditingIndex = -1     ; Track index of event currently being text-edited

; ==============================================================================
; CREATE MAIN GUI & NATIVE CONTROLS
; ==============================================================================
Global $hMainGUI = GUICreate("CSV Calendar", $iClientW, $iClientH, -1, -1, _
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
Global $idBtnPrint  = GUICtrlCreateButton("Print", 330, 12, 60, 26)
Global $idBtnExport = GUICtrlCreateButton("Export", 395, 12, 60, 26)

; View Selection Dropdown
Global $idComboView = GUICtrlCreateCombo("", 465, 13, 140, 25, $CBS_DROPDOWNLIST)
GUICtrlSetData($idComboView, "1 Day View|4 Day View|Week View|4 Person View|7 Person View|Month View|Upcoming Schedule", "1 Day View")

; Prominent Date Title Label
Global $idLblDateTitle = GUICtrlCreateLabel("", 615, 10, 420, 32)
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
    Switch GUIGetMsg()
        Case $GUI_EVENT_CLOSE
            Exit

        Case $idBtnZoomIn
            _SetZoom($fZoom + 0.2)

        Case $idBtnZoomOut
            _SetZoom($fZoom - 0.2)

        Case $idBtnPrevDay
            _CloseInPlaceEdit(True)
            $sCurrentDate = _DateAdd('d', -1, $sCurrentDate)
            _UpdateDateTitle()
            _WinAPI_InvalidateRect($hCanvas, 0, True)

        Case $idBtnNextDay
            _CloseInPlaceEdit(True)
            $sCurrentDate = _DateAdd('d', 1, $sCurrentDate)
            _UpdateDateTitle()
            _WinAPI_InvalidateRect($hCanvas, 0, True)

        Case $idBtnPrev
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
            _WinAPI_InvalidateRect($hCanvas, 0, True)

        Case $idBtnNext
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
            _WinAPI_InvalidateRect($hCanvas, 0, True)
            
        Case $idBtnPrint
            _PrintSchedule()

        Case $idBtnExport
            _ExportUpcomingSchedule()

        Case $idComboView
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
            _UpdateDateTitle()
            _UpdateScrollBars()
            _WinAPI_InvalidateRect($hMainGUI, 0, True)
            _WinAPI_InvalidateRect($hCanvas, 0, True)
            _WinAPI_RedrawWindow($hCanvas, 0, 0, BitOR($RDW_INVALIDATE, $RDW_UPDATENOW, $RDW_ERASE, $RDW_ALLCHILDREN))
            GUICtrlSetState($idLblDateTitle, $GUI_FOCUS)
    EndSwitch
WEnd

; ==============================================================================
; FILE I/O MANAGEMENT (CSV & EXPORT)
; ==============================================================================
Func _LoadCSV()
    If Not FileExists($sCSVFile) Then Return
    Local $aLines = FileReadToArray($sCSVFile)
    If @error Then Return
    For $i = 1 To UBound($aLines) - 1 ; Skip header
        Local $aParts = StringSplit($aLines[$i], ",", 2)
        If UBound($aParts) >= 6 Then
            Local $sTitle = StringReplace($aParts[0], "%2C", ",")
            $sTitle = StringReplace($sTitle, "%0A", @CRLF)
            _AddEvent($sTitle, Int($aParts[1]), Int($aParts[2]), Int($aParts[3]), $aParts[4], Int($aParts[5]))
        EndIf
    Next
EndFunc

Func _SaveCSV()
    Local $hFile = FileOpen($sCSVFile, 2) ; Overwrite mode
    FileWriteLine($hFile, "Title,StartMin,Duration,Color,Date,PersonIdx")
    For $i = 0 To UBound($aEvents) - 1
        Local $sTitle = StringReplace($aEvents[$i][0], ",", "%2C")
        $sTitle = StringReplace($sTitle, @CRLF, "%0A")
        $sTitle = StringReplace($sTitle, @CR, "%0A")
        $sTitle = StringReplace($sTitle, @LF, "%0A")
        FileWriteLine($hFile, $sTitle & "," & $aEvents[$i][1] & "," & $aEvents[$i][2] & "," & $aEvents[$i][3] & "," & $aEvents[$i][4] & "," & $aEvents[$i][5])
    Next
    FileClose($hFile)
EndFunc

Func _ExportUpcomingSchedule()
    Local $sSavePath = FileSaveDialog("Export Upcoming Schedule", @DesktopDir, "Text Files (*.txt)", BitOR($FO_OVERWRITE, $FD_PATHMUSTEXIST), "Upcoming_Schedule.txt", $hMainGUI)
    If @error Or $sSavePath == "" Then Return
    If StringRight($sSavePath, 4) <> ".txt" Then $sSavePath &= ".txt"

    Local $aUpIdx[0]
    For $i = 0 To UBound($aEvents) - 1
        If $aEvents[$i][4] >= $sCurrentDate Then _ArrayAdd($aUpIdx, $i)
    Next

    For $i = 0 To UBound($aUpIdx) - 2
        For $j = $i + 1 To UBound($aUpIdx) - 1
            Local $idxI = $aUpIdx[$i], $idxJ = $aUpIdx[$j]
            If $aEvents[$idxJ][4] < $aEvents[$idxI][4] Or ($aEvents[$idxJ][4] == $aEvents[$idxI][4] And $aEvents[$idxJ][1] < $aEvents[$idxI][1]) Then
                $aUpIdx[$i] = $idxJ
                $aUpIdx[$j] = $idxI
            EndIf
        Next
    Next

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
            Local $sDate = _FormatDateTitle($aEvents[$e][4])
            Local $sTime = _MinToTimeString($aEvents[$e][1]) & " - " & _MinToTimeString($aEvents[$e][1] + $aEvents[$e][2])
            Local $sPerson = ""
            If $aEvents[$e][5] < UBound($aPeople) Then $sPerson = $aPeople[$aEvents[$e][5]]

            FileWriteLine($hFile, $aEvents[$e][0])
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
        
        GUICtrlSetData($idInPlaceEdit, $aEvents[$iEditingIndex][0])
        GUICtrlSetPos($idInPlaceEdit, $aRect[0], $aRect[1], $iW, $iH)
        GUICtrlSetState($idInPlaceEdit, $GUI_SHOW)
        GUICtrlSetState($idInPlaceEdit, $GUI_FOCUS)
    EndIf
EndFunc

Func _CloseInPlaceEdit($bSave = True)
    If $iEditingIndex <> -1 Then
        If $bSave Then 
            $aEvents[$iEditingIndex][0] = GUICtrlRead($idInPlaceEdit)
            _SaveCSV()
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
                If $aEvents[$i][4] >= $sCurrentDate Then $iCount += 1
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
        Local $iColIdx = _GetEventColumnIndex($i)
        If $iColIdx < 0 Or $iColIdx >= $iColCount Then ContinueLoop

        Local $iStartMin = $aEvents[$i][1]
        Local $iDuration = $aEvents[$i][2]
        Local $iColor    = $aEvents[$i][3]

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
                Local $sDisplayText = $aEvents[$i][0] & @CRLF & $sEventTime
                
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

; ==============================================================================
; FIXED MONTH VIEW DRAWING
; ==============================================================================
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
                    If $aEvents[$e][4] == $sCellDate Then _ArrayAdd($aDayEventIdx, $e)
                Next
                
                For $ei = 0 To UBound($aDayEventIdx) - 2
                    For $ej = $ei + 1 To UBound($aDayEventIdx) - 1
                        Local $idxI = $aDayEventIdx[$ei], $idxJ = $aDayEventIdx[$ej]
                        If $aEvents[$idxJ][1] < $aEvents[$idxI][1] Then
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

                        Local $iColor = $aEvents[$eIdx][3]
                        
                        Local $hBrushEv = _GDI_CreateSolidBrush($iColor)
                        Local $hPenEv   = _GDI_CreatePen(0, 1, _DarkenColor($iColor, 15))
                        
                        _WinAPI_SelectObject($hMemDC, $hPenEv)
                        _WinAPI_SelectObject($hMemDC, $hBrushEv)
                        
                        _GDI_RoundRect($hMemDC, $iPillLeft, $iPillTop, $iPillRight, $iPillBottom, 4, 4)

                        If $eIdx <> $iEditingIndex Then
                            _GDI_SetTextColor($hMemDC, _ContrastColor($iColor))
                            Local $tEvRect = _WinAPI_CreateRect($iPillLeft + 5, $iPillTop + 2, $iPillRight - 5, $iPillBottom - 2)
                            _WinAPI_DrawText($hMemDC, $aEvents[$eIdx][0], $tEvRect, BitOR($DT_LEFT, $DT_TOP, $DT_SINGLELINE, $DT_END_ELLIPSIS))
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
    
    Local $aUpIdx[0]
    For $i = 0 To UBound($aEvents) - 1
        If $aEvents[$i][4] >= $sCurrentDate Then _ArrayAdd($aUpIdx, $i)
    Next
    
    For $i = 0 To UBound($aUpIdx) - 2
        For $j = $i + 1 To UBound($aUpIdx) - 1
            Local $idxI = $aUpIdx[$i], $idxJ = $aUpIdx[$j]
            If $aEvents[$idxJ][4] < $aEvents[$idxI][4] Or ($aEvents[$idxJ][4] == $aEvents[$idxI][4] And $aEvents[$idxJ][1] < $aEvents[$idxI][1]) Then
                $aUpIdx[$i] = $idxJ
                $aUpIdx[$j] = $idxI
            EndIf
        Next
    Next
    
    Local $iY = 20 - $iScrollY
    For $i = 0 To UBound($aUpIdx) - 1
        Local $e = $aUpIdx[$i]
        
        If $iY + 100 > 0 And $iY < $iCanvasH Then
            Local $sDate = _FormatDateTitle($aEvents[$e][4])
            Local $sTime = _MinToTimeString($aEvents[$e][1]) & " - " & _MinToTimeString($aEvents[$e][1] + $aEvents[$e][2])
            Local $sPerson = ""
            If $aEvents[$e][5] < UBound($aPeople) Then $sPerson = $aPeople[$aEvents[$e][5]]
            
            Local $iCardLeft = 20 - $iScrollX
            Local $iCardRight = $iEffectiveW - 20 - $iScrollX
            Local $iCardTop = $iY
            Local $iCardBottom = $iY + 95
            
            Local $hBrushEv = _GDI_CreateSolidBrush($aEvents[$e][3])
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
                _WinAPI_DrawText($hMemDC, $aEvents[$e][0], $tTitle, BitOR($DT_LEFT, $DT_TOP, $DT_SINGLELINE, $DT_END_ELLIPSIS))
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

    For $i = UBound($aEvents) - 1 To 0 Step -1
        Local $aRect = _GetEventScreenRect($i)
        If @error Then ContinueLoop

        If $iMouseX >= $aRect[0] And $iMouseX <= $aRect[2] And $iMouseY >= $aRect[1] And $iMouseY <= $aRect[3] Then
            $iDragIndex = $i
            $iOrigStart = $aEvents[$i][1]
            $iOrigDuration = $aEvents[$i][2]
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
            _AddEvent($aEvents[$iDragIndex][0], $aEvents[$iDragIndex][1], $aEvents[$iDragIndex][2], _
                      $aEvents[$iDragIndex][3], $aEvents[$iDragIndex][4], $aEvents[$iDragIndex][5])
            $iDragIndex = UBound($aEvents) - 1 
            $bCopyTriggered = True
        EndIf

        If $iViewMode == 6 Then
            Local $sHoverDate = _GetDateFromMonthXY(BitAND($lParam, 0xFFFF), BitShift($lParam, 16))
            If $sHoverDate <> "" Then $aEvents[$iDragIndex][4] = $sHoverDate
        Else
            Local $iCurrentMin = Round($iMouseY / $fZoom)
            $iCurrentMin = Round($iCurrentMin / 15) * 15 

            Switch $iDragMode
                Case 1 
                    Local $iNewStart = Round(($iMouseY - $iDragOffsetY) / $fZoom)
                    $iNewStart = Round($iNewStart / 15) * 15
                    If $iNewStart < 0 Then $iNewStart = 0
                    If ($iNewStart + $aEvents[$iDragIndex][2]) > 1440 Then $iNewStart = 1440 - $aEvents[$iDragIndex][2]
                    $aEvents[$iDragIndex][1] = $iNewStart
                    
                    If $iMouseX > $iTimeColWidth Then
                        Local $iColWidth = (($iClientW > $iCanvasWidth ? $iClientW : $iCanvasWidth) - $iTimeColWidth) / _GetColCount()
                        Local $iNewCol = Floor(($iMouseX - $iTimeColWidth) / $iColWidth)
                        If $iNewCol >= 0 And $iNewCol < _GetColCount() Then
                            If _IsPeopleView() Then
                                $aEvents[$iDragIndex][5] = $iNewCol
                            Else
                                $aEvents[$iDragIndex][4] = _DateAdd('d', $iNewCol, $sCurrentDate)
                            EndIf
                        EndIf
                    EndIf

                Case 2
                    If $iCurrentMin < 0 Then $iCurrentMin = 0
                    Local $iEndMin = $iOrigStart + $iOrigDuration
                    If ($iEndMin - $iCurrentMin) >= 15 Then
                        $aEvents[$iDragIndex][1] = $iCurrentMin
                        $aEvents[$iDragIndex][2] = $iEndMin - $iCurrentMin
                    EndIf

                Case 3
                    If $iCurrentMin > 1440 Then $iCurrentMin = 1440
                    If ($iCurrentMin - $iOrigStart) >= 15 Then
                        $aEvents[$iDragIndex][2] = $iCurrentMin - $iOrigStart
                    EndIf
            EndSwitch
        EndIf

        _WinAPI_InvalidateRect($hCanvas, 0, True)
        Return $GUI_RUNDEFMSG
    EndIf

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
    If $iDragMode > 0 Then _SaveCSV()
    $iDragMode = 0
    $iDragIndex = -1
    Return $GUI_RUNDEFMSG
EndFunc

Func WM_LBUTTONDBLCLK($hWnd, $iMsg, $wParam, $lParam)
    If $hWnd <> $hCanvas Then Return $GUI_RUNDEFMSG
    If BitShift($lParam, 16) < $iSubHeaderH And $iViewMode <> 7 Then Return $GUI_RUNDEFMSG

    Local $iMouseX = BitAND($lParam, 0xFFFF)
    Local $iMouseY = BitShift($lParam, 16)

    For $i = UBound($aEvents) - 1 To 0 Step -1
        Local $aRect = _GetEventScreenRect($i)
        If Not @error And $iMouseX >= $aRect[0] And $iMouseX <= $aRect[2] And $iMouseY >= $aRect[1] And $iMouseY <= $aRect[3] Then
            _OpenInPlaceEdit($i)
            Return $GUI_RUNDEFMSG
        EndIf
    Next

    Local $aColors[5] = [0x039BE5, 0x33B679, 0x8E24AA, 0xF4511E, 0xE67C73]
    Local $iColor = $aColors[Mod(UBound($aEvents), 5)]

    If $iViewMode == 6 Then
        Local $sClickedDate = _GetDateFromMonthXY($iMouseX, $iMouseY)
        If $sClickedDate <> "" Then
            _AddEvent("New Event", 540, 60, $iColor, $sClickedDate, 0)
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
        If $iStartMin > (1440 - 60) Then $iStartMin = 1440 - 60
        
        Local $sEvDate = _IsPeopleView() ? $sCurrentDate : _DateAdd('d', $iClickedCol, $sCurrentDate)
        Local $iPersonIdx = _IsPeopleView() ? $iClickedCol : 0

        _AddEvent("New Event", $iStartMin, 60, $iColor, $sEvDate, $iPersonIdx)
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
Func _GetEventScreenRect($iEventIdx)
    Local $aRect[4]
    If $iViewMode <= 5 Then
        Local $iColIdx = _GetEventColumnIndex($iEventIdx)
        If $iColIdx < 0 Then Return SetError(1, 0, $aRect)
        
        Local $iEffectiveW = ($iClientW > $iCanvasWidth ? $iClientW : $iCanvasWidth)
        Local $iDayColW = ($iEffectiveW - $iTimeColWidth) / _GetColCount()

        $aRect[0] = $iTimeColWidth + ($iColIdx * $iDayColW) - $iScrollX + 4
        $aRect[1] = Round($aEvents[$iEventIdx][1] * $fZoom) - $iScrollY + $iSubHeaderH
        $aRect[2] = $aRect[0] + $iDayColW - 8
        $aRect[3] = Round(($aEvents[$iEventIdx][1] + $aEvents[$iEventIdx][2]) * $fZoom) - $iScrollY + $iSubHeaderH
        Return $aRect
        
    ElseIf $iViewMode == 6 Then
        Local $sEvDate = $aEvents[$iEventIdx][4]
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
        Local $sCellDate = $aEvents[$iEventIdx][4]
        
        Local $aDayEventIdx[0]
        For $e = 0 To UBound($aEvents) - 1
            If $aEvents[$e][4] == $sCellDate Then _ArrayAdd($aDayEventIdx, $e)
        Next
        
        For $ei = 0 To UBound($aDayEventIdx) - 2
            For $ej = $ei + 1 To UBound($aDayEventIdx) - 1
                Local $idxI = $aDayEventIdx[$ei], $idxJ = $aDayEventIdx[$ej]
                If $aEvents[$idxJ][1] < $aEvents[$idxI][1] Then
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
        Local $aUpIdx[0]
        For $i = 0 To UBound($aEvents) - 1
            If $aEvents[$i][4] >= $sCurrentDate Then _ArrayAdd($aUpIdx, $i)
        Next
        For $i = 0 To UBound($aUpIdx) - 2
            For $j = $i + 1 To UBound($aUpIdx) - 1
                Local $idxI = $aUpIdx[$i], $idxJ = $aUpIdx[$j]
                If $aEvents[$idxJ][4] < $aEvents[$idxI][4] Or ($aEvents[$idxJ][4] == $aEvents[$idxI][4] And $aEvents[$idxJ][1] < $aEvents[$idxI][1]) Then
                    $aUpIdx[$i] = $idxJ
                    $aUpIdx[$j] = $idxI
                EndIf
            Next
        Next
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
                If $aEvents[$i][4] == $sCurrentDate Then _ArrayAdd($aResult, $i)
            Next
            
        Case 2, 4
            For $i = 0 To UBound($aEvents) - 1
                Local $sDate = $aEvents[$i][4]
                For $d = 0 To 3
                    If $sDate == _DateAdd('d', $d, $sCurrentDate) Then 
                        _ArrayAdd($aResult, $i)
                        ExitLoop
                    EndIf
                Next
            Next
            
        Case 3, 5
            For $i = 0 To UBound($aEvents) - 1
                Local $sDate = $aEvents[$i][4]
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
                Local $sDate = $aEvents[$i][4]
                Local $eYear = Int(StringLeft($sDate, 4))
                Local $eMonth = Int(StringMid($sDate, 6, 2))
                If $eYear == $iYear And $eMonth == $iMonth Then 
                    _ArrayAdd($aResult, $i)
                EndIf
            Next
            
        Case 7
            For $i = 0 To UBound($aEvents) - 1
                If $aEvents[$i][4] >= $sCurrentDate Then _ArrayAdd($aResult, $i)
            Next
    EndSwitch
    
    For $i = 0 To UBound($aResult) - 2
        For $j = $i + 1 To UBound($aResult) - 1
            Local $idxI = $aResult[$i], $idxJ = $aResult[$j]
            If $aEvents[$idxJ][4] < $aEvents[$idxI][4] Or _
               ($aEvents[$idxJ][4] == $aEvents[$idxI][4] And $aEvents[$idxJ][1] < $aEvents[$idxI][1]) Then
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
    Local $sEvDate    = $aEvents[$iEventIdx][4]
    Local $iPersonIdx = $aEvents[$iEventIdx][5]

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
    DllStructSetData($tPRINTDLG, "Flags", BitOR(0x00000100, 0x00000004, 0x00040000)) ; PD_RETURNDC | PD_NOPAGENUMS | PD_USEDEVMODECOPIESANDCOLLATE
    DllStructSetData($tPRINTDLG, "nFromPage", 1)
    DllStructSetData($tPRINTDLG, "nToPage", 1)
    DllStructSetData($tPRINTDLG, "nMinPage", 1)
    DllStructSetData($tPRINTDLG, "nMaxPage", 1)
    DllStructSetData($tPRINTDLG, "nCopies", 1)

    If Not _WinAPI_PrintDlg($tPRINTDLG) Then Return

    Local $hPrintDC = DllStructGetData($tPRINTDLG, "hDC")
    If Not $hPrintDC Then Return

    ; Printer metrics
    Local $iPageWidth  = _WinAPI_GetDeviceCaps($hPrintDC, 8)  ; HORZRES
    Local $iPageHeight = _WinAPI_GetDeviceCaps($hPrintDC, 10) ; VERTRES
    Local $iDPIX       = _WinAPI_GetDeviceCaps($hPrintDC, 88) ; LOGPIXELSX
    Local $iDPIY       = _WinAPI_GetDeviceCaps($hPrintDC, 90) ; LOGPIXELSY

    ; Minimal Margins (0.25 inch)
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

    Local $iHourH = Round($iDPIY * 0.65) ; 0.65 inch height per hour
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

        ; Title
        Local $sDateTitle = GUICtrlRead($idLblDateTitle)
        Local $hOldFont = _WinAPI_SelectObject($hPrintDC, $hFontTitle)
        _GDI_SetTextColor($hPrintDC, 0x202124)
        Local $tTitleRect = _WinAPI_CreateRect($iMarginX, $iMarginY, $iMarginX + $iUsableW, $iMarginY + $iTitleH)
        _WinAPI_DrawText($hPrintDC, $sDateTitle, $tTitleRect, BitOR($DT_LEFT, $DT_TOP, $DT_SINGLELINE))

        ; Header Background & Dividers
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

        ; Time Grid Lines & Labels
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

        ; Event Blocks (Vector Rectangles)
        _WinAPI_SelectObject($hPrintDC, $hFontEvent)
        For $i = 0 To UBound($aEvents) - 1
            Local $iColIdx = _GetEventColumnIndex($i)
            If $iColIdx < 0 Or $iColIdx >= $iColCount Then ContinueLoop

            Local $iEvStart = $aEvents[$i][1]
            Local $iEvEnd   = $iEvStart + $aEvents[$i][2]

            If $iEvEnd > $iStartMinPage And $iEvStart < $iEndMinPage Then
                Local $iBoxTop    = $iGridTop + Round(($iEvStart - $iStartMinPage) * $iMinH)
                Local $iBoxBottom = $iGridTop + Round(($iEvEnd - $iStartMinPage) * $iMinH)

                If $iBoxTop < $iGridTop Then $iBoxTop = $iGridTop
                If $iBoxBottom > ($iGridTop + $iGridH) Then $iBoxBottom = $iGridTop + $iGridH

                If ($iBoxBottom - $iBoxTop) >= 4 Then
                    Local $iBoxLeft  = $iMarginX + $iTimeColW + ($iColIdx * $iDayColW) + 4
                    Local $iBoxRight = $iBoxLeft + $iDayColW - 8
                    Local $iColor    = $aEvents[$i][3]

                    Local $hBrushEv = _GDI_CreateSolidBrush($iColor)
                    Local $hPenEv   = _GDI_CreatePen(0, 1, _DarkenColor($iColor, 20))

                    _WinAPI_SelectObject($hPrintDC, $hBrushEv)
                    _WinAPI_SelectObject($hPrintDC, $hPenEv)
                    _GDI_RoundRect($hPrintDC, $iBoxLeft, $iBoxTop, $iBoxRight, $iBoxBottom, 8, 8)

                    _GDI_SetTextColor($hPrintDC, _ContrastColor($iColor))
                    Local $iBoxH = $iBoxBottom - $iBoxTop
                    If $iBoxH >= Round($iDPIY * 0.15) Then
                        Local $tTextRect = _WinAPI_CreateRect($iBoxLeft + 6, $iBoxTop + 4, $iBoxRight - 6, $iBoxBottom - 4)
                        Local $sEvText = $aEvents[$i][0] & @CRLF & _MinToTimeString($aEvents[$i][1]) & " - " & _MinToTimeString($iEvEnd)
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

    ; Title
    Local $sDateTitle = GUICtrlRead($idLblDateTitle)
    Local $hOldFont = _WinAPI_SelectObject($hPrintDC, $hFontTitle)
    _GDI_SetTextColor($hPrintDC, 0x202124)
    Local $tTitleRect = _WinAPI_CreateRect($iMarginX, $iMarginY, $iMarginX + $iUsableW, $iMarginY + $iTitleH)
    _WinAPI_DrawText($hPrintDC, $sDateTitle, $tTitleRect, BitOR($DT_LEFT, $DT_TOP, $DT_SINGLELINE))

    ; Subheader Days
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

    ; Grid
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
                    If $aEvents[$e][4] == $sCellDate Then _ArrayAdd($aDayEventIdx, $e)
                Next

                For $ei = 0 To UBound($aDayEventIdx) - 2
                    For $ej = $ei + 1 To UBound($aDayEventIdx) - 1
                        Local $idxI = $aDayEventIdx[$ei], $idxJ = $aDayEventIdx[$ej]
                        If $aEvents[$idxJ][1] < $aEvents[$idxI][1] Then
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

                        Local $iColor = $aEvents[$eIdx][3]
                        Local $hBrushEv = _GDI_CreateSolidBrush($iColor)
                        Local $hPenEv   = _GDI_CreatePen(0, 1, _DarkenColor($iColor, 15))

                        _WinAPI_SelectObject($hPrintDC, $hPenEv)
                        _WinAPI_SelectObject($hPrintDC, $hBrushEv)
                        _GDI_RoundRect($hPrintDC, $iPillLeft, $iPillTop, $iPillRight, $iPillBottom, 6, 6)

                        _GDI_SetTextColor($hPrintDC, _ContrastColor($iColor))
                        Local $tEvRect = _WinAPI_CreateRect($iPillLeft + 4, $iPillTop + 2, $iPillRight - 4, $iPillBottom)
                        _WinAPI_DrawText($hPrintDC, $aEvents[$eIdx][0], $tEvRect, BitOR($DT_LEFT, $DT_TOP, $DT_SINGLELINE, $DT_END_ELLIPSIS))

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
    Local $aUpIdx[0]
    For $i = 0 To UBound($aEvents) - 1
        If $aEvents[$i][4] >= $sCurrentDate Then _ArrayAdd($aUpIdx, $i)
    Next

    For $i = 0 To UBound($aUpIdx) - 2
        For $j = $i + 1 To UBound($aUpIdx) - 1
            Local $idxI = $aUpIdx[$i], $idxJ = $aUpIdx[$j]
            If $aEvents[$idxJ][4] < $aEvents[$idxI][4] Or ($aEvents[$idxJ][4] == $aEvents[$idxI][4] And $aEvents[$idxJ][1] < $aEvents[$idxI][1]) Then
                $aUpIdx[$i] = $idxJ
                $aUpIdx[$j] = $idxI
            EndIf
        Next
    Next

    Local $iTitleH  = Round($iDPIY * 0.5)
    Local $iCardH   = Round($iDPIY * 0.9)
    Local $iCardGap = Round($iDPIY * 0.15)

    Local $hFontTitle = _WinAPI_CreateFont(-Round($iDPIY * 18 / 72), 0, 0, 0, 700, False, False, False, 0, 0, 0, 0, 0, "Segoe UI")
    Local $hFontItem  = _WinAPI_CreateFont(-Round($iDPIY * 14 / 72), 0, 0, 0, 700, False, False, False, 0, 0, 0, 0, 0, "Segoe UI")
    Local $hFontText  = _WinAPI_CreateFont(-Round($iDPIY * 11 / 72), 0, 0, 0, 400, False, False, False, 0, 0, 0, 0, 0, "Segoe UI")

    Local $bInPage = False
    Local $iCurrentY = _StartPageHeader($hPrintDC, $hFontTitle, $iMarginX, $iMarginY, $iUsableW, $iTitleH)
    $bInPage = True

    If UBound($aUpIdx) == 0 Then
        Local $hOld = _WinAPI_SelectObject($hPrintDC, $hFontText)
        _GDI_SetTextColor($hPrintDC, 0x5F6368)
        Local $tRect = _WinAPI_CreateRect($iMarginX, $iCurrentY, $iMarginX + $iUsableW, $iCurrentY + $iCardH)
        _WinAPI_DrawText($hPrintDC, "No upcoming events found.", $tRect, BitOR($DT_LEFT, $DT_TOP, $DT_SINGLELINE))
        _WinAPI_SelectObject($hPrintDC, $hOld)
    Else
        For $i = 0 To UBound($aUpIdx) - 1
            ; Multi-page pagination check
            If ($iCurrentY + $iCardH) > ($iMarginY + $iUsableH) Then
                DllCall("gdi32.dll", "int", "EndPage", "handle", $hPrintDC)
                $iCurrentY = _StartPageHeader($hPrintDC, $hFontTitle, $iMarginX, $iMarginY, $iUsableW, $iTitleH)
            EndIf

            Local $e = $aUpIdx[$i]
            Local $sDate = _FormatDateTitle($aEvents[$e][4])
            Local $sTime = _MinToTimeString($aEvents[$e][1]) & " - " & _MinToTimeString($aEvents[$e][1] + $aEvents[$e][2])
            Local $sPerson = ""
            If $aEvents[$e][5] < UBound($aPeople) Then $sPerson = $aPeople[$aEvents[$e][5]]

            Local $iLeft = $iMarginX
            Local $iRight = $iMarginX + $iUsableW
            Local $iTop = $iCurrentY
            Local $iBottom = $iCurrentY + $iCardH

            Local $iColorBarW = Round($iDPIX * 0.12)
            Local $hBrushEv = _GDI_CreateSolidBrush($aEvents[$e][3])
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
            _WinAPI_DrawText($hPrintDC, $aEvents[$e][0], $tTitleRect, BitOR($DT_LEFT, $DT_TOP, $DT_SINGLELINE, $DT_END_ELLIPSIS))

            _WinAPI_SelectObject($hPrintDC, $hFontText)
            _GDI_SetTextColor($hPrintDC, 0x5F6368)
            Local $tDescRect = _WinAPI_CreateRect($iTextLeft, $iTop + Round($iDPIY * 0.5), $iRight - $iPadX, $iBottom - Round($iDPIY * 0.1))
            _WinAPI_DrawText($hPrintDC, $sDate & "   •   " & $sTime & "   •   " & $sPerson, $tDescRect, BitOR($DT_LEFT, $DT_TOP, $DT_SINGLELINE, $DT_END_ELLIPSIS))
            _WinAPI_SelectObject($hPrintDC, $hOldFont)

            $iCurrentY += $iCardH + $iCardGap
        Next
    EndIf

    If $bInPage Then DllCall("gdi32.dll", "int", "EndPage", "handle", $hPrintDC)

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

Func _GDI_RoundRect($hDC, $iLeft, $iTop, $iRight, $iBottom, $iWidth, $iHeight)
    Return DllCall("gdi32.dll", "bool", "RoundRect", "handle", $hDC, "int", $iLeft, "int", $iTop, "int", $iRight, "int", $iBottom, "int", $iWidth, "int", $iHeight)[0]
EndFunc

Func _GDI_Ellipse($hDC, $iLeft, $iTop, $iRight, $iBottom)
    Return DllCall("gdi32.dll", "bool", "Ellipse", "handle", $hDC, "int", $iLeft, "int", $iTop, "int", $iRight, "int", $iBottom)[0]
EndFunc

Func _GDI_CreateSolidBrush($iRGB)
    Return _WinAPI_CreateSolidBrush(_RGB2BGR($iRGB))
EndFunc

Func _GDI_CreatePen($iStyle, $iWidth, $iRGB)
    Return _WinAPI_CreatePen($iStyle, $iWidth, _RGB2BGR($iRGB))
EndFunc

Func _GDI_SetTextColor($hDC, $iRGB)
    Return _WinAPI_SetTextColor($hDC, _RGB2BGR($iRGB))
EndFunc

Func _RGB2BGR($iColor)
    Return BitOR(BitShift(BitAND($iColor, 0x0000FF), -16), BitAND($iColor, 0x00FF00), BitShift(BitAND($iColor, 0xFF0000), 16))
EndFunc

Func _DarkenColor($iRGB, $iPercent)
    Local $iR = BitShift(BitAND($iRGB, 0xFF0000), 16) * (1 - ($iPercent / 100))
    Local $iG = BitShift(BitAND($iRGB, 0x00FF00), 8)  * (1 - ($iPercent / 100))
    Local $iB = BitAND($iRGB, 0x0000FF)               * (1 - ($iPercent / 100))
    Return BitOR(BitShift(Int($iR), -16), BitShift(Int($iG), -8), Int($iB))
EndFunc

Func _ContrastColor($iBG)
    Local $iR = BitShift(BitAND($iBG, 0xFF0000), 16)
    Local $iG = BitShift(BitAND($iBG, 0x00FF00), 8)
    Local $iB = BitAND($iBG, 0x0000FF)
    Local $iLuminance = ($iR * 299 + $iG * 587 + $iB * 114) / 1000
    
    Return ($iLuminance > 125) ? 0x000000 : 0xFFFFFF
EndFunc

; ==============================================================================
; DATA & FORMATTING UTILITIES
; ==============================================================================
Func _AddEvent($sTitle, $iStartMin, $iDuration, $iColor, $sDate, $iPersonIdx = 0)
    Local $iIndex = UBound($aEvents)
    ReDim $aEvents[$iIndex + 1][6]
    $aEvents[$iIndex][0] = $sTitle
    $aEvents[$iIndex][1] = $iStartMin
    $aEvents[$iIndex][2] = $iDuration
    $aEvents[$iIndex][3] = $iColor
    $aEvents[$iIndex][4] = $sDate
    $aEvents[$iIndex][5] = $iPersonIdx
EndFunc

Func _MinToTimeString($iMinutes)
    Local $iH = Floor($iMinutes / 60)
    Local $iM = Mod($iMinutes, 60)
    Local $sAmPm = ($iH >= 12 ? "pm" : "am")
    If $iH > 12 Then $iH -= 12
    If $iH == 0 Then $iH = 12
    Return StringFormat("%d:%02d%s", $iH, $iM, $sAmPm)
EndFunc

Func _FormatDateTitle($sDate)
    Local $aMonths = ["January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"]
    Local $iMonth = Int(StringMid($sDate, 6, 2))
    Local $iDay = Int(StringRight($sDate, 2))
    Local $iYear = StringLeft($sDate, 4)
    Return $aMonths[$iMonth - 1] & " " & $iDay & ", " & $iYear
EndFunc

Func _FormatDayHeader($sDate)
    Local $aDays = ["Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"]
    Local $aMonths = ["Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"]
    Local $iDayOfWeek = _DateToDayOfWeek(StringLeft($sDate, 4), StringMid($sDate, 6, 2), StringRight($sDate, 2))
    Local $iMonth = Int(StringMid($sDate, 6, 2))
    Local $iDay = Int(StringRight($sDate, 2))
    Return $aDays[$iDayOfWeek - 1] & ", " & $aMonths[$iMonth - 1] & " " & $iDay
EndFunc