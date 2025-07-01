/* $begin shellmain */
#include "csapp.h"
#include <errno.h>

#define MAXARGS 128 /* max args on a command line */
#define MAX_PIPE_CMDS 32 /* max commands seperated with pipeline */
#define MAXJOBS 16 /* max jobs at any point in time */
#define MAXJID 1 << 16 /* max job ID */
#define PROMPT "CSE4100-SP-P2> " /* shell prompt */

/*
 * Job states: UNDEF (for initialization), FG (forground), BG (background), ST (stopped)
 * Job state transitions and enabling actions:
 *      FG -> ST : crtl-z
 *      ST -> FG : fg command
 *      ST -> BG : bg command
 *      BG -> FG : fg command
 * At most 1 job can be in the FG state.
*/
typedef enum {UNDEF, FG, BG, ST} job_state;

struct job_t {
    pid_t pid;                  /* process id */
    int jid;                    /* job id, start from 1*/
    job_state state;            /* 0: UNDEF | 1: FG | 2: BG | 3: ST */
    char cmdline[MAXLINE];      /* command */

    int nprocs;                 /* total number of processes in the job */
    int exited;                 /* number of processes that have exited */

    int idx;                    /* index variable to store the pid in the pids */
    pid_t pids[MAX_PIPE_CMDS];  /* each pid of the child processes in the same process group */
};

struct job_t jobs[MAXJOBS];     /* Global array for storing the jobs */
int next_jid = 1;               /* next job ID to allocate */

/* Function prototypes */
void eval(char *cmdline);                                                   /* evaluate cmdline no pipelines */
void eval_pipe(char *cmdline);                                              /* evaluate cmdline with pipelines */
int parseline(char *buf, char **argv);                                      /* parse cmdline */
int builtin_command(char **argv);                                           /* return 1 and process if it is builtin command / return 0, if it is not a builtin command */

void do_bgfg(char **argv);                                                  /* process "fg", "bg" command */
void do_kill(char **argv);                                                  /* process "kill" command */
void waitfg(pid_t pid);                                                     /* wait for foreground job to terminate */

/* Helper routines */
void clear_job(struct job_t *job);                                          /* clear job information */
void init_jobs(struct job_t *jobs);                                         /* initialize job list */
int max_jid(struct job_t *jobs);                                            /* return the maximum job ID which is already stored in the job list */
int add_job(struct job_t *jobs, pid_t pid, job_state state, char *cmdline); /* add a job to the job list */
int delete_job(struct job_t *jobs, pid_t pid);                              /* delete a job from the job list */
pid_t fg_pid(struct job_t *jobs);                                           /* return the pid of the foreground job */
struct job_t *get_job_pid(struct job_t *jobs, pid_t pid);                   /* return the job corresponding to the given pid(pgid) */
struct job_t *get_job_jid(struct job_t *jobs, int jid);                     /* return the job corresponding to the given job id */
struct job_t *get_job_member_pid(struct job_t *jobs, pid_t pid);            /* return the job corresponding to the given pid*/
int pid_to_jid(struct job_t *jobs, pid_t pid);                              /* return the job id corresponding to the given pid(pgid) */
void list_jobs(struct job_t *jobs);                                         /* list all jobs in the job list, do the "jobs" command */

/* Signal handlers */
void sigchld_handler(int sig);  /* SIGCHLD handler */
void sigtstp_handler(int sig);  /* SIGTSTP handler */
void sigint_handler(int sig);   /* SIGINT handler */

