#pragma clang diagnostic push
#pragma ide diagnostic ignored "cppcoreguidelines-narrowing-conversions"
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
#define Pmsg 11
enum {
    DELETE = 0, SET = 1, GET = 2
};
enum {
    REPLY = 1, LOOKUP = 2
};

enum {
    CLIENT, PEER
};

int printV = 0;

typedef struct networkChars {
    char *ip;
    char *port;
} networkChars;


typedef struct clsock_fd {
    uint16_t hashID;
    int sock_fd;
    UT_hash_handle hh;
} clsock_fd;


typedef struct dataElement {
    u_int16_t kLen;
    uint32_t vLen;
    uint8_t *key;
    uint8_t *value;
    UT_hash_handle hh;
} dataElement;


typedef struct clientReq {
    int8_t type;
    uint8_t ack;
    dataElement *data;
} clientReq;
typedef struct peerReq {
    int8_t type;
    uint16_t hashID;
    uint16_t nodeID;
    in_addr_t nodeIP;
    in_port_t nodePort;
} peerReq;

typedef struct rnvsH {
    int8_t control;
    clientReq *cReq;
    peerReq *pReq;
} rnvsH;


typedef struct peer {
    uint16_t id;
    in_addr_t ip;
    in_port_t port;
    struct peer *pre;
    struct peer *post;
} peer;

struct dataElement *entries = NULL;
struct clsock_fd *clsockfds = NULL;

networkChars getNetworkChars(in_addr_t ip, in_port_t port);

int connectPeer(peer *peer1);

/**
 * docode char buffer, save and allocate data in rnvsH
 * @param head buffer as received from client
 * @oaram req type structure
 * @retval 0 success
 * @retval -1 error
 */
rnvsH *demarshall(int fd);

char *marshall(rnvsH *res, uint64_t *size);

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

clsock_fd *getClSockFd(uint16_t key);

int addClSockFd(clsock_fd *key);

int delClSockFd(clsock_fd *key);


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

int bindPeer(peer *peer1);

clientReq *handleClReq(clientReq *req);

char *marshallClient(const clientReq *cReq, uint64_t *size);

char *marshallPeer(const peerReq *pReq, uint64_t *size);

peerReq *demarshallPeer(unsigned char top, int fd);

clientReq *demarshallClient(unsigned char top, int fd);

int lookup(uint16_t hashID, peer node);

int newSock();

int newSocket(in_addr_t ip, in_port_t port);

