// ElectroGuiCalc Lite by iknlm
// Пошаговый калькулятор электрика: один вопрос на экране, ответил - следующий.
// Отдельная программа, от ElectroGuiCalc Pro не зависит. C++20, Dear ImGui, DirectX 11, WinAPI.
#define NOMINMAX
#ifdef _WIN32
#include <windows.h>
#include <d3d11.h>
#include <dwmapi.h>
#include "../imgui/imgui_impl_win32.h"
#include "../imgui/imgui_impl_dx11.h"
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dwmapi.lib")
#endif
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "../imgui/imgui.h"

// ======================= ЯЗЫК =======================
static int g_lang = 1;   // 0 = English, 1 = русский
static const char* TR(const char* en, const char* ru) { return g_lang == 1 ? ru : en; }

// ======================= ЦВЕТА =======================
static const ImVec4 COL_BG(0.055f, 0.063f, 0.086f, 1.0f);
static const ImVec4 COL_CARD(0.094f, 0.106f, 0.145f, 1.0f);
static const ImVec4 COL_FRAME(0.137f, 0.153f, 0.208f, 1.0f);
static const ImVec4 COL_BORDER(0.22f, 0.25f, 0.33f, 1.0f);
static const ImVec4 COL_ACCENT(0.30f, 0.62f, 1.00f, 1.0f);
static const ImVec4 COL_TEXT(0.93f, 0.94f, 0.97f, 1.0f);
static const ImVec4 COL_DIM(0.58f, 0.62f, 0.72f, 1.0f);
static const ImVec4 COL_GOOD(0.40f, 0.90f, 0.50f, 1.0f);
static const ImVec4 COL_BAD(1.00f, 0.42f, 0.42f, 1.0f);
static const ImVec4 COL_WARN(1.00f, 0.78f, 0.30f, 1.0f);

static ImVec4 WithAlpha(const ImVec4& c, float a) { return ImVec4(c.x, c.y, c.z, a); }

// ======================= ШАГИ МАСТЕРА =======================
enum class StepKind { Choice, Number };

struct Step {
    const char* q_en;        // вопрос
    const char* q_ru;
    const char* hint_en;     // пояснение под вопросом
    const char* hint_ru;
    StepKind kind;
    int* choice;                      // для выбора
    const char* const* opts_en;
    const char* const* opts_ru;
    int opts_count;
    float* value;                     // для числа
    const char* unit_en;
    const char* unit_ru;
    float vmin, vmax;
};

struct Calc {
    const char* name_en;
    const char* name_ru;
    const char* desc_en;
    const char* desc_ru;
    const Step* steps;
    int steps_count;
    void (*result)();
};

// ======================= ВВОДИМЫЕ ДАННЫЕ =======================
// Кабель
static int   c_net = 0, c_mat = 0, c_load = 1, c_inst = 0;
static float c_power = 3.5f, c_len = 20.0f;
// Автомат
static int   b_net = 0, b_type = 1;
static float b_power = 3.5f;
// Нагрузка
static int   l_net = 0, l_sim = 1;
static float l_power = 5.0f, l_hours = 4.0f, l_price = 6.0f;
// Двигатель
static int   m_net = 0, m_start = 0;
static float m_power = 5.5f;
// Заземление
static int   g_soil = 2, g_norm = 2, g_rod = 2, g_zone = 0;
// Молниезащита
static int   z_rel = 0;
static float z_hx = 5.0f, z_dist = 5.0f;

// ======================= ВАРИАНТЫ ОТВЕТОВ =======================
static const char* const NET_EN[] = { "220 V, single-phase (flat, house)", "380 V, three-phase" };
static const char* const NET_RU[] = { "220 В, одна фаза (квартира, дом)", "380 В, три фазы" };
static const char* const MAT_EN[] = { "Copper", "Aluminium" };
static const char* const MAT_RU[] = { "Медь", "Алюминий" };
static const char* const LOAD_EN[] = { "Heaters, boiler, kettle, incandescent lamps", "Ordinary appliances, sockets, LED lighting", "Motors, pumps, compressors, machines" };
static const char* const LOAD_RU[] = { "Обогреватели, бойлер, чайник, лампы накаливания", "Обычная техника, розетки, светодиодный свет", "Двигатели, насосы, компрессоры, станки" };
static const float LOAD_COS[] = { 1.0f, 0.95f, 0.85f };
static const char* const INST_EN[] = { "Open: on a wall, in a tray, in the air", "In a pipe, corrugated tube or trunking", "In the ground (trench)" };
static const char* const INST_RU[] = { "Открыто: по стене, в лотке, по воздуху", "В трубе, гофре или кабель-канале", "В земле (траншея)" };
static const char* const SIM_EN[] = { "Everything runs at once", "Most of it runs at once", "About half runs at once" };
static const char* const SIM_RU[] = { "Всё работает одновременно", "Одновременно работает большая часть", "Одновременно работает примерно половина" };
static const float SIM_K[] = { 1.0f, 0.8f, 0.5f };
static const char* const START_EN[] = { "Direct start (just a contactor)", "Star-delta", "Soft starter", "Frequency converter (VFD)" };
static const char* const START_RU[] = { "Прямой пуск (просто пускатель)", "Звезда-треугольник", "Устройство плавного пуска", "Частотный преобразователь" };
static const char* const SOIL_EN[] = { "Peat, wet soil", "Black earth (chernozem)", "Clay", "Loam", "Sandy loam", "Sand" };
static const char* const SOIL_RU[] = { "Торф, сырой грунт", "Чернозём", "Глина", "Суглинок", "Супесь", "Песок" };
static const float SOIL_RHO[] = { 20.0f, 50.0f, 60.0f, 100.0f, 300.0f, 500.0f };
static const char* const NORM_EN[] = { "4 Ohm - substation neutral, main earthing", "10 Ohm - lightning protection", "30 Ohm - repeated earthing of a house" };
static const char* const NORM_RU[] = { "4 Ом - нейтраль подстанции, главное заземление", "10 Ом - молниезащита", "30 Ом - повторное заземление дома" };
static const float NORM_OHM[] = { 4.0f, 10.0f, 30.0f };
static const char* const ROD_EN[] = { "2 m", "2.5 m", "3 m" };
static const char* const ROD_RU[] = { "2 м", "2,5 м", "3 м" };
static const float ROD_LEN[] = { 2.0f, 2.5f, 3.0f };
static const char* const ZONE_EN[] = { "Don't know / not needed", "Zone I - north, long hard winter", "Zone II - central Russia", "Zone III - mild climate", "Zone IV - south, soil does not freeze" };
static const char* const ZONE_RU[] = { "Не знаю / не учитывать", "Зона I - север, долгая суровая зима", "Зона II - средняя полоса", "Зона III - умеренный климат", "Зона IV - юг, грунт не промерзает" };
static const float ZONE_K[] = { 1.0f, 1.9f, 1.5f, 1.3f, 1.15f };
static const char* const REL_EN[] = { "0.9 - ordinary buildings", "0.99 - important objects", "0.999 - especially important objects" };
static const char* const REL_RU[] = { "0,9 - обычные здания", "0,99 - важные объекты", "0,999 - особо важные объекты" };

