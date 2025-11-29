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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <unistd.h>

#include <sys/socket.h>

#include <hash.h>

static const char help[] = "USAGE: %s [-hp] [-s hostname] [files...]\n"
"\n"
"ft -- A basic file transfer utility.\n"
"\n"
"  -s   Set the IP address to send files to.\n"
"  -p   Show progess.\n"
"  -h   Show this help.\n";

static int port;
static char *dest = NULL;

static int receive = 1;
static int progress = 1;

static int fd;

int main(int argc, char **argv) {
    extern int optind;
    extern int optopt;
    int c;

    size_t i;

    static word_t hash[8];
    char *msg = "";

    if(argc < 2){
        fprintf(stderr, help, *argv);
    }

    while((c = getopt(argc, argv, "hps:")) > 0){
        switch(c){
            case 'h':
                printf(help, *argv);
                break;
            case 'p':
                progress = 1;
                break;
            case 's':
                if(dest != NULL){
                    fprintf(stderr, "%s: Destination hostname set twice!\n",
                            *argv);
                    return EXIT_FAILURE;
                }
                dest = argv[optopt];
                receive = 0;
            default:
                break;
        }
    }

    for(i=0;argv[optind][i];i++){
        if(argv[optind][i] < '0' || argv[optind][i] >= '9'){
            fprintf(stderr, "%s: Invalid port number!\n", *argv);
            return EXIT_FAILURE;
        }
    }
    port = atoi(argv[optind++]);

    for(;argv[optind];optind++){
        puts(argv[optind]);
    }

    sha256(hash, (unsigned char*)msg, strlen(msg));

    fputs("0x", stdout);
    for(i=0;i<8;i++){
        printf("%08lx", hash[i]);
    }
    fputc('\n', stdout);

    sha256(hash, (unsigned char*)msg, strlen(msg));

    fputs("0x", stdout);
    for(i=0;i<8;i++){
        printf("%08lx", hash[i]);
    }
    fputc('\n', stdout);

    return EXIT_SUCCESS;
}
