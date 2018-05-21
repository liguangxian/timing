#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "postgres.h"
#include "fmgr.h"
#include "commands/dbcommands.h"
#include "utils/timestamp.h"
#include "libpq-fe.h"
#include "libpq-int.h"
#include "utils/formatting.h"
#include "postgres_ext.h"
#include "commands/dbcommands.h"
#include "utils/lsyscache.h"
#include "utils/builtins.h"
#include "miscadmin.h"




#ifdef PG_MODULE_MAGIC
PG_MODULE_MAGIC;
#endif

PG_FUNCTION_INFO_V1(update);
PG_FUNCTION_INFO_V1(insert_timing);

Datum update(PG_FUNCTION_ARGS);
Datum insert_timing(PG_FUNCTION_ARGS);

static PGconn*
connectdb();

static int
interval_cmpare_internal(Interval *interval1, Interval *interval2);

static inline TimeOffset
interval_cmpare_value(const Interval *interval);

static char*
charge_time_type(char *cmd,char *get_type,char *get_inter,char *cstr_time);

static PGconn* connectdb()
{
	PGconn      *conn;
	Name		db;
	char        db_name[NAMEDATALEN];
	char        *user_name;
	char        conninfo[256];


	db = (Name) palloc(NAMEDATALEN);

	/* get current database name */
	namestrcpy(db, get_database_name(MyDatabaseId));
	strcpy(db_name, db->data);

	/*  get current database user name */
	user_name = GetUserNameFromId(GetUserId(), true);

	/* connect pg_database */
	sprintf(conninfo, " dbname = %s user = %s", db_name, user_name);
	conn = PQconnectdb(conninfo);

	return conn;
}

static inline TimeOffset
interval_cmpare_value(const Interval *interval)
{
	TimeOffset	span;

	span = interval->time;

#ifdef HAVE_INT64_TIMESTAMP
	span += interval->month * INT64CONST(30) * USECS_PER_DAY;
	span += interval->day * INT64CONST(24) * USECS_PER_HOUR;
#else
	span += interval->month * ((double) DAYS_PER_MONTH * SECS_PER_DAY);
	span += interval->day * ((double) HOURS_PER_DAY * SECS_PER_HOUR);
#endif

	return span;
}

//two interval compare
static int
interval_cmpare_internal(Interval *interval1, Interval *interval2)
{
	TimeOffset	span1 = interval_cmpare_value(interval1);
	TimeOffset	span2 = interval_cmpare_value(interval2);

	return ((span1 < span2) ? -1 : (span1 > span2) ? 1 : 0);
}

//charge string type,return 'cmd';
static char*
charge_time_type(char *cmd,char *get_type,char *get_inter,char *cstr_time)
{
	char		 cunit_time[20];
	char		 *quar = "quarter";

	Interval	 *inter_time = (Interval *)palloc(sizeof(Interval));
	Interval	 *unit_time = (Interval *)palloc(sizeof(Interval));


	if(!strcmp(get_type, quar))
		sprintf(cunit_time, "3 month");
	else
		sprintf(cunit_time, "1 %s", get_type);



	//timing time
	inter_time = DatumGetIntervalP((DirectFunctionCall3(interval_in,CStringGetDatum(get_inter), ObjectIdGetDatum(InvalidOid), Int32GetDatum(-1))));
	//unit time
	unit_time = DatumGetIntervalP((DirectFunctionCall3(interval_in,CStringGetDatum(cunit_time), ObjectIdGetDatum(InvalidOid), Int32GetDatum(-1))));



	if(interval_cmpare_value(inter_time) < 0)
	{
		 if (!strcmp(get_type, quar))
			 sprintf(cmd, "select date_trunc(\'%s\',timestamp \'%s\' - interval \'%s\' + interval \'3 month\' ) + interval \'%s\'",get_type, cstr_time, get_inter, get_inter);
		 else
			 sprintf(cmd, "select date_trunc(\'%s\',timestamp \'%s\'  - interval \'%s\' + interval \'1 %s\') + interval \'%s\'", get_type, cstr_time, get_inter, get_type, get_inter);

	}
	else
	{
		if (!strcmp(get_type, quar))
		{
			if(interval_cmpare_internal(inter_time,unit_time) != -1)
				sprintf(cmd,"select date_trunc(\'%s\',timestamp \'%s\') + interval \'%s\'",get_type, cstr_time, get_inter);
			else
				sprintf(cmd,"select date_trunc(\'%s\',timestamp \'%s\' + interval \'3 month\') + interval \'%s\'",get_type, cstr_time, get_inter);
		}
		else
		{
			if(interval_cmpare_internal(inter_time,unit_time) != -1)
				sprintf(cmd,"select date_trunc(\'%s\',timestamp \'%s\') + interval \'%s\'",get_type, cstr_time, get_inter);

			else
				sprintf(cmd, "select date_trunc(\'%s\',timestamp \'%s\' + interval \'1 %s\') + interval \'%s\'", get_type, cstr_time, get_type, get_inter);
		}
	}
	return cmd;
}

