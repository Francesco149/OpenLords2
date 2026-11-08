/* rh_files.c — hermetic file overlay for retail Lords2 (copy-on-write into the run dir).
 *
 * The game dir stays pristine and is never written. Every retail file API that names a path
 * inside the game dir (or a relative one — cwd is the game dir) resolves through:
 *   read  : <rundir>\rel if it exists, else <gamedir>\rel (unless deleted in this run)
 *   write : <rundir>\rel (copied from the game dir first when the open keeps content)
 * Saves (*.sav: lastturn / safeturn / old_turn / the user's) come from the run dir only: the
 * staged game dir holds none, and a scenario's start save is copied into the run dir.
 * FindFirstFile merges both dirs (sorted, fixed timestamps / attributes), so the save list a
 * retail menu shows is a function of the run alone. (OpenSummoners / openrecet origin.) */
#include "rhost.h"
#include <mmsystem.h>

static char G[MAX_PATH], R[MAX_PATH];
static size_t GL;
static char g_white[256][96]; static int g_nwhite;          /* game-relative paths deleted in this run */
static long g_nopen;

static int is_white(const char *rel) { for (int i = 0; i < g_nwhite; i++) if (!_stricmp(g_white[i], rel)) return 1; return 0; }
static void unwhite(const char *rel) { for (int i = 0; i < g_nwhite; i++) if (!_stricmp(g_white[i], rel)) { strcpy(g_white[i], g_white[--g_nwhite]); return; } }
static void white(const char *rel) { if (!is_white(rel) && g_nwhite < 256) snprintf(g_white[g_nwhite++], 96, "%s", rel); }

/* path → game-relative part. Returns 1 if inside the game dir. */
static int game_rel(const char *path, char *rel, size_t n)
{
    char full[MAX_PATH];
    if (!path || !*path) return 0;
    if (!GetFullPathNameA(path, sizeof full, full, NULL)) return 0;
    if (_strnicmp(full, G, GL) || (full[GL] != '\\' && full[GL] != 0)) return 0;
    const char *r = full + GL; while (*r == '\\') r++;
    snprintf(rel, n, "%s", r);
    return *rel != 0;
}
static int exists(const char *p) { DWORD a = GetFileAttributesA(p); return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY); }
static void mkparents(const char *p)
{
    char t[MAX_PATH]; snprintf(t, sizeof t, "%s", p);
    for (char *s = t + strlen(R) + 1; *s; s++) if (*s == '\\') { *s = 0; CreateDirectoryA(t, NULL); *s = '\\'; }
}
static const char *resolve(const char *path, int want_write, int keep, char *out, size_t n)
{
    char rel[MAX_PATH];
    if (!G[0] || !game_rel(path, rel, sizeof rel)) return path;
    char rp[MAX_PATH], gp[MAX_PATH];
    snprintf(rp, sizeof rp, "%s\\%s", R, rel);
    snprintf(gp, sizeof gp, "%s\\%s", G, rel);
    if (want_write) {
        mkparents(rp);
        if (keep && !exists(rp) && !is_white(rel) && exists(gp)) CopyFileA(gp, rp, TRUE);
        unwhite(rel);
        snprintf(out, n, "%s", rp);
    } else if (exists(rp)) snprintf(out, n, "%s", rp);
    else if (is_white(rel)) snprintf(out, n, "%s", rp);
    else snprintf(out, n, "%s", gp);
    return out;
}
const char *rh_files_resolve(const char *path, int want_write, char *out, size_t n) { return resolve(path, want_write, 0, out, n); }


/* ---- the shadow's file rollback (rh_state.c meta_save / meta_restore) ----
 * A shadowed call runs retail, then the port on the same pre-state, then keeps retail's post-state: memory and the
 * host layers are rolled back, and so must the run dir's files be, or a call that saves writes twice (the save
 * rotation: 0x49a3e6's year-end save; a skirmish's score.dat). While a journal is open, the first write / delete /
 * move that touches a run-dir path records its content (or absence) first; a meta snapshot holds the touched paths'
 * content at that moment and the whiteout list; restoring puts every touched path back to the snapshot's content
 * (a path the snapshot does not know: its first-touch content). Files are small (a save is 470 KB). */
