#pragma once
// Standard includes
#define USE_EXT1 1
#define __STDC_WANT_LIB_EXT1__ 1
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>

// System includes
#include <sys/socket.h>
#include <sys/epoll.h>
#include <sys/mman.h>

#include <arpa/inet.h>

#include <pthread.h>

#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <semaphore.h>

#include <errno.h>

#ifndef MAX_CLIENTS
#define MAX_CLIENTS 10000
#endif

#define MAX_MESSAGE_CHARS 4096
#define MAX_HEADER_SIZE 1024 // should be enough for now.
#define MAX_THREADS_COUNT 16
#define MAX_EVENTS MAX_THREADS_COUNT + 1
#define PORT 8888
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
    int listen_fd;
    int epoll_fd;
    ThreadConnection threads[MAX_THREADS_COUNT];
    ThreadConnection *thread_pool[MAX_THREADS_COUNT];
    pthread_mutex_t thread_pool_lock;
    // thread_pool_sem will be zero when all threads are working,
    // producer calls wait before sending work which will block when there are no free threads,
    // consumer calls post when done / starts waiting
    sem_t thread_pool_sem; 
    size_t thread_pool_top; // this might be reduntant with the sem, but I don't feel like calling getvalue
} ListenerState;

enum HttpMethod
{
    METHOD_INVALID,
    METHOD_GET,
    MEHTOD_POST,
};

static char *request_field_strings[] = { "A-IM", "Accept", "Accept-Charset", "Accept-Datetime", "Accept-Encoding", "Access-Control-Request-Headers", "Access-Control-Request-Method", "Authorization", "Cache-Control", "Connection", "Content-Encoding", "Content-Length", "Content-MD5", "Content-Type", "Cookie", "Date", "Expect", "Forewarded", "From", "Host", };

enum { REQUEST_A_IM, REQUEST_ACCEPT, REQUEST_ACCEPT_CHARSET, REQUEST_ACCEPT_ENCODING, REQUEST_ACCESS_CONTROL_REQUEST_HEADERS, REQUEST_ACCESS_CONTROL_REQUEST_METHOD, REQUEST_AUTHORIZATION, REQUEST_CACHE_CONTROL, REQUEST_CONNECTION, REQUEST_CONTENT_ENCODING, REQUEST_CONTENT_LENGTH, REQUEST_CONTENT_MD5, REQUEST_CONTENT_TYPE, REQUEST_COOKIE, REQUEST_DATE, REQUEST_EXPECT, REQUEST_FOREWARDED, REQUEST_FROM, REQUEST_HOST };

typedef struct
{
    char message_buffer[MAX_MESSAGE_CHARS];
    enum HttpMethod method;
    char *uri;
    char *fields[sizeof(request_field_strings) / sizeof(char *)];
} HttpRequestEvent;

int initListener(ListenerState *listener, int listen_fd, void *(*thread_function)(void *));
int awaitJob(ListenerState *listener, ThreadConnection *thread_connect, HttpRequestEvent *event);
int listenDispatch(ListenerState *listener, int timeout);
int returnSocketToListener(ListenerState *listener, int fd);

Request getRequest(char *read_buf, long size);
Response createErrorMsg(int status_code, char *data);
int sendResponse(Response response_data, int connection);
// Dynamic file loading --- Pretty gosh darn fast, all things considered.
char * loadFile(const char * filename, long * file_size);
char * getErrorDescription(int status_code);

// php definitions, for mod_php.c
char * runPHPScript(char * filename, Request request);
