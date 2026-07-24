/*
 * ==============================================================================
 * PUBLIC DOMAIN
 * ==============================================================================
 * COMPILE INSTRUCTIONS (GCC / MinGW on Windows):
 *   gcc -Os -s -o adhocsync-by-version02.exe adhocsync-by-version02.c -lws2_32
 * ==============================================================================
 */

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdbool.h>

#pragma comment(lib, "ws2_32.lib")

#define DELIM "¦"
#define MAX_SERVERS 7
#define TCP_TIMEOUT_MS 100 
#define CSV_HEADER "ID" DELIM "Title" DELIM "StartMin" DELIM "Duration" DELIM "Color" DELIM "Date" DELIM "PersonIdx" DELIM "Version" DELIM "LastModifiedBy\n"

// Global Configuration Variables
char g_iniPath[MAX_PATH];
char g_csvPath[MAX_PATH];
char g_logPath[MAX_PATH];
char g_myIP[64] = "127.0.0.1 (Fallback)";

int g_port = 9876;
int g_syncIntervalMs = 180000;
bool g_logging = false;
int g_deleteThreshold = 100;
char g_servers[MAX_SERVERS][64];
int g_myNodeID = 1;

// Track sync cycles per server node index
int g_nodeSyncCycles[MAX_SERVERS] = {0};

typedef struct {
    char id[64];
    int version;
    char *lineContent;
} CsvRow;

typedef struct {
    char id[64];
    int version;
} ClientRowState;

void _Log(const char *format, ...) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    char timeBuf[64];
    snprintf(timeBuf, sizeof(timeBuf), "[%04d-%02d-%02d %02d:%02d:%02d]", 
            t->tm_year + 1900, t->tm_mon + 1, t->tm_mday, 
            t->tm_hour, t->tm_min, t->tm_sec);

    va_list args;
    printf("%s ", timeBuf);
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    printf("\n");

    if (g_logging) {
        FILE *f = fopen(g_logPath, "a");
        if (f) {
            fprintf(f, "%s ", timeBuf);
            va_start(args, format);
            vfprintf(f, format, args);
            va_end(args);
            fprintf(f, "\n");
            fclose(f);
        }
    }
}

bool _IsIP(const char *ip) {
    int a, b, c, d;
    return sscanf(ip, "%d.%d.%d.%d", &a, &b, &c, &d) == 4;
}

bool _IsLocalIP(const char *ip) {
    if (strcmp(ip, "127.0.0.1") == 0 || strcmp(ip, "localhost") == 0) return true;
    
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) == SOCKET_ERROR) return false;
    struct hostent *host = gethostbyname(hostname);
    if (!host) return false;

    for (int i = 0; host->h_addr_list[i] != NULL; i++) {
        struct in_addr addr;
        memcpy(&addr, host->h_addr_list[i], sizeof(struct in_addr));
        if (strcmp(inet_ntoa(addr), ip) == 0) return true;
    }
    return false;
}

