#include <stdio.h>
#include <string.h>

#include <cmark.h>
#include "ext_scanners.h"

typedef struct {
  int n_columns;
  cmark_llist *cells;
} table_row;

static void free_table_row(table_row *row) {

  if (!row)
    return;

  cmark_llist_free_full(row->cells, (CMarkListFreeFunc) cmark_strbuf_free);

  free(row);
}

static cmark_strbuf *unescape_pipes(const char *string, cmark_bufsize_t len)
{
  cmark_strbuf *res = cmark_strbuf_new(len + 1);
  bufsize_t r, w;

  cmark_strbuf_puts(res, string);

  for (r = 0, w = 0; r < len; ++r) {
    if (res->ptr[r] == '\\' && res->ptr[r + 1] == '|')
      r++;

    res->ptr[w++] = res->ptr[r];
  }

  cmark_strbuf_truncate(res, w);

  return res;
}

static table_row *row_from_string(const char *string) {
  table_row *row = NULL;
  bufsize_t cell_matched = 0;
  bufsize_t cell_offset = 0;

  row = malloc(sizeof(table_row));
  row->n_columns = 0;
  row->cells = NULL;

  do {
    cell_matched = scan_table_cell(string, cell_offset);
    if (cell_matched) {
      cmark_strbuf *cell_buf = unescape_pipes(string + cell_offset + 1,
          cell_matched - 1);
      row->n_columns += 1;
      row->cells = cmark_llist_append(row->cells, cell_buf);
    }
    cell_offset += cell_matched;
  } while (cell_matched);

  cell_matched = scan_table_row_end(string, cell_offset);
  cell_offset += cell_matched;

  if (!cell_matched || cell_offset != (cmark_bufsize_t) strlen(string)) {
    free_table_row(row);
    row = NULL;
  }

  return row;
}

static cmark_node *try_opening_table_header(cmark_syntax_extension *self,
                                            cmark_parser * parser,
                                            cmark_node   * parent_container,
                                            const char   * input) {
  bufsize_t matched = scan_table_start(input, cmark_parser_get_first_nonspace(parser));
  cmark_node *table_header;
  cmark_node *ret = NULL;
  table_row *header_row = NULL;
  table_row *marker_row = NULL;
  cmark_node_type ptype = cmark_node_get_type(parent_container);

  if (!matched)
    goto done;

  if (ptype == CMARK_NODE_PARAGRAPH)
    header_row = row_from_string(cmark_node_get_string_content(parent_container));

  marker_row = row_from_string(input + cmark_parser_get_first_nonspace(parser));

  assert(marker_row);

  if (header_row && header_row->n_columns != marker_row->n_columns) {
    free_table_row(header_row);
    header_row = NULL;
  }

  if (header_row) {
    ret = parent_container;
    cmark_node_set_type(parent_container, CMARK_NODE_TABLE);
  } else {
    ret = cmark_parser_add_child(parser, parent_container,
        CMARK_NODE_TABLE, cmark_parser_get_offset(parser));
  }

  cmark_node_set_syntax_extension(ret, self);
  cmark_node_set_n_table_columns(ret, marker_row->n_columns);

  if (header_row) {
    cmark_llist *tmp;

    table_header = cmark_parser_add_child(parser, parent_container,
        CMARK_NODE_TABLE_ROW, cmark_parser_get_offset(parser));
    cmark_node_set_syntax_extension(table_header, self);
    cmark_node_set_is_table_header(table_header, true);

    for (tmp = header_row->cells; tmp; tmp = tmp->next) {
      cmark_strbuf *cell_buf = (cmark_strbuf *) tmp->data;
      cmark_node *header_cell = cmark_parser_add_child(parser, table_header,
          CMARK_NODE_TABLE_CELL, cmark_parser_get_offset(parser));
      cmark_node_set_string_content(header_cell, cmark_strbuf_get(cell_buf));
      cmark_node_set_syntax_extension(header_cell, self);
    }
  }

  cmark_parser_advance_offset(parser, input,
                   strlen(input) - 1 - cmark_parser_get_offset(parser),
                   false);
done:
  free_table_row(header_row);
  free_table_row(marker_row);
  return ret;
}

