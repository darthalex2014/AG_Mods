#include <windows.h>
#include <oleauto.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma comment(lib, "OleAut32.lib")

// ======================================================================
// AG_FuelTanks v1.1 (Space Rangers HD: Universe / Evolution EvoBalance)
// Author: AlexGoD
// ======================================================================

static void Log(const char* fmt, ...) {
    FILE* f = fopen("AG_FuelTanks_log.txt", "a");
    if (f) {
        va_list args;
        va_start(args, fmt);
        vfprintf(f, fmt, args);
        va_end(args);
        fclose(f);
    }
}

// ======================================================================
// Цветовая палитра линеек (Rich Text движка игры)
// ======================================================================

#define CLR_MK9     L"<color=215,90,255>"   // Фиолетовый (Реликт Т9 Индиго)
#define CLR_END     L"</color>"
#define CLR_ACRYN   L"<color=255,160,80>"   // Оранжевый цвет акринового оборудования

// ======================================================================
// Reversible Patch Manager (Restores original bytes on FreeLibrary!)
// ======================================================================

struct CodePatch {
    DWORD address;
    size_t length;
    BYTE origBytes[32];
};

static CodePatch s_patches[16];
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

static void RestoreAllPatches() {
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
    Log("=== AG_FuelTanks v1.1 hooks cleanly uninstalled (original bytes restored) ===\n");
}

// Stubs for ScriptLibs compatibility
extern "C" __declspec(dllexport) void __stdcall AG_FuelTanks_Save() {}
extern "C" __declspec(dllexport) void __stdcall AG_FuelTanks_Load() {}

// ======================================================================
// Canonical Russian Tier Names (Vanilla / EvoBalance / Universe)
// ======================================================================

static const wchar_t* s_tierNames[10] = {
    L"",
    L"Гипержидкостный",
    L"Конденсатный",
    L"Редукционный",
    L"Протопузырчатый",
    L"Позиционный",
    L"Эндокластерный",
    L"Гироскопический",
    L"Текриновый",
    L"Реликтовый"
};

// ======================================================================
// Model Specification Tables (Capacity & Base Costs)
// ======================================================================

// Базовые значения mCapacity: 10, 15, 20, 25, 30, 35, 40, 45, Т9=50
static const int s_vanillaCapacity[10] = { 0, 10, 15, 20, 25, 30, 35, 40, 45, 50 };

// Базовые цены поколений (Level^2 * 500 cr, Т9 = 44 000 cr)
static const int s_baseTierCost[10]    = { 0, 500, 2000, 4500, 8000, 12500, 18000, 24500, 32000, 44000 };

// ======================================================================
// String Allocation Helpers for Delphi WideString (BSTR)
// ======================================================================

static void SetItemConfigBlockName(void* item, const wchar_t* name) {
    if (!item) return;
    BSTR* pBstr = (BSTR*)((char*)item + 0x38);
    if (*pBstr) {
        SysFreeString(*pBstr);
        *pBstr = NULL;
    }
    if (name && name[0]) {
        *pBstr = SysAllocString(name);
    }
}

static void SetItemNameOverride(void* item, const wchar_t* name) {
    if (!item) return;
    BSTR* pBstr = (BSTR*)((char*)item + 0x24);
    if (*pBstr) {
        SysFreeString(*pBstr);
        *pBstr = NULL;
    }
    if (name && name[0]) {
        *pBstr = SysAllocString(name);
    }
}

static const wchar_t* GetItemConfigBlockName(void* item) {
    if (!item) return NULL;
    BSTR bstr = *(BSTR*)((char*)item + 0x38);
    return bstr ? (const wchar_t*)bstr : NULL;
}

static const wchar_t* GetItemNameOverride(void* item) {
    if (!item) return NULL;
    BSTR bstr = *(BSTR*)((char*)item + 0x24);
    return bstr ? (const wchar_t*)bstr : NULL;
}

// ======================================================================
// Cost & Stat Calculation Helpers
// ======================================================================

