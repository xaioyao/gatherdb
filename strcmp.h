#include "my_global.h" 

static bool mydb_strcmp(const char* a,const char *b,int len);
static bool mydb_strcpy(char *dst,const char *src,int len);
static int mydb_strreplace(char *output,const char *src,const char *substr);

static bool mydb_strcmp(const char* a,const char *b,int len)
{
	int idx=0;
	while(idx<len)
	{
		if(*a!=*b) return false;
		a++;b++;idx++;
		if(*a=='\0'||*b=='\0')break;
	}
	return true;
}

static bool mydb_strcpy(char *dst,const char *src,int len)
{
	dst=(char *)malloc(len+1);
	dst[len]='\0';
	memcpy(dst,src,len);
	return true;
}


static int mydb_strreplace(char *output,char *src,char *orgstr,char *newsstr)
{
	if(src==NULL||orgstr==NULL||output==NULL||newsstr==NULL){return -1;}
	char *p,*tmpnew;
	p=strstr(src,orgstr);
	if(p==NULL) return -1;
	tmpnew=(char *)malloc(strlen(src)+1);
	strcpy(tmpnew,src);
	while(p=strstr(tmpnew,orgstr))
	{
		strncat(output,tmpnew,p-tmpnew);
		strncat(output,newsstr,strlen(newsstr));
		tmpnew=p+strlen(orgstr);
	}
	strncat(output,tmpnew,strlen(tmpnew));
}

