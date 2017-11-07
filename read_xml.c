#include "read_xml.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>

enum
  {
    lex_id = 256,
    lex_literal,
  };

FILE*
parser_messg(const char *source,
	    struct location_t *loc,
	    const char *type)
{
  fprintf(stderr, "%s:%u:%u: %s: ",
	  source, loc->line_no, loc->col_no,
	  type);
  return stderr;
}

FILE*
parser_error(struct read_xml_t *X)
{
  ++X->errors;
  return parser_messg(X->source,
		      &X->lex_loc,
		      "error");
}


static void
_save_loc(struct read_xml_t *X, struct location_t *loc)
{
  *loc = X->loc;
}


static void
_copy_loc(struct location_t const *from, struct location_t *to)
{
  to->line_no = from->line_no;
  to->col_no = from->col_no;
}


void
read_xml_init(struct read_xml_t *X, int io1, const char *name1)
{
  X->io = io1;
  X->source = name1;
  X->line_start = X->io_buf;
  X->loc.line_no = 1;
  X->loc.col_no = 0;
  X->beg_col_no = 0;
  X->end_col_no = 0;
  X->xmlns = xml_token_by_str("xmlns", 146349010);

  if (X->xmlns == 0)
    {
      fprintf(parser_messg(X->source, &X->loc, "warning"), "%s\n"
	      "have no \"xmlns\" symbol, xml bindings are unavailable");
    }

  X->ending_loc.line_no = 1;
  X->ending_loc.col_no = 1;
  
  X->stack_size = 0;
  X->bound[0].name_index = 0;
  X->bound[0].namesp_token = xml_token_by_str("", 0);
  X->bound_size = 1;
  X->bound_text[0] = 0;
  X->bound_text_size = 1;
  X->text_size = 0;
  X->attrs_size = 0;

  X->errors = 0;
  
  X->warned_about_max_line_no = false;
  X->warned_about_max_col_no = false;
  X->warned_about_unresolved = false;
  X->warned_abount_unknown_tag_balance = false;
  X->eof = false;
}


static int
_getc(struct read_xml_t *X)
{
  if (X->loc.col_no != X->end_col_no)
    return X->line_start[X->loc.col_no++];

  unsigned nread = read(X->io, X->io_buf, sizeof(X->io_buf));
  if (nread - 1 < sizeof(X->io_buf))
    {
      X->beg_col_no = X->loc.col_no;
      X->line_start = X->io_buf - X->beg_col_no;
      X->end_col_no = X->beg_col_no + nread;
      return X->line_start[X->loc.col_no++];
    }

  X->eof = true;
  return -1;
}


static void
_ungetc(struct read_xml_t *X)
{
  if (X->beg_col_no != X->loc.col_no)
    --X->loc.col_no;
}


static void
_got_newline(struct read_xml_t *X)
{
  if (X->loc.line_no < max_line_no)
    ++X->loc.line_no;
  else if (!X->warned_about_max_line_no)
    {
      X->warned_about_max_line_no = true;
      fprintf(parser_messg(X->source, &X->loc, "note"), "%s\n",
	      "this is the last tracked line number");
    }

  X->line_start += X->loc.col_no;
  X->beg_col_no -= X->loc.col_no;
  X->end_col_no -= X->loc.col_no;
  X->loc.col_no = 0;
}


static void
_close_text(struct read_xml_t *X)
{
  if (X->text_size != (max_text_size - 1))
    {
      X->text[X->text_size++] = 0;
      X->lex_symbol =
	xml_token_by_str((X->text + X->lex_text_index), X->text_hash);      
    }
  else
    {
      X->text_size = X->lex_text_index;
      X->text[X->lex_text_index] = 0;
      X->lex_symbol = ~0;
      fprintf(parser_error(X), "%s %u %s\n",
	      "too much text, please set \"max_text_size\" to",
	      (2*max_text_size), "or more");
    }  
}


static void
_add_text(struct read_xml_t *X, int c)
{
  X->text_hash = 33*X->text_hash + c;
  
  if (X->text_size != (max_text_size - 1))
    X->text[X->text_size++] = c;
}