// ======================= ВЫВОД РЕЗУЛЬТАТОВ =======================
static void Row(const char* label, const char* value, const ImVec4& color) {
    ImGui::TextColored(COL_DIM, "%s", label);
    ImGui::SameLine(ImGui::GetContentRegionMax().x - ImGui::CalcTextSize(value).x);
    ImGui::TextColored(color, "%s", value);
    ImGui::Dummy(ImVec2(0.0f, 4.0f));
}

static void Verdict(const char* text, const ImVec4& color) {
    ImGui::Dummy(ImVec2(0.0f, 6.0f));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0.0f, 6.0f));
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(color, "%s", text);
    ImGui::PopTextWrapPos();
}

static void Note(const char* text) {
    ImGui::Dummy(ImVec2(0.0f, 4.0f));
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(COL_DIM, "%s", text);
    ImGui::PopTextWrapPos();
}

// ======================= РАСЧЁТЫ =======================
// Допустимые длительные токи по ПУЭ (7-е изд.), табл. 1.3.4-1.3.7. 0 = сечения нет.
static const float PUE_S[] = { 1.5f, 2.5f, 4.0f, 6.0f, 10.0f, 16.0f, 25.0f, 35.0f, 50.0f, 70.0f, 95.0f, 120.0f, 150.0f };
constexpr int PUE_N = 13;
static const float CU_OPEN[PUE_N] = { 23, 30, 41, 50, 80, 100, 140, 170, 215, 270, 330, 385, 440 };
static const float CU_PIPE2[PUE_N] = { 19, 27, 38, 46, 70, 85, 115, 135, 185, 225, 275, 315, 360 };
static const float CU_PIPE3[PUE_N] = { 17, 25, 35, 42, 60, 80, 100, 125, 170, 210, 255, 290, 330 };
static const float CU_EARTH2[PUE_N] = { 33, 44, 55, 70, 105, 135, 175, 210, 265, 320, 385, 445, 505 };
static const float CU_EARTH3[PUE_N] = { 27, 38, 49, 60, 90, 115, 150, 180, 225, 275, 330, 385, 435 };
static const float AL_OPEN[PUE_N] = { 0, 24, 32, 39, 60, 75, 105, 130, 165, 210, 255, 295, 340 };
static const float AL_PIPE2[PUE_N] = { 0, 20, 28, 36, 50, 60, 85, 100, 140, 175, 215, 245, 275 };
static const float AL_PIPE3[PUE_N] = { 0, 19, 28, 32, 47, 60, 80, 95, 130, 165, 200, 220, 255 };
static const float AL_EARTH2[PUE_N] = { 0, 34, 42, 55, 80, 105, 135, 160, 205, 245, 295, 340, 390 };
static const float AL_EARTH3[PUE_N] = { 0, 29, 38, 46, 70, 90, 115, 140, 175, 210, 255, 295, 335 };

static const int BREAKERS[] = { 6, 10, 16, 20, 25, 32, 40, 50, 63, 80, 100, 125, 160, 200, 250 };

static float TableCurrent(int i, bool cu, bool three, int inst) {
    switch (inst) {
    case 1:  return cu ? (three ? CU_PIPE3[i] : CU_PIPE2[i]) : (three ? AL_PIPE3[i] : AL_PIPE2[i]);
    case 2:  return cu ? (three ? CU_EARTH3[i] : CU_EARTH2[i]) : (three ? AL_EARTH3[i] : AL_EARTH2[i]);
    default: return cu ? CU_OPEN[i] : AL_OPEN[i];
    }
}

// Ток нагрузки, А
static float LoadCurrent(float power_kw, bool three, float cosf) {
    const float u = three ? 380.0f : 220.0f;
    return three ? power_kw * 1000.0f / (1.732f * u * cosf) : power_kw * 1000.0f / (u * cosf);
}

// Ближайший стандартный автомат не меньше тока (0 = больше 250 А)
static int BreakerFor(float current) {
    for (int r : BREAKERS) if ((float)r >= current) return r;
    return 0;
}

// Наибольший автомат для тонких сечений (1,5 / 2,5 / 4 / 6 мм2) - как принято на практике,
// строже таблиц ПУЭ: у современных кабелей допустимый ток ниже табличного.
static const int SMALL_MAX_CU[4] = { 16, 25, 32, 40 };
static const int SMALL_MAX_AL[4] = { 0, 16, 25, 32 };

static void ResultCable() {
    const bool three = (c_net == 1);
    const bool cu = (c_mat == 0);
    const float u = three ? 380.0f : 220.0f;
    const float cosf = LOAD_COS[c_load];
    const float sinf_v = sqrtf(1.0f - cosf * cosf);
    const float current = LoadCurrent(c_power, three, cosf);
    const int in_rating = BreakerFor(current);

    const float rho = (cu ? 0.0175f : 0.028f) * 1.18f;   // жила нагрета до +65 °C
    const float x0 = 0.00008f;                           // Ом/м
    const float k = three ? 1.732f : 2.0f;
    const float u_ph = three ? u / 1.732f : u;
    const float ik_min = (float)in_rating * 10.0f * 1.1f;   // автомат C, запас 1,1

    int heat = -1, chosen = -1;
    float drop_v = 0.0f, allow = 0.0f;
    for (int i = 0; i < PUE_N && in_rating > 0; ++i) {
        const float it = TableCurrent(i, cu, three, c_inst);
        if (it <= 0.0f || it < current || it < (float)in_rating) continue;
        if (i < 4 && in_rating > (cu ? SMALL_MAX_CU[i] : SMALL_MAX_AL[i])) continue;   // тонкий кабель - не под большой автомат
        if (heat < 0) heat = i;
        const float s = PUE_S[i];
        const float dv = k * current * c_len * (rho / s * cosf + x0 * sinf_v);
        if (dv / u * 100.0f > 5.0f) continue;                // падение напряжения не больше 5 %
        const float r = 2.0f * c_len * rho / s, x = 2.0f * c_len * x0;
        const float ik = u_ph / (sqrtf(r * r + x * x) + 0.03f);
        if (ik < ik_min) continue;                           // автомат должен отключить КЗ в конце линии
        chosen = i; drop_v = dv; allow = it;
        break;
    }

    char buf[96];
    snprintf(buf, sizeof(buf), "%.1f A", current);
    Row(TR("Load current", "Ток нагрузки"), buf, COL_TEXT);

    if (chosen < 0) {
        Verdict(TR("No cable up to 150 mm2 fits this load and length. Split the load into several lines or ask a designer.",
            "Кабель до 150 мм2 для такой нагрузки и длины не подходит. Разделите нагрузку на несколько линий или обратитесь к проектировщику."), COL_BAD);
        return;
    }

    const float s = PUE_S[chosen];
    const int cores = three ? 5 : 3;
    snprintf(buf, sizeof(buf), "C%d", in_rating);
    Row(TR("Circuit breaker", "Автомат"), buf, COL_ACCENT);
    snprintf(buf, sizeof(buf), "%g %s", (double)s, TR("mm2", "мм2"));
    Row(TR("Core cross-section", "Сечение жилы"), buf, COL_GOOD);
    snprintf(buf, sizeof(buf), "%dx%g, %s", cores, (double)s, cu ? TR("copper", "медь") : TR("aluminium", "алюминий"));
    Row(TR("Cable", "Кабель"), buf, COL_GOOD);
    snprintf(buf, sizeof(buf), "%.0f A", allow);
    Row(TR("Cable allowable current", "Допустимый ток кабеля"), buf, COL_TEXT);
    snprintf(buf, sizeof(buf), "%.1f V (%.1f %%)", drop_v, drop_v / u * 100.0f);
    Row(TR("Voltage drop", "Падение напряжения"), buf, COL_TEXT);

    char msg[256];
    if (g_lang == 1)
        snprintf(msg, sizeof(msg), "Берите кабель %dx%g мм2 (%s) и автомат C%d.", cores, (double)s, cu ? "медь" : "алюминий", in_rating);
    else
        snprintf(msg, sizeof(msg), "Take a %dx%g mm2 %s cable and a C%d breaker.", cores, (double)s, cu ? "copper" : "aluminium", in_rating);
    Verdict(msg, COL_GOOD);
    if (chosen > heat)
        Note(TR("The section is larger than heating alone requires: the line is long, so voltage drop or short-circuit tripping decided.",
            "Сечение больше, чем нужно только по нагреву: линия длинная, поэтому решило падение напряжения или отключение при коротком замыкании."));
    Note(three
        ? TR("Cores: three phases, neutral and protective earth. Calculated per PUE tables 1.3.4-1.3.7.",
            "Жилы: три фазы, ноль и защитная земля. Расчёт по таблицам ПУЭ 1.3.4-1.3.7.")
        : TR("Cores: phase, neutral and protective earth. Calculated per PUE tables 1.3.4-1.3.7.",
            "Жилы: фаза, ноль и защитная земля. Расчёт по таблицам ПУЭ 1.3.4-1.3.7."));
}

