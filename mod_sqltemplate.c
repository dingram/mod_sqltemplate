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
 *   (http://www.coelho.net/mod_macro/), and uses some code from it.
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
#include "apu.h"
#include "apu_version.h"

#if (APU_MAJOR_VERSION < 1) || (APU_MAJOR_VERSION == 1 && APU_MINOR_VERSION < 3)
#if (APU_HAVE_PGSQL)
#include <libpq-fe.h>
#endif
#if (APU_HAVE_MYSQL)
#include <mysql.h>
#endif
#if (APU_HAVE_SQLITE2)
#include <sqlite.h>
#endif
#if (APU_HAVE_SQLITE3)
#include <sqlite3.h>
#endif
#endif

#ifdef _DEBUG_SQLTPL
#  define debug(l, x) do { if (l <= _DEBUG_SQLTPL) { x; } } while(0)
#else
#  define debug(l, x)
#endif

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

#define BEGIN_SQLSIMPLEIF "<SQLSimpleIf"
#define END_SQLSIMPLEIF   "</SQLSimpleIf>"


#define empty_string_p(p) (!(p) || !*(p))
#define trim(line) while (*(line)==' ' || *(line)=='\t') (line)++


/* the fake apr_dbd_get_name function, courtesy of Bojan Smojver and mod_spin */
#if (APU_MAJOR_VERSION < 1) || (APU_MAJOR_VERSION == 1 && APU_MINOR_VERSION < 3)
static const char *get_name(const apr_dbd_driver_t *driver,apr_pool_t *pool,
                            apr_dbd_results_t *res,int col){
#if (APU_HAVE_PGSQL)
  struct apr_dbd_pgsql_results_t{
    int random;
    PGconn *handle;
    PGresult *res;
    size_t ntuples;
    size_t sz;
    size_t index;
  } *pgres;
#endif
#if (APU_HAVE_MYSQL)
  struct apr_dbd_mysql_results_t{
    int random;
    MYSQL_RES *res;
    MYSQL_STMT *statement;
    MYSQL_BIND *bind;
  } *myres;
#endif
#if (APU_HAVE_SQLITE2)
  struct apr_dbd_sqlite2_results_t{
    int random;
    sqlite *handle;
    char **res;
    size_t ntuples;
    size_t sz;
    size_t index;
  } *s2res;
#endif
#if (APU_HAVE_SQLITE3)
  typedef struct{
    char *name;
    char *value;
    int size;
    int type;
  } apr_dbd_sqlite3_column_t;

  typedef struct{
    apr_dbd_results_t *res;
    apr_dbd_sqlite3_column_t **columns;
    apr_dbd_row_t *next_row;
    int columnCount;
    int rownum;
  } apr_dbd_sqlite3_row_t;

  struct apr_dbd_sqlite3_results_t{
    int random;
    sqlite3 *handle;
    sqlite3_stmt *stmt;
    apr_dbd_sqlite3_row_t *next_row;
    size_t sz;
    int tuples;
    char **col_names;
  } *s3res;
#endif
  const char *dname=apr_dbd_name(driver);

  if(!strcmp(dname,"pgsql")){
#if (APU_HAVE_PGSQL)
    pgres=(struct apr_dbd_pgsql_results_t *)res;

    return (pgres->res?PQfname(pgres->res,col):NULL);
#endif
  } else if(!strcmp(dname,"mysql")){
#if (APU_HAVE_MYSQL)
    myres=(struct apr_dbd_mysql_results_t *)res;

    if((col<0) || (col>=mysql_num_fields(myres->res)))
      return NULL;

    return mysql_fetch_fields(myres->res)[col].name;
#endif
  } else if(!strcmp(dname,"sqlite2")){
#if (APU_HAVE_SQLITE2)
    s2res=(struct apr_dbd_sqlite2_results_t *)res;

    if((col<0) || (col>=s2res->sz))
      return NULL;

    return s2res->res[col];
#endif
  } else if(!strcmp(dname,"sqlite3")){
#if (APU_HAVE_SQLITE3)
    s3res=(struct apr_dbd_sqlite3_results_t *)res;

    if((col<0) || (col>=s3res->sz))
      return NULL;

    return s3res->next_row->columns[col]->name;
#endif
  }

  return apr_psprintf(pool,"column%d",col);
}
#endif

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
  int section_nesting = 1, any_nesting = 1, line_number = 0;

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
        if (any_nesting<0) {
          ap_log_error(APLOG_MARK, APLOG_NOERRNO|APLOG_WARNING,
              0, NULL,
              "bad (negative) nesting on line %d of %s",
              line_number, where);
        }
      }
      else if (!strncmp(first, "<", 1)) {
        any_nesting++;
      }

      if (!strcasecmp(first, end_token)) { /* okay! */
        section_nesting--;
        if (!section_nesting) {
          if (any_nesting) {
            ap_log_error(APLOG_MARK, APLOG_NOERRNO|APLOG_WARNING,
                0, NULL,
                "bad cumulated nesting (%+d) in %s",
                any_nesting, where);
          }
          *plines = lines;
          return NULL;
        }
      }
      else if (begin_token && !strcasecmp(first, begin_token)) {
        section_nesting++;
      }
    }
    /* free first. */
    new  = apr_array_push(lines);
    *new = apr_psprintf(p, "%s\n", line); /* put '\n' back */
  }

  return apr_psprintf(p, "expected token not found: %s", end_token);
}


