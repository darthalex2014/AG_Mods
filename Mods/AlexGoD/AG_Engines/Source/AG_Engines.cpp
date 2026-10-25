#include <windows.h>
#include <oleauto.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma comment(lib, "OleAut32.lib")

// ======================================================================
// AG_Engines v3.4 (Space Rangers HD: Universe / Evolution EvoBalance)
// Author: AlexGoD
// ======================================================================

static void Log(const char* fmt, ...) {
    FILE* f = fopen("Z:\\21\\AG_Engines_log.txt", "a");
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
#define CLR_M       L"<color=230,240,255>"  // Белый / серебро (M)
#define CLR_MKM     L"<color=70,255,90>"    // Кислотно-зелёный (Mk.1M .. Mk.8M)
#define CLR_MK9     L"<color=215,90,255>"   // Фиолетовый (Mk.9)
#define CLR_MK9M    L"<color=255,60,60>"    // Алый / красный (Mk.9M)
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
    Log("=== AG_Engines v3.4 hooks cleanly uninstalled (original bytes restored) ===\n");
}

// Stubs for ScriptLibs compatibility
extern "C" __declspec(dllexport) void __stdcall AG_Engines_Save() {}
extern "C" __declspec(dllexport) void __stdcall AG_Engines_Load() {}

// ======================================================================
// Canonical Russian Tier Names (EvoBalance / Universe)
// ======================================================================

static const wchar_t* s_tierNames[10] = {
    L"",
    L"Нырковый",
    L"Сингулярный",
    L"Жабродюзный",
    L"Потоковый",
    L"Сплэшевый",
    L"Гравитонный",
    L"Стансерный",
    L"Темпоральный",
    L"Реликтовый"
};

// ======================================================================
// Model Specification Tables (T1 .. T8 + T9)
// ======================================================================

static const int s_vanillaSpeed[10] = { 0, 400, 450, 500, 600, 700, 800, 900, 1000, 1100 };
static const int s_vanillaJump[10]  = { 0,  17,  19,  21,  25,  29,  33,  37,   41,   45 };
static const int s_base9Cost        = 44000;

static const int s_mkSpeed[10]      = { 0, 425, 475, 550, 650, 750, 850, 950, 1050, 1150 };
static const int s_mkJump[10]       = { 0,  18,  20,  23,  27,  31,  35,  39,   42,   47 };
static const int s_mkBaseCost[10]   = { 0, 1250, 3250, 6250, 10250, 15250, 21250, 28250, 36250, 50000 };

static const int s_tSpeed[10]       = { 0, 430, 480, 560, 665, 765, 865, 965, 1035, 1140 };
static const int s_tJump[10]        = { 0,  18,  20,  23,  27,  31,  35,  39,   42,   46 };
static const int s_tBaseCost[10]    = { 0, 1100, 2900, 5800, 9800, 14500, 20500, 27500, 35500, 46000 };

static const int s_mktSpeed[10]     = { 0, 445, 495, 585, 690, 790, 890, 990, 1065, 1200 };
static const int s_mktJump[10]      = { 0,  18,  20,  24,  28,  32,  36,  40,   43,   49 };
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

