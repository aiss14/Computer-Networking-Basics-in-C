#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <math.h>

typedef struct _packet {
    uint32_t header;
    uint32_t root_delay;
    uint32_t root_dispersion;
    uint32_t reference_id;
    uint32_t reference_timestamp_s;
    uint32_t reference_timestamp_f;
    uint32_t origin_timestamp_s;
    uint32_t origin_timestamp_f;
    uint32_t receive_timestamp_s;
    uint32_t receive_timestamp_f;
    uint32_t transmit_timestamp_s;
    uint32_t transmit_timestamp_f;
    uint32_t value_len;
    struct _packet *next;
} packet;

typedef struct _timeStamps {
    struct timespec rootDelay;
    struct timespec rootDispersion;
    struct timespec t1;
    struct timespec t2;
    struct timespec t3;
    struct timespec t4;
    struct timespec clientDispersion;
    struct timespec clientDelay;
    packet *p;
    struct _timeStamps *next;
} timeStamps;

typedef struct _ntpserver {
    char *hostname;
    timeStamps *timeStamps1;
    struct _ntpserver *next;
} ntpserverResult;

ntpserverResult *ntp_init(const char *hostname) {
    ntpserverResult *n = calloc(1, sizeof(ntpserverResult));
    n->hostname = strdup(hostname);
    return n;
}

packet *packet_new() {
    packet *p = malloc(sizeof(packet));
    p->header = 0;
    p->root_delay = 0;
    p->root_dispersion = 0;
    p->reference_id = 0;
    p->reference_timestamp_s = 0;
    p->reference_timestamp_f = 0;

    p->origin_timestamp_s = 0;
    p->origin_timestamp_f = 0;

    p->receive_timestamp_s = 0;
    p->receive_timestamp_f = 0;

    p->transmit_timestamp_s = 0;
    p->transmit_timestamp_f = 0;
    return p;
}

#define VN_4 4 << 3
#define MODE_CLIENT 3

#define NTPOFFSET 2208988800

packet *packet_clientreq() {
    packet *p = malloc(sizeof(packet));
    p->header = 0 | VN_4 | MODE_CLIENT;
    p->root_delay = 0;
    p->root_dispersion = 0;
    p->reference_id = 0;
    p->reference_timestamp_s = 0;
    p->origin_timestamp_s = 0;
    p->receive_timestamp_s = 0;
    p->transmit_timestamp_s = 0;
    p->value_len = 0;
    return p;
}

packet *packet_decode(const unsigned char *buffer, size_t buf_len) {
    packet *p = packet_new();
    p->origin_timestamp_s = (buffer[31] << 24u) |
                            (buffer[30] << 16u) |
                            (buffer[29] << 8u) |
                            (buffer[28] << 0u);
    p->origin_timestamp_f = (buffer[27] << 24u) |
                            (buffer[26] << 16u) |
                            (buffer[25] << 8u) |
                            (buffer[24] << 0u);


    p->receive_timestamp_s = (buffer[39] << 24u) |
                             (buffer[38] << 16u) |
                             (buffer[37] << 8u) |
                             (buffer[36] << 0u);
    p->receive_timestamp_f =
            (buffer[35] << 24u) |
            (buffer[34] << 16u) |
            (buffer[33] << 8u) |
            (buffer[32] << 0u);

    p->transmit_timestamp_s = (buffer[47] << 25u << 31u) |
                              (buffer[46] << 17u << 31u) |
                              (buffer[45] << 9 << 31u) |
                              (buffer[44] << 1u << 31u) |
                              (buffer[43] << 24u) |
                              (buffer[42] << 16u) |
                              (buffer[41] << 8u) |
                              (buffer[40] << 0u);
    return p;
}

unsigned char *packet_encodeClientreq() {
    size_t packet_size = 16 * 4;
    unsigned char *buffer = calloc(packet_size, sizeof(unsigned char));
    buffer[0] |= VN_4 | MODE_CLIENT;
    return buffer;
}


struct timespec getRTT(timeStamps stamps) {
    struct timespec rtt;
    rtt.tv_sec = stamps.t4.tv_sec - stamps.t1.tv_sec - stamps.t3.tv_sec + stamps.t2.tv_sec;
    rtt.tv_nsec = stamps.t4.tv_nsec - stamps.t1.tv_nsec - stamps.t3.tv_nsec + stamps.t2.tv_nsec;
    return rtt;
}

