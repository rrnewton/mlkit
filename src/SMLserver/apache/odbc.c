#include "stdlib.h"
#include "stdio.h"
#include "string.h"
#include "sql.h"
#include "../../Runtime/List.h"
#include "../../Runtime/String.h"
#include "DbCommon.h"

#define MAXMSG 1024

enum
{
  DBError = 0, 
  DBData = 1, 
  DBDml = 2,
  DBEod = 3
} DBReturn;

enum
{
  AUTO_COMMIT,
  MANUAL_COMMIT
} COMMIT_MODE;

struct myString
{
  char *cstring;
  unsigned int length;
}

typedef struct 
{
  SQLHENV envhp;
  int dbid;
  char msg[MAXMSG];
  void *freeSessionsGlobal;
  unsigned int number_of_sessions;
  unsigned char about_to_shutdown; // != 0 if we are shutting this environment down
  struct myString DSN;
  struct myString UID;
  struct myString PW;
} oDb_t;

typedef struct oSes
{
  struct oSes *next;
  SQLHDBC connhp;
  SQLHSTMT stmthp;
  SQLSMALLINT cols;
  int needsClosing;
  oDb_t *db;
  COMMIT_MODE mode;
  int *datasizes;
  char *rowp;
  char msg[MAXMSG];
} oSes_t;

typedef struct
{
  char *DNS;
  char *username;
  char *password;
  thread_lock tlock;
  cond_var cvar;
  int maxdepth;
  oDb_t *dbspec;
} db_conf;

typedef struct
{
  void *dbSessions;
  void *freeSessions;
  int theOne;
  int depth;
} dbOraData;


#ifdef MAX
#undef MAX
#endif
#define MAX(a,b) ((a) < (b) ? (b) : (a))

#define ErrorCheck(status,handletype,handle,buffer,code,rd) {                        \
  if (status != SQL_SUCCESS)                                                         \
  {                                                                                  \
    if (putmsg(status, handletype, handle, buffer, MAXMSG, rd)!=SQL_SUCCESS)         \
    {                                                                                \
      code                                                                           \
    }                                                                                \
  }                                                                                  \
}

static SQLRETURN
putmsg(SQLRETURN status, SQLSMALLINT handletype, SQLHANDLE handle, char *msg, int msgLength, void *ctx)/*{{{*/
{
  int i;
  SQLCHAR SQLstate;
  SQLINTEGER naterrptr;
  SQLSMALLINT msgl;
  SQLRETURN stat;
  switch (status)
  {
    case SQL_SUCCESS:
      msg[0] = 0;
      return SQL_SUCCESS;
      break;
    case SQL_SUCCESS_WITH_INFO:
      status = SQL_SUCCESS;
      for (i = 1; status == SQL_SUCCESS || status = SQL_SUCCESS_WITH_INFO; i++)
      {
        msg[0] = 0;
        status = SQLGetDiagRec(handletype, handle, i, &SQLstate, &naterrptr, msg, msgLength - 1, &msgl);
        if (msgl < msgLength)
        {
          msg[msgl] = 0;
          dblog1(ctx, msg);
        }
        else
        {
          dblog1(ctx,"ErrorBuffer too small");
        }
      }
      return SQL_SUCCESS;
      break;
    default:
      stat = status;
      status = SQL_SUCCESS;
      for (i = 1; status == SQL_SUCCESS || status = SQL_SUCCESS_WITH_INFO; i++)
      {
        status = SQLGetDiagRec(handletype, handle, i, &SQLstate, &naterrptr, msg, msgLength - 1, &msgl);
        if (msgl < msgLength)
        {
          msg[msgl] = 0;
          dblog1(ctx, msg);
        }
        else
        {
          dblog1(ctx,"ErrorBuffer too small");
        }
      }
      return stat;
      break;
  }
  return SQL_ERROR;
}/*}}}*/

