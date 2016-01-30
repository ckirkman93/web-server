#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <errno.h>
#include <time.h>
#include <sys/stat.h>
#include <assert.h>

// Globals
unsigned short g_usPort;
char *pathToFile;

int DEFAULT_PORT = 80;
int CHUNK_SIZE = 1024;
char GET[] = "GET";
char ROOT_DIR[] = "./web_root";
char DEFAULT_FILE[] = "index.html";
char DEFAULT_FILE_2[] = "index.htm";
char DELIMITER[] = "\r\n\r\n";

// Function Prototypes
void parse_args(int argc, char **argv);

int parseRequestMethod(char request[], char file[], int *responseStatus);
int getPathToFile(char **pathToFile, char request[], int *responseStatus);
void buildResponseHeader(int httpStatusCode, char pathToFile[], char **respHeader);
int getFormattedDate(char **dateString, time_t timeVal);
int getContentType(char **contentType, char *pathToFile);
void getResponseContent(char pathToFile[], char **respContent, int *contentLength);
int sendResponse(char responseHeader[], char responseContent[]);

// Function Implementations

int main(int argc, char **argv) {
    parse_args(argc, argv);
    printf("Starting TCP server on port: %hu\n", g_usPort);

    // Set up listening socket address.
    struct sockaddr_in svr_addr;
    svr_addr.sin_family = AF_INET;
    // Host to network short - converting given port to be used in network
    svr_addr.sin_port = htons(g_usPort);
    // Host to network long - allows socket to "bind to all local interfaces"
    svr_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    // Arbitrary
    int maxClients = 10;

    // Create listening socket.
    int svr_sock = socket(AF_INET, SOCK_STREAM, 0);
    // This allows the socket to be reused immediately.
    setsockopt(svr_sock, SOL_SOCKET, SO_REUSEADDR, &(int){ 1 }, sizeof(int));

    // Bind server to the given port number.
    if (bind(svr_sock, (struct sockaddr*) &svr_addr, sizeof (svr_addr)) < 0) {
        printf("Bind failed.\n");
        return 0;
    }

    // Listen for clients.
    if (listen(svr_sock, maxClients) < 0) {
        printf("Server full.\n");
        return 0;
    }

    // Main server loop
    for (;;) {
        printf("Listening for client...\n");
        
        // Create client address struct.
        struct sockaddr_in client_addr;
        socklen_t client_addr_len = sizeof (client_addr);

        // Accept incoming request to connect from a client.
        int client_sock = accept(svr_sock, (struct sockaddr*) &client_addr,
                &client_addr_len);       
        fprintf(stderr, "Client connected.\n");

        // Create an array to store the client's request.
        char *request;
        if ((request = malloc(sizeof(char) * CHUNK_SIZE)) == NULL) {
            fprintf(stderr, "Out of memory error.\n");
            return -1;
        }
        
        /**Receive Request**/
        //
        // Receive first chunk.
        int bytesRcvd = recv(client_sock, request, CHUNK_SIZE, 0);
        if (bytesRcvd < 0) {
            fprintf(stderr, "Failed to receive\n");
            continue;
        }
        
        // Keep receiving until the delimiter is encountered.
        if (!strstr(request, DELIMITER) && !strchr(request, '\0')) {
            int totalBytesRcvd = bytesRcvd;
            int reallocCount = 0;
            for(;;) {
                if (totalBytesRcvd >= CHUNK_SIZE) {
                    reallocCount++;
                    request = realloc(request, CHUNK_SIZE + (CHUNK_SIZE * reallocCount) * sizeof(char));
                }
                bytesRcvd = recv(client_sock, request + totalBytesRcvd, reallocCount - totalBytesRcvd, 0);
                if (strstr(request + totalBytesRcvd, DELIMITER) || strchr(request + totalBytesRcvd, '\0')) {
                    break;
                }
                totalBytesRcvd += bytesRcvd;
            }  
        }
        fprintf(stderr, "Got request:\n%s", request);
        //
        /**End Receive Request**/
        
        /**Setting up variables & buffers**/
        //
        int responseStatus = 0; // HTTP status code to be returned
        int contentLength = 0;
        if ((pathToFile = malloc(CHUNK_SIZE * sizeof(char))) == NULL) {
            fprintf(stderr, "Out of memory error (pathToFile).\n");
            continue;
        }        
        char *responseHeader;
        if ((responseHeader = malloc(CHUNK_SIZE * sizeof(char))) == NULL) {
            fprintf(stderr, "Out of memory error (responseHeader).\n");
            continue;
        }
        char *responseContent;
        if ((responseContent = malloc(CHUNK_SIZE * sizeof(char))) == NULL) {
            fprintf(stderr, "Out of memory error (responseContent).\n");
            continue;
        }
        //
        /**End variable & buffer setup**/
        
        /*Parse the request: ie, is it a GET?*/        
        if (parseRequestMethod(request, pathToFile, &responseStatus) == 0) {
            buildResponseHeader(responseStatus, NULL, &responseHeader);
        }
        
        if (responseStatus == 0) {
            /* Get the path to the file it's requesting (if there has been no error thus far) */
            int pathResult;
            if ((pathResult = getPathToFile(&pathToFile, request, &responseStatus)) == 0) {
                buildResponseHeader(responseStatus, NULL, &responseHeader);
            }
        }
        
        /* If there still hasn't been an error yet, it means the requested file
           exists and the request was valid, so this should be a successful response. */
        if (responseStatus == 0) {
            responseStatus = 200;
            
            // The call to stat() in buildResponseHeader() appears to screw up my pathToFile variable??
            char pathBackup[strlen(pathToFile)];
            strcpy(pathBackup, pathToFile);

            responseStatus = 200;
            buildResponseHeader(200, pathToFile, &responseHeader);

            strcpy(pathToFile, pathBackup);        
            getResponseContent(pathToFile, &responseContent, &contentLength);           
        }
        
        /* Sending in main() because subroutines + strings are apparently 
         * Too Advanced for the likes of me. :) */
        
        int headerBytesSent = 0;
        int headerLen = strlen(responseHeader);        
        
        /* Sending header */
        fprintf(stderr, "Sending header...\n");
        while (headerBytesSent < headerLen) {
            // Keep calling send until the entire file is sent.
            // TODO: What's up with the plus one? I forget.
            int sendResult = send(client_sock, responseHeader + headerBytesSent, headerLen - headerBytesSent, 0);
            if (sendResult == -1) {
                fprintf(stderr, "Failed to send header.\n");
                continue;
            }
            headerBytesSent += sendResult;
        }        
        
        /* Sending content */
        if (responseStatus == 200) {
            fprintf(stderr, "Sending content...\n");
            int contentBytesSent = 0;
            
            while (contentBytesSent < contentLength) {
                int sendResult = send(client_sock, responseContent + contentBytesSent, contentLength - contentBytesSent, 0);
                if (sendResult == -1) {
                    fprintf(stderr, "Failed to send content.\n");
                    return -1;
                }
                contentBytesSent += sendResult;
            }
        }
        
        free(request);
        //free(pathToFile); -> results in 'double free' error??
        free(responseHeader);
        free(responseContent);

        close(client_sock);
        fprintf(stderr, "Connection closed.\n------\n\n");
    }

    return 0;
}

