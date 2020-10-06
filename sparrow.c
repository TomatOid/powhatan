#include "sparrow.h"

int initListener(ListenerState *listener, int listen_fd, void *(*thread_function)(void *))
{
    listener->listen_fd = listen_fd;
    listener->thread_pool_lock = (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;
    if (sem_init(&listener->thread_pool_sem, 0, 0) < 0) return 0;

    for (size_t i = 0; i < MAX_THREADS_COUNT; i++)
    {
        listener->threads[i] = (ThreadConnection) { .start = PTHREAD_COND_INITIALIZER, .lock = PTHREAD_MUTEX_INITIALIZER };
        pthread_create(&listener->threads[i].thread, NULL, thread_function, &listener->threads[i]);
    }

    set_str_constraint_handler_s(NULL);
    return 1;
}

// Consumer function
int awaitJob(ListenerState *listener, ThreadConnection *thread_connect, HttpRequestEvent *event)
{
    // push ourselves onto the free threads pool and wait
    thread_connect->busy = 0;
    pthread_mutex_lock(&listener->thread_pool_lock);
    listener->thread_pool[listener->thread_pool_top++] = thread_connect;
    pthread_mutex_unlock(&listener->thread_pool_lock);
    sem_post(&listener->thread_pool_sem);
    pthread_mutex_lock(&thread_connect->lock);
    while (!thread_connect->busy) pthread_cond_wait(&thread_connect->start, &thread_connect->lock);
    // the producer is responsible for waiting and poping, as well as setting fd
    pthread_mutex_unlock(&thread_connect->lock);
    // now we read the request from the fd
    memset(event, 0, sizeof(HttpRequestEvent));
    if (read(thread_connect->socket, event->message_buffer, MAX_MESSAGE_CHARS - 1) < 0) return 0;
    // and parse it
    rsize_t buffer_remaining = MAX_MESSAGE_CHARS;
    char *internal_ptr = event->message_buffer;
    char *token_ptr = strtok_s(event->message_buffer, &buffer_remaining, " \0", &internal_ptr);
    if (!token_ptr) goto err;
    if (memcmp(token_ptr, "GET\0", 4) == 0) event->method = METHOD_GET;
    else if (memcmp(token_ptr, "POST\0", 5) == 0) event->method = MEHTOD_POST;
    else goto err;
    event->uri = strtok_s(NULL, &buffer_remaining, " \t\n", &internal_ptr); // TODO: set up constraint handlers
    return 1;
    err:
    event->method = METHOD_INVALID;
    return 0;
}

// Producer function
int listenDispatch(ListenerState *listener, int timeout)
{
    struct sockaddr_in address;
    socklen_t address_length = sizeof(address);
    int new_fd = accept(listener->listen_fd, (struct sockaddr *)&address, &address_length);
    if (new_fd < 0) { fprintf(stderr, "Error accepting socket\n"); return 0; }
    struct epoll_event event = { .data.fd = new_fd, .events = EPOLLIN | EPOLLOUT };

    sem_wait(&listener->thread_pool_sem);
    pthread_mutex_lock(&listener->thread_pool_lock);
    ThreadConnection *free_thread = listener->thread_pool[--listener->thread_pool_top];
    pthread_mutex_unlock(&listener->thread_pool_lock);
    pthread_mutex_lock(&free_thread->lock);
    free_thread->busy = 1;
    free_thread->socket = new_fd;
    pthread_cond_signal(&free_thread->start);
    pthread_mutex_unlock(&free_thread->lock);
    return 1;
}