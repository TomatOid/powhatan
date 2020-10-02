#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include "lib/liblfds7.1.1/liblfds711/inc/liblfds711.h"
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

typedef struct {
    int epoll_fd;
    int listen_fd;
    int thread_count;
    ThreadConnection *threads;
    struct lfds711_freelist_element *volatile (*elimination_array)[LFDS_FREELIST_ELIMINATION_ARRAY_ELEMENT_SIZE_IN_FREELIST_ELEMENTS];
    struct lfds711_freelist_element *freelist_elements;
    struct lfds711_freelist_state *free_threads_list;
    struct lfds711_prng_st_state *random_state;
} ListenerState;

Request getRequest(char *read_buf, long size);
Response createErrorMsg(int status_code, char *data);
int sendResponse(Response response_data, int request);


