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
#include <string.h>
typedef struct process {
    char *command;
    pid_t pid;
} process;


void exec_function(char*, vector*);
int exec_function_copy(char* cmd, vector * hist);
static FILE* hist_file = NULL;
static FILE* f_file = NULL;
static size_t k1 = 0;
static pid_t childpid = 0;

int exec_cd(char* path){
    int k1 = chdir(path);
    if(k1){
        print_no_directory(path);
        return 0;
    }
    return 1;
}

void exec_history(vector * hist){
    size_t size = vector_size(hist);
    size_t index;
    for(index = 0; index < size; index++){
        print_history_line(index, vector_get(hist,index) );
    }
    return;
}

void exec_takehistory(char * cmd, vector* hist){
    size_t size = vector_size(hist);
    if(!cmd) {
        print_invalid_index();

        return;
    }
    int length = strlen(cmd);
    int k1;
    for(k1 = 0; k1 < length; k1 ++){
        if (!isdigit (cmd[k1])) {
            print_invalid_index();
            return;
        }
    }
    size_t num = atoi(cmd);
    if(size <= num) {
        print_invalid_index();
        return;
    }

    char * h = vector_get(hist, num);
    print_command(h);
    exec_function (h, hist);
}

void exec_prefix(char * cmd, vector * hist){
    int length = strlen(cmd);
    size_t size = vector_size(hist);
    if(!size) {
        print_no_history_match();
        return;
    }
    if(!length) {
        char * h = vector_get(hist,size-1);
        print_command(h);
        exec_function(h,hist);
        return;
    }
    size_t k1; 
    for(k1 = size ; k1 > 0; k1 --){
        if(strncmp(cmd, vector_get(hist, k1-1), length) == 0){
            char * h = vector_get(hist,k1-1);
            print_command(h);
            exec_function(h,hist);
            return;            
        }

    }
    print_no_history_match();
    return;

}

void end_prog(vector* hist){

    if(hist_file){
        for(k1 = 0; k1 < vector_size(hist); k1 ++){
            fprintf (hist_file , "%s\n" , vector_get(hist, k1) );
        }
        fclose(hist_file);
    }


    //kill all the child
    vector_destroy(hist);
    exit(0);
}

void sigint_handler(int signal){
    if(childpid) kill(childpid, SIGINT);
    return;
}

void logic_exec (char* cmd, vector * hist){
    if( strstr(cmd, "&&") ){
        vector_push_back(hist , cmd);
        char input1 [strstr(cmd, "&&") - cmd-1];

        int i;
        for(i = 0; i < strstr(cmd, "&&")-cmd-1; i++){
            input1[i] = cmd[i]; 
        }
        input1[i] = 0;
        char * input2 = strdup(strstr(cmd, "&&") + 3);

        int k = exec_function_copy(input1,hist);
        if (!k) {
            free(input2);
            return;
        }
        if (k) exec_function_copy(input2, hist);
        free(input2);
    }
    else if (strstr(cmd , "||" ) ){
        vector_push_back(hist , cmd);
        char input1 [strstr(cmd, "||") - cmd-1];

        int i;
        for(i = 0; i < strstr(cmd, "||")-cmd-1; i++){
            input1[i] = cmd[i]; 
        }
        input1[i] = 0;
        char * input2 = strdup(strstr(cmd, "||") + 3);

        int k = exec_function_copy(input1,hist);
        if (k){
            free(input2);
            return;
        }
        if (!k) exec_function_copy(input2, hist);
        free(input2);
    }
    else if (strstr(cmd , ";") ){
        vector_push_back(hist , cmd);
        char input1 [strstr(cmd,  ";") - cmd-1];

        int i;
        for(i = 0; i < strstr(cmd,  ";")-cmd-1; i++){
            input1[i] = cmd[i]; 
        }
        input1[i] = 0;
        char * input2 = strdup(strstr(cmd,  ";") + 2);

        exec_function_copy(input1,hist);

        exec_function_copy(input2, hist);
        free(input2);        
    }
    else{
        exec_function(cmd, hist);
    }
    return;
}

int exec_function_copy(char* cmd, vector * hist){
    int result = 1;
    sstring* inputs = cstr_to_sstring(cmd);
    vector* inputv = sstring_split(inputs, ' ');
    char * copy =vector_get (inputv, 0); 
    if(strcmp(copy, "cd") == 0 ) {
        result = exec_cd(cmd+3);
    }
    else if(strcmp(copy, "!history") == 0 ) {
        exec_history(hist);
    }
    else if( * copy == '#' && isdigit( *(copy+1) ) ) {
        exec_takehistory(cmd+1, hist);
    }
    else if( * copy == '!' ) {
        exec_prefix(cmd+1, hist);
    }
    else if(!strcmp(copy, "eixt") ){
        end_prog(hist);
    }
    else {
        fflush(stdin);
        fflush(stdout);
        int status;
        childpid = fork();
        if(childpid == -1) {
            print_fork_failed();
            exit(1); 
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
            size_t k1;
            char * inputc [vector_size(inputv) + 1];
            for (k1 = 0; k1 < vector_size(inputv); k1++){
                inputc[k1] = vector_get(inputv, k1);
            }
            inputc[k1] = NULL;
            execvp(vector_get(inputv, 0) , inputc);
            print_exec_failed(cmd);
            exit(1);
        }

    }
    sstring_destroy(inputs);
    vector_destroy(inputv);
    return result;
}   