int main(int argc, char *argv[]) {
    if (argc != 10) {
        perror("invalid argument count");
        return (-1);
    }
    peer thisPeer;
    peer pre;
    peer post;
    thisPeer.pre = &pre;
    thisPeer.post = &post;

    if (argv[1] == 0 || argv[4] == 0 || argv[7] == 0) {
        perror("id can't be 0!");

    }
    thisPeer.id = atoi(argv[1]);
    switch (inet_pton(AF_INET, argv[2], &thisPeer.ip)) {
        case -1:
            perror("pton");
            break;
        case 0:
            perror("ip adress invalid");
            break;
    }
    thisPeer.port = atoi(argv[3]);

    thisPeer.pre->id = atoi(argv[4]);
    switch (inet_pton(AF_INET, argv[5], &thisPeer.pre->ip)) {
        case -1:
            perror("pton");
            break;
        case 0:
            perror("ip adress invalid");
            break;
    }
    thisPeer.pre->port = atoi(argv[6]);

    thisPeer.post->id = atoi(argv[7]);
    switch (inet_pton(AF_INET, argv[8], &thisPeer.post->ip)) {
        case -1:
            perror("pton");
            break;
        case 0:
            perror("ip adress invalid");
            break;
    }
    thisPeer.post->port = atoi(argv[9]);

    socklen_t sin_size;
    char s[INET6_ADDRSTRLEN];
    struct sockaddr_storage their_addr; // connector's address information


    struct sigaction sa;


    fd_set master;    // master file descriptor list
    fd_set read_fds;  // temp file descriptor list for select()
    int fdmax;        // maximum file descriptor number

    int listener;     // listening socket descriptor
    int newfd;        // newly accept()ed socket descriptor
    struct sockaddr_storage remoteaddr; // client address
    socklen_t addrlen;

    char buf[256];    // buffer for client data
    int nbytes;

    char remoteIP[INET6_ADDRSTRLEN];

    int yes = 1;        // for setsockopt() SO_REUSEADDR, below
    int i, j, rv;

    struct addrinfo hints, *ai, *p;

    FD_ZERO(&master);    // clear the master and temp sets
    FD_ZERO(&read_fds);

    // get us a socket and bind it
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    int length = snprintf(NULL, 0, "%d", thisPeer.port);
    char *portC = malloc(length + 1);
    snprintf(portC, length + 1, "%d", thisPeer.port);

    if ((rv = getaddrinfo(NULL, portC, &hints, &ai)) != 0) { //todo
        fprintf(stderr, "selectserver: %s\n", gai_strerror(rv));
        exit(1);
    }

    for (p = ai; p != NULL; p = p->ai_next) {
        listener = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (listener < 0) {
            continue;
        }

        // lose the pesky "address already in use" error message
        setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));

        if (bind(listener, p->ai_addr, p->ai_addrlen) < 0) {
            close(listener);
            continue;
        }

        break;
    }

    // if we got here, it means we didn't get bound
    if (p == NULL) {
        fprintf(stderr, "selectserver: failed to bind\n");
        exit(2);
    }

    freeaddrinfo(ai); // all done with this

    // listen
    if (listen(listener, 10) == -1) {
        perror("listen");
        exit(3);
    }
    sa.sa_handler = sigchld_handler; // reap all dead processes
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        perror("sigaction");
        exit(1);
    }
    // add the listener to the master set
    FD_SET(listener, &master);

    // keep track of the biggest file descriptor
    fdmax = listener; // so far, it's this one

    // main loop
    for (;;) {
        rnvsH res;
        uint64_t size;
        char *rMsg;
        int clsock;
        read_fds = master; // copy it
        if (select(fdmax + 1, &read_fds, NULL, NULL, NULL) == -1) {
            perror("select");
            exit(4);
        }

        // run through the existing connections looking for data to read
        for (i = 0; i <= fdmax; i++) {
            if (FD_ISSET(i, &read_fds)) { // we got one!!
                if (i == listener) {
                    // handle new connections
                    addrlen = sizeof remoteaddr;
                    newfd = accept(listener,
                                   (struct sockaddr *) &remoteaddr,
                                   &addrlen);

                    if (newfd == -1) {
                        perror("accept");
                        continue;
                    } else {
                        FD_SET(newfd, &master); // add to master set
                        if (newfd > fdmax) {    // keep track of the max
                            fdmax = newfd;
                        }
                        printf("selectserver: new connection from %s on "
                               "socket %d\n",
                               inet_ntop(remoteaddr.ss_family,
                                         get_in_addr((struct sockaddr *) &remoteaddr),
                                         remoteIP, INET6_ADDRSTRLEN),
                               newfd);
                    }
                } else {// if we are the already connected client  ++++++++++++
                    // handle data from a client means receive the header first and decide the type of message+++++++++++++++
                    rnvsH *req = demarshall(newfd);
                    if (NULL == req) {
                        fwrite("unmarshall failed", sizeof(char), 17, stderr);
                        exit(1);
                    }
                    int responsible;
                    uint16_t hash_id;
                    switch (req->control) {

                        case PEER:
                            res.control = PEER;
                            res.pReq = malloc(sizeof(peerReq));
                            hash_id = req->pReq->hashID;

                            responsible =
                                    (thisPeer.pre->id > thisPeer.id) &&
                                    (hash_id > thisPeer.pre->id || thisPeer.id > hash_id) ||
                                    (thisPeer.pre->id < thisPeer.id) &&
                                    (thisPeer.id > hash_id && thisPeer.pre->id < hash_id);

                            int responsible_succ =
                                    (thisPeer.id > thisPeer.post->id) &&
                                    (hash_id > thisPeer.id || thisPeer.post->id > hash_id) ||
                                    (thisPeer.id < thisPeer.post->id) &&
                                    (thisPeer.post->id > hash_id && thisPeer.id < hash_id);
                            if ((responsible) && (req->pReq->type = REPLY)) {
                                //TODO Aiss
                                // create a socket with the IP and Port that are in the peer Request
                                int psock = newSocket(req->pReq->nodeIP, req->pReq->nodePort);
                                FD_SET(psock, &master); // add to master set
                                if (psock > fdmax) {    // keep track of the max
                                    fdmax = psock;
                                }
                                rMsg = marshall(&res, &size);

                                if (
                                        sendall(psock, rMsg,
                                                &size) == -1) {
                                    perror("send message");
                                }
//free buffers
                                free(rMsg);
                                FD_CLR(psock, &master);
                                close(psock);

                            } else if (responsible_succ) {
                                // TO DO Gero send reply to original creator

                                // modify the ring message by affecting the succesors infos (IP , ID and port) and reply bits and controls bits are set to 1
                                res.pReq->hashID = req->pReq->hashID;
                                res.pReq->nodeID = thisPeer.post->id;
                                res.pReq->nodePort = thisPeer.post->port;
                                res.pReq->nodeIP = thisPeer.post->ip;
                                res.pReq->type = REPLY;

                                //TO DO create a socket for the original peer , the one which is directly connected to client , create from the Preq params
                                //get char* of IP and Port

                                //create socket
                                int os = newSocket(req->pReq->nodeIP, req->pReq->nodeIP); //todo aiss

                                //TO DO send the modified msg to the original creator
                                rMsg = marshall(&res, &size);
                                sendall(os, rMsg, &size);
                                // after setting the socket , clear it and clear i
                                FD_CLR(os, &master);
                                close(os);
                            } else lookup(hash_id, thisPeer);
                        case CLIENT:
                            res.control = CLIENT;

                            switch (req->cReq->data->kLen) {
                                case 0:
                                    hash_id = 0;
                                    break;
                                case 1:
                                    hash_id = req->cReq->data->key[0] << 8;
                                    break;
                                default:
                                    hash_id = req->cReq->data->key[0] << 8 | req->cReq->data->key[1];
                                    break;
                            } //set hash_id
                            clsock_fd *fd = malloc(sizeof(clsock_fd));

                            //if ack, simpy redirect the response to client
                            if (req->cReq->ack == 1) {
                                res.cReq = handleClReq(req->cReq);
                                rMsg = marshall(&res, &size);
                                fd = getClSockFd(hash_id);
                                clsock = fd->sock_fd;
                                if (!FD_ISSET(clsock, &master)) {
                                    perror("Client SockFD");
                                }

                                if (
                                        sendall(clsock, rMsg,
                                                &size) == -1) {
                                    perror("send message");
                                }
                                FD_CLR(clsock, &master);
                                close(clsock);

                                // TODO  Aiss if ack bit =1 then handle clReq and send it to the Client socket which we must create

                            } else {
                                //handle request by client
                                //TODO Gero else if ack bit =0 then client socket =i and for every request (get delete set ) we handle the request then we clear the socket
                                // FD _ CLR then close
                                clsock = i;

                                responsible =
                                        (thisPeer.pre->id > thisPeer.id) &&
                                        (hash_id > thisPeer.pre->id || thisPeer.id > hash_id) ||
                                        (thisPeer.pre->id < thisPeer.id) &&
                                        (thisPeer.id > hash_id && thisPeer.pre->id < hash_id);

                                if (!responsible) {
                                    if (-1 == lookup(hash_id, thisPeer)) {
                                        perror("lookup");
                                    }
                                    //todo safe socket fd
                                    fd->hashID = hash_id;
                                    fd->sock_fd = clsock;
                                    addClSockFd(fd);

                                } else {
                                    res.cReq = handleClReq(req->cReq);
                                    //todo send client response
                                    if (req->cReq->type != SET) {
                                        free(req->cReq->data->value);
                                        free(req->cReq->data->key);
                                        free(req->cReq->data);
                                        free(req->cReq);
                                    }
                                    FD_SET(clsock, &master);

                                    rMsg = marshall(&res, &size);
                                    if (
                                            sendall(clsock, rMsg,
                                                    &size) == -1) {
                                        perror("send message");
                                    }
                                    FD_CLR(clsock, &master);
                                    close(clsock); //todo add thisPeer to other exits?
                                }
                            }
                    }
                } // END handle data from client
            } // END got new incoming connection
        } // END looping through file descriptors
    } // END for(;;)--and you thought it would never end!


    return 0;
}
/*
        sin_size = sizeof their_addr;
        new_fd = accept(sockfd, (struct sockaddr *) &their_addr, &sin_size);
        if (new_fd == -1) {
            perror("accept");
            continue;
        }

        inet_ntop(their_addr
                          .ss_family,
                  get_in_addr((struct sockaddr *) &their_addr),
                  s, sizeof s);
        printf("server: got connection from %s\n", s);
        /*

/*start handling request/ // Das soll in der große for Schleife sein , die vom Beej genommen war
/*  rnvsH res;
  uint64_t size;
  char *rMsg;

  rnvsH *req = demarshall(new_fd);
  if (NULL == req) {
      fwrite("unmarshall failed", sizeof(char), 17, stderr);
      exit(1);
  }
  switch (req->control) {
      case CLIENT:
          res.control = CLIENT;
          uint16_t hashID;
          switch (req->cReq->data->kLen) {
              case 0:
                  hashID = 0;
                  break;
              case 1:
                  hashID = req->cReq->data->key[0] << 8;
                  break;
              default:
                  hashID = req->cReq->data->key[0] << 8 | req->cReq->data->key[1];
                  break;
          }
          int responsible =
                  (thisPeer.pre->id > thisPeer.id) && (hashID > thisPeer.pre->id || thisPeer.id > hashID) ||
                  (thisPeer.pre->id < thisPeer.id) && (thisPeer.id > hashID && thisPeer.pre->id < hashID);

          if (!responsible) {
              if (-1 == lookup(hashID, thisPeer)) {
                  perror("lookup");
              }
              else
              //todo safe file descriptor in hash table select
          } else {
              res.cReq = handleClReq(req->cReq, thisPeer);  //todo reduce variables needed?

              if (req->cReq->type != SET) {
                  free(req->cReq->data->value);
                  free(req->cReq->data->key);
                  free(req->cReq->data);
                  free(req->cReq);
              }
              close(new_fd); //todo add thisPeer to other exits?
          }
          break;
      case PEER:
          res.control=PEER;
          uint16_t hash_id;


          break;
  }
  rMsg = marshall(&res, &size);

  if (
          sendall(new_fd, rMsg,
                  &size) == -1) {
      perror("send message");
  }
//free buffers
  free(rMsg);


}
}
*/
clientReq *handleClReq(clientReq *req) {

    clientReq *res = malloc(sizeof(rnvsH));
    res->data = NULL;

    res->ack = 0;

    switch (req->type) {
        case GET:
            printf("GET");
            res->data = getData(req->data);
            //seresres->type = GET;
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
    }//size of res
    return res;
} // return response for client


