/* -*- Mode: c; c-basic-offset: 2 -*-
 *
 * manifest.c - Run tests from SPARQL query test manifests
 *
 * Copyright (C) 2014, David Beckett http://www.dajobe.org/
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
 * USAGE:
 *   manifest MANIFEST-FILE [BASE-URI]
 *
 * Run the tests in MANIFEST-FILE
 *
 * NOTE: This is not a supported utility.  It is only used for testing
 * invoked by 'improve' and 'check-sparql' and may be replaced.
 *
 */

#include <stdio.h>
#include <string.h>

#ifdef HAVE_CONFIG_H
#include <rasqal_config.h>
#endif

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif

#include <rasqal.h>

#include <rasqal_internal.h>

#include <raptor2.h>


int main(int argc, char *argv[]);


static const char *program = "manifest";

static int debug = 1;
static int dryrun = 0;
static int verbose = 1;

static int error_count = 0;
static int warning_count = 0;

static const int indent_step = 2;
static const int linewrap = 78;
static const int banner_width = linewrap - 10;

static void
manifest_log_handler(void *data, raptor_log_message *message)
{
  raptor_parser *parser = (raptor_parser*)data;

  switch(message->level) {
    case RAPTOR_LOG_LEVEL_FATAL:
    case RAPTOR_LOG_LEVEL_ERROR:
      fprintf(stderr, "%s: Error - ", program);
      raptor_locator_print(message->locator, stderr);
      fprintf(stderr, " - %s\n", message->text);

      raptor_parser_parse_abort(parser);
      error_count++;
      break;

    case RAPTOR_LOG_LEVEL_WARN:
      fprintf(stderr, "%s: Warning - ", program);
      raptor_locator_print(message->locator, stderr);
      fprintf(stderr, " - %s\n", message->text);

      warning_count++;
      break;

    case RAPTOR_LOG_LEVEL_NONE:
    case RAPTOR_LOG_LEVEL_TRACE:
    case RAPTOR_LOG_LEVEL_DEBUG:
    case RAPTOR_LOG_LEVEL_INFO:

      fprintf(stderr, "%s: Unexpected %s message - ", program,
              raptor_log_level_get_label(message->level));
      raptor_locator_print(message->locator, stderr);
      fprintf(stderr, " - %s\n", message->text);
      break;
  }
}

typedef enum
{
  STATE_PASS,
  STATE_FAIL,
  STATE_XFAIL,
  STATE_UXPASS,
  STATE_SKIP,
  STATE_LAST = STATE_SKIP
} manifest_test_state;

typedef enum {
  /* these are alternatives */
  FLAG_IS_QUERY     = 1, /* SPARQL query; lang="sparql10" or "sparql11" */
  FLAG_IS_UPDATE    = 2, /* SPARQL update; lang="sparql-update" */
  FLAG_IS_PROTOCOL  = 4, /* SPARQL protocol */
  FLAG_IS_SYNTAX    = 8, /* syntax test: implies no execution */

  /* these are extras */
  FLAG_LANG_SPARQL_11 = 16, /* "sparql11" else "sparql10" */
  FLAG_MUST_FAIL      = 32, /* must FAIL otherwise must PASS  */
  FLAG_HAS_ENTAILMENT_REGIME = 64,
  FLAG_RESULT_CARDINALITY_LAX = 128, /* else strict (exact match) */
} manifest_test_type_bitflags;


typedef struct
{
  char* dir;
  rasqal_literal* test_node; /* the test node (URI or blank node) */
  char* name; /* <test-uri> mf:name ?value */
  char* desc; /* <test-uri> rdfs:comment ?value */
  manifest_test_state expect; /* derived from <test-uri> rdf:type ?value */
  raptor_uri* data; /* <test-uri> qt:data ?uri */
  raptor_uri* data_graph;  /* <test-uri> qt:dataGraph ?uri */
  raptor_uri* expected_result; /* <test-uri> mf:result ?uri */
  unsigned int flags; /* bit flags from #manifest_test_type_bitflags */

  /* Test output */
  manifest_test_state result;
  char* details; /* error details */
  char* log; /* error log */
} manifest_test;


