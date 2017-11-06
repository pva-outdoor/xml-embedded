#include "read_xml.h"
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

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

unsigned symbol_hashtab[symbol_hashtab_size];
struct pair_t *symbols;
unsigned symbols_size;
unsigned symbols_cap;

unsigned str_to_symbol(const char *str, unsigned str_hash)
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
	  symbols = mem;
	}
      else
	return 0;
    }

  symbols[symbols_size].str = strdup(str);
  symbols[symbols_size].next = *loc;
  return *loc = ++symbols_size;
}


const char *
str_from_symbol(unsigned s)
{
  return (unsigned)(s - 1) < symbols_size ? symbols[s - 1].str : "";
}


int main(int argc, char *argv[])
{
  const char *fname = (1 < argc) ? argv[1] :  "commlib.xml";
  int io = open(fname, O_RDONLY);

  if (io != -1)
    {
      struct read_xml_t X[1];
      read_xml_init(X, io, fname);

      unsigned used_bindings = 0;
      unsigned used_binding_text = 0;
      unsigned used_text = 0;
      unsigned used_attrs = 0;
      unsigned used_stack = 0;

      while (!X->eof)
	{
	  if (read_xml_bump(X) <= tag_type_closed)
	    {
	      struct attr_t *a, *ea;
	      for(a = X->attrs, ea = a + X->attrs_size; a != ea; ++a)
		{
		  if (a->id)
		    symbols[a->id - 1].is_tag = true;
		}
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
		  if (symbols[s - 1].is_tag)
		    puts(str_from_symbol(s));
		}

	      if (hash_worst < this_case)
		hash_worst = this_case;
	    }
	}
            
      fprintf(stderr, "finished with %u errors\n", (X->errors));
      fprintf(stderr, "sizeof(read_xml_t) = %u\n", sizeof(*X));
      fprintf(stderr, "symbols_size = %u\n", symbols_size);
      fprintf(stderr, "used_bindings = %u\n", used_bindings);
      fprintf(stderr, "used_binding_text = %u\n", used_binding_text);
      fprintf(stderr, "used_text = %u\n", used_text);
      fprintf(stderr, "used_attrs = %u\n", used_attrs);
      fprintf(stderr, "used_stack = %u\n", used_stack);

      if (hash_fill)
	{
	  fprintf(stderr, "hash_size = %u\n", symbol_hashtab_size);
	  fprintf(stderr, "hash_fill = %u%%\n", 100*hash_fill/symbol_hashtab_size);
	  fprintf(stderr, "hash_avg_case = %u\n", (symbols_size + hash_fill/2)/hash_fill);
	  fprintf(stderr, "hash_worst_case = %u\n", hash_worst);
	}
      
      return X->errors != 0;
    }
  else
    {
      fprintf(stderr, "%s \"%s\" %s\n",
	      "file", (fname), "not found");
      return 1;
    }
}
