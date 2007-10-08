/*
 * rasqal_xsd_datatypes.c - Rasqal XML Schema Datatypes support
 *
 * $Id$
 *
 * Copyright (C) 2005-2007, David Beckett http://purl.org/net/dajobe/
 * Copyright (C) 2005-2005, University of Bristol, UK http://www.bristol.ac.uk/
 * 
 * This package is Free Software and part of Redland http://librdf.org/
 * 
 * It is licensed under the following three licenses as alternatives:
 *   1. GNU Lesser General Public License (LGPL) V2.1 or any newer version
 *   2. GNU General Public License (GPL) V2 or any newer version
 *   3. Apache License, V2.0 or any newer version
 * 
 * You may not use this file except in compliance with at least one of
 * the above three licenses.
 * 
 * See LICENSE.html or LICENSE.txt at the top of this package for the
 * complete terms and further detail along with the license texts for
 * the licenses in COPYING.LIB, COPYING and LICENSE-2.0.txt respectively.
 * 
 * 
 */

#ifdef HAVE_CONFIG_H
#include <rasqal_config.h>
#endif

#ifdef WIN32
#include <win32_rasqal_config.h>
#endif

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#include <stdarg.h>

#include "rasqal.h"
#include "rasqal_internal.h"


/* libxml2 datatypes */
#ifdef HAVE_LIBXML_XMLSCHEMAS_H
#include <libxml/xmlschemas.h>
#include <libxml/xmlschemastypes.h>
#include <libxml/schemasInternals.h>
#endif


/*
 *
 * References
 *
 * XPath Functions and Operators
 * http://www.w3.org/TR/xpath-functions/
 *
 * Datatypes hierarchy
 * http://www.w3.org/TR/xpath-functions/#datatypes
 * 
 * Casting
 * http://www.w3.org/TR/xpath-functions/#casting-from-primitive-to-primitive
 *
 */


typedef struct {
  const char *name;
  raptor_uri* uri;
  int (*check)(const unsigned char* string, int flags);
} rasqal_xsd_datatype_info;

#if 0
#define RASQAL_XPFO_BASE_URI "http://www.w3.org/2004/07/xpath-functions"

#define RASQAL_SPARQL_OP_NAMESPACE_URI "http://www.w3.org/2001/sw/DataAccess/operations"

#define RASQAL_XSD_DATATYPES_SIZE 7

typedef enum {
  DT_dateTime,
  DT_time,
  DT_date,
  DT_string,
  DT_numeric,
  DT_double,
  DT_integer,
} rasqal_xsd_datatype_id;
#endif


static int
rasqal_xsd_check_boolean_format(const unsigned char* string, int flags) 
{
  /* FIXME
   * Strictly only {true, false, 1, 0} are allowed according to
   * http://www.w3.org/TR/xmlschema-2/#boolean
   */
  if(!strcmp((const char*)string, "true") || 
     !strcmp((const char*)string, "TRUE") ||
     !strcmp((const char*)string, "1") ||
     !strcmp((const char*)string, "false") || 
     !strcmp((const char*)string, "FALSE") ||
     !strcmp((const char*)string, "0"))
    return 1;

  return 0;
}


#define ADVANCE_OR_DIE(p) if(!*(++p)) return 0;


/**
 * rasqal_xsd_check_dateTime_format:
 * @string: lexical form string
 * flags: flags
 *
 * INTERNAL - Check an XSD dateTime lexical form
 *
 * Return value: non-0 if the string is valid
 */