typedef struct
{
  rasqal_world* world;
  manifest_test_state state;
  char* name; /* short name */
  char* desc; /* description from ?manifest rdfs:comment ?value */
  char* dir; /* directory */
  char* path; /* for envariable PATH */
  raptor_sequence* tests; /* sequence of manifest_test */
  char* details; /* error details */
} manifest_testsuite;


typedef struct
{
  manifest_test_state state;
  char* details;
  raptor_sequence* states[STATE_LAST + 1];
} manifest_test_result;


static const char manifest_test_state_chars[STATE_LAST + 1] = ".F*!-";
static const char* manifest_test_state_labels[STATE_LAST + 1] = {
  "pass",
  "FAIL",
  "XFAIL",
  "UXPASS",
  "SKIP"
};


/* prototypes */
static void manifest_free_test(manifest_test* t);
static void manifest_free_testsuite(manifest_testsuite* ts);


static char
manifest_test_state_char(manifest_test_state state)
{
  if(state > STATE_LAST)
    return '\0';

  return manifest_test_state_chars[(unsigned int)state];
}


static const char*
manifest_test_state_label(manifest_test_state state)
{
  if(state > STATE_LAST)
    return NULL;

  return manifest_test_state_labels[(unsigned int)state];
}


static manifest_test_result*
manifest_new_test_result(void)
{
  manifest_test_result* result;
  int i;

  result = (manifest_test_result*)calloc(sizeof(*result), 1);
  if(!result)
    return NULL;

  result->state = STATE_FAIL;
  /* total_result->details = NULL; */
  for(i = 0; i < STATE_LAST; i++)
    /* Holding pointers; the tests are owned by the testsuites */
    result->states[i] = raptor_new_sequence(NULL, NULL);
  return result;
}

static void
manifest_free_test_result(manifest_test_result* result)
{
  int i;

  if(!result)
    return;

  if(result->details)
    free(result->details);

  for(i = 0; i < STATE_LAST; i++) {
    if(result->states[i])
      raptor_free_sequence(result->states[i]);
  }

  free(result);
}


/**
 * manifest_new_test:
 * @name: test name
 * @description: description (or NULL)
 * @dir: directory (or NULL)
 * @expect: expected result - pass or fail
 * @test_node: identifier for this test
 * @data: data URI
 * @data_graph: data graph URI
 * @expected_result: expected result file
 * @flags: bit flags
 *
 * Create a new test from paramters
 *
 * These are all input parameters and become owned by this object.
 *
 */
static manifest_test*
manifest_new_test(char *name, char *description, char* dir,
                  rasqal_literal* test_node,
                  raptor_uri* data,
                  raptor_uri* data_graph,
                  raptor_uri* expected_result,
                  unsigned int flags)
{
  manifest_test* t;
  manifest_test_state expect = (flags & FLAG_MUST_FAIL) ? STATE_FAIL : STATE_PASS;

  t = (manifest_test*)calloc(sizeof(*t), 1);
  t->name = name;
  if(description)
    t->desc = description;
  t->dir = dir;
  t->expect = expect;
  t->test_node = test_node;
  t->data = data;
  t->data_graph = data_graph;
  t->expected_result = expected_result;
  t->flags = flags;

  return t;
}

static void
manifest_free_test(manifest_test* t)
{
  if(!t)
    return;

  if(t->name)
    free(t->name);
  if(t->desc)
    free(t->desc);
  if(t->dir)
    free(t->dir);
  if(t->test_node)
    rasqal_free_literal(t->test_node);
  if(t->data)
    raptor_free_uri(t->data);
  if(t->data_graph)
    raptor_free_uri(t->data_graph);
  if(t->expected_result)
    raptor_free_uri(t->expected_result);
  free(t);
}


static unsigned int
manifest_decode_test_type(raptor_uri* test_type)
{
  unsigned int flags = 0;
  const char* str;

  if(!test_type)
    return flags;

  str = (const char*)raptor_uri_as_string(test_type);

  if(strstr(str, "UpdateEvaluationTest"))
    return FLAG_IS_UPDATE;

  if(strstr(str, "ProtocolTest"))
    return FLAG_IS_PROTOCOL;

  if(strstr(str, "Syntax")) {
    flags |= FLAG_IS_SYNTAX;

    if(strstr(str, "Negative") || strstr(str, "TestBadSyntax")) {
      flags |= FLAG_MUST_FAIL;
    }
  }

  if(strstr(str, "Test11"))
    flags |= FLAG_LANG_SPARQL_11;

  return flags;
}