static void ResultBreaker() {
    const bool three = (b_net == 1);
    const float current = LoadCurrent(b_power, three, LOAD_COS[b_type]);
    const int in_rating = BreakerFor(current);
    static const char CURVE[] = { 'B', 'C', 'D' };

    char buf[96];
    snprintf(buf, sizeof(buf), "%.1f A", current);
    Row(TR("Load current", "Ток нагрузки"), buf, COL_TEXT);
    if (in_rating == 0) {
        Verdict(TR("The current is above 250 A. Such a breaker is chosen by a designer.",
            "Ток больше 250 А. Такой автомат подбирает проектировщик."), COL_BAD);
        return;
    }
    snprintf(buf, sizeof(buf), "%c%d", CURVE[b_type], in_rating);
    Row(TR("Circuit breaker", "Автомат"), buf, COL_GOOD);
    snprintf(buf, sizeof(buf), "%d", three ? 3 : 1);
    Row(TR("Poles", "Полюсов"), buf, COL_TEXT);

    // наименьший медный кабель, который этот автомат защитит (прокладка в трубе - с запасом)
    float s_min = 0.0f;
    for (int i = 0; i < PUE_N; ++i)
        if ((three ? CU_PIPE3[i] : CU_PIPE2[i]) >= (float)in_rating) { s_min = PUE_S[i]; break; }
    if (s_min > 0.0f) {
        snprintf(buf, sizeof(buf), "%g %s", (double)s_min, TR("mm2", "мм2"));
        Row(TR("Copper cable, not thinner than", "Медный кабель не тоньше"), buf, COL_ACCENT);
    }

    char msg[256];
    if (g_lang == 1) snprintf(msg, sizeof(msg), "Ставьте автомат %c%d.", CURVE[b_type], in_rating);
    else snprintf(msg, sizeof(msg), "Install a %c%d breaker.", CURVE[b_type], in_rating);
    Verdict(msg, COL_GOOD);
    Note(TR("B - lighting and heaters, C - sockets and ordinary loads, D - motors with heavy start. The breaker protects the cable, so the cable must not be thinner than shown.",
        "B - освещение и нагреватели, C - розетки и обычная нагрузка, D - двигатели с тяжёлым пуском. Автомат защищает кабель, поэтому кабель не должен быть тоньше указанного."));
}

static void ResultLoad() {
    const bool three = (l_net == 1);
    const float p_calc = l_power * SIM_K[l_sim];
    const float current = LoadCurrent(p_calc, three, 0.95f);
    const float day = p_calc * l_hours;

    char buf[96];
    snprintf(buf, sizeof(buf), "%.2f %s", p_calc, TR("kW", "кВт"));
    Row(TR("Design power", "Расчётная мощность"), buf, COL_TEXT);
    snprintf(buf, sizeof(buf), "%.1f A", current);
    Row(TR("Current", "Ток"), buf, COL_ACCENT);
    snprintf(buf, sizeof(buf), "%.1f %s", day, TR("kWh", "кВт*ч"));
    Row(TR("Energy per day", "Расход за день"), buf, COL_TEXT);
    snprintf(buf, sizeof(buf), "%.0f %s", day * 30.0f, TR("kWh", "кВт*ч"));
    Row(TR("Energy per month", "Расход за месяц"), buf, COL_TEXT);
    snprintf(buf, sizeof(buf), "%.0f", day * 30.0f * l_price);
    Row(TR("Cost per month", "Стоимость за месяц"), buf, COL_GOOD);

    const int in_rating = BreakerFor(current);
    char msg[256];
    if (in_rating <= 0)
        snprintf(msg, sizeof(msg), "%s", TR("The current is above 250 A, the input is chosen by a designer.", "Ток больше 250 А, ввод подбирает проектировщик."));
    else if (g_lang == 1)
        snprintf(msg, sizeof(msg), "Вводной автомат под такую нагрузку: C%d.", in_rating);
    else
        snprintf(msg, sizeof(msg), "Input breaker for this load: C%d.", in_rating);
    Verdict(msg, in_rating > 0 ? COL_GOOD : COL_BAD);
    Note(TR("A month is counted as 30 days. Power factor is taken as 0.95.", "Месяц считается за 30 дней. Коэффициент мощности принят 0,95."));
}

