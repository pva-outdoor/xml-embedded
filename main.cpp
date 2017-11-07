extern "C" {
#include "read_xml.h"
}
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <map>
#include <vector>
using namespace std;


enum
  {
    symbol_hashtab_size = 5051
  };


struct pair_t
{
  const char *str;
  unsigned next;
  bool is_tag;
};

unsigned
symbol_hashtab[symbol_hashtab_size];

struct pair_t *
symbols;

unsigned
symbols_size;

unsigned
symbols_cap;

xml_token_t xml_token_by_str(const char *str, unsigned str_hash)
{
  unsigned *loc = symbol_hashtab + (str_hash % symbol_hashtab_size);
  unsigned s;
  for(s = *loc; s != 0; s = symbols[s-1].next)
    {
      if (0 == strcmp(str, symbols[s-1].str))
	return s;
    }

  if (symbols_cap == symbols_size)
    {
      unsigned new_cap = symbols_cap ? 2*symbols_cap : 256;
      void *mem = realloc(symbols, sizeof(*symbols)*new_cap);
      if (mem)
	{
	  symbols_cap = new_cap;
	  symbols = (pair_t*)mem;
	}
      else
	return 0;
    }

  symbols[symbols_size].str = strdup(str);
  symbols[symbols_size].next = *loc;
  return *loc = ++symbols_size;
}


xml_token_t xml_token_by_str_(const char *str)
{
  unsigned hash = 0;
  for(const unsigned char *s = (const unsigned char *)str; *s; ++s)
    hash = 33*hash + (*s);

  return xml_token_by_str(str, hash);
}


const char *
xml_token_name(xml_token_t s)
{
  return (unsigned)(s - 1) < symbols_size ? symbols[s - 1].str : "";
}



xml_token_t t_STRING = xml_token_by_str_("<string>");
xml_token_t t_NUMBER = xml_token_by_str_("<number>");
xml_token_t t_ID = xml_token_by_str_("ID");
xml_token_t t_string = xml_token_by_str_("string");
xml_token_t t_type = xml_token_by_str_("type");
xml_token_t t_anyType = xml_token_by_str_("anyType");



bool is_number(const char *s)
{
  for(; *s; ++s)
    if (!isdigit(*s))
      return false;

  return true;
}


bool is_id(const char *s)
{
  for(; *s; ++s)
    if ( !(isalnum(*s) || *s=='_') )
      return false;

  return true;
}


enum type_kind_t
  {
    kind_struct,
    kind_enum,
    kind_string,
    kind_number
  };

struct kind_t
{
  xml_token_t same_as;
  type_kind_t type;
};

typedef map<xml_token_t, unsigned> node_info_t;
typedef vector<xml_token_t> member_set_t;
typedef map<member_set_t, kind_t> kinds_t;

struct mined_info1_t
{
  node_info_t members;
  xml_token_t type;
  pair<kinds_t::iterator, bool> kind;
  bool is_item;
  bool is_number;
  bool is_string;
};

typedef map<xml_token_t, mined_info1_t> mined_info_t;

mined_info_t mined_info;

void
add_mined_item(xml_token_t tag_token,
		    xml_token_t val_token, const char *text)
{
  mined_info1_t &info = mined_info[tag_token];
  ++info.members[val_token];
  
  if (is_number(text))
    {
      val_token = t_NUMBER;
      info.is_number = true;
    }
  else if (!is_id(text))
    {
      val_token = t_STRING;
      info.is_string = true;
    }
  
  ++info.members[val_token];
  info.is_item = true;  
}

