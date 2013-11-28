/* libcomps - C alternative to yum.comps library
 * Copyright (C) 2013 Jindrich Luza
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to  Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301,
 * USA
 */

#include <stdio.h>
#include <ctype.h>
#include <signal.h>

#include "comps_types.h"
#include "comps_parse.h"
#include "comps_elem.h"

#define BUFF_SIZE 1024

#define XML_DTD
void comps_parse_check_attributes(COMPS_Parsed *parsed, COMPS_Elem* elem);

inline unsigned __comps_is_whitespace_only(const char * s, int len)
{
    int i;
    for (i = 0; i<len; i++) {
        if (!isspace(s[i])) return 0;
    }
    return 1;
}
COMPS_Parsed* comps_parse_parsed_create() {
    COMPS_Parsed *ret;
    ret =  malloc(sizeof(COMPS_Parsed));
    if (ret == NULL) return NULL;
    return ret;
}

unsigned comps_parse_parsed_init(COMPS_Parsed * parsed, const char * encoding,
                                 char log_stdout) {
    if (parsed == NULL)
        return 0;
    parsed->parser = XML_ParserCreate((const XML_Char*)encoding);

    XML_SetDefaultHandler(parsed->parser, &comps_parse_def_handler);
    XML_SetElementHandler(parsed->parser, &comps_parse_start_elem_handler,
                              &comps_parse_end_elem_handler);
    XML_SetCharacterDataHandler(parsed->parser, &comps_parse_char_data_handler);

    parsed->enc = encoding;
    parsed->elem_stack = comps_list_create();
    parsed->text_buffer = comps_list_create();
    parsed->text_buffer_len = 0;
    parsed->text_buffer_pt = NULL;
    parsed->tmp_buffer = NULL;
    parsed->log = (COMPS_Log*)comps_object_create(&COMPS_Log_ObjInfo, NULL);
    parsed->log->std_out = log_stdout;
    parsed->comps_doc = NULL;
    parsed->fatal_error = 0;
    if (parsed->elem_stack == NULL || parsed->text_buffer == NULL) {
        if (!parsed->elem_stack)
            comps_list_destroy(&parsed->elem_stack);
        if (!parsed->text_buffer)
            comps_list_destroy(&parsed->text_buffer);
        COMPS_OBJECT_DESTROY(parsed->log);
        free(parsed);
        return 0;
    }
    comps_list_init(parsed->elem_stack);
    comps_list_init(parsed->text_buffer);
    XML_SetUserData(parsed->parser, parsed);
    return 1;
}

void comps_parse_parsed_reinit(COMPS_Parsed *parsed) {
    parsed->fatal_error = 0;
    XML_ParserReset(parsed->parser, parsed->enc);
    XML_SetDefaultHandler(parsed->parser, &comps_parse_def_handler);
    XML_SetElementHandler(parsed->parser, &comps_parse_start_elem_handler,
                                          &comps_parse_end_elem_handler);
    XML_SetCharacterDataHandler(parsed->parser, &comps_parse_char_data_handler);
    XML_SetUserData(parsed->parser, parsed);
    comps_list_clear(parsed->elem_stack);
    comps_list_clear(parsed->text_buffer);
    comps_hslist_clear(parsed->log->entries);
    COMPS_OBJECT_DESTROY(parsed->comps_doc);
    //comps_doc_destroy(&parsed->comps_doc);
}

void comps_parse_parsed_destroy(COMPS_Parsed *parsed) {
    comps_list_destroy(&parsed->elem_stack);
    comps_list_destroy(&parsed->text_buffer);
    COMPS_OBJECT_DESTROY(parsed->log);
    //if (parsed->log)
    //    comps_log_destroy(parsed->log);
    COMPS_OBJECT_DESTROY(parsed->comps_doc);
    //comps_doc_destroy(&parsed->comps_doc);
    XML_ParserFree(parsed->parser);
    free(parsed);
}