static void ResultMotor() {
    const bool three = (m_net == 0);
    const float u = three ? 380.0f : 220.0f;
    const float cosf = 0.85f, eff = 0.90f, ratio = 6.5f;
    const float p_in = m_power / eff;
    const float in_m = three ? p_in * 1000.0f / (1.732f * u * cosf) : p_in * 1000.0f / (u * cosf);
    float kstart = ratio;
    if (m_start == 1) kstart = ratio / 3.0f;
    else if (m_start == 2) kstart = 3.0f;
    else if (m_start == 3) kstart = 1.5f;
    const float ist = in_m * kstart;

    // автомат: не меньше 1,25 Iн и не должен сработать от пускового тока
    int best = 0;
    char curve = 'C';
    for (int r : BREAKERS) {
        if ((float)r < in_m * 1.25f) continue;
        if (1.2f * ist <= 5.0f * (float)r) { best = r; curve = 'C'; break; }
        if (1.2f * ist <= 10.0f * (float)r) { best = r; curve = 'D'; break; }
    }
    static const int CONTACTORS[] = { 9, 12, 18, 25, 32, 40, 50, 65, 80, 95, 115, 150, 185, 225, 265, 330 };
    int contactor = 0;
    for (int c : CONTACTORS) if ((float)c >= in_m) { contactor = c; break; }

    char buf[96];
    snprintf(buf, sizeof(buf), "%.1f A", in_m);
    Row(TR("Rated current", "Номинальный ток"), buf, COL_ACCENT);
    snprintf(buf, sizeof(buf), "%.1f A", ist);
    Row(TR("Starting current", "Пусковой ток"), buf, COL_WARN);
    if (best > 0) snprintf(buf, sizeof(buf), "%c%d", curve, best);
    else snprintf(buf, sizeof(buf), "> 250 A");
    Row(TR("Circuit breaker", "Автомат"), buf, best > 0 ? COL_GOOD : COL_BAD);
    if (contactor > 0) snprintf(buf, sizeof(buf), "%d A (AC-3)", contactor);
    else snprintf(buf, sizeof(buf), "> 330 A");
    Row(TR("Contactor", "Контактор (пускатель)"), buf, contactor > 0 ? COL_GOOD : COL_BAD);
    snprintf(buf, sizeof(buf), "%.1f - %.1f A", in_m * 0.9f, in_m * 1.1f);
    Row(TR("Thermal relay setting range", "Диапазон уставки теплового реле"), buf, COL_TEXT);

    if (best > 0 && contactor > 0) {
        char msg[256];
        if (g_lang == 1)
            snprintf(msg, sizeof(msg), "Автомат %c%d, контактор на %d А, тепловое реле выставить на %.1f А.", curve, best, contactor, in_m);
        else
            snprintf(msg, sizeof(msg), "Breaker %c%d, contactor %d A, thermal relay set to %.1f A.", curve, best, contactor, in_m);
        Verdict(msg, COL_GOOD);
    }
    else {
        Verdict(TR("The motor is too large for this calculator, ask a designer.", "Двигатель слишком мощный для этого калькулятора, обратитесь к проектировщику."), COL_BAD);
    }
    Note(TR("Typical values are assumed: power factor 0.85, efficiency 0.9, direct-start current 6.5 times the rated one. Check the motor nameplate.",
        "Приняты типовые значения: cos φ 0,85, КПД 0,9, пусковой ток при прямом пуске в 6,5 раза больше номинального. Сверьте с шильдиком двигателя."));
}

// Коэффициент использования вертикальных электродов в ряд при шаге, равном длине электрода
static float GroundEta(int n) {
    static const float N_PTS[7] = { 1, 2, 3, 5, 10, 15, 20 };
    static const float ETA[7] = { 1.0f, 0.85f, 0.78f, 0.70f, 0.59f, 0.54f, 0.49f };
    if (n <= 1) return 1.0f;
    float fn = (float)n;
    if (fn > 20.0f) fn = 20.0f;
    for (int j = 0; j < 6; ++j) {
        if (fn <= N_PTS[j + 1]) {
            const float t = (fn - N_PTS[j]) / (N_PTS[j + 1] - N_PTS[j]);
            return ETA[j] + (ETA[j + 1] - ETA[j]) * t;
        }
    }
    return ETA[6];
}

static void ResultGround() {
    const float rho = SOIL_RHO[g_soil] * ZONE_K[g_zone];
    const float len = ROD_LEN[g_rod];
    const float d = 0.016f;                 // круглая сталь 16 мм
    const float mid = 0.7f + len * 0.5f;    // глубина середины электрода (верх на 0,7 м)
    const float r1 = rho / (2.0f * 3.14159265f * len) *
        (logf(2.0f * len / d) + 0.5f * logf((4.0f * mid + len) / (4.0f * mid - len)));
    const float norm = NORM_OHM[g_norm];

    int need = 0;
    float total = 0.0f;
    for (int n = 1; n <= 100; ++n) {
        const float r = r1 / ((float)n * GroundEta(n));
        if (r <= norm) { need = n; total = r; break; }
    }

    char buf[96];
    snprintf(buf, sizeof(buf), "%.0f %s", rho, TR("Ohm*m", "Ом*м"));
    Row(TR("Design soil resistivity", "Расчётное сопротивление грунта"), buf, COL_TEXT);
    snprintf(buf, sizeof(buf), "%.1f %s", r1, TR("Ohm", "Ом"));
    Row(TR("One rod", "Один электрод"), buf, COL_TEXT);

    if (need == 0) {
        Verdict(TR("Even 100 rods are not enough. Take longer rods or deep earthing.",
            "Даже 100 электродов не хватает. Нужны электроды длиннее или глубинное заземление."), COL_BAD);
        return;
    }
    snprintf(buf, sizeof(buf), "%d", need);
    Row(TR("Rods needed", "Нужно электродов"), buf, COL_GOOD);
    snprintf(buf, sizeof(buf), "%.2f %s", total, TR("Ohm", "Ом"));
    Row(TR("Resulting resistance", "Получится сопротивление"), buf, COL_GOOD);

    char msg[256];
    if (need == 1) {
        if (g_lang == 1) snprintf(msg, sizeof(msg), "Хватит одного электрода длиной %g м.", (double)len);
        else snprintf(msg, sizeof(msg), "One rod of %g m is enough.", (double)len);
    }
    else if (g_lang == 1)
        snprintf(msg, sizeof(msg), "Забейте %d электродов по %g м в ряд с шагом %g м и соедините стальной полосой.", need, (double)len, (double)len);
    else
        snprintf(msg, sizeof(msg), "Drive %d rods of %g m in a row, %g m apart, and join them with a steel strip.", need, (double)len, (double)len);
    Verdict(msg, COL_GOOD);
    Note(TR("Rods are round steel 16 mm, the top is 0.7 m deep. After installation the resistance must be measured.",
        "Электроды - круглая сталь 16 мм, верх на глубине 0,7 м. После монтажа сопротивление обязательно измеряют прибором."));
}

// Радиус зоны защиты на высоте hx для молниеотвода высотой h (СО 153-34.21.122-2003)
static float LightningZone(float h, float hx, float* r0_out) {
    float h0 = 0.85f * h, r0 = 1.2f * h;                       // надёжность 0,9
    if (z_rel == 0) {
        if (h > 100.0f) r0 = (1.2f - 0.001f * (h - 100.0f)) * h;
    }
    else if (z_rel == 1) {                                     // 0,99
        h0 = 0.8f * h;
        r0 = (h <= 30.0f) ? 0.8f * h : (0.8f - 0.00143f * (h - 30.0f)) * h;
    }
    else {                                                     // 0,999
        h0 = (h <= 30.0f) ? 0.7f * h : (0.7f - 0.000714f * (h - 30.0f)) * h;
        r0 = (h <= 30.0f) ? 0.6f * h : (0.6f - 0.00143f * (h - 30.0f)) * h;
    }
    if (r0_out) *r0_out = r0;
    return (hx < h0 && h0 > 0.0f) ? r0 * (h0 - hx) / h0 : 0.0f;
}

