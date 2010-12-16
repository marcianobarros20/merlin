#define _GNU_SOURCE 1 /* for vasprintf */

#include "db_wrap.h"
#include <assert.h>

#include <stdio.h> /*printf()*/
#include <stdlib.h> /*getenv(), atexit()*/
#include <string.h> /* strlen() */
#include <inttypes.h> /* PRIuXX macros */
#include <stdbool.h>
#define MARKER printf("MARKER: %s:%d:%s():\t",__FILE__,__LINE__,__func__); printf
#define FIXME(X) MARKER("FIXME: " X)

struct {
	db_wrap_conn_params mysql;
	db_wrap_conn_params sqlite3;
	db_wrap_conn_params oracle;
} ConnParams = {
db_wrap_conn_params_empty_m,
db_wrap_conn_params_empty_m,
db_wrap_conn_params_empty_m
};


static struct {
	bool useTempTables;
	bool testMySQL;
	bool testSQLite3;
	bool testOracle;
} ThisApp = {
true/*useTempTables*/,
false/*testMySQL*/,
false/*testSQLite3*/,
false/*testOracle*/
};

static void show_errinfo_impl(db_wrap * wr, int rc, unsigned int line)
{
	if (0 != rc)
	{
		char const * errStr = NULL;
		int dbErrCode = 0;
		wr->api->error_info(wr, &errStr, NULL, &dbErrCode);
		MARKER("line #%u, DB driver error info: db_wrap rc=%d, back-end error code=%d [%s]\n",
			   line, rc, dbErrCode, errStr);
	}
}
#define show_errinfo(WR,RC) show_errinfo_impl(WR, RC, __LINE__)

void test_libdbi_generic(char const * driver, db_wrap * wr)
{
	MARKER("Running generic tests: [%s]\n",driver);
#define TABLE_DEF \
	"table t(vint integer, vdbl float(12), vstr varchar(32))"
	char const * sql = NULL;
	if (ThisApp.useTempTables)
	{
		sql = (0==strcmp(driver,"ocilib"))
			/* and the incompatibilities are already showing up! */
			? ("create global temporary " TABLE_DEF)
			: ("create temporary " TABLE_DEF)
			;
	}
	else
	{
		sql = "create " TABLE_DEF;
		;
	}
#undef TABLE_DEF
	assert(NULL != sql);
	db_wrap_result * res = NULL;
	int rc;

#if 0
	rc = wr->api->query_result(wr, sql, strlen(sql), &res);
	show_errinfo(wr, rc);
	assert(0 == rc);
	assert(NULL != res);
	//MARKER("dbi_wrap_result@%p, dbi_result@%p\n",(void const *)res, res->impl.data);
	rc = res->api->finalize(res);
	show_errinfo(wr, rc);
	assert(0 == rc);
	res = NULL;
#else
	rc = db_wrap_query_exec(wr, sql, strlen(sql));
	show_errinfo(wr, rc);
	assert(0 == rc);
#endif

	/**
	   The ocilib impl will behave just fine without a COMMIT, but the data
	   written to the db cannot be read after this test finished (they're
	   rolled back). If i enable a begin/commit block then it works just fine
	   in mysql/sqlite3 (using libdbi) but Oracle breaks with an "unexpected EOF"
	   somewhere in the process.

	   Leave this at 0 until we figure out what's wrong here.
	 */
#define TRY_BEGIN_COMMIT 0

	int i;
	const int count = 10;
	char const * strVal = "hi, world";
	for(i = 1; i <= count; ++i)
	{
		char * q = NULL;
		rc = asprintf(&q,"insert into t (vint, vdbl, vstr) values(%d,%2.1lf,'%s')",
			          i,(i*1.1),strVal);
		assert(rc > 0);
		assert(q);
		res = NULL;
		rc = wr->api->query_result(wr, q, strlen(q), &res);
		show_errinfo(wr, rc);
		//MARKER("Query rc=[%d]  [%s]\n",rc, q);
		free(q);
		assert(0 == rc);
		assert(NULL != res);
		rc = res->api->finalize(res);
		assert(0 == rc);
		show_errinfo(wr, rc);
		res = NULL;
	}

	sql =
		"select * from t order by vint desc"
		;
	res = NULL;
	rc = wr->api->query_result(wr, sql, strlen(sql), &res);
	assert(0 == rc);
	assert(NULL != res);
	//assert(res->impl.data == db_wrap_dbi_result(res));

	/* ensure that stepping acts as expected. */
	int gotCount = 0;
	while(0 == (rc = res->api->step(res)))
	{
		++gotCount;
		if (1 == gotCount)
		{
			size_t sz = 0;
			char const * strCheck = NULL;
			char * strCP = NULL;
			/** The following two blocks must behave equivalently, except for
			    how they copy (or not) the underlying string ... */
#if 1
			rc = res->api->get_string_ndx(res, 2, &strCheck, &sz);
#else
			rc = db_wrap_result_string_copy_ndx(res, 2, &strCP, &sz);
			strCheck = strCP;
#endif
			assert(0 == rc);
			assert(sz > 0);
			assert(0 == strcmp( strCheck, strVal) );
			/*MARKER("Read string [%s]\n",strCheck);*/
			if (NULL != strCP) free( strCP );
		}
	}
	assert(gotCount == count);
	assert(DB_WRAP_E_DONE == rc);
	res->api->finalize(res);

	// FIXME: add reset() to the result API.

	/**
	   Now try fetching some values...
	*/



	if (0)
	{
		FIXME("get-double is not working. Not sure why.\n");
		char const * dblSql = "select vdbl from t order by vint desc limit 1";
		// not yet working. don't yet know why
		double doubleGet = -1.0;
		rc = db_wrap_query_double(wr, dblSql, strlen(dblSql), &doubleGet);
		MARKER("doubleGet=%lf\n",doubleGet);
		assert(0 == rc);
		assert(11.0 == doubleGet);
	}

	res = NULL;
	rc = wr->api->query_result(wr, sql, strlen(sql), &res);
	assert(0 == rc);
	assert(NULL != res);

	int32_t intGet = -1;
	const int32_t intExpect = count;

#if 1
#if 0
	dbi_result dbires = db_wrap_dbi_result(res);
	assert(dbi_result_next_row( dbires) );
	intGet = dbi_result_get_int_idx(dbires, 1);
#elif 0
	dbi_result dbires = (dbi_result)res->impl.data;
	assert(dbi_result_next_row( dbires) );
	intGet = dbi_result_get_int_idx(dbires, 1);
#else
	rc = res->api->step(res);
	assert(0 == rc);
	rc = res->api->get_int32_ndx(res, 0, &intGet);
	assert(0 == rc);
#endif
	//MARKER("got int=%d\n",intGet);
	assert(intGet == intExpect);
#endif
	rc = res->api->finalize(res);
	assert(0 == rc);

	intGet = -1;
	rc = db_wrap_query_int32(wr, sql, strlen(sql), &intGet);
	assert(0 == rc);
	assert(intGet == intExpect);

	int64_t int64Get = -1;
	rc = db_wrap_query_int64(wr, sql, strlen(sql), &int64Get);
	assert(0 == rc);
	assert(intGet == (int)int64Get);


}

