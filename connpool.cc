#define MYSQL_SERVER 1
#include "sql_class.h"           // SSV
#include "ha_gatherdb.h"
#include "mysql.h"
#include "sql_select.h"

int connpool::init()
{
	DBUG_ENTER("connpool::init");
	instances_count=0;
	instancepool = new connect_pool();
	pools = new connect_pool();
	DBUG_RETURN(0);
}

MYSQL_CONNECT *connpool::fetchone(MYSQL_INSTANCE *instance)
{
	DBUG_ENTER("connpool::fetchone");
	//加锁
	instancepool=pools;
	int idx1=0;
	do
	{
		idx1++;
		if(instancepool->param->instance->server==instance->server&&instancepool->param->instance->sport==instance->sport)
		{
			for(uint idx=0;idx<MAX_CONNECTIONS;idx++)
			{
				if(instancepool->connections[idx].isalive&&!instancepool->connections[idx].isused)
				{
					return &instancepool->connections[idx];
				}
			}
		}
		instancepool=pools->next;
	}while(idx1<instances_count);
	//解锁
	DBUG_RETURN(NULL);
}

void connpool::releaseone(MYSQL_CONNECT *connection)
{
	DBUG_ENTER("connpool::releaseone");
	//加锁
	connection->isused=false;
	//解锁		
}

int connpool::dispose()
{
	DBUG_ENTER("connpool::dispose");
	//释放缓冲池
	DBUG_RETURN(0);	
}

int connpool::realiveconnect(MYSQL_CONNECT *connection,CONNECT_PARAM *param)
{
	DBUG_ENTER("connpool::realiveconnect");
	if(mysql_real_connect(connection->mysql,
		param->instance->server,
		param->user,
		param->password,
		param->schema,
		param->instance->sport,
		MYSQL_UNIX_ADDR,0))
	{
		connection->isalive=true;
		DBUG_RETURN(0);	
	}	
	DBUG_RETURN(1);	
}

int connpool::pool_real_connect()
{
	DBUG_ENTER("connpool::pool_real_connect");
	instancepool=pools;
	int idx1=0;
	do
	{
		idx1++;
		for(uint idx=0;idx<MAX_CONNECTIONS;idx++)
		{
			if(instancepool->connections[idx].isalive) continue;
			if(!mysql_real_connect(instancepool->connections[idx].mysql,
				instancepool->param->instance->server,
				instancepool->param->user,
				instancepool->param->password,
				instancepool->param->schema,
				instancepool->param->instance->sport,
				MYSQL_UNIX_ADDR,0))
			{
				instancepool->connections[idx].isalive=false;
				continue;
			}
			instancepool->connections[idx].isalive=true;
		}
		instancepool=pools->next;
	}while(idx1<instances_count);
	DBUG_RETURN(0);
}


int connpool::_init_connect()
{
	DBUG_ENTER("connpool::_init_connect");
	instancepool=pools;
	int idx1=0;
	do
	{
		idx1++;
		instancepool->free_length=MAX_CONNECTIONS;
		for(uint idx=0;idx<MAX_CONNECTIONS;idx++){
			instancepool->connections[idx].mysql=mysql_init(NULL);
			instancepool->connections[idx].isused=false;
			instancepool->connections[idx].isalive=false;
		}
		instancepool=pools->next;
	}while(idx1<instances_count);
	DBUG_RETURN(0);	
}

