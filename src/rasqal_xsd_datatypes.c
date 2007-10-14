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
#include <limits.h>

#include "rasqal.h"
#include "rasqal_internal.h"

#if 0
/* libxml2 datatypes */
#ifdef HAVE_LIBXML_XMLSCHEMAS_H
#include <libxml/xmlschemas.h>
#include <libxml/xmlschemastypes.h>
#include <libxml/schemasInternals.h>
#endif
#endif

/* Local definitions */

/**
 * rasqal_xsd_datetime:
 *
 * INTERNAL - XML schema dateTime datatype
 *
 * Signed types are required for normalization process where a value
 * can be negative temporarily.
 */
typedef struct {
  signed int year;
  /* the following fields are integer values not characters */
  unsigned char month;
  unsigned char day;
  signed char hour;
  signed char minute;
  signed char second;
  /* second_frac is a string of 1-3 length (+1 for NUL)
   * supports only up to milliseconds
   */
  char second_frac[3+1];
  /* have_tz is an integer flag: non-0 if 'Z'ulu timezone is present */
  char have_tz;
} rasqal_xsd_datetime;


static int rasqal_xsd_datetime_parse_and_normalize(const unsigned char *datetime_string, rasqal_xsd_datetime *result);
static unsigned char *rasqal_xsd_datetime_to_string(const rasqal_xsd_datetime *dt);
static unsigned int days_per_month(int month, int year);

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
  rasqal_xsd_datetime d;
  
  /* This should be correct according to 
   * http://www.w3.org/TR/xmlschema-2/#dateTime
   */
  return !rasqal_xsd_datetime_parse_and_normalize(string, &d);
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


typedef rasqal_literal* (*rasqal_extension_fn)(raptor_uri* name, raptor_sequence *args, char **error_p);


typedef struct {
  const unsigned char *name;
  int min_nargs;
  int max_nargs;
  rasqal_extension_fn fn;
  raptor_uri* uri;
} rasqal_xsd_datatype_fn_info;


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


static rasqal_xsd_datatype_info* sparql_xsd_datatypes_table=NULL;

static raptor_uri* rasqal_xsd_namespace_uri=NULL;

int
rasqal_xsd_init(void) 
{
  int i;

  rasqal_xsd_namespace_uri=raptor_new_uri(raptor_xmlschema_datatypes_namespace_uri);
  if(!rasqal_xsd_namespace_uri)
    return 1;
  
  sparql_xsd_datatypes_table=
    (rasqal_xsd_datatype_info*)RASQAL_CALLOC(rasqal_xsd_datatype_info,
                                             RASQAL_LITERAL_LAST_XSD+2,
                                             sizeof(rasqal_xsd_datatype_info));
  if(!sparql_xsd_datatypes_table)
    return 1;
  
  for(i=RASQAL_LITERAL_FIRST_XSD; i <= RASQAL_LITERAL_LAST_XSD; i++) {
    sparql_xsd_datatypes_table[i].name=sparql_xsd_names[i];
    sparql_xsd_datatypes_table[i].uri= raptor_new_uri_from_uri_local_name(rasqal_xsd_namespace_uri, (const unsigned char*)sparql_xsd_datatypes_table[i].name);
    if(!sparql_xsd_datatypes_table[i].uri)
      return 1;
  }
  
  /* no RASQAL_LITERAL_STRING check needed */
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
  if(sparql_xsd_datatypes_table) {
    int i;
    
    for(i=RASQAL_LITERAL_FIRST_XSD; i <= RASQAL_LITERAL_LAST_XSD; i++) {
      if(sparql_xsd_datatypes_table[i].uri)
        raptor_free_uri(sparql_xsd_datatypes_table[i].uri);
    }
    
    RASQAL_FREE(table, sparql_xsd_datatypes_table);
    sparql_xsd_datatypes_table=NULL;
  }

  if(rasqal_xsd_namespace_uri) {
    raptor_free_uri(rasqal_xsd_namespace_uri);
    rasqal_xsd_namespace_uri=NULL;
  }
}
 

  
rasqal_literal_type
rasqal_xsd_datatype_uri_to_type(raptor_uri* uri)
{
  int i;
  rasqal_literal_type native_type=RASQAL_LITERAL_UNKNOWN;
  
  if(!uri || !sparql_xsd_datatypes_table)
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
  if(sparql_xsd_datatypes_table &&
     type >= RASQAL_LITERAL_FIRST_XSD && type <= (int)RASQAL_LITERAL_LAST_XSD)
    return sparql_xsd_datatypes_table[(int)type].uri;
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
  if(sparql_xsd_datatypes_table &&
     sparql_xsd_datatypes_table[native_type].check)
    return sparql_xsd_datatypes_table[native_type].check(string, flags);
  return 1;
}