void InitConfig(const char *argv0) {
    char drive[_MAX_DRIVE], dir[_MAX_DIR], fname[_MAX_FNAME], ext[_MAX_EXT];
    _splitpath(argv0, drive, dir, fname, ext);

    snprintf(g_iniPath, sizeof(g_iniPath), "%s%s%s.ini", drive, dir, fname);
    snprintf(g_csvPath, sizeof(g_csvPath), "%s%s%s.csv", drive, dir, fname);
    snprintf(g_logPath, sizeof(g_logPath), "%s%s%s.log", drive, dir, fname);

    FILE *f = fopen(g_iniPath, "r");
    if (!f) {
        f = fopen(g_iniPath, "w");
        if (f) {
            fprintf(f, "[Network]\nPort=9876\nSyncIntervalMs=180000\nLogging=1\nDeleteThreshold=100\n");
            fprintf(f, "[Servers]\n");
            for (int i = 1; i <= MAX_SERVERS; i++) {
                fprintf(f, "Server%d=0\n", i);
            }
            fclose(f);
        }
    } else {
        fclose(f);
    }

    FILE *fCsv = fopen(g_csvPath, "r");
    if (!fCsv) {
        fCsv = fopen(g_csvPath, "w");
        if (fCsv) {
            fprintf(fCsv, CSV_HEADER);
            fclose(fCsv);
        }
    } else {
        fclose(fCsv);
    }

    char temp[256];
    GetPrivateProfileStringA("Network", "Port", "9876", temp, sizeof(temp), g_iniPath);
    g_port = atoi(temp);
    GetPrivateProfileStringA("Network", "SyncIntervalMs", "180000", temp, sizeof(temp), g_iniPath);
    g_syncIntervalMs = atoi(temp);
    GetPrivateProfileStringA("Network", "Logging", "1", temp, sizeof(temp), g_iniPath);
    g_logging = (atoi(temp) == 1);
    GetPrivateProfileStringA("Network", "DeleteThreshold", "100", temp, sizeof(temp), g_iniPath);
    g_deleteThreshold = atoi(temp);

    for (int i = 1; i <= MAX_SERVERS; i++) {
        char key[32];
        snprintf(key, sizeof(key), "Server%d", i);
        GetPrivateProfileStringA("Servers", key, "0", g_servers[i - 1], sizeof(g_servers[i - 1]), g_iniPath);
    }

    g_myNodeID = 1;
    for (int i = 0; i < MAX_SERVERS; i++) {
        if (strcmp(g_servers[i], "0") != 0 && _IsLocalIP(g_servers[i])) {
            g_myNodeID = i + 1;
            strcpy(g_myIP, g_servers[i]);
            break;
        }
    }
}

bool _ConnectTimeout(SOCKET sock, struct sockaddr_in *addr, int timeout_ms) {
    u_long mode = 1; 
    ioctlsocket(sock, FIONBIO, &mode);
    connect(sock, (struct sockaddr*)addr, sizeof(*addr));

    fd_set writefds;
    FD_ZERO(&writefds);
    FD_SET(sock, &writefds);

    struct timeval tv = { timeout_ms / 1000, (timeout_ms % 1000) * 1000 };
    int ret = select(0, NULL, &writefds, NULL, &tv);

    mode = 0; 
    ioctlsocket(sock, FIONBIO, &mode);

    if (ret > 0) {
        int err = 0;
        int errlen = sizeof(err);
        getsockopt(sock, SOL_SOCKET, SO_ERROR, (char*)&err, &errlen);
        return (err == 0);
    }
    return false;
}

int _RecvTimeout(SOCKET sock, char *buf, int len, int timeout_ms) {
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(sock, &readfds);
    struct timeval tv = { timeout_ms / 1000, (timeout_ms % 1000) * 1000 };
    int ret = select(0, &readfds, NULL, NULL, &tv);
    if (ret > 0) return recv(sock, buf, len, 0);
    return -1;
}

int _SendTimeout(SOCKET sock, const char *buf, int len, int timeout_ms) {
    fd_set writefds;
    FD_ZERO(&writefds);
    FD_SET(sock, &writefds);
    struct timeval tv = { timeout_ms / 1000, (timeout_ms % 1000) * 1000 };
    int ret = select(0, NULL, &writefds, NULL, &tv);
    if (ret > 0) return send(sock, buf, len, 0);
    return -1;
}

bool _IsCsvEmpty() {
    FILE *f = fopen(g_csvPath, "r");
    if (!f) return true;
    char line[512];
    int dataRows = 0;
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "ID" DELIM, 3) == 0) continue;
        if (strlen(line) > 3) dataRows++;
    }
    fclose(f);
    return (dataRows == 0);
}

