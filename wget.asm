; ============================================================================
; Basic Win32 Wget in MASM32 Assembly with Auto-Resume & Auto-Retry
; ============================================================================
;
; COMPILE INSTRUCTIONS (MASM32):
;   ml /c /coff wget.asm
;   link /subsystem:console wget.obj
;
; USAGE:
;   wget.exe <URL> <Output_Filename>
;   Example: wget.exe https://example.com/largefile.zip output.zip
;
; PUBLIC DOMAIN DEDICATION:
;   This is free and unencumbered software released into the public domain.
;   Anyone is free to copy, modify, publish, use, compile, sell, or
;   distribute this software, either in source code form or as a compiled
;   binary, for any purpose, commercial or non-commercial, and by any
;   means.
; ============================================================================

.386
.model flat, stdcall
option casemap:none

; --- Includes & Libraries ---
include \masm32\include\windows.inc
include \masm32\include\kernel32.inc
include \masm32\include\wininet.inc
include \masm32\include\msvcrt.inc

includelib \masm32\lib\kernel32.lib
includelib \masm32\lib\wininet.lib
includelib \masm32\lib\msvcrt.lib

; --- Constants ---
GetFileExInfoStandard equ 0
CHUNK_SIZE            equ 8192
MAX_RETRIES           equ 5
RETRY_DELAY_MS        equ 3000

; --- Prototypes ---
GetLocalFileSize PROTO :DWORD
QwordModDword    PROTO :DWORD, :DWORD, :DWORD

.data
    szUserAgent     db "BasicWget/2.0", 0
    szUsage         db "Usage: wget <URL> <Output_Filename>", 10
                    db "Example: wget https://example.com/file.zip output.zip", 10, 0
    szConnecting    db "Connecting to %s...", 10, 0
    szReconnecting  db "[Attempt %d/%d] Reconnecting to %s...", 10, 0
    szErrInternet   db "Error: InternetOpen failed. Error Code: %lu", 10, 0
    szErrConnFailed db "Connection failed (Error %lu). Retrying in %d seconds...", 10, 0
    szRangeFmt      db "Range: bytes=%I64u-", 13, 10, 0
    szAlreadyDone   db "File is already fully downloaded (%I64u bytes).", 10, 0
    szErrClient     db "Server returned client error HTTP %lu. Aborting.", 10, 0
    szErrServer     db "Server returned error HTTP %lu. Retrying in %d seconds...", 10, 0
    szResuming      db "Resuming download from byte %I64u...", 10, 0
    szStartingFresh db "Starting fresh download...", 10, 0
    szErrFile       db "Error: Could not open output file '%s' for writing.", 10, 0
    szDot           db ".", 0
    szNetDrop       db 10, "Network connection dropped! Progress saved (%I64u bytes).", 10, 0
    szRetryInfo     db "Retrying (%d/%d) in %d seconds...", 10, 0
    szComplete      db 10, "Download complete! Final size: %I64u bytes.", 10, 0
    szFailedAll     db 10, "Download failed after %d attempts. You can run the program again to resume.", 10, 0
    szModeAppend    db "ab", 0
    szModeWrite     db "wb", 0

.data?
    hInternet       dd ?
    hUrl            dd ?
    hFile           dd ?
    pUrl            dd ?
    pOutFile        dd ?
    argc            dd ?
    argv            dd ?
    env             dd ?
    startupInfo     dd ?    ; Fixed: Renamed from 'si' (reserved register)
    attempt         dd ?
    isComplete      dd ?
    statusCode      dd ?
    dwLen           dd ?
    bytesRead       dd ?
    readSuccess     dd ?
    
    existingSize      qword ?
    sessionDownloaded qword ?
    totalBytes        qword ?
    
    rangeHeader     db 128 dup(?)
    buffer          db CHUNK_SIZE dup(?)

.code