struct timespec getOffset(timeStamps stamps) {
    struct timespec offset;
    offset.tv_sec = (stamps.t2.tv_sec - stamps.t1.tv_sec - stamps.t4.tv_sec + stamps.t3.tv_sec) / 2;
    offset.tv_nsec = (stamps.t2.tv_nsec - stamps.t1.tv_nsec - stamps.t4.tv_nsec + stamps.t3.tv_nsec) / 2;
    return offset;
}

struct timespec getDispersion(ntpserverResult *serverResults) {
    struct timespec maxRTT = getRTT(*serverResults->timeStamps1);
    struct timespec minRTT = getRTT(*serverResults->timeStamps1);
    struct timespec disp = getRTT(*serverResults->timeStamps1);;

    for (timeStamps *stamps = serverResults->timeStamps1; stamps != 0; stamps = stamps->next) {
        struct timespec rtt = getRTT(*stamps);

        if (minRTT.tv_sec == rtt.tv_sec) {
            if (minRTT.tv_nsec > rtt.tv_nsec) {
                minRTT = rtt;
            }
        } else if (minRTT.tv_sec > rtt.tv_sec) {
            minRTT = rtt;
        }

        if (maxRTT.tv_sec == rtt.tv_sec) {
            if (maxRTT.tv_nsec < rtt.tv_nsec) {
                maxRTT = rtt;
            }
        } else if (maxRTT.tv_sec < rtt.tv_sec) {
            maxRTT = rtt;
        }
    }
    disp.tv_sec = maxRTT.tv_sec - minRTT.tv_sec;
    disp.tv_nsec = maxRTT.tv_nsec - minRTT.tv_nsec;
    return disp;
}

int calculateResults(ntpserverResult *tmpNTPres, timeStamps *results, packet *tmpPacket);

int main(int argc, char **argv) {

    //parse args
    if (argc < 3) {
        fprintf(stderr, "Not enough args!\n");
    }

    int n = atoi(argv[1]);

    ntpserverResult *tmpNTPres = ntp_init(argv[2]);
    ntpserverResult *headNTPres = tmpNTPres;
    for (int i = 3; i < argc; ++i) {
        tmpNTPres->next = ntp_init(argv[i]);
        tmpNTPres = tmpNTPres->next;
        tmpNTPres->next = NULL;
    }
    int sockfd, c = 0;
    for (tmpNTPres = headNTPres; tmpNTPres != NULL; tmpNTPres = tmpNTPres->next) {

        //create socket
        struct addrinfo hints, *res, *p;
        char ipstr[INET6_ADDRSTRLEN];
        char *ipver;

        memset(&hints, 0, sizeof hints); // make sure the struct is empty
        hints.ai_family = AF_UNSPEC;     // don't care IPv4 or IPv6
        hints.ai_socktype = SOCK_DGRAM; // TCP stream sockets
        hints.ai_flags = AI_PASSIVE;
        int status;
        if (0 != (status = getaddrinfo(tmpNTPres->hostname, "123", &hints, &res))) {
            fprintf(stderr, "getaddrinfo error: %sockfd\n", gai_strerror(status));
            exit(1);
        }

        for (p = res; p != NULL; p = p->ai_next) {
            void *addr;

            // get the pointer to the address itself,
            // different fields in IPv4 and IPv6:
            if (p->ai_family == AF_INET) { // IPv4
                struct sockaddr_in *ipv4 = (struct sockaddr_in *) p->ai_addr;
                addr = &(ipv4->sin_addr);
                ipver = "IPv4";
            } else { // IPv6
                struct sockaddr_in6 *ipv6 = (struct sockaddr_in6 *) p->ai_addr;
                addr = &(ipv6->sin6_addr);
                ipver = "IPv6";
            }

            // convert the IP to a string and print it:
            inet_ntop(p->ai_family, addr, ipstr, sizeof ipstr);
//            printf("trying  %s: %s\n", ipver, ipstr);

            //try connection
            if (-1 == (sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol))) {
                continue;
            }
/*
            if (-1 == bind(sockfd, p->ai_addr, p->ai_addrlen)) {
                continue;
            }*/

            if (-1 == (status = connect(sockfd, p->ai_addr, p->ai_addrlen))) {
                continue;
            }

            break;
        } //connect to server
//        printf("Connected to:  %s: %s\n", ipver, ipstr);
        tmpNTPres->timeStamps1 = calloc(1, sizeof(timeStamps));
        timeStamps *results = tmpNTPres->timeStamps1;
        size_t len = 12 * 4;
        packet *tmpPacket = packet_new();
        unsigned char *msg;
        for (int i = 0; i < n; ++i) {
            msg = packet_encodeClientreq();

            clock_gettime(CLOCK_REALTIME, &results->t1);
            if (-1 == send(sockfd, msg, len, 0)) {
                perror("sendto");
            }
            char buffer[48];
            while (48!= recv(sockfd, &buffer, len, 0));
            tmpPacket = (packet*)&buffer;
            clock_gettime(CLOCK_REALTIME, &results->t4);

            calculateResults(tmpNTPres, results, tmpPacket);

            if (i < n - 1) {
                results->next = calloc(1, sizeof(timeStamps)); //todo move this up, avoid if not needed
                results = results->next;
                results->next = NULL;
            }

            sleep(8);
        }
//        tmpPacket = NULL;
//        results = NULL;

        close(sockfd);
//        printf("Done with server nr %i\n", c++);
//        freeaddrinfo(res);
    }

    for (tmpNTPres = headNTPres; tmpNTPres != 0; tmpNTPres = tmpNTPres->next) {
        int i = 0;
        for (timeStamps *results = tmpNTPres->timeStamps1; results != 0; results = results->next) {
            //calculate results here
            struct timespec offset = getOffset(*results);
//            printf("T1 %lld.%.9ld \n T2 %lld.%.9ld \n T3 %lld.%.9ld \n T4 %lld.%.9ld \n", (long long)results->t1.tv_sec, results->rootDispersion.tv_nsec) //todo complete for debugging
            printf("%s; %i; %lld.%.9ld; %lld.%.9ld; %lld.%.9ld; %lld.%.9ld\n", tmpNTPres->hostname, i, (long long)results->rootDispersion.tv_sec, results->rootDispersion.tv_nsec, (long long)results->clientDispersion.tv_sec, results->clientDispersion.tv_nsec, (long long)results->clientDelay.tv_sec, results->clientDelay.tv_nsec, (long long)offset.tv_sec, offset.tv_nsec);
            i++;
        }
    }
    //send to server 8 times
    //recv
    // clock\_gettime
    // calculate delay and dispersion
    // ptiny host; n; root_disp; disp; delay; offset;
    return 0;
}

