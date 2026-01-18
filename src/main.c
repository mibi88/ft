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

#include <signal.h>

#include <utime.h>

#include <time.h>

#ifdef AF_INET6
#define DOMAIN (force_ipv6 ? AF_INET6 : (force_ipv4 ? AF_INET : AF_UNSPEC))
#else
#define DOMAIN AF_INET
#endif

#define MAGIC "FTv1"

#define BACKLOG_MAX 128

#define PROGRESS_W 40

#define BUFFER_MAX 1024

static const char help[] =
"USAGE: %s [-hpy46] [-s hostname] port [files...]\n"
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

static int client_fd;

static char *name;

static size_t bytes;
static size_t total_bytes;

static unsigned char data_buf[BUFFER_MAX];
static size_t data_buf_size;

static void show_progress(size_t v, size_t on, int w) {
    int n = v*w/on;
    int i;

    fputs("\033[1K[", stdout);
    for(i=0;i<n;i++) fputc('=', stdout);
    for(i=n;i<w;i++) fputc('-', stdout);
    printf("] %luB/%luB\r", v, on);
}

typedef int sock_fnc_t(int, const struct sockaddr *, socklen_t);

static void open_socket(sock_fnc_t call, int passive) {
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

    hints.ai_family = DOMAIN;
    if(passive) hints.ai_flags = AI_PASSIVE;

    if((err = getaddrinfo(dest, port, &hints, &addr))){
        fprintf(stderr, "%s: getaddrinfo failed with error `%s'!\n", name,
                gai_strerror(err));
        exit(EXIT_FAILURE);
    }

    for(current=addr;current!=NULL;current=current->ai_next){
        socket_fd = socket(current->ai_family, current->ai_socktype,
                           current->ai_protocol);

        if(socket_fd < 0) continue;

        if(!call(socket_fd, current->ai_addr, current->ai_addrlen)){
            break;
        }

        close(socket_fd);
    }

    freeaddrinfo(addr);

    if(current == NULL){
        fprintf(stderr, "%s: Failed to connect!\n", name);
        exit(EXIT_FAILURE);
    }
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

#define UNLOAD_U32(u, b) u = (((unsigned long int)(b)[0]<<24)| \
                              ((unsigned long int)(b)[1]<<16)| \
                              ((unsigned long int)(b)[2]<<8)| \
                              (unsigned long int)(b)[3])
#define UNLOAD_U64(u, b) u = (((unsigned long int)(b)[0]<<7*8)| \
                              ((unsigned long int)(b)[1]<<6*8)| \
                              ((unsigned long int)(b)[2]<<5*8)| \
                              ((unsigned long int)(b)[3]<<4*8)| \
                              ((unsigned long int)(b)[4]<<3*8)| \
                              ((unsigned long int)(b)[5]<<2*8)| \
                              ((unsigned long int)(b)[6]<<8)| \
                              (unsigned long int)(b)[7])

static word_t hash[8];

#define SEND(fd, b, l, f) \
    { \
        if(send(fd, b, l, f) != (ssize_t)(l)){ \
            fprintf(stderr, "%s: Send error!\n", name); \
            close(socket_fd); \
            close(fd); \
            exit(EXIT_FAILURE); \
        } \
    }

#define RECV(fd, b, l, f, is_fd_open) \
    { \
        if(recv(fd, b, l, f) != (ssize_t)(l)){ \
            fprintf(stderr, "%s: Receive error!\n", name); \
            close(socket_fd); \
            close(client_fd); \
            if(is_fd_open) close(fd); \
            exit(EXIT_FAILURE); \
        } \
    }

ssize_t read_size = 0;

static unsigned char send_next(void *_data){
    (void)_data;

    if(!data_buf_size){
        read_size = total_bytes-bytes > BUFFER_MAX ?
                    BUFFER_MAX : total_bytes-bytes;

        if(read(fd, data_buf, read_size) < read_size){
            if(progress) fputc('\n', stdout);
            fprintf(stderr, "%s: Read error!\n", name);
            close(socket_fd);
            close(fd);
            exit(EXIT_FAILURE);
        }

        if(send(socket_fd, data_buf, read_size, 0) < read_size){
            if(progress) fputc('\n', stdout);
            fprintf(stderr, "%s: Send error!\n", name);
            close(socket_fd);
            close(fd);
            exit(EXIT_FAILURE);
        }

        bytes += read_size;

        if(progress){
            show_progress(bytes, total_bytes, PROGRESS_W);
        }

        data_buf_size = read_size;
    }

    return data_buf[read_size-(data_buf_size--)];
}

