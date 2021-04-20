#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <memory.h>
#include <unistd.h>
#include <pthread.h>

#define STUDENTS_COUNT (100)
#define ASSISTANTS_COUNT (100)
#define MAX_THREADS (5)

typedef struct student_t{
    int grade,id;
    char password[256];
} student_t;
typedef struct assistant_t
{
    int id;
    char password[256];
}assistant_t;
typedef struct node_t {
    struct node_t *next;
    int *pclient;
}node_t;
typedef struct queue_t {
    node_t *head ;
    node_t *tail ;
}queue_t;

queue_t *q;

void init_queue() {
    q = (queue_t *)calloc(1, sizeof(queue_t));
    q->head = NULL;
    q->tail = NULL;
}

pthread_t thread_pool[MAX_THREADS]; // TODO bigger than 1
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t data_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t condition_var = PTHREAD_COND_INITIALIZER;

student_t **students = NULL;
assistant_t **assistants = NULL;



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
int dequeue(){
    int result;
    
    pthread_mutex_lock(&mutex);

    while (q->head == NULL)
    {
        pthread_cond_wait(&condition_var, &mutex);
    }
    
    if (q->head->pclient == NULL)
    {
        return -1;
    }
    result = *(q->head->pclient);
    node_t *temp = q->head;
    q->head = q->head->next;
    if(q->head == NULL)
    {
        q->tail = NULL;
    }
    temp->next = NULL;
    free(temp->pclient);
    free(temp);
    pthread_mutex_unlock(&mutex);
    return result;
}
void enqueue(int *pclient){
    pthread_mutex_lock(&mutex);
    node_t *newnode = (node_t *)calloc(1, sizeof(node_t));
    newnode->pclient = pclient;
    newnode->next = NULL;
    if(q->tail == NULL){
        q->head = newnode;
    }else{
        q->tail->next = newnode;
    }
    q->tail = newnode;

    
    pthread_cond_broadcast(&condition_var);
    pthread_mutex_unlock(&mutex);
}

int tcp_establish(int port) {
    int srvfd;
    struct addrinfo *a =
	    alloc_tcp_addr(NULL/*host*/, port, AI_PASSIVE);
    DO_SYS( srvfd = socket( a->ai_family,
				 a->ai_socktype,
				 a->ai_protocol ) 	);
    DO_SYS( bind( srvfd,
				 a->ai_addr,
				 a->ai_addrlen  ) 	);
    DO_SYS( listen( srvfd,
				 5/*backlog*/   ) 	);
    freeaddrinfo( a );
    return srvfd;
}

int check_id(int id,char *password){
    int i;
    int retval = 0;

    pthread_mutex_lock(&data_mutex);
    for (i = 0; i < ASSISTANTS_COUNT && assistants[i] != NULL; ++i)
    {
        if (id == assistants[i]->id && strcmp(assistants[i]->password,password)==0){
            retval = -id;
            break;
        } 
    }
    for (i = 0; i < STUDENTS_COUNT && students[i] != NULL; ++i)
    {
        if (id==students[i]->id && strcmp(students[i]->password,password)==0){
            retval = id;
            break;
        }
    }
    pthread_mutex_unlock(&data_mutex);
    return retval;
}

int read_grade(int id) {
    pthread_mutex_lock(&data_mutex);
    int retval = 0;
    for (int i = 0; i < STUDENTS_COUNT && students[i] != NULL; ++i)
    {
        if(students[i]->id == id){
            retval = students[i]->grade;
            break;
        }
    }
    pthread_mutex_unlock(&data_mutex);
    return retval;
}
void swap(int *x, int *y) 
{ 
    int temp = *x; 
    *x = *y; 
    *y = temp; 
} 
char *get_gradelist() {
    pthread_mutex_lock(&data_mutex);
    int n, count = 0, i = 0, j = 0;
    int temp; 
    char tempi;
    int min = students[0]->id;
    for (int i = 0; i < STUDENTS_COUNT && students[i] != NULL; ++i) {
        count++;
    }
    // 15 is max length for formatted string (9 for ID, 3 for grade and some spaces)
    char *s = (char *)calloc(15 * count, sizeof(char));
    for (i = 0; i < count - 1; i++){
        for (j = i + 1; j < count; j++){
            if (students[i]->id > students[j]->id){
                temp = students[i]->id;
                students[i]->id = students[j]->id;
                students[j]->id = temp;

                temp = students[i]->grade;
                students[i]->grade = students[j]->grade;
                students[j]->grade = temp;
            }

        }
    }
    for (int i = 0; i < count; i++){
        n = sprintf(s + strlen(s),"%d: %d\n",students[i]->id,students[i]->grade);
    }
  
    pthread_mutex_unlock(&data_mutex);
    return s;

}

void update(int id,int grade) {
    pthread_mutex_lock(&data_mutex);
    for (int i = 0; i < STUDENTS_COUNT && students[i] != NULL; ++i)
    {
        if(students[i]->id == id){
            students[i]->grade = grade;
        }
    }
    pthread_mutex_unlock(&data_mutex);
}

