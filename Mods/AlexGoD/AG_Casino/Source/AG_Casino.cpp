#include <windows.h>
#include <stdint.h>
#include <d3d9.h>
#include <oleauto.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <mmsystem.h>

#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "oleaut32.lib")

// ======================================================================
// AG_Casino v7.0 - Zero-Trace Pure Native Roulette
// Author: AlexGoD
// - 100% Pure Native architecture (same as AG_Search and AG_Engines)
// - ZERO save traces: NO galaxy scripts (TScript), 0 bytes in .sav files
// - Can be enabled/disabled anytime without ANY "missing mod" warnings!
// - Native assembly hook into Pirate Base service menu (TfRuinsTalk.BuildBuiltinServiceOptions)
// - Full D3D9 StateBlock capture/restore: fonts NEVER turn white!
// - Triple ESC suppression (WH_GETMESSAGE + WH_KEYBOARD + WndProc)
// ======================================================================

static void Log(const char* fmt, ...) {
    FILE* f = fopen("AG_Casino_log.txt", "a");
    if (f) {
        va_list args;
        va_start(args, fmt);
        vfprintf(f, fmt, args);
        va_end(args);
        fclose(f);
    }
}

static BOOL DetectIsEnglishFast() {
    const char* userProfile = getenv("USERPROFILE");
    if (userProfile) {
        char langFile[MAX_PATH];
        sprintf(langFile, "%s\\Documents\\SpaceRangersHD\\Lang.txt", userProfile);
        FILE* f = fopen(langFile, "r");
        if (f) {
            char buf[128];
            if (fgets(buf, sizeof(buf), f)) {
                fclose(f);
                if (strstr(buf, "english") || strstr(buf, "eng") || strstr(buf, "English") || strstr(buf, "Eng")) {
                    return TRUE;
                }
            } else {
                fclose(f);
            }
        }
    }
    return FALSE;
}

// ----------------------------------------------------------------------
// Dynamic AOB Pattern Scanner
// ----------------------------------------------------------------------
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

// ----------------------------------------------------------------------
// Game Engine Pointers & Dynamic Invocation
// ----------------------------------------------------------------------
typedef DWORD (__fastcall *fn_GetPlayer)();
static fn_GetPlayer s_fnGetPlayer = NULL;
static DWORD* s_ppEncPlayer = NULL;
static uintptr_t s_fnSetShipMoney = 0;
static DWORD* s_ppD3DDevice = NULL;

