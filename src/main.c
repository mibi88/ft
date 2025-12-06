/* ft -- A basic file transfer utility.
 * by Mibi88
 *
 * This software is licensed under the BSD-3-Clause license:
 *
 * Copyright 2025 Mibi88
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 * contributors may be used to endorse or promote products derived from this
 * software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/* TODO: Make the code work without this hack. */
#define _POSIX_C_SOURCE 200112L

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <unistd.h>

#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <hash.h>

#ifdef AF_INET6
#define DOMAIN (force_ipv6 ? AF_INET6 : (force_ipv4 ? AF_INET : AF_UNSPEC))
#else
#define DOMAIN AF_INET
#endif

#define MAGIC "FTv1"

static const char help[] = "USAGE: %s [-hp46] [-s hostname] [files...]\n"
"\n"
"ft -- A basic file transfer utility.\n"
"\n"
"  -s   Set the IP address to send files to.\n"
"  -p   Show progess.\n"
"  -y   Accept all.\n"
"  -4   Force IPv4.\n"
"  -6   Force IPv6.\n"
"  -h   Show this help.\n";

static char *port = "8000";
static char *dest = NULL;

static int receive = 1;
static int progress = 0;
static int acceptall = 0;
static int force_ipv6 = 0;
static int force_ipv4 = 0;

static int socket_fd;

static int fd;

static char *name;

static unsigned char file_next(void *_data){
    unsigned char c;

    (void)_data;

    /* TODO: Don't read and send the file byte per byte. */

    if(read(fd, &c, 1) < 1){
        fprintf(stderr, "%s: Read error!\n", name);
        close(socket_fd);
        close(fd);
        exit(EXIT_FAILURE);
    }

    if(send(socket_fd, &c, 1, 0) < 1){
        fprintf(stderr, "%s: Send error!\n", name);
        close(socket_fd);
        close(fd);
        exit(EXIT_FAILURE);
    }

    return c;
}

#define LOAD_U64_LOOP(l, b, u) l(b, u, 0); \
                               l(b, u, 1); \
                               l(b, u, 2); \
                               l(b, u, 3); \
                               l(b, u, 4); \
                               l(b, u, 5); \
                               l(b, u, 6); \
                               l(b, u, 7)
#define LOAD_U64_LOAD(b, u, i) b[i] = (u>>((7-i)*8))&0xFF
#define LOAD_U64(b, u)         LOAD_U64_LOOP(LOAD_U64_LOAD, (b), (u))

#define LOAD_U32_LOOP(l, b, u) l(b, u, 0); \
                               l(b, u, 1); \
                               l(b, u, 2); \
                               l(b, u, 3);
#define LOAD_U32_LOAD(b, u, i) b[i] = (u>>((3-i)*8))&0xFF
#define LOAD_U32(b, u)         LOAD_U32_LOOP(LOAD_U32_LOAD, (b), (u))

