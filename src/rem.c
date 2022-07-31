/***************************************************/
/*  File: rem.c                                    */
/*  Author: Hifumi1337                             */
/*  Version: 1.2.37                                */
/*  Project: https://github.com/Hifumi1337/rem     */
/***************************************************/

#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "utils/syntax_hl.h"
#include "utils/bindings.h"

#define CTRL_KEY(k) ((k) & 0x1f)
#define VERSION "1.2.37"
#define TAB_STOP 4
#define QUIT_TIMES 1
#define DEFAULT_MSG "^X: Exit | ^S: Save | ^Q: Query"

typedef struct erow {
    int idx;
    int size;
    int rsize;
    char *chars;
    char *render;
    unsigned char *syntax_hl;
    int multi_syntax_hl;
} erow;

struct editorConfig {
    int xpos, ypos;
    int rx;
    int rowoff;
    int coloff;
    int screenrows;
    int screencols;
    int numrows;
    erow *row;
    int dirty;
    char *filename;
    char statusmsg[80];
    time_t statusmsg_time;
    struct editorSyntax *syntax;
    struct termios prev_terminal_state;
};

struct editorConfig EC;

void editorSetStatusMessage(const char *fmt, ...);
void editorRefreshScreen();
char *editorPrompt(char *prompt, void (*callback)(char *, int));

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

int is_seperator(int c) {
    return isspace(c) || c == '\0' || strchr(",.()+-/*=~%<>[];", c) != NULL;
}

void editorUpdateSyntax(erow *row) {
    row->syntax_hl = realloc(row->syntax_hl, row->rsize);
    memset(row->syntax_hl, SYNTAX_HL_DEFAULT, row->rsize);

    if (EC.syntax == NULL) { return; }

    char **keywords = EC.syntax->keywords;

    char *scs = EC.syntax->singleline_comment_s;
    char *mcs = EC.syntax->multiline_comment_s;
    char *mce = EC.syntax->multiline_comment_e;

    int scs_len = scs ? strlen(scs) : 0;
    int mcs_len = mcs ? strlen(mcs) : 0;
    int mce_len = mce ? strlen(mce) : 0;

    int prev_seperator = 1;
    int in_str = 0;
    int in_comment = (row->idx > 0 && EC.row[row->idx - 1].multi_syntax_hl);
    
    int i = 0;

    while (i < row->rsize) {
        char c = row->render[i];
        unsigned char prev_syntax_hl = (i > 0) ? row->syntax_hl[i - 1] : SYNTAX_HL_DEFAULT;

        if (scs_len && !in_str && !in_comment) {
            if (!strncmp(&row->render[i], scs, scs_len)) {
                memset(&row->syntax_hl[i], SYNTAX_HL_COMMENT, row->rsize - i);
                break;
            }
        }

        if (mcs_len && mce_len && !in_str) {
            if (in_comment) {
                row->syntax_hl[i] = SYNTAX_HL_MULTI_COMMENT;

                if (!strncmp(&row->render[i], mce, mce_len)) {
                    memset(&row->syntax_hl[i], SYNTAX_HL_MULTI_COMMENT, mce_len);

                    i += mce_len;
                    in_comment = 0;
                    prev_seperator = 1;
                    continue;
                } else {
                    i++;
                    continue;
                }
            } else if (!strncmp(&row->render[i], mcs, mcs_len)) {
                memset(&row->syntax_hl[i], SYNTAX_HL_MULTI_COMMENT, mcs_len);

                i += mcs_len;
                in_comment = 1;
                continue;
            }
        }

        if (EC.syntax->flags & HL_STRINGS) {
            if (in_str) {
                row->syntax_hl[i] = SYNTAX_HL_STR;

                if (c == '\\' && i + 1 < row->rsize) {
                    row->syntax_hl[i + 1] = SYNTAX_HL_STR;
                    i += 2;
                    continue;
                }

                if (c == in_str) {
                    in_str = 0;
                }

                i++;
                prev_seperator = 1;
                continue;
            } else {
                if (c == '"' || c == '\'') {
                    in_str = c;
                    row->syntax_hl[i] = SYNTAX_HL_STR;
                    i++;
                    continue;
                }
            }
        }

        if (EC.syntax->flags & HL_NUMBERS) {
            if ((isdigit(c) && (prev_seperator || prev_syntax_hl == SYNTAX_HL_NUM)) || (c == '.' && prev_syntax_hl == SYNTAX_HL_NUM)) {
                row->syntax_hl[i] = SYNTAX_HL_NUM;
                i++;
                prev_seperator = 0;
                continue;
            }
        }

        if (prev_seperator) {
            int k;

            for (k = 0; keywords[k]; k++) {
                int key_len = strlen(keywords[k]);
                
                // Add new lines for extra keywords
                int kw2 = keywords[k][key_len - 1] == '2';
                // int kw3 = keywords[k][key_len - 1] == '3';

                // Add new lines for extra keywords
                if (kw2) { key_len--; }
                // if (kw3) { key_len--; }

                if (!strncmp(&row->render[i], keywords[k], key_len) && is_seperator(row->render[i + key_len])) {
                    
                    // Add new lines for extra keywords
                    memset(&row->syntax_hl[i], kw2 ? SYNTAX_HL_KEYWORD2 : SYNTAX_HL_KEYWORD1, key_len);
                    // memset(&row->syntax_hl[i], kw3 ? SYNTAX_HL_KEYWORD3 : SYNTAX_HL_KEYWORD1, key_len);
                    
                    i += key_len;
                    break;
                }
            }

            if (keywords[k] != NULL) {
                prev_seperator = 0;
                continue;
            }
        }

        prev_seperator = is_seperator(c);
        i++;
    }

    int data_updated = (row->multi_syntax_hl != in_comment);
    row->multi_syntax_hl = in_comment;

    if (data_updated && row->idx + 1 < EC.numrows) {
        editorUpdateSyntax(&EC.row[row->idx + 1]);
    }
}

