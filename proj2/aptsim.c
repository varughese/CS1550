#include <sys/mman.h>
#include <linux/unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "sem.h"

/* Macro to make semaphore creation easier */
#define NEW_SEM(sem, val) mmap(NULL, sizeof(struct cs1550_sem), PROT_READ | PROT_WRITE,\
               MAP_SHARED | MAP_ANONYMOUS, 0, 0);\
          sem->value = val

/* Macro to make shared counter variable creation easier */
#define NEW_SHARED_COUNTER(name)  mmap(NULL, sizeof(int), PROT_READ | PROT_WRITE,\
               MAP_SHARED | MAP_ANONYMOUS, 0, 0);\
			   *name = 0

/* Constant to show how many tenants per agent. Changing this makes testing easier. */
#define MAX_TENANTS_PER_AGENT 10

void down(struct cs1550_sem *sem) {
	syscall(__NR_cs1550_down, sem);
}

void up(struct cs1550_sem *sem) {
	syscall(__NR_cs1550_up, sem);
}

struct cli_args {
	int total_tenants; // number of tenants
	int total_agents; // number of agents
	int p_tenant_follows; // probability of a tenant immediately following another
	int delay_tenant; // delay in seconds when tenant doesnt follow another
	int p_agent_follows; // probability of agent immediately following another agemnt
	int delay_agent; // delay in seconds when an agent does not immediately follow another agent
};

/* This function loads the arguments into a struct. It 
makes it so the order of the arguments do not matter. It also
makes the variable names slightly easier to read */
int loadArguments(int argc, char *argv[], struct cli_args* args) {
	char **pargv = argv+1;

	/* Set default values. This is useful for testing */
	args->total_tenants = 1;
	args->total_agents = 1;
	args->p_tenant_follows = 100;
	args->delay_tenant = 0;
	args->p_agent_follows = 100;
	args->delay_agent = 0;

	if(argc != 13) {
		printf("ERROR: Invalid arguments. Please specify: -m -k -pt -dt -pa -da.\n");
		return -1;
	}
	for(; *pargv != argv[argc]; pargv++) {
		char* current_arg = *pargv;
		if(current_arg[0] == '-') {
			int value = atoi(*(pargv+1));
			if(strcmp(current_arg, "-m") == 0) {
				args->total_tenants = value;
			} else if(strcmp(current_arg, "-k") == 0) {
				args->total_agents = value;
			} else if(strcmp(current_arg, "-pt") == 0) {
				args->p_tenant_follows = value;
			} else if(strcmp(current_arg, "-dt") == 0) {
				args->delay_tenant = value;
			} else if(strcmp(current_arg, "-pa") == 0) {
				args->p_agent_follows = value;
			} else if(strcmp(current_arg, "-da") == 0) {
				args->delay_agent = value;
			} else {
				printf("Unsupported argument %s.\n", current_arg);
				return -1;
			}
		}
	}
}


/* Global variables that indicate the id of the process */
int tenant_id;
int agent_id;
/* The start time which is used to calculate elapsed time */
time_t start_time;

/* Function to print elapsed time in seconds since start of program */
int getElapsedTime() {
	return (int)(time(NULL) - start_time);
}

/* SHARED DATA */
struct cs1550_sem *mutex;
struct cs1550_sem *waiting_for_agent;
struct cs1550_sem *tenant_arrived;
struct cs1550_sem *agent_arrived;
struct cs1550_sem *apt_lock;
struct cs1550_sem *tenants_leave;
struct cs1550_sem *agent_apt_lock;

int *current_agent_tenants;
int *tenants_in_apt;

/* AGENT */
void agentArrives() {
	printf("Agent %d arrives at time %d.\n", agent_id, getElapsedTime());
	/* Capture the mutex */
	down(mutex);
	/* 
	If we are not the first agent, we follow a 
	different protocol. This is because
	any agent that comes after the first needs to wake up
	any waiting agents.
	*/
	if(agent_id != 0) {
		/* Let go off the mutex because we are going to block */
		up(mutex);
		/* We block in here until we are woken up by the agent in the apartment. 
		If there is no agent in the apartment, then this semaphore will be 1, 
		so we will not block.
		Since we knew there was a agent in
		here before, we can wake up and free any waiting tenants that 
		could not meet the agent because they were too full. */
		down(agent_apt_lock);
		down(mutex);
		/* This needs to be in here so we know to wake up waiting
		tenants. */
		up(waiting_for_agent);
	} else {
		/* If we are an agent that came in
		without having to wait on anyone,
		grab the apt lock. */
		down(agent_apt_lock);
	}
	up(mutex);
	/* Signal to the tenants that they can wake up since an agent is here. */
	up(agent_arrived);
	/* Wait for a tenant to arrive to continue. */
	down(tenant_arrived);
}
void openApt() {
	printf("Agent %d opens the apartment for inspection at time %d.\n", agent_id, getElapsedTime());
	/* Allow tenants to enter the apartment */
	up(apt_lock);
}
void agentLeaves() {
	/* Block until we are signalled that the last tenant left */
	down(tenants_leave);
	down(mutex);
	printf("Agent %d leaves the apartment at time %d.\n", agent_id, getElapsedTime());
	printf("The apartment is now empty.\n");
	/* Now we are leaving, so reset the current_agent_tenants and let the next
	agent enter the apartment. */
	up(agent_apt_lock);
	*current_agent_tenants = 0;
	up(mutex);
}