rnvsH *demarshall(int fd) { //handle both types of requests, set type in rnvs accordingly
    /*recv type */
    unsigned char head;
    int rHead = recv(fd, &head, 1, 0);
    if (rHead == 0) {
        printf("connection closed by host");
        exit(1);
    }
    if (rHead == -1) {
        perror("recv");
        exit(1);
    }

    /*check control bit */
    rnvsH *req = malloc(sizeof(rnvsH));
    uint8_t msB = 1;
    msB = msB << 8;
    if (head & msB) { //check control bit
        //demarshall peer request
        req->control = PEER;
        req->pReq = demarshallPeer(head, fd);
    } else {
        req->control = CLIENT;
        req->cReq = demarshallClient(head, fd);
    }
    return req;
}

peerReq *demarshallPeer(unsigned char top, int fd) {//demarshall peer request
    peerReq *req = malloc(sizeof(peerReq));
    //check for reply/lookup
    uint8_t lsb = 1;
    if (top & lsb) {
        req->type = LOOKUP;
    } else if (top & (lsb << 1)) {
        req->type = REPLY;
    }
    //receive hashID
    recv(fd, &req->hashID, sizeof(req->hashID), 0);
    recv(fd, &req->nodeID, sizeof(req->nodeID), 0);
    recv(fd, &req->nodeIP, sizeof(req->nodeID), 0);
    recv(fd, &req->nodePort, sizeof(req->nodePort), 0);

    return req;
}

