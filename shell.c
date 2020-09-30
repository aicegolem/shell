/**
 * shell
 * CS 241 - Fall 2020
 * signal
 */
#include "format.h"
#include "shell.h"
#include "vector.h"
#include "sstring.h"
#include <unistd.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <ctype.h>
#include <fcntl.h>
#include <string.h>
typedef struct process
{
    char *command;
    pid_t pid;
} process;

int exec_function(char *, vector *, int);
int exec_function_copy(char *cmd, vector *hist);
static FILE *hist_file = NULL;
static FILE *f_file = NULL;
static size_t k1 = 0;
static pid_t childpid = 0;
static vector *process_record = NULL;

// typedef struct process_info {
//      int pid;
//     long int nthreads;
//     unsigned long int vsize;
//     char state;
//     char *start_str;
//     char *time_str;
//      char *command;
// } process_info;

void reaping_zomble_child()
{
    size_t size = vector_size(process_record);
    int status;
    size_t k = 0;
    for (size_t i = 0; i < size; i++)
    {
        process_info *take = vector_get(process_record, k);
        pid_t k2 = waitpid(take->pid, &status, WNOHANG);
        if (k2 == take->pid)
        {
            vector_erase(process_record, k);
            k--;
        }
        k++;
    }
}

process_info* create_process_info(pid_t pid, char* command) {
    process_info* new_process_info = calloc(sizeof(process_info), 1);

    time_t raw_time;
    struct tm* timeinfo;
    time(&raw_time);
    timeinfo = localtime (&raw_time);
    new_process_info->command = strdup(command); //heap
    new_process_info->pid = pid;    
    char* start_time = calloc(100, 1);
    size_t buffer_size = time_struct_to_string(start_time, 100, timeinfo);
    start_time = realloc(start_time, buffer_size + 1);
    new_process_info->start_str = start_time; 

    char path[PATH_MAX];
    memset(path, 0, PATH_MAX);
    sprintf(path, "/proc/%d/stat", pid);
    FILE* ptr = fopen(path, "r");
    char* buf = NULL;
    size_t size = 0;
    getline(&buf, &size, ptr);
    sstring* stat = cstr_to_sstring(buf);
    vector* stat_vector_after_split = sstring_split(stat, ' ');
    long int utime = strtol(vector_get(stat_vector_after_split, 13 ), NULL, 10) / sysconf(_SC_CLK_TCK);
    long int stime = strtol(vector_get(stat_vector_after_split, 14 ), NULL, 10) / sysconf(_SC_CLK_TCK);

    long int total_cpu_time = utime + stime;
    size_t minutes = total_cpu_time / 60;
    size_t seconds = total_cpu_time % 60;
    char* cpu_time = calloc(100, 1);
    int cpu_time_str_size = execution_time_to_string(cpu_time, 100, minutes, seconds);
    cpu_time = realloc(cpu_time, cpu_time_str_size + 1);

    new_process_info->time_str = cpu_time;
    // update state
    char* state = vector_get(stat_vector_after_split, 2 );
    new_process_info->state = *state;
    // get vmsize in kb
    char* vmsize = vector_get(stat_vector_after_split, 22 );
    new_process_info->vsize = strtol(vmsize, NULL, 10) / 1024;
    // get threads num
    char* threads = vector_get(stat_vector_after_split, 19 );
    new_process_info->nthreads = strtol(threads, NULL, 10);

    vector_destroy(stat_vector_after_split);
    sstring_destroy(stat);
    free(buf);
    fclose(ptr);

    new_process_info->time_str = cpu_time; // heap


    return new_process_info;
}

void flush()
{
    fflush(stdin);
    fflush(stdout);
}

int exec_cd(char *path)
{
    int k1 = chdir(path);
    if (k1)
    {
        print_no_directory(path);
        return 0;
    }
    return 1;
}

void exec_history(vector *hist)
{
    size_t size = vector_size(hist);
    size_t index;
    for (index = 0; index < size; index++)
    {
        print_history_line(index, vector_get(hist, index));
    }
    return;
}