/**
 * manifest_new_testsuite:
 * @world: rasqal world
 * @name: testsuite name
 * @dir: directory containing testsuite
 * @uri: manifest URI
 * @base_uri: manifest base URI
 *
 * Create a new testsuite from a manifest
 */
static manifest_testsuite*
manifest_new_testsuite(rasqal_world* world,
                       char *name, char* dir,
                       raptor_uri* uri, raptor_uri* base_uri)
{
  manifest_testsuite *ts;
  rasqal_dataset* ds = NULL;
  int rc = 0;
  raptor_world* raptor_world_ptr = rasqal_world_get_raptor(world);
  rasqal_literal* manifest_node = NULL;
  rasqal_literal* entries_node = NULL;
  rasqal_literal* list_node = NULL;
  rasqal_literal* node = NULL;
  const unsigned char* str = NULL;
  size_t size;
  raptor_sequence* tests = NULL;

  /* Initialize base */
  ts = (manifest_testsuite*)calloc(sizeof(*ts), 1);
  if(!ts)
    return NULL;

  ts->world = world;
  ts->state = STATE_PASS;
  ts->name = strdup(name);
  /* ts->desc = NULL; */
  ts->dir = dir ? strdup(dir) : NULL;
  /* ts->path = NULL; */
  /* ts->tests = NULL; */
  /* ts->details = NULL; */

  /* Create Namespace URIs, concept URIs and rasqal literal concepts  */
  raptor_uri* rdfs_namespace_uri = raptor_new_uri(raptor_world_ptr, raptor_rdf_schema_namespace_uri);
  raptor_uri* mf_namespace_uri = raptor_new_uri(raptor_world_ptr, (const unsigned char*)"http://www.w3.org/2001/sw/DataAccess/tests/test-manifest#");
  raptor_uri* t_namespace_uri = raptor_new_uri(raptor_world_ptr, (const unsigned char*)"http://ns.librdf.org/2009/test-manifest#");
  raptor_uri* tq_namespace_uri = raptor_new_uri(raptor_world_ptr, (const unsigned char*)"http://www.w3.org/2001/sw/DataAccess/tests/test-query#");

  raptor_uri* mf_Manifest_uri = raptor_new_uri_from_uri_local_name(raptor_world_ptr, mf_namespace_uri, (const unsigned char*)"Manifest");
  raptor_uri* mf_entries_uri = raptor_new_uri_from_uri_local_name(raptor_world_ptr, mf_namespace_uri, (const unsigned char*)"entries");
  raptor_uri* mf_name_uri = raptor_new_uri_from_uri_local_name(raptor_world_ptr, mf_namespace_uri, (const unsigned char*)"name");
  raptor_uri* mf_action_uri = raptor_new_uri_from_uri_local_name(raptor_world_ptr, mf_namespace_uri, (const unsigned char*)"action");
  raptor_uri* mf_result_uri = raptor_new_uri_from_uri_local_name(raptor_world_ptr, mf_namespace_uri, (const unsigned char*)"result");
  raptor_uri* mf_resultCardinality_uri = raptor_new_uri_from_uri_local_name(raptor_world_ptr, mf_namespace_uri, (const unsigned char*)"resultCardinality");
  raptor_uri* rdf_type_uri = raptor_new_uri_for_rdf_concept(raptor_world_ptr, (const unsigned char*)"type");
  raptor_uri* rdf_first_uri = raptor_new_uri_for_rdf_concept(raptor_world_ptr, (const unsigned char*)"first");
  raptor_uri* rdf_rest_uri = raptor_new_uri_for_rdf_concept(raptor_world_ptr, (const unsigned char*)"rest");
  raptor_uri* rdf_nil_uri = raptor_new_uri_for_rdf_concept(raptor_world_ptr, (const unsigned char*)"nil");
  raptor_uri* rdfs_comment_uri = raptor_new_uri_from_uri_local_name(raptor_world_ptr, rdfs_namespace_uri, (const unsigned char*)"comment");
  raptor_uri* t_path_uri = raptor_new_uri_from_uri_local_name(raptor_world_ptr, t_namespace_uri, (const unsigned char*)"path");
  raptor_uri* tq_data_uri = raptor_new_uri_from_uri_local_name(raptor_world_ptr, tq_namespace_uri, (const unsigned char*)"data");
  raptor_uri* tq_graphData_uri = raptor_new_uri_from_uri_local_name(raptor_world_ptr, tq_namespace_uri, (const unsigned char*)"graphData");

  rasqal_literal* mf_Manifest_literal = rasqal_new_uri_literal(world, raptor_uri_copy(mf_Manifest_uri));
  rasqal_literal* mf_entries_literal = rasqal_new_uri_literal(world, raptor_uri_copy(mf_entries_uri));
  rasqal_literal* mf_name_literal = rasqal_new_uri_literal(world, raptor_uri_copy(mf_name_uri));
  rasqal_literal* mf_action_literal = rasqal_new_uri_literal(world, raptor_uri_copy(mf_action_uri));
  rasqal_literal* mf_result_literal = rasqal_new_uri_literal(world, raptor_uri_copy(mf_result_uri));
  rasqal_literal* mf_resultCardinality_literal = rasqal_new_uri_literal(world, raptor_uri_copy(mf_resultCardinality_uri));
  rasqal_literal* rdf_type_literal = rasqal_new_uri_literal(world, raptor_uri_copy(rdf_type_uri));
  rasqal_literal* rdf_first_literal = rasqal_new_uri_literal(world, raptor_uri_copy(rdf_first_uri));
  rasqal_literal* rdf_rest_literal = rasqal_new_uri_literal(world, raptor_uri_copy(rdf_rest_uri));
  rasqal_literal* rdfs_comment_literal = rasqal_new_uri_literal(world, raptor_uri_copy(rdfs_comment_uri));
  rasqal_literal* t_path_literal = rasqal_new_uri_literal(world, raptor_uri_copy(t_path_uri));
  rasqal_literal* tq_data_literal = rasqal_new_uri_literal(world, raptor_uri_copy(tq_data_uri));
  rasqal_literal* tq_graphData_literal = rasqal_new_uri_literal(world, raptor_uri_copy(tq_graphData_uri));


  /* Make an RDF graph (dataset) to query */
  ds = rasqal_new_dataset(world);
  if(!ds) {
    fprintf(stderr, "%s: Failed to create dataset", program);
    rc = 1;
    goto tidy;
  }

  if(rasqal_dataset_load_graph_uri(ds, /* graph name */ NULL,
                                   uri, base_uri)) {
    fprintf(stderr, "%s: Failed to load graph into dataset", program);
    rc = 1;
    goto tidy;
  }


  manifest_node = rasqal_dataset_get_source(ds,
                                            rdf_type_literal,
                                            mf_Manifest_literal);
  if(!manifest_node) {
    fprintf(stderr, "No manifest found in graph\n");
    rc = 1;
    goto tidy;
  }

  if(debug > 2) {
    fputs("Manifest node is: ", stderr);
    rasqal_literal_print(manifest_node, stderr);
    fputc('\n', stderr);
  }


  entries_node = rasqal_dataset_get_target(ds,
                                           manifest_node,
                                           mf_entries_literal);
  if(!entries_node) {
    fprintf(stderr, "No tests found in manifest graph\n");
    rc = 0;
    goto tidy;
  }

  if(debug > 2) {
    fputs("Entries node is: ", stderr);
    rasqal_literal_print(entries_node, stderr);
    fputc('\n', stderr);
  }

  /* Get some text fields */
  node = rasqal_dataset_get_target(ds,
                                   manifest_node,
                                   rdfs_comment_literal);
  if(node) {
    str = rasqal_literal_as_counted_string(node, &size, 0, NULL);
    if(str) {
      ts->desc = (char*)malloc(size + 1);
      memcpy(ts->desc, str, size + 1);

      if(debug > 0) {
        fprintf(stderr, "Testsuite Description is: '%s'\n", ts->desc);
      }
    }
  }

  node = rasqal_dataset_get_target(ds,
                                   manifest_node,
                                   t_path_literal);
  if(node) {
    str = rasqal_literal_as_counted_string(node, &size, 0, NULL);
    if(str) {
      ts->path = (char*)malloc(size + 1);
      memcpy(ts->path, str, size + 1);

      if(debug > 0) {
        fprintf(stderr, "Testsuite PATH is: '%s'\n", ts->path);
      }
    }
  }


  tests = raptor_new_sequence((raptor_data_free_handler)manifest_free_test,
                              NULL);
  for(list_node = entries_node; list_node; ) {
    rasqal_literal* entry_node;
    rasqal_literal* action_node;
    manifest_test* t;

    if(debug > 1) {
      fputs("List node is: ", stderr);
      rasqal_literal_print(list_node, stderr);
      fputc('\n', stderr);
    }

    entry_node = rasqal_dataset_get_target(ds,
                                           list_node,
                                           rdf_first_literal);
    if(debug > 0) {
      fputs("Test resource is: ", stderr);
      rasqal_literal_print(entry_node, stderr);
      fputc('\n', stderr);
    }

    /* Get some text fields */
    char* test_name = NULL;
    node = rasqal_dataset_get_target(ds,
                                     entry_node,
                                     mf_name_literal);
    if(node) {
      str = rasqal_literal_as_counted_string(node, &size, 0, NULL);
      if(str) {
        test_name = (char*)malloc(size + 1);
        memcpy(test_name, str, size + 1);

        if(debug > 0) {
          fprintf(stderr, "  Test name: '%s'\n", test_name);
        }
      }
    }

    char* test_desc = NULL;
    node = rasqal_dataset_get_target(ds,
                                     entry_node,
                                     rdfs_comment_literal);
    if(node) {
      str = rasqal_literal_as_counted_string(node, &size, 0, NULL);
      if(str) {
        test_desc = (char*)malloc(size + 1);
        memcpy(test_desc, str, size + 1);

        if(debug > 0) {
          fprintf(stderr, "  Test desc: '%s'\n", test_desc);
        }
      }
    }

    action_node = rasqal_dataset_get_target(ds,
                                            entry_node,
                                            mf_action_literal);
    raptor_uri* test_data_uri = NULL;
    raptor_uri* test_graph_data_uri = NULL;
    if(action_node) {
      if(debug > 1) {
        fputs("  Action node is: ", stderr);
        rasqal_literal_print(action_node, stderr);
        fputc('\n', stderr);
      }

      node = rasqal_dataset_get_target(ds,
                                       action_node,
                                       tq_data_literal);
      if(node) {
        uri = rasqal_literal_as_uri(node);
        if(uri) {
          test_data_uri = uri;
                   if(debug > 0) {
            fprintf(stderr, "  Test data URI: '%s'\n",
                    raptor_uri_as_string(test_data_uri));
          }
        }
      }

      node = rasqal_dataset_get_target(ds,
                                       action_node,
                                       tq_graphData_literal);
      if(node) {
        uri = rasqal_literal_as_uri(node);
        if(uri) {
          test_graph_data_uri = raptor_uri_copy(uri);
                   if(debug > 0) {
            fprintf(stderr, "  Test graph data URI: '%s'\n",
                    raptor_uri_as_string(test_graph_data_uri));
          }
        }
      }

    } /* end if action node */

    raptor_uri* test_result_uri = NULL;
    node = rasqal_dataset_get_target(ds,
                                     entry_node,
                                     mf_result_literal);
    if(node) {
      uri = rasqal_literal_as_uri(node);
      if(uri) {
        test_result_uri = raptor_uri_copy(uri);

        if(debug > 0) {
          fprintf(stderr, "  Test result URI: '%s'\n",
                  raptor_uri_as_string(test_result_uri));
        }
      }
    }

    raptor_uri* test_type = NULL;
    node = rasqal_dataset_get_target(ds,
                                     entry_node,
                                     rdf_type_literal);
    if(node) {
      test_type = rasqal_literal_as_uri(node);

      if(debug > 0) {
        fprintf(stderr, "  Test type: '%s'\n",
                raptor_uri_as_string(test_type));
      }
    }

    unsigned int test_flags = manifest_decode_test_type(test_type);
    if(!test_flags & (FLAG_IS_QUERY | FLAG_IS_UPDATE | FLAG_IS_PROTOCOL | FLAG_IS_SYNTAX) ) {
      fprintf(stderr, "%s: Test resource %s has no type - assuming a query\n",
              program, rasqal_literal_as_string(entry_node));
      test_flags |= FLAG_IS_QUERY;
    }

    /* Get a few more flags from other nodes */
    node = rasqal_dataset_get_target(ds,
                                     entry_node,
                                     mf_resultCardinality_literal);
    if(node) {
      uri = rasqal_literal_as_uri(node);
      if(uri) {
        int is_lax;

        str = raptor_uri_as_string(uri);
        is_lax = strstr((const char*)str, "LaxCardinality");
        
        if(is_lax)
          test_flags |= FLAG_RESULT_CARDINALITY_LAX;
      }
    }

    if(debug > 0) {
      fprintf(stderr, "  Test result cardinality: %s\n",
              (test_flags & FLAG_RESULT_CARDINALITY_LAX) ? "lax" : "strict");
    }

#if 0
  my $test_uri=$entry_node; $test_uri =~ s/^<(.+)>$/$1/;
  my $test_type=$query_type; $test_type =~ s/^<(.+)>$/$1/ if defined $test_type;

  my $test_approval=$triples{$entry_node}->{"<${dawgt}approval>"}->[0];
  my $is_approved = 0;
  my $is_withdrawn = 0;
  if($test_approval) {
    warn "Test $name ($test_uri) state $test_approval\n"
      if $debug > 1;
    if($test_approval eq "<${dawgt}Withdrawn>") {
      warn "Test $name ($test_uri) was withdrawn\n"
	if $debug;
      $is_withdrawn = 1;
    }
    if($test_approval eq "<${dawgt}Approved>") {
      $is_approved = 1;
    }
  }

  my $has_entailment_regime = exists $triples{$action_node}->{"<${ent}entailmentRegime>"} || $triples{$action_node}->{"<${sd}entailmentRegime>"};;
#endif


    /* All the parameters become owned by the test */
    t = manifest_new_test(test_name, test_desc, dir,
                          rasqal_new_literal_from_literal(entry_node),
                          raptor_uri_copy(test_data_uri),
                          test_graph_data_uri,
                          test_result_uri,
                          test_flags);
    test_name = NULL;

    if(test_flags & (FLAG_IS_UPDATE | FLAG_IS_PROTOCOL)) {
      manifest_free_test(t);
      t = NULL;
      fprintf(stderr, "%s: Ignoring test %s type UPDATE / PROTOCOL - not supported\n", program, rasqal_literal_as_string(entry_node));
    } else {
      raptor_sequence_push(tests, t);
    }


    list_node = rasqal_dataset_get_target(ds,
                                          list_node,
                                          rdf_rest_literal);
    if(!list_node)
      break;

    if(list_node->type == RASQAL_LITERAL_URI) {
      uri = rasqal_literal_as_uri(list_node);
      if(uri && raptor_uri_equals(uri, rdf_nil_uri))
        break;
    }
  } /* end for list_node */

  ts->tests = tests; tests = NULL;
  ts->state = STATE_PASS;
  ts->details = NULL;

  tidy:
  if(ds)
    rasqal_free_dataset(ds);

  if(tests)
    raptor_free_sequence(tests);

  if(rdfs_namespace_uri)
    raptor_free_uri(rdfs_namespace_uri);
  if(mf_namespace_uri)
    raptor_free_uri(mf_namespace_uri);
  if(t_namespace_uri)
    raptor_free_uri(t_namespace_uri);
  if(tq_namespace_uri)
    raptor_free_uri(tq_namespace_uri);

  if(mf_Manifest_uri)
    raptor_free_uri(mf_Manifest_uri);
  if(mf_entries_uri)
    raptor_free_uri(mf_entries_uri);
  if(mf_name_uri)
    raptor_free_uri(mf_name_uri);
  if(mf_action_uri)
    raptor_free_uri(mf_action_uri);
  if(mf_result_uri)
    raptor_free_uri(mf_result_uri);
  if(mf_resultCardinality_uri)
    raptor_free_uri(mf_resultCardinality_uri);
  if(rdf_type_uri)
    raptor_free_uri(rdf_type_uri);
  if(rdf_first_uri)
    raptor_free_uri(rdf_first_uri);
  if(rdf_rest_uri)
    raptor_free_uri(rdf_rest_uri);
  if(rdf_nil_uri)
    raptor_free_uri(rdf_nil_uri);
  if(rdfs_comment_uri)
    raptor_free_uri(rdfs_comment_uri);
  if(t_path_uri)
    raptor_free_uri(t_path_uri);
  if(tq_data_uri)
    raptor_free_uri(tq_data_uri);
  if(tq_graphData_uri)
    raptor_free_uri(tq_graphData_uri);

  if(mf_Manifest_literal)
    rasqal_free_literal(mf_Manifest_literal);
  if(mf_entries_literal)
    rasqal_free_literal(mf_entries_literal);
  if(mf_name_literal)
    rasqal_free_literal(mf_name_literal);
  if(mf_action_literal)
    rasqal_free_literal(mf_action_literal);
  if(mf_result_literal)
    rasqal_free_literal(mf_result_literal);
  if(mf_resultCardinality_literal)
    rasqal_free_literal(mf_resultCardinality_literal);
  if(rdf_type_literal)
    rasqal_free_literal(rdf_type_literal);
  if(rdf_first_literal)
    rasqal_free_literal(rdf_first_literal);
  if(rdf_rest_literal)
    rasqal_free_literal(rdf_rest_literal);
  if(rdfs_comment_literal)
    rasqal_free_literal(rdfs_comment_literal);
  if(t_path_literal)
    rasqal_free_literal(t_path_literal);
  if(tq_data_literal)
    rasqal_free_literal(tq_data_literal);
  if(tq_graphData_literal)
    rasqal_free_literal(tq_graphData_literal);

  return ts;
}


