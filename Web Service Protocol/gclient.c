/**Client structure (syscalls and error checking) is from Beej's Guide to Network Programming : http://beej.us/guide/bgnet/
*/


#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <arpa/inet.h>

#define BUFFERSIZE 32
#define HEADSIZE 7
#define DELETE 0
#define SET 1
#define GET 2
/**
 * docode char buffer, save and allocate data in rnvsH
 * @param head buffer as received from client
 * @oaram req head structure
 * @retval 0 success
 * @retval -1 error
 */
typedef struct dataElement {
    short int kLen;
    long int vLen;
   unsigned char *key;
   unsigned char *value;
} dataElement;

typedef struct rnvsH {
     char type;
     char ack;
    dataElement *data;
} rnvsH;


struct dataElement *entries = NULL;

rnvsH *demarshall(unsigned char *head, int fd);

unsigned char *marshall(struct rnvsH *res, uint64_t *size);

int sendall(int s, unsigned char *buf, uint64_t *len);


/**
 * searches array for data, saves it to element
 * @param head hashmap
 * @param keyptr
 * @param keylen
 * @return
 */
int readData(unsigned char **value, long int *readSize);

int testData(char **value, long int *readSize);


void *get_in_addr(struct sockaddr *sa) {
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in *) sa)->sin_addr);
    }

    return &(((struct sockaddr_in6 *) sa)->sin6_addr);
}

int main(int argc, char *argv[]) {
    struct addrinfo hints, *res, *p;
    int status;
    char ipstr[INET6_ADDRSTRLEN];

    //check arguments
    if (argc != 5) {
        fprintf(stderr, "usage: hostname, port, Type\n");
        return 1;
    }

    //fill out hints struct
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC; // AF_INET or AF_INET6 to force version
    hints.ai_socktype = SOCK_STREAM;

    //get address infos
    if ((status = getaddrinfo(argv[1], argv[2], &hints, &res)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(status));
        return 2;
    }


    int s;
    //loop through addresses, check for successful socket creation and connection
    for (p = res; p != NULL; p = p->ai_next) {
        //initialize socket
        if ((s = socket(res->ai_family, res->ai_socktype, res->ai_protocol)) == -1) {
            perror("client: socket");
            continue;
        }

        //connect to remote host
        if (connect(s, res->ai_addr, res->ai_addrlen) == -1) {
            perror("client: connect");
            continue;
        }

        break;
    }

    if (p == NULL) {
        fprintf(stderr, "client: failed to connect\n");
        return 2;
    }

    inet_ntop(p->ai_family, get_in_addr((struct sockaddr *) p->ai_addr), ipstr, sizeof ipstr);
//    printf("client: connecting to %s\n", ipstr);

    freeaddrinfo(res); // free the linked list


    /**send request*/
//    printf("client: received:\n");
    rnvsH *req = malloc(sizeof(rnvsH));
    req->ack = 0;
    req->data = malloc(sizeof(dataElement));
    req->data->kLen = (short) strlen(argv[4]);
    req->data->key = malloc(sizeof(char) * req->data->kLen);
    req->data->vLen = 0;
    memcpy(req->data->key, argv[4], req->data->kLen);

    if (0 == strcmp(argv[3], "GET")) {
        req->type = GET;

    } else if (0 == strcmp(argv[3], "SET")) {
        req->type = SET;
        readData(&req->data->value, &req->data->vLen);
//        testData(&req->data->value, &req->data->vLen);
    } else if (0 == strcmp(argv[3], "DELETE")) {
        req->type = DELETE;
    } else {
        fprintf(stderr, "Invalid request type \"%s\"", argv[4]);
    }

    uint64_t *size = malloc(sizeof(uint64_t));
    unsigned char *msg = marshall(req, size);

    if (sendall(s, msg, size) == -1) {
        perror("send message");
    }


    /**receive response*/
    unsigned char hBuf[HEADSIZE];    //Buffer for head
    int hbytes = recv(s, hBuf, HEADSIZE, 0); //todo for-loop?
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

    rnvsH *rMsg = demarshall(hBuf, s);
    if (NULL == req) {
        fwrite("demarshall failed", sizeof(char), 17, stderr);
        exit(1);
    }
    int get = 0, set = 0, del = 0;
    switch (rMsg->type) {
        case GET:
            get = 1;
            break;
        case SET:
            set = 1;
            break;
        case DELETE:
            del = 1;
            break;
        default:
            break;
    }
    //    printf("Decoded packet header: \n");
    /*  printf("\tACK: %i\n", rMsg->ack);
      printf("\tGET: %i\n", get);
      printf("\tSET: %i\n", set);
      printf("\tDEL: %i\n", del);
      printf("\tKey Length: %i Bytes\n", rMsg->data->kLen);
      printf("\tValue Length: %li Bytes", rMsg->data->vLen);
      if (!rMsg->ack) {
          printf("\nServer did not acknowledge operation!");
      } else {
          printf("\n");
      }*/
    if (rMsg->data->vLen > 0) {
        fwrite(rMsg->data->value, rMsg->data->vLen, 1,
               stdout); //add correct printing of 0 bytes / remove termination by 0 bytes
//        fprintf(stdout, "\nsize: %ld", rMsg->data->vLen);
    }
    close(s);
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
    long numbytes = 0;
    if (req->data->vLen > 0) {
        for (long remBytes = req->data->vLen; remBytes > numbytes;) {
            numbytes += recv(fd, req->data->value + numbytes, remBytes, 0);

            if (numbytes == -1) {
                perror("recv");
                exit(1);
            }

            if (numbytes == 0) {
                printf("connection closed by host");
            }
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
int readData(unsigned char **value, long int *readSize) {
    int bufsize = BUFFERSIZE;
    char *buf = calloc(bufsize, sizeof(char));
    char *tempbuf;
    long c = 0;
    char k;
    while (1) {
        k = (char) fgetc(stdin);
        if (k == EOF) { //reached end of file
            break;
        } else {
            if (c < bufsize - 1) { //check if buffer is full
                buf[c] = k; //save char in buffer
                c++;
            } else {
                bufsize += BUFFERSIZE; //increment buf-size
                tempbuf = realloc(buf, bufsize * sizeof(char)); //expands buffer by reallocating with new bufSize
                if (tempbuf == NULL) {
                    perror("Realloc failed");
                    return -1;
                }
                buf = tempbuf;
                //proceed reading
                buf[c] = k;
                c++;
            }
        }
    }
    *value = realloc(buf, c);
    *readSize = c;
    return 0;

}

int testData(char **value, long int *readSize) {
    long int size = 3;
    char *k = malloc(size * sizeof(char));
    k[0] = 'h';
    k[1] = '\0';
    k[2] = 'z';
    *value = realloc(k, sizeof(char) * size);
    *readSize = size;
    return 0;
}

int sendall(int s, unsigned char *buf, uint64_t *len) { //from beesguide
    int total = 0;        // how many bytes we've sent
    uint64_t bytesleft = *len; // how many we have left to send
    int n = 0;

    while (total < *len) {
        n = send(s, buf +total, bytesleft, 0);
        if (n == -1) { break; }
        total += n;
        bytesleft -= n;
    }

//    *len = total; // return number actually sent here

    return n == -1 ? -1 : 0; // return -1 on failure, 0 on success
}