static void ResultLightning() {
    const float h_max = (z_rel == 0) ? 150.0f : 100.0f;
    float h_min = 0.0f;
    for (float h = (z_hx > 0.5f ? z_hx : 0.5f); h <= h_max; h += 0.1f) {
        if (LightningZone(h, z_hx, nullptr) >= z_dist) { h_min = h; break; }
    }
    if (h_min <= 0.0f) {
        Verdict(TR("One rod cannot protect this object. Several rods or a wire are needed - ask a designer.",
            "Один молниеотвод такой объект не закроет. Нужны несколько молниеотводов или трос - обратитесь к проектировщику."), COL_BAD);
        return;
    }
    float r0 = 0.0f;
    const float rx = LightningZone(h_min, z_hx, &r0);

    char buf[96];
    snprintf(buf, sizeof(buf), "%.1f %s", h_min, TR("m", "м"));
    Row(TR("Rod height, not less than", "Высота молниеотвода не меньше"), buf, COL_GOOD);
    snprintf(buf, sizeof(buf), "%.1f %s", r0, TR("m", "м"));
    Row(TR("Protected radius at ground level", "Радиус защиты на земле"), buf, COL_TEXT);
    snprintf(buf, sizeof(buf), "%.1f %s", rx, TR("m", "м"));
    Row(TR("Protected radius at object height", "Радиус защиты на высоте объекта"), buf, COL_TEXT);

    char msg[256];
    if (g_lang == 1) snprintf(msg, sizeof(msg), "Молниеотвод высотой %.1f м (от земли) закрывает объект целиком.", h_min);
    else snprintf(msg, sizeof(msg), "A rod %.1f m high (measured from the ground) covers the whole object.", h_min);
    Verdict(msg, COL_GOOD);
    Note(TR("Single rod, cone-shaped zone per SO 153-34.21.122-2003. The rod needs its own earthing.",
        "Одиночный стержневой молниеотвод, зона в виде конуса по СО 153-34.21.122-2003. Молниеотводу нужно своё заземление."));
}

// ======================= СПИСОК КАЛЬКУЛЯТОРОВ =======================
#define CHOICE(qe, qr, he, hr, var, oe, orr) { qe, qr, he, hr, StepKind::Choice, &var, oe, orr, (int)(sizeof(oe) / sizeof(oe[0])), nullptr, "", "", 0.0f, 0.0f }
#define NUMBER(qe, qr, he, hr, var, ue, ur, mn, mx) { qe, qr, he, hr, StepKind::Number, nullptr, nullptr, nullptr, 0, &var, ue, ur, mn, mx }

static const Step STEPS_CABLE[] = {
    CHOICE("What is your network?", "Какая у вас сеть?", "A flat or a house is usually 220 V.", "В квартире и обычном доме чаще всего 220 В.", c_net, NET_EN, NET_RU),
    NUMBER("What is the total power of the load?", "Какая мощность нагрузки?", "Add up the power of everything this cable will feed. It is written on the appliance plate. 1000 W = 1 kW.",
        "Сложите мощность всего, что будет питать этот кабель. Она написана на табличке прибора. 1000 Вт = 1 кВт.", c_power, "kW", "кВт", 0.01f, 500.0f),
    CHOICE("What will be connected?", "Что будет подключено?", "This affects the current at the same power.", "От этого зависит ток при той же мощности.", c_load, LOAD_EN, LOAD_RU),
    NUMBER("How long is the cable?", "Какая длина кабеля?", "From the panel to the farthest consumer, along the actual route.", "От щитка до самого дальнего потребителя, по реальной трассе.", c_len, "m", "м", 0.5f, 2000.0f),
    CHOICE("How will the cable be laid?", "Как будет проложен кабель?", "A cable in a pipe cools worse and carries less current.", "В трубе кабель охлаждается хуже и держит меньший ток.", c_inst, INST_EN, INST_RU),
    CHOICE("What are the cores made of?", "Из чего жилы кабеля?", "Copper is used for new wiring in homes.", "Для новой проводки в жилье берут медь.", c_mat, MAT_EN, MAT_RU),
};
static const Step STEPS_BREAKER[] = {
    CHOICE("What is your network?", "Какая у вас сеть?", "", "", b_net, NET_EN, NET_RU),
    NUMBER("What is the power of the load on this line?", "Какая мощность нагрузки на этой линии?", "1000 W = 1 kW.", "1000 Вт = 1 кВт.", b_power, "kW", "кВт", 0.01f, 500.0f),
    CHOICE("What is connected to this line?", "Что подключено к этой линии?", "This decides the breaker type: B, C or D.", "От этого зависит тип автомата: B, C или D.", b_type, LOAD_EN, LOAD_RU),
};
static const Step STEPS_LOAD[] = {
    CHOICE("What is your network?", "Какая у вас сеть?", "", "", l_net, NET_EN, NET_RU),
    NUMBER("What is the total power of all appliances?", "Какая суммарная мощность всех приборов?", "Add up everything that is connected. 1000 W = 1 kW.", "Сложите всё, что подключено. 1000 Вт = 1 кВт.", l_power, "kW", "кВт", 0.01f, 1000.0f),
    CHOICE("How much of it runs at the same time?", "Сколько из этого работает одновременно?", "Appliances are rarely all switched on at once.", "Приборы редко включены все сразу.", l_sim, SIM_EN, SIM_RU),
    NUMBER("How many hours a day does it run?", "Сколько часов в день это работает?", "On average.", "В среднем.", l_hours, "h", "ч", 0.0f, 24.0f),
    NUMBER("What is the price of one kWh?", "Сколько стоит один кВт*ч?", "See your electricity bill.", "Посмотрите в квитанции за электричество.", l_price, "", "", 0.0f, 1000.0f),
};
static const char* const MNET_EN[] = { "380 V, three-phase", "220 V, single-phase" };
static const char* const MNET_RU[] = { "380 В, три фазы", "220 В, одна фаза" };
static const Step STEPS_MOTOR[] = {
    NUMBER("What is the motor power?", "Какая мощность двигателя?", "Shaft power from the nameplate.", "Мощность на валу, с шильдика двигателя.", m_power, "kW", "кВт", 0.05f, 200.0f),
    CHOICE("What network is the motor on?", "В какую сеть включён двигатель?", "", "", m_net, MNET_EN, MNET_RU),
    CHOICE("How is the motor started?", "Как запускается двигатель?", "The starting current depends on this.", "От этого зависит пусковой ток.", m_start, START_EN, START_RU),
};
static const Step STEPS_GROUND[] = {
    CHOICE("What soil do you have?", "Какой у вас грунт?", "The wetter and denser the soil, the easier the earthing.", "Чем грунт влажнее и плотнее, тем проще сделать заземление.", g_soil, SOIL_EN, SOIL_RU),
    CHOICE("What resistance is required?", "Какое сопротивление нужно получить?", "For a private house 30 Ohm is usually enough.", "Для частного дома обычно достаточно 30 Ом.", g_norm, NORM_EN, NORM_RU),
    CHOICE("How long are the rods?", "Какой длины электроды?", "The length of one rod driven into the ground.", "Длина одного штыря, который забивают в землю.", g_rod, ROD_EN, ROD_RU),
    CHOICE("What is your climate?", "Какой у вас климат?", "In winter the soil freezes and conducts worse.", "Зимой грунт промерзает и хуже проводит ток.", g_zone, ZONE_EN, ZONE_RU),
};
static const Step STEPS_LIGHTNING[] = {
    NUMBER("How tall is the object?", "Какая высота объекта?", "The highest point of the building.", "Самая высокая точка здания.", z_hx, "m", "м", 0.5f, 90.0f),
    NUMBER("How far is the farthest corner from the rod?", "Сколько от молниеотвода до самого дальнего угла?", "Horizontally, at roof level.", "По горизонтали, на уровне крыши.", z_dist, "m", "м", 0.5f, 100.0f),
    CHOICE("How reliable must the protection be?", "Какая нужна надёжность защиты?", "For a house or a garage 0.9 is enough.", "Для дома или гаража хватает 0,9.", z_rel, REL_EN, REL_RU),
};

