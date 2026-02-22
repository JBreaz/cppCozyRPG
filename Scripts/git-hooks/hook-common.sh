#!/bin/sh
# Scripts/git-hooks/hook-common.sh
# Shared utilities for Git hooks (Unreal + strict binary conflict guard).
# LOCKLESS: we do NOT maintain a lock file. We compute "required guarded" each time.
# Ledgers in .git:
#   - ue_binary_conflicts.context   (context id to prevent stale approvals)
#   - ue_binary_conflicts.resolved  (approvals written by helpers)
#   - ue_binary_conflicts.audit     (audit trail)

hook_repo_root() { git rev-parse --show-toplevel 2>/dev/null; }
hook_git_dir()   { git rev-parse --git-dir 2>/dev/null; }
hook_git_common_dir() { git rev-parse --git-common-dir 2>/dev/null; }
hook_git_state_dir() {
  if [ -n "${HOOK_GIT_STATE_DIR_CACHE:-}" ]; then
    printf "%s" "$HOOK_GIT_STATE_DIR_CACHE"
    return 0
  fi

  gd="$(hook_git_common_dir 2>/dev/null || true)"
  [ -n "${gd:-}" ] || gd="$(hook_git_dir 2>/dev/null || true)"
  HOOK_GIT_STATE_DIR_CACHE="$gd"
  printf "%s" "$gd"
}

hook_debug() {
  [ "${UE_HOOK_DEBUG:-0}" = "1" ] || return 0
  printf "%s[HOOK-DEBUG] %s%s\n" "${CYAN:-}" "$*" "${RESET:-}" 1>&2
}

# Convert git paths into something Git Bash can always write to on Windows.
hook_git_path() {
  p="$(git rev-parse --git-path "$1" 2>/dev/null || true)"
  [ -n "$p" ] || return 1

  if command -v cygpath >/dev/null 2>&1; then
    cygpath -u "$p"
  else
    printf "%s" "$p"
  fi
}

hook_context_file()  { gd="$(hook_git_state_dir 2>/dev/null || true)"; [ -n "${gd:-}" ] || return 1; printf "%s/%s" "$gd" "ue_binary_conflicts.context"; }
hook_resolved_file() { gd="$(hook_git_state_dir 2>/dev/null || true)"; [ -n "${gd:-}" ] || return 1; printf "%s/%s" "$gd" "ue_binary_conflicts.resolved"; }
hook_audit_file()    { gd="$(hook_git_state_dir 2>/dev/null || true)"; [ -n "${gd:-}" ] || return 1; printf "%s/%s" "$gd" "ue_binary_conflicts.audit"; }

hook_list_approved_guarded() {
  rf="$(hook_resolved_file 2>/dev/null || true)"
  [ -n "${rf:-}" ] || return 0
  [ -f "$rf" ] || return 0

  # Normalize: strip NUL, strip CR, drop empty, sort unique
  tr -d '\000' <"$rf" 2>/dev/null | tr -d '\r' | sed '/^$/d' | sort -u
}

hook_list_remaining_required_guarded() {
  req_tmp="$(mktemp 2>/dev/null || true)"
  app_tmp="$(mktemp 2>/dev/null || true)"
  [ -n "${req_tmp:-}" ] && [ -n "${app_tmp:-}" ] || return 0

  hook_list_required_guarded_unmerged_and_overlap 2>/dev/null |
    tr -d '\000' | sed '/^$/d' | sort -u >"$req_tmp" || true

  hook_list_approved_guarded 2>/dev/null >"$app_tmp" || true

  # remaining = required - approved, filter empty lines
  comm -23 "$req_tmp" "$app_tmp" 2>/dev/null | sed '/^[[:space:]]*$/d' || true

  rm -f "$req_tmp" "$app_tmp" 2>/dev/null || true
}

hook_is_merge_in_progress() {
  if [ "${HOOK_MERGE_CACHE_SET:-0}" = "1" ]; then
    [ "${HOOK_MERGE_CACHE:-1}" -eq 0 ] && return 0 || return 1
  fi

  gd="$(hook_git_state_dir)"
  if [ -f "$gd/MERGE_HEAD" ] || [ -f "$gd/CHERRY_PICK_HEAD" ] || [ -f "$gd/REVERT_HEAD" ]; then
    HOOK_MERGE_CACHE=0
    HOOK_MERGE_CACHE_SET=1
    return 0
  fi

  HOOK_MERGE_CACHE=1
  HOOK_MERGE_CACHE_SET=1
  return 1
}

hook_cleanup_stale_rebase_markers() {
  # Simplified - just clean REBASE_HEAD when directories are gone
  rbm="$(hook_git_path "rebase-merge" 2>/dev/null || true)"
  rba="$(hook_git_path "rebase-apply" 2>/dev/null || true)"
  
  has_dir=0
  if [ -n "$rbm" ] && [ -d "$rbm" ]; then has_dir=1; fi
  if [ -n "$rba" ] && [ -d "$rba" ]; then has_dir=1; fi
  
  if [ "$has_dir" -eq 0 ]; then
    rebase_head="$(hook_git_path "REBASE_HEAD" 2>/dev/null || true)"
    if [ -n "$rebase_head" ] && [ -f "$rebase_head" ]; then
      hook_trace "cleaning stale REBASE_HEAD (no rebase directories)"
      rm -f "$rebase_head" 2>/dev/null || true
    fi
  fi
}