int connpool::init_instances()
{
	MYSQL_FILE *mf;
	char buff[100],*ptr,*orgptr;
	char *ininame="D:\\Soft\\MySqlImprove\\mysql-5.5.36\\sql\\data\\gather.ini"; 
	if(!(mf= mysql_file_fopen(0,ininame,  O_RDONLY, MYF(0))))
	{return -1;	}
	int error,spacecount;
	instances_count=0;
	
	while (mysql_file_fgets(buff, sizeof(buff) - 1, mf))
	{
		orgptr=buff;
		instances_count++;
		if(instances_count==1)
		{
			instancepool=pools;
		}
		else
		{
			instancepool->next = new connect_pool();
			instancepool=instancepool->next;
		}
		spacecount=0;
		for(ptr=buff;ptr<buff+strlen(buff);ptr++)
		{
			if(*ptr=='\r'||*ptr=='\n') break;
			if(*ptr==SPACECHAR) 
			{
				spacecount++;
				switch(spacecount)
				{
					case 1:	
						{
							instancepool->param->instance->server=(char *)my_malloc(ptr-orgptr+1,MYF(0));
							memcpy(instancepool->param->instance->server,orgptr,ptr-orgptr);
							instancepool->param->instance->server[ptr-orgptr]=0;
							break;
						}
					case 2: instancepool->param->instance->sport=(ulong) my_strtoll10(orgptr, (char**) 0,
														  &error);
							break;
					case 3: 
						{
							instancepool->param->user=(char *)my_malloc(ptr-orgptr+1,MYF(0));
							memcpy(instancepool->param->user,orgptr,ptr-orgptr);
							instancepool->param->user[ptr-orgptr]=0;
							break;
						}
					case 4: 
						{
							instancepool->param->password=(char *)my_malloc(ptr-orgptr+1,MYF(0));
							memcpy(instancepool->param->password,orgptr,ptr-orgptr);
							instancepool->param->password[ptr-orgptr]=0;
							break;
						}
					case 5: 
						{
							instancepool->param->schema=(char *)my_malloc(ptr-orgptr+1,MYF(0));
							memcpy(instancepool->param->schema,orgptr,ptr-orgptr);
							instancepool->param->schema[ptr-orgptr]=0;
							break;
						}
					case 6: 
						{
							instancepool->param->table_name=(char *)my_malloc(ptr-orgptr+1,MYF(0));
							memcpy(instancepool->param->table_name,orgptr,ptr-orgptr);
							instancepool->param->table_name[ptr-orgptr]=0;
							break;
						}
				}
				orgptr=ptr+1;
			}
		}
	}
	mysql_file_fclose(mf, MYF(0));
	return 0;
}
mydb_value_list::mydb_value_list(mydb_value_list *mvl)
{
	if(mvl->value!=NULL) 
	{
		value=(char *)my_malloc(strlen(mvl->value)+1,MYF(0));
		strcpy(value,mvl->value);
		value_type=mvl->value_type;
	}
	else mydb_value_list();

}

mydb_value_list::mydb_value_list(Item *item)
{
	if(item)
	{
		value=(char *)my_malloc(strlen(item->name)+1,MYF(0));
		strcpy(value,item->name);
		value_type=item->type();
	}
	else
		mydb_value_list();
}

mydb_field_cond::mydb_field_cond(mydb_field_cond *mfc):s_min(&mfc->s_min),s_max(&mfc->s_max)
{
	field_name=new mydb_field_detail();
	field_name->f_name=(char *)my_malloc(strlen(mfc->field_name->f_name)+1,MYF(0));
	strcpy(field_name->f_name,mfc->field_name->f_name);
	field_name->field_table.table_alias=(char *)my_malloc(strlen(mfc->field_name->field_table.table_alias)+1,MYF(0));
	strcpy(field_name->field_table.table_alias,mfc->field_name->field_table.table_alias);
	field_name->field_table.schema_name=(char *)my_malloc(strlen(mfc->field_name->field_table.schema_name)+1,MYF(0));
	strcpy(field_name->field_table.schema_name,mfc->field_name->field_table.schema_name);
	field_name->field_table.table_name=(char *)my_malloc(strlen(mfc->field_name->field_table.table_name)+1,MYF(0));
	strcpy(field_name->field_table.table_name,mfc->field_name->field_table.table_name);
	field_name->field_table.is_alias_used = mfc->field_name->field_table.is_alias_used;
	isRange=mfc->isRange;
	List_iterator<mydb_value_list> li(mfc->values);
	mydb_value_list *mvl;
	while((mvl=li++))
	{
		mydb_value_list *ul=new mydb_value_list(mvl);
		values.push_back(ul);
	}
}