const char*
rasqal_xsd_datatype_label(rasqal_literal_type native_type)
{
  return sparql_xsd_datatypes_table ?
    sparql_xsd_datatypes_table[native_type].name :
    NULL;
}


int
rasqal_xsd_is_datatype_uri(raptor_uri* uri)
{
  return (rasqal_xsd_datatype_uri_to_type(uri) != RASQAL_LITERAL_UNKNOWN);
}


/**
 * days_per_month:
 * @month month 1-12
 * @year gregorian year
 *
 * INTERNAL - returns the number of days in given month and year.
 *
 * Return value: number of days or 0 on invalid arguments
 */
static unsigned int
days_per_month(int month, int year) {
  switch(month) {
    case 1:
    case 3:
    case 5:
    case 7:
    case 8:
    case 10:
    case 12:
      return 31;
  
    case 4:
    case 6:
    case 9:
    case 11:
      return 30;
  
    case 2:
      /* any of bottom 2 bits non-zero -> not 0 mod 4 -> not leap year */
      if(year & 3)
        return 28;

      /* 0 mod 400 and 0 mod 4 -> leap year */
      if(!(year % 400))
        return 29;

      /* 0 mod 100 and not 0 mod 400 and 0 mod 4 -> not leap year */
      if(!(year % 100))
        return 28;

      /* other 0 mod 4 years -> leap year */
      return 29;

    default:
       /* error */
      return 0;
  }
}


#ifndef ISNUM
#define ISNUM(c) ((c)>='0'&&(c)<='9')
#endif


/**
 * rasqal_xsd_datetime_normalize:
 * @datetime: date time
 *
 * INTERNAl - Normalize a date time into the allowed range
 *
 * Return value: zero on success, non zero on failure.
 */
static int
rasqal_xsd_datetime_normalize(rasqal_xsd_datetime *datetime)
{
  int t;
  
  /* second & second parts: no need to normalize as they are not
   * touched after range check
   */
  
  /* minute */
  if(datetime->minute < 0) {
    datetime->minute += 60;
    datetime->hour--;
  } else if(datetime->minute > 59) {
    datetime->minute -= 60;
    datetime->hour++;
  }
  
  /* hour */
  if(datetime->hour < 0) {
    datetime->hour += 24;
    datetime->day--;
  } else if(datetime->hour > 23) {
    datetime->hour -= 24;
    datetime->day++;
  }
  
  /* day */
  if(datetime->day < 1) {
    int y2= (t == 12) ? datetime->year-1 : datetime->year;
    t=datetime->month--;
    datetime->day += days_per_month(t, y2);
  } else if(datetime->day > (t=days_per_month(datetime->month, datetime->year))) {
    datetime->day -= t;
    datetime->month++;
  }
  
  /* month & year */
  if(datetime->month < 1) {
    datetime->month += 12;
    datetime->year--;
    /* there is no year 0 - go backwards to year -1 */
    if(!datetime->year)
      datetime->year--;
  } else if(datetime->month > 12) {
    datetime->month -= 12;
    datetime->year++;
    /* there is no year 0 - go forwards to year 1 */
    if(!datetime->year)
      datetime->year++;
  }

  /* success */
  return 0;
}


