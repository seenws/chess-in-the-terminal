#ifndef CITT_HEADERS_UCI_H_
#define CITT_HEADERS_UCI_H_

/* Reads UCI commands from `stdin` and drives the engine until EOF or
   `quit`. Single-threaded: `stop` is honored at the next abort poll
   inside the search, and only takes effect for the currently-running
   `go` command.  */
void uci_loop(void);

#endif