mydb_field_cond::mydb_field_cond():s_min(),s_max()
{
	isRange=false;
}

mydb_field_cond::mydb_field_cond(Item *field,Item *value):s_min(),s_max()
{
	field_name=new mydb_field_detail(field);
	isRange=false;
	addvalue(value);
}

mydb_field_cond::mydb_field_cond(Item *field,Item *minvalue,Item *maxvalue):s_min(minvalue),s_max(maxvalue)
{
	field_name=new mydb_field_detail(field);
	isRange=true;
}

void mydb_field_cond::setfield(Item *field)
{
	field_name=new mydb_field_detail(field);
}

void mydb_field_cond::addvalue(Item *value)
{
	mydb_value_list *mvl=new mydb_value_list(value);
	values.push_back(mvl);
}

mydb_field_cond::mydb_field_cond(Item *multilist):s_min(),s_max()
{
	Item *ul=multilist->next;
	_nodes=0;
	do
	{
		_nodes++;
		if(ul->type()==Item::FIELD_ITEM)
		{
			setfield(ul);
			break;
		}
		else
		{
			addvalue(ul);
		}
	}while(ul=ul->next);
	isRange=false;
}

void list_sql_tree::_move_node(Item **conds,int nodes)
{
	for(uint idx=0;idx<nodes;idx++)
	{
		if((*conds)->next)
		{
			(*conds)=(*conds)->next;
		}
		else break;
	}
}

void list_sql_tree::_add_fields_2(Item *field,Item *value)
{
	mydb_field_cond *mfc=new mydb_field_cond(field,value);
	fieldlist.push_back(mfc);
}
void list_sql_tree::_add_fields_3(Item *field,Item *minvalue,Item *maxvalue)
{
	mydb_field_cond *mfc=new mydb_field_cond(field,minvalue,maxvalue);
	fieldlist.push_back(mfc);
}
int list_sql_tree::_add_fields_n(Item *multilist)
{
	mydb_field_cond *mfc=new mydb_field_cond(multilist);
	fieldlist.push_back(mfc);
	return mfc->_nodes;
}

int list_sql_tree::_list_sql_table_list()
{
	SELECT_LEX select_lex=list_thd->lex->select_lex;
	COND *list_where=select_lex.where; 
	SQL_I_List<TABLE_LIST> table_list=select_lex.table_list;
	TABLE_LIST *lst_table=table_list.first;
	if(!lst_table) return -1;
	do
	{
		mydb_schema_table *schema_table=new mydb_schema_table();
		schema_table->schema_name=(char *)my_malloc(strlen(lst_table->db)+1,MYF(0));
		strcpy(schema_table->schema_name,lst_table->db);
		schema_table->table_name=(char *)my_malloc(strlen(lst_table->table_name)+1,MYF(0));
		strcpy(schema_table->table_name,lst_table->table_name);
		schema_table->table_alias=(char *)my_malloc(strlen(lst_table->alias)+1,MYF(0));
		strcpy(schema_table->table_alias,lst_table->alias);

		schema_table->is_alias_used=lst_table->table->alias_name_used;
		tablelist.push_back(schema_table);
	}while(lst_table=lst_table->next_leaf);
	return 0;
}