int
main(int argc, char *argv[])
{
  char fname[256];
  unsigned nfiles;
  unsigned errors = 0;

  unsigned used_bindings = 0;
  unsigned used_binding_text = 0;
  unsigned used_text = 0;
  unsigned used_attrs = 0;
  unsigned used_stack = 0;

  vector<xml_token_t> my_stack;

  for (nfiles=0; fgets(fname, sizeof(fname), stdin); ++nfiles)
    {
      fname[strcspn(fname, "\n")] = 0;      
      int io = open(fname, O_RDONLY);

      if (io != -1)
	{
	  struct read_xml_t X[1];
	  read_xml_init(X, io, fname);

	  while (!X->eof)
	    {
	      tag_type_t tag_type = read_xml_bump(X);
	      if (tag_type <= tag_type_closed)
		{
		  if (X->attrs_size)
		    {
		      attr_t *a, *ea;
		      unsigned tag = X->attrs[0].id_token;
		      // use "type = ..." it instead of tag name
		      for(a = X->attrs + 1,
			    ea = X->attrs + X->attrs_size; a < ea; ++a)
			{
			  if (a->id_token == t_type)
			    {
			      tag = a->val_token;
			      break;
			    }
			}
		      // add attribute values
		      for(a = X->attrs + 1,
			    ea = X->attrs + X->attrs_size; a < ea; ++a)
			{
			  if (a->id_token != t_type)
			    add_mined_item(tag, a->id_token,
					   X->text + a->val_index);
			}
		      // add subtag
		      if (tag_type == tag_type_open)
			{
			  if (!my_stack.empty())
			    ++mined_info[my_stack.back()].members[tag];
			  my_stack.push_back(tag);
			}		    
		    }
		}
	      else if (tag_type == tag_type_text)
		{
		  // add tag text
		  if (X->stack_size)
		    {
		      add_mined_item(X->stack[X->stack_size - 1].id_token,
				     xml_token_by_str(X->text, X->text_hash),
				     X->text);
		    }
		}
	      else if (tag_type == tag_type_closing)
		{
		  if (!my_stack.empty())
		    my_stack.pop_back();
		}
		
	      
	      if (used_attrs < X->attrs_size)
		used_attrs = X->attrs_size;
	      if (used_bindings < X->bound_size)
		used_bindings = X->bound_size;
	      if (used_binding_text < X->bound_text_size)
		used_binding_text = X->bound_text_size;
	      if (used_text < X->text_size)
		used_text = X->text_size;
	      if (used_stack < X->stack_size)
		used_stack = X->stack_size;
	    }

	  errors += X->errors;
	  if (errors != 0)
	    break;
	}
      else
	{
	  ++errors;
	  fprintf(stderr, "%s \"%s\" %s\n",
		  "file", (fname), "not found");
	  return 1;
	}
    }

  // collect hashtab statistics
  unsigned hash_fill = 0;
  unsigned hash_worst = 0;
  for(unsigned n=0; n<symbol_hashtab_size; ++n)
    {
      unsigned s = symbol_hashtab[n];
      if (s)
	{
	  ++hash_fill;
	  unsigned this_case;
	  for (this_case = 0; s; s = symbols[s - 1].next)
	    {
	      ++this_case;
	    }

	  if (hash_worst < this_case)
	    hash_worst = this_case;
	}
    }

  pair<member_set_t, kind_t> kind1;
  map<member_set_t, kind_t> kinds;

  // assign member kind
  for(mined_info_t::iterator
	a1 = mined_info.begin(), a2 = mined_info.end();
      a1 != a2; ++a1)
    {
      kind1.first.clear();
      kind1.second.same_as = a1->first;
      
      for(node_info_t::iterator
	    b1 = a1->second.members.begin(), b2 = a1->second.members.end();
	  b1 != b2; ++b1)
	kind1.first.push_back(b1->first);

      if (a1->second.is_string)
	kind1.second.type = kind_string;
      else if (a1->second.is_number)
	kind1.second.type = kind_number;
      else if (a1->second.is_item)
	kind1.second.type = kind_enum;
      else 
	kind1.second.type = kind_struct;
      
      a1->second.kind = kinds.insert(kind1);
    }

  // render
  for(mined_info_t::iterator
	a1 = mined_info.begin(), a2 = mined_info.end();
      a1 != a2; ++a1)
    {
      type_kind_t kind = a1->second.kind.first->second.type;
      if (a1->second.kind.second)
	{
	  if (kind == kind_struct)
	    {
	      printf("items_%s = \n", xml_token_name(a1->first));
	      printf("  {\n");

	      for(node_info_t::iterator
		    b1 = a1->second.members.begin(),
		    b2 = a1->second.members.end();
		  b1 != b2; ++b1)
		{
		  mined_info_t::iterator node = mined_info.find(b1->first);
		  const char *name = xml_token_name(b1->first);
		  printf("    {\"%s\" tag_%s, ", name, name);
		  if (node != mined_info.end())
		    {
		      const char *ref_type =
			xml_token_name(node->second.kind.first->second.same_as);
		      switch(node->second.kind.first->second.type)
			{
			case kind_number:
			  printf("type_NUMBER"); break;
			case kind_string:
			  printf("type_STRING"); break;
			case kind_enum:
			  printf("/*enum*/type_%s", ref_type); break;
			default:
			  printf("/*struct*/type_%s", ref_type); break;
			}
		    }
		  else
		    printf("type_unknown");
	      
		  printf("},\n");
		}
	      printf("  };\n\n");
	    }
	  else if (kind == kind_enum)
	    {
	      printf("items_%s = \n", xml_token_name(a1->first));
	      printf("  {\n");

	      for(node_info_t::iterator
		    b1 = a1->second.members.begin(),
		    b2 = a1->second.members.end();
		  b1 != b2; ++b1)
		{
		  const char *name = xml_token_name(b1->first);
		  printf("    val_%s,\n", name);
		}
	      
	      printf("  };\n\n");
	    }
	}
    }
  

  fprintf(stderr, "\n");
  fprintf(stderr, "processed %u files\n", (nfiles));
  fprintf(stderr, "finished with %u errors\n", (errors));
  fprintf(stderr, "sizeof(read_xml_t) = %u\n", sizeof(read_xml_t));
  fprintf(stderr, "symbols_size = %u\n", symbols_size);
  fprintf(stderr, "used_bindings = %u\n", used_bindings);
  fprintf(stderr, "used_binding_text = %u\n", used_binding_text);
  fprintf(stderr, "used_text = %u\n", used_text);
  fprintf(stderr, "used_attrs = %u\n", used_attrs);
  fprintf(stderr, "used_stack = %u\n", used_stack);

  if (hash_fill)
    {
      fprintf(stderr, "hash_size = %u\n", symbol_hashtab_size);
      fprintf(stderr, "hash_fill = %u%%\n",
	      100*hash_fill/symbol_hashtab_size);
      fprintf(stderr, "hash_avg_case = %u\n",
	      (symbols_size + hash_fill/2)/hash_fill);
      fprintf(stderr, "hash_worst_case = %u\n", hash_worst);
    }
        
  return 0;
}
