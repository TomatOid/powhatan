#include <stdio.h>
#include <sys/socket.h>
#include <pthreads.h>

#define MAX_THREADS_COUNT 16

typedef struct
{
    pthread_mutex_t lock;
    pthread_cond_t start;
    int busy;
    int socket;
} ThreadConnection;

ThreadConnection thread_connections[MAX_THREADS_COUNT];
ThreadConnection *thread_pool[MAX_THREADS_COUNT];
size_t thread_pool_top = 0;
sem_t pool_sem;

int popSockToFreeThread(int socket)
{
    sem_wait(&pool_sem);
    if (!thread_pool_top) { sem_post(&pool_sem); return 0; } // fail if there are no free threads
    thread_pool[--thread_pool_top]->socket = socket;
    thread_pool[thread_pool_top]->busy = 1;
    pthread_condition_broadcast(&thread_pool[thread_pool_top]->start, &thread_pool[thread_pool_top]->lock);
    sem_post(&pool_sem);
    return 1;
}
void *handleConnection(void *my_thread_connect)
{
    ThreadConnection *thread_connect = my_thread_connect;
    for (;;)
    {
        // wait until we recieve a signal
        thread_connect->busy = 0;
        pthread_mutex_lock(&thread_connect->lock);
        sem_wait(&pool_sem);
        thread_pool[thread_pool_top++] = thread_connect;
        sem_post(&pool_sem);
        while (!thread_connect->busy) pthread_cond_wait(&thread_connect->start, &thread_connect->lock);
        
