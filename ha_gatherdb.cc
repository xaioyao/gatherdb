/* Copyright (c) 2004, 2013, Oracle and/or its affiliates. All rights reserved.

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; version 2 of the License.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA */

/**
  @file ha_gatherdb.cc

  @brief
  The ha_gatherdb engine is a stubbed storage engine for gatherdb purposes only;
  it does nothing at this point. Its purpose is to provide a source
  code illustration of how to begin writing new storage engines; see also
  /storage/gatherdb/ha_gatherdb.h.

  @details
  ha_gatherdb will let you create/open/delete tables, but
  nothing further (for gatherdb, indexes are not supported nor can data
  be stored in the table). Use this gatherdb as a template for
  implementing the same functionality in your own storage engine. You
  can enable the gatherdb storage engine in your build by doing the
  following during your build process:<br> ./configure
  --with-gatherdb-storage-engine

  Once this is done, MySQL will let you create tables with:<br>
  CREATE TABLE <table name> (...) ENGINE=GATHERDB;

  The gatherdb storage engine is set up to use table locks. It
  implements an gatherdb "SHARE" that is inserted into a hash by table
  name. You can use this to store information of state that any
  gatherdb handler object will be able to see when it is using that
  table.

  Please read the object definition in ha_gatherdb.h before reading the rest
  of this file.

  @note
  When you create an GATHERDB table, the MySQL Server creates a table .frm
  (format) file in the database directory, using the table name as the file
  name as is customary with MySQL. No other files are created. To get an idea
  of what occurs, here is an gatherdb select that would do a scan of an entire
  table:

  @code
  ha_gatherdb::store_lock
  ha_gatherdb::external_lock
  ha_gatherdb::info
  ha_gatherdb::rnd_init
  ha_gatherdb::extra
  ENUM HA_EXTRA_CACHE        Cache record in HA_rrnd()
  ha_gatherdb::rnd_next
  ha_gatherdb::rnd_next
  ha_gatherdb::rnd_next
  ha_gatherdb::rnd_next
  ha_gatherdb::rnd_next
  ha_gatherdb::rnd_next
  ha_gatherdb::rnd_next
  ha_gatherdb::rnd_next
  ha_gatherdb::rnd_next
  ha_gatherdb::extra
  ENUM HA_EXTRA_NO_CACHE     End caching of records (def)
  ha_gatherdb::external_lock
  ha_gatherdb::extra
  ENUM HA_EXTRA_RESET        Reset database to after open
  @endcode

  Here you see that the gatherdb storage engine has 9 rows called before
  rnd_next signals that it has reached the end of its data. Also note that
  the table in question was already opened; had it not been open, a call to
  ha_gatherdb::open() would also have been necessary. Calls to
  ha_gatherdb::extra() are hints as to what will be occuring to the request.

  A Longer Example can be found called the "Skeleton Engine" which can be 
  found on TangentOrg. It has both an engine and a full build environment
  for building a pluggable storage engine.

  Happy coding!<br>
    -Brian
*/


#define MYSQL_SERVER 1
#include "sql_priv.h"
#include "sql_class.h"           // MYSQL_HANDLERTON_INTERFACE_VERSION
#include <mysql/plugin.h>
#include "mysql.h"

#ifdef USE_PRAGMA_IMPLEMENTATION
#pragma implementation        // gcc: Class implementation
#endif

#include "ha_gatherdb.h"
#include "probes_mysql.h"
#include "sql_plugin.h"
#include <mysql/plugin.h>

static handler *gatherdb_create_handler(handlerton *hton,
                                       TABLE_SHARE *table, 
                                       MEM_ROOT *mem_root);

handlerton *gatherdb_hton;

/* Interface to mysqld, to check system tables supported by SE */
static const char* gatherdb_system_database();
static bool gatherdb_is_supported_system_table(const char *db,
                                      const char *table_name,
                                      bool is_sql_layer_system_table);

/* Variables for gatherdb share methods */

/* 
   Hash used to track the number of open tables; variable for gatherdb share
   methods
*/
static HASH gatherdb_open_tables;
static connpool *cp;
/* The mutex used to init the hash; variable for gatherdb share methods */
mysql_mutex_t gatherdb_mutex;

/**
  @brief
  Function we use in the creation of our hash to get key.
*/

static uchar* gatherdb_get_key(GATHERDB_SHARE *share, size_t *length,
                             my_bool not_used __attribute__((unused)))
{
  *length=share->table_name_length;
  return (uchar*) share->table_name;
}