hook_is_rebase_in_progress() {
  if [ "${HOOK_REBASE_CACHE_SET:-0}" = "1" ]; then
    [ "${HOOK_REBASE_CACHE:-1}" -eq 0 ] && return 0 || return 1
  fi

  # Check Git's native markers - directories ONLY
  gd="$(hook_git_state_dir 2>/dev/null || true)"
  rbm="$gd/rebase-merge"
  rba="$gd/rebase-apply"
  
  if [ -n "$rbm" ] && [ -d "$rbm" ]; then
    hook_trace "rebase detected via rebase-merge dir"
    HOOK_REBASE_CACHE=0
    HOOK_REBASE_CACHE_SET=1
    return 0
  fi
  if [ -n "$rba" ] && [ -d "$rba" ]; then
    hook_trace "rebase detected via rebase-apply dir"
    HOOK_REBASE_CACHE=0
    HOOK_REBASE_CACHE_SET=1
    return 0
  fi

  # Fallback: during some rebase steps/hooks, marker directories can be transient.
  # Reflog action still carries rebase context (ex: "rebase (pick)").
  case "${GIT_REFLOG_ACTION:-}" in
    *rebase*)
      hook_trace "rebase detected via GIT_REFLOG_ACTION=$GIT_REFLOG_ACTION"
      HOOK_REBASE_CACHE=0
      HOOK_REBASE_CACHE_SET=1
      return 0
      ;;
  esac
  
  hook_trace "no rebase detected"
  HOOK_REBASE_CACHE=1
  HOOK_REBASE_CACHE_SET=1
  return 1
}

hook_has_unmerged() {
  # fast: any unmerged entries?
  git ls-files -u 2>/dev/null | awk 'NF{exit 0} END{exit 1}'
  # awk exits 0 if it saw any line (meaning there IS unmerged)
}

hook_is_rebase_stopped() {
  rbm_patch="$(hook_git_path "rebase-merge/patch" 2>/dev/null || true)"
  rbm_stop="$(hook_git_path "rebase-merge/stopped-sha" 2>/dev/null || true)"
  rba_apply="$(hook_git_path "rebase-apply/applying" 2>/dev/null || true)"
  rba_patch="$(hook_git_path "rebase-apply/patch" 2>/dev/null || true)"

  # rebase-merge: stopped-sha + patch (patch removed after continue)
  if [ -n "$rbm_stop" ] && [ -n "$rbm_patch" ] && [ -f "$rbm_stop" ] && [ -f "$rbm_patch" ]; then
    return 0
  fi

  # rebase-apply backend uses "applying" + "patch" while stopped.
  if [ -n "$rba_apply" ] && [ -n "$rba_patch" ] && [ -f "$rba_apply" ] && [ -f "$rba_patch" ]; then
    return 0
  fi

  return 1
}

hook_should_show_guard_guidance() {
  # Show guidance only when user action is required AND we just reset ledgers.
  # Check if we're at the START of a conflict (ledgers were just reset)
  
  ctx="$(hook_context)"
  if [ "$ctx" = "none" ]; then
    return 1
  fi
  
  # Only show if there are unmerged files OR rebase is stopped
  if hook_is_merge_in_progress; then
    hook_has_unmerged; return $?
  fi
  
  if hook_is_rebase_in_progress; then
    # Only show during stopped state with unmerged files
    # This prevents showing after every commit in multi-commit rebase
    if hook_is_rebase_stopped && hook_has_unmerged; then
      return 0
    fi
    return 1
  fi
  
  return 1
}

hook_context() {
  if [ "${HOOK_CTX_CACHE_SET:-0}" = "1" ]; then
    echo "$HOOK_CTX_CACHE"
    return
  fi

  if hook_is_merge_in_progress; then
    HOOK_CTX_CACHE="merge"
  elif hook_is_rebase_in_progress; then
    HOOK_CTX_CACHE="rebase"
  else
    HOOK_CTX_CACHE="none"
  fi

  HOOK_CTX_CACHE_SET=1
  echo "$HOOK_CTX_CACHE"
}

hook_has_user_tty() {
  if [ -t 0 ] || [ -t 1 ]; then
    return 0
  fi
  if [ -r /dev/tty ] && [ -w /dev/tty ]; then
    return 0
  fi
  return 1
}

hook_noninteractive_flag() {
  case "${UE_SYNC_FORCE_INTERACTIVE:-0}" in
    1|true|TRUE|yes|YES) return 0 ;;
  esac

  case "${UE_SYNC_FORCE_NONINTERACTIVE:-0}" in
    1|true|TRUE|yes|YES) echo "-NonInteractive"; return 0 ;;
  esac

  if [ -n "${CI:-}" ] || [ -n "${GITHUB_ACTIONS:-}" ] || [ -n "${TF_BUILD:-}" ] || [ -n "${JENKINS_URL:-}" ]; then
    echo "-NonInteractive"
    return 0
  fi

  # Interactive only when we can talk to a real user terminal.
  if hook_has_user_tty; then
    return 0
  fi

  echo "-NonInteractive"
}

hook_audit() {
  msg="$1"
  ts="$(date '+%Y-%m-%d %H:%M:%S' 2>/dev/null || echo 'time-unknown')"
  user="$(whoami 2>/dev/null || echo 'user-unknown')"
  ctx="$(hook_context)"
  f="$(hook_audit_file || true)"
  [ -n "$f" ] || return 0
  printf "[%s] [%s] [%s] [HOOK] %s\n" "$ts" "$ctx" "$user" "$msg" >>"$f" 2>/dev/null || true
}

# -----------------------------
# Unmerged / guarded detection
# -----------------------------

hook_list_unmerged() {
  git ls-files -u 2>/dev/null | awk -F'\t' 'NF>1{print $2}' | sort -u
}

