#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void Log(const char* fmt, ...) {
    FILE* f = fopen("Z:\\21\\AG_Search_log.txt", "a");
    if (f) {
        va_list args;
        va_start(args, fmt);
        vfprintf(f, fmt, args);
        va_end(args);
        fclose(f);
    }
}

// Stubs for ScriptLibs compatibility
extern "C" __declspec(dllexport) void __stdcall AG_Search_Save() {}
extern "C" __declspec(dllexport) void __stdcall AG_Search_Load() {}

// ======================================================================
// Reversible Patch Manager (Restores original bytes on FreeLibrary!)
// ======================================================================

struct CodePatch {
    DWORD address;
    size_t length;
    BYTE origBytes[16];
};

static CodePatch s_patches[32];
static size_t s_patchCount = 0;
static BOOL s_installed = FALSE;

static void RecordOriginalBytes(DWORD addr, size_t len) {
    if (!addr || len == 0 || len > sizeof(s_patches[0].origBytes)) return;
    if (s_patchCount >= (sizeof(s_patches) / sizeof(s_patches[0]))) return;

    s_patches[s_patchCount].address = addr;
    s_patches[s_patchCount].length = len;
    memcpy(s_patches[s_patchCount].origBytes, (const void*)addr, len);
    s_patchCount++;
}

