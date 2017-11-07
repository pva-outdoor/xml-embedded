#ifndef READ_XML_H
#define READ_XML_H

#include <stdio.h>
#include <stdbool.h>

typedef short unsigned xml_token_t;

extern xml_token_t
xml_token_by_str(const char *str, unsigned str_hash);

extern const char*
xml_token_name(xml_token_t symbol);

enum
  {
    max_line_no = ~0,
    max_col_no = ~0,
    max_esc_length = 20,
    max_attrs_size = 20,
    max_bound_size = 20,
    max_stack_size = 20,
    max_text_size = 1024,
    max_bound_text_size = 64,
    io_buf_size = 1024,
  };


enum tag_type_t
  {
    tag_type_open,
    tag_type_closed,
    tag_type_closing,
    tag_type_text,
    tag_type_error
  };    


struct location_t
{
  unsigned line_no;
  unsigned col_no;
};


struct attr_t
{
  struct location_t loc;
  short unsigned namesp_index;
  short unsigned id_index;
  short unsigned val_index;
  xml_token_t namesp_token;
  xml_token_t id_token;
  xml_token_t val_token;
};


struct binding_t
{
  short unsigned name_index;
  xml_token_t namesp_token;
};


struct stack_node_t
{
  struct location_t loc;
  xml_token_t id_token;
  xml_token_t namesp_token;
  short unsigned bound_size;
  short unsigned bound_text_size;
};


struct read_xml_t
{
  const char *source;
  struct location_t loc;
  struct location_t lex_loc;
  struct location_t ending_loc;
 
  struct attr_t attrs[1 + max_attrs_size];
  struct stack_node_t stack[max_stack_size];
  struct binding_t bound[max_bound_size];

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
	    struct location_t *loc,
	    const char *type);


FILE*
parser_error(struct read_xml_t *X);

void
read_xml_init(struct read_xml_t *X,
	      int io, const char *name);

enum tag_type_t
read_xml_bump(struct read_xml_t *X);

#endif /* READ_XML_H */
