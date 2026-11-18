/* retail_map.h — map Lords2.exe at its fixed ImageBase (0x400000) inside the host process
 * WITHOUT running its entry point. The host links at 0x200000 without ASLR and carries
 * RETAIL_HOLE_DEFINE, a .bss array the PE loader reserves for us (rhost.x puts .bss first);
 * retail_map() VirtualProtect()s it RWX and copies retail's sections in.
 * Imports go through a host policy hook (rh_import_policy): the host supplies its own
 * implementation (fake DirectDraw / DirectSound, stubbed DirectPlay / Smacker) or NULL to
 * let the real DLL resolve it. (OpenSummoners / openrecet tools/rhost origin.) */
#pragma once
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Lords2 SizeOfImage 0x1e9000: the hole covers 0x400000..0x5e9000 with margin */
#define RETAIL_HOLE_DEFINE \
    __attribute__((aligned(4096))) char g_retail_hole[0x480000];

/* every byte range a map took from the FILE (the standalone's audit: none may be code) */
/* port: from --data-image; synth: 1 = built from the port's tables (rl_image.c: headers, .idata), 2 = absent (left zero,
 * never read: .rsrc / .reloc under port_map) */
typedef struct { char name[9]; uint32_t va, len, ch; int copied, port, synth; } retail_range_t;
#define RETAIL_MAX_RANGES 16
typedef struct {
    uint8_t *base;
    uint32_t size;
    uint32_t entry;
    int n_imports_resolved, n_imports_host, n_imports_stubbed;
    retail_range_t r[RETAIL_MAX_RANGES]; int nr;   /* the headers + each section: copied, or not (code) */
} retail_image_t;

/* a section that holds or may run code: never copied by a data-only map */
#define RETAIL_SEC_CODE(ch) (((ch) & (IMAGE_SCN_CNT_CODE | IMAGE_SCN_MEM_EXECUTE)) != 0)

/* host policy: return a host function for (dll, name|ordinal), or NULL to resolve from the
 * real DLL; *skip_dll = 1 means never LoadLibrary this DLL (every import must come from the host) */
void *rh_import_policy(const char *dll, const char *name, int ord, int *skip_dll);

static void __cdecl retail_unresolved_import(void)
{
    fprintf(stderr, "[retail_map] call through UNRESOLVED import\n");
    ExitProcess(99);
}

static void retail_bind_imports(uint8_t *base, IMAGE_NT_HEADERS32 *nt, retail_image_t *out)
{
    out->n_imports_resolved = out->n_imports_host = out->n_imports_stubbed = 0;
    IMAGE_DATA_DIRECTORY *idd = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    for (IMAGE_IMPORT_DESCRIPTOR *d = (void *)(base + idd->VirtualAddress); d->Name; d++) {
        const char *dll = (const char *)(base + d->Name);
        HMODULE h = NULL;
        int loaded = 0;
        IMAGE_THUNK_DATA32 *ilt = (void *)(base + (d->OriginalFirstThunk ? d->OriginalFirstThunk : d->FirstThunk));
        IMAGE_THUNK_DATA32 *iat = (void *)(base + d->FirstThunk);
        for (; ilt->u1.AddressOfData; ilt++, iat++) {
            int ord = (ilt->u1.Ordinal & IMAGE_ORDINAL_FLAG32) ? (int)(ilt->u1.Ordinal & 0xffff) : 0;
            const char *name = ord ? NULL : (const char *)((IMAGE_IMPORT_BY_NAME *)(base + ilt->u1.AddressOfData))->Name;
            int skip = 0;
            void *p = rh_import_policy(dll, name, ord, &skip);
            if (p) { iat->u1.Function = (uint32_t)(uintptr_t)p; out->n_imports_host++; continue; }
            if (!skip && !loaded) { h = LoadLibraryA(dll); loaded = 1; }
            FARPROC q = h ? GetProcAddress(h, ord ? (LPCSTR)(uintptr_t)ord : name) : NULL;
            if (q) { iat->u1.Function = (uint32_t)(uintptr_t)q; out->n_imports_resolved++; }
            else   { iat->u1.Function = (uint32_t)(uintptr_t)retail_unresolved_import; out->n_imports_stubbed++; }
        }
    }
}