Datum insert_timing(PG_FUNCTION_ARGS)
{
	PGconn		 *conn;
	PGresult     *res;
	char 		 cstr[256] = "last_time: ";
	char 		 cmd[256];
	char		 *get_type;
	char 		 *cstr_last_time;
	char		 *cstr_next_time;
	char        *get_inter;

	get_type = text_to_cstring(PG_GETARG_TEXT_P(0));
	get_inter = text_to_cstring(PG_GETARG_TEXT_P(1));

	conn = connectdb();
	sprintf(cmd, "select date_trunc(\'%s\',now()) + interval \'%s\'", get_type, get_inter);
	res = PQexec(conn, cmd);
	cstr_last_time = PQgetvalue(res, 0, 0);

	res = PQexec(conn, charge_time_type(cmd,get_type,get_inter,cstr_last_time));


    cstr_next_time = PQgetvalue(res, 0, 0);
    sprintf(cmd,"insert into timing_table(timetype,last_time,next_time,inter_value) values(\'%s\',\'%s\',\'%s\',\'%s\')",get_type, cstr_last_time, cstr_next_time, get_inter);
    res = PQexec(conn, cmd);
	if(!res || PQresultStatus(res) != PGRES_COMMAND_OK)
				ereport(ERROR,
						(errcode(ERRCODE_TRIGGERED_ACTION_EXCEPTION),
							 errmsg("select date_trunc(\'%s\',now()) + interval \'%s\' is Failed! ", get_type, get_inter)));

	PQclear(res);
	strcat(cstr, cstr_last_time);
	strcat(cstr, "   next_time: ");
	strcat(cstr, cstr_next_time);

	PG_RETURN_TEXT_P(cstring_to_text(cstr));

}
Datum update(PG_FUNCTION_ARGS)
{

	PGconn		    *conn;
	PGresult 		*res;
	char    		cstr[256] = "last_time: ";
	char            cmd[256];
	char            *cstr_next_time;
	char            *get_type;
	char            *cstr_up_next_time;
	char  			*get_inter;
	char 			 *ch;
	int             id;

	id = PG_GETARG_INT16(0);

	conn = connectdb();
	sprintf(cmd,"select timetype,next_time,inter_value from timing_table where id = %d",id);
	res = PQexec(conn, cmd);
	ch = PQcmdTuples(res);

	if(!strcmp(ch, "0"))
		ereport(ERROR,
				(errcode(ERRCODE_TRIGGERED_ACTION_EXCEPTION),
						errmsg("No found id  =  \"%d\" record, Please cheack your id number! ", id)));
	if( !res || PQresultStatus(res) != PGRES_TUPLES_OK  )
					ereport(ERROR,
							(errcode(ERRCODE_TRIGGERED_ACTION_EXCEPTION),
								 errmsg("No found id  =  \"%d\" record, Please cheack your id number! ", id)));


	get_type = PQgetvalue(res, 0, 0);

	cstr_next_time = PQgetvalue(res, 0, 1);
	get_inter = PQgetvalue(res, 0, 2);

	res = PQexec(conn, charge_time_type(cmd,get_type,get_inter,cstr_next_time));
	/*
	last_time = DatumGetTimestamp(DirectFunctionCall3(timestamp_in,
							CStringGetDatum(rstr1), ObjectIdGetDatum(InvalidOid), Int32GetDatum(-1)));
	next_time = DatumGetTimestamp(DirectFunctionCall3(timestamp_in,
						CStringGetDatum(rstr1), ObjectIdGetDatum(InvalidOid), Int32GetDatum(-1)));
	sub_time = DatumGetIntervalP(DirectFunctionCall2(timestamp_mi,TimestampGetDatum(next_time),TimestampGetDatum(last_time)));
	up_next_time = DatumGetTimestamp(DirectFunctionCall2(timestamp_pl_interval,TimestampGetDatum(next_time),IntervalPGetDatum(sub_time)));

	rstr2 = DatumGetCString(DirectFunctionCall1(timestamp_out,TimestampGetDatum(up_next_time)));
   */

/*
	 inter_time = DatumGetIntervalP((DirectFunctionCall3(interval_in, CStringGetDatum(get_inter), ObjectIdGetDatum(InvalidOid), Int32GetDatum(-1))));
	 unit_time = DatumGetIntervalP((DirectFunctionCall3(interval_in,CStringGetDatum(cunit_time), ObjectIdGetDatum(InvalidOid), Int32GetDatum(-1))));

	 if(!strcmp(get_type, quar))
	 {
		 sprintf(cunit_time, "3 month");
	 }
	 else
	 {
		 sprintf(cunit_time, "1 %s", get_type);
	 }

	 if(interval_cmpare_value(inter_time) < 0)
	 {
		 if (!strcmp(get_type, quar))
		 {
			 printf("55555555\n");
		 	 sprintf(cmd, "select date_trunc(\'%s\',timestamp \'%s\' - interval \'%s\' + interval \'3 month\' ) + interval \'%s\'",get_type, cstr_next_time, get_inter, get_inter);
		 }
		 else
		 {
			 printf("666666666\n");
		 	 sprintf(cmd, "select date_trunc(\'%s\',timestamp \'%s\'  - interval \'%s\' + interval \'1 %s\') + interval \'%s\'", get_type, cstr_next_time, get_inter, get_type, get_inter);
		 }
	 }
	 else
	 {
	    if (!strcmp(get_type, quar))
	    {
	    	if(interval_cmpare_internal(inter_time,unit_time) != -1)
	    	{
	    		printf("7777777777\n");
	    		sprintf(cmd, "select date_trunc(\'%s\',timestamp \'%s\' ) + interval \'%s\'", get_type, cstr_next_time, get_inter);
	    	}
	    	else
	    	{
	    		printf("8888888888\n");
	    		sprintf(cmd, "select date_trunc(\'%s\',timestamp \'%s\' + interval \'3 month\') + interval \'%s\'", get_type, cstr_next_time, get_inter);
	    	}
	    }
	    else
	    {
	    	if(interval_cmpare_internal(inter_time,unit_time) != -1)
	    	{
	    		printf("9999999999\n");
	    		sprintf(cmd, "select date_trunc(\'%s\',timestamp \'%s\' ) + interval \'%s\'", get_type, cstr_next_time, get_inter);
	    	}
	    	else
	    	{
	    		printf("10101010\n");
	    		sprintf(cmd, "select date_trunc(\'%s\',timestamp \'%s\' + interval \'1 %s\') + interval \'%s\'", get_type, cstr_next_time, get_type, get_inter);
	    	}
	    }

	 }
*/

	cstr_up_next_time = PQgetvalue(res, 0, 0);
	sprintf(cmd, "update timing_table set last_time = \'%s\',next_time = \'%s\' where id = %d", cstr_next_time, cstr_up_next_time, id);
	res = PQexec(conn, cmd);
	PQclear(res);

	strcat(cstr, cstr_next_time);
	strcat(cstr, "    next_time: ");
	strcat(cstr, cstr_up_next_time);

	PG_RETURN_TEXT_P(cstring_to_text(cstr));
}