#ifdef HAVE_PSI_INTERFACE
static PSI_mutex_key ex_key_mutex_gatherdb, ex_key_mutex_GATHERDB_SHARE_mutex;

static PSI_mutex_info all_gatherdb_mutexes[]=
{
  { &ex_key_mutex_gatherdb, "gatherdb", PSI_FLAG_GLOBAL},
  { &ex_key_mutex_GATHERDB_SHARE_mutex, "GATHERDB_SHARE::mutex", 0}
};

static void init_gatherdb_psi_keys()
{
  const char* category= "gatherdb";
  int count;

  if (PSI_server == NULL)
    return;

  count= array_elements(all_gatherdb_mutexes);
  PSI_server->register_mutex(category, all_gatherdb_mutexes, count);
}
#endif

static void cpool_init_func()
{
	cp=new connpool();
	cp->init();
	cp->init_instances();
	cp->_init_connect();
}

static int gatherdb_init_func(void *p)
{
  DBUG_ENTER("gatherdb_init_func");

#ifdef HAVE_PSI_INTERFACE
  init_gatherdb_psi_keys();
#endif

  gatherdb_hton= (handlerton *)p;
  mysql_mutex_init(ex_key_mutex_gatherdb, &gatherdb_mutex, MY_MUTEX_INIT_FAST);
  (void) my_hash_init(&gatherdb_open_tables,system_charset_info,32,0,0,
                      (my_hash_get_key) gatherdb_get_key,0,0);

  gatherdb_hton->state=   SHOW_OPTION_YES;
  gatherdb_hton->create=  gatherdb_create_handler;
  gatherdb_hton->flags=   HTON_CAN_RECREATE;
  gatherdb_hton->system_database=   NULL;
  gatherdb_hton->is_supported_system_table= gatherdb_is_supported_system_table;
  //初始化连接缓冲池
  cpool_init_func();
  DBUG_RETURN(0);
}


static int gatherdb_done_func(void *p)
{
  int error= 0;
  DBUG_ENTER("gatherdb_done_func");

  if (gatherdb_open_tables.records)
    error= 1;
  my_hash_free(&gatherdb_open_tables);
  mysql_mutex_destroy(&gatherdb_mutex);
  //释放连接缓冲池
  DBUG_RETURN(error);
}


/**
  @brief
  Example of simple lock controls. The "share" it creates is a
  structure we will pass to each gatherdb handler. Do you have to have
  one of these? Well, you have pieces that are used for locking, and
  they are needed to function.
*/

static GATHERDB_SHARE *get_share(const char *table_name, TABLE *table)
{
  GATHERDB_SHARE *share;
  uint length;
  char *tmp_name;

  mysql_mutex_lock(&gatherdb_mutex);
  length=(uint) strlen(table_name);

  if (!(share=(GATHERDB_SHARE*) my_hash_search(&gatherdb_open_tables,
                                              (uchar*) table_name,
                                              length)))
  {
    if (!(share=(GATHERDB_SHARE *)
          my_multi_malloc(MYF(MY_WME | MY_ZEROFILL),
                          &share, sizeof(*share),
                          &tmp_name, length+1,
                          NullS)))
    {
      mysql_mutex_unlock(&gatherdb_mutex);
      return NULL;
    }

    share->use_count=0;
    share->table_name_length=length;
    share->table_name=tmp_name;
    strmov(share->table_name,table_name);
    if (my_hash_insert(&gatherdb_open_tables, (uchar*) share))
      goto error;
    thr_lock_init(&share->lock);
    mysql_mutex_init(ex_key_mutex_GATHERDB_SHARE_mutex,
                     &share->mutex, MY_MUTEX_INIT_FAST);
  }
  share->use_count++;
  mysql_mutex_unlock(&gatherdb_mutex);

  return share;

error:
  mysql_mutex_destroy(&share->mutex);
  my_free(share);

  return NULL;
}


/**
  @brief
  Free lock controls. We call this whenever we close a table. If the table had
  the last reference to the share, then we free memory associated with it.
*/

static int free_share(GATHERDB_SHARE *share)
{
  mysql_mutex_lock(&gatherdb_mutex);
  if (!--share->use_count)
  {
    my_hash_delete(&gatherdb_open_tables, (uchar*) share);
    thr_lock_delete(&share->lock);
    mysql_mutex_destroy(&share->mutex);
    my_free(share);
  }
  mysql_mutex_unlock(&gatherdb_mutex);

  return 0;
}

