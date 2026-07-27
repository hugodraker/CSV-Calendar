/*
 * ============================================================================
 * Basic Win32 Wget in C with Auto-Resume & Auto-Retry (WinINet API)
 * ============================================================================
 * 
 * COMPILE INSTRUCTIONS (GCC / MinGW):
 *   gcc wget.c -Os -s -o wget.exe -lwininet
 *
 * USAGE:
 *   wget.exe <URL> <Output_Filename>
 *   Example: wget.exe https://example.com/largefile.zip output.zip
 *
 * PUBLIC DOMAIN DEDICATION:
 *   This is free and unencumbered software released into the public domain.
 *   Anyone is free to copy, modify, publish, use, compile, sell, or
 *   distribute this software, either in source code form or as a compiled
 *   binary, for any purpose, commercial or non-commercial, and by any
 *   means.
 * ============================================================================
 */

#include <windows.h>
#include <wininet.h>
#include <stdio.h>
#include <stdlib.h>

#define CHUNK_SIZE 8192    // 8KB download buffer
#define MAX_RETRIES 5      // Maximum reconnection attempts
#define RETRY_DELAY_MS 3000 // Wait 3 seconds between retries

// Helper to get local file size in bytes (0 if file doesn't exist)
unsigned long long get_local_file_size(const char *filename) {
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (!GetFileAttributesExA(filename, GetFileExInfoStandard, &fad)) {
        return 0; // File does not exist
    }
    LARGE_INTEGER size;
    size.HighPart = fad.nFileSizeHigh;
    size.LowPart = fad.nFileSizeLow;
    return (unsigned long long)size.QuadPart;
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        printf("Usage: %s <URL> <Output_Filename>\n", argv[0]);
        printf("Example: %s https://example.com/file.zip output.zip\n", argv[0]);
        return 1;
    }

    const char *url = argv[1];
    const char *outFile = argv[2];

    // Initialize WinINet
    HINTERNET hInternet = InternetOpenA(
        "BasicWget/2.0",
        INTERNET_OPEN_TYPE_DIRECT,
        NULL, NULL, 0
    );

    if (!hInternet) {
        fprintf(stderr, "Error: InternetOpen failed. Error Code: %lu\n", GetLastError());
        return 1;
    }

    int attempt = 0;
    int isComplete = 0;

    while (attempt < MAX_RETRIES && !isComplete) {
        unsigned long long existingSize = get_local_file_size(outFile);

        // Build Range HTTP header if we are attempting to resume
        char rangeHeader[128] = "";
        if (existingSize > 0) {
            snprintf(rangeHeader, sizeof(rangeHeader), "Range: bytes=%I64u-\r\n", existingSize);
        }

        if (attempt > 0) {
            printf("[Attempt %d/%d] Reconnecting to %s...\n", attempt + 1, MAX_RETRIES, url);
        } else {
            printf("Connecting to %s...\n", url);
        }

        // Open URL with Range header (if resuming)
        HINTERNET hUrl = InternetOpenUrlA(
            hInternet,
            url,
            rangeHeader[0] ? rangeHeader : NULL,
            rangeHeader[0] ? (DWORD)-1L : 0,
            INTERNET_FLAG_RELOAD | INTERNET_FLAG_SECURE | INTERNET_FLAG_NO_CACHE_WRITE,
            0
        );

        if (!hUrl) {
            attempt++;
            fprintf(stderr, "Connection failed (Error %lu). Retrying in %d seconds...\n", 
                    GetLastError(), RETRY_DELAY_MS / 1000);
            Sleep(RETRY_DELAY_MS);
            continue;
        }

        // Query HTTP Status Code
        DWORD statusCode = 0;
        DWORD dwLen = sizeof(statusCode);
        if (HttpQueryInfoA(hUrl, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &statusCode, &dwLen, NULL)) {
            
            // 416 = Range Not Satisfiable (File is already completely downloaded)
            if (statusCode == 416) {
                printf("File is already fully downloaded (%I64u bytes).\n", existingSize);
                InternetCloseHandle(hUrl);
                InternetCloseHandle(hInternet);
                return 0;
            }

            // HTTP 4xx Client Errors (e.g. 404 Not Found) shouldn't be retried
            if (statusCode >= 400 && statusCode < 500) {
                fprintf(stderr, "Server returned client error HTTP %lu. Aborting.\n", statusCode);
                InternetCloseHandle(hUrl);
                InternetCloseHandle(hInternet);
                return 1;
            }

            // HTTP 5xx Server Errors -> Retry
            if (statusCode >= 500) {
                attempt++;
                fprintf(stderr, "Server returned error HTTP %lu. Retrying in %d seconds...\n", 
                        statusCode, RETRY_DELAY_MS / 1000);
                InternetCloseHandle(hUrl);
                Sleep(RETRY_DELAY_MS);
                continue;
            }
        }

        // Determine local file mode based on server response:
        // HTTP 206 = Partial Content (Server accepted our range request, resume appending)
        // HTTP 200 = OK (Server ignored range request or fresh download, start from scratch)
        FILE *file = NULL;
        if (statusCode == 206 && existingSize > 0) {
            file = fopen(outFile, "ab"); // Append mode
            printf("Resuming download from byte %I64u...\n", existingSize);
        } else {
            file = fopen(outFile, "wb"); // Overwrite mode
            existingSize = 0;
            printf("Starting fresh download...\n");
        }

        if (!file) {
            fprintf(stderr, "Error: Could not open output file '%s' for writing.\n", outFile);
            InternetCloseHandle(hUrl);
            InternetCloseHandle(hInternet);
            return 1;
        }

        // Download loop
        char buffer[CHUNK_SIZE];
        DWORD bytesRead = 0;
        unsigned long long sessionDownloaded = 0;
        BOOL readSuccess = FALSE;

        while ((readSuccess = InternetReadFile(hUrl, buffer, sizeof(buffer), &bytesRead)) && bytesRead > 0) {
            fwrite(buffer, 1, bytesRead, file);
            sessionDownloaded += bytesRead;

            // Simple progress feedback (dot every ~800KB)
            if ((existingSize + sessionDownloaded) % (CHUNK_SIZE * 100) < CHUNK_SIZE) {
                printf(".");
                fflush(stdout);
            }
        }

        fclose(file);
        InternetCloseHandle(hUrl);

        // Check if stream finished cleanly or dropped connection mid-file
        if (!readSuccess) {
            attempt++;
            printf("\nNetwork connection dropped! Progress saved (%I64u bytes).\n", 
                   get_local_file_size(outFile));
            printf("Retrying (%d/%d) in %d seconds...\n", attempt, MAX_RETRIES, RETRY_DELAY_MS / 1000);
            Sleep(RETRY_DELAY_MS);
        } else {
            // InternetReadFile returned TRUE and bytesRead == 0 (Normal EOF)
            isComplete = 1;
            printf("\nDownload complete! Final size: %I64u bytes.\n", get_local_file_size(outFile));
        }
    }

    InternetCloseHandle(hInternet);

    if (!isComplete) {
        fprintf(stderr, "\nDownload failed after %d attempts. You can run the program again to resume.\n", MAX_RETRIES);
        return 1;
    }

    return 0;
}