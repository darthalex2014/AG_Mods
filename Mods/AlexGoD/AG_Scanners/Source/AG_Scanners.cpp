#include <windows.h>
#include <oleauto.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma comment(lib, "OleAut32.lib")

// ======================================================================
// AG_Scanners v1.0 (Space Rangers HD: Universe / Evolution EvoBalance)
// Author: AlexGoD
// ======================================================================

static void Log(const char* fmt, ...) {
    FILE* f = fopen("AG_Scanners_log.txt", "a");
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

#define CLR_MK      L"<color=255,220,50>"   // Золотисто-жёлтый (Mk.1 .. Mk.8)
#define CLR_M       L"<color=230,240,255>"  // Белый / серебро (T)
#define CLR_MKM     L"<color=70,255,90>"    // Кислотно-зелёный (Mk.1T .. Mk.8T)
#define CLR_MK9     L"<color=215,90,255>"   // Фиолетовый (Mk.9)
#define CLR_MK9M    L"<color=255,60,60>"    // Алый / красный (Mk.9T)
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
    Log("=== AG_Scanners v1.0 hooks cleanly uninstalled (original bytes restored) ===\n");
}

// Stubs for ScriptLibs compatibility
extern "C" __declspec(dllexport) void __stdcall AG_Scanners_Save() {}
extern "C" __declspec(dllexport) void __stdcall AG_Scanners_Load() {}

// ======================================================================
// Canonical Russian Tier Names (EvoBalance / Universe)
// ======================================================================

static const wchar_t* s_tierNames[10] = {
    L"",
    L"Трассионный",
    L"Вортексный",
    L"Нейрощупный",
    L"Молекулярный",
    L"Коллоидный",
    L"Текторный",
    L"Деатомарный",
    L"Кванторный",
    L"Реликтовый"
};

// ======================================================================
// Model Specification Tables (Power % & Prices)
// Power % corresponds to penetration through DefGenerator shields!
// ======================================================================

// Ваниль EvoBalance: 11%, 14%, 17%, 20%, 24%, 28%, 32%, 36% (Базовый Т9 = 40%)
static const int s_vanillaPower[10] = { 0, 11, 14, 17, 20, 24, 28, 32, 36, 40 };
static const int s_base9Cost        = 44000;

// Линейка Mk (Легкие 15-29т): 12%, 15%, 18%, 21%, 25%, 29%, 33%, 37%, 42%
static const int s_mkPower[10]      = { 0, 12, 15, 18, 21, 25, 29, 33, 37, 42 };
static const int s_mkBaseCost[10]   = { 0, 1250, 3250, 6250, 10250, 15250, 21250, 28250, 36250, 50000 };

// Линейка T (Тяжелые силовые 30-50т): 12%, 15%, 18%, 22%, 26%, 30%, 34%, 38%, 41%
static const int s_tPower[10]       = { 0, 12, 15, 18, 22, 26, 30, 34, 38, 41 };
static const int s_tBaseCost[10]    = { 0, 1100, 2900, 5800, 9800, 14500, 20500, 27500, 35500, 46000 };

// Линейка Mk.T (Флагманские гибриды 30-50т): 13%, 16%, 19%, 23%, 27%, 31%, 35%, 39%, 43%
static const int s_mktPower[10]     = { 0, 13, 16, 19, 23, 27, 31, 35, 39, 43 };
static const int s_mktBaseCost[10]  = { 0, 1600, 3800, 7200, 11800, 17000, 23500, 31000, 40000, 58000 };

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

static int CalculateCustomScanerCost(int weight, int baseCost, int owner) {
    if (weight <= 0) weight = 30;
    double ratio = 30.0 / (double)weight;
    if (ratio < 0.5) ratio = 0.5;
    if (ratio > 2.0) ratio = 2.0;

    double weightFactor = 1.0 + (ratio - 0.5) / 1.5;
    double ownerFactor = (double)GetOwnerPriceFactor(owner);

    int cost = (int)(weightFactor * (double)baseCost * ownerFactor + 0.5);
    cost = (cost / 10) * 10;
    if (cost < 100) cost = 100;
    return cost;
}

