#include "read_xml.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>

static FILE*
parser_lex_error(struct read_xml_t *X);

static FILE*
parser_attr_error(struct read_xml_t *X);

void
parser_fatal_error(struct read_xml_t *X);

static void
_end_of_open_tag(struct read_xml_t *X);

static void
_read_text(struct read_xml_t *X);

static enum xml_node_type_t
_read_tag(struct read_xml_t *X);

static void
_do_open_tag(struct read_xml_t *X);

static void
_ignore_rest_tag(struct read_xml_t *X);

static void
_do_close_tag(struct read_xml_t *X);

static void
_read_esc(struct read_xml_t *X);

static void
_read_attr_id(struct read_xml_t *X,
	      struct xml_attr_t *attr1);

static void
_read_attr_val(struct read_xml_t *X,
	       struct xml_attr_t *attr1);

static void
_do_resolve_namespaces(struct read_xml_t *X);

static void
_read_attr_id(struct read_xml_t *X,
	      struct xml_attr_t *attr1);


/*i
@chapter Text reader

A buffered text reader is used.
 */
static int
_getc(struct read_xml_t *X)
{
  /*i
During parsing text locations are tracked for each parsed item.
  */
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
  /*i
Each new line symbol @kbd{LF} increases line counter but we can track
a limited count of lines.  When the limit is reached the counter is
freezed.
   */
  if (X->loc.line_no < max_line_no)
    ++X->loc.line_no;
  else if (!X->warned_about_max_line_no)
    {
      X->warned_about_max_line_no = true;
      fprintf(parser_messg(X->source, &X->loc, "note"),
	      "%s\n",
	      "this is the last tracked line number");
    }

  /*i
Any way column number is restarting to 0 (index before the first symbol).
   */
  X->line_start += X->loc.col_no;
  X->beg_col_no -= X->loc.col_no;
  X->end_col_no -= X->loc.col_no;
  X->loc.col_no = 0;
}


static void
_close_text(struct read_xml_t *X)
{
  /*i
All text collected by the parser is null-terminated.  Also the text 
length and hash is known at load time.
   */
  if (X->text_size != (max_text_size - 1))
    {
      X->text[X->text_size++] = 0;      
      X->lex_symbol =
	xml_token_by_name((X->text + X->lex_text_index), X->text_hash);      
    }
  else
    {
      /*
The summary text collected inside insgle tag or plain text value is
limited.
       */
      X->text_size = X->lex_text_index;
      X->text[X->lex_text_index] = 0;
      X->lex_symbol = not_a_token;
      fprintf(parser_lex_error(X),
	      "%s %u %s\n",
	      "too much text, please set \"max_text_size\" to",
	      (2*max_text_size), "or more");
    }  
}


static void
_add_text(struct read_xml_t *X, int c)
{
  /*i
Hashing is used faster string comparsion.  Every read string is
automatically hashed during reading.
   */
  X->text_hash = 33*X->text_hash + c;
  
  if (X->text_size != (max_text_size - 1))
    X->text[X->text_size++] = c;
}

/*i
The following iterative
algorithm is applied to calculate hash:
 */
unsigned
string_hash(const char *str)
{
  const unsigned char *s;
  unsigned hash = 0;

  /*i
@example
hash(0, str) = 0
hash(i + 1, str) = 33 * hash(i, str) + str[i]
@end example
   */
  for(s = (unsigned char*)str; *s; ++s)
    hash = 33*hash + (*s);
  
  return hash;
}

void
init_read_xml(struct read_xml_t *X, int io1, const char *name1)
{
  X->io = io1;
  X->source = name1;
  X->line_start = X->io_buf;
  X->loc.line_no = 1;
  X->loc.col_no = 0;
  X->tag_loc.line_no = 1;
  X->tag_loc.col_no = 1;
  X->lex_loc.line_no = 1;
  X->lex_loc.col_no = 1;
  X->beg_col_no = 0;
  X->end_col_no = 0;

  /*i
When parser can not recognize ``xmlns'' token no bindings are available.
   */
  X->xmlns = xml_token_by_name("xmlns", 146349010);
  if (X->xmlns == not_a_token)
    {
      fprintf(parser_messg(X->source, &X->lex_loc, "warning"),
	      "%s\n"
	      "have no \"xmlns\" symbol, xml bindings are unavailable");
    }

  X->ending_loc.line_no = 1;
  X->ending_loc.col_no = 1;
  
  /*i
Initially only empty namespace (``'') is bound to empty alias.
   */
  X->bound[0].name_index = 0;
  X->bound[0].namesp_token = xml_token_by_name("", 0);
  X->bound_size = 1;  
  X->bound_text[0] = 0;
  X->bound_text_size = 1;
  
  X->stack_size = 0;
  X->text_size = 0;
  X->attrs_size = 0;
  X->errors = 0;
  X->state = xml_read__text;
  
  X->warned_about_max_line_no = false;
  X->warned_about_max_col_no = false;
  X->warned_about_unresolved = false;
  X->warned_abount_unknown_tag_balance = false;
  X->eof = false;
}