void handle_client_fd(int clifd)
{   
    int k, N=256;
    int login_student=0, login_ta=0;
    char buf[N],*token;
    for(;;){
      DO_SYS(     k =  read (clifd, buf , N   ) );
        if (0 == k) {
            // Closed connection
            DO_SYS( close(clifd) );
            return;
        }
      buf[k]='\0';
      token = strtok(buf," ");
      if(strcmp(token,"Login")==0){
          token = strtok(NULL," ");
           
          if(token !=0){
            int id = atoi(token);
            char *password = strtok(NULL,"\n");
            if (password == NULL){
                char s[] = "Wrong User information";
                DO_SYS(write(clifd,s,sizeof(s)));
            }
            else if(check_id(id,password) < 0 && login_ta == 0 && login_student ==0){
                login_ta= -check_id(id,password);
                char s[100];
                int n = sprintf(s,"Welcome TA %d",login_ta);
                DO_SYS(write(clifd,s,n));
            } 
            else if(check_id(id,password)>0 && login_ta ==0 && login_student ==0){
                login_student = check_id(id,password);
                char s[100];
                int n = sprintf(s,"Welcome Student %d",login_student);
                DO_SYS(write(clifd,s,n));
            }
            else if(check_id(id,password)==0){
                char s[] = "Wrong User Information";
                DO_SYS(write(clifd,s,sizeof(s)));
            }else{
                char s[] = "Wrong User Information";
                DO_SYS(write(clifd,s,sizeof(s)));
            }
          }
      }
      else if(strcmp(token,"ReadGrade") ==0){
          token =strtok(NULL," ");
          if(token != 0){
            if(login_ta==0 && login_student==0){
                char s[] = "Not logged in";
                DO_SYS(write(clifd,s , sizeof(s)));
            }else if(login_ta > 0){
                int id = atoi(token);
                char s[10];
                int n = sprintf(s,"%d",read_grade(id));
                DO_SYS(write(clifd,s,n));
            }else if(login_student>0){
                char s[] = "Action not allowed";
                DO_SYS(write(clifd,s,sizeof(s)));
            }else{
                char s[] = "Invalid id";
                DO_SYS(write(clifd,s,sizeof(s)));
            }
        }else if(login_student>0){
                char s[10];
                int n = sprintf(s,"%d",read_grade(login_student)); 
                DO_SYS(write(clifd,s , n));
            }else{
                char s[] = "Missing argument";
                DO_SYS(write(clifd,s,sizeof(s)));
            }
      }
      else if(strcmp(token,"Logout")==0){
            if(login_student >0){
                char s[100];
                int n=sprintf(s,"Good bye %d",login_student);
                login_student = 0;  
                DO_SYS(write(clifd,s ,n));
            }else if(login_ta >0){
                char s[100];
                int n=sprintf(s,"Good bye %d",login_ta);
                login_ta = 0;  
                DO_SYS(write(clifd,s ,n));  
            }else{
                char s[] ="Not logged in";
                DO_SYS(write(clifd,s,sizeof(s)));
            }
       }
      else if(strcmp(token,"UpdateGrade")==0){
           token = strtok(NULL," ");

           if(token != 0){
               if(login_ta > 0){
                   int id = atoi(token);
                   token = strtok(NULL,"\n");
                   int grade = atoi(token);
                   if (grade == 0){
                       printf("Wrong Input\n");
                   }
                   update(id,grade);
                   char s[] = "";
                   DO_SYS(write(clifd,s,sizeof(s)));
               }
           }else{
                char s[] = "Wrong Input";
                DO_SYS(write(clifd,s,sizeof(s)));
           }
           
       }
      else if(strcmp(token,"Gradelist")==0){
          if(login_ta >0){
              char *s = NULL;
              s = get_gradelist();
              DO_SYS(write(clifd, s,strlen(s)));
              free(s);
              s = NULL;
          } else if(login_student>0){
              printf("Action not allowed");
          }
       }
      else {
            char s[] = "Wrong Input";
            DO_SYS(write(clifd,s,sizeof(s)));
      }
  }
}

void *thread_function(void *arg){
    int fd;
    while(1) {
        fd = dequeue(); // dequeue is blocking until valid fd is given
        //we have a connection
        if (fd != -1)
        {
            handle_client_fd(fd);
        }
    }
}

void echo_server(int port)
{
  
  int clifd, srvfd = tcp_establish(port);
  int *pclient = NULL;
  pid_t pid;
  for(;;) {
    DO_SYS( clifd = accept(srvfd, NULL, NULL) );
    pclient = malloc(sizeof(int)); // pthread worker will free()
    *pclient = clifd;
    enqueue(pclient);
  }

}

int main(int argc, char *argv[]) {
    int port;
    FILE *fp;
    char line[256];
    char *token;
    students = calloc(STUDENTS_COUNT, sizeof(student_t *));
    assistants = calloc(ASSISTANTS_COUNT, sizeof(assistant_t *));
    init_queue(q);

    if (students == NULL || assistants == NULL)
    {
        printf("Failed allocating memory\n");
        exit(EXIT_FAILURE);
    }

    if (argc != 2) {
        printf("Usage: <program> <port>\n");
        exit(EXIT_FAILURE);
    }
    port = atoi(argv[1]);

    fp = fopen("students.txt","r");
  
    if (fp == NULL){
      printf("io faliure\n");
      exit(EXIT_FAILURE);
    }
    
    int i = 0;
    while(!feof(fp)){
        students[i] = (student_t *) calloc(1, sizeof(student_t));
        fgets(line,256,fp);
        token = strtok(line,":");
        students[i]->id = atoi(token);
        students[i]->grade = 0;
        token = strtok(NULL,"\n");
        strcpy(students[i]->password,token);
        ++i;
    }
    fclose(fp);

    // assistants
    fp = fopen("assistants.txt","r");
    i = 0;
    while(!feof(fp)){
        assistants[i] = (assistant_t *)calloc(1, sizeof(assistant_t));
        fgets(line,256,fp);
        token = strtok(line,":");
        assistants[i]->id = atoi(token);
        token = strtok(NULL,"\n");
        strcpy(assistants[i]->password,token);
        ++i;
    }
    fclose(fp);
    printf("Starting server on port %d\n", port);
  for(int i =0;i < MAX_THREADS;i++){
      pthread_create(&thread_pool[i],NULL,thread_function,NULL);
  }
  echo_server(port);
  
  printf("Server died\n");
  return 0;
}