void _MergeCsvData(const char *csvFile, const char *newCsvContent) {
    if (!newCsvContent || strlen(newCsvContent) == 0) return;

    CsvRow *localRows = NULL;
    int rowCount = 0;

    FILE *f = fopen(csvFile, "r");
    if (f) {
        char line[512];
        while (fgets(line, sizeof(line), f)) {
            if (strncmp(line, "ID" DELIM, 3) == 0) continue;
            if (strlen(line) <= 3) continue;

            localRows = realloc(localRows, (rowCount + 1) * sizeof(CsvRow));
            localRows[rowCount].lineContent = _strdup(line);
            
            char *nextToken = NULL;
            char *copy = _strdup(line);
            char *tok = strtok_s(copy, DELIM, &nextToken);
            if (tok) strcpy(localRows[rowCount].id, tok);
            
            int col = 0;
            localRows[rowCount].version = 0;
            while (tok) {
                if (col == 7) localRows[rowCount].version = atoi(tok);
                tok = strtok_s(NULL, DELIM, &nextToken);
                col++;
            }
            free(copy);
            rowCount++;
        }
        fclose(f);
    }

    char *deltaCopy = _strdup(newCsvContent);
    char *nextDeltaLine = NULL;
    char *deltaLine = strtok_s(deltaCopy, "\r\n", &nextDeltaLine);

    int updatedCount = 0;
    int insertedCount = 0;

    while (deltaLine) {
        if (strncmp(deltaLine, "ID" DELIM, 3) == 0) {
            deltaLine = strtok_s(NULL, "\r\n", &nextDeltaLine);
            continue;
        }

        if (strlen(deltaLine) > 5) {
            char lineBuf[512];
            snprintf(lineBuf, sizeof(lineBuf), "%s\n", deltaLine);

            char *nextToken = NULL;
            char *copy = _strdup(deltaLine);
            char *tok = strtok_s(copy, DELIM, &nextToken);
            char newId[64] = {0};
            int newVersion = 0;

            if (tok) strcpy(newId, tok);
            
            int col = 0;
            while (tok) {
                if (col == 7) newVersion = atoi(tok);
                tok = strtok_s(NULL, DELIM, &nextToken);
                col++;
            }
            free(copy);

            bool found = false;
            for (int i = 0; i < rowCount; i++) {
                if (strcmp(localRows[i].id, newId) == 0) {
                    found = true;
                    if (newVersion >= localRows[i].version) {
                        if (newVersion > localRows[i].version) {
                            updatedCount++;
                            _Log("[CLIENT] Incoming Sync Write (Updated) -> ID: %s | Ver: %d", newId, newVersion);
                        }
                        free(localRows[i].lineContent);
                        localRows[i].lineContent = _strdup(lineBuf);
                        localRows[i].version = newVersion;
                    }
                    break;
                }
            }

            if (!found) {
                localRows = realloc(localRows, (rowCount + 1) * sizeof(CsvRow));
                strcpy(localRows[rowCount].id, newId);
                localRows[rowCount].version = newVersion;
                localRows[rowCount].lineContent = _strdup(lineBuf);
                rowCount++;
                insertedCount++;
                _Log("[CLIENT] Incoming Sync Write (Inserted) -> ID: %s | Ver: %d", newId, newVersion);
            }
        }
        deltaLine = strtok_s(NULL, "\r\n", &nextDeltaLine);
    }
    free(deltaCopy);

    if (updatedCount > 0 || insertedCount > 0 || rowCount > 0) {
        char tempFile[MAX_PATH];
        snprintf(tempFile, sizeof(tempFile), "%s.tmp", csvFile);
        
        FILE *fw = fopen(tempFile, "w");
        if (fw) {
            fprintf(fw, CSV_HEADER);
            for (int i = 0; i < rowCount; i++) {
                fprintf(fw, "%s", localRows[i].lineContent);
            }
            fclose(fw);
            
            remove(csvFile);
            rename(tempFile, csvFile);
        }
    }

    for (int i = 0; i < rowCount; i++) free(localRows[i].lineContent);
    free(localRows);
}

