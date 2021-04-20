#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <memory.h>
#include <unistd.h>
#include <netinet/in.h>


#define DO_SYS(syscall) do {		\
    if( (syscall) == -1 ) {		\
        perror( #syscall );		\
        exit(EXIT_FAILURE);		\
    }						\
} while( 0 )


struct addrinfo*
alloc_tcp_addr(const char *host, uint16_t port, int flags)
{
    int err;   struct addrinfo hint, *a;   char ps[16];

    snprintf(ps, sizeof(ps), "%hu", port); // why string?
    memset(&hint, 0, sizeof(hint));
    hint.ai_flags    = flags;
    hint.ai_family   = AF_UNSPEC;
    hint.ai_socktype = SOCK_STREAM;
    hint.ai_protocol = IPPROTO_TCP;

    if( (err = getaddrinfo(host, ps, &hint, &a)) != 0 ) {
        fprintf(stderr,"%s\n", gai_strerror(err));
        exit(EXIT_FAILURE);
    }

    return a; 
}

int tcp_connect(const char* host, uint16_t port)
{
    int clifd;
    struct addrinfo *a = alloc_tcp_addr(host, port, 0);

    DO_SYS( clifd = socket( a->ai_family,
				 a->ai_socktype,
				 a->ai_protocol ) 	);
    DO_SYS( connect( clifd,
				 a->ai_addr,
				 a->ai_addrlen  )   );

    freeaddrinfo( a );
    return clifd;
}

void echo_client(const char *host, uint16_t port)
{
  char buf[256], msg[256];
  int k, fd;
  int pout[2];
  int pin[2];
  pid_t pid;
  DO_SYS(pipe(pout));
  DO_SYS(pipe(pin));
  DO_SYS(pid = fork());
  if (0 == pid){ //child
    close(pout[0]);
    close(pin[1]);
    for(;;) {
      printf("\n> ");
      fflush(stdin);
      scanf("%[^\n]%*c", msg);
      if (strlen(msg) == 0)
      {
        continue;
      }      
      
      DO_SYS(     write(pout[1], msg, strlen(msg)) );
      DO_SYS(         k = read (pin[0], buf, sizeof(buf)));
      buf[k]='\0';         
      
      DO_SYS(     write(STDOUT_FILENO, buf, k) );
    }
  } else { // Parent
    close(0);
    close(1);
    close(pout[1]);
    close(pin[0]);
    dup2(pout[0], STDIN_FILENO);
    dup2(pin[1], STDOUT_FILENO);

    fd = tcp_connect(host, port);
    for (;;) {
      DO_SYS(     k = read(STDIN_FILENO, msg, sizeof(msg)) );
      msg[k] = '\0';

      DO_SYS(     write(fd, msg, strlen(msg)));
      DO_SYS(         k = read (fd, buf, sizeof(buf)));
      buf[k]='\0';

      DO_SYS(     write(STDOUT_FILENO, buf, k) );
    }
    DO_SYS(     close(fd)                    );
  }  
}

int main(int argc, char *argv[]) {
  char server_name[256];
  int port;

  if (argc != 3) {
        printf("Usage: <program> <address> <port>\n");
        exit(EXIT_FAILURE);
    }
  strncpy(server_name,argv[1],256);
  port = atoi(argv[2]);
  printf("Connecting to server %s:%d", server_name, port);
  echo_client(server_name, port);
  printf("Echo done\n");
  return 0;
}
