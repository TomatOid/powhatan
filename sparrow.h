// Standard includes
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// System includes
#include <sys/socket.h>
#include <sys/epoll.h>
#include <sys/mman.h>

#include <arpa/inet.h>

#include <pthread.h>

#include <unistd.h>
#include <signal.h>
#include <time.h>

#include <errno.h>

#include "lib/liblfds7.1.1/liblfds711/inc/liblfds711.h"

#define MAX_MESSAGE_CHARS 8192
#define MAX_HEADER_SIZE 1024 // should be enough for now.
/*
* HTTP Structs*
*/

typedef struct {
    char *method;
    char *url;
} Request;

typedef struct {
    int status_code;
    char *data;
    long data_length; // Required for binary files. Must keep the size of the file.
    char *data_type;
} Response;

typedef struct
{
    pthread_mutex_t lock;
    pthread_cond_t start;
    pthread_t thread;
    int busy;
    int socket;
} ThreadConnection;

typedef struct 
{
    int epoll_fd;
    int listen_fd;
    size_t thread_count;
    ThreadConnection *threads;
    struct epoll_event *events;
    struct lfds711_freelist_element *volatile (*elimination_array)[LFDS711_FREELIST_ELIMINATION_ARRAY_ELEMENT_SIZE_IN_FREELIST_ELEMENTS];
    struct lfds711_freelist_element *freelist_elements;
    struct lfds711_freelist_state *free_threads_list;
    struct lfds711_prng_st_state *random_state;
} ListenerState;

enum HttpMethod
{
    METHOD_INVALID,
    METHOD_GET,
    MEHTOD_POST,
};

typedef struct
{
    char message_buffer[MAX_MESSAGE_CHARS];
    enum HttpMethod method;

} HttpRequestEvent;

Request getRequest(char *read_buf, long size);
Response createErrorMsg(int status_code, char *data);
int sendResponse(Response response_data, int connection);
// Dynamic file loading --- Pretty gosh darn fast, all things considered.
char * loadFile(const char * filename, long * file_size);
char * getErrorDescription(int status_code);

// php definitions, for mod_php.c
char * runPHPScript(char * filename, Request request);