/**
 * rasqal_xsd_datetime_parse_and_normalize:
 * @datetime_string: xsd:dateTime as lexical form string
 * @result: target struct for holding dateTime components
 *
 * INTERNAL - Parse a xsd:dateTime string to a normalized #rasqal_xsd_datetime struct.
 *
 * http://www.w3.org/TR/xmlschema-2/#dt-dateTime
 *
 * "The lexical space of dateTime consists of finite-length sequences of
 * characters of the form:
 * '-'? yyyy '-' mm '-' dd 'T' hh ':' mm ':' ss ('.' s+)? (zzzzzz)?,
 * where
 *
 * * '-'? yyyy is a four-or-more digit optionally negative-signed numeral that
 *   represents the year; if more than four digits, leading zeros are
 *   prohibited, and '0000' is prohibited (see the Note above (3.2.7); also
 *   note that a plus sign is not permitted);
 * * the remaining '-'s are separators between parts of the date portion;
 * * the first mm is a two-digit numeral that represents the month;
 * * dd is a two-digit numeral that represents the day;
 * * 'T' is a separator indicating that time-of-day follows;
 * * hh is a two-digit numeral that represents the hour; '24' is permitted if
 *   the minutes and seconds represented are zero, and the dateTime value so
 *   represented is the first instant of the following day (the hour property
 *   of a dateTime object in the value space cannot have a value greater
 *   than 23);
 * * ':' is a separator between parts of the time-of-day portion;
 * * the second mm is a two-digit numeral that represents the minute;
 * * ss is a two-integer-digit numeral that represents the whole seconds;
 * * '.' s+ (if present) represents the fractional seconds;
 * * zzzzzz (if present) represents the timezone"
 *
 * Return value: zero on success, non zero on failure.
 */