static cmark_node *try_opening_table_row(cmark_syntax_extension *self,
                                         cmark_parser * parser,
                                         cmark_node   * parent_container,
                                         const char   * input) {
  cmark_node *table_row_block;
  table_row *row;

  if (cmark_parser_is_blank(parser))
    return NULL;

  table_row_block = cmark_parser_add_child(parser, parent_container,
      CMARK_NODE_TABLE_ROW, cmark_parser_get_offset(parser));

  cmark_node_set_syntax_extension(table_row_block, self);

  /* We don't advance the offset here */

  row = row_from_string(input + cmark_parser_get_first_nonspace(parser));

  {
    cmark_llist *tmp;

    for (tmp = row->cells; tmp; tmp = tmp->next) {
      cmark_strbuf *cell_buf = (cmark_strbuf *) tmp->data;
      cmark_node *cell = cmark_parser_add_child(parser, table_row_block,
          CMARK_NODE_TABLE_CELL, cmark_parser_get_offset(parser));
      cmark_node_set_string_content(cell, cmark_strbuf_get(cell_buf));
      cmark_node_set_syntax_extension(cell, self);
    }
  }

  free_table_row(row);

  cmark_parser_advance_offset(parser, input,
                   strlen(input) - 1 - cmark_parser_get_offset(parser),
                   false);

  return table_row_block;
}

static cmark_node *try_opening_table_block(cmark_syntax_extension * syntax_extension,
                                           bool              indented,
                                           cmark_parser    * parser,
                                           cmark_node      * parent_container,
                                           const char      * input) {
  cmark_node_type parent_type = cmark_node_get_type(parent_container);

  if (!indented && (parent_type == CMARK_NODE_PARAGRAPH || parent_type == CMARK_NODE_DOCUMENT)) {
    return try_opening_table_header(syntax_extension, parser, parent_container, input);
  } else if (!indented && parent_type == CMARK_NODE_TABLE) {
    return try_opening_table_row(syntax_extension, parser, parent_container, input);
  }

  return NULL;
}

static bool table_matches(cmark_syntax_extension *self,
                          cmark_parser * parser,
                          const char   * input,
                          cmark_node   * parent_container) {
  bool res = false;

  if (cmark_node_get_type(parent_container) == CMARK_NODE_TABLE) {
    table_row *new_row = row_from_string(input + cmark_parser_get_first_nonspace(parser));
    if (new_row) {
        if (new_row->n_columns == cmark_node_get_n_table_columns(parent_container))
          res = true;
    }
    free_table_row(new_row);
  }

  return res;
}

cmark_syntax_extension *cmark_table_extension_new(void) {
  cmark_syntax_extension *ext = cmark_syntax_extension_new("piped-tables");

  ext->last_block_matches = table_matches;
  ext->try_opening_block = try_opening_table_block;

  return ext;
}

static bool is_space_or_tab(char c) {
  return (c == ' ' || c == '\t');
}

static bool is_line_end_char(char c) {
  return (c == '\n' || c == '\r');
}

static cmark_bufsize_t parse_flexlist_marker(cmark_mem *mem, const char *input,
                                             cmark_bufsize_t pos, bool interrupts_paragraph,
                                             cmark_list **dataptr) {
  unsigned char c;
  cmark_bufsize_t startpos;
  cmark_list *data;
  cmark_bufsize_t i;
  cmark_bufsize_t ret;

  startpos = pos;
  c = input[pos];

  if (c == '~') {
    pos++;
    if (!cmark_isspace(input[pos])) {
      ret = 0;
      goto done;
    }

    if (interrupts_paragraph) {
      i = pos;
      // require non-blank content after list marker:
      while (is_space_or_tab(input[i])) {
        i++;
      }
      if (input[i] == '\n') {
        ret = 0;
        goto done;
      }
    }

    data = (cmark_list *)mem->calloc(1, sizeof(*data));
    data->marker_offset = 0; // will be adjusted later
    data->list_type = CMARK_BULLET_LIST;
    data->bullet_char = c;
    data->start = 0;
    data->delimiter = CMARK_NO_DELIM;
    data->tight = false;

    ret = pos - startpos;
    *dataptr = data;
  } else {
    ret = 0;
  }

done:
  return ret;
}

