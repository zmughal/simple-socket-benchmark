#define _XOPEN_SOURCE 600


#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>


#define handle_error(msg) \
    do { perror(msg); exit(EXIT_FAILURE); } while (0)

#define MAXDATASIZE    256 /* max number of bytes we can get at once */


#define MIN_METHOD_ID         1
#define METHOD_ASYNC_POLL     1
#define METHOD_SELECT         2
#define METHOD_CLOSE_SOCKET   3
#define MAX_METHOD_ID         3


#define ADDR         "localhost"
#define HOST         "localhost"

char PORT[6];

void start_client(int method_id, int num_req_per_client) {
	int sockfd, numbytes;
	struct addrinfo hints, *servinfo, *p;
	int rv;
	char s[INET6_ADDRSTRLEN];

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	if ((rv = getaddrinfo(HOST, PORT, &hints, &servinfo)) != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
		exit(1);
	}

	/* loop through all the results and connect to the first we can */
	for(p = servinfo; p != NULL; p = p->ai_next) {
		if ((sockfd = socket(p->ai_family, p->ai_socktype,
				p->ai_protocol)) == -1) {
			perror("client: socket");
			continue;
		}

		if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
			close(sockfd);
			/*perror("client: connect");*/
			continue;
		}

		break;
	}

	if (p == NULL) {
		fprintf(stderr, "client: failed to connect\n");
		exit(2);
	}

	freeaddrinfo(servinfo); /* all done with this structure */

	char buf[MAXDATASIZE];
	int req_count = 0;

	switch(method_id) {
		case METHOD_CLOSE_SOCKET:
			while( req_count++ < num_req_per_client ) {
				send(sockfd, "1", 2, 0);
				recv(sockfd, NULL, 0, 0); /* flush */
				while( 1 ) {
					if ((numbytes = recv(sockfd, buf, MAXDATASIZE-1, 0)) <= 0) {
						break;
					}
					buf[numbytes] = '\0';
				}
				recv(sockfd, NULL, 0, 0);
				send(sockfd, "e", 2, 0);
				close(sockfd);
				connect(sockfd, p->ai_addr, p->ai_addrlen); /* for the next iteration */
			}
		case METHOD_SELECT:
			/* TODO */
			break;
		case METHOD_ASYNC_POLL:
			/* TODO */
			break;
	}
	printf("client done\n");

	return;
}


void start_server(int method, int num_clients, int MAXCONN, int LISTEN_QUEUE) {
	int sockfd, new_fd;  /* listen on sock_fd, new connection on new_fd */
	struct addrinfo hints, *servinfo, *p;
	socklen_t sin_size;
	struct sigaction sa;
	int yes=1;
	char s[INET6_ADDRSTRLEN];
	int rv;
	int count = 0;
	int done = 0;

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE; /* use my IP */

	if ((rv = getaddrinfo(NULL, PORT, &hints, &servinfo)) != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
		exit(1);
	}

	/* loop through all the results and bind to the first we can */
	for(p = servinfo; p != NULL; p = p->ai_next) {
		if ((sockfd = socket(p->ai_family, p->ai_socktype,
				p->ai_protocol)) == -1) {
			perror("server: socket");
			continue;
		}

		if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes,
				sizeof(int)) == -1) {
			perror("setsockopt");
			exit(1);
		}

		if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
			close(sockfd);
			perror("server: bind");
			continue;
		}

		break;
	}

	if (p == NULL)  {
		fprintf(stderr, "server: failed to bind\n");
		exit(2);
	}

	freeaddrinfo(servinfo); /* all done with this structure */

	if (listen(sockfd, LISTEN_QUEUE) == -1) {
		perror("listen");
		exit(1);
	}


	char buf[MAXDATASIZE];
	switch(method) {
		case METHOD_CLOSE_SOCKET:
			while(!done) {
				new_fd = accept(sockfd, NULL, NULL);
				if (new_fd == -1) {
					perror("accept");
					continue;
				}
				count = 0;
				while(1) {
					int bytes;
					if( (bytes = recv(new_fd, buf, MAXDATASIZE - 1, 0)) <= 0 )
						break;
					buf[bytes] = '\0';
					if( strncmp(buf, "e", 1) )
						if( 0 == --num_clients ) {
							done = 1;
							break;
						}
				}
				send(new_fd, "1", 2, 0);
				printf("recv: %d\n", count++);
				recv(sockfd, NULL, 0, 0); /* flush */
				close(new_fd);
			}
			break;
		case METHOD_ASYNC_POLL:
			/* TODO */
			break;
		case METHOD_SELECT:
			/* TODO */
			break;

	}
}

int main(int argc, char** argv) {
	int client;
	if(argc != 6) {
		fprintf(stderr,
			"Usage: %s [b|s|c] [port] [number of clients] [number of requests per client] [method_id]\n",
			argv[0]);
		exit(EXIT_FAILURE);
	}
	int num_clients, num_req_per_client, method_id;
	char mode;
	sscanf(argv[1], "%c", &mode);
	sscanf(argv[2], "%s", &PORT);
	sscanf(argv[3], "%d", &num_clients);
	sscanf(argv[4], "%d", &num_req_per_client);
	sscanf(argv[5], "%d", &method_id);
	if( mode != 'b' && mode != 's' && mode != 'c' ) {
		fprintf(stderr, "Invalid mode: must be either 'b', 's', or 'c'\n");
		exit(EXIT_FAILURE);
	}
	if( method_id < MIN_METHOD_ID || method_id > MAX_METHOD_ID ) {
		fprintf(stderr, "Method ID out of range: %d - %d\n",
			MIN_METHOD_ID, MAX_METHOD_ID);
		exit(EXIT_FAILURE);
	}
	if( num_clients <= 0 || num_req_per_client <= 0 ) {
		fprintf(stderr, "number of clients and number of requests per client must be greater than 0\n");
		exit(EXIT_FAILURE);
	}
	/* TODO tic */
	if( fork() == 0 ) {
		start_server(method_id, num_clients, num_clients, num_clients);
		exit(EXIT_SUCCESS);
	}
	for (client = 0; client < num_clients; client++) {
		if(fork() == 0) {
			start_client(method_id, num_req_per_client);
			exit(EXIT_SUCCESS);
		}
	}
	while (wait(NULL)) {
		if (errno == ECHILD) {
			break;
		}
	}
	/* TODO toc */
}
