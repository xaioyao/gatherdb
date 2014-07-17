/* Copyright (c) 2004, 2010, Oracle and/or its affiliates. All rights reserved.

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

/** @file ha_gatherdb.h

    @brief
  The ha_gatherdb engine is a stubbed storage engine for gatherdb purposes only;
  it does nothing at this point. Its purpose is to provide a source
  code illustration of how to begin writing new storage engines; see also
  /storage/gatherdb/ha_gatherdb.cc.

    @note
  Please read ha_gatherdb.cc before reading this file.
  Reminder: The gatherdb storage engine implements all methods that are *required*
  to be implemented. For a full list of all methods that you can implement, see
  handler.h.

   @see
  /sql/handler.h and /storage/gatherdb/ha_gatherdb.cc
*/
#ifdef USE_PRAGMA_INTERFACE
#pragma interface			/* gcc class implementation */
#endif

#include "mysql.h"
#include "strcmp.h"
#include "mydb_list.h"

/** @brief
  GATHERDB_SHARE is a structure that will be shared among all open handlers.
  This gatherdb implements the minimum of what you will probably need.
*/

typedef struct st_gatherdb_share {
  char *table_name;
  uint table_name_length,use_count;
  mysql_mutex_t mutex;
  THR_LOCK lock;
} GATHERDB_SHARE;

#define MAX_CONNECTIONS 1


typedef struct mydb_mysql_instance{
	char* server;
	uint sport;
}MYSQL_INSTANCE;

typedef struct mydb_connect_param{
	MYSQL_INSTANCE *instance;
	char* user;
	char* password;
	char* schema;
	char* table_name;
}CONNECT_PARAM;

#define MYDB_MAX_SQL_LENGTH 1024
#define MYDB_TABLE_MAP "table_map"
#define MYDB_TRAIN_MAP "train_map"
#define MYDB_TRAIN_MAP_ID "trainid"
#define MYDB_PACKAGE_MAP_ID "packageid"
static MYSQL_INSTANCE sharding_instance={"127.0.0.1",3306};
static CONNECT_PARAM sharding_instance_param={&sharding_instance,"root","","tzroute",""};

typedef struct mydb_mysql_connect{
	MYSQL *mysql;
	bool isused;
	bool isalive;
}MYSQL_CONNECT;

class connect_pool
{
public:
	CONNECT_PARAM *param;
	MYSQL_CONNECT connections[MAX_CONNECTIONS];
	uint free_length;
	connect_pool *next;
	connect_pool(){
		param=(CONNECT_PARAM *)my_malloc(sizeof(CONNECT_PARAM),MYF(0));
		param->instance = (MYSQL_INSTANCE *)my_malloc(sizeof(MYSQL_INSTANCE),MYF(0));
		param->instance->server=0;
		param->instance->sport=0;
	}
	~connect_pool(){
		free(param->instance);
		free(param);
	};
};

class connpool
{
private:

public:
	connect_pool* pools;
	connect_pool *instancepool;
	uint instances_count;
	mysql_mutex_t mutex;
	THR_LOCK lock;

	connpool(){};
	~connpool(){};
	int init();
	int init_instances();
	MYSQL_CONNECT *fetchone(MYSQL_INSTANCE *instance);
	void releaseone(MYSQL_CONNECT *connection);
	int realiveconnect(MYSQL_CONNECT *connection,CONNECT_PARAM *param); 
	int _init_connect();
	int pool_real_connect();
	int dispose();
};

#define SPACECHAR ','

class mydb_value_list
{
public:
	char *value;
	enum Item::Type value_type;
	mydb_value_list(){value=0;value_type=Item::INT_ITEM;};
	mydb_value_list(Item *item);
	mydb_value_list(mydb_value_list *mvl);
};

class mydb_schema_table{
public:
	char  *schema_name;
	char  *table_name;
	char  *table_alias;
	bool is_alias_used;
	mydb_schema_table(){};
};


class mydb_field_detail{
public:
	mydb_schema_table field_table;
	char *f_name;

	mydb_field_detail()
	{
		
	};
	mydb_field_detail(Item *item)
	{
		field_table.schema_name=(char *)my_malloc(strlen(((Item_field *)item)->field->table->s->db.str)+1,MYF(0));
		strcpy(field_table.schema_name,((Item_field *)item)->field->table->s->db.str);
		field_table.table_alias=(char *)my_malloc(strlen(*(((Item_field *)item)->field->table_name))+1,MYF(0));
		strcpy(field_table.table_alias,*(((Item_field *)item)->field->table_name));
		field_table.table_name=(char *)my_malloc(strlen(((Item_field *)item)->field->table->alias)+1,MYF(0));
		strcpy(field_table.table_alias,((Item_field *)item)->field->table->alias);
		field_table.is_alias_used=((Item_field *)item)->field->table->alias_name_used;
		f_name=(char *)my_malloc(strlen(item->name)+1,MYF(0));
		strcpy(f_name,item->name);
	};
};

class mydb_field_cond{
public:
	mydb_field_detail *field_name;
	List<mydb_value_list> values;//¼ÇÂ¼Âß¼­ÔËËã·ûvalue
	bool isRange;
	mydb_value_list s_min;//¼ÇÂ¼between
	mydb_value_list s_max;