void test_mysql_1()
{
#if ! DB_WRAP_CONFIG_ENABLE_LIBDBI
	assert(0 && "ERROR: dbi:mysql support not compiled in!");
#else
	db_wrap * wr = NULL;
	int rc = db_wrap_driver_init("dbi:mysql", &ConnParams.mysql, &wr);
	assert(0 == rc);
	assert(wr);
	rc = wr->api->connect(wr);
	assert(0 == rc);

	char * sqlCP = NULL;
	char const * sql = "hi, 'world'";
	size_t const sz = strlen(sql);
	size_t const sz2 = wr->api->sql_quote(wr, sql, sz, &sqlCP);
	assert(0 != sz2);
	assert(sz != sz2);
	/* ACHTUNG: what libdbi does here with the escaping is NOT SQL STANDARD. */
	assert(0 == strcmp("'hi, \\'world\\''", sqlCP) );
	rc = wr->api->free_string(wr, sqlCP);
	assert(0 == rc);

	test_libdbi_generic("dbi:mysql",wr);

	rc = wr->api->finalize(wr);
	assert(0 == rc);
#endif
}

void test_sqlite_1()
{
#if ! DB_WRAP_CONFIG_ENABLE_LIBDBI
	assert(0 && "ERROR: dbi:sqlite3 support not compiled in!");
#else
	db_wrap * wr = NULL;
	int rc = db_wrap_driver_init("dbi:sqlite3", &ConnParams.sqlite3, &wr);
	assert(0 == rc);
	assert(wr);
	char const * dbdir = getenv("PWD");
	rc = wr->api->option_set(wr, "sqlite3_dbdir", dbdir);
	assert(0 == rc);
	rc = wr->api->connect(wr);
	assert(0 == rc);
	char const * errmsg = NULL;
	int dbErrno = 0;
	rc = wr->api->error_info(wr, &errmsg, NULL, &dbErrno);
	assert(0 == rc);
	assert(NULL == errmsg);

	char * sqlCP = NULL;
	char const * sql = "hi, 'world'";
	size_t const sz = strlen(sql);
	size_t const sz2 = wr->api->sql_quote(wr, sql, sz, &sqlCP);
	assert(0 != sz2);
	assert(sz != sz2);
	assert(0 == strcmp("'hi, ''world'''", sqlCP) );
	rc = wr->api->free_string(wr, sqlCP);
	assert(0 == rc);

	sql = NULL;
	rc = wr->api->option_get(wr, "sqlite3_dbdir", &sql);
	assert(0 == rc);
	assert(0 == strcmp( sql, dbdir) );

	test_libdbi_generic("dbi:sqlite3",wr);


	rc = wr->api->finalize(wr);
	assert(0 == rc);
#endif
}

