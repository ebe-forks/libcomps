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

#include "comps_utils.h"

void* __comps_str_clone(void *str) {
    char *ret;
    if (str == NULL)
        return NULL;
    ret = malloc(sizeof(char) * (strlen((char*)str)+1));
    if (ret==NULL) return NULL;
    memcpy(ret, str, sizeof(char) * (strlen((char*)str)+1));
    return ret;
}

inline char __comps_strcmp(void *s1, void *s2) {
    if (s1 == NULL && s2 == NULL) return 1;
    else if ((s1 == NULL && s2 != NULL) || (s1 != NULL && s2 == NULL)) return 0;
    return (strcmp((const char*)s1, (const char*)s2) == 0);
}

inline void __comps_xml_prop(char *key, char *val,
                             xmlTextWriterPtr writer) {
    xmlTextWriterStartElement(writer, BAD_CAST key);
    xmlTextWriterWriteString(writer, BAD_CAST val);
    xmlTextWriterEndElement(writer);
}

char* __comps_num2boolstr(COMPS_Object* obj) {
    char *ret;
    char *_bool;
    if (((COMPS_Num*)obj)->val) {
        _bool = "true";
    } else {
        _bool = "false";
    }
    ret = malloc(sizeof(char) * (strlen(_bool)+1));
    ret[0] = 0;
    strcat(ret, _bool);
    return ret;
}
