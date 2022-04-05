#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

#define CTRL_KEY(k) ((k) & 0x1f)
#define REM_VERSION "0.0.10"

/***************************************************/
/*  File: rem.c                                    */
/*  Author: Hifumi1337                             */
/*  Version: 0.0.10                                */
/*  Project: https://github.com/Hifumi1337/rem     */
/***************************************************/

enum editorKey {
    ARROW_LEFT = 1000,
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN,
    DEL_KEY,
    HOME_KEY,
    END_KEY,
    PAGE_UP,
    PAGE_DOWN,
};

typedef struct erow {
    int size;
    char *chars;
} erow;

struct editorConfig {
    int xpos, ypos;
    int rowoff;
    int coloff;
    int screenrows;
    int screencols;
    int numrows;
    erow *row;
    struct termios prev_terminal_state;
};

struct editorConfig EC;

// Destroys processes once they're complete or enter an error state
void destroy(const char *e) {
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);

    perror(e);
    exit(1);
}

void disableRawMode() {
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &EC.prev_terminal_state) == -1) {
        destroy("tcsetattr");
    }

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &EC.prev_terminal_state);
}

void enableRawMode() {
    if (tcgetattr(STDIN_FILENO, &EC.prev_terminal_state) == -1) {
        destroy("tcgetattr");
    }

    atexit(disableRawMode);
    
    struct termios raw = EC.prev_terminal_state;

    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= ~(CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);

    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
        destroy("tcsetattr");
    }
}

int editorReadKey() {
    int key_read;
    char i; // i = User input

    while ((key_read = read(STDIN_FILENO, &i, 1)) != 1) {
        if (key_read == -1 && errno == EAGAIN) {
            destroy("read");
        }
    }

    if (i == '\x1b') {
        char seq[3];

        if (read(STDIN_FILENO, &seq[0], 1) != 1) {
            return '\x1b';
        }

        if (read(STDIN_FILENO, &seq[1], 1) != 1) {
            return '\x1b';
        }

        if (seq[0] == '[') {
            if (seq[1] >= '0' && seq[1] <= '9') { 
                if (read(STDIN_FILENO, &seq[2], 1) != 1) {
                    return '\x1b';
                }

                if (seq[2] == '$') {
                    switch (seq[1]) {
                        case '1': return HOME_KEY;
                        case '3': return DEL_KEY;
                        case '4': return END_KEY;
                        case '5': return PAGE_UP;
                        case '6': return PAGE_DOWN;
                        case '7': return HOME_KEY;
                        case '8': return END_KEY;
                    }
                }
            } else {
                switch (seq[1]) {
                    case 'A': return ARROW_UP;
                    case 'B': return ARROW_DOWN;
                    case 'C': return ARROW_RIGHT;
                    case 'D': return ARROW_LEFT;
                    case 'H': return HOME_KEY;
                    case 'F': return END_KEY;
                }
            }
        } else if (seq[0] == 'O') {
            switch (seq[1]) {
                case 'H': return HOME_KEY;
                case 'F': return END_KEY;
            }
        }

        return '\x1b';
    } else {
        return i;
    }
}

int getCursorPos(int *rows, int *cols) {
    char buf[32];
    unsigned int n = 0;

    if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) {
        return -1;
    }

    while (n < sizeof(buf) - 1) {
        if (read(STDIN_FILENO, &buf[n], 1) != 1) {
            break;
        }

        if (buf[n] == 'R') { break; }
        n++;
    }

    buf[n] = '\0';

    if (buf[0] != '\x1b' || buf[1] != '[') { return -1; }
    
    if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) {
        return -1;
    }
    
    return 0;
}

// Obtains the size of the user's terminal
int getWindowSize(int *rows, int *cols) {
    struct winsize w;

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == -1 || w.ws_col == 0) {
        if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) { return -1; }
        return getCursorPos(rows, cols);
    } else {
        *cols = w.ws_col;
        *rows = w.ws_row;
        return 0;
    }
}

void editorAppendRow(char *s, size_t len) {
    EC.row = realloc(EC.row, sizeof(erow) * (EC.numrows + 1));

    int ll = EC.numrows;

    EC.row[ll].size = len;
    EC.row[ll].chars = malloc(len + 1);
    memcpy(EC.row[ll].chars, s, len);
    EC.row[ll].chars[len] = '\0';
    EC.numrows++;
}

void editorOpen(char *filename) {
    FILE *filepath = fopen(filename, "r");

    if (!filepath) {
        destroy("fopen");
    }

    char *line = NULL;
    size_t linecap = 0;
    ssize_t line_len;

    while ((line_len = getline(&line, &linecap, filepath)) != -1) {
        while (line_len > 0 && (line[line_len - 1] == '\n' || line[line_len - 1] == '\r')) {
            line_len--;
        }

        editorAppendRow(line, line_len);
    }

    free(line);
    fclose(filepath);
}