static void
manifest_free_testsuite(manifest_testsuite* ts)
{
  if(!ts)
    return;

  if(ts->name)
    free(ts->name);
  if(ts->desc)
    free(ts->desc);
  if(ts->dir)
    free(ts->dir);
  if(ts->path)
    free(ts->path);
  if(ts->tests)
    raptor_free_sequence(ts->tests);
  if(ts->details)
    free(ts->details);
  free(ts);
}


static void
manifest_indent(FILE* fh, unsigned int indent)
{
  while(indent--)
    fputc(' ', fh);
}


static manifest_test_result*
manifest_run_testsuite(manifest_testsuite* ts, unsigned int indent)
{
  char* name = ts->name;
  char* desc = ts->desc ? ts->desc : name;
  int i;
  unsigned int expected_failures_count = 0;
  manifest_test* t = NULL;
  unsigned int column;
  manifest_test_result* result;

  /* Initialize */
  result = manifest_new_test_result();

  /* Run testsuite */
  manifest_indent(stdout, indent);
  fprintf(stdout, "Running testsuite %s: %s\n", name, desc);

  column = indent;
  for(i = 0; (t = raptor_sequence_get_at(ts->tests, i)); i++) {
    if(dryrun) {
      t->result = STATE_SKIP;
      t->details = NULL;
    } else {
      t->result = STATE_PASS;
#if 0
      t->result = manifest_run_test(ts, t);
#endif
    }

    if(t->expect == STATE_FAIL)
      expected_failures_count++;


    manifest_test_state state = t->result;
    if(!verbose)
      fputc(stdout, manifest_test_state_char(state));
    raptor_sequence_push(result->states[(unsigned int)state], t);

    column++;
    if(!verbose && column > linewrap) {
      fputc(stdout, '\n');
      manifest_indent(stdout, indent);
      column = indent;
    }

    if(verbose) {
      const char* label = manifest_test_state_label(state);
      unsigned int my_indent = indent + indent_step;
      manifest_indent(stdout, my_indent);
      fputs(t->name, stdout);
      fputs(": ", stdout);
      fputs(label, stdout);
      if(t->details) {
        fputs(" - ", stdout);
        fputs(t->details, stdout);
      }
      fputc('\n', stdout);
      if(verbose > 1) {
	if(state == STATE_FAIL && t->log) {
#if 0
	  my(@lines)=split(/\n/, $t->{log});
	  print $i."  ".join("\n${i}  ", @lines)."\n";
#endif
	}
      }
    }

  }

  if(!verbose)
    fputc('\n', stderr);

  unsigned int xfailed_count = raptor_sequence_size(result->states[STATE_XFAIL]);
  unsigned int failed_count = raptor_sequence_size(result->states[STATE_FAIL]);

  result->state = ((xfailed_count == expected_failures_count) && !failed_count) ? STATE_PASS : STATE_FAIL;

  return result;
}