static int lists_match(cmark_list *list_data, cmark_list *item_data) {
  return (list_data->list_type == item_data->list_type &&
          list_data->delimiter == item_data->delimiter &&
          // list_data->marker_offset == item_data.marker_offset &&
          list_data->bullet_char == item_data->bullet_char);
}

static cmark_node *try_opening_flexlist_block(cmark_syntax_extension * self,
                                           bool              indented,
                                           cmark_parser    * parser,
                                           cmark_node      * parent_container,
                                           const char      * input) {
  cmark_bufsize_t matched;
  cmark_node_type parent_type = cmark_node_get_type(parent_container);
  cmark_list *data = NULL;
  int indent = cmark_parser_get_indent(parser);
  cmark_bufsize_t first_nonspace = cmark_parser_get_first_nonspace(parser);
  cmark_mem *mem = cmark_parser_get_mem (parser);
  cmark_node *ret = NULL;
  bool save_partially_consumed_tab;
  cmark_bufsize_t save_column;
  int save_offset;

  if ((!indented || parent_type == CMARK_NODE_LIST) &&
    indent < 4 &&
    (matched = parse_flexlist_marker(mem, input, first_nonspace,
                                     parent_type == CMARK_NODE_PARAGRAPH, &data))) {
    // Note that we can have new list items starting with >= 4
    // spaces indent, as long as the list container is still open.
    int i = 0;

    // compute padding:
    cmark_parser_advance_offset(parser, input,
                     cmark_parser_get_first_nonspace(parser) + matched - cmark_parser_get_offset (parser),
                     false);

    save_partially_consumed_tab = cmark_parser_has_partially_consumed_tab (parser);
    save_offset = cmark_parser_get_offset (parser);
    save_column = cmark_parser_get_column (parser);

    while (cmark_parser_get_column (parser) - save_column <= 5 &&
           is_space_or_tab(input[cmark_parser_get_offset(parser)])) {
      cmark_parser_advance_offset(parser, input, 1, true);
    }

    i = cmark_parser_get_column (parser) - save_column;
    if (i >= 5 || i < 1 ||
        // only spaces after list marker:
        is_line_end_char(input[cmark_parser_get_offset(parser)])) {
      data->padding = matched + 1;
      cmark_parser_set_offset (parser, save_offset);
      cmark_parser_set_column (parser, save_column);
      cmark_parser_set_partially_consumed_tab (parser, save_partially_consumed_tab);
      if (i > 0) {
        cmark_parser_advance_offset(parser, input, 1, true);
      }
    } else {
      data->padding = matched + i;
    }

    // check container; if it's a list, see if this list item
    // can continue the list; otherwise, create a list container.

    data->marker_offset = cmark_parser_get_indent (parser);

    if (parent_type != CMARK_NODE_LIST ||
        !lists_match(cmark_node_get_list (parent_container), data)) {
      ret = cmark_parser_add_child(parser, parent_container, CMARK_NODE_LIST,
                                   cmark_parser_get_first_nonspace(parser) + 1);

      cmark_node_set_list (ret, data);
      cmark_node_set_syntax_extension(ret, self);
      cmark_node_set_html_attrs (ret, strdup("hotdoc-flex-list=\"true\""));
      parent_container = ret;
    }

    // add the list item
    ret = cmark_parser_add_child(parser, parent_container, CMARK_NODE_ITEM,
                                 cmark_parser_get_first_nonspace(parser) + 1);
    cmark_node_set_syntax_extension(ret, self);
    cmark_node_set_html_attrs (ret, strdup("hotdoc-flex-item=\"true\""));

    cmark_node_set_list (ret, data);
    mem->free(data);
  }

  return ret;
}

