/*
 * The following license applies to "mod_sqltemplate" version 0.1. It
 * is a third-party module by Dave Ingram <apache@dmi.me.uk> for the
 * Apache HTTP Server (http://httpd.apache.org/).
 *
 * This license is basically the same as Apache, with the additional
 * provision that any modification must be properly documented.
 *
 * =====================================================================
 * Copyright (c) 2008 David Ingram. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials provided
 *    with the distribution.
 *
 * 3. The end-user documentation included with the redistribution, if
 *    any, must include the following acknowledgment:
 *    "This product includes software developed by David Ingram
 *    <apache@dmi.me.uk> for use in the mod_sqltemplate project
 *    (http://www.dmi.me.uk/code/mod_sqltemplate/)."
 *    Alternately, this acknowledgment may appear in the software
 *    itself, if and wherever such third-party acknowledgments
 *    normally appear.
 *
 * 4. The name "mod_sqltemplate" must not be used to endorse or
 *    promote products derived from this software without prior
 *    written permission. For written permission, please contact Dave
 *    Ingram <apache@dmi.me.uk>.
 *
 * 5. Products derived from this software may not be called
 *    "mod_sqltemplate" nor may "mod_sqltemplate" appear in their
 *    names without prior written permission of Dave Ingram.
 *
 * 6. Any modification must be properly documented and acknowledged by
 *    its author.
 *
 * THIS SOFTWARE IS PROVIDED BY DAVID INGRAM ``AS IS'' AND ANY
 * EXPRESSED OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE APACHE GROUP OR ITS
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 * ====================================================================
 *
 *   This product was inspired by software developed by Fabien Coelho
 *   <mod.macro@coelho.net> for use in the mod_macro project
 *   (http://www.coelho.net/mod_macro/).
 *
 * ====================================================================
 */

#include "httpd.h"
#include "http_config.h"
#include "http_log.h"

#include "apr.h"
#include "apr_strings.h"
#include "apr_portable.h"
#include "apr_file_io.h"


/* a macro: name, arguments, contents, location.
 */
typedef struct {
  char * name;        /* case-insensitive name of the macro. */
  apr_array_header_t * arguments; /* of char* */
  apr_array_header_t * contents;
  char * location;            /* of the macro definition. */
} sqlrpt_t;




/* handles: <SQLRepeat "SQL statement"> any trash...
 */
static const char *sqltemplate_rpt_section(cmd_parms * cmd,
                                 void * dummy,
                                 const char * arg)
{
    const char * errmsg, * where;
    char ** new, * name, * endp;
    macro_t * macro, * old;

    debug(fprintf(stderr, "macro_section: arg='%s'\n", arg));

    macro_init(cmd->temp_pool); /* lazy... */

    endp = ap_strchr_c(arg, '>');
    if (endp == NULL) {
      return apr_pstrcat(cmd->pool, cmd->cmd->name,
                         "> directive missing closing '>'", NULL);
    }

    /* hmmm... drops out '>[^>]*$'
     */
    if (endp) {
        *endp = '\0';
    }

    /* get name. */
    name = ap_getword_conf(cmd->temp_pool, &arg);

    if (empty_string_p(name)) {
        return "macro definition: name not specified";
    }

    old = get_macro_by_name(all_macros, name);
    if (old) {
#if !defined(MOD_MACRO_NO_WARNINGS)
        /* already define: warn about the redefinition. */
        ap_log_error(APLOG_MARK, APLOG_NOERRNO|APLOG_WARNING, 0, NULL,
                     "macro '%s' multiply defined. "
                     "%s, redefined on line %d of %s",
                     old->name, old->location,
                     cmd->config_file->line_number, cmd->config_file->name);
#endif
        macro = old;
    }
    else {
        macro = (macro_t *)apr_palloc(cmd->temp_pool, sizeof(macro_t));
    }

    macro->name = name;
    debug(fprintf(stderr, "macro_section: name=%s\n", name));

    /* get arguments. */
    macro->location = apr_psprintf(cmd->temp_pool,
                                   "defined on line %d of %s",
                                   cmd->config_file->line_number,
                                   cmd->config_file->name);
    debug(fprintf(stderr, "macro_section: location=%s\n", macro->location));

    where = apr_psprintf(cmd->temp_pool, "macro '%s' (%s)",
                        macro->name, macro->location);

#if !defined(MOD_MACRO_NO_CHAR_PREFIX_WARNINGS) || \
    !defined(MOD_MACRO_NO_WARNINGS)
    if (looks_like_an_argument(name)) {
        ap_log_error(APLOG_MARK, APLOG_NOERRNO|APLOG_WARNING, 0, NULL,
                     "%s better prefix a macro name with any of '%s'\n",
                     where, ARG_PREFIX);
    }
#endif

    macro->arguments = get_arguments(cmd->temp_pool, arg);

    errmsg = check_macro_arguments(cmd->temp_pool, macro);

    if (errmsg) {
        return errmsg;
    }

    errmsg = get_lines_till_end_token(cmd->temp_pool, cmd->config_file,
                                      END_MACRO, BEGIN_MACRO,
                                      where, &macro->contents);

    if (errmsg) {
        return apr_psprintf(cmd->temp_pool,
                           "%s\n\tcontents error: %s", where, errmsg);
    }

    errmsg = check_macro_contents(cmd->temp_pool, macro);

    if (errmsg) {
        return apr_psprintf(cmd->temp_pool,
                           "%s\n\tcontents checking error: %s", where, errmsg);
    }

    /* add the new macro. */
    new  = apr_array_push(all_macros);
    *new = (char *)macro;

    return NULL;
}

#define BEGIN_SQLRPT "<SQLRepeat"
#define END_SQLRPT   "</SQLRepeat>"

#define BEGIN_SQLCATSET "<SQLCatSet"
#define END_SQLCATSET   "</SQLCatSet>"

#define BEGIN_SQLIF "<SQLIf"
#define END_SQLIF   "</SQLIf>"

/*
 * Command table
 */
static const command_rec sqltemplate_cmds[] =
{
  /* configuration file macro stuff
   */
  AP_INIT_RAW_ARGS(BEGIN_SQLRPT, sqltemplate_rpt_section, NULL, EXEC_ON_READ | OR_ALL,
                   "Beginning of a SQL repeating template section."),

#if 0
  /* configuration errors and warnings.
   */
  AP_INIT_RAW_ARGS(ERROR_KEYWORD, say_it, (void *)APLOG_ERR, OR_ALL,
                   "Error in a configuration file."),
  AP_INIT_RAW_ARGS(WARNING_KEYWORD, say_it, (void *)APLOG_WARNING, OR_ALL,
                   "Warning in a configuration file."),
#endif

  { NULL }
};

/* Dispatch list for API hooks */
module AP_MODULE_DECLARE_DATA sqltemplate_module = {
  STANDARD20_MODULE_STUFF,
  NULL,                  /* create per-dir    config structures */
  NULL,                  /* merge  per-dir    config structures */
  NULL,                  /* create per-server config structures */
  NULL,                  /* merge  per-server config structures */
  sqltemplate_cmds,      /* table of config file commands       */
  NULL,                  /* register hooks                      */
};

