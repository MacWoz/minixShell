#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <dirent.h>
#include <errno.h>

#include "builtins.h"

int echo(char*[]);
int undefined(char *[]);
int terminateShell(char* []);
int changeDirectory(char* []);
int sendSignal(char* []);
int listFiles(char* []);
int countFD(char* []);

builtin_pair builtins_table[]={
	{"exit",	&terminateShell},
	{"lecho",	&echo},
	{"lcd",		&changeDirectory},
	{"cd",		&changeDirectory},
	{"lkill",	&sendSignal},
	{"lls",		&listFiles},
	{"lds",		&countFD},
	{NULL,NULL}
};

int terminateShell(char* argv[])
{
    if(argv[1] != NULL)
    {
        fprintf(stderr, "Builtin %s error.\n", argv[0]);
        return BUILTIN_ERROR;
    }
    exit(0);
}

int echo(char* argv[])
{
	int i =1;
	if (argv[i]) printf("%s", argv[i++]);
	while  (argv[i])
		printf(" %s", argv[i++]);

	printf("\n");
	fflush(stdout);
	return 0;
}

int changeDirectory(char* argv[])
{
    int ch = -1;
    if(argv[1] == NULL) /** zmieniamy  na wartosc zmiennej HOME */
    {
        ch = chdir(getenv("HOME"));
        if(ch == -1)
        {
            fprintf(stderr, "Builtin %s error.\n", argv[0]);
            return BUILTIN_ERROR;
        }
        return 0;
    }

    if(argv[2] != NULL) /** za dużo argumentów */
    {
        fprintf(stderr, "Builtin %s error.\n", argv[0]);
        return BUILTIN_ERROR;
    }
    /** jeden argument, ok */
    ch = chdir(argv[1]);
    if(ch == -1)
    {
        fprintf(stderr, "Builtin %s error.\n", argv[0]);
        return BUILTIN_ERROR;
    }
    return 0;
}

int sendSignal(char* argv[])
{
    if((argv[1] == NULL) || (argv[3] != NULL)) /** za duzo / za mało argumentów */
    {
        fprintf(stderr, "Builtin %s error.\n", argv[0]);
        return BUILTIN_ERROR;
    }
    if(argv[2] == NULL) /** wysyłamy SIGTERM */
    {
        int p = 1;
        int pid = 0;
        int i = 0;

        for(i=strlen(argv[1])-1;i>=0;--i)
        {
            if(i == 0)
            {
                if(argv[1][i] == '-')
                {
                    pid = -pid;
                    break;
                }
            }
            int c = argv[1][i] - '0';
            if((c<0) || (c>9))
            {
                fprintf(stderr, "Builtin %s error.\n", argv[0]);
                return BUILTIN_ERROR;
            }
            pid += c*p;
            p *= 10;
        }

        int status = kill(pid, SIGTERM);
        if(status == -1)
        {
            fprintf(stderr, "Builtin %s error.\n", argv[0]);
            return BUILTIN_ERROR;
        }
        return 0;
    }
    /** Podany numer procesu i numer sygnału */
    int p = 1;
    int pid = 0;
    int signalNumber = 0;
    int i = 0;

    for(i=strlen(argv[1])-1;i>=0;--i)
    {
        if(i == 0)
        {
            if(argv[1][i] == '-')
                break;
            else
            {
                fprintf(stderr, "Builtin %s error.\n", argv[0]);
                return BUILTIN_ERROR;
            }
        }
        int c = argv[1][i] - '0';
        if((c<0) || (c>9))
        {
            fprintf(stderr, "Builtin %s error.\n", argv[0]);
            return BUILTIN_ERROR;
        }
        signalNumber += c*p;
        p *= 10;
    }

    p = 1;
    i = 0;
    for(i=strlen(argv[2])-1;i>=0;--i)
    {
        int c = argv[2][i] - '0';
        if((c<0) || (c>9))
        {
            fprintf(stderr, "Builtin %s error.\n", argv[0]);
            return BUILTIN_ERROR;
        }
        pid += c*p;
        p *= 10;
    }

    int status = kill(pid, signalNumber);
    if(status == -1)
    {
        fprintf(stderr, "Builtin %s error.\n", argv[0]);
        return BUILTIN_ERROR;
    }
    return 0;
}

int listFiles(char* argv[]) /** readdir, opendir/fdopendir, closedir */
{
    if(argv[1] != NULL)
    {
        fprintf(stderr, "Builtin %s error.\n", argv[0]);
        return BUILTIN_ERROR;
    }
    DIR* dir = opendir(".");
    struct dirent *dp;
    if(dir == NULL)
    {
        fprintf(stderr, "Builtin %s error.\n", argv[0]);
        return BUILTIN_ERROR;
    }

    while(1)
    {
        errno = 0;
        dp = readdir(dir);
        if((dp == NULL) && (errno != 0)) /** błąd */
        {
            fprintf(stderr, "Builtin %s error.\n", argv[0]);
            return BUILTIN_ERROR;
        }

        if((dp == NULL) && (errno == 0))
            return 0;

        if(dp->d_name[0] == '.')
            continue;
        printf("%s\n", dp->d_name);
    }
}

int countFD(char* argv[]){
	int n, fds;
	fds=0;
	for (n=0; n< OPEN_MAX; n++){
		if ((fcntl(n, F_GETFD) != -1) || (errno != EBADF) ) fds++;
	}

	printf("%d file descriptors used.\n", fds);
	return 0;
}

int undefined(char* argv[])
{
	fprintf(stderr, "Command %s undefined.\n", argv[0]);
	return BUILTIN_ERROR;
}
