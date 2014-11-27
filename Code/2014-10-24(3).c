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
bool stdin_status; /// czy czytamy z terminala czy nie
int start, end;
bool end_of_command;
bool shift;
int buffer_position;
int buffer_to_parse_position;
int line_end; /// koniec aktualnej linii
///int curent_line_length

/**
Funkcja wykonuje komendę w procesie potomnym.
*/
void execute()
{
    line* ln = NULL;
    command* com = NULL;
    int i;
    /**printf("STRLEN %d\n", strlen(buffer_to_parse));*/
   /// for(i=0;i<strlen(buffer_to_parse);++i)
       /// printf("%c", buffer_to_parse[i]);
    ///printf("\n");
    ln = parseline(buffer_to_parse);
    if((ln == NULL) || (*(ln->pipelines) == NULL) || (**(ln->pipelines) == NULL))
    {
       /// printf("LN NULL\n");
        printf("%s\n", SYNTAX_ERROR_STR);
        fflush(stdout);
        return ;
    }

    com = pickfirstcommand(ln);
    if((com == NULL) || (com->argv == NULL) || (com->argv[0] == NULL))
    {
       /// printf("COM NULL\n");
        if(buffer[start] != '#') /** jeśli linia nie jest komentarzem */
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
Ustawia znacznik end na znaleziony znak lub na ostatniz nak w buforze
gdy nie ma końca linii.
*/
void findNewLine()
{
    int i=0;
    for(i=start;buffer[i] != 0;++i)
    {
        if(buffer[i] == '\n')
        {
            line_end = i;
            ///current_line_length += (end_line-start);
            return;
        }
    }
    line_end = i-1;
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
        end_of_command = false;
        start = end = line_end = 0;
        buffer_position = buffer_to_parse_position = 0;
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
    if(buffer[0] == 0) /** bufor pusty */
    {
        start = line_end = end = 0;
        buffer_position = buffer_to_parse_position = 0;
    }
    else
    {
        buffer_position = buffer_to_parse_position = end+1;
        start = line_end = end+1;
    }
}

 /**
 Jeśli terminalMode == true to czytamy z terminala, jeśli nie to z pliku.
 Funkcja wczytuje maksymalnie MAX_LINE_LENGTH znaków. Potem dzieli
 bufor na linie i wykonuje je po kolei.
 */
void getLines(bool terminalMode)
{
    int i;
    int k = 0 ;
    if(buffer_position == end) /** tylko wtedy czytamy dalej */
    {
        if(buffer[buffer_position] == '\n')
        {
            end_of_command = true;
        }
        shiftBufferLeft();
        if(end==0)
        {
            if(buffer[0] == 0) /** bufor jest pusty */
            {
                k= read(0, buffer, MAX_LINE_LENGTH + 1);
                end = k-1;
            }
            else /** jeden znak z nowej komendy jest już wczytany */
            {
                k = read(0, buffer+1, MAX_LINE_LENGTH + 1);
                end = k;
            }
        }
        else if(end>0)
        {
            k = read(0, buffer+end+1, MAX_LINE_LENGTH + 1);
            buffer_position = end + 1;
            buffer_to_parse_position = end + 1;
            end += k;
        }
        if(k == -1)
        {
            ///printf("BLAD READ!\n");
            ///printf("%s\n", strerror(errno));
            exit(1);
        }

        if(k == 0)
        {
            ///printf("%d\n", strlen(buffer_to_parse));
            if(strlen(buffer_to_parse))
            {
                buffer_to_parse[buffer_to_parse_position] = 0;
                execute();
            }
            if(stdin_status) printf("\n"); /** jesli czytamy z terminala to przejdź do nowej linii */
           /// printf("KONIEC INPUTU\n");
            fflush(stdout);
            exit(0);
        }
        ///printf("END= %d\n", end);
    }

    ///printf("BUFFER %s\n", buffer);

    ///printf("czytanie: k== %d\n", k);
    while(buffer_position != end)
    {
        findNewLine();
        ///printf("END=%d endline =%d\n", end, line_end);


        for(buffer_position=start;buffer_position<=line_end; ++buffer_position, ++buffer_to_parse_position)
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

            else if(buffer_position == line_end)
            {
                if(buffer[buffer_position] == '\n') /** koniec linii ale nie bufora */
                {
                    ///printf("\nstart %d ENDLINE na %d, end %d\n", start, line_end, end);
                    buffer_to_parse[buffer_to_parse_position] = 0;

                    if(start != line_end) /** Jesli start == end to linia pusta, nie robimy execute() żeby uniknąć Syntax error. */
                        execute();

                    buffer_position++;
                    start = buffer_position;

                    for(int i=0;i<MAX_LINE_LENGTH+10;++i)
                        buffer_to_parse[i] = 0;
                    buffer_to_parse_position = 0;
                    break;
                }
            }
        }
    }
}

int main(int argc, char *argv[])
{
   /// current_line_length = 0;
    end_of_command = false;
    shift = false;
    start = end = line_end = 0;
    buffer_position = buffer_to_parse_position = 0;
	bool syntax_error = 0;
	bool printed_syntax_error = 0;
    struct stat stdin_buffer;
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
        fflush(stdout);
	}
}