static oDb_t * 
DBinitConn (void *ctx, char *DSN, char *userid, char *password, int dbid)/*{{{*/
{
  SQLRETURN status;
  oDb_t *db;
  char *ctmp;
  unsigned int dbsize = strlen(DSN) + strlen(userid) + strlen(password) + 3;
  db = (oDb_t *) malloc(sizeof(oDb_t) + dbsize);
  if (!db) 
  {
    dblog1(ctx, "Malloc failed");
    return NULL;
  }
  ctmp = (char *) db;
  ctmp += sizeof(oDb_t);
  db->DNS.cstring = ctmp;
  db->DNS.length = strlen(DNS);
  ctmp += db->DNS->length + 1;
  db->UID.cstring = ctmp;
  db->UID.length = strlen(userid);
  ctmp += db->UID->length + 1;
  db->PW.cstring = ctmp;
  db->PW.length = strlen (password);
  strcpy(db->DNS.cstring, DNS);
  strcpy(db->UID.cstring, userid);
  strcpy(db->PW.cstring, password);

  db->dbid = dbid;
  db->freeSessionsGlobal = NULL;
  db->envhp = NULL;
  db->number_of_sessions = 0;
  db->about_to_shutdown = 0;
  db->msg[0] = 0;
  status = SQLSetEnvAttr(SQL_NULL_HANDLE, SQL_ATTR_CONNECTION_POOLING, SQL_CP_ONE_PER_HENV, 0)
  ErrorCheck(status, SQL_HANDLE_ENV, SQL_NULL_HANDLE, db->msg,
      dblog1(ctx, "Connection pooling setup failed");
      return NULL;,
      ctx
      )
  status = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &db->envhp);
  ErrorCheck(status, SQL_HANDLE_ENV, db->envhp, db->msg,
      dblog1(ctx, "SQLAllocHandle failed");
      return NULL;,
      ctx
      )
  dblog1(ctx, "dbinit2");

  status = SQLSetEnvAttr(db->envhp, SQL_ATTR_ODBC_VERSION, SQL_OV_ODBC3, 0);
  ErrorCheck(status, SQL_HANDLE_ENV, db->envhp, db->msg,
      dblog1(ctx, "ODBC version setup failed");
      return NULL;,
      ctx
      )
  return db;
}/*}}}*/

void
DBCheckNSetIfServerGoneBad(oDb_t *db, SQLRETURN errcode, void *ctx, int lock)/*{{{*/
{
  db_conf *dbc;
  switch (errcode)
  {
    case 28: // your session has been killed
    case 1012: // not logged on
    case 1041: // internal error. hostdef extension doesn't exist
    case 3113: // end-of-file on communication channel
    case 3114: // not connected to ORACLE
    case 12571: // TNS:packet writer failure
    case 24324: // service handle not initialized
      dblog1(ctx, "Database gone bad. ODBC environment about to shutdown");
      dbc = (db_conf *) apsmlGetDBData(db->dbid,ctx);
      if (!dbc) return;
      if (lock) lock_thread(dbc->tlock);
      if (db == dbc->dbspec) dbc->dbspec = NULL;
      db->about_to_shutdown = 1;
      if (lock) unlock_thread(dbc->tlock);
      return;
      break;
    default:
      return;
      break;
  }
  return;
}/*}}}*/

void 
DBShutDown(oDb_t *db, void *ctx)/*{{{*/
{
  sb4 errcode = 0;
  sword status;
  if (!db) return;
  status = OCISessionPoolDestroy(db->poolhp, db->errhp, OCI_SPD_FORCE);
  ErrorCheck(status, OCI_HTYPE_ERROR, db,
      dblog1(ctx, "Closing down the session pool gave an error, but I am still shutting down");,
      ctx
      )
  status = OCIHandleFree(db->poolhp, OCI_HTYPE_SPOOL);
  status = OCIHandleFree(db->errhp, OCI_HTYPE_ERROR);
  status = OCIHandleFree(db->envhp, OCI_HTYPE_ENV);
  return;
}/*}}}*/

void
DBShutDownWconf(void *db2, void *ctx)/*{{{*/
{
  oDb_t *db;
  db_conf *db1 = (db_conf *) db2;
  if (!db1 || !(db1->dbspec)) return;
  db = db1->dbspec;
  db1->dbspec = NULL;
  DBShutDown(db, ctx);
  return;
}/*}}}*/