void exec_function(char* cmd, vector * hist){
    sstring* inputs = cstr_to_sstring(cmd);
    vector* inputv = sstring_split(inputs, ' ');
    char * copy =vector_get (inputv, 0); 
    if(strcmp(copy, "cd") == 0 ) {
        exec_cd(cmd+3);
        vector_push_back(hist, cmd);
    }
    else if(strcmp(copy, "!history") == 0 ) {
        exec_history(hist);
    }
    else if( * copy == '#' && isdigit( *(copy+1) ) ) {
        exec_takehistory(cmd+1, hist);
    }
    else if( * copy == '!' ) {
        exec_prefix(cmd+1, hist);
    }
    else if(!strcmp(copy, "exit") ){
        end_prog(hist);
    }
    else {
        fflush(stdin);
        fflush(stdout);
        int status;
        pid_t child = fork();
        if(child == -1) {
            print_fork_failed();
            exit(1); 
            return;
        }
        if(child) {
            waitpid(child, &status, 0); 
            if(WIFEXITED(status) )
                if(WEXITSTATUS(status)){
                }
            vector_push_back(hist, cmd);
        }
        if(!child){
            print_command_executed(getpid());
            size_t k1;
            char * inputc [vector_size(inputv) + 1];
            for (k1 = 0; k1 < vector_size(inputv); k1++){
                inputc[k1] = vector_get(inputv, k1);
            }
            inputc[k1] = NULL;
            execvp(vector_get(inputv, 0) , inputc);
            print_exec_failed(cmd);
            exit(1);
        }

    }
    sstring_destroy(inputs);
    vector_destroy(inputv);
    return;
}   

void exec_f_file(FILE * f_file, vector* hist, char* optarg){
    while(1){          //when we are not confronted with EXIT or SIG
        fflush(stdin);
        print_prompt(  get_full_path(optarg) , getpid());        
        fflush(stdout);
        char * cmd = NULL;
        size_t len_stdin = 0;
        ssize_t cmd_length = getline(&cmd, &len_stdin, f_file);
        if(cmd_length == -1){
            fclose(f_file);
            free(cmd);
            end_prog(hist);
        }
        if(cmd_length > 0 && cmd[cmd_length-1] == '\n') cmd[cmd_length-1] = 0;
        print_command(cmd);
        logic_exec(cmd, hist);
        free(cmd);
        cmd = NULL;
        
    }    
}


int shell(int argc, char *argv[]) {
    //TODO: history and file mode
    if(argc != 1 || argc != 3 || argc != 5) {
        print_usage();
    }                       
    vector * hist = string_vector_create(); 
    signal(SIGINT, sigint_handler);
    int k1 = getopt(argc, argv, "h:f:");
    while(k1 != -1){
        switch(k1){
        case 'h':
                //char* history_file = strdup(optarg);
                hist_file = fopen (optarg , "a+"); //hi
                if(!hist_file) {
                    print_history_file_error();
                    exit(1);
                }
                break;
        case 'f':
                f_file = fopen (optarg, "r");
                exec_f_file(f_file, hist,optarg);
                break;
        default:
                break;
        }
        k1 = getopt(argc, argv, "h:f:");
    }                            //     it's kind of different. if we open shell with a -f, we should turn into a read
                                //  mode and read from file instead of stdin, otherwise are same.


    //begin
    //int status;
    // pid_t child_begin = fork();
    // if(child_begin = -1) print_fork_failed();
    // if(!child_begin) {    //child

    while(1){          //when we are not confronted with EXIT or SIG
        char buffer[1000];
        getcwd(buffer, sizeof(buffer));
        fflush(stdin);
        print_prompt(  buffer , getpid());        
        fflush(stdout);
        char * cmd = NULL;
        size_t len_stdin = 0;
        ssize_t cmd_length = getline(&cmd, &len_stdin, stdin);
        if(cmd_length == -1 ){
            //TODO 
            free(cmd);
            end_prog(hist);
        }
        
        if(cmd_length > 0 && cmd[cmd_length-1] == '\n') cmd[cmd_length-1] = 0;
        logic_exec(cmd, hist);
        free(cmd);
        cmd = NULL;
        
    }
 


    //free(history_file);

    return 0;
}