/* $begin shellmain */
int main() 
{
    char cmdline[MAXLINE]; /* Command line */

    /* Install the signal handlers */
    Signal(SIGINT, sigint_handler); /* ctrl - c */
    Signal(SIGTSTP, sigtstp_handler); /* ctrl - z*/
    Signal(SIGCHLD, sigchld_handler); /* Terminated or stopped child */

    /* Ignore signal */
    Signal(SIGTTOU, SIG_IGN);
    Signal(SIGTTIN, SIG_IGN);

    /* Initialize the job list */
    init_jobs(jobs);

    /* Execute the shell's read / eval loop */
    while (1) {
	    /* Read */
	    printf(PROMPT);
        if (fgets(cmdline, MAXLINE, stdin) == NULL) {
            if (feof(stdin)) {
                exit(0);
            }
            else {
                continue;
            }
        }               

	    /* Evaluate */
        if (strchr(cmdline, '|')) { // if the command has no pipelines
            eval_pipe(cmdline);
        }
        else { // command including pipelines
            eval(cmdline);
        }
    } 
}
/* $end shellmain */

/* $begin eval */
/* eval - Evaluate a command line (no pipeline) */
void eval(char *cmdline) 
{
    char *argv[MAXARGS]; /* Argument list execve() */
    char buf[MAXLINE];   /* Holds modified command line */
    int bg;              /* Should the job run in bg or fg? */
    pid_t pid;           /* Process id */
    sigset_t mask;       /* mask for signal */

    strcpy(buf, cmdline);
    bg = parseline(buf, argv); 

    if (argv[0] == NULL)  
	    return;   /* Ignore empty lines */

    if (!builtin_command(argv)) { /* not a builtin command*/
        Sigemptyset(&mask);
        Sigaddset(&mask, SIGCHLD);
        Sigprocmask(SIG_BLOCK, &mask, NULL); /* block the SIGCHLD signal */

        if ((pid = Fork()) == 0) { /* child process */
            Sigprocmask(SIG_UNBLOCK, &mask, NULL); /* unblock the SIGCHLD signal in the child (child process inherits the parent's signal mask) */
            Setpgid(0, 0); /* puts the child process in a new process group */

            if (execvp(argv[0], argv) < 0) { /* execute the command */
                printf("%s: Command not found\n", argv[0]);
                exit(1);
            }
        }

        /* parent process */
        Setpgid(pid, pid); /* Setpgid() must be called to put the child process in a new group before calling tcsetpgrp() */
        add_job(jobs, pid, bg ? BG : FG, cmdline); /* add job into jobs */
        Sigprocmask(SIG_UNBLOCK, &mask, NULL); /* unblock the SIGCHLD signal in parent */

        if (bg) {
            printf("[%d] (%d) %s", pid_to_jid(jobs, pid), pid, cmdline);
        }
        else {
            /* Transfer terminal control to the process group before running the foreground job */
            tcsetpgrp(STDIN_FILENO, pid);
            
            waitfg(pid);
            
            /* After the job completes, the shell regains control */
            tcsetpgrp(STDIN_FILENO, getpgrp());
        }
    }
    return;
}
/* $end eval */