static DWORD GetPlayerShip() {
    __try {
        if (s_fnGetPlayer) return s_fnGetPlayer();
        if (s_ppEncPlayer && !IsBadReadPtr(s_ppEncPlayer, 4)) {
            return (*s_ppEncPlayer) ^ 0xB1CD15D3;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return 0;
}

static int GetPlayerMoney() {
    DWORD player = GetPlayerShip();
    if (!player) return 0;
    return *(int*)(player + 0xF0);
}

static void SetShipMoneyDelphi(DWORD player, int money) {
    __try {
        if (s_fnSetShipMoney) {
            uintptr_t fn = s_fnSetShipMoney;
            __asm {
                mov eax, player
                mov edx, money
                call fn
            }
            return;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}

    __try {
        *(int*)(player + 0xF0) = money;
        *(DWORD*)(player + 0x490) = (DWORD)money ^ 0xA4A576AD;
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

static void AddPlayerMoney(int delta) {
    DWORD player = GetPlayerShip();
    if (!player) return;
    int cur = *(int*)(player + 0xF0);
    int next = cur + delta;
    if (next < 0) next = 0;
    SetShipMoneyDelphi(player, next);
    Log("[AG_Casino] Credits: %d -> %d (%+d)\n", cur, next, delta);
}

// ----------------------------------------------------------------------
// Golden Roulette State & Rules
// ----------------------------------------------------------------------
static BOOL g_rouletteActive = FALSE;
static BOOL g_isEnglish = FALSE;
static BOOL g_ignoreClicksUntilMouseRelease = TRUE;
static BOOL g_isSpinning = FALSE;
static BOOL g_ballDropPlayed = FALSE;
static DWORD g_spinStartTime = 0;
static int g_winningNumber = -1;
static int g_selectedChipValue = 5000;
static wchar_t g_statusTextW[128] = L"СДЕЛАЙТЕ СТАВКИ И НАЖМИТЕ КРУТИТЬ!";

static DWORD g_voiceEndTime = 0;

static const int WELCOME_DURATIONS_MS[20] = {
    1100, 2400, 1500, 1450, 1700, 2400, 3000, 2700, 1500, 1850,
    1150, 1900, 1600, 2450, 2250, 1700, 2400, 1850, 2000, 2150
};

static void PlaySfxSafe(const char* wavPath) {
    if (GetTickCount() >= g_voiceEndTime) {
        PlaySoundA(wavPath, NULL, SND_ASYNC);
    }
}

#define TBL_BASE_W 1624.0f
#define TBL_BASE_H 969.0f

static float g_scale = 1.0f;
static float g_tblX = 0.0f;
static float g_tblY = 0.0f;

static const int RED_NUMBERS[18] = { 1, 3, 5, 7, 9, 12, 14, 16, 18, 19, 21, 23, 25, 27, 30, 32, 34, 36 };

static BOOL IsRedNumber(int num) {
    for (int i = 0; i < 18; i++) {
        if (RED_NUMBERS[i] == num) return TRUE;
    }
    return FALSE;
}

enum BetType {
    BET_STRAIGHT_0 = 0,
    BET_DOZEN_1 = 37,
    BET_DOZEN_2 = 38,
    BET_DOZEN_3 = 39,
    BET_COL_1 = 40,
    BET_COL_2 = 41,
    BET_COL_3 = 42,
    BET_LOW = 43,
    BET_EVEN = 44,
    BET_RED = 45,
    BET_BLACK = 46,
    BET_ODD = 47,
    BET_HIGH = 48,
    BET_COUNT = 49
};

static int g_bets[BET_COUNT] = { 0 };

// ----------------------------------------------------------------------
// Calibrated Coordinates Data Structures (1624 x 969 Virtual Canvas)
// ----------------------------------------------------------------------
struct BetCellCoord {
    float x, y, w, h;
    float chip_x, chip_y;
};

struct PickChipCoord {
    float cx, cy, r;
    int val;
};

struct ButtonCoord {
    float x, y, w, h;
};

static BetCellCoord g_betCells[BET_COUNT];
static PickChipCoord g_pickChips[6];
static ButtonCoord g_btnSpin, g_btnDouble, g_btnClear, g_btnExit;
static float g_fieldChipDiameter = 42.0f;

static void InitCalibratedCoordinates() {
    g_btnSpin = { 806.0f, 588.0f, 279.0f, 105.0f };
    g_btnClear = { 1105.0f, 599.0f, 152.0f, 89.0f };
    g_btnDouble = { 1274.0f, 599.0f, 155.0f, 89.0f };
    g_btnExit = { 1444.0f, 599.0f, 135.0f, 86.0f };

    g_pickChips[0] = { 101.0f, 637.0f, 53.0f, 100 };
    g_pickChips[1] = { 232.0f, 637.0f, 53.0f, 500 };
    g_pickChips[2] = { 364.0f, 637.0f, 52.0f, 1000 };
    g_pickChips[3] = { 490.0f, 638.0f, 52.0f, 5000 };
    g_pickChips[4] = { 612.0f, 639.0f, 53.0f, 25000 };
    g_pickChips[5] = { 731.0f, 639.0f, 52.0f, 100000 };

    g_betCells[0] = { 49.0f, 63.0f, 87.0f, 297.0f, 92.0f, 211.0f };
    g_betCells[1] = { 147.0f, 261.0f, 103.0f, 96.0f, 198.0f, 309.0f };
    g_betCells[2] = { 147.0f, 161.0f, 103.0f, 100.0f, 198.0f, 211.0f };
    g_betCells[3] = { 147.0f, 61.0f, 103.0f, 100.0f, 198.0f, 111.0f };
    g_betCells[4] = { 253.0f, 261.0f, 99.0f, 96.0f, 302.0f, 309.0f };
    g_betCells[5] = { 253.0f, 161.0f, 99.0f, 100.0f, 302.0f, 211.0f };
    g_betCells[6] = { 253.0f, 61.0f, 99.0f, 100.0f, 302.0f, 111.0f };
    g_betCells[7] = { 358.0f, 261.0f, 100.0f, 96.0f, 408.0f, 309.0f };
    g_betCells[8] = { 358.0f, 161.0f, 100.0f, 100.0f, 408.0f, 211.0f };
    g_betCells[9] = { 358.0f, 61.0f, 100.0f, 100.0f, 408.0f, 111.0f };
    g_betCells[10] = { 463.0f, 261.0f, 101.0f, 96.0f, 513.0f, 309.0f };
    g_betCells[11] = { 463.0f, 161.0f, 101.0f, 100.0f, 513.0f, 211.0f };
    g_betCells[12] = { 463.0f, 61.0f, 101.0f, 100.0f, 513.0f, 111.0f };
    g_betCells[13] = { 569.0f, 261.0f, 98.0f, 96.0f, 618.0f, 309.0f };
    g_betCells[14] = { 569.0f, 161.0f, 98.0f, 100.0f, 618.0f, 211.0f };
    g_betCells[15] = { 569.0f, 61.0f, 98.0f, 100.0f, 618.0f, 111.0f };
    g_betCells[16] = { 670.0f, 261.0f, 102.0f, 96.0f, 721.0f, 309.0f };
    g_betCells[17] = { 670.0f, 161.0f, 102.0f, 100.0f, 721.0f, 211.0f };
    g_betCells[18] = { 670.0f, 61.0f, 102.0f, 100.0f, 721.0f, 111.0f };
    g_betCells[19] = { 776.0f, 261.0f, 102.0f, 96.0f, 827.0f, 309.0f };
    g_betCells[20] = { 776.0f, 161.0f, 102.0f, 100.0f, 827.0f, 211.0f };
    g_betCells[21] = { 776.0f, 61.0f, 102.0f, 100.0f, 827.0f, 111.0f };
    g_betCells[22] = { 877.0f, 261.0f, 106.0f, 96.0f, 930.0f, 309.0f };
    g_betCells[23] = { 877.0f, 161.0f, 106.0f, 100.0f, 930.0f, 211.0f };
    g_betCells[24] = { 877.0f, 61.0f, 106.0f, 100.0f, 930.0f, 111.0f };
    g_betCells[25] = { 989.0f, 261.0f, 102.0f, 96.0f, 1040.0f, 309.0f };
    g_betCells[26] = { 989.0f, 161.0f, 102.0f, 100.0f, 1040.0f, 211.0f };
    g_betCells[27] = { 989.0f, 61.0f, 102.0f, 100.0f, 1040.0f, 111.0f };
    g_betCells[28] = { 1094.0f, 261.0f, 102.0f, 96.0f, 1145.0f, 309.0f };
    g_betCells[29] = { 1094.0f, 161.0f, 102.0f, 100.0f, 1145.0f, 211.0f };
    g_betCells[30] = { 1094.0f, 61.0f, 102.0f, 100.0f, 1145.0f, 111.0f };
    g_betCells[31] = { 1197.0f, 261.0f, 107.0f, 96.0f, 1250.0f, 309.0f };
    g_betCells[32] = { 1197.0f, 161.0f, 107.0f, 100.0f, 1250.0f, 211.0f };
    g_betCells[33] = { 1197.0f, 61.0f, 107.0f, 100.0f, 1250.0f, 111.0f };
    g_betCells[34] = { 1305.0f, 261.0f, 106.0f, 96.0f, 1358.0f, 309.0f };
    g_betCells[35] = { 1305.0f, 161.0f, 106.0f, 100.0f, 1358.0f, 211.0f };
    g_betCells[36] = { 1305.0f, 61.0f, 106.0f, 100.0f, 1358.0f, 111.0f };
    g_betCells[37] = { 147.0f, 376.0f, 409.0f, 75.0f, 351.0f, 413.0f };
    g_betCells[38] = { 570.0f, 376.0f, 413.0f, 75.0f, 776.0f, 413.0f };
    g_betCells[39] = { 993.0f, 376.0f, 415.0f, 75.0f, 1200.0f, 413.0f };
    g_betCells[40] = { 1415.0f, 261.0f, 153.0f, 96.0f, 1491.0f, 309.0f };
    g_betCells[41] = { 1415.0f, 161.0f, 153.0f, 100.0f, 1491.0f, 211.0f };
    g_betCells[42] = { 1415.0f, 61.0f, 153.0f, 100.0f, 1491.0f, 111.0f };
    g_betCells[43] = { 147.0f, 461.0f, 211.0f, 73.0f, 252.0f, 497.0f };
    g_betCells[44] = { 367.0f, 461.0f, 190.0f, 73.0f, 462.0f, 497.0f };
    g_betCells[45] = { 570.0f, 461.0f, 197.0f, 73.0f, 668.0f, 497.0f };
    g_betCells[46] = { 781.0f, 461.0f, 200.0f, 73.0f, 881.0f, 497.0f };
    g_betCells[47] = { 994.0f, 461.0f, 202.0f, 73.0f, 1095.0f, 497.0f };
    g_betCells[48] = { 1205.0f, 461.0f, 207.0f, 73.0f, 1308.0f, 497.0f };
}

static int GetTotalBet() {
    int sum = 0;
    for (int i = 0; i < BET_COUNT; i++) sum += g_bets[i];
    return sum;
}

static void ClearBets() {
    int total = GetTotalBet();
    if (total > 0) {
        AddPlayerMoney(total);
        memset(g_bets, 0, sizeof(g_bets));
        PlaySfxSafe("Mods\\AlexGoD\\AG_Casino\\DATA\\Sound\\sfx_chip_remove.wav");
        wcscpy(g_statusTextW, g_isEnglish ? L"BETS CLEARED. CREDITS REFUNDED." : L"СТАВКИ ОЧИЩЕНЫ. КРЕДИТЫ ВОЗВРАЩЕНЫ.");
    } else {
        wcscpy(g_statusTextW, g_isEnglish ? L"NO BETS ON THE TABLE." : L"СТАВОК НА СТОЛЕ НЕТ.");
    }
}

static void CloseRouletteAndRefund() {
    if (!g_rouletteActive) return;
    if (g_isSpinning) return;

    int totalUnspun = GetTotalBet();
    if (totalUnspun > 0) {
        AddPlayerMoney(totalUnspun);
        memset(g_bets, 0, sizeof(g_bets));
        Log("[AG_Casino] Auto-refunded %d credits upon closing roulette.\n", totalUnspun);
    }
    g_rouletteActive = FALSE;
}

static void DoubleBets() {
    int total = GetTotalBet();
    if (total == 0) return;
    int balance = GetPlayerMoney();
    if (balance < total) {
        wcscpy(g_statusTextW, g_isEnglish ? L"NOT ENOUGH CREDITS TO DOUBLE!" : L"НЕДОСТАТОЧНО КРЕДИТОВ ДЛЯ УДВОЕНИЯ!");
        return;
    }
    AddPlayerMoney(-total);
    for (int i = 0; i < BET_COUNT; i++) g_bets[i] *= 2;
    PlaySfxSafe("Mods\\AlexGoD\\AG_Casino\\DATA\\Sound\\sfx_chip.wav");
    wcscpy(g_statusTextW, g_isEnglish ? L"BETS DOUBLED." : L"СТАВКИ УДВОЕНЫ.");
}

static void PlaceBet(int betIndex, int amount) {
    if (g_isSpinning) return;
    int balance = GetPlayerMoney();
    if (balance < amount) {
        wcscpy(g_statusTextW, g_isEnglish ? L"NOT ENOUGH CREDITS FOR BET!" : L"НЕДОСТАТОЧНО КРЕДИТОВ ДЛЯ СТАВКИ!");
        return;
    }
    AddPlayerMoney(-amount);
    g_bets[betIndex] += amount;
    PlaySfxSafe("Mods\\AlexGoD\\AG_Casino\\DATA\\Sound\\sfx_chip.wav");
    wcscpy(g_statusTextW, g_isEnglish ? L"BET ACCEPTED." : L"СТАВКА ПРИНЯТА.");
}

static void RemoveBet(int betIndex) {
    if (g_isSpinning || g_bets[betIndex] <= 0) return;
    int removeVal = g_selectedChipValue;
    if (removeVal > g_bets[betIndex]) removeVal = g_bets[betIndex];
    g_bets[betIndex] -= removeVal;
    AddPlayerMoney(removeVal);
    PlaySfxSafe("Mods\\AlexGoD\\AG_Casino\\DATA\\Sound\\sfx_chip_remove.wav");
    wcscpy(g_statusTextW, g_isEnglish ? L"BET REMOVED." : L"СТАВКА СНЯТА.");
}

static void StartSpin() {
    if (g_isSpinning) return;
    if (GetTotalBet() == 0) {
        wcscpy(g_statusTextW, g_isEnglish ? L"PLACE AT LEAST ONE BET FIRST!" : L"СНАЧАЛА СДЕЛАЙТЕ СТАВКУ НА СТОЛЕ!");
        return;
    }
    g_isSpinning = TRUE;
    g_ballDropPlayed = FALSE;
    g_voiceEndTime = 0;
    g_spinStartTime = GetTickCount();
    wcscpy(g_statusTextW, g_isEnglish ? L"BETS ARE CLOSED! WHEEL IS SPINNING..." : L"СТАВКИ СДЕЛАНЫ! КОЛЕСО КРУТИТСЯ...");
    PlaySoundA("Mods\\AlexGoD\\AG_Casino\\DATA\\Sound\\sfx_wheel_spin.wav", NULL, SND_ASYNC);
}

static void ResolveSpin() {
    g_isSpinning = FALSE;
    g_winningNumber = rand() % 37;

    int totalBet = GetTotalBet();
    int totalWon = 0;
    if (g_bets[g_winningNumber] > 0) totalWon += g_bets[g_winningNumber] * 36;
    if (g_winningNumber >= 1 && g_winningNumber <= 12) totalWon += g_bets[BET_DOZEN_1] * 3;
    if (g_winningNumber >= 13 && g_winningNumber <= 24) totalWon += g_bets[BET_DOZEN_2] * 3;
    if (g_winningNumber >= 25 && g_winningNumber <= 36) totalWon += g_bets[BET_DOZEN_3] * 3;
    if (g_winningNumber > 0) {
        int rem = g_winningNumber % 3;
        if (rem == 1) totalWon += g_bets[BET_COL_1] * 3;
        else if (rem == 2) totalWon += g_bets[BET_COL_2] * 3;
        else if (rem == 0) totalWon += g_bets[BET_COL_3] * 3;
    }
    if (g_winningNumber >= 1 && g_winningNumber <= 18) totalWon += g_bets[BET_LOW] * 2;
    if (g_winningNumber >= 19 && g_winningNumber <= 36) totalWon += g_bets[BET_HIGH] * 2;
    if (g_winningNumber > 0) {
        if (g_winningNumber % 2 == 0) totalWon += g_bets[BET_EVEN] * 2;
        else totalWon += g_bets[BET_ODD] * 2;
        if (IsRedNumber(g_winningNumber)) totalWon += g_bets[BET_RED] * 2;
        else totalWon += g_bets[BET_BLACK] * 2;
    }

    int netProfit = totalWon - totalBet;
    const wchar_t* colName = (g_winningNumber == 0)
        ? (g_isEnglish ? L"GREEN" : L"ЗЕЛЁНОЕ")
        : (IsRedNumber(g_winningNumber) ? (g_isEnglish ? L"RED" : L"КРАСНОЕ") : (g_isEnglish ? L"BLACK" : L"ЧЁРНОЕ"));

    char voicePath[128];
    if (g_winningNumber == 0) {
        sprintf(voicePath, "Mods\\AlexGoD\\AG_Casino\\DATA\\Sound\\Voice\\num_0.wav");
    } else {
        char var = (rand() % 2 == 0) ? 'a' : 'b';
        sprintf(voicePath, "Mods\\AlexGoD\\AG_Casino\\DATA\\Sound\\Voice\\num_%d_%c.wav", g_winningNumber, var);
    }
    PlaySoundA(voicePath, NULL, SND_ASYNC);
    g_voiceEndTime = GetTickCount() + 2500;

    if (netProfit > 0) {
        AddPlayerMoney(totalWon);
        if (g_isEnglish) {
            swprintf(g_statusTextW, 128, L"OUTCOME: %d (%ls) | NET WIN: +%d CR! CONGRATULATIONS!", g_winningNumber, colName, netProfit);
        } else {
            swprintf(g_statusTextW, 128, L"ВЫПАЛО: %d (%ls) | ЧИСТЫЙ ВЫИГРЫШ: +%d CR! ПОЗДРАВЛЯЕМ!", g_winningNumber, colName, netProfit);
        }
    } else if (netProfit == 0 && totalWon > 0) {
        AddPlayerMoney(totalWon);
        if (g_isEnglish) {
            swprintf(g_statusTextW, 128, L"OUTCOME: %d (%ls) | BREAK EVEN (0 CR). BETS RETURNED.", g_winningNumber, colName);
        } else {
            swprintf(g_statusTextW, 128, L"ВЫПАЛО: %d (%ls) | ВЫ ВЫШЛИ В НОЛЬ (0 CR). СТАВКИ ВЕРНУЛИСЬ.", g_winningNumber, colName);
        }
    } else {
        if (g_isEnglish) {
            swprintf(g_statusTextW, 128, L"OUTCOME: %d (%ls) | LOSS: -%d CR. HOUSE TAKES THE BET.", g_winningNumber, colName, totalBet);
        } else {
            swprintf(g_statusTextW, 128, L"ВЫПАЛО: %d (%ls) | ИТОГ: -%d CR. КАЗИНО ЗАБИРАЕТ БАНК.", g_winningNumber, colName, totalBet);
        }
    }
    memset(g_bets, 0, sizeof(g_bets));
}

static void FormatBetAmount(int amount, wchar_t* out, int maxLen) {
    if (amount < 1000) {
        swprintf(out, maxLen, L"%d", amount);
    } else if (amount < 1000000) {
        int k = amount / 1000;
        int rem = amount % 1000;
        if (rem == 0) swprintf(out, maxLen, L"%dK", k);
        else swprintf(out, maxLen, L"%.1fK", (float)amount / 1000.0f);
    } else {
        int m = amount / 1000000;
        int rem = amount % 1000000;
        if (rem == 0) swprintf(out, maxLen, L"%dM", m);
        else swprintf(out, maxLen, L"%.1fM", (float)amount / 1000000.0f);
    }
}

// ----------------------------------------------------------------------
// DirectX 9 Visual Elements & GDI Overlays
// ----------------------------------------------------------------------
struct D3DVertex2D {
    float x, y, z, rhw;
    DWORD color;
    float u, v;
};
#define FVF_2D (D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1)

static IDirect3DTexture9* s_pTableTexture = NULL;
static IDirect3DTexture9* s_pChipTexture = NULL;
static IDirect3DTexture9* s_pTextTexture = NULL;
static IDirect3DTexture9* s_pBoardBetsTexture = NULL;
static IDirect3DDevice9* s_pLastDev = NULL;

#define TEXT_TEX_W 1400
#define TEXT_TEX_H 105
static HDC s_hTextDC = NULL;
static HBITMAP s_hTextBmp = NULL;
static DWORD* s_pTextPixels = NULL;
static HFONT s_hFontGoldLarge = NULL;
static HFONT s_hFontGoldMid = NULL;
static wchar_t s_lastStatusTextW[128] = { 0 };

#define BOARD_TEX_W 1624
#define BOARD_TEX_H 969
static HDC s_hBoardDC = NULL;
static HBITMAP s_hBoardBmp = NULL;
static DWORD* s_pBoardPixels = NULL;
static HFONT s_hFontChip = NULL;

static int s_lastMoney = -1;
static int s_lastBet = -1;
static int s_lastBetsCache[BET_COUNT] = { 0 };

static void DrawColoredRect(IDirect3DDevice9* dev, float x, float y, float w, float h, DWORD color) {
    D3DVertex2D v[4] = {
        { x,     y,     0.0f, 1.0f, color, 0.0f, 0.0f },
        { x + w, y,     0.0f, 1.0f, color, 0.0f, 0.0f },
        { x,     y + h, 0.0f, 1.0f, color, 0.0f, 0.0f },
        { x + w, y + h, 0.0f, 1.0f, color, 0.0f, 0.0f }
    };
    dev->SetTexture(0, NULL);
    dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
    dev->SetFVF(FVF_2D);
    dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(D3DVertex2D));
}

static void DrawCircleRing(IDirect3DDevice9* dev, float cx, float cy, float rInner, float rOuter, DWORD color) {
    const int SEGMENTS = 36;
    D3DVertex2D v[(SEGMENTS + 1) * 2];
    for (int i = 0; i <= SEGMENTS; i++) {
        float angle = (float)i * (6.2831853f / (float)SEGMENTS);
        float cosA = cosf(angle);
        float sinA = sinf(angle);

        v[i * 2 + 0].x = cx + rOuter * cosA;
        v[i * 2 + 0].y = cy + rOuter * sinA;
        v[i * 2 + 0].z = 0.0f;
        v[i * 2 + 0].rhw = 1.0f;
        v[i * 2 + 0].color = color;
        v[i * 2 + 0].u = 0.0f;
        v[i * 2 + 0].v = 0.0f;

        v[i * 2 + 1].x = cx + rInner * cosA;
        v[i * 2 + 1].y = cy + rInner * sinA;
        v[i * 2 + 1].z = 0.0f;
        v[i * 2 + 1].rhw = 1.0f;
        v[i * 2 + 1].color = color;
        v[i * 2 + 1].u = 0.0f;
        v[i * 2 + 1].v = 0.0f;
    }
    dev->SetTexture(0, NULL);
    dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
    dev->SetFVF(FVF_2D);
    dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, SEGMENTS * 2, v, sizeof(D3DVertex2D));
}

static void DrawTexturedQuad(IDirect3DDevice9* dev, IDirect3DTexture9* tex, float x, float y, float w, float h) {
    if (!tex) return;
    D3DVertex2D v[4] = {
        { x,     y,     0.0f, 1.0f, 0xFFFFFFFF, 0.0f, 0.0f },
        { x + w, y,     0.0f, 1.0f, 0xFFFFFFFF, 1.0f, 0.0f },
        { x,     y + h, 0.0f, 1.0f, 0xFFFFFFFF, 0.0f, 1.0f },
        { x + w, y + h, 0.0f, 1.0f, 0xFFFFFFFF, 1.0f, 1.0f }
    };
    dev->SetTexture(0, tex);
    dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    dev->SetFVF(FVF_2D);
    dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(D3DVertex2D));
}

static void LoadTextures(IDirect3DDevice9* dev) {
    if (dev != s_pLastDev) {
        if (s_pTableTexture) { s_pTableTexture->Release(); s_pTableTexture = NULL; }
        if (s_pChipTexture) { s_pChipTexture->Release(); s_pChipTexture = NULL; }
        if (s_pTextTexture) { s_pTextTexture->Release(); s_pTextTexture = NULL; }
        if (s_pBoardBetsTexture) { s_pBoardBetsTexture->Release(); s_pBoardBetsTexture = NULL; }
        s_pLastDev = dev;
    }

    if (s_pTableTexture == NULL) {
        FILE* f = fopen("Mods\\AlexGoD\\AG_Casino\\DATA\\Table\\RouletteTable.raw", "rb");
        if (f) {
            int w = 1624, h = 969;
            int size = w * h * 4;
            unsigned char* raw = (unsigned char*)malloc(size);
            if (raw) {
                fread(raw, 1, size, f);
                HRESULT hr = dev->CreateTexture(w, h, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &s_pTableTexture, NULL);
                if (SUCCEEDED(hr) && s_pTableTexture) {
                    D3DLOCKED_RECT lr;
                    if (SUCCEEDED(s_pTableTexture->LockRect(0, &lr, NULL, 0))) {
                        for (int y = 0; y < h; y++) {
                            memcpy((BYTE*)lr.pBits + (y * lr.Pitch), raw + (y * w * 4), w * 4);
                        }
                        s_pTableTexture->UnlockRect(0);
                    }
                }
                free(raw);
            }
            fclose(f);
        }
    }

    if (s_pChipTexture == NULL) {
        FILE* f = fopen("Mods\\AlexGoD\\AG_Casino\\DATA\\Table\\BetChip.raw", "rb");
        if (f) {
            int w = 256, h = 256;
            int size = w * h * 4;
            unsigned char* raw = (unsigned char*)malloc(size);
            if (raw) {
                fread(raw, 1, size, f);
                HRESULT hr = dev->CreateTexture(w, h, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &s_pChipTexture, NULL);
                if (SUCCEEDED(hr) && s_pChipTexture) {
                    D3DLOCKED_RECT lr;
                    if (SUCCEEDED(s_pChipTexture->LockRect(0, &lr, NULL, 0))) {
                        for (int y = 0; y < h; y++) {
                            memcpy((BYTE*)lr.pBits + (y * lr.Pitch), raw + (y * w * 4), w * 4);
                        }
                        s_pChipTexture->UnlockRect(0);
                    }
                }
                free(raw);
            }
            fclose(f);
        }
    }
}

static void UpdateBoardBetsOverlay(IDirect3DDevice9* dev) {
    if (!s_hBoardDC) {
        HDC screenDC = GetDC(NULL);
        s_hBoardDC = CreateCompatibleDC(screenDC);
        BITMAPINFO bi = { 0 };
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = BOARD_TEX_W;
        bi.bmiHeader.biHeight = -BOARD_TEX_H;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        s_hBoardBmp = CreateDIBSection(s_hBoardDC, &bi, DIB_RGB_COLORS, (void**)&s_pBoardPixels, NULL, 0);
        SelectObject(s_hBoardDC, s_hBoardBmp);

        s_hFontChip = CreateFontW(14, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, RUSSIAN_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Arial");
        SetBkMode(s_hBoardDC, TRANSPARENT);
        ReleaseDC(NULL, screenDC);
    }

    BOOL betsChanged = FALSE;
    for (int i = 0; i < BET_COUNT; i++) {
        if (s_lastBetsCache[i] != g_bets[i]) {
            betsChanged = TRUE;
            s_lastBetsCache[i] = g_bets[i];
        }
    }

    if (betsChanged) {
        memset(s_pBoardPixels, 0, BOARD_TEX_W * BOARD_TEX_H * 4);
        SelectObject(s_hBoardDC, s_hFontChip);
        SetTextColor(s_hBoardDC, RGB(255, 230, 40));

        for (int i = 0; i < BET_COUNT; i++) {
            if (g_bets[i] <= 0) continue;
            float cx = g_betCells[i].chip_x;
            float cy = g_betCells[i].chip_y;
            if (cx <= 0 || cy <= 0) continue;

            wchar_t buf[32];
            FormatBetAmount(g_bets[i], buf, 32);

            RECT rc = { (int)cx - 24, (int)cy - 9, (int)cx + 24, (int)cy + 9 };
            DrawTextW(s_hBoardDC, buf, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }

        for (int i = 0; i < BOARD_TEX_W * BOARD_TEX_H; i++) {
            if ((s_pBoardPixels[i] & 0x00FFFFFF) != 0) {
                s_pBoardPixels[i] |= 0xFF000000;
            }
        }

        if (!s_pBoardBetsTexture) {
            dev->CreateTexture(BOARD_TEX_W, BOARD_TEX_H, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &s_pBoardBetsTexture, NULL);
        }
        if (s_pBoardBetsTexture) {
            D3DLOCKED_RECT lr;
            if (SUCCEEDED(s_pBoardBetsTexture->LockRect(0, &lr, NULL, 0))) {
                for (int y = 0; y < BOARD_TEX_H; y++) {
                    memcpy((BYTE*)lr.pBits + y * lr.Pitch, (BYTE*)s_pBoardPixels + y * BOARD_TEX_W * 4, BOARD_TEX_W * 4);
                }
                s_pBoardBetsTexture->UnlockRect(0);
            }
        }
    }
}

static void UpdateTextOverlay(IDirect3DDevice9* dev) {
    if (!s_hTextDC) {
        HDC screenDC = GetDC(NULL);
        s_hTextDC = CreateCompatibleDC(screenDC);
        BITMAPINFO bi = { 0 };
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = TEXT_TEX_W;
        bi.bmiHeader.biHeight = -TEXT_TEX_H;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        s_hTextBmp = CreateDIBSection(s_hTextDC, &bi, DIB_RGB_COLORS, (void**)&s_pTextPixels, NULL, 0);
        SelectObject(s_hTextDC, s_hTextBmp);

        s_hFontGoldLarge = CreateFontW(23, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, RUSSIAN_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Arial");
        s_hFontGoldMid = CreateFontW(21, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, RUSSIAN_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Arial");
        SetBkMode(s_hTextDC, TRANSPARENT);
        ReleaseDC(NULL, screenDC);
    }

    int curMoney = GetPlayerMoney();
    int curBet = GetTotalBet();

    if (wcscmp(s_lastStatusTextW, g_statusTextW) != 0 || s_lastMoney != curMoney || s_lastBet != curBet) {
        wcscpy(s_lastStatusTextW, g_statusTextW);
        s_lastMoney = curMoney;
        s_lastBet = curBet;

        memset(s_pTextPixels, 0, TEXT_TEX_W * TEXT_TEX_H * 4);

        SelectObject(s_hTextDC, s_hFontGoldLarge);
        SetTextColor(s_hTextDC, RGB(255, 235, 80));
        RECT rcStatus = { 0, 5, TEXT_TEX_W, 35 };
        DrawTextW(s_hTextDC, g_statusTextW, -1, &rcStatus, DT_CENTER | DT_SINGLELINE);

        SelectObject(s_hTextDC, s_hFontGoldMid);
        SetTextColor(s_hTextDC, RGB(255, 215, 0));

        wchar_t bufMoney[64];
        if (g_isEnglish) {
            swprintf(bufMoney, 64, L"BALANCE: %d CR", curMoney);
        } else {
            swprintf(bufMoney, 64, L"БАЛАНС: %d CR", curMoney);
        }
        RECT rcMoney = { 0, 42, TEXT_TEX_W, 68 };
        DrawTextW(s_hTextDC, bufMoney, -1, &rcMoney, DT_CENTER | DT_SINGLELINE);

        wchar_t bufBet[64];
        if (g_isEnglish) {
            swprintf(bufBet, 64, L"CURRENT BET ON TABLE: %d CR", curBet);
        } else {
            swprintf(bufBet, 64, L"ТЕКУЩАЯ СТАВКА НА СТОЛЕ: %d CR", curBet);
        }
        RECT rcBet = { 0, 72, TEXT_TEX_W, 98 };
        DrawTextW(s_hTextDC, bufBet, -1, &rcBet, DT_CENTER | DT_SINGLELINE);

        for (int i = 0; i < TEXT_TEX_W * TEXT_TEX_H; i++) {
            DWORD c = s_pTextPixels[i];
            if ((c & 0x00FFFFFF) != 0) {
                s_pTextPixels[i] |= 0xFF000000;
            }
        }

        if (!s_pTextTexture) {
            dev->CreateTexture(TEXT_TEX_W, TEXT_TEX_H, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &s_pTextTexture, NULL);
        }
        if (s_pTextTexture) {
            D3DLOCKED_RECT lr;
            if (SUCCEEDED(s_pTextTexture->LockRect(0, &lr, NULL, 0))) {
                for (int y = 0; y < TEXT_TEX_H; y++) {
                    memcpy((BYTE*)lr.pBits + y * lr.Pitch, (BYTE*)s_pTextPixels + y * TEXT_TEX_W * 4, TEXT_TEX_W * 4);
                }
                s_pTextTexture->UnlockRect(0);
            }
        }
    }
}

// ----------------------------------------------------------------------
// Mouse Input Handler with Inverse Resolution Transform
// ----------------------------------------------------------------------
static BOOL HandleRouletteClick(float mx, float my, BOOL isRightClick) {
    if (!g_rouletteActive) return FALSE;
    if (g_scale <= 0.001f) return FALSE;

    float rx = (mx - g_tblX) / g_scale;
    float ry = (my - g_tblY) / g_scale;

    if (rx < 0 || rx > TBL_BASE_W || ry < 0 || ry > TBL_BASE_H) return FALSE;

    if (!isRightClick) {
        if (rx >= g_btnSpin.x && rx <= g_btnSpin.x + g_btnSpin.w && ry >= g_btnSpin.y && ry <= g_btnSpin.y + g_btnSpin.h) {
            StartSpin();
            return TRUE;
        }
        if (rx >= g_btnDouble.x && rx <= g_btnDouble.x + g_btnDouble.w && ry >= g_btnDouble.y && ry <= g_btnDouble.y + g_btnDouble.h) {
            DoubleBets();
            return TRUE;
        }
        if (rx >= g_btnClear.x && rx <= g_btnClear.x + g_btnClear.w && ry >= g_btnClear.y && ry <= g_btnClear.y + g_btnClear.h) {
            ClearBets();
            return TRUE;
        }
        if (rx >= g_btnExit.x && rx <= g_btnExit.x + g_btnExit.w && ry >= g_btnExit.y && ry <= g_btnExit.y + g_btnExit.h) {
            CloseRouletteAndRefund();
            return TRUE;
        }
    }

    if (!isRightClick) {
        for (int i = 0; i < 6; i++) {
            float dx = rx - g_pickChips[i].cx;
            float dy = ry - g_pickChips[i].cy;
            if (dx * dx + dy * dy <= g_pickChips[i].r * g_pickChips[i].r) {
                g_selectedChipValue = g_pickChips[i].val;
                PlaySfxSafe("Mods\\AlexGoD\\AG_Casino\\DATA\\Sound\\sfx_chip.wav");
                return TRUE;
            }
        }
    }

    for (int i = 0; i < BET_COUNT; i++) {
        if (rx >= g_betCells[i].x && rx <= g_betCells[i].x + g_betCells[i].w &&
            ry >= g_betCells[i].y && ry <= g_betCells[i].y + g_betCells[i].h) {
            if (isRightClick) RemoveBet(i);
            else PlaceBet(i, g_selectedChipValue);
            return TRUE;
        }
    }

    return TRUE;
}

// ----------------------------------------------------------------------
// Window Subclassing & Windows Hooks (Triple-Layer ESC Neutralizer)
// ----------------------------------------------------------------------
static WNDPROC s_origWndProc = NULL;
static HWND s_gameHWnd = NULL;
static HHOOK s_hGetMsgHook = NULL;
static HHOOK s_hKeyboardHook = NULL;

static LRESULT CALLBACK Casino_SubclassWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (g_rouletteActive) {
        if (uMsg == WM_KEYDOWN || uMsg == WM_KEYUP || uMsg == WM_CHAR || uMsg == WM_SYSKEYDOWN || uMsg == WM_SYSKEYUP) {
            if (wParam == VK_ESCAPE) {
                return 0;
            }
        }
    }
    if (s_origWndProc) {
        return CallWindowProc(s_origWndProc, hWnd, uMsg, wParam, lParam);
    }
    return DefWindowProc(hWnd, uMsg, wParam, lParam);
}

static LRESULT CALLBACK Casino_GetMsgProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0 && g_rouletteActive) {
        MSG* pMsg = (MSG*)lParam;
        if (pMsg) {
            if (pMsg->message == WM_KEYDOWN || pMsg->message == WM_KEYUP || pMsg->message == WM_CHAR ||
                pMsg->message == WM_SYSKEYDOWN || pMsg->message == WM_SYSKEYUP) {
                if (pMsg->wParam == VK_ESCAPE) {
                    pMsg->message = WM_NULL;
                    pMsg->wParam = 0;
                    pMsg->lParam = 0;
                }
            }
        }
    }
    return CallNextHookEx(s_hGetMsgHook, nCode, wParam, lParam);
}

static LRESULT CALLBACK Casino_KeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0 && g_rouletteActive) {
        if (wParam == VK_ESCAPE) {
            return 1;
        }
    }
    return CallNextHookEx(s_hKeyboardHook, nCode, wParam, lParam);
}

static void PollRouletteInput(HWND hWnd) {
    if (!g_rouletteActive || !hWnd) return;

    POINT pt;
    if (GetCursorPos(&pt) && ScreenToClient(hWnd, &pt)) {
        SHORT lKey = GetAsyncKeyState(VK_LBUTTON);
        SHORT rKey = GetAsyncKeyState(VK_RBUTTON);

        static BOOL wasLDown = FALSE;
        static BOOL wasRDown = FALSE;

        BOOL isLDown = (lKey & 0x8000) != 0;
        BOOL isRDown = (rKey & 0x8000) != 0;

        if (g_ignoreClicksUntilMouseRelease) {
            if (!isLDown && !isRDown) {
                g_ignoreClicksUntilMouseRelease = FALSE;
                wasLDown = FALSE;
                wasRDown = FALSE;
            }
            return;
        }

        if (isLDown && !wasLDown) {
            HandleRouletteClick((float)pt.x, (float)pt.y, FALSE);
        }
        if (isRDown && !wasRDown) {
            HandleRouletteClick((float)pt.x, (float)pt.y, TRUE);
        }

        wasLDown = isLDown;
        wasRDown = isRDown;
    }
}

static void RenderRouletteOverlay(IDirect3DDevice9* dev) {
    if (!dev) return;

    if (g_isSpinning) {
        DWORD elapsed = GetTickCount() - g_spinStartTime;
        if (!g_ballDropPlayed && elapsed >= 1600) {
            g_ballDropPlayed = TRUE;
            PlaySoundA("Mods\\AlexGoD\\AG_Casino\\DATA\\Sound\\sfx_ball_drop.wav", NULL, SND_ASYNC);
            wcscpy(g_statusTextW, g_isEnglish ? L"BALL IS DROPPING..." : L"ШАРИК ПАДАЕТ В КОЛЕСО...");
        }
        if (elapsed >= 3600) {
            ResolveSpin();
        }
    }

    LoadTextures(dev);

    D3DVIEWPORT9 vp;
    float sw = 1920.0f, sh = 1080.0f;
    if (SUCCEEDED(dev->GetViewport(&vp))) {
        sw = (float)vp.Width;
        sh = (float)vp.Height;
    }

    float scaleX = (sw * 0.96f) / TBL_BASE_W;
    float scaleY = (sh * 0.96f) / TBL_BASE_H;
    g_scale = (scaleX < scaleY) ? scaleX : scaleY;
    if (g_scale < 0.4f) g_scale = 0.4f;

    float curTblW = TBL_BASE_W * g_scale;
    float curTblH = TBL_BASE_H * g_scale;
    g_tblX = (sw - curTblW) * 0.5f;
    g_tblY = (sh - curTblH) * 0.5f;

    // CAPTURE ALL DIRECTX 9 PIPELINE STATES (prevents font discoloration and state leakage!)
    IDirect3DStateBlock9* pStateBlock = NULL;
    if (FAILED(dev->CreateStateBlock(D3DSBT_ALL, &pStateBlock))) {
        pStateBlock = NULL;
    } else {
        pStateBlock->Capture();
    }

    dev->SetVertexShader(NULL);
    dev->SetPixelShader(NULL);
    dev->SetRenderState(D3DRS_ZENABLE, FALSE);
    dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    dev->SetRenderState(D3DRS_LIGHTING, FALSE);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);

    // 1. Draw Table Texture
    DrawTexturedQuad(dev, s_pTableTexture, g_tblX, g_tblY, curTblW, curTblH);

    // 2. Draw Real Poker Chips on Placed Bets
    float scaledChipDiam = g_fieldChipDiameter * g_scale;
    float chipRadius = scaledChipDiam * 0.5f;
    for (int i = 0; i < BET_COUNT; i++) {
        if (g_bets[i] <= 0) continue;
        float cx = g_tblX + g_betCells[i].chip_x * g_scale;
        float cy = g_tblY + g_betCells[i].chip_y * g_scale;
        if (s_pChipTexture) {
            DrawTexturedQuad(dev, s_pChipTexture, cx - chipRadius, cy - chipRadius, scaledChipDiam, scaledChipDiam);
        } else {
            DrawColoredRect(dev, cx - chipRadius, cy - chipRadius, scaledChipDiam, scaledChipDiam, 0xEEFFD700);
        }
    }

    // 3. Draw Formatted Bet Amounts On Top of Chips
    UpdateBoardBetsOverlay(dev);
    if (s_pBoardBetsTexture) {
        DrawTexturedQuad(dev, s_pBoardBetsTexture, g_tblX, g_tblY, curTblW, curTblH);
    }

    // 4. Highlight Selected Pick Chip with Circular Golden Ring
    for (int i = 0; i < 6; i++) {
        if (g_pickChips[i].val == g_selectedChipValue) {
            float cx = g_tblX + g_pickChips[i].cx * g_scale;
            float cy = g_tblY + g_pickChips[i].cy * g_scale;
            float r = g_pickChips[i].r * g_scale;
            DrawCircleRing(dev, cx, cy, r - 5.0f * g_scale, r + 3.0f * g_scale, 0xEEFFD700);
            DrawCircleRing(dev, cx, cy, 0.0f, r, 0x33FFD700);
            break;
        }
    }

    // 5. Highlight Winning Number
    if (g_winningNumber >= 0 && !g_isSpinning && g_winningNumber < BET_COUNT) {
        float wx = g_tblX + g_betCells[g_winningNumber].x * g_scale;
        float wy = g_tblY + g_betCells[g_winningNumber].y * g_scale;
        float ww = g_betCells[g_winningNumber].w * g_scale;
        float wh = g_betCells[g_winningNumber].h * g_scale;
        DrawColoredRect(dev, wx, wy, ww, wh, 0x99FFFF00);
    }

    // 6. Draw 3 Gold Lines Status Box
    UpdateTextOverlay(dev);
    if (s_pTextTexture) {
        DrawTexturedQuad(dev, s_pTextTexture, g_tblX + 110.0f * g_scale, g_tblY + 735.0f * g_scale, 1400.0f * g_scale, 105.0f * g_scale);
    }

    // RESTORE ALL DIRECTX 9 PIPELINE STATES (100% bit-exact restoration!)
    if (pStateBlock) {
        pStateBlock->Apply();
        pStateBlock->Release();
    }
}