static int IdentifyCustomModelByConfig(const wchar_t* configBlock, int* outLevel) {
    if (!configBlock || !configBlock[0]) return 0; // 0 = Vanilla

    if (wcscmp(configBlock, L"ScanerBase9") == 0) {
        if (outLevel) *outLevel = 9;
        return 4; // Base T9
    }
    if (wcscmp(configBlock, L"ScanerMk9T") == 0) {
        if (outLevel) *outLevel = 9;
        return 3; // Mk.9T
    }
    if (wcsncmp(configBlock, L"ScanerMkT", 9) == 0) {
        int lvl = configBlock[9] - L'0';
        if (lvl >= 1 && lvl <= 8) {
            if (outLevel) *outLevel = lvl;
            return 3; // Mk.XT
        }
    }
    if (wcscmp(configBlock, L"ScanerMk9") == 0) {
        if (outLevel) *outLevel = 9;
        return 1; // Mk.9
    }
    if (wcsncmp(configBlock, L"ScanerMk", 8) == 0) {
        int lvl = configBlock[8] - L'0';
        if (lvl >= 1 && lvl <= 8) {
            if (outLevel) *outLevel = lvl;
            return 1; // Mk
        }
    }
    if (wcscmp(configBlock, L"ScanerT9") == 0) {
        if (outLevel) *outLevel = 9;
        return 2; // T-9
    }
    if (wcsncmp(configBlock, L"ScanerT", 7) == 0 && configBlock[7] >= L'1' && configBlock[7] <= L'8') {
        int lvl = configBlock[7] - L'0';
        if (lvl >= 1 && lvl <= 8) {
            if (outLevel) *outLevel = lvl;
            return 2; // T
        }
    }

    return 0;
}

static bool EndsWithWord(const wchar_t* str, size_t strLen, const wchar_t* word, size_t wordLen) {
    if (strLen >= wordLen && wcsncmp(str + strLen - wordLen, word, wordLen) == 0) {
        return true;
    }
    const wchar_t* tagEnd = L"</color>";
    size_t tagLen = 8;
    if (strLen >= wordLen + tagLen && 
        wcsncmp(str + strLen - tagLen, tagEnd, tagLen) == 0 &&
        wcsncmp(str + strLen - tagLen - wordLen, word, wordLen) == 0) {
        return true;
    }
    return false;
}

static int IdentifyCustomModelByNameOverride(const wchar_t* nameOverride, int techLevel, int* outLevel) {
    if (!nameOverride || !nameOverride[0]) return 0;
    size_t len = wcslen(nameOverride);

    // Mk.XT
    for (int lvl = 1; lvl <= 9; lvl++) {
        wchar_t patT[16];
        swprintf(patT, 16, L"Mk.%dT", lvl);
        if (EndsWithWord(nameOverride, len, patT, wcslen(patT))) {
            if (outLevel) *outLevel = lvl;
            return 3;
        }
    }

    // Mk.X
    for (int lvl = 1; lvl <= 9; lvl++) {
        wchar_t pat[16];
        swprintf(pat, 16, L"Mk.%d", lvl);
        if (EndsWithWord(nameOverride, len, pat, wcslen(pat))) {
            if (outLevel) *outLevel = lvl;
            return 1;
        }
    }

    // T
    if (EndsWithWord(nameOverride, len, L" T", 2) || EndsWithWord(nameOverride, len, L"T", 1)) {
        if (techLevel >= 1 && techLevel <= 9) {
            if (outLevel) *outLevel = techLevel;
            return 2;
        }
    }

    // Base T9 Реликтовый сканер (без суффиксов Mk/T)
    if (wcsstr(nameOverride, L"Реликтовый сканер") != NULL) {
        if (outLevel) *outLevel = 9;
        return 4;
    }

    return 0;
}

static int IdentifyCustomModelFromItem(void* item, int* outLevel) {
    if (!item) return 0;
    int lvl = *(unsigned char*)((char*)item + 0x60);
    if (outLevel) *outLevel = lvl;

    const wchar_t* configBlock = GetItemConfigBlockName(item);
    int kind = IdentifyCustomModelByConfig(configBlock, outLevel);
    if (kind != 0) return kind;

    const wchar_t* nameOverride = GetItemNameOverride(item);
    return IdentifyCustomModelByNameOverride(nameOverride, lvl, outLevel);
}

