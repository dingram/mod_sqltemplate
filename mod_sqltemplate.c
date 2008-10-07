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
#include "apr_dbd.h"
#include "apr_portable.h"
#include "apr_file_io.h"

#include "mod_dbd.h"


/* a repeat section: name, arguments, contents, location.
*/
typedef struct {
  char * query;        /* The SQL query to execute */
  apr_array_header_t * arguments; /* of char* */
  apr_array_header_t * contents;
  char * location;            /* of the definition. */
} sqlrpt_t;

#define BEGIN_SQLRPT "<SQLRepeat"
#define END_SQLRPT   "</SQLRepeat>"

#define BEGIN_SQLCATSET "<SQLCatSet"
#define END_SQLCATSET   "</SQLCatSet>"

#define BEGIN_SQLIF "<SQLIf"
#define END_SQLIF   "</SQLIf>"


#define empty_string_p(p) (!(p) || !*(p))
#define trim(line) while (*(line)==' ' || *(line)=='\t') (line)++

/* optional function - look it up once in post_config */
static ap_dbd_t *(*mod_sqltemplate_acquire_fn)(request_rec*) = NULL;
static void (*mod_sqltemplate_prepare_fn)(server_rec*, const char*, const char*) = NULL;


static apr_array_header_t * get_arguments(apr_pool_t * p, const char * line)
{
  apr_array_header_t * args = apr_array_make(p, 1, sizeof(char *));
  char * arg, ** new;

  trim(line);
  while (*line) {
    arg = ap_getword_conf(p, &line);
    new = apr_array_push(args);
    *new = arg;
    trim(line);
  }

  return args;
}

/* get read lines as an array till end_token.
   counts nesting for begin_token/end_token.
   it assumes a line-per-line configuration (thru getline).
   this function could be exported.
   begin_token may be NULL.
   */
static char * get_lines_till_end_token(apr_pool_t * p,
    ap_configfile_t * config_file,
    const char * end_token,
    const char * begin_token,
    const char * where,
    apr_array_header_t ** plines)
{
  apr_array_header_t * lines = apr_array_make(p, 1, sizeof(char *));
  char ** new, * first, * ptr;
  char line[MAX_STRING_LEN]; /* sorry, but that is expected by getline. */
  int macro_nesting = 1, any_nesting = 1, line_number = 0;

  while (!ap_cfg_getline(line, MAX_STRING_LEN, config_file)) {
    ptr = line;
    /* first char? or first non blank? */
    if (*line=='#') continue;
    first = ap_getword_conf_nc(p, &ptr);
    line_number++;
    if (first) {
      /* nesting... */
      if (!strncmp(first, "</", 2)) {
        any_nesting--;
#if !defined(MOD_MACRO_NO_WARNINGS)
        if (any_nesting<0) {
          ap_log_error(APLOG_MARK, APLOG_NOERRNO|APLOG_WARNING,
              0, NULL,
              "bad (negative) nesting on line %d of %s",
              line_number, where);
        }
#endif
      }
      else if (!strncmp(first, "<", 1)) {
        any_nesting++;
      }

      if (!strcasecmp(first, end_token)) { /* okay! */
        macro_nesting--;
        if (!macro_nesting) {
#if !defined(MOD_MACRO_NO_WARNINGS)
          if (any_nesting) {
            ap_log_error(APLOG_MARK, APLOG_NOERRNO|APLOG_WARNING,
                0, NULL,
                "bad cumulated nesting (%+d) in %s",
                any_nesting, where);
          }
#endif
          *plines = lines;
          return NULL;
        }
      }
      else if (begin_token && !strcasecmp(first, begin_token)) {
        macro_nesting++;
      }
    }
    /* free first. */
    new  = apr_array_push(lines);
    *new = apr_psprintf(p, "%s\n", line); /* put '\n' back */
  }

  return apr_psprintf(p, "expected token not found: %s", end_token);
}


static void display_contents(apr_array_header_t * contents) {
  int i;

  fprintf(stderr, "Contents: \n");

  for (i = 0; i < contents->nelts; i++) {
    printf("  %s", ((char **)contents->elts)[i]);
  }
}



typedef struct {
  int index;                    /* current element. */
  int char_index;               /* current char in element. */
  int length;                   /* cached length of the current line. */
  apr_array_header_t * contents;/* array of char * */
  ap_configfile_t * next;       /* next config once this one is processed. */
  ap_configfile_t ** upper;     /* hack: where to update it if needed. */
} array_contents_t;

/* next config if any. */
static int next_one(array_contents_t * ml)
{
  if (ml->next) {
    ap_assert(ml->upper);
    *(ml->upper) = ml->next;
    return 1;
  }
  return 0;
}

/* returns next char or -1.
*/
static int array_getch(void * param)
{
  array_contents_t * ml = (array_contents_t *)param;
  char ** tab = (char **)ml->contents->elts;

  while (ml->char_index >= ml->length) { /* next element */
    if (ml->index >= ml->contents->nelts) {
      /* maybe update. */
      if (ml->next && ml->next->getch && next_one(ml)) {
        return ml->next->getch(ml->next->param);
      }
      return -1;
    }
    ml->index++;
    ml->char_index = 0;
    ml->length = ml->index >= ml->contents->nelts
      ? 0 : strlen(tab[ml->index]);
  }

  return tab[ml->index][ml->char_index++];
}

