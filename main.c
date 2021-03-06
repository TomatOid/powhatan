#define USE_BLOCK_ALLOCATOR_FOR_EPOLL
#include "sparrow.h"

#define FILES_COUNT sizeof(file_names) / sizeof(char *)

/*
* Threading Functions and Structures 
*/

ThreadConnection thread_connections[MAX_THREADS_COUNT];
ThreadConnection *thread_pool[MAX_THREADS_COUNT];
size_t thread_pool_top = 0;
pthread_mutex_t pool_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t pool_push_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t pool_push_cond = PTHREAD_COND_INITIALIZER;
int listener;

const char *file_names[] = { "index.html" };
char *file_buffers[FILES_COUNT];
int file_buffer_sizes[FILES_COUNT];
int default_file = 0;

// this assumes it is already locked
int waitFor(pthread_cond_t *condition, pthread_mutex_t *lock, int wait_ms)
{
    struct timespec time_spec;
    clock_gettime(CLOCK_REALTIME, &time_spec);
    time_spec.tv_nsec += 1000000L * (long)wait_ms;
    return pthread_cond_timedwait(condition, lock, &time_spec);
}

void popSockToFreeThread(int socket)
{
    // wait untill there are free threads
    pthread_mutex_lock(&pool_push_lock);
    while (!thread_pool_top) waitFor(&pool_push_cond, &pool_push_lock, 50); // there is a possible edge case where this times out
    pthread_mutex_unlock(&pool_push_lock); 
    
    // now pop the thread from the pool and signal it
    pthread_mutex_lock(&pool_lock);
    thread_pool[--thread_pool_top]->socket = socket;
    thread_pool[thread_pool_top]->busy = 1;
    pthread_cond_signal(&thread_pool[thread_pool_top]->start);
    pthread_mutex_unlock(&pool_lock);
}

void *threadFunction(void *my_thread_connect)
{
    char message_buffer[MAX_MESSAGE_CHARS];
    char *request_lines[3];
    
    Request request_data;
    Response response_data;
    char * file_buffer;
    long file_size;

    ThreadConnection *thread_connect = my_thread_connect;
    for (;;)
    {
        // wait until we recieve a signal
        thread_connect->busy = 0;
        pthread_mutex_lock(&thread_connect->lock);
        pthread_mutex_lock(&pool_lock);
        thread_pool[thread_pool_top++] = thread_connect;
        pthread_cond_signal(&pool_push_cond); // signal that we have pushed just in case the main thread is waiting on free threads
        pthread_mutex_unlock(&pool_lock);
        while (!thread_connect->busy) pthread_cond_wait(&thread_connect->start, &thread_connect->lock);
        pthread_mutex_unlock(&thread_connect->lock);
        // reset the buffer and read the data from the client  
        memset(&message_buffer, 0, MAX_MESSAGE_CHARS);
        if (read(thread_connect->socket, message_buffer, MAX_MESSAGE_CHARS - 1) <= 0) // length - 1 to guarentee null termination
            fprintf(stderr, "There was an error reading from the socket\n");
        else
        {
            printf("Getting request\n");
            request_data = getRequest(message_buffer, MAX_MESSAGE_CHARS);
            printf("Method: %s| Url: %s\n", request_data.method, request_data.url);

            // check the method
            // yes i know we aren't checking the protocol, but I don't believe it will be totally necessary yet.
            if(strncmp(request_data.method, "GET", 4) == 0) {
                // url redirection

                // content negotiation

                // load the file or run it?? e.g. php
                if (strncmp(request_data.url + strlen(request_data.url) - 4, ".php", 5) == 0) {
                    printf("Executing php file\n");
                    file_buffer = runPHPScript(request_data.url, request_data);
                    file_size = strlen(file_buffer);
                } else {
                    file_buffer = loadFile(request_data.url, &file_size);
                }

                if(file_buffer == NULL) {
                    // Couldn't find file
                    response_data = createErrorMsg(404, "404 Not Found");
                } else {
                    response_data.data = file_buffer;
                    response_data.data_length = file_size;

                    response_data.data_type = "text/html"; // will update later.

                    response_data.status_code = 200; // OK
                }
            } else if (strncmp(request_data.method, "HEAD", 5) == 0) {
                // head method only send headers.
                // not implemented, the codebase needs a refactor for this method to work effeciently.
                // something like checkFile to see if the content exists, and if so send the type
            } else {
                // unsupported method. 505
                response_data = createErrorMsg(501, "501 Not Implemented");
            } 

            if(sendResponse(response_data, thread_connect->socket) < 0) {
                printf("Error sending response\n");
            }

            close(thread_connect->socket);
            
            /*
            request_lines[0] = strtok(message_buffer, " \t\n"); 
            if (strncmp(request_lines[0], "GET\0", 4) == 0)
            {
                request_lines[1] = strtok(NULL, " \t");
                request_lines[2] = strtok(NULL, " \t\n");
                // Now check if uncompatible protocol
                if (!request_lines[2] || strncmp(request_lines[2], "HTTP/1.0", 8) != 0 && strncmp(request_lines[2], "HTTP/1.1", 8) != 0)
                {
                    write(thread_connect->socket, "HTTP/1.0 505 HTTP Version not supported\n", 40);
                }
                else
                {
                    int file = -1;
                    if (strcmp(request_lines[1], "/\0") == 0)
                    {
                        file = default_file;
                    }
                    else if (request_lines[1][0])
                    {
                        for (int i = 0; i < FILES_COUNT; i++)
                        {
                            if (strcmp(request_lines[1] + 1, file_names[i]) == 0)
                            {
                                file = i;
                                break;
                            }
                        }
                    }
                    if (file < 0)
                    {
                        write(thread_connect->socket, "HTTP/1.0 404 Not Found\n\n", 24);
                    }
                    else
                    {
                        write(thread_connect->socket, "HTTP/1.0 200 OK\n\n", 17);
                        write(thread_connect->socket, file_buffers[file], file_buffer_sizes[file]);
                    }
                    close(thread_connect->socket);
                }
            }
            */
        }
    }
}

