/*** includes ***/

#include<stdio.h>
#include<unistd.h>
#include<termios.h>
#include<stdlib.h>
#include<ctype.h>
#include<errno.h>
#include<sys/ioctl.h>
#include<string.h>

/*** defines ***/
#define CTRL_KEY(k) ((k) & 0x1f)


/*** data ***/

struct editorConfig {
    int screenrows;
    int screencols;
    struct termios orig_termios;
};

struct editorConfig E;

/*** append buffer ***/
struct abuf {
    char *b;
    int len;
};

#define ABUF_INIT {NULL, 0}

void abAppend(struct abuf *ab, const char *s, int len)
{
    char* new = realloc(ab->b, ab->len + len);

    if(new == NULL) return;
    memcpy(&new[ab->len], s, len);
    ab->b = new;
    ab->len += len;
}

void abFree(struct abuf *ab)
{
    free(ab->b);
}

/*** terminal ***/

void die(const char* string)
{
    write(STDOUT_FILENO, "\x1b[2J", 4); 
    write(STDOUT_FILENO, "\x1b[H", 3);  

    perror(string);
    exit(1);
}

void disableRawMode()
{
    // to restore original terminal attributes on exit
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1) 
        die("tcsetattr");
}

void enableRawMode()
{
    if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1)
        die("tcgetattr");
    
    atexit(disableRawMode);

    struct termios raw = E.orig_termios;
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

int getCursorPosition(int* rows, int* cols)
{
    char buf[32];
    unsigned int i = 0;


    if(write(STDOUT_FILENO, "\x1b[6n", 4) != 4)
        return -1;

    while(i < sizeof(buf)-1)
    {
        if(read(STDIN_FILENO, &buf[i], 1) != 1) break;
        if(buf[i] == 'R')  break;  
        i++;
    }
    
    buf[i] = '\0';
    if(buf[0] != '\x1b' || buf[1] != '[') return -1;
    if(sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1;
    return 0;
}

int getWindowSize(int* rows, int* cols)
{
    struct winsize ws;

    if(ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0)
    {
        if(write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12)
            return -1;

        return getCursorPosition(rows, cols);
        editorReadKey();
        return -1;
    }
    else
    {
        *rows = ws.ws_row;
        *cols = ws.ws_col;
        return 0;
    }
} // in this function, ioctl fills up library provided winsize struct. 
  // *rows, *cols is then filled up from that struct
  // this allows us to return multiple values + return success/failure


void initEditor()
{
    if(getWindowSize(&E.screenrows, &E.screencols) == -1)
    {
        die("getWindowSize");
    }
}

void editorDrawRows(struct abuf *ab)
{
    int y;
    for(y = 0; y < E.screenrows; y += 1)
    {
        abAppend(ab, "~", 1);
        // write(STDOUT_FILENO, "~", 1);

        if(y != E.screenrows - 1)
        {
            abAppend(ab, "\r\n", 3);
            // write(STDOUT_FILENO, "\r\n", 2);
        }
    }
}

/*** output  ***/
void editorRefreshScreen()
{
    struct abuf ab = ABUF_INIT;
    
    abAppend(&ab, "\x1b[2J", 4);
    // write(stdout_fileno, "\x1b[2J", 4); // that is the sequence to clear screen. 
    abAppend(&ab, "\x1b[H", 3);
    // write(STDOUT_FILENO, "\x1b[H", 3); // send cursor to top of the screen. 

    editorDrawRows(&ab);

    abAppend(&ab, "\x1b[H", 3);
    // write(STDOUT_FILENO, "\x1b[H", 3);

    write(STDOUT_FILENO, ab.b, ab.len);
    abFree(&ab);
}

/*** input  ***/

void editorProcessKeypress()
{
    char c = editorReadKey();

    switch (c)
    {
        case CTRL_KEY('q'):
            write(STDOUT_FILENO, "\x1b[2J", 4); 
            write(STDOUT_FILENO, "\x1b[H", 3); 
            exit(0); // 0 for success; 1 for failure
            break;
    }
}

/*** init ***/

int main()
{
    enableRawMode();
    initEditor();

    while(1)
    {
        editorRefreshScreen();
        editorProcessKeypress();
    }
    return 0;
}
