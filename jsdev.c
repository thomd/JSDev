/*  jsdev.c
    Douglas Crockford
    2012-01-06

    Public Domain

    JSDev is a simple JavaScript preprocessor. It implements a tiny macro
    language that is written in the form of tagged comments. These comments are
    normally ignored, and will be removed by JSMin. But JSDev will activate
    these comments, replacing them with executable forms that can be used to do
    debugging, testing, logging, or tracing. JSDev scans a source looking for
    and replacing patterns. A pattern is a slashstar comment containing a
    tag and some stuff, and optionally a condition wrapped in parens.
    There must be no space between the slashstar and the <tag>.
*/
        /*<tag> <stuff>*/
        /*<tag>(<condition>) <stuff>*/
/*
    The command line will contain a list of <tag> names, each of which can
    optionally be followed by a colon and <method> name. There must not be
    any spaces around the colon.

    A <tag> may contain any short sequence of ASCII letters, digits,
    underbar (_), dollar ($), and period(.). The active <tag> strings are
    declared in the method line. All <tag>s that are not declared in the
    command line are ignored.

    The <condition> and <stuff> may not include a string or regexp containing
    starslash, or a comment.

    If a <tag> does not have a :<method>, then it will expand into

        {<stuff>}

    Effectively, the outer part of the comment is replaced with braces, turning
    an inert comment into an executable block. If a <condition> was included,
    it will expand into

        if (<condition>) {<stuff>}

    Note that there can be no space between the <tag> and the paren that
    encloses the <condition>. If there is a space, then everything is <stuff>.

    If <tag> was declared with :<method>, then it will expand into

        {<method>(<stuff>);}

    A function call is constructed, invoking the <method>, and using the
    <stuff> as the arguments. If a condition was included, it will expand into

        if (<condition>) {<method>(<stuff>);}

    Also, a method line can contain a comment.

        -comment <comment>

            A string that will be prepended to the output as a comment.

    Sample method line:

        jsdev debug log:console.log alarm:alert -comment "Devel Edition"

    That will enable
*/
        /*debug <stuff>*/
/*
    comments that expand into

        {<stuff>;}

    as well as
*/
        /*log <stuff>*/
/*
    comments that expand into

        {console.log(<stuff>);}

    and
*/
        /*alarm(<condition>) <stuff>*/
/*
    comments that expand into

        if (<condition>) {alert(<stuff>);}

    It will also insert the comment

        // Devel Edition

    at the top of the output file.

    A program is read from stdin, and a modified program is written to stdout.
*/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define FALSE          0
#define MAX_NR_TAGS    100
#define MAX_TAG_LENGTH 80
#define TRUE           1

static int  cr;
static int  line_nr;
static int  nr_tags;
static int  preview = 0;

static char methods[MAX_NR_TAGS][MAX_TAG_LENGTH + 1];
static char tag                 [MAX_TAG_LENGTH + 1];
static char tags   [MAX_NR_TAGS][MAX_TAG_LENGTH + 1];

static void
error(char* message)
{
    fputs("JSDev: ", stderr);
    if (line_nr) {
        fprintf(stderr, "%d. ", line_nr);
    } else {
        fputs("bad method line ", stderr);
    }
    fputs(message, stderr);
    fputs("\r\n", stderr);
    exit(1);
}


