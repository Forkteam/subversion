#include "svn_hash.h"
#include "svn_mergeinfo.h"
static const char *unidiff_with_mergeinfo =
  "Index: A/C"                                                          NL
  "===================================================================" NL
  "--- A/C\t(revision 2)"                                               NL
  "+++ A/C\t(working copy)"                                             NL
  "Modified: svn:ignore"                                                NL
  "## -7,6 +7,7 ##"                                                     NL
  " configure"                                                          NL
  " libtool"                                                            NL
  " .gdb_history"                                                       NL
  "+.swig_checked"                                                      NL
  " *.orig"                                                             NL
  " *.rej"                                                              NL
  " TAGS"                                                               NL
  "Modified: svn:mergeinfo"                                             NL
  "## -0,1 +0,3 ##"                                                     NL
  "   Reverse-merged /subversion/branches/1.6.x-r935631:r952683-955333" NL
  "   /subversion/branches/nfc-nfd-aware-client:r870276,870376 をマージしました"NL
  "   Fusionné /subversion/branches/1.7.x-r1507044:r1507300-1511568"    NL
  "   Merged /subversion/branches/1.8.x-openssl-dirs:r1535139"          NL;
/* The above diff intentionally contains i18n versions of some lines. */
  SVN_TEST_STRING_ASSERT(prop_patch->name, "prop_add");
static svn_error_t *
test_parse_unidiff_with_mergeinfo(apr_pool_t *pool)
{
  svn_patch_file_t *patch_file;
  svn_boolean_t reverse;
  svn_boolean_t ignore_whitespace;
  int i;
  apr_pool_t *iterpool;

  reverse = FALSE;
  ignore_whitespace = FALSE;
  iterpool = svn_pool_create(pool);
  for (i = 0; i < 2; i++)
    {
      svn_patch_t *patch;
      svn_mergeinfo_t mergeinfo;
      svn_mergeinfo_t reverse_mergeinfo;
      svn_rangelist_t *rangelist;
      svn_merge_range_t *range;

      svn_pool_clear(iterpool);

      SVN_ERR(create_patch_file(&patch_file, unidiff_with_mergeinfo,
                                pool));

      SVN_ERR(svn_diff_parse_next_patch(&patch, patch_file, reverse,
                                        ignore_whitespace, iterpool,
                                        iterpool));
      SVN_TEST_ASSERT(patch);
      SVN_TEST_STRING_ASSERT(patch->old_filename, "A/C");
      SVN_TEST_STRING_ASSERT(patch->new_filename, "A/C");

      /* svn:ignore */
      SVN_TEST_ASSERT(apr_hash_count(patch->prop_patches) == 1);

      SVN_TEST_ASSERT(patch->mergeinfo);
      SVN_TEST_ASSERT(patch->reverse_mergeinfo);

      if (reverse)
        {
          mergeinfo = patch->reverse_mergeinfo;
          reverse_mergeinfo = patch->mergeinfo;
        }
      else
        {
          mergeinfo = patch->mergeinfo;
          reverse_mergeinfo = patch->reverse_mergeinfo;
        }

      rangelist = svn_hash_gets(reverse_mergeinfo,
                                "/subversion/branches/1.6.x-r935631");
      SVN_TEST_ASSERT(rangelist);
      SVN_TEST_ASSERT(rangelist->nelts == 1);
      range = APR_ARRAY_IDX(rangelist, 0, svn_merge_range_t *);
      SVN_TEST_ASSERT(range->start == 952682);
      SVN_TEST_ASSERT(range->end == 955333);

      rangelist = svn_hash_gets(mergeinfo,
                                "/subversion/branches/nfc-nfd-aware-client");
      SVN_TEST_ASSERT(rangelist);
      SVN_TEST_ASSERT(rangelist->nelts == 2);
      range = APR_ARRAY_IDX(rangelist, 0, svn_merge_range_t *);
      SVN_TEST_ASSERT(range->end == 870276);
      range = APR_ARRAY_IDX(rangelist, 1, svn_merge_range_t *);
      SVN_TEST_ASSERT(range->end == 870376);

      rangelist = svn_hash_gets(mergeinfo,
                                "/subversion/branches/1.8.x-openssl-dirs");
      SVN_TEST_ASSERT(rangelist);
      SVN_TEST_ASSERT(rangelist->nelts == 1);
      range = APR_ARRAY_IDX(rangelist, 0, svn_merge_range_t *);
      SVN_TEST_ASSERT(range->end == 1535139);

      reverse = !reverse;
      SVN_ERR(svn_diff_close_patch_file(patch_file, pool));
    }
  svn_pool_destroy(iterpool);
  return SVN_NO_ERROR;
}

    SVN_TEST_PASS2(test_parse_unidiff_with_mergeinfo,
                   "test parsing unidiffs with mergeinfo"),