// ----------------------------------------------------------------------
// Exported Functions (0 Arguments)
// ----------------------------------------------------------------------
extern "C" __declspec(dllexport) void __stdcall Casino_OpenRoulette() {
    InitCalibratedCoordinates();

    g_isEnglish = DetectIsEnglishFast();

    int prevBets = GetTotalBet();
    if (prevBets > 0) {
        AddPlayerMoney(prevBets);
        memset(g_bets, 0, sizeof(g_bets));
    }

    g_rouletteActive = TRUE;
    g_isSpinning = FALSE;
    g_winningNumber = -1;
    g_ignoreClicksUntilMouseRelease = TRUE;
    wcscpy(g_statusTextW, g_isEnglish ? L"PLACE YOUR BETS AND CLICK SPIN!" : L"СДЕЛАЙТЕ СТАВКИ И НАЖМИТЕ КРУТИТЬ!");

    int r = 1 + rand() % 20;
    char path[128];
    sprintf(path, "Mods\\AlexGoD\\AG_Casino\\DATA\\Sound\\Voice\\welcome_%02d.wav", r);
    PlaySoundA(path, NULL, SND_ASYNC);
    g_voiceEndTime = GetTickCount() + WELCOME_DURATIONS_MS[r - 1];

    Log("[AG_Casino] Casino_OpenRoulette called (Lang=%s)! Activated with dealer welcome_%02d.wav.\n", g_isEnglish ? "Eng" : "Rus", r);
}