static int
is_alphanum(int c)
{
/*
    Return TRUE if the character is a letter, digit, underscore,
    dollar sign, or period.
*/
    return ((c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') ||
            (c >= 'A' && c <= 'Z') ||
             c == '_' || c == '$' || c == '.');
}


static int
emit(int c)
{
/*
    Send a character to stdout.
*/
    if (c > 0 && fputc(c, stdout) == EOF) {
        error("write error.");
    }
    return c;
}


static void
emits(char* s)
{
/*
    Send a string to stdout.
*/
    if (fputs(s, stdout) == EOF) {
        error("write error.");
    }
}


static int
peek()
{
    if (!preview) {
        preview = fgetc(stdin);
    }
    return preview;
}


static int
get(int echo)
{
/*
    Return the next character from the input. If the echo argument is TRUE,
    then the character will also be emitted.
*/
    int c;
    if (preview) {
        c = preview;
        preview = 0;
    } else {
        c = fgetc(stdin);
    }
    if (c <= 0) {
        return EOF;
    } else if (c == '\r') {
        cr = TRUE;
        line_nr += 1;
    } else {
        if (c == '\n' && !cr) {
            line_nr += 1;
        }
        cr = FALSE;
    }
    if (echo) {
        emit(c);
    }
    return c;
}


static void
unget(int c)
{
    preview = c;
}


static void
string(int quote, int in_comment)
{
    int c, was = line_nr;
    for (;;) {
        c = get(TRUE);
        if (c == quote) {
            return;
        }
        if (c == '\\') {
            c = get(TRUE);
        }
        if (in_comment && c == '*' && peek() == '/') {
            error("unexpected close comment in string.");
        }
        if (c == EOF) {
            line_nr = was;
            error("unterminated string literal.");
        }
    }
}


static int
pre_regexp(int left)
{
    return (left == '(' || left == ',' || left == '=' ||
            left == ':' || left == '[' || left == '!' ||
            left == '&' || left == '|' || left == '?' ||
            left == '{' || left == '}' || left == ';');
}


static void
regexp(int in_comment)
{
    int c, was = line_nr;
    for (;;) {
        c = get(TRUE);
        if (c == '[') {
            for (;;) {
                c = get(TRUE);
                if (c == ']') {
                    break;
                }
                if (c == '\\') {
                    c = get(TRUE);
                }
                if (in_comment && c == '*' && peek() == '/') {
                    error("unexpected close comment in regexp.");
                }
                if (c == EOF) {
                    error("unterminated set in Regular Expression literal.");
                }
            }
        } else if (c == '/') {
            if (in_comment && (peek() == '/' || peek() == '*')) {
                error("unexpected comment.");
            }
            return;
        } else if (c == '\\') {
            c = get(TRUE);
        }
        if (in_comment && c == '*' && peek() == '/') {
            error("unexpected comment.");
        }
        if (c == EOF) {
            line_nr = was;
            error("unterminated regexp literal.");
        }
    }
}


static void
condition()
{
    int c, left = '{', paren = 0;
    for (;;) {
        c = get(TRUE);
        if (c == '(' || c == '{' || c == '[') {
            paren += 1;
        } else if (c == ')' || c == '}' || c == ']') {
            paren -= 1;
            if (paren == 0) {
                return;
            }
        } else if (c == EOF) {
            error("Unterminated condition.");
        } else if (c == '\'' || c == '"' || c == '`') {
            string(c, TRUE);
        } else if (c == '/') {
            if (peek() == '/' || peek() == '*') {
                error("unexpected comment.");
            }
            if (pre_regexp(left)) {
                regexp(TRUE);
            }
        } else if (c == '*' && peek() == '/') {
            error("unclosed condition.");
        }
        if (c > ' ') {
            left = c;
        }
    }
}


static void
stuff()
{
    int c, left = '{', paren = 0;
    while (peek() == ' ') {
        get(FALSE);
    }
    for (;;) {
        while (peek() == '*') {
            get(FALSE);
            if (peek() == '/') {
                get(FALSE);
                if (paren > 0) {
                    error("Unbalanced stuff");
                }
                return;
            }
            emit('*');
        }
        c = get(TRUE);
        if (c == EOF) {
            error("Unterminated stuff.");
        } else if (c == '\'' || c == '"' || c == '`') {
            string(c, TRUE);
        } else if (c == '(' || c == '{' || c == '[') {
            paren += 1;
        } else if (c == ')' || c == '}' || c == ']') {
            paren -= 1;
            if (paren < 0) {
                error("Unbalanced stuff");
            }
        } else if (c == '/') {
            if (peek() == '/' || peek() == '*') {
                error("unexpected comment.");
            }
            if (pre_regexp(left)) {
                regexp(TRUE);
            }
        }
        if (c > ' ') {
            left = c;
        }
    }
}


static void
expand(int tag_nr)
{
    int c;

    c = peek();
    if (c == '(') {
        emits("if ");
        condition();
        emit(' ');
    }
    emit('{');
    if (methods[tag_nr][0]) {
        emits(methods[tag_nr]);
        emit('(');
        stuff();
        emits(");}");
    } else {
        stuff();
        emit('}');
    }
}


static int
match()
{
    int tag_nr;

    for (tag_nr = 0; tag_nr < nr_tags; tag_nr += 1) {
        if (strcmp(tag, tags[tag_nr]) == 0) {
            return tag_nr;
        }
    }
    return EOF;
}


static void
process()
{
/*
    Loop through the program text, looking for patterns.
*/
    int c, i, left = 0;
    line_nr = 1;
    c = get(FALSE);
    for (;;) {
        if (c == EOF) {
            break;
        } else if (c == '\'' || c == '"' || c == '`') {
            emit(c);
            string(c, FALSE);
            c = 0;
/*
    The most complicated case is the slash. It can mean division or a regexp
    literal or a line comment or a block comment. A block comment can also be
    a pattern to be expanded.
*/
        } else if (c == '/') {
/*
    A slash slash comment skips to the end of the file.
*/
            if (peek() == '/') {
                emit('/');
                for (;;) {
                    c = get(TRUE);
                    if (c == '\n' || c == '\r' || c == EOF) {
                        break;
                    }
                }
                c = get(FALSE);
/*
    The first component of a slash star comment might be the tag.
*/
            } else {
                if (peek() == '*') {
                    get(FALSE);
                    for (i = 0; i < MAX_TAG_LENGTH; i += 1) {
                        c = get(FALSE);
                        if (!is_alphanum(c)) {
                            break;
                        }
                        tag[i] = (char)c;
                    }
                    tag[i] = 0;
                    unget(c);
/*
    Did the tag match something?
*/
                    i = i == 0 ? -1 : match();
                    if (i >= 0) {
                        expand(i);
                        c = get(FALSE);
                    } else {
/*
    If the tag didn't match, then echo the comment.
*/
                        emits("/*");
                        emits(tag);
                        for (;;) {
                            if (c == EOF) {
                                error("unterminated comment.");
                            }
                            if (c == '/') {
                                c = get(TRUE);
                                if (c == '*' || c == '/') {
                                    error("nested comment.");
                                }
                            } else if (c == '*') {
                                c = get(TRUE);
                                if (c == '/') {
                                    break;
                                }
                            } else {
                                c = get(TRUE);
                            }
                        }
                        c = get(FALSE);
                    }
                } else {
                    emit('/');
/*
    We are looking at a single slash. Is it a division operator, or is it the
    start of a regexp literal? If is not possible to tell for sure without doing
    a complete parse of the program, and we are not going to do that. Instead,
    we are adopting the convention that a regexp literal must have one of a
    small set of characters to its left.
*/
                    if (pre_regexp(left)) {
                        regexp(FALSE);
                    }
                    left = '/';
                    c = get(FALSE);
                }
            }
        } else {
/*
    The character was nothing special, to just echo it.
    If it wasn't whitespace, remember it as the character to the left of the
    next character.
*/
            emit(c);
            if (c > ' ') {
                left = c;
            }
            c = get(FALSE);
        }
    }
}


extern int
main(int argc, char* argv[])
{
    int c = EOF, comment = FALSE, i, j, k;
    cr = FALSE;
    line_nr = 0;
    nr_tags = 0;
    for (i = 1; i < argc; i += 1) {
        if (comment) {
            comment = FALSE;
            emits("// ");
            emits(argv[i]);
            emit('\n');
        } else if (strcmp(argv[i], "-comment") == 0) {
            comment = TRUE;
        } else {
            for (j = 0; j < MAX_TAG_LENGTH; j += 1) {
                c = argv[i][j];
                if (!is_alphanum(c)) {
                    break;
                }
                tags[nr_tags][j] = (char)c;
            }
            if (j == 0) {
                error(argv[i]);
            }
            tags[nr_tags][j] = 0;
            if (c == 0) {
                methods[nr_tags][0] = 0;
            } else if (c == ':') {
                j += 1;
                for (k = 0; k < MAX_TAG_LENGTH; k += 1) {
                    c = argv[i][j + k];
                    if (!is_alphanum(c)) {
                        break;
                    }
                    methods[nr_tags][k] = (char)c;
                }
                methods[nr_tags][k] = 0;
                if (k == 0 || c != 0) {
                    error(argv[i]);
                }
            } else {
                error(argv[i]);
            }
            nr_tags += 1;
        }
    }
    process();
    return 0;
}