hook_guard_cache_file() {
  if [ -n "${HOOK_GUARD_CACHE_FILE:-}" ]; then
    printf "%s" "$HOOK_GUARD_CACHE_FILE"
    return 0
  fi

  f="$(mktemp 2>/dev/null || true)"
  if [ -z "${f:-}" ]; then
    f="$(hook_git_path ".ue_guard_cache_$$" 2>/dev/null || true)"
  fi

  if [ -n "${f:-}" ]; then
    : >"$f" 2>/dev/null || true
    HOOK_GUARD_CACHE_FILE="$f"
    export HOOK_GUARD_CACHE_FILE
    printf "%s" "$HOOK_GUARD_CACHE_FILE"
    return 0
  fi

  return 1
}

hook_is_guarded_lfs_binary() {
  f="$1"
  nf="$(hook_norm_path "$f")"

  cachef="$(hook_guard_cache_file 2>/dev/null || true)"
  if [ -n "${cachef:-}" ] && [ -f "$cachef" ]; then
    cached="$(awk -F'\t' -v p="$nf" '$1==p {print $2; exit}' "$cachef" 2>/dev/null || true)"
    case "$cached" in
      1) return 0 ;;
      0) return 1 ;;
    esac
  fi

  attrs="$(git check-attr --cached filter diff merge text -- "$f" 2>/dev/null || true)"
  if [ -z "$attrs" ]; then
    if [ -n "${cachef:-}" ]; then printf "%s\t0\n" "$nf" >>"$cachef" 2>/dev/null || true; fi
    return 1
  fi

  # NOTE: We intentionally do NOT require filter/diff=lfs.
  # Guarded = merge=binary AND -text (text: unset).
  merge="$(printf '%s\n' "$attrs" | awk -F': ' '$2=="merge"{print $3; exit}')"
  text="$(printf '%s\n' "$attrs"  | awk -F': ' '$2=="text"{print $3; exit}')"

  if [ "$merge" = "binary" ] && [ "$text" = "unset" ]; then
    if [ -n "${cachef:-}" ]; then printf "%s\t1\n" "$nf" >>"$cachef" 2>/dev/null || true; fi
    return 0
  fi

  if [ -n "${cachef:-}" ]; then printf "%s\t0\n" "$nf" >>"$cachef" 2>/dev/null || true; fi
  return 1
}

# Batch filter: read paths from stdin, emit only guarded binary paths.
# Uses a single `git check-attr` call for better performance on large path sets.
hook_list_guarded_from_stdin() {
  in_tmp="$(mktemp 2>/dev/null || true)"
  [ -n "${in_tmp:-}" ] || return 0

  tr -d '\000' | tr -d '\r' | sed '/^[[:space:]]*$/d' | while IFS= read -r p; do
    [ -n "${p:-}" ] || continue
    hook_norm_path "$p"; echo
  done | sort -u >"$in_tmp" 2>/dev/null || : >"$in_tmp"

  if [ ! -s "$in_tmp" ]; then
    rm -f "$in_tmp" 2>/dev/null || true
    return 0
  fi

  git check-attr --cached merge text --stdin <"$in_tmp" 2>/dev/null \
    | tr -d '\r' \
    | awk -F': ' '
        NF >= 3 {
          p=$1; a=$2; v=$3;
          if (p == "") next;
          seen[p]=1;
          if (a=="merge") m[p]=v;
          else if (a=="text") t[p]=v;
        }
        END {
          for (p in seen) {
            if (m[p] == "binary" && t[p] == "unset") print p;
          }
        }
      ' \
    | sort -u

  rm -f "$in_tmp" 2>/dev/null || true
}

hook_is_lfs_tracked() {
  f="$1"
  # Reuse the same check-attr call pattern as hook_is_guarded_lfs_binary
  attrs="$(git check-attr --cached filter -- "$f" 2>/dev/null || true)"
  [ -n "$attrs" ] || return 1
  
  filter="$(printf '%s\n' "$attrs" | awk -F': ' '$2=="filter"{print $3; exit}')"
  [ "$filter" = "lfs" ]
}

hook_list_lfs_tracked_from_stdin() {
  in_tmp="$(mktemp 2>/dev/null || true)"
  [ -n "${in_tmp:-}" ] || return 0

  tr -d '\000' | tr -d '\r' | sed '/^[[:space:]]*$/d' | while IFS= read -r p; do
    [ -n "${p:-}" ] || continue
    hook_norm_path "$p"; echo
  done | sort -u >"$in_tmp" 2>/dev/null || : >"$in_tmp"

  if [ ! -s "$in_tmp" ]; then
    rm -f "$in_tmp" 2>/dev/null || true
    return 0
  fi

  git check-attr --cached filter --stdin <"$in_tmp" 2>/dev/null \
    | tr -d '\r' \
    | awk -F': ' 'NF >= 3 && $2 == "filter" && $3 == "lfs" { print $1 }' \
    | sort -u

  rm -f "$in_tmp" 2>/dev/null || true
}

hook_staged_content_is_lfs_pointer() {
  f="$1"
  first="$(git show ":$f" 2>/dev/null | sed -n '1p' || true)"
  [ "$first" = "version https://git-lfs.github.com/spec/v1" ]
}

hook_staged_is_lfs_pointer() {
  f="$1"
  # If it's LFS-tracked, pointer-in-index is normal and should NOT block.
  if hook_is_lfs_tracked "$f"; then
    return 1
  fi

  hook_staged_content_is_lfs_pointer "$f"
}