void editorSetSyntaxHl() {
    EC.syntax = NULL;

    if (EC.filename == NULL) { return; }

    char *ext = strchr(EC.filename, '.');

    for (unsigned int e = 0; e < SYNTAXDB_ENTRIES; e++) {
        struct editorSyntax *s = &SyntaxDB[e];
        unsigned int i = 0;

        while (s->filematch[i]) {
            int is_ext = (s->filematch[i][0] == '.');

            if ((is_ext && ext && !strcmp(ext, s->filematch[i])) || (!is_ext && strstr(EC.filename, s->filematch[i]))) {
                EC.syntax = s;

                int frow;

                for (frow = 0; frow < EC.numrows; frow++) {
                    editorUpdateSyntax(&EC.row[frow]);
                }

                return;
            }

            i++;
        }
    }
}

// Converts x-position to render index position
int editorRowXposToRx(erow *row, int xpos) {
    int rx = 0;
    int u;

    for (u = 0; u < xpos; u++) {
        if (row->chars[u] == '\t') {
            rx += (TAB_STOP - 1) - (rx % TAB_STOP);
        }
        rx++;
    }
    return rx;
}

int editorRowRxToXpos(erow *row, int rx) {
    int cur_rx = 0;
    int xpos;

    for (xpos = 0; xpos < row->size; xpos++) {
        if (row->chars[xpos] == '\t') {
            cur_rx += (TAB_STOP - 1) - (cur_rx % TAB_STOP);
        }

        cur_rx++;

        if (cur_rx > rx) { return xpos; }
    }

    // Handles any out of range returns
    return xpos;
}

void editorUpdateRow(erow *row) {
    int tabs = 0;
    int u;

    for (u = 0; u < row->size; u++) {
        if (row->chars[u] == '\t') {
            tabs++;
        }
    }

    free(row->render);
    row->render = malloc(row->size + tabs*(TAB_STOP - 1) + 1);

    int eur = 0;

    for (u = 0; u < row->size; u++) {
        if (row->chars[u] == '\t') {
            row->render[eur++] = ' ';

            while (eur % TAB_STOP != 0) {
                row->render[eur++] = ' ';
            }
        } else {
            row->render[eur++] = row->chars[u];
        }
    }

    row->render[eur] = '\0';
    row->rsize = eur;

    editorUpdateSyntax(row);
}

