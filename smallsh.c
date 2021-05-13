//Name:Chris Hauser
//Program: CS 344 HW 3 Smallsh
//Date: 11/1/2020
//Description: An elementary shell program which takes in user input and attempts to execute one of three built in functions: exit, cd, and status,
//or executes another status along the PATH. Can run processes in the background and use SIGINT to stop foreground processes. Can use SIGTSTP to trigger foreground
// only mode.


#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h> 
#include <signal.h>

int lastStatus;
int bgChildren[10000];
int bgChildrenWait[10000];
int lastChild = 0;
int childStatus;
char* dn = "/dev/null";
int fgMode = 0;
int curFG = -1;
int dummyStat;



// call structure with fields to track the arguments, number of them, input and output file names, and a couple flags
struct call {
	char* command;
	char* args[513];
	int argCount;
	int empty;
	int bg;
	char* infile;
	char* outfile;
};

//replaces all instances of $$ in the string data fields of the given call struct
struct call* pidReplace(struct call* thisCall) {
	char rep[10];
	sprintf(rep, "%d", getpid());

	// replace instances of $$ in the command
	char* comm = thisCall->command;
	char* buffer = malloc(sizeof(comm));
	int j = 0;
	// iterate through the command,looking for $ characters. When one is found, check for a second. If a second is found, add the pid 
	// to the new string we are building. Otherwise, append the next character in the string.
	while (j < strlen(thisCall->command)) {
		if (comm[j] == '$') {
			if (comm[j + 1] == '$') {
				strcat(buffer, rep);
				j = j + 2;
			}
		}
		strncat(buffer, &comm[j], 1);
		j++;
	}
	thisCall->command = calloc(strlen(buffer) + 1, sizeof(char));
	strcpy(thisCall->command, buffer);

	

	// iterate through args and replace instances of $$ with the pid
	for (int i = 0; i < thisCall->argCount; i++) {
		char* thisArg = thisCall->args[i];
		char* buffer = malloc(sizeof(thisArg) * 5);
		int j = 0;
		while (j < strlen(thisArg)) {
			if (thisArg[j] == '$') {
				if (thisArg[j + 1] == '$') {
					strcat(buffer, rep);
					j = j + 2;
				}
			}
			strncat(buffer, &thisArg[j], 1);
			j++;
		}
		thisCall->args[i] = calloc(strlen(buffer) + 1, sizeof(char));
		strcpy(thisCall->args[i], buffer);
		
	}

	// replace instances of $$ in the infile
	char* in = thisCall->infile;
	buffer = malloc(sizeof(in));
	j = 0;
	while (j < strlen(thisCall->infile)) {
		if (in[j] == '$') {
			if (in[j + 1] == '$') {
				strcat(buffer, rep);
				j = j + 2;
			}
		}
		strncat(buffer, &in[j], 1);
		j++;
	}
	thisCall->infile = calloc(strlen(buffer) + 1, sizeof(char));
	strcpy(thisCall->infile, buffer);
	

	// replace instances of $$ in the outfile
	char* out = thisCall->outfile;
	buffer = malloc(sizeof(out));
	j = 0;
	while (j < strlen(thisCall->outfile)) {
		if (out[j] == '$') {
			if (out[j + 1] == '$') {
				strcat(buffer, rep);
				j = j + 2;
			}
		}
		strncat(buffer, &out[j], 1);
		j++;
	}
	thisCall->outfile = calloc(strlen(buffer) + 1, sizeof(char));
	strcpy(thisCall->outfile, buffer);
	free(buffer);

	return thisCall;

}

//takes in a given call struct and prints its data members
void printCall(struct call* thisCall) {
	fflush(NULL);
	printf("\n\n");
	for (int i = 0; i < thisCall->argCount; i++) {
		printf("arg [%d] is %s\n", i, thisCall->args[i]);
	}
	printf("command is %s\n", thisCall->command);
	printf("argcount is %d\n", thisCall->argCount);
	printf("bg is %d\n", thisCall->bg);
	printf("infile is %s\n", thisCall->infile);
	//printf("length of infile is %lu", strlen(thisCall->infile));
	printf("outfile is %s\n", thisCall->outfile);
	//printf("length of outfile is %lu", strlen(thisCall->outfile));
	return;
}