	int _nodes;
	mydb_field_cond();
	mydb_field_cond(Item *field,Item *value);
	mydb_field_cond(Item *field,Item *minvalue,Item *maxvalue);
	mydb_field_cond(Item *multilist);
	mydb_field_cond(mydb_field_cond *mfc);
	void setfield(Item *field);
	void addvalue(Item *value);	
};

class shard_table_map
{
private:
public:
	char **table_map;
	shard_table_map(){table_map=(char **)my_malloc(sizeof(char **),MYF(0));};
	int init();
	bool table_in_list(const char *table_name);
};

class mydb_shard_table_map{
public:
	mydb_schema_table orgtable;
	mydb_schema_table shardtable;
	mydb_shard_table_map(){};
};

class list_sql_tree
{
private:
	char *_query;
	List<mydb_schema_table> tablelist;
	THD *list_thd;
	List<mydb_field_cond> fieldlist;
	List<mydb_field_cond> mergelist;
	List<char> where_cond;
	//MEM_ROOT mem_root;
	void _move_node(Item **conds,int nodes);
	void _add_fields_2(Item *field,Item *value);
	void _add_fields_3(Item *field,Item *minvalue,Item *maxvalue);
	int _add_fields_n(Item *multilist);
	mydb_field_cond* _is_filed_in_list(mydb_field_cond *fl);
	void _merge_field_cond(mydb_field_cond* mfiled,mydb_field_cond* filed);
	int _list_sql_table_list();
	int _list_lex_where_tree(COND *cond);
	int _list_lex_tree(COND *conds);
	int _fetch_field_cond(mydb_field_cond *mfc,char *c_cond);
	int _list_field_cond();
	char *_make_where_str();
	int _make_shard_command(char *result);
public:
	char **sql_commands;
	List<CONNECT_PARAM> shard_info;
	list_sql_tree(){
		//init_alloc_root(&mem_root,256,0);
	};
	list_sql_tree(THD *thd){
		list_thd=thd;_query=list_thd->query();
		//init_alloc_root(&mem_root,256,0);
	};
	~list_sql_tree(){
		//free_root(&mem_root,MYF(0));
	};
	int list_lex_tree();
	int list_lex_merge();
	int get_shard_table_info();
	int resetup_sql_command(shard_table_map *stm1);
};

static shard_table_map *stm;
static	MYSQL *mysql;
/** @brief
  Class definition for the storage engine
*/
class ha_gatherdb: public handler
{
  THR_LOCK_DATA lock;      ///< MySQL lock
  GATHERDB_SHARE *share;    ///< Shared lock info
  MYSQL_ROW_OFFSET current_position;
  int result_position;
  /**
    Array of all stored results we get during a query execution.
  */
  DYNAMIC_ARRAY results;
  THD *thd;//current_thd
  list_sql_tree *lst;
  connpool *cpool;
private:
	uint convert_row_to_internal_format(uchar *record,
                                                  MYSQL_ROW row,
                                                  MYSQL_RES *result);
	int read_next(uchar *buf);
	int rnd_next_int(uchar *buf);
	void *ha_gatherdb::store_result();
public:
	ha_gatherdb(handlerton *hton, TABLE_SHARE *table_arg);
	~ha_gatherdb(){ }

  /** @brief
    The name that will be used for display purposes.
   */
  const char *table_type() const { return "GATHERDB"; }



  /** @brief
    The file extensions.
   */
  const char **bas_ext() const;

  /** @brief
    This is a list of flags that indicate what functionality the storage engine
    implements. The current table flags are documented in handler.h
  */
  ulonglong table_flags() const
  {
    /*
      We are saying that this engine is just statement capable to have
      an engine that can only handle statement-based logging. This is
      used in testing.
    */
    return HA_BINLOG_STMT_CAPABLE;
  }

  /** @brief
    This is a bitmap of flags that indicates how the storage engine
    implements indexes. The current index flags are documented in
    handler.h. If you do not implement indexes, just return zero here.

      @details
    part is the key part to check. First key part is 0.
    If all_parts is set, MySQL wants to know the flags for the combined
    index, up to and including 'part'.
  */
  ulong index_flags(uint inx, uint part, bool all_parts) const
  {
    return 0;
  }

  /*
    Everything below are methods that we implement in ha_gatherdb.cc.

    Most of these methods are not obligatory, skip them and
    MySQL will treat them as not implemented
  */
  /** @brief
    We implement this in ha_gatherdb.cc; it's a required method.
  */
  int open(const char *name, int mode, uint test_if_locked);    // required

  /** @brief
    We implement this in ha_gatherdb.cc; it's a required method.
  */
  int close(void);                                              // required


  /** @brief
    Unlike index_init(), rnd_init() can be called two consecutive times
    without rnd_end() in between (it only makes sense if scan=1). In this
    case, the second call should prepare for the new table scan (e.g if
    rnd_init() allocates the cursor, the second call should position the
    cursor to the start of the table; no need to deallocate and allocate
    it again. This is a required method.
  */

	int rnd_init(bool scan);                                      //required
	int rnd_next(uchar *buf);                                     ///< required
	int rnd_pos(uchar *buf, uchar *pos);                          ///< required
	void position(const uchar *record);                           ///< required
	int info(uint);                                               ///< required
	int external_lock(THD *thd, int lock_type);                   ///< required

	int create(const char *name, TABLE *form,
				HA_CREATE_INFO *create_info);                      ///< required

	THR_LOCK_DATA **store_lock(THD *thd, THR_LOCK_DATA **to,
								enum thr_lock_type lock_type);     ///< required
};