static int
rasqal_xsd_check_dateTime_format(const unsigned char* string, int flags) 
{
  const char* p;
  
  /* FIXME validate dateTime format:
   * according to http://www.w3.org/TR/xmlschema-2/#dateTime
   *
   * '-'? yyyy '-' mm '-' dd 'T' hh ':' mm ':' ss ('.' s+)? (zzzzzz)?
   *
   * and does not check the fields are valid ranges.  This lets through
   * 9999-99-99T99:99:99Z and does not check leap years, days in months
   * etc. etc.
   */
  p=(const char*)string;
  if(*p == '-') {
    ADVANCE_OR_DIE(p);
  }
  /* YYYY */
  if(isdigit(*p)) {
    ADVANCE_OR_DIE(p);
  }
  if(isdigit(*p)) {
    ADVANCE_OR_DIE(p);
  }
  if(isdigit(*p)) {
    ADVANCE_OR_DIE(p);
  }
  if(isdigit(*p)) {
    ADVANCE_OR_DIE(p);
  }
  if(*p != '-')
     return 0;
  ADVANCE_OR_DIE(p);
  /* MM */
  if(isdigit(*p)) {
    ADVANCE_OR_DIE(p);
  }
  if(isdigit(*p)) {
    ADVANCE_OR_DIE(p);
  }
  if(*p != '-')
    return 0;
  ADVANCE_OR_DIE(p);
  /* DD */
  if(isdigit(*p)) {
    ADVANCE_OR_DIE(p);
  }
  if(isdigit(*p)) {
    ADVANCE_OR_DIE(p);
  }
  if(*p != 'T')
    return 0;
  ADVANCE_OR_DIE(p);
  /* HH */
  if(isdigit(*p)) {
    ADVANCE_OR_DIE(p);
  }
  if(isdigit(*p)) {
    ADVANCE_OR_DIE(p);
  }
  if(*p != ':')
    return 0;
  ADVANCE_OR_DIE(p);
  /* MM */
  if(isdigit(*p)) {
    ADVANCE_OR_DIE(p);
  }
  if(isdigit(*p)) {
    ADVANCE_OR_DIE(p);
  }
  if(*p != ':')
    return 0;
  ADVANCE_OR_DIE(p);
  /* SS */
  if(isdigit(*p)) {
    ADVANCE_OR_DIE(p);
  }
  if(isdigit(*p))
    p++;

  /* optional end before . */
  if(!*p)
    return 1;
  /* next char may be '.' */
  if(*p == '.') {
    p++;
    while(*p && isdigit(*p))
      p++;
    /* optional end after extra .s+ digits */
    if(!p)
      return 0;
  }
  if(*p == 'Z') {
    p++;
    /* must end at this point */
    if(!*p)
      return 1;
    else
      return 0;
  }
  
  /* FIXME - ignoring  the full syntax of timezone, a string of the form:
   *   (('+' | '-') hh ':' mm) | 'Z'
   *
   */
  
  return 1;
}


/**
 * rasqal_xsd_check_decimal_format:
 * @string: lexical form string
 * flags: flags
 *
 * INTERNAL - Check an XSD decimal lexical form
 *
 * Return value: non-0 if the string is valid
 */
static int
rasqal_xsd_check_decimal_format(const unsigned char* string, int flags) 
{
  const char* p;
  
  /* This should be correct according to 
   * http://www.w3.org/TR/xmlschema-2/#decimal
   */
  p=(const char*)string;
  if(*p == '+' || *p == '-') {
    ADVANCE_OR_DIE(p);
  }

  while(*p && isdigit(*p))
    p++;
  if(!*p)
    return 1;
  /* Fail if first non-digit is not '.' */
  if(*p != '.')
    return 0;
  p++;
  
  while(*p && isdigit(*p))
    p++;
  /* Fail if anything other than a digit seen before NUL */
  if(*p)
    return 0;

  return 1;
}


/**
 * rasqal_xsd_check_double_format:
 * @string: lexical form string
 * flags: flags
 *
 * INTERNAL - Check an XSD double lexical form
 *
 * Return value: non-0 if the string is valid
 */
static int
rasqal_xsd_check_double_format(const unsigned char* string, int flags) 
{
  /* FIXME validate using
   * http://www.w3.org/TR/xmlschema-2/#double
   */
  double d=0.0;
  char* eptr=NULL;

  d=strtod((const char*)string, &eptr);
  if((unsigned char*)eptr != string && *eptr=='\0')
    return 1;

  return 0;
}


/**
 * rasqal_xsd_check_float_format:
 * @string: lexical form string
 * flags: flags
 *
 * INTERNAL - Check an XSD float lexical form
 *
 * Return value: non-0 if the string is valid
 */