/* $begin eval_pipe*/
/* eval_pipe - Evaluate a command line with pipe(s) */
void eval_pipe(char *cmdline)
{
    char buf[MAXLINE];
    char tmp[MAXLINE];
    char *cmds[MAX_PIPE_CMDS];
    int num_cmds = 0;

    strcpy(buf, cmdline);

    /* seperate command */
    char *token = strtok(buf, "|");
    while (token && num_cmds < MAX_PIPE_CMDS) {
        while (*token == ' ') token++;
        cmds[num_cmds++] = token;
        token = strtok(NULL, "|");
    }

    strcpy(tmp, cmds[num_cmds - 1]);
    char *temp_argv[MAXARGS];
    int bg = parseline(tmp, temp_argv); /* check if the pipeline command is background */

    sigset_t mask;
    Sigemptyset(&mask);
    Sigaddset(&mask, SIGCHLD);
    Sigprocmask(SIG_BLOCK, &mask, NULL);

    int prev_fd = -1;
    pid_t pgid = 0;

    for (int i = 0; i < num_cmds; ++i) {
        int pipefd[2];
        int is_last = (i == num_cmds - 1);

        if (!is_last && pipe(pipefd) < 0) { /* create a pipe */
            printf("pipe error\n");
            exit(1);
        }

        pid_t pid;

        if ((pid = Fork()) == 0) { /* child process */
            Sigprocmask(SIG_UNBLOCK, &mask, NULL); /* unblock SIGCHLD in child process */
            if (pgid == 0)
                pgid = getpid(); /* set process group id to the current child process */
            setpgid(0, pgid); /* set the child process group id to pgid */

            /* Read from the previous command */
            if (prev_fd != -1) {
                dup2(prev_fd, STDIN_FILENO);
                close(prev_fd);
            }

            /* Connect output to the next command */
            if (!is_last) {
                close(pipefd[0]); /* close the read end of the pipe */
                dup2(pipefd[1], STDOUT_FILENO); /* redirect stdout to the write end of the pipe */
                close(pipefd[1]); /* close the write end of the pipe */
            }

            char *argv[MAXARGS];
            parseline(cmds[i], argv);

            if (!builtin_command(argv)) {
                if (execvp(argv[0], argv) < 0) {
                    printf("%s: command not found\n", argv[0]);
                    exit(1);    
                }
            }
        }

        // parent process 
        if(pgid == 0) { /* if the process is the first in the pipeline */
            pgid = pid;
            add_job(jobs, pgid, bg ? BG : FG, cmdline);
            struct job_t *job = get_job_pid(jobs, pgid);
            job->nprocs = num_cmds;
            job->exited = 0;
        }

        setpgid(pid, pgid); /* set the child process group id to pgid */

        /* find the job with pgid */
        struct job_t *job = get_job_pid(jobs, pgid);
        if (job) { /* add the pid to the job */
            job->pids[job->idx++] = pid;
        }

        if (prev_fd != -1)
            close(prev_fd); /* close the read end of the previous pipe */

        if (!is_last) {
            close(pipefd[1]);      /* close the write end of the pipe */
            prev_fd = pipefd[0];   // pass the read end to the next command
        }
    }

    Sigprocmask(SIG_UNBLOCK, &mask, NULL);
    
    if (bg) {
        printf("[%d] (%d) %s", pid_to_jid(jobs, pgid), pgid, cmdline);
    }
    else {
        waitfg(pgid);
    }
}
/* $end eval_pipe */

/* $begin builtin_command */
/* builtin_command - If first arg is a builtin command, run it and return true */
int builtin_command(char **argv) 
{
    if (!strcmp(argv[0], "quit")) { /* exit command */
        exit(0);
    }
    
    if (!strcmp(argv[0], "exit")) { /* quit command */
	    exit(0);
    }

    if (!strcmp(argv[0], "&")) { /* Ignore singleton & */
        return 1;
    }

    if (!strcmp(argv[0], "cd")) { /* cd command */
        char dest[MAXLINE];

        if (argv[2]) {
            fprintf(stderr, "cd: too many arguments\n");
            return 1;
        }

        if (!argv[1] || !strcmp(argv[1], "~") || !strcmp(argv[1], "~/")) { // set home location for "cd", "cd ~", "cd ~/"
            char *home = getenv("HOME");
            if (!home) {
                fprintf(stderr, "cd: HOME not set\n");
                return 1;
            }
            strcpy(dest, home);
        } else if (argv[1][0] == '$') { // if argv[1] is an environment variable
            char envname[MAXLINE];
            size_t len = strlen(argv[1]) - 1;
            strncpy(envname, argv[1] + 1, len);
            envname[len] = '\0';

            char *value = getenv(envname);
            if (!value) {
                fprintf(stderr, "cd: %s: No such environment variable\n", envname);
                return 1;
            }
            strcpy(dest, value);
        } else {
            strcpy(dest, argv[1]);
        }
        
        if (chdir(dest) < 0) { // change the current working directory to dest
            printf("cd: no such file or directory: %s\n", argv[1]);
        }
        return 1;
    }

    if (!strcmp(argv[0], "jobs")) { /* jobs command */
        list_jobs(jobs);
        return 1;
    }

    if (!strcmp(argv[0], "bg") || !strcmp(argv[0], "fg")) { /* bg or fg command */
        do_bgfg(argv);
        return 1;
    }

    if (!strcmp(argv[0], "kill")) { /* kill command */
        do_kill(argv);
        return 1;
    }

    return 0;                     /* Not a builtin command */
}
/* $end builtin_command */