static int
rasqal_xsd_datetime_parse_and_normalize(const unsigned char *datetime_string,
                                        rasqal_xsd_datetime *result)
{
  const char *p, *q; 
  char b[16];
  unsigned int l, t, t2, is_neg;
  unsigned long u;

  if(!datetime_string || !result)
    return -1;
  
  p=(const char *)datetime_string;
  is_neg=0;

  /* Parse year */
  
  /* negative years permitted */
  if(*p == '-') {
    is_neg=1;
    p++;
  }
  for(q=p; ISNUM(*p); p++)
    ;
  l=p-q;
  
  /* error if
     - less than 4 digits in year
     - more than 4 digits && leading zeros
     - '-' does not follow numbers
   */
  if(l < 4 || (l > 4 && *q=='0') || *p!='-')
    return -1;

  l=(l < sizeof(b)-1 ? l : sizeof(b)-1);
  strncpy(b, q, l);
  b[l]=0; /* ensure nul termination */
  u=strtoul(b, 0, 10);
  
  /* year "0000" not permitted
   * restrict to signed int range
   * >= instead of > to allow for +-1 year adjustment in normalization
   * (however, these +-INT_MAX years cannot be parsed back in if
   * converted to string)
   */
  if(!u || u >= INT_MAX)
    return -1;
    
  result->year=is_neg ? -(int)u : (int)u;

  /* parse month */
  
  for(q=++p; ISNUM(*p); p++)
    ;
  l=p-q;
  
  /* error if month is not 2 digits or '-' is not the separator */
  if(l != 2 || *p!='-')
    return -2;
  
  t=(*q++-'0')*10;
  t+=*q-'0';
  
  /* month must be 1..12 */
  if(t < 1 || t > 12)
    return -2;
  
  result->month=t;
  
  /* parse day */
  
  for(q=++p; ISNUM(*p); p++)
    ;
  l=p-q;
  
  /* error if day is not 2 digits or 'T' is not the separator */
  if(l != 2 || *p != 'T')
    return -3;
  
  t=(*q++-'0')*10;
  t+=*q-'0';
  
  /* day must be 1..days_per_month */
  if(t < 1 || t > days_per_month(result->month, result->year))
    return -3;
    
  result->day=t;
  
  /* parse hour */
  
  for(q=++p; ISNUM(*p); p++)
    ;
  l=p-q;
  
  /* error if hour is not 2 digits or ':' is not the separator */
  if(l != 2 || *p != ':')
    return -4;
   
  t=(*q++-'0')*10;
  t+=*q-'0';
 
  /* hour must be 0..24 - will handle special case 24 later
   * (no need to check for < 0)
   */
  if(t > 24)
    return -4;
    
  result->hour=t;
 
  /* parse minute */

  for(q=++p; ISNUM(*p); p++)
    ;
  l=p-q;
  
  /* error if minute is not 2 digits or ':' is not the separator */
  if(l != 2 || *p != ':')
    return -5;
   
  t=(*q++-'0')*10;
  t+=*q-'0';
 
  /* minute must be 0..59
   * (no need to check for < 0)
   */
  if(t > 59)
    return -5;

  result->minute=t;
  
  /* parse second whole part */
  
  for(q=++p; ISNUM(*p); p++)
    ;
  l=p-q;
  
  /* error if second is not 2 digits or separator is not 
   * '.' (second fraction)
   * 'Z' (utc)
   * '+' or '-' (timezone offset)
   * nul (end of string - second fraction and timezone are optional)
   */
  if(l != 2 || (*p && *p != '.' && *p != 'Z' && *p != '+' && *p != '-'))
    return -6;
    
  t=(*q++-'0')*10;
  t+=*q-'0';

  /* second must be 0..59
  * (no need to check for < 0)
  */
  if(t > 59)
    return -6;

  result->second=t;

  /* now that we have hour, minute and second, we can check
   * if hour == 24 -> only 24:00:00 permitted (normalized later)
   */
  if(result->hour==24 && (result->minute || result->second))
    return -7;
  
  /* parse fraction seconds if any */
  result->second_frac[0]=0;
  if(*p == '.') {
    for(q=++p; ISNUM(*p); p++)
      ;

    /* ignore trailing zeros */
    while(*--p == '0')
      ;
    p++;

    if(!(*q=='0' && q==p)) {
      /* allow ".0" */
      l=p-q;
      /* support only to milliseconds with truncation */
      if(l > sizeof(result->second_frac)-1)
        l=sizeof(result->second_frac)-1;

      if(l<1) /* need at least 1 num */
        return -8;

      for(t2=0; t2 < l; ++t2)
        result->second_frac[t2]=*q++;

      result->second_frac[l]=0;
    }

    /* skip ignored trailing zeros */
    while(*p == '0')
      p++;
  }
  
  /* parse & adjust timezone offset */
  /* result is normalized later */
  result->have_tz=0;
  if(*p) {
    if(*p == 'Z') {
      /* utc timezone - no need to adjust */
      p++;
      result->have_tz=1;
    } else if(*p=='+' || *p=='-') {
      /* work out timezone offsets */
      is_neg=*p == '-';
     
      /* timezone hours */
      for(q=++p; ISNUM(*p); p++)
        ;
      l=p-q;
      if(l != 2 || *p!=':')
        return -9;

      t2=(*q++ - '0')*10;
      t2+=*q - '0';
      if(t2 > 14)
        /* tz offset hours are restricted to 0..14
         * (no need to check for < 0)
         */
        return -9;
    
      /* negative tz offset adds to the result */
      result->hour+=is_neg ? t2 : -t2;

      /* timezone minutes */    
      for(q=++p; ISNUM(*p); p++)
        ;
      l=p-q;
      if(l!=2)
        return -10;

      t=(*q++ - '0')*10;
      t+=*q - '0';
      if(t > 59 || (t2 == 14 && t!=0)) {
        /* tz offset minutes are restricted to 0..59
         * (no need to check for < 0)
         * or 0 if hour offset is exactly +-14 
         */
        return -10;
      }
    
      /* negative tz offset adds to the result */
      result->minute += is_neg ? t : -t;
      result->have_tz=1;
    }
    
    /* failure if extra chars after the timezone part */
    if(*p)
      return -11;

  }

  return rasqal_xsd_datetime_normalize(result);
}