; ----------------------------------------------------------------------------
; Helper: GetLocalFileSize(pszFilename)
; Returns 64-bit file size in EDX:EAX (0 if file doesn't exist)
; ----------------------------------------------------------------------------
GetLocalFileSize proc pszFilename:DWORD
    LOCAL fad:WIN32_FILE_ATTRIBUTE_DATA

    invoke GetFileAttributesExA, pszFilename, GetFileExInfoStandard, addr fad
    test eax, eax
    jz @failed

    mov eax, fad.nFileSizeLow
    mov edx, fad.nFileSizeHigh
    ret

@failed:
    xor eax, eax
    xor edx, edx
    ret
GetLocalFileSize endp

; ----------------------------------------------------------------------------
; Helper: QwordModDword(dwLow, dwHigh, dwDivisor)
; Computes (EDX:EAX) % Divisor for 64-bit modulo on 32-bit x86
; Returns remainder in EAX
; ----------------------------------------------------------------------------
QwordModDword proc dwLow:DWORD, dwHigh:DWORD, dwDivisor:DWORD
    push ebx
    mov ebx, dwDivisor
    mov eax, dwHigh
    xor edx, edx
    div ebx               ; EDX = high % divisor
    mov eax, dwLow
    div ebx               ; EDX = (high % divisor * 2^32 + low) % divisor
    mov eax, edx          ; Remainder in EAX
    pop ebx
    ret
QwordModDword endp

; ----------------------------------------------------------------------------
; Main Entry Point
; ----------------------------------------------------------------------------
main proc
    ; Parse Command Line Arguments using MSVCRT
    invoke crt___getmainargs, addr argc, addr argv, addr env, 0, addr startupInfo
    
    cmp argc, 3
    jl show_usage

    ; Extract argv[1] (URL) and argv[2] (outFile)
    mov esi, argv
    mov eax, [esi + 4]
    mov pUrl, eax
    mov eax, [esi + 8]
    mov pOutFile, eax

    ; Initialize WinINet API
    invoke InternetOpenA, addr szUserAgent, INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0
    mov hInternet, eax
    test eax, eax
    jnz @internet_ok

    invoke GetLastError
    invoke crt_printf, addr szErrInternet, eax
    invoke ExitProcess, 1

@internet_ok:
    mov attempt, 0
    mov isComplete, 0

retry_loop:
    mov eax, attempt
    cmp eax, MAX_RETRIES
    jge retry_loop_end
    cmp isComplete, 0
    jne retry_loop_end

    ; Get size of local target file if it already exists
    invoke GetLocalFileSize, pOutFile
    mov dword ptr [existingSize], eax
    mov dword ptr [existingSize + 4], edx

    ; Build Range Header if existingSize > 0
    mov rangeHeader[0], 0
    mov eax, dword ptr [existingSize]
    or  eax, dword ptr [existingSize + 4]
    jz @no_range

    ; Fixed: Using crt_sprintf instead of undefined crt_snprintf
    invoke crt_sprintf, addr rangeHeader, addr szRangeFmt, dword ptr [existingSize], dword ptr [existingSize + 4]

@no_range:
    ; Print status
    cmp attempt, 0
    jg @print_reconnect
    invoke crt_printf, addr szConnecting, pUrl
    jmp @conn_msg_done

@print_reconnect:
    mov eax, attempt
    inc eax
    invoke crt_printf, addr szReconnecting, eax, MAX_RETRIES, pUrl

@conn_msg_done:
    ; Prepare OpenUrl parameters
    cmp rangeHeader[0], 0
    je @no_hdr
    mov eax, offset rangeHeader
    mov ecx, -1
    jmp @do_open

@no_hdr:
    xor eax, eax
    xor ecx, ecx

@do_open:
    ; Flags: INTERNET_FLAG_RELOAD | INTERNET_FLAG_SECURE | INTERNET_FLAG_NO_CACHE_WRITE
    mov edx, INTERNET_FLAG_RELOAD or INTERNET_FLAG_SECURE or INTERNET_FLAG_NO_CACHE_WRITE

    invoke InternetOpenUrlA, hInternet, pUrl, eax, ecx, edx, 0
    mov hUrl, eax
    test eax, eax
    jnz @url_opened

    ; Failed connection
    inc attempt
    invoke GetLastError
    invoke crt_printf, addr szErrConnFailed, eax, RETRY_DELAY_MS / 1000
    invoke Sleep, RETRY_DELAY_MS
    jmp retry_loop

@url_opened:
    ; Query HTTP Status Code
    mov dwLen, 4
    mov statusCode, 0
    invoke HttpQueryInfoA, hUrl, HTTP_QUERY_STATUS_CODE or HTTP_QUERY_FLAG_NUMBER, addr statusCode, addr dwLen, NULL
    test eax, eax
    jz @check_file_mode

    ; Check HTTP 416 (Range Not Satisfiable -> File already fully downloaded)
    cmp statusCode, 416
    jne @chk_4xx
    invoke crt_printf, addr szAlreadyDone, dword ptr [existingSize], dword ptr [existingSize + 4]
    invoke InternetCloseHandle, hUrl
    invoke InternetCloseHandle, hInternet
    invoke ExitProcess, 0

@chk_4xx:
    ; Check HTTP 4xx Client Errors
    cmp statusCode, 400
    jb @chk_5xx
    cmp statusCode, 500
    jae @chk_5xx
    invoke crt_printf, addr szErrClient, statusCode
    invoke InternetCloseHandle, hUrl
    invoke InternetCloseHandle, hInternet
    invoke ExitProcess, 1

@chk_5xx:
    ; Check HTTP 5xx Server Errors
    cmp statusCode, 500
    jb @check_file_mode
    inc attempt
    invoke crt_printf, addr szErrServer, statusCode, RETRY_DELAY_MS / 1000
    invoke InternetCloseHandle, hUrl
    invoke Sleep, RETRY_DELAY_MS
    jmp retry_loop

@check_file_mode:
    ; Open local file (HTTP 206 = Append mode, HTTP 200 = Overwrite mode)
    cmp statusCode, 206
    jne @open_fresh
    mov eax, dword ptr [existingSize]
    or  eax, dword ptr [existingSize + 4]
    jz @open_fresh

    invoke crt_fopen, pOutFile, addr szModeAppend
    mov hFile, eax
    invoke crt_printf, addr szResuming, dword ptr [existingSize], dword ptr [existingSize + 4]
    jmp @file_handle_check

@open_fresh:
    invoke crt_fopen, pOutFile, addr szModeWrite
    mov hFile, eax
    mov dword ptr [existingSize], 0
    mov dword ptr [existingSize + 4], 0
    invoke crt_printf, addr szStartingFresh

@file_handle_check:
    cmp hFile, 0
    jnz @file_ok
    invoke crt_printf, addr szErrFile, pOutFile
    invoke InternetCloseHandle, hUrl
    invoke InternetCloseHandle, hInternet
    invoke ExitProcess, 1

@file_ok:
    mov dword ptr [sessionDownloaded], 0
    mov dword ptr [sessionDownloaded + 4], 0

download_loop:
    invoke InternetReadFile, hUrl, addr buffer, CHUNK_SIZE, addr bytesRead
    mov readSuccess, eax
    test eax, eax
    jz download_loop_end
    cmp bytesRead, 0
    je download_loop_end

    ; Write chunk to file
    invoke crt_fwrite, addr buffer, 1, bytesRead, hFile

    ; sessionDownloaded += bytesRead
    mov eax, bytesRead
    cdq
    add dword ptr [sessionDownloaded], eax
    adc dword ptr [sessionDownloaded + 4], edx

    ; Check if we should print a progress dot: (existingSize + sessionDownloaded) % 819200 < 8192
    mov eax, dword ptr [existingSize]
    mov edx, dword ptr [existingSize + 4]
    add eax, dword ptr [sessionDownloaded]
    adc edx, dword ptr [sessionDownloaded + 4]

    invoke QwordModDword, eax, edx, 819200
    cmp eax, CHUNK_SIZE
    jae download_loop

    invoke crt_printf, addr szDot
    invoke crt_fflush, 0
    jmp download_loop

download_loop_end:
    invoke crt_fclose, hFile
    invoke InternetCloseHandle, hUrl

    cmp readSuccess, 0
    jnz @download_clean

    ; Network drop mid-file
    inc attempt
    invoke GetLocalFileSize, pOutFile
    invoke crt_printf, addr szNetDrop, eax, edx
    invoke crt_printf, addr szRetryInfo, attempt, MAX_RETRIES, RETRY_DELAY_MS / 1000
    invoke Sleep, RETRY_DELAY_MS
    jmp retry_loop

@download_clean:
    mov isComplete, 1
    invoke GetLocalFileSize, pOutFile
    invoke crt_printf, addr szComplete, eax, edx
    jmp retry_loop

retry_loop_end:
    invoke InternetCloseHandle, hInternet

    cmp isComplete, 0
    jnz @exit_success

    invoke crt_printf, addr szFailedAll, MAX_RETRIES
    invoke ExitProcess, 1

@exit_success:
    invoke ExitProcess, 0

show_usage:
    invoke crt_printf, addr szUsage
    invoke ExitProcess, 1
main endp

end main