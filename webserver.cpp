#include "webserver.h"

//== WEBSERVER CLASS DEFINITIONS
// CONSTRUCTOR
WebServer::WebServer(unsigned short portNumber){
  Initialize();
  this->portNum = portNumber;
  if (CreateSocket() != true) { exit(EXIT_FAILURE); }
  if (BindSocket() != true) { exit(EXIT_FAILURE); }
  StartHTTPService(); // forever loop, program has to receive SIGINT to exit
}
// StartHTTPService
void WebServer::StartHTTPService()
{
  std::cout << "Starting HTTP Service..." << std::endl;
  while(proceed == true) {
    pthread_mutex_lock(&sharedResources->sock_mx); //== MUTEX
    listen(sharedResources->sock , 3); //  Listen for connections
    pthread_mutex_unlock(&sharedResources->sock_mx); //== UNLOCK
    // how to organize a thread pool, stack?
    pthread_create(&httpConnections[0], NULL, AcceptConnection, sharedResources);
    pthread_join(httpConnections[0],NULL); // thread returns to program
  }
}
// CreateSocket
bool WebServer::CreateSocket()
{
  //  0= pick any protocol that socket type supports, can also use IPPROTO_TCP
  sharedResources->sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sharedResources->sock < 0) {
    perror("ERROR opening socket");
    return false;
  }
  optval = 1;
  //  Allows bind to reuse socket address (if supported)
  setsockopt(sharedResources->sock, SOL_SOCKET, SO_REUSEADDR,
	     (const void *)&optval, sizeof(int));
  //  Define the server's Internet address
  bzero((char *) &serverAddr, sizeof(serverAddr));
  serverAddr.sin_family = AF_INET;
  serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);
  serverAddr.sin_port = htons(portNum);
  return true;
}
// BindSocket, bind parent socket to port, returns false if bind unsuccessful
bool WebServer::BindSocket()
{
  // bind: associate the parent socket with a port
  if (bind(sharedResources->sock, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0) {
    perror("ERROR on binding");
    return false;
  }
  return true;
}
// Initialize
void WebServer::Initialize() {
  signal(SIGINT, SignalHandler); // register handler for SIGINT
  sharedResources = new struct PThreadResources; // shared resources for threads
  pthread_mutex_init(&sharedResources->sock_mx, NULL);
  pthread_mutex_init(&sharedResources->file_mx, NULL);
  pthread_mutex_init(&sharedResources->dir_mx, NULL);
  pthread_mutex_init(&sharedResources->regex_mx, NULL);
  // have to use \\w instead of just \w
  sharedResources->httpHeaderRegex = std::regex(
    "^(GET|HEAD|POST|CONNECT) (\\/|(\\/.*)+\\.(html|txt|png|gif|jpg|css|js)) HTTP\\/\\d\\.\\d\r\n");
}
// DESTRUCTOR
WebServer::~WebServer(){
  std::cout << "Closing TCP sockets..." << std::endl;
  close(sharedResources->sock);
  pthread_mutex_destroy(&sharedResources->sock_mx);
  pthread_mutex_destroy(&sharedResources->file_mx);
  pthread_mutex_destroy(&sharedResources->dir_mx);
  pthread_mutex_destroy(&sharedResources->regex_mx);
}
//== PTHREAD FUNCTION AND HELPER FUNCTIONS
// PThread function
void * AcceptConnection(void * sharedResources) {
  // This struct contains mutexes, conditionVars,
  struct PThreadResources * shared = (struct PThreadResources*)sharedResources;
  char * buffer = new char[BUFFER_SIZE];
  char path[256] = "./www"; // all files should be hosted from www folder
  int clientSocket;
  struct sockaddr_in clientAddr; // client addr
  socklen_t clientLen;
  size_t numBytes = 0;
  int currentSize;
  cmatch match1, match2; // since buffer is char*, need cmatch instead of smatch
  struct RequestMessage * request = new struct RequestMessage;
  struct ResponseMessage * response = new struct ResponseMessage;
  filebuf * fileBufPoint; // file buffer pointer, used for getting file size
  //== accept connection from client
  pthread_mutex_lock(&shared->sock_mx); //== MUTEX
  clientSocket = accept(shared->sock, (struct sockaddr *)&clientAddr, &clientLen);
  // cout << clientSocket << endl;
  if (clientSocket < 0) {
    pthread_mutex_unlock(&shared->sock_mx); //== UNLOCK
    perror("accept connection failed\n");
    delete [] buffer;
    pthread_exit(NULL);
  }
  //== receive and verify request message ====
  memset(buffer, 0, BUFFER_SIZE);
  numBytes = ReceiveMessage(clientSocket, buffer, shared);
  //== UPDATE REQUEST MEMBER VALUES WITH REGEX
  InitReqRecStructs(buffer, request, response);
  //== NOW, TRY TO OPEN FILE THAT CLIENT REQUESTED
  if (strcmp(request->uri, "/") == 0)// did they simply request index.html?
    strcat(path, "/index.html");
  else
    strcat(path, request->uri);
  shared->ifs.open(path, ifstream::binary);
  fileBufPoint = shared->ifs.rdbuf();
  numBytes = fileBufPoint->pubseekoff(0,shared->ifs.end,shared->ifs.in);
  fileBufPoint->pubseekpos(0,shared->ifs.in); // go back to beginning
  if (numBytes < 0)
    perror("unable to get file size");
  bzero(buffer, BUFFER_SIZE);
  strcpy(buffer, request->httpVersion);
  strcat(buffer, " 200 OK\r\n");
  DateTimeRFC(buffer);
  strcat(buffer, "Content-Length: ");
  strcat(buffer, to_string(numBytes).c_str());
  strcat(buffer, "\r\n");
  strcat(buffer, "Connection: keep-alive\r\n");
  strcat(buffer, "Content-Type: ");
  strcat(buffer, response->content);
  strcat(buffer, "\r\n\r\n");
  currentSize = strlen(buffer); // get offset for remaining message
  shared->ifs.read(buffer + currentSize, numBytes);
  send(clientSocket, buffer, (currentSize+numBytes), 0);
  close(clientSocket);
  shared->ifs.close();
  pthread_mutex_unlock(&shared->sock_mx); //== UNLOCK
  delete [] buffer;
  pthread_exit(NULL);
}
// ReceiveMessage, buffers message and handles errors
size_t ReceiveMessage(int sock, char * buf, struct PThreadResources * shared) {
  size_t numBytes;
  cmatch match1; // since buffer is char*, need cmatch instead of smatch

  numBytes = recv(sock, buf, BUFFER_SIZE, 0);
  if ((int)numBytes < 0) {
    pthread_mutex_unlock(&shared->sock_mx); //== UNLOCK
    perror("ERROR in recvfrom");
    close(sock);
    delete [] buf;
    pthread_exit(NULL);
  }
  if (regex_search(buf, match1, shared->httpHeaderRegex))
    cout << match1.str();
  else {
    pthread_mutex_unlock(&shared->sock_mx); //== UNLOCK
    perror("parsing and verifying failed\n");
    cout << buf << endl;
    close(sock);
    delete [] buf;
    pthread_exit(NULL);
  }
  return numBytes;
}
// SignalHandler, catch signals registered with this handler
void SignalHandler(int signal) {
  switch(signal) {
    case SIGINT:
      cout << endl <<
      "Caught SIGINT, HTTP server will exit after next request." << endl;
      proceed = false;;
  }
}
// DateTimeRFC, return date and time in RFC 822 format:
//   https://www.w3.org/Protocols/rfc822/#z28
void DateTimeRFC(char * buf) {
  time_t rawtime;
  struct tm * timeinfo;
  char dateTimeRFC[64]; // return value of this function
  size_t currentLen; // current length of dateTimeRFC
  strcat(buf, "Date: ");
  time(&rawtime);
  timeinfo = localtime(&rawtime);
  currentLen = strlen(buf);
  // https://www.ibm.com/support/knowledgecenter/en/SSRULV_9.1.0/com.ibm.tivoli.itws.doc_9.1/distr/src_tr/awstrstrftime.htm
  strftime(dateTimeRFC, (sizeof(dateTimeRFC) - currentLen), "%a, %d %b %Y %H:%M:%S %Z", timeinfo);
  strcat(buf, dateTimeRFC);
  strcat(buf, "\r\n");
  return;
}
// InitReqRecStructs, sets values for request and response structs with regex
void InitReqRecStructs(char * buffer, struct RequestMessage * request,
        struct ResponseMessage * response) {
  cmatch match1, match2; // since buffer is char*, need cmatch instead of smatch
  //== UPDATE REQUEST MEMBER VALUES WITH REGEX
  regex_search(buffer, match1, regex("^(GET|HEAD|POST)"));
  strcpy(request->method, match1.str().c_str());
  regex_search(buffer, match1, regex("((\\/.+)+\\.(html|txt|png|gif|jpg|css|js)|\\/)"));
  strcpy(request->uri, match1.str().c_str());
  regex_search(buffer, match1, regex("HTTP\\/\\d\\.\\d"));
  strcpy(request->httpVersion, match1.str().c_str());
  regex_search(buffer, match1, regex("Connection: (keep-alive|close)"));
  regex_search(match1.str().c_str(), match2, regex("(keep-alive|close)"));
  strcpy(request->connection, match2.str().c_str());
  regex_search(buffer, match1, regex("(html|txt|png|gif|jpg|css|js)"));
  if (match1.str() == "html")
    strcpy(response->content, "text/html");
  else if (match1.str() == "txt")
    strcpy(response->content, "text/plain");
  else if (match1.str() == "png")
    strcpy(response->content, "image/png");
  else if (match1.str() == "gif")
    strcpy(response->content, "image/gif");
  else if (match1.str() == "jpg")
    strcpy(response->content, "image/jpg");
  else if (match1.str() == "css")
    strcpy(response->content, "text/css");
  else if (match1.str() == "js")
    strcpy(response->content, "application/javascript");
  return;
}
