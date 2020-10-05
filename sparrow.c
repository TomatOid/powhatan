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

    listener->epoll_fd = epoll_create1(0);
    epoll_ctl(listener->epoll_fd, EPOLL_CTL_ADD, listen_fd, NULL); // error check maybe??
    return 1;
}

// Consumer function
int awaitJob(ListenerState *listener, ThreadConnection *thread_connect, HttpRequestEvent *event)
{
    // push ourselves onto the free threads pool and wait
    pthread_mutex_lock(&listener->thread_pool_lock);
    listener->thread_pool[listener->thread_pool_top++] = thread_connect;
    sem_post(&listener->thread_pool_sem);
    pthread_mutex_unlock(&listener->thread_pool_lock);
    pthread_mutex_lock(&thread_connect->lock);
    while (!thread_connect->busy) pthread_cond_wait(&thread_connect->start, &thread_connect->lock);
    // the producer is responsible for waiting and poping, as well as setting fd
    pthread_mutex_unlock(&thread_connect->lock);
    // now we read the request from the fd
    memset(event, 0, sizeof(HttpRequestEvent));
    if (read(thread_connect->socket, event->message_buffer, MAX_MESSAGE_CHARS) < 0) return 0;
    // and parse it
    rsize_t buffer_remaining = MAX_MESSAGE_CHARS;
    char *token_ptr = event->message_buffer;
    strtok_s(event->message_buffer, &buffer_remaining, " \t", &token_ptr);
    if (memcmp(event->message_buffer, "GET\0", 4) == 0) event->method = METHOD_GET;
    else if (memcmp(event->message_buffer, "POST\0", 5) == 0) event->method = MEHTOD_POST;
    else event->method = METHOD_INVALID;
    event->uri = strtok_s(NULL, &buffer_remaining, " \t\n", &token_ptr); // TODO: set up constraint handlers
    return 1;
}

// Producer function
int listenDispatch(ListenerState *listener, int timeout)
{
    struct epoll_event event_buffer[MAX_EVENTS];
    int events_count = epoll_wait(listener->epoll_fd, event_buffer, MAX_EVENTS, timeout);
    for (int i = 0; i < events_count; i++)
    {
        if (event_buffer[i].data.fd == listener->listen_fd)
        {
            struct sockaddr_in address;
            socklen_t address_length = sizeof(address);
            int new_fd = accept(listener, (struct sockaddr_t *)&address, &address_length);
            if (new_fd < 0) { fprintf(stderr, "Error accepting socket\n"); continue; }
            epoll_ctl(listener->epoll_fd, EPOLL_CTL_ADD, new_fd, NULL);
        }
        else if (event_buffer[i].events & EPOLLIN && event_buffer[i].events & EPOLLOUT)
        {
            sem_wait(&listener->thread_pool_sem);
            pthread_mutex_lock(&listener->thread_pool_lock);
            ThreadConnection *free_thread = &listener->thread_pool[--listener->thread_pool_top];
            pthread_mutex_unlock(&listener->thread_pool_lock);
            pthread_mutex_lock(&free_thread->lock);
            free_thread->busy = 1;
            free_thread->socket = event_buffer[i].data.fd;
            pthread_cond_signal(&free_thread->start);
            pthread_mutex_unlock(&free_thread->lock);
        }
    }
}