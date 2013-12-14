/*
 * proxy.c - CS:APP Web proxy
 *
 * TEAM MEMBERS:
 *     Lingliang Zhang, lz781@nyu.edu 
 *     Oleg Grishin, og402@nyu.edu 
 * 
 * IMPORTANT: A simple HTTP Proxy
 * that is able to utilize multiple threads
 * to deal with several clients at once
 */ 

#include "csapp.h"
#include <string.h>

/*
 * Function prototypes
 */
int parse_uri(char *uri, char *target_addr, char *path, int  *port);
void format_log_entry(char *logstring, struct sockaddr_in *sockaddr, char *uri, int size);
void print_log(struct sockaddr_in *sockaddr, char *uri, int size);
void response_controller(int connfd, struct sockaddr_in *clientaddr);

/*
 * Open file to write log into
 */

FILE *logfile;

/*
 * respond_to_connect - takes a connfd and handles the proxy logic
 */
void response_controller(int connfd, struct sockaddr_in *clientaddr)
{
  size_t n;
  int port = 0, serverfd;
  size_t totalByteCount = 0;
  char buf[MAXLINE], hostname[MAXLINE], path[MAXLINE],
       method[16], version[16], leadLine[MAXLINE], uri[MAXLINE];
  rio_t rio_client;
  rio_t rio_server;
  Rio_readinitb(&rio_client, connfd);

  // fix parse_uri design flaw
  path[0] = '/';

  // handle first line, extract uri
  int stageCounter = 0;
  char *token;
  n = Rio_readlineb(&rio_client, buf, MAXLINE);
  printf("READ: \n%s", buf);
  Rio_writen(connfd, buf, n);

  // tokenize the url to send the path to parse_uri
  token = strtok(buf, " ");
  while (token != NULL) {
    switch (stageCounter++)
    {
      case 0:
        strcpy(method, token);
        break;
      case 1:
        strcpy(uri, token);
        if (parse_uri(token, hostname, path+1, &port) == -1) {
          return;
        }
        break;
      case 2:
        strcpy(version, token);
        break;
    }   
    token = strtok(NULL, " ");
  }

  // after a successful request, connect to the server
  serverfd = Open_clientfd(hostname, port);
  Rio_readinitb(&rio_server, serverfd);

  // Write the initial header to the server
  sprintf(leadLine, "%s %s %s", method, path, version);
  Rio_writen(serverfd, leadLine, strlen(leadLine));
  
  while((n = Rio_readlineb(&rio_client, buf, MAXLINE)) > 0 &&
      buf[0] != '\r' && buf[0] != '\n') {
    printf("%s", buf);
    Rio_writen(serverfd, buf, n);
  }
  Rio_writen(serverfd, "\r\n", 2);

  printf("\nRESPOND: \n%s", leadLine);
  // Read response from server
  while((n = Rio_readnb(&rio_server, buf, MAXLINE)) > 0) {
    printf("%s", buf);
    Rio_writen(connfd, buf, n);
    totalByteCount += n;
  }

  // Exchange complete
  Close(serverfd);
  print_log(clientaddr, uri, totalByteCount);
}

/* 
 * main - Main routine for the proxy program 
 */
int main(int argc, char **argv)
{

    int listenfd, connfd, port;
    socklen_t clientlen;
    struct sockaddr_in clientaddr;
    struct hostent *hp;
    char *client_ip;


    /* Check arguments */
    if (argc != 2) {
      fprintf(stderr, "Usage: %s <port number>\n", argv[0]);
      exit(0);
    }

    port = atoi(argv[1]);

    /* bind to listening port */
    if ((listenfd = Open_listenfd(port)) == -1)
      unix_error("Couldn't establish connection to port");

    /* Open fd for logfile */
    logfile = fopen("proxy.log", "a");

    while (1) {
        clientlen = sizeof(clientaddr);
        connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);

        /* Get the client's network information */
        hp = Gethostbyaddr((const char *)&clientaddr.sin_addr.s_addr,
          sizeof(clientaddr.sin_addr.s_addr), AF_INET);
        client_ip = inet_ntoa(clientaddr.sin_addr);
        printf("Client connected to %s (%s)\n", hp->h_name, client_ip);
        response_controller(connfd, &clientaddr);
        Close(connfd);
    }

    exit(0);
}



/*
 * parse_uri - URI parser
 * 
 * Given a URI from an HTTP proxy GET request (i.e., a URL), extract
 * the host name, path name, and port.  The memory for hostname and
 * pathname must already be allocated and should be at least MAXLINE
 * bytes. Return -1 if there are any problems.
 */
int parse_uri(char *uri, char *hostname, char *pathname, int *port)
{
    char *hostbegin;
    char *hostend;
    char *pathbegin;
    int len;

    if (strncasecmp(uri, "http://", 7) != 0) {
	hostname[0] = '\0';
	return -1;
    }
       
    /* Extract the host name */
    hostbegin = uri + 7;
    hostend = strpbrk(hostbegin, " :/\r\n\0");
    len = hostend - hostbegin;
    strncpy(hostname, hostbegin, len);
    hostname[len] = '\0';
    
    /* Extract the port number */
    *port = 80; /* default */
    if (*hostend == ':')   
	*port = atoi(hostend + 1);
    
    /* Extract the path */
    pathbegin = strchr(hostbegin, '/');
    if (pathbegin == NULL) {
	pathname[0] = '\0';
    }
    else {
	pathbegin++;	
	strcpy(pathname, pathbegin);
    }

    return 0;
}

/*
 * format_log_entry - Create a formatted log entry in logstring. 
 * 
 * The inputs are the socket address of the requesting client
 * (sockaddr), the URI from the request (uri), and the size in bytes
 * of the response from the server (size).
 */
void format_log_entry(char *logstring, struct sockaddr_in *sockaddr, 
		      char *uri, int size)
{
    time_t now;
    char time_str[MAXLINE];
    unsigned long host;
    unsigned char a, b, c, d;

    /* Get a formatted time string */
    now = time(NULL);
    strftime(time_str, MAXLINE, "%a %d %b %Y %H:%M:%S %Z", localtime(&now));

    /* 
     * Convert the IP address in network byte order to dotted decimal
     * form. Note that we could have used inet_ntoa, but chose not to
     * because inet_ntoa is a Class 3 thread unsafe function that
     * returns a pointer to a static variable (Ch 13, CS:APP).
     */
    host = ntohl(sockaddr->sin_addr.s_addr);
    a = host >> 24;
    b = (host >> 16) & 0xff;
    c = (host >> 8) & 0xff;
    d = host & 0xff;

    /* Return the formatted log entry string */
    sprintf(logstring, "%s: %d.%d.%d.%d %s %d\n", time_str, a, b, c, d, uri, size);
}

/*
 * print_log - makes a log entry
 */
void print_log(struct sockaddr_in *sockaddr, char *uri, int size)
{
    char *logstring[MAXLINE];
    format_log_entry(logstring, sockaddr, uri, size);
    fprintf(logfile, logstring);
    fflush(logfile);
}

