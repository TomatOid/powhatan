#include "sparrow.h"

char * runPHPScript(char * path, Request request_data) {
	printf("Executing php script: %s\n", request_data.url);

	printf("Setting environment variables\n");
	//putenv("AUTH_TYPE="); // Authentication type
	putenv("CONTENT_LENGTH=NULL"); // Content Length
	//putenv("CONTENT_TYPE="); // Content Type
	putenv("GATEWAY_INTERFACE=CGI/1.1"); // Gateway Interface
	putenv("PATH_INFO=script.php"); // Path Info
	//putenv("PATH_TRANSLATED="); // Path Translated
	putenv("QUERY_STRING=\"\""); // Query String
	putenv("REMOTE_ADDR=127.0.0.1");
	putenv("REMOTE_HOST=NULL");
	//putenv("REMOTE_IDENT="); // Remote Identification
	//putenv("REMOTE_USER="); // Remote User
	putenv("REQUEST_METHOD=GET"); // Requested Method
	putenv("SCRIPT_NAME=/script.php"); // Script Name
	putenv("SERVER_NAME=localhost"); // Server Name

	char server_port[20];
	sprintf(server_port, "SERVER_PORT=%d", PORT);
	putenv(server_port); // Server Port

	putenv("SERVER_PROTOCOL=HTTP/1.1"); // Server Protocol
	putenv("SERVER_SOFTWARE=Powhatan"); // Server Software
	putenv("REDIRECT_STATUS=CGI");
	putenv("SCRIPT_FILENAME=script.php");

	printf("Executing script now.\n");
	FILE *fp = popen("php-cgi -fscript.php", "r"); // change into directory and run the script

	if (fp == NULL) {
		printf("Failed to run command");
		return NULL;
	}

	printf("Response:\n");

	while(!feof(fp)) {
		printf("%c", fgetc(fp));
	}

	pclose(fp);


	//return NULL;
	return "<html><body><h1>This would be a php script, but, you know, whoops?</h1></body></html>";
}