/* TENANT */
void tenantArrives() {
	printf("Tenant %d arrives at time %d.\n", tenant_id, getElapsedTime());
	down(mutex);
	/* If the current agent has the max number of tenants, we block.
	We will wait for the next agent to wake us. */
	if(*current_agent_tenants >= MAX_TENANTS_PER_AGENT) {
		up(mutex);
		/* Wait for the next agent to wake us up */
		down(waiting_for_agent);
		down(mutex);
		/* Now, we increase the number of free agents, because
		now we have a new agent assigned to this tenant */
		(*current_agent_tenants)++;
		/* Free the next waiting tenant. Since we have no 
		 way of knowing who the "last" waiting agent is all we 
		 can do is wakeup the next one.
		*/
		if(*current_agent_tenants < MAX_TENANTS_PER_AGENT) {
			up(waiting_for_agent);
		}
	} else {
		(*current_agent_tenants)++;
	}
	/* If we are the first agent here, let an agent know.
	If not, we do not want multiple tenants to up this semaphore */
	(*tenants_in_apt)++;
	if(*current_agent_tenants == 1) {
		up(tenant_arrived);
	}
	up(mutex);
	/* Wait for a agent to arrive */
	down(agent_arrived);
	down(mutex);
	/* We want to make sure that the agent arrived 
	semaphore is reset. So every tenant needs to.
	This will increment one more time than 
	neccesary. This is cleaned up in the tenantLeaves
	function. */
	up(agent_arrived);
	up(mutex);
}
void viewApt() {
	/* Wait until a agent unlocks the apartment for us to visit it */
	down(apt_lock);
	printf("Tenant %d inspects the apartment at time %d.\n", tenant_id, getElapsedTime());
	/* Unlock the apartment for the next tenant waiting.
	We do not know the tenants waiting so all we can do is call the next one.
	When the tenant leaves, they will reset the value of this semaphore */
	up(apt_lock);
	sleep(2);
}
void tenantLeaves() {
	down(mutex);
	/* Decrease the number of tenants in the apartment. This variable
	is different that current_agent_tenants in apartment. This is because
	there can be 5 tenants in the apartment that visit it and then leave,
	and than once there are no more the agent can leave as well. current_agent_tenants
	counts how many agents the current agent has shown the apartment to */
	(*tenants_in_apt)--;
	printf("Tenant %d leaves the apartment at time %d.\n", tenant_id, getElapsedTime());
	if(*tenants_in_apt == 0) {
		/* Make sure no other tenant can enter because 
		   this agent is leaving. Since the tenants
		   block on this condition, we make this true.
		   Then the agent will reset this counter to 0.
		*/
		*current_agent_tenants = MAX_TENANTS_PER_AGENT;
		up(tenants_leave);
		up(mutex);
		/* One more person has to down apt_lock to reset the semaphore.
		Since this tenant is the last one to leave, it will do it.
		*/
		down(agent_arrived);
		down(apt_lock);
	} else {
		up(mutex);
	}

}

void agentProcess() {
	agentArrives();
	openApt();
	agentLeaves();
}

void tenantProcess() {
	tenantArrives();
	viewApt();
	tenantLeaves();
}

int initSharedVariables() {
	mutex = NEW_SEM(mutex, 1);
	waiting_for_agent = NEW_SEM(waiting_for_agent, 0);
	tenant_arrived = NEW_SEM(tenant_arrived, 0);
	agent_arrived = NEW_SEM(agent_arrived, 0);
	apt_lock = NEW_SEM(apt_lock, 0);
	tenants_leave = NEW_SEM(tenants_leave, 0);

	agent_apt_lock = NEW_SEM(agent_apt_lock, 1);

	current_agent_tenants = NEW_SHARED_COUNTER(current_agent_tenants);
	tenants_in_apt = NEW_SHARED_COUNTER(tenants_in_apt);
}

int shouldDelay(int probability) {
	int chosen = rand() % 100;
	return chosen > probability;
}

/* 
For loop to create agent proccesses. Self-explanatory code.

Do not wait if it is the first one. Check probability and sleep for the 
specified delay time.

After this fork and create a child. Start the tenant process and break 
from the loop so more processes are not made.
*/ 
void startTenantCreation(struct cli_args *args) {
	int i;
	for(i=0; i<args->total_tenants; i++) {
		if(i!=0 && shouldDelay(args->p_tenant_follows)) {
			sleep(args->delay_tenant);
		}
		if(fork() == 0) {
			tenant_id = i;
			tenantProcess();
			break;
		}
	}
}

/* Follows above logic with different arguments. */
void startAgentCreation(struct cli_args *args) {
	int i;
	for(i=0; i<args->total_agents; i++) {
		if(i!=0 && shouldDelay(args->p_agent_follows)) {
			sleep(args->delay_agent);
		}
		if(fork() == 0) {
			agent_id = i;
			agentProcess();
			break;
		}			
	}
}

int main(int argc, char *argv[]) {
	struct cli_args *args = malloc(sizeof(struct cli_args));
	int validArgs = loadArguments(argc, argv, args);
	int wait_counter, total_processes;
	if(validArgs < 0) return -1;

	initSharedVariables();

	start_time = time(NULL);
	printf("The apartment is now empty\n");
	if(fork() == 0) {
		// TENANT CREATOR PROCESS
		startTenantCreation(args);
	} else {
		// AGENT CREATOR PROCESS
		startAgentCreation(args);
	}

	/* Make the parent process wait for startTenantCreation, and all of 
	the tenants and agents.
	*/
	total_processes = args->total_agents + args->total_tenants + 1;
	for(wait_counter = 0; wait_counter < total_processes; wait_counter++) {
		wait(NULL);
	}

	return 0;
}