static oSes_t *
DBgetSession (oDb_t *db, void *rd)/*{{{*/
{
  SQLRETURN status;
  oSes_t *ses;
  if (db == NULL) return NULL;
  ses = (oSes_t *) malloc (sizeof(oSes_t));
  if (!ses) 
  {
    dblog1(rd, "malloc failed");
    return NULL;
  }
  ses->db = db;
  ses->mode = AUTO_COMMIT;
  ses->stmthp = SQL_NULL_HANDLE;
  ses->datasizes = NULL;
  ses->needsClosing = 0;
  ses->rowp = NULL;
  ses->msg[0] = 0;
  ses->connhp = NULL;
  ses->next = NULL;
  status = SQLAllocHandle(SQL_HANDLE_DBC, db->envhp, &ses->connhp);
  ErrorCheck(status, SQL_HANDLE_ENV, db->envhp, db->msg,
      dblog1(rd, "oracleDB: DataBase alloc failed; are we out of memory?");
      free(ses);
      return NULL;,
      rd
      )
  status = SQLSetConnectAttr(ses->connhp, SQL_ATTR_QUIET_MODE, NULL, NULL);
  ErrorCheck(status, SQL_HANDLE_DBC, db->conn, db->msg,
      SQLFreeHandle (SQL_HANDLE_DBC, ses->connhp);
      free(ses);
      return NULL;,
      rd
      )
  status = SQLConnect(ses->connhp, db->DSN.cstring, db->DSN.length,
                                   db->UID.cstring, db->UID.length,
                                   db->PW.cstring, db->PW.length);
  ErrorCheck(status, SQL_HANDLE_DBC, ses->connhp, ses->msg,
      DBCheckNSetIfServerGoneBad(db, status, rd, 0);
      SQLFreeHandle (SQL_HANDLE_DBC, ses->connhp);
      free(ses);
      return NULL;,
      rd
      )
  db->number_of_sessions++;
  return ses;
}/*}}}*/

static void
DBFlushStmt (oSes_t *ses, void *ctx)/*{{{*/
{
  sword status;
  sb4 errcode = 0;
  dvoid *db;
  if (ses == NULL) return;
  db = ses->db;
  if (ses->mode == OCI_DEFAULT)
  {
    ses->mode = COMMIT_ON_SUCCESS;
    status = OCITransRollback(ses->svchp, ses->errhp, OCI_DEFAULT);
    ErrorCheck(status, OCI_HTYPE_ERROR, ses, ;, ctx)
  }
  if (ses->datasizes)
  {
    free(ses->datasizes);
    ses->datasizes = NULL;
  }
  if (ses->rowp)
  {
    free(ses->rowp);
    ses->rowp = NULL;
  }
  if (ses->stmthp != NULL)
  {
    status = OCIStmtRelease(ses->stmthp, ses->errhp, NULL, 0, OCI_STRLS_CACHE_DELETE);
    ses->stmthp = NULL;
  }
  return;
}/*}}}*/

int
DBExecuteSQL (oSes_t *ses, char *sql, void *ctx)/*{{{*/
{
  if (ses == NULL || sql = NULL) return DBError;
  SQLRETURN status;
  status = SQLAllocHandle(SQL_HANDLE_STMT, ses->connhp, &(ses->stmthp)); 
  ErrorCheck(status, SQL_HANDLE_DBC, ses->connhp, db->msg,
      DBCheckNSetIfServerGoneBad(db, status, rd, 0);
      return DBError;,
      rd
      )
  ses->needsClosing = 0;
  status = SQLExecDirect(ses->stmthp, sql, SQL_NTS);
  ErrorCheck(status, SQL_HANDLE_STMT, ses->stmthp, ses->msg,
      DBCheckNSetIfServerGoneBad(ses->db, status, ctx, 1);
      SQLFreeHandle(SQL_HANDLE_STMT, ses->stmthp);
      ses->stmthp = SQL_NULL_HANDLE;
      return DBError;,
      ctx
      )
  ses->needsClosing = 1;
  status SQLNumResultCols(ses->stmthp, &ses->cols);
  ErrorCheck(status, SQL_HANDLE_STMT, ses->stmthp, ses->msg,
      DBCheckNSetIfServerGoneBad(ses->db, errcode, ctx, 1);
      DBFlushStmt(ses,ctx);
      ses->stmthp = SQL_NULL_HANDLE;
      return DBError;,
      ctx
      )
  if (ses->cols > 0) return DBData;
  SQLCloseCursor(ses->stmthp);
  ErrorCheck(status, SQL_HANDLE_STMT, ses->stmthp, ses->msg,
      DBCheckNSetIfServerGoneBad(ses->db, errcode, ctx, 1);
      ses->needsClosing = 0;
      DBFlushStmt(ses,ctx);
      return DBError;,
      ctx
      )
  SQLFreeHandle(SQL_HANDLE_STMT, ses->stmthp);
  ses->stmthp = SQL_NULL_HANDLE;
  return DBDml;
}/*}}}*/

