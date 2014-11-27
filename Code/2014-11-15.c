#include "includes.h"

typedef int bool;
const bool false = 0;
const bool true = 1;

char buffer[2*MAX_LINE_LENGTH + 10] = {0};
char buffer_to_parse[MAX_LINE_LENGTH + 10] = {0};
bool stdin_status; /// czy czytamy z terminala czy nie
int start, end;
bool end_of_command;
bool can_shift;
int buffer_position;
int buffer_to_parse_position;
int line_end; /// koniec aktualnej linii
int last_line_end; /// koniec poprzedniej linii
bool too_long_line;

/**
Funkcja czyszcząca oba bufory (w przypadku zbyt długich linii)
*/
void clearBuffers()
{
    int i;
    for(i=0;i<MAX_LINE_LENGTH + 10;++i)
        buffer_to_parse[i] = 0;

    for(i=0;i<2*MAX_LINE_LENGTH + 10;++i)
        buffer[i] = 0;
}

/**
Funkcja sprawdza czy wpisana komenda jest komendą shella.
Jeśli jest - powinna być wykonana w procesie głównym a nie w potomnym.
*/
int checkIfItIsShellCommand(char commandName[])
{
    int i=0;
    for(i=0;i<5;++i)
    {
        if(!strcmp(commandName, builtins_table[i].name)) /** komenda znaleziona */
            return i;
    }
    return -1;
}

/**
Funkcja wykonuje komendę w procesie shella a nie w potomnym.
*/
void executeAsShellCommand(char* argv[], int commandNumber)
{
    builtins_table[commandNumber].fun(argv);
}

