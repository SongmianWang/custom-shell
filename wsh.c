#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/wait.h>

#define MAX_LINE_LENGTH 1024
#define MAX_ARGS 64
#define DEFAULT_HISTORY_SIZE 5

typedef struct ShellVar {
    char *name;
    char *value;
    struct ShellVar *next;
    struct ShellVar *prev;
} ShellVar;

typedef struct {
    char **commands; // Array of command strings
    int capacity;    // Maximum number of commands
    int count;       // Current command numbder
    int start;       // Index of the first command
    int set_zero;
} History;

int batch_mode_on = 0;
ShellVar *shellVars = NULL;
History cmdHistory = {NULL, DEFAULT_HISTORY_SIZE, 0, 0, 0};

void init_history() {
    cmdHistory.commands = (char**)malloc(sizeof(char*) * cmdHistory.capacity);
    for (int i = 0; i < cmdHistory.capacity; i++) {
        cmdHistory.commands[i] = NULL;
    }
    cmdHistory.count = 0;
}

void free_history() {
    for (int i = 0; i < cmdHistory.capacity; i++) {
        free(cmdHistory.commands[i]);
    }
    free(cmdHistory.commands);
}

void print_history() {
    if(cmdHistory.set_zero == 1){
        return;
    }
    for (int i = 0; i < cmdHistory.count; i++) {
        int index = (cmdHistory.start + i) % cmdHistory.capacity;
        printf("%d) %s\n", i + 1, cmdHistory.commands[index]);
    }
}

void add_command_to_history(char **args) {
    if (args[0] == NULL || strcmp(args[0], "") == 0) return;

    // Calculate the total length needed for the concatenated string
    int length = 0;
    for (int i = 0; args[i] != NULL; i++) {
        length += strlen(args[i]) + 1; 
    }

    // Allocate memory for the concatenated command string
    char *command = (char *)malloc(length);

    // Concatenate the command strings
    command[0] = '\0'; // Initialize the string to empty
    for (int i = 0; args[i] != NULL; i++) {
        strcat(command, args[i]);
        if (args[i + 1] != NULL) {
            strcat(command, " "); // Add space between arguments
        }
    }

    // Avoid storing consecutive duplicates
    int firstIndex = cmdHistory.start;
    if (cmdHistory.count > 0 && strcmp(cmdHistory.commands[firstIndex], command) == 0) {
        free(command); 
        return;
    }

    // Prepend the new command, adjusting the start index
    cmdHistory.start = (cmdHistory.start - 1 + cmdHistory.capacity) % cmdHistory.capacity;
    if (cmdHistory.commands[cmdHistory.start] != NULL) {
        free(cmdHistory.commands[cmdHistory.start]); // Free memory if overwriting old command
    }
    cmdHistory.commands[cmdHistory.start] = command;

    // Adjust the count if we haven't filled the buffer yet
    if (cmdHistory.count < cmdHistory.capacity) {
        cmdHistory.count++;
    }
}

void set_history_size(int newSize) {
    if (newSize == 0){
        for (int i = 0; i < cmdHistory.count; i++) {
            free(cmdHistory.commands[(cmdHistory.start + i) % cmdHistory.capacity]);
        }
        free(cmdHistory.commands);
        init_history();
        cmdHistory.set_zero = 1;
        return;
    }

    cmdHistory.set_zero = 0;
    char **newCommands = (char**)malloc(sizeof(char*) * newSize);
    int newCount = cmdHistory.count < newSize ? cmdHistory.count : newSize;

    // Copy the first (newSize) commands to the new array
    for (int i = 0; i < newCount; i++) {
        int idx = (cmdHistory.start + i) % cmdHistory.capacity; // Adjusted to copy from the start
        newCommands[i] = cmdHistory.commands[idx];
    }

    // Free any commands that are not going to be copied over to the new array
    for (int i = 0; i < cmdHistory.count; i++) {
        int shouldFree = 1;
        for (int j = 0; j < newCount; j++) {
            int idx = (cmdHistory.start + i) % cmdHistory.capacity;
            if (newCommands[j] == cmdHistory.commands[idx]) {
                shouldFree = 0;
                break;
            }
        }
        if (shouldFree) {
            free(cmdHistory.commands[(cmdHistory.start + i) % cmdHistory.capacity]);
        }
    }

    // Free the old commands array and update history with the new settings
    free(cmdHistory.commands);

    // Update history to use the new array and settings
    cmdHistory.commands = newCommands;
    cmdHistory.capacity = newSize;
    cmdHistory.count = newCount;
    cmdHistory.start = 0; // Since we're keeping the oldest commands, start should be reset to 0
}

ShellVar* find_shell_var(ShellVar *head, const char *name) {
    ShellVar *current = head;
    while (current != NULL) {
        if (strcmp(current->name, name) == 0) {
            return current;
        }
        current = current->next;
    }
    return NULL;
}

