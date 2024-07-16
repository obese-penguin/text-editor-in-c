/*** includes ***/

#include<stdio.h>
#include<unistd.h>
#include<termios.h>
#include<stdlib.h>
#include<ctype.h>
#include<errno.h>

/*** defines ***/
#define CTRL_KEY(k) ((k) & 0x1f)


/*** data ***/

struct termios orig_termios;

/*** terminal ***/

void die(const char* string)
{
    perror(string);

    exit(1);
}

void disableRawMode()
{
    // to restore original terminal attributes on exit
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios) == -1) 
        die("tcsetattr");
}

void enableRawMode()
{
    if (tcgetattr(STDIN_FILENO, &orig_termios) == -1)
        die("tcgetattr");
    
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

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) 
        die("tcsetattr");
}

char editorReadKey()
{
    int nread;
    char c;

    while((nread = read(STDIN_FILENO, &c, 1)) != 1)
    {
        if(nread == -1 && errno != EAGAIN) die("read"); // errno != EAGAIN is for cygwin
    }

    return c;
}

/*** output  ***/
void editorRefreshScreen()
{
    write(STDOUT_FILENO, "\x1b[2J", 4); // that is the sequence to clear screen. 
    write(STDOUT_FILENO, "\x1b[H", 3); // send cursor to top of the screen. 
}

/*** input  ***/

void editorProcessKeypress()
{
    char c = editorReadKey();

    switch (c)
    {
        case CTRL_KEY('q'):
            exit(0);
            break;
    }
}

/*** init ***/

int main()
{
    editorRefreshScreen();

    enableRawMode();

    while(1)
    {
        editorProcessKeypress();
    }
    return 0;
}
