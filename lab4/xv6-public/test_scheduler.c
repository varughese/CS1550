#include "types.h"
#include "user.h"
#include "syscall.h"

int main(int argc, char *argv[]) {
	setpriority(10);
	if (fork() == 0) {
		setpriority(5);
		if(fork() == 0) {
			setpriority(1);
			printf(1, "Priority 5\n");
		} else {
			setpriority(2);
			printf(1, "Priority 5\n");
			wait();

		}
	} else {
		printf(1, "Priority 10 Parent\n");
		wait();
	}
	exit();
}