hook_emit_unreal_sidecars_if_present() {
  p="$1"
  case "$p" in
    *.uasset|*.umap)
      base="${p%.*}"
      for ext in uexp ubulk uptnl; do
        sc="$base.$ext"
        # Include sidecar if present either in index or working tree
        if git cat-file -e ":$sc" >/dev/null 2>&1 || [ -f "$(hook_repo_root)/$sc" ]; then
          printf "%s\n" "$sc"
        fi
      done
      ;;
  esac
}

# -----------------------------
# Context-bound ledger
# -----------------------------

hook_merge_head_sha() {
  gd="$(hook_git_state_dir)"
  [ -f "$gd/MERGE_HEAD" ] || return 1
  head="$(cat "$gd/MERGE_HEAD" 2>/dev/null | tr -d '\r\n')"
  [ -n "$head" ] || return 1
  printf "%s" "$head"
}

hook_rebase_patch_sha() {
  p1="$(hook_git_path "rebase-merge/patch" 2>/dev/null || true)"
  p2="$(hook_git_path "rebase-apply/patch" 2>/dev/null || true)"

  for p in "$p1" "$p2"; do
    [ -n "${p:-}" ] || continue
    [ -f "$p" ] || continue
    line="$(LC_ALL=C head -n 1 "$p" 2>/dev/null | tr -d '\r\n')"
    case "$line" in
      From\ *)
        sha="$(printf '%s\n' "$line" | awk '{print $2}')"
        [ -n "${sha:-}" ] && { printf "%s" "$sha"; return 0; }
        ;;
    esac
  done

  return 1
}

hook_rebase_seq_current_sha() {
  # rebase-merge backend: try "done" last, then next todo item.
  done_p="$(hook_git_path "rebase-merge/done" 2>/dev/null || true)"
  todo_p="$(hook_git_path "rebase-merge/git-rebase-todo" 2>/dev/null || true)"

  if [ -n "${done_p:-}" ] && [ -f "$done_p" ]; then
    sha="$(awk 'NF && $1 !~ /^#/ && $2 ~ /^[0-9a-fA-F]{7,40}$/ {print $2}' "$done_p" | tail -n 1)"
    [ -n "${sha:-}" ] && { printf "%s" "$sha"; return 0; }
  fi

  if [ -n "${todo_p:-}" ] && [ -f "$todo_p" ]; then
    sha="$(awk 'NF && $1 !~ /^#/ && $2 ~ /^[0-9a-fA-F]{7,40}$/ {print $2; exit}' "$todo_p")"
    [ -n "${sha:-}" ] && { printf "%s" "$sha"; return 0; }
  fi

  return 1
}

hook_rebase_head_sha() {
  # If rebase is stopped, stopped-sha is the most accurate.
  stopped_p="$(hook_git_path "rebase-merge/stopped-sha" 2>/dev/null || true)"
  if [ -n "${stopped_p:-}" ] && [ -f "$stopped_p" ]; then
    cat "$stopped_p" 2>/dev/null | tr -d '\r\n'
    return 0
  fi

  # Only trust REBASE_HEAD if a rebase is actually in progress.
  if hook_is_rebase_in_progress; then
    if git rev-parse -q --verify REBASE_HEAD >/dev/null 2>&1; then
      git rev-parse -q --verify REBASE_HEAD 2>/dev/null | tr -d '\r\n'
      return 0
    fi
  fi
  cp_p="$(hook_git_path "CHERRY_PICK_HEAD" 2>/dev/null || true)"
  if [ -n "${cp_p:-}" ] && [ -f "$cp_p" ]; then
    cat "$cp_p" 2>/dev/null | tr -d '\r\n'
    return 0
  fi
  sha="$(hook_rebase_patch_sha 2>/dev/null || true)"
  if [ -n "${sha:-}" ]; then
    printf "%s" "$sha"
    return 0
  fi
  sha="$(hook_rebase_seq_current_sha 2>/dev/null || true)"
  if [ -n "${sha:-}" ]; then
    printf "%s" "$sha"
    return 0
  fi
  orig_m="$(hook_git_path "rebase-merge/orig-head" 2>/dev/null || true)"
  if [ -n "${orig_m:-}" ] && [ -f "$orig_m" ]; then
    cat "$orig_m" 2>/dev/null | tr -d '\r\n'
    return 0
  fi
  orig_a="$(hook_git_path "rebase-apply/orig-head" 2>/dev/null || true)"
  if [ -n "${orig_a:-}" ] && [ -f "$orig_a" ]; then
    cat "$orig_a" 2>/dev/null | tr -d '\r\n'
    return 0
  fi
  return 1
}

hook_rebase_onto_sha() {
  # During rebase, HEAD moves as commits are applied.
  # Using HEAD for overlap checks causes false positives because it already contains
  # the in-progress commit's changes. The stable "onto" SHA is stored by git.
  local onto f
  f="$(hook_git_path "rebase-merge/onto" 2>/dev/null || true)"
  if [ -n "$f" ] && [ -f "$f" ]; then
    onto="$(cat "$f" 2>/dev/null | tr -d '\r\n')"
    if [ -n "$onto" ]; then
      printf "%s\n" "$onto"
      return 0
    fi
  fi
  
  f="$(hook_git_path "rebase-apply/onto" 2>/dev/null || true)"
  if [ -n "$f" ] && [ -f "$f" ]; then
    onto="$(cat "$f" 2>/dev/null | tr -d '\r\n')"
    if [ -n "$onto" ]; then
      printf "%s\n" "$onto"
      return 0
    fi
  fi

  # Soft-fail: return empty string instead of error
  # This allows hooks to continue with fallback logic
  printf ""
  return 1
}

