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

typedef struct {
    uint8_t *base;
    uint32_t size;
    uint32_t entry;
    int n_imports_resolved, n_imports_host, n_imports_stubbed;
} retail_image_t;

/* host policy: return a host function for (dll, name|ordinal), or NULL to resolve from the
 * real DLL; *skip_dll = 1 means never LoadLibrary this DLL (every import must come from the host) */
void *rh_import_policy(const char *dll, const char *name, int ord, int *skip_dll);

static void __cdecl retail_unresolved_import(void)
{
    fprintf(stderr, "[retail_map] call through UNRESOLVED import\n");
    ExitProcess(99);
}

static int retail_map(const char *path, retail_image_t *out)
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
    IMAGE_SECTION_HEADER *sec = IMAGE_FIRST_SECTION(nt);
    for (int i = 0; i < nt->FileHeader.NumberOfSections; i++) {
        /* the loader maps whole pages: raw bytes past VirtualSize but inside its last page are code too
         * (Lords2's GOG patch appended FUN_004cfb08 there without growing .text's VirtualSize) */
        uint32_t al = nt->OptionalHeader.SectionAlignment, raw = sec[i].SizeOfRawData;
        uint32_t vlim = (sec[i].Misc.VirtualSize + al - 1) & ~(al - 1);
        if (sec[i].Misc.VirtualSize && raw > vlim) raw = vlim;
        if (raw) memcpy(base + sec[i].VirtualAddress, file + sec[i].PointerToRawData, raw);
    }
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
    out->base = base; out->size = sz;
    out->entry = ib + nt->OptionalHeader.AddressOfEntryPoint;
    free(file);
    return 0;
}
