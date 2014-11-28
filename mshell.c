#include "includes.h" /** wszystkie include'y są zebrane w osobnym pliku, który zawiera też typedef dla tybu bool */
#define MAX_PIPES_IN_LINE   MAX_LINE_LENGTH/2
#define MAX_ENDED_PROCESSES 3000

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
bool no_prompt;
bool printTerminated;
bool backgroundCommand = false;
/** struktury do obsługi sygnałów */
struct sigaction sigInt;
struct sigaction sigChld;
/** Ile procesów działa jeszcze w foregroundzie */
volatile int fgr = 0;
/** pidy dzieci wywołanych w foregroundzie w tej linii */
volatile int foregroundProcesses[MAX_ENDED_PROCESSES];
/** ile było wszystkich procesów z foregroundu w tej linii */
volatile int foregroundCounter = 0;
/** Tablica na zakończone procesy z tła i ich statusy */
int endedProcesses[MAX_ENDED_PROCESSES][2];
/** Ile procesów z backgroundu się zakończyło i są wpisane w tablicy jako oczekujące */
volatile int backgroundCounter = 0;

/** Funkcja sprawdza czy proces o danym pidzie jest z foregroundu czy z backgroundu */
bool contains(int pid)
{
    int i;
    for(i=0;i<foregroundCounter;++i)
    {
        if(foregroundProcesses[i] == pid)
            return true;
    }
    return false;
}

/** Handler dla SIGCHLD */
void chldHandler(int sig_nb)
{
    int saved_errno = errno;
    int pid = 0;
    int status;
    while((pid = waitpid(-1, &status, WNOHANG)))
    {
        if(pid == -1)
        {
            if(errno == ECHILD) /** nie ma obecnie dzieci do sprawdzenia */
                break;

            else exit(1);
        }
        else
        {
            if(contains(pid)) /** proces z foregroundu */
                --fgr;

            else  /** proces z backgroundu */
            {
                if(backgroundCounter < 3000)
                endedProcesses[backgroundCounter][0] = pid;
                endedProcesses[backgroundCounter][1] = status;
                ++backgroundCounter;
            }
        }
    }
    errno = saved_errno;
}

/** Funkcja czyszcząca oba bufory (w przypadku zbyt długich linii) */
void clearBuffers()
{
    int i;
    for(i=0;i<MAX_LINE_LENGTH + 10;++i)
        buffer_to_parse[i] = 0;

    for(i=0;i<2*MAX_LINE_LENGTH + 10;++i)
        buffer[i] = 0;
}

/** Funkcja sprawdza czy linia jest komentarzem */
bool isComment()
{
    int i;
    for(i = 0;i<MAX_LINE_LENGTH+10;++i)
    {
        if(buffer_to_parse[i] == '#') return true;
        else if(buffer_to_parse[i] == ' ') continue;
        else return false;
    }
    return false;
}

/** Funkcja sprawdza czy linia jest pusta (czy składa sie tylko z białych znaków) */
bool isEmptyLine()
{
    int i;
    for(i = 0;i<MAX_LINE_LENGTH+10;++i)
    {
        if(buffer_to_parse[i] == '\n')
            return true;
        else if(isblank(buffer_to_parse[i]))
            continue;
        else if(buffer_to_parse[i] == 0)
            return true;
        else return false;
    }
    return true;
}

/**
Funkcja sprawdza czy wpisana komenda jest komendą shella.
Jeśli jest - powinna być wykonana w procesie głównym a nie w potomnym.
*/
int checkIfItIsShellCommand(char commandName[])
{
    int i=0;
    while(builtins_table[i].name != NULL)
    {
        if(!strcmp(commandName, builtins_table[i].name)) /** komenda znaleziona */
            return i;
        else ++i;
    }
    return -1;
}

/** Funkcja wykonuje komendę w procesie shella a nie w potomnym. */
void executeAsShellCommand(char* argv[], int commandNumber)
{
    builtins_table[commandNumber].fun(argv);
}