/* data_only (--standalone): the PE headers + every NON-code section; each code section (rounded up to whole pages: the
 * GOG patch's code lies past .text's VirtualSize) is 0xCC — never a byte of it read from the file.
 * port_va[n_port] (--data-image): sections that start at these VAs are not read from the file either (left zero: the
 * caller copies the port's image there; .port = 1, .copied = 0) */
static int retail_map(const char *path, retail_image_t *out, int data_only, const uint32_t *port_va, int n_port)
{
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "open %s failed\n", path); return -1; }
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *file = malloc(n);
    if (fread(file, 1, n, f) != (size_t)n) { fclose(f); return -1; }
    fclose(f);

    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)file;
    IMAGE_NT_HEADERS32 *nt = (IMAGE_NT_HEADERS32 *)(file + dos->e_lfanew);
    uint32_t ib = nt->OptionalHeader.ImageBase, sz = nt->OptionalHeader.SizeOfImage;
    uint8_t *base = NULL;
    DWORD old;
    if (VirtualProtect((void *)(uintptr_t)ib, sz, PAGE_EXECUTE_READWRITE, &old)) {
        base = (uint8_t *)(uintptr_t)ib;
        memset(base, 0, sz);
    }
    if (base != (uint8_t *)(uintptr_t)ib) {
        fprintf(stderr, "retail_map: %08x+%x is not inside the host hole (err %lu)\n", ib, sz, GetLastError());
        return -1;
    }
    memcpy(base, file, nt->OptionalHeader.SizeOfHeaders);
    out->nr = 0;
    retail_range_t *hr = &out->r[out->nr++];
    snprintf(hr->name, sizeof hr->name, "(hdr)"); hr->va = ib; hr->len = nt->OptionalHeader.SizeOfHeaders; hr->ch = 0; hr->copied = 1; hr->port = 0; hr->synth = 0;
    IMAGE_SECTION_HEADER *sec = IMAGE_FIRST_SECTION(nt);
    for (int i = 0; i < nt->FileHeader.NumberOfSections; i++) {
        if (out->nr < RETAIL_MAX_RANGES) {
            uint32_t al = nt->OptionalHeader.SectionAlignment;
            retail_range_t *r = &out->r[out->nr++];
            memcpy(r->name, sec[i].Name, 8); r->name[8] = 0;
            r->va = ib + sec[i].VirtualAddress; r->ch = sec[i].Characteristics;
            r->len = (sec[i].Misc.VirtualSize + al - 1) & ~(al - 1);
            r->copied = !(data_only && RETAIL_SEC_CODE(sec[i].Characteristics));
            r->port = 0; r->synth = 0;
            for (int k = 0; k < n_port; k++)
                if (port_va[k] == r->va && !RETAIL_SEC_CODE(sec[i].Characteristics)) { r->port = 1; r->copied = 0; }
            if (r->port) continue;
            if (!r->copied) { memset(base + sec[i].VirtualAddress, 0xCC, r->len); continue; }
        }
        /* the loader maps whole pages: raw bytes past VirtualSize but inside its last page are code too
         * (Lords2's GOG patch appended FUN_004cfb08 there without growing .text's VirtualSize) */
        uint32_t al = nt->OptionalHeader.SectionAlignment, raw = sec[i].SizeOfRawData;
        uint32_t vlim = (sec[i].Misc.VirtualSize + al - 1) & ~(al - 1);
        if (sec[i].Misc.VirtualSize && raw > vlim) raw = vlim;
        if (raw && sec[i].PointerToRawData) memcpy(base + sec[i].VirtualAddress, file + sec[i].PointerToRawData, raw);
    }
    retail_bind_imports(base, nt, out);
    out->base = base; out->size = sz;
    out->entry = ib + nt->OptionalHeader.AddressOfEntryPoint;
    free(file);
    return 0;
}

