#include "sparrow.h"

#define ELIMINATION_ARRAY_SIZE 4

// TODO error check malloc
ListenerState createListener(int listen_fd, void *(*thread_function) (void *), size_t threads_count)
{
    ListenerState result;

    result.elimination_array = aligned_alloc(LFDS711_PAL_ATOMIC_ISOLATION_IN_BYTES, ELIMINATION_ARRAY_SIZE * sizeof(struct lfds711_freelist_element) * LFDS711_FREELIST_ELIMINATION_ARRAY_ELEMENT_SIZE_IN_FREELIST_ELEMENTS);
    lfds711_freelist_init_valid_on_current_logical_core(&result.free_threads_list, result.elimination_array, ELIMINATION_ARRAY_SIZE, NULL);

    lfds711_prng_st_init(&result.random_state, LFDS711_PRNG_SEED);

    lfds711_freelist_query(result.free_threads_list, LFDS711_FREELIST_QUERY_GET_ELIMINATION_ARRAY_EXTRA_ELEMENTS_IN_FREELIST_ELEMENTS, NULL, &result.thread_count);

    result.thread_count += threads_count;
    threads_count = result.thread_count;
    
    result.threads = malloc(threads_count * sizeof(ThreadConnection));
    result.events = malloc(threads_count * threads_count);
    result.listen_fd = listen_fd;
    for (size_t i = 0; i < threads_count; i++)
    {
        result.threads[i] = (ThreadConnection) { .lock = PTHREAD_MUTEX_INITIALIZER, .start = PTHREAD_COND_INITIALIZER };
        pthread_create(&result.threads[i].thread, NULL, thread_function, (void *)&result.threads[i]);
    }

    result.epoll_fd = epoll_create1(0);
    epoll_ctl(result.epoll_fd, EPOLL_CTL_ADD, listen_fd, NULL);

    return result;
}

void listenFor(ListenerState listener, int wait_ms)
{

}