/* Returns 1 if a GET request is detected.
           0 if it is not a GET request, sets the responseStatus accordingly. */
int parseRequestMethod(char request[], char file[], int *responseStatus) {
    int retVal = 0;
    char requestMethod[10];
    
    if (sscanf(request, "%[^ ] %[^ ]", requestMethod, file) != 2) {
        *responseStatus = 400;
        return 0;
    }
    
    switch (requestMethod[0]) {
        case 'G':
            if (strcmp(requestMethod, "GET") == 0) {
                retVal = 1;
                fprintf(stderr, "Detected GET request.\n");
            }
            break;
        case 'O':
            if (strcmp(requestMethod, "OPTIONS") == 0) {
               *responseStatus = 501;
            }
            break;
        case 'H':
            if (strcmp(requestMethod, "HEAD") == 0) {
                *responseStatus = 501;
            }
            break;
        case 'P':
            if (strcmp(requestMethod, "PUT") == 0 || strcmp(requestMethod, "POST") == 0) {
                *responseStatus = 501;
            }
            break;
        case 'D':
            if (strcmp(requestMethod, "DELETE") == 0) {
                *responseStatus = 501;
            }
            break;
        case 'T':
            if (strcmp(requestMethod, "TRACE") == 0) {
                *responseStatus = 501;
            }
            break;
        case 'C':
            if (strcmp(requestMethod, "CONNECT") == 0) {
                *responseStatus = 501;
            }
            break;
        default:
            *responseStatus = 400;
    }
    return retVal;
}