static int
rasqal_xsd_check_float_format(const unsigned char* string, int flags) 
{
  /* FIXME validate using
   * http://www.w3.org/TR/xmlschema-2/#float
   */
  double d=0.0;
  char* eptr=NULL;

  d=strtod((const char*)string, &eptr);
  if((unsigned char*)eptr != string && *eptr=='\0')
    return 1;

  return 0;
}


/**
 * rasqal_xsd_check_integer_format:
 * @string: lexical form string
 * flags: flags
 *
 * INTERNAL - Check an XSD integer lexical form
 *
 * Return value: non-0 if the string is valid
 */
static int
rasqal_xsd_check_integer_format(const unsigned char* string, int flags)
{
  long int v;
  char* eptr=NULL;

  /* This should be correct according to 
   * http://www.w3.org/TR/xmlschema-2/#integer
   */

  v=(int)strtol((const char*)string, &eptr, 10);

  if((unsigned char*)eptr != string && *eptr=='\0')
    return 1;

  return 0;
}


#if 0
typedef rasqal_literal* (*rasqal_extension_fn)(raptor_uri* name, raptor_sequence *args, char **error_p);


typedef struct {
  const unsigned char *name;
  int min_nargs;
  int max_nargs;
  rasqal_extension_fn fn;
  raptor_uri* uri;
} rasqal_xsd_datatype_fn_info;
#endif


static const char* sparql_xsd_names[RASQAL_LITERAL_LAST_XSD+2]=
{
  NULL, NULL, NULL, 
  "string",
  "boolean",
  "integer",
  "double",
  "float",
  "decimal",
  "dateTime",
  NULL
};


static rasqal_xsd_datatype_info* sparql_xsd_datatypes_table;

raptor_uri* rasqal_xsd_namespace_uri=NULL;

raptor_uri* rasqal_xsd_integer_uri=NULL;
raptor_uri* rasqal_xsd_double_uri=NULL;
raptor_uri* rasqal_xsd_float_uri=NULL;
raptor_uri* rasqal_xsd_boolean_uri=NULL;
raptor_uri* rasqal_xsd_decimal_uri=NULL;
raptor_uri* rasqal_xsd_datetime_uri=NULL;
raptor_uri* rasqal_xsd_string_uri=NULL;


int
rasqal_xsd_init(void) 
{
  int i;

  rasqal_xsd_namespace_uri=raptor_new_uri(raptor_xmlschema_datatypes_namespace_uri);
  
  rasqal_xsd_integer_uri=raptor_new_uri_from_uri_local_name(rasqal_xsd_namespace_uri, (const unsigned char*)"integer");
  rasqal_xsd_double_uri=raptor_new_uri_from_uri_local_name(rasqal_xsd_namespace_uri, (const unsigned char*)"double");
  rasqal_xsd_float_uri=raptor_new_uri_from_uri_local_name(rasqal_xsd_namespace_uri, (const unsigned char*)"float");
  rasqal_xsd_boolean_uri=raptor_new_uri_from_uri_local_name(rasqal_xsd_namespace_uri, (const unsigned char*)"boolean");
  rasqal_xsd_decimal_uri=raptor_new_uri_from_uri_local_name(rasqal_xsd_namespace_uri, (const unsigned char*)"decimal");
  rasqal_xsd_datetime_uri=raptor_new_uri_from_uri_local_name(rasqal_xsd_namespace_uri, (const unsigned char*)"dateTime");
  rasqal_xsd_string_uri=raptor_new_uri_from_uri_local_name(rasqal_xsd_namespace_uri, (const unsigned char*)"string");

  sparql_xsd_datatypes_table=(rasqal_xsd_datatype_info*)RASQAL_CALLOC(rasqal_xsd_datatype_info, RASQAL_LITERAL_LAST_XSD+2, sizeof(rasqal_xsd_datatype_info));
  if(!sparql_xsd_datatypes_table)
    return 1;
  
  for(i=RASQAL_LITERAL_FIRST_XSD; i <= RASQAL_LITERAL_LAST_XSD; i++) {
    sparql_xsd_datatypes_table[i].name=sparql_xsd_names[i];
  }
  
  sparql_xsd_datatypes_table[RASQAL_LITERAL_STRING].uri=rasqal_xsd_string_uri;
  sparql_xsd_datatypes_table[RASQAL_LITERAL_BOOLEAN].uri=rasqal_xsd_boolean_uri;
  sparql_xsd_datatypes_table[RASQAL_LITERAL_INTEGER].uri=rasqal_xsd_integer_uri;
  sparql_xsd_datatypes_table[RASQAL_LITERAL_DOUBLE].uri=rasqal_xsd_double_uri;
  sparql_xsd_datatypes_table[RASQAL_LITERAL_FLOAT].uri=rasqal_xsd_float_uri;
  sparql_xsd_datatypes_table[RASQAL_LITERAL_DECIMAL].uri=rasqal_xsd_decimal_uri;
  sparql_xsd_datatypes_table[RASQAL_LITERAL_DATETIME].uri=rasqal_xsd_datetime_uri;

  sparql_xsd_datatypes_table[RASQAL_LITERAL_BOOLEAN].check=rasqal_xsd_check_boolean_format;
  sparql_xsd_datatypes_table[RASQAL_LITERAL_INTEGER].check=rasqal_xsd_check_integer_format;
  sparql_xsd_datatypes_table[RASQAL_LITERAL_DOUBLE].check=rasqal_xsd_check_double_format;
  sparql_xsd_datatypes_table[RASQAL_LITERAL_FLOAT].check=rasqal_xsd_check_float_format;
  sparql_xsd_datatypes_table[RASQAL_LITERAL_DECIMAL].check=rasqal_xsd_check_decimal_format;
  sparql_xsd_datatypes_table[RASQAL_LITERAL_DATETIME].check=rasqal_xsd_check_dateTime_format;
  
  return 0;
}