void exec_takehistory(char *cmd, vector *hist)
{
    size_t size = vector_size(hist);
    if (!cmd)
    {
        print_invalid_index();

        return;
    }
    int length = strlen(cmd);
    int k1;
    for (k1 = 0; k1 < length; k1++)
    {
        if (!isdigit(cmd[k1]))
        {
            print_invalid_index();
            return;
        }
    }
    size_t num = atoi(cmd);
    if (size <= num)
    {
        print_invalid_index();
        return;
    }

    char *h = vector_get(hist, num);
    print_command(h);
    exec_function(h, hist, 1);
}

void exec_prefix(char *cmd, vector *hist)
{
    int length = strlen(cmd);
    size_t size = vector_size(hist);
    if (!size)
    {
        print_no_history_match();
        return;
    }
    if (!length)
    {
        char *h = vector_get(hist, size - 1);
        print_command(h);
        exec_function(h, hist, 1);
        return;
    }
    size_t k1;
    for (k1 = size; k1 > 0; k1--)
    {
        if (strncmp(cmd, vector_get(hist, k1 - 1), length) == 0)
        {
            char *h = vector_get(hist, k1 - 1);
            print_command(h);
            exec_function(h, hist, 1);
            return;
        }
    }
    print_no_history_match();
    return;
}

void destroy_process(process_info *m)
{
    free(m->command);
    free(m->time_str);
    free(m->start_str);
    free(m);
    return;
}

void update_process()
{
    process_info *new = NULL;
    process_info *old = NULL;
    for (size_t i = 0; i < vector_size(process_record); i++)
    {
        old = vector_get(process_record, i);
        new = create_process_info(old->pid, old->command);
        char *str = strdup(old->start_str);
        free(new->start_str);
        new->start_str = str;
        vector_set(process_record, i, new);
        destroy_process(old);
    }
}

void exec_ps()
{
    flush();
    reaping_zomble_child();
    print_process_info_header();
    update_process();
    for (size_t i = 0; i < vector_size(process_record); i++)
    {
        print_process_info(vector_get(process_record, i));
    }

    return;
}

void end_prog(vector *hist)
{

    if (hist_file)
    {
        for (k1 = 0; k1 < vector_size(hist); k1++)
        {
            fprintf(hist_file, "%s\n", vector_get(hist, k1));
        }
        fclose(hist_file);
    }

    for (size_t i = 0; i < vector_size(process_record); i++)
    {
        destroy_process(vector_get(process_record, i));
    }

    vector_destroy(process_record);
    vector_destroy(hist);
    exit(0);
}

void sigint_handler(int signal)
{
    if (childpid)
        kill(childpid, SIGINT);
    return;
}

void logic_exec(char *cmd, vector *hist)
{
    if (strstr(cmd, "&&"))
    {
        vector_push_back(hist, cmd);
        char input1[strstr(cmd, "&&") - cmd - 1];

        int i;
        for (i = 0; i < strstr(cmd, "&&") - cmd - 1; i++)
        {
            input1[i] = cmd[i];
        }
        input1[i] = 0;
        char *input2 = strdup(strstr(cmd, "&&") + 3);

        int k = exec_function(input1, hist, 0);
        if (!k)
        {
            free(input2);
            return;
        }
        if (k)
            exec_function(input2, hist, 0);
        free(input2);
    }
    else if (strstr(cmd, "||"))
    {
        vector_push_back(hist, cmd);
        char input1[strstr(cmd, "||") - cmd - 1];

        int i;
        for (i = 0; i < strstr(cmd, "||") - cmd - 1; i++)
        {
            input1[i] = cmd[i];
        }
        input1[i] = 0;
        char *input2 = strdup(strstr(cmd, "||") + 3);

        int k = exec_function(input1, hist, 0);
        if (k)
        {
            free(input2);
            return;
        }
        if (!k)
            exec_function(input2, hist, 0);
        free(input2);
    }
    else if (strstr(cmd, ";"))
    {
        vector_push_back(hist, cmd);
        char input1[strstr(cmd, ";") - cmd];

        int i;
        for (i = 0; i < strstr(cmd, ";") - cmd; i++)
        {
            input1[i] = cmd[i];
        }
        input1[i] = 0;
        char *input2 = strdup(strstr(cmd, ";") + 2);

        exec_function(input1, hist, 0);

        exec_function(input2, hist, 0);
        free(input2);
    }
    else
    {
        exec_function(cmd, hist, 1);
    }
    return;
}