void editorInsertRow(int at, char *s, size_t len) {
    if (at < 0 || at > EC.numrows) { return; }
    
    EC.row = realloc(EC.row, sizeof(erow) * (EC.numrows + 1));
    memmove(&EC.row[at + 1], &EC.row[at], sizeof(erow) * (EC.numrows - at));

    for (int a = at + 1; a <= EC.numrows; a++) {
        EC.row[a].idx++;
    }
    
    EC.row[at].idx = at;
    EC.row[at].size = len;
    EC.row[at].chars = malloc(len + 1);
    
    memcpy(EC.row[at].chars, s, len);
    
    EC.row[at].chars[len] = '\0';
    EC.row[at].rsize = 0;
    EC.row[at].render = NULL;
    EC.row[at].syntax_hl = NULL;
    EC.row[at].multi_syntax_hl = 0;

    editorUpdateRow(&EC.row[at]);

    EC.numrows++;
    EC.dirty++;
}

void editorFreeRow(erow *row) {
    free(row->render);
    free(row->chars);
    free(row->syntax_hl);
}

void editorDelRow(int at) {
    if (at < 0 || at >= EC.numrows) { return; }

    editorFreeRow(&EC.row[at]);
    memmove(&EC.row[at], &EC.row[at + 1], sizeof(erow) * (EC.numrows - at - 1));

    for (int a = at; a < EC.numrows - 1; a++) {
        EC.row[a].idx--;
    }

    EC.numrows--;
    EC.dirty++;
}

// Inserts a single character into the editor row
void editorRowInsertChar(erow *row, int at, int c) {
    if (at < 0 || at > row->size) {
        at = row->size;
    }

    row->chars = realloc(row->chars, row->size + 2);
    memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1);
    row->size++;
    row->chars[at] = c;

    editorUpdateRow(row);
    EC.dirty++;
}

void editorRowAppendStr(erow *row, char *s, size_t len) {
    row->chars = realloc(row->chars, row->size + len + 1);
    memcpy(&row->chars[row->size], s, len);
    row->size += len;
    row->chars[row->size] = '\0';
    editorUpdateRow(row);
    EC.dirty++;
}

void editorInsertNewLine() {
    if (EC.xpos == 0) {
        editorInsertRow(EC.ypos, "", 0);
    } else {
        erow *row = &EC.row[EC.ypos];
        editorInsertRow(EC.ypos + 1, &row->chars[EC.xpos], row->size - EC.xpos);
        row = &EC.row[EC.ypos];
        row->size = EC.xpos;
        row->chars[row->size] = '\0';
        editorUpdateRow(row);
    }

    EC.ypos++;
    EC.xpos = 0;
}

void editorRowDelChar(erow *row, int at) {
    if (at < 0 || at >= row->size) { return; }

    memmove(&row->chars[at], &row->chars[at + 1], row->size - at);
    row->size--;
    
    editorUpdateRow(row);
    EC.dirty++;
}

void editorInsertChar(int c) {
    if (EC.ypos == EC.numrows) {
        editorInsertRow(EC.numrows, "", 0);
    }

    editorRowInsertChar(&EC.row[EC.ypos], EC.xpos, c);
    EC.xpos++;
}

// Deletes the character when a backspace is detected
void editorDelChar() {
    if (EC.ypos == EC.numrows) { return; }
    if (EC.xpos == 0 && EC.ypos == 0) { return; }

    erow *row = &EC.row[EC.ypos];

    if (EC.xpos > 0) {
        editorRowDelChar(row, EC.xpos - 1);
        EC.xpos--;
    } else {
        EC.xpos = EC.row[EC.ypos - 1].size;
        editorRowAppendStr(&EC.row[EC.ypos - 1], row->chars, row->size);
        editorDelRow(EC.ypos);
        EC.ypos--;
    }
}

char *editorRowsToString(int *buflen) {
    int totlen = 0;
    int j;

    for (j = 0; j < EC.numrows; j++) {
        totlen += EC.row[j].size + 1;
    }
    *buflen = totlen;

    char *buf = malloc(totlen);
    char *p = buf;

    for (j =0; j < EC.numrows; j++) {
        memcpy(p, EC.row[j].chars, EC.row[j].size);
        p += EC.row[j].size;
        *p = '\n';
        p++;
    }

    return buf;
}