/* $begin do_bgfg */
/* do_bgfg - Execute the builtin commands "bg", "fg" */
void do_bgfg(char **argv)
{
    pid_t pid;
    struct job_t *job;
    char *id = argv[1];

    if (id == NULL) { /* no argument */
        printf("%s command requires PID or %%jobid argument\n", argv[0]);
        return;
    }

    if (id[0] == '%') { /* the argument is a job id */
        int jid = atoi(&id[1]);
        job = get_job_jid(jobs, jid);
        if (job == NULL) {
            printf("%%%d: No such job\n", jid);
            return;
        }
    } else if (isdigit(id[0])) { /* the argument is a pid */
        pid = atoi(id);
        job = get_job_member_pid(jobs, pid);
        if (job == NULL) {
            printf("(%d): No such process\n", pid);
            return;
        }
    } else {
        printf("%s: argument must be a PID or %%jobid\n", argv[0]);
        return;
    }

    Kill(-(job->pid), SIGCONT); /* send the SIGCONT to the pid */

    if (!strcmp(argv[0], "bg")) {
        job->state = BG;
        printf("[%d] (%d) %s", job->jid, job->pid, job->cmdline);
    }
    else {
        job->state = FG;
        tcsetpgrp(STDIN_FILENO, job->pid);
        waitfg(job->pid);
        tcsetpgrp(STDIN_FILENO, getpgrp());
    }
}
/* $end do_bgfg */

/* $begin do_kill */
/* do_kill - Execute the kill command */
void do_kill(char **argv)
{
    if (argv[1] == NULL) {
        printf("kill command requires %%jobid argument\n");
        return;
    }

    if(argv[1][0] != '%') {
        printf("kill: argument must be a %%jobid\n");
        return;
    }

    int jid = atoi(&argv[1][1]);
    struct job_t *job = get_job_jid(jobs, jid);

    if (job == NULL) {
        printf("%%%d: No such job\n", jid);
        return;
    }

    Kill(-job->pid, SIGTERM);
}
/* $end do_kill */

/* $begin waitfg */
/* waitfg - Block until process pid is no longer the foreground process */
void waitfg(pid_t pid)
{
    while (pid == fg_pid(jobs)) {
        sleep(0);
    }
    return;
}
/* $end waitfg */

/* $begin parseline */
/* parseline - Parse the command line and build the argv array */
int parseline(char *buf, char **argv) 
{
    char *delim;         /* Points to first space delimiter */
    int argc;            /* Number of args */
    int bg;              /* Background job? */

    if(buf[strlen(buf) - 1] == '\n') {
        buf[strlen(buf)-1] = ' ';  /* Replace trailing '\n' with space */
    }

    while (*buf && (*buf == ' ')) /* Ignore leading spaces */
	    buf++;

    /* Build the argv list */
    argc = 0;

    while (*buf) {
        if (*buf == '"' || *buf == '\'') { /* handle arguments enclosed in quotes */
            char quote = *buf++; /* " or ' */
            argv[argc++] = buf;
            while (*buf && *buf != quote) buf++;
            if (*buf == quote) *buf++ = '\0';
        } else { /* normal argument (up to the next space) */
            argv[argc++] = buf;
            while (*buf && *buf != ' ') buf++;
            if (*buf == ' ') *buf++ = '\0';
        }

        while (*buf && (*buf == ' ')) /* Ignore leading spaces */ 
            buf++; 
    }

    argv[argc] = NULL;
    
    if (argc == 0)  /* Ignore blank line */
	    return 1;

    /* Should the job run in the background? */
    if ((bg = ((*argv[argc-1] == '&')) != 0)) {
	    argv[--argc] = NULL;
    } else if ((bg = (argv[argc - 1][strlen(argv[argc-1]) - 1] == '&'))) { /* case where '&' is attached without space. ex) ls -al&*/
        argv[argc - 1][strlen(argv[argc - 1]) - 1] = '\0';
    }

    return bg;
}
/* $end parseline */