static int __fastcall GetModelExpectedPowerFastcall(void* item) {
    if (!item) return 11;
    __try {
        if (*(BYTE*)((char*)item + 0x0C) != 46) return 11; // item_scaner only
        int lvl = *(unsigned char*)((char*)item + 0x60);
        int modelLvl = lvl;
        int modelKind = IdentifyCustomModelFromItem(item, &modelLvl);

        if (modelKind == 1 && modelLvl >= 1 && modelLvl <= 9) return s_mkPower[modelLvl];
        if (modelKind == 2 && modelLvl >= 1 && modelLvl <= 9) return s_tPower[modelLvl];
        if (modelKind == 3 && modelLvl >= 1 && modelLvl <= 9) return s_mktPower[modelLvl];
        if (modelKind == 4) return s_vanillaPower[9];

        if (lvl >= 1 && lvl <= 8) return s_vanillaPower[lvl];
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 11;
    }
    return 11;
}

// ======================================================================
// Target Addresses & Hooks State
// ======================================================================

static DWORD g_scanerInitExitAddr            = 0;
static DWORD g_scanerCalcPowerAddr           = 0;
static DWORD g_applySMMExitAddr              = 0;
static DWORD g_prevSMMHook                   = 0;
static DWORD g_mmTemplatesGlobalPtrAddr      = 0x008136B4;
static DWORD g_origEquipmentSaveToBufferAddr = 0;
static DWORD g_readByteFromBufferAddr        = 0x007B81F4;
static DWORD g_scanerLoadReturnAddr          = 0;