#define COUNT(a) (int)(sizeof(a) / sizeof(a[0]))
static const Calc CALCS[] = {
    { "Cable", "Кабель", "Which cable and breaker to take", "Какой кабель и автомат взять", STEPS_CABLE, COUNT(STEPS_CABLE), ResultCable },
    { "Circuit breaker", "Автомат", "Which breaker suits the load", "Какой автомат подойдёт под нагрузку", STEPS_BREAKER, COUNT(STEPS_BREAKER), ResultBreaker },
    { "Load and consumption", "Нагрузка и расход", "Current, energy and cost per month", "Ток, расход энергии и стоимость за месяц", STEPS_LOAD, COUNT(STEPS_LOAD), ResultLoad },
    { "Motor", "Двигатель", "Breaker, contactor and thermal relay", "Автомат, пускатель и тепловое реле", STEPS_MOTOR, COUNT(STEPS_MOTOR), ResultMotor },
    { "Earthing", "Заземление", "How many rods to drive in", "Сколько электродов забить", STEPS_GROUND, COUNT(STEPS_GROUND), ResultGround },
    { "Lightning rod", "Молниеотвод", "How tall the rod must be", "Какой высоты нужен молниеотвод", STEPS_LIGHTNING, COUNT(STEPS_LIGHTNING), ResultLightning },
};
constexpr int CALCS_N = COUNT(CALCS);

// ======================= СОСТОЯНИЕ МАСТЕРА =======================
static int   g_calc = -1;       // -1 = меню выбора
static int   g_step = 0;        // номер шага; равен числу шагов - экран результата
static float g_anim = 0.0f;     // время с момента появления блока
static char  g_num[32] = "";    // текст в поле ввода числа
static bool  g_focus = false;   // поставить курсор в поле ввода

static void GoTo(int calc, int step) {
    g_calc = calc;
    g_step = step;
    g_anim = 0.0f;
    g_focus = true;
    if (calc >= 0 && step < CALCS[calc].steps_count) {
        const Step& s = CALCS[calc].steps[step];
        if (s.kind == StepKind::Number) snprintf(g_num, sizeof(g_num), "%g", (double)*s.value);
    }
}

// ======================= ВИДЖЕТЫ =======================
// Большая кнопка-плашка: заголовок и необязательная подпись
static bool BigButton(const char* id, const char* title, const char* sub, bool selected, float w, float h) {
    ImGui::PushID(id);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton("##big", ImVec2(w, h));
    const bool hovered = ImGui::IsItemHovered();
    if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 q(p.x + w, p.y + h);
    ImVec4 bg = COL_FRAME;
    if (selected) bg = ImVec4(COL_ACCENT.x * 0.30f, COL_ACCENT.y * 0.30f, COL_ACCENT.z * 0.30f + 0.05f, 1.0f);
    else if (hovered) bg = ImVec4(COL_FRAME.x + 0.04f, COL_FRAME.y + 0.04f, COL_FRAME.z + 0.05f, 1.0f);
    dl->AddRectFilled(p, q, ImGui::GetColorU32(bg), 10.0f);
    dl->AddRect(p, q, ImGui::GetColorU32(selected ? COL_ACCENT : (hovered ? WithAlpha(COL_ACCENT, 0.55f) : COL_BORDER)), 10.0f, 0, selected ? 2.0f : 1.0f);
    const float line = ImGui::GetTextLineHeight();
    if (sub && sub[0]) {
        dl->AddText(ImVec2(p.x + 18.0f, p.y + h * 0.5f - line - 1.0f), ImGui::GetColorU32(COL_TEXT), title);
        dl->AddText(ImVec2(p.x + 18.0f, p.y + h * 0.5f + 2.0f), ImGui::GetColorU32(COL_DIM), sub);
    }
    else {
        dl->AddText(ImVec2(p.x + 18.0f, p.y + (h - line) * 0.5f), ImGui::GetColorU32(COL_TEXT), title);
    }
    ImGui::PopID();
    return pressed;
}

