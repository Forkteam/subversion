/* lock.c :  functions for manipulating filesystem locks.
 *
 * ====================================================================
 * Copyright (c) 2000-2004 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 *
 * This software consists of voluntary contributions made by many
 * individuals.  For exact contribution history, see the revision
 * history and logs, available at http://subversion.tigris.org/.
 * ====================================================================
 */


#include "svn_pools.h"
#include "svn_error.h"
#include "svn_fs.h"

#include "lock.h"
#include "err.h"
#include "bdb/locks-table.h"
#include "bdb/lock-tokens-table.h"
#include "../libsvn_fs/fs-loader.h"


static svn_error_t *
check_lock_expired (svn_lock_t *lock, trail_t *trail)
{
  if (! lock->expiration_date
      || (lock->expiration_date > apr_time_now()))   
    return SVN_NO_ERROR;

  SVN_ERR (svn_fs_bdb__lock_token_delete (trail->fs, lock->token, trail));
  SVN_ERR (svn_fs_bdb__lock_delete (trail->fs, lock->path, trail));
  
  return svn_error_create (SVN_ERR_FS_LOCK_EXPIRED, 0, "Lock expired.");
}


svn_error_t *
svn_fs_base__lock (svn_lock_t **lock,
                   svn_fs_t *fs,
                   const char *path,
                   svn_boolean_t force,
                   long int timeout,
                   const char *current_token,
                   apr_pool_t *pool)
{
  return svn_error_create (SVN_ERR_UNSUPPORTED_FEATURE, 0,
                           "Function not yet implemented.");
}


struct unlock_args
{
  const char *token;
  svn_boolean_t force;
};


static svn_error_t *
txn_body_unlock (void *baton, trail_t *trail)
{
  struct unlock_args *args = baton;
  svn_lock_t *lock;

  /* This could return SVN_ERR_FS_BAD_LOCK_TOKEN */
  SVN_ERR (svn_fs_bdb__lock_get (&lock, trail->fs, args->token, trail));
  
  /* This could return SVN_ERR_FS_LOCK_EXPIRED */
  SVN_ERR (check_lock_expired (lock, trail));

  /* There better be a username attached to the fs. */
  if (!trail->fs->access_ctx || !trail->fs->access_ctx->username)
    return svn_fs_base__err_no_user (trail->fs);

  /* And that username better be the same as the lock's owner. */
  if (!args->force
      && strcmp(trail->fs->access_ctx->username, lock->owner) != 0)
    return svn_fs_base__err_lock_owner_mismatch (trail->fs,
             trail->fs->access_ctx->username,
             lock->owner);

  SVN_ERR (svn_fs_bdb__lock_token_delete (trail->fs, lock->token, trail));
  return svn_fs_bdb__lock_delete (trail->fs, lock->path, trail);
}


svn_error_t *
svn_fs_base__unlock (svn_fs_t *fs,
                     const char *token,
                     svn_boolean_t force,
                     apr_pool_t *pool)
{
  struct unlock_args args;

  SVN_ERR (svn_fs_base__check_fs (fs));

  args.token = token;
  args.force = force;
  return svn_fs_base__retry_txn (fs, txn_body_unlock, &args, pool);
}


svn_error_t *
svn_fs_base__get_lock_from_path_helper (svn_lock_t **lock_p,
                                        const char *path,
                                        trail_t *trail)
{
  const char *lock_token;
  svn_error_t *err;
  
  err = svn_fs_bdb__lock_token_get (&lock_token, trail->fs, path, trail);
  if (err && err->apr_err == SVN_ERR_FS_NO_SUCH_LOCK)
    {
      svn_error_clear (err);
      *lock_p = NULL;
      return SVN_NO_ERROR;
    }
  else
    SVN_ERR (err);

  SVN_ERR (svn_fs_bdb__lock_get (lock_p, trail->fs, lock_token, trail));

  err = check_lock_expired (*lock_p, trail);
  if (err && err->apr_err == SVN_ERR_FS_LOCK_EXPIRED)
    {
      svn_error_clear (err);
      *lock_p = NULL;
      return SVN_NO_ERROR;
    }

  return err;
}


struct lock_token_get_args
{
  svn_lock_t **lock_p;
  const char *path;
};


static svn_error_t *
txn_body_get_lock_from_path (void *baton, trail_t *trail)
{
  struct lock_token_get_args *args = baton;
  return svn_fs_base__get_lock_from_path_helper (args->lock_p, args->path,
                                                 trail);
}


