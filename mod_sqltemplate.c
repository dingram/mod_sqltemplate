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
 *    (http://www.dmi.me.uk/code/apache/mod_sqltemplate/)."
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
    fprintf(stderr, "  \033[1;37m%s\033[0m", ((char **)contents->elts)[i]);
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

#define debug(x) x

/* replace name by replacement at the beginning of buf of bufsize.
   returns an error message or NULL.
*/
static char * substitute(char * buf, int bufsize,
                         const char * name,
                         const char * replacement)
{
    int
      lbuf  = strlen(buf),
      lname = strlen(name),
      lrepl = strlen(replacement),
      lsubs = lrepl,
      shift = lsubs - lname,
      size  = lbuf + shift,
      i, j;

    /* buf must starts with name */
    ap_assert(!strncmp(buf, name, lname));

    /* ??? */
    if (!strcmp(name,replacement)) return NULL;

    debug(fprintf(stderr,
                  "substitute(%s,%s,%s,sh=%d,lbuf=%d,lrepl=%d,lsubs=%d)\n",
                  buf,name,replacement, shift, lbuf, lrepl, lsubs));

    if (size >= bufsize) {
        /* could/should I reallocate? */
        return "cannot substitute, buffer size too small";
    }

    /* shift the end of line */
    if (shift < 0) {
        for (i = lname; i <= lbuf; i++)
            buf[i + shift] = buf[i];
    } else if (shift > 0) {
        for (i = lbuf; i >= lname; i--)
            buf[i + shift] = buf[i];
    }

    /* insert the replacement with escapes */
    j = 0;
    for (i = 0; i < lrepl; i++, j++)
    {
      buf[j] = replacement[i];
    }

    return NULL;
}

/* find first occurence of args in buf.
   in case of conflict, the LONGEST argument is kept. (could be the FIRST?).
   returns the pointer and the whichone found, or NULL.
*/
static char * next_substitution(const char * buf,
                                const apr_array_header_t * args,
                                int * whichone)
{
    int i;
    char * chosen = NULL, * found, ** tab = (char **)args->elts;
    size_t lchosen = 0, lfound;

    for (i = 0; i < args->nelts; i++) {
        found = ap_strstr(buf, tab[i]);
        lfound = strlen(tab[i]);
        if (found && ( i==0 || *(found-1) != '\\' )
                  && (!chosen || found < chosen ||
                      (found == chosen && lchosen < lfound))) {
            chosen = found;
            lchosen = lfound;
            *whichone = i;
        }
    }

    return chosen;
}

/* substitute arguments by replacements in buf of bufsize.
   returns an error message or NULL.
   if used is defined, returns the used macro arguments.
*/
static char * substitute_macro_args(char * buf, int bufsize,
                                    const apr_array_header_t * arguments,
                                    const apr_array_header_t * replacements,
                                    apr_array_header_t * used)
{
    char * ptr = buf, * errmsg,
        ** atab = (char **)arguments->elts,
        ** rtab = (char **)replacements->elts;
    int whichone = -1;

    if (used) {
        ap_assert(used->nalloc >= replacements->nelts);
    }
    debug(fprintf(stderr, "1# %s", buf));

    while ((ptr = next_substitution(ptr, arguments, &whichone))) {
        errmsg = substitute(ptr, buf - ptr + bufsize,
                            atab[whichone], rtab[whichone]);
        if (errmsg) {
            return errmsg;
        }
        ptr += strlen(rtab[whichone]);
        if (used) {
            used->elts[whichone] = 1;
        }
    }
    debug(fprintf(stderr, "2# %s", buf));

    return NULL;
}

/* perform substitutions in a macro contents and
   return the result as a newly allocated array, if result is defined.
   may also return an error message.
   passes used down to substitute_macro_args.
*/
static const char * process_content(apr_pool_t * p,
                                    const apr_array_header_t * contents,
                                    const apr_array_header_t * arguments,
                                    const apr_array_header_t * replacements,
                                    apr_array_header_t * used,
                                    apr_array_header_t ** result)
{
    char ** new, * errmsg, line[MAX_STRING_LEN];
    int i;

    if (result) {
        *result = apr_array_make(p, 1, sizeof(char *));
    }

    for (i = 0; i < contents->nelts; i++) {
        strncpy(line, ((char **)contents->elts)[i], MAX_STRING_LEN - 1);
        errmsg = substitute_macro_args(line, MAX_STRING_LEN,
                                       arguments, replacements, used);
        if (errmsg) {
#if 0
            return apr_psprintf(p, "while processing line %d of macro '%s'"
                               " (%s)%s",
                                i + 1, macro->name, macro->location, errmsg);
#endif
          return errmsg;
        }

        if (result) {
            new = apr_array_push(*result);
            *new = apr_pstrdup(p, line);
        }
    }
    return NULL;
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
  macro = (macro_t *)apr_palloc(cmd->temp_pool, sizeof(macro_t));
#endif
  fprintf(stderr, "sql_repeat query: %s\n", name);

  /* get query arguments. */
  location = apr_psprintf(cmd->temp_pool,
      "line %d of %s",
      cmd->config_file->line_number,
      cmd->config_file->name);
  fprintf(stderr, "sql_repeat location: %s\n", location);

  where = apr_psprintf(cmd->temp_pool, "SQLRepeat \"%s\" (%s)",
      name, location);

  apr_array_header_t * query_arguments = get_arguments(cmd->temp_pool, arg);

  errmsg=NULL;

  fprintf(stderr, "sql_repeat query args: %s\n", arg);

  apr_array_header_t * contents;

  errmsg = get_lines_till_end_token(cmd->temp_pool, cmd->config_file,
      END_SQLRPT, BEGIN_SQLRPT,
      where, &contents);

  if (errmsg) {
    return apr_psprintf(cmd->temp_pool,
        "%s\n\tcontents error: %s", where, errmsg);
  }

  display_contents(contents);

  apr_array_header_t *query_fields, *replacements, *newcontents;
  int i=0;
  query_fields = apr_array_make(cmd->temp_pool, 1, sizeof(char*));
  replacements = apr_array_make(cmd->temp_pool, 1, sizeof(char*));
  {
    char **new;
    new = apr_array_push(query_fields); *new = apr_psprintf(cmd->temp_pool, "${apache_hosts.id}");
    new = apr_array_push(replacements); *new = apr_psprintf(cmd->temp_pool, "1");
    new = apr_array_push(query_fields); *new = apr_psprintf(cmd->temp_pool, "${apache_hosts.hostname}");
    new = apr_array_push(replacements); *new = apr_psprintf(cmd->temp_pool, "test");
    new = apr_array_push(query_fields); *new = apr_psprintf(cmd->temp_pool, "${apache_hosts.htroot}");
    new = apr_array_push(replacements); *new = apr_psprintf(cmd->temp_pool, "example.net/htdocs");
    new = apr_array_push(query_fields); *new = apr_psprintf(cmd->temp_pool, "${domain}");
    new = apr_array_push(replacements); *new = apr_psprintf(cmd->temp_pool, "example.com");
    new = apr_array_push(query_fields); *new = apr_psprintf(cmd->temp_pool, "${apache_host_aliases.hostname}");
    new = apr_array_push(replacements); *new = apr_psprintf(cmd->temp_pool, "*.example.net");
    new = apr_array_push(query_fields); *new = apr_psprintf(cmd->temp_pool, "\\$");
    new = apr_array_push(replacements); *new = apr_psprintf(cmd->temp_pool, "$");
  }

  // while (replacements = fetch_next_row) {
  errmsg = process_content(cmd->temp_pool, contents, query_fields, replacements,
                           NULL, &newcontents);

  if (errmsg) {
      return apr_psprintf(cmd->temp_pool,
                         "%s error while substituting:\n%s",
                         where, errmsg);
  }

  /* fix??? why is it wrong? should I -- the new one? */
  cmd->config_file->line_number++;

  display_contents(newcontents);

  cmd->config_file = make_array_config
      (cmd->temp_pool, newcontents, where, cmd->config_file, &cmd->config_file);
  // }

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