int calculateResults(ntpserverResult *tmpNTPres, timeStamps *results, packet *tmpPacket) {
    uint32_t rootdispersion = ntohl(tmpPacket->root_dispersion);
    results->rootDispersion.tv_sec = rootdispersion >> 16;
    uint64_t rdf = 0x0000FFFF&rootdispersion;
    rdf *= 1000000000;
    rdf >>= 16;
    results->rootDispersion.tv_nsec = rdf;

    uint32_t rootdelay = ntohl(tmpPacket->root_delay);
    results->rootDelay.tv_sec = rootdelay >> 16;
    uint64_t rdlf = 0x0000FFFF&rootdelay;
    rdlf *= 1000000000;
    rdlf >>= 16;
    results->rootDelay.tv_nsec = rdlf;


    results->t2.tv_sec = ntohl(tmpPacket->receive_timestamp_s) - NTPOFFSET;
    uint64_t rtf = ntohl(tmpPacket->receive_timestamp_f);
    rtf *= 1000000;
    rtf >>= 32;
    results->t2.tv_nsec = rtf;

    results->t3.tv_sec = ntohl(tmpPacket->transmit_timestamp_s) - NTPOFFSET;
    uint64_t ttf = ntohl(tmpPacket->transmit_timestamp_f);
    ttf *= 1000000;
    ttf >>= 32;
    results->t3.tv_nsec = ttf;

    results->p = tmpPacket;
//            tmpPacket->next = packet_new();
//            tmpPacket = tmpPacket->next;
/*  if (-1 == recvfrom(sockfd, buf, len, 0, (struct sockaddr*)&their_addr, &their_addr)){
      perror("recvfrom");
  }*/
    results->clientDispersion = getDispersion(tmpNTPres);
    struct timespec delay = getDispersion(tmpNTPres);
    delay.tv_nsec = delay.tv_nsec/2;
    delay.tv_sec = delay.tv_sec/2;
    results->clientDelay = delay;

    return 0;
}