extern "C" __declspec(dllexport) int __stdcall Casino_IsActive() {
    return g_rouletteActive ? 1 : 0;
}

// ----------------------------------------------------------------------
// Native Pirate Base Choice Integration (Zero Save Trace Architecture)
// ----------------------------------------------------------------------
static uintptr_t s_fnAddChoice = 0;
static uintptr_t s_pirateChoiceHookTarget = 0;
static BYTE s_origPirateChoiceBytes[5] = { 0 };
static BOOL s_hookPirateChoiceInstalled = FALSE;

__declspec(naked) void OnCasinoChoiceClicked() {
    __asm {
        pushad
        call Casino_OpenRoulette
        popad
        ret
    }
}

void __fastcall AddCasinoPirateChoice(void* selfRuinsTalk) {
    if (!selfRuinsTalk || !s_fnAddChoice) return;

    BOOL isEng = DetectIsEnglishFast();
    const wchar_t* title = isEng ? L"- Underground Casino" : L"- Сыграть в рулетку";

    BSTR bstr = SysAllocString(title);
    if (!bstr) return;

    uintptr_t fn = s_fnAddChoice;
    __asm {
        push selfRuinsTalk                // Callback.Data
        push offset OnCasinoChoiceClicked // Callback.Code
        xor ecx, ecx                      // Value: PtrInt = 0
        mov edx, bstr                     // Text: WideString
        mov eax, selfRuinsTalk            // Self: TfRuinsTalk
        call fn                           // TfRuinsTalk.AddChoice (pops 8 bytes with ret 8)
    }

    SysFreeString(bstr);
}