# Portable-ish epoch mtime (GNU stat vs BSD stat)
hook_mtime_epoch() {
  p="$1"
  [ -e "$p" ] || return 1

  if stat -c %Y "$p" >/dev/null 2>&1; then
    stat -c %Y "$p" 2>/dev/null
    return 0
  fi
  if stat -f %m "$p" >/dev/null 2>&1; then
    stat -f %m "$p" 2>/dev/null
    return 0
  fi

  return 1
}

hook_operation_stamp() {
  gd="$(hook_git_state_dir)"
  ctx="$(hook_context)"

  if [ "$ctx" = "merge" ]; then
    hook_mtime_epoch "$gd/MERGE_HEAD" 2>/dev/null || true
    return 0
  fi

  if [ "$ctx" = "rebase" ]; then
    # IMPORTANT: directory mtimes change during --continue.
    # Use stable marker files instead so the stamp stays constant for the whole rebase.
    onto_p="$(hook_git_path "rebase-merge/onto" 2>/dev/null || true)"
    if [ -n "${onto_p:-}" ] && [ -f "$onto_p" ]; then
      hook_mtime_epoch "$onto_p" 2>/dev/null || true
      return 0
    fi
    head_p="$(hook_git_path "rebase-merge/head-name" 2>/dev/null || true)"
    if [ -n "${head_p:-}" ] && [ -f "$head_p" ]; then
      hook_mtime_epoch "$head_p" 2>/dev/null || true
      return 0
    fi
    orig_p="$(hook_git_path "rebase-apply/orig-head" 2>/dev/null || true)"
    if [ -n "${orig_p:-}" ] && [ -f "$orig_p" ]; then
      hook_mtime_epoch "$orig_p" 2>/dev/null || true
      return 0
    fi

    # last resort
    echo "nostamp"
    return 0
  fi

  return 1
}


hook_other_side_sha() {
  ctx="$(hook_context)"
  if [ "$ctx" = "merge" ]; then hook_merge_head_sha; return $?; fi
  if [ "$ctx" = "rebase" ]; then hook_rebase_head_sha; return $?; fi
  return 1
}

hook_operation_context_id() {
  ctx="$(hook_context)"
  [ "$ctx" != "none" ] || return 1

  stamp="$(hook_operation_stamp 2>/dev/null || true)"
  [ -n "${stamp:-}" ] || stamp="nostamp"

  if [ "$ctx" = "merge" ]; then
    # Merge: use MERGE_HEAD as the "other" side
    other="$(hook_merge_head_sha 2>/dev/null || true)"
    if [ -z "${other:-}" ]; then
      printf "%s:%s:%s:%s" "$ctx" "unknown" "unknown" "$stamp"
      return 0
    fi

    base="$(git merge-base HEAD "$other" 2>/dev/null || true)"
    if [ -z "${base:-}" ]; then
      printf "%s:%s:%s:%s" "$ctx" "$other" "nobase" "$stamp"
      return 0
    fi

    printf "%s:%s:%s:%s" "$ctx" "$other" "$base" "$stamp"
    return 0
  fi

  if [ "$ctx" = "rebase" ]; then
    # Rebase: scope context to the CURRENT stopped commit so approvals do not
    # carry from stop N to stop N+1 in a multi-commit rebase.
    onto="$(hook_rebase_onto_sha 2>/dev/null || true)"
    current="$(hook_rebase_head_sha 2>/dev/null || true)"

    # Fallback for unusual states where rebase head is temporarily unavailable.
    if [ -z "${current:-}" ]; then
      orig="$(hook_git_path "rebase-merge/orig-head" 2>/dev/null || true)"
      if [ -z "${orig:-}" ]; then
        orig="$(hook_git_path "rebase-apply/orig-head" 2>/dev/null || true)"
      fi
      if [ -n "${orig:-}" ] && [ -f "$orig" ]; then
        current="$(cat "$orig" 2>/dev/null | tr -d '\r\n')"
      fi
    fi

    [ -n "${onto:-}" ] || onto="unknown"
    [ -n "${current:-}" ] || current="unknown"

    printf "%s:%s:%s:%s" "$ctx" "$onto" "$current" "$stamp"
    return 0
  fi

  # Fallback for other contexts
  other="$(hook_other_side_sha 2>/dev/null || true)"
  if [ -z "${other:-}" ]; then
    printf "%s:%s:%s:%s" "$ctx" "unknown" "unknown" "$stamp"
    return 0
  fi

  base="$(git merge-base HEAD "$other" 2>/dev/null || true)"
  if [ -z "${base:-}" ]; then
    printf "%s:%s:%s:%s" "$ctx" "$other" "nobase" "$stamp"
    return 0
  fi

  printf "%s:%s:%s:%s" "$ctx" "$other" "$base" "$stamp"
}

hook_ctx_marker_path() {
  if hook_is_merge_in_progress; then
    echo "$(hook_git_path "MERGE_HEAD" 2>/dev/null || true)"; return 0
  fi
  # Rebase markers (best effort)
  p="$(hook_git_path "rebase-merge/stopped-sha" 2>/dev/null || true)"
  if [ -n "${p:-}" ] && [ -f "$p" ]; then echo "$p"; return 0; fi
  p="$(hook_git_path "rebase-apply/orig-head" 2>/dev/null || true)"
  if [ -n "${p:-}" ] && [ -f "$p" ]; then echo "$p"; return 0; fi
  # If REBASE_HEAD exists as ref, we can't stat it easily; fall back to 0
  echo ""; return 1
}