typedef struct { char path[MAX_PATH]; int existed; uint8_t *data; DWORD n; } fsnap_t;
static fsnap_t g_jr[64]; static int g_njr, g_jopen;
static int fread_all(const char *p, uint8_t **d, DWORD *n)
{
    HANDLE h = CreateFileA(p, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) { *d = NULL; *n = 0; return 0; }
    DWORD sz = GetFileSize(h, NULL), got = 0;
    *d = malloc(sz ? sz : 1); ReadFile(h, *d, sz, &got, NULL); *n = got;
    CloseHandle(h);
    return 1;
}
static void fwrite_all(const char *p, int existed, const uint8_t *d, DWORD n)
{
    if (!existed) { DeleteFileA(p); return; }
    HANDLE h = CreateFileA(p, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, CREATE_ALWAYS, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD w; WriteFile(h, d, n, &w, NULL); CloseHandle(h);
}
static void journal_touch(const char *rp)   /* rp: a run-dir path about to be written / deleted / replaced */
{
    if (!g_jopen || _strnicmp(rp, R, strlen(R))) return;
    for (int i = 0; i < g_njr; i++) if (!_stricmp(g_jr[i].path, rp)) return;
    if (g_njr >= 64) rh_fatal("rh_files: more than 64 files touched in one shadowed call");
    fsnap_t *f = &g_jr[g_njr++];
    snprintf(f->path, sizeof f->path, "%s", rp);
    f->existed = fread_all(rp, &f->data, &f->n);
}
typedef struct { int opener; int nwhite; char white[256][96]; int n; fsnap_t f[64]; } fmeta_t;
void *rh_files_meta_save(void)
{
    fmeta_t *m = calloc(1, sizeof *m);
    if (!g_jopen) { g_jopen = 1; g_njr = 0; m->opener = 1; }
    m->nwhite = g_nwhite; memcpy(m->white, g_white, sizeof g_white);
    for (int i = 0; i < g_njr; i++) {   /* the touched paths' content now */
        m->f[m->n] = g_jr[i];
        m->f[m->n].existed = fread_all(g_jr[i].path, &m->f[m->n].data, &m->f[m->n].n);
        m->n++;
    }
    return m;
}
void rh_files_meta_restore(const void *p)
{
    const fmeta_t *m = p;
    for (int i = 0; i < g_njr; i++) {
        const fsnap_t *s = &g_jr[i];
        for (int k = 0; k < m->n; k++) if (!_stricmp(m->f[k].path, s->path)) { s = &m->f[k]; break; }
        fwrite_all(s->path, s->existed, s->data, s->n);
    }
    g_nwhite = m->nwhite; memcpy(g_white, m->white, sizeof g_white);
}
void rh_files_meta_free(void *p)
{
    fmeta_t *m = p;
    if (!m) return;
    for (int i = 0; i < m->n; i++) free(m->f[i].data);
    if (m->opener) { for (int i = 0; i < g_njr; i++) free(g_jr[i].data); g_njr = 0; g_jopen = 0; }
    free(m);
}

static HANDLE WINAPI my_CreateFileA(LPCSTR p, DWORD acc, DWORD share, LPSECURITY_ATTRIBUTES sa, DWORD disp, DWORD fl, HANDLE tmpl)
{
    int w = (acc & (GENERIC_WRITE | FILE_WRITE_DATA | FILE_APPEND_DATA)) || disp == CREATE_ALWAYS ||
            disp == CREATE_NEW || disp == TRUNCATE_EXISTING;
    int keep = disp == OPEN_EXISTING || disp == OPEN_ALWAYS;
    char buf[MAX_PATH];
    const char *q = resolve(p, w, keep, buf, sizeof buf);
    if (w) journal_touch(q);
    HANDLE h = CreateFileA(q, acc, share | FILE_SHARE_READ, sa, disp, fl, tmpl);
    g_nopen++;
    if (O.log_files) rh_log("file %s %s -> %s = %s (f%u)\n", w ? "W" : "R", p, q, h == INVALID_HANDLE_VALUE ? "FAIL" : "ok", g_frame);
    return h;
}
static BOOL WINAPI my_DeleteFileA(LPCSTR p)
{
    char rel[MAX_PATH];
    if (!game_rel(p, rel, sizeof rel)) return DeleteFileA(p);
    char rp[MAX_PATH], gp[MAX_PATH];
    snprintf(rp, sizeof rp, "%s\\%s", R, rel); snprintf(gp, sizeof gp, "%s\\%s", G, rel);
    BOOL had = exists(rp) || (!is_white(rel) && exists(gp));
    journal_touch(rp);
    DeleteFileA(rp);
    white(rel);
    if (O.log_files) rh_log("delete %s = %d (f%u)\n", p, had, g_frame);
    if (!had) SetLastError(ERROR_FILE_NOT_FOUND);
    return had;
}
static BOOL WINAPI my_MoveFileA(LPCSTR src, LPCSTR dst)
{
    char rs[MAX_PATH], rd[MAX_PATH], b1[MAX_PATH], b2[MAX_PATH];
    int gs = game_rel(src, rs, sizeof rs), gd = game_rel(dst, rd, sizeof rd);
    const char *s = resolve(src, 0, 0, b1, sizeof b1);
    if (!exists(s)) { SetLastError(ERROR_FILE_NOT_FOUND); return FALSE; }
    const char *d = resolve(dst, 1, 0, b2, sizeof b2);
    if (exists(d)) { SetLastError(ERROR_ALREADY_EXISTS); return FALSE; }
    journal_touch(s); journal_touch(d);
    BOOL r = (gs && _strnicmp(s, R, strlen(R))) ? CopyFileA(s, d, TRUE) : MoveFileA(s, d);   /* a game-dir source is never moved */
    if (r && gs) white(rs);
    if (r && gd) unwhite(rd);
    if (O.log_files) rh_log("move %s -> %s = %d (f%u)\n", src, dst, r, g_frame);
    return r;
}

/* ---- FindFirstFile over the merged view ---- */
#define FIND_BASE 0x7D000000u
typedef struct { WIN32_FIND_DATAA *e; int n, i; } find_t;
static find_t g_find[64];
static int cmp_name(const void *a, const void *b) { return _stricmp(((const WIN32_FIND_DATAA *)a)->cFileName, ((const WIN32_FIND_DATAA *)b)->cFileName); }
static void norm(WIN32_FIND_DATAA *d)
{
    static const FILETIME ft = { 0x92810000u, 0x01dc7ab1u };   /* 2026-01-01, every file: no host clock leaks */
    d->ftCreationTime = d->ftLastAccessTime = d->ftLastWriteTime = ft;
    d->dwFileAttributes = (d->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_ARCHIVE;
    /* the bytes after the name's NUL are whatever the OS's buffer held (differs per run / host): retail's save
     * list copies the whole 260-byte field into its globals (0x4e87c0..), so they are zeroed */
    size_t n = strnlen(d->cFileName, sizeof d->cFileName);
    memset(d->cFileName + n, 0, sizeof d->cFileName - n);
    memset(d->cAlternateFileName, 0, sizeof d->cAlternateFileName);
    d->dwReserved0 = d->dwReserved1 = 0;
}
static void add_dir(find_t *f, const char *pattern, int cap_hint, int from_run, const char *reldir)
{
    WIN32_FIND_DATAA d;
    HANDLE h = FindFirstFileA(pattern, &d);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        char rel[MAX_PATH];
        snprintf(rel, sizeof rel, "%s%s%s", reldir, *reldir ? "\\" : "", d.cFileName);
        if (!from_run && is_white(rel)) continue;
        int dup = -1;
        for (int k = 0; k < f->n; k++) if (!_stricmp(f->e[k].cFileName, d.cFileName)) dup = k;
        norm(&d);
        if (dup >= 0) { if (from_run) f->e[dup] = d; continue; }
        f->e = realloc(f->e, (f->n + 1) * sizeof *f->e);
        f->e[f->n++] = d;
    } while (FindNextFileA(h, &d));
    FindClose(h);
    (void)cap_hint;
}
static HANDLE WINAPI my_FindFirstFileA(LPCSTR pat, LPWIN32_FIND_DATAA out)
{
    char rel[MAX_PATH], full[MAX_PATH];
    if (!GetFullPathNameA(pat, sizeof full, full, NULL) || !game_rel(full, rel, sizeof rel)) {
        HANDLE h = FindFirstFileA(pat, out);
        if (h != INVALID_HANDLE_VALUE) { norm(out); rh_log("find: outside the game dir: %s\n", pat); }
        return h;
    }
    int slot = -1;
    for (int i = 0; i < 64; i++) if (!g_find[i].e && !g_find[i].n) { slot = i; break; }
    if (slot < 0) rh_fatal("FindFirstFileA: more than 64 open finds");
    find_t *f = &g_find[slot];
    char reldir[MAX_PATH]; snprintf(reldir, sizeof reldir, "%s", rel);
    char *bs = strrchr(reldir, '\\'); const char *mask = rel;
    if (bs) { *bs = 0; mask = bs + 1; } else reldir[0] = 0;
    char p1[MAX_PATH], p2[MAX_PATH];
    snprintf(p1, sizeof p1, "%s%s%s\\%s", G, *reldir ? "\\" : "", reldir, mask);
    snprintf(p2, sizeof p2, "%s%s%s\\%s", R, *reldir ? "\\" : "", reldir, mask);
    add_dir(f, p1, 0, 0, reldir);
    add_dir(f, p2, 0, 1, reldir);
    if (O.log_files) rh_log("find %s -> %d entries (f%u)\n", pat, f->n, g_frame);
    if (!f->n) { free(f->e); f->e = NULL; SetLastError(ERROR_FILE_NOT_FOUND); return INVALID_HANDLE_VALUE; }
    qsort(f->e, f->n, sizeof *f->e, cmp_name);
    *out = f->e[0]; f->i = 1;
    return (HANDLE)(uintptr_t)(FIND_BASE + 4u * (uint32_t)slot);
}
static find_t *find_of(HANDLE h)
{
    uint32_t v = (uint32_t)(uintptr_t)h;
    if (v < FIND_BASE || v >= FIND_BASE + 4u * 64 || (v & 3)) return NULL;
    return &g_find[(v - FIND_BASE) >> 2];
}
static BOOL WINAPI my_FindNextFileA(HANDLE h, LPWIN32_FIND_DATAA out)
{
    find_t *f = find_of(h);
    if (!f) { BOOL r = FindNextFileA(h, out); if (r) norm(out); return r; }
    if (f->i >= f->n) { SetLastError(ERROR_NO_MORE_FILES); return FALSE; }
    *out = f->e[f->i++];
    return TRUE;
}
static BOOL WINAPI my_FindClose(HANDLE h)
{
    find_t *f = find_of(h);
    if (!f) return FindClose(h);
    free(f->e); f->e = NULL; f->n = f->i = 0;
    return TRUE;
}
static HMMIO WINAPI my_mmioOpenA(LPSTR p, LPMMIOINFO info, DWORD fl)
{
    char buf[MAX_PATH];
    int w = (fl & (MMIO_CREATE | MMIO_WRITE | MMIO_READWRITE)) != 0;
    const char *q = p ? resolve(p, w, !(fl & MMIO_CREATE), buf, sizeof buf) : p;
    HMMIO h = mmioOpenA((LPSTR)q, info, fl);
    if (O.log_files) rh_log("mmioOpen %s -> %s = %s (f%u)\n", p ? p : "(null)", q ? q : "", h ? "ok" : "FAIL", g_frame);
    return h;
}
static UINT WINAPI my_GetDriveTypeA(LPCSTR root) { (void)root; return DRIVE_FIXED; }
/* retail's asset-retry chain _chdir()s into subdirs that may not exist; the CRT stores the raw error in
 * _doserrno (DAT_004e4534): Windows says ERROR_PATH_NOT_FOUND, wine ERROR_FILE_NOT_FOUND — answer it here */
static BOOL WINAPI my_SetCurrentDirectoryA(LPCSTR p)
{
    DWORD a = p ? GetFileAttributesA(p) : INVALID_FILE_ATTRIBUTES;
    if (a == INVALID_FILE_ATTRIBUTES || !(a & FILE_ATTRIBUTE_DIRECTORY)) {
        if (O.log_files) rh_log("chdir %s = no such directory (f%u)\n", p ? p : "(null)", g_frame);
        SetLastError(ERROR_PATH_NOT_FOUND);
        return FALSE;
    }
    if (O.log_files) rh_log("chdir %s (f%u)\n", p, g_frame);
    return SetCurrentDirectoryA(p);
}

void rh_files_install(void)
{
    GetFullPathNameA(O.gamedir, sizeof G, G, NULL);
    GetFullPathNameA(O.rundir, sizeof R, R, NULL);
    for (size_t l = strlen(G); l && G[l - 1] == '\\'; ) G[--l] = 0;
    for (size_t l = strlen(R); l && R[l - 1] == '\\'; ) R[--l] = 0;
    GL = strlen(G);
    CreateDirectoryA(R, NULL);
    iat_override("CreateFileA", my_CreateFileA);
    iat_override("DeleteFileA", my_DeleteFileA);
    iat_override("MoveFileA", my_MoveFileA);
    iat_override("FindFirstFileA", my_FindFirstFileA);
    iat_override("FindNextFileA", my_FindNextFileA);
    iat_override("FindClose", my_FindClose);
    iat_override("mmioOpenA", my_mmioOpenA);
    iat_override("GetDriveTypeA", my_GetDriveTypeA);
    iat_override("SetCurrentDirectoryA", my_SetCurrentDirectoryA);
    rh_log("overlay: game=%s run=%s\n", G, R);
}