void *
DBGetColumnInfo (oSes_t *ses, void *dump(void *, int, int, char *), void **columnCtx, 
                 void *ctx)/*{{{*/
{
  SQLSMALLINT n, i;
  SQLRETURN status;
  SQLCHAR *colname;
  SQLSMALLINT colnamelength;
  int *datasizes;
  if (ses->stmthp == SQL_NULL_HANDLE) return NULL;
  ses->datasizes = (int *) malloc((n+1) * sizeof (int));
  
  if (ses->datasizes == NULL) return NULL;
  datasizes = ses->datasizes;
  datasizes[0] = ses->cols;
  for (i=1; i <= ses->cols; i++)
  {
    // Get column data
  // SQLColAttribute with SQL_DESC_OCTET_LENGTH will do
    // Get column name
    status = SQLColAttribute(ses->stmthp, i, SQL_DESC_NAME, &colname, 
                             SQL_IS_POINTER, &colnamelength, NULL);
    ErrorCheck(status, SQL_HANDLE_STMT, ses->stmthp, ses->msg,
        DBCheckNSetIfServerGoneBad(ses->db, errcode, ctx, 1);
        DBFlushStmt(ses,ctx);
        return NULL;,
        ctx
        )
    *columnCtx = dump(*columnCtx, i, (int) colnamelength, colname);
    // Get size of data
    status = SQLColAttribute(ses->stmthp, i, SQL_DESC_OCTET_LENGTH, 
                             NULL, NULL, NULL, datasizes+i);
    ErrorCheck(status, SQL_HANDLE_STMT, ses->stmthp, ses->smg,
        DBCheckNSetIfServerGoneBad(ses->db, errcode, ctx, 1);
        DBFlushStmt(ses,ctx);
        return NULL;,
        ctx
        )
  }
  return *columnCtx;
}/*}}}*/

static int
DBGetRow (oSes_t *ses, void *dump(void *, int, char *), void **rowCtx, void *ctx)/*{{{*/
{
  unsigned int i, n;
  SQLRETURN status;
  unsigned int size = 0;
  if (ses->stmthp == NULL) return DBEod;
  n = ses->datasizes[0];
  if (!ses->rowp) 
  {
    for (i=1; i <= n; i++) size += ses->datasizes[i] + 1 + sizeof(SQLLEN);
    ses->rowp = (char *) malloc(size);
    if (!ses->rowp)
    {
      DBFlushStmt(ses, ctx);
      return DBError;
    }
    for (i=1, size = n * sizeof(SQLLEN); i <= n; i++)
    {
      status = SQLBindCol(ses->stmthp, (SQLUSMALLINT) i, SQL_C_CHAR,
                              (SQLPOINTER) ses->rowp+size, 
                              (SQLINTEGER) (ses->datasizes[i] + 1),
                              (SQLLEN *) (ses->rowp + (i * sizeof(SQLLEN))));
      ErrorCheck(status, SQL_HANDLE_STMT, ses->stmthp, ses->msg,
          DBCheckNSetIfServerGoneBad(ses->db, errcode, ctx, 1);
          DBFlushStmt(ses,ctx);
          return DBError;,
          ctx
          )
      size += ses->datasizes[i]+1;
    }
  }
  status = SQLFetch(ses->stmthp);
  if (status == SQL_NO_DATA)
  {
    DBCheckNSetIfServerGoneBad(ses->db, errcode, ctx, 1);
    DBFlushStmt(ses,ctx);
    return DBEod;
  }
  ErrorCheck(status, SQL_HANDLE_STMT, ses->stmthp, ses->msg,
        DBCheckNSetIfServerGoneBad(ses->db, errcode, ctx, 1);
        DBFlushStmt(ses,ctx);
        return DBError;,
        ctx
        )
  for (i=1, size = 0; i < n; i++) size += ses->datasizes[i] + 1 + sizeof(SQLLEN);
  SQLLEN *a;
  for (i=n-1; i >= 0; i--)
  {
    a = (SQLLEN *)(ses->rowp + (i * sizeof(SQLLEN)));
    if (*a != SQL_NULL_DATA && *a > ses->datasizes[i]+1)
    {
      *a = -(*a - (ses->datasizes[i] + 1));
    }
    *rowCtx = dump(*rowCtx, a, ses->rowp+size);
    size -= (ses->datasizes[i] + 1);
  }
  return DBData;
}/*}}}*/