/**********************************************
 * Helper routines that manipulate the job list
 **********************************************/

/* $begin clear_job */
/* clear_job - Clear the entries in a job struct */
void clear_job(struct job_t *job)
{
    job->pid = 0;
    job->jid = 0;
    job->state = UNDEF;
    job->cmdline[0] = '\0';
    job->nprocs = 0;
    job->exited = 0;
    job->idx = 0;
    memset(job->pids, 0, sizeof(job->pids));
} 
/* $end clear_job */
 
/* $begin init_jobs */
/* init_jobs - Initialize the job list */
void init_jobs(struct job_t *jobs)
{
    for (int i = 0; i < MAXJOBS; i++) {
        clear_job(&jobs[i]);
    }
}
/* end init_job */

/* $begin max_jid */
/* max_jid - Returns largest allocated job ID */
int max_jid(struct job_t *jobs)
{
    int max = 0;
    
    for (int i = 0; i < MAXJOBS; i++) {
        if (jobs[i].jid > max) {
            max = jobs[i].jid;
        }
    }

    return max;
}
/* $end max_jid */

/* $begin add_job */
/* add_job - Add a job to the job list */
int add_job(struct job_t *jobs, pid_t pid, job_state state, char *cmdline)
{
    if (pid < 1)
        return 0;

    for (int i = 0; i < MAXJOBS; i++) {
        if (jobs[i].pid == 0) {
            jobs[i].pid = pid;
            jobs[i].state = state;
            jobs[i].jid = next_jid++;
            if (next_jid > MAXJOBS)
                next_jid = 1;
            strcpy(jobs[i].cmdline, cmdline);    
            return 1;
        }
    }
    
    /* if jobs is full */
    printf("Too many jobs!!!\n");
    return 0;
}
/* $end add_job */

/* $begin delete_job */
/* delete_job - Delete a job whose PID=pid from the job list */
int delete_job(struct job_t *jobs, pid_t pid)
{
    if (pid < 1)
        return 0;
    
    for (int i = 0; i < MAXJOBS; i++) {
        if (jobs[i].pid == pid) {
            clear_job(&jobs[i]);
            next_jid = max_jid(jobs) + 1;
            return 1;
        }
    }

    return 0;
}
/* $end delete_job */

/* $begin fg_pid */
/* fg_pid - Return PID of current foreground job, 0 if no such job */
pid_t fg_pid(struct job_t *jobs)
{
    for (int i = 0; i < MAXJOBS; i++) {
        if (jobs[i].state == FG)
            return jobs[i].pid;
    }

    return 0;
}
/* $end fg_pid */

/* $begin get_job_pid */
/* get_job_pid - Find a job by pgid on the job list */
struct job_t *get_job_pid(struct job_t *jobs, pid_t pid)
{
    if (pid < 1)
        return NULL;

    for (int i = 0; i < MAXJOBS; i++) {
        if(jobs[i].pid == pid) {
            return &jobs[i];
        }
    }

    return NULL;
}
/* $end get_job_pid */

/* $begin get_job_member_pid */
/* get_job_member_pid - Find a job by pid */
struct job_t *get_job_member_pid(struct job_t *jobs, pid_t pid) {
    for (int i = 0; i < MAXJOBS; i++) {
        if (jobs[i].pid == 0)
            continue;

        if (jobs[i].pid == pid)
            return &jobs[i];

        for (int j = 0; j < jobs[i].nprocs; j++) {
            if (jobs[i].pids[j] == pid) {
                return &jobs[i];
            }
        }
    }