static void send_file(char *file) {
    struct stat statbuf;

    off_t size;

    size_t i;

    unsigned char buffer[4*8+1];

    data_buf_size = 0;

    fd = open(file, O_RDONLY);
    if(fd < 0){
        fprintf(stderr, "%s: Failed to open `%s'!\n", name, file);
        exit(EXIT_FAILURE);
    }
    fstat(fd, &statbuf);

    if(!S_ISREG(statbuf.st_mode)){
        fprintf(stderr, "%s: `%s' is not a regular file...\n", name, file);
        close(fd);
        exit(EXIT_FAILURE);
    }

    if(statbuf.st_mode&S_IXUSR){
        puts("Execute bit set!");
    }

    /* Get the file size */
    size = lseek(fd, 0, SEEK_END);
    if(size < 0){
        fprintf(stderr, "%s: Failed to seek to the end of `%s'...\n", name,
                file);
        close(fd);
        exit(EXIT_FAILURE);
    }

    if(lseek(fd, 0, SEEK_SET) < 0){
        fprintf(stderr, "%s: Failed to seek to the start of `%s'...\n", name,
                file);
        close(fd);
        exit(EXIT_FAILURE);
    }

    open_socket(connect, 0);

    /* Send some file metadata. */

    SEND(socket_fd, MAGIC, sizeof(MAGIC), 0);

    LOAD_U64(buffer,    statbuf.st_atime);
    LOAD_U64(buffer+8,  statbuf.st_mtime);
    LOAD_U64(buffer+16, statbuf.st_ctime);
    LOAD_U64(buffer+24, size);
    buffer[4*8] = (statbuf.st_mode&S_IXUSR) != 0;

    SEND(socket_fd, buffer, sizeof(buffer), 0);

    SEND(socket_fd, file, strlen(file)+1, 0);

    /* Hash and send the file. */

    total_bytes = size;
    bytes = 0;

    if(progress){
        show_progress(bytes, total_bytes, PROGRESS_W);
    }
    sha256_fnc(hash, send_next, size, NULL);
    if(progress) fputc('\n', stdout);

    printf("File `%s' (%lu bytes) sent successfully!\n"
           "SHA-256 checksum: ", file, size);
    for(i=0;i<8;i++){
        printf("%08lx", hash[i]);
        LOAD_U32(buffer+i*4, hash[i]);
    }
    fputc('\n', stdout);

    SEND(socket_fd, buffer, 8*4, 0);

    close(fd);
    close(socket_fd);
}

static char *basename(char *name) {
    size_t len = strlen(name);

    size_t i;

    if(!len) return name;
    if(len == 1){
        return name[0] == '/' ? name+1 : name;
    }

    for(i=len-1;i ? i-- : i;){
        if(name[i] == '/') return name+i+1;
    }

    return name;
}

#include <errno.h>

static unsigned char receive_next(void *_data){
    ssize_t received;

    (void)_data;

    if(!data_buf_size){
        read_size = total_bytes-bytes > BUFFER_MAX ?
                    BUFFER_MAX : total_bytes-bytes;

        if((received = recv(client_fd, data_buf, read_size, 0)) < 1){
            if(progress) fputc('\n', stdout);
            fprintf(stderr, "%s: Receive error!\n", name);
            close(socket_fd);
            close(client_fd);
            close(fd);
            exit(EXIT_FAILURE);
        }

        read_size = received;

        if(write(fd, data_buf, read_size) < read_size){
            if(progress) fputc('\n', stdout);
            fprintf(stderr, "%s: Write error!\n", name);
            close(socket_fd);
            close(client_fd);
            close(fd);
            exit(EXIT_FAILURE);
        }

        bytes += read_size;

        if(progress){
            show_progress(bytes, total_bytes, PROGRESS_W);
        }

        data_buf_size = read_size;
    }

    return data_buf[read_size-(data_buf_size--)];
}