//exit the shell and terminates all other processes and jobs.


/*Takes in a user command input and parses it into a command struct*/ 
struct call* parseInput(char* input) {
	struct call* thisCall = malloc(sizeof(struct call));
	/*initialize data members in the structure for input, output, and background flag.*/

	thisCall->empty = 1;
	thisCall->bg = 0;
	thisCall->infile = "";
	thisCall->outfile = "";
	char* savePtr;
	

	//check for blank lines or inputs from ctrl+z and other inputs that give a len of 0
	if ((strlen(input) == 1) | (strlen(input)==0)) {
		fflush(NULL);
		printf("returning");
		return thisCall;
	}
	if (input[0] == '#') {
		
		fflush(NULL);
		return thisCall;
	}
	//remove new line char
	input[strlen(input) - 1] = 0;
	// check if last char is   & and remove it, then set bg for thisCall to indicate it should be a background process.
	if (input[strlen(input) - 1] == 38) {
		input[strlen(input) - 1] = 0;
		thisCall->bg = 1;
	}

	//mark that the call has content and should be run as an actual command when returned
	thisCall->empty = 0;
	fflush(NULL);
	char* token = strtok_r(input, " ", &savePtr);
	fflush(NULL);
	//copy the first word into command and the first index of args
	thisCall->command = calloc(strlen(token) + 1, sizeof(char));
	strcpy(thisCall->command, token);
	thisCall->args[0] = calloc(strlen(token) + 1, sizeof(char));
	strcpy(thisCall->args[0], token);
	token = strtok_r(NULL, " ", &savePtr);
	int arg_index = 1;
	//iterate through the whole input, with the default action to add one more arg to the array. If special characters are found, add
	//the next token after to the corresponding field, for input or output, or set the background flag for &
	while(token!= NULL){

		if (strcmp(token, "<") == 0) {

			fflush(NULL);
			token = strtok_r(NULL, " ", &savePtr);
			thisCall->infile = calloc(strlen(token) + 1, sizeof(char));
			strcpy(thisCall->infile, token);
		}

		else if (strcmp(token, ">") == 0) {

			fflush(NULL);
			token = strtok_r(NULL, " ", &savePtr);
			thisCall->outfile = calloc(strlen(token) + 1, sizeof(char));
			strcpy(thisCall->outfile, token);

		}

		else{
			thisCall->args[arg_index] = calloc(strlen(token) + 1, sizeof(char));
			strcpy(thisCall->args[arg_index], token);
			arg_index++;

		}

		token = strtok_r(NULL, " ", &savePtr);
	}
	thisCall->argCount = arg_index;
	
	return thisCall;
}

// signal handler to swap between foreground only mode and off
void handle_SIGTSTP(int signo) {

	if (fgMode == 0) {
		fflush(NULL);
		char* msgOn = "\nEntering foreground-only mode (& is now ignored)\n:";
		fflush(NULL);
		write(STDOUT_FILENO, msgOn, 52);
		fgMode = 1;
	}
	else {
		fflush(NULL);
		char* msgOff = "\nExiting foreground-only mode\n:";
		fflush(NULL);
		write(STDOUT_FILENO, msgOff, 31);
		fgMode = 0;
	}
	

}