void
rasqal_xsd_finish(void) 
{
  if(sparql_xsd_datatypes_table)
    RASQAL_FREE(table, sparql_xsd_datatypes_table);

  if(rasqal_xsd_integer_uri)
    raptor_free_uri(rasqal_xsd_integer_uri);
  if(rasqal_xsd_double_uri)
    raptor_free_uri(rasqal_xsd_double_uri);
  if(rasqal_xsd_float_uri)
    raptor_free_uri(rasqal_xsd_float_uri);
  if(rasqal_xsd_boolean_uri)
    raptor_free_uri(rasqal_xsd_boolean_uri);
  if(rasqal_xsd_decimal_uri)
    raptor_free_uri(rasqal_xsd_decimal_uri);
  if(rasqal_xsd_datetime_uri)
    raptor_free_uri(rasqal_xsd_datetime_uri);
  if(rasqal_xsd_string_uri)
    raptor_free_uri(rasqal_xsd_string_uri);

  if(rasqal_xsd_namespace_uri)
    raptor_free_uri(rasqal_xsd_namespace_uri);
}
 

  
rasqal_literal_type
rasqal_xsd_datatype_uri_to_type(raptor_uri* uri)
{
  int i;
  rasqal_literal_type native_type=RASQAL_LITERAL_UNKNOWN;
  
  if(!uri)
    return native_type;
  
  for(i=(int)RASQAL_LITERAL_FIRST_XSD; i <= (int)RASQAL_LITERAL_LAST_XSD; i++) {
    if(raptor_uri_equals(uri, sparql_xsd_datatypes_table[i].uri)) {
      native_type=(rasqal_literal_type)i;
      break;
    }
  }
  return native_type;
}


raptor_uri*
rasqal_xsd_datatype_type_to_uri(rasqal_literal_type type)
{
  if(type >= RASQAL_LITERAL_FIRST_XSD && type <= (int)RASQAL_LITERAL_LAST_XSD)
    return sparql_xsd_datatypes_table[(int)type].uri;
  else
    return NULL;
}


/**
 * rasqal_xsd_datatype_check:
 * @native_type: rasqal XSD type
 * @string: string
 * @flags: check flags
 *
 * INTERNAL - check a string as a valid lexical form of an XSD datatype
 *
 * Return value: non-0 if the string is valid
 */
