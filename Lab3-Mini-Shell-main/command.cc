#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>
#include <time.h>
#include <glob.h>
#include <wordexp.h>
#include <errno.h>
#include <vector>
#include <string>
#include <cctype>
#include <iostream>
#include <sstream>
#include "command.h"
#include "tokenizer.h"
using namespace std;

void childHandler(int)
{
    int status;
    pid_t pid;
    int savedErrno = errno;    

    while ((pid = waitpid(-1, &status, WNOHANG)) > 0)
    {
        char line[100];
        int length = sprintf(line, "Child process %d terminated\n", (int)pid);

        int fd = open("myshell.log", O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (fd >= 0)
        {
            write(fd, line, length);
            close(fd);
        }
    }

    errno = savedErrno;
}


void parse(vector<Token> &tokens)
{
    Command currentCommand;
    SimpleCommand *currentSimpleCommand = new SimpleCommand();

    int hasWord = 0;
    int error = 0;   

    for (size_t i = 0; i < tokens.size() && error == 0; i++)
    {
        TokenType type = tokens[i].type;

        if (type == TOKEN_EOF)
        {
            break;
        }

        if (type == TOKEN_COMMAND || type == TOKEN_ARGUMENT)
        {
            string word = tokens[i].value;
            int quoted = 0;
            if (word[0] == '"' || word[0] == '\'')
            {
                char quote = word[0];
                quoted = 1;

                while (word.size() < 2 || word[word.size() - 1] != quote)
                {
                    if (i + 1 >= tokens.size() || tokens[i + 1].type == TOKEN_EOF)
                    {
                        fprintf(stderr, "myshell: unterminated quote\n");
                        error = 1;
                        break;
                    }
                    i++;
                    word = word + " " + tokens[i].value;
                }

                if (error == 0)
                {
                    word = word.substr(1, word.size() - 2);  
                }
            }

            if (error == 0)
            {
                if (quoted == 0)
                {
                    glob_t matches;
                    if (glob(word.c_str(), GLOB_NOCHECK, NULL, &matches) == 0)
                    {
                        for (size_t j = 0; j < matches.gl_pathc; j++)
                        {
                            currentSimpleCommand->insertArgument(strdup(matches.gl_pathv[j]));
                        }
                        globfree(&matches);
                    }
                    else
                    {
                        currentSimpleCommand->insertArgument(strdup(word.c_str()));
                    }
                }
                else
                {
                    currentSimpleCommand->insertArgument(strdup(word.c_str()));
                }
                hasWord = 1;
            }
        }
        else if (type == TOKEN_PIPE)
        {
            if (hasWord == 0)
            {
                fprintf(stderr, "myshell: syntax error near unexpected token '|'\n");
                error = 1;
            }
            else
            {
                currentCommand.insertSimpleCommand(currentSimpleCommand);
                currentSimpleCommand = new SimpleCommand();
                hasWord = 0;
            }
        }

        else if (type == TOKEN_BACKGROUND)
        {
            if (tokens[i + 1].type != TOKEN_EOF)
            {
                fprintf(stderr, "myshell: '&' must be at the end of the command\n");
                error = 1;
            }
            else
            {
                currentCommand._background = 1;
            }
        }
        else
        {
            if (i + 1 >= tokens.size() ||
                (tokens[i + 1].type != TOKEN_COMMAND && tokens[i + 1].type != TOKEN_ARGUMENT))
            {
                fprintf(stderr, "myshell: missing filename after '%s'\n", tokens[i].value.c_str());
                error = 1;
            }
            else
            {
                i++;
                char *file = strdup(tokens[i].value.c_str());

                if (type == TOKEN_INPUT)                        // <
                {
                    free(currentCommand._inputFile);
                    currentCommand._inputFile = file;
                }
                else if (type == TOKEN_ERROR)                   // 2>
                {
                    free(currentCommand._errFile);
                    currentCommand._errFile = file;
                }
                else if (type == TOKEN_REDIRECT_AND_ERROR)      // >>&
                {
                    free(currentCommand._outFile);
                    currentCommand._outFile = file;
                    currentCommand._out_error = 1;
                    currentCommand._append = 1;
                }
                else if (type == TOKEN_APPEND)                  // >>
                {
                    free(currentCommand._outFile);
                    currentCommand._outFile = file;
                    currentCommand._append = 1;
                }
                else                                            // >
                {
                    free(currentCommand._outFile);
                    currentCommand._outFile = file;
                    currentCommand._append = 0;
                }
            }
        }
    }

    if (error == 0 && hasWord == 0 && tokens.size() > 1)
    {
        if (currentCommand._numberOfSimpleCommands > 0)
        {
            fprintf(stderr, "myshell: syntax error: missing command after '|'\n");
        }
        else
        {
            fprintf(stderr, "myshell: missing command\n");
        }
        error = 1;
    }

    if (hasWord == 1)
    {
        currentCommand.insertSimpleCommand(currentSimpleCommand);
    }
    else
    {
        free(currentSimpleCommand->_arguments);
        delete currentSimpleCommand;
    }

    if (error == 1 || currentCommand._numberOfSimpleCommands == 0)
    {
        currentCommand.clear();
        return;
    }

    currentCommand.execute();
}

SimpleCommand::SimpleCommand()
{
    _numberOfAvailableArguments = 5;
    _numberOfArguments = 0;
    _arguments = (char **)malloc(_numberOfAvailableArguments * sizeof(char *));
}

void SimpleCommand::insertArgument(char *argument)
{
    if (_numberOfAvailableArguments == _numberOfArguments + 1)
    {
        _numberOfAvailableArguments *= 2;
        _arguments = (char **)realloc(_arguments,
                                      _numberOfAvailableArguments * sizeof(char *));
    }

    _arguments[_numberOfArguments] = argument;
    _arguments[_numberOfArguments + 1] = NULL;

    _numberOfArguments++;
}

Command::Command()
{
    _numberOfAvailableSimpleCommands = 1;
    _simpleCommands = (SimpleCommand **)
        malloc(_numberOfAvailableSimpleCommands * sizeof(SimpleCommand *));

    _numberOfSimpleCommands = 0;
    _outFile = 0;
    _inputFile = 0;
    _errFile = 0;
    _out_error = 0;
    _background = 0;
    _append = 0;
}

void Command::insertSimpleCommand(SimpleCommand *simpleCommand)
{
    if (_numberOfAvailableSimpleCommands == _numberOfSimpleCommands)
    {
        _numberOfAvailableSimpleCommands *= 2;
        _simpleCommands = (SimpleCommand **)realloc(_simpleCommands,
                                                    _numberOfAvailableSimpleCommands * sizeof(SimpleCommand *));
    }

    _simpleCommands[_numberOfSimpleCommands] = simpleCommand;
    _numberOfSimpleCommands++;
}

void Command::clear()
{
    for (int i = 0; i < _numberOfSimpleCommands; i++)
    {
        for (int j = 0; j < _simpleCommands[i]->_numberOfArguments; j++)
        {
            free(_simpleCommands[i]->_arguments[j]);
        }

        free(_simpleCommands[i]->_arguments);
        delete _simpleCommands[i];   
    }

    free(_simpleCommands);

    if (_outFile)
    {
        free(_outFile);
    }

    if (_inputFile)
    {
        free(_inputFile);
    }

    if (_errFile)
    {
        free(_errFile);
    }

    _simpleCommands = NULL;
    _numberOfSimpleCommands = 0;
    _outFile = 0;
    _inputFile = 0;
    _errFile = 0;
    _out_error = 0;
    _background = 0;
    _append = 0;
}

void Command::print()
{
    printf("\n\n");
    printf("              COMMAND TABLE                \n");
    printf("\n");
    printf("  #   Simple Commands\n");
    printf("  --- ----------------------------------------------------------\n");

    for (int i = 0; i < _numberOfSimpleCommands; i++)
    {
        printf("  %-3d ", i);
        for (int j = 0; j < _simpleCommands[i]->_numberOfArguments; j++)
        {
            printf("\"%s\" \t", _simpleCommands[i]->_arguments[j]);
        }
        printf("\n");
    }

    printf("\n\n");
    printf("  Output       Input        Error        Err&Out       Background\n");
    printf("  ------------ ------------ ------------ ------------ ------------\n");
    printf("  %-12s %-12s %-12s %-12s %-12s\n", _outFile ? _outFile : "default",
           _inputFile ? _inputFile : "default", _errFile ? _errFile : "default",
           (_out_error == 1 && _outFile) ? _outFile : "default",
           _background ? "YES" : "NO");
    printf("\n\n");

    fflush(stdout); 
}

void Command::execute()
{
    print();
    if (_numberOfSimpleCommands == 1)
    {
        char **args = _simpleCommands[0]->_arguments;

        if (strcmp(args[0], "exit") == 0)
        {
            printf("Good bye!!\n");
            clear();
            exit(0);
        }

        if (strcmp(args[0], "cd") == 0)
        {
            char *dir = args[1];
            if (dir == NULL)
            {
                dir = getenv("HOME"); 
            }

            if (dir == NULL || chdir(dir) != 0)
            {
                perror("cd");
            }

            clear();
            return;
        }
    }

    int n = _numberOfSimpleCommands;

    int *pipeFds = (int *)malloc(2 * n * sizeof(int));

    for (int k = 0; k < n - 1; k++)
    {
        if (pipe(&pipeFds[2 * k]) < 0)
        {
            perror("pipe");
            for (int j = 0; j < 2 * k; j++)
            {
                close(pipeFds[j]);
            }
            free(pipeFds);
            clear();
            return;
        }
    }

    pid_t *pids = (pid_t *)malloc(n * sizeof(pid_t));
    int started = 0;

    for (int i = 0; i < n; i++)
    {
        pid_t pid = fork();

        if (pid < 0)
        {
            perror("fork");
            break;
        }

        if (pid == 0)
        {
            signal(SIGINT, SIG_DFL);     

            if (i > 0)
            {
                dup2(pipeFds[2 * (i - 1)], 0);
            }
            if (i < n - 1)
            {
                dup2(pipeFds[2 * i + 1], 1);
            }

            for (int k = 0; k < 2 * (n - 1); k++)
            {
                close(pipeFds[k]);
            }

            if (i == 0 && _inputFile)
            {
                int fd = open(_inputFile, O_RDONLY);
                if (fd < 0)
                {
                    perror(_inputFile);
                    _exit(1);
                }
                dup2(fd, 0);
                close(fd);
            }

            if (i == n - 1 && _outFile)
            {
                int flags = O_WRONLY | O_CREAT;
                if (_append)
                {
                    flags = flags | O_APPEND;
                }
                else
                {
                    flags = flags | O_TRUNC;
                }

                int fd = open(_outFile, flags, 0666);
                if (fd < 0)
                {
                    perror(_outFile);
                    _exit(1);
                }
                dup2(fd, 1);
                if (_out_error)
                {
                    dup2(fd, 2);    
                }
                close(fd);
            }
            if (i == n - 1 && _errFile)
            {
                int fd = open(_errFile, O_WRONLY | O_CREAT | O_TRUNC, 0666);
                if (fd < 0)
                {
                    perror(_errFile);
                    _exit(1);
                }
                dup2(fd, 2);
                close(fd);
            }

            execvp(_simpleCommands[i]->_arguments[0], _simpleCommands[i]->_arguments);

            if (errno == ENOENT)
            {
                fprintf(stderr, "myshell: %s: command not found\n", _simpleCommands[i]->_arguments[0]);
            }
            else
            {
                perror(_simpleCommands[i]->_arguments[0]);
            }
            _exit(127);
        }

        pids[started] = pid;
        started++;
    }

    for (int k = 0; k < 2 * (n - 1); k++)
    {
        close(pipeFds[k]);
    }

    if (_background == 0)
    {
        for (int k = 0; k < started; k++)
        {
            int status;
            pid_t result = waitpid(pids[k], &status, 0);

            if (result > 0)
            {
                char line[100];
                int length = sprintf(line, "Child process %d terminated\n", (int)result);

                int fd = open("myshell.log", O_WRONLY | O_CREAT | O_APPEND, 0644);
                if (fd >= 0)
                {
                    write(fd, line, length);
                    close(fd);
                }
            }
        }
    }

    free(pipeFds);
    free(pids);
    clear();
}

void Command::prompt()
{
    signal(SIGINT, SIG_IGN);
    signal(SIGCHLD, childHandler);

    string input;
    while (true)
    {
        printf("myshell>");
        fflush(stdout);
        if (!getline(cin, input))
        {
            printf("\nGood bye!!\n");
            break;
        }

        vector<Token> tokens = tokenize(input);
        parse(tokens);
    }
}

Command Command::_currentCommand;
SimpleCommand *Command::_currentSimpleCommand;

int main()
{
    Command::_currentCommand.prompt();
    return 0;
}