/*i
@section XML document content
 */
enum xml_node_type_t
bump_xml_node(struct read_xml_t *X)
{
  if (X->state == xml_read__end_of_tag)
    {
      _end_of_open_tag(X);
      return xml_node_close;
    }
  
  /*i
Each XML document contains structured tags and plain text.
  */

  for(;;)
    {
      X->text_size = X->lex_text_index = 0;
      X->attrs_size = 0;

      _read_text(X);

      if (X->text_size)
	{
	  _ungetc(X);
	  _close_text(X);
	  X->state == xml_read__text;
	  return xml_node_text;
	}
      /*i
Structured data tags are limited in folding depth.
       */
      X->tag_loc = X->loc;
      if (X->stack_size == max_stack_size)
	{
	  fprintf(parser_attr_error(X),
		  "%s %u %s\n",
		  "too deep node, please set \"max_stack_size\" to",
		  (2*max_stack_size), "or more");
	  return -1;
	}
      /*
The following structured data tags are available:

@table @var
       */
      struct xml_stack_node_t *top = X->stack + X->stack_size;
      top->bound_size = X->bound_size;
      top->bound_text_size = X->bound_text_size;

      X->want_warn_end_of_tag = true;

      int c = _getc(X);
      if (0) ;
      /*i
@item Comments

@example
<!-- ...  -->
@end example
       */
      else if (c == '!')
	{
	  if ('-' == (c = _getc(X)) && '-' == (c = _getc(X)))
	    {
	      unsigned n;
	      do
		{
		  /*i
Please break repeating ``-'' more then 2 times inside comments to
avoid unwanted ``-->'' (which will end the comment).
		   */
		  for(n = 0; '-' == (c = _getc(X)); ++n);
		  
	          if (c == '\n')
		    _got_newline(X);
		  
		  if (c == -1)
		    {
		      fprintf(parser_error(X),
			      "%s\n",
			      "missing \"-->\"");
		      break;
		    }
		}
	      while (c != '>' || n < 2);
	      continue;
	    }
	}
      /*i
@item DTD or processing instructions

@example
<[ ... ]>
<? ... ?>
@end example

These constrictions are ignored.
      */
      else if (c == '[' || c == '?')	
	{
	  X->want_warn_end_of_tag = false;
	}
      /*i
@item Closing tags

@example
</tag>
@end example
       */
      else if (c == '/')
	{
	  _read_tag(X);
	  /*i
The tag can be ended only with ``>'' symbol.
Note: ``</ >'' is an invalid tag.
	  */
	  if (X->attrs_size != 0)
	    {
	      if (X->lex_token != '>')
		{
		  fprintf(parser_attr_error(X),
			  "%s\n",
			  "closing tag must be ended with \">\"");
		  fprintf(parser_messg(X->source, &X->attrs[0].loc, "note"),
			  "%s\n",
			  "here was \"</\"");
		}
	      break;
	    }

	  fprintf(parser_attr_error(X), "%s\n",
		  "closing tag must be: </tag>");
	}
      /*i
Reading past the end of file also simulates a closing tag.
       */
      else if (c == -1)
	{
	  break;
	}
      /*i
@item Opening tags

@example
<tag>
<tag/>
@end example
       */
      else
	{	  
	  _ungetc(X);
	  _read_tag(X);
	  /*i
Note: ``< />'' and ``< >'' are invalid tags.
	   */	  
	  if (X->attrs_size != 0)
	    {
	      _do_open_tag(X);
	      X->state = xml_read__end_of_tag;
	      return xml_node_open;
	    }

	  fprintf(parser_attr_error(X),
		  "%s\n",
		  "open tag must be: <tag> [<attr> ...]");	  
	}
      _ignore_rest_tag(X);
    }

  _do_close_tag(X);
  return xml_node_close;
}