int
rasqal_xsd_datatype_check(rasqal_literal_type native_type, 
                          const unsigned char* string, int flags)
{
  if(sparql_xsd_datatypes_table[native_type].check)
    return sparql_xsd_datatypes_table[native_type].check(string, flags);
  else
    return 1;
}


const char*
rasqal_xsd_datatype_label(rasqal_literal_type native_type)
{
  return sparql_xsd_datatypes_table[native_type].name;
}


int
rasqal_xsd_is_datatype_uri(raptor_uri* uri)
{
  return (rasqal_xsd_datatype_uri_to_type(uri) != RASQAL_LITERAL_UNKNOWN);
}


#if 0
static rasqal_literal*
rasqal_xsd_datatypes_date_less_than(raptor_uri* name, raptor_sequence *args,
                                    char **error_p) {
  int error=0;
  int b;
  rasqal_literal* l1;
  rasqal_literal* l2;
  
  if(raptor_sequence_size(args) != 2)
    return NULL;
  
  l1=(rasqal_literal*)raptor_sequence_get_at(args, 0);
  l2=(rasqal_literal*)raptor_sequence_get_at(args, 1);
  
  b=(rasqal_literal_compare(l1, l2, 0, &error) < 0);
  if(error)
    return NULL;

  return rasqal_new_boolean_literal(b);
}


static rasqal_literal*
rasqal_xsd_datatypes_date_greater_than(raptor_uri* name, raptor_sequence *args,
                                       char **error_p) {
  int error=0;
  int b;
  rasqal_literal* l1;
  rasqal_literal* l2;
  
  if(raptor_sequence_size(args) != 2)
    return NULL;
  
  l1=(rasqal_literal*)raptor_sequence_get_at(args, 0);
  l2=(rasqal_literal*)raptor_sequence_get_at(args, 1);
  
  b=(rasqal_literal_compare(l1, l2, 0, &error) > 0);
  if(error)
    return NULL;

  return rasqal_new_boolean_literal(b);
}


static rasqal_literal*
rasqal_xsd_datatypes_date_equal(raptor_uri* name, raptor_sequence *args,
                                char **error_p) {
  int error=0;
  int b;
  rasqal_literal* l1;
  rasqal_literal* l2;
  
  if(raptor_sequence_size(args) != 2)
    return NULL;
  
  l1=(rasqal_literal*)raptor_sequence_get_at(args, 0);
  l2=(rasqal_literal*)raptor_sequence_get_at(args, 1);
  
  b=(rasqal_literal_compare(l1, l2, 0, &error) == 0);
  if(error)
    return NULL;

  return rasqal_new_boolean_literal(b);
}


#define RASQAL_XSD_DATATYPE_FNS_SIZE 9
static rasqal_xsd_datatype_fn_info rasqal_xsd_datatype_fns[RASQAL_XSD_DATATYPE_FNS_SIZE]={
  { (const unsigned char*)"date-less-than",        1, 1, rasqal_xsd_datatypes_date_less_than },
  { (const unsigned char*)"dateTime-less-than",    1, 1, rasqal_xsd_datatypes_date_less_than },
  { (const unsigned char*)"time-less-than",        1, 1, rasqal_xsd_datatypes_date_less_than },
  { (const unsigned char*)"date-greater-than",     1, 1, rasqal_xsd_datatypes_date_greater_than },
  { (const unsigned char*)"dateTime-greater-than", 1, 1, rasqal_xsd_datatypes_date_greater_than },
  { (const unsigned char*)"time-greater-than",     1, 1, rasqal_xsd_datatypes_date_greater_than },
  { (const unsigned char*)"date-equal",            1, 1, rasqal_xsd_datatypes_date_equal },
  { (const unsigned char*)"dateTime-equal",        1, 1, rasqal_xsd_datatypes_date_equal },
  { (const unsigned char*)"time-equal",            1, 1, rasqal_xsd_datatypes_date_equal }
};



static raptor_uri* raptor_xpfo_base_uri=NULL;
static raptor_uri* rasqal_sparql_op_namespace_uri=NULL;


