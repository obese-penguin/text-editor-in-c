/*** includes ***/

#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include<stdio.h>
#include<stdbool.h>
#include<unistd.h>
#include<termios.h>
#include<stdlib.h>
#include<ctype.h>
#include<errno.h>
#include<sys/ioctl.h>
#include<sys/types.h>
#include<string.h>

/*** defines ***/
#define CTRL_KEY(k) ((k) & 0x1f)
#define KILO_VERSION "0.0.1"

enum editorKey {
    ARROW_UP = 1000,
    ARROW_DOWN,
    ARROW_RIGHT,
    ARROW_LEFT,
    PAGE_UP,
    PAGE_DOWN,
    HOME_KEY,
    END_KEY,
    DEL_KEY
};

/*** data ***/

typedef struct erow {
    int size;
    char* chars;
} erow;

struct editorConfig {
    int cx, cy; 
    int screenrows;
    int numrows;
    int rowoff;
    int coloff;
    erow* row;
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

int editorReadKey()
{
    int nread;
    char c;

    while((nread = read(STDIN_FILENO, &c, 1)) != 1)
    {
        if(nread == -1 && errno != EAGAIN) die("read"); // errno != EAGAIN is for cygwin
    }

    char seq[3];
    if(c == '\x1b')
    {
        if(read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
        if(read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';

        if(seq[0] == '[')
        {
            if(seq[1] >= '0' && seq[1] <= '9')
            {
                if(read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
                if(seq[2] == '~')
                {
                    switch (seq[1])
                    {
                        case '1': return HOME_KEY;
                        case '3': return DEL_KEY;
                        case '4': return END_KEY;
                        case '5': return PAGE_UP;
                        case '6': return PAGE_DOWN;
                        case '7': return HOME_KEY;
                        case '8': return END_KEY;
                    }
                }
            }

            else 
            {
                switch(seq[1])
                {
                    case 'A': return ARROW_UP;
                    case 'B': return ARROW_DOWN;
                    case 'C': return ARROW_RIGHT;
                    case 'D': return ARROW_LEFT;
                    case 'H': return HOME_KEY;
                    case 'F': return END_KEY;
                }
            }
        }

        else if(seq[0] == 'O')
        {
            switch(seq[1])
            {
                case 'H': return HOME_KEY;
                case 'F': return END_KEY;
            }
        }
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
    E.cx = 0; E.cy = 0;
    E.numrows = 0;
    E.rowoff = 0; E.coloff = 0;

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
        int current = y + E.rowoff; 
        if(current >= E.numrows)
        {
            if(y == E.screenrows/3 && E.numrows == 0)
            {
                char welcome[80];
                int welcomelen = snprintf(welcome, sizeof(welcome), "Kilo editor -- version %s", KILO_VERSION);
                
                if(welcomelen > E.screencols) welcomelen = E.screencols;

                int padding = (E.screencols - welcomelen)/2;

                if(padding)
                {
                    abAppend(ab, "~", 1);
                    padding -= 1;
                }

                while(padding--)
                    abAppend(ab, " ", 1);

                abAppend(ab, welcome, welcomelen);
            }

            else abAppend(ab, "~", 1);
        }
        
        else 
        {
            int len = E.row[current].size - E.coloff;
            if(len < 0) len = 0;
            if(len > E.screencols) len = E.screencols;
            abAppend(ab, &E.row[current].chars[E.coloff], len); 
        }

        abAppend(ab, "\x1b[K", 3);

        if(y != E.screenrows - 1)
        {
            abAppend(ab, "\r\n", 3);
        }
    }
}

/*** row operations ***/
void editorAppendRow(char* s, size_t len)
{
    E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1)); // why +1??

    int at = E.numrows;
    E.row[at].size = len;
    E.row[at].chars = malloc(len + 1);
    memcpy(E.row[at].chars, s, len);
    E.row[at].chars[len] = '\0';
    E.numrows++;
}

/*** file i/o  ***/
void editorOpen(char* filename)
{
    FILE* fp = fopen(filename, "r");
    if(!fp) die("fopen");

    char* line = NULL;
    size_t linecap = 0;
    ssize_t linelen;

    while((linelen = getline(&line, &linecap, fp)) != -1)
    {
        while(linelen >= 0 && (line[linelen-1] == '\n' || line[linelen-1] == '\r')) linelen -= 1;
        editorAppendRow(line, linelen);
    }

    free(line);
    fclose(fp);

}

/*** output  ***/
void editorScroll()
{
    // updates E.rowoff if E.cy moves beyond bounds
    // vertical scrolling
    if(E.cy < E.rowoff)
    {
        E.rowoff = E.cy;
    }
    else if (E.cy >= E.rowoff + E.screenrows)
    {
        E.rowoff = E.cy - E.screenrows + 1;
    }

    // horizontal scrolling
    if(E.cx < E.coloff)
    {
        E.coloff = E.cx;
    }
    else if (E.cx >= E.coloff + E.screencols)
    {
        E.coloff = E.cx - E.screencols + 1;
    }
}

void editorRefreshScreen()
{
    editorScroll();

    struct abuf ab = ABUF_INIT;

    abAppend(&ab, "\x1b[?25l", 6); // make cursor disappear
    abAppend(&ab, "\x1b[H", 3);

    editorDrawRows(&ab);

    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy - E.rowoff)+1, (E.cx - E.coloff)+1);
    abAppend(&ab, buf, strlen(buf));

    abAppend(&ab, "\x1b[?25h", 6); // make cursor reappear
    
    write(STDOUT_FILENO, ab.b, ab.len);
    abFree(&ab);
}

/*** input  ***/

void editorMoveCursor(int c)
{
    erow *row = (E.cy > E.numrows) ? NULL : &E.row[E.cy];
   
    switch (c)
    {
        case ARROW_UP:
            if(E.cy != 0)
                E.cy -= 1;
            break;
        case ARROW_LEFT:
            if(E.cx != 0)
                E.cx -= 1;
            else if (E.cy > 0)
            {
                E.cy -= 1;
                E.cx = E.row[E.cy].size;
            }
            break;
        case ARROW_DOWN:
            if(E.cy < E.numrows)
                E.cy += 1;
            break;
        case ARROW_RIGHT:
            if(E.cx < row->size && row)
            E.cx += 1;
            else if(row)
            {
                E.cy += 1;
                E.cx = 0;
            }
            break;
    }

    row = (E.cy > E.numrows) ? NULL : &E.row[E.cy];
    int rowlen = row ? row->size : 0; 
    if(E.cx > rowlen)
        E.cx = row->size;
}

void editorProcessKeypress()
{
    int c = editorReadKey();

    switch (c)
    {
        case CTRL_KEY('q'):
            write(STDOUT_FILENO, "\x1b[2J", 4); 
            write(STDOUT_FILENO, "\x1b[H", 3); 
            exit(0); // 0 for success; 1 for failure
            break;
    
        case HOME_KEY:
            E.cx = 0;
            break;

        case END_KEY:
            E.cx = E.screencols - 1;
            break;

        case PAGE_UP:
        case PAGE_DOWN:
            {
                int times = E.screenrows;
                while (times--)
                {
                    editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
                }
                break;
            }
        case ARROW_UP:
        case ARROW_DOWN:
        case ARROW_RIGHT:
        case ARROW_LEFT:
            editorMoveCursor(c);
            break; 
    }
}

/*** init ***/

int main(int argc, char* argv[])
{
    enableRawMode();
    initEditor();
    if(argc >= 2)
    {
        editorOpen(argv[1]);
    }

    while(1)
    {
        editorRefreshScreen();
        editorProcessKeypress();
    }
    return 0;
}