static float GetOwnerFuelPriceFactor(int owner) {
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

static int CalculateCustomEngineCost(int weight, int baseCost, int owner) {
    if (weight <= 0) weight = 40;
    double ratio = 40.0 / (double)weight;
    if (ratio < 0.5) ratio = 0.5;
    if (ratio > 2.0) ratio = 2.0;

    double weightFactor = 1.0 + (ratio - 0.5) / 1.5;
    double ownerFactor = (double)GetOwnerFuelPriceFactor(owner);

    int cost = (int)(weightFactor * (double)baseCost * ownerFactor + 0.5);
    cost = (cost / 10) * 10;
    if (cost < 100) cost = 100;
    return cost;
}

static int IdentifyCustomModelByConfig(const wchar_t* configBlock, int* outLevel) {
    if (!configBlock || !configBlock[0]) return 0; // 0 = Vanilla

    if (wcscmp(configBlock, L"EngineBase9") == 0) {
        if (outLevel) *outLevel = 9;
        return 4; // Base T9
    }
    if (wcscmp(configBlock, L"EngineMk9T") == 0 || wcscmp(configBlock, L"EngineMk9MKM") == 0) {
        if (outLevel) *outLevel = 9;
        return 3; // Mk.9T
    }
    if (wcsncmp(configBlock, L"EngineMkT", 9) == 0) {
        int lvl = configBlock[9] - L'0';
        if (lvl >= 1 && lvl <= 8) {
            if (outLevel) *outLevel = lvl;
            return 3; // Mk.XT
        }
    }
    if (wcsncmp(configBlock, L"EngineMKM", 9) == 0) {
        int lvl = configBlock[9] - L'0';
        if (lvl >= 1 && lvl <= 8) {
            if (outLevel) *outLevel = lvl;
            return 3; // Legacy Mk.XM
        }
    }
    if (wcscmp(configBlock, L"EngineMk9") == 0) {
        if (outLevel) *outLevel = 9;
        return 1; // Mk.9
    }
    if (wcsncmp(configBlock, L"EngineMk", 8) == 0) {
        int lvl = configBlock[8] - L'0';
        if (lvl >= 1 && lvl <= 8) {
            if (outLevel) *outLevel = lvl;
            return 1; // Mk
        }
    }
    if (wcscmp(configBlock, L"EngineT9") == 0) {
        if (outLevel) *outLevel = 9;
        return 2; // T-9
    }
    if (wcsncmp(configBlock, L"EngineT", 7) == 0 && configBlock[7] >= L'1' && configBlock[7] <= L'8') {
        int lvl = configBlock[7] - L'0';
        if (outLevel) *outLevel = lvl;
        return 2; // T
    }
    if (wcsncmp(configBlock, L"EngineM", 7) == 0 && configBlock[7] >= L'1' && configBlock[7] <= L'8') {
        int lvl = configBlock[7] - L'0';
        if (outLevel) *outLevel = lvl;
        return 2; // Legacy M
    }

    return 0;
}

// Хелпер проверки окончания строки с учётом или без учёта закрывающего тега </color>
static bool EndsWithWord(const wchar_t* str, size_t strLen, const wchar_t* word, size_t wordLen) {
    if (strLen >= wordLen && wcsncmp(str + strLen - wordLen, word, wordLen) == 0) {
        return true;
    }
    // Проверка, если в конце стоит закрывающий тег </color>
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

    // Поиск вариантов Mk.XT и legacy Mk.XM
    for (int lvl = 1; lvl <= 9; lvl++) {
        wchar_t patT[16];
        swprintf(patT, 16, L"Mk.%dT", lvl);
        if (EndsWithWord(nameOverride, len, patT, wcslen(patT))) {
            if (outLevel) *outLevel = lvl;
            return 3;
        }
        wchar_t patM[16];
        swprintf(patM, 16, L"Mk.%dM", lvl);
        if (EndsWithWord(nameOverride, len, patM, wcslen(patM))) {
            if (outLevel) *outLevel = lvl;
            return 3;
        }
    }

    // Поиск вариантов Mk.X
    for (int lvl = 1; lvl <= 9; lvl++) {
        wchar_t pat[16];
        swprintf(pat, 16, L"Mk.%d", lvl);
        if (EndsWithWord(nameOverride, len, pat, wcslen(pat))) {
            if (outLevel) *outLevel = lvl;
            return 1;
        }
    }

    // Поиск варианта T или legacy M
    if (EndsWithWord(nameOverride, len, L" T", 2) || EndsWithWord(nameOverride, len, L"T", 1) ||
        EndsWithWord(nameOverride, len, L" M", 2) || EndsWithWord(nameOverride, len, L"M", 1)) {
        if (techLevel >= 1 && techLevel <= 9) {
            if (outLevel) *outLevel = techLevel;
            return 2;
        }
    }

    // Базовый Т9 Реликтовый двигатель (без суффиксов Mk/T)
    if (wcsstr(nameOverride, L"Реликтовый двигатель") != NULL) {
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

static int __fastcall GetModelExpectedSpeedFastcall(void* item) {
    if (!item) return 400;
    __try {
        if (*(BYTE*)((char*)item + 0x0C) != 44) return 400;
        int lvl = *(unsigned char*)((char*)item + 0x60);
        int modelLvl = lvl;
        int modelKind = IdentifyCustomModelFromItem(item, &modelLvl);

        if (modelKind == 1 && modelLvl >= 1 && modelLvl <= 9) return s_mkSpeed[modelLvl];
        if (modelKind == 2 && modelLvl >= 1 && modelLvl <= 9) return s_tSpeed[modelLvl];
        if (modelKind == 3 && modelLvl >= 1 && modelLvl <= 9) return s_mktSpeed[modelLvl];
        if (modelKind == 4) return s_vanillaSpeed[9];

        if (lvl >= 1 && lvl <= 8) return s_vanillaSpeed[lvl];
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 400;
    }
    return 400;
}

static int __fastcall GetModelExpectedJumpFastcall(void* item) {
    if (!item) return 17;
    __try {
        if (*(BYTE*)((char*)item + 0x0C) != 44) return 17;
        int lvl = *(unsigned char*)((char*)item + 0x60);
        int modelLvl = lvl;
        int modelKind = IdentifyCustomModelFromItem(item, &modelLvl);

        if (modelKind == 1 && modelLvl >= 1 && modelLvl <= 9) return s_mkJump[modelLvl];
        if (modelKind == 2 && modelLvl >= 1 && modelLvl <= 9) return s_tJump[modelLvl];
        if (modelKind == 3 && modelLvl >= 1 && modelLvl <= 9) return s_mktJump[modelLvl];
        if (modelKind == 4) return s_vanillaJump[9];

        if (lvl >= 1 && lvl <= 8) return s_vanillaJump[lvl];
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 17;
    }
    return 17;
}

// ======================================================================
// Target Addresses & Hooks State
// ======================================================================

static DWORD g_engineInitExitAddr            = 0;
static DWORD g_calcGeneratedSpeedAddr        = 0;
static DWORD g_calcGeneratedJumpAddr         = 0;
static DWORD g_hasStandardStatsAddr          = 0;
static DWORD g_applySMMExitAddr              = 0;
static DWORD g_prevSMMHook                   = 0;
static DWORD g_mmTemplatesGlobalPtrAddr      = 0x008136B4;

static DWORD g_origEquipmentSaveToBufferAddr = 0;
static DWORD g_engineLoadExitReturnAddr      = 0;

static DWORD g_tRuinsEngineCallAddr          = 0;
static DWORD g_tshipCreateEngineAddr         = 0;
static DWORD g_stationEngineReturn           = 0;

extern "C" {
    volatile LONG g_stationEngineInit = 0;
}

// ======================================================================
// Helper: Check if Engine has an Acryn with a unique 3D model (KindGraph)
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
        if (*(BYTE*)((char*)item + 0x0C) != 44) return; // item_engine only

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

            if (kind == 1) { // Mk (Жёлтый для 1-8 и 9)
                clrTag = CLR_MK;
                swprintf(suffix, 32, L"Mk.%d", lvl);
            } else if (kind == 2) { // T (Белый)
                clrTag = CLR_M;
                swprintf(suffix, 32, L"T");
            } else if (kind == 3) { // Mk.T (Зелёный для 1-8 и 9)
                clrTag = CLR_MKM;
                swprintf(suffix, 32, L"Mk.%dT", lvl);
            }

            // Итоговое имя: <оранжевый>Название Акрина</оранжевый> <цвет линейки>Суффикс</цвет>
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
            *pBstr = NULL; // Hide from TEquipment.SaveToBuffer so .sav has empty ConfigBlockName
            Log("[SAVE] Stripped ConfigBlockName '%S' from engine before saving\n", (const wchar_t*)saved);
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

__declspec(naked) void Hook_TEngine_SaveToBuffer_InheritedCall() {
    __asm {
        push ebx
        push esi
        mov ebx, eax                  // Self (TEngine*)
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

extern "C" void __stdcall PostLoadEngineFromBuffer_C(void* item) {
    if (!item) return;
    __try {
        int lvl = 0;
        int kind = IdentifyCustomModelFromItem(item, &lvl);
        if (kind == 0) return;

        int special = *(int*)((char*)item + 0x54);
        if (special > 0) {
            // Восстанавливаем корректную покраску и имя акрина
            PostApplySpecialMicroModule_C(item);
        }

        if (HasAcrynCustomModel(item, NULL, NULL)) {
            SetItemConfigBlockName(item, NULL);
        } else {
            wchar_t cfgBlock[32] = {0};
            if (kind == 1) {
                swprintf(cfgBlock, 32, L"EngineMk%d", lvl);
            } else if (kind == 2) {
                swprintf(cfgBlock, 32, L"EngineT%d", lvl);
            } else if (kind == 3) {
                if (lvl == 9) wcscpy(cfgBlock, L"EngineMk9T");
                else swprintf(cfgBlock, 32, L"EngineMkT%d", lvl);
            } else if (kind == 4) {
                wcscpy(cfgBlock, L"EngineBase9");
            }
            if (cfgBlock[0]) {
                SetItemConfigBlockName(item, cfgBlock);
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("[LOAD ERROR] Exception in PostLoadEngineFromBuffer_C\n");
    }
}

__declspec(naked) void Hook_TEngine_LoadFromBuffer_Exit_Naked() {
    __asm {
        mov [ebx + 0x69], al
        mov byte ptr [ebx + 0x0C], 0x2C

        pushad
        push ebx
        call PostLoadEngineFromBuffer_C
        popad

        jmp dword ptr [g_engineLoadExitReturnAddr]
    }
}

// ======================================================================
// Fast Uniform Random Number Generator
// ======================================================================

static unsigned int s_randSeed = 987654321;
static unsigned int ModRandom(unsigned int minVal, unsigned int maxVal) {
    if (maxVal <= minVal) return minVal;
    s_randSeed = s_randSeed * 1664525u + 1013904223u;
    return minVal + (s_randSeed % (maxVal - minVal + 1));
}

// ======================================================================
// Post-Init Hook: Sets canonical tier names + colored series suffixes!
// ======================================================================

extern "C" void __stdcall PostProcessEngineInit_C(void* item, DWORD callerAddr, int weight, int level, int owner) {
    if (!item) return;

    level = level & 0xFF;
    if (level < 1) level = 1;
    if (level > 8) level = 8;

    if (g_stationEngineInit > 0) return;
    if (callerAddr == 0x006E151A) return;
    if (callerAddr == 0x005BF68B) {
        Log("[START BONUS] Player starting bonus engine kept pristine native Level %d\n", level);
        return;
    }

    const wchar_t* tierName = s_tierNames[level];

    if (weight < 50) {
        if (level == 8) {
            int roll = ModRandom(1, 100);
            if (roll <= 74) {
                // 74% Ванильный Темпоральный легкий Т8 - оставляем штатно
                return;
            } else if (roll <= 98) {
                // 24% Mk.8 (Жёлтый)
                wchar_t fullName[160];
                swprintf(fullName, 160, L"%s двигатель " CLR_MK L"Mk.8" CLR_END, tierName);
                SetItemConfigBlockName(item, L"EngineMk8");
                SetItemNameOverride(item, fullName);
                *(int*)((char*)item + 0x64) = s_mkSpeed[8];
                *(signed char*)((char*)item + 0x68) = (signed char)s_mkJump[8];
                *(int*)((char*)item + 0x20) = CalculateCustomEngineCost(weight, s_mkBaseCost[8], owner);
            } else if (roll == 99) {
                // 1% Базовый Т9 Реликтовый (Алый титан)
                SetItemConfigBlockName(item, L"EngineBase9");
                SetItemNameOverride(item, L"Реликтовый двигатель");
                *(int*)((char*)item + 0x64) = s_vanillaSpeed[9];
                *(signed char*)((char*)item + 0x68) = (signed char)s_vanillaJump[9];
                *(int*)((char*)item + 0x20) = CalculateCustomEngineCost(weight, s_base9Cost, owner);
            } else {
                // 1% Реликтовый Mk.9 (Жёлтый)
                SetItemConfigBlockName(item, L"EngineMk9");
                SetItemNameOverride(item, L"Реликтовый двигатель " CLR_MK L"Mk.9" CLR_END);
                *(int*)((char*)item + 0x64) = s_mkSpeed[9];
                *(signed char*)((char*)item + 0x68) = (signed char)s_mkJump[9];
                *(int*)((char*)item + 0x20) = CalculateCustomEngineCost(weight, s_mkBaseCost[9], owner);
            }
        } else {
            // Линейка Mk для Т1..Т7 (25% шанс среди легких)
            if (ModRandom(1, 100) > 75) {
                wchar_t cfgBlock[32];
                wchar_t fullName[160];
                swprintf(cfgBlock, 32, L"EngineMk%d", level);
                swprintf(fullName, 160, L"%s двигатель " CLR_MK L"Mk.%d" CLR_END, tierName, level);

                SetItemConfigBlockName(item, cfgBlock);
                SetItemNameOverride(item, fullName);
                *(int*)((char*)item + 0x64) = s_mkSpeed[level];
                *(signed char*)((char*)item + 0x68) = (signed char)s_mkJump[level];
                *(int*)((char*)item + 0x20) = CalculateCustomEngineCost(weight, s_mkBaseCost[level], owner);
            }
        }
    } else {
        if (level == 8) {
            // Тяжёлый сектор Т8: 50% ванильный хлам, 50% кастомные тяжёлые
            if (ModRandom(1, 100) > 50) {
                int roll = ModRandom(1, 100);
                if (roll <= 73) {
                    // 73% Серия T: T-8 (Белый)
                    wchar_t fullName[160];
                    swprintf(fullName, 160, L"%s двигатель " CLR_M L"T" CLR_END, tierName);
                    SetItemConfigBlockName(item, L"EngineT8");
                    SetItemNameOverride(item, fullName);
                    *(int*)((char*)item + 0x64) = s_tSpeed[8];
                    *(signed char*)((char*)item + 0x68) = (signed char)s_tJump[8];
                    *(int*)((char*)item + 0x20) = CalculateCustomEngineCost(weight, s_tBaseCost[8], owner);
                } else if (roll <= 96) {
                    // 23% Серия Mk.T: Mk.8T (Зелёный)
                    wchar_t fullName[160];
                    swprintf(fullName, 160, L"%s двигатель " CLR_MKM L"Mk.8T" CLR_END, tierName);
                    SetItemConfigBlockName(item, L"EngineMkT8");
                    SetItemNameOverride(item, fullName);
                    *(int*)((char*)item + 0x64) = s_mktSpeed[8];
                    *(signed char*)((char*)item + 0x68) = (signed char)s_mktJump[8];
                    *(int*)((char*)item + 0x20) = CalculateCustomEngineCost(weight, s_mktBaseCost[8], owner);
                } else if (roll <= 98) {
                    // 2% Реликтовый T: T-9 (Белый)
                    SetItemConfigBlockName(item, L"EngineT9");
                    SetItemNameOverride(item, L"Реликтовый двигатель " CLR_M L"T" CLR_END);
                    *(int*)((char*)item + 0x64) = s_tSpeed[9];
                    *(signed char*)((char*)item + 0x68) = (signed char)s_tJump[9];
                    *(int*)((char*)item + 0x20) = CalculateCustomEngineCost(weight, s_tBaseCost[9], owner);
                } else {
                    // 2% Реликтовый Mk.9T (Зелёный дредноут)
                    SetItemConfigBlockName(item, L"EngineMk9T");
                    SetItemNameOverride(item, L"Реликтовый двигатель " CLR_MKM L"Mk.9T" CLR_END);
                    *(int*)((char*)item + 0x64) = s_mktSpeed[9];
                    *(signed char*)((char*)item + 0x68) = (signed char)s_mktJump[9];
                    *(int*)((char*)item + 0x20) = CalculateCustomEngineCost(weight, s_mktBaseCost[9], owner);
                }
            }
        } else {
            // Тяжёлый сектор для Т1..Т7: 50% ванильный хлам, 50% кастомные
            if (ModRandom(1, 100) > 50) {
                if (ModRandom(1, 100) <= 75) {
                    // Линейка T (Белый цвет, бывшая M)
                    wchar_t cfgBlock[32];
                    wchar_t fullName[160];
                    swprintf(cfgBlock, 32, L"EngineT%d", level);
                    swprintf(fullName, 160, L"%s двигатель " CLR_M L"T" CLR_END, tierName);

                    SetItemConfigBlockName(item, cfgBlock);
                    SetItemNameOverride(item, fullName);
                    *(int*)((char*)item + 0x64) = s_tSpeed[level];
                    *(signed char*)((char*)item + 0x68) = (signed char)s_tJump[level];
                    *(int*)((char*)item + 0x20) = CalculateCustomEngineCost(weight, s_tBaseCost[level], owner);
                } else {
                    // Линейка Mk.T (Зелёный цвет, бывшая MKM)
                    wchar_t cfgBlock[32];
                    wchar_t fullName[160];
                    swprintf(cfgBlock, 32, L"EngineMkT%d", level);
                    swprintf(fullName, 160, L"%s двигатель " CLR_MKM L"Mk.%dT" CLR_END, tierName, level);

                    SetItemConfigBlockName(item, cfgBlock);
                    SetItemNameOverride(item, fullName);
                    *(int*)((char*)item + 0x64) = s_mktSpeed[level];
                    *(signed char*)((char*)item + 0x68) = (signed char)s_mktJump[level];
                    *(int*)((char*)item + 0x20) = CalculateCustomEngineCost(weight, s_mktBaseCost[level], owner);
                }
            }
        }
    }
}

__declspec(naked) void Hook_TEngine_Init_Exit_Naked() {
    __asm {
        mov [ebx + 0x20], eax

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
        call PostProcessEngineInit_C
        popad

        pop ebx
        pop ebp
        ret 4
    }
}

// ======================================================================
// Generated Speed/Jump Hooks
// ======================================================================

__declspec(naked) void Hook_CalculateGeneratedSpeed_Naked() {
    __asm {
        push ebx
        push esi
        push edi
        mov ecx, eax
        call GetModelExpectedSpeedFastcall
        pop edi
        pop esi
        pop ebx
        ret
    }
}

__declspec(naked) void Hook_CalculateGeneratedJumpRange_Naked() {
    __asm {
        push ebx
        push esi
        push edi
        mov ecx, eax
        call GetModelExpectedJumpFastcall
        pop edi
        pop esi
        pop ebx
        ret
    }
}

// ======================================================================
// Station TRuins Call Wrapper
// ======================================================================

__declspec(naked) void Hook_TRuins_Engine_Call() {
    __asm {
        pushad
        mov eax, [esp + 32]
        mov [g_stationEngineReturn], eax
        popad

        pushfd
        inc dword ptr [g_stationEngineInit]
        popfd

        mov dword ptr [esp], offset station_engine_return
        jmp dword ptr [g_tshipCreateEngineAddr]

    station_engine_return:
        pushad
        pushfd
        dec dword ptr [g_stationEngineInit]
        popfd
        popad
        jmp dword ptr [g_stationEngineReturn]
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

void InstallEngineHooks() {
    if (s_installed) return;

    Log("=== AG_Engines v3.4 initializing (Colored Suffixes + Savegame Safe + Acryn Support) ===\n");

    uintptr_t codeBase = 0;
    size_t codeSize = 0;
    if (!GetCodeSectionRange(&codeBase, &codeSize)) {
        Log("[ERROR] Failed to obtain code section range\n");
        return;
    }

    // 1. Resolve MicroModuleTemplates global pointer from TEngine.HasStandardStats
    const unsigned char sigHasStats[] = {
        0x53, 0x56, 0x57, 0x8B, 0xF8, 0x8B, 0x77, 0x50, 0x85, 0xF6, 0x74, 0x15, 0x6B, 0xC6, 0x49,
        0x8B, 0x15, 0x00, 0x00, 0x00, 0x00, 0x8B, 0x12, 0x83, 0xBC, 0x82, 0xF8, 0xFE, 0xFF, 0xFF, 0x00
    };
    g_hasStandardStatsAddr = (DWORD)FindPattern(codeBase, codeSize, sigHasStats, "xxxxxxxxxxxxxxxxx????xxxxxxxxxx");
    if (g_hasStandardStatsAddr) {
        g_mmTemplatesGlobalPtrAddr = *(DWORD*)(g_hasStandardStatsAddr + 17);
    }

    // 2. TEngine.Init Epilogue Hook at 0x006FE246
    const unsigned char sigEngineInitExit[] = {
        0x8B, 0xC3, 0xE8, 0x00, 0x00, 0x00, 0x00, 0x89, 0x43, 0x20, 0x5B, 0x5D, 0xC2, 0x04, 0x00
    };
    uintptr_t matchExit = FindPattern(codeBase, codeSize, sigEngineInitExit, "xxx????xxxxxxxx");
    if (matchExit) {
        g_engineInitExitAddr = (DWORD)(matchExit + 7);
        InstallJmpHook(g_engineInitExitAddr, (void*)Hook_TEngine_Init_Exit_Naked, 8, "TEngine.Init Exit");

        // 2a. TEngine.SaveToBuffer -> TEquipment.SaveToBuffer (at ~0x006FE250):
        for (uintptr_t p = g_engineInitExitAddr + 8; p < g_engineInitExitAddr + 0x28; p++) {
            if (*(BYTE*)p == 0xE8) {
                int rel = *(int*)(p + 1);
                uintptr_t target = (p + 5) + rel;
                if (target > g_engineInitExitAddr - 0x10000 && target < g_engineInitExitAddr) {
                    g_origEquipmentSaveToBufferAddr = (DWORD)target;
                    InstallCallRedirect((DWORD)p, (void*)Hook_TEngine_SaveToBuffer_InheritedCall, "TEngine.SaveToBuffer -> TEquipment.SaveToBuffer");
                    break;
                }
            }
        }
        if (!g_origEquipmentSaveToBufferAddr) {
            Log("[ERROR] Failed to locate TEngine.SaveToBuffer -> TEquipment.SaveToBuffer call\n");
        }

        // 2b. TEngine.LoadFromBuffer exit (at ~0x006FE2E0):
        const unsigned char sigLoadExit[] = { 0x88, 0x43, 0x69, 0xC6, 0x43, 0x0C, 0x2C };
        uintptr_t matchLoadExit = FindPattern(g_engineInitExitAddr + 0x30, 0x90, sigLoadExit, "xxxxxxx");
        if (matchLoadExit) {
            g_engineLoadExitReturnAddr = (DWORD)(matchLoadExit + 7);
            InstallJmpHook((DWORD)matchLoadExit, (void*)Hook_TEngine_LoadFromBuffer_Exit_Naked, 7, "TEngine.LoadFromBuffer Exit");
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

    // 4. TEngine.CalculateGeneratedSpeed
    const unsigned char sigCalcSpeed[] = {
        0x0F, 0xB6, 0x40, 0x60, 0x8B, 0x15, 0x00, 0x00, 0x00, 0x00, 0x0F, 0xB7, 0x44, 0x82, 0xFC, 0xC3
    };
    g_calcGeneratedSpeedAddr = (DWORD)FindPattern(codeBase, codeSize, sigCalcSpeed, "xxxxxx????xxxxxx");
    if (g_calcGeneratedSpeedAddr) {
        InstallJmpHook(g_calcGeneratedSpeedAddr, (void*)Hook_CalculateGeneratedSpeed_Naked, 16, "CalculateGeneratedSpeed");
    }

    // 5. TEngine.CalculateGeneratedJumpRange
    const unsigned char sigCalcJump[] = {
        0x0F, 0xB6, 0x40, 0x60, 0x8B, 0x15, 0x00, 0x00, 0x00, 0x00, 0x0F, 0xB6, 0x44, 0x82, 0xFE, 0xC3
    };
    g_calcGeneratedJumpAddr = (DWORD)FindPattern(codeBase, codeSize, sigCalcJump, "xxxxxx????xxxxxx");
    if (g_calcGeneratedJumpAddr) {
        InstallJmpHook(g_calcGeneratedJumpAddr, (void*)Hook_CalculateGeneratedJumpRange_Naked, 16, "CalculateGeneratedJumpRange");
    }

    // 6. TRuins station engine call isolation
    const unsigned char sigTRuinsEngine[] = {
        0xB1, 0x01, 0x8B, 0x45, 0xFC, 0xE8, 0x00, 0x00, 0x00, 0x00, 0x53, 0x8B, 0x45, 0xFC, 0x8D, 0x88, 0xE4, 0x00, 0x00, 0x00
    };
    uintptr_t matchTRuinsEngine = FindPattern(codeBase, codeSize, sigTRuinsEngine, "xxxxxx????xxxxxxxxxx");
    if (matchTRuinsEngine) {
        g_tRuinsEngineCallAddr = (DWORD)(matchTRuinsEngine + 5);
        DWORD rel = *(DWORD*)(g_tRuinsEngineCallAddr + 1);
        g_tshipCreateEngineAddr = g_tRuinsEngineCallAddr + 5 + rel;
        InstallCallRedirect(g_tRuinsEngineCallAddr, (void*)Hook_TRuins_Engine_Call, "TRuins.Init -> TShip.CreateAndEquipEngine");
    }

    s_installed = TRUE;
    Log("=== AG_Engines v3.4 hooks installed successfully (%u patches recorded) ===\n", (unsigned int)s_patchCount);
}

extern "C" __declspec(dllexport) void __stdcall AG_Engines_Init() {
    InstallEngineHooks();
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
    if (fdwReason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hinstDLL);
        InstallEngineHooks();
    } else if (fdwReason == DLL_PROCESS_DETACH) {
        if (lpvReserved == NULL && s_installed) {
            RestoreAllPatches();
        }
    }
    return TRUE;
}