/* ---- port_map (--standalone --data-image): the whole image from the port, Lords2.exe never opened ----
 * Layout + imports: src/rl_image.c (tools/gen_image_defs.py). The headers are synthesized (DOS + NT + the section
 * table; data directories: the import table + the IAT only), .idata is built from rl_imports (descriptors, lookup
 * table and hint/name entries in a fresh layout; every IAT slot at its retail VA), each code section is 0xCC, the
 * sections in port_va[] are left zero for the caller's port image, any other section (.rsrc, .reloc) is absent:
 * zero, never read (rhost fakes the resource APIs; the image never relocates). */
#include "rl_image.h"
static int port_map(retail_image_t *out, const uint32_t *port_va, int n_port)
{
    uint32_t ib = RL_IMAGE_BASE, sz = RL_IMAGE_SIZE, al = RL_IMAGE_SEC_ALIGN;
    uint8_t *base = (uint8_t *)(uintptr_t)ib;
    DWORD old;
    if (!VirtualProtect(base, sz, PAGE_EXECUTE_READWRITE, &old)) {
        fprintf(stderr, "port_map: %08x+%x is not inside the host hole (err %lu)\n", ib, sz, GetLastError());
        return -1;
    }
    memset(base, 0, sz);
    /* headers */
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
    dos->e_magic = IMAGE_DOS_SIGNATURE; dos->e_lfanew = 0x40;
    IMAGE_NT_HEADERS32 *nt = (IMAGE_NT_HEADERS32 *)(base + dos->e_lfanew);
    nt->Signature = IMAGE_NT_SIGNATURE;
    nt->FileHeader.Machine = IMAGE_FILE_MACHINE_I386;
    nt->FileHeader.NumberOfSections = RL_IMAGE_NSECTIONS;
    nt->FileHeader.SizeOfOptionalHeader = sizeof(IMAGE_OPTIONAL_HEADER32);
    nt->FileHeader.Characteristics = IMAGE_FILE_EXECUTABLE_IMAGE | IMAGE_FILE_32BIT_MACHINE | IMAGE_FILE_RELOCS_STRIPPED;
    IMAGE_OPTIONAL_HEADER32 *oh = &nt->OptionalHeader;
    oh->Magic = IMAGE_NT_OPTIONAL_HDR32_MAGIC;
    oh->AddressOfEntryPoint = RL_IMAGE_ENTRY - ib;
    oh->ImageBase = ib; oh->SectionAlignment = al; oh->FileAlignment = 0x200;
    oh->SizeOfImage = sz; oh->SizeOfHeaders = 0x400;
    oh->Subsystem = IMAGE_SUBSYSTEM_WINDOWS_GUI;
    oh->NumberOfRvaAndSizes = IMAGE_NUMBEROF_DIRECTORY_ENTRIES;
    IMAGE_SECTION_HEADER *sec = IMAGE_FIRST_SECTION(nt);
    out->nr = 0;
    retail_range_t *hr = &out->r[out->nr++];
    snprintf(hr->name, sizeof hr->name, "(hdr)"); hr->va = ib; hr->len = oh->SizeOfHeaders; hr->ch = 0;
    hr->copied = 0; hr->port = 0; hr->synth = 1;
    const rl_image_section_t *idata = NULL;
    for (int i = 0; i < RL_IMAGE_NSECTIONS; i++) {
        const rl_image_section_t *s = &rl_image_sections[i];
        strncpy((char *)sec[i].Name, s->name, 8);
        sec[i].Misc.VirtualSize = s->vsize; sec[i].VirtualAddress = s->va - ib;
        sec[i].SizeOfRawData = s->raw; sec[i].PointerToRawData = 0; sec[i].Characteristics = s->ch;
        if (out->nr >= RETAIL_MAX_RANGES) { fprintf(stderr, "port_map: too many sections\n"); return -1; }
        retail_range_t *r = &out->r[out->nr++];
        memcpy(r->name, sec[i].Name, 8); r->name[8] = 0;
        r->va = s->va; r->ch = s->ch; r->len = (s->vsize + al - 1) & ~(al - 1);
        r->copied = 0; r->port = 0; r->synth = 0;
        if (RETAIL_SEC_CODE(s->ch)) { memset(base + (s->va - ib), 0xCC, r->len); continue; }
        for (int k = 0; k < n_port; k++) if (port_va[k] == s->va) r->port = 1;
        if (r->port) continue;
        if (RL_IMAGE_IAT_LO >= s->va && RL_IMAGE_IAT_HI <= s->va + r->len) { idata = s; r->synth = 1; continue; }
        r->synth = 2;
    }
    if (!idata) { fprintf(stderr, "port_map: no section holds the IAT\n"); return -1; }
    /* .idata: descriptors, then the lookup tables, then (past the IAT) the hint/name entries and the DLL names */
    int ndll = 0;
    for (int i = 0; i < RL_NIMPORTS; i++) ndll += !i || strcmp(rl_imports[i].dll, rl_imports[i - 1].dll);
    uint32_t lo = idata->va, hi = idata->va + ((idata->vsize + al - 1) & ~(al - 1));
    uint32_t desc = lo, ilt = lo + 20u * (ndll + 1), names = (RL_IMAGE_IAT_HI + 1) & ~1u;
    IMAGE_IMPORT_DESCRIPTOR *d = (IMAGE_IMPORT_DESCRIPTOR *)(uintptr_t)desc;
    for (int i = 0; i < RL_NIMPORTS; ) {
        int j = i;
        while (j < RL_NIMPORTS && !strcmp(rl_imports[j].dll, rl_imports[i].dll)) j++;
        if (ilt + 4u * (j - i + 1) > RL_IMAGE_IAT_LO && ilt < RL_IMAGE_IAT_HI) { fprintf(stderr, "port_map: lookup tables reach the IAT\n"); return -1; }
        size_t dl = strlen(rl_imports[i].dll) + 1;
        if (names + dl > hi) { fprintf(stderr, "port_map: .idata full\n"); return -1; }
        memcpy((void *)(uintptr_t)names, rl_imports[i].dll, dl);
        d->Name = names - ib; names = (names + dl + 1) & ~1u;
        d->OriginalFirstThunk = ilt - ib; d->FirstThunk = rl_imports[i].slot - ib;
        for (int k = i; k < j; k++) {
            const rl_import_t *m = &rl_imports[k];
            if (m->slot != rl_imports[i].slot + 4u * (k - i) || m->slot + 4 > RL_IMAGE_IAT_HI) { fprintf(stderr, "port_map: %s slots not contiguous\n", m->dll); return -1; }
            uint32_t v;
            if (!m->name) v = IMAGE_ORDINAL_FLAG32 | m->ord;
            else {
                size_t nl = strlen(m->name) + 1;
                if (names + 2 + nl > hi) { fprintf(stderr, "port_map: .idata full\n"); return -1; }
                *(uint16_t *)(uintptr_t)names = m->hint; memcpy((void *)(uintptr_t)(names + 2), m->name, nl);
                v = names - ib; names = (names + 2 + nl + 1) & ~1u;
            }
            ((uint32_t *)(uintptr_t)ilt)[k - i] = v;
            *(uint32_t *)(uintptr_t)m->slot = v;
        }
        ilt += 4u * (j - i + 1);
        d++; i = j;
    }
    oh->DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress = desc - ib;
    oh->DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].Size = 20u * (ndll + 1);
    oh->DataDirectory[IMAGE_DIRECTORY_ENTRY_IAT].VirtualAddress = RL_IMAGE_IAT_LO - ib;
    oh->DataDirectory[IMAGE_DIRECTORY_ENTRY_IAT].Size = RL_IMAGE_IAT_HI - RL_IMAGE_IAT_LO;
    retail_bind_imports(base, nt, out);
    out->base = base; out->size = sz; out->entry = RL_IMAGE_ENTRY;
    return 0;
}