void empty_xmlGenericErrorFunc(void * ctx, const char * msg, ...) {
    (void) ctx;
    (void) msg;
}

int comps_parse_validate_dtd(char *filename, char *dtd_file) {
    xmlDocPtr fptr;
    xmlDtdPtr dtd_ptr;
    xmlValidCtxtPtr vctxt;
    int ret;
    xmlErrorPtr err;

    fptr = xmlReadFile(filename, NULL, 0);
    if (fptr == NULL) {
        return -1;
    }

    dtd_ptr = xmlParseDTD(NULL, (const xmlChar*)dtd_file);
    if (dtd_ptr == NULL) {
        xmlFreeDoc(fptr);
        return -2;
    }

    vctxt = xmlNewValidCtxt();
    xmlSetGenericErrorFunc(vctxt, empty_xmlGenericErrorFunc);
    if (vctxt == NULL) {
        xmlFreeDoc(fptr);
        xmlFreeDtd(dtd_ptr);
        return -3;
    }
    ret = xmlValidateDtd(vctxt, fptr, dtd_ptr);
    if (!ret) {
        err = xmlGetLastError();
        printf("%s\n", err->message);
    }
    xmlFreeDoc(fptr);
    xmlFreeDtd(dtd_ptr);
    xmlFreeValidCtxt(vctxt);
    if (!ret)
        return -err->code;
    else
        return ret;
}

signed char comps_parse_file(COMPS_Parsed *parsed, FILE *f,
                             COMPS_DefaultsOptions *options) {
    void *buff;
    int bytes_read;

    if (!f) {
        comps_log_error(parsed->log, COMPS_ERR_READFD, 0);
        parsed->fatal_error = 1;
        return -1;
    }
    comps_parse_parsed_reinit(parsed);
    if (options)
        parsed->def_options = options;
    else
        parsed->def_options = &COMPS_DDefaultsOptions;

    for (;;) {
        buff = XML_GetBuffer(parsed->parser, BUFF_SIZE);
        if (buff == NULL) {
            comps_log_error(parsed->log, COMPS_ERR_MALLOC, 0);
            raise(SIGABRT);
            return -1;
        }
        bytes_read = fread(buff, sizeof(char), BUFF_SIZE, f);
        if (bytes_read < 0)
            comps_log_error(parsed->log, COMPS_ERR_READFD, 0);
        if (!XML_ParseBuffer(parsed->parser, bytes_read, bytes_read == 0)) {
            comps_log_error_x(parsed->log, COMPS_ERR_PARSER, 3,
                          comps_num(XML_GetCurrentLineNumber(parsed->parser)),
                          comps_num(XML_GetCurrentColumnNumber(parsed->parser)),
                          comps_str(XML_ErrorString(
                                    XML_GetErrorCode(parsed->parser))));
            parsed->fatal_error = 1;
        }
        if (bytes_read == 0) break;
    }
    fclose(f);
    if (parsed->fatal_error == 0 && parsed->log->entries->first == NULL)
        return 0;
    else if (parsed->fatal_error != 1)
        return 1;
    else
        return -1;
}

signed char comps_parse_str(COMPS_Parsed *parsed, char *str,
                            COMPS_DefaultsOptions *options) {
    if (options)
        parsed->def_options = options;
    else
        parsed->def_options = &COMPS_DDefaultsOptions;

    if (!XML_Parse(parsed->parser, str, strlen(str), 1)) {
        comps_log_error_x(parsed->log, COMPS_ERR_PARSER, 3,
                          comps_num(XML_GetCurrentLineNumber(parsed->parser)),
                          comps_num(XML_GetCurrentColumnNumber(parsed->parser)),
                          comps_str(XML_ErrorString(
                                            XML_GetErrorCode(parsed->parser))));
        parsed->fatal_error = 1;
    }
    if (parsed->fatal_error == 0 && parsed->log->entries->first == NULL)
        return 0;
    else if (parsed->fatal_error != 1)
        return 1;
    else
        return -1;
}

