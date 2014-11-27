#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "config.h"
#include "siparse.h"
#include "utils.h"
typedef int bool;
const bool false = 0;
const bool true = 1;

char buffer[2*MAX_LINE_LENGTH + 10] = {0};
char buffer_to_parse[MAX_LINE_LENGTH + 10] = {0};
int start, end;
bool end_of_command;

void clearParsedBuffer()
{
    int i=0;
    for(i=0;i<MAX_LINE_LENGTH+10;++i)
        buffer_to_parse[i] = 0;
}

void execute()
{
    line* ln = NULL;
    command* com = NULL;

    ln = parseline(buffer_to_parse);
    if((ln == NULL) || (*(ln->pipelines) == NULL) || (**(ln->pipelines) == NULL))
    {
        printf("LN NULL\n");
        printf("%s\n", SYNTAX_ERROR_STR);
        fflush(stdout);
        return ;
    }

    com = pickfirstcommand(ln);
    if((com == NULL) || (com->argv == NULL) || (com->argv[0] == NULL))
    {
        printf("COM NULL\n");
        printf("%s\n", SYNTAX_ERROR_STR);
        fflush(stdout);
        return ;
    }

    int child_pid = fork();
    if(child_pid == -1)
        exit(1);

    if(child_pid > 0) /// parent
    {
        int wait_status = waitpid(child_pid, NULL, 0);
        if(wait_status == -1)
            exit(1);
    }
    else /// child
    {
        int exec_status = execvp(com->argv[0], com->argv);
        if(exec_status == -1)
        {
            if(errno == ENOENT) /** 2 */
                fprintf(stderr, "%s: no such file or directory\n", com->argv[0]);
            else if(errno == EACCES) /** 13 */
                fprintf(stderr, "%s: permission denied\n", com->argv[0]);
            else
                fprintf(stderr, "%s: exec error\n", com->argv[0]);
            fflush(stdout);
            exit(EXEC_FAILURE);
        }
    }
}

void shiftBufferLeft()
{
    int i;
    if(end_of_command)
    {
        end_of_command = false;
        for(i=0;i<=end;++i)
            buffer[i] = 0;
        return ;
    }
    for(i=start;i<=end;++i)
    {
        buffer[i-start] = buffer[i];
    }
    for(i=end-start+1;i<=end;++i)
    {
        buffer[i-start] = 0;
    }
}

void getLines(bool terminal) /** jeśli terminal == true to czytamy z terminala, jeśli nie to z pliku */
{
    line* ln;
    command* com;
    int i;
    int k = read(start, buffer, MAX_LINE_LENGTH + 2);
    if(k == -1)
        exit(1);

    if(k == 0)
    {
        printf("\n");
        fflush(stdout);
        exit(0);
    }
    /**if((k < MAX_LINE_LENGTH + 2) && (syntax_error == 1))
    {
        syntax_error = 0;
        printed_syntax_error = 0;
        continue;
    }
    if(k > MAX_LINE_LENGTH + 1)
    {
        syntax_error = 1;
        if(printed_syntax_error == 0)
        {
            fprintf(stderr, "%s\n", SYNTAX_ERROR_STR);
            fflush(stdout);
            printed_syntax_error = 1;
        }
    }*/
    printf("czytanie: k== %d\n", k);

    end += k;
    int buffer_position = start;
    int buffer_to_parse_position = 0;
    while(buffer_position != end)
    {
        buffer_to_parse_position = 0;
        clearParsedBuffer();
        for(buffer_position=start;buffer_position<=end; ++buffer_position, ++buffer_to_parse_position)
        {
            if(buffer_position == end)
            {
                if(buffer[buffer_position] == '\n')
                    end_of_command = true;
                return ;
            }
            if(buffer[buffer_position] == '\n')
            {
                start = buffer_position + 1;
                buffer_position++;
                buffer_to_parse[buffer_to_parse_position] = 0;
                printf("\nENDLINE na %d\n", buffer_position);
                break;
            }
            buffer_to_parse[buffer_to_parse_position] = buffer[buffer_position];
        }
        execute();
    }
    shiftBufferLeft();
    end = end - start;
    start = end;
}

int main(int argc, char *argv[])
{
    end_of_command = false;
    start = end = 0;
	bool syntax_error = 0;
	bool printed_syntax_error = 0;
    struct stat stdin_buffer;
    bool stdin_status; /// czy czytamy z terminala czy nie
    int check = fstat(0, &stdin_buffer);
    if(check == -1) exit(1);
    /** makro testuje czy czytamy z terminala czy nie, jak zwraca nie zero to tak. */
    if(S_ISCHR(stdin_buffer.st_mode)) stdin_status = true;
    else stdin_status = false;

	while(1)
	{
		if(stdin_status) /** czytamy z terminala   */
		{
            printf("$ ");
            fflush(stdout);
        }
        getLines(stdin_status);
	}
}