void handle_dollor_value(char **args) {
    for (int i = 0; args[i] != NULL; i++) {
        if (args[i][0] == '$' && args[i][1] != '\0') {
            char *varName = args[i] + 1; // Skip past the '$'
            char *varValue = getenv(varName); // Try to get the environment variable value

            if (!varValue) {
                // If not found in environment, check shell variables
                ShellVar *var = find_shell_var(shellVars, varName);
                if (var) {
                    varValue = var->value;
                } else {
                    // If not found, treat as empty string
                    varValue = "";
                }
            }
            args[i] = varValue;
        }
    }

    for (int i = 0; args[i] != NULL; i++) {
        if (strcmp(args[i], "") == 0 && args[i + 1] != NULL) {
            // Shift all elements to the left to overwrite the empty string
            for (int j = i; args[j] != NULL; j++) {
                args[j] = args[j + 1];
            }
            // After shifting, decrease i to check the new element at this index on the next iteration
            i--;
        }
    }
}

char **parse_input(char *input) {
    // Split the input into command and arguments
    // Return an array of strings where each string is a command or argument
    char **args = (char **)malloc((MAX_ARGS + 1) * sizeof(char *)); // +1 for NULL terminator
    if (args == NULL) {
        printf("malloc error in parsing");
        exit(-1);
    }

    int arg_count = 0;
    char *token;
    const char *delimiter = " \t\n"; // Space, tab, newline

    while ((token = strsep(&input, delimiter)) != NULL && arg_count < MAX_ARGS) {
        // Skip empty tokens
        if (*token == '\0') {
            continue;
        }

        // Store token in args array
        args[arg_count] = strdup(token); 
        if (args[arg_count] == NULL) {
            printf("error in argcount");
            exit(-1);
        }
        arg_count++;
    }

    args[arg_count] = NULL; // set the last args to NULL
    return args;
}

char ***parse_pipe_input(char *input) {
    char ***cmd = malloc(MAX_ARGS * sizeof(char **));
    if (cmd == NULL) {
        printf("malloc error in cmd");
        exit(-1);
    }

    int cmd_count = 0;
    char *segment;
    const char *pipe_delim = "|";

    while ((segment = strsep(&input, pipe_delim)) != NULL && cmd_count < MAX_ARGS - 1) {
        cmd[cmd_count] = parse_input(segment); 
        cmd_count++;
    }

    cmd[cmd_count] = NULL;
    return cmd;
}

void execute_pipe_command(char ***cmds) {
    int p[2], fd_in = 0;
    pid_t pid, firstChildPid = 0;

    for (int i = 0; cmds[i] != NULL; i++) {
        pipe(p);
        pid = fork();
        if (pid == 0) {      //child process
            // Create a new process group
            if (i == 0) {
                setpgid(0, 0); // Creating a new group
                firstChildPid = getpid(); // Store first child PID for later setting PGID
            } else {
                // Join the existing process group
                setpgid(0, firstChildPid);
            }

            dup2(fd_in, 0);  //Redirects the standard input to the standard input of the process.
            if (cmds[i + 1] != NULL) {
                dup2(p[1], 1);   //Redirects the standard output of the process to the write end of the pipe
            }
            close(p[0]);
            execvp(cmds[i][0], cmds[i]);
            printf("execvp failed");
            exit(-1);
        } else if (pid < 0) {       //error
            printf("fork failed");
            exit(-1);
        } else{     //parent process
             if (i == 0) {
                firstChildPid = pid; // Set PGID of subsequent children
            }
            setpgid(pid, firstChildPid); 
        }
            //wait(NULL);
            close(p[1]);
            if (fd_in != 0) close(fd_in);
            fd_in = p[0];    //updates fd_in to the read end of the current pipe      
    }
    int status;
    while (waitpid(-firstChildPid, &status, 0) > 0); 
}

void execute_command(char **args) {
    // Check if the command is an exit command
    if (strcmp(args[0], "exit") == 0) {
        exit(0); // Terminate the shell
    }

    if (strcmp(args[0], "history") == 0) {
        if(args[1] != NULL && strcmp(args[1], "set") == 0){
            int size = atoi(args[2]);
            set_history_size(size); 
            return;
        }
        print_history(); 
        return;
    }

    if (strcmp(args[0], "cd") == 0) {
        // Change directory
        if (args[1] == NULL) {
            printf("expected argument to \"cd\"\n");
        } else {
            if (chdir(args[1]) != 0) {
                perror("cd failed");
            }
        }
        return;
    }

    handle_dollor_value(args);


    if(batch_mode_on == 0){
        add_command_to_history(args);}

    pid_t pid = fork();
    if (pid == -1) {
        // Error forking
        printf("fork");
        exit(-1);
    } else if (pid == 0) {
        // Child process
        if (execvp(args[0], args) == -1) {
            // Error executing command
            printf("execvp: No such file or directory\n");
            exit(-1);
        }
    } else {
        // Parent process
        int status;
        if (waitpid(pid, &status, 0) == -1) {
            // Error waiting for child process
            printf("waitpid");
            exit(-1);
        }
    }
}