static void RestoreAllSearchPatches() {
    for (size_t i = s_patchCount; i > 0; i--) {
        const CodePatch& p = s_patches[i - 1];
        DWORD oldProtect;
        if (VirtualProtect((void*)p.address, p.length, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            memcpy((void*)p.address, p.origBytes, p.length);
            VirtualProtect((void*)p.address, p.length, oldProtect, &oldProtect);
        }
    }
    s_patchCount = 0;
    s_installed = FALSE;
    Log("=== AG_Search v2.1 hooks cleanly uninstalled (original bytes restored) ===\n");
}

// ======================================================================
// Dynamically Resolved Targets via AOB Scanner
// ======================================================================

static DWORD g_addInfoSearchResultAddr = 0;
static DWORD g_getSearchResultLimitAddr = 0;
static DWORD g_getStatBonusAddr         = 0;
static DWORD g_jumpFoundAddr            = 0;
static DWORD g_jumpNotFoundAddr         = 0;

static int GetItemStatBonus(void* item, int bonusKind) {
    if (!item || !g_getStatBonusAddr) return 0;
    int bonus = 0;
    DWORD fn = g_getStatBonusAddr;
    __asm {
        push ebx
        mov eax, item
        mov edx, bonusKind
        call fn
        pop ebx
        mov bonus, eax
    }
    return bonus;
}

// ======================================================================
// Data Structures for Sorting
// ======================================================================

typedef struct {
    void* item;       // Pointer to item object in game memory
    void* planet;     // Planet where item is sold (if on a planet)
    void* ship;       // Station or ship where item is sold
    int category;     // Search category (1 - engines, 2 - tanks, ..., 12 - weapons)

    int stat1;        // Primary stat (descending, with Acryn/MM bonuses)
    int stat2;        // Secondary stat (descending, with Acryn/MM bonuses)
    int stat3;        // Tertiary stat (descending, e.g. MinDamage for weapons)
    int weight;       // Weight (ascending: lighter first, except Hull)
} FoundItem;

#define MAX_ITEMS 256
static FoundItem g_collectedItems[MAX_ITEMS];
static int g_collectedCount = 0;

static int CompareItems(const void* a, const void* b) {
    const FoundItem* ia = (const FoundItem*)a;
    const FoundItem* ib = (const FoundItem*)b;

    switch (ia->category) {
        case 1: // Engines: Speed desc -> Jump desc -> Weight asc (lighter first)
            if (ia->stat1 != ib->stat1) return ib->stat1 - ia->stat1;
            if (ia->stat2 != ib->stat2) return ib->stat2 - ia->stat2;
            return ia->weight - ib->weight;

        case 2: // Fuel Tanks: Capacity desc -> Weight asc (lighter first)
            if (ia->stat1 != ib->stat1) return ib->stat1 - ia->stat1;
            return ia->weight - ib->weight;

        case 3: // Radars: Range desc -> Weight asc (lighter first)
            if (ia->stat1 != ib->stat1) return ib->stat1 - ia->stat1;
            return ia->weight - ib->weight;

        case 4: // Scanners: ScanPower desc -> Weight asc (lighter first)
            if (ia->stat1 != ib->stat1) return ib->stat1 - ia->stat1;
            return ia->weight - ib->weight;

        case 5: // Droids: RepairPoints desc -> Weight asc (lighter first)
            if (ia->stat1 != ib->stat1) return ib->stat1 - ia->stat1;
            return ia->weight - ib->weight;

        case 6: // Cargo Hooks: PickupPower desc -> Range desc -> Weight asc (lighter first)
            if (ia->stat1 != ib->stat1) return ib->stat1 - ia->stat1;
            if (ia->stat2 != ib->stat2) return ib->stat2 - ia->stat2;
            return ia->weight - ib->weight;

        case 7: // Def Generators: Defense % desc -> Weight asc (lighter first)
            if (ia->stat1 != ib->stat1) return ib->stat1 - ia->stat1;
            return ia->weight - ib->weight;

        case 11: // Hulls: Capacity desc (heaviest/most capacious first) -> Armor desc
            if (ia->stat1 != ib->stat1) return ib->stat1 - ia->stat1;
            return ib->stat2 - ia->stat2;

        case 12: // Weapons: MaxDamage desc -> Range desc -> MinDamage desc -> Weight asc (lighter first)
            if (ia->stat1 != ib->stat1) return ib->stat1 - ia->stat1;
            if (ia->stat2 != ib->stat2) return ib->stat2 - ia->stat2;
            if (ia->stat3 != ib->stat3) return ib->stat3 - ia->stat3;
            return ia->weight - ib->weight;

        default:
            return 0;
    }
}

// ======================================================================
// Collection Hook
// ======================================================================

extern "C" void __cdecl CollectEquipment_C(void* item, void* parent_ebp) {
    if (!item || !parent_ebp) return;

    void* fInfo = *(void**)((char*)parent_ebp - 0x0C);
    if (!fInfo) return;
    int category = *(int*)((char*)fInfo + 0xEC);

    int limit = 30;
    if (g_getSearchResultLimitAddr) {
        DWORD fn = g_getSearchResultLimitAddr;
        __asm {
            mov eax, fInfo
            mov edx, fn
            call edx
            mov limit, eax
        }
    }
    if (limit <= 0) limit = 30;

    if (g_collectedCount >= limit || g_collectedCount >= MAX_ITEMS) return;

    for (int i = 0; i < g_collectedCount; i++) {
        if (g_collectedItems[i].item == item) return;
    }

    g_collectedItems[g_collectedCount].item     = item;
    g_collectedItems[g_collectedCount].planet   = *(void**)((char*)parent_ebp - 0x24);
    g_collectedItems[g_collectedCount].ship     = *(void**)((char*)parent_ebp - 0x04);
    g_collectedItems[g_collectedCount].category = category;
    g_collectedItems[g_collectedCount].stat1    = 0;
    g_collectedItems[g_collectedCount].stat2    = 0;
    g_collectedItems[g_collectedCount].stat3    = 0;
    g_collectedItems[g_collectedCount].weight   = *(int*)((char*)item + 0x18);

    switch (category) {
        case 1: { // Engine
            int baseSpeed = *(int*)((char*)item + 0x64);
            int speedBonus = GetItemStatBonus(item, 2); // bonus_speed
            int baseJump  = (int)*(signed char*)((char*)item + 0x68);
            int jumpBonus  = GetItemStatBonus(item, 3); // bonus_jump_range

            g_collectedItems[g_collectedCount].stat1 = baseSpeed + speedBonus;
            g_collectedItems[g_collectedCount].stat2 = baseJump + jumpBonus;
            break;
        }

        case 2: { // Fuel Tanks
            int baseCap = (int)*(unsigned char*)((char*)item + 0x68);
            int capBonus = GetItemStatBonus(item, 1); // bonus_fuel_capacity

            g_collectedItems[g_collectedCount].stat1 = baseCap + capBonus;
            break;
        }

        case 3: { // Radar
            int baseRange = *(int*)((char*)item + 0x64);
            int rangeBonus = GetItemStatBonus(item, 4); // bonus_radar_range

            g_collectedItems[g_collectedCount].stat1 = baseRange + rangeBonus;
            break;
        }

        case 4: { // Scanner
            int basePower = (int)*(signed char*)((char*)item + 0x61);
            int powerBonus = GetItemStatBonus(item, 5); // bonus_scanner_power

            g_collectedItems[g_collectedCount].stat1 = basePower + powerBonus;
            break;
        }

        case 5: { // Droid
            int baseRepair = (int)*(unsigned char*)((char*)item + 0x61);
            int repairBonus = GetItemStatBonus(item, 6); // bonus_repair_points

            g_collectedItems[g_collectedCount].stat1 = baseRepair + repairBonus;
            break;
        }

        case 6: { // Cargo Hook
            int basePickup = *(int*)((char*)item + 0x64);
            int pickupBonus = GetItemStatBonus(item, 7); // bonus_hook_power
            int baseRange  = *(int*)((char*)item + 0x68);
            int rangeBonus  = GetItemStatBonus(item, 21); // bonus_hook_range

            g_collectedItems[g_collectedCount].stat1 = basePickup + pickupBonus;
            g_collectedItems[g_collectedCount].stat2 = baseRange + rangeBonus;
            break;
        }

        case 7: { // Def Generator
            float damageFactor = *(float*)((char*)item + 0x64);
            int basePercent = (int)((1.0f - damageFactor) * 100.0f + 0.5f);
            int defBonus = GetItemStatBonus(item, 8); // bonus_defense

            g_collectedItems[g_collectedCount].stat1 = basePercent + defBonus;
            break;
        }

        case 11: { // Hull
            int baseArmor = (int)*(signed char*)((char*)item + 0x65);
            int armorBonus = GetItemStatBonus(item, 0); // bonus_armor

            g_collectedItems[g_collectedCount].stat1 = *(int*)((char*)item + 0x18);
            g_collectedItems[g_collectedCount].stat2 = baseArmor + armorBonus;
            break;
        }

        case 12: { // Weapon
            int maxDmg = *(int*)((char*)item + 0x6C);
            int baseRange = *(int*)((char*)item + 0x64);
            int rangeBonus = GetItemStatBonus(item, 12); // bonus_weapon_range
            int minDmg = *(int*)((char*)item + 0x68);

            g_collectedItems[g_collectedCount].stat1 = maxDmg;
            g_collectedItems[g_collectedCount].stat2 = baseRange + rangeBonus;
            g_collectedItems[g_collectedCount].stat3 = minDmg;
            break;
        }

        default:
            if (g_addInfoSearchResultAddr) {
                DWORD fn = g_addInfoSearchResultAddr;
                __asm {
                    push parent_ebp
                    mov eax, item
                    mov edx, fn
                    call edx
                    pop ecx
                }
            }
            return;
    }

    g_collectedCount++;
}

__declspec(naked) void Hook_CollectEquipment_Naked() {
    __asm {
        pushad
        mov edx, [esp + 32 + 4] // parent_ebp
        push edx
        push eax                // item
        call CollectEquipment_C
        add esp, 8
        popad
        ret
    }
}

// ======================================================================
// Loop End Hook: Sort and Render
// ======================================================================

extern "C" void __cdecl HandleSearchLoopEnd_C(void* parent_ebp) {
    if (g_collectedCount <= 0 || !parent_ebp || !g_addInfoSearchResultAddr) {
        g_collectedCount = 0;
        return;
    }

    int count = g_collectedCount;
    int cat = g_collectedItems[0].category;

    qsort(g_collectedItems, count, sizeof(FoundItem), CompareItems);

    Log("[SEARCH] Category %d: rendering %d sorted items\n", cat, count);

    DWORD fn = g_addInfoSearchResultAddr;
    for (int i = 0; i < count; i++) {
        *(void**)((char*)parent_ebp - 0x24) = g_collectedItems[i].planet;
        *(void**)((char*)parent_ebp - 0x04) = g_collectedItems[i].ship;

        void* item = g_collectedItems[i].item;
        __asm {
            push parent_ebp
            mov eax, item
            mov edx, fn
            call edx
            pop ecx
        }
    }

    g_collectedCount = 0;
}

__declspec(naked) void Hook_SearchLoopEnd_Naked() {
    __asm {
        pushad
        push ebp
        call HandleSearchLoopEnd_C
        add esp, 4
        popad

        cmp byte ptr [ebp - 0x11], 0
        jz jump_not_found
        jmp dword ptr [g_jumpFoundAddr]

    jump_not_found:
        jmp dword ptr [g_jumpNotFoundAddr]
    }
}

// ======================================================================
// AOB Pattern Scanner
// ======================================================================

static uintptr_t FindPattern(uintptr_t start, size_t size, const unsigned char* pattern, const char* mask) {
    size_t patternLen = strlen(mask);
    if (size < patternLen) return 0;
    size_t scanEnd = size - patternLen;

    for (size_t i = 0; i <= scanEnd; i++) {
        bool match = true;
        for (size_t j = 0; j < patternLen; j++) {
            if (mask[j] != '?' && pattern[j] != *(const unsigned char*)(start + i + j)) {
                match = false;
                break;
            }
        }
        if (match) {
            return start + i;
        }
    }
    return 0;
}

static BOOL GetCodeSectionRange(uintptr_t* codeBase, size_t* codeSize) {
    HMODULE hMod = GetModuleHandleA(NULL);
    if (!hMod) return FALSE;

    IMAGE_DOS_HEADER* dosHdr = (IMAGE_DOS_HEADER*)hMod;
    if (dosHdr->e_magic != IMAGE_DOS_SIGNATURE) return FALSE;

    IMAGE_NT_HEADERS* ntHdr = (IMAGE_NT_HEADERS*)((char*)hMod + dosHdr->e_lfanew);
    if (ntHdr->Signature != IMAGE_NT_SIGNATURE) return FALSE;

    IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(ntHdr);
    for (WORD i = 0; i < ntHdr->FileHeader.NumberOfSections; i++, sec++) {
        if ((sec->Characteristics & IMAGE_SCN_MEM_EXECUTE) && (sec->Characteristics & IMAGE_SCN_CNT_CODE)) {
            *codeBase = (uintptr_t)hMod + sec->VirtualAddress;
            *codeSize = sec->Misc.VirtualSize;
            return TRUE;
        }
    }

    *codeBase = (uintptr_t)hMod + ntHdr->OptionalHeader.BaseOfCode;
    *codeSize = ntHdr->OptionalHeader.SizeOfCode;
    return TRUE;
}

// ======================================================================
// Hook Installation with Dynamic AOB Resolution & Reversible Patches
// ======================================================================

static void InstallSearchHooks() {
    if (s_installed) return;

    Log("=== AG_Search v2.1 active (Reversible Hooks + Acryn & MM Bonuses + 9-Category Sorting) ===\n");

    uintptr_t codeBase = 0;
    size_t codeSize = 0;
    if (!GetCodeSectionRange(&codeBase, &codeSize)) {
        Log("[ERROR] Failed to obtain code section range\n");
        return;
    }

    // 0. Locate TEquipment.GetStatBonus
    const unsigned char sigGetStatBonus[] = {
        0x53, 0x56, 0x57, 0x55, 0x83, 0xC4, 0xF4, 0x8B, 0xDA, 0x8B, 0xF8, 0x8B, 0xC3, 0x04, 0xEA
    };
    uintptr_t statBonusMatch = FindPattern(codeBase, codeSize, sigGetStatBonus, "xxxxxxxxxxxxxxx");
    if (statBonusMatch) {
        g_getStatBonusAddr = (DWORD)statBonusMatch;
    }

    // 1. Locate CheckInfoSearchResult category checks
    const unsigned char sigCategory[] = {
        0x8B, 0x45, 0x08,                    // mov eax, [ebp + 8]
        0x8B, 0x40, 0xF4,                    // mov eax, [eax - 0xC]
        0x8B, 0x80, 0xEC, 0x00, 0x00, 0x00,  // mov eax, [eax + 0xEC]
        0x83, 0xF8, 0x01                     // cmp eax, 1
    };
    uintptr_t catCheckAddr = FindPattern(codeBase, codeSize, sigCategory, "xxxxxxxxxxxxxx");
    if (!catCheckAddr) {
        Log("[WARN] AOB signature for CheckInfoSearchResult not found. Hooks NOT installed.\n");
        return;
    }

    // 2. Locate SearchLoopEnd in RunSearch BEFORE patching anything
    const unsigned char sigLoopEnd[] = {
        0x80, 0x7D, 0xEF, 0x00,              // cmp byte ptr [ebp - 0x11], 0
        0x0F, 0x84, 0x00, 0x00, 0x00, 0x00,  // je rel32
        0xBA, 0x0A, 0x00, 0x00, 0x00,        // mov edx, 0xA
        0x8B, 0x45, 0xF4                     // mov eax, [ebp - 0xC]
    };
    uintptr_t loopEndAddr = FindPattern(codeBase, codeSize, sigLoopEnd, "xxxxxx????xxxxxxx");
    if (!loopEndAddr) {
        Log("[WARN] AOB signature for SearchLoopEnd not found. Hooks NOT installed.\n");
        return;
    }

    int relNotFound = *(int*)(loopEndAddr + 6);
    g_jumpNotFoundAddr = (DWORD)((loopEndAddr + 10) + relNotFound);
    g_jumpFoundAddr    = (DWORD)(loopEndAddr + 10);

    // 3. Scan within CheckInfoSearchResult for all AddInfoSearchResult calls
    const unsigned char sigCall[] = { 0x8B, 0x45, 0x08, 0x50, 0x8B, 0xC3, 0xE8 };
    uintptr_t scanPtr = catCheckAddr;
    uintptr_t scanLimit = catCheckAddr + 0x1200;
    int hookedCallsCount = 0;

    DWORD oldP;
    while (scanPtr < scanLimit) {
        uintptr_t match = FindPattern(scanPtr, scanLimit - scanPtr, sigCall, "xxxxxxx");
        if (!match) break;

        uintptr_t callSite = match + 6;
        int relCall = *(int*)(callSite + 1);
        uintptr_t targetFn = (callSite + 5) + relCall;

        if (!g_addInfoSearchResultAddr) {
            g_addInfoSearchResultAddr = (DWORD)targetFn;

            const unsigned char sigLimit[] = { 0x8B, 0x40, 0xF4, 0xE8 };
            uintptr_t limitMatch = FindPattern(targetFn, 0x100, sigLimit, "xxxx");
            if (limitMatch) {
                uintptr_t limitCallSite = limitMatch + 3;
                int limitRel = *(int*)(limitCallSite + 1);
                g_getSearchResultLimitAddr = (DWORD)((limitCallSite + 5) + limitRel);
            }
        }

        if (VirtualProtect((void*)callSite, 5, PAGE_EXECUTE_READWRITE, &oldP)) {
            RecordOriginalBytes((DWORD)callSite, 5);
            BYTE jmpCode[5];
            jmpCode[0] = 0xE8;
            *(DWORD*)(&jmpCode[1]) = (DWORD)Hook_CollectEquipment_Naked - ((DWORD)callSite + 5);
            memcpy((void*)callSite, jmpCode, 5);
            VirtualProtect((void*)callSite, 5, oldP, &oldP);
            hookedCallsCount++;
        }

        scanPtr = match + 7;
    }

    // 4. Patch SearchLoopEnd
    if (VirtualProtect((void*)loopEndAddr, 10, PAGE_EXECUTE_READWRITE, &oldP)) {
        RecordOriginalBytes((DWORD)loopEndAddr, 10);
        BYTE jmpCode[10];
        memset(jmpCode, 0x90, 10);
        jmpCode[0] = 0xE9;
        *(DWORD*)(&jmpCode[1]) = (DWORD)Hook_SearchLoopEnd_Naked - ((DWORD)loopEndAddr + 5);
        memcpy((void*)loopEndAddr, jmpCode, 10);
        VirtualProtect((void*)loopEndAddr, 10, oldP, &oldP);
    }

    s_installed = TRUE;
    Log("=== AG_Search v2.1 initialized (%u patches recorded) ===\n", (unsigned int)s_patchCount);
}

extern "C" __declspec(dllexport) void __stdcall AG_Search_Init() {
    InstallSearchHooks();
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
    if (fdwReason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hinstDLL);
        InstallSearchHooks();
    } else if (fdwReason == DLL_PROCESS_DETACH) {
        if (lpvReserved == NULL && s_installed) {
            RestoreAllSearchPatches();
        }
    }
    return TRUE;
}