struct abuf {
    char *b;
    int len;
};

#define ABUF_INIT {NULL, 0}

void aAppend(struct abuf *ab, const char *s, int len) {
    char *new = realloc(ab->b, ab->len + len);

    if (new == NULL) { return; }

    memcpy(&new[ab->len], s, len);
    ab->b = new;
    ab->len += len;
}

void aFree(struct abuf *ab) {
    free(ab->b);
}

void editorScroll() {
    if (EC.ypos < EC.rowoff) {
        EC.rowoff = EC.ypos;
    }

    if (EC.ypos >= EC.rowoff + EC.screenrows) {
        EC.rowoff = EC.ypos - EC.screenrows + 1;
    }

    if (EC.xpos < EC.coloff) {
        EC.coloff = EC.xpos;
    }

    if (EC.xpos >= EC.coloff + EC.screencols) {
        EC.coloff = EC.xpos - EC.screencols + 1;
    }
}

// Draws a $ on the left side of the terminal, regardless of size
void editorDrawRows(struct abuf *ab) {
    int r;

    for (r = 0; r < EC.screenrows; r++) {
        int filerow = r + EC.rowoff;

        if (filerow >= EC.numrows) {
            if (EC.numrows == 0 && r == EC.screenrows / 3) {
                char intro[80];
                int intro_len = snprintf(intro, sizeof(intro), "Rem Terminal Editor | v%s", REM_VERSION);

                if (intro_len > EC.screencols) {
                    intro_len = EC.screencols;
                }

                int padding = (EC.screencols - intro_len) / 2;

                if (padding) {
                    aAppend(ab, "$", 1);
                    padding--;
                }

                while (padding--) { aAppend(ab," ", 1); }
                
                aAppend(ab, intro, intro_len);
            
            } else {
                aAppend(ab, "$", 1);
            }
        } else {
            int col_len = EC.row[filerow].size - EC.coloff;

            if (col_len < 0) {
                col_len = 0;
            }
            
            if (col_len > EC.screencols) {
                col_len = EC.screencols;
            }
            
            aAppend(ab, &EC.row[filerow].chars[EC.coloff], col_len);
        }

        aAppend(ab, "\x1b[K", 3);

        if (r < EC.screenrows - 1) {
            aAppend(ab, "\r\n", 2);
        }
    }
}

void editorRefreshScreen() {
    editorScroll();

    struct abuf ab = ABUF_INIT;

    aAppend(&ab, "\x1b[?25l", 6);
    aAppend(&ab, "\x1b[H", 3);

    editorDrawRows(&ab);

    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (EC.ypos - EC.rowoff) + 1, (EC.xpos - EC.coloff) + 1);

    aAppend(&ab, buf, strlen(buf));
    aAppend(&ab, "\x1b[?25h", 6);

    write(STDOUT_FILENO, ab.b, ab.len);
    aFree(&ab);
}

void editorMoveCursor(int key) {
    switch (key) {
        case ARROW_LEFT:
            if (EC.xpos != 0) {
                EC.xpos--;
            }
            break;
        case ARROW_RIGHT:
            EC.xpos++;
            break;
        case ARROW_UP:
            if (EC.ypos != 0) {
                EC.ypos--;
            }
            break;
        case ARROW_DOWN:
            if (EC.ypos < EC.numrows) {
                EC.ypos++;
            }
            break;
    }
}

void editorProcessKey() {
    int i = editorReadKey();

    switch (i) {
        case CTRL_KEY('q'): // Exits the editor
            write(STDOUT_FILENO, "\x1b[2J", 4);
            write(STDOUT_FILENO, "\x1b[H", 3);
            exit(0);
            break;
        case HOME_KEY:
            EC.xpos = 0;
            break;
        case END_KEY:
            EC.xpos = EC.screencols - 1;
            break;
        case PAGE_UP:
        case PAGE_DOWN:
            {
                int n_times = EC.screenrows;

                while (n_times--) {
                    editorMoveCursor(i == PAGE_UP ? ARROW_UP : ARROW_DOWN);
                }
            }
            break;
        case ARROW_UP:
        case ARROW_DOWN:
        case ARROW_LEFT:
        case ARROW_RIGHT:
            editorMoveCursor(i);
            break;
    }
}

void initEditor() {
    EC.xpos = 0;
    EC.ypos = 0;
    EC.rowoff = 0;
    EC.coloff = 0;
    EC.numrows = 0;
    EC.row = NULL;

    if (getWindowSize(&EC.screenrows, &EC.screencols) == -1) {
        destroy("getWindowSize");
    }
}

/* ⚡ ᕙ(`▿´)ᕗ ⚡ */
int main(int argc, char *argv[]) {
    enableRawMode();
    initEditor();

    if (argc >= 2) {
        editorOpen(argv[1]);
    }

    // Main editor loop
    while (1) {
        editorRefreshScreen();
        editorProcessKey();
    }
    
    return 0;
}