void set_shell_var(char *name, char *value) {
    ShellVar *current = shellVars;

    size_t len = strcspn(value, "\n"); 
    if (value[len] == '\n') {
        value[len] = '\0'; // Replace \n with \0 to remove it
    }

    // Search for an existing variable with the same name
    while (current != NULL && strcmp(current->name, name) != 0) {
        current = current->next;
    }

    // If found, update the variable's value
    if (current != NULL) {
        free(current->value); // Free the old value
        current->value = strdup(value); // Set the new value
        if (current->value == NULL) {
            printf("Error updating variable value");
            exit(-1);
        }
    } else {
        // If not found, add a new variable
        ShellVar *newVar = (ShellVar *)malloc(sizeof(ShellVar));
        if (newVar == NULL) {
            printf("Error allocating memory for new variable");
            exit(-1);
        }
        newVar->name = strdup(name);
        newVar->value = strdup(value);
        newVar->next = shellVars; // Next of new var is the current head
        newVar->prev = NULL; // Previous of new var is NULL since it will be the new head

        if (newVar->name == NULL || newVar->value == NULL) {
            printf("Error setting variable name or value");
            exit(-1);
        }

        // Update the previous head's previous pointer to the new variable, if the list isn't empty
        if (shellVars != NULL) {
            shellVars->prev = newVar;
        }

        // Finally, update the head of the list to the new variable
        shellVars = newVar;
    }
}


void list_shell_vars() {
    // Find the last variable in the list
    ShellVar *current = shellVars;
    if (current == NULL) {
        return; 
    }
    while (current->next != NULL) {
        current = current->next;
    }

    // Traverse backward from the last variable
    while (current != NULL) {
        if(current->value[0] != '\0'){
            printf("%s=%s\n", current->name, current->value);
        }
        current = current->prev;
    }
}

void handle_var_command(char *input) {
    char *command = strtok(input, " "); // "local" or "export"
    char *var_assignment = strtok(NULL, ""); // "VARNAME=value"
    
    if (var_assignment == NULL) {
        printf("Error: No variable assignment provided.\n");
        return;
    }
    
    char *var_name = strtok(var_assignment, "=");
    char *var_value = strtok(NULL, "");  //return value is "\n" when reach end
    
    if (var_name == NULL || var_value == NULL) {
        printf("Error: Invalid variable assignment format.\n");
        return;
    }
    
    if (strcmp(command, "local") == 0) {
        // Set shell variable (implement this function)
        set_shell_var(var_name, var_value);
    } else if (strcmp(command, "export") == 0) {
        // Set environment variable
        var_value[strcspn(var_value, "\n")] = '\0';
        if (setenv(var_name, var_value, 1) != 0) {
            perror("setenv failed");
        }
    }
}

void interactive_mode() {
    char *input;
    init_history();
    while (1) {
        printf("wsh> ");
        fflush(stdout);
        
        // Read input
        size_t bufsize = MAX_LINE_LENGTH;
        input = (char *)malloc(bufsize * sizeof(char));
        if (getline(&input, &bufsize, stdin) == -1) {
            //Command-D or no input
            exit(0);
        }
        
        //printf("input: %s", input);

        // Detect pipes
        if (strchr(input, '|') != NULL) {
            char ***args = parse_pipe_input(input);
            // for (int i = 0; args[i] != NULL; i++) { // Loop through each command
            //     printf("Command %d:\n", i+1);
            //     for (int j = 0; args[i][j] != NULL; j++) { // Loop through each argument of the command
            //         printf("\tArgument %d: %s\n", j+1, args[i][j]);
            //     }
            // }
            execute_pipe_command(args);
            free(args);
        }else if (strcmp(input, "vars\n") == 0) { // Note: input includes newline character
            list_shell_vars();
        }else if (strncmp(input, "local ", 6) == 0 || strncmp(input, "export ", 7) == 0) { //handle var command            
            handle_var_command(input);
        }else{
            // Parse normal input
            char **args = parse_input(input);
            // Execute normal command
            execute_command(args);
            free(args);
        }
        
        // Free memory
        free(input);
    }
    free_history();
}

void batch_mode(const char *batch_file) {
    FILE *file = fopen(batch_file, "r");
    if (file == NULL) {
        fprintf(stderr, "Error opening file %s\n", batch_file);
        exit(-1);
    }
    
    char *input;
    size_t bufsize = MAX_LINE_LENGTH;
    while (getline(&input, &bufsize, file) != -1) {
        // Detect pipes
        if (strchr(input, '|') != NULL) {
            char ***args = parse_pipe_input(input);
            execute_pipe_command(args);
            free(args);
        }else if (strcmp(input, "vars\n") == 0) { // Note: input includes newline character
            list_shell_vars();
        }else if (strncmp(input, "local ", 6) == 0 || strncmp(input, "export ", 7) == 0) { //handle var command            
            handle_var_command(input);
        }else{
            // Parse normal input
            char **args = parse_input(input);
            // Execute normal command
            execute_command(args);
            free(args);
        }

        // Free memory
        free(input);
        input = NULL;
    }
    
    fclose(file);
}

int main(int argc, char *argv[]) {
    if (argc == 1) {
        // Interactive mode
        interactive_mode();
    } else if (argc == 2) {
        // Batch mode
        batch_mode_on = 1;
        batch_mode(argv[1]);
    } else {
        printf("wrong arguments");
        return 1;
    }
    return 0;
}