static void
_read_esc(struct read_xml_t *X)
{
  struct location_t loc;
  unsigned esc_len;
  int c, esc_type;
  char esc[max_esc_length + 1];

  loc = X->loc;
  /* _save_loc(X, &loc); */
  
  if ('#' != (esc_type = _getc(X)))    
    _ungetc(X);
  
  for(esc_len = 0; ';' != (c = _getc(X)); )
    {
      if (esc_len == max_esc_length)
	{
	  fprintf(parser_messg(X->source, &loc, "error"), "%s %u %s\n",
		  "escape must be shorter", esc_len, "symbols");
	  break;
	}
	  
      if (isalnum(c))
	esc[esc_len++] = c;
      else
	{
	  fprintf(parser_messg(X->source, &loc, "error"), "%s\n",
		  "missing \";\" in escape");
	  break;
	}
    }
  
  esc[esc_len] = 0;

  if (esc_type == '#')
    {
      char *end;
      if (esc[0] == 'x')
	c = strtoul((esc + 1), &end, 16);
      else 
	c = strtoul((esc), &end, 10);
      if (*end)
	fprintf(parser_messg(X->source, &loc, "error"), "%s\n",
		"extra text in escape");
    }
  else
    {
      // Cichelli hash
      const char *esc1;
      switch(esc_len + esc[0])
	{
	case 110: esc1 = "<lt"; break;
	case 105: esc1 = ">gt"; break;
	case 101: esc1 = "'apos"; break;
	case 117: esc1 = "\"quot"; break;
	case 100: esc1 = "&amp"; break;
	case 114: esc1 = " nbsp"; break;
	default: esc1 = "?"; break;
	}

      if (0 == strcmp(esc, (esc1 + 1)))
	c = esc[0];
      else
	{
	  ++X->errors;
	  fprintf(parser_messg(X->source, &loc, "error"), "%s \"&%s;\"\n",
		  "unknown escape", (esc));
	  c = '?';
	}
    }
  // TODO: utf-8
  _add_text(X, c);  
}


static void
_next_lex(struct read_xml_t *X)
{
  X->lex_text_index = X->text_size;
  X->text_hash = 0;
  
  for(;;)
    {
      int c;
      // filter out spaces
      while ((c = _getc(X)) <= ' ')
	{
	  if (c == '\n') _got_newline(X);
	  else if (c == -1) return;
	}
      X->lex_loc = X->loc;
      /* _save_loc(X, &X->lex_loc); */
      
      // <id_token>
      for(; (' ' < c) &&
	    (!ispunct(c) || c == '_' ||
	     c == '-' || c == '.'); c = _getc(X))
	_add_text(X, c);
      
      if (X->lex_text_index != X->text_size)
	{
	  _ungetc(X);
	  _close_text(X);
	  X->lex_token = lex_id;
	  return;
	}
      // <literal>
      if (c == '"')
	{
	  while ('"' != (c = _getc(X)))
	    {
	      if (' ' <= c || c == '\t')
		{
		  if (c == '&')
		    _read_esc(X);
		  _add_text(X, c);
		}
	      else
		{
		  fprintf(parser_error(X), "%s\n",
			  "literal not closed");
		  break;
		}
	    }
	  _close_text(X);
	  X->lex_token = lex_literal;
	  return;
	}      
      // etc
      X->lex_token = c;
      return;
    }
}


static void
_do_unbind_to(struct read_xml_t *X, struct stack_node_t *state)
{
  X->bound_size = state->bound_size;
  X->bound_text_size = state->bound_text_size;
}


static void
_do_bind_namesp(struct read_xml_t *X,
		struct attr_t *attr1, unsigned namesp_token)
{
  // add bindings
  if (X->bound_size != max_bound_size)
    {
      struct binding_t *binding1 = X->bound + (X->bound_size++);
      binding1->namesp_token = namesp_token;
      binding1->name_index = X->bound_text_size;
      unsigned name_size = attr1->val_index - attr1->namesp_index;
      unsigned new_bound_text_size = X->bound_text_size + name_size;
      if (new_bound_text_size <= max_bound_text_size)
	{
	  memcpy(X->bound_text + X->bound_text_size,
		 X->text + attr1->namesp_index,
		 name_size);
	  X->bound_text_size = new_bound_text_size;
	}
      else
	fprintf(parser_error(X), "%s %u %s\n",
		"too much bound text, please set \"max_bound_text_size\" to",
		(2*max_bound_text_size), "or more");
    }
  else
    fprintf(parser_error(X), "%s %u %s\n",
	    "too much many bindings, please set \"max_bound_size\" to",
	    (2*max_bound_text_size), "or more");
}
  