void editorOpen(char *filename) {
    free(EC.filename);
    EC.filename = strdup(filename);

    editorSetSyntaxHl();

    FILE *filepath = fopen(filename, "r");

    if (!filepath) {
        destroy("fopen");
    }

    char *line = NULL;
    size_t linecap = 0;
    ssize_t line_len;

    while ((line_len = getline(&line, &linecap, filepath)) != -1) {
        while (line_len > 0 && (line[line_len - 1] == '\n' ||
                                line[line_len - 1] == '\r')) {
            line_len--;
        }

        editorInsertRow(EC.numrows, line, line_len);
    }

    free(line);
    fclose(filepath);
    EC.dirty = 0;
}

void editorSave() {
    // Checks if the file is a new file. Prompts for a new filename.
    if (EC.filename == NULL) {
        EC.filename = editorPrompt("Save file as (ESC to cancel): %s", NULL);

        if (EC.filename == NULL) {
            editorSetStatusMessage("%s | Status: Save Aborted | v%s", DEFAULT_MSG, VERSION);
            return;
        }

        editorSetSyntaxHl();
    }

    // If the file doesn't exist we do the following:
    int len;
    char *buf = editorRowsToString(&len);
    int fo = open(EC.filename, O_RDWR | O_CREAT, 0644); // Create a new file (O_CREAT), open the file for R & W (O_RDWR). Default permissions set to 644 (Owner: RW, Everyone Else: R)

    if (fo != -1) {
        // Sets the file length - Safer than O_TRUNC (We can't be losing any data!)
        if (ftruncate(fo, len) != -1) {
            // Writes to the file
            if (write(fo, buf, len) == len) {
                close(fo);
                free(buf); // Frees memory
                EC.dirty = 0;
                editorSetStatusMessage("%s | Status: %d bytes written to disk | v%s", DEFAULT_MSG, len, VERSION); // Displays number of bytes written to disk
                return;
            }
        }
        close(fo);
    }

    free(buf); // Frees memory
    editorSetStatusMessage("%s | Status: Unable to save file: %s | v%s", DEFAULT_MSG, strerror(errno), VERSION); // Notifies of an error is unable to save file
}

// Incremental search function
void editorSearchCallback(char *query, int key) {
    static int last_match = -1;
    static int direction = 1;
    static int save_syn_line;
    static char *save_syn_hl = NULL;

    if (save_syn_hl) {
        memcpy(EC.row[save_syn_line].syntax_hl, save_syn_hl, EC.row[save_syn_line].rsize);
        free(save_syn_hl);
        save_syn_hl = NULL;
    }

    if (key == '\r' || key == '\x1b') {
        last_match = -1;
        direction = 1;
        return;
    } else if (key == ARROW_RIGHT || key == ARROW_DOWN) {
        direction = 1;
    } else if (key == ARROW_LEFT || key == ARROW_UP) {
        direction = -1;
    } else {
        last_match = -1;
        direction = 1;
    }

    if (last_match == -1) { direction = 1; }
    
    int current_pos = last_match;
    int q;

    for (q = 0; q < EC.numrows; q++) {
        current_pos += direction;

        if (current_pos == -1) {
            current_pos = EC.numrows - 1;
        } else if (current_pos == EC.numrows) {
            current_pos = 0;
        }

        erow *row = &EC.row[current_pos];

        char *match = strstr(row->render, query);

        if (match) {
            last_match = current_pos;
            EC.ypos = current_pos;
            EC.xpos = editorRowRxToXpos(row, match - row->render);
            EC.xpos = match - row->render;
            EC.rowoff = EC.numrows;

            save_syn_line = current_pos;
            save_syn_hl = malloc(row->rsize);
            
            memcpy(save_syn_hl, row->syntax_hl, row->rsize);
            memset(&row->syntax_hl[match - row->render], SYNTAX_HL_QUERY, strlen(query));
            
            break;
        }
    }
}