#ifdef _DEBUG_SQLTPL
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
#endif


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
                         const int targetlen,
                         const char * replacement)
{
    int
      lbuf  = buf         ? strlen(buf        ) : 0,
      lrepl = replacement ? strlen(replacement) : 0,
      shift = lrepl - targetlen,
      size  = lbuf + shift,
      i, j;

    if (!lbuf || !targetlen || !replacement) return NULL;

    debug(4, fprintf(stderr,
                  "substitute(%s,%d,%s,sh=%d,lbuf=%d,lrepl=%d)\n",
                  buf,targetlen,replacement, shift, lbuf, lrepl));

    // TODO: escape double quotes

    if (size >= bufsize) {
        /* could/should I reallocate? */
        return "cannot substitute, buffer size too small";
    }

    /* shift the end of line */
    if (shift < 0) {
        for (i = targetlen; i <= lbuf; i++)
            buf[i + shift] = buf[i];
    } else if (shift > 0) {
        for (i = lbuf; i >= targetlen; i--)
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


/**
 * Find next place for substitution.
 */
/*
 * Algorithm:
 *   do {
 *     target = strstr(buf, "$");
 *   } while (target>buf && *(target-1)=='\\');
 *   if (*(target+1) == '{') {
 *     endbrace = strstr(target+1, '}');
 *     if (!endbrace) {
 *       syntax error;
 *       return NULL;
 *     }
 *     find which tab element this is strcmp()==0 to
 *     if (not in tab) {
 *       warn "Unrecognised variable"
 *       continue;
 *     }
 *     found match, return starting position = target, length = len(match)+3
 *   } elseif (!*(target+1)) {
 *     continue;
 *   } else  {
 *     loop through tab {
 *       if (ap_strstr(target+1, tab[i]) != target+1) {
 *         continue;
 *       }
 *       if (longer than current match) {
 *         save as current match;
 *       }
 *     }
 *   }
 */
static char * find_next_substitution(const char * buf,
                                     const apr_array_header_t * args,
                                     int * replacement_len,
                                     int * whichone,
                                     int lineno,
                                     const char *where)
{
  char *target=NULL;
  char *found=NULL;
  char *chosen=NULL;
  char **tab = (char**)args->elts;

  do {
    target = ap_strstr(found?found:buf, "$");

    if (!target || !*target || !*(target+1)) {
      // no '$', or it's at the end
      return NULL;
    }

    // skip '$'
    found=target+1;

    if (target > buf && *(target-1)=='\\') {
      // convert "\$" to "$"
      *whichone=-1;
      *replacement_len=2;
      return (target-1);

    } else if (*found == '{') {         // something of the form ${foo}
      // skip '{'
      found++;

      // find matching '}'
      char *endbrace = ap_strstr(found, "}");
      if (!endbrace) {
        // syntax error
        ap_log_error(APLOG_MARK, APLOG_NOERRNO|APLOG_WARNING, 0, NULL, "Syntax error: no closing brace on line %d of %s", lineno, where);
        return NULL;
      }

      int i=0;
      // find out which variable it is
      for (i=0; i<args->nelts; i++) {
        if ((strlen(tab[i]) == endbrace - found) && strncmp(found, tab[i], endbrace - found)==0) {
          chosen=found;
          *whichone = i;
          *replacement_len = strlen(tab[i]) + 3; /* 3 = strlen("${}") */
          // there will only ever be one
          break;
        }
      }
    } else {                     // something of the form $foo
      // find the longest match
      int i=0;
      size_t lchosen = 0;
      for (i = 0; i < args->nelts; i++) {
        if (ap_strstr(found, tab[i]) == found) {
          size_t lfound = strlen(tab[i]);
          if (lchosen < lfound) {
            chosen = found;
            lchosen = lfound;
            *whichone = i;
            *replacement_len = lfound + 1; /* 1 = strlen("$"); */
          }
        }
      }
    }

    if (chosen) {
      return target;
    } else {
      // warning: unrecognised variable
      char *varend=target+1;
      int inbrace=0;
      do {
        if (!inbrace && varend==target+1 && *varend=='{') {
          inbrace=1;
        }
        varend++;
      } while (*varend &&
               ((inbrace && *(varend-1) != '}') ||
                (!inbrace && ( (*varend >= 'a' && *varend <= 'z') ||
                               (*varend >= 'A' && *varend <= 'Z') ||
                               (*varend >= '0' && *varend <= '9') ||
                                *varend == '_'
                             )
                )
               )
              );
      char varname[varend-target+1];
      memset(varname, 0, varend-target+1);
      strncpy(varname, target, varend-target);
      ap_log_error(APLOG_MARK, APLOG_NOERRNO|APLOG_WARNING, 0, NULL,
          "Unrecognised variable %s on line %d of %s", varname, lineno, where);
      // try again
    }
  } while (target && *target);
  return NULL;
}



/* substitute arguments by replacements in buf of bufsize.
   returns an error message or NULL.
   if used is defined, returns the used arguments.
*/
static char * substitute_section_args(char * buf, int bufsize,
                                    const apr_array_header_t * arguments,
                                    const apr_array_header_t * replacements,
                                    apr_array_header_t * used,
                                    int lineno,
                                    const char *where)
{
    char * ptr = buf, * errmsg,
        ** rtab = (char **)replacements->elts;
    int whichone = -1;

    if (used) {
        ap_assert(used->nalloc >= replacements->nelts);
    }
    debug(4, fprintf(stderr, "1# %s", buf));

    int len=0;
    while ((ptr = find_next_substitution(ptr, arguments, &len, &whichone, lineno, where))) {
      if (whichone<0) {
        // replace "\$" with "$"
        debug(4, fprintf(stderr, "substitute("));
        debug(4, fprintf(stderr, "ptr:\"%s\", ", ptr));
        debug(4, fprintf(stderr, "buf:%p - ptr:%p + bufsize:%d = %d, ", buf, ptr, bufsize, buf - ptr + bufsize));
        debug(4, fprintf(stderr, "targetlen:%d, ", len));
        debug(4, fprintf(stderr, "\"$\""));
        debug(4, fprintf(stderr, ");\n"));

        errmsg = substitute(ptr, buf - ptr + bufsize, len, "$");
        if (errmsg) {
          return errmsg;
        }
        ptr+=2;
      } else {
        debug(4, fprintf(stderr, "substitute("));
        debug(4, fprintf(stderr, "ptr:\"%s\", ", ptr));
        debug(4, fprintf(stderr, "buf:%p - ptr:%p + bufsize:%d = %d, ", buf, ptr, bufsize, buf - ptr + bufsize));
        debug(4, fprintf(stderr, "targetlen:%d, ", len));
        debug(4, fprintf(stderr, "rtab[whichone:%d]:\"%s\"", whichone, rtab[whichone]));
        debug(4, fprintf(stderr, ");\n"));

        errmsg = substitute(ptr, buf - ptr + bufsize, len, rtab[whichone]);
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
    }
    debug(4, fprintf(stderr, "2# %s", buf));

    return NULL;
}

/* perform substitutions in section contents and
   return the result as a newly allocated array, if result is defined.
   may also return an error message.
   passes used down to substitute_section_args.
*/
static const char * process_content(apr_pool_t * p,
                                    const apr_array_header_t * contents,
                                    const apr_array_header_t * arguments,
                                    const apr_array_header_t * replacements,
                                    apr_array_header_t * used,
                                    apr_array_header_t ** result,
                                    const char *where)
{
    char ** new, * errmsg, line[MAX_STRING_LEN];
    int i;

    if (result) {
      *result = apr_array_make(p, 1, sizeof(char *));
    }

    for (i = 0; i < contents->nelts; i++) {
      debug(4, fprintf(stderr, "Line %d of %d\n", i+1, contents->nelts));
      strncpy(line, ((char **)contents->elts)[i], MAX_STRING_LEN - 1);
      errmsg = substitute_section_args(line, MAX_STRING_LEN, arguments, replacements, used, i+1, where);
      debug(4, fprintf(stderr, "Line %d of %d done\n", i+1, contents->nelts));
      if (errmsg) {
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

  debug(2, fprintf(stderr, "Driver: %p\n", dbinfo->driver));
  debug(2, fprintf(stderr, "Handle: %p\n", dbinfo->handle));

  if (dbinfo->handle) {
    return NULL;
  }

  const char *err;
  debug(3, fprintf(stderr, "Attempting connect with:\n  driver %s\n  params %s\n", dbinfo->driver_name, dbinfo->params));
#if (APU_MAJOR_VERSION < 1) || (APU_MAJOR_VERSION == 1 && APU_MINOR_VERSION < 3)
  apr_status_t rv = apr_dbd_open(dbinfo->driver, pool, dbinfo->params, &dbinfo->handle);
#else
  apr_status_t rv = apr_dbd_open_ex(dbinfo->driver, pool, dbinfo->params, &dbinfo->handle, &err);
#endif
  debug(2, fprintf(stderr, "Connected\n"));
  if (rv != APR_SUCCESS) {
    switch (rv) {
      case APR_EGENERAL:
          ap_log_error(APLOG_MARK, APLOG_ERR, 0, s, "mod_sqltemplate: Can't connect to %s: %s", dbinfo->driver_name, err);
          return apr_psprintf(pool, "mod_sqltemplate: Can't connect to %s", dbinfo->driver_name);
          break;
      default:
          return apr_psprintf(pool, "mod_sqltemplate: mod_sqltemplate not compatible with APR in open");
          break;
    }
  }

  // automatic cleanup
  apr_pool_cleanup_register(pool, dbinfo, sqltpl_db_close, apr_pool_cleanup_null);
  return NULL;
}



/* handles: <SQLSimpleIf "truth value">
*/
static const char *sqltemplate_simpleif_section(cmd_parms * cmd,
    void * dummy,
    const char * arg)
{
  char *endp = ap_strrchr_c(arg, '>');
  char *test_value;
  if (endp == NULL) {
    return apr_pstrcat(cmd->pool, cmd->cmd->name,
        "> directive missing closing '>'", NULL);
  }

  if (endp) {
    *endp = '\0';
  }

  /* get argument. */
  test_value = ap_getword_conf(cmd->temp_pool, &arg);

  trim(arg);
  if (*arg) {
    return "<SQLSimpleIf> only takes at most one argument";
  }

  const char *errmsg = NULL;
  apr_array_header_t * contents=NULL;

  const char *location = apr_psprintf(cmd->temp_pool,
      "line %d of %s",
      cmd->config_file->line_number,
      cmd->config_file->name);

  const char *where = apr_psprintf(cmd->temp_pool, "SQLSimpleIf at %s", location);

  errmsg = get_lines_till_end_token(cmd->temp_pool, cmd->config_file,
      END_SQLSIMPLEIF, BEGIN_SQLSIMPLEIF,
      where, &contents);

  if (errmsg) {
    return apr_psprintf(cmd->temp_pool,
        "%s\n\tcontents error: %s", where, errmsg);
  }

  if (empty_string_p(test_value)) {
    /* treat empty argument as "false" */
    return NULL;
  }

  debug(1, fprintf(stderr, "SQLSimpleIf at %s:\n", location));

  int negate = 0;

  if (*test_value=='!') {
    test_value++;
    negate = !0;
  }

  int do_include = (atoi(test_value)                      != 0 ||
                    apr_strnatcasecmp(test_value, "yes" ) == 0 ||
                    apr_strnatcasecmp(test_value, "on"  ) == 0 ||
                    apr_strnatcasecmp(test_value, "true") == 0
                   );

  if (do_include != negate) {
    debug(1, display_contents(contents));
    cmd->config_file = make_array_config(cmd->temp_pool, contents, where, cmd->config_file, &cmd->config_file);
  } else {
    debug(1, fprintf(stderr, "[ignored]\n"));
  }

  return NULL;
}


/**
 * Check the section command ends with a ">", and consign everything after that
 * to oblivion.
 */
static const char *sqltpl_sec_open_check(cmd_parms *cmd, const char *arg)
{
  char *endp = ap_strrchr_c(arg, '>');

  if (endp == NULL) {
    return apr_pstrcat(cmd->pool, cmd->cmd->name,
        "> directive missing closing '>'", NULL);
  }

  /*   endp =~ s/>[^>]*$//   */
  if (endp) {
    *endp = '\0';
  }

  return NULL;
}


#define could_error(x) do {\
  const char * errmsg = (x);\
  if (errmsg) return errmsg;\
} while (0)

#define could_error_msg(p,m,x) do {\
  const char * errmsg = (x);\
  if (errmsg) return apr_psprintf(p, "%s%s", m, errmsg);\
} while (0)


/**
 * Perform an SQL query, and return column names if requested.
 *
 * @param query The SQL to execute
 * @param args  An APR array of arguments for the query
 * @param pool  An APR memory pool that we can use
 * @param dbinfo Information about the database connection
 * @param res    Address of a pointer to fill with query result; pointer may be NULL on entry.
 * @param col_names If not NULL, this array will be filled with the query's column names
 *
 * @return An error message, or NULL if no error.
 */
static const char *sqltpl_dbquery(char               *query,
                                  apr_array_header_t *args,
                                  apr_pool_t         *pool,
                                  server_rec         *server,
                                  sqltpl_dbinfo_t    *dbinfo,
                                  apr_dbd_results_t **res,
                                  apr_array_header_t *col_names
                                 ) {

#if 0
  apr_dbd_prepared_t *stmt;

  fprintf(stderr, "pre-prepare DBINFO: %p %p %p\n", dbinfo->driver, dbinfo->handle, pool);
  fprintf(stderr, "Preparing query...\n  %s\n", query);
  apr_status_t rv = apr_dbd_prepare(dbinfo->driver, pool, dbinfo->handle, query, NULL, &stmt);
  fprintf(stderr, "Prepared.\n  %s\n", query);
  if (rv) {
    fprintf(stderr, "DBINFO: %p %p %d\n", dbinfo->driver, dbinfo->handle, rv);
    const char *dberrmsg = apr_dbd_error(dbinfo->driver, dbinfo->handle, rv);
    ap_log_error(APLOG_MARK, APLOG_ERR, rv, server,
                 "DBD: failed to prepare SQL statements: %s",
                 (dberrmsg ? dberrmsg : "[???]"));
    return "Failed to prepare SQL statement";
  }
  fprintf(stderr, "post-prepare DBINFO: %p %p\n", dbinfo->driver, dbinfo->handle);
  if (apr_dbd_pselect(dbinfo->driver, pool, dbinfo->handle, res, stmt, 0, args->nelts, (const char**)args->elts) != 0) {
    ap_log_error(APLOG_MARK, APLOG_ERR, 0, server, "Failed to execute query: %s", query);
    apr_pool_destroy(pool);
    return "Failed to execute query";
  }
  fprintf(stderr, "post-select DBINFO: %p %p\n", dbinfo->driver, dbinfo->handle);
#else
  if (apr_dbd_select(dbinfo->driver, pool, dbinfo->handle, res, query, 0) != 0) {
    ap_log_error(APLOG_MARK, APLOG_ERR, 0, server, "Failed to execute query: %s", query);
    return "Failed to execute query";
  }
#endif

  if (col_names) {
    int i=0;
    const char *name;
#if (APU_MAJOR_VERSION > 1) || (APU_MAJOR_VERSION == 1 && APU_MINOR_VERSION >= 3)
    for (name = apr_dbd_get_name(dbinfo->driver, *res, i);
         name != NULL;
         name = apr_dbd_get_name(dbinfo->driver, *res, ++i)) {
      char **new = apr_array_push(col_names); *new = apr_psprintf(pool, "%s", name);
    }
#else
    for (name = get_name(dbinfo->driver, pool, *res, i);
         name != NULL;
         name = get_name(dbinfo->driver, pool, *res, ++i)) {
      char **new = apr_array_push(col_names); *new = apr_psprintf(pool, "%s", name);
    }
#endif
    debug(2, display_array(col_names));
  }

  return NULL;
}


/* handles: <SQLRepeat "SQL statement">
*/
static const char *sqltemplate_rpt_section(cmd_parms * cmd,
    void * dummy,
    const char * arg)
{
  const char * where, * location;
  char * query;

  could_error(sqltpl_sec_open_check(cmd, arg));

  /* get name. */
  query = ap_getword_conf(cmd->temp_pool, &arg);

  if (empty_string_p(query)) {
    return "SQL repeat definition: query not specified";
  }

  /* get query arguments. */
  location = apr_psprintf(cmd->temp_pool, "%s:%d", cmd->config_file->name, cmd->config_file->line_number);
  where    = apr_psprintf(cmd->temp_pool, "SQLRepeat at %s:%d", cmd->config_file->name, cmd->config_file->line_number);

  debug(1, fprintf(stderr, "%s:\n", where));
  debug(2, fprintf(stderr, "Query: %s\n", query));

  apr_array_header_t * query_arguments = get_arguments(cmd->temp_pool, arg);
  apr_array_header_t * contents=NULL;

  could_error(get_lines_till_end_token(cmd->temp_pool, cmd->config_file, END_SQLRPT, BEGIN_SQLRPT, where, &contents));

  debug(2, display_contents(contents));


  // acquire DB connection
  could_error_msg(cmd->temp_pool, "Database error: ", sqltemplate_db_connect(cmd->pool, cmd->server));

  sqltpl_dbinfo_t *dbinfo = get_dbinfo(cmd->pool, cmd->server);
  debug(3, fprintf(stderr, "DBINFO: %p %p\n", dbinfo->driver, dbinfo->handle));
  apr_status_t rv;

  // prepare the query

  debug(3, fprintf(stderr, "Preparing sub-pool...\n  %s\n", query));
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

  do {
    const char *errmsg = sqltpl_dbquery(query, query_arguments, prepared_pool, cmd->server, dbinfo, &res, query_fields);
    if (errmsg) {
      apr_pool_destroy(prepared_pool);
      return errmsg;
    }
  } while (0);

  apr_array_header_t *finalcontents = apr_array_make(cmd->temp_pool, 1, sizeof(char*));

  for (rv = apr_dbd_get_row(dbinfo->driver, prepared_pool, res, &row, -1);
       rv != -1;
       rv = apr_dbd_get_row(dbinfo->driver, prepared_pool, res, &row, -1)) {

    if (rv != 0) {
      ap_log_error(APLOG_MARK, APLOG_ERR, rv, cmd->server, "Error retrieving results from database");
      apr_pool_destroy(prepared_pool);
      return "Error retrieving results";
    }

    debug(3, fprintf(stderr, "Clearing replacements\n"));
#if (APU_MAJOR_VERSION < 1) || (APU_MAJOR_VERSION == 1 && APU_MINOR_VERSION < 3)
    replacements->nelts = 0;
#else
    apr_array_clear(replacements);
#endif

    debug(2, fprintf(stderr, "Fetching entries\n"));
    const char *ent;
    int i;
    for (i=0; i < query_fields->nelts; i++) {
      ent = apr_dbd_get_entry(dbinfo->driver, row, i);
      if (ent) {
        char **new = apr_array_push(replacements); *new = (char *)ent;
      } else {
        char **new = apr_array_push(replacements); *new = apr_pstrdup(cmd->temp_pool, "");
      }
    }

    debug(2, display_array(replacements));

    debug(3, fprintf(stderr, "Before "));
    debug(3, display_contents(contents));

    debug(4, fprintf(stderr, "processing...\n"));

    could_error_msg(cmd->temp_pool, "Error while substituting: ", process_content(prepared_pool, contents, query_fields, replacements, NULL, &newcontents, where));

    debug(3, fprintf(stderr, "After "));
    debug(3, display_contents(newcontents));

    // append to final contents
    apr_array_cat(finalcontents, newcontents);

  }

  if (finalcontents->nelts) {
    debug(1, fprintf(stderr, "Final "));
    debug(1, display_contents(finalcontents));

    /* fix??? why is it wrong? should I -- the new one? */
    cmd->config_file->line_number++;

    cmd->config_file = make_array_config
        (prepared_pool, finalcontents, where, cmd->config_file, &cmd->config_file);
  } else {
    debug(1, fprintf(stderr, "[no query results]\n"));
  }

  return NULL;
}


/* handles: <SQLCatSet "SQL statement">
*/
static const char *sqltemplate_catset_section(cmd_parms * cmd,
    void * dummy,
    const char * arg)
{
  const char *where, *location;
  char *query, *sep;

  could_error(sqltpl_sec_open_check(cmd, arg));

  /* get seperator. */
  sep = ap_getword_conf(cmd->temp_pool, &arg);


  /* get query. */
  query = ap_getword_conf(cmd->temp_pool, &arg);

  if (empty_string_p(query)) {
    return "SQLCatSet definition: query not specified";
  }


  /* get query arguments. */
  location = apr_psprintf(cmd->temp_pool, "%s:%d", cmd->config_file->name, cmd->config_file->line_number);
  where    = apr_psprintf(cmd->temp_pool, "SQLCatSet at %s:%d", cmd->config_file->name, cmd->config_file->line_number);

  debug(1, fprintf(stderr, "%s:\n", where));
  debug(2, fprintf(stderr, "SQLCatSet seperator: \"%s\"\n", sep));
  debug(2, fprintf(stderr, "SQLCatSet query: %s\n", query));

  apr_array_header_t * query_arguments = get_arguments(cmd->temp_pool, arg);
  apr_array_header_t * contents=NULL;

  could_error(get_lines_till_end_token(cmd->temp_pool, cmd->config_file, END_SQLCATSET, BEGIN_SQLCATSET, where, &contents));

  debug(2, display_contents(contents));


  // acquire DB connection
  could_error_msg(cmd->temp_pool, "Database error: ", sqltemplate_db_connect(cmd->pool, cmd->server));

  sqltpl_dbinfo_t *dbinfo = get_dbinfo(cmd->pool, cmd->server);
  debug(3, fprintf(stderr, "DBINFO: %p %p\n", dbinfo->driver, dbinfo->handle));
  apr_status_t rv;

  // prepare the query

  debug(3, fprintf(stderr, "Preparing sub-pool...\n  %s\n", query));
  // set up a sub-pool
  apr_pool_t *prepared_pool;
  rv = apr_pool_create(&prepared_pool, cmd->pool);
  if (rv != APR_SUCCESS) {
    ap_log_error(APLOG_MARK, APLOG_CRIT, rv, cmd->server, "SQLTemplate: Failed to create memory pool");
    return "Memory error";
  }


  apr_array_header_t *query_fields, *replacements, *newcontents;
  query_fields = apr_array_make(prepared_pool, 1, sizeof(char*));

  apr_dbd_results_t *res = NULL;
  apr_dbd_row_t *row = NULL;

  do {
    const char *errmsg = sqltpl_dbquery(query, query_arguments, prepared_pool, cmd->server, dbinfo, &res, query_fields);
    if (errmsg) {
      apr_pool_destroy(prepared_pool);
      return errmsg;
    }
  } while (0);

  // this is going to eat memory. Optimal way: find out which fields are actually used, and just allocate for those
  replacements = apr_array_make(prepared_pool, query_fields->nelts, sizeof(char*));
  int i;
  for (i=0; i<query_fields->nelts; i++) {
    ((char **)replacements->elts)[i] = apr_pstrdup(cmd->temp_pool, "");
  }

  int rowcount=0;

  for (rv = apr_dbd_get_row(dbinfo->driver, prepared_pool, res, &row, -1);
       rv != -1;
       rv = apr_dbd_get_row(dbinfo->driver, prepared_pool, res, &row, -1)) {

    if (rv != 0) {
      ap_log_error(APLOG_MARK, APLOG_ERR, rv, cmd->server, "Error retrieving results from database");
      apr_pool_destroy(prepared_pool);
      return "Error retrieving results";
    }

    debug(3, fprintf(stderr, "Clearing replacements\n"));
#if (APU_MAJOR_VERSION < 1) || (APU_MAJOR_VERSION == 1 && APU_MINOR_VERSION < 3)
    replacements->nelts = 0;
#else
    apr_array_clear(replacements);
#endif

    debug(2, fprintf(stderr, "Fetching entries\n"));
    const char *ent;
    char **rtab=(char**)replacements->elts;
    for (i=0; i < query_fields->nelts; i++) {
      ent = apr_dbd_get_entry(dbinfo->driver, row, i);
      if (!ent) {
        ent = "";
      }
      if (*rtab[i]) {
        debug(3, fprintf(stderr, "Appending \"%s%s\" to set\n", sep, ent));
        rtab[i] = apr_pstrcat(cmd->temp_pool, rtab[i], sep, ent, NULL);
      } else {
        debug(3, fprintf(stderr, "Appending \"%s\" to set\n", ent));
        rtab[i] = apr_pstrcat(cmd->temp_pool, rtab[i], ent, NULL);
      }
    }

    debug(3, display_array(replacements));
    rowcount++;
  }

  debug(2, fprintf(stderr, "Before "));
  debug(2, display_contents(contents));

  debug(3, fprintf(stderr, "Processing...\n"));

  could_error_msg(cmd->temp_pool, "Error while substituting: ", process_content(prepared_pool, contents, query_fields, replacements, NULL, &newcontents, where));

  if (rowcount) {
    debug(1, fprintf(stderr, "Final "));
    debug(1, display_contents(newcontents));

    /* fix??? why is it wrong? should I -- the new one? */
    cmd->config_file->line_number++;

    cmd->config_file = make_array_config
        (prepared_pool, newcontents, where, cmd->config_file, &cmd->config_file);
  } else {
    debug(1, fprintf(stderr, "[no query results]\n"));
  }

  return NULL;
}


static const char *sqltemplate_db_param(cmd_parms *cmd, void *dconf, const char *val)
{
  sqltpl_dbinfo_t *dbinfo = get_dbinfo(cmd->pool, cmd->server);

  switch ((long) cmd->info) {
    case 0:
      dbinfo->driver_name = val;
      switch (apr_dbd_get_driver(cmd->temp_pool, dbinfo->driver_name, &dbinfo->driver)) {
        case APR_ENOTIMPL:
            return apr_psprintf(cmd->temp_pool, "mod_sqltemplate: No driver for %s", dbinfo->driver_name);
        case APR_EDSOOPEN:
            return apr_psprintf(cmd->temp_pool,
#ifdef NETWARE
                                "mod_sqltemplate: Can't load driver dbd%s.nlm -- please ensure that support for %s has been compiled into apr-util.",
#else
                                "mod_sqltemplate: Can't load driver apr_dbd_%s.so -- please ensure that support for %s has been compiled into apr-util.",
#endif
                                dbinfo->driver_name, dbinfo->driver_name);
        case APR_ESYMNOTFOUND:
            return apr_psprintf(cmd->temp_pool,
                                "mod_sqltemplate: Failed to load driver apr_dbd_%s_driver",
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
  AP_INIT_RAW_ARGS(BEGIN_SQLCATSET, sqltemplate_catset_section, NULL, EXEC_ON_READ | OR_ALL,
      "Beginning of a SQL concatenated set template section."),
  AP_INIT_RAW_ARGS(BEGIN_SQLSIMPLEIF, sqltemplate_simpleif_section, NULL, EXEC_ON_READ | OR_ALL,
      "Beginning of a simple conditional-include section."),

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