int list_sql_tree::_list_lex_where_tree(COND *cond)
{
	if(!cond)	return -1;
	if(cond->type()==Item::FUNC_ITEM)
	{
		//根据操作符目数，移动链表
		switch(((Item_func*) cond)->functype())
		{
		case Item_func::NOT_FUNC:break;
		case Item_func::EQ_FUNC://二元操作符
		case Item_func::EQUAL_FUNC:
		case Item_func::NE_FUNC:
		case Item_func::LT_FUNC:
		case Item_func::LE_FUNC:
		case Item_func::GE_FUNC:
		case Item_func::GT_FUNC:
		case Item_func::FT_FUNC:
		case Item_func::LIKE_FUNC:
			{
				Item *left_item=((Item_func*) cond)->arguments()[0];
				Item *right_item= ((Item_func*) cond)->arguments()[1];				
				if(left_item->type() == Item::FIELD_ITEM ^ right_item->type() == Item::FIELD_ITEM)
				{
					if(left_item->type() == Item::FIELD_ITEM)
					{
						_add_fields_2(left_item,right_item);
					}
					else
					{
						_add_fields_2(right_item,left_item);
					}
				}
				else
				{}
				break;
			}
		case Item_func::BETWEEN://三元操作符
			{
				Item *left_item= ((Item_func*) cond)->arguments()[0];
				Item *mid_item= ((Item_func*) cond)->arguments()[1];
				Item *right_item= ((Item_func*) cond)->arguments()[2];
				if(left_item->type() == Item::FIELD_ITEM)
				{
					_add_fields_3(left_item,mid_item,right_item);
				}
				else
				{}
				break;
			}
		case Item_func::IN_FUNC:
			{
				_add_fields_n(cond);
				break;//多元操作符	
			}
		}
	}
	return 0;
}

int list_sql_tree::list_lex_tree()
{
	if(list_thd->lex->select_lex.join==0) return -1;
	JOIN *join=list_thd->lex->select_lex.join;
	_list_sql_table_list();
	return _list_lex_tree(join->conds);
}

int list_sql_tree::_list_lex_tree(COND *conds)
{	
	if (conds->type() == Item::COND_ITEM)
	{
		List_iterator<Item> li(*((Item_cond*) conds)->argument_list());
		Item *item;
		while ((item=li++))
		{
			_list_lex_tree(item);
		}
	}
	else
	{
		_list_lex_where_tree(conds);
	}
	return 0;
}

static bool mfc_field_cmp(mydb_field_detail *f1,mydb_field_detail *f2)
{
	return mydb_strcmp(f1->f_name,f2->f_name,strlen(f1->f_name))
		||mydb_strcmp(f1->field_table.schema_name,f2->field_table.schema_name,strlen(f1->field_table.schema_name))
		||mydb_strcmp(f1->field_table.table_alias,f2->field_table.table_alias,strlen(f1->field_table.table_alias))
		||mydb_strcmp(f1->field_table.table_name,f2->field_table.table_name,strlen(f1->field_table.table_name));
}

mydb_field_cond* list_sql_tree::_is_filed_in_list(mydb_field_cond *fl)
{
	if(mergelist.elements==0) return NULL;
	list_node *ul=mergelist.first_node();
	mydb_field_cond *mfc;
	do
	{
		mfc=(mydb_field_cond *)ul->info;
		if(mfc_field_cmp(mfc->field_name,fl->field_name))
		{
			return mfc;
		}
		ul=ul->next;
	}while(ul->info);
	return NULL;
}

void list_sql_tree::_merge_field_cond(mydb_field_cond* mfiled,mydb_field_cond* filed)
{
	int error=0;
	if(filed->isRange)
	{
		if(filed->s_max.value_type==Item::INT_ITEM)
		{
			long f_max=my_strtoll10(filed->s_max.value, (char**) 0, &error);
			long m_max=my_strtoll10(mfiled->s_max.value, (char**) 0, &error);
			char s_max[32];
			if(f_max>m_max)
			{
				int10_to_str(f_max,s_max,0);
				mfiled->s_max.value=(char *)my_malloc(strlen(s_max)+1,MYF(0));
				strcpy(mfiled->s_max.value,s_max);
			}
		}
		if(filed->s_min.value_type==Item::INT_ITEM)
		{
			long f_min=my_strtoll10(filed->s_min.value, (char**) 0, &error);
			long m_min=my_strtoll10(mfiled->s_min.value, (char**) 0, &error);
			char s_min[32];
			if(f_min<m_min)
			{
				int10_to_str(f_min,s_min,0);
				mfiled->s_min.value=(char *)my_malloc(strlen(s_min)+1,MYF(0));
				strcpy(mfiled->s_min.value,s_min);
			}		
		}
	}
	else
	{
		List_iterator<mydb_value_list> li(filed->values);
		mydb_value_list *vl;
		bool bflag;
		while((vl=li++))
		{
			bflag=false;
			List_iterator<mydb_value_list> ul(mfiled->values);
			mydb_value_list *uvl;
			while((uvl=ul++))
			{
				if(mydb_strcmp(uvl->value,vl->value,strlen(vl->value)))
				{
					bflag=true;
				}
			}
			if(!bflag)
			{
				mydb_value_list *mvl=new   mydb_value_list(vl);//(&mem_root)
				mfiled->values.push_back(mvl);
			}
		}
	}
}