static handler* gatherdb_create_handler(handlerton *hton,
                                       TABLE_SHARE *table, 
                                       MEM_ROOT *mem_root)
{
  return new (mem_root) ha_gatherdb(hton, table);
}

ha_gatherdb::ha_gatherdb(handlerton *hton, TABLE_SHARE *table_arg)
  :handler(hton, table_arg)
{
	cpool=cp;
	cpool->pool_real_connect();
	lst=0;
}


/**
  @brief
  If frm_error() is called then we will use this to determine
  the file extensions that exist for the storage engine. This is also
  used by the default rename_table and delete_table method in
  handler.cc.

  For engines that have two file name extentions (separate meta/index file
  and data file), the order of elements is relevant. First element of engine
  file name extentions array should be meta/index file extention. Second
  element - data file extention. This order is assumed by
  prepare_for_repair() when REPAIR TABLE ... USE_FRM is issued.

  @see
  rename_table method in handler.cc and
  delete_table method in handler.cc
*/

static const char *ha_gatherdb_exts[] = {
  NullS
};

static void* init_table_map()
{
	if(stm==NULL)
	{
		stm=new shard_table_map();
		stm->init();
	}
	return NULL;
}

const char **ha_gatherdb::bas_ext() const
{
  return ha_gatherdb_exts;
}


/*
  List of all system tables specific to the SE.
  Array element would look like below,
     { "<database_name>", "<system table name>" },
  The last element MUST be,
     { (const char*)NULL, (const char*)NULL }

  This array is optional, so every SE need not implement it.
*/
static st_system_tablename ha_gatherdb_system_tables[]= {
  {(const char*)NULL, (const char*)NULL}
};

/**
  @brief Check if the given db.tablename is a system table for this SE.

  @param db                         Database name to check.
  @param table_name                 table name to check.
  @param is_sql_layer_system_table  if the supplied db.table_name is a SQL
                                    layer system table.

  @return
    @retval TRUE   Given db.table_name is supported system table.
    @retval FALSE  Given db.table_name is not a supported system table.
*/
static bool gatherdb_is_supported_system_table(const char *db,
                                              const char *table_name,
                                              bool is_sql_layer_system_table)
{
  st_system_tablename *systab;

  // Does this SE support "ALL" SQL layer system tables ?
  if (is_sql_layer_system_table)
    return false;

  // Check if this is SE layer system tables
  systab= ha_gatherdb_system_tables;
  while (systab && systab->db)
  {
    if (systab->db == db &&
        strcmp(systab->tablename, table_name) == 0)
      return true;
    systab++;
  }

  return false;
}



/*
  Convert MySQL result set row to handler internal format

  SYNOPSIS
    convert_row_to_internal_format()
      record    Byte pointer to record
      row       MySQL result set row from fetchrow()
      result	Result set to use

  DESCRIPTION
    This method simply iterates through a row returned via fetchrow with
    values from a successful SELECT , and then stores each column's value
    in the field object via the field object pointer (pointing to the table's
    array of field object pointers). This is how the handler needs the data
    to be stored to then return results back to the user

  RETURN VALUE
    0   After fields have had field values stored from record
*/

uint ha_gatherdb::convert_row_to_internal_format(uchar *record,
                                                  MYSQL_ROW row,
                                                  MYSQL_RES *result)
{
  ulong *lengths;
  Field **field;
  my_bitmap_map *old_map= dbug_tmp_use_all_columns(table, table->write_set);
  DBUG_ENTER("ha_gatherdb::convert_row_to_internal_format");

  lengths= mysql_fetch_lengths(result);

  for (field= table->field; *field; field++, row++, lengths++)
  {
    /*
      index variable to move us through the row at the
      same iterative step as the field
    */
    my_ptrdiff_t old_ptr;
    old_ptr= (my_ptrdiff_t) (record - table->record[0]);
    (*field)->move_field_offset(old_ptr);
    if (!*row)
    {
      (*field)->set_null();
      (*field)->reset();
    }
    else
    {
      if (bitmap_is_set(table->read_set, (*field)->field_index))
      {
        (*field)->set_notnull();
        (*field)->store(*row, *lengths, &my_charset_bin);
      }
    }
    (*field)->move_field_offset(-old_ptr);
  }
  dbug_tmp_restore_column_map(table->write_set, old_map);
  DBUG_RETURN(0);
}