// Searches each row for query
void editorSearch() {
    int s_xpos = EC.xpos; // Saves x position
    int s_ypos = EC.ypos; // Saves y position
    int s_coloff = EC.coloff; // Saves column position
    int s_rowoff = EC.rowoff; // Saves row position

    char *query = editorPrompt("Query (ESC to cancel): %s (Search using Arrows/Enter)", editorSearchCallback);

    if (query) {
        free(query);
    } else {
        // Returns user to their original mouse position after search
        EC.xpos = s_xpos;
        EC.ypos = s_ypos;
        EC.coloff = s_coloff;
        EC.rowoff = s_rowoff;
    }
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
    EC.rx = 0;

    if (EC.ypos < EC.numrows) {
        EC.rx = editorRowXposToRx(&EC.row[EC.ypos], EC.xpos);
    }

    if (EC.ypos < EC.rowoff) {
        EC.rowoff = EC.ypos;
    }

    if (EC.ypos >= EC.rowoff + EC.screenrows) {
        EC.rowoff = EC.ypos - EC.screenrows + 1;
    }

    if (EC.rx < EC.coloff) {
        EC.coloff = EC.rx;
    }

    if (EC.rx >= EC.coloff + EC.screencols) {
        EC.coloff = EC.rx - EC.screencols + 1;
    }
}

// Draws a $ on the left side of the terminal, regardless of size + draws entire row of terminal
void editorDrawRows(struct abuf *ab) {
    int r;

    for (r = 0; r < EC.screenrows; r++) {
        int filerow = r + EC.rowoff;

        if (filerow >= EC.numrows) {
            if (EC.numrows == 0 && r == EC.screenrows / 3) {
                char intro[80];
                int intro_len = snprintf(intro, sizeof(intro), "Rem Terminal Editor | v%s", VERSION);

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
            int col_len = EC.row[filerow].rsize - EC.coloff;

            if (col_len < 0) {
                col_len = 0;
            }
            
            if (col_len > EC.screencols) {
                col_len = EC.screencols;
            }

            char *c = &EC.row[filerow].render[EC.coloff];
            unsigned char *syntax_hl = &EC.row[filerow].syntax_hl[EC.coloff];
            int current_color = -1;
            
            int j;

            for (j = 0; j < col_len; j++) {
                if (iscntrl(c[j])) {
                    char sym = (c[j] <= 26) ? '@' + c[j] : '?';
                    aAppend(ab, "\x1b[7m", 4);
                    aAppend(ab, &sym, 1);
                    aAppend(ab, "\x1b[m", 3);

                    if (current_color != -1) {
                        char buf[16];
                        int c_len = snprintf(buf, sizeof(buf), "\x1b[%dm", current_color);
                        aAppend(ab, buf, c_len);
                    }
                } else if (syntax_hl[j] == SYNTAX_HL_DEFAULT) {

                    if (current_color != -1) {
                        aAppend(ab, "\x1b[39m", 5);
                        current_color = -1;
                    }
                    
                    aAppend(ab, &c[j], 1);
                
                } else {
                    int syntax_color = editorSyntaxToColor(syntax_hl[j]);

                    if (syntax_color != current_color) {
                        current_color = syntax_color;

                        char buf[16];
                        int c_len = snprintf(buf, sizeof(buf), "\x1b[%dm", syntax_color);
                        aAppend(ab, buf, c_len);
                    }

                    aAppend(ab, &c[j], 1);
                }
            }

            aAppend(ab, "\x1b[39m", 5);
        }

        aAppend(ab, "\x1b[K", 3);
        aAppend(ab, "\r\n", 2);
    }
}

void editorDrawStatusBar(struct abuf *ab) {
    aAppend(ab, "\x1b[7m", 4);

    char status[80], rstatus[80];

    int len = snprintf(status, sizeof(status), "%.20s - %d lines %s", EC.filename ? EC.filename : "[No File Chosen]", EC.numrows, EC.dirty ? "(modified)" : "");

    int rlen = snprintf(rstatus, sizeof(rstatus), "Filetype: %s | %d/%d", EC.syntax ? EC.syntax->filetype : "No Filetype Present", EC.ypos + 1, EC.numrows);

    if (len > EC.screencols) {
        len = EC.screencols;
    }

    aAppend(ab, status, len);

    while (len < EC.screencols) {
        if (EC.screencols - len == rlen) {
            aAppend(ab, rstatus, rlen);
            break;
        } else {
            aAppend(ab, " ", 1);
            len++;
        }
    }
    aAppend(ab, "\x1b[m", 3);
    aAppend(ab, "\r\n", 2);
}

void editorDrawMessageBar(struct abuf *ab) {
    aAppend(ab, "\x1b[K", 3);

    int msg_len = strlen(EC.statusmsg);

    if (msg_len > EC.screencols) {
        msg_len = EC.screencols;
    }

    // How many seconds before the status messahe disappears (set to 10s)
    if (msg_len && time(NULL) - EC.statusmsg_time < 10) {
        aAppend(ab, EC.statusmsg, msg_len);
    }
}

void editorRefreshScreen() {
    editorScroll();

    struct abuf ab = ABUF_INIT;

    aAppend(&ab, "\x1b[?25l", 6);
    aAppend(&ab, "\x1b[H", 3);

    editorDrawRows(&ab);
    editorDrawStatusBar(&ab);
    editorDrawMessageBar(&ab);

    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (EC.ypos - EC.rowoff) + 1,
                                              (EC.rx - EC.coloff) + 1);

    aAppend(&ab, buf, strlen(buf));
    aAppend(&ab, "\x1b[?25h", 6);

    write(STDOUT_FILENO, ab.b, ab.len);
    aFree(&ab);
}