/**
 * rasqal_xsd_datetime_to_string:
 * @dt: datetime struct
 *
 * INTERNAL - Convert a #rasqal_xsd_datetime struct to a xsd:dateTime lexical form string.
 *
 * Caller should RASQAL_FREE() the returned string.
 *
 * Return value: lexical form string or NULL on failure.
 */
static unsigned char*
rasqal_xsd_datetime_to_string(const rasqal_xsd_datetime *dt)
{
  unsigned char *ret=0;
  int is_neg;
  int r=0;
  int i;
   
  if(!dt)
    return NULL;
    
  is_neg=dt->year<0;

  /* format twice: first with null buffer of zero size to get the
   * required buffer size second time to the allocated buffer
   */
  for(i=0; i < 2; i++) {
    r=snprintf((char*)ret, r, "%s%04d-%2.2d-%2.2dT%2.2d:%2.2d:%2.2d%s%s%s",
      is_neg ? "-" : "",
      is_neg ? -dt->year : dt->year,
      dt->month,
      dt->day,
      dt->hour,
      dt->minute,
      dt->second,
      *dt->second_frac ? "." : "",
      dt->second_frac,
      dt->have_tz ? "Z" : "");

    /* error? */
    if(r<0) {
      if(ret)
        RASQAL_FREE(cstring, ret);
      return NULL;
    }

    /* alloc return buffer on first pass */
    if(!i) {
      ret=(unsigned char *)RASQAL_MALLOC(cstring, ++r);
      if(!ret)
        return NULL;
    }
  }
  return ret;
}


/**
 * rasqal_xsd_datetime_string_to_canonical:
 * @datetime_string: xsd:dateTime as lexical form string
 *
 * Convert a XML Schema dateTime lexical form string to its canonical form.
 *
 * Caller should RASQAL_FREE() the returned string.
 *
 * Return value: canonical lexical form string or NULL on failure.
 *
 *
 * http://www.w3.org/TR/xmlschema-2/#dateTime-canonical-representation
 * 
 * "Except for trailing fractional zero digits in the seconds representation,
 * '24:00:00' time representations, and timezone (for timezoned values),
 * the mapping from literals to values is one-to-one.
 * Where there is more than one possible representation,
 * the canonical representation is as follows:
 *    * The 2-digit numeral representing the hour must not be '24';
 *    * The fractional second string, if present, must not end in '0';
 *    * for timezoned values, the timezone must be represented with 'Z'
 *      (All timezoned dateTime values are UTC.)."
 */