/* Returns: 1 if the path given is a valid file/directory
            0 if is not, or if there is an error; sets the response status accordingly
            If it's a directory, this adds (/)index.htm(l) onto the end of 'file' in main() */
int getPathToFile(char **pathToFile, char request[], int *responseStatus) {
    FILE *fileP; 
    char *extension = malloc(sizeof(char) * 5); 
    
    if (strstr(*pathToFile, "..") != NULL) {
        // HACKERS
        *responseStatus = 500;
        return 0;
    }
    
    // Prepend the requested path with the web root directory.
    int requestedPathLen = strlen(ROOT_DIR) + strlen(*pathToFile);
    char pathFromRoot[requestedPathLen];
    strcpy(pathFromRoot, ROOT_DIR);
    strcat(pathFromRoot, *pathToFile);    
    
    if ((extension = strchr(pathFromRoot + 1, '.')) != NULL) {
        if (strcmp(extension, ".html") == 0 ||
            strcmp(extension, ".htm") == 0 ||
            strcmp(extension, ".txt") == 0 ||
            strcmp(extension, ".gif") == 0 ||
            strcmp(extension, ".jpg") == 0 ||
            strcmp(extension, ".jpeg") == 0) {
            // It's a file with a supported extension.
            // Check for existence.   
            fileP = fopen(pathFromRoot, "r");
            if (fileP == NULL) {
                *responseStatus = 404;
                return 0;
            } else { // It exists, just set pathToFile to pathFromFile.
                strcpy(*pathToFile, pathFromRoot);
                return 1;
            }
        } else { // It's a file without a supported extension
            *responseStatus = 500;
            return 0;
        }
    } else { // It's a directory.
        
        free(extension);
        
        // For index.html and index.htm
        int pathSize = strlen(pathFromRoot) + strlen(DEFAULT_FILE);
        int pathSize2 = strlen(pathFromRoot) + strlen(DEFAULT_FILE_2);
        
        char pathToDefaultFile[pathSize + 1];
        char pathToDefaultFile2[pathSize2 + 1];
        
        strcpy(pathToDefaultFile, pathFromRoot);
        // Tack on a / at the end of the path if it's missing.
        if (pathToDefaultFile[strlen(pathFromRoot) - 1] != '/') {
            strcat(pathToDefaultFile, "/");
        }
        // Make a copy of this intermediate for the second default file.
        strcpy(pathToDefaultFile2, pathToDefaultFile);
        
        strcat(pathToDefaultFile, DEFAULT_FILE);
        
        // Try to get the first default file
        fileP = fopen(pathToDefaultFile, "r");
        if (fileP != NULL) {
            strcpy(*pathToFile, pathToDefaultFile);
            return 1;
        } else {            
            // Try to get the second default file
            strcat(pathToDefaultFile2, DEFAULT_FILE_2);
            fileP = fopen(pathToDefaultFile2, "r");
            if (fileP != NULL) {
                strcpy(*pathToFile, pathToDefaultFile2);
                return 1;
            } else {
                *responseStatus = 404;
                return 0;
            }
        }
    }
}