int 
DBTransStart (oSes_t *ses, void *ctx)/*{{{*/
{
  SQLRETURN status;
  if (ses == NULL || ses->mode == MANUAL_COMMIT) return DBError;
  status = SQLSetConnectAttr(ses->connhp, SQL_ATTR_AUTOCOMMIT, SQL_AUTOCOMMIT_OFF, NULL);
  ErrorCheck(status, SQL_HANDLE_DBC, ses->connhp, ses->msg,
      DBCheckNSetIfServerGoneBad(ses->db, status, ctx, 0);
      return DBError;,
      ctx)
  ses->mode = MANUAL_COMMIT;
  return DBDml;
}/*}}}*/

int
DBTransCommit (oSes_t *ses, void *ctx)/*{{{*/
{
  SQLRETURN status;
  if (ses == NULL) return DBError;
  if (ses->mode == AUTO_COMMIT) 
  {
    DBFlushStmt(ses,ctx);
    return DBError;
  }
  ses->mode = AUTO_COMMIT;
  status = SQLSetConnectAttr(ses->connhp, SQL_ATTR_AUTOCOMMIT, SQL_AUTOCOMMIT_ON, NULL);
  ErrorCheck(status, SQL_HANDLE_DBC, ses->connhp, ses->msg,
      DBCheckNSetIfServerGoneBad(ses->db, status, rd, 1);
      DBFlushStmt(ses,ctx);
      return DBError;,
      ctx
      )
  return DBDml;
}/*}}}*/

int 
DBTransRollBack(oSes_t *ses, void *ctx)/*{{{*/
{
  SQLRETURN status;
  if (ses == NULL) return DBError;
  if (ses->mode == AUTO_COMMIT) 
  {
    DBFlushStmt(ses,ctx);
    return DBError;
  }
  status = SQLEndTran(SQL_HANDLE_DBC, ses->connhp, SQL_ROLLBACK);
  ErrorCheck(status, SQL_HANDLE_DBC, ses->connhp, ses->msg,
      DBCheckNSetIfServerGoneBad(ses->db, status, ctx, 1);
      DBFlushStmt(ses,ctx);
      return DBError;,
      ctx
      )
  ses->mode = AUTO_COMMIT;
  status = SQLSetConnectAttr(ses->connhp, SQL_ATTR_AUTOCOMMIT, SQL_AUTOCOMMIT_ON, NULL);
  ErrorCheck(status, SQL_HANDLE_DBC, ses->connhp, ses->msg,
      DBCheckNSetIfServerGoneBad(ses->db, status, rd, 1);
      DBFlushStmt(ses,ctx);
      return DBError;,
      ctx
      )
  return DBDml;
}/*}}}*/

static int
DBReturnSession (oSes_t *ses, void *ctx)/*{{{*/
{
  sword status;
  sb4 errcode = 0;
  dvoid *db;
  OCIError *errhp;
  unsigned char should_we_shutdown;
  unsigned int number_of_sessions;
  ub4 type = 0;
  if (ses == NULL) return DBError;
  db = ses->db;
  if (ses->mode == OCI_DEFAULT)
  { // A transaction is open
    DBFlushStmt(ses,ctx);
  }
  if (ses->stmthp)
  {
    status = OCIAttrGet((dvoid *) ses->stmthp, OCI_HTYPE_STMT, (dvoid *) &type, 
                   NULL, OCI_ATTR_STMT_STATE, ses->errhp);
    switch (type)
    {
      case OCI_STMT_STATE_INITIALIZED:
        status = OCIStmtRelease(ses->stmthp, ses->errhp, NULL, 0, OCI_STRLS_CACHE_DELETE);
        break;
      case OCI_STMT_STATE_EXECUTED:
        status = OCIStmtRelease(ses->stmthp, ses->errhp, NULL, 0, OCI_STRLS_CACHE_DELETE);
        break;
      case OCI_STMT_STATE_END_OF_FETCH:
        status = OCIStmtRelease(ses->stmthp, ses->errhp, NULL, 0, OCI_DEFAULT);
        break;
      default:
        status = OCIStmtRelease(ses->stmthp, ses->errhp, NULL, 0, OCI_STRLS_CACHE_DELETE);
        break;
    }
    ses->stmthp = NULL;
    DBFlushStmt(ses,ctx);
  }
  errhp = ses->errhp;
  status = OCISessionRelease(ses->svchp, errhp, NULL, 0, OCI_DEFAULT);
  ErrorCheck(status, OCI_HTYPE_ERROR, ses, 
      DBCheckNSetIfServerGoneBad(ses->db, errcode, ctx, 0);,
      ctx)
  ses->db->number_of_sessions--;
  should_we_shutdown = ses->db->about_to_shutdown;
  number_of_sessions = ses->db->number_of_sessions;
  status = OCIHandleFree(errhp, OCI_HTYPE_ERROR); // freeing ses
  if (should_we_shutdown && number_of_sessions == 0)
  {
    DBShutDown((oDb_t *) db, ctx);
  }
  return DBEod;
}/*}}}*/

