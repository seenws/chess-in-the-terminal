#ifndef CITT_HEADERS_UCI_H_
#define CITT_HEADERS_UCI_H_

/* Reads UCI commands from `stdin` and drives the engine until EOF or
   `quit`. Single-threaded: `stop` is honored at the next abort poll
   inside the search, and only affects the currently-running `go`.  */
void uci_loop(void);

#endif /* CITT_HEADERS_UCI_H_ */
