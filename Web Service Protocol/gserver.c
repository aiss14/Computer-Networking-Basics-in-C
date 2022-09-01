/*
** server.c -- a stream socket server demo
 * source: beej's guide to network programming: https://beej.us/guide/bgnet/examples/server.c
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>

#include "uthash.h"

#define BACKLOG 10   // how many pending connections queue will hold

#define HEADSIZE 7
#define DELETE 0
#define SET 1
#define GET 2

typedef struct dataElement {
    short int kLen;
    long int vLen;
    unsigned char *key;
    unsigned char *value;
    UT_hash_handle hh;
} dataElement;

typedef struct rnvsH {
    char type;
    char ack;
    dataElement *data;
} rnvsH;


struct dataElement *entries = NULL;


/**
 * docode char buffer, save and allocate data in rnvsH
 * @param head buffer as received from client
 * @oaram req head structure
 * @retval 0 success
 * @retval -1 error
 */
rnvsH *demarshall(unsigned char *head, int fd);

unsigned char *marshall(struct rnvsH *res, uint64_t *size);

int sendall(int s, char *buf, uint64_t *len);


/**
 * searches array for data, saves it to element
 * @param head hashmap
 * @param keyptr
 * @param keylen
 * @return
 */
struct dataElement *getData(dataElement *element);

int addData(dataElement *e);


int delData(dataElement *element);

int deleteALl();

void sigchld_handler(int s) {
    // waitpid() might overwrite errno, so we save and restore it:
    int saved_errno = errno;

    while (waitpid(-1, NULL, WNOHANG) > 0);

    errno = saved_errno;
}


// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa) {
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in *) sa)->sin_addr);
    }

    return &(((struct sockaddr_in6 *) sa)->sin6_addr);
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        perror("invalid argument count");
        return (-1);
    }

    char *port = argv[1];


    int sockfd, new_fd;  // listen on sock_fd, new connection on new_fd
    struct addrinfo hints, *servinfo, *p;
    struct sockaddr_storage their_addr; // connector's address information
    socklen_t sin_size;
    struct sigaction sa;
    int yes = 1;
    char s[INET6_ADDRSTRLEN];
    int rv;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE; // use my IP

    if ((rv = getaddrinfo(NULL, port, &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return 1;
    }

    // loop through all the results and bind to the first we can
    for (p = servinfo; p != NULL; p = p->ai_next) {
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

    freeaddrinfo(servinfo); // all done with this structure

    if (p == NULL) {
        fprintf(stderr, "server: failed to bind\n");
        exit(1);
    }

    if (listen(sockfd, BACKLOG) == -1) {
        perror("listen");
        exit(1);
    }

    sa.sa_handler = sigchld_handler; // reap all dead processes
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        perror("sigaction");
        exit(1);
    }

//    struct dataElement *s;
    printf("server: waiting for connections...\n");

    while (1) {  // main accept() loop
        sin_size = sizeof their_addr;
        new_fd = accept(sockfd, (struct sockaddr *) &their_addr, &sin_size);
        if (new_fd == -1) {
            perror("accept");
            continue;
        }

        inet_ntop(their_addr.ss_family,
                  get_in_addr((struct sockaddr *) &their_addr),
                  s, sizeof s);
        printf("server: got connection from %s\n", s);



        /**start handling request*/
        unsigned char hBuf[HEADSIZE];    //Buffer for head
        int hbytes = recv(new_fd, hBuf, HEADSIZE, 0); //todo for-loop?
        if (hbytes == 0) {
            printf("connection closed by host");
            exit(1);
        }
        if (hbytes == -1) {
            perror("recv");
            exit(1);
        }
        if (hbytes != HEADSIZE) {
            fwrite("invalid head", sizeof(char), 12, stderr);
            exit(1);
        }

        rnvsH *req = demarshall(hBuf, new_fd);
        if (NULL == req) {
            fwrite("demarshall failed", sizeof(char), 17, stderr);
            exit(1);
        }

        rnvsH *res = malloc(sizeof(rnvsH));
        res->data = malloc(sizeof(dataElement));
        res->data->kLen = 0;
        res->data->vLen = 0;
        res->ack = 0;

        switch (req->type) {
            case GET:
                printf("GET");
                res->data = getData(req->data);
                //set res
                res->type = GET;
                if (res->data == NULL) {
                    fprintf(stderr, "data not found");
                } else {
                    res->ack = 1;
                }
                break;

            case SET:
                printf("SET");
                res->type = SET;

                //add data to hashmap
                res->ack = addData(req->data) ? 0 : 1;

                break;
            case DELETE:
                printf("DELETE");
                res->type = DELETE;
                res->ack = delData(req->data) ? 0 : 1;
                break;
            default:
                break;
        }
        uint64_t *size = malloc(sizeof(uint64_t)); //size of res

        char *rMsg = marshall(res, size);

        //send res
        if (sendall(new_fd, rMsg, size) == -1) {
            perror("send message");
        }



        //free buffers
        free(req);
        free(rMsg);
//            free(kBuf);
//            free(vBuf);
        free(size);

        close(new_fd); //todo add this to other exits?

        //response

//            exit(0);
//        }
//        close(new_fd);  // parent doesn't need this
    }
    close(new_fd); //todo add this to other exits?
    deleteALl();
    return 0;
}


rnvsH *demarshall(unsigned char *head, int fd) {
    rnvsH *req = malloc(sizeof(rnvsH));
    req->data = malloc(sizeof(dataElement));
    //handle first byte

    unsigned char lsB = 1;
    char byte = head[0];
    req->type = -1;
    for (int i = 0; i < 3; ++i) {
        if ((lsB & byte)) {
            if (-1 == req->type) {
                req->type = (char) i;
            } else {
                return NULL;
            }
        }
        byte = byte >> 1;
    }
    //handle acknowledge bit, todo evaluate necessity server side
    if ((lsB & byte)) {
        req->ack = 1;
    }

    //handle key length:
    u_short kLen = head[1] << 0 | head[2] << 8;
    req->data->kLen = ntohs(kLen);

    u_long vLen = 0;
    vLen |= ((head[3] << 0) | (head[4] << 8) | (head[5] << 16) | (head[6] << 24));

    req->data->vLen = ntohl(vLen);
    //receive keytb
    req->data->key = malloc(sizeof(char) * req->data->kLen);
    if (req->data->kLen > 0) {
        for (short remBytes = req->data->kLen;
             remBytes > 0;) {
            short numbytes = recv(fd, req->data->key, remBytes, 0);
            if (numbytes == -1) {
                perror("recv");
                exit(1);
            }

            if (numbytes == 0) {
                printf("connection closed by host");
            }
            remBytes -= numbytes;
        }
    }
    //receive data
    req->data->value = malloc(sizeof(char) * req->data->vLen);
    if (req->data->vLen > 0) {
        long buff=0;
        for (long remBytes = req->data->vLen; remBytes > 0;) {
            long numbytes = recv(fd, req->data->value , remBytes, 0);

            if (numbytes == -1) {
                perror("recv");
                exit(1);
            }

            if (numbytes == 0) {
                printf("connection closed by host");
            }

            remBytes -= numbytes;
        }
    }

    return req;
}

unsigned char *marshall(struct rnvsH *res, uint64_t *size) {
    *size = HEADSIZE;

    unsigned char lsB = 1;
    unsigned char *head = calloc(sizeof(char), HEADSIZE);

    //set type bit
    head[0] = 0;
    lsB = lsB << res->type;
    head[0] |= lsB;
    lsB = lsB << (2 - res->type);
    //set acknowledge bit, todo evaluate necessity server side
    if (res->ack) {
        head[0] |= lsB << 1;
    }

    if (res->data == NULL) {
        return head;
    }
    *size = HEADSIZE + res->data->vLen + res->data->kLen;
    //marshal key length:
    short kLen = htons(res->data->kLen);
    head[1] |= kLen;
    head[2] |= kLen >> 8;

    //marshal value length
    long vLen = htonl(res->data->vLen);
    for (int i = 3; i < 7; ++i) {
        head[i] |= vLen;
        vLen = vLen >> 8;
    }

    unsigned char *msg = realloc(head, *size);
    for (int i = 0; i < res->data->kLen; ++i) {
        msg[i + HEADSIZE] = res->data->key[i];
    }
    for (int i = 0; i < res->data->vLen; ++i) {
        msg[i + HEADSIZE + res->data->kLen] = res->data->value[i];
    }

    return msg;
}

int addData(dataElement *e) {
    while (getData(e) != NULL) {
        fprintf(stderr, "key already in use, overwriting...");
        delData(e);

    }
        HASH_ADD_KEYPTR(hh,entries,e->key,e->kLen,e);

    return 0;
}

int delData(dataElement *element) {
    dataElement *b = getData(element);
    if (b == NULL) {
        return -1;
    }
    HASH_DEL(entries, b);
    free(b);
    return 0;
}

int deleteAll() {
    dataElement *current, *tmp;
    HASH_ITER(hh, entries, current, tmp) {
        HASH_DEL(entries, current);
        free(current);
    }
}

struct dataElement *getData(dataElement *element) {
    dataElement *e =0;
    char key[element->kLen];
    for (int i = 0; i < element->kLen; ++i) {
        key[i] = element->key[i];
    }
    short klen = element->kLen;
    HASH_FIND(hh, entries, key, klen, e);
    return e;
}

int sendall(int s, char *buf, uint64_t *len) { //from beesguide
    int total = 0;        // how many bytes we've sent
    uint64_t bytesleft = *len; // how many we have left to send
    int n;
    while (total < *len) {
        if ((n = send(s, buf +total, bytesleft, 0)) == -1) {
            perror("send");
            break;
        }
        total += n;
        bytesleft -= n;
    }

    *len = total; // return number actually sent here

    return n == -1 ? -1 : 0; // return -1 on failure, 0 on success
}

