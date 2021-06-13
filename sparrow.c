#include "sparrow.h"
#include <errno.h>

#define TIMEOUT_US 1000000
#define EPOLL_FLAGS EPOLLONESHOT | EPOLLIN

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
    struct epoll_event event = { .data.fd = listen_fd, .events = EPOLLIN | EPOLLRDHUP };
    epoll_ctl(listener->epoll_fd, EPOLL_CTL_ADD, listen_fd, &event);
    return 1;
}

int readWithTimeout(int fd, char *recv_buffer, size_t max_chars, int wait_us)
{
    fd_set set;
    struct timeval timeout = { .tv_usec = wait_us };

    FD_ZERO(&set);
    FD_SET(fd, &set);

    int return_value = select(fd + 1, &set, NULL, NULL, &timeout);
    if (return_value <= 0) return return_value;
    read(fd, recv_buffer, max_chars);
    return 1;
}

int customStrCmp(const void *a, const void *b)
{
    return strcmp(*(char **)a, *(char **)b);
}

// find the next occurence of delimiter string in input string
char *multiToken(char *string, char *delimiter, char **internal)
{
    if (!internal) return NULL;
    if (!string) string = *internal;
    if (!string) return NULL;

    *internal = strstr(string, delimiter);
    if (!*internal) return NULL;
    **internal = '\0';
    *internal += strlen(delimiter);
    return string;
}

// Consumer function
int awaitJob(ListenerState *listener, ThreadConnection *thread_connect, HttpRequestEvent *event)
{
    int read_return;
    do {
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
        // minus one is for garanteed null-termination
        read_return = read(thread_connect->socket, event->message_buffer, MAX_MESSAGE_CHARS - 1);
        int read_error = errno;
        
        if (read_return < 0) 
        {
            printf("%d\n", read_error);
            goto err;
        }
        if (!read_return)
        {
            close(thread_connect->socket);
        }
    } while (!read_return);

    // and parse it
    char *internal_ptr = event->message_buffer;
    // this should be safe as long as the buffer remains NULL-terminated, 
    // which it should as long as we don't mess with internal_ptr
    char *token_ptr = multiToken(event->message_buffer, " ", &internal_ptr);
    if (!token_ptr) goto err;
    // possible exploit here if you had, say 1021 'A's and then a space,
    // you could cause the program to compare past the end of the buffer.
    // replacing memcmp with strncmp
    // the buffer is NULL terminated, so strncmp will not read past the end
    // eh actually it was probably fine since token_ptr will always point to the
    // beginning of the buffer at this stage
    if (strncmp(token_ptr, "GET", 4) == 0) event->method = METHOD_GET;
    else if (strncmp(token_ptr, "POST", 5) == 0) event->method = METHOD_POST;
    else goto err;
    event->uri = multiToken(NULL, " ", &internal_ptr);
    multiToken(NULL, "\r\n", &internal_ptr); // Protocol version

    while (token_ptr)
    {
        token_ptr = multiToken(NULL, ": ", &internal_ptr);
        if (!token_ptr) break;
        char **field = bsearch(&token_ptr, request_field_strings, sizeof(request_field_strings) / sizeof(char *), sizeof(char *), customStrCmp);
        token_ptr = multiToken(NULL, "\r\n", &internal_ptr);
        if (field && *field && token_ptr)
        {
            ptrdiff_t field_index = field - request_field_strings;
            if (field_index < sizeof(request_field_strings) / sizeof(char *) && field_index >= 0)
            {
                event->fields[field_index] = token_ptr;
            }
        }
    }
    return 1;

    err:
    event->method = METHOD_INVALID;
    return 0;
}

int returnSocketToListener(ListenerState *listener, int fd)
{
    struct epoll_event event = { .data.fd = fd, .events = EPOLL_FLAGS };
    return epoll_ctl(listener->epoll_fd, EPOLL_CTL_MOD, fd, &event);
}

// Producer function
int listenDispatch(ListenerState *listener, int timeout)
{
    struct epoll_event event_buffer[MAX_EVENTS];
    for (;;)
    {
        int events_count = epoll_wait(listener->epoll_fd, event_buffer, MAX_EVENTS, timeout);
        if (events_count < 0) perror("wait");
        for (int i = 0; i < events_count; i++)
        {
            if (event_buffer[i].data.fd == listener->listen_fd)
            {
                puts("New connection");
                struct sockaddr_in address;
                socklen_t address_length = sizeof(address);
                int new_fd = accept(listener->listen_fd, (struct sockaddr *)&address, &address_length);
                if (new_fd < 0) { fprintf(stderr, "Error accepting socket\n"); continue; }
                struct epoll_event event = { .data.fd = new_fd, .events = EPOLL_FLAGS };
                if (epoll_ctl(listener->epoll_fd, EPOLL_CTL_ADD, new_fd, &event) < 0) perror("ctl");
            }
            else if (event_buffer[i].events & EPOLLIN)
            {
                puts("Handling Event");
                sem_wait(&listener->thread_pool_sem);
                pthread_mutex_lock(&listener->thread_pool_lock);
                ThreadConnection *free_thread = listener->thread_pool[--listener->thread_pool_top];
                pthread_mutex_unlock(&listener->thread_pool_lock);
                pthread_mutex_lock(&free_thread->lock);
                free_thread->busy = 1;
                free_thread->socket = event_buffer[i].data.fd;
                pthread_cond_signal(&free_thread->start);
                pthread_mutex_unlock(&free_thread->lock);
            }
            else
            {
                puts("EPOLLRDHUP");
                epoll_ctl(listener->epoll_fd, EPOLL_CTL_DEL, event_buffer[i].data.fd, NULL);
                close(event_buffer[i].data.fd);
            }
        }
    }
    return 1;
}