    return NULL;
}
/* $end get_job_member_pid */

/* $begin get_job_jid*/
/* get_job_jid - Find a job (by JID) on the job list */
struct job_t *get_job_jid(struct job_t *jobs, int jid)
{
    if (jid < 1)
        return NULL;
    
    for (int i = 0; i < MAXJOBS; i++) {
        if (jobs[i].jid == jid)
            return &jobs[i];
    }
        
    return NULL;
}
/* $end get_job_jid */

/* $begin pid_to_jid*/
/* pid_to_jid - Find the job with pid and return that job's jid */
int pid_to_jid(struct job_t *jobs, pid_t pid)
{
    if (pid < 1)
        return 0;
    
    for (int i = 0; i < MAXJOBS; i++) {
        if (jobs[i].pid == 0)
            continue;
        
        if (jobs[i].pid == pid)
            return jobs[i].jid;

        for (int j = 0; j < jobs[i].nprocs; j++) {
            if (jobs[i].pids[j] == pid) {
                return jobs[i].jid;
            }
        }
    }
    
    return 0;
}
/* $end pid_to_jid */

/* $begin list_jobs */
/* list_jobs - Print the job list */
void list_jobs(struct job_t *jobs)
{
    for (int i = 0; i < MAXJOBS; i++) {
        if (jobs[i].pid != 0) {
            printf("[%d] (%d) ", jobs[i].jid, jobs[i].pid);
            switch (jobs[i].state) {
                case FG:
                    printf("foreground ");
                    break;
                case BG:
                    printf("running ");
                    break;
                case ST:
                    printf("stopped ");
                    break;
                default:
                    printf("listjob error: job[%d].state=%d ", i, jobs[i].state);
            }
            printf("%s", jobs[i].cmdline);
        }
    }
}
/* $end list_jobs */

/******************************
 * end job list helper routines
 ******************************/


/******************
 * Signal Handlers
 *****************/
/* $begin signal_handlers */
 void sigchld_handler(int sig)
{
    int olderrno = errno;
    pid_t pid;
    int status;

    while ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED)) > 0) {
        struct job_t *job = get_job_member_pid(jobs, pid);

        if (job == NULL) {
            printf("no job with pid\n");
            continue;
        }

        if (WIFEXITED(status) || WIFSIGNALED(status)) {
            job->exited++;

            if(WIFSIGNALED(status)) {
                printf("Job [%d] (%d) terminated by signal %d\n", pid_to_jid(jobs, pid), pid, WTERMSIG(status));   
            }

            if (job->exited >= job->nprocs) {
                delete_job(jobs, job->pid);
            }
        }   
        if (WIFSTOPPED(status)) { /* process is stopped because of a signal */
            printf("Job [%d] (%d) stopped by signal %d\n", pid_to_jid(jobs, pid), pid, WSTOPSIG(status));
            job->state = ST;
        }
    }

    if (pid < 0 && errno != ECHILD)
        unix_error("waitpid error");

    errno = olderrno;

    return;
}
/* $end sigchld_handler */

/* $begin sigtstp_handler */
/* Send SIGTSTP to the currently running foreground job */
void sigtstp_handler(int sig)
{
    pid_t pid = fg_pid(jobs); /* return foreground job's pid */

    if (pid != 0) {
        struct job_t *job = get_job_pid(jobs, pid);
        if (job->state == ST) { /* do nothing */
            return;
        }
        else {
            Kill(-pid, SIGTSTP);
        }
    }
    return;
}
/* $end sigtstp_handler */

/* $begin sigint_handler */
/* Send SIGINT to the currently running foreground job */
void sigint_handler(int sig)
{
    pid_t pid = fg_pid(jobs);

    if (pid != 0) {
        Kill(-pid, SIGINT);
    }

    return;
}
/* $end sigint_handler */

/*********************
 * End Signal Handlers
 *********************/