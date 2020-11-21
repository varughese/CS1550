#ifndef _CS1550_SEM_
#define _CS1550_SEM_

/*
 * This file contains the struct definition for the user land 
 * programs
 */

struct cs1550_sem {
	int value;
	struct cs1550_queue *q;
};
struct cs1550_queue {
	int count;
	struct cs1550_node *head;
	struct cs1550_node *tail;
};

struct cs1550_node {
	struct task_struct *process;
	struct cs1550_node *next;
};


#endif