svn_error_t *
svn_fs_base__get_lock_from_path (svn_lock_t **lock,
                                 svn_fs_t *fs,
                                 const char *path,
                                 apr_pool_t *pool)
{
  struct lock_token_get_args args;
  svn_error_t *err;

  SVN_ERR (svn_fs_base__check_fs (fs));
  
  args.path = path;
  args.lock_p = lock;  
  return svn_fs_base__retry_txn (fs, txn_body_get_lock_from_path,
                                 &args, pool);
  return err;
}


struct lock_get_args
{
  svn_lock_t **lock_p;
  const char *lock_token;
};


static svn_error_t *
txn_body_get_lock_from_token (void *baton, trail_t *trail)
{
  struct lock_get_args *args = baton;
  
  SVN_ERR (svn_fs_bdb__lock_get (args->lock_p, trail->fs,
                                 args->lock_token, trail));
  
  return check_lock_expired (*args->lock_p, trail);
}


svn_error_t *
svn_fs_base__get_lock_from_token (svn_lock_t **lock,
                                  svn_fs_t *fs,
                                  const char *token,
                                  apr_pool_t *pool)
{
  struct lock_get_args args;

  SVN_ERR (svn_fs_base__check_fs (fs));
  
  args.lock_token = token;
  args.lock_p = lock;
  return svn_fs_base__retry_txn (fs, txn_body_get_lock_from_token,
                                 &args, pool);
}



svn_error_t *
svn_fs_base__get_locks_helper (apr_hash_t **locks_p,
                               const char *path,
                               trail_t *trail)
{
  apr_hash_index_t *hi;

  SVN_ERR (svn_fs_bdb__lock_tokens_get (locks_p, trail->fs, path, trail));

  hi = apr_hash_first (trail->pool, *locks_p);
  while (hi)
    {
      const void *key;
      apr_ssize_t keylen;
      void *token;
      svn_lock_t *lock;
      svn_error_t *err;

      apr_hash_this (hi, &key, &keylen, &token);
      hi = apr_hash_next (hi);

      SVN_ERR (svn_fs_bdb__lock_get (&lock, trail->fs, token, trail));
      err = check_lock_expired (lock, trail);

      /* If the lock has expired, simply remove it from the hash */
      if (err && err->apr_err == SVN_ERR_FS_LOCK_EXPIRED)
        {
          svn_error_clear (err);
          lock = NULL;
        }
      else
        SVN_ERR (err);

      apr_hash_set (*locks_p, key, keylen, lock);
    }

  return SVN_NO_ERROR;
}


struct locks_get_args
{
  apr_hash_t **locks_p;
  const char *path;
};


static svn_error_t *
txn_body_get_locks (void *baton, trail_t *trail)
{
  struct locks_get_args *args = baton;
  return svn_fs_base__get_locks_helper (args->locks_p, args->path, trail);
}


svn_error_t *
svn_fs_base__get_locks (apr_hash_t **locks,
                        svn_fs_t *fs,
                        const char *path,
                        apr_pool_t *pool)
{
  struct locks_get_args args;

  SVN_ERR (svn_fs_base__check_fs (fs));
  
  args.locks_p = locks;
  args.path = path;
  return svn_fs_base__retry_txn (fs, txn_body_get_locks, &args, pool);
}



svn_error_t *
svn_fs_base__allow_locked_operation (svn_boolean_t *allow,
                                     const char *path,
                                     svn_node_kind_t kind,
                                     svn_boolean_t recurse,
                                     trail_t *trail)
{
  *allow = FALSE;

  /* In order for partial-key match to work properly, a directory
     must end with '/'.  */
  if (kind == svn_node_dir)
    path = apr_pstrcat (trail->pool, path, "/", NULL);

  if (kind == svn_node_dir && recurse)
    {
      apr_hash_t *locks;
      
      /* Discover all locks at or below the path. */
      SVN_ERR (svn_fs_base__get_locks_helper (&locks, path, trail));

      /* Easy out. */
      if (apr_hash_count (locks) == 0)
        {
          *allow = TRUE;
          return SVN_NO_ERROR;
        }

      /* Some number of locks exist below path; are we allowed to
         change them? */
      return svn_fs__verify_locks (allow, trail->fs, locks, trail->pool);      
    }

  /* We're either checking a file, or checking a path non-recursively: */
    {
      svn_lock_t *lock;

      /* Discover any lock attached to the path. */
      SVN_ERR (svn_fs_base__get_lock_from_path_helper (&lock, path, trail));

      /* Easy out. */
      if (! lock)
        {
          *allow = TRUE;
          return SVN_NO_ERROR;
        }

      /* The path is locked;  are we allowed to change it? */
      return svn_fs__verify_lock (allow, trail->fs, lock, trail->pool);
    }
}
