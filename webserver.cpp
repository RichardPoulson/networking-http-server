#include "webserver.h"

//== Class WebServer
// CONSTRUCTOR
WebServer::WebServer(unsigned short portNumber){
  Initialize();
  this->portNum = portNumber;
  if (CreateSocket() != true) { exit(EXIT_FAILURE); }
  if (BindSocket() != true) { exit(EXIT_FAILURE); }
  StartHTTPService(); // forever loop, program has to receive SIGINT to exit
}
//== PThread instructions and resources
void * AcceptConnection(void * sharedResources) {
  struct PThreadResources * shared = (struct PThreadResources*)sharedResources;
  char buffer[BUFFER_SIZE];
  char path[256] = "./www/"; // all files should be hosted from www folder
  int clientSocket;
  struct sockaddr_in clientAddr; // client addr
  socklen_t clientLen;
  int numBytes, currentSize;
  cmatch match1, match2; // since buffer is char*, need cmatch instead of smatch
  time_t rawtime;
  struct tm * timeinfo;
  char dateTime[100];
  struct RequestMessage * request = new struct RequestMessage;
  struct ResponseMessage * response = new struct ResponseMessage;

  //== accept connection from client
  pthread_mutex_lock(&shared->sock_mx); //== MUTEX
  clientSocket = accept(shared->sock, (struct sockaddr *)&clientAddr, &clientLen);
  pthread_mutex_unlock(&shared->sock_mx); //== UNLOCK
  // cout << clientSocket << endl;
  if (clientSocket < 0) {
    perror("accept connection failed\n");
    pthread_exit(NULL);
  }
  //== receive and verify request message ====
  bzero(buffer, sizeof(buffer)); // clear buffer
  numBytes = recv(clientSocket, buffer, sizeof(buffer), 0);
  if ((int)numBytes < 0) {
    perror("ERROR in recvfrom");
    close(clientSocket);
    pthread_exit(NULL);
  }
  pthread_mutex_lock(&shared->regex_mx); //== MUTEX
  if (regex_search(buffer, match1, shared->httpHeaderRegex)) {
    pthread_mutex_unlock(&shared->regex_mx); //== UNLOCK
    std::cout << "HTTP Request Verified!" << std::endl;
    std::cout << buffer << std::endl;
  }
  else {
    pthread_mutex_unlock(&shared->regex_mx); //== UNLOCK
    cout << "HTTP Request Invalid!" << endl;
    perror("parsing and verifying failed\n");
    close(clientSocket);
    pthread_exit(NULL);
  }
  //== UPDATE REQUEST MEMBER VALUES WITH REGEX
  regex_search(buffer, match1, regex("^(GET|HEAD|POST)"));
  request->method = match1.str();
  regex_search(buffer, match1, regex("(\/|(\/\\w+)+.(html|txt|png|gif|jpg|css|js))"));
  request->uri = match1.str();
  regex_search(buffer, match1, regex("HTTP\/\\d\.\\d"));
  request->httpVersion = match1.str();
  regex_search(buffer, match1, regex("Connection: (keep-alive|close)"));
  regex_search(buffer, match2, regex("(keep-alive|close)"));
  request->connection = match2.str();
  //== NOW, TRY TO OPEN FILE THAT CLIENT REQUESTED
  if (request->uri == "/") { // did they simply request index.html?
    strcat(path, "index.html");
    pthread_mutex_lock(&shared->file_mx); //== MUTEX
    shared->file = fopen(path, "rb");
  }
  else {
    strcat(path, request->uri.c_str());
    pthread_mutex_lock(&shared->file_mx); //== MUTEX
    shared->file = fopen(path, "rb");
  }
  //== DID THE CLIENT REQUEST A FILE THAT EXISTS?
  if(!shared->file) {
    fclose(shared->file);
    pthread_mutex_unlock(&shared->file_mx); //== UNLOCK
  }
  fseek(shared->file, 0L, SEEK_END); // seek to end in order to get size
  numBytes = ftell(shared->file); // get size of file in bytes
  rewind(shared->file); // seek back to beginning of file

  bzero(buffer, BUFFER_SIZE);
  strcpy(buffer, "HTTP/1.1 200 OK\r\n");
  strcat(buffer, "Date: ");
  // RFC 822: date and time specification
  //   https://www.w3.org/Protocols/rfc822/#z28
  time(&rawtime);
  timeinfo = localtime(&rawtime);
  // https://www.ibm.com/support/knowledgecenter/en/SSRULV_9.1.0/com.ibm.tivoli.itws.doc_9.1/distr/src_tr/awstrstrftime.htm
  strftime(dateTime, sizeof(buffer), "%a, %d %b %Y %H:%M:%S %Z", timeinfo);
  strcat(buffer, dateTime);
  strcat(buffer, "\r\n");
  strcat(buffer, "Content-Length: ");
  strcat(buffer, to_string(numBytes).c_str());
  strcat(buffer, "\r\n");
  strcat(buffer, "Connection: keep-alive\r\n");
  strcat(buffer, "Content-Type: text/html\r\n\r\n");
  currentSize = strlen(buffer); // get offset for remaining message
  fread(buffer + currentSize, numBytes, 1, shared->file);
  send(clientSocket, buffer, strlen(buffer), 0);
  cout << strlen(buffer) << endl;
  close(clientSocket);
  fclose(shared->file);
  pthread_mutex_unlock(&shared->file_mx); //== UNLOCK
  pthread_exit(NULL);
}
// StartHTTPService,
void WebServer::StartHTTPService()
{
  std::cout << "Starting HTTP Service..." << std::endl;
  pthread_mutex_lock(&sharedResources->sock_mx); //== MUTEX
  listen(sharedResources->sock , 3); //  Listen for connections
  pthread_mutex_unlock(&sharedResources->sock_mx); //== UNLOCK
  // how to organize a thread pool, stack?
  pthread_create(&httpConnections[0], NULL, AcceptConnection, sharedResources);
  pthread_join(httpConnections[0],NULL); // thread returns to program
}
/*
void WebServer::ReadFileToBuffer(char * file){
  char path[256] = "./www/";
  strcat(path, file);
  ifstream myfile;
  myfile.open(path, ios::binary);
  if (myfile.is_open())
  {
    myfile->close();
  }
  else std::cout << "Unable to open file";
}
*/
bool WebServer::CreateSocket()
{
  //  0= pick any protocol that socket type supports, can also use IPPROTO_TCP
  sharedResources->sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sharedResources->sock < 0) {
    perror("ERROR opening socket");
    //  Creation failed, so return false
    return false;
  }
  //  Allows bind to reuse socket address (if supported)
  optval = 1;
  setsockopt(sharedResources->sock, SOL_SOCKET, SO_REUSEADDR,
	     (const void *)&optval, sizeof(int));

  //  Define the server's Internet address
  bzero((char *) &serverAddr, sizeof(serverAddr));
  serverAddr.sin_family = AF_INET;
  serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);
  serverAddr.sin_port = htons(portNum);
  //  Creation successful, return true
  return true;
}
// BindSocket, bind parent socket to port, returns false if bind unsuccessful
bool WebServer::BindSocket()
{
  // bind: associate the parent socket with a port
  if (bind(sharedResources->sock, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0) {
    perror("ERROR on binding");
    //  Binding failed, so return false
    return false;
  }
  //  Binding successful, return true
  return true;
}
// SignalHandler, catch signals registered with this handler
void SignalHandler(int signal) {
  switch(signal) {
    case SIGINT:
      cout << endl << "Caught SIGINT" << endl;
      exit(EXIT_SUCCESS);
  }
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
void WebServer::Initialize() {
  signal(SIGINT, SignalHandler); // register handler for SIGINT
  sharedResources = new struct PThreadResources; // shared resources for threads
  pthread_mutex_init(&sharedResources->sock_mx, NULL);
  pthread_mutex_init(&sharedResources->file_mx, NULL);
  pthread_mutex_init(&sharedResources->dir_mx, NULL);
  pthread_mutex_init(&sharedResources->regex_mx, NULL);
  // have to use \\w instead of just \w
  sharedResources->httpHeaderRegex = std::regex(
    "^(GET|HEAD|POST) (\/|(\/\\w+)+.(html|txt|png|gif|jpg|css|js)) HTTP\/\\d\.\\d\r\n");
}
