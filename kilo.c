/*** includes ***/

#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE
#include<stdio.h>
#include<stdbool.h>
#include<unistd.h>
#include<termios.h>
#include<stdlib.h>
#include<stdarg.h>
#include<ctype.h>
#include<errno.h>
#include<time.h>
#include<string.h>
#include<fcntl.h>
#include<sys/ioctl.h>
#include<sys/types.h>

/*** defines ***/
#define CTRL_KEY(k) ((k) & 0x1f)
#define KILO_VERSION "0.0.1"
#define KILO_TAB_STOP 8

enum editorKey {
    BACKSPACE = 127,
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
    int rsize;
    char* chars;
    char* render;
} erow;

struct editorConfig {
    int cx, cy; 
    int rx;
    int screenrows;
    int numrows;
    int rowoff;
    int coloff;
    int dirty;
    erow* row;
    int screencols;
    struct termios orig_termios;
    char* filename;
    char statusmsg[80];
    time_t statusmsg_time;
};

struct editorConfig E;

/*** prototypes ***/
void editorSetStatusMessage(const char* fmt, ...);

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
    E.rx = 0;
    E.numrows = 0;
    E.rowoff = 0; E.coloff = 0;
    E.dirty = 0;
    E.filename = NULL;
    E.statusmsg[0] = '\0';
    E.statusmsg_time = 0;

    if(getWindowSize(&E.screenrows, &E.screencols) == -1)
    {
        die("getWindowSize");
    }

    E.screenrows -= 2;

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
            int len = E.row[current].rsize - E.coloff;
            if(len < 0) len = 0;
            if(len > E.screencols) len = E.screencols;
            abAppend(ab, &E.row[current].render[E.coloff], len); 
        }

        abAppend(ab, "\x1b[K", 3);

        abAppend(ab, "\r\n", 2);
    }
}

/*** row operations ***/
int editorRowCxToRx(erow *row, int cx)
{
    int rx = 0;
    int j;
    for(j = 0; j < cx; j += 1)
    {
        if(row->chars[j] == '\t') rx += (KILO_TAB_STOP - 1) - (rx % 8);
        rx += 1;
    }
    
    return rx;
}

void editorUpdateRow(erow *row)
{
    int tabs = 0;
    int j;
    for(j = 0; j < row->size; j += 1)
        if(row->chars[j] == '\t') tabs += 1;

    free(row->render);
    row->render = malloc(row->size + tabs*(KILO_TAB_STOP - 1) + 1);

    int idx = 0;
    for(j = 0; j < row->size; j += 1)
    {
        if(row->chars[j] == '\t')
        {
            row->render[idx++] = ' ';
            while(idx % KILO_TAB_STOP != 0) row->render[idx++] = ' ';
        }
        else
        {
            row->render[idx++] = row->chars[j];
        }
    }
    row->render[idx] = '\0';
    row->rsize = idx;
}

void editorAppendRow(char* s, size_t len)
{
    E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1)); // why +1??

    int at = E.numrows;
    E.row[at].size = len;
    E.row[at].chars = malloc(len + 1);
    memcpy(E.row[at].chars, s, len);
    E.row[at].chars[len] = '\0';

    E.row[at].render = NULL;
    E.row[at].rsize = 0;
    editorUpdateRow(&E.row[at]);

    E.numrows++;
    E.dirty++;
}

void editorRowInsertChar(erow *row, int at, int c)
{
    if(at < 0 || at > row->size) at = row->size;
    row->chars = realloc(row->chars, row->size + 2);
    memmove(&row->chars[at+1], &row->chars[at], row->size - at + 1);
    row->size++;
    row->chars[at] = c;
    editorUpdateRow(row);

    E.dirty++;
}

/*** editor oprerations  ***/

void editorInsertChar(int c)
{
    if(E.cy == E.numrows) editorAppendRow("", 0);
    editorRowInsertChar(&E.row[E.cy], E.cx, c);
    E.cx += 1;
}

/*** file i/o  ***/
char* editorRowsToString(int *buflen)
{
    // returns both buff and total len of buff
    int totlen = 0;
    int j;
    for(j = 0; j < E.numrows; j += 1)
    {
        totlen += E.row[j].size + 1; 
    }
    
    *buflen = totlen;

    char* buf = malloc(totlen);
    char* p = buf;
    for(j = 0; j < E.numrows; j += 1)
    {
        memcpy(p, E.row[j].chars, E.row[j].size);
        p += E.row[j].size;
        *p = '\n';
        p++;
    }

    return buf;
}

void editorOpen(char* filename)
{
    FILE* fp = fopen(filename, "r");
    
    free(E.filename);
    E.filename = strdup(filename);

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
    E.dirty = 0;
}