hook_reset_ledgers_for_new_context_if_needed() {
  ctx="$(hook_context)"
  CF="$(hook_context_file || true)"
  RF="$(hook_resolved_file || true)"

  # If no context, clear ledgers
  if [ "$ctx" = "none" ]; then
    [ -n "$CF" ] && rm -f "$CF" 2>/dev/null || true
    [ -n "$RF" ] && rm -f "$RF" 2>/dev/null || true
    return 0
  fi

  [ -n "$CF" ] || return 0

  cur="$(hook_operation_context_id 2>/dev/null || true)"
  [ -n "${cur:-}" ] || return 0

  prev=""
  if [ -f "$CF" ]; then
    prev="$(cat "$CF" 2>/dev/null | tr -d '\r\n')"
  fi

  if [ "$prev" != "$cur" ]; then
    if ! printf "%s\n" "$cur" >"$CF" 2>/dev/null; then
      echo "[ERROR] Failed to write context file: $CF" 1>&2
      echo "[ERROR] This means the binary-guard cannot safely prevent stale approvals." 1>&2
      echo "[ERROR] Stop and contact Ronnie." 1>&2
      return 2
    fi

    [ -n "$RF" ] && rm -f "$RF" 2>/dev/null || true
    hook_audit "context changed/new op -> reset resolved ($cur)"
  fi

  return 0
}

# -----------------------------
# Overlap detection
# -----------------------------

hook_list_overlap_paths_between() {
  left="$1"
  right="$2"

  base="$(git merge-base "$left" "$right" 2>/dev/null || true)"
  [ -n "$base" ] || return 0

  t1="$(mktemp 2>/dev/null || true)"
  t2="$(mktemp 2>/dev/null || true)"
  if [ -z "${t1:-}" ] || [ -z "${t2:-}" ]; then
    return 0
  fi

  git diff --name-only "$base" "$left"  2>/dev/null | sort -u >"$t1" || true
  git diff --name-only "$base" "$right" 2>/dev/null | sort -u >"$t2" || true

  comm -12 "$t1" "$t2" 2>/dev/null || true

  rm -f "$t1" "$t2" 2>/dev/null || true
}

hook_list_guarded_overlap_candidates() {
  ctx="$(hook_context)"

  if [ "$ctx" = "merge" ]; then
    other="$(hook_merge_head_sha || true)"
    [ -n "$other" ] || return 0
    hook_list_overlap_paths_between HEAD "$other" | hook_list_guarded_from_stdin
    return 0
  fi

  if [ "$ctx" = "rebase" ]; then
    other="$(hook_rebase_head_sha || true)"
    onto="$(hook_rebase_onto_sha || true)"
    
    if [ -z "$other" ]; then
      hook_trace "rebase overlap: no other SHA"
      return 0
    fi
    
    parent="$(git rev-parse -q --verify "${other}^" 2>/dev/null || true)"

    # Prefer merge-tree for accurate 3-way overlap
    if [ -n "$onto" ] && [ -n "$parent" ]; then
      hook_trace "rebase overlap: using merge-tree (parent=$parent onto=$onto other=$other)"
      
      t1="$(mktemp 2>/dev/null || true)"
      t2="$(mktemp 2>/dev/null || true)"
      [ -n "$t1" ] && [ -n "$t2" ] || return 0
      
      # Paths changed in commit being applied
      git diff --name-only "$parent" "$other" 2>/dev/null | sort -u >"$t1" || true
      
      # Paths changed on target branch
      git diff --name-only "$parent" "$onto" 2>/dev/null | sort -u >"$t2" || true
      
      # Only paths changed on BOTH sides
      comm -12 "$t1" "$t2" 2>/dev/null | hook_list_guarded_from_stdin
      
      rm -f "$t1" "$t2" 2>/dev/null || true
      return 0
    fi
    
    hook_trace "rebase overlap: missing parent or onto, using fallback"
    
    # Fallback: diff intersection
    base="${parent:-$(git merge-base HEAD "$other" 2>/dev/null || true)}"
    [ -n "$base" ] || return 0

    target="${onto:-HEAD}"
    
    t1="$(mktemp 2>/dev/null || true)"
    t2="$(mktemp 2>/dev/null || true)"
    [ -n "$t1" ] && [ -n "$t2" ] || return 0

    git diff --name-only "$base" "$target" 2>/dev/null | sort -u >"$t1" || true
    git diff --name-only "$base" "$other" 2>/dev/null | sort -u >"$t2" || true

    comm -12 "$t1" "$t2" 2>/dev/null | hook_list_guarded_from_stdin

    rm -f "$t1" "$t2" 2>/dev/null || true
    return 0
  fi

  return 0
}

hook_print_resolution_instructions() {
  echo ""
  echo "Resolve guarded binary conflicts ONLY with:"
  echo "  git ours   <patterns...>"
  echo "  git theirs <patterns...>"
  echo ""
  echo "Examples:"
  echo "  git ours   \"**/*.uasset\" \"**/*.umap\" \"**/*.png\""
  echo "  git theirs \"Content/Blueprints/*.uasset\""
}

hook_has_conflict_markers() {
  f="$1"
  [ -f "$f" ] || return 1
  LC_ALL=C grep -a -m 1 -n '<<<<<<<\|=======\|>>>>>>>' "$f" >/dev/null 2>&1
}

