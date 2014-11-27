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
bool shift;
int buffer_position;
int buffer_to_parse_position;
int curent_line_length;

/**
Funkcja wykonuje komendę w procesie potomnym
*/
void execute()
{
    line* ln = NULL;
    command* com = NULL;
    int i;
    printf("STRLEN %d\n", strlen(buffer_to_parse));
    for(i=0;i<strlen(buffer_to_parse);++i)
        printf("%c", buffer_to_parse[i]);
    printf("\n");
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

/**
Funkcja szuka znaku nowej linii w buforze od miejsca start do końca.
Jeśli znajdzie, zwraca indeks miejsca, jeśli nie - zwraca -1
*/
int findNewLine()
{
    int i;
    for(i=start;buffer[i] != 0;++i)
    {

        if(buffer[i] == '\n')
        {
            end = i;
            current_line_length += (end-start);
            buffer[i] = 0;
            return i;
        }
    }
    current_line_length += (i-start);
    end = i-1;
    return -1;
}

/**
Funkcja pozbywa się już wykonanych linii z bufora
i przesuwa pozostałą zawartość na początek.
*/
void shiftBufferLeft()
{
    int i;
    if(end_of_command)
    {
        start = end = 0;
        end_of_command = false;
        for(i=0;i<2*MAX_LINE_LENGTH + 10;++i)
            buffer[i] = 0;
        for(i=0;i<MAX_LINE_LENGTH + 10;++i)
            buffer_to_parse[i] = 0;
        return ;
    }
    for(i=start;i<=end;++i)
    {
        buffer[i-start] = buffer[i];
        buffer_to_parse[i-start] = buffer[i];
    }
    for(i=end-start+1;i<2*MAX_LINE_LENGTH+10;++i)
        buffer[i] = 0;
    for(i=end-start+1;i<MAX_LINE_LENGTH+10;++i)
        buffer_to_parse[i] = 0;
    end = end-start;
    start = 0;
}

 /**
 Jeśli terminalMode == true to czytamy z terminala, jeśli nie to z pliku.
 Funkcja wczytuje maksymalnie MAX_LINE_LENGTH znaków. Potem dzieli
 bufor na linie i wykonuje je po kolei.
 */
void getLines(bool terminalMode)
{
    line* ln;
    command* com;
    int i;
    int k = 0 ;
    if(buffer_position == end) /** tylko wtedy czytamy dalej */
    {
        if(end==0)
        {
            if(buffer[0] == 0) /** bufor jest pusty */
            {
                start = 0;
                buffer_position = 0;
                buffer_to_parse_position = 0;
                k= read(0, buffer, MAX_LINE_LENGTH + 2);
                end = k-1;
            }
            else /** jeden znak z nowej komendy jet już wczytany */
            {
                start = 0;
                buffer_position = 1;
                buffer_to_parse_position = 1;
                k = read(0, buffer+1, MAX_LINE_LENGTH + 2);
                end = k;
            }
        }
        if(end>0)
        {
            k = read(0, buffer+end+1, MAX_LINE_LENGTH + 2);
            buffer_position = 1;
            buffer_to_parse_position = 1;
            end += k;

        if(k == -1)
        {
            printf("BLAD READ!\n");
            printf("%s\n", strerror(errno));
            exit(1);
        }

        if(k == 0)
        {
            printf("%d\n", strlen(buffer_to_parse));
            if(strlen(buffer_to_parse))
            {
                buffer_to_parse[buffer_to_parse_position] = 0;
                execute();
            }
            printf("\n");
            printf("KONIEC INPUTU\n");
            fflush(stdout);
            exit(0);
        }
        if(end == 0)
        {
            buffer_position = buffer_to_parse_position = 0;
            end = k-1;
        }
        if(buffer[buffer_position] == '\n')
        {
            shiftBufferLeft();
            start = 0;
            buffer_position = buffer_to_parse_position = 0;
            end = k-1;
        }
        else
        {
            shiftBufferLeft();
            end -= start;
            start = end+1;
            buffer_position = buffer_to_parse_position = start;
            end += k;
        }
        printf("END= %d\n", end);
    }

    printf("BUFFER %s\n", buffer);

    printf("czytanie: k== %d\n", k);
    while(buffer_position != end)
    {
        printf("END= %d\n", end);
        for(buffer_position=start;buffer_position<=end; ++buffer_position, ++buffer_to_parse_position)
        {
            buffer_to_parse[buffer_to_parse_position] = buffer[buffer_position];
            ///printf("WCZYTANO %c na pozycje %d\n", buffer[buffer_position], buffer_to_parse_position);
            if(buffer_position == end)
            {
                if(buffer[buffer_position] == '\n')
                {
                    buffer_to_parse[buffer_to_parse_position] = 0;
                    execute();
                    end_of_command = true;
                    for(int i=0;i<MAX_LINE_LENGTH+10;++i)
                        buffer_to_parse[i] = 0;
                    buffer_to_parse_position = 0;
                }
                return ;
            }
            if(buffer[buffer_position] == '\n')
            {
                buffer_position++;
                start = buffer_position;
                printf("\nENDLINE na %d, end %d\n", buffer_position-1, end);
                execute();
                for(int i=0;i<MAX_LINE_LENGTH+10;++i)
                    buffer_to_parse[i] = 0;
                buffer_to_parse_position = 0;
                break;
            }
        }
    }
}

int main(int argc, char *argv[])
{
    current_line_length = 0;
    end_of_command = false;
    shift = false;
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