void editorSetStatusMessage(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(EC.statusmsg, sizeof(EC.statusmsg), fmt, ap);
    va_end(ap);

    EC.statusmsg_time = time(NULL);
}

// Prompt for status bar (saving files)
char *editorPrompt(char *prompt, void (*callback)(char *, int)) {
    size_t bufsize = 128;
    char *buf = malloc(bufsize);

    size_t buflen = 0;
    buf[0] = '\0';

    while (1) {
        editorSetStatusMessage(prompt, buf);
        editorRefreshScreen();

        int k = editorReadKey();

        if (k == DEL_KEY || k == CTRL_KEY('h') || k == BACKSPACE) {
            if (buflen != 0) {
                buf[--buflen] = '\0';
            }
        } else if (k == '\x1b') { // ESC key
            // editorSetStatusMessage("");
            editorSetStatusMessage("%s | v%s", DEFAULT_MSG, VERSION);
            if (callback) { callback(buf, k); }
            free(buf);
            return NULL;
        }  else if (k == '\r') {
            if (buflen != 0) {
                editorSetStatusMessage("");
                if (callback) { callback(buf, k); }
                return buf;
            }
        } else if (!iscntrl(k) && k < 128) {
            if (buflen == bufsize -1) {
                bufsize += 2;
                buf = realloc(buf, bufsize);
            }

            buf[buflen++] = k;
            buf[buflen] = '\0';
        }

        if (callback) { callback(buf, k); }
    }
}