int list_sql_tree::list_lex_merge()
{
	List_iterator<mydb_field_cond> li(fieldlist);
	mydb_field_cond *ul,*fl;
	while(ul=li++)
	{    
		if((fl=_is_filed_in_list(ul)))
		{
			_merge_field_cond(fl,ul);
		}
		else
		{
			mydb_field_cond *ll=new mydb_field_cond(ul);// (&mem_root)
			mergelist.push_back(ll);
		}		
	}
	return 0;
}


int shard_table_map::init()
{
	//获取shard表的所有信息
	if(mysql==NULL)
		mysql=mysql_init(NULL);
	if(mysql==NULL)
	{
		return 1;
	}
	if(!mysql_real_connect(mysql,sharding_instance.server,
								 sharding_instance_param.user,
								 sharding_instance_param.password,
								 sharding_instance_param.schema,
								 sharding_instance.sport,
								 MYSQL_UNIX_ADDR,0))
	{
		return 2;
	}
	char query[100];
	sprintf(query,"select table_name from %s",MYDB_TABLE_MAP);
	mysql_real_query(mysql,query,strlen(query));
	MYSQL_RES *result=mysql_store_result(mysql);
	if(!result)
	{
		goto err;
	}
	MYSQL_ROW row;
	int idx=0;
	while(row=mysql_fetch_row(result))
	{
		char *t=(char *)my_malloc(strlen(row[0])+1,MYF(0));
		strcpy(t,row[0]);
		table_map[idx]=t;
		idx++;
	}
	mysql_close(mysql);	
	return 0;	
err:
	mysql_close(mysql);	
	return -1;
}

bool shard_table_map::table_in_list(const char *table_name)
{
	char **li=table_map;
	char *ul;
	while((ul=*li++))
	{
		if(mydb_strcmp(ul,table_name,strlen(ul)))
		{
			return true;
		}
	}
	return false;
}

int list_sql_tree::_fetch_field_cond(mydb_field_cond *mfc,char *c_cond)
{
	if(mfc==NULL) return NULL;
	if(mfc->isRange)
	{
		sprintf(c_cond,"%s between %s and %s",mfc->field_name->f_name,
			mfc->s_min.value,mfc->s_max.value);
	}
	else
	{
		List_iterator<mydb_value_list> li(mfc->values);
		strcat(c_cond,mfc->field_name->f_name);
		strcat(c_cond," in (");
		mydb_value_list *value;
		value=li++;
		strcat(c_cond,"'");
		strcat(c_cond,value->value);
		strcat(c_cond,"'");
		while((value=li++))
		{
			strcat(c_cond,",");
			strcat(c_cond,"'");
			strcat(c_cond,value->value);
			strcat(c_cond,"'");
		}
		strcat(c_cond,")");
	}
	return 0;
}

int list_sql_tree::_list_field_cond()
{
	list_node *ln=mergelist.first_node();
	mydb_field_cond *ul;
	char *where_c=(char *)my_malloc(MYDB_MAX_SQL_LENGTH,MYF(0));
	*where_c='\0';
	int idx=0;
	do
	{
		ul=(mydb_field_cond *)ln->info;
		if(mydb_strcmp(ul->field_name->f_name,MYDB_TRAIN_MAP_ID,strlen(MYDB_TRAIN_MAP_ID))
			||mydb_strcmp(ul->field_name->f_name,MYDB_PACKAGE_MAP_ID,strlen(MYDB_PACKAGE_MAP_ID)))
		{
			_fetch_field_cond(ul,where_c);
			where_cond.push_back(where_c);
			idx++;
		}
		ln=ln->next;
	}while(ln->info);
	return idx;
}