/**
  @brief
  Used for opening tables. The name will be the name of the file.

  @details
  A table is opened when it needs to be opened; e.g. when a request comes in
  for a SELECT on the table (tables are not open and closed for each request,
  they are cached).

  Called from handler.cc by handler::ha_open(). The server opens all tables by
  calling ha_open() which then calls the handler specific open().

  @see
  handler::ha_open() in handler.cc
*/

int ha_gatherdb::open(const char *name, int mode, uint test_if_locked)
{
  DBUG_ENTER("ha_gatherdb::open");

  if (!(share = get_share(name, table)))
    DBUG_RETURN(1);
  thr_lock_data_init(&share->lock,&lock,NULL);
  my_init_dynamic_array(&results, sizeof(MYSQL_RES *), 4, 4);
  result_position=0;
  DBUG_RETURN(0);
}


/**
  @brief
  Closes a table. We call the free_share() function to free any resources
  that we have allocated in the "shared" structure.

  @details
  Called from sql_base.cc, sql_select.cc, and table.cc. In sql_select.cc it is
  only used to close up temporary tables or during the process where a
  temporary table is converted over to being a myisam table.

  For sql_base.cc look at close_data_tables().

  @see
  sql_base.cc, sql_select.cc and table.cc
*/

int ha_gatherdb::close(void)
{
  DBUG_ENTER("ha_gatherdb::close");
  DBUG_RETURN(free_share(share));
}



/**
  @brief
  rnd_init() is called when the system wants the storage engine to do a table
  scan. See the gatherdb in the introduction at the top of this file to see when
  rnd_init() is called.

  @details
  Called from filesort.cc, records.cc, sql_handler.cc, sql_select.cc, sql_table.cc,
  and sql_update.cc.

  @see
  filesort.cc, records.cc, sql_handler.cc, sql_select.cc, sql_table.cc and sql_update.cc
*/
int ha_gatherdb::rnd_init(bool scan)
{
  DBUG_ENTER("ha_gatherdb::rnd_init");
  store_result();
  DBUG_RETURN(0);
}

int ha_gatherdb::rnd_next_int(uchar *buf) 
{
  DBUG_ENTER("ha_gatherdb::rnd_next_int");

  DBUG_RETURN(read_next(buf));
}

void *ha_gatherdb::store_result()
{
  DBUG_ENTER("ha_federated::store_result");
  char **select_sql=lst->sql_commands;
  char *sql_command;
  int idx=0;
  while(idx<lst->shard_info.elements)
  {
	sql_command=*select_sql;
	MYSQL *sql_mysql=mysql_init(NULL);
	mysql_real_connect(sql_mysql,"127.0.0.1","root","","",3306,NULL,0);
	mysql_real_query(sql_mysql,sql_command,strlen(sql_command));
	MYSQL_RES *result= mysql_store_result(sql_mysql);
	if (result)
	{
		(void) insert_dynamic(&results, (uchar*) &result);
	}
	select_sql++;
	idx++;
	//mysql_close(sql_mysql);
  }
  DBUG_RETURN(NULL);
}


/*
  ha_federated::read_next

  reads from a result set and converts to mysql internal
  format

  SYNOPSIS
    field_in_record_is_null()
      buf       byte pointer to record 
      result    mysql result set 

    DESCRIPTION
     This method is a wrapper method that reads one record from a result
     set and converts it to the internal table format

    RETURN VALUE
      1    error
      0    no error 
*/

int ha_gatherdb::read_next(uchar *buf)
{
  int retval;
  MYSQL_ROW row;
  DBUG_ENTER("ha_gatherdb::read_next");

  table->status= STATUS_NOT_FOUND;              // For easier return
 
  /* Save current data cursor position. */
  MYSQL_RES *result;
  get_dynamic(&results, (uchar *) &result, result_position);
  current_position= result->data_cursor;

  /* Fetch a row, insert it back in a row format. */
  if (!(row= mysql_fetch_row(result)))
  {
	
	do
	{
	  result_position++;
	  get_dynamic(&results, (uchar *) &result, result_position);
	  if(!result)DBUG_RETURN(HA_ERR_END_OF_FILE);
	  current_position= result->data_cursor;
	  row= mysql_fetch_row(result);
	}while(!row);
  }
  if(result_position==results.elements)
    DBUG_RETURN(HA_ERR_END_OF_FILE);

  if (!(retval= convert_row_to_internal_format(buf, row, result)))
    table->status= 0;

  DBUG_RETURN(retval);
}