clientReq *demarshallClient(unsigned char top, int fd) {//demarshall client request
    unsigned char head[HEADSIZE];    //Buffer for type
    int recvBytes = recv(fd, head + 1, HEADSIZE - 1, 0);
    if (recvBytes == 0) {
        printf("connection closed by host");
        exit(1);
    }
    if (recvBytes == -1) {
        perror("recv");
        exit(1);
    }
    if (recvBytes != HEADSIZE - 1) {
        fwrite("invalid type", sizeof(char), 12, stderr);
        exit(1);
    }
    head[0] = top;

    clientReq *req = malloc(sizeof(clientReq));
    req->data = malloc(sizeof(dataElement));
    //handle first byte

    unsigned char lsB = 1;
    char byte = head[0];
    req->type = (char) -1;
    for (int i = 0; i < 3; ++i) {
        if ((lsB & byte)) {
            if ((char) -1 == req->type) {
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
        short keyPointer = 0;
        for (short remBytes = req->data->kLen;
             remBytes > 0;) {
            short numbytes = recv(fd, keyPointer + req->data->key, remBytes, 0);
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
        long bufPointer = 0;
        for (long remBytes = req->data->vLen; remBytes > 0;) {
            long numbytes = recv(fd, req->data->value + bufPointer, remBytes, 0);

            if (numbytes == -1) {
                perror("recv");
                exit(1);
            }

            if (numbytes == 0) {
                printf("connection closed by host");
            }
            bufPointer += numbytes;
            remBytes -= numbytes;
        }
    }

    return req;
}


char *marshall(rnvsH *res, uint64_t *size) {

    char *msg;
    if (res->control == CLIENT) {
        msg = marshallClient(res->cReq, size);
    } else {
        msg = marshallPeer(res->pReq, size);
    }
    return msg;
}

char *marshallPeer(const peerReq *pReq, uint64_t *size) {
    *size = Pmsg;
    char *msg;
    char *head = calloc(sizeof(char), Pmsg);
    //set first head byte
    switch (pReq->type) {
        case LOOKUP:
            head[0] |= 1 << 0;
        case REPLY:
            head[0] |= 1 << 1;
    }
    head[0] |= 1 << 7;

    short hid = htons(pReq->hashID);
    head[1] |= hid;
    head[2] |= hid >> 8;
    short nid = htons(pReq->hashID);
    head[3] |= nid;
    head[4] |= nid >> 8;
    long nip = htonl(pReq->nodeIP);
    for (int i = 5; i < 8; ++i) {
        head[i] |= nip;
        nip |= nip >> 8;
    }
    short nport = htons(pReq->nodePort);
    head[9] |= nport;
    head[10] |= nport >> 8;
    msg = realloc(head, *size);
    return msg;

};

char *marshallClient(const clientReq *cReq, uint64_t *size) {
    *size = HEADSIZE;
    char *msg;
    unsigned char lsB = 1;
    char *head = calloc(sizeof(char), HEADSIZE);

    //set type bit
    head[0] = 0;
    lsB = lsB << cReq->type;
    head[0] |= lsB;
    lsB = lsB << (2 - cReq->type);
    //set acknowledge bit, todo evaluate necessity server side
    if (cReq->ack) {
        head[0] |= lsB << 1;
    }

    if (cReq->data == NULL) {
        return head;
    }
    *size = HEADSIZE + cReq->data->vLen + cReq->data->kLen;
    //marshal key length:
    short kLen = htons(cReq->data->kLen);
    head[1] |= kLen;
    head[2] |= kLen >> 8;

    //marshal value length
    long vLen = htonl(cReq->data->vLen);
    for (int i = 3; i < 7; ++i) {
        head[i] |= vLen;
        vLen = vLen >> 8;
    }

    msg = realloc(head, *size);
    for (int i = 0; i < cReq->data->kLen; ++i) {
        msg[i + HEADSIZE] = cReq->data->key[i];
    }
    for (int i = 0; i < cReq->data->vLen; ++i) {
        msg[i + HEADSIZE + cReq->data->kLen] = cReq->data->value[i];
    }
    return msg;
}

int addData(dataElement *e) {
    if (getData(e) != NULL) {
        fprintf(stderr, "key already in use, overwriting...");
        delData(e);
    }
    HASH_ADD_KEYPTR(hh, entries, e->key, e->kLen, e);
    return 0;
}

int delData(dataElement *element) {
    dataElement *b = getData(element);
    if (b == NULL) {
        return -1;
    }
    HASH_DEL(entries, b);
    free(b->value);
    free(b->key);
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
    dataElement *e;
    char key[element->kLen];
    for (int i = 0; i < element->kLen; ++i) {
        key[i] = element->key[i];
    }
    short klen = element->kLen;
    HASH_FIND(hh, entries, key, klen, e);
    return e;
}

clsock_fd *getClSockFd(uint16_t key) {
    clsock_fd *fd;
    HASH_FIND(hh, clsockfds, &key, sizeof(uint16_t), fd);
    return fd;
};

int addClSockFd(clsock_fd *fd) {
    if (getClSockFd(fd->hashID) != NULL) {
        fprintf(stderr, "key already in use, overwriting...");
        delClSockFd(fd);
    }
    HASH_ADD_KEYPTR(hh, clsockfds, &fd->hashID, sizeof(uint16_t), fd);
    return 0;
}

int delClSockFd(clsock_fd *fd) {
    clsock_fd *b = getClSockFd(fd->hashID);
    if (b == NULL) {
        return -1;
    }
    HASH_DEL(clsockfds, b);
    return 0;
}


int sendall(int s, char *buf, uint64_t *len) { //from beesguide
    uint64_t total = 0;        // how many bytes we've sent
    uint64_t bytesleft = *len; // how many we have left to send
    int n;
    while (total < *len) {
        if ((n = send(s, buf + total, bytesleft, 0)) == -1) {
            perror("send");
            break;
        }
        total += n;
        bytesleft -= n;
    }

    *len = total; // return number actually sent here

    return n == -1 ? -1 : 0; // return -1 on failure, 0 on success
}

int connectPeer(peer *peer1) {
    struct addrinfo hints, *res, *p;
    int status;
    char ipstr[INET6_ADDRSTRLEN];
    //fill out hints struct
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC; // AF_INET or AF_INET6 to force version
    hints.ai_socktype = SOCK_STREAM;

    networkChars n = getNetworkChars(peer1->ip, peer1->port);

    //get address infos
    if ((status = getaddrinfo(n.ip, n.port, &hints, &res)) != 0) {
        fprintf(stderr, "getaddrinfo: %sockfd\n", gai_strerror(status));
        return 2;
    }


    int sockfd;
    //loop through addresses, check for successful socket creation and connection
    for (p = res; p != NULL; p = p->ai_next) {
        //initialize socket
        if ((sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol)) == -1) {
            perror("client: socket");
            continue;
        }

        //connectPeer to remote host
        if (connect(sockfd, res->ai_addr, res->ai_addrlen) == -1) {
            perror("client: connectPeer");
            continue;
        }

        break;
    }

    if (p == NULL) {
        fprintf(stderr, "client: failed to connectPeer\n");
        return 2;
    }

    inet_ntop(p->ai_family, get_in_addr((struct sockaddr *) p->ai_addr), ipstr, sizeof ipstr);
    if (printV) {

        printf("client: connecting to %sockfd\n", ipstr);
    }

    freeaddrinfo(res); // free the linked list
    return sockfd;
}

int bindPeer(peer *peer1) {
    int sockfd;  // listen on sock_fd, new connection on new_fd
    struct addrinfo hints, *servinfo, *p;
    int yes = 1;
    int rv;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE; // use my IP

    if ((rv = getaddrinfo(peer1->ip, peer1->port, &hints, &servinfo)) != 0) {
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
            return -1;
        }

        if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            close(sockfd);
            perror("server: bind");
            continue;
        }

        break;
    }

    freeaddrinfo(servinfo); // all done with thisPeer structure

    if (p == NULL) {
        fprintf(stderr, "server: failed to bind\n");
        return -1;
    }

    struct sigaction sa;
    sa.sa_handler = sigchld_handler; // reap all dead processes
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        perror("sigaction");
        return -1;
    }

    /* int yes = 1;
     struct sockaddr_in thisPeerAdress;
     thisPeerAdress.sin_family = AF_INET;
     thisPeerAdress.sin_port = peer1->port;
     memset(thisPeerAdress.sin_zero, 0, 8);
     thisPeerAdress.sin_addr.s_addr = peer1->ip;

     if ((sockfd = socket(PF_INET, SOCK_STREAM,
                          IPPROTO_TCP)) == -1) {
         perror("server: socket");
         return -1;
     }

     if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes,
                    sizeof(int)) == -1) {
         perror("setsockopt");
         return -1;
     }

     if (bind(sockfd, ((struct sockaddr*)&thisPeerAdress), sizeof (thisPeerAdress)) == -1) {
         close(sockfd);
         perror("server: bind");
         return -1;
     }

     if (listen(sockfd, BACKLOG) == -1) {
         perror("listen");
         return -1;
     }*/

    return sockfd;
}

