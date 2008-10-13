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

#define debug(x)

extern module AP_MODULE_DECLARE_DATA sqltemplate_module;


/* information about DB handles
*/
typedef struct {
  const char *driver_name;
  const char *params;
  const apr_dbd_driver_t *driver;
  apr_dbd_t *handle;
  apr_pool_t *pool;
} sqltpl_dbinfo_t;

#define BEGIN_SQLRPT "<SQLRepeat"
#define END_SQLRPT   "</SQLRepeat>"

#define BEGIN_SQLCATSET "<SQLCatSet"
#define END_SQLCATSET   "</SQLCatSet>"

#define BEGIN_SQLIF "<SQLIf"
#define END_SQLIF   "</SQLIf>"


#define empty_string_p(p) (!(p) || !*(p))
#define trim(line) while (*(line)==' ' || *(line)=='\t') (line)++


static void *get_dbinfo(apr_pool_t *pool, server_rec *s) {
  sqltpl_dbinfo_t *dbinfo = ap_get_module_config(s->module_config, &sqltemplate_module);

  if (!dbinfo) {
    dbinfo=apr_pcalloc(pool, sizeof(sqltpl_dbinfo_t));
    // any other initialisation
    dbinfo->driver_name = "";
    dbinfo->params = "";

    ap_set_module_config(s->module_config, &sqltemplate_module, dbinfo);
  }

  return dbinfo;
}



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