int
apsmlDropSession(oSes_t *ses, void *rd)/*{{{*/
{
  dbOraData *dbdata;
  oSes_t *tmpses, *rses;
  int dbid;
  oDb_t *db;
  if (ses == NULL || rd == NULL) return DBError;
  dbid = ses->db->dbid;
  db = ses->db;
  dbdata = (dbOraData *) getDbData(dbid, rd);
  if (dbdata == NULL) return DBError;
  if (dbdata->dbSessions == ses)
  {
    dbdata->dbSessions = ses->next;
  }
  else
  {
    rses = (oSes_t *) dbdata->dbSessions;
    tmpses = rses;
    while (tmpses != NULL)
    {
      if (tmpses == ses)
      {
        rses->next = tmpses->next;
        break;
      }
      rses = tmpses;
      tmpses = tmpses->next;
    }
  }
  dbdata->depth--;
  db_conf *dbc = (db_conf *) apsmlGetDBData(dbid, rd);
  lock_thread(dbc->tlock);
  if (dbdata->theOne)
  {
    ses->next = NULL;
    tmpses = NULL;
    for (rses = dbdata->freeSessions; rses; rses = rses->next) tmpses = rses;
    if (tmpses)
    {
      tmpses->next = ses;
    }
    else
    {
      dbdata->freeSessions = ses;
    }
    if (!dbdata->dbSessions)
    {
      dbdata->theOne = 0;
      ses = dbdata->freeSessions;
      dbdata->freeSessions = NULL;
      if (db->freeSessionsGlobal)
      {
        while ((rses = db->freeSessionsGlobal))
        {
          db->freeSessionsGlobal = rses->next;
          DBReturnSession(rses, rd);
        }
      }
      db->freeSessionsGlobal = ses;
      i = db->maxdepth;
      while((rses = db->freeSessionsGlobal))
      {
        if (i)
        {
          rses = rses->next;
          i--;
          continue;
        }
        ses = rses;
        rses = rses->next;
        DBReturnSession(ses,rd);
      }
      broadcast_cond(dbc->cvar);
    }
  }
  else
  {
    DBReturnSession(ses,rd);
    broadcast_cond(dbc->cvar);
  }
  unlock_thread(dbc->tlock);
  if (dbdata->dbSessions == NULL) 
  {
    removeDbData(dbid, rd);
    free(dbdata);
  }
  return DBEod;
}/*}}}*/

