// Syntax highlighting
enum editorSyntaxHl {
    SYNTAX_HL_DEFAULT = 0,
    SYNTAX_HL_COMMENT,
    SYNTAX_HL_MULTI_COMMENT,
    SYNTAX_HL_KEYWORD1,
    SYNTAX_HL_KEYWORD2,
    // SYNTAX_HL_KEYWORD3,
    SYNTAX_HL_STR,
    SYNTAX_HL_NUM,
    SYNTAX_HL_QUERY
};

#define HL_NUMBERS (1<<0)
#define HL_STRINGS (1<<1)

struct editorSyntax {
    char *filetype;
    char **filematch;
    char **keywords;
    char *singleline_comment_s;
    char *multiline_comment_s;
    char *multiline_comment_e;
    int flags;
};

// C/C++
char *syntax_hl_extensions_c[] = {
    ".c",
    ".h",
    ".cpp",
    NULL
};

char *syntax_hl_keywords_c[] = {

    // Keyword 1
    "define", "sizeof", "int", "switch", "case", "char", "for", "while", "return", "if", "break", "continue", "else", "else if", "true", "struct", "union", "typedef", "static", "include", "printf", "enum", "void", "const",

    // Keyword 2
    "false2",

    // Keyword 3
    // "main3",
    
    NULL
};

// Python
char *syntax_hl_extensions_py[] = {
    ".py",
    NULL
};

char *syntax_hl_keywords_py[] = {

    // Keyword 1
    "def", "for", "while", "return", "if", "break", "continue", "else", "else if", "True", "self", "print", "try", "except", "class", "None", "__init__",

    // Keyword 2
    "False2",

    // Keyword 3
    // "main3",
    
    NULL
};

struct editorSyntax SyntaxDB[] = {
    {
        "c",
        syntax_hl_extensions_c,
        syntax_hl_keywords_c,
        "//", "/*", "*/",
        HL_NUMBERS | HL_STRINGS
    },
    {
        "py",
        syntax_hl_extensions_py,
        syntax_hl_keywords_py,
        "#", "/*", "*/", // I know Python uses """, but that breaks everything
        HL_NUMBERS | HL_STRINGS
    },
};

#define SYNTAXDB_ENTRIES (sizeof(SyntaxDB) / sizeof(SyntaxDB[0]))

/*
Color schema

The editorSyntaxToColor() function in rem.c handles color assignment.
This switch/case just returns the number in the middle of the ANSI code.

ex: \e[0;30m => return 30
*/

int editorSyntaxToColor(int syntax_hl) {
    switch (syntax_hl) {
        case SYNTAX_HL_COMMENT:
        case SYNTAX_HL_MULTI_COMMENT:
            return 36; // Cyan
        case SYNTAX_HL_KEYWORD1:
            return 95; // Purple
        case SYNTAX_HL_KEYWORD2:
            return 91; // Red
        // case SYNTAX_HL_KEYWORD3:
        //     return 92; // Green
        case SYNTAX_HL_STR:
            return 93; // Yellow
        case SYNTAX_HL_NUM:
            return 35; // Purple
        case SYNTAX_HL_QUERY:
            return 34; // Blue
        default:
            return 37; // White
    }
}
