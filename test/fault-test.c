#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>

void fault_log(char *message)
{
	int fd;
	pid_t pid;
	char buffer[200];
	
	pid = getpid();
	fd = open("/dev/kmsg", O_WRONLY);
	if(fd<0) {
		return;
	}
	sprintf(buffer, "<20> [%d] %s\n", pid, message);
	write(fd, buffer, strlen(buffer));
	close(fd);
}

void do_fault(void) 
{
	void *ptr;
	char c;
	fault_log("inside do_fault");

	ptr = 0;
	c = *(char *)ptr;
}

void delay_then_fault(int count)
{
	int i;
	for (i=count; i>0; i--) {
		printf("%d", i);
		printf(i>1 ? "," : "...\n");
		fflush(stdout);
		sleep(1);
	}
	fault_log("I was minding my own business, when...");
	do_fault();
}

int main(int argc, char **argv)
{
	int count = 5;

	if (argc>1) {
		if (strcmp(argv[1], "-h")==0 || strcmp(argv[1],"--help")==0) {
			printf("Usage fault-test [<delay>]\n");
			exit(0);
		}
		count = atoi(argv[1]);
	}
	printf("Doing a segfault in ");
	delay_then_fault(count);
	return 0;
}