oSes_t *
apsmlGetSession(int dbid, void *rd)/*{{{*/
{
  oSes_t *ses;
  oDb_t *db;
  int i;
  dbOraData *dbdata = (dbOraData *) getDbData(dbid, rd);
  if (!dbdata) 
  {
    dbdata = (dbOraData *) malloc(sizeof (dbOraData));
    if (!dbdata) return NULL;
    dbdata->freeSessions = NULL;
    dbdata->dbSessions = NULL;
    dbdata->theOne = 0;
    dbdata->depth = 0;
    if (putDbData(dbid, dbdata, rd)) 
    {
      free(dbdata);
      return NULL;
    }
  }
  dblog1(rd, "1");
  if (dbdata->freeSessions)
  {
    dbdata->depth++;
    ses = dbdata->freeSessions;
    dbdata->freeSessions = ses->next;
    return ses;
  }
  dblog1(rd, "2");
  db_conf *dbc = (db_conf *) apsmlGetDBData(dbid,rd);
  if (dbc == NULL)
  {
    dblog1(rd, "Database not configred");
    return NULL;
  }
  if (dbdata->depth >= dbc->maxdepth) 
  {
    return (oSes_t *) 1;
  }
  lock_thread(dbc->tlock);
  if (!dbc->dbspec)
  {
    if (!dbc->DSN || !dbc->username || !dbc->password || 
         dbc->maxdepth < 1)
    {
      unlock_thread(dbc->tlock);
      dblog1(rd, 
           "One or more of DSN, UserName, PassWord and SessionMaxDepth not set");
      return NULL;
    }
    dblog1(rd, "Initializing database connection");
    dbc->dbspec = DBinitConn(rd, dbc->DNS, dbc->username, 
                                    dbc->password, dbid);
    dblog1(rd, "Database initialization call done");
  }
  dblog1(rd, "3");
  if (!dbc->dbspec)
  {
    unlock_thread(dbc->tlock);
    dblog1(rd, "Database did not start");
    return NULL;
  }
  dblog1(rd, "4");
  db = dbc->dbspec;
  if (db->number_of_sessions == 0)
  {
    for (i = 0; i < dbc->maxdepth; i++)
    {
      ses = DBgetSession(dbc->dbspec, rd);
      if (ses == NULL)
      {
        while (dbdata->freeSessions)
        {
          ses = freeSessions->next;
          DBReturnSession(dbdata->freeSessions,rd);
          dbdata->freeSessions = ses;
        }
        dblog1(rd, "Could not get session");
        unlock_thread(dbc->tlock);
        return NULL;
      }
      ses->next = dbdata->freeSessions;
      dbdata->freeSessions = ses;
    }
    dbdata->depth = 1;
    ses = dbdata->freeSessions;
    dbdata->freeSessions = ses->next;
    ses->next = NULL;
    dbdata->dbSessions = ses;
    dbdata->theOne = 1;
    unlock_thread(dbc->tlock);
    return ses;
  }
  else
  {
    if (db->freeSessionsGlobal)
    {
      dbdata->theOne = 1;
      ses = db->freeSessionsGlobal;
      db->freeSessionsGlobal = NULL;
      dbdata->freeSessions = ses->next;
      ses->next = dbdata->dbSessions;
      dbdata->dbSessions = ses;
      dbdata->depth++;
      unlock_thread(dbc->tlock);
      return ses;
    }
    else
    {
      ses = DBgetSession(db,rd);
      if (ses)
      {
        ses->next = dbdata->dbSessions;
        dbdata->dbSessions = ses;
        dbdata->depth++;
        unlock_thread(dbc->tlock);
        return ses;
      }
      else 
      {
        dblog1(rd, "Could not get session");
        wait_cond(dbc->cvar);
        unlock_thread(dbc->tlock);
        return apsmlGetSession(dbid, rd);
      }
    }
  }
  dblog1(rd, "ODBC driver: End of apsmlGetSession reached. This is not suppose to happend");
  unlock_thread(dbc->tlock);
  return NULL;
}/*}}}*/

//void
//apsmlORAChildInit(void *cd1, int num, void *pool, void *server)/*{{{*/
//{
//  db_conf *cd = (db_conf *) cd1;
//  proc_lock_child_init(&(cd->plock), cd->plockname, pool);
//  return;
//}/*}}}*/

void
apsmlDbCleanUpReq(void *rd, void *dbdata1)/*{{{*/
{
  oSes_t *ses;
  dbOraData *dbdata = (dbOraData *) dbdata1;
  if (rd == NULL || dbdata == NULL) return;
  while ((ses = dbdata->dbSessions))
  {
    dbdata->theOne = 0;
    apsmlDropSession(ses, rd);
  }
  return;
}/*}}}*/

void 
apsmlORAChildInit(void *c1, int num, void *pool, void *server)
{
  return;
}

