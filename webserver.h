/*
 * @author: Richard Poulson
 * @date: 6 Oct 2018
 *
 * WebServer object:
 * - receives HTTP requests, parses and verifies the data.
 *
 * @references:
 *   https://www.geeksforgeeks.org/socket-programming-cc/
 *   https://stackoverflow.com/questions/1151582/pthread-function-from-a-class
 */

#define BUFFER_SIZE 8192 // Size of buffers, in bytes (8KB)
#define NUM_CONNECTION_THREADS 8 // maximum number of client connections

#include <stddef.h> // NULL, nullptr_t
#include <stdio.h> // FILE, size_t, fopen, fclose, fread, fwrite,
#include <iostream> // cout
#include <fstream> // ifstream
#include <signal.h> // signal(int void (*func)(int))
#include <unistd.h>
#include <stdlib.h> //  exit, EXIT_FAILURE, EXIT_SUCCESS
#include <string.h> //  strlen, strcpy, strcat, strcmp
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h> //  socklen_t,
#include <netinet/in.h> //  sockaddr_in, INADDR_ANY,
#include <arpa/inet.h> //  htonl, htons, inet_ntoa,
#include <dirent.h> //  "Traverse directory", opendir, readdir,
#include <errno.h> //  "C Errors", errno
#include <regex> //  Regular expressions
#include <pthread.h> // process threads,
#include <time.h> //  time_t, tm,
#include <stack> //  stack of process threads

using namespace std;

void SignalHandler(int signal); // signal handler for WebServer class
void * AcceptConnection(void * sharedResources); // PThread function, services clients

struct RequestMessage
{
  string method;
  string uri;
  string httpVersion;
  string connection;
  RequestMessage(string requestMethod = "GET", string version = "HTTP/1.1")
  {
    this->method = requestMethod;
    this->httpVersion = version;
  }
};
struct ResponseMessage
{
  string httpVersion;
  unsigned char statusCode;
  string responsePhrase;
  string date;
  string length;
  string connection;
  string content;
  FILE * message;
  ResponseMessage(string version = "HTTP/1.1", unsigned char code = 200,
          string phrase = "Document Follows")
  {
    this->httpVersion = version;
    this->statusCode = code;
    this->responsePhrase = phrase;
  }
};

struct PThreadResources {
  // threads need to wait, even if only reading
  pthread_mutex_t sock_mx, file_mx, dir_mx, regex_mx;
  int sock; // file descriptor for server socket
  FILE * file;
  DIR * directory;
  struct dirent *dir;
  regex httpHeaderRegex; // used to verify if request is in valid format
};

class WebServer
{
public:
  WebServer(unsigned short portNumber); // receives port number as parameter
  ~WebServer();
private:
  struct PThreadResources * sharedResources;
  // arrays to hold all pthread_t data types
  pthread_t httpConnections[NUM_CONNECTION_THREADS];
  socklen_t clientLen;
  unsigned short portNum; // port to listen on
  struct sockaddr_in serverAddr; // server's addr
  int optval; // flag value for setsockopt
  void Initialize();
  bool CreateSocket();
  bool BindSocket();
  void StartHTTPService();
  char* DateTimeRFC(); // return date and time in RFC format
};