static bool flexlist_item_matches(cmark_syntax_extension *self,
                                  cmark_parser *parser,
                                  const char *input,
                                  cmark_node *container) {
  bool res = false;
  cmark_list *as_list = cmark_node_get_list (container);

  if (cmark_node_get_type (container) == CMARK_NODE_LIST) {
    res = true;
    goto done;
  }

  if (cmark_parser_get_indent (parser) >=
      as_list->marker_offset + as_list->padding) {
    cmark_parser_advance_offset(parser, input, as_list->marker_offset +
                                        as_list->padding, true);
    res = true;
  } else if (cmark_parser_is_blank (parser) && cmark_node_get_first_child (container) != NULL) {
    cmark_parser_advance_offset(parser, input, cmark_parser_get_first_nonspace (parser) - cmark_parser_get_offset (parser),
                     false);
    res = true;
  }

done:
  return res;
}

cmark_syntax_extension *cmark_flexlist_extension_new(void) {
  cmark_syntax_extension *ext = cmark_syntax_extension_new("flex-list");

  ext->try_opening_block = try_opening_flexlist_block;
  ext->last_block_matches = flexlist_item_matches;

  return ext;
}

static cmark_node *strikethrough_match(cmark_syntax_extension *self,
                                       cmark_parser *parser,
                                       cmark_node *parent,
                                       unsigned char character,
                                       cmark_inline_parser *inline_parser)
{
  cmark_node *res = NULL;
  bool left_flanking, right_flanking, punct_before, punct_after;
  int num_delims;

  /* Exit early */
  if (character != '~')
    return NULL;

  num_delims = cmark_inline_parser_scan_delimiters(inline_parser, 1, '~',
      &left_flanking, &right_flanking, &punct_before, &punct_after);

  if (num_delims > 0) { /* Should not be needed */
    bool can_open, can_close;

    res = cmark_node_new(CMARK_NODE_TEXT);
    cmark_node_set_literal(res, "~");

    can_open = left_flanking;
    can_close = right_flanking;
    if (can_open || can_close)
      cmark_inline_parser_push_delimiter(inline_parser, character, can_open, can_close, res);
  }

  return res;
}

static delimiter *strikethrough_insert(cmark_syntax_extension *self,
                                       cmark_parser *parser,
                                       cmark_inline_parser *inline_parser,
                                       delimiter *opener,
                                       delimiter *closer)
{
  cmark_node *strikethrough;
  cmark_node *tmp, *next;
  delimiter *delim, *tmp_delim;
  delimiter *res = closer->next;

  strikethrough = opener->inl_text;
  cmark_node_set_type(strikethrough, CMARK_NODE_STRIKETHROUGH);
  cmark_node_set_string_content(strikethrough, "~");
  tmp = cmark_node_next(opener->inl_text);

  while (tmp) {
    if (tmp == closer->inl_text)
      break;
    next = cmark_node_next(tmp);
    cmark_node_append_child(strikethrough, tmp);
    tmp = next;
  }

  cmark_node_free(closer->inl_text);

  delim = closer;
  while (delim != NULL && delim != opener) {
    tmp_delim = delim->previous;
    cmark_inline_parser_remove_delimiter(inline_parser, delim);
    delim = tmp_delim;
  }

  cmark_inline_parser_remove_delimiter(inline_parser, opener);

  return res;
}

cmark_syntax_extension *cmark_strikethrough_extension_new(void) {
  cmark_syntax_extension *ext = cmark_syntax_extension_new("tilde_strikethrough");

  ext->match_inline = strikethrough_match;
  ext->insert_inline_from_delim = strikethrough_insert;
  ext->special_inline_chars = cmark_llist_append(ext->special_inline_chars,
      (void *) '~');

  return ext;
}

bool init_libcmarkextensions(cmark_plugin *plugin) {
  cmark_plugin_register_syntax_extension(plugin, cmark_table_extension_new());
  cmark_plugin_register_syntax_extension(plugin, cmark_flexlist_extension_new());
  cmark_plugin_register_syntax_extension(plugin, cmark_strikethrough_extension_new());
  return true;
}