// Обычная кнопка: основная (цветом акцента) или второстепенная
static bool ActionButton(const char* label, bool primary, bool enabled, float w) {
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 9.0f);
    if (primary) {
        ImGui::PushStyleColor(ImGuiCol_Button, WithAlpha(COL_ACCENT, enabled ? 0.85f : 0.25f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, WithAlpha(COL_ACCENT, enabled ? 1.0f : 0.25f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, WithAlpha(COL_ACCENT, enabled ? 0.70f : 0.25f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.03f, 0.05f, 0.09f, enabled ? 1.0f : 0.6f));
    }
    else {
        ImGui::PushStyleColor(ImGuiCol_Button, COL_FRAME);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(COL_FRAME.x + 0.05f, COL_FRAME.y + 0.05f, COL_FRAME.z + 0.06f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, COL_FRAME);
        ImGui::PushStyleColor(ImGuiCol_Text, COL_TEXT);
    }
    const bool pressed = ImGui::Button(label, ImVec2(w, 44.0f)) && enabled;
    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar();
    return pressed;
}

// В поле числа пускаем только цифры и точку; запятая становится точкой
static int NumberFilter(ImGuiInputTextCallbackData* d) {
    if (d->EventChar == ',') { d->EventChar = '.'; return 0; }
    if ((d->EventChar >= '0' && d->EventChar <= '9') || d->EventChar == '.') return 0;
    return 1;
}

// ======================= ЭКРАНЫ =======================
static void ScreenMenu(float card_w) {
    ImGui::PushFont(nullptr, 26.0f);
    ImGui::TextColored(COL_TEXT, "%s", TR("What do you want to calculate?", "Что вы хотите рассчитать?"));
    ImGui::PopFont();
    ImGui::TextColored(COL_DIM, "%s", TR("Choose one. Then answer a few simple questions.", "Выберите одно. Дальше ответите на несколько простых вопросов."));
    ImGui::Dummy(ImVec2(0.0f, 12.0f));
    for (int i = 0; i < CALCS_N; ++i) {
        char id[16];
        snprintf(id, sizeof(id), "calc%d", i);
        if (BigButton(id, TR(CALCS[i].name_en, CALCS[i].name_ru), TR(CALCS[i].desc_en, CALCS[i].desc_ru), false, card_w, 62.0f))
            GoTo(i, 0);
        ImGui::Dummy(ImVec2(0.0f, 4.0f));
    }
}

// Строка "Кабель - шаг 2 из 6" и полоска прогресса
static void StepHeader(const Calc& calc, int step, float card_w) {
    char head[96];
    if (step < calc.steps_count)
        snprintf(head, sizeof(head), "%s  -  %s %d %s %d", TR(calc.name_en, calc.name_ru), TR("step", "шаг"), step + 1, TR("of", "из"), calc.steps_count);
    else
        snprintf(head, sizeof(head), "%s  -  %s", TR(calc.name_en, calc.name_ru), TR("result", "результат"));
    ImGui::TextColored(COL_ACCENT, "%s", head);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float done = (float)(step < calc.steps_count ? step : calc.steps_count) / (float)calc.steps_count;
    dl->AddRectFilled(p, ImVec2(p.x + card_w, p.y + 5.0f), ImGui::GetColorU32(COL_FRAME), 3.0f);
    if (done > 0.0f)
        dl->AddRectFilled(p, ImVec2(p.x + card_w * done, p.y + 5.0f), ImGui::GetColorU32(COL_ACCENT), 3.0f);
    ImGui::Dummy(ImVec2(card_w, 5.0f));
    ImGui::Dummy(ImVec2(0.0f, 14.0f));
}

static void ScreenStep(const Calc& calc, float card_w, float bottom_y) {
    const Step& s = calc.steps[g_step];
    StepHeader(calc, g_step, card_w);

    ImGui::PushFont(nullptr, 26.0f);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(COL_TEXT, "%s", TR(s.q_en, s.q_ru));
    ImGui::PopTextWrapPos();
    ImGui::PopFont();
    const char* hint = TR(s.hint_en, s.hint_ru);
    if (hint[0]) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(COL_DIM, "%s", hint);
        ImGui::PopTextWrapPos();
    }
    ImGui::Dummy(ImVec2(0.0f, 12.0f));

    bool valid = true;
    if (s.kind == StepKind::Choice) {
        if (*s.choice < 0 || *s.choice >= s.opts_count) *s.choice = 0;
        for (int i = 0; i < s.opts_count; ++i) {
            char id[16];
            snprintf(id, sizeof(id), "opt%d", i);
            if (BigButton(id, TR(s.opts_en[i], s.opts_ru[i]), nullptr, *s.choice == i, card_w, 48.0f))
                *s.choice = i;
            ImGui::Dummy(ImVec2(0.0f, 3.0f));
        }
    }
    else {
        ImGui::PushFont(nullptr, 30.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 9.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(14.0f, 10.0f));
        ImGui::SetNextItemWidth(220.0f);
        if (g_focus) { ImGui::SetKeyboardFocusHere(); g_focus = false; }
        ImGui::InputText("##number", g_num, sizeof(g_num), ImGuiInputTextFlags_CallbackCharFilter | ImGuiInputTextFlags_AutoSelectAll, NumberFilter);
        const char* unit = TR(s.unit_en, s.unit_ru);
        if (unit[0]) {
            ImGui::SameLine(0.0f, 12.0f);
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(COL_DIM, "%s", unit);
        }
        ImGui::PopStyleVar(2);
        ImGui::PopFont();

        char* end = nullptr;
        const float v = strtof(g_num, &end);
        valid = (end != g_num) && (*end == 0) && v >= s.vmin && v <= s.vmax;
        if (valid) *s.value = v;
        else {
            char err[128];
            if (g_lang == 1) snprintf(err, sizeof(err), "Введите число от %g до %g", (double)s.vmin, (double)s.vmax);
            else snprintf(err, sizeof(err), "Enter a number from %g to %g", (double)s.vmin, (double)s.vmax);
            ImGui::Dummy(ImVec2(0.0f, 4.0f));
            ImGui::TextColored(COL_BAD, "%s", err);
        }
    }

    // кнопки внизу блока
    if (ImGui::GetCursorPosY() < bottom_y) ImGui::SetCursorPosY(bottom_y);
    else ImGui::Dummy(ImVec2(0.0f, 10.0f));
    const float bw = (card_w - 12.0f) * 0.5f;
    const bool back = ActionButton(TR("Back", "Назад"), false, true, bw) || ImGui::IsKeyPressed(ImGuiKey_Escape, false);
    ImGui::SameLine(0.0f, 12.0f);
    const bool last = (g_step == calc.steps_count - 1);
    const bool enter = ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false);
    const bool next = ActionButton(last ? TR("Calculate", "Рассчитать") : TR("Next", "Далее"), true, valid, bw) || (enter && valid);

    if (next) GoTo(g_calc, g_step + 1);
    else if (back) GoTo(g_step == 0 ? -1 : g_calc, g_step == 0 ? 0 : g_step - 1);
}

static void ScreenResult(const Calc& calc, float card_w, float bottom_y) {
    StepHeader(calc, calc.steps_count, card_w);
    ImGui::PushFont(nullptr, 26.0f);
    ImGui::TextColored(COL_TEXT, "%s", TR("Done", "Готово"));
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0.0f, 10.0f));

    calc.result();

    if (ImGui::GetCursorPosY() < bottom_y) ImGui::SetCursorPosY(bottom_y);
    else ImGui::Dummy(ImVec2(0.0f, 10.0f));
    const float bw = (card_w - 12.0f) * 0.5f;
    const bool edit = ActionButton(TR("Change the data", "Изменить данные"), false, true, bw) || ImGui::IsKeyPressed(ImGuiKey_Escape, false);
    ImGui::SameLine(0.0f, 12.0f);
    const bool fresh = ActionButton(TR("New calculation", "Новый расчёт"), true, true, bw);
    if (fresh) GoTo(-1, 0);
    else if (edit) GoTo(g_calc, 0);
}

// Один кадр интерфейса. w, h - размер клиентской области окна.
static void RenderUI(float w, float h) {
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(w, h));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::Begin("##root", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::PopStyleVar(3);

    // верхняя строка: название слева, язык справа
    ImGui::SetCursorPos(ImVec2(24.0f, 18.0f));
    ImGui::TextColored(COL_ACCENT, "ElectroGuiCalc Lite");
    ImGui::SameLine(0.0f, 8.0f);
    ImGui::TextColored(COL_DIM, "by iknlm");
    ImGui::SetCursorPos(ImVec2(w - 24.0f - 64.0f, 12.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f);
    ImGui::PushStyleColor(ImGuiCol_Button, COL_FRAME);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(COL_FRAME.x + 0.05f, COL_FRAME.y + 0.05f, COL_FRAME.z + 0.06f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, COL_FRAME);
    if (ImGui::Button(g_lang == 1 ? "EN" : "RU", ImVec2(64.0f, 30.0f))) g_lang = 1 - g_lang;
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar();

    // блок по центру
    float outer_w = w - 48.0f;
    if (outer_w > 680.0f) outer_w = 680.0f;
    if (outer_w < 320.0f) outer_w = 320.0f;
    const float outer_h = (h - 60.0f - 24.0f > 200.0f) ? h - 60.0f - 24.0f : 200.0f;
    ImGui::SetCursorPos(ImVec2((w - outer_w) * 0.5f, 60.0f));

    g_anim += ImGui::GetIO().DeltaTime;
    float a = g_anim / 0.18f;
    if (a > 1.0f) a = 1.0f;

    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 14.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(28.0f, 24.0f));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, COL_CARD);
    ImGui::BeginChild("##card", ImVec2(outer_w, outer_h), ImGuiChildFlags_AlwaysUseWindowPadding);
    {
        const float shift = (1.0f - a) * 18.0f;                         // новый блок выезжает и проявляется
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.05f + 0.95f * a);
        ImGui::PushID(g_calc * 100 + g_step);                           // у каждого блока свои виджеты
        if (shift > 0.01f) ImGui::Indent(shift);
        const float card_w = ImGui::GetContentRegionAvail().x - shift;
        const float bottom_y = outer_h - 24.0f - 44.0f;
        if (g_calc < 0 || g_calc >= CALCS_N) ScreenMenu(card_w);
        else if (g_step < CALCS[g_calc].steps_count) ScreenStep(CALCS[g_calc], card_w, bottom_y);
        else ScreenResult(CALCS[g_calc], card_w, bottom_y);
        if (shift > 0.01f) ImGui::Unindent(shift);
        ImGui::PopID();
        ImGui::PopStyleVar();
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);

    ImGui::End();
}