const unsigned char*
rasqal_xsd_datetime_string_to_canonical(const unsigned char* datetime_string)
{
  rasqal_xsd_datetime d; /* allocated on stack */

  /* parse_and_normalize makes the rasqal_xsd_datetime canonical... */
  if(rasqal_xsd_datetime_parse_and_normalize(datetime_string, &d))
    return NULL;
  /* ... so return a string representation of it */
  return rasqal_xsd_datetime_to_string(&d);
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

#define MYASSERT(c) \
  if(!(c)) { \
    fprintf(stderr, "%s: assertion failed at %s:%d: %s\n", program, __FILE__, __LINE__, #c); \
    exit(1); \
  }


static int test_datetime_parser_tostring(const char *in_str, const char *out_expected)
{
  unsigned char const *s;
  int r=1;
  s=rasqal_xsd_datetime_string_to_canonical((const unsigned char *)in_str);
  if(s) {
    r=strcmp((char*)s, out_expected);
    RASQAL_FREE(cstring, (void*)s);
  }
  return r;
}


static void test_datetimes(const char *program)
{
  rasqal_xsd_datetime d;

  /* days_per_month */
  
  MYASSERT(!days_per_month(0,287));
  
  MYASSERT(days_per_month(1,467) == 31);

  MYASSERT(days_per_month(2,1900) == 28);  
  MYASSERT(days_per_month(2,1901) == 28);
  MYASSERT(days_per_month(2,2000) == 29);
  MYASSERT(days_per_month(2,2004) == 29);
  
  MYASSERT(days_per_month(3,1955) == 31);
  MYASSERT(days_per_month(4,3612) == 30);
  MYASSERT(days_per_month(5,467) == 31);
  MYASSERT(days_per_month(6,398) == 30);
  MYASSERT(days_per_month(7,1832) == 31);
  MYASSERT(days_per_month(8,8579248) == 31);
  MYASSERT(days_per_month(9,843) == 30);
  MYASSERT(days_per_month(10,84409) == 31);
  MYASSERT(days_per_month(11,398) == 30);
  MYASSERT(days_per_month(12,4853) == 31);
  MYASSERT(!days_per_month(13,45894));
  
  /* rasqal_xsd_datetime_parse_and_normalize,
     rasqal_xsd_datetime_to_string and
     rasqal_xsd_datetime_string_to_canonical */
  
  #define PARSE_AND_NORMALIZE(_s,_d) \
    rasqal_xsd_datetime_parse_and_normalize((const unsigned char*)_s, _d)
  
  /* generic */

  MYASSERT(!rasqal_xsd_datetime_to_string(0));

  MYASSERT(PARSE_AND_NORMALIZE(0,0));
  MYASSERT(PARSE_AND_NORMALIZE("uhgsufi",0));
  MYASSERT(PARSE_AND_NORMALIZE(0,&d));
  MYASSERT(PARSE_AND_NORMALIZE("fsdhufhdsuifhidu",&d));
  
  /* year */
  
  MYASSERT(PARSE_AND_NORMALIZE("123-12-12T12:12:12Z",&d));
  MYASSERT(PARSE_AND_NORMALIZE("-123-12-12T12:12:12Z",&d));
  MYASSERT(PARSE_AND_NORMALIZE("0000-12-12T12:12:12Z",&d));
  MYASSERT(PARSE_AND_NORMALIZE("01234-12-12T12:12:12Z",&d));
  MYASSERT(PARSE_AND_NORMALIZE("-01234-12-12T12:12:12Z",&d));
  MYASSERT(PARSE_AND_NORMALIZE("1234a12-12T12:12:12Z",&d));
  MYASSERT(PARSE_AND_NORMALIZE("-1234b12-12T12:12:12Z",&d));
  MYASSERT(PARSE_AND_NORMALIZE("g162-12-12T12:12:12Z",&d));
  MYASSERT(PARSE_AND_NORMALIZE("5476574658746587465874-12-12T12:12:12Z",&d));
  
  MYASSERT(test_datetime_parser_tostring("1234-12-12T12:12:12Z", "1234-12-12T12:12:12Z")==0);
  MYASSERT(test_datetime_parser_tostring("-1234-12-12T12:12:12Z", "-1234-12-12T12:12:12Z")==0);
  MYASSERT(test_datetime_parser_tostring("1234567890-12-12T12:12:12Z", "1234567890-12-12T12:12:12Z")==0);
  MYASSERT(test_datetime_parser_tostring("-1234567890-12-12T12:12:12Z", "-1234567890-12-12T12:12:12Z")==0);
  
  /* month */
  
  MYASSERT(PARSE_AND_NORMALIZE("2004-v-12T12:12:12Z",&d));
  MYASSERT(PARSE_AND_NORMALIZE("2004-00-12T12:12:12Z",&d));
  MYASSERT(PARSE_AND_NORMALIZE("PARSE_AND_NORMALIZE-011-12T12:12:12Z",&d));
  MYASSERT(PARSE_AND_NORMALIZE("2004-13-12T12:12:12Z",&d));
  MYASSERT(PARSE_AND_NORMALIZE("2004-12.12T12:12:12Z",&d));

  MYASSERT(test_datetime_parser_tostring("2004-01-01T12:12:12Z", "2004-01-01T12:12:12Z")==0);

  /* day */
  
  MYASSERT(PARSE_AND_NORMALIZE("2004-01-ffT12:12:12Z",&d));
  MYASSERT(PARSE_AND_NORMALIZE("2004-01-00T12:12:12Z",&d));
  MYASSERT(PARSE_AND_NORMALIZE("2004-01-007T12:12:12Z",&d));
  MYASSERT(PARSE_AND_NORMALIZE("2004-01-32T12:12:12Z",&d));
  MYASSERT(PARSE_AND_NORMALIZE("2004-01-01t12:12:12Z",&d));
  MYASSERT(PARSE_AND_NORMALIZE("2004-01- 1T12:12:12Z",&d));
  
  MYASSERT(PARSE_AND_NORMALIZE("2005-02-29T12:12:12Z",&d));
  MYASSERT(!PARSE_AND_NORMALIZE("2005-02-28T12:12:12Z",&d));
  MYASSERT(!PARSE_AND_NORMALIZE("2004-02-29T12:12:12Z",&d));
  MYASSERT(!PARSE_AND_NORMALIZE("2000-02-29T12:12:12Z",&d));
  MYASSERT(PARSE_AND_NORMALIZE("1900-02-29T12:12:12Z",&d));

  MYASSERT(test_datetime_parser_tostring("2012-04-12T12:12:12Z", "2012-04-12T12:12:12Z")==0);
  
  /* hour */

  MYASSERT(PARSE_AND_NORMALIZE("2004-01-01Tew:12:12Z",&d));
  MYASSERT(PARSE_AND_NORMALIZE("2004-01-01T-1:12:12Z",&d));
  MYASSERT(PARSE_AND_NORMALIZE("2004-01-01T001:12:12Z",&d));
  MYASSERT(PARSE_AND_NORMALIZE("2004-01-01T25:12:12Z",&d));
  MYASSERT(PARSE_AND_NORMALIZE("2004-01-01T01.12:12Z",&d));
  
  MYASSERT(PARSE_AND_NORMALIZE("2004-01-01T24:12:00Z",&d));
  MYASSERT(PARSE_AND_NORMALIZE("2004-01-01T24:00:34Z",&d));
  MYASSERT(PARSE_AND_NORMALIZE("2004-01-01T24:12:34Z",&d));
  MYASSERT(!PARSE_AND_NORMALIZE("2004-01-01T24:00:00Z",&d));
  
  MYASSERT(test_datetime_parser_tostring("2012-04-12T24:00:00", "2012-04-13T00:00:00")==0);
  
  /* minute */
  
  MYASSERT(PARSE_AND_NORMALIZE("2004-01-01T12:ij:12Z",&d));
  MYASSERT(PARSE_AND_NORMALIZE("2004-01-01T12:-1:12Z",&d));
  MYASSERT(PARSE_AND_NORMALIZE("2004-01-01T12:042:12Z",&d));
  MYASSERT(PARSE_AND_NORMALIZE("2004-01-01T12:69:12Z",&d));
  MYASSERT(PARSE_AND_NORMALIZE("2004-01-01T12:12.12Z",&d));
  
  /* second */

  MYASSERT(PARSE_AND_NORMALIZE("2004-01-01T12:12:ijZ",&d));
  MYASSERT(PARSE_AND_NORMALIZE("2004-01-01T12:12:-1",&d));
  MYASSERT(PARSE_AND_NORMALIZE("2004-01-01T12:12:054Z",&d));
  MYASSERT(PARSE_AND_NORMALIZE("2004-01-01T12:12:69Z",&d));
  MYASSERT(PARSE_AND_NORMALIZE("2004-01-01T12:12:12z",&d));
  
  MYASSERT(!PARSE_AND_NORMALIZE("2004-01-01T12:12:12",&d));
  
  /* fraction second */
  
  MYASSERT(PARSE_AND_NORMALIZE("2004-01-01T12:12:12.",&d));
  MYASSERT(PARSE_AND_NORMALIZE("2004-01-01T12:12:12.i",&d));
  MYASSERT(!PARSE_AND_NORMALIZE("2004-01-01T12:12:12.0",&d));
  MYASSERT(!PARSE_AND_NORMALIZE("2004-01-01T12:12:12.01",&d));
  MYASSERT(!PARSE_AND_NORMALIZE("2004-01-01T12:12:12.1",&d));
  MYASSERT(!PARSE_AND_NORMALIZE("2004-01-01T12:12:12.100",&d));
  MYASSERT(!PARSE_AND_NORMALIZE("2004-01-01T12:12:12.1000000000000000000000000000000000000000000",&d));
  MYASSERT(!PARSE_AND_NORMALIZE("2004-01-01T12:12:12.5798459847598743987549",&d));
  MYASSERT(PARSE_AND_NORMALIZE("2004-01-01T12:12:12.1d",&d));
  MYASSERT(!PARSE_AND_NORMALIZE("2004-01-01T12:12:12.1Z",&d));

  MYASSERT(test_datetime_parser_tostring("2006-05-18T18:36:03.01Z", "2006-05-18T18:36:03.01Z")==0);
  MYASSERT(test_datetime_parser_tostring("2006-05-18T18:36:03.10Z", "2006-05-18T18:36:03.1Z")==0);
  MYASSERT(test_datetime_parser_tostring("2006-05-18T18:36:03.010Z", "2006-05-18T18:36:03.01Z")==0);
  MYASSERT(test_datetime_parser_tostring("2006-05-18T18:36:03.1234Z", "2006-05-18T18:36:03.123Z")==0);
  MYASSERT(test_datetime_parser_tostring("2006-05-18T18:36:03.1234", "2006-05-18T18:36:03.123")==0);
  MYASSERT(test_datetime_parser_tostring("2006-05-18T18:36:03.1239Z", "2006-05-18T18:36:03.123Z")==0);
  MYASSERT(test_datetime_parser_tostring("2006-05-18T18:36:03.1239", "2006-05-18T18:36:03.123")==0);

  /* timezones + normalization */

  MYASSERT(PARSE_AND_NORMALIZE("2004-01-01T12:12:12+",&d));
  MYASSERT(PARSE_AND_NORMALIZE("2004-01-01T12:12:12-",&d));
  MYASSERT(PARSE_AND_NORMALIZE("2004-01-01T12:12:12+00.00",&d));
  MYASSERT(PARSE_AND_NORMALIZE("2004-01-01T12:12:12+aa:bb",&d));
  MYASSERT(PARSE_AND_NORMALIZE("2004-01-01T12:12:12+15:00",&d));
  MYASSERT(PARSE_AND_NORMALIZE("2004-01-01T12:12:12+14:01",&d));
  MYASSERT(!PARSE_AND_NORMALIZE("2004-01-01T12:12:12+14:00",&d));
  MYASSERT(PARSE_AND_NORMALIZE("2004-01-01T12:12:12-14:01",&d));
  MYASSERT(!PARSE_AND_NORMALIZE("2004-01-01T12:12:12-14:00",&d));
  MYASSERT(PARSE_AND_NORMALIZE("2004-01-01T12:12:12+10:99",&d));
  MYASSERT(!PARSE_AND_NORMALIZE("2004-01-01T12:12:12+10:59",&d));
  MYASSERT(PARSE_AND_NORMALIZE("2004-01-01T12:12:12+10:059",&d));
  MYASSERT(PARSE_AND_NORMALIZE("2004-01-01T12:12:12+010:59",&d));
  MYASSERT(PARSE_AND_NORMALIZE("2004-01-01T12:12:12+10:59a",&d));
  MYASSERT(PARSE_AND_NORMALIZE("2004-01-01T12:12:12+10:059",&d));

  MYASSERT(test_datetime_parser_tostring("2004-12-31T23:50:22-01:15", "2005-01-01T01:05:22Z")==0);
  MYASSERT(test_datetime_parser_tostring("2005-01-01T01:00:05+02:12", "2004-12-31T22:48:05Z")==0);
  MYASSERT(test_datetime_parser_tostring("0001-01-01T00:00:00+00:01", "-0001-12-31T23:59:00Z")==0);
  MYASSERT(test_datetime_parser_tostring("-0001-12-31T23:59:00-00:01", "0001-01-01T00:00:00Z")==0);
}


int
main(int argc, char *argv[]) {
  char const *program=rasqal_basename(*argv);
  test_datetimes(program);

#if 0
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
#endif

  return 0;
}
#endif