static float GetOwnerPriceFactor(int owner) {
    switch (owner) {
        case 0: return 0.70f; // Maloc
        case 1: return 0.90f; // Peleng
        case 2: return 1.00f; // Human
        case 3: return 1.15f; // Feyan
        case 4: return 1.30f; // Gaal
        case 7: return 0.80f; // Pirate
        default: return 1.00f;
    }
}

// Фикс ванильной экономики: устраняет срез -50% для легких баков (20-39т),
// поднимая пол до 0.8x, сохраняя 40т на 1.0x и 80т на 2.0x!
static int CalculateFuelTankCost(int weight, int baseCost, int owner) {
    if (weight <= 0) weight = 40;

    double weightFactor = 1.0;
    if (weight <= 40) {
        weightFactor = 0.80 + ((double)(weight - 20) / 20.0) * 0.20;
    } else {
        weightFactor = 1.00 + ((double)(weight - 40) / 40.0) * 1.00;
    }

    double ownerFactor = (double)GetOwnerPriceFactor(owner);
    int cost = (int)(weightFactor * (double)baseCost * ownerFactor + 0.5);
    cost = (cost / 10) * 10;
    if (cost < 100) cost = 100;
    return cost;
}

static int IdentifyCustomModelByConfig(const wchar_t* configBlock, int* outLevel) {
    if (!configBlock || !configBlock[0]) return 0;

    if (wcscmp(configBlock, L"FuelTanksBase9") == 0) {
        if (outLevel) *outLevel = 9;
        return 4; // Base T9
    }
    return 0;
}

static int IdentifyCustomModelByNameOverride(const wchar_t* nameOverride, int techLevel, int* outLevel) {
    if (!nameOverride || !nameOverride[0]) return 0;

    if (wcsstr(nameOverride, L"Реликтовый топливный бак") != NULL) {
        if (outLevel) *outLevel = 9;
        return 4;
    }
    return 0;
}

static int IdentifyCustomModelFromItem(void* item, int* outLevel) {
    if (!item) return 0;
    int lvl = *(unsigned char*)((char*)item + 0x60); // TechLevel is at 0x60 in TFuelTanks
    if (outLevel) *outLevel = lvl;

    const wchar_t* configBlock = GetItemConfigBlockName(item);
    int kind = IdentifyCustomModelByConfig(configBlock, outLevel);
    if (kind != 0) return kind;

    const wchar_t* nameOverride = GetItemNameOverride(item);
    return IdentifyCustomModelByNameOverride(nameOverride, lvl, outLevel);
}

