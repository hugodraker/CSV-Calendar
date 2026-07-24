' =============================================================================
' C File Splitter - Command Line Version
' =============================================================================
' Usage: cscript //nologo splitter.vbs source.c 3
'
' Splits a C source file into N chunks along procedure boundaries (function
' definitions), ensuring no function is split across chunks.
' =============================================================================

Option Explicit

Dim args, inputFile, numChunks, fso

Set args = WScript.Arguments
Set fso = CreateObject("Scripting.FileSystemObject")

' ---- Parse command line arguments ----
If args.Count < 2 Then
    WScript.Echo "Usage: cscript //nologo splitter.vbs source.c 3"
    WScript.Echo "  source.c  - Input C source file"
    WScript.Echo "  3         - Number of chunks to split into (minimum 2)"
    WScript.Quit 1
End If

inputFile = args(0)
numChunks = CInt(args(1))

' ---- Validate inputs ----
If numChunks < 2 Then
    WScript.Echo "Error: Minimum number of chunks is 2"
    WScript.Quit 1
End If

If Not fso.FileExists(inputFile) Then
    WScript.Echo "Error: File not found: " & inputFile
    WScript.Quit 1
End If

' ---- Read the file ----
Dim ts, content
Set ts = fso.OpenTextFile(inputFile, 1)
content = ts.ReadAll
ts.Close

Dim strDir, strBaseName
strDir = fso.GetParentFolderName(inputFile)
strBaseName = fso.GetBaseName(inputFile)

Dim lines
lines = Split(content, vbCrLf)
Dim totalLines : totalLines = UBound(lines) + 1

WScript.Echo "Read " & totalLines & " lines from " & inputFile

' =============================================================================
' PASS 1: Strip comments/strings, build codeLines array, compute brace depth
' =============================================================================
Dim codeLines()
ReDim codeLines(totalLines - 1)
Dim depthBefore(), depthAfter()
ReDim depthBefore(totalLines - 1)
ReDim depthAfter(totalLines - 1)

Dim inBlockComment : inBlockComment = False
Dim i, stripped, braceDelta, depth
depth = 0

For i = 0 To totalLines - 1
    stripped = StripLine(lines(i), inBlockComment)
    codeLines(i) = stripped
    depthBefore(i) = depth
    braceDelta = CountChar(stripped, "{") - CountChar(stripped, "}")
    depth = depth + braceDelta
    depthAfter(i) = depth
Next

WScript.Echo "Brace depth analysis complete"

' =============================================================================
' PASS 2: Find procedure boundaries (function definitions at depth 0)
' =============================================================================
Dim boundaryLines()
ReDim boundaryLines(totalLines)
Dim bCount : bCount = 0

' First boundary is always line 0 (includes prelude: includes, typedefs, globals)
boundaryLines(0) = 0
bCount = 1

For i = 1 To totalLines - 1
    If depthBefore(i) = 0 Then
        If IsFunctionStart(codeLines, i) Then
            boundaryLines(bCount) = i
            bCount = bCount + 1
        End If
    End If
Next

Dim numProcedures : numProcedures = bCount - 1
WScript.Echo "Found " & numProcedures & " procedure(s), " & bCount & " segment(s)"

' =============================================================================
' PASS 3: Choose split points - distribute segments evenly across chunks
' =============================================================================
Dim segsPerChunk, segRemainder
segsPerChunk = bCount \ numChunks
segRemainder = bCount Mod numChunks

Dim actualChunks
If segsPerChunk = 0 And segRemainder > 0 Then
    actualChunks = bCount
Else
    actualChunks = numChunks
End If

If actualChunks > bCount Then
    WScript.Echo "Note: Requested chunks (" & numChunks & ") exceeds segments (" & bCount & ")"
    WScript.Echo "      Creating " & bCount & " files instead"
    actualChunks = bCount
End If

' =============================================================================
' PASS 4: Write each chunk to <num>.c
' =============================================================================
Dim segIdx : segIdx = 0
Dim cIdx

WScript.Echo ""
WScript.Echo "Writing chunks:"

For cIdx = 1 To actualChunks
    Dim numSegs : numSegs = segsPerChunk
    If cIdx <= segRemainder Then numSegs = numSegs + 1

    Dim beginLine, endLine, lineCount
    beginLine = boundaryLines(segIdx)

    segIdx = segIdx + numSegs

    If segIdx >= bCount Then
        endLine = totalLines - 1
    Else
        endLine = boundaryLines(segIdx) - 1
    End If

    ' Handle edge case where beginning > end (empty chunk - shouldn't happen but guard anyway)
    If endLine < beginLine Then endLine = beginLine
    
    lineCount = endLine - beginLine + 1

    Dim outPath
    outPath = strDir & "\" & cIdx & ".c"

    Dim buf, j
    buf = ""
    For j = beginLine To endLine
        If j >= 0 And j <= UBound(lines) Then
            buf = buf & lines(j) & vbCrLf
        End If
    Next

    Dim out
    Set out = fso.CreateTextFile(outPath, True)
    out.Write buf
    out.Close

    WScript.Echo "  " & cIdx & ".c - " & lineCount & " lines (segment " & (segIdx - numSegs + 1) & " to " & segIdx & ")"
