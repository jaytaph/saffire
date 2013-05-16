/*
 Copyright (c) 2012-2013, The Saffire Group
 All rights reserved.

 Redistribution and use in source and binary forms, with or without
 modification, are permitted provided that the following conditions are met:
     * Redistributions of source code must retain the above copyright
       notice, this list of conditions and the following disclaimer.
     * Redistributions in binary form must reproduce the above copyright
       notice, this list of conditions and the following disclaimer in the
       documentation and/or other materials provided with the distribution.
     * Neither the name of the Saffire Group the
       names of its contributors may be used to endorse or promote products
       derived from this software without specific prior written permission.

 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 DISCLAIMED. IN NO EVENT SHALL COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY
 DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <histedit.h>
#include "compiler/saffire_parser.h"
#include "compiler/parser.tab.h"
#include "compiler/parser.h"
#include "compiler/lex.yy.h"
#include "general/config.h"
#include "repl/repl.h"
#include "general/output.h"
#include "version.h"

const char *repl_logo = "   _____        ,__  ,__                \n"
                        "  (        ___  /  ` /  ` ` .___    ___ \n"
                        "   `--.   /   ` |__  |__  | /   \\ .'   `\n"
                        "      |  |    | |    |    | |   ' |----'\n"
                        " \\___.'  `.__/| |    |    / /     `.___,\n"
                        "                /    /                  \n"
                        "\n"
                        saffire_version " interactive/REPL mode. Use CTRL-C to quit.\n";

// Forward define
char *repl_prompt(EditLine *el);

int yyparse (yyscan_t scanner, SaffireParser *saffireParser);


#define DEFAULT_HIST_FILE ".saffire_history"

char *hist_file = DEFAULT_HIST_FILE;


int repl_readline(void *_as, int lineno, char *buf, int max) {
    repl_argstruct_t *as = (repl_argstruct_t *)_as;

    // We need to set the linenumber inside our structure, otherwise prompt() does not know the correct linenumber
    as->lineno = lineno;

    int count;
    const char *line = el_gets(as->el, &count);
    if (count > 0) {
        history(as->hist, &as->ev, H_SAVE, hist_file);
        history(as->hist, &as->ev, H_ENTER, line);
    }

    // Make sure we don't overflow our 'buf'.
    // @TODO: What to do with the remaining characters???
    if (count > max) count = max;

    strncpy(buf, line, count);
    return count;
}



int repl(void) {
    config_init("/etc/saffire/saffire.ini");

    /*
     * Init history and setup repl argument structure
     */
    repl_argstruct_t repl_as;

    repl_as.ps1 = config_get_string("repl.ps1", ">");
    repl_as.ps2 = config_get_string("repl.ps2", "...>");
    repl_as.context = "global";
    repl_as.completeLine = 0;
    repl_as.atStart = 1;
    repl_as.echo = NULL;


    // initialize EditLine library
    repl_as.el = el_init("saffire", stdin, stdout, stderr);
    el_set(repl_as.el, EL_PROMPT, &repl_prompt);
    el_set(repl_as.el, EL_EDITOR, config_get_string("repl.editor", "emacs"));


    // Initialize history
    repl_as.hist = history_init();
    if (! repl_as.hist) {
        fprintf(stderr, "Warning: cannot initialize history\n");
    }
    history(repl_as.hist, &repl_as.ev, H_SETSIZE, config_get_long("repl.history.size", 800));
    el_set(repl_as.el, EL_HIST, history, repl_as.hist);

    // Load history file
    hist_file = config_get_string("repl.history.path", DEFAULT_HIST_FILE);
    history(repl_as.hist, &repl_as.ev, H_LOAD, hist_file);


    /*
     * Init parser structures
     */
    SaffireParser sp;
    yyscan_t scanner;

    // Initialize saffire structure
    sp.mode = SAFFIRE_EXECMODE_REPL;
    sp.filehandle = NULL;
    sp.eof = 0;
    sp.ast = NULL;
    sp.error = NULL;
    sp.yyparse = repl_readline;
    sp.yyparse_args = (void *)&repl_as;

    // Initialize scanner structure and hook the saffire structure as extra info
    yylex_init_extra(&sp, &scanner);

    // We need to link our scanner into the editline, so we can use it inside the repl_prompt() function
    el_set(repl_as.el, EL_CLIENTDATA, scanner);


    if (config_get_bool("repl.logo", 1) == 1) {
        output(repl_logo);
        //printf("Saffire interactive/REPL mode. Use CTRL-C to quit.\n");
    }

    /*
     * Global initialization
     */
    parser_init();

    // Mainloop
    while (! sp.eof) {
        // New 'parse' loop
        repl_as.atStart = 1;
        int status = yyparse(scanner, &sp);
        printf("Returning from yyparse() with status %d\n", status);

        // Did something went wrong?
        if (status) {
            if (sp.error) {
                fprintf(stdout, "Error: %s\n", sp.error);
                free(sp.error);
            }
            continue;
        }

        // Do something with our data

        if (sp.mode == SAFFIRE_EXECMODE_REPL && repl_as.echo != NULL)  {
            printf("repl output: '%s'\n", repl_as.echo);
            free(repl_as.echo);
            repl_as.echo = NULL;
        }
    }

    // Here be generic finalization
    parser_fini();

    // Destroy scanner structure
    yylex_destroy(scanner);

    history_end(repl_as.hist);
    el_end(repl_as.el);

    return 0;
}
