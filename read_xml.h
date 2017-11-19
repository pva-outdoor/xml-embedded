#ifndef READ_XML_H
#define READ_XML_H

#include <stdio.h>
#include <stdbool.h>

/**
Data type to hold token id.  Please adjust also `not_a_token' to some
value inside `xml_token_t' value range.
 */
typedef short unsigned
xml_token_t;

/**
User-defined function which is called before printing every optional
diagnostic message. No default implementation is given. You can just
return true or false or return false after after some call count (to
limit number of messages).

@return nonzero to allow the message.
 */
extern int
extra_messages_allowed();


/**
User-defined function to calculate uniqual string ID.  Optional hash
may be given.

@param str - the string.
@param opt_hash - optional hash.
@return str ID (an unique value that is mapped to string).

The same strings should give same IDs.  Different strings should give
different IDs.

If the hash is not given (zero) it should be calculated with the
following algorithm:

@code
hash(0, str) = 0
hash(i + 1, str) = 33 * hash(i, str) + str[i]
@literal

This algorithm is used by the XML read itself.
The hash may be not calculated if it is not used.
 */
extern xml_token_t
xml_token_by_name(const char *str, unsigned opt_hash);

unsigned
string_hash(const char *str);


/**
User-defined function to get string by it's ID.  
It is used for diagnostic messages.
 */
extern const char*
xml_token_name(xml_token_t token);

/*i
@chapter Parser limitations

Usually there is no need to parse XML documents of every structure
what can be coded with XML file. For example it is rarely used
structure with node depth of 100.  Furthermore if you have a document
of 100 nodes depth it is rather a bad-formed document or an attack
then a real document.

The following limitations on XML douments are applied:

@itemize
 */
enum
  {
    /*i
@item Maximum (2**32 - 1) line numbers may be referenced in messages.
The parser can process more lines but line numbering in messages is
limited. All the rest lines will be references as the maximum allowed
line number.
     */
    max_line_no = ~0,

    /*i
@item Maximum (2**32 - 1) column numbers may be referenced in messages.
The parser can process more columns but column numbering in messages is
limited. All the rest columns will be references as the maximum allowed
column number.
     */
    max_col_no = ~0,

    /*i
@item XML escapes can be not longer 20 bytes.  Otherwise such escapes
are treated as errors and will be replaced with ``?'' symbol by
recovery action.
     */
    max_esc_length = 20,

    /*i
@item Maximum 20 attributes (without xml bindings) allowed per tag.
XML bindings are not treated as tag attributes.
     */
    max_attrs_size = 1 + 20,

    /*i
@item Maximum 20 simultaneous bindings are allowed.  The bindings list
for current node consists of the node bindings and the all bindings of
it's parent tags (up to the root).
     */
    max_bound_size = 20,

    /*i
@item Maximum depth of 20 nodes is allowed.
     */
    max_stack_size = 20,

    /*i
@item Maximum 1024 bytes of text per node is allowed. This includes
either text data + terminating null character for XML text or summary
text of tag/attribute names, namespace aliases, attribute values and
terminating null characters for every string.
     */
    max_text_size = 1024,

    /*i
@item Maximum 64 bytes for namespace aliases and their terminating 
null characters per active bindings for each node is allowed.
     */
    max_bound_text_size = 64,

    /*i
@item XML document is processed by blocks of 1024 bytes.  Actually
this is rather setting then limitation.
     */
    io_buf_size = 1024,

    /*i
@item Maximum 65535 different tokens may be recognized.  This includes
XML keywords, tag names, attribute names and values, XML text values.
A document of more different token count may be processed but fast
compare feature cannot use more tokens.
     */
    not_a_token = 65535,
/*i
@end itemize
 */
  };


enum xml_node_type_t
  {
    xml_node_open,
    xml_node_text,
    xml_node_close,
  };    


enum xml_read_state_t
  {
    xml_read__text,
    xml_read__end_of_tag,
  };

/*i
@section Location tracking

For diagnostic purpose we track each parsed object location:

@itemize
 */
struct xml_location_t
{
  /*i
@item line number
   */
  unsigned line_no;
  /*i
@item column number
   */
  unsigned col_no;
};
/*i
@end itemize

Note: some limitation applies to their values.
 */



struct xml_attr_t
{
  /*i
@item location
   */
  struct xml_location_t loc;
  /*i
@item
   */
  short unsigned namesp_index;
  short unsigned id_index;
  short unsigned val_index;
  xml_token_t namesp_token;
  xml_token_t id_token;
  xml_token_t val_token;
};


struct xml_binding_t
{
  short unsigned name_index;
  xml_token_t namesp_token;
};


struct xml_stack_node_t
{
  struct xml_location_t loc;
  xml_token_t id_token;
  xml_token_t namesp_token;
  short unsigned bound_size;
  short unsigned bound_text_size;
};


struct read_xml_t
{
  const char *source;
  struct xml_location_t loc;
  struct xml_location_t lex_loc;
  struct xml_location_t tag_loc;
  struct xml_location_t ending_loc;
 
  struct xml_attr_t attrs[1 + max_attrs_size];
  struct xml_stack_node_t stack[max_stack_size];
  struct xml_binding_t bound[max_bound_size];

  unsigned char *line_start;

  int io;

  unsigned text_hash;
  unsigned errors;
  unsigned beg_col_no;
  unsigned end_col_no;
  

  short int lex_token;
  xml_token_t lex_symbol;
  short unsigned lex_text_index;
  
  short unsigned attrs_size;
  short unsigned stack_size;  
  short unsigned bound_size;
  short unsigned text_size;
  short unsigned bound_text_size;
  
  xml_token_t xmlns;

  enum xml_read_state_t state;
  
  char text[max_text_size];
  char bound_text[max_bound_text_size];

  bool warned_about_max_line_no;
  bool warned_about_max_col_no;
  bool warned_about_unresolved;
  bool warned_abount_unknown_tag_balance;
  bool want_warn_end_of_tag;
  bool eof;

  unsigned char io_buf[io_buf_size];
};


FILE*
parser_messg(const char *source,
	     struct xml_location_t *loc,
	     const char *type);

FILE*
parser_error(struct read_xml_t *X);

FILE*
parser_error_loc(struct read_xml_t *X, struct xml_location_t *loc);


struct xml_location_t *
last_xml_location(struct read_xml_t *X);


void
init_read_xml(struct read_xml_t *X,
	      int io, const char *name);

enum xml_node_type_t
bump_xml_node(struct read_xml_t *X);

struct xml_attr_t*
find_xml_attr(struct read_xml_t *X,
	      xml_token_t id_token, xml_token_t namesp_token);


struct xml_attr_t*
bump_xml_tag_at(struct read_xml_t *X, unsigned level);


struct xml_attr_t*
find_xml_tag_at(struct read_xml_t *X,
		xml_token_t id_token, xml_token_t namesp_token,
		unsigned level);

struct xml_attr_t*
find_xml_tag_recursive(struct read_xml_t *X,
		       xml_token_t id_token, xml_token_t namesp_token,
		       unsigned min_level);

void
ignore_rest_xml_at(struct read_xml_t *X, unsigned up_to_depth);

xml_token_t
current_xml_tag_token(struct read_xml_t *X);

#endif /* READ_XML_H */