Next

WScript.Echo ""
WScript.Echo "Done! Split into " & actualChunks & " file(s)."
WScript.Echo "Saved to: " & strDir

WScript.Quit 0

' =============================================================================
' FUNCTION: Count occurrences of a character in a string
' =============================================================================
Function CountChar(str, ch)
    Dim c, k
    c = 0
    k = 1
    Do
        k = InStr(k, str, ch)
        If k = 0 Then Exit Do
        c = c + 1
        k = k + 1
    Loop
    CountChar = c
End Function

' =============================================================================
' FUNCTION: Strip comments and string/char literals from a line
' =============================================================================
' Removes:
'   - // line comments
'   - /* */ block comments (tracks multi-line state)
'   - "..." string literals (replaced with space)
'   - '...' char literals (replaced with space)
' =============================================================================
Function StripLine(ByVal s, ByRef inBlockComment)
    Dim result, pos, inStr, inChr, inLnCmt
    result = ""
    inStr = False
    inChr = False
    inLnCmt = False
    pos = 1

    Do While pos <= Len(s)
        Dim ch, nextCh
        ch = Mid(s, pos, 1)
        If pos < Len(s) Then
            nextCh = Mid(s, pos + 1, 1)
        Else
            nextCh = ""
        End If

        If inBlockComment Then
            ' Inside /* ... */
            If ch = "*" And nextCh = "/" Then
                inBlockComment = False
                pos = pos + 2
            Else
                pos = pos + 1
            End If

        ElseIf inLnCmt Then
            ' Rest of line is a // comment, stop here
            Exit Do

        ElseIf inStr Then
            ' Inside "..."
            If ch = "\" Then
                pos = pos + 2  ' skip escaped char
            ElseIf ch = """" Then
                inStr = False
                pos = pos + 1
            Else
                pos = pos + 1
            End If

        ElseIf inChr Then
            ' Inside '...'
            If ch = "\" Then
                pos = pos + 2
            ElseIf ch = "'" Then
                inChr = False
                pos = pos + 1
            Else
                pos = pos + 1
            End If

        Else
            ' Normal code
            If ch = "/" And nextCh = "/" Then
                inLnCmt = True
                pos = pos + 2
            ElseIf ch = "/" And nextCh = "*" Then
                inBlockComment = True
                pos = pos + 2
            ElseIf ch = """" Then
                inStr = True
                result = result & " "  ' placeholder
                pos = pos + 1
            ElseIf ch = "'" Then
                inChr = True
                result = result & " "
                pos = pos + 1
            Else
                result = result & ch
                pos = pos + 1
            End If
        End If
    Loop

    StripLine = result
End Function

' =============================================================================
' FUNCTION: Check if a stripped code line looks like a function definition
' =============================================================================
' Criteria:
'   - Not blank, not a preprocessor directive
'   - Contains ( and )
'   - Does NOT end with ;
'   - Does NOT start with control-flow keywords (if, for, while, switch, else, do)
'   - Has { on same line OR next non-blank code line starts with {
' =============================================================================
Function IsFunctionStart(codeLines, idx)
    Dim s : s = Trim(codeLines(idx))
    Dim ub : ub = UBound(codeLines)

    ' Reject blank lines
    If s = "" Then IsFunctionStart = False : Exit Function

    ' Reject preprocessor (#include, #define, #if, etc.)
    If Left(s, 1) = "#" Then IsFunctionStart = False : Exit Function

    ' Must contain parentheses
    If InStr(s, "(") = 0 Or InStr(s, ")") = 0 Then
        IsFunctionStart = False
        Exit Function
    End If

    ' Must NOT end with semicolon (declaration/prototype/call)
    If Right(s, 1) = ";" Then
        IsFunctionStart = False
        Exit Function
    End If

    ' Reject control-flow keywords
    Dim lower : lower = LCase(s)
    If StartsWithWord(lower, "if ") Or _
       StartsWithWord(lower, "for ") Or _
       StartsWithWord(lower, "while ") Or _
       StartsWithWord(lower, "switch ") Or _
       StartsWithWord(lower, "else ") Or _
       StartsWithWord(lower, "do ") Or _
       StartsWithWord(lower, "else{") Or _
       StartsWithWord(lower, "do{") Then
        IsFunctionStart = False
        Exit Function
    End If

    ' Case A: opening brace on the SAME line
    If InStr(s, "{") > 0 Then
        IsFunctionStart = True
        Exit Function
    End If

    ' Case B: opening brace on the NEXT non-blank code line
    Dim k : k = idx + 1
    Do While k <= ub
        Dim nxt : nxt = Trim(codeLines(k))
        If nxt <> "" Then
            If Left(nxt, 1) = "{" Then
                IsFunctionStart = True
            Else
                IsFunctionStart = False
            End If
            Exit Function
        End If
        k = k + 1
    Loop

    IsFunctionStart = False
End Function

' =============================================================================
' FUNCTION: Check if str starts with word (followed by space or brace)
' =============================================================================
Function StartsWithWord(str, word)
    Dim wLen : wLen = Len(word)
    If Len(str) < wLen Then
        StartsWithWord = False
    Else
        StartsWithWord = (Left(str, wLen) = word)
    End If
End Function