void editorMoveCursor(int key) {
    erow *row = (EC.ypos >= EC.numrows) ? NULL : &EC.row[EC.ypos];

    switch (key) {
        case ARROW_LEFT:
            if (EC.xpos != 0) {
                EC.xpos--;
            } else if (EC.ypos > 0) {
                EC.ypos--;
                EC.xpos = EC.row[EC.ypos].size;
            }
            break;
        case ARROW_RIGHT:
            if (row && EC.xpos < row->size) {
                EC.xpos++;
            } else if (row && EC.xpos == row->size) {
                EC.ypos++;
                EC.xpos = 0;
            }
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

    row = (EC.ypos >= EC.numrows) ? NULL : &EC.row[EC.ypos];
    int row_len = row ? row->size : 0;

    if (EC.xpos > row_len) {
        EC.xpos = row_len;
    }
}

void editorProcessKey() {
    static int quit_times = QUIT_TIMES;

    int i = editorReadKey();

    switch (i) {
        case '\r': // Enter key
            editorInsertNewLine();
            break;
        case CTRL_KEY('x'): // Exits the editor
            if (EC.dirty && quit_times > 0) {
                editorSetStatusMessage("[WARNING] File has unsaved changes! Press ^X again to exit", quit_times);
                quit_times--;
                return;
            }

            write(STDOUT_FILENO, "\x1b[2J", 4);
            write(STDOUT_FILENO, "\x1b[H", 3);
            exit(0);
            break;
        case CTRL_KEY('s'): // Saves the file
            editorSave();
            
            int is_writable;
            is_writable = access(EC.filename, W_OK);

            if (EC.filename != NULL && is_writable == 0) {
                editorSetStatusMessage("%s | Status: File saved | v%s", DEFAULT_MSG, VERSION); // Displays message only if a file is open + saved
            } else {
                editorSetStatusMessage("%s | Status: File is not writable | v%s", DEFAULT_MSG, VERSION);
            }
            
            break;
        case HOME_KEY:
            EC.xpos = 0;
            break;
        case END_KEY:
            if (EC.ypos < EC.numrows) {
                EC.xpos = EC.row[EC.ypos].size;
            }
            break;
        case CTRL_KEY('q'):
            editorSearch();

            if ('\r') {
                editorSetStatusMessage("%s | Status: Search Aborted | v%s", DEFAULT_MSG, VERSION); // Displays message only if a file is open + saved
            }

            break;
        case BACKSPACE:
        case CTRL_KEY('h'):
        case DEL_KEY:
            if (i == DEL_KEY) {
                editorMoveCursor(ARROW_RIGHT);
            }
            
            editorDelChar();
            break;
        case PAGE_UP:
        case PAGE_DOWN:
            {
                if (i == PAGE_UP) {
                    EC.ypos = EC.rowoff;
                } else if (i == PAGE_DOWN) {
                    EC.ypos = EC.rowoff + EC.screenrows - 1;
                    
                    if (EC.ypos > EC.numrows) {
                        EC.ypos = EC.numrows;
                    }
                }

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
        case CTRL_KEY('l'):
        case '\x1b':
            break;
        default:
            editorInsertChar(i);
            break;
    }

    quit_times = QUIT_TIMES;
}

void initEditor() {
    EC.xpos = 0;
    EC.ypos = 0;
    EC.rx = 0;
    EC.rowoff = 0;
    EC.coloff = 0;
    EC.numrows = 0;
    EC.row = NULL;
    EC.dirty = 0;
    EC.filename = NULL;
    EC.statusmsg[0] = '\0';
    EC.statusmsg_time = 0;
    EC.syntax = NULL;

    if (getWindowSize(&EC.screenrows, &EC.screencols) == -1) {
        destroy("getWindowSize");
    }

    EC.screenrows -= 2;
}

/* ⚡ ᕙ(`▿´)ᕗ ⚡ */
int main(int argc, char *argv[]) {

    int is_writable;

    if (argc == 2) {
        if (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help") || !strcmp(argv[1], "help")) {
            printf("Creator: Hifumi1337 (https://github.com/Hifumi1337)\n");
            printf("Version: %s\n\n", VERSION);

            printf("01010010 01100101 01101101\n\n");

            printf("Rem is a terminal code editor for personal use that does most things terminal editors do, but it's a very basic implementation, so there's not too much bloat\n\n");
            
            printf("Simple Command(s) Overview:\n");
            printf("Ctrl+X => Exit the terminal editor\n");
            printf("Ctrl+Q => Search the contents of the open file\n");
            printf("Ctrl+S => Save the contents of the file to disk\n\n");
            exit(0);
        } else if (!strcmp(argv[1], "-v") || !strcmp(argv[1], "--version")) {
            printf("Rem: v%s\n\n", VERSION);
            exit(0);
        }
    }
    
    enableRawMode();
    initEditor();

    if (argc >= 2) {
        editorOpen(argv[1]);
    }

    is_writable = access(EC.filename, W_OK); // Checks if file is writable

    if (is_writable == 0) {
        editorSetStatusMessage("%s | v%s", DEFAULT_MSG, VERSION);
    } else {
        editorSetStatusMessage("%s | v%s - %s is not writable", DEFAULT_MSG, VERSION, EC.filename);
    }

    // Main editor loop
    while (1) {
        editorRefreshScreen();
        editorProcessKey();
    }
    
    return 0;
}