void comps_parse_end_elem_handler(void *userData, const XML_Char *s) {
    COMPS_ListItem * item;
    char * alltext = NULL;
    size_t item_len;
    int index=0;
    #define parser_line XML_GetCurrentLineNumber(((COMPS_Parsed*)userData)->parser)
    #define parser_col XML_GetCurrentColumnNumber(((COMPS_Parsed*)userData)->parser)
    #define parsed ((COMPS_Parsed*)userData)
    #define last_elem ((COMPS_Elem*)parsed->elem_stack->last->data)

    /* check if there's some text in recent element - are we interested in?*/
    if (parsed->text_buffer_len) {
        alltext = malloc(sizeof(char)*(parsed->text_buffer_len + 1));
        if (alltext == NULL) {
            comps_log_error(parsed->log, COMPS_ERR_MALLOC, 0);
            raise(SIGABRT);
        }
    }
    while ((item = comps_list_shift(parsed->text_buffer)) != NULL) {
        item_len = strlen((char*)item->data);
        memcpy(alltext + index, item->data, item_len * sizeof(char));
        comps_list_item_destroy(item);
        index += item_len;
    }
    /* set zero char at the end of string */
    if (alltext)
        memset(alltext+parsed->text_buffer_len, 0, sizeof(char));
    parsed->tmp_buffer = alltext;

    /* start postprocess for currently processed elements */
    if (comps_elem_get_type(s) == last_elem->type) {
        if (last_elem->valid && COMPS_ElemInfos[last_elem->type]->postproc) {
            COMPS_ElemInfos[last_elem->type]->postproc((COMPS_Parsed*)userData,
                                                       last_elem);
        }
        if (last_elem->valid && parsed->tmp_buffer) {
            comps_log_error_x(parsed->log, COMPS_ERR_TEXT_BETWEEN, 3,
                              comps_str(parsed->tmp_buffer), comps_num(parser_line),
                              comps_num(parser_col));
        }
        /* finaly, remove element from element stack */
        item = comps_list_pop(parsed->elem_stack);
        comps_list_item_destroy(item);
    }
    free(alltext);
    parsed->text_buffer_len = 0;
    #undef parsed
    #undef parser_line
    #undef parser_col
}

void comps_parse_def_handler(void *userData, const XML_Char *s, int len) {
    (void) userData;
    (void) s;
    (void) len;
}

void comps_parse_start_elem_handler(void *userData,
                              const XML_Char *s,
                              const XML_Char **attrs) {
    #define parser_line XML_GetCurrentLineNumber(((COMPS_Parsed*)userData)->parser)
    #define parser_col XML_GetCurrentColumnNumber(((COMPS_Parsed*)userData)->parser)
    #define ELEMINFO  COMPS_ElemInfos[elem->type]
    #define LAST ((COMPS_Parsed*)userData)->elem_stack->last
    #define LASTELEM  ((COMPS_Elem*)((COMPS_Parsed*)userData)->elem_stack->last->data)

    COMPS_Elem * elem = NULL;
    COMPS_ElemType type;
    COMPS_ListItem * item;

    /* create new element */
    type = comps_elem_get_type(s);
    elem = comps_elem_create(s, attrs, type);
    if (elem == NULL) {
        comps_log_error_x(((COMPS_Parsed*)userData)->log, COMPS_ERR_MALLOC, 0);
        raise(SIGABRT);
        return;
    }
    elem->valid = 0;

    if (ELEMINFO->ancestors[0] != COMPS_ELEM_NONE) {
        if (!LAST) {

        } else {
            elem->ancestor = LASTELEM;
            for (int x = 0; ELEMINFO->ancestors[x] != COMPS_ELEM_SENTINEL; x++){
                if (ELEMINFO->ancestors[x] == LASTELEM->type) {
                    elem->valid = 1;
                    break;
                }
            }
        }
    } else {
        elem->valid = 1;
    }
    if (!elem->valid) {
        comps_log_error_x(((COMPS_Parsed*)userData)->log,
                          COMPS_ERR_NOPARENT, 3, comps_str(s),
                          comps_num(parser_line),
                          comps_num(parser_col));
    }

    item = comps_list_item_create(elem, NULL, &comps_elem_destroy);
    if (((COMPS_Parsed*)userData)->text_buffer->first) {
        comps_log_error_x(((COMPS_Parsed*)userData)->log,
                          COMPS_ERR_TEXT_BETWEEN, 3,
                          comps_str((char*)((COMPS_Parsed*)userData)
                                         ->text_buffer->first->data),
                          comps_num(parser_line), comps_num(parser_col));
        comps_list_clear(((COMPS_Parsed*)userData)->text_buffer);
        ((COMPS_Parsed*)userData)->text_buffer_len = 0;
    }

    /* end append it to element stack */
    comps_list_append(((COMPS_Parsed*)userData)->elem_stack, item);
    if (ELEMINFO->attributes) {
       comps_parse_check_attributes(((COMPS_Parsed*)userData), elem);
    }
    
    
    /* preprocess new element */
    if (ELEMINFO->preproc && elem->valid) {
        ELEMINFO->preproc((COMPS_Parsed*)userData, elem);
    } else {

    }

    #undef parser_line
    #undef parser_col
    #undef ELEMINFO
    #undef LAST
    #undef LASTELEM
}