static void
rasqal_init_datatypes(void) {
  int i;
  
  raptor_xpfo_base_uri=raptor_new_uri((const unsigned char*)RASQAL_XPFO_BASE_URI);
  rasqal_sparql_op_namespace_uri=raptor_new_uri((const unsigned char*)RASQAL_SPARQL_OP_NAMESPACE_URI);

  for(i=0; i< RASQAL_XSD_DATATYPES_SIZE; i++) {
    rasqal_xsd_datatypes[i].uri=raptor_new_uri_from_uri_local_name(raptor_xpfo_base_uri,
                                                                   (const unsigned char*)rasqal_xsd_datatypes[i].name);
  }

  for(i=0; i< RASQAL_XSD_DATATYPE_FNS_SIZE; i++) {
    rasqal_xsd_datatype_fns[i].uri=raptor_new_uri_from_uri_local_name(rasqal_sparql_op_namespace_uri,
                                                                  rasqal_xsd_datatype_fns[i].name);
  }

}


static void
rasqal_finish_datatypes(void) {
  int i;
  
  for(i=0; i< RASQAL_XSD_DATATYPES_SIZE; i++)
    if(rasqal_xsd_datatypes[i].uri)
      raptor_free_uri(rasqal_xsd_datatypes[i].uri);

  for(i=0; i< RASQAL_XSD_DATATYPE_FNS_SIZE; i++)
    if(rasqal_xsd_datatype_fns[i].uri)
      raptor_free_uri(rasqal_xsd_datatype_fns[i].uri);

  if(raptor_xpfo_base_uri)
    raptor_free_uri(raptor_xpfo_base_uri);

  if(rasqal_sparql_op_namespace_uri)
    raptor_free_uri(rasqal_sparql_op_namespace_uri);
}
#endif


/*
 * 
 * Facets
 * 
 * Ordered
 * [Definition:] A value space, and hence a datatype, is said to be
 * ordered if there exists an order-relation defined for that
 * value space.
 * -- http://www.w3.org/TR/xmlschema-2/#dt-ordered
 * 
 * Bounded
 * [Definition:] A datatype is bounded if its value space has either
 * an inclusive upper bound or an exclusive upper bound and either
 * an inclusive lower bound or an exclusive lower bound.
 * -- http://www.w3.org/TR/xmlschema-2/#dt-bounded
 * 
 * Cardinality
 * [Definition:] Every value space has associated with it the concept
 * of cardinality. Some value spaces are finite, some are countably
 * infinite while still others could conceivably be uncountably infinite
 * (although no value space defined by this specification is
 * uncountable infinite). A datatype is said to have the cardinality of
 * its value space.
 * -- http://www.w3.org/TR/xmlschema-2/#dt-cardinality
 * 
 * Numeric
 * [Definition:] A datatype is said to be numeric if its values are
 * conceptually quantities (in some mathematical number system).
 * -- http://www.w3.org/TR/xmlschema-2/#dt-numeric
 */



/*
 * Types: dateTime, date, time
 *   http://www.w3.org/TR/xmlschema-2/#dateTime
 *   http://www.w3.org/TR/xmlschema-2/#date
 *   http://www.w3.org/TR/xmlschema-2/#time
 * all (partial ordered, bounded, countably infinite, not numeric)
 * 
 * Functions (all operators)
 * op:date-equal, op:date-less-than, op:date-greater-than
 *
 * ??? dateTime equiv???
 * op:dateTime-equal, op:dateTime-less-than, op:dateTime-greater-than
 *
 * ??? time equiv???
 * op:time-equal, op:time-less-than, op:time-greater-than
 */

#if 0
typedef struct
{
  /* dateTime and date */
  int year;
  unsigned int month         :4;  /* 1..12 (4 bits)  */
  unsigned int day           :5;  /* 1..31 (5 bits)  */

  /* dateTime and time */
  unsigned int hour          :5;  /* 0..24 (5 bits) */
  unsigned int minute        :6;  /* 0..59 (6 bits) */
  double       second;

  /* optional (when have_timezone=1) dateTime, date, time */
  unsigned int have_timezone :1;  /* boolean (1 bit) */
  int          timezone      :11; /* +/-14 hours in minutes (-14*60..14*60) */
} rasqal_xsd_datetime;
#endif