__declspec(naked) void Hook_PirateBaseAddChoice() {
    __asm {
        // [esp + 0]: return address to 0x0056240D
        // [esp + 4]: Callback.Code (ShowPirateBaseChameleonDialog)
        // [esp + 8]: Callback.Data (Self: TfRuinsTalk)

        // 1. Duplicate Callback args for the original Chameleon AddChoice:
        push dword ptr [esp + 8]          // duplicate Callback.Data
        push dword ptr [esp + 8]          // duplicate Callback.Code
        call dword ptr [s_fnAddChoice]    // calls original AddChoice, pops 8 bytes via ret 8

        // 2. Add our Casino choice right after Chameleon!
        // In BuildBuiltinServiceOptions, EBX is Self (TfRuinsTalk).
        pushad
        mov ecx, ebx                      // pass Self in ECX (__fastcall first arg)
        call AddCasinoPirateChoice
        popad

        // 3. Pop the caller's original 8-byte Callback args and return to caller (0x0056240D):
        ret 8
    }
}

// ----------------------------------------------------------------------
// PresentScreenBuffer Hook
// ----------------------------------------------------------------------
static uintptr_t s_presentHookTarget = 0;
static DWORD s_presentReturnAddr = 0;
static BYTE s_origPresentBytes[8] = { 0 };
static BOOL s_hookPresentInstalled = FALSE;

