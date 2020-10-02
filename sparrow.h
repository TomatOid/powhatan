/*
* HTTP Structs*
*/

typedef struct {
    char * method;
    char * url;
} Request;

typedef struct {
    int status_code;
    char * data;
    long data_length; // Required for binary files. Must keep the size of the file.
    char * data_type;
} Response;

Request getRequest(char * read_buf, long size);
Response createErrorMsg(int status_code, char * data);
int sendResponse(Response response_data, int request);

/**
* HTTP Methods
*/

// Does nothing right now.
Request getRequest(char * read_buf, long size) {
	Request request_data;
	
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

int sendResponse(Response response_data, int request) {
	return -1; // Error not implemented
}