int exec_function(char *cmd, vector *hist, int need_push)
{
    int result = 1;
    sstring *inputs = cstr_to_sstring(cmd);
    vector *inputv = sstring_split(inputs, ' ');
    char *copy = vector_get(inputv, 0);
    if (strcmp(copy, "cd") == 0)
    {
        result = exec_cd(cmd + 3);
        if (need_push)
            vector_push_back(hist, cmd);
    }
    else if (strcmp(copy, "!history") == 0)
    {
        exec_history(hist);
    }
    else if (*copy == '#' && isdigit(*(copy + 1)))
    {
        exec_takehistory(cmd + 1, hist);
    }
    else if (*copy == '!')
    {
        exec_prefix(cmd + 1, hist);
    }
    else if (!strcmp(copy, "exit"))
    {
        end_prog(hist);
    }
    else if (!strcmp(copy, "ps"))
    {
        exec_ps();
    }
    else if (!strcmp(copy, "kill"))
    {
        vector_push_back(hist, cmd);
        if (!vector_get(inputv, 1))
            print_invalid_command(cmd);
        pid_t a = atoi(vector_get(inputv, 1));
        if (!a)
        {
            print_invalid_command(cmd);
        }
        process_info *p = NULL;
        for (size_t i = 0; i < vector_size(process_record); i++)
        {
            process_info *old = vector_get(process_record, i);
            if (old->pid == a)
                p = old;
        }
        if (!p)
            print_no_process_found(a);
        int k = kill(a, SIGKILL);
        if (!k)
        {
            print_killed_process(a, p->command);
        }
    }
    else if (!strcmp(copy, "stop"))
    {
        vector_push_back(hist, cmd);
        if (!vector_get(inputv, 1))
            print_invalid_command(cmd);
        pid_t a = atoi(vector_get(inputv, 1));
        if (!a)
        {
            print_invalid_command(cmd);
        }
        process_info *p = NULL;
        for (size_t i = 0; i < vector_size(process_record); i++)
        {
            process_info *old = vector_get(process_record, i);
            if (old->pid == a)
                p = old;
        }
        if (!p)
            print_no_process_found(a);
        int k = kill(a, SIGSTOP);
        if (!k)
        {
            print_stopped_process(a, p->command);
        }
    }
    else if (!strcmp(copy, "cont"))
    {
        vector_push_back(hist, cmd);
        if (!vector_get(inputv, 1))
            print_invalid_command(cmd);
        pid_t a = atoi(vector_get(inputv, 1));
        if (!a)
        {
            print_invalid_command(cmd);
        }
        process_info *p = NULL;
        for (size_t i = 0; i < vector_size(process_record); i++)
        {
            process_info *old = vector_get(process_record, i);
            if (old->pid == a)
                p = old;
        }
        if (!p)
            print_no_process_found(a);
        int k = kill(a, SIGCONT);
        if (!k)
        {
            print_continued_process(a, p->command);
        }
    }
    else
    {

        if (cmd[strlen(cmd) - 1] == '&')
        { //background process
            update_process();
            reaping_zomble_child();
            int status;
            flush();
            pid_t p = fork();
            if (p == -1)
            {
                print_fork_failed();
                exit(1);
                return 0;
            }
            if (p)
            { //parent
                
                waitpid(p, &status, WNOHANG);
                process_info *new = create_process_info(p, cmd);
                vector_push_back(process_record, new);
                // if(status2 == p){                don't know whether need or not
                //     if(WIFEXITED(status) )
                //         if(WEXITSTATUS(status)){
                //             result = 0;
                //         }
                // }
            }
            else
            { //children
                if (setpgid(0, 0) == -1)
                {
                    print_setpgid_failed();
                }
                if (need_push)
                    vector_push_back(hist, cmd);
                print_command_executed(getpid());
                size_t k1;
                char *inputc[vector_size(inputv)];
                for (k1 = 0; k1 < vector_size(inputv) - 1; k1++)
                {
                    inputc[k1] = vector_get(inputv, k1);
                }
                inputc[k1] = NULL;
                execvp(vector_get(inputv, 0), inputc);
                print_exec_failed(cmd);
                exit(1);
            }
        }
        else if(vector_size(inputv) >= 3 && ! strcmp (vector_get(inputv, vector_size(inputv) - 2), ">>") ){ //APPEND
            if(need_push) vector_push_back(hist, cmd);
            flush();
            int status;
            childpid = fork();
            if(childpid == -1) {
                print_fork_failed();
                exit(1); 
                return 0;
            }
            if(childpid) {
                waitpid(childpid, &status, 0); 
                if(WIFEXITED(status) )
                    if(WEXITSTATUS(status)){
                        result = 0;
                    }
            }
            if(!childpid){
                print_command_executed(getpid());
                flush();
                char* path = vector_get(inputv, vector_size(inputv) -1 );
                int file = open(path, O_APPEND | O_CREAT | O_WRONLY,  S_IRUSR | S_IWUSR);
                if(file == -1 ) exit(1);
                if(dup2(file,1) == -1 ) exit(1);
                close(file);
                size_t k1;
                char * inputc [vector_size(inputv) - 1];
                for (k1 = 0; k1 < vector_size(inputv) -2 ; k1++){
                    inputc[k1] = vector_get(inputv, k1);
                }
                inputc[k1] = NULL;
                execvp(vector_get(inputv, 0) , inputc);
                print_exec_failed(cmd);
                exit(1);
            }

        }
        else if(vector_size(inputv) >= 3 && ! strcmp (vector_get(inputv, vector_size(inputv) - 2), ">") ){ //write
            if(need_push) vector_push_back(hist, cmd);
            flush();
            int status;
            childpid = fork();
            if(childpid == -1) {
                print_fork_failed();
                exit(1); 
                return 0;
            }
            if(childpid) {
                waitpid(childpid, &status, 0); 
                if(WIFEXITED(status) )
                    if(WEXITSTATUS(status)){
                        result = 0;
                    }
            }
            if(!childpid){
                print_command_executed(getpid());
                flush();
                char* path = vector_get(inputv, vector_size(inputv) -1 );
                int file = open(path, O_TRUNC | O_CREAT | O_WRONLY,  S_IRUSR | S_IWUSR);
                if(file == -1 ) exit(1);
                if(dup2(file,1) == -1 ) exit(1);
                close(file);
                size_t k1;
                char * inputc [vector_size(inputv) - 1];
                for (k1 = 0; k1 < vector_size(inputv) -2 ; k1++){
                    inputc[k1] = vector_get(inputv, k1);
                }
                inputc[k1] = NULL;
                execvp(vector_get(inputv, 0) , inputc);
                print_exec_failed(cmd);
                exit(1);
            }
        }
        else if(vector_size(inputv) >= 3 && ! strcmp (vector_get(inputv, vector_size(inputv) - 2), "<") ){ //read
            if(need_push) vector_push_back(hist, cmd);
            flush();
            int status;
            childpid = fork();
            if(childpid == -1) {
                print_fork_failed();
                exit(1); 
                return 0;
            }
            if(childpid) {
                waitpid(childpid, &status, 0); 
                if(WIFEXITED(status) )
                    if(WEXITSTATUS(status)){
                        result = 0;
                    }
            }
            if(!childpid){
                print_command_executed(getpid());
                flush();
                char* path = vector_get(inputv, vector_size(inputv) -1 );
                int file = open(path,   O_RDONLY);
                if(file == -1 ) exit(1);
                if(dup2(file,0) == -1 ) exit(1);
                close(file);
                size_t k1;
                char * inputc [vector_size(inputv) - 1];
                for (k1 = 0; k1 < vector_size(inputv) -2 ; k1++){
                    inputc[k1] = vector_get(inputv, k1);
                }
                inputc[k1] = NULL;
                execvp(vector_get(inputv, 0) , inputc);
                print_exec_failed(cmd);
                exit(1);
            }}
        else
        { // normal exec
            flush();
            int status;
            childpid = fork();
            if (childpid == -1)
            {
                print_fork_failed();
                exit(1);
                return 0;
            }
            if (childpid)
            {
                waitpid(childpid, &status, 0);
                if (WIFEXITED(status))
                    if (WEXITSTATUS(status))
                    {
                        result = 0;
                    }
                if (need_push)
                    vector_push_back(hist, cmd);
            }
            if (!childpid)
            {
                print_command_executed(getpid());
                size_t k1;
                char *inputc[vector_size(inputv) + 1];
                for (k1 = 0; k1 < vector_size(inputv); k1++)
                {
                    inputc[k1] = vector_get(inputv, k1);
                }
                inputc[k1] = NULL;
                execvp(vector_get(inputv, 0), inputc);
                print_exec_failed(cmd);
                exit(1);
            }
        }
    }
    sstring_destroy(inputs);
    vector_destroy(inputv);
    return result;
}