// Node-Specific Cleanup Function: Deletes only Color 2 entries originating from targetNodeID
void _ProcessNodeSpecificCleanup(const char *csvFile, int targetNodeID) {
    FILE *f = fopen(csvFile, "r");
    if (!f) return;

    char tempFile[MAX_PATH];
    snprintf(tempFile, sizeof(tempFile), "%s.tmp", csvFile);
    
    FILE *fw = fopen(tempFile, "w");
    if (!fw) { fclose(f); return; }

    char line[512];
    int deletedCount = 0;

    fprintf(fw, CSV_HEADER);

    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "ID" DELIM, 3) == 0) continue;

        char lineCopy[512];
        strcpy(lineCopy, line);
        lineCopy[strcspn(lineCopy, "\r\n")] = 0;
        
        if (strlen(lineCopy) == 0) continue; 
        
        char *nextToken = NULL;
        char *token = strtok_s(lineCopy, DELIM, &nextToken);
        char fields[10][128];
        int fCount = 0;
        while (token != NULL && fCount < 10) {
            strcpy(fields[fCount++], token);
            token = strtok_s(NULL, DELIM, &nextToken);
        }

        // Field Indices: 4 = Color, 8 = LastModifiedBy
        int color = (fCount >= 5) ? atoi(fields[4]) : 0;
        int modifiedBy = (fCount >= 9) ? atoi(fields[8]) : 0;

        if (color == 2 && modifiedBy == targetNodeID) {
            deletedCount++;
            _Log("[CLEANUP] Deleted entry ID: %s originating from Node %d (Color is 2)", fields[0], targetNodeID);
            continue;
        }

        fprintf(fw, "%s", line);
    }
    
    fclose(f);
    fclose(fw);

    if (deletedCount > 0) {
        remove(csvFile);
        rename(tempFile, csvFile);
        _Log("Cleanup Complete for Node %d. Removed %d items.", targetNodeID, deletedCount);
    } else {
        remove(tempFile);
    }
}