void buildResponseHeader(int httpStatusCode, char *pathToFile, char **respHeader) {    
    char *response = malloc(CHUNK_SIZE * sizeof(char));
    strcpy(response, "HTTP/1.1 ");
    
    switch(httpStatusCode) {
        case 200:
            strcat(response, "200 OK\r\n");
            break;
        case 400:
            strcat(response, "400 Bad Request\r\n");
            break;
        case 404:
            strcat(response, "404 Not Found\r\n");
            break;
        case 500:
            strcat(response, "500 Internal Server Error\r\n");
            break;
        case 501:
            strcat(response, "501 Not Implemented\r\n");
            break;
        default:
            strcat(response, "500 Internal Server Error\r\n");
    }
    
    strcat(response, "Connection: close\r\n");
    
    char *dateString;
    dateString = malloc(100 * sizeof(char));
    assert(dateString != NULL);
    
    getFormattedDate(&dateString, 0);
    
    char dateHeaderLine[100];    
    strcpy(dateHeaderLine, "Date: ");
    strcat(dateHeaderLine, dateString);
    strcat(dateHeaderLine, "\r\n");
    
    //free(dateString); -> double free error    
    strcat(response, dateHeaderLine);
            
    if (httpStatusCode == 200) {
        
        struct stat *statBuffer;
        statBuffer = malloc(sizeof(struct stat));
        
        stat(pathToFile, statBuffer);
        
        int fileSize = statBuffer->st_size;
        time_t lastModDate = statBuffer->st_mtime;
        
        /*Append response header with Content-Length line*/
        char *contentLength = malloc(sizeof(char) * 100);
        assert(contentLength != NULL);
        sprintf(contentLength, "Content-Length: %d\r\n", fileSize);
        strcat(response, contentLength);
        
        /*Append response header with Last-Modified line*/
        char *lastModDateString;
        lastModDateString = malloc(100 * sizeof(char));
        assert(lastModDateString != NULL); 
                
        getFormattedDate(&lastModDateString, lastModDate);
        strcat(response, "Last-Modified: ");
        strcat(response, lastModDateString);
        strcat(response, "\r\n");
        
        /*Append response header with Content-Type line (if the type is supported)*/
        char *contentType = malloc(50 * sizeof(char));
        assert(contentType != NULL);
        
        if (getContentType(&contentType, pathToFile) == 1) {
            strcat(response, "Content-Type: ");
            strcat(response, contentType);
            strcat(response, "\r\n");
        }        
    }    
    strcat(response, "\r\n");
    *respHeader = response;
    
    fprintf(stderr, "Response Header is:\n\n%s", response);
}

/* Returns content if successful,
           NULL if it was not, sets response status */
void getResponseContent(char *pathToFile, char **responseContent, int *contentLength) {
    FILE *fileP;
    int bufferSize = CHUNK_SIZE;
    char filePathBuffer[CHUNK_SIZE];
    
    /* If I don't make a copy, pathToFile gets overwritten
     * when I call fopen(), for some reason. Clearly I'm missing something. */
    strcpy(filePathBuffer, pathToFile);
    
    fileP = fopen(filePathBuffer, "rb");
    
    struct stat *statBuffer;
    statBuffer = malloc(sizeof(struct stat));
        
    stat(filePathBuffer, statBuffer);
    *contentLength = statBuffer->st_size;
    
    if (*contentLength > bufferSize) {
        *responseContent = realloc(*responseContent, *contentLength);
    }
    
    int bytesRead = fread(*responseContent, 1, *contentLength, fileP);
    
    fclose(fileP);
    
    if (bytesRead != *contentLength) {
        fprintf(stderr, "Error reading file\n");
    }
}

int getFormattedDate(char **dateString, time_t timeVal) {
    char buffer[100];
    struct tm *timeStrc;
    
    if (timeVal == 0) {
        timeVal = time(NULL);
    }
    
    timeStrc = localtime(&timeVal);
    
    // TODO: error checking
    strftime(buffer, 100, "%a, %d %b %Y %T %Z", timeStrc);
    *dateString = buffer;
    
    return 1;
}

int getContentType(char **contentType, char *pathToFile) {
    char *extension = strchr(pathToFile + 1, '.');
    if (strcmp(extension, ".html") == 0 || strcmp(extension, ".htm") == 0) {
        *contentType = "text/html";
    } else if (strcmp(extension, ".txt") == 0) {
        *contentType = "text/plain";
    } else if (strcmp(extension, ".jpg") == 0 || strcmp(extension, ".jpeg") == 0) {
        *contentType = "image/jpeg";
    } else if (strcmp(extension, ".gif") == 0) {
        *contentType = "image/gif";
    } else { // Unsupported type
        return 0;
    }    
    return 1;
}

void parse_args(int argc, char **argv) {
    unsigned long ulPort;
    if (argc == 2) {
        errno = 0;
        char *endptr = NULL;
        ulPort = strtoul(argv[1], &endptr, 10);

        if (0 == errno) {
            if ('\0' != endptr[0])
                errno = EINVAL;
            else if (ulPort > USHRT_MAX)
                errno = ERANGE;
        }
        if (0 != errno) {
            // Report any errors and abort
            fprintf(stderr, "Failed to parse port number \"%s\": %s\n",
                    argv[1], strerror(errno));
            abort();
        }
    } else {
        ulPort = DEFAULT_PORT;
    }
    g_usPort = ulPort;
}
