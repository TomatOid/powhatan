#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>

/*
* HTTP Structs*
*/

typedef struct {
    char * method;
    char * url;
} Request;

typedef struct {
    int status_code;
    char * data;
    long data_length; // Required for binary files. Must keep the size of the file.
    char * data_type;
} Response;

Request getRequest(char * read_buf, long size);
Response createErrorMsg(int status_code, char * data);
int sendResponse(Response response_data, int request);