static void
_do_resolve_namespaces(struct read_xml_t *X)
{
  struct attr_t *a, *ea;
  for(a = X->attrs, ea = X->attrs + X->attrs_size; a != ea; ++a)
    {
      const char *alias = X->text + a->namesp_index;
      struct binding_t *last = X->bound + X->bound_size;
      for(;;)
	{
	  if (X->bound == last)
	    {
	      if (!X->warned_about_unresolved)
		{
		  X->warned_about_unresolved = true;
		  fprintf(parser_error(X), "%s\n",
			  "tags/attributes with unresolved aliases are ignored");
		}	      
	      fprintf(parser_messg(X->source, &a->loc, "warning"),
		      "%s \"%s\" %s\n",
		      "namespace alias", (alias), "is unknown");
	      /* _do_bind_namesp(X, a, 0); */
	      break;
	    }
	  --last;
	  if (0 == strcmp(alias, X->bound_text + last->name_index))
	    {
	      a->namesp_token = last->namesp_token;
	      break;
	    }
	}
    }
}


static void
_do_open_tag(struct read_xml_t *X)
{
  struct stack_node_t *top = X->stack + (X->stack_size++);
  top->id_token = X->attrs[0].id_token;
  top->namesp_token = X->attrs[0].namesp_token;
}

static void
_do_close_tag(struct read_xml_t *X)
{
  // restore bindings
  if (X->stack_size != 0)
    {
      struct stack_node_t *top = X->stack + X->stack_size - 1;
      _do_unbind_to(X, top);

      if (!X->warned_abount_unknown_tag_balance &&
	  (top->id_token == 0 || top->namesp_token == 0))
	{
	  X->warned_abount_unknown_tag_balance = true;
	  fprintf(parser_messg(X->source, &X->lex_loc, "warning(once)"), "%s\n",
		  "tags with unknown ids or namespaces "
		  "are not checked for open/close balance");
	}

      if (top->id_token != X->attrs[0].id_token ||
	  top->namesp_token != X->attrs[0].namesp_token)
	{
	  fprintf(parser_error(X), "%s \"%s:%s\" %s\n",
		  "closing tag", xml_token_name(X->attrs[0].namesp_token),
		  xml_token_name(X->attrs[0].id_token),
		  "mismatches opening tag");
	  fprintf(parser_messg(X->source, &top->loc, "note"),
		  "%s \"%s:%s\" %s\n",
		  "the opening", xml_token_name(top->namesp_token),
		  xml_token_name(top->id_token), "was here");
	}

      if (0 == --X->stack_size)
	X->ending_loc = X->stack->loc;
    }
  else
    {
      fprintf(parser_error(X), "%s\n",
	      "no closing tag is needed here");
      fprintf(parser_messg(X->source, &X->ending_loc, "note"), "%s\n",
	      "here we are at root");
    }
}


static enum tag_type_t
_read_tag(struct read_xml_t *X, enum tag_type_t tag_type)
{
  _next_lex(X);

  // read attributes
  while (X->lex_token == lex_id)
    {
      struct attr_t *attr1 = X->attrs + X->attrs_size;
      attr1->loc = X->lex_loc;
      attr1->id_index = X->lex_text_index;
      attr1->id_token = X->lex_symbol;      
      attr1->val_token = attr1->namesp_token = 0;
      attr1->val_index = attr1->namesp_index = X->text_size - 1;
      _next_lex(X);
      if (X->lex_token == ':')
	{	  
	  _next_lex(X);	  
	  if (X->lex_token == lex_id)
	    {
	      if (X->xmlns == 0 ||
		  attr1->id_token != X->xmlns) /* alias:xx = ... */
		{
		  attr1->namesp_token = attr1->id_token;
		  attr1->id_token = X->lex_symbol;
		  attr1->namesp_index = attr1->id_index;
		  attr1->id_index = X->lex_text_index;
		}
	      else		/* xmlns:xx = ... or xmlns = ... */
		{
		  attr1->namesp_token = 0;
		  attr1->namesp_index = X->lex_text_index;
		}
	      _next_lex(X);
	    }
	  else
	    fprintf(parser_error(X), "%s\n",
		    "<attr-id_token> must be: <namesp_token>:<id_token>");
	}

      if (X->attrs_size != 0)
	{
	  if (X->lex_token == '=')
	    {
	      _next_lex(X);
	      if (X->lex_token == lex_literal)
		{
		  attr1->val_token = X->lex_symbol;
		  attr1->val_index = X->lex_text_index;
		  _next_lex(X);
		}
	      else
		fprintf(parser_error(X), "%s\n"
			"<attr-val_token> must be a literal string");		
	    }
	  else 
	    fprintf(parser_error(X), "%s\n",
		    "<attr> must be: <attr-id_token> = <attr-val_token>");

	  if (X->xmlns != 0 && attr1->id_token == X->xmlns)
	    {
	      _do_bind_namesp(X, attr1, attr1->val_token);
	      continue;
	    }
	}      

      if (X->attrs_size != max_attrs_size)
	++X->attrs_size;	  
      else
	{
	  fprintf(parser_error(X), "%s %u %s\n",
		  "too many attributes, please set \"max_attrs_size\" to",
		  (2*max_bound_text_size), "or more");
	  break;
	}
    }

  _do_resolve_namespaces(X);

  // finish tag
  if (X->attrs_size != 0)
    {
      if (X->lex_token == '/')
	{
	  if (tag_type == tag_type_open)
	    {
	      int c;
	      if ('>' != (c = _getc(X)))
		{
		  _ungetc(X);
		  fprintf(parser_error(X), "%s\n",
			  "closed tag must be ended with \"/>\"");
		}
	      return tag_type_closed;
	    }	  
	  fprintf(parser_error(X), "%s\n",
		  "closing tag must be ended with \">\"");
	  fprintf(parser_messg(X->source, &X->attrs[0].loc, "note"), "%s\n",
		  "here was \"</\"");
	  return tag_type_closed;
	}
      else if (X->lex_token == '>')
	{
	  if (tag_type == tag_type_open)
	    {
	      _do_open_tag(X);
	      return tag_type_open;
	    }
	  else
	    {
	      _do_close_tag(X);
	      return tag_type_closing;
	    }
	}

      fprintf(parser_error(X), "%s\n",
	      "missing \">\"");
    }
  else
    fprintf(parser_error(X), "%s\n",
	    "tag must be: <tag> [<attr> ...]");

  return tag_type_error;
}