/* 
 * Type: string
 * (not ordered, not bounded, countably infinite, not numeric)
 * 
 * fn:contains
 *   Indicates whether one xs:string contains another xs:string. A
 *   collation may be specified.
 *
 * fn:starts-with
 *   Indicates whether the value of one xs:string begins with the
 *   collation units of another xs:string. A collation may be
 *   specified.
 *
 * fn:ends-with
 *   Indicates whether the value of one xs:string ends with the
 *   collation units of another xs:string. A collation may be
 *   specified.
 *
 * fn:substring-before
 *   Returns the collation units of one xs:string that precede in
 *   that xs:string the collation units of another xs:string. A
 *   collation may be specified.
 *
 * fn:substring-after
 *   Returns the collation units of xs:string that follow in that
 *   xs:string the collation units of another xs:string. A collation
 *   may be specified.
 *
 * fn:string-length
 *   Returns the length of the argument.
 *
 * fn:upper-case
 *   Returns the upper-cased value of the argument.
 *
 * fn:lower-case
 *   Returns the lower-cased value of the argument.
 *
 * fn:matches (input, pattern)
 *   fn:matches (input, pattern, flags)
 *
 *   Returns an xs:boolean value that indicates whether the
 *   value of the first argument is matched by the regular expression that
 *   is the value of the second argument.
 *
 *   flags = string of s,m,i,x char combinations ("" when omitted)
 *
 *   Regular expressions: Perl5 syntax as defined in "Functions and
 *   Operators".
 *
 *  http://www.w3.org/TR/xpath-functions/#func-contains
 *  http://www.w3.org/TR/xpath-functions/#func-starts-with
 *  http://www.w3.org/TR/xpath-functions/#func-ends-with
 *  http://www.w3.org/TR/xpath-functions/#func-substring-before
 *  http://www.w3.org/TR/xpath-functions/#func-substring-after
 *  http://www.w3.org/TR/xpath-functions/#func-string-length
 *  http://www.w3.org/TR/xpath-functions/#func-upper-case
 *  http://www.w3.org/TR/xpath-functions/#func-lower-case
 *  http://www.w3.org/TR/xpath-functions/#func-matches
 *
 * ??? no equality comparison fn:compare???
 *  fn:compare($comparand1 as xs:string, $comparand2 as xs:string) as xs:integer
 *  fn:compare($comparand1 as xs:string, $comparand2 as xs:string,
 *             $collation as xs:string) as xs:integer
 * [[This function, invoked with the first signature, backs up the
 * "eq", "ne", "gt", "lt", "le" and "ge" operators on string
 * values.]]
 *
 */

#if 0
typedef struct
{
  unsigned char *string;
  size_t length;
} rasqal_xsd_string;
#endif