hook_working_tree_is_lfs_pointer() {
  f="$1"
  [ -f "$f" ] || return 1

  # Read a small prefix only; strip NULs so Bash command-substitution never sees them.
  first_line="$(
    LC_ALL=C head -c 200 "$f" 2>/dev/null \
      | tr -d '\000' \
      | tr -d '\r' \
      | { IFS= read -r line || true; printf '%s' "$line"; }
  )"

  [ "$first_line" = "version https://git-lfs.github.com/spec/v1" ]
}


hook_run_unrealsync() {
  old="$1"; new="$2"; flag="$3"

  # Allow outer hooks to suppress nested UnrealSync executions while they run
  # internal git commands (e.g., post-checkout -> git pull -> post-merge).
  case "${UE_SYNC_SUPPRESS:-0}" in
    1|true|TRUE|yes|YES)
      hook_trace "skip UnrealSync: suppressed by UE_SYNC_SUPPRESS"
      return 0
      ;;
  esac

  # Avoid any Unreal sync noise/work while Git is actively resolving merge/rebase state.
  if hook_is_merge_in_progress || hook_is_rebase_in_progress; then
    hook_trace "skip UnrealSync: merge/rebase in progress"
    return 0
  fi

  # Nothing to diff => nothing to do.
  if [ -z "${old:-}" ] || [ -z "${new:-}" ] || [ "$old" = "$new" ]; then
    hook_trace "skip UnrealSync: empty or identical revisions (old=$old new=$new)"
    return 0
  fi

  REPO_ROOT="$(hook_repo_root)"
  UE_PS_SCRIPT="$REPO_ROOT/Scripts/Unreal/UnrealSync.ps1"
  [ -f "$UE_PS_SCRIPT" ] || return 0

  NONINTERACTIVE="$(hook_noninteractive_flag)"

  PS_EXE="powershell"
  if command -v pwsh >/dev/null 2>&1; then
    PS_EXE="pwsh"
  fi

  if [ -z "${NONINTERACTIVE:-}" ] && hook_has_user_tty; then
    "$PS_EXE" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "$UE_PS_SCRIPT" -OldRev "$old" -NewRev "$new" -Flag "$flag"
  else
    "$PS_EXE" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "$UE_PS_SCRIPT" -OldRev "$old" -NewRev "$new" -Flag "$flag" $NONINTERACTIVE
  fi
}

hook_norm_path() {
  printf "%s" "$1" | tr '\\' '/' | tr -d '\r'
}

hook_list_required_guarded_unmerged_and_overlap() {
  # union(guarded unmerged, guarded overlap), unique, normalized
  tmp="$(mktemp 2>/dev/null || true)"
  [ -n "${tmp:-}" ] || return 0
  : >"$tmp" 2>/dev/null || true

  hook_list_unmerged | hook_list_guarded_from_stdin | while IFS= read -r p; do
    [ -n "${p:-}" ] || continue
    hook_norm_path "$p"; echo
  done >>"$tmp" 2>/dev/null || true

  hook_list_guarded_overlap_candidates 2>/dev/null | while IFS= read -r p; do
    [ -n "${p:-}" ] || continue
    hook_norm_path "$p"; echo
  done >>"$tmp" 2>/dev/null || true

  sort -u "$tmp" 2>/dev/null || true
  rm -f "$tmp" 2>/dev/null || true
}

hook_print_required_if_any() {
  # Only warn if there are *remaining* required approvals (required - approved)
  remaining="$(hook_list_remaining_required_guarded 2>/dev/null | tr -d '\000' || true)"

  if [ -n "${remaining:-}" ]; then
    warn "Guarded binary conflicts detected (required approvals):"
    printf '%s\n' "$remaining" | sed 's/^/  - /'
    hook_print_resolution_instructions
  fi
}

hook_list_paths_changed_in_commit() {
  c="$1"
  [ -n "$c" ] || return 0

  # Use diff-tree (handles merges and is cheap). Show only paths.
  git diff-tree --no-commit-id --name-only -r "$c" 2>/dev/null | sort -u
}

hook_list_rebase_patch_paths() {
  p1="$(hook_git_path "rebase-merge/patch" 2>/dev/null || true)"
  p2="$(hook_git_path "rebase-apply/patch" 2>/dev/null || true)"

  for p in "$p1" "$p2"; do
    [ -n "${p:-}" ] || continue
    [ -f "$p" ] || continue
    # Empty patch files are common during rebase stops; treat as unavailable.
    if [ ! -s "$p" ]; then
      continue
    fi
    tmp="$(mktemp 2>/dev/null || true)"
    [ -n "${tmp:-}" ] || continue
    LC_ALL=C awk '
      $1 == "+++"
        { f=$2; sub("^b/", "", f); if (f != "/dev/null") print f; next }
      $1 == "---"
        { f=$2; sub("^a/", "", f); if (f != "/dev/null") print f; next }
    ' "$p" 2>/dev/null | sort -u >"$tmp" || true
    if [ -s "$tmp" ]; then
      cat "$tmp"
      rm -f "$tmp" 2>/dev/null || true
      return 0
    fi
    rm -f "$tmp" 2>/dev/null || true
  done

  return 1
}

