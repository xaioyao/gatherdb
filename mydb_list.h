#include "my_global.h"
#include "typelib.h"

struct mydblist_node
{
  mydblist_node *next;
  void *info;
  mydblist_node()					/* For end_of_list */
  {
    info= 0;
    next= this;
  }
};

template <class T> class mydb_list
{
private:
	mydblist_node *first,**last;
public:
	int elements;
	mydb_list(){elements=0; first= NULL; last=&first;};
	mydblist_node *first_node()
	{
		return first;
	};
	bool push_back(T *a)
	{
		if(elements==0)
		{
			mydblist_node node;
			node.info=(void *)a;
			first=&node;
			last=&first;
		}
		else
		{
			mydblist_node node;
			node.info=(void *)a;
			*last=(&node);
		}
		elements++;
		return true;
	};
};