/*
 * Type: double
 *   (partial ordered, bounded, countably infinite, numeric)
 * 
 * Type: decimal
 *   (total ordered, not bounded, countably infinite, numeric)
 *
 * Derived Type: integer (derived from decimal)
 *   (total ordered, not bounded, countably infinite, numeric)
 * 
 * Functions:
 * 1 arguments
 *   op:numeric-unary-plus
 *   op:numeric-unary-minus
 *   http://www.w3.org/TR/xpath-functions/#func-numeric-unary-plus
 *   http://www.w3.org/TR/xpath-functions/#func-numeric-unary-minus
 *
 * 2 arguments
 *   op:numeric-equal
 *   op:numeric-less-than
 *   op:numeric-greater-than
 *   op:numeric-add
 *   op:numeric-subtract
 *   op:numeric-multiply
 *   op:numeric-divide
 *   http://www.w3.org/TR/xpath-functions/#func-numeric-equal
 *   http://www.w3.org/TR/xpath-functions/#func-numeric-less-than
 *   http://www.w3.org/TR/xpath-functions/#func-numeric-greater-than
 *   http://www.w3.org/TR/xpath-functions/#func-numeric-add
 *   http://www.w3.org/TR/xpath-functions/#func-numeric-subtract
 *   http://www.w3.org/TR/xpath-functions/#func-numeric-multiply
 *   http://www.w3.org/TR/xpath-functions/#func-numeric-divide
 *
 * [[The parameters and return types for the above operators are the
 * basic numeric types: xs:integer, xs:decimal, xs:float and
 * xs:double, and types derived from them.  The word "numeric" in
 * function signatures signifies these four types. For simplicity,
 * each operator is defined to operate on operands of the same type
 * and to return the same type. The exceptions are op:numeric-divide,
 * which returns an xs:decimal if called with two xs:integer operands
 * and op:numeric-integer-divide which always returns an xs:integer.]]
 * -- http://www.w3.org/TR/xpath-functions/#op.numeric
 *
 *
 * Numeric type promotion
 * http://www.w3.org/TR/xpath20/#dt-type-promotion
 *
 * [[xs:decimal (or any type derived by restriction from xs:decimal,
 * including xs:integer) can be promoted to either of the types
 * xs:float or xs:double.]]
 *
 * For here that means xs:integer to xs:double and xs:decimal to xs:double
 *
 * [[A function that expects a parameter $p of type xs:decimal can be
 * invoked with a value of type xs:integer. This is an example of
 * subtype substitution. The value retains its original type. Within
 * the body of the function, $p instance of xs:integer returns
 * true.]]
 *
 *
 * B.2 Operator Mapping
 * http://www.w3.org/TR/xpath20/#mapping
 *
 * [[When referring to a type, the term numeric denotes the types
 * xs:integer, xs:decimal, xs:float, and xs:double]]
 *
 * [[If the result type of an operator is listed as numeric, it means
 * "the first type in the ordered list (xs:integer, xs:decimal,
 * xs:float, xs:double) into which all operands can be converted by
 * subtype substitution and numeric type promotion."]]
 * 
 */



#ifdef STANDALONE
#include <stdio.h>

int main(int argc, char *argv[]);


int
main(int argc, char *argv[]) {
  raptor_uri *xsd_uri;
  raptor_uri *dateTime_uri;
  rasqal_literal *l1, *l2;
  int fn_i;
  raptor_uri* fn_uri;
  const unsigned char *fn_name;
  rasqal_extension_fn fn;
  raptor_sequence *fn_args;
  char *error;
  rasqal_literal *fn_result;


  rasqal_init();

  xsd_uri=raptor_new_uri(raptor_xmlschema_datatypes_namespace_uri);
  dateTime_uri=raptor_new_uri_from_uri_local_name(xsd_uri, (const unsigned char*)"dateTime");

  rasqal_init_datatypes();

  fn_args=raptor_new_sequence((raptor_sequence_free_handler*)rasqal_free_literal, (raptor_sequence_print_handler*)rasqal_literal_print);
  l1=rasqal_new_string_literal((unsigned char*)strdup("2004-05-04"), NULL, raptor_uri_copy(dateTime_uri), NULL);
  raptor_sequence_push(fn_args, l1);
  l2=rasqal_new_string_literal((unsigned char*)strdup("2003-01-02"), NULL, raptor_uri_copy(dateTime_uri), NULL);
  raptor_sequence_push(fn_args, l2);
  
  fn_i=0;
  fn_name=rasqal_xsd_datatype_fns[fn_i].name;
  fn=rasqal_xsd_datatype_fns[fn_i].fn;
  fn_uri=rasqal_xsd_datatype_fns[fn_i].uri;

  error=NULL;
  fn_result=fn(fn_uri, fn_args, &error);
  raptor_free_sequence(fn_args);

  if(!fn_result) {
    if(error)
      fprintf(stderr, "function %s failed with error %s\n", fn_name, error);
    else
      fprintf(stderr, "function %s unknown error\n", fn_name);
  } else {
    fprintf(stderr, "function %s returned result: ", fn_name);
    rasqal_literal_print(fn_result, stderr);
    fputc('\n', stderr);
  }
  

  if(fn_result) 
    rasqal_free_literal(fn_result);

  rasqal_finish_datatypes();
  
  raptor_free_uri(xsd_uri);
  raptor_free_uri(dateTime_uri);

  rasqal_finish();

  return 0;
}
#endif