static void ApplyStyle() {
    ImGuiStyle& st = ImGui::GetStyle();
    st.WindowRounding = 0.0f;
    st.FrameRounding = 8.0f;
    st.ScrollbarRounding = 8.0f;
    st.ItemSpacing = ImVec2(8.0f, 6.0f);
    st.Colors[ImGuiCol_WindowBg] = COL_BG;
    st.Colors[ImGuiCol_Text] = COL_TEXT;
    st.Colors[ImGuiCol_FrameBg] = COL_FRAME;
    st.Colors[ImGuiCol_FrameBgHovered] = COL_FRAME;
    st.Colors[ImGuiCol_FrameBgActive] = COL_FRAME;
    st.Colors[ImGuiCol_Border] = COL_BORDER;
    st.Colors[ImGuiCol_Separator] = COL_BORDER;
    st.Colors[ImGuiCol_TextSelectedBg] = WithAlpha(COL_ACCENT, 0.40f);
    st.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    st.Colors[ImGuiCol_ScrollbarGrab] = COL_BORDER;
}

// ======================= WINDOWS: ОКНО И DIRECTX 11 =======================
#ifdef _WIN32
static ID3D11Device* g_device = nullptr;
static ID3D11DeviceContext* g_context = nullptr;
static IDXGISwapChain* g_swap = nullptr;
static ID3D11RenderTargetView* g_rtv = nullptr;
static bool g_ready = false;

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

static void CreateTarget() {
    ID3D11Texture2D* back = nullptr;
    if (SUCCEEDED(g_swap->GetBuffer(0, IID_PPV_ARGS(&back))) && back) {
        g_device->CreateRenderTargetView(back, nullptr, &g_rtv);
        back->Release();
    }
}

static void ReleaseTarget() {
    if (g_rtv) { g_rtv->Release(); g_rtv = nullptr; }
}

static bool CreateDevice(HWND hwnd) {
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    const D3D_FEATURE_LEVEL levels[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    D3D_FEATURE_LEVEL got;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 2,
        D3D11_SDK_VERSION, &sd, &g_swap, &g_device, &got, &g_context);
    if (FAILED(hr))   // нет видеокарты или драйвера (виртуальная машина) - программная отрисовка
        hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, levels, 2,
            D3D11_SDK_VERSION, &sd, &g_swap, &g_device, &got, &g_context);
    if (FAILED(hr)) return false;
    CreateTarget();
    return true;
}

static void DestroyDevice() {
    ReleaseTarget();
    if (g_swap) { g_swap->Release(); g_swap = nullptr; }
    if (g_context) { g_context->Release(); g_context = nullptr; }
    if (g_device) { g_device->Release(); g_device = nullptr; }
}

static void Frame(HWND hwnd) {
    if (!g_ready || !g_rtv) return;
    RECT rc;
    ::GetClientRect(hwnd, &rc);
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    RenderUI((float)(rc.right - rc.left), (float)(rc.bottom - rc.top));
    ImGui::Render();
    const float clear[4] = { COL_BG.x, COL_BG.y, COL_BG.z, 1.0f };
    g_context->OMSetRenderTargets(1, &g_rtv, nullptr);
    g_context->ClearRenderTargetView(g_rtv, clear);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    g_swap->Present(1, 0);
}

static LRESULT WINAPI WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (g_ready && ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam)) return 1;
    switch (msg) {
    case WM_SIZE:
        if (g_device && wParam != SIZE_MINIMIZED) {
            ReleaseTarget();
            g_swap->ResizeBuffers(0, (UINT)LOWORD(lParam), (UINT)HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
            CreateTarget();
        }
        return 0;
    case WM_GETMINMAXINFO: {
        MINMAXINFO* mm = (MINMAXINFO*)lParam;
        mm->ptMinTrackSize.x = 560;
        mm->ptMinTrackSize.y = 640;
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_SYSCOMMAND:
        if ((wParam & 0xFFF0) == SC_KEYMENU) return 0;
        break;
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return ::DefWindowProcW(hwnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE, LPSTR, int show) {
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hIcon = ::LoadIcon(nullptr, IDI_APPLICATION);
    wc.hCursor = ::LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)::GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = L"ElectroGuiCalcLite";
    if (!::RegisterClassExW(&wc)) return 1;

    HWND hwnd = ::CreateWindowExW(0, wc.lpszClassName, L"ElectroGuiCalc Lite by iknlm", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 760, 820, nullptr, nullptr, inst, nullptr);
    if (!hwnd) return 1;

    BOOL dark = TRUE;   // тёмный заголовок окна (Windows 10/11; на старых системах вызов просто не сработает)
    ::DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark));

    if (!CreateDevice(hwnd)) {
        DestroyDevice();
        ::MessageBoxW(hwnd, L"DirectX 11 could not be started.", L"ElectroGuiCalc Lite", MB_ICONERROR);
        ::DestroyWindow(hwnd);
        return 1;
    }

    // язык по умолчанию - как в системе
    g_lang = (PRIMARYLANGID(::GetUserDefaultUILanguage()) == LANG_RUSSIAN) ? 1 : 0;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;   // программа ничего не пишет на диск

    // латиница, греческие буквы (cos φ), кириллица
    static const ImWchar ranges[] = { 0x0020, 0x00FF, 0x0370, 0x03FF, 0x0400, 0x052F, 0 };
    const char* font_path = "C:\\Windows\\Fonts\\segoeui.ttf";
    if (::GetFileAttributesA(font_path) != INVALID_FILE_ATTRIBUTES)
        io.Fonts->AddFontFromFileTTF(font_path, 18.0f, nullptr, ranges);
    else
        io.Fonts->AddFontDefault();

    ApplyStyle();
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_device, g_context);
    g_ready = true;

    ::ShowWindow(hwnd, show);
    ::UpdateWindow(hwnd);

    bool done = false;
    while (!done) {
        MSG msg;
        while (::PeekMessageW(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            ::TranslateMessage(&msg);
            ::DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) done = true;
        }
        if (done) break;
        if (::IsIconic(hwnd)) { ::Sleep(50); continue; }
        Frame(hwnd);
    }

    g_ready = false;
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    DestroyDevice();
    ::DestroyWindow(hwnd);
    ::UnregisterClassW(wc.lpszClassName, inst);
    return 0;
}
#endif