/**
  @brief
  This is called for each row of the table scan. When you run out of records
  you should return HA_ERR_END_OF_FILE. Fill buff up with the row information.
  The Field structure for the table is the key to getting data into buf
  in a manner that will allow the server to understand it.

  @details
  Called from filesort.cc, records.cc, sql_handler.cc, sql_select.cc, sql_table.cc,
  and sql_update.cc.

  @see
  filesort.cc, records.cc, sql_handler.cc, sql_select.cc, sql_table.cc and sql_update.cc
*/
int ha_gatherdb::rnd_next(uchar *buf)
{
  int rc;
  DBUG_ENTER("ha_gatherdb::rnd_next");
  MYSQL_READ_ROW_START(table_share->db.str, table_share->table_name.str,
                       TRUE);
  rc= rnd_next_int(buf);
  MYSQL_READ_ROW_DONE(rc);
  DBUG_RETURN(rc);
}


/**
  @brief
  position() is called after each call to rnd_next() if the data needs
  to be ordered. You can do something like the following to store
  the position:
  @code
  my_store_ptr(ref, ref_length, current_position);
  @endcode

  @details
  The server uses ref to store data. ref_length in the above case is
  the size needed to store current_position. ref is just a byte array
  that the server will maintain. If you are using offsets to mark rows, then
  current_position should be the offset. If it is a primary key like in
  BDB, then it needs to be a primary key.

  Called from filesort.cc, sql_select.cc, sql_delete.cc, and sql_update.cc.

  @see
  filesort.cc, sql_select.cc, sql_delete.cc and sql_update.cc
*/
void ha_gatherdb::position(const uchar *record)
{
  DBUG_ENTER("ha_gatherdb::position");
  DBUG_VOID_RETURN;
}


/**
  @brief
  This is like rnd_next, but you are given a position to use
  to determine the row. The position will be of the type that you stored in
  ref. You can use ha_get_ptr(pos,ref_length) to retrieve whatever key
  or position you saved when position() was called.

  @details
  Called from filesort.cc, records.cc, sql_insert.cc, sql_select.cc, and sql_update.cc.

  @see
  filesort.cc, records.cc, sql_insert.cc, sql_select.cc and sql_update.cc
*/
int ha_gatherdb::rnd_pos(uchar *buf, uchar *pos)
{
  int rc;
  DBUG_ENTER("ha_gatherdb::rnd_pos");
  MYSQL_READ_ROW_START(table_share->db.str, table_share->table_name.str,
                       TRUE);
  rc= HA_ERR_WRONG_COMMAND;
  MYSQL_READ_ROW_DONE(rc);
  DBUG_RETURN(rc);
}


/**
  @brief
  ::info() is used to return information to the optimizer. See my_base.h for
  the complete description.

  @details
  Currently this table handler doesn't implement most of the fields really needed.
  SHOW also makes use of this data.

  You will probably want to have the following in your code:
  @code
  if (records < 2)
    records = 2;
  @endcode
  The reason is that the server will optimize for cases of only a single
  record. If, in a table scan, you don't know the number of records, it
  will probably be better to set records to two so you can return as many
  records as you need. Along with records, a few more variables you may wish
  to set are:
    records
    deleted
    data_file_length
    index_file_length
    delete_length
    check_time
  Take a look at the public variables in handler.h for more information.

  Called in filesort.cc, ha_heap.cc, item_sum.cc, opt_sum.cc, sql_delete.cc,
  sql_delete.cc, sql_derived.cc, sql_select.cc, sql_select.cc, sql_select.cc,
  sql_select.cc, sql_select.cc, sql_show.cc, sql_show.cc, sql_show.cc, sql_show.cc,
  sql_table.cc, sql_union.cc, and sql_update.cc.

  @see
  filesort.cc, ha_heap.cc, item_sum.cc, opt_sum.cc, sql_delete.cc, sql_delete.cc,
  sql_derived.cc, sql_select.cc, sql_select.cc, sql_select.cc, sql_select.cc,
  sql_select.cc, sql_show.cc, sql_show.cc, sql_show.cc, sql_show.cc, sql_table.cc,
  sql_union.cc and sql_update.cc
*/
int ha_gatherdb::info(uint flag)
{
  DBUG_ENTER("ha_gatherdb::info");
  init_table_map();
  if(lst!=NULL) free(lst);
  lst=new list_sql_tree(current_thd);
  lst->list_lex_tree();
  lst->list_lex_merge();
  lst->get_shard_table_info();
  lst->resetup_sql_command(stm);
  DBUG_RETURN(0);
}




