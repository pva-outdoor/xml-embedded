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
#include <algorithm>
using namespace std;


enum
  {
    token_hashtab_size = 5051
  };


struct token_info_t
{
  const char *str;
  unsigned next;
  bool is_used;
};




xml_token_t
token_hashtab[token_hashtab_size];

vector<token_info_t> tokens;

int global_is_verbose = true;

int extra_messages_allowed()
{
  return true;
}

xml_token_t
xml_token_by_name(const char *str, unsigned opt_hash)
{
  if (tokens.empty())
    fill((token_hashtab + 0), (token_hashtab + token_hashtab_size), not_a_token);

  if (opt_hash == 0)
    {
      for(const unsigned char *s = (const unsigned char *)str; *s; ++s)
	opt_hash = 33*opt_hash + (*s);
    }
  
  xml_token_t *loc = token_hashtab + (opt_hash % token_hashtab_size);
  xml_token_t t;
  
  for(t = *loc; t != not_a_token; t = tokens[t].next)
    {
      if (0 == strcmp(str, tokens[t].str))
	return t;
    }

  token_info_t token1;
  token1.str = strdup(str);
  token1.next = *loc;
  *loc = tokens.size();
  token1.is_used = false;
  tokens.push_back(token1);
  return *loc;
}


const char *
xml_token_name(xml_token_t t)
{
  if (t < tokens.size())
    {
      token_info_t &info = tokens[t];
      info.is_used = true;
      return info.str;
    }
  return  "<unknown>";
}



xml_token_t t_STRING = xml_token_by_name("<string>", 0);
xml_token_t t_NUMBER = xml_token_by_name("<number>", 0);
xml_token_t t_ID = xml_token_by_name("ID", 0);
xml_token_t t_string = xml_token_by_name("string", 0);
xml_token_t t_type = xml_token_by_name("type", 0);
xml_token_t t_anyType = xml_token_by_name("anyType", 0);



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

typedef vector<xml_token_t> member_set_t;
typedef map<member_set_t, kind_t> kinds_t;

struct mined_info1_t
{
  member_set_t members;
  xml_token_t type;
  pair<kinds_t::iterator, bool> kind;
  bool is_item;
  bool is_number;
  bool is_string;
};

typedef map<xml_token_t, mined_info1_t> mined_info_t;

mined_info_t mined_info;

void
add_member(mined_info1_t &info, xml_token_t member)
{
  if (find(info.members.begin(),
	   info.members.end(), member) ==
      info.members.end())
    info.members.push_back(member);
}

