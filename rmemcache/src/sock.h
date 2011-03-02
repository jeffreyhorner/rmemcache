/* sockets */
void mc_SockTimeout(int delay);
int mc_SockOpen(int port);
int mc_SockListen(int sockp, char *buf, int len);
int mc_SockConnect(int port, char *host);
int mc_SockClose(int sockp);
int mc_SockRead(int sockp, void *buf, int maxlen, int blocking);
int mc_SockWrite(int sockp, const void *buf, int len);