static void send_file(char *file) {
    static word_t hash[8];

    struct stat statbuf;

    off_t size;

    size_t i;

    struct addrinfo *addr;

    struct addrinfo *current;

    struct addrinfo hints = {
        0,              /* ai_flags */
        AF_UNSPEC,      /* ai_family (set later) */
        SOCK_STREAM,    /* ai_socktype */
        0,              /* ai_protocol (allow any protocol) */
        0,              /* ai_addrlen */
        NULL,           /* ai_addr */
        NULL,           /* ai_canonname */
        NULL            /* ai_next */
    };

    int err;

    unsigned char buffer[4*8+1];

    hints.ai_family = DOMAIN;

    if((err = getaddrinfo(dest, port, &hints, &addr))){
        fprintf(stderr, "%s: getaddrinfo failed with error `%s'!\n", name,
                gai_strerror(err));
        exit(EXIT_FAILURE);
    }

    for(current=addr;current!=NULL;current=current->ai_next){
        socket_fd = socket(current->ai_family, current->ai_socktype,
                           current->ai_protocol);

        if(socket_fd < 0) continue;

        if(!connect(socket_fd, current->ai_addr, current->ai_addrlen)) break;

        close(socket_fd);
    }

    freeaddrinfo(addr);

    if(current == NULL){
        fprintf(stderr, "%s: Failed to connect!\n", name);
        exit(EXIT_FAILURE);
    }

    fd = open(file, O_RDONLY);
    if(fd < 0){
        fprintf(stderr, "%s: Failed to open `%s'!\n", name, file);
        close(socket_fd);
        exit(EXIT_FAILURE);
    }
    fstat(fd, &statbuf);

    if(statbuf.st_mode&S_IXUSR){
        puts("Execute bit set!");
    }

    /* Get the file size */
    size = lseek(fd, 0, SEEK_END);
    if(size < 0){
        fprintf(stderr, "%s: Failed to seek to the end of `%s'...\n", name,
                file);
        close(fd);
        close(socket_fd);
        exit(EXIT_FAILURE);
    }

    if(lseek(fd, 0, SEEK_SET) < 0){
        fprintf(stderr, "%s: Failed to seek to the start of `%s'...\n", name,
                file);
        close(fd);
        close(socket_fd);
        exit(EXIT_FAILURE);
    }

    /* Send some file metadata. */

    if(send(socket_fd, MAGIC, sizeof(MAGIC), 0) < 1){
        fprintf(stderr, "%s: Send error!\n", name);
        close(socket_fd);
        close(fd);
        exit(EXIT_FAILURE);
    }

    LOAD_U64(buffer,    statbuf.st_atime);
    LOAD_U64(buffer+8,  statbuf.st_mtime);
    LOAD_U64(buffer+16, statbuf.st_ctime);
    LOAD_U64(buffer+24, size);
    buffer[4*8] = (statbuf.st_mode&S_IXUSR) != 0;

    if(send(socket_fd, buffer, sizeof(buffer), 0) < 1){
        fprintf(stderr, "%s: Send error!\n", name);
        close(socket_fd);
        close(fd);
        exit(EXIT_FAILURE);
    }

    if(send(socket_fd, file, strlen(file)+1, 0) < 1){
        fprintf(stderr, "%s: Send error!\n", name);
        close(socket_fd);
        close(fd);
        exit(EXIT_FAILURE);
    }

    /* Hash and send the file. */
    sha256_fnc(hash, file_next, size, &fd);

    printf("File `%s' (%lu bytes) sent successfully!\n"
           "SHA-256 checksum: ", file, size);
    for(i=0;i<8;i++){
        printf("%08lx", hash[i]);
        LOAD_U32(buffer+i*4, hash[i]);
    }
    fputc('\n', stdout);

    if(send(socket_fd, buffer, 8*4, 0) < 1){
        fprintf(stderr, "%s: Send error!\n", name);
        close(socket_fd);
        close(fd);
        exit(EXIT_FAILURE);
    }

    close(fd);
    close(socket_fd);
}

int main(int argc, char **argv) {
    extern int optind;
    extern int optopt;
    int c;

    size_t i;

    name = *argv;

    while((c = getopt(argc, argv, "hpys:46")) > 0){
        switch(c){
            case 'h':
                printf(help, *argv);
                break;
            case 'p':
                progress = 1;
                break;
            case 'y':
                acceptall = 1;
                break;
            case 's':
                if(dest != NULL){
                    fprintf(stderr, "%s: Destination hostname set twice!\n",
                            *argv);
                    return EXIT_FAILURE;
                }
                dest = optarg;
                receive = 0;
                break;
            case '4':
                force_ipv4 = 1;
                break;
            case '6':
                force_ipv6 = 1;
                break;
            default:
                break;
        }
    }

    if(argv[optind] == NULL || (!receive && dest == NULL)){
        fprintf(stderr, help, *argv);
        return EXIT_FAILURE;
    }

    for(i=0;argv[optind][i];i++){
        if(argv[optind][i] < '0' || argv[optind][i] >= '9'){
            fprintf(stderr, "%s: Invalid port number!\n", *argv);
            return EXIT_FAILURE;
        }
    }
    port = argv[optind++];

    for(;argv[optind];optind++){
        if(!receive){
            printf("Sending `%s'...\n", argv[optind]);
            send_file(argv[optind]);
        }
    }

    return EXIT_SUCCESS;
}