void _HandleServerClient(SOCKET clientSocket) {
    char buffer[4096];
    char *data = NULL;
    int dataSize = 0;
    int timeouts = 0;

    while (timeouts < 10) { 
        int bytesReceived = _RecvTimeout(clientSocket, buffer, sizeof(buffer) - 1, TCP_TIMEOUT_MS);
        if (bytesReceived > 0) {
            buffer[bytesReceived] = '\0';
            data = realloc(data, dataSize + bytesReceived + 1);
            memcpy(data + dataSize, buffer, bytesReceived + 1);
            dataSize += bytesReceived;
            timeouts = 0;
            if (strstr(data, "[EOF]")) break;
        } else {
            timeouts++;
        }
    }

    if (!data) {
        closesocket(clientSocket);
        return;
    }

    int reqNodeID = -1;
    int reqIsEmpty = 0;

    char *nextOuter = NULL;
    char *linePtr = strtok_s(data, "\r\n", &nextOuter);
    if (linePtr && sscanf(linePtr, "CLIENT_SYNC_DELTA" DELIM "%d" DELIM "%d", &reqNodeID, &reqIsEmpty) >= 2) {
        
        ClientRowState *clientStates = NULL;
        int clientCount = 0;

        linePtr = strtok_s(NULL, "\r\n", &nextOuter);
        while (linePtr) {
            if (strcmp(linePtr, "[EOF]") == 0) break;
            if (strlen(linePtr) > 0 && strncmp(linePtr, "ID" DELIM, 3) != 0) {
                char *nextToken = NULL;
                char *copy = _strdup(linePtr);
                char *tok = strtok_s(copy, DELIM, &nextToken);
                if (tok) {
                    clientStates = realloc(clientStates, (clientCount + 1) * sizeof(ClientRowState));
                    strcpy(clientStates[clientCount].id, tok);
                    tok = strtok_s(NULL, DELIM, &nextToken);
                    clientStates[clientCount].version = tok ? atoi(tok) : 0;
                    clientCount++;
                }
                free(copy);
            }
            linePtr = strtok_s(NULL, "\r\n", &nextOuter);
        }

        FILE *f = fopen(g_csvPath, "r");
        int sentRows = 0;
        
        if (f) {
            char csvLine[512];

            while (fgets(csvLine, sizeof(csvLine), f)) {
                if (strncmp(csvLine, "ID" DELIM, 3) == 0) continue;

                char *nextToken = NULL;
                char *copy = _strdup(csvLine);
                char *tok = strtok_s(copy, DELIM, &nextToken);
                char rowID[64] = {0};
                int rowVersion = 0;
                int rowModifiedBy = 0;
                int col = 0;

                while (tok) {
                    if (col == 0) strcpy(rowID, tok);
                    if (col == 7) rowVersion = atoi(tok);
                    if (col == 8) rowModifiedBy = atoi(tok);
                    tok = strtok_s(NULL, DELIM, &nextToken);
                    col++;
                }
                free(copy);

                bool allowOwnData = (reqIsEmpty == 1);
                if (!allowOwnData && rowModifiedBy == reqNodeID) {
                    continue;
                }

                int clientVer = -1; 
                for (int k = 0; k < clientCount; k++) {
                    if (strcmp(clientStates[k].id, rowID) == 0) {
                        clientVer = clientStates[k].version;
                        break;
                    }
                }

                if (clientVer == -1 || rowVersion > clientVer) {
                    _SendTimeout(clientSocket, csvLine, (int)strlen(csvLine), TCP_TIMEOUT_MS);
                    sentRows++;
                    _Log("[SERVER] Outgoing Sync Write -> ID: %s | Ver: %d", rowID, rowVersion);
                }
            }
            fclose(f);
        }
        
        free(clientStates);
        
        // Only log server response summary if rows were actually sent
        if (sentRows > 0) {
            _Log("Server: Sent %d total row(s) to Node %d.", sentRows, reqNodeID);
        }
    } 

    const char *eofMarker = "[EOF]";
    _SendTimeout(clientSocket, eofMarker, (int)strlen(eofMarker), TCP_TIMEOUT_MS);
    closesocket(clientSocket);
    free(data);
}

