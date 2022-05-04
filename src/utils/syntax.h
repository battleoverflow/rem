#include <stdio.h>

#define HL_NUMBERS (1<<0)
#define HL_STRINGS (1<<1)

// Syntax highlighting enum
enum editorSyntaxHl {
    SYNTAX_HL_DEFAULT = 0,
    SYNTAX_HL_COMMENT,
    SYNTAX_HL_KEYWORD1,
    SYNTAX_HL_KEYWORD2,
    SYNTAX_HL_STR,
    SYNTAX_HL_NUM,
    SYNTAX_HL_QUERY
};

struct editorSyntax {
    char *filetype;
    char **filematch;
    char **keywords;
    char *singleline_comment_s;
    int flags;
};

// C
char *syntax_hl_extensions_c[] = {
    ".c",
    ".h",
    ".cpp",
    NULL
};

char *syntax_hl_keywords_c[] = {
    "switch", "if", "while", "for", "break", "continue", "return", "else", "struct", "union", "typedef", "static", "enum", "class", "case", "(", ")"," else if", "{", "}",

    "int|", "long|", "double|", "float|", "char|", "unsigned|", "signed|", "void|", NULL
};

// Python
char *syntax_hl_extensions_py[] = {
    ".py",
    NULL
};

char *syntax_hl_keywords_py[] = {
    "if", "while", "for", "break", "continue", "return", "else:", "elif", "class", "def", "True", "False", "import", "from", "==", "!=", "=", "===", "<", ">", "<=", ">=", "*", "+", "-", "%", "):",
    
    ":|", NULL
};

struct editorSyntax SyntaxDB[] = {
    {
        "c",
        syntax_hl_extensions_c,
        syntax_hl_keywords_c,
        "//",
        HL_NUMBERS | HL_STRINGS
    },
    {
        "py",
        syntax_hl_extensions_py,
        syntax_hl_keywords_py,
        "#",
        HL_NUMBERS | HL_STRINGS
    },
};