void test_oracle_1()
{
#if ! DB_WRAP_CONFIG_ENABLE_OCILIB
	assert(0 && "ERROR: oracle support not compiled in!");
#else
	char const * driver = "ocilib";
	db_wrap * wr = NULL;
	int rc = db_wrap_driver_init(driver, &ConnParams.oracle, &wr);
	assert(0 == rc);
	assert(wr);
	rc = wr->api->connect(wr);
	char const * errmsg = NULL;
	int dbErrCode = 0;
	wr->api->error_info(wr, &errmsg, NULL, dbErrCode);
	MARKER("connect rc=%d. Error code [%d], error string=[%s]\n",rc, dbErrCode, errmsg);
	assert(0 == rc);
	MARKER("Connected to Oracle! Erfolg! Success! Booya!\n");

	bool oldTempVal = ThisApp.useTempTables;
	if (oldTempVal)
	{
		MARKER("WARNING: the oci driver isn't working with TEMP tables (not sure why). "
			   "Disabling them. Make sure the db state is clean before running the tests!\n");
		ThisApp.useTempTables = false;
	}
	test_libdbi_generic(driver, wr);
	ThisApp.useTempTables = oldTempVal;
	wr->api->finalize(wr);
#endif
}

static void show_help(char const * appname)
{
	printf("Usage:\n\t%s [-s] [-m] [-o] [-t]\n",appname);
	puts("Options:");
	puts("\t-t = use non-temporary tables for tests. Will fail if the tables already exist.");
	puts("\t-m = enables mysql test.");
	puts("\t-s = enables sqlite3 test.");
	puts("\t-o = enables oracle test.");
}

int main(int argc, char const ** argv)
{
	int i;
	int testCount = 0;
	for(i = 1; i < argc; ++i)
	{
		char const * arg = argv[i];
		if (0 == strcmp("-t", arg) )
		{
			ThisApp.useTempTables = false;
			continue;
		}
		else if (0 == strcmp("-s", arg) )
		{
			ThisApp.testSQLite3 = true;
			++testCount;
			continue;
		}
		else if (0 == strcmp("-m", arg) )
		{
			ThisApp.testMySQL = true;
			++testCount;
			continue;
		}
		else if (0 == strcmp("-o", arg) )
		{
			ThisApp.testOracle = true;
			++testCount;
			continue;
		}
		else if ((0 == strcmp("-?", arg))
			     || (0 == strcmp("--help", arg)) )
		{
			show_help(argv[0]);
			return 1;
		}
	}

	if (testCount < 1)
	{
		puts("No test options specified!");
		show_help(argv[0]);
		return 1;
	}

	{
		ConnParams.mysql.host = "localhost";
		ConnParams.mysql.port = 3306;
		ConnParams.mysql.username = "merlin";
		ConnParams.mysql.password = "merlin";
		ConnParams.mysql.dbname = "merlin";
	}
	{
		ConnParams.sqlite3.dbname = "merlin.sqlite";
	}
	{
		//FIXME("non-default oracle port not yet supported in my oci bits.\n");
		ConnParams.oracle = ConnParams.mysql;
		ConnParams.oracle.host = "ora9.int.consol.de";
		ConnParams.oracle.dbname = "ora10g";
		ConnParams.oracle.port = 0;
	}
	if (ThisApp.testMySQL) test_mysql_1();
	if (ThisApp.testSQLite3) test_sqlite_1();
	if (ThisApp.testOracle) test_oracle_1();
	MARKER("If you got this far, it worked.\n");
	return 0;
}