void comps_parse_char_data_handler(void *userData,
                            const XML_Char *s,
                            int len) {

    char * c = NULL;
    COMPS_ListItem * it;

    /* skip whitespace data */
    if (__comps_is_whitespace_only(s, len)) {
        return;
    }
    if ((c = malloc(sizeof(char) * (len+1))) == NULL) {
        comps_log_error(((COMPS_Parsed*)userData)->log, COMPS_ERR_MALLOC, 0);
        raise(SIGABRT);
        return;
    }

    /* copy text data of element to temporal string and append it to text_buffer
       list*/
    memcpy(c, s, sizeof(char) * len);
    memset(c+len, 0, sizeof(char));
    /* and increment total lenght of strings in text_buffer */
    ((COMPS_Parsed*)userData)->text_buffer_len += len;

    it = comps_list_item_create((void*)c, NULL, &free);
        comps_list_append(((COMPS_Parsed*)userData)->text_buffer, it);

}

void comps_parse_check_attributes(COMPS_Parsed *parsed, COMPS_Elem* elem) {
    #define parser_line XML_GetCurrentLineNumber(parsed->parser)
    #define parser_col XML_GetCurrentColumnNumber(parsed->parser)
    const COMPS_ElemInfo *info;
    info = COMPS_ElemInfos[elem->type];
    int attr_count;
    COMPS_HSList *keys;
    char *val;
    COMPS_HSListItem *it;

    for (attr_count = 0; info->attributes[attr_count] != NULL; attr_count++);
    keys = comps_dict_keys(elem->attrs);
    for (int x =0; x<attr_count; x++) {
        for (it = keys->first; it != NULL; it = it->next) {
            if (strcmp((char*)it->data, info->attributes[x]->name) == 0) {
                if (info->attributes[x]->val_check) {
                    val = comps_dict_get(elem->attrs, it->data);
                    if (!info->attributes[x]->val_check(val)) {
                        ///error
                    }
                    
                }
                comps_hslist_remove(keys, it);
                keys->data_destructor(it->data);
                free(it);
                break;
            }
        }
    }
    for (it = keys->first; it != NULL; it = it->next) {
        comps_log_warning_x(parsed->log, COMPS_ERR_ATTR_UNKNOWN, 4,
                            comps_str(it->data), comps_str(info->name),
                            comps_num(parser_line), comps_num(parser_col));
    }
    comps_hslist_destroy(&keys);
    #undef parser_line
    #undef parser_col
}
