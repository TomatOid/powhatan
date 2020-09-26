#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <unistd.h>
#include <semaphore.h>

#define MAX_THREADS_COUNT 16
#define MAX_MESSAGE_CHARS 65536

typedef struct
{
    pthread_mutex_t lock;
    pthread_cond_t start;
    pthread_t thread;
    int busy;
    int socket;
} ThreadConnection;

ThreadConnection thread_connections[MAX_THREADS_COUNT];
ThreadConnection *thread_pool[MAX_THREADS_COUNT];
size_t thread_pool_top = 0;
sem_t pool_sem;
pthread_mutex_t pool_push_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t pool_push_cond = PTHREAD_COND_INITIALIZER;

void popSockToFreeThread(int socket)
{
    puts("got a connection");
    // wait untill there are free threads
    pthread_mutex_lock(&pool_push_lock);
    while (!thread_pool_top) pthread_cond_wait(&pool_push_cond, &pool_push_lock); // there is a possible edge case where this times out
    pthread_mutex_unlock(&pool_push_lock); 
    
    sem_wait(&pool_sem);
    thread_pool[--thread_pool_top]->socket = socket;
    thread_pool[thread_pool_top]->busy = 1;
    pthread_cond_signal(&thread_pool[thread_pool_top]->start);
    sem_post(&pool_sem);
}

void *handleConnection(void *my_thread_connect)
{
    char message_buffer[MAX_MESSAGE_CHARS] = { 0 };
    //char path[MAX_MESSAGE_CHARS] = { 0 };
    char *request_lines[3] = { 0 };
    ThreadConnection *thread_connect = my_thread_connect;
    for (;;)
    {
        // wait until we recieve a signal
        thread_connect->busy = 0;
        pthread_mutex_lock(&thread_connect->lock);
        sem_wait(&pool_sem);
        thread_pool[thread_pool_top++] = thread_connect;
        pthread_cond_signal(&pool_push_cond); // signal that we have pushed just in case the main thread is waiting on free threads
        sem_post(&pool_sem);
        while (!thread_connect->busy) pthread_cond_wait(&thread_connect->start, &thread_connect->lock);
        pthread_mutex_unlock(&thread_connect->lock);
        puts("new request");
        memset(&message_buffer, 0, MAX_MESSAGE_CHARS);
        if (read(thread_connect->socket, message_buffer, MAX_MESSAGE_CHARS - 1) <= 0) // length - 1 to guarentee null termination
            fprintf(stderr, "There was an error reading from the socket\n");
        else
        {
            request_lines[0] = strtok(message_buffer, " \t\n"); 
            if (strncmp(request_lines[0], "GET\0", 4) == 0)
            {
                request_lines[1] = strtok(NULL, " \t");
                request_lines[2] = strtok(NULL, " \t\n");
                // Now check if uncompatible protocol
                if (!request_lines[2] || strncmp(request_lines[2], "HTTP/1.0", 8) != 0 && strncmp(request_lines[2], "HTTP/1.1", 8) != 0)
                {
                    write(thread_connect->socket, "HTTP/1.0 400 Bad Request\n", 25);
                }
                else
                {
                    write(thread_connect->socket, "HTTP/1.0 200 OK\n\n", 17);
                    write(thread_connect->socket, "Hello World\n", 12);
                    fsync(thread_connect->socket);
                }
            }
        }
    }
}

int main()
{
    struct sockaddr_in client_address;
    struct sockaddr_in server_address = { .sin_family = AF_INET, .sin_addr.s_addr = INADDR_ANY, .sin_port = htons(8888) };

    int listener = socket(AF_INET, SOCK_STREAM, 0);
    if (bind(listener, (struct sockaddr *)&server_address, sizeof(server_address))) { puts("Error binding."); exit(EXIT_FAILURE); }

    sem_init(&pool_sem, 0, 1);
    for (int i = 0; i < MAX_THREADS_COUNT; i++)
    {
        thread_connections[i] = (ThreadConnection) { .lock = PTHREAD_MUTEX_INITIALIZER, .start = PTHREAD_COND_INITIALIZER };
        pthread_create(&thread_connections[i].thread, NULL, handleConnection, &thread_connections[i]);
    }

    listen(listener, 10000);
    
    for (;;)
    {
        socklen_t address_length = sizeof(struct sockaddr_in);
        popSockToFreeThread(accept(listener, (struct sockaddr *)&client_address, &address_length));
    }
}