hook_list_rebase_overlap_for_current_commit() {
  other="$(hook_rebase_head_sha 2>/dev/null || true)"
  onto="$(hook_rebase_onto_sha 2>/dev/null || true)"
  
  if [ -z "$other" ]; then
    hook_trace "rebase overlap: no other SHA (rebase_head_sha failed)"
    return 0
  fi

  parent="$(git rev-parse -q --verify "${other}^" 2>/dev/null || true)"

  if [ -n "$parent" ] && [ -n "$onto" ]; then
    hook_trace "rebase overlap: trying merge-tree with parent=$parent onto=$onto other=$other"
    if mt_out="$(git merge-tree --name-only "$parent" "$onto" "$other" 2>/dev/null)"; then
      hook_trace "rebase overlap: merge-tree succeeded"
      
      # CRITICAL FIX: Filter to only paths modified on BOTH sides
      t1="$(mktemp 2>/dev/null || true)"
      t2="$(mktemp 2>/dev/null || true)"
      [ -n "$t1" ] && [ -n "$t2" ] || return 0
      
      # Paths changed in commit being applied
      git diff --name-only "$parent" "$other" 2>/dev/null | sort -u >"$t1" || true
      
      # Paths changed on target branch
      git diff --name-only "$parent" "$onto" 2>/dev/null | sort -u >"$t2" || true
      
      # Only paths changed on BOTH sides
      overlap="$(comm -12 "$t1" "$t2" 2>/dev/null || true)"
      overlap_count=$(printf '%s\n' "$overlap" | wc -l)
      
      rm -f "$t1" "$t2" 2>/dev/null || true
      
      hook_trace "rebase overlap: filtered to $overlap_count actual overlaps"
      printf '%s\n' "$overlap" | sed '/^$/d' | sort -u
      return 0
    else
      hook_trace "rebase overlap: merge-tree failed, falling back to diff"
    fi
  fi

  # Fallback: diff intersection
  if [ -n "$parent" ]; then
    base="$parent"
  elif [ -n "$onto" ]; then
    base="$(git merge-base "$onto" "$other" 2>/dev/null || true)"
  else
    base="$(git merge-base HEAD "$other" 2>/dev/null || true)"
  fi
  
  if [ -z "$base" ]; then
    hook_trace "rebase overlap: no base found, cannot compute overlap"
    return 0
  fi

  t1="$(mktemp 2>/dev/null || true)"
  t2="$(mktemp 2>/dev/null || true)"
  [ -n "$t1" ] && [ -n "$t2" ] || return 0

  # A = paths changed on target side (onto/HEAD) since base
  target="${onto:-HEAD}"
  git diff --name-only "$base" "$target" 2>/dev/null | tr -d '\r' | sort -u >"$t1" || true

  # B = paths touched by the applying commit (prefer patch file if present)
  if ! hook_list_rebase_patch_paths >"$t2" 2>/dev/null; then
    if [ -n "$parent" ]; then
      git diff --name-only "$parent" "$other" 2>/dev/null | tr -d '\r' | sort -u >"$t2" || true
    else
      hook_list_paths_changed_in_commit "$other" | tr -d '\r' >"$t2" || true
    fi
  fi

  overlap_count=$(comm -12 "$t1" "$t2" 2>/dev/null | wc -l)
  hook_trace "rebase overlap: fallback diff found $overlap_count overlapping paths"
  
  comm -12 "$t1" "$t2" 2>/dev/null || true

  rm -f "$t1" "$t2" 2>/dev/null || true
}

# -----------------------------
# Hook tracing (opt-in)
# -----------------------------
hook_trace_enabled() { [ "${UE_HOOK_TRACE:-0}" = "1" ]; }

hook_trace_file() {
  hook_git_path "ue_binary_conflicts.trace"
}

hook_trace() {
  hook_trace_enabled || return 0
  msg="$1"
  ts="$(date '+%Y-%m-%d %H:%M:%S' 2>/dev/null || echo 'time-unknown')"
  f="$(hook_trace_file 2>/dev/null || true)"
  [ -n "$f" ] || return 0
  printf "[%s] [%s] [pid=%s] %s\n" "$ts" "${HOOK_NAME:-hook}" "$$" "$msg" >>"$f" 2>/dev/null || true
}

hook_trace_state() {
  hook_trace_enabled || return 0

  hook_trace "git_dir=$(hook_git_dir 2>/dev/null || true) common_dir=$(hook_git_common_dir 2>/dev/null || true) GIT_DIR=${GIT_DIR:-}"

  # basic context
  if hook_is_rebase_in_progress; then rb=1; else rb=0; fi
  if hook_is_rebase_stopped; then rs=1; else rs=0; fi
  if hook_has_unmerged; then um=1; else um=0; fi

  hook_trace "ctx=$(hook_context) rebase_in_progress=$rb rebase_stopped=$rs unmerged=$um reflog=${GIT_REFLOG_ACTION:-}"

  # rebase/merge markers - add existence checks
  gd="$(hook_git_state_dir)"
  
  # Trace rebase-merge markers
  rbm_dir="$(hook_git_path "rebase-merge" 2>/dev/null || true)"
  if [ -n "$rbm_dir" ]; then
    if [ -d "$rbm_dir" ]; then
      hook_trace "rebase-merge dir EXISTS at: $rbm_dir"
    else
      hook_trace "rebase-merge dir MISSING at: $rbm_dir"
    fi
  else
    hook_trace "rebase-merge path resolution FAILED"
  fi

  for f in \
    rebase-merge/patch \
    rebase-merge/stopped-sha \
    rebase-merge/done \
    rebase-merge/git-rebase-todo \
    rebase-merge/onto \
    rebase-apply/applying \
    rebase-apply/patch \
    MERGE_HEAD \
    CHERRY_PICK_HEAD \
    REBASE_HEAD
  do
    resolved="$(hook_git_path "$f" 2>/dev/null || true)"
    if [ -n "$resolved" ] && [ -e "$resolved" ]; then
      hook_trace "marker $f present at: $resolved"
    fi
  done
}
