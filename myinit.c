#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <signal.h>

#define MAXPROC 32
#define LOG_FILE "/tmp/myinit.log"

pid_t pid_list[MAXPROC];
char *config_file;
int num_children = 0;
int log_fd;

typedef struct {
    char *command;
    char **args;
    int num_args;
    char *input_file;
    char *output_file;
} ChildConfig;

ChildConfig child_config[MAXPROC];

void log_message(const char *message) {
    write(log_fd, message, strlen(message));
    fsync(log_fd);
}

void start_child(int index) {
    if (index >= num_children || index < 0) return;
    pid_t cpid;
    cpid = fork();
    if (cpid == -1) {
        log_message("Fork failed; cpid == -1\n");
        return;
    } else if (cpid == 0) {
        // Child process
        int in_fd = open(child_config[index].input_file, O_RDONLY);
        if (in_fd == -1) {
            char buffer[256];
            snprintf(buffer, sizeof(buffer), "Failed to open input file %s, ended process cpid: %d\n", child_config[index].input_file, getpid());
            log_message(buffer);
            exit(1);
        }
        int out_fd = open(child_config[index].output_file, O_WRONLY | O_CREAT | O_TRUNC, 0666);
        if (out_fd == -1) {
            char buffer[256];
            snprintf(buffer, sizeof(buffer), "Failed to open output file %s, ended process cpid: %d\n", child_config[index].output_file, getpid());
            log_message(buffer);
            exit(1);
        }
        if (dup2(in_fd, STDIN_FILENO) == -1 || dup2(out_fd, STDOUT_FILENO) == -1) {
            char buffer[256];
            snprintf(buffer, sizeof(buffer), "Failed to redirect standard input or output, ended process cpid: %d\n", getpid());
            log_message(buffer);
            close(in_fd);
            close(out_fd);
            exit(1);
        }
        close(in_fd);
        close(out_fd);

        execv(child_config[index].command, child_config[index].args);
        perror("execv failed");
        char buffer[256];
        snprintf(buffer, sizeof(buffer), "execv failed, ended process cpid: %d\n", cpid);
        log_message(buffer);
        exit(1);
    } else {
        // Parent process
        pid_list[index] = cpid;
        char buffer[256];
        snprintf(buffer, sizeof(buffer), "Started child %d: PID %d\n", index, cpid);
        log_message(buffer);
    }
}

void read_config(      const char *filename) {
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        log_message("Failed to open config file\n");
        return;
    }

    char line[1024];
    num_children = 0;
    while (fgets(line, sizeof(line), fp)) {
        if (num_children >= MAXPROC) {
            log_message("Maximum number of processes reached.\n");
            break;
        }

        char *token, *tokens[MAXPROC + 2];
        int token_count = 0;

        token = strtok(line, " \n");
        while (token != NULL && token_count < (MAXPROC + 2)) {
            tokens[token_count++] = strdup(token);
            token = strtok(NULL, " \n");
        }

        if (token_count >= 3) {
            ChildConfig *child = &child_config[num_children];
            child->command = tokens[0];
            child->input_file = tokens[token_count - 2];
            child->output_file = tokens[token_count - 1];

            child->num_args = token_count - 3;
            child->args = calloc(child->num_args + 2, sizeof(char *));

            child->args[0] = child->command;
            for (int i = 1; i <= child->num_args; i++) {
                child->args[i] = tokens[i];
            }
            child->args[child->num_args + 1] = NULL;

            num_children++;
        } else {
            log_message("Invalid config line\n");
            for (int i = 0; i < token_count; i++) {
                free(tokens[i]);
            }
        }
    }
    fclose(fp);
}

void kill_all_children() {
    for (int i = 0; i < MAXPROC; i++) {
        if (pid_list[i]!= 0) {
            kill(pid_list[i], SIGTERM);
            waitpid(pid_list[i], NULL, 0);
            char buffer[256];
            snprintf(buffer, sizeof(buffer), "Child %d (PID %d) exited by signal SIGTERM\n", i, pid_list[i]);
            log_message(buffer);
            pid_list[i] = 0;
        }
    }
}

void sighup_handler(int signum) {
    log_message("Received SIGHUP, reloading config and restarting children\n");
    read_config(config_file);
    kill_all_children();
    for (int i = 0; i < num_children; i++) {
        start_child(i);
    }
}

void sigterm_handler(int signum) {
    log_message("Received SIGTERM, ending all processes\n");
    kill_all_children();
    log_message("myinit ended");
    exit(0);
}

void daemonize() {
    struct rlimit flim;
    if (getppid() != 1) {
        signal(SIGTTOU, SIG_IGN);
        signal(SIGTTIN, SIG_IGN);
        signal(SIGTSTP, SIG_IGN);
        
        if (fork() != 0) exit(0);
        setsid();
    }
    
    getrlimit(RLIMIT_NOFILE, &flim);
    for (int fd = 0; fd < flim.rlim_max; fd++) close(fd);
    
    if (chdir("/") != 0) exit(1);
    
    log_fd = open(LOG_FILE, O_CREAT | O_TRUNC | O_WRONLY, 0600);
    if (log_fd < 0) exit(1);
    log_message("Daemon started\n");
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <config_file>\n", argv[0]);
        return 1;
    }
    config_file = argv[1];

    daemonize();
    
    signal(SIGHUP, sighup_handler);
    signal(SIGTERM, sigterm_handler);
    
    read_config(config_file);
    
    for (int i = 0; i < num_children; i++) {
        start_child(i);
    }

    int status;
    pid_t cpid;
    while ((cpid = waitpid(-1, &status, 0)) > 0) {
        for (int i = 0; i < num_children; i++) {
            if (pid_list[i] == cpid) {
                char buffer[256];
                snprintf(buffer, sizeof(buffer), "Child %d (PID %d) exited, restarting\n", i, cpid);
                log_message(buffer);
                start_child(i);
            }
        }
    }

    close(log_fd);
    return 0;
}