static void RestoreAllHooks() {
    if (s_pirateChoiceHookTarget && s_hookPirateChoiceInstalled) {
        DWORD oldProtect;
        if (VirtualProtect((void*)s_pirateChoiceHookTarget, 5, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            memcpy((void*)s_pirateChoiceHookTarget, s_origPirateChoiceBytes, 5);
            VirtualProtect((void*)s_pirateChoiceHookTarget, 5, oldProtect, &oldProtect);
            FlushInstructionCache(GetCurrentProcess(), (void*)s_pirateChoiceHookTarget, 5);
            Log("[AG_Casino] Cleanly restored original Pirate Base choice bytes.\n");
        }
        s_hookPirateChoiceInstalled = FALSE;
    }
    if (s_presentHookTarget && s_hookPresentInstalled) {
        DWORD oldProtect;
        if (VirtualProtect((void*)s_presentHookTarget, 8, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            memcpy((void*)s_presentHookTarget, s_origPresentBytes, 8);
            VirtualProtect((void*)s_presentHookTarget, 8, oldProtect, &oldProtect);
            FlushInstructionCache(GetCurrentProcess(), (void*)s_presentHookTarget, 8);
            Log("[AG_Casino] Cleanly restored original PresentScreenBuffer bytes.\n");
        }
        s_hookPresentInstalled = FALSE;
    }
    if (s_hGetMsgHook) {
        UnhookWindowsHookEx(s_hGetMsgHook);
        s_hGetMsgHook = NULL;
    }
    if (s_hKeyboardHook) {
        UnhookWindowsHookEx(s_hKeyboardHook);
        s_hKeyboardHook = NULL;
    }
    if (s_gameHWnd && s_origWndProc) {
        SetWindowLongPtrA(s_gameHWnd, GWLP_WNDPROC, (LONG_PTR)s_origWndProc);
        s_origWndProc = NULL;
        s_gameHWnd = NULL;
    }
    if (s_pTableTexture) { s_pTableTexture->Release(); s_pTableTexture = NULL; }
    if (s_pChipTexture) { s_pChipTexture->Release(); s_pChipTexture = NULL; }
    if (s_pTextTexture) { s_pTextTexture->Release(); s_pTextTexture = NULL; }
    if (s_pBoardBetsTexture) { s_pBoardBetsTexture->Release(); s_pBoardBetsTexture = NULL; }
    if (s_hTextBmp) { DeleteObject(s_hTextBmp); s_hTextBmp = NULL; }
    if (s_hTextDC) { DeleteDC(s_hTextDC); s_hTextDC = NULL; }
    if (s_hBoardBmp) { DeleteObject(s_hBoardBmp); s_hBoardBmp = NULL; }
    if (s_hBoardDC) { DeleteDC(s_hBoardDC); s_hBoardDC = NULL; }
    if (s_hFontGoldLarge) { DeleteObject(s_hFontGoldLarge); s_hFontGoldLarge = NULL; }
    if (s_hFontGoldMid) { DeleteObject(s_hFontGoldMid); s_hFontGoldMid = NULL; }
    if (s_hFontChip) { DeleteObject(s_hFontChip); s_hFontChip = NULL; }
    s_pLastDev = NULL;
}

void __stdcall OnBeforeEndScene() {
    if (!g_rouletteActive) return;

    DWORD player = GetPlayerShip();
    if (!player || *(DWORD*)(player + 0x20) == 0) {
        CloseRouletteAndRefund();
        return;
    }

    if (!s_ppD3DDevice || IsBadReadPtr(s_ppD3DDevice, 4) || !*s_ppD3DDevice) return;
    IDirect3DDevice9* dev = (IDirect3DDevice9*)(*s_ppD3DDevice);

    HWND hWnd = GetForegroundWindow();
    DWORD pid = 0;
    GetWindowThreadProcessId(hWnd, &pid);
    if (pid == GetCurrentProcessId()) {
        if (hWnd != s_gameHWnd) {
            s_gameHWnd = hWnd;
            s_origWndProc = (WNDPROC)SetWindowLongPtrA(hWnd, GWLP_WNDPROC, (LONG_PTR)Casino_SubclassWndProc);

            DWORD curThread = GetCurrentThreadId();
            if (!s_hGetMsgHook) {
                s_hGetMsgHook = SetWindowsHookExA(WH_GETMESSAGE, Casino_GetMsgProc, NULL, curThread);
            }
            if (!s_hKeyboardHook) {
                s_hKeyboardHook = SetWindowsHookExA(WH_KEYBOARD, Casino_KeyboardProc, NULL, curThread);
            }
            Log("[AG_Casino] Installed triple ESC protection (WndProc + WH_GETMESSAGE + WH_KEYBOARD) on HWND 0x%08X\n", (DWORD)hWnd);
        }
        PollRouletteInput(hWnd);
    }

    RenderRouletteOverlay(dev);
}

__declspec(naked) void Hook_PresentScreenBuffer() {
    __asm {
        pushad
        call OnBeforeEndScene
        popad

        mov edx, [s_ppD3DDevice]
        mov eax, [edx]
        push eax
        mov eax, [eax]

        jmp dword ptr [s_presentReturnAddr]
    }
}

// ----------------------------------------------------------------------
// Installation of Hooks via AOB
// ----------------------------------------------------------------------
static void InstallHooks() {
    __try {
        InitCalibratedCoordinates();

        uintptr_t codeBase = 0;
        size_t codeSize = 0;
        if (!GetCodeSectionRange(&codeBase, &codeSize)) {
            Log("[AG_Casino] Failed to query code section.\n");
            return;
        }

        // 1. Dynamic scan for GetPlayer
        const unsigned char sigGetPlayer[] = { 0xA1, 0x00, 0x00, 0x00, 0x00, 0x35, 0xD3, 0x15, 0xCD, 0xB1, 0xC3 };
        uintptr_t getPlayerAddr = FindPattern(codeBase, codeSize, sigGetPlayer, "x????xxxxxx");
        if (getPlayerAddr) {
            s_fnGetPlayer = (fn_GetPlayer)getPlayerAddr;
            s_ppEncPlayer = *(DWORD**)(getPlayerAddr + 1);
            Log("[AG_Casino] Found GetPlayer at 0x%08X (EncPlayer: 0x%08X)\n", getPlayerAddr, (DWORD)s_ppEncPlayer);
        } else {
            s_fnGetPlayer = (fn_GetPlayer)0x00747300;
            s_ppEncPlayer = (DWORD*)0x008100FC;
            Log("[AG_Casino] Fallback GetPlayer at 0x00747300\n");
        }

        // 2. Dynamic scan for SetShipMoney
        const unsigned char sigSetMoney[] = { 0x53, 0x56, 0x8B, 0xF2, 0x8B, 0xD8, 0x81, 0xFE, 0x00, 0xE1, 0xF5, 0x05 };
        uintptr_t setMoneyAddr = FindPattern(codeBase, codeSize, sigSetMoney, "xxxxxxxxxxxx");
        if (setMoneyAddr) {
            s_fnSetShipMoney = setMoneyAddr;
            Log("[AG_Casino] Found SetShipMoney at 0x%08X\n", setMoneyAddr);
        } else {
            s_fnSetShipMoney = 0x006C7938;
            Log("[AG_Casino] Fallback SetShipMoney at 0x006C7938\n");
        }

        // 3. Dynamic scan for PresentScreenBuffer
        const unsigned char sigPresent[] = {
            0x80, 0x38, 0x00, 0x74, 0x00,
            0xA1, 0x00, 0x00, 0x00, 0x00,
            0x50, 0x8B, 0x00, 0xFF, 0x90, 0xA8, 0x00, 0x00, 0x00
        };
        uintptr_t matchPresent = FindPattern(codeBase, codeSize, sigPresent, "xxxx?x????xxxxxxxxx");
        if (matchPresent) {
            s_presentHookTarget = matchPresent + 5;
            s_ppD3DDevice = *(DWORD**)(s_presentHookTarget + 1);
            Log("[AG_Casino] Found PresentScreenBuffer at 0x%08X (DevicePtr: 0x%08X)\n", s_presentHookTarget, (DWORD)s_ppD3DDevice);
        } else {
            s_presentHookTarget = 0x007CAF19;
            s_ppD3DDevice = (DWORD*)0x0081302C;
            Log("[AG_Casino] Fallback PresentScreenBuffer at 0x007CAF19\n");
        }

        s_presentReturnAddr = (DWORD)s_presentHookTarget + 8;

        DWORD oldProtect;
        if (VirtualProtect((void*)s_presentHookTarget, 8, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            memcpy(s_origPresentBytes, (const void*)s_presentHookTarget, 8);
            BYTE jump[8] = { 0xE9, 0, 0, 0, 0, 0x90, 0x90, 0x90 };
            DWORD rel = (DWORD)Hook_PresentScreenBuffer - (s_presentHookTarget + 5);
            memcpy(&jump[1], &rel, 4);
            memcpy((void*)s_presentHookTarget, jump, 8);
            VirtualProtect((void*)s_presentHookTarget, 8, oldProtect, &oldProtect);
            FlushInstructionCache(GetCurrentProcess(), (void*)s_presentHookTarget, 8);
            s_hookPresentInstalled = TRUE;
            Log("[AG_Casino] Hook installed on PresentScreenBuffer at 0x%08X -> ret 0x%08X\n", s_presentHookTarget, s_presentReturnAddr);
        }

        // 4. Dynamic scan for Pirate Base AddChoice hook site
        const unsigned char sigPirateChoice[] = {
            0x8B, 0x95, 0x44, 0xFF, 0xFF, 0xFF,
            0x33, 0xC9,
            0x8B, 0xC3,
            0xE8, 0x00, 0x00, 0x00, 0x00,
            0xE8, 0x00, 0x00, 0x00, 0x00
        };
        uintptr_t matchPirate = FindPattern(codeBase, codeSize, sigPirateChoice, "xxxxxxxxxxx????x????");
        if (matchPirate) {
            s_pirateChoiceHookTarget = matchPirate + 10;
            int32_t relAddChoice = *(int32_t*)(s_pirateChoiceHookTarget + 1);
            s_fnAddChoice = s_pirateChoiceHookTarget + 5 + relAddChoice;
            Log("[AG_Casino] Found Pirate Base AddChoice call at 0x%08X (AddChoice target: 0x%08X)\n", s_pirateChoiceHookTarget, s_fnAddChoice);
        } else {
            s_pirateChoiceHookTarget = 0x00562408;
            s_fnAddChoice = 0x00559E88;
            Log("[AG_Casino] Fallback Pirate Base AddChoice call at 0x00562408 (AddChoice target: 0x00559E88)\n");
        }

        if (s_pirateChoiceHookTarget && VirtualProtect((void*)s_pirateChoiceHookTarget, 5, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            memcpy(s_origPirateChoiceBytes, (const void*)s_pirateChoiceHookTarget, 5);
            BYTE callOp[5] = { 0xE8, 0, 0, 0, 0 };
            DWORD rel = (DWORD)Hook_PirateBaseAddChoice - (s_pirateChoiceHookTarget + 5);
            memcpy(&callOp[1], &rel, 4);
            memcpy((void*)s_pirateChoiceHookTarget, callOp, 5);
            VirtualProtect((void*)s_pirateChoiceHookTarget, 5, oldProtect, &oldProtect);
            FlushInstructionCache(GetCurrentProcess(), (void*)s_pirateChoiceHookTarget, 5);
            s_hookPirateChoiceInstalled = TRUE;
            Log("[AG_Casino] Hook installed on Pirate Base AddChoice at 0x%08X -> Hook_PirateBaseAddChoice\n", s_pirateChoiceHookTarget);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("[AG_Casino] Exception in InstallHooks\n");
    }
}

extern "C" __declspec(dllexport) void __stdcall AG_Casino_Init() {
    static BOOL s_initialized = FALSE;
    if (s_initialized) return;
    s_initialized = TRUE;

    Log("[AG_Casino] AG_Casino_Init v7.0 (Pure Native Zero-Trace Edition)\n");
    InstallHooks();
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        HMODULE hPinned = NULL;
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_PIN | GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, (LPCSTR)DllMain, &hPinned);
        Log("[AG_Casino] DLL_PROCESS_ATTACH v7.0 (Pinned in memory)\n");
    } else if (ul_reason_for_call == DLL_PROCESS_DETACH) {
        RestoreAllHooks();
        Log("[AG_Casino] DLL_PROCESS_DETACH v7.0 (All hooks cleanly restored)\n");
    }
    return TRUE;
}