/* returns a buf a la fgets.
   no more than a line at a time, otherwise the parsing is too much ahead...
   NULL at EOF.
   */
static void * array_getstr(void * buf, size_t bufsize, void * param)
{
  array_contents_t * ml = (array_contents_t *)param;
  char * buffer = (char *) buf;
  size_t i = 0;
  int next = 0;

  while (i < bufsize - 1 && next != '\n'
      && ((next = array_getch(param)) != -1)) {
    buffer[i++] = (char)next;
  }

  if (next == -1 && i == 0) { /* EOF */
    /* maybe update to next. */
    if (next_one(ml)) {
      ap_assert(ml->next->getstr);
      return ml->next->getstr(buf, bufsize, ml->next->param);
    }
    return NULL;
  }

  buffer[i] = '\0';
  return buf;
}

/* close the array stream?
*/
static int array_close(void * param)
{
  array_contents_t * ml = (array_contents_t *)param;
  ml->index = ml->contents->nelts;
  ml->char_index = ml->length;
  return 0;
}

/* this one could be exported.
*/
static ap_configfile_t * make_array_config(apr_pool_t * p,
    apr_array_header_t * contents,
    const char * where,
    ap_configfile_t * cfg,
    ap_configfile_t ** upper)
{
  array_contents_t * ls =
    (array_contents_t *)apr_palloc(p, sizeof(array_contents_t));

  ls->index      = 0;
  ls->char_index = 0;
  ls->contents   = contents;
  ls->length     = ls->contents->nelts < 1
    ? 0 : strlen(((char **)ls->contents->elts)[0]);
  ls->next       = cfg;
  ls->upper      = upper;

  return ap_pcfg_open_custom(p, where, (void *)ls,
      array_getch, array_getstr, array_close);
}





/* handles: <SQLRepeat "SQL statement"> any trash...
*/
static const char *sqltemplate_rpt_section(cmd_parms * cmd,
    void * dummy,
    const char * arg)
{
  const char * errmsg, * where, * location;
  char ** new, * name, * endp;
  //macro_t * macro, * old;

  fprintf(stderr, "sql_repeat_section: arg='%s'\n", arg);

  //macro_init(cmd->temp_pool); /* lazy... */

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
    return "SQL repeat definition: query not specified";
  }

#if 0
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
#endif
  fprintf(stderr, "sql_repeat_section: query: %s\n", name);

  /* get arguments. */
  location = apr_psprintf(cmd->temp_pool,
      "line %d of %s",
      cmd->config_file->line_number,
      cmd->config_file->name);
  fprintf(stderr, "sql_repeat_section: location=%s\n", location);

  where = apr_psprintf(cmd->temp_pool, "SQLRepeat \"%s\" (%s)",
      name, location);

#if 0
#if !defined(MOD_MACRO_NO_CHAR_PREFIX_WARNINGS) || \
  !defined(MOD_MACRO_NO_WARNINGS)
  if (looks_like_an_argument(name)) {
    ap_log_error(APLOG_MARK, APLOG_NOERRNO|APLOG_WARNING, 0, NULL,
        "%s better prefix a macro name with any of '%s'\n",
        where, ARG_PREFIX);
  }
#endif
#endif

  apr_array_header_t * arguments = get_arguments(cmd->temp_pool, arg);

  //errmsg = check_macro_arguments(cmd->temp_pool, macro);
  errmsg=NULL;

  if (errmsg) {
    fprintf(stderr, "error: %s\n", errmsg);
    return errmsg;
  }

  fprintf(stderr, "SQL_repeat prepared query args: %s\n", arg);

  apr_array_header_t * contents;

  errmsg = get_lines_till_end_token(cmd->temp_pool, cmd->config_file,
      END_SQLRPT, BEGIN_SQLRPT,
      where, &contents);

  if (errmsg) {
    return apr_psprintf(cmd->temp_pool,
        "%s\n\tcontents error: %s", where, errmsg);
  }

  display_contents(contents);

  //errmsg = check_macro_contents(cmd->temp_pool, macro);

  if (errmsg) {
    return apr_psprintf(cmd->temp_pool,
        "%s\n\tcontents checking error: %s", where, errmsg);
  }

  /* add the new macro. */
  //new  = apr_array_push(all_macros);
  //*new = (char *)macro;

  return NULL;
}

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

/*
  if (mod_sqltemplate_prepare_fn == NULL) {
  mod_sqltemplate_prepare_fn = APR_RETRIEVE_OPTIONAL_FN(ap_dbd_prepare);
  if (mod_sqltemplate_prepare_fn == NULL) {
  return "You must load mod_dbd to enable AuthDBD functions";
  }
  mod_sqltemplate_acquire_fn = APR_RETRIEVE_OPTIONAL_FN(ap_dbd_acquire);
  }
  label = apr_psprintf(cmd->pool, "mod_sqltemplate_%d", ++label_num);
 */

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