/*i
The open tag can have no content.  In this case it can be expressed as
closing tag.  The following constructions are equivalent:

@example
<tag>  <!-- no content -->  </tag>
<tag/>
@end example
 */
static void
_end_of_open_tag(struct read_xml_t *X)
{
  if (X->lex_token == '/')
    {
      if ('>' != _getc(X))
	{
	  _ungetc(X);
	  fprintf(parser_error(X),
		  "%s\n",
		  "closed tag must be ended with \"/>\"");
	}
      _do_close_tag(X);
    }
}
/*i
@end table

All other constructions are assumed to be invalid.
*/
static void
_ignore_rest_tag(struct read_xml_t *X)
{
  int c;
  while ('>' != (c = _getc(X)) && c != -1)
    {
      if (c == '\n')
	_got_newline(X);

      if (X->want_warn_end_of_tag && ' ' < c)
	{
	  X->want_warn_end_of_tag = false;
	  fprintf(parser_error(X),
		  "%s\n",
		  "extra text");
	}
    }
}


static void
_read_text(struct read_xml_t *X)
{
  int c;
  /*i
Plain text can not include ``<'', ``>'' characters which are used to
indicate non-plain tag inside docment.
  */
  while('<' != (c = _getc(X)))
    {	  
      if (' ' < c)
	{
	  /*i
The text consists of words and spaces, and consequent spaces are
joined together in a signle space character.  The leading and trailing
spaces inside one structured node are trimmed.
	  */	      
	  if (X->text_size)
	    _add_text(X, ' ');

	  do
	    {
	      /*i
Ampersand is also a special symbol which is used to encode escapes
which are mapped to symbols.  Those symbols are assumed to be 
non-spaces.
	      */		  
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

/*i
@section Escapes
 */
static void
_read_esc(struct read_xml_t *X)
{
  struct xml_location_t loc;
  unsigned esc_len;
  int c, esc_type;
  char esc[max_esc_length + 1];

  loc = X->loc;
  /* _save_loc(X, &loc); */

  /*i
We recognize symbol-code (started with ``#'') and symbol-name escapes.
All escapes must be ended with ``;'' symbol.
   */
  if ('#' != (esc_type = _getc(X)))    
    _ungetc(X);

  for(esc_len = 0; ';' != (c = _getc(X)); )
    {
      /*i
When escape length exsceeds it's allowed limit the appropriate error
message is printed.
       */
      if (esc_len == max_esc_length)
	{
	  fprintf(parser_error_loc(X, &loc),
		  "%s %u %s\n",
		  "escape must be shorter", esc_len, "symbols");
	  break;
	}
      /*i
Only alpha-numeric symbols in esapes are allowed.  It is assumed that
``;'' is missing when non-alpha-numeric symbol was found before ``;''.
       */	  
      if (isalnum(c))
	esc[esc_len++] = c;
      else
	{
	  fprintf(parser_error_loc(X, &loc),
		  "%s\n",
		  "missing \";\" in escape");
	  break;
	}
    }
  
  esc[esc_len] = 0;
  if (esc_type == '#')
    {
  /*i
For symbol-code escapes, decimal and hexadecimal codes are supported.
   */
      char *end;
      if (esc[0] == 'x')
	c = strtoul((esc + 1), &end, 16);
      else 
	c = strtoul((esc), &end, 10);
      /*i
Such escapes must have no non-digit or non-hex symbols.
       */
      if (*end)
	fprintf(parser_error_loc(X, &loc),
		"%s\n",
		"extra text in escape");
    }
  else
    {
      /*i
The following symbol-name escapes are recognized:

@itemize
       */
      // Cichelli hash
      const char *esc1;
      switch(esc_len + esc[0])
	{
	  /*i
@item lt
gives ``<''
	   */
	case 110: esc1 = "<lt"; break;
	  /*i
@item gt
gives ``>''
	   */
	case 105: esc1 = ">gt"; break;
	  /*i
@item apos
gives apostrophe
	   */
	case 101: esc1 = "'apos"; break;
	  /*i
@item quote
gives quotaion mark
	   */
	case 117: esc1 = "\"quot"; break;
	  /*i
@item amp
gives ampersand
	   */
	case 100: esc1 = "&amp"; break;
	  /*i
@item nbsp
gives usual space
	   */
	case 114: esc1 = " nbsp"; break;
	  /*i
@end itemize
	   */
	default: esc1 = "?"; break;
	}

      if (0 == strcmp(esc, (esc1 + 1)))
	c = esc1[0];
      else
	{
	  /*i

All other symbols gives question mark ``?''.  Appropriate error
message is generated.
	   */
	  ++X->errors;
	  fprintf(parser_error_loc(X, &loc),
		  "%s \"&%s;\"\n",
		  "unknown escape", (esc));
	  c = '?';
	}
    }
  // TODO: utf-8
  _add_text(X, c);  
}



static void
_do_unbind_to(struct read_xml_t *X, struct xml_stack_node_t *state)
{
  X->bound_size = state->bound_size;
  X->bound_text_size = state->bound_text_size;
}


static void
_do_bind_namesp(struct read_xml_t *X,
		struct xml_attr_t *attr1, unsigned namesp_token)
{
  // add bindings
  if (X->bound_size != max_bound_size)
    {
      struct xml_binding_t *binding1 = X->bound + (X->bound_size++);
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
	fprintf(parser_attr_error(X),
		"%s %u %s\n",
		"too much bound text, please set \"max_bound_text_size\" to",
		(2*max_bound_text_size), "or more");
    }
  else
    fprintf(parser_attr_error(X),
	    "%s %u %s\n",
	    "too much many bindings, please set \"max_bound_size\" to",
	    (2*max_bound_text_size), "or more");
}
  

/*i
@section Opening tags
 */
static void
_do_open_tag(struct read_xml_t *X)
{
  struct xml_stack_node_t *top = X->stack + (X->stack_size++);
  top->id_token = X->attrs[0].id_token;
  top->namesp_token = X->attrs[0].namesp_token;
}


/*i
@section Closing tags
 */
static void
_do_close_tag(struct read_xml_t *X)
{
  // restore bindings
  if (X->stack_size != 0)
    {
      struct xml_stack_node_t *top = X->stack + X->stack_size - 1;
      _do_unbind_to(X, top);
      /*i
Each closing tag must have appropriate open tag.  The strict rule
applies only to recognized tags.  Tags what was not recognized (for
example because of unknown namespaces) may close other unrecognized
tags.  They are not checked for open/close balance.
       */
      if (!X->warned_abount_unknown_tag_balance &&
	  (top->id_token == not_a_token || top->namesp_token == not_a_token))
	{
	  X->warned_abount_unknown_tag_balance = true;
	  if (extra_messages_allowed())
	    {
	      fprintf(parser_messg(X->source,
				   &X->lex_loc,
				   "warning(once)"),
		      "%s\n",
		      "tags with unknown ids or namespaces "
		      "are not checked for open/close balance");
	    }
	}
      /*i
When a recognized closing tag mismatches the open tag it is an
error message printed.
       */
      if (top->id_token != X->attrs[0].id_token ||
	  top->namesp_token != X->attrs[0].namesp_token)
	{
	  fprintf(parser_attr_error(X),
		  "%s \"%s:%s\" %s\n",
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
  else if (!X->eof)
    {
      /*i
It is possible what closing tag have no opening tag at all.
The following example demonstrates such tree:

@example
<Root>
  <tag>
    data
  </tag>
</Root>
</invalid-tag>
@end example

Ih this case the closing tag is ignored but the appropriate
diagnostic message is printed.
       */
      if (extra_messages_allowed())
	{
	  fprintf(parser_messg(X->source, &X->attrs[0].loc, "warning"),
		  "%s\n",
		  "no closing tag is needed here");
	  fprintf(parser_messg(X->source, &X->ending_loc, "note"),
		  "%s\n",
		  "here we are at root");
	}
    }

  X->state = xml_read__text;
}


/*i
@section Tag lexics
 */

enum
  {
    lex_id = 256,
    lex_literal,
  };

static void
_next_lex(struct read_xml_t *X)
{
  X->lex_text_index = X->text_size;
  X->text_hash = 0;

  /*i
The following tag tokens are recognized:

@itemize
*/
  for(;;)
    {
      int c;
      /*i
@item Spaces
All spaces except line feeds inside tags are ignored.
Line feed are used for calculating message location.
       */
      while ((c = _getc(X)) <= ' ')
	{
	  if (c == '\n') _got_newline(X);
	  else if (c == -1) return;
	}
      X->lex_loc = X->loc;
      /* _save_loc(X, &X->lex_loc); */

      /*i
@item <id>
May contain any non-space non-punctuation characters and dots,
hypthens and underscores.
       */
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
      /*i
@item <literal>
Contains characters enclosed with quotation marks.  No control
characters are allowed inside.
       */
      if (c == '"')
	{
	  while ('"' != (c = _getc(X)))
	    {
	      if (' ' <= c || c == '\t')
		{
		  /*i
The same escaping rules applies as for plain text.
		   */
		  if (c == '&')
		    _read_esc(X);
		  _add_text(X, c);
		}
	      else
		{
		  /*
It is not correct if end-of-file or non-allowed symbol is found inside
a literal.
		   */
		  fprintf(parser_error(X),
			  "%s\n",
			  "literal not closed");
		  break;
		}
	    }
	  _close_text(X);
	  X->lex_token = lex_literal;
	  return;
	}
      /*i
@item Single-character tokens
All other non-space characters are recognized as single-haracter tokens.
       */
      X->lex_token = c;
      return;
    }
  /*i
@end itemize
   */
}

/*i
@section Tags format
 */
static enum xml_node_type_t
_read_tag(struct read_xml_t *X)
{
  _next_lex(X);

  /*i
Each tag must be of the following format:

@example
<tag-id> [<attr> ...]
@end example

where:

@table @var

@item  <tag-id>
Full tag id, have the same format as @var{<attr-id>}.
   */
  while (X->lex_token == lex_id)
    {
      struct xml_attr_t *attr1 = X->attrs + X->attrs_size;
      attr1->loc = X->lex_loc;
/*
@item <attr>
Attribute assignment or namespace binding:

@example
<attr-id> = <literal>
@end example
*/      
      _read_attr_id(X, attr1);

      if (X->attrs_size != 0)
	{
	  if (X->lex_token == '=')
	    {
	      _next_lex(X);
	      _read_attr_val(X, attr1);
	    }
	  else
	    {
	      fprintf(parser_lex_error(X),
		      "%s\n",
		      "<attr> must be: <attr-id> = <literal>");
	    }
	  /*i
When ``xmlns'' token is recognized by the parser and used in the
@var{<attr-id>} definition the referenced namespace is bound to
specfied alias.  The attribute is not added to attribute list of the
tag.
	   */
	  if (X->xmlns != not_a_token && attr1->id_token == X->xmlns)
	    {
	      _do_bind_namesp(X, attr1, attr1->val_token);
	      continue;
	    }
	}      
      /*i
If more then maximum allowed @var{<attr>}s specified the parsing is
aborted.
       */
      if (X->attrs_size != max_attrs_size)
	++X->attrs_size;	  
      else
	{
	  fprintf(parser_lex_error(X),
		  "%s %u %s\n",
		  "too many attributes, please set \"max_attrs_size\" to",
		  (2*max_bound_text_size), "or more");
	  break;
	}
    }
  
  _do_resolve_namespaces(X);
}

/*i
@item <attr-id>
Full attribute name, must be in a forms:

@example
<id>
<alias>:<id>
@end example
 */
static void
_read_attr_id(struct read_xml_t *X,
	      struct xml_attr_t *attr1)
{
  /*i
When only @var{<id>} is specified the namespace is assumed to be
default one (as if alias was an empty string).
   */
  attr1->id_index = X->lex_text_index;
  attr1->id_token = X->lex_symbol;      
  attr1->val_token = attr1->namesp_token = not_a_token;
  attr1->val_index = attr1->namesp_index = X->text_size - 1;
  _next_lex(X);
  if (X->lex_token == ':')
    {	  
      _next_lex(X);	  
      if (X->lex_token == lex_id)
	{
	  /*i
When ``xmlns'' recognizion is not possible or @var{<alias>} is not
``xmlns'' the @var{<alias>} must be a name what was bound to some
namespace before.  The @var{<id>} is assumed to be a name inside
what namespace.
	  */
	  if (X->xmlns == not_a_token ||
	      attr1->id_token != X->xmlns)
	    {
	      attr1->namesp_token = attr1->id_token;
	      attr1->id_token = X->lex_symbol;
	      attr1->namesp_index = attr1->id_index;
	      attr1->id_index = X->lex_text_index;
	    }
	  else
	    /*i
The namespace binding occurs when @var{<alias>} is ``xmlns''. In this
case the @var{<id>} is an alias to bind.
	    */
	    {
	      attr1->namesp_token = not_a_token;
	      attr1->namesp_index = X->lex_text_index;
	    }
	  _next_lex(X);
	}
      else
	fprintf(parser_lex_error(X),
		"%s\n",
		"<attr-id> must be: <namesp>:<id>");
    }
}

/*i
@item <attr-val>
A literal string with attribute value or namespace name.
 */
static void
_read_attr_val(struct read_xml_t *X,
	       struct xml_attr_t *attr1)
{
  if (X->lex_token == lex_literal)
    {
      attr1->val_token = X->lex_symbol;
      attr1->val_index = X->lex_text_index;
      _next_lex(X);
    }
  else
    fprintf(parser_lex_error(X),
	    "%s\n"
	    "<attr-val> must be a literal string");		

}
/*i
@end table
 */

/*i
Each used namespace alias must be bound in this tag or before.
 */
static void
_do_resolve_namespaces(struct read_xml_t *X)
{
  struct xml_attr_t *a, *ea;
  for(a = X->attrs, ea = X->attrs + X->attrs_size; a != ea; ++a)
    {
      const char *alias = X->text + a->namesp_index;
      struct xml_binding_t *last = X->bound + X->bound_size;
      for(;;)
	{
	  if (X->bound == last)
	    {
	      /*i
Pay attention to optional messages that printed when no binding found
for given alias (optional).
	       */
	      if (extra_messages_allowed())
		{	      
		  fprintf(parser_messg(X->source,
				       &a->loc,
				       "warning"),
			  "%s \"%s\" %s\n",
			  "namespace alias", (alias), "is unknown");
		  /* _do_bind_namesp(X, a, 0); */
		}
	      /*i
Note that unresolved alias makes their tags and atributes ignored
during the rest processing.
	       */
	      if (!X->warned_about_unresolved)
		{
		  X->warned_about_unresolved = true;
		  fprintf(parser_messg(X->source,
				       &a->loc,
				       "note"),
			  "%s\n",
			  "tags/attributes with unresolved aliases are ignored");
		}	      
	      break;
	    }
	  --last;
	  /*i
The namespaces that are bound to aliases will be used with appropriate
tags and attributes.
	   */
	  if (0 == strcmp(alias, X->bound_text + last->name_index))
	    {
	      a->namesp_token = last->namesp_token;
	      break;
	    }
	}
    }
}

/*i
@section Generated messages

All parser messages are prepended with location and message type
header.
 */
FILE*
parser_messg(const char *source,
	    struct xml_location_t *loc,
	    const char *type)
{
  if (loc && loc->line_no)
    {
  /*i
@example
<file-name>:<line>:<col>: <messg-type>:
@end example

if location of object related to message is known.
   */
      fprintf(stderr,
	      "%s:%u:%u: %s: ",
	      source, loc->line_no, loc->col_no,
	      type);
    }
  else
    {
  /*i
@example
<file-name>:<line>:<col>: <messg-type>:
@end example

if location of object related to message is unknown.
   */
      fprintf(stderr,
	      "%s: %s: ",
	      source, type);
    }
  return stderr;
}

/*i
The @var{<messg-type>} may be ``error'', ``warning'', ``note'' and ``hint''.
 */
FILE*
parser_error_loc(struct read_xml_t *X, struct xml_location_t *loc)
{
  /*i
Error messages are counted.  Non-zero count indicates parsing failure.
   */
  ++X->errors;
  return parser_messg(X->source, loc, "error");
}


FILE*
parser_error(struct read_xml_t *X)
{
  return parser_error_loc(X, &X->loc);
}


FILE*
parser_lex_error(struct read_xml_t *X)
{
  return parser_error_loc(X, &X->lex_loc);
}


static FILE*
parser_attr_error(struct read_xml_t *X)
{
  return parser_error_loc(X, &X->tag_loc);
}


void
parser_fatal_error(struct read_xml_t *X)
{
  X->eof = -2;
}


/* static void */
/* _save_loc(struct read_xml_t *X, struct xml_location_t *loc) */
/* { */
/*   *loc = X->loc; */
/* } */


/* static void */
/* _copy_loc(struct xml_location_t const *from, struct xml_location_t *to) */
/* { */
/*   to->line_no = from->line_no; */
/*   to->col_no = from->col_no; */
/* } */




xml_token_t
current_xml_tag_token(struct read_xml_t *X)
{
  return (X->stack_size) ?
    X->stack[X->stack_size - 1].id_token:
    not_a_token;
}


struct xml_attr_t*
find_xml_attr(struct read_xml_t *X,
	      xml_token_t id_token, xml_token_t namesp_token)
{
  if (X->attrs_size)
    {
      struct xml_attr_t *a, *ea;
      for(a = X->attrs + 1,
	    ea = X->attrs + X->attrs_size;
	  a < ea; ++a)
	{
	  if (a->id_token == id_token &&
	      a->namesp_token == namesp_token)
	    return a;
	}

      if (extra_messages_allowed())
	{
	  fprintf(parser_messg(X->source, &X->attrs[0].loc, "warning"),
		  "%s \"%s:%s\" %s \"%s:%s\"\n",
		  "no attribute", xml_token_name(namesp_token),
		  xml_token_name(id_token),
		  "found in", (X->text + X->attrs[0].namesp_index),
		  (X->text + X->attrs[0].id_index));
	}
    }
  else
    {
      if (extra_messages_allowed())
	{
	  fprintf(parser_messg(X->source, &X->lex_loc, "warning"),
		  "%s \"%s:%s\"\n",
		  "here should be a tag with attribute",
		  xml_token_name(namesp_token),
		  xml_token_name(id_token));
	}
    }
  return 0;
}


struct xml_attr_t *
find_xml_tag_recursive(struct read_xml_t *X,
	      xml_token_t id_token, xml_token_t namesp_token,
	      unsigned min_level)
{
  struct xml_location_t loc_start;

  loc_start = X->tag_loc;
  
  while (!X->eof && min_level <= X->stack_size)
    {
      enum xml_node_type_t node_type;
      node_type = bump_xml_node(X);
      if (node_type == xml_node_open &&
	  X->attrs[0].id_token == id_token &&
	  X->attrs[0].namesp_token == namesp_token)
	{
	  return X->attrs;
	}		
    }

  if (extra_messages_allowed())
    {
      fprintf(parser_messg(X->source, &loc_start, "warning"),
	      "%s \"%s:%s\" %s\n",
	      "no tag", xml_token_name(namesp_token),
	      xml_token_name(id_token), "found");
      fprintf(parser_messg(X->source, &X->tag_loc, "note"),
	      "%s\n",
	      "up to here");
    }
  
  return 0;
}


struct xml_attr_t*
bump_xml_tag_at(struct read_xml_t *X, unsigned level)
{
  while (!X->eof && level <= X->stack_size)
    {
      ignore_rest_xml_at(X, level + 1);
      if (bump_xml_node(X) <= xml_node_text)
	return X->attrs;
    }

  return 0;
}


struct xml_attr_t*
find_xml_tag_at(struct read_xml_t *X,
		xml_token_t id_token, xml_token_t namesp_token,
		unsigned level)
{
  struct xml_attr_t* tag;
  while (0 != (tag = bump_xml_tag_at(X, level)))
    {
      if (tag->id_token == id_token &&
	  tag->namesp_token == namesp_token)
	{
	  return tag;
	}
    }

  return 0;
}


void
ignore_rest_xml_at(struct read_xml_t *X, unsigned level)
{
  unsigned max_level = X->stack_size;
  while (!X->eof && level <= X->stack_size)
    {
      if (bump_xml_node(X) <= xml_node_text &&
	  X->stack_size <= max_level)
	{
	  max_level = X->stack_size;

	  if (extra_messages_allowed())
	    {
	      fprintf(parser_messg(X->source, &X->attrs[0].loc, "warning"),
		      "<%s:%s> %s\n",
		      (X->text + X->attrs[0].namesp_index),
		      (X->text + X->attrs[0].id_index), "is skipped");
	    }
	}
    }
}