//the bulk of the program, which asks for user input, then runs it as either a built in command or an executed one.
void shell() {
	// set up ignore, default, and special SIGTSTP handlers 
	
	struct sigaction ignore_action;
	ignore_action.sa_handler = SIG_IGN;
	sigfillset(&ignore_action.sa_mask);
	ignore_action.sa_flags = 0;

	struct sigaction def_action;
	def_action.sa_handler = SIG_DFL;
	sigfillset(&def_action.sa_mask);
	def_action.sa_flags = 0;

	sigaction(SIGINT, &ignore_action, NULL);

	struct sigaction SIGTSTP_action;
	SIGTSTP_action.sa_handler = handle_SIGTSTP;
	sigfillset(&SIGTSTP_action.sa_mask);
	SIGTSTP_action.sa_flags = SA_RESTART;
	
	sigaction(SIGTSTP, &SIGTSTP_action, NULL);
	

	char input[2024] = "";
	// initialize the lastStatus
	lastStatus = 0;
	// scan in a user string for teh file name
	while (1) {

		// read in user input
		printf(":");

		fgets(input, 2024, stdin);


		fflush(NULL);
		//send the input to get parsed and stored as a call struct
		struct call* thisCall = parseInput(input);
		// if not empty, replace instances of $$ with the pid, then try to match it to a built in command
		if (thisCall->empty == 0) {
			thisCall = pidReplace(thisCall);
			//built in function for exit
			// exit kills all children by iterating through the background children list, then self terminates.
			if (strcmp(thisCall->command, "exit") == 0) {
				for (int child = 0; child < lastChild; child++) {
					if (bgChildren[child] != -1) {
						kill(bgChildren[child], SIGKILL);
					}
				}
				exit(0);
				return;
			}
			// built in function handler for cd
			else if (strcmp(thisCall->command, "cd") == 0) {
			//with no arg besides command specified, change to the home directory
				if (thisCall->argCount == 1) {
					chdir(getenv("HOME"));
				}
				//otherwise, change to the first argument
				else {
					chdir(thisCall->args[1]);
				}
			}

			// built in function handler for status
			else if (strcmp(thisCall->command, "status") == 0) {
				// interpret and then print the last status
				fflush(NULL);
				if (WIFEXITED(lastStatus)) {
					printf("Last process exited with status %d\n", WEXITSTATUS(lastStatus));
				}
				else {
					printf("Last process exited due to signal %d\n", WTERMSIG(lastStatus));
				}
				
			}

			//handlers for forking other executions

			else {

				pid_t spawnPid = fork();
				// if the call was not marked for background, or we are in foreground only mode, proceed here
				if ((thisCall->bg == 0) | (fgMode == 1)) {
					switch (spawnPid) {
					case -1:
						//error catcher for forks
						fflush(NULL);
						perror("fork()\n");
						exit(1);
						break;
					case 0:
						// In the child process
						;
						// load up the signal handlers to terminate upon SIGINT and ignore SIGTSTP
						sigaction(SIGINT, &def_action, NULL);
						sigaction(SIGTSTP, &ignore_action, NULL);


						// assign the correct file descriptors based on the infile and outfile data members, then dup2 them to the appropriate stream
						if (strcmp(thisCall->infile, "") != 0) {
							int inFD = open(thisCall->infile, O_RDONLY);
							if (inFD == -1) {
								fflush(NULL);
								perror("input open()");
								lastStatus = 1;
								exit(1);
							}
							int result = dup2(inFD, 0);
							if (result == -1) {
								fflush(NULL);
								perror("input dup2()");
								lastStatus = 1;
								exit(1);
							}
						}

						if (strcmp(thisCall->outfile, "") != 0) {
							int outFD = open(thisCall->outfile, O_WRONLY | O_CREAT | O_TRUNC, 0777);
							if (outFD == -1) {
								fflush(NULL);
								perror("output open()");
								lastStatus = 1;
								exit(1);

							}
							int result = dup2(outFD, 1);
							if (result == -1) {
								fflush(NULL);
								perror("output dup2()");
								lastStatus = 1;
								exit(1);
							}
						}
						fflush(NULL);
						// try to run the argument
						execvp(thisCall->command, thisCall->args);
						//if here, the exec failed
						lastStatus = 1;
						fflush(NULL);
						//this prints errors occuring during execv
						fflush(NULL);
						perror("execvp");
						exit(1);
						break;
					default:
						// In the parent process
						// Wait for child's termination
						curFG = spawnPid;
						spawnPid = waitpid(spawnPid, &childStatus, 0);

						lastStatus = childStatus;
						// for signal terminations, print a message saying the signal
						if (WIFSIGNALED(childStatus)) {
							printf("\nChild process exited due to signal %d\n", WTERMSIG(childStatus));
						}

						/*printf("PARENT(%d): child(%d) terminated with status %d. Exiting\n", getpid(), spawnPid, lastStatus);*/
					}
				}

				// separate forking for background processes, I'm sure this could  all be done as one.
				else if (thisCall->bg == 1) {


					switch (spawnPid) {
					case -1:
						//error catcher for forks
						fflush(NULL);
						perror("fork()\n");
						exit(1);
						break;
					case 0:
						// In the child process
						
						;
						// assign the signal handler to ignore SIGTSTP
						sigaction(SIGTSTP, &ignore_action, NULL);


						// assign io as before if specified
						if (strcmp(thisCall->infile, "") != 0) {
							int inFD = open(thisCall->infile, O_RDONLY);
							if (inFD == -1) {
								fflush(NULL);
								perror("input open()");
								exit(1);
								break;
							}
							int result = dup2(inFD, 0);
							if (result == -1) {
								fflush(NULL);
								perror("input dup2()");
								exit(1);
								break;
							}
						}

						else {
							// send input and output to /dev/null if no other io is specified
							thisCall->infile = calloc(strlen(dn) + 1, sizeof(char));
							strcpy(thisCall->infile, dn);
							int inFD = open(thisCall->infile, O_RDONLY);

							if (inFD == -1) {
								fflush(NULL);
								perror("input open()");
								exit(1);
								break;
							}
							int result = dup2(inFD, 0);
							if (result == -1) {
								fflush(NULL);
								perror("input dup2()");
								exit(1);
								break;
							}
						}

						if (strcmp(thisCall->outfile, "") != 0) {
							int outFD = open(thisCall->outfile, O_WRONLY | O_CREAT | O_TRUNC, 0777);
							if (outFD == -1) {
								fflush(NULL);
								perror("output open()");
								exit(1);
								break;
							}
							int result = dup2(outFD, 1);
							if (result == -1) {
								fflush(NULL);
								perror("output dup2()");
								exit(1);
								break;
							}
						}
						else {

							thisCall->outfile = calloc(strlen(dn) + 1, sizeof(char));
							strcpy(thisCall->outfile, dn);
							int outFD = open(thisCall->outfile, O_WRONLY | O_CREAT | O_TRUNC, 0777);

							if (outFD == -1) {
								fflush(NULL);
								perror("output open()");
								exit(1);
								break;
							}
							int result = dup2(outFD, 1);
							if (result == -1) {
								fflush(NULL);
								perror("output dup2()");
								exit(1);
								break;
							}
						}

						fflush(NULL);
						execvp(thisCall->command, thisCall->args);
						fflush(NULL);
						//this prints errors occuring during execv

						perror("execvp");
						exit(1);
						break;

					default:
						// In the parent process
						// add the last bg child to the current processes array and print the process pid
						printf("Background process %d has begun\n", spawnPid);
						bgChildren[lastChild] = spawnPid;
						spawnPid = waitpid(spawnPid, &childStatus, WNOHANG);

						lastChild++;

					}
				}
			}


			// iterate through background processes and return which have closed.
			for (int child = 0; child < lastChild; child++) {
				// check that this child hasn't been marked completed
				if (bgChildren[child] != -1) {
					//printf("bgchildren[%d] is %d", child, bgChildren[child]);
					bgChildrenWait[child] = waitpid(bgChildren[child], &childStatus, WNOHANG);
					//printf("bgchildren[%d] is %d", child, bgChildren[child]);
					if (bgChildrenWait[child] > 0) {
						//taken from module example for exit status handling with children
						if (WIFEXITED(childStatus)) {
							printf("Child %d exited normally with status %d\n", bgChildrenWait[child], WEXITSTATUS(childStatus));
						}
						else {
							printf("Child %d exited abnormally due to signal %d\n", bgChildrenWait[child], WTERMSIG(childStatus));
						}
						// set to -1 stop tracking it
						bgChildren[child] = -1;
					}
				}
			}
		}
	}
}

int main(void) {
	shell();

	return EXIT_SUCCESS;
}