static void display_array(apr_array_header_t * array) {
  int i;

  fprintf(stderr, "Array: \n");

  for (i = 0; i < array->nelts; i++) {
    fprintf(stderr, "  [%03d] = \"\033[1;37m%s\033[0m\"\n", i, ((char **)array->elts)[i]);
  }
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

/* replace name by replacement at the beginning of buf of bufsize.
   returns an error message or NULL.
*/
static char * substitute(char * buf, int bufsize,
                         const char * name,
                         const char * replacement)
{
    int
      lbuf  = buf         ? strlen(buf        ) : 0,
      lname = name        ? strlen(name       ) : 0,
      lrepl = replacement ? strlen(replacement) : 0,
      lsubs = lrepl,
      shift = lsubs - lname,
      size  = lbuf + shift,
      i, j;

    if (!lbuf || !lname || !lrepl) return NULL;

    /* buf must starts with name */
    ap_assert(!strncmp(buf, name, lname));

    /* ??? */
    if (!strcmp(name,replacement)) return NULL;

    debug(fprintf(stderr,
                  "substitute(%s,%s,%s,sh=%d,lbuf=%d,lrepl=%d,lsubs=%d)\n",
                  buf,name,replacement, shift, lbuf, lrepl, lsubs));

    // TODO: escape double quotes

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
      debug(fprintf(stderr, "errmsg = substitute("));
      debug(fprintf(stderr, "ptr:\"%s\", ", ptr));
      debug(fprintf(stderr, "buf:%p - ptr:%p + bufsize:%d = %d, ", buf, ptr, bufsize, buf - ptr + bufsize));
      debug(fprintf(stderr, "atab[whichone:%d]:\"%s\", ", whichone, atab[whichone]));
      debug(fprintf(stderr, "rtab[whichone:%d]:\"%s\"", whichone, rtab[whichone]));
      debug(fprintf(stderr, ");\n"));

        errmsg = substitute(ptr, buf - ptr + bufsize,
                            atab[whichone], rtab[whichone]);
        if (errmsg) {
            return errmsg;
        }
        ptr += strlen(rtab[whichone]);
        if (!*rtab[whichone]) {
          ptr++;
        }
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
      debug(fprintf(stderr, "Line %d of %d\n", i+1, contents->nelts));
      strncpy(line, ((char **)contents->elts)[i], MAX_STRING_LEN - 1);
      errmsg = substitute_macro_args(line, MAX_STRING_LEN,
                                       arguments, replacements, used);
      debug(fprintf(stderr, "Line %d of %d done\n", i+1, contents->nelts));
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


// automatic cleanup function, called on pool destruction
static apr_status_t sqltpl_db_close(void *data) {
  sqltpl_dbinfo_t *dbinfo = data;
  if (!dbinfo || !dbinfo->driver || !dbinfo->handle) {
    // already freed?
    return APR_SUCCESS;
  } else {
    return apr_dbd_close(dbinfo->driver, dbinfo->handle);
  }
}

static const char *sqltemplate_db_connect(apr_pool_t *pool, server_rec *s) {
  sqltpl_dbinfo_t *dbinfo = get_dbinfo(pool, s);
  if (!dbinfo->driver || !dbinfo->params || !*(dbinfo->params) || !dbinfo->driver_name || !*(dbinfo->driver_name)) {
    return "Database connection not set up - please use SQLTemplateDBDriver and SQLTemplateDBParams";
  }

  debug(fprintf(stderr, "Driver: %p\n", dbinfo->driver));
  debug(fprintf(stderr, "Handle: %p\n", dbinfo->handle));

  if (dbinfo->handle) {
    return NULL;
  }

  const char *err;
  debug(fprintf(stderr, "Attempting connect with:\n  driver %s\n  params %s\n", dbinfo->driver_name, dbinfo->params));
  apr_status_t rv = apr_dbd_open_ex(dbinfo->driver, pool, dbinfo->params, &dbinfo->handle, &err);
  debug(fprintf(stderr, "Done\n"));
  if (rv != APR_SUCCESS) {
    switch (rv) {
      case APR_EGENERAL:
          ap_log_error(APLOG_MARK, APLOG_ERR, 0, s, "mod_sqltemplate: Can't connect to %s: %s", dbinfo->driver_name, err);
          return apr_psprintf(pool, "DBD: Can't connect to %s", dbinfo->driver_name);
          break;
      default:
          return apr_psprintf(pool, "DBD: mod_dbd not compatible with APR in open");
          break;
    }
  }

  // automatic cleanup
  apr_pool_cleanup_register(pool, dbinfo, sqltpl_db_close, apr_pool_cleanup_null);
  return NULL;
}




/* handles: <SQLRepeat "SQL statement">
*/
static const char *sqltemplate_rpt_section(cmd_parms * cmd,
    void * dummy,
    const char * arg)
{
  const char * errmsg, * where, * location;
  char ** new, * query, * endp;
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
  query = ap_getword_conf(cmd->temp_pool, &arg);

  if (empty_string_p(query)) {
    return "SQL repeat definition: query not specified";
  }

#if 0
  macro = (macro_t *)apr_palloc(cmd->temp_pool, sizeof(macro_t));
#endif
  debug(fprintf(stderr, "sql_repeat query: %s\n", query));

  /* get query arguments. */
  location = apr_psprintf(cmd->temp_pool,
      "line %d of %s",
      cmd->config_file->line_number,
      cmd->config_file->name);
  //fprintf(stderr, "sql_repeat location: %s\n", location);

  //where = apr_psprintf(cmd->temp_pool, "SQLRepeat \"%s\" (%s)", name, location);
  where = apr_psprintf(cmd->temp_pool, "SQLRepeat at %s", location);

  apr_array_header_t * query_arguments = get_arguments(cmd->temp_pool, arg);

  errmsg=NULL;

  int i=0;

  for (i=0; i < query_arguments->nelts; i++) {
    debug(fprintf(stderr, "sql_repeat query arg: %s\n", ((char **)query_arguments->elts)[i]));
  }

  apr_array_header_t * contents;

  errmsg = get_lines_till_end_token(cmd->temp_pool, cmd->config_file,
      END_SQLRPT, BEGIN_SQLRPT,
      where, &contents);

  if (errmsg) {
    return apr_psprintf(cmd->temp_pool,
        "%s\n\tcontents error: %s", where, errmsg);
  }

  debug(display_contents(contents));


  // acquire DB connection
  errmsg = sqltemplate_db_connect(cmd->temp_pool, cmd->server);

  if (errmsg) {
    return apr_psprintf(cmd->temp_pool, "%s: Database error: %s", where, errmsg);
  }

  sqltpl_dbinfo_t *dbinfo = get_dbinfo(cmd->temp_pool, cmd->server);
  debug(fprintf(stderr, "DBINFO: %p %p\n", dbinfo->driver, dbinfo->handle));
  apr_status_t rv;
  apr_dbd_prepared_t *stmt;

  // prepare the query

  debug(fprintf(stderr, "Preparing sub-pool...\n  %s\n", query));
  // set up a sub-pool
  apr_pool_t *prepared_pool;
  rv = apr_pool_create(&prepared_pool, cmd->pool);
  if (rv != APR_SUCCESS) {
    ap_log_error(APLOG_MARK, APLOG_CRIT, rv, cmd->server, "SQLTemplate: Failed to create memory pool");
    return "Memory error";
  }


  apr_array_header_t *query_fields, *replacements, *newcontents;
  query_fields = apr_array_make(prepared_pool, 1, sizeof(char*));
  replacements = apr_array_make(prepared_pool, 1, sizeof(char*));

  apr_dbd_results_t *res = NULL;
  apr_dbd_row_t *row = NULL;


#if 0
  fprintf(stderr, "pre-prepare DBINFO: %p %p %p\n", dbinfo->driver, dbinfo->handle, prepared_pool);
  fprintf(stderr, "Preparing query...\n  %s\n", query);
  rv = apr_dbd_prepare(dbinfo->driver, prepared_pool, dbinfo->handle, query, NULL, &stmt);
  fprintf(stderr, "Prepared.\n  %s\n", query);
  if (rv) {
    fprintf(stderr, "DBINFO: %p %p %d\n", dbinfo->driver, dbinfo->handle, rv);
    const char *dberrmsg = apr_dbd_error(dbinfo->driver, dbinfo->handle, rv);
    ap_log_error(APLOG_MARK, APLOG_ERR, rv, cmd->server,
                 "DBD: failed to prepare SQL statements: %s",
                 (dberrmsg ? dberrmsg : "[???]"));
    return "Failed to prepare SQL statement";
  }
  fprintf(stderr, "post-prepare DBINFO: %p %p\n", dbinfo->driver, dbinfo->handle);
  if (apr_dbd_pselect(dbinfo->driver, prepared_pool, dbinfo->handle, &res, stmt, 0, query_arguments->nelts, (const char**)query_arguments->elts) != 0) {
    ap_log_error(APLOG_MARK, APLOG_ERR, 0, cmd->server, "Failed to execute query: %s", query);
    apr_pool_destroy(prepared_pool);
    return "Failed to execute query";
  }
  fprintf(stderr, "post-select DBINFO: %p %p\n", dbinfo->driver, dbinfo->handle);
#else
  if (apr_dbd_select(dbinfo->driver, prepared_pool, dbinfo->handle, &res, query, 0) != 0) {
    ap_log_error(APLOG_MARK, APLOG_ERR, 0, cmd->server, "Failed to execute query: %s", query);
    apr_pool_destroy(prepared_pool);
    return "Failed to execute query";
  }
#endif

  i = 0;
  const char *name;
  for (name = apr_dbd_get_name(dbinfo->driver, res, i);
       name != NULL;
       name = apr_dbd_get_name(dbinfo->driver, res, ++i)) {
    new = apr_array_push(query_fields); *new = apr_psprintf(prepared_pool, "${%s}", name);
  }
  new = apr_array_push(query_fields); *new = "\\$";
  debug(display_array(query_fields));

  // while (replacements = fetch_next_row) {
  for (rv = apr_dbd_get_row(dbinfo->driver, prepared_pool, res, &row, -1);
       rv != -1;
       rv = apr_dbd_get_row(dbinfo->driver, prepared_pool, res, &row, -1)) {

    if (rv != 0) {
      ap_log_error(APLOG_MARK, APLOG_ERR, rv, cmd->server, "Error retrieving results from database");
      apr_pool_destroy(prepared_pool);
      return "Error retrieving results";
    }

    debug(fprintf(stderr, "Clearing replacements\n"));
    apr_array_clear(replacements);

    debug(fprintf(stderr, "Fetching entries\n"));
    const char *ent;
    for (i=0; i < query_fields->nelts - 1; i++) {
      ent = apr_dbd_get_entry(dbinfo->driver, row, i);
      if (ent) {
        new = apr_array_push(replacements); *new = (char *)ent;
      }
    }

    // add special replacement: \$ --> $
    new = apr_array_push(replacements); *new = apr_psprintf(prepared_pool, "$");
    debug(display_array(replacements));

    debug(fprintf(stderr, "Before "));
    debug(display_contents(contents));

    debug(fprintf(stderr, "processing...\n"));

    errmsg = process_content(prepared_pool, contents, query_fields, replacements,
                             NULL, &newcontents);

    if (errmsg) {
      return apr_psprintf(prepared_pool,
                         "%s error while substituting:\n%s",
                         where, errmsg);
    }

    /* fix??? why is it wrong? should I -- the new one? */
    cmd->config_file->line_number++;

    debug(fprintf(stderr, "After "));
    display_contents(newcontents);

    cmd->config_file = make_array_config
        (prepared_pool, newcontents, where, cmd->config_file, &cmd->config_file);
  }

  return NULL;
}


static const char *sqltemplate_db_param(cmd_parms *cmd, void *dconf, const char *val)
{
  const apr_dbd_driver_t *driver = NULL;
  sqltpl_dbinfo_t *dbinfo = get_dbinfo(cmd->temp_pool, cmd->server);

  switch ((long) cmd->info) {
    case 0:
      dbinfo->driver_name = val;
      switch (apr_dbd_get_driver(cmd->temp_pool, dbinfo->driver_name, &dbinfo->driver)) {
      case APR_ENOTIMPL:
          return apr_psprintf(cmd->temp_pool, "DBD: No driver for %s", dbinfo->driver_name);
      case APR_EDSOOPEN:
          return apr_psprintf(cmd->temp_pool,
#ifdef NETWARE
                              "DBD: Can't load driver file dbd%s.nlm",
#else
                              "DBD: Can't load driver file apr_dbd_%s.so",
#endif
                              dbinfo->driver_name);
      case APR_ESYMNOTFOUND:
          return apr_psprintf(cmd->temp_pool,
                              "DBD: Failed to load driver apr_dbd_%s_driver",
                              dbinfo->driver_name);
      }
      break;
    case 1:
      dbinfo->params = val;
      break;
  }

  return NULL;
}


/*
 * Command table
 */
static const command_rec sqltemplate_cmds[] =
{
  AP_INIT_TAKE1("SQLTemplateDBDriver", sqltemplate_db_param, (void*)0, EXEC_ON_READ | OR_ALL,
      "DBD driver to use"),
  AP_INIT_TAKE1("SQLTemplateDBParams", sqltemplate_db_param, (void*)1, EXEC_ON_READ | OR_ALL,
      "DBD driver parameters"),
  AP_INIT_RAW_ARGS(BEGIN_SQLRPT, sqltemplate_rpt_section, NULL, EXEC_ON_READ | OR_ALL,
      "Beginning of a SQL repeating template section."),

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