void _RunAllClientSyncs() {
    bool isEmpty = _IsCsvEmpty();

    char *idVerBuf = NULL;
    int idVerLen = 0;

    FILE *f = fopen(g_csvPath, "r");
    if (f) {
        char line[512];
        while (fgets(line, sizeof(line), f)) {
            if (strncmp(line, "ID" DELIM, 3) == 0) continue;
            
            char *nextToken = NULL;
            char *copy = _strdup(line);
            char *tok = strtok_s(copy, DELIM, &nextToken);
            char id[64] = {0};
            int ver = 0;
            int col = 0;

            while (tok) {
                if (col == 0) strcpy(id, tok);
                if (col == 7) ver = atoi(tok);
                tok = strtok_s(NULL, DELIM, &nextToken);
                col++;
            }
            free(copy);

            if (strlen(id) > 0) {
                char rowPair[128];
                snprintf(rowPair, sizeof(rowPair), "%s" DELIM "%d\n", id, ver);
                int pairLen = (int)strlen(rowPair);
                idVerBuf = realloc(idVerBuf, idVerLen + pairLen + 1);
                strcpy(idVerBuf + idVerLen, rowPair);
                idVerLen += pairLen;
            }
        }
        fclose(f);
    }

    for (int i = 0; i < MAX_SERVERS; i++) {
        if (strcmp(g_servers[i], "0") == 0 || strcmp(g_servers[i], "") == 0 || !_IsIP(g_servers[i])) continue;
        if (_IsLocalIP(g_servers[i])) continue;

        int targetNodeID = i + 1;

        SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sock == INVALID_SOCKET) continue;

        struct sockaddr_in serverAddr;
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_addr.s_addr = inet_addr(g_servers[i]);
        serverAddr.sin_port = htons((u_short)g_port);

        if (!_ConnectTimeout(sock, &serverAddr, TCP_TIMEOUT_MS)) {
            closesocket(sock);
            continue;
        }

        char header[256];
        snprintf(header, sizeof(header), "CLIENT_SYNC_DELTA" DELIM "%d" DELIM "%d\n", g_myNodeID, isEmpty ? 1 : 0);
        _SendTimeout(sock, header, (int)strlen(header), TCP_TIMEOUT_MS);

        if (idVerBuf && idVerLen > 0) {
            _SendTimeout(sock, idVerBuf, idVerLen, TCP_TIMEOUT_MS);
        }

        const char *eofMarker = "[EOF]";
        _SendTimeout(sock, eofMarker, (int)strlen(eofMarker), TCP_TIMEOUT_MS);

        char recvBuf[4096];
        char *receivedData = NULL;
        int recvSize = 0;
        int timeouts = 0;

        while (timeouts < 15) { 
            int bytes = _RecvTimeout(sock, recvBuf, sizeof(recvBuf) - 1, TCP_TIMEOUT_MS);
            if (bytes > 0) {
                recvBuf[bytes] = '\0';
                receivedData = realloc(receivedData, recvSize + bytes + 1);
                memcpy(receivedData + recvSize, recvBuf, bytes + 1);
                recvSize += bytes;
                timeouts = 0;
                if (strstr(receivedData, "[EOF]")) break;
            } else {
                timeouts++;
            }
        }

        // Successful sync connection achieved with targetNodeID
        if (receivedData) {
            char *eofPtr = strstr(receivedData, "[EOF]");
            if (eofPtr) *eofPtr = '\0';
            
            if (strlen(receivedData) > 0) {
                _MergeCsvData(g_csvPath, receivedData);
            }
            free(receivedData);

            // Increment cycle count ONLY for this specific node after a successful sync
            g_nodeSyncCycles[i]++;
            _Log("Successful sync completed with Node %d (Cycle count: %d/%d)", targetNodeID, g_nodeSyncCycles[i], g_deleteThreshold);

            if (g_nodeSyncCycles[i] >= g_deleteThreshold) {
                _Log("Delete threshold reached for Node %d. Running node-specific cleanup.", targetNodeID);
                _ProcessNodeSpecificCleanup(g_csvPath, targetNodeID);
                g_nodeSyncCycles[i] = 0;
            }
        }

        closesocket(sock);
    }

    free(idVerBuf);
}

int main(int argc, char *argv[]) {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) return 1;

    InitConfig(argv[0]);

    SOCKET listenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSock == INVALID_SOCKET) { WSACleanup(); return 1; }

    struct sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons((u_short)g_port);

    if (bind(listenSock, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        closesocket(listenSock); WSACleanup(); return 1;
    }

    if (listen(listenSock, 7) == SOCKET_ERROR) {
        closesocket(listenSock); WSACleanup(); return 1;
    }

    u_long mode = 1;
    ioctlsocket(listenSock, FIONBIO, &mode);

    _Log("================================================");
    _Log("   Delta Sync Service Started");
    _Log("   Node ID      : %d", g_myNodeID);
    _Log("   IP Address   : %s", g_myIP);
    _Log("   Port         : %d", g_port);
    _Log("   Interval (ms): %d", g_syncIntervalMs);
    _Log("================================================");

    DWORD syncTimer = GetTickCount();

    while (true) {
        struct sockaddr_in clientAddr;
        int clientAddrLen = sizeof(clientAddr);
        SOCKET clientSock = accept(listenSock, (struct sockaddr*)&clientAddr, &clientAddrLen);

        if (clientSock != INVALID_SOCKET) {
            _HandleServerClient(clientSock);
        }

        if (GetTickCount() - syncTimer >= (DWORD)g_syncIntervalMs) {
            syncTimer = GetTickCount();
            _RunAllClientSyncs();
        }

        Sleep(50);
    }

    closesocket(listenSock);
    WSACleanup();
    return 0;
}