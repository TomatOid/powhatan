#include "sparrow.h"
#define PRINT_DEBUG

ListenerState listener;
const char ok_response_header[] = "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nConnection: keep-alive\r\nContent-Length: %zu\r\n\r\n";

FILE *getFilePtrFromURI(char *uri, size_t *len)
{
    // idk how fopen and friends deal with unicode, so I will only support
    // 7-bit ASCII characters for now. UTF support needs to come later

    // first, let's check the safety of this path.
    // there should be no '/../' in there.
    
    int unbroken_dots_since_start = 0;
    char *c = uri;
    do
    {
        switch (*c)
        {
            case '/':
                if (unbroken_dots_since_start == 2)
                    return NULL;
                unbroken_dots_since_start = 0;
                break;
            
            case '.':
                unbroken_dots_since_start += (unbroken_dots_since_start >= 0);
                break;

            default:
                if (*c >= 0xEF)
                    return NULL;
                unbroken_dots_since_start = -1;
                break;
        }
    } while (*c++);

    if (unbroken_dots_since_start == 2)
        return NULL;

    uri += (*uri == '/');

    if (access(uri, F_OK | R_OK) == -1)
        return NULL;

    FILE *response_file = fopen(uri, "rb");

    fseek(response_file, 0, SEEK_END);
    *len = ftell(response_file);
    rewind(response_file);
    
    return response_file;
}

void *threadFunction(void *thread_arg)
{
    puts("started thread.");
    HttpRequestEvent request;
    ThreadConnection *thread_connect = thread_arg;
    for (;;)
    {
        awaitJob(&listener, thread_connect, &request);
        setsockopt(thread_connect->socket, IPPROTO_TCP, TCP_CORK, &(int){1}, sizeof(int));
        switch (request.method)
        {
        case METHOD_GET:
        {
            size_t uri_file_len = 0;
            FILE *uri_file = getFilePtrFromURI(request.uri, &uri_file_len); 
            if (!uri_file)
            {
                write(thread_connect->socket, "HTTP/1.1 404 Not Found\r\n\r\n", 27);
            }
            else
            {
                dprintf(thread_connect->socket, ok_response_header, "text/html", uri_file_len);
                sendfile(thread_connect->socket, fileno(uri_file), NULL, uri_file_len);
                fclose(uri_file);
            }
            if (request.fields[REQUEST_CONNECTION])
            {
                if (strcmp("keep-alive", request.fields[REQUEST_CONNECTION]) == 0)
                {
                    returnSocketToListener(&listener, thread_connect->socket);
                    continue;
                }
            }
            break;
        }
        
        default:
            puts("invalid method");
            break;
        }
        setsockopt(thread_connect->socket, IPPROTO_TCP, TCP_CORK, &(int){0}, sizeof(int));
    }
}

void exitFunction()
{
    puts("\rExiting...");
    for (int i = 0; i < MAX_THREADS_COUNT; i++)
    {
        pthread_cancel(listener.threads[i].thread);
    }
}

int main()
{
    struct sockaddr_in server_address = { .sin_family = AF_INET, .sin_addr.s_addr = INADDR_ANY, .sin_port = htons(PORT) };

    struct sigaction sigint_action = { .sa_handler = exit };
    sigemptyset(&sigint_action.sa_mask);
    sigaddset(&sigint_action.sa_mask, SIGINT);
    sigaction(SIGINT, &sigint_action, NULL);
    atexit(exitFunction);
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (bind(listen_fd, (struct sockaddr *)&server_address, sizeof(server_address))) 
    {
        puts("Error binding."); 
        exit(EXIT_FAILURE); 
    }
    listen(listen_fd, 10000);
    initListener(&listener, listen_fd, threadFunction);

    listenDispatch(&listener, 1000);
}