/**
  @brief
  This create a lock on the table. If you are implementing a storage engine
  that can handle transacations look at ha_berkely.cc to see how you will
  want to go about doing this. Otherwise you should consider calling flock()
  here. Hint: Read the section "locking functions for mysql" in lock.cc to understand
  this.

  @details
  Called from lock.cc by lock_external() and unlock_external(). Also called
  from sql_table.cc by copy_data_between_tables().

  @see
  lock.cc by lock_external() and unlock_external() in lock.cc;
  the section "locking functions for mysql" in lock.cc;
  copy_data_between_tables() in sql_table.cc.
*/
int ha_gatherdb::external_lock(THD *thd, int lock_type)
{
  DBUG_ENTER("ha_gatherdb::external_lock");
  DBUG_RETURN(0);
}


/**
  @brief
  The idea with handler::store_lock() is: The statement decides which locks
  should be needed for the table. For updates/deletes/inserts we get WRITE
  locks, for SELECT... we get read locks.

  @details
  Before adding the lock into the table lock handler (see thr_lock.c),
  mysqld calls store lock with the requested locks. Store lock can now
  modify a write lock to a read lock (or some other lock), ignore the
  lock (if we don't want to use MySQL table locks at all), or add locks
  for many tables (like we do when we are using a MERGE handler).

  Berkeley DB, for gatherdb, changes all WRITE locks to TL_WRITE_ALLOW_WRITE
  (which signals that we are doing WRITES, but are still allowing other
  readers and writers).

  When releasing locks, store_lock() is also called. In this case one
  usually doesn't have to do anything.

  In some exceptional cases MySQL may send a request for a TL_IGNORE;
  This means that we are requesting the same lock as last time and this
  should also be ignored. (This may happen when someone does a flush
  table when we have opened a part of the tables, in which case mysqld
  closes and reopens the tables and tries to get the same locks at last
  time). In the future we will probably try to remove this.

  Called from lock.cc by get_lock_data().

  @note
  In this method one should NEVER rely on table->in_use, it may, in fact,
  refer to a different thread! (this happens if get_lock_data() is called
  from mysql_lock_abort_for_thread() function)

  @see
  get_lock_data() in lock.cc
*/
THR_LOCK_DATA **ha_gatherdb::store_lock(THD *thd,
                                       THR_LOCK_DATA **to,
                                       enum thr_lock_type lock_type)
{
  if (lock_type != TL_IGNORE && lock.type == TL_UNLOCK)
    lock.type=lock_type;
  *to++= &lock;
  return to;
}





/**
  @brief
  create() is called to create a database. The variable name will have the name
  of the table.

  @details
  When create() is called you do not need to worry about
  opening the table. Also, the .frm file will have already been
  created so adjusting create_info is not necessary. You can overwrite
  the .frm file at this point if you wish to change the table
  definition, but there are no methods currently provided for doing
  so.

  Called from handle.cc by ha_create_table().

  @see
  ha_create_table() in handle.cc
*/

int ha_gatherdb::create(const char *name, TABLE *table_arg,
                       HA_CREATE_INFO *create_info)
{
  DBUG_ENTER("ha_gatherdb::create");
  /*
    This is not implemented but we want someone to be able to see that it
    works.
  */
  DBUG_RETURN(0);
}


struct st_mysql_storage_engine gatherdb_storage_engine=
{ MYSQL_HANDLERTON_INTERFACE_VERSION };


mysql_declare_plugin(gatherdb)
{
  MYSQL_STORAGE_ENGINE_PLUGIN,
  &gatherdb_storage_engine,
  "GATHERDB",
  "Zhiheng YAO, TZ",
  "Gather TZ storage engine",
  PLUGIN_LICENSE_GPL,
  gatherdb_init_func,                            /* Plugin Init */
  gatherdb_done_func,                            /* Plugin Deinit */
  0x0001 /* 0.1 */,
  NULL,                                  /* status variables */
  NULL,                     /* system variables */
  NULL,                                         /* config options */
  0,                                            /* flags */
}
mysql_declare_plugin_end;