/**
 * manifest_test_manifests:
 * @world: world
 * @manifest_uris: array of manifest URIs
 * @base_uri: base URI for manifest
 * @indent: indent size
 *
 * Run the given manifest testsuites returning a test result
 *
 * Return value: test result or NULL on failure
 */
static manifest_test_result*
manifest_test_manifests(rasqal_world* world,
                        raptor_uri** manifest_uris,
                        raptor_uri* base_uri,
                        unsigned int indent)
{
  manifest_test_state total_state = STATE_PASS;
  manifest_test_result* total_result = NULL;
  raptor_uri* uri;
  int i = 0;

  total_result = manifest_new_test_result();
  if(!total_result)
    return NULL;

  for(i = 0; (uri = manifest_uris[i]); i++) {
    int j;
    manifest_testsuite *ts;
    manifest_test_result* result = NULL;
    char* testsuite_name = (char*)raptor_uri_as_string(uri);

    ts = manifest_new_testsuite(world,
                                /* name */ testsuite_name,
                                /* dir */ NULL,
                                uri, base_uri);

    if(!ts) {
      fprintf(stderr, "%s: Failed to create test suite %s\n",
              program, testsuite_name);
      break;
    }

    result = manifest_run_testsuite(ts, indent);

#if 0
    format_testsuite_result(stdout, result, indent + indent_step);
#endif
    for(j = 0; j < STATE_LAST; j++)
      raptor_sequence_join(total_result->states[i], result->states[i]);

    if(result) {
      if(result->state == STATE_FAIL)
        total_state = STATE_FAIL;
    }

    manifest_free_test_result(result);

    if(i > 1)
      fputc('\n', stdout);

    if(ts)
      manifest_free_testsuite(ts);
  }

  total_result->state = total_state;

  manifest_indent(stdout, indent);
  fputs("Testsuites summary:\n", stdout);

#if 0
  format_testsuite_result(stdout, total_result, indent + indent_step);
#endif
  if(verbose) {
    manifest_indent(stdout, indent);
    fprintf(stdout, "Result status: %d\n", total_state);
  }

  return total_result;
}