int 
apsmlORASetVal (int i, void *rd, int pos, void *val)/*{{{*/
{
  int id;
  char *sd, *target;
  db_conf *cd;
  dblog1(rd, "apsmlORASetVal");
  cd = (db_conf *) apsmlGetDBData (i,rd);
  if (cd == NULL) 
  {
    cd = (db_conf *) malloc (sizeof (db_conf));
    if (!cd) return 2;
    cd->username = NULL;
    cd->password = NULL;
    cd->TNSname = NULL;
    cd->maxdepth = 0;
    cd->maxsessions = 0;
    cd->minsessions = 0;
    cd->dbspec = NULL;
    if (create_thread_lock(&(cd->tlock), rd))
    {
      free(cd);
      return 2;
    }
    if (create_cond_variable(&(cd->cvar), cd->tlock, rd))
    {
      destroy_thread_lock(cd->tlock);
      free(cd);
      return 2;
    }
    if (apsmlPutDBData (i,(void *) cd, apsmlORAChildInit, DBShutDownWconf, apsmlDbCleanUpReq, rd))
    {
      destroy_thread_lock(cd->tlock);
      free(cd);
      return 2;
    }
    cd = (db_conf *) apsmlGetDBData (i,rd);
  }
  switch (pos)
  {
    case 5:
      id = (int) val;
      cd->maxdepth = id;
      if (cd->maxsessions && cd->maxsessions < cd->maxdepth) return 3;
      break;
    case 6:
      id = (int) val;
      cd->minsessions = id;
      break;
    case 7:
      id = (int) val;
      cd->maxsessions = id;
      if (cd->maxdepth && cd->maxsessions < cd->maxdepth) return 3;
      break;
    case 2:
    case 3:
    case 4:
      sd = (char *) val;
      target = (char *) malloc (strlen (sd)+1);
      if (!target) return 2;
      strcpy(target, sd);
      switch (pos)
      {
        case 2:
          if (cd->username) free(cd->username);
          cd->username = target;
          break;
        case 3:
          if (cd->password) free(cd->password);
          cd->password = target;
          break;
        case 4:
          if (cd->TNSname) free(cd->TNSname);
          cd->TNSname = target;
          break;
      }
      break;
    default:
      return 1;
      break;
  }
  return 0;
}/*}}}*/


typedef struct
{
  Region rListAddr;
  Region rStringAddr;
  Region rAddrEPairs;
  int *list;
} cNames_t;

static void *
dumpCNames (void *ctx1, int pos, int length, char *data)/*{{{*/
{
  String rs;
  int *pair;
  cNames_t *ctx = (cNames_t *) ctx1;
  rs = convertBinStringToML(ctx->rStringAddr, length, data);
  allocRecordML(ctx->rListAddr, 2, pair);
  first(pair) = (int) rs;
  second(pair) = (int) ctx->list;
  makeCONS(pair, ctx->list);
  return ctx;
}/*}}}*/

int
apsmlGetCNames(Region rListAddr, Region rStringAddr, oSes_t *ses, void *rd)/*{{{*/
{
  cNames_t cn1;
  cNames_t *cn = &cn1;
  cn->rListAddr = rListAddr;
  cn->rStringAddr = rStringAddr;
  cn->rAddrEPairs = NULL;
  makeNIL(cn->list);
  if (DBGetColumnInfo(ses, dumpCNames, (void **) &cn, rd) == NULL)
  {
    raise_overflow();
    return (int) cn1.list;
  }
  return (int) cn1.list;
}/*}}}*/

static void *
dumpRows(void *ctx1, STRLEN data1, char *data2)/*{{{*/
{
  String rs;
  sb2 *ivarp;
  int *pair, ivar, *pair2;
  cNames_t *ctx = (cNames_t *) ctx1;
  allocRecordML(ctx->rListAddr, 2, pair);
  allocRecordML(ctx->rAddrEPairs, 2, pair2);
  if (data1 == SQL_NULL_DATA)
  {
    rs = NULL;
    ivar = -1;
  }
  else if (data1 < 0)
  {
    rs = NULL;
    ivar = -data1;
  }
  else 
  {
    rs = convertBinStringToML(ctx->rStringAddr, (int) data1, data2);
    ival = 0;
  }
  first(pair2) = (int) rs;
  second(pair2) = ivar;
  first(pair) = (int) pair2;
  second(pair) = (int) ctx->list;
  makeCONS(pair, ctx->list);
  return ctx;
}/*}}}*/

int
apsmlGetRow(int vAddrPair, Region rAddrLPairs, Region rAddrEPairs, Region rAddrString, 
            oSes_t *ses, void *rd)/*{{{*/
{
  cNames_t cn1;
  int res;
  cNames_t *cn = &cn1;
  cn->rListAddr = rAddrLPairs;
  cn->rStringAddr = rAddrString;
  cn->rAddrEPairs = rAddrEPairs;
  makeNIL(cn->list);
  res = DBGetRow(ses, dumpRows, (void **) &cn, rd);
  first(vAddrPair) = (int) cn1.list;
  second(vAddrPair) = res;
  return vAddrPair;
}/*}}}*/