// Быстрый вызов ожидаемой вместимости для Научной Базы
static int __fastcall GetModelExpectedCapacityFastcall(void* item) {
    if (!item) return 10;
    __try {
        if (*(BYTE*)((char*)item + 0x0C) != 43) return 10; // item_fueltanks only
        int lvl = *(unsigned char*)((char*)item + 0x60);    // TechLevel is at 0x60
        int weight = *(int*)((char*)item + 0x18);          // Weight is at 0x18
        int modelLvl = lvl;
        int modelKind = IdentifyCustomModelFromItem(item, &modelLvl);

        if (modelKind == 4 || lvl == 9) {
            return (weight / 2) + 50; // Т9: 20т->60, 40т->70, 80т->90
        }

        if (lvl >= 1 && lvl <= 8) {
            return (weight / 2) + s_vanillaCapacity[lvl];
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 10;
    }
    return 10;
}

// ======================================================================
// Target Addresses & Hooks State
// ======================================================================

static DWORD g_fuelInitExitAddr              = 0;
static DWORD g_calcGeneratedCapacityAddr     = 0;
static DWORD g_hasStandardStatsAddr          = 0;
static DWORD g_applySMMExitAddr              = 0;
static DWORD g_prevSMMHook                   = 0;
static DWORD g_mmTemplatesGlobalPtrAddr      = 0x008136B4;

static DWORD g_origEquipmentSaveToBufferAddr = 0;

// ======================================================================
// Helper: Check if Fuel Tank has an Acryn with a unique 3D model (KindGraph)
// ======================================================================

static bool HasAcrynCustomModel(void* item, BSTR* outAcrynName, BSTR* outKindGraph) {
    if (outAcrynName) *outAcrynName = NULL;
    if (outKindGraph) *outKindGraph = NULL;
    if (!item) return false;

    int special = *(int*)((char*)item + 0x54);
    if (special <= 0) return false;

    char*** pppMmTemplates = (char***)g_mmTemplatesGlobalPtrAddr;
    if (!pppMmTemplates || !*pppMmTemplates || !**pppMmTemplates) return false;
    char* mmBase = **pppMmTemplates;

    DWORD entryOffset = (DWORD)(special * 0x49 * 4);
    BSTR acrynName = *(BSTR*)(mmBase + entryOffset - 0x120);
    BSTR kindGraph = *(BSTR*)(mmBase + entryOffset - 0x18);

    if (outAcrynName) *outAcrynName = acrynName;
    if (outKindGraph) *outKindGraph = kindGraph;

    return (kindGraph && !IsBadStringPtrW(kindGraph, 64) && kindGraph[0] != L'\0');
}

// ======================================================================
// Acryn Post-Processor: Hooked at exit of ApplySpecialMicroModule
// ======================================================================

extern "C" void __stdcall PostApplySpecialMicroModule_C(void* item) {
    if (!item) return;
    __try {
        if (*(BYTE*)((char*)item + 0x0C) != 43) return; // item_fueltanks only

        int special = *(int*)((char*)item + 0x54);
        if (special <= 0) return;

        int lvl = 0;
        int kind = IdentifyCustomModelFromItem(item, &lvl);
        if (kind == 0) return;

        BSTR acrynName = NULL;
        BSTR kindGraph = NULL;
        bool hasCustomModel = HasAcrynCustomModel(item, &acrynName, &kindGraph);

        if (acrynName && !IsBadStringPtrW(acrynName, 128) && acrynName[0] != L'\0') {
            wchar_t fullAcrynName[256];
            swprintf(fullAcrynName, 256, L"%s%s%s", CLR_ACRYN, (const wchar_t*)acrynName, CLR_END);
            SetItemNameOverride(item, fullAcrynName);
        }

        if (hasCustomModel) {
            SetItemConfigBlockName(item, NULL);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("[ACRYN ERROR] Exception in PostApplySpecialMicroModule_C\n");
    }
}

__declspec(naked) void Hook_ApplySpecialMicroModule_Exit_Naked() {
    __asm {
        pushad
        push ebx
        call PostApplySpecialMicroModule_C
        popad

        cmp dword ptr [g_prevSMMHook], 0
        je DefaultEpilogue
        jmp dword ptr [g_prevSMMHook]

    DefaultEpilogue:
        add esp, 0x44
        pop ebp
        pop edi
        pop esi
        pop ebx
        ret
    }
}

// ======================================================================
// Save/Load Hooks: Prevent ANY Custom ConfigBlockName from Entering .sav!
// ======================================================================

static BSTR __fastcall StripCustomConfigBeforeSaveFastcall(void* item) {
    if (!item) return NULL;
    __try {
        BSTR* pBstr = (BSTR*)((char*)item + 0x38);
        BSTR saved = *pBstr;
        if (saved) {
            *pBstr = NULL; // Hide from TEquipment.SaveToBuffer so .sav has empty ConfigBlockName
            Log("[SAVE] Stripped ConfigBlockName '%S' from fuel tank before saving\n", (const wchar_t*)saved);
            return saved;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return NULL;
}

static void __fastcall RestoreCustomConfigAfterSaveFastcall(void* item, BSTR saved) {
    if (item && saved) {
        *(BSTR*)((char*)item + 0x38) = saved;
    }
}

__declspec(naked) void Hook_TFuelTanks_SaveToBuffer_InheritedCall() {
    __asm {
        push ebx
        push esi
        mov ebx, eax                  // Self (TFuelTanks*)
        mov esi, edx                  // Buffer (TBufEC*)

        mov ecx, ebx
        call StripCustomConfigBeforeSaveFastcall
        push eax                      // save original BSTR on stack

        mov edx, esi                  // Buffer
        mov eax, ebx                  // Self
        call dword ptr [g_origEquipmentSaveToBufferAddr]

        pop edx                       // saved BSTR
        mov ecx, ebx                  // Self
        call RestoreCustomConfigAfterSaveFastcall

        pop esi
        pop ebx
        ret
    }
}

extern "C" void __stdcall PostLoadFuelTanksFromBuffer_C(void* item) {
    if (!item) return;
    __try {
        int lvl = 0;
        int kind = IdentifyCustomModelFromItem(item, &lvl);
        if (kind == 0) return;

        int special = *(int*)((char*)item + 0x54);
        if (special > 0) {
            PostApplySpecialMicroModule_C(item);
        }

        if (HasAcrynCustomModel(item, NULL, NULL)) {
            SetItemConfigBlockName(item, NULL);
        } else {
            if (kind == 4 || lvl == 9) {
                SetItemConfigBlockName(item, L"FuelTanksBase9");
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("[LOAD ERROR] Exception in PostLoadFuelTanksFromBuffer_C\n");
    }
}

__declspec(naked) void Hook_TFuelTanks_LoadFromBuffer_Exit_Naked() {
    __asm {
        mov byte ptr [ebx + 0x68], al // Restore original instruction: Capacity = al

        pushad
        push ebx
        call PostLoadFuelTanksFromBuffer_C
        popad

        pop esi
        pop ebx
        ret
    }
}

// ======================================================================
// Fast Uniform Random Number Generator
// ======================================================================

static unsigned int s_randSeed = 314159265;
static unsigned int ModRandom(unsigned int minVal, unsigned int maxVal) {
    if (maxVal <= minVal) return minVal;
    s_randSeed = s_randSeed * 1664525u + 1013904223u;
    return minVal + (s_randSeed % (maxVal - minVal + 1));
}

// ======================================================================
// Post-Init Hook: Fixes light tank pricing + Spawns rare Tier 9 Relic!
// ======================================================================

extern "C" void __stdcall PostProcessFuelTanksInit_C(void* item, DWORD callerAddr, int weight, int level, int owner) {
    if (!item) return;

    // 1. СТРОГИЙ ПРЕДОХРАНИТЕЛЬ: ТОЛЬКО топливные баки (ItemType = 43)
    if (*(BYTE*)((char*)item + 0x0C) != 43) return;

    // 2. СТРОГИЙ ПРЕДОХРАНИТЕЛЬ ВЕСА: нормальный вес баков от 20 до 80 тонн
    if (weight < 20 || weight > 80) return;

    // 3. ИЗОЛЯЦИЯ: отсекаем NPC, станции и Доминаторов
    if (callerAddr == 0x006E14CA) return; // TShip.CreateAndEquipFuelTanks
    if (callerAddr == 0x005BF644) return; // aPlayer starting equipment
    if (owner == 5 || owner == 6) return;  // Dominator (oiDominator) or Uninhabited

    level = level & 0xFF;
    if (level < 1) level = 1;
    if (level > 8) level = 8;

    // Все ванильные баки Т1..Т8 в магазинах получают справедливую цену без бага -50%!
    *(int*)((char*)item + 0x20) = CalculateFuelTankCost(weight, s_baseTierCost[level], owner);

    // Спавн Реликта 9-го тира на поздней стадии игры (Т8)
    if (level == 8) {
        int roll = ModRandom(1, 100);
        if (roll <= 2) {
            // === РОВНО 2% ШАНС (1 НА 50 БАКОВ Т8) ===
            SetItemConfigBlockName(item, L"FuelTanksBase9");
            SetItemNameOverride(item, L"Реликтовый топливный бак");

            int customCapacity = (weight / 2) + 50; // 20т->60, 40т->70, 80т->90
            *(BYTE*)((char*)item + 0x68) = (BYTE)customCapacity; // Capacity is at +0x68
            *(int*)((char*)item + 0x64) = customCapacity;        // Fuel is at +0x64 (полный бак)

            *(int*)((char*)item + 0x20) = CalculateFuelTankCost(weight, s_baseTierCost[9], owner);
            Log("[RELIC T9] Spawned Relic Fuel Tank! Weight=%d, Capacity=%d, Price=%d, Owner=%d\n", 
                weight, customCapacity, *(int*)((char*)item + 0x20), owner);
        }
    }
}

__declspec(naked) void Hook_TFuelTanks_Init_Exit_Naked() {
    __asm {
        xor eax, eax
        mov [esi + 0x50], eax

        pushad
        movzx ecx, byte ptr [esi + 0x1C]  // OwnerId
        push ecx
        movzx ecx, byte ptr [esi + 0x60]  // TechLevel
        push ecx
        mov ecx, [esi + 0x18]             // Weight
        push ecx
        mov ecx, [ebp + 4]                // Caller return address
        push ecx
        push esi                          // item (Self = esi!)
        call PostProcessFuelTanksInit_C
        popad

        pop esi
        pop ebx
        pop ebp
        ret 4
    }
}

// ======================================================================
// Calculated Capacity Hook (Science Base / HasStandardStats)
// ======================================================================

__declspec(naked) void Hook_CalculateFuelCapacity_Naked() {
    __asm {
        push ebx
        mov ebx, eax
        mov ecx, eax
        call GetModelExpectedCapacityFastcall
        pop ebx
        ret
    }
}

// ======================================================================
// HasStandardStats Hook: Guarantees Science Base Upgrades for T9!
// ======================================================================

static BOOL __fastcall FuelTanks_HasStandardStats_Fastcall(void* item) {
    if (!item) return FALSE;
    __try {
        if (*(BYTE*)((char*)item + 0x0C) != 43) return FALSE;

        int lvl = *(BYTE*)((char*)item + 0x60);
        int weight = *(int*)((char*)item + 0x18);
        int cap = *(BYTE*)((char*)item + 0x68);
        int mmIdx = *(int*)((char*)item + 0x50);
        int mmBonus = 0;
        int sizePercent = 0;

        if (mmIdx > 0 && g_mmTemplatesGlobalPtrAddr) {
            char*** ppp = (char***)g_mmTemplatesGlobalPtrAddr;
            if (ppp && *ppp && **ppp) {
                char* mmBase = **ppp;
                DWORD entryOffset = (DWORD)(mmIdx * 0x49 * 4);
                mmBonus = *(int*)(mmBase + entryOffset - 0x10C); // bonFuel
                sizePercent = *(int*)(mmBase + entryOffset - 0x60); // SizePercent
            }
        }

        int baseCap = cap - mmBonus;
        int modelLvl = lvl;
        int modelKind = IdentifyCustomModelFromItem(item, &modelLvl);

        int baseTableCap = 0;
        if (modelKind == 4 || lvl == 9) {
            baseTableCap = 50; // T9 base
        } else if (lvl >= 1 && lvl <= 8) {
            baseTableCap = s_vanillaCapacity[lvl];
        } else {
            baseTableCap = 10;
        }

        int expectedWeight = (baseCap - baseTableCap) * 2;
        if (sizePercent > 0) {
            expectedWeight = (int)((double)expectedWeight * (double)sizePercent / 100.0 + 0.5);
        }

        int diff = expectedWeight - weight;
        if (diff < 0) diff = -diff;
        return (diff <= 1);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return FALSE;
    }
}

__declspec(naked) void Hook_TFuelTanks_HasStandardStats_Naked() {
    __asm {
        push ecx
        push edx
        mov ecx, eax // Self (TFuelTanks*)
        call FuelTanks_HasStandardStats_Fastcall
        pop edx
        pop ecx
        ret
    }
}

// ======================================================================
// AOB Pattern Scanner & Reversible Hook Installers
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
        if (match) return start + i;
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

static void InstallCallRedirect(DWORD callAddr, void* target, const char* name) {
    DWORD oldProtect;
    if (!callAddr || *(BYTE*)callAddr != 0xE8) {
        Log("[WARN] Call redirect skipped: %s at 0x%08X (not 0xE8)\n", name, callAddr);
        return;
    }
    if (VirtualProtect((void*)callAddr, 5, PAGE_EXECUTE_READWRITE, &oldProtect)) {
        RecordOriginalBytes(callAddr, 5);
        *(DWORD*)(callAddr + 1) = (DWORD)target - (callAddr + 5);
        VirtualProtect((void*)callAddr, 5, oldProtect, &oldProtect);
        Log("[HOOK] Call redirect: %s 0x%08X -> 0x%08X\n", name, callAddr, (DWORD)target);
    }
}

static void InstallJmpHook(DWORD addr, void* target, size_t len, const char* name) {
    if (!addr || len < 5) return;
    DWORD oldProtect;
    if (VirtualProtect((void*)addr, len, PAGE_EXECUTE_READWRITE, &oldProtect)) {
        RecordOriginalBytes(addr, len);
        memset((void*)addr, 0x90, len);
        BYTE jmpCode[5];
        jmpCode[0] = 0xE9;
        *(DWORD*)(&jmpCode[1]) = (DWORD)target - (addr + 5);
        memcpy((void*)addr, jmpCode, 5);
        VirtualProtect((void*)addr, len, oldProtect, &oldProtect);
        Log("[HOOK] Jmp hook installed: %s at 0x%08X (len %u)\n", name, addr, (unsigned int)len);
    }
}

// ======================================================================
// Initialization & Installation
// ======================================================================

void InstallFuelTanksHooks() {
    if (s_installed) return;

    Log("=== AG_FuelTanks v1.1 initializing (Savegame Safe + Science Base + Acryn Support) ===\n");

    uintptr_t codeBase = 0;
    size_t codeSize = 0;
    if (!GetCodeSectionRange(&codeBase, &codeSize)) {
        Log("[ERROR] Failed to obtain code section range\n");
        return;
    }

    // 1. TFuelTanks.HasStandardStats at 0x006FDB44
    const unsigned char sigHasStats[] = {
        0x53, 0x56, 0x57, 0x51, 0x8B, 0xD8, 0x8B, 0x73, 0x50, 0x85, 0xF6
    };
    uintptr_t matchHasStats = FindPattern(codeBase, codeSize, sigHasStats, "xxxxxxxxxxx");
    if (matchHasStats) {
        g_hasStandardStatsAddr = (DWORD)matchHasStats;
        // Read MicroModuleTemplates global pointer from offset +0x16
        g_mmTemplatesGlobalPtrAddr = *(DWORD*)(matchHasStats + 16);
        InstallJmpHook(g_hasStandardStatsAddr, (void*)Hook_TFuelTanks_HasStandardStats_Naked, 11, "TFuelTanks.HasStandardStats");
    }

    // 2. TFuelTanks.Init Hook at 0x006FD5C0
    const unsigned char sigFuelInit[] = {
        0x55, 0x8B, 0xEC, 0x53, 0x56, 0x8B, 0xF0, 0xC6, 0x46, 0x0C, 0x2B, 0x89, 0x56, 0x18
    };
    uintptr_t matchInit = FindPattern(codeBase, codeSize, sigFuelInit, "xxxxxxxxxxxxxx");
    if (matchInit) {
        // Epilogue is at matchInit + 0x3B (0x006FD5FB)
        g_fuelInitExitAddr = (DWORD)(matchInit + 0x3B);
        InstallJmpHook(g_fuelInitExitAddr, (void*)Hook_TFuelTanks_Init_Exit_Naked, 9, "TFuelTanks.Init Exit");

        // 2a. TFuelTanks.SaveToBuffer -> TEquipment.SaveToBuffer call at matchInit + 0x52 (0x006FD612):
        for (uintptr_t p = matchInit + 0x45; p < matchInit + 0x70; p++) {
            if (*(BYTE*)p == 0xE8) {
                int rel = *(int*)(p + 1);
                uintptr_t target = (p + 5) + rel;
                if (target > matchInit - 0x10000 && target < matchInit) {
                    g_origEquipmentSaveToBufferAddr = (DWORD)target;
                    InstallCallRedirect((DWORD)p, (void*)Hook_TFuelTanks_SaveToBuffer_InheritedCall, "TFuelTanks.SaveToBuffer -> TEquipment.SaveToBuffer");
                    break;
                }
            }
        }

        // 2b. TFuelTanks.LoadFromBuffer exit at matchInit + 0xA9 (0x006FD669):
        const unsigned char sigLoadExit[] = { 0x88, 0x43, 0x68, 0x5E, 0x5B, 0xC3 };
        uintptr_t matchLoadExit = FindPattern(matchInit + 0x80, 0x40, sigLoadExit, "xxxxxx");
        if (matchLoadExit) {
            InstallJmpHook((DWORD)matchLoadExit, (void*)Hook_TFuelTanks_LoadFromBuffer_Exit_Naked, 6, "TFuelTanks.LoadFromBuffer Exit");
        }
    }

    // 3. ApplySpecialMicroModule Exit Hook (with chaining across all AG mods!)
    const unsigned char sigApplySMMStart[] = {
        0x53, 0x56, 0x57, 0x55, 0x83, 0xC4, 0xBC, 0x8B, 0xDA, 0x8B, 0xF0, 0x83, 0xFE, 0xFF, 0x74, 0x0A, 0x85, 0xDB, 0x74, 0x06, 0x83, 0x7B, 0x54, 0x00, 0x74, 0x0F
    };
    uintptr_t matchSMMStart = FindPattern(codeBase, codeSize, sigApplySMMStart, "xxxxxxxxxxxxxxxxxxxxxxxxxx");
    if (matchSMMStart) {
        uintptr_t epilogueAddr = matchSMMStart + 0x22C;
        if (*(BYTE*)epilogueAddr == 0xE9) {
            int rel = *(int*)(epilogueAddr + 1);
            g_prevSMMHook = (DWORD)((epilogueAddr + 5) + rel);
            Log("[HOOK] ApplySpecialMicroModule chained to previous hook at 0x%08X\n", g_prevSMMHook);
        }
        g_applySMMExitAddr = (DWORD)epilogueAddr;
        InstallJmpHook(g_applySMMExitAddr, (void*)Hook_ApplySpecialMicroModule_Exit_Naked, 8, "ApplySpecialMicroModule Exit");
    }

    // 4. TFuelTanks.CalculateCapacity at 0x006FD978
    const unsigned char sigCalcCap[] = {
        0x53, 0x8B, 0xD8, 0x0F, 0xB6, 0x53, 0x60, 0x8B, 0x43, 0x18, 0xE8
    };
    uintptr_t matchCalcCap = FindPattern(codeBase, codeSize, sigCalcCap, "xxxxxxxxxxx");
    if (matchCalcCap) {
        g_calcGeneratedCapacityAddr = (DWORD)matchCalcCap;
        InstallJmpHook(g_calcGeneratedCapacityAddr, (void*)Hook_CalculateFuelCapacity_Naked, 17, "TFuelTanks.CalculateCapacity");
    }

    s_installed = TRUE;
    Log("=== AG_FuelTanks v1.1 hooks installed successfully (%u patches recorded) ===\n", (unsigned int)s_patchCount);
}

extern "C" __declspec(dllexport) void __stdcall AG_FuelTanks_Init() {
    InstallFuelTanksHooks();
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
    if (fdwReason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hinstDLL);
        InstallFuelTanksHooks();
    } else if (fdwReason == DLL_PROCESS_DETACH) {
        if (lpvReserved == NULL && s_installed) {
            RestoreAllPatches();
        }
    }
    return TRUE;
}
