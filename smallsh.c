// If you are not compiling with the gcc option --std=gnu99, then
// uncomment the following line or you might get a compiler warning
//#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <signal.h>
#include <unistd.h> 
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>

/* struct for command parsing */
struct CommandLine
{
    char *command;
    char args[512][100];
    char *inputFile;
    char *outputFile;   
};

// global variables
int foregroundOnly = 0, signalStop = 0, exit_status = 0;
struct sigaction default_action = {0}, ignore_action = {0};


/* 
 *  helper function to expand the $ variable (Source: GeeksforGeeks.org) 
*/
char* replaceWord(const char* s, const char* oldWord, const char* newWord) 
{ 
    char* result; 
    int i, count = 0; 
    int newWlen = strlen(newWord); 
    int oldWlen = strlen(oldWord); 
  
    // count num for times $$ occurs in string 
    for (i = 0; s[i] != '\0'; i++) { 
        if (strstr(&s[i], oldWord) == &s[i]) { 
            count++; 
  
            // Jumping to index after the old word. 
            i += oldWlen - 1; 
        } 
    } 
  
    // make a new string with new length 
    result = (char*)malloc(i + count * (newWlen - oldWlen) + 1); 
  
    i = 0; 
    while (*s) { 
        // compare the substring with the result 
        if (strstr(s, oldWord) == s) { 
            strcpy(&result[i], newWord); 
            i += newWlen; 
            s += oldWlen; 
        } 
        else
            result[i++] = *s++; 
    } 
    result[i] = '\0'; 
    return result; 
} 

/* 
 *  signal handler for SIGINT 
*/
void handle_SIGINT(int signo) {
    default_action.sa_handler = SIG_DFL;
    sigaction(SIGINT, &default_action, NULL);
}

/* 
 *  signal handler for SIGTSTP 
*/
void handle_SIGTSTP(int signo){
    // send msg of only foreground mode activation
    if (foregroundOnly == 0) {
        char* message = "Entering foreground-only mode (& is now ignored)\n";
	      write(STDOUT_FILENO, message, strlen(message));
        foregroundOnly = 1;
    }
    else {
        char* message = "Exiting foreground-only mode\n";
	      write(STDOUT_FILENO, message, strlen(message));
        foregroundOnly = 0;
    }
}

void signalSetup() {    
    // register SIGTSTP handler (switching foreground modes in parent)
    struct sigaction SIGTSTP_action = {0};
    SIGTSTP_action.sa_handler = handle_SIGTSTP;
    sigfillset(&SIGTSTP_action.sa_mask);
    sigaction(SIGTSTP, &SIGTSTP_action, NULL);
    
    // register SIGINT handler (ignore in parent)
    ignore_action.sa_handler = SIG_IGN;
    sigaction(SIGINT, &ignore_action, NULL);
    
}

/* 
 *  function to expand the command variable with the process ID
 */
char* variableExpansion(char* command) {
    // get the process id and convert it to a string
    char* expansion = "$$";
    int processID = getpid();
    char sprocessID[20];
    sprintf(sprocessID, "%d", processID);
    
    // expand the $$ to the process id in other function
    char* newCommand = replaceWord(command, expansion, sprocessID);
    return newCommand;
}

/* 
 *  function to handle the built-in commands (exit, cd, status)
 */
int builtInCommands(char* command, char args[][100]) {
    // if the command is to exit, exit
    if (strcmp(command, "exit") == 0) {
        // kill all processes before exiting
        kill(0, SIGKILL);
        
        // exit the program
        exit(EXIT_SUCCESS);
    }
    
    // if the command is cd check for args
    else if (strcmp(command, "cd") == 0) {
    
        // if there are no arguments, changes to directory in HOME environment variable
        if (strcmp(args[0], "") == 0) {
            
            char pathDir[100]; 
            getcwd(pathDir, sizeof(pathDir));
            
            chdir(getenv("HOME"));
            getcwd(pathDir, sizeof(pathDir));
        }
        
        // else go to the directory specified (one arg allowed)
        else {
            chdir(args[0]);
        }
        // return a value of 1 to let program know a command was run
        return 1;
    }
    
    // if the command is status, display the current status
    else if (strcmp(command, "status") == 0){
        if (signalStop == 0) {
            printf("exit value %d", exit_status);
            fflush(stdout);
        }
        else {
            printf("terminated by signal %d", exit_status);
            fflush(stdout);
        }
        
        // return a value of 1 to let program know a command was run
        return 1;
    }
    return 0;
}