/**
Funkcja bierze całą linię i wykonuje kolejne pipeline'y.
*/
void execute()
{
    line* ln = NULL;
    int pipelineNumber = 0;
    ln = parseline(buffer_to_parse);
    if(ln == NULL)
    {
        fprintf(stderr, "%s\n", SYNTAX_ERROR_STR);
        fflush(stderr);
        return ;
    }
    if(ln->flags)
    {
        backgroundCommand = true;
        printTerminated = false;
    }
    else
    {
        printTerminated = true;
        backgroundCommand = false;
    }
    int lineLength = 0;
    for(;;++lineLength)
    {
        if(ln->pipelines[lineLength] == NULL)
            break;
    }

    /** sprawdzamy czy nie ma gdzieś pustej komendy w pipe (dajemy Syntax error jeśli jest) */
    for(pipelineNumber=0;pipelineNumber < lineLength;++pipelineNumber)
    {
        pipeline p = ln->pipelines[pipelineNumber];
        int pipelineLength = 0;
        for(;;++pipelineLength)
        {
            if(p[pipelineLength] == NULL)
                break;
        }
        int pos;
        for(pos = 0;pos<pipelineLength;++pos)
        {
            if(p[pos]->argv[0] == NULL) /** mamy pustą komendę */
            {
                if(pipelineLength > 1)
                {
                    fprintf(stderr, "Syntax error.\n");
                    return;
                }

            }
        }
    }

    for(pipelineNumber=0;pipelineNumber < lineLength;++pipelineNumber)
    {
        int commandNumber = 0;
        fgr = 0;
        foregroundCounter = 0;
        pipeline p = ln->pipelines[pipelineNumber];
        int pipelineLength = 0;
        for(;;++pipelineLength)
        {
            if(p[pipelineLength] == NULL)
                break;
        }

        if(pipelineLength == 1)
        {
            command* com = p[commandNumber];
            if(com->argv[0] == NULL)
                continue;
            int number = checkIfItIsShellCommand(com->argv[0]);
            if(number >= 0)
            {
                executeAsShellCommand(com->argv, number);
                continue;
            }
        }

        int pipes[MAX_PIPES_IN_LINE][2];
        sigset_t newMask;
        sigemptyset(&newMask);
        sigaddset(&newMask, SIGCHLD);
        sigprocmask(SIG_BLOCK, &newMask, NULL);

        for(commandNumber = 0;commandNumber<pipelineLength;++commandNumber)
        {
            int i;
            command* com = p[commandNumber];
            int createPipe = pipe(pipes[commandNumber]);
            if(createPipe == -1)
                exit(1);

            if(commandNumber >= 2)
            {
                int c1 = close(pipes[commandNumber-2][0]);
                int c2 = close(pipes[commandNumber-2][1]);
                if((c1 == -1) || (c2 == -1))
                    exit(1);
            }

            int child_pid = fork();
            if(child_pid == -1)
                exit(1);

            if(child_pid > 0) /** parent */
            {
                if(!backgroundCommand)
                {
                    ++fgr;
                    foregroundProcesses[foregroundCounter] = child_pid;
                    ++foregroundCounter;
                }
                if(commandNumber == pipelineLength-1)
                {
                    int c1, c2, c3, c4;
                    c1 = c2 = c3 = c4 = 0;

                    if(commandNumber > 0)
                    {
                        c1 = close(pipes[commandNumber-1][0]);
                        c2 = close(pipes[commandNumber-1][1]);
                    }
                    c3 = close(pipes[commandNumber][0]);
                    c4 = close(pipes[commandNumber][1]);
                    if((c1 == -1) || (c2 == -1) || (c3 == -1) || (c4 == -1))
                        exit(1);

                    sigset_t mask;
                    sigemptyset(&mask);
                    sigfillset(&mask);
                    sigdelset(&mask, SIGCHLD);

                    while(fgr > 0)
                    {
                        sigsuspend(&mask);
                    }
                    sigprocmask(SIG_UNBLOCK, &newMask, NULL);
                    if(!backgroundCommand)
                        printTerminated = true;
                }
            }
            else /** child */
            {
                sigprocmask(SIG_UNBLOCK, &newMask, NULL);
                sigemptyset(&sigInt.sa_mask);
                sigInt.sa_handler = SIG_DFL;
                sigaction(SIGINT, &sigInt, NULL);

                sigemptyset(&sigChld.sa_mask);
                sigInt.sa_handler = SIG_DFL;
                sigaction(SIGCHLD, &sigChld, NULL);

                if(backgroundCommand)
                {
                    int group = setsid();
                    if(group == -1)
                        exit(1);
                }

                int c = close(pipes[commandNumber][0]);
                if(c == -1)
                    exit(1);

                if(commandNumber > 0)
                {
                    c = close(pipes[commandNumber-1][1]);
                    if(c == -1)
                        exit(1);
                    c = close(STDIN_FILENO);
                    if(c == -1)
                        exit(1);
                    c = dup2(pipes[commandNumber-1][0], STDIN_FILENO); /** stdin */
                    if(c == -1)
                        exit(1);
                    c = close(pipes[commandNumber-1][0]);
                    if(c == -1)
                        exit(1);
                }

                if(commandNumber < pipelineLength - 1)
                {
                    c = close(STDOUT_FILENO);
                    if(c == -1)
                        exit(1);
                    c = dup2(pipes[commandNumber][1], STDOUT_FILENO); /** stdout */
                    if(c == -1)
                        exit(1);
                    c = close(pipes[commandNumber][1]);
                    if(c == -1)
                        exit(1);
                }

                else if(commandNumber == pipelineLength - 1)
                {
                    c = close(pipes[commandNumber][1]);
                    if(c == -1)
                        exit(1);
                }

                int l = 0;
                for(l=0;;++l)
                {
                    if(com->redirs[l] == NULL)
                        break;
                }
                for(i=0;i<l;++i)
                {
                    if(IS_RIN(((com->redirs)[i])->flags))
                    {
                        int f = open((com->redirs[i])->filename, O_RDONLY, S_IRUSR | S_IWUSR);
                        if(f == -1)
                        {
                            if(errno == EACCES)
                            {
                                fprintf(stderr, "%s: permission denied\n", ((com->redirs)[i])->filename);
                            }
                            else if(errno == ENOENT)
                            {
                                fprintf(stderr, "%s: no such file or directory\n", ((com->redirs)[i])->filename);
                            }
                            exit(1);
                        }
                        int c1 = close(STDIN_FILENO);
                        if(c1 == -1)
                            exit(1);
                        c1 = dup2(f, STDIN_FILENO);
                        if(c1 == -1)
                            exit(1);
                        c1 = close(f);
                        if(c1 == -1)
                            exit(1);
                    }

                    if(IS_ROUT(((com->redirs)[i])->flags))
                    {
                        int f = open(((com->redirs)[i])->filename, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
                        if(f == -1)
                        {
                            if(errno == EACCES)
                            {
                                fprintf(stderr, "%s: permission denied\n", ((com->redirs)[i])->filename);
                            }
                            else if(errno == ENOENT)
                            {
                                fprintf(stderr, "%s: no such file or directory\n", ((com->redirs)[i])->filename);
                            }
                            exit(1);
                        }
                        int c1 = close(STDOUT_FILENO);
                        if(c1 == -1)
                            exit(1);
                        c1 = dup2(f, STDOUT_FILENO);
                        if(c1 == -1)
                            exit(1);
                        c1 = close(f);
                        if(c1 == -1)
                            exit(1);
                    }

                    else if(IS_RAPPEND(((com->redirs)[i])->flags))
                    {
                        int f = open(((com->redirs)[i])->filename, O_WRONLY | O_CREAT | O_APPEND, S_IRUSR | S_IWUSR | S_IRWXO);
                        if(f == -1)
                        {
                            if(errno == EACCES)
                            {
                                fprintf(stderr, "%s: permission denied\n", ((com->redirs)[i])->filename);
                            }
                            else if(errno == ENOENT)
                            {
                                fprintf(stderr, "%s: no such file or directory\n", ((com->redirs)[i])->filename);
                            }
                            exit(1);
                        }
                        int c1 = close(STDOUT_FILENO);
                        if(c1 == -1)
                            exit(1);
                        c1 = dup2(f, STDOUT_FILENO);
                        if(c1 == -1)
                            exit(1);
                        c1 = close(f);
                        if(c1 == -1)
                            exit(1);
                    }
                }

                int exec_status = execvp(com->argv[0], com->argv);
                if(exec_status == -1)
                {
                    close(STDOUT_FILENO);
                    close(STDIN_FILENO);
                    if(errno == ENOENT)
                        fprintf(stderr, "%s: no such file or directory\n", com->argv[0]);
                    else if(errno == EACCES)
                        fprintf(stderr, "%s: permission denied\n", com->argv[0]);
                    else
                        fprintf(stderr, "%s: exec error\n", com->argv[0]);
                    fflush(stderr);
                    exit(EXEC_FAILURE);
                }
            }
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
        if(end==0)
        {
            if(buffer[0] == 0) /** bufor jest pusty */
            {
                k = read(0, buffer, MAX_LINE_LENGTH + 1);
                if(k == -1)
                {
                    if(errno == EINTR) /** Przerwane przez sygnał, czytamy jeszcze raz */
                    {
                        no_prompt = true;
                        return ;
                    }
                    else
                        exit(1);
                }

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
                if(k == -1)
                {
                    if(errno == EINTR) /** Przerwane przez sygnał, czytamy jeszcze raz */
                    {
                        no_prompt = true;
                        return ;
                    }

                    else
                        exit(1);
                }
                buffer_to_parse_position = buffer_position = 1;
                start = line_end = 1;
                last_line_end = -1;
                end = k;
            }
        }
        else if(end>0)
        {
            k = read(0, buffer+end+1, MAX_LINE_LENGTH + 1);
            if(k == -1)
            {
                if(errno == EINTR) /** Przerwane przez sygnał, czytamy jeszcze raz */
                {
                    no_prompt = true;
                    return ;
                }

                else
                    exit(1);
            }
            buffer_position = end + 1;
            buffer_to_parse_position = end + 1;
            start = line_end = end+1;
            end += k;
            last_line_end = -1;
        }

        if(k == 0) /** koniec pliku */
        {
            if(strlen(buffer_to_parse))
            {
                buffer_to_parse[buffer_to_parse_position] = 0;
                if(isEmptyLine())
                    printTerminated = true;

                if((!too_long_line) && (!isComment()) && (!isEmptyLine()))
                    execute();
            }
            if(stdin_status)
                printf("\n"); /** jesli czytamy z terminala to przejdź do nowej linii */

            fflush(stdout);
            exit(0);
        }
    }

    while(buffer_position <= end)
    {
        findNewLine();
        if(line_end - (last_line_end+1) > MAX_LINE_LENGTH)
        {
            int p;
            for(p=start;p<=end;++p) /** ręczne cofanie bufora */
                buffer[p-start] = buffer[p];

            for(p=0;p<MAX_LINE_LENGTH + 10;++p)
                buffer_to_parse[p] = 0;
            for(p=end+1;p<2*MAX_LINE_LENGTH + 10;++p)
                buffer[p] = 0;
            if(!too_long_line)
            {
                fprintf(stderr, "Syntax error.\n");
                fflush(stderr);
            }
            too_long_line = true;
            line_end = line_end - start;
            start = buffer_position = buffer_to_parse_position = 0;
            end = k-1;
            last_line_end = -1;
        }
        for(buffer_position=start;buffer_position<=line_end; ++buffer_position, ++buffer_to_parse_position)
        {
            buffer_to_parse[buffer_to_parse_position] = buffer[buffer_position];

            if(buffer_position == end)
            {
                if(buffer[buffer_position] == '\n')
                {
                    buffer_to_parse[buffer_to_parse_position] = 0;
                    last_line_end = end;
                    if(isEmptyLine())
                        printTerminated = true;

                    if((!too_long_line) && (!isComment()) && (!isEmptyLine()))
                        execute();

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
                    buffer_to_parse[buffer_to_parse_position] = 0;
                    last_line_end = buffer_position;
                    if(isEmptyLine())
                        printTerminated = true;

                    if((!too_long_line) && (!isComment()) && (!isEmptyLine()))
                        execute();

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

int main(int argc, char* argv[])
{
    can_shift = false;
    too_long_line = false;
    end_of_command = false;
    no_prompt = false;
    printTerminated = false;

    start = end = line_end = 0;
    last_line_end = -1;
    buffer_position = buffer_to_parse_position = 0;
    struct stat stdin_buffer;
    int check = fstat(0, &stdin_buffer);
    if(check == -1) exit(1);

    if(S_ISCHR(stdin_buffer.st_mode))  /** makro testuje czy czytamy z terminala czy nie, jak zwraca nie zero to tak. */
        stdin_status = true;
    else stdin_status = false;

    sigInt.sa_handler = SIG_IGN;
    sigInt.sa_flags = 0;
    sigemptyset(&sigInt.sa_mask);
    sigaction(SIGINT, &sigInt, NULL);

    sigChld.sa_handler = chldHandler;
    sigInt.sa_flags = 0;
    sigemptyset(&sigChld.sa_mask);
    sigaction(SIGCHLD, &sigChld, NULL);

	while(true)
	{
		if(stdin_status && (!too_long_line)) /** czytamy z terminala */
		{
            if(printTerminated)
            {
                printTerminated = false;
                sigset_t newMask;
                sigemptyset(&newMask);
                sigaddset(&newMask, SIGCHLD);
                sigprocmask(SIG_BLOCK, &newMask, NULL);
                int pointer = 0;
                for(pointer = 0;pointer < backgroundCounter;++pointer)
                {
                    int childPid = endedProcesses[pointer][0];
                    int exitStatus = endedProcesses[pointer][1];
                    if(WIFEXITED(exitStatus))
                        printf("Background process %d terminated. (exited with status %d)\n", childPid, WEXITSTATUS(exitStatus));

                    else if(WIFSIGNALED(exitStatus))
                        printf("Background process %d terminated. (killed by signal %d)\n", childPid, WTERMSIG(exitStatus));

                    fflush(stdout);
                }
                sigprocmask(SIG_UNBLOCK, &newMask, NULL);
                foregroundCounter = 0;
                backgroundCounter = 0;
            }

            if(!no_prompt)
                printf("$ ");

            no_prompt = false;
            fflush(stdout);
        }
        getLines(stdin_status);
        fflush(stdout);
	}
}
