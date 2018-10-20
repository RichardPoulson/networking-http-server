#include "webserver.h"

int main(int argc, char **argv) {
  if (argc != 2) {
    cout << argc << endl;
    cout <<
      "Invalid number of additional arguments, please enter one number" <<
      "as argument." << endl;
      exit(EXIT_FAILURE);
  }
  WebServer myServer(atoi(argv[1])); // first argument is string rep. of num.
}