void editorSave()
{
    if(E.filename == NULL) return;

    int len;
    char* buf = editorRowsToString(&len);

    int fd = open(E.filename, O_RDWR | O_CREAT, 0644);
    if(fd != -1)
    {
        if(ftruncate(fd, len) != -1)
        {
            if(write(fd, buf, len) == len)
            {
                close(fd);
                free(buf);
                editorSetStatusMessage("%d bytes written to desk", len);
                E.dirty = 0;
                return;
            }
        }
        close(fd);    
    }
    editorSetStatusMessage("Cant save! I/O error is: ", strerror(errno));
    free(buf);
}

/*** output  ***/

void editorScroll()
{
    E.rx = E.cx;

    if(E.cy < E.numrows)
    {
        E.rx = editorRowCxToRx(&E.row[E.cy], E.cx);
    }

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
    if(E.rx < E.coloff)
    {
        E.coloff = E.rx;
    }
    else if (E.rx >= E.coloff + E.screencols)
    {
        E.coloff = E.rx - E.screencols + 1;
    }
}

void editorDrawStatusBar(struct abuf* ab)
{
    abAppend(ab, "\x1b[7m", 4);

    char status[80], rstatus[80];
    
    int len = snprintf(status, sizeof(status), "%.20s - %d lines %s",
            E.filename ? E.filename : "[No Name]",
            E.numrows,
            E.dirty == 0 ? "" : "(modified)");
    int rlen = snprintf(rstatus, sizeof(rstatus), "%d/%d", E.cy + 1, E.numrows);

    if(len > E.screencols) len = E.screencols;

    abAppend(ab, status, len);

    while (len < E.screencols - rlen)
    {
        abAppend(ab, " ", 1);
        len += 1;
    }

    abAppend(ab, rstatus, rlen);

    abAppend(ab, "\x1b[m", 3);
    abAppend(ab, "\r\n", 2);
}

void editorDrawMessageBar(struct abuf* ab)
{
    abAppend(ab, "\x1b[K", 3);
    int msglen = strlen(E.statusmsg);
    if(msglen > E.screencols) msglen = E.screencols; 
    if(msglen && time(NULL) - E.statusmsg_time < 5) abAppend(ab, E.statusmsg, msglen);
}

void editorRefreshScreen()
{
    editorScroll();

    struct abuf ab = ABUF_INIT;

    abAppend(&ab, "\x1b[?25l", 6); // make cursor disappear
    abAppend(&ab, "\x1b[H", 3);

    editorDrawRows(&ab);
    editorDrawStatusBar(&ab);
    editorDrawMessageBar(&ab);

    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy - E.rowoff)+1, (E.rx - E.coloff)+1);
    abAppend(&ab, buf, strlen(buf));

    abAppend(&ab, "\x1b[?25h", 6); // make cursor reappear
    
    write(STDOUT_FILENO, ab.b, ab.len);
    abFree(&ab);
}

void editorSetStatusMessage(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);
    va_end(ap);
    E.statusmsg_time = time(NULL);
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
            if(row && E.cx < row->size)
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
        case '\r':
            // something
            break;

        case CTRL_KEY('q'):
            write(STDOUT_FILENO, "\x1b[2J", 4); 
            write(STDOUT_FILENO, "\x1b[H", 3); 
            exit(0); // 0 for success; 1 for failure
            break;
    
        case CTRL_KEY('s'):
            editorSave();
            break;

        case HOME_KEY:
            E.cx = 0;
            break;

        case END_KEY:
            if(E.cy < E.numrows)
                E.cx = E.row[E.cy].size; 
            break;

        case BACKSPACE: 
        case CTRL_KEY('h'):
        case DEL_KEY:
            // something
            break; 

        case PAGE_UP:
        case PAGE_DOWN:
            {
                if(c == PAGE_UP) E.cy = E.rowoff;
                else if(c == PAGE_DOWN)
                {
                    E.cy = E.rowoff + E.screenrows - 1;
                    if(E.cy > E.numrows) E.cy = E.numrows;
                }

                int times = E.screenrows;
                while (times--)
                {
                    editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
                }
                break;
            }

        case CTRL_KEY('l'):
        case '\x1b':
            break;

        case ARROW_UP:
        case ARROW_DOWN:
        case ARROW_RIGHT:
        case ARROW_LEFT:
            editorMoveCursor(c);
            break; 
        default: 
            editorInsertChar(c);
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

    editorSetStatusMessage("HELP: Ctrl-S = save | Ctrl-Q = quit");

    while(1)
    {
        editorRefreshScreen();
        editorProcessKeypress();
    }
    return 0;
}