void exec_f_file(FILE *f_file, vector *hist, char *optarg)
{
    while (1)
    { //when we are not confronted with EXIT or SIG
        print_prompt(get_full_path(optarg), getpid());
        flush();
        char *cmd = NULL;
        size_t len_stdin = 0;
        ssize_t cmd_length = getline(&cmd, &len_stdin, f_file);
        if (cmd_length == -1)
        {
            fclose(f_file);
            free(cmd);
            end_prog(hist);
        }
        if (cmd_length > 0 && cmd[cmd_length - 1] == '\n')
            cmd[cmd_length - 1] = 0;
        print_command(cmd);
        logic_exec(cmd, hist);
        free(cmd);
        cmd = NULL;
    }
}

int shell(int argc, char *argv[])
{
    //close(2);
    if (argc != 1 && argc != 3 && argc != 5)
    {
        print_usage();
    }
    vector *hist = string_vector_create();
    signal(SIGINT, sigint_handler);
    process_record = shallow_vector_create();
    int k1 = getopt(argc, argv, "h:f:");
    while (k1 != -1)
    {
        switch (k1)
        {
        case 'h':
            //char* history_file = strdup(optarg);
            hist_file = fopen(optarg, "a+"); //hi
            if (!hist_file)
            {
                print_history_file_error();
                exit(1);
            }
            break;
        case 'f':
            f_file = fopen(optarg, "r");
            exec_f_file(f_file, hist, optarg);
            break;
        default:
            break;
        }
        k1 = getopt(argc, argv, "h:f:");
    }

    vector_push_back(process_record, create_process_info(getpid(), "./shell"));

    while (1)
    {
        char path[1023];
        getcwd(path, sizeof(path));
        flush();
        print_prompt(path, getpid());
        char *cmd = NULL;
        size_t len_stdin = 0;
        ssize_t cmd_length = getline(&cmd, &len_stdin, stdin);
        if (cmd_length == -1)
        {
            //TODO
            free(cmd);
            end_prog(hist);
        }

        if (cmd[cmd_length - 1] == '\n')
            cmd[cmd_length - 1] = 0;
        logic_exec(cmd, hist);
        free(cmd);
        cmd = NULL;
    }

    return 0;
}
