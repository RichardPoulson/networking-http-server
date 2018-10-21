/*
 * @author Richard Poulson
 * @date 6 Oct 2018
 *
 * WebServer object:
 * - receives HTTP requests, parses and verifies the data.
 *
 * @references:
 *   https://www.geeksforgeeks.org/socket-programming-cc/
 *   https://stackoverflow.com/questions/1151582/pthread-function-from-a-class
 *   https://stackoverflow.com/questions/4250013/is-destructor-called-if-sigint-or-sigstp-issued
 *   http://www.cplusplus.com/reference/fstream/basic_ifstream/rdbuf/
 */

// if buffer is larger than ~ 200KB, may have to put buffers in heap (dynamic)
#define BUFFER_SIZE 1048576 // Size of buffers, in bytes (1MB)
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

static volatile bool proceed = true;

void SignalHandler(int signal); // signal handler for WebServer class
void DateTimeRFC(char * buf); // append date and time (RFC 822) to buffer
size_t ReceiveMessage(int, char *, struct PThreadResources *);

void InitReqRecStructs(char*, struct RequestMessage*, struct ResponseMessage*);
void * AcceptConnection(void * sharedResources); // PThread function, services clients

// data structs for HTML header sections
struct RequestMessage
{
  char method[32]; // GET,HEAD,POST
  char uri[256]; // /,
  char httpVersion[32]; // e.g. HTTP/1.1
  char connection[32]; // keep-alive, close
};
struct ResponseMessage
{
  char httpVersion[32]; // e.g. HTTP/1.1
  unsigned char statusCode; // e.g. 200
  char connection[32]; // keep-alive, close
  char content[32]; // e.g. "text/css"
};
// one struct's members are shared among PThreads
struct PThreadResources {
  pthread_mutex_t sock_mx, file_mx, dir_mx, regex_mx, cout_mx;
  int sock; // server socket
  ifstream ifs;
  //FILE * file;
  //DIR * directory;
  //struct dirent *dir;
  regex httpHeaderRegex; // checks HTTP method, URI, and HTTP version
};
//== WebServer, object contains parent sockets, pthreads create client socket
class WebServer
{
public:
  WebServer(unsigned short portNumber);
  ~WebServer();
private:
  struct PThreadResources * sharedResources;
  pthread_t httpConnections[NUM_CONNECTION_THREADS];
  unsigned short portNum; // port to listen on
  struct sockaddr_in serverAddr; // server's addr
  int optval; // flag value for setsockopt

  void Initialize();
  bool CreateSocket();
  bool BindSocket();
  void StartHTTPService();
  void StopHTTPService();
};
