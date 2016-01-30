#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <assert.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>

#define DEFAULT_HTTP_PORT 80
#define CHUNK_SIZE 1024

typedef struct url_s {
    unsigned short usPort; // in host byte order
    char *szServer; // allocated by parse_url(), must be freed by the caller
    char *szFile; // allocated by parse_url(), must be freed by the caller
} url_t;

struct hostent *host;

const char *delimiter = "\r\n\r\n";

url_t parse_url(const char *szURL) {
    url_t url;
    memset(&url, 0, sizeof (url));

    unsigned int urllen = strlen(szURL) + 1;
    url.szServer = (char*) malloc(urllen * sizeof (char));
    assert(NULL != url.szServer);
    url.szFile = (char*) malloc(urllen * sizeof (char));
    assert(NULL != url.szFile);

    char server[urllen];

    /* Ignores the 'http://' which begins the string. Put everything not a 
     * '/' into 'server', the rest goes into the file string. */
    int result = sscanf(szURL, "http://%[^/]/%s", server, url.szFile);
    if (EOF == result) {
        fprintf(stderr, "Failed to parse URL: %s\n", strerror(errno));
        exit(1);
    } else if (1 == result) {
        url.szFile[0] = '\0';
    } else if (result < 1) {
        fprintf(stderr, "Error: %s is not a valid HTTP request\n", szURL);
        exit(1);
    }

    /* Puts everything up to a ':' character into the server string.
     * The number after is the port. */
    result = sscanf(server, "%[^:]:%hu", url.szServer, &url.usPort);
    if (EOF == result) {
        fprintf(stderr, "Failed to parse URL: %s\n", strerror(errno));
        exit(1);
    } else if (1 == result) {
        url.usPort = DEFAULT_HTTP_PORT;
    } else if (result < 1) {
        fprintf(stderr, "Error: %s is not a valid HTTP request\n", szURL);
        exit(1);
    }

    assert(NULL != url.szServer);
    assert(NULL != url.szFile);
    assert(url.usPort > 0);
    return url;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr,
                "Usage: http_client URL\n");
        exit(1);
    }

    // Parse the URL

    url_t url = parse_url(argv[1]);
    fprintf(stderr, "Server: %s:%hu\nFile: /%s\n",
            url.szServer, url.usPort, url.szFile);

    if ((host = gethostbyname(url.szServer)) == NULL) {
        fprintf(stderr, "Invalid host '%s.'\n", url.szServer);
        exit(1);
    }

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        fprintf(stderr, "Error creating socket.\n");
        exit(1);
    }

    // Set up server address.
    struct sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
    memcpy(&serv_addr.sin_addr, host->h_addr_list[0], host->h_length);
    serv_addr.sin_port = htons(url.usPort);

    if (connect(sock, (struct sockaddr*) &serv_addr, sizeof (serv_addr)) < 0) {
        fprintf(stderr, "Connection failed. ;_;\n");
        return -1;
    }
    fprintf(stderr, "Connected to server. :D\n\n");

    char request[CHUNK_SIZE];
    char *response;
    response = (char*) malloc(sizeof(char*) * CHUNK_SIZE);
    if (response == NULL) {
        fprintf(stderr, "Error allocating memory!\n");
        return -1;
    }
    
    // Create the request.
    sprintf(request, "GET /%s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n", url.szFile, url.szServer);
    fprintf(stderr, "Sending HTTP request:\n----\n%s", request);

    int requestLen = strlen(request);
    int totalBytesSent = 0;

    while (totalBytesSent < requestLen) {
        // Keep calling send until the entire request is sent.
        int sendResult = send(sock, request + totalBytesSent, requestLen - totalBytesSent + 1, 0);
        if (sendResult == -1) {
            fprintf(stderr, "Request sending failed.\n");
            return -1;
        }
        totalBytesSent += sendResult;
    }

    // Receive the server's response.
    int totalBytesRcvd = 0;
    int bytesRcvd = 0;
    int reallocCount = 1;
    for(;;) {
        if (totalBytesRcvd >= CHUNK_SIZE) {
            reallocCount++;
            response = (char*) realloc(response, CHUNK_SIZE * reallocCount * sizeof(char*));
            if (response == NULL) {
                fprintf(stderr, "Error reallocating memory while receiving.\n");
                return -1;
            }
        }
        bytesRcvd = recv(sock, response + totalBytesRcvd, CHUNK_SIZE, 0);
        if (bytesRcvd == -1) {
            fprintf(stderr, "Error receiving response.\n");
            return -1;
        }
        if (bytesRcvd == 0) {
            break;
        }
        totalBytesRcvd += bytesRcvd;
    }

    /* Setting the end of the header by the fact that a string's ending is '\0'.
     *(Changes the first character of the delimiter to \0.) */
    char *headerEnd;
    if ((headerEnd = strstr(response, delimiter)) != NULL) {
        *headerEnd = '\0';
    } else {
        fprintf(stderr, "Unable to parse server's response. No header end delimiter found.\n");
        return -1;
    }
    
    // Renaming the first part of the response to responseHeader.
    char *responseHeader = response;
    
    // Making a pointer to the start of the content, which begins after the delimiter.
    char *responseContent = headerEnd;
    responseContent += strlen(delimiter);
    
    fprintf(stderr, "Response header:\n----\n%s\n\n", responseHeader);
    
    // Try to extract the content length from the header.
    int contentLenHeaderVal = 0;
    char *contentLenPtr, contentLenHeader[24] = "Content-Length: ";

    // Get a pointer to the occurrence of "Content-Length: ".
    if ((contentLenPtr = strstr(response, contentLenHeader)) != NULL) {
        // contentLenPtr now points to beginning of "Content-Length: ";
        // Moving it to the beginning of the integer value.
        contentLenPtr += strlen(contentLenHeader);
        sscanf(contentLenPtr, "%d", &contentLenHeaderVal);
    } else {
        fprintf(stderr, "No Content-Length header line found.\n");
    }

    int contentBytesRcvd = totalBytesRcvd - strlen(responseHeader) - strlen(delimiter);
    
    fwrite(responseContent, sizeof(char), contentBytesRcvd, stdout);
    
    char endMessage[1000];
    if (contentBytesRcvd > 0) {
        sprintf(endMessage, "Received %d of %d bytes of content.\n", contentBytesRcvd, contentLenHeaderVal);
    } else {
        sprintf(endMessage, "No content received.\n");
    }
    fprintf(stderr, "\n%s", endMessage);
    

    close(sock);
    
    free(response);
    free(url.szServer);
    free(url.szFile);
    
    return 0;
}