void
add_mined_item(xml_token_t tag_token,
		    xml_token_t val_token, const char *text)
{
  mined_info1_t &info = mined_info[tag_token];
  add_member(info, val_token);
  
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
	  init_read_xml(X, io, fname);

	  while (!X->eof)
	    {
	      xml_node_type_t tag_type = bump_xml_node(X);
	      if (tag_type == xml_node_open)
		{
		  if (X->attrs_size)
		    {
		      xml_attr_t *a, *ea;
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
		      if (tag_type == xml_node_open)
			{
			  if (!my_stack.empty())
			    add_member(mined_info[my_stack.back()], tag);
			  my_stack.push_back(tag);
			}		    
		    }
		}
	      else if (tag_type == xml_node_text)
		{
		  // add tag text
		  if (X->stack_size)
		    {
		      add_mined_item(X->stack[X->stack_size - 1].id_token,
				     xml_token_by_name(X->text, X->text_hash),
				     X->text);
		    }
		}
	      else if (tag_type == xml_node_close)
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

  pair<member_set_t, kind_t> kind1;
  map<member_set_t, kind_t> kinds;

  // assign member kind
  for(mined_info_t::iterator
	a1 = mined_info.begin(), a2 = mined_info.end();
      a1 != a2; ++a1)
    {
      kind1.first = a1->second.members;
      kind1.second.same_as = a1->first;

      if (a1->second.is_string)
	kind1.second.type = kind_string;
      else if (a1->second.is_number)
	kind1.second.type = kind_number;
      else if (a1->second.is_item)
	kind1.second.type = kind_enum;
      else 
	kind1.second.type = kind_struct;
      
      sort(kind1.first.begin(),
	   kind1.first.end());
      
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
	      printf("static int\n");
	      printf("struct_%s(struct converter_t *X, "
		     "xml_token_t tag_token,\n", xml_token_name(a1->first));
	      printf("     xml_token_t val_token, const char *text)\n");
	      printf("{\n");
	      printf("  switch(tag_token)\n");
	      printf("    {\n");
	      for(member_set_t::iterator
		    b1 = a1->second.members.begin(),
		    b2 = a1->second.members.end();
		  b1 != b2; ++b1)
		{
		  mined_info_t::iterator node = mined_info.find(*b1);
		  const char *name = xml_token_name(*b1);
		  if (node != mined_info.end())
		    {
		      const char *ref_type =
			xml_token_name(node->second.kind.first->second.same_as);
		      switch(node->second.kind.first->second.type)
			{
			case kind_number:
			  printf("    case t_%s:\n"
				 "      put_u16(X, 0x0001, strtol(text));\n"
				 "      break;\n"
				 "\n", name);
			  break;
			case kind_string:
			  printf("    case t_%s:\n"
				 "      put_str(X, 0x0001, text);\n"
				 "      break;\n"
				 "\n", name);
			  break;
			case kind_enum:
			  printf("    case t_%s:\n"
				 "      put_u8(X, 0x0001, enum_%s(val_token));\n"
				 "      break;\n"
				 "\n", name, ref_type);
			  break;
			default:
			  printf("    case t_%s:\n"
				 "      convert(X, 0x8000, struct_%s);\n"
				 "      break;\n"
				 "\n", name, ref_type);
			  break;
			}
		    }
		  else
		    printf("    /* unknown type \"%s\" */\n", name);
		}
	      
	      printf("    default: return -1;\n");
	      printf("    }\n");
	      printf("   return 0;\n");
	      printf("}\n");
	      printf("\n");
	      printf("\n");
	    }
	  else if (kind == kind_enum)
	    {
	      printf("static unsigned char\n");
	      printf("enum_%s(xml_token_t val_token)\n", xml_token_name(a1->first));
	      printf("{\n");
	      printf("  switch(val_token)\n");
	      printf("    {\n");

	      unsigned num = 0;
	      for(member_set_t::iterator
		    b1 = a1->second.members.begin(),
		    b2 = a1->second.members.end();
		  b1 != b2; ++b1)
		{
		  const char *name = xml_token_name(*b1);
		  printf("    case t_%s: return %u;\n", name, num++);		 
		}
	      
	      printf("    default: return 255;\n");
	      printf("    }\n");
	      printf("}\n");
	      printf("\n");
	      printf("\n");
	    }
	}
    }
  

  printf("Used tokens:\n");
  // collect hashtab statistics
  unsigned hash_fill = 0;
  unsigned hash_worst = 0;
  for(unsigned n=0; n<token_hashtab_size; ++n)
    {
      unsigned s = token_hashtab[n];
      if (s)
	{
	  ++hash_fill;
	  unsigned this_case;
	  for (this_case = 0; s; s = tokens[s - 1].next)
	    {
	      if (tokens[s - 1].is_used)
		printf("  %s\n", tokens[s - 1].str);
	      ++this_case;
	    }

	  if (hash_worst < this_case)
	    hash_worst = this_case;
	}
    }
  
  fprintf(stderr, "\n");
  fprintf(stderr, "processed %u files\n", (nfiles));
  fprintf(stderr, "finished with %u errors\n", (errors));
  fprintf(stderr, "sizeof(read_xml_t) = %u\n", sizeof(read_xml_t));
  fprintf(stderr, "symbols_size = %u\n", tokens.size());
  fprintf(stderr, "used_bindings = %u\n", used_bindings);
  fprintf(stderr, "used_binding_text = %u\n", used_binding_text);
  fprintf(stderr, "used_text = %u\n", used_text);
  fprintf(stderr, "used_attrs = %u\n", used_attrs);
  fprintf(stderr, "used_stack = %u\n", used_stack);

  if (hash_fill)
    {
      fprintf(stderr, "hash_size = %u\n", token_hashtab_size);
      fprintf(stderr, "hash_fill = %u%%\n",
	      100*hash_fill/token_hashtab_size);
      fprintf(stderr, "hash_avg_case = %u\n",
	      (tokens.size() + hash_fill/2)/hash_fill);
      fprintf(stderr, "hash_worst_case = %u\n", hash_worst);
    }
        
  return 0;
}