int
main(int argc, char *argv[])
{
  rasqal_world *world = NULL;
  raptor_world* raptor_world_ptr = NULL;
  unsigned char *uri_string;
  raptor_uri *uri;
  raptor_uri *base_uri;
  int rc = 0;
  int free_uri_string = 0;

  if(argc < 2 || argc > 3) {
    fprintf(stderr, "USAGE: %s MANIFEST-FILE [BASE-URI]\n", program);
    rc = 1;
    goto tidy;
  }

  world = rasqal_new_world();
  if(!world || rasqal_world_open(world)) {
    fprintf(stderr, "%s: rasqal_world init failed\n", program);
    rc = 1;
    goto tidy;
  }

  raptor_world_ptr = rasqal_world_get_raptor(world);
  rasqal_world_set_log_handler(world, world, manifest_log_handler);

  uri_string = (unsigned char*)argv[1];
  if(!access((const char*)uri_string, R_OK)) {
    uri_string = raptor_uri_filename_to_uri_string((char*)uri_string);
    uri = raptor_new_uri(raptor_world_ptr, uri_string);
    free_uri_string = 1;
  } else {
    uri = raptor_new_uri(raptor_world_ptr, (const unsigned char*)uri_string);
  }

  if(argc == 3) {
    char* base_uri_string = argv[2];
    base_uri = raptor_new_uri(raptor_world_ptr, (unsigned char*)(base_uri_string));
  } else {
    base_uri = raptor_uri_copy(uri);
  }

  raptor_uri* manifest_uris[2] = { uri, NULL };

  manifest_test_result* result;
  result = manifest_test_manifests(world, manifest_uris, base_uri, 0);

  if(result)
    manifest_free_test_result(result);

  raptor_free_uri(base_uri);
  raptor_free_uri(uri);
  if(free_uri_string)
    raptor_free_memory(uri_string);

  rasqal_free_world(world);

  tidy:
  if(warning_count)
    rc = 2;
  else if(error_count)
    rc = 1;

  return rc;
}