int lookup(uint16_t hashID, peer node) {
    rnvsH pLookup;
    pLookup.control = PEER;
    pLookup.pReq->hashID = hashID;
    pLookup.pReq->nodeID = node.id;
    pLookup.pReq->nodeIP = node.ip;
    pLookup.pReq->nodePort = node.port;
    uint64_t size;
    int fd = connectPeer(node.post);
    char *msg = marshall(&pLookup, &size);
    if (sendall(fd, msg, &size) == -1) {
        perror("lookup message");
        return -1;
    }
    return 0;
}

int newSocket(in_addr_t ip, in_port_t port) {
    char ipC[32];
    inet_ntop(AF_INET, &ip, ipC, 32);
    int length = snprintf(NULL, 0, "%d", port);
    char *portC = malloc(length + 1);
    snprintf(portC, length + 1, "%d", port);

    struct addrinfo hints, *res, *p;
    int s;
    int status;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC; // AF_INET or AF_INET6 to force version
    hints.ai_socktype = SOCK_STREAM;
    //get address infos
    if ((status = getaddrinfo(ipC, portC, &hints, &res)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(status));
        return 2;
    }
    for (p = res; p != NULL; p = p->ai_next) {
//initialize socket
        if ((s = socket(res->ai_family, res->ai_socktype, res->ai_protocol)) == -1) {
            perror("client: socket");
            continue;
        }

//connectPeer to remote host
        if (connect(s, res->ai_addr, res->ai_addrlen) == -1) {
            perror("client: connectPeer");
            continue;
        }

        break;
    }

    if (p == NULL) {
        fprintf(stderr, "client: failed to connectPeer\n");
        return 2;
    }
    return s;
}

networkChars getNetworkChars(in_addr_t ip, in_port_t port) {
    networkChars networkChars1;
    char ipC[32];
    inet_ntop(AF_INET, &ip, ipC, 32);
    int length = snprintf(NULL, 0, "%d", port);
    char *portC = malloc(length + 1);
    snprintf(portC, length + 1, "%d", port);
    networkChars1.port = portC;
    networkChars1.ip = ipC;
    return networkChars1;
};

#pragma clang diagnostic pop