int forkForeground(char* command, char *args[], char* inputFile, char* outputFile, int isThereInput, int isThereOutput) {
    int childStatus, result, redirection = 0;
    char* curr_status[50];
  
    // fork a new process
  	pid_t spawnPid = fork();
  
  	switch(spawnPid) {
    case -1:
 		    perror("fork()\n");
  		  exit(1);
  		  break;
        
    // this is the child process
  	case 0:
        // signal handling for SIGINT and SIGTSTP, empty statement on next line to allow declaration after a label
        ;
        struct sigaction SIGINT_action = {0}, SIGTSTP_action = {0};
        
        // register SIGTSTP handler (ignore in child)
        SIGTSTP_action.sa_handler = SIG_IGN;
        sigfillset(&SIGTSTP_action.sa_mask);
        sigaction(SIGTSTP, &SIGTSTP_action, NULL);
        
        // register SIGINT handler (default behavior in child)
        SIGINT_action.sa_handler = handle_SIGINT;
        sigfillset(&SIGINT_action.sa_mask);
        sigaction(SIGINT, &SIGINT_action, NULL);
        
        // check if there needs to be I/O redirection
        if (isThereInput == 1) {
            // Open source file
          	int sourceFD = open(inputFile, O_RDONLY);
          	if (sourceFD == -1) { 
          		perror("source open()");
          		exit(1); 
          	} 
          
          	// Redirect stdin to source file
          	result = dup2(sourceFD, 0);
          	if (result == -1) { 
          		perror("source dup2()");
          		exit(1); 
          	}
            redirection = 1;
        }
      
        if (isThereOutput == 1) {
            // Open target file
            int targetFD = open(outputFile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (targetFD == -1) { 
              	perror("target open()");
              	exit(1); 
            }
            
            // Redirect stdout to target file
            result = dup2(targetFD, 1);
            if (result == -1) { 
              	perror("target dup2()"); 
              	exit(1); 
            }
            redirection = 1;
        }
 
        // if there was I/O redirection, then call exec without args
        if (redirection == 1) {
            args[1] = NULL;
            execvp(command, args);
            
            // exec only returns if there is an error, set exit status to 1
        		perror("execvp");
        		exit(1);
	      	  break;
        }
 
        else {
            // replace the current program with command given and args
    		    execvp(command, args);
          
            // exec only returns if there is an error, set exit status to 1
        		perror("execvp");
        		exit(1);
	      	  break;
    		}
       
    // this is back in the parent process
  	default:
        // wait for child process termination
        spawnPid = waitpid(spawnPid, &childStatus, 0);
 
        // find exit status based on termination
        if (WIFEXITED(childStatus)) {
            signalStop = 0;
            return WEXITSTATUS(childStatus);
        } 
        
        else {
            printf("terminated by signal %d\n", WTERMSIG(childStatus));
            fflush(stdout);
            
            // set a flag for exit status to know temrination by signal
            signalStop = 1;
            return WTERMSIG(childStatus);
        }
        break;
	  }
}

void checkbg(int pidarray[]) {
    int childStatus;
    pid_t childPid;

    // if background process has finished, print out its pid
    for (int i = 0; i < 100; i++) {
        
        childPid = waitpid(pidarray[i], &childStatus, WNOHANG);
        
        // if return val is > 0, the process has terminated
        if (childPid > 0 && pidarray[i] != 0) {
        
            // find exit status based on termination
            if (WIFEXITED(childStatus)) {
                printf("background %d is done: exit value %d\n", pidarray[i], WEXITSTATUS(childStatus));
                fflush(stdout);
                
                // set a flag for exit status to know temrination by signal
                signalStop = 0;
                exit_status = WEXITSTATUS(childStatus);
            } 
            
            else {
                printf("background %d is done: terminated by signal %d\n", pidarray[i], WTERMSIG(childStatus));
                fflush(stdout);
                
                // set a flag for exit status to know temrination by signal
                signalStop = 1;
                exit_status = WTERMSIG(childStatus);
            }
        }
    }
}


void forkBackground(char* command, char *args[], char* inputFile, char* outputFile, int isThereInput, int isThereOutput, int numPids, int pidarray[]) {
    int childStatus, result, redirection = 0;
    int sourceFD, targetFD;
    
    // fork a new process
  	pid_t childPid = fork();
   
    // add to the array
    pidarray[numPids] = childPid;
    numPids++;
  
  	switch(childPid) {
    case -1:
 		    perror("fork()\n");
  		  exit(1);
  		  break;
        
    // this is the child process
  	case 0:
        // signal handling for SIGTSTP, empty statement on next line to allow declaration after a label
        ;
        struct sigaction SIGTSTP_action = {0};
        
        // register SIGTSTP handler (ignore in child)
        SIGTSTP_action.sa_handler = SIG_IGN;
        sigfillset(&SIGTSTP_action.sa_mask);
        sigaction(SIGTSTP, &SIGTSTP_action, NULL);
        
     
        // print the pid of the background process when starting
        printf("background pid is %d\n", getpid());
        fflush(stdout);
        
        // redirect the input for all background processes
        if (isThereInput == 1) {
            // open source file from user
          	sourceFD = open(inputFile, O_RDONLY);
            redirection = 1;
        }
        else {
            // open source file from dev/null
          	sourceFD = open("/dev/null", O_RDONLY);
        }
        
        // check for error with file
        if (sourceFD == -1) { 
            perror("source open()");
            exit(1); 
        }
          
      	// redirect stdin to source file
      	result = dup2(sourceFD, 0);
      	if (result == -1) { 
      		  perror("source dup2()"); 
      		  exit(1); 
      	}
        
        // redirect the output for all background processes
        if (isThereOutput == 1) {
            // open target file from user
            targetFD = open(outputFile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            redirection = 1;
        }
        
        else {
            // open target file from dev/null
            targetFD = open("/dev/null", O_WRONLY | O_CREAT | O_TRUNC, 0644);
        }
        
        // check for error with file
        if (targetFD == -1) { 
          	perror("target open()"); 
          	exit(1); 
        }
        
        // redirect stdout to target file
        result = dup2(targetFD, 1);
        if (result == -1) { 
          	perror("target dup2()"); 
          	exit(1); 
        }
 
        // if there was I/O redirection, then call exec without args
        if (redirection == 1) {
            args[1] = NULL;
            execvp(command, args);
            
            // exec only returns if there is an error
        		perror("execvp");
        		exit(1);
        		break;
        }
 
        else {
            // replace the current program with command given and args
    		    execvp(command, args);
                
            // exec only returns if there is an error
        		perror("execvp");
        		exit(1);
        		break;
    		}
       
    // this is back in the parent process
  	default:
        
        break;
	  }
}


/* 
*  overall main control over commands
*  based upon user input
*/
void startTerminal()
{
    // variable for the current exit status (for built-in status) shouldn't reset after each loop
    int numPids = 0, numCommands = 0;
    int pidarray[100];
    
    // infinite loop until user enters 'exit' command
    while (1) {
        // create variables for holding user input and an array for the tokens
        char input[2048];
        char tokenList[512][100];
        
        // variable bool for whether a command is foreground vs background
        int backgroundStatus = 0;
    
        // make struct pointer and malloc
        struct CommandLine *cl = malloc(sizeof(struct CommandLine));
        
        // set up the signals for parent process
        signalSetup();
        
        // get user input for command prompt and remove the newline that fgets adds
        printf("\n: ");
        fflush(stdout);
        fgets(input, sizeof(input), stdin);
        input[strlen(input) - 1] = '\0';
        
        // variable expansion for input ($$ -> ProcessID)
        char* temp = variableExpansion(input);
        strcpy(input, temp);
        
        // if the input is not empty and its not a comment (with #)
        if(strlen(input) != 0 && input[0] != '#') {
          
            // begin parsing the input
            char *saveptr = NULL;
            char* token = strtok_r(input, " ", &saveptr);
            int num_tokens = 0, numArgs = 0, isThereInput = 0, isThereOutput = 0;
          
            // continue parsing, copying all tokens to an array
            while (token != NULL) {
                strcpy(tokenList[num_tokens], token);
                num_tokens++;
                token = strtok_r(NULL, " ", &saveptr);
            }
          
            // allocate memory for the command based upon tokens and copy into the struct
            cl->command = calloc(strlen(tokenList[0]) + 1, sizeof(char));
            strcpy(cl->command, tokenList[0]);
            
            // allocate memory for argument array
            memset(cl->args[numArgs], '\0', num_tokens*100*sizeof(char));
          
            // loop through all tokens to find each struct part (starting after the command token)
            for (int i = 1; i < num_tokens; i++) {
            
                // if token is <, set the next token as an input file and mark bool as there is an input
                if (strcmp(tokenList[i], "<") == 0) {
                    if (strcmp(tokenList[i+1], "") != 0) {
                        cl->inputFile = calloc(strlen(tokenList[i+1]) + 1, sizeof(char));
                        strcpy(cl->inputFile, tokenList[i+1]);
                        i++;
                        isThereInput = 1;
                    }
                }
                
                // if token is >, set the next token as an output file and mark bool as there is an output
                else if (strcmp(tokenList[i], ">") == 0) {
                    if (strcmp(tokenList[i+1], "") != 0) {
                        cl->outputFile = calloc(strlen(tokenList[i+1]) + 1, sizeof(char));
                        strcpy(cl->outputFile, tokenList[i+1]);
                        i++;
                        isThereOutput = 1;
                    }
                }
                
                // if token is &, set variable to run in background setting
                else if (strcmp(tokenList[i], "&") == 0) {
                
                    // if the command is echo and & is encountered, it should be printed (not run as a bg process)
                    if (strcmp(cl->command, "echo") == 0) {
                        strcpy(cl->args[numArgs], tokenList[i]);
                        numArgs++;
                    }
                    
                    // else it should toggle to run as a background process
                    else {
                        if (foregroundOnly != 1) {
                            backgroundStatus = 1;
                        }
                    }
                }
                
                // if token reaches here, make it an argument and add to array
                else {
                    strcpy(cl->args[numArgs], tokenList[i]);
                    numArgs++;
                }
            }
            
            // handle the three built-in commands for smallsh (exit, cd, status)
            int builtInNum = builtInCommands(cl->command, cl->args);
            
            // if a built-in command was not run, fork the parent process into a child
            if (builtInNum != 1) {
                // convert args to a proper array that exec() functions can use
                int newNumArgs = numArgs + 2;
                char *newArgs[newNumArgs];
                newArgs[0] = cl->command;
                for (int i = 0; i < numArgs; i++) {
                    newArgs[i+1] = cl->args[i];    // first arg should be the command, following will be other args
                }
                newArgs[newNumArgs-1] = NULL;    // last slot should contain a NULL value
            
                // call function that handles fork and exec
                if (backgroundStatus == 1) {
                     forkBackground(cl->command, newArgs, cl->inputFile, cl->outputFile, isThereInput, isThereOutput, numPids, pidarray);
                }
                else {
                     exit_status = forkForeground(cl->command, newArgs, cl->inputFile, cl->outputFile, isThereInput, isThereOutput);
                     
                }
            }
            
            // free the struct as it isn't needed for the next instance of the loop
            free(cl->command);
            if (isThereInput != 0) { free(cl->inputFile); }
            if (isThereOutput != 0) { free(cl->outputFile); }
            free(cl);
            
            // keep track of the number of commands run (or loops)
            numCommands++;
            
            // check for background process terminations
            checkbg(pidarray);
        }
    }
}

/*
 *  to compile: gcc --std=gnu99 -o smallsh smallsh.c
*/
int main() {
    startTerminal();
    
    return EXIT_SUCCESS;
}