/**
Funkcja wykonuje komendę w procesie potomnym.
*/
void execute()
{
    line* ln = NULL;
    command* com = NULL;
    ln = parseline(buffer_to_parse);
    if((ln == NULL) || (*(ln->pipelines) == NULL) || (**(ln->pipelines) == NULL))
    {
        printf("%s\n", SYNTAX_ERROR_STR);
        fflush(stdout);
        return ;
    }

    com = pickfirstcommand(ln);
    if((com == NULL) || (com->argv == NULL) || (com->argv[0] == NULL))
    {
        if(buffer_to_parse[0] != '#') /** jeśli linia nie jest komentarzem */
            printf("%s\n", SYNTAX_ERROR_STR);

        fflush(stdout);
        return ;
    }
    int number = checkIfItIsShellCommand(com->argv[0]);
    if(number >= 0)
    {
        executeAsShellCommand(com->argv, number);
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
Ustawia znacznik end na znaleziony znak lub na ostatni znak w buforze
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
            return;
        }
    }
    if(i>0)
        line_end = i-1;
    else line_end = 0;
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
        last_line_end = -1;
        buffer_position = buffer_to_parse_position = 0;
        for(i=0;i<2*MAX_LINE_LENGTH + 10;++i)
            buffer[i] = 0;
        for(i=0;i<MAX_LINE_LENGTH + 10;++i)
            buffer_to_parse[i] = 0;
        return ;
    }

    for(i=last_line_end+1;i<=end;++i)
    {
        buffer[i-(last_line_end+1)] = buffer[i];
        buffer_to_parse[i-(last_line_end+1)] = buffer[i];
    }
    for(i=end-last_line_end;i<2*MAX_LINE_LENGTH+10;++i)
        buffer[i] = 0;
    for(i=end-last_line_end;i<MAX_LINE_LENGTH+10;++i)
        buffer_to_parse[i] = 0;
    end = end-(last_line_end+1);
    if(buffer[0] == 0) /** bufor pusty */
    {
        buffer_position = buffer_to_parse_position = 0;
        start = line_end = end = 0;
        last_line_end = -1;
    }
    else
    {
        buffer_position = buffer_to_parse_position = end+1;
        start = line_end = end+1;
        last_line_end = -1;
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
            end_of_command = true;

        shiftBufferLeft();
        ///printf("PO PRZESUNIECIU buffer pos %d parse pos %d\n", buffer_position, buffer_to_parse_position);
        fflush(stdout);
        if(end==0)
        {
            if(buffer[0] == 0) /** bufor jest pusty */
            {
                k = read(0, buffer, MAX_LINE_LENGTH + 1);
                buffer_to_parse_position = buffer_position = 0;
                start = line_end = 0;
                last_line_end = -1;
                if(k>0)
                    end = k-1;
                else end = 0;
            }
            else /** jeden znak z nowej komendy jest już wczytany */
            {
                k = read(0, buffer+1, MAX_LINE_LENGTH + 1);
                buffer_to_parse_position = buffer_position = 1;
                start = line_end = 1;
                last_line_end = -1;
                end = k;
            }
        }
        else if(end>0)
        {
            k = read(0, buffer+end+1, MAX_LINE_LENGTH + 1);
            buffer_position = end + 1;
            buffer_to_parse_position = end + 1;
            start = line_end = end+1;
            end += k;
            last_line_end = -1;
        }
        if(k == -1)
        {
            ///printf("BLAD READ!");
            ///printf("\n");
            ///printf("%s\n", strerror(errno));
            exit(1);
        }

        if(k == 0)
        {
            ///printf("%d\n", strlen(buffer_to_parse));
            if(strlen(buffer_to_parse))
            {
                buffer_to_parse[buffer_to_parse_position] = 0;
                if(!too_long_line)
                    execute();
            }
            if(stdin_status) printf("\n"); /** jesli czytamy z terminala to przejdź do nowej linii */

           /// printf("KONIEC INPUTU\n");
            fflush(stdout);
            exit(0);
        }
        ///printf("END= %d\n", end);
    }



   /// printf("czytanie: k== %d\n", k);
   /// printf("BUFFER %s\n------------\n", buffer+last_line_end+1);
   /// fflush(stdout);
    while(buffer_position <= end)
    {
        findNewLine();
        if(line_end - (last_line_end+1) > MAX_LINE_LENGTH)
        {
           /// printf("too long line! %d %d\n", start, line_end);
            int p;
            for(p=start;p<=end;++p) /** ręczne cofanie bufora */
                buffer[p-start] = buffer[p];

            for(p=0;p<MAX_LINE_LENGTH + 10;++p)
                buffer_to_parse[p] = 0;
            for(p=end+1;p<2*MAX_LINE_LENGTH + 10;++p)
                buffer[p] = 0;

            too_long_line = true;
            line_end = line_end - start;
            start = buffer_position = buffer_to_parse_position = 0;
            end = k-1;
            last_line_end = -1;
        }
       /// printf("%s\n", buffer+last_line_end+1);
       /// printf("%s\n", buffer_to_parse);
        ///printf("start=%d END=%d endline=%d, last endline=%d\n", start, end, line_end, last_line_end);
        ///fflush(stdout);


        for(buffer_position=start;buffer_position<=line_end; ++buffer_position, ++buffer_to_parse_position)
        {
            buffer_to_parse[buffer_to_parse_position] = buffer[buffer_position];
            ///printf("WCZYTANO %c na pozycje %d\n", buffer[buffer_position], buffer_to_parse_position);
            ///fflush(stdout);
            if(buffer_position == end)
            {
                if(buffer[buffer_position] == '\n')
                {
                    buffer_to_parse[buffer_to_parse_position] = 0;
                   /// printf("COMMAND start %d end %d %s\n", last_line_end+1, line_end, buffer_to_parse);
                    ///fflush(stdout);
                    last_line_end = end;
                    if(start != line_end) /** Jesli start == line_end to linia pusta, nie robimy execute() żeby uniknąć Syntax error. */
                    {
                        ///printf("COMMAND %s\n", buffer_to_parse);
                        if(!too_long_line)
                            execute();
                    }
                    end_of_command = true;
                    for(int i=0;i<MAX_LINE_LENGTH+10;++i)
                        buffer_to_parse[i] = 0;
                    buffer_to_parse_position = 0;
                    if(too_long_line)
                    {
                        buffer_position = 0;
                        clearBuffers();
                        last_line_end = -1;
                        line_end = end = start =0;
                        end_of_command = false;
                        too_long_line = false;
                    }
                }
                return ;
            }

            else if(buffer_position == line_end)
            {
                if(buffer[buffer_position] == '\n') /** koniec linii ale nie bufora */
                {
                   ///printf("OKOK222\n");
                    ///printf("\nstart %d ENDLINE na %d, end %d last line end %d\n", start, line_end, end, last_line_end);
                    ///printf("COMMAND %s\n", buffer_to_parse);
                    ///fflush(stdout);
                    buffer_to_parse[buffer_to_parse_position] = 0;
                    last_line_end = buffer_position;

                    if(start != line_end) /** Jesli start == line_end to linia pusta, nie robimy execute() żeby uniknąć Syntax error. */
                    {
                        ///printf("COMMAND %s\n", buffer_to_parse);
                        ///fflush(stdout);
                        if(!too_long_line)
                            execute();
                    }

                    buffer_position++;
                    start = buffer_position;
                    line_end = start;

                    for(int i=0;i<MAX_LINE_LENGTH+10;++i)
                        buffer_to_parse[i] = 0;
                    buffer_to_parse_position = 0;

                    if(too_long_line)
                        too_long_line = false;

                    break;
                }
            }
        }
    }
}

int main(int argc, char *argv[])
{
    can_shift = false;
    too_long_line = false;
    end_of_command = false;
    start = end = line_end = 0;
    last_line_end = -1;
    buffer_position = buffer_to_parse_position = 0;
    struct stat stdin_buffer;
    int check = fstat(0, &stdin_buffer);
    if(check == -1) exit(1);
    /** makro testuje czy czytamy z terminala czy nie, jak zwraca nie zero to tak. */
    if(S_ISCHR(stdin_buffer.st_mode)) stdin_status = true;
    else stdin_status = false;

	while(true)
	{
		if(stdin_status && (!too_long_line)) /** czytamy z terminala   */
		{
            printf("$ ");
            fflush(stdout);
        }
        getLines(stdin_status);
        fflush(stdout);
	}
}
