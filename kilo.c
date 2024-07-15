#include<stdio.h>
#include<unistd.h>
#include<termios.h>
#include<stdlib.h>
#include<ctype.h>

struct termios orig_termios;

void disableRawMode()
{
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

void enableRawMode()
{
    tcgetattr(STDIN_FILENO, &orig_termios);
    
    atexit(disableRawMode);

    struct termios raw = orig_termios;
    raw.c_iflag &= ~(IXON | ICRNL | BRKINT | INPCK | ISTRIP); // ???? // note iflag
    raw.c_oflag &= ~(OPOST);  // note oflag
    raw.c_cflag |= (CS8);  // note cflag
    raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);  // note lflag
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;

    // ICRNL -- Input Carriage Return New Line. 
    //          see carriange return and ctrl+m
    // ISIG -- for ctrl+c and ctrl+z. 
    // IEXTEN -- ctrl+v or ctrl+o. not sure about this.
    // OPOST -- disabled translation of '\n' to '\r\n'

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

int main()
{
    enableRawMode();

    char c;
    while(1)
    {
        char c = '\0';
        read(STDIN_FILENO, &c, 1);

        if (iscntrl(c))
        {
            printf("%d\r\n", c); 
        }

        else 
        {
            printf("%d, ('%c')\r\n", c, c);
        }

        if(c == 'q')
            break;
    }
    return 0;
}
