
all: GradeServer GradeClient

GradeServer: GradeServer.c
	gcc -o GradeServer GradeServer.c -lpthread

GradeClient: client.c
	gcc -o GradeClient client.c