/**
* HTTP Methods
*/

// Does nothing right now.
Request getRequest(char * read_buf, long size) {
    Request request_data;
    
    request_data.method = strtok(read_buf, " \t");
    request_data.url = strtok(NULL, " \t\n");

    return request_data;
}

Response createErrorMsg(int status_code, char * data) {
    Response response_data;
    response_data.status_code = status_code;
    response_data.data = data;
    response_data.data_length = strlen(data); // This line is why we can only use this method for error messages. If used with binary files, it will probably cut off before the end because of null character.
    response_data.data_type = "text/plain"; // could be updated later
    return response_data;
}

int sendResponse(Response response_data, int connection) {

    // really don't like this, I would rather use malloc, but I hope you're happy
    char headers[MAX_HEADER_SIZE];
    sprintf(headers, "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %ld\r\n\r\n", response_data.status_code, getErrorDescription(response_data.status_code), response_data.data_type, response_data.data_length);

    // let's write the headers first. The client should wait til they recieve more data 
    int bytes = write(connection, headers, strlen(headers));

    if(bytes < 0) {
        return -1;
    }

    bytes += write(connection, response_data.data, response_data.data_length); // yes, I know write could also return -1 here, but I just wanted to get it working

    return bytes; 
}

// yes, i know, its hacky, but we need to do something, maybe defines?
char * getErrorDescription(int status_code) {
    switch (status_code) {
        case 200:
            return "OK";
        case 404:
            return "Not found";
        case 501:
            return "Not Implemented";
        default:
            return "ERROR";
    }
}

char * loadFile(const char * filename, long * file_size) {

    char * buffer;

    if(*(filename + strlen(filename) - 1) == '/') {
        printf("Can't retreive directories currently!");
        return NULL;
    }

    // janky code to remove beginning /
    if(access(filename + 1, F_OK | R_OK) != -1) { 
        FILE* response_file = fopen(filename + 1,"rb");
        //printf("File opened: %d\n", response_file == NULL);

        //printf("Getting size of file\n");
        fseek(response_file, 0, SEEK_END);
        *file_size = ftell(response_file);
        fseek(response_file, 0, SEEK_SET);  /* same as rewind(f); */

        printf("Size: %ld\n", (*file_size));

        //printf("Reading file into buffer\n");
        buffer = mmap(NULL, (*file_size), PROT_READ, MAP_PRIVATE, fileno(response_file), 0);
        //fread(file_buffer, 1, (*fsize), response_file);

        fclose(response_file);

        //printf("Setting end of file buffer.\n");
        //file_buffer[(*fsize)] = 0;

    } else {
        buffer = NULL;   
        printf("Could not find file.\n");
        perror("Failed");
        errno = 0;
    }

    return buffer;
}

void exitFunction()
{
    close(listener);
}

void loadFiles()
{
    for (int i = 0; i < FILES_COUNT; i++)
    {
        FILE *stream = fopen(file_names[i], "r");
        
        if (!stream)
        {
            fprintf(stderr, "Error loading file: %s, aborting!\n", file_names[i]);
            exit(EXIT_FAILURE);
        }
        
        // get the length of the file so we know how much to allocate
        fseek(stream, 0, SEEK_END);
        file_buffer_sizes[i] = ftell(stream);
        file_buffers[i] = malloc(file_buffer_sizes[i]);
        rewind(stream);
        fread(file_buffers[i], 1, file_buffer_sizes[i], stream);
        fclose(stream);
    }
}

int main()
{
    struct sockaddr_in client_address;
    struct sockaddr_in server_address = { .sin_family = AF_INET, .sin_addr.s_addr = INADDR_ANY, .sin_port = htons(PORT) };

    listener = socket(AF_INET, SOCK_STREAM, 0);
    if (bind(listener, (struct sockaddr *)&server_address, sizeof(server_address))) { puts("Error binding."); exit(EXIT_FAILURE); }

    for (int i = 0; i < MAX_THREADS_COUNT; i++)
    {
        thread_connections[i] = (ThreadConnection) { .lock = PTHREAD_MUTEX_INITIALIZER, .start = PTHREAD_COND_INITIALIZER };
        pthread_create(&thread_connections[i].thread, NULL, threadFunction, &thread_connections[i]);
    }

    loadFiles();

    signal(SIGTERM, exitFunction);
    listen(listener, 10000);
    
    for (;;)
    {
        socklen_t address_length = sizeof(struct sockaddr_in);
        popSockToFreeThread(accept(listener, (struct sockaddr *)&client_address, &address_length));
    }
}