// ======================================================================
// Helper: Check if Scaner has an Acryn with a unique 3D model (KindGraph)
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
        if (*(BYTE*)((char*)item + 0x0C) != 46) return; // item_scaner only

        int special = *(int*)((char*)item + 0x54);
        if (special <= 0) return;

        int lvl = 0;
        int kind = IdentifyCustomModelFromItem(item, &lvl);
        if (kind == 0) return;

        BSTR acrynName = NULL;
        BSTR kindGraph = NULL;
        bool hasCustomModel = HasAcrynCustomModel(item, &acrynName, &kindGraph);

        if (acrynName && !IsBadStringPtrW(acrynName, 128) && acrynName[0] != L'\0') {
            const wchar_t* clrTag = CLR_MK;
            wchar_t suffix[32] = {0};

            if (kind == 1) { // Mk (Жёлтый)
                clrTag = CLR_MK;
                swprintf(suffix, 32, L"Mk.%d", lvl);
            } else if (kind == 2) { // T (Белый)
                clrTag = CLR_M;
                swprintf(suffix, 32, L"T");
            } else if (kind == 3) { // Mk.T (Зелёный)
                clrTag = CLR_MKM;
                swprintf(suffix, 32, L"Mk.%dT", lvl);
            }

            wchar_t fullAcrynName[256];
            if (suffix[0]) {
                swprintf(fullAcrynName, 256, L"%s%s%s %s%s%s", 
                         CLR_ACRYN, (const wchar_t*)acrynName, CLR_END, 
                         clrTag, suffix, CLR_END);
            } else {
                swprintf(fullAcrynName, 256, L"%s%s%s", 
                         CLR_ACRYN, (const wchar_t*)acrynName, CLR_END);
            }

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
            *pBstr = NULL; // Hide from TEquipment.SaveToBuffer
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

__declspec(naked) void Hook_TScaner_SaveToBuffer_InheritedCall() {
    __asm {
        push ebx
        push esi
        mov ebx, eax                  // Self (TScaner*)
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

extern "C" void __stdcall PostLoadScanerFromBuffer_C(void* item) {
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
            wchar_t cfgBlock[32] = {0};
            if (kind == 1) {
                swprintf(cfgBlock, 32, L"ScanerMk%d", lvl);
            } else if (kind == 2) {
                swprintf(cfgBlock, 32, L"ScanerT%d", lvl);
            } else if (kind == 3) {
                if (lvl == 9) wcscpy(cfgBlock, L"ScanerMk9T");
                else swprintf(cfgBlock, 32, L"ScanerMkT%d", lvl);
            } else if (kind == 4) {
                wcscpy(cfgBlock, L"ScanerBase9");
            }
            if (cfgBlock[0]) {
                SetItemConfigBlockName(item, cfgBlock);
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("[LOAD ERROR] Exception in PostLoadScanerFromBuffer_C\n");
    }
}

__declspec(naked) void Hook_TScaner_LoadFromBuffer_Naked() {
    __asm {
        mov eax, esi
        call dword ptr [g_readByteFromBufferAddr]
        mov byte ptr [ebx + 0x61], al

        pushad
        push ebx
        call PostLoadScanerFromBuffer_C
        popad

        jmp dword ptr [g_scanerLoadReturnAddr]
    }
}

// ======================================================================
// Fast Uniform Random Number Generator
// ======================================================================

static unsigned int s_randSeed = 246813579;
static unsigned int ModRandom(unsigned int minVal, unsigned int maxVal) {
    if (maxVal <= minVal) return minVal;
    s_randSeed = s_randSeed * 1664525u + 1013904223u;
    return minVal + (s_randSeed % (maxVal - minVal + 1));
}

// ======================================================================
// Post-Init Hook: Sets canonical tier names + colored series suffixes!
// ======================================================================

extern "C" void __stdcall PostProcessScanerInit_C(void* item, DWORD callerAddr, int weight, int level, int owner) {
    if (!item) return;

    level = level & 0xFF;
    if (level < 1) level = 1;
    if (level > 8) level = 8;

    // Skip TShip equipment calls (NPCs, warriors, pirates, stations)
    if (callerAddr == 0x006E15BA) return;
    // Skip Player start bonus
    if (callerAddr == 0x005BF719) return;

    const wchar_t* tierName = s_tierNames[level];

    if (weight < 30) {
        if (level == 8) {
            int roll = ModRandom(1, 100);
            if (roll <= 74) {
                return; // 74% Ванильный Кванторный легкий Т8
            } else if (roll <= 98) {
                // 24% Mk.8 (Жёлтый, 37% силы)
                wchar_t fullName[160];
                swprintf(fullName, 160, L"%s сканер " CLR_MK L"Mk.8" CLR_END, tierName);
                SetItemConfigBlockName(item, L"ScanerMk8");
                SetItemNameOverride(item, fullName);
                *(BYTE*)((char*)item + 0x61) = (BYTE)s_mkPower[8];
                *(int*)((char*)item + 0x20) = CalculateCustomScanerCost(weight, s_mkBaseCost[8], owner);
            } else if (roll == 99) {
                // 1% Базовый Т9 Реликтовый сканер (40% силы)
                SetItemConfigBlockName(item, L"ScanerBase9");
                SetItemNameOverride(item, L"Реликтовый сканер");
                *(BYTE*)((char*)item + 0x61) = (BYTE)s_vanillaPower[9];
                *(int*)((char*)item + 0x20) = CalculateCustomScanerCost(weight, s_base9Cost, owner);
            } else {
                // 1% Реликтовый Mk.9 (Жёлтый, 42% силы)
                SetItemConfigBlockName(item, L"ScanerMk9");
                SetItemNameOverride(item, L"Реликтовый сканер " CLR_MK L"Mk.9" CLR_END);
                *(BYTE*)((char*)item + 0x61) = (BYTE)s_mkPower[9];
                *(int*)((char*)item + 0x20) = CalculateCustomScanerCost(weight, s_mkBaseCost[9], owner);
            }
        } else {
            // Линейка Mk для Т1..Т7 (25% шанс среди легких)
            if (ModRandom(1, 100) > 75) {
                wchar_t cfgBlock[32];
                wchar_t fullName[160];
                swprintf(cfgBlock, 32, L"ScanerMk%d", level);
                swprintf(fullName, 160, L"%s сканер " CLR_MK L"Mk.%d" CLR_END, tierName, level);

                SetItemConfigBlockName(item, cfgBlock);
                SetItemNameOverride(item, fullName);
                *(BYTE*)((char*)item + 0x61) = (BYTE)s_mkPower[level];
                *(int*)((char*)item + 0x20) = CalculateCustomScanerCost(weight, s_mkBaseCost[level], owner);
            }
        }
    } else {
        if (level == 8) {
            // Тяжёлый сектор Т8: 50% ваниль, 50% кастомные тяжёлые
            if (ModRandom(1, 100) > 50) {
                int roll = ModRandom(1, 100);
                if (roll <= 73) {
                    // 73% Серия T: T-8 (Белый, 38% силы)
                    wchar_t fullName[160];
                    swprintf(fullName, 160, L"%s сканер " CLR_M L"T" CLR_END, tierName);
                    SetItemConfigBlockName(item, L"ScanerT8");
                    SetItemNameOverride(item, fullName);
                    *(BYTE*)((char*)item + 0x61) = (BYTE)s_tPower[8];
                    *(int*)((char*)item + 0x20) = CalculateCustomScanerCost(weight, s_tBaseCost[8], owner);
                } else if (roll <= 96) {
                    // 23% Серия Mk.T: Mk.8T (Зелёный, 39% силы)
                    wchar_t fullName[160];
                    swprintf(fullName, 160, L"%s сканер " CLR_MKM L"Mk.8T" CLR_END, tierName);
                    SetItemConfigBlockName(item, L"ScanerMkT8");
                    SetItemNameOverride(item, fullName);
                    *(BYTE*)((char*)item + 0x61) = (BYTE)s_mktPower[8];
                    *(int*)((char*)item + 0x20) = CalculateCustomScanerCost(weight, s_mktBaseCost[8], owner);
                } else if (roll <= 98) {
                    // 2% Реликтовый T: T-9 (Белый, 41% силы)
                    SetItemConfigBlockName(item, L"ScanerT9");
                    SetItemNameOverride(item, L"Реликтовый сканер " CLR_M L"T" CLR_END);
                    *(BYTE*)((char*)item + 0x61) = (BYTE)s_tPower[9];
                    *(int*)((char*)item + 0x20) = CalculateCustomScanerCost(weight, s_tBaseCost[9], owner);
                } else {
                    // 2% Реликтовый Mk.9T (Зелёный флагман, 43% силы)
                    SetItemConfigBlockName(item, L"ScanerMk9T");
                    SetItemNameOverride(item, L"Реликтовый сканер " CLR_MKM L"Mk.9T" CLR_END);
                    *(BYTE*)((char*)item + 0x61) = (BYTE)s_mktPower[9];
                    *(int*)((char*)item + 0x20) = CalculateCustomScanerCost(weight, s_mktBaseCost[9], owner);
                }
            }
        } else {
            // Тяжёлый сектор для Т1..Т7: 50% ваниль, 50% кастомные
            if (ModRandom(1, 100) > 50) {
                if (ModRandom(1, 100) <= 75) {
                    // Линейка T (Белый цвет)
                    wchar_t cfgBlock[32];
                    wchar_t fullName[160];
                    swprintf(cfgBlock, 32, L"ScanerT%d", level);
                    swprintf(fullName, 160, L"%s сканер " CLR_M L"T" CLR_END, tierName);

                    SetItemConfigBlockName(item, cfgBlock);
                    SetItemNameOverride(item, fullName);
                    *(BYTE*)((char*)item + 0x61) = (BYTE)s_tPower[level];
                    *(int*)((char*)item + 0x20) = CalculateCustomScanerCost(weight, s_tBaseCost[level], owner);
                } else {
                    // Линейка Mk.T (Зелёный цвет)
                    wchar_t cfgBlock[32];
                    wchar_t fullName[160];
                    swprintf(cfgBlock, 32, L"ScanerMkT%d", level);
                    swprintf(fullName, 160, L"%s сканер " CLR_MKM L"Mk.%dT" CLR_END, tierName, level);

                    SetItemConfigBlockName(item, cfgBlock);
                    SetItemNameOverride(item, fullName);
                    *(BYTE*)((char*)item + 0x61) = (BYTE)s_mktPower[level];
                    *(int*)((char*)item + 0x20) = CalculateCustomScanerCost(weight, s_mktBaseCost[level], owner);
                }
            }
        }
    }
}

__declspec(naked) void Hook_TScaner_Init_Exit_Naked() {
    __asm {
        mov [ebx + 0x20], eax
        xor eax, eax
        mov [ebx + 0x50], eax

        pushad
        movzx ecx, byte ptr [ebp + 8]
        push ecx
        movzx ecx, byte ptr [ebx + 0x60]
        push ecx
        mov ecx, [ebx + 0x18]
        push ecx
        mov ecx, [ebp + 4]
        push ecx
        push ebx
        call PostProcessScanerInit_C
        popad

        pop ebx
        pop ebp
        ret 4
    }
}

// ======================================================================
// Calculated Power Hook (Science Base / HasStandardStats / Tooltip)
// ======================================================================

__declspec(naked) void Hook_CalculateScanerPower_Naked() {
    __asm {
        push ebx
        mov ebx, eax
        mov ecx, eax
        call GetModelExpectedPowerFastcall
        pop ebx
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

void InstallScanerHooks() {
    if (s_installed) return;

    Log("=== AG_Scanners v1.0 initializing (EvoBalance + Savegame Safe + Acryn Support) ===\n");

    uintptr_t codeBase = 0;
    size_t codeSize = 0;
    if (!GetCodeSectionRange(&codeBase, &codeSize)) {
        Log("[ERROR] Failed to obtain code section range\n");
        return;
    }

    // 1. Resolve MicroModuleTemplates global pointer from TScaner.HasStandardStats
    const unsigned char sigHasStats[] = {
        0x53, 0x56, 0x8B, 0xD8, 0x8B, 0x73, 0x50, 0x85, 0xF6, 0x74, 0x15, 0x6B, 0xC6, 0x49, 0x8B, 0x15
    };
    uintptr_t matchHasStats = FindPattern(codeBase, codeSize, sigHasStats, "xxxxxxxxxxxxxxxx");
    if (matchHasStats) {
        g_mmTemplatesGlobalPtrAddr = *(DWORD*)(matchHasStats + 16);
    }

    // 2. TScaner.Init Start Hook (unique signature)
    const unsigned char sigScanerInit[] = {
        0x55, 0x8B, 0xEC, 0x53, 0x8B, 0xD8, 0xC6, 0x43, 0x0C, 0x2E, 0x89, 0x53, 0x18, 0x88, 0x4B, 0x60
    };
    uintptr_t matchInit = FindPattern(codeBase, codeSize, sigScanerInit, "xxxxxxxxxxxxxxxx");
    if (matchInit) {
        // Epilogue is at matchInit + 0x2F
        g_scanerInitExitAddr = (DWORD)(matchInit + 0x2F);
        InstallJmpHook(g_scanerInitExitAddr, (void*)Hook_TScaner_Init_Exit_Naked, 8, "TScaner.Init Exit");

        // 2a. TScaner.SaveToBuffer -> TEquipment.SaveToBuffer at ~matchInit + 0x46:
        for (uintptr_t p = matchInit + 0x3C; p < matchInit + 0x55; p++) {
            if (*(BYTE*)p == 0xE8) {
                int rel = *(int*)(p + 1);
                uintptr_t target = (p + 5) + rel;
                if (target > matchInit - 0x10000 && target < matchInit) {
                    g_origEquipmentSaveToBufferAddr = (DWORD)target;
                    InstallCallRedirect((DWORD)p, (void*)Hook_TScaner_SaveToBuffer_InheritedCall, "TScaner.SaveToBuffer -> TEquipment.SaveToBuffer");
                    break;
                }
            }
        }

        // 2b. TScaner.LoadFromBuffer at matchInit + 0x7D:
        uintptr_t loadFromBufAddr = matchInit + 0x64; // ~0x7002D0
        // Find 8B C6 E8 ?? ?? ?? ?? 88 43 61 at ~0x7002E9
        const unsigned char sigLoadPowerRead[] = { 0x8B, 0xC6, 0xE8, 0x00, 0x00, 0x00, 0x00, 0x88, 0x43, 0x61 };
        uintptr_t matchLoadPower = FindPattern(loadFromBufAddr, 0x30, sigLoadPowerRead, "xxx????xxx");
        if (matchLoadPower) {
            // Target readByte function
            int rel = *(int*)(matchLoadPower + 3);
            g_readByteFromBufferAddr = (DWORD)((matchLoadPower + 7) + rel);
            g_scanerLoadReturnAddr = (DWORD)(matchLoadPower + 10);
            InstallJmpHook((DWORD)matchLoadPower, (void*)Hook_TScaner_LoadFromBuffer_Naked, 10, "TScaner.LoadFromBuffer Power Read");
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

    // 4. TScaner.CalculatePower at 0x0070054C
    const unsigned char sigCalcScaner[] = {
        0x53, 0x8B, 0xD8, 0x0F, 0xB6, 0x43, 0x60, 0xE8
    };
    uintptr_t matchCalc = FindPattern(codeBase, codeSize, sigCalcScaner, "xxxxxxxx");
    if (matchCalc) {
        g_scanerCalcPowerAddr = (DWORD)matchCalc;
        InstallJmpHook(g_scanerCalcPowerAddr, (void*)Hook_CalculateScanerPower_Naked, 29, "TScaner.CalculatePower");
    }

    s_installed = TRUE;
    Log("=== AG_Scanners v1.0 hooks installed successfully (%u patches recorded) ===\n", (unsigned int)s_patchCount);
}

extern "C" __declspec(dllexport) void __stdcall AG_Scanners_Init() {
    InstallScanerHooks();
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
    if (fdwReason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hinstDLL);
        InstallScanerHooks();
    } else if (fdwReason == DLL_PROCESS_DETACH) {
        if (lpvReserved == NULL && s_installed) {
            RestoreAllPatches();
        }
    }
    return TRUE;
}