static void
_read_text(struct read_xml_t *X)
{
  int c;
  while('<' != (c = _getc(X)))
    {
      if (' ' < c)
	{
	  if (X->text_size)
	    _add_text(X, ' ');

	  do
	    {
	      if (c != '&')
		_add_text(X, c);
	      else
		_read_esc(X);

	      c = _getc(X);
	      if ('<' == c)
		return;
	    }
	  while (' ' < c);
	}

      if (c == '\n')
	_got_newline(X);
      
      if (c == -1)
	break;	  	  
    }
}


enum tag_type_t
read_xml_bump(struct read_xml_t *X)
{  
  for(;;)
    {
      X->text_size = X->lex_text_index = 0;
      X->attrs_size = 0;

      _read_text(X);

      if (X->text_size)
	{
	  _ungetc(X);
	  _close_text(X);
	  return tag_type_text;
	}

      if (X->eof)
	break;

      if (X->stack_size == max_stack_size)
	{
	  fprintf(parser_error(X), "%s %u %s\n",
		  "too deep node, please set \"max_stack_size\" to",
		  (2*max_stack_size), "or more");
	  return -1;
	}
      
      // tags
      struct stack_node_t *top = X->stack + X->stack_size;
      top->loc = X->loc;
      /* _save_loc(X, &top->loc); */
      top->bound_size = X->bound_size;
      top->bound_text_size = X->bound_text_size;
      
      int c = _getc(X);
      if (c == '/')
	{
	  _read_tag(X, tag_type_closing);
	  return tag_type_closing;
	}
      
      X->want_warn_end_of_tag = true;
      if (c == '!')
	{
	  if ('-' == (c = _getc(X)) && '-' == (c = _getc(X)))
	    {
	      unsigned n;
	      do
		{
		  for(n = 0; '-' == (c = _getc(X)); ++n);
		  
	          if (c == '\n')
		    _got_newline(X);
		  
		  if (c == -1)
		    {
		      fprintf(parser_error(X), "%s\n",
			      "missing \"-->\"");
		      break;
		    }
		}
	      while (c != '>' || n < 2);
	      continue;
	    }
	}
      else if (c == '[' || c == '?')	
	{
	  X->want_warn_end_of_tag = false;
	}
      else
	{	  
	  _ungetc(X);
	  return _read_tag(X, tag_type_open);
	}

      // read until end of tag
      while ('>' != (c = _getc(X)))
	{
	  if (c == '\n')
	    _got_newline(X);

	  if (X->want_warn_end_of_tag && ' ' < c)
	    {
	      X->want_warn_end_of_tag = false;
	      fprintf(parser_error(X), "%s\n",
		      "extra text");
	    }	    
	}
    }
}