char *list_sql_tree::_make_where_str()
{
	list_node *ln=where_cond.first_node(); 
	char *result=(char *)my_malloc(MYDB_MAX_SQL_LENGTH,MYF(0));
	*result='\0';
	char *ul;
	int idx=0;
	do
	{
		if(idx>0)strcat(result," and ");
		ul=(char *)ln->info;
		strcat(result,"(");
		strcat(result,ul);
		strcat(result,")");
		idx++;
		ln=ln->next;
	}while(ln->info);
	return result;
}

int list_sql_tree::_make_shard_command(char *result)
{
	_list_field_cond();
	sprintf(result,"select serverip,serverport,shard_schema,shard_prefix from %s where %s",MYDB_TRAIN_MAP,_make_where_str());
	return 0;
}

//查询获取查询列表
int list_sql_tree::get_shard_table_info()
{
	char sql_command[MYDB_MAX_SQL_LENGTH];
	_make_shard_command(sql_command);
	mysql=mysql_init(NULL);

	if(!mysql_real_connect(mysql,sharding_instance.server,
								 sharding_instance_param.user,
								 sharding_instance_param.password,
								 sharding_instance_param.schema,
								 sharding_instance.sport,
								 NULL,0))
	{
		return 2;
	}
	mysql_real_query(mysql,sql_command,strlen(sql_command));
	MYSQL_RES *result=mysql_store_result(mysql);
	if(!result)
	{
		goto err;
	}
	MYSQL_ROW row;
	int error=0;
	while(row=mysql_fetch_row(result))
	{
		CONNECT_PARAM *mcp=(CONNECT_PARAM *)my_malloc(sizeof(CONNECT_PARAM),MYF(0));
		mcp->instance=(MYSQL_INSTANCE *)my_malloc(sizeof(MYSQL_INSTANCE),MYF(0));
		mcp->instance->server=(char *)my_malloc(strlen(row[0])+1,MYF(0));
		strcpy(mcp->instance->server,row[0]);
		mcp->instance->sport=my_strtoll10(row[1], (char**) 0, &error);
		mcp->schema=(char *)my_malloc(strlen(row[2])+1,MYF(0));
		strcpy(mcp->schema,row[2]);
		mcp->table_name=(char *)my_malloc(strlen(row[3])+1,MYF(0));
		strcpy(mcp->table_name,row[3]);
		shard_info.push_back(mcp);
	}
	mysql_free_result(result);
	mysql_close(mysql);	
	return 0;	
err:
	mysql_close(mysql);	
	return -1;	
}

int list_sql_tree::resetup_sql_command(shard_table_map *stm1)
{
	sql_commands=(char **)my_malloc(sizeof(char **),MYF(0));
	char **cursor=sql_commands;
	List_iterator<CONNECT_PARAM> ui(shard_info);
	CONNECT_PARAM *mcp;
	while((mcp=ui++))
	{
		List_iterator<mydb_schema_table> li(tablelist);
		mydb_schema_table *mst;
		char *sql_command=(char *)my_malloc(MYDB_MAX_SQL_LENGTH,MYF(0));
		char sql_tmp[MYDB_MAX_SQL_LENGTH];
		strcpy(sql_command,_query);
		sql_tmp[0]='\0';
		while((mst=li++))
		{
			if(stm1->table_in_list(mst->table_name))
			{
				char fullname[MYDB_MAX_SQL_LENGTH];
				sprintf(fullname,"%s.%s%s",mcp->schema,mcp->table_name,mst->table_name);
				mydb_strreplace(sql_tmp,sql_command,mst->table_name,fullname);
				strcpy(sql_command,sql_tmp);
			}
		}
		*cursor=sql_command;
		cursor++;
	}
	return 0;
}