void receive_file(void) {
    static char filename[FILENAME_MAX];
    static char *outname;
    unsigned char buffer[4*8+1];
    time_t atime;
    time_t mtime;
    time_t ctime;

    size_t size;

    unsigned char exec_bit;

    size_t i;

    struct stat statbuf;

    int accept = 0;

    struct utimbuf timestamps;

    data_buf_size = 0;

    RECV(client_fd, buffer, sizeof(MAGIC), 0, 0);

    if(memcmp((unsigned char*)MAGIC, buffer, 4)){
        fprintf(stderr, "%s: Invalid magic number!\n", name);
        close(client_fd);
        close(socket_fd);
        exit(EXIT_FAILURE);
    }

    RECV(client_fd, buffer, 4*8+1, 0, 0);
    UNLOAD_U64(atime, buffer);
    UNLOAD_U64(mtime, buffer+8);
    UNLOAD_U64(ctime, buffer+16);
    UNLOAD_U64(size, buffer+24);
    exec_bit = buffer[4*8];

    for(i=0;i<FILENAME_MAX;i++){
        /* TODO: Do not receive the path one byte at a time. */

        RECV(client_fd, filename+i, 1, 0, 0);

        if(!filename[i]) break;
    }
    if(i >= FILENAME_MAX && filename[FILENAME_MAX-1]){
        fprintf(stderr, "%s: Too long file name!\n", name);
        close(socket_fd);
        close(client_fd);
        exit(EXIT_FAILURE);
    }

    printf("Receiving %s (%lu bytes)%s...\n"
           "atime: %ld seconds since UNIX epoch\n"
           "mtime: %ld seconds since UNIX epoch\n"
           "ctime: %ld seconds since UNIX epoch\n", filename, size,
           exec_bit ? " (execute bit set)" : "", atime, mtime, ctime);

    /* FIXME: Check if the file name is valid UTF-8. */

    outname = basename(filename);

    if(!acceptall){
        char c = '\0';
        char buffer[3];

        fputs("Skip this file? [Y/n] ", stdout);
        if(fgets(buffer, 3, stdin) != NULL) c = buffer[0];
        if(c != 'n' && c != 'N'){
            puts("Skipping...");
            return;
        }

        printf("Save file as `%s'? [y/N] ", outname);
        if(fgets(buffer, 3, stdin) != NULL) c = buffer[0];
        if(c == 'y' || c == 'Y'){
            accept = 1;
        }
    }

    if(acceptall || accept){
        printf("Saving file to `%s'...\n", outname);

        if(!stat((const char*)outname, &statbuf)){
            if(acceptall){
                fprintf(stderr, "%s: File already exists!\n", name);
                close(socket_fd);
                close(client_fd);
                exit(EXIT_FAILURE);
            }else{
                fputs("File already exists!\n", stderr);
                accept = 0;
            }
        }
    }
    if(!acceptall && !accept){
        do{
            char *ptr;

            fputs("Enter file name: ", stdout);
            if(fgets(filename, FILENAME_MAX, stdin) == NULL){
                fputs("Failed to read input!\n", stderr);
                continue;
            }
            if((ptr = strchr(filename, '\n')) == NULL){
                fputs("File name too long!\n", stderr);
                continue;
            }

            *ptr = '\0';

            if(!strlen(filename)){
                fputs("Invalid file name!\n", stderr);
                continue;
            }
            if(!stat((const char*)filename, &statbuf)){
                fputs("File already exists!\n", stderr);
                continue;
            }
            break;
        }while(1);
        outname = filename;
    }

    fd = open(outname, O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP |
                                           S_IROTH |
                                           (exec_bit ? S_IXUSR | S_IXGRP |
                                                       S_IXOTH : 0));
    if(fd < 0){
        fprintf(stderr, "%s: Failed to open `%s'!\n", name, outname);
        close(socket_fd);
        close(client_fd);
        exit(EXIT_FAILURE);
    }
    fsync(fd);

    /* Hash and receive the file */

    total_bytes = size;
    bytes = 0;

    if(progress){
        show_progress(bytes, total_bytes, PROGRESS_W);
    }
    sha256_fnc(hash, receive_next, size, NULL);
    if(progress) fputc('\n', stdout);

    for(i=0;i<8;i++){
        word_t word;

        RECV(client_fd, buffer, 4, 0, 1);
        UNLOAD_U32(word, buffer);

        if(word != hash[i]){
            fprintf(stderr, "%s: SHA-256 checksum incorrect!\n"
                            "SHA-256 checksum: ", name);
            for(i=0;i<8;i++){
                fprintf(stderr, "%08lx", hash[i]);
                LOAD_U32(buffer+i*4, hash[i]);
            }
            fputc('\n', stderr);

            close(socket_fd);
            close(client_fd);
            close(fd);
            exit(EXIT_FAILURE);
        }
    }

    printf("File `%s' (%lu bytes) received successfully!\n"
           "SHA-256 checksum: ", outname, size);
    for(i=0;i<8;i++){
        printf("%08lx", hash[i]);
        LOAD_U32(buffer+i*4, hash[i]);
    }
    fputc('\n', stdout);

    fsync(fd);
    close(fd);

    /* XXX: Is the time there in seconds on all systems? */
    timestamps.actime = atime;
    timestamps.modtime = mtime;

    if(utime(outname, &timestamps)){
        fprintf(stderr, "%s: Failed to set timestamps!\n", name);
        exit(EXIT_FAILURE);
    }
}

void on_sigint(int signum) {
    (void)signum;

    puts("Closing socket...");
    close(socket_fd);
    exit(EXIT_SUCCESS);
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
                return EXIT_SUCCESS;
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

    if(receive){
        open_socket(bind, 1);

        if(listen(socket_fd, BACKLOG_MAX)){
            fprintf(stderr, "%s: listen failed!\n", *argv);
            return EXIT_FAILURE;
        }

        signal(SIGINT, on_sigint);

        while(1){
            client_fd = accept(socket_fd, NULL, NULL);

            receive_file();

            close(client_fd);
        }
    }else{
        for(;argv[optind];optind++){
            printf("Sending `%s'...\n", argv[optind]);
            send_file(argv[optind]);
        }
    }

    return EXIT_SUCCESS;
}
