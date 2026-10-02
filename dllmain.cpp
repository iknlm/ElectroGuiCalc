#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <tchar.h>
#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <cstdint>
#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"
#include "imgui/imgui_impl_win32.h"
#include "imgui/imgui_impl_dx11.h"
#include "include/IconsFontAwesome6.h"

#include <commdlg.h>
#pragma comment(lib, "comdlg32.lib")
#include <shellapi.h>
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")
#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")

// ======================= DIRECTX 11 GLOBALS =======================
static ID3D11Device* g_pd3dDevice = nullptr;
static ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
static IDXGISwapChain* g_pSwapChain = nullptr;
static ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;
static HWND                    g_hwnd = nullptr;
static bool                    g_maximized = false;
static bool                    g_imgui_ready = false;
static bool                    g_pending_drag = false;   // перетаскивание окна начнём ПОСЛЕ кадра
static bool                    g_in_frame = false;       // защита от рисования кадра внутри кадра
static bool                    g_in_move = false;        // окно сейчас тащат (системный цикл перемещения)

static float g_intro_anim_time = -1.0f;

enum class WinAnim { None, Minimizing, Restoring };
static WinAnim g_win_anim = WinAnim::None;
static float   g_win_anim_t = 0.0f;
static constexpr float WIN_ANIM_DUR = 0.20f;

bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CreateRenderTarget();
void CleanupRenderTarget();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// === NEW: скруглённые углы главного окна (как у Discord) ===
// Windows 11: системное сглаженное скругление через DWM.
// Windows 10: DWM так не умеет - вырезаем окно по скруглённому прямоугольнику.
static void ApplyWindowCorners(HWND hwnd) {
    const DWORD kCornerPreference = 33;   // DWMWA_WINDOW_CORNER_PREFERENCE
    const int   kRound = 2;               // DWMWCP_ROUND
    int pref = kRound;
    if (SUCCEEDED(::DwmSetWindowAttribute(hwnd, kCornerPreference, &pref, sizeof(pref)))) {
        ::SetWindowRgn(hwnd, nullptr, TRUE);
        return;
    }
    if (::IsZoomed(hwnd)) {               // развёрнутое окно - без скругления
        ::SetWindowRgn(hwnd, nullptr, TRUE);
        return;
    }
    RECT rc;
    ::GetWindowRect(hwnd, &rc);
    const int radius = 16;
    HRGN rgn = ::CreateRoundRectRgn(0, 0, rc.right - rc.left + 1, rc.bottom - rc.top + 1, radius, radius);
    ::SetWindowRgn(hwnd, rgn, TRUE);      // регион теперь принадлежит окну, удалять не нужно
}

// ======================= GLOBALS & THEME =======================
namespace globals { bool menu_opened = true; }

// ======================= HISTORY =======================
namespace history {
    struct Entry {
        std::string category;
        std::string summary;
        time_t timestamp;
    };
    static std::vector<Entry> g_entries;

    inline void Add(const char* cat, const char* summary) {
        Entry e;
        e.category = cat;
        e.summary = summary;
        e.timestamp = time(nullptr);
        g_entries.push_back(e);
        if (g_entries.size() > 500)
            g_entries.erase(g_entries.begin());
    }
    inline void Clear() { g_entries.clear(); }
}

struct ThemeSettings {
    ImVec4 accent = ImVec4(0.30f, 0.49f, 1.00f, 1.00f);
    ImVec4 icon_color = ImVec4(0.30f, 0.49f, 1.00f, 1.00f);

    ImVec4 window_bg = ImVec4(0.012f, 0.020f, 0.038f, 1.00f);
    ImVec4 card_bg = ImVec4(0.035f, 0.055f, 0.095f, 1.00f);
    ImVec4 card_border = ImVec4(0.10f, 0.15f, 0.25f, 0.60f);

    ImVec4 switch_off = ImVec4(0.15f, 0.15f, 0.18f, 1.00f);
    ImVec4 switch_on = ImVec4(0.30f, 0.49f, 1.00f, 1.00f);

    // === NEW: радиокнопки / чекбоксы ===
    ImVec4 radio_mark = ImVec4(0.30f, 0.49f, 1.00f, 1.00f);   // точка / галочка
    ImVec4 radio_hover = ImVec4(0.18f, 0.29f, 0.60f, 0.60f);   // подсветка при наведении
    ImVec4 frame_bg = ImVec4(0.080f, 0.100f, 0.150f, 1.00f);  // фон кружков и полей ввода

    // === NEW: цвета кнопок с обводкой ===
    ImVec4 btn_danger = ImVec4(1.00f, 0.36f, 0.36f, 1.00f);   // удалить, сбросить, закрыть
    ImVec4 btn_success = ImVec4(0.35f, 0.85f, 0.50f, 1.00f);  // сохранить, экспорт, открыть
    ImVec4 btn_warning = ImVec4(1.00f, 0.72f, 0.30f, 1.00f);  // осторожные действия
    bool   button_glow = true;                                // свечение при наведении
    bool   bg_animated = true;                                // анимированный фон (сетка точек)

    // === NEW: цвета результатов расчётов ===
    ImVec4 res_good = ImVec4(0.40f, 1.00f, 0.40f, 1.00f);    // результаты, "Норма"
    ImVec4 res_bad = ImVec4(1.00f, 0.40f, 0.40f, 1.00f);     // "Не проходит", ошибки
    ImVec4 res_warn = ImVec4(1.00f, 0.80f, 0.40f, 1.00f);    // промежуточные значения, примечания
    ImVec4 res_info = ImVec4(0.60f, 0.85f, 1.00f, 1.00f);    // пояснения (критерий, способ пуска)

    ImVec4 scrollbar_idle = ImVec4(1.00f, 1.00f, 1.00f, 0.05f);
    ImVec4 scrollbar_hovered = ImVec4(0.50f, 0.50f, 0.50f, 0.65f);
    ImVec4 scrollbar_active = ImVec4(0.70f, 0.70f, 0.70f, 0.90f);
    float  scrollbar_width = 8.0f;

    ImVec4 text_main = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
    ImVec4 text_dim = ImVec4(0.51f, 0.52f, 0.56f, 1.00f);

    float card_rounding = 10.0f;
    bool  show_clock = true;

    bool  intro_animation = true;
    float intro_duration = 0.30f;
    float intro_scale_min = 0.92f;

    bool  minimize_animation = true;
    bool  close_animation = true;      // рассыпание в пыль при закрытии
} g_theme;

// ======================= I18N (EN / RU) =======================
namespace i18n {

    enum Lang { LANG_EN = 0, LANG_RU = 1 };
    static Lang g_lang = LANG_EN;

    struct Entry { const char* en; const char* ru; };

    // Ключ = английский текст как в коде. Словарь пополняется по шагам.
    static const Entry g_dict[] = {
        // ─── Сайдбар: группы ───
        { "CALCULATORS", "КАЛЬКУЛЯТОРЫ" },
        { "TOOLS",       "ИНСТРУМЕНТЫ" },
        { "MISC",        "РАЗНОЕ" },

        // ─── Сайдбар: табы (коротко — ширина таба 150 px) ───
        { "Math Calc",   "Математика" },
        { "Cable Size",  "Кабель" },
        { "Grid Load",   "Нагрузка" },
        { "Breaker",     "Автомат" },
        { "Grounding",   "Заземление" },
        { "Motor",       "Двигатель" },
        { "Formulas",    "Формулы" },
        { "Date Calc",   "Даты" },
        { "Converter",   "Конвертер" },
        { "Window Ctrl", "Окна" },
        { "Reference",   "Справочник" },
        { "History",     "История" },
        { "Settings",    "Настройки" },

        // ─── Settings: OPTIONS ───
        { "OPTIONS",                 "ОПЦИИ" },
        { "Auto-Recalculate Values", "Автопересчёт значений" },
        { "Show clock in sidebar",   "Часы в боковой панели" },
        { "Language:",               "Язык:" },
        // ─── Cable: карточки ───
{ "PARAMETERS",                     "ПАРАМЕТРЫ" },
{ "RESULT",                         "РЕЗУЛЬТАТ" },
{ "MAX LENGTH FOR 5% VOLTAGE DROP", "МАКС. ДЛИНА ПРИ ПАДЕНИИ 5%" },

// ─── Cable: параметры ───
{ "Material:",        "Материал:" },
{ "Copper",           "Медь" },
{ "Aluminum",         "Алюминий" },
{ "Phases:",          "Фазы:" },
{ "1 Phase",          "1 фаза" },
{ "3 Phases",         "3 фазы" },
{ "Installation:",    "Прокладка:" },
{ "Air",              "Воздух" },
{ "Pipe",             "Труба" },
{ "Earth",            "Земля" },
{ "Water",            "Вода" },
{ "Insulation:",      "Изоляция:" },
{ "PVC 70C",          "ПВХ 70°C" },
{ "XLPE 90C",         "СПЭ 90°C" },
{ "Rubber 60C",       "Резина 60°C" },
{ "Ambient temp, C:", "Темп. среды, °C:" },
{ "Power, kW:",       "Мощность, кВт:" },
{ "Voltage, V:",      "Напряжение, В:" },
{ "cos φ:",         "Коэф. мощности (cos φ):" },
{ "Length, m:",       "Длина, м:" },
{ "Manual section",   "Ручное сечение" },
{ "Section, mm^2:",   "Сечение, мм²:" },

// ─── Cable: результат ───
{ "Current:",                  "Ток:" },
{ "Section:",                  "Сечение:" },
{ "Required section:",         "Требуемое сечение:" },
{ "Section check:",            "Проверка сечения:" },
{ "OK",                        "Норма" },
{ "OVERLOAD",                  "ПЕРЕГРУЗ" },
{ "FAIL",                      "НЕ ПРОХОДИТ" },
{ "n/a",                       "н/д" },
{ "(auto)",                    "(авто)" },
{ "Temp factor:",              "Темп. коэффициент:" },
{ "Voltage drop:",             "Падение напряжения:" },
{ "Drop percent:",             "Падение, %:" },
{ "Nearest standard section:", "Ближайшее стандартное сечение:" },
{ "Ik:",                       "Ik (ток КЗ):" },
{ "Min Ik for breaker:",       "Мин. Ik для автомата:" },
{ "Ik check:",                 "Проверка Ik:" },
{ "Calculate",                 "Рассчитать" },

// ─── Cable: макс. длина ───
{ "Section",       "Сечение" },
{ "Max length, m", "Макс. длина, м" },
{ "at current",    "при токе" },
// ─── Motor: карточки ───
{ "MOTOR PARAMETERS",   "ПАРАМЕТРЫ ДВИГАТЕЛЯ" },
{ "CONNECTION DIAGRAM", "СХЕМА ПОДКЛЮЧЕНИЯ" },

// ─── Motor: параметры ───
{ "Mode:",                 "Режим:" },
{ "Forward",               "Прямой расчёт" },
{ "Reverse",               "Обратный расчёт" },
{ "Shaft power, kW:",      "Мощность на валу, кВт:" },
{ "Nominal current, A:",   "Номинальный ток, А:" },
{ "Efficiency (0.5-1.0):", "КПД (0.5-1.0):" },
{ "Start method:",         "Способ пуска:" },
{ "DOL",                   "Прямой" },
{ "Star-Delta",            "Звезда-треугольник" },
{ "Soft",                  "Плавный" },
{ "VFD",                   "ЧП" },
{ "Start ratio (Ist/In):", "Кратность пуска (Iп/Iн):" },

// ─── Motor: результат ───
{ "Input power:",           "Потребляемая мощность:" },
{ "Nominal current:",       "Номинальный ток:" },
{ "Shaft power:",           "Мощность на валу:" },
{ "Starting current:",      "Пусковой ток:" },
{ "Recommended breaker:",   "Рекоменд. автомат:" },
{ "Recommended contactor:", "Рекоменд. контактор:" },
{ "Thermal relay range:",   "Уставка тепл. реле:" },
{ "Save to History",        "Сохранить в историю" },

// ─── Motor: названия способов пуска (MotorStartName) ───
{ "DOL (direct on line)",  "Прямой пуск" },
{ "Soft starter",          "Плавный пуск (УПП)" },
{ "VFD (frequency drive)", "Частотный преобразователь" },

// ─── Motor: схема ───
{ "Show:",       "Показать:" },
{ "Star",        "Звезда" },
{ "Delta",       "Треугольник" },
{ "STAR (Y)",    "ЗВЕЗДА (Y)" },
{ "DELTA",       "ТРЕУГОЛЬНИК" },
// ─── Grid Load ───
        { "TOTAL LOAD CALCULATOR", "РАСЧЁТ НАГРУЗКИ" },
        { "Total Power, kW:",      "Суммарная мощность, кВт:" },
        { "Hours per day:",        "Часов в сутки:" },
        { "Total Current Load:",   "Суммарный ток:" },
        { "Daily energy:",         "Энергия в сутки:" },
        { "Recalculate Load",      "Пересчитать нагрузку" },

        // ─── Breaker ───
        { "CIRCUIT BREAKER SELECTOR",   "ПОДБОР АВТОМАТА" },
        { "Current load:",              "Ток нагрузки:" },
        { "Safety margin:",             "Коэф. запаса:" },
        { "Target current:",            "Расчётный ток:" },
        { "Curve type:",                "Характеристика:" },
        { "Safety Margin, x",           "Коэф. запаса, x" },
        { "Select Breaker Rating",      "Подобрать номинал" },
        { "Recommended Breaker:",       "Рекомендуемый автомат:" },
        { "Click button to select breaker rating", "Нажмите кнопку, чтобы подобрать номинал" },
        { "Standard Ratings:",          "Стандартные номиналы:" },
        { "Lighting, resistive loads",  "Освещение, активная нагрузка" },
        { "Sockets, mixed household",   "Розетки, бытовая нагрузка" },
        { "Motors, transformers, high inrush", "Двигатели, трансформаторы, большой пусковой ток" },

        // ─── Grounding ───
        { "EARTHING RESISTANCE",      "СОПРОТИВЛЕНИЕ ЗАЗЕМЛЕНИЯ" },
        { "Soil Resistivity, Ohm*m:", "Удельное сопр. грунта, Ом·м:" },
        { "Rod Length, m:",           "Длина электрода, м:" },
        { "Number of Rods:",          "Количество электродов:" },
        { "Resistance:",              "Сопротивление:" },
        { "Normal (<= 4 Ohm)",        "Норма (<= 4 Ом)" },
        { "Exceeds limit (> 4 Ohm)",  "Превышение (> 4 Ом)" },
        // ─── Общие ───
{ "Result:",    "Результат:" },
{ "Copy",       "Копировать" },
{ "Error:",     "Ошибка:" },

// ─── Math ───
{ "SCIENTIFIC CALCULATOR",         "ИНЖЕНЕРНЫЙ КАЛЬКУЛЯТОР" },
{ "Input A:",                      "Число A:" },
{ "Input B:",                      "Число B:" },
{ "Operation:",                    "Операция:" },
{ "Degrees mode (for sin/cos/tan)", "Градусы (для sin/cos/tan)" },
{ "Constants:",                    "Константы:" },
{ "+  Add",                        "+  Сложение" },
{ "-  Subtract",                   "-  Вычитание" },
{ "*  Multiply",                   "*  Умножение" },
{ "/  Divide",                     "/  Деление" },
{ "^  Power (A^B)",                "^  Степень (A^B)" },
{ "sqrt  Square Root (of A)",      "sqrt  Квадратный корень (из A)" },
{ "%  Modulo (A%B)",               "%  Остаток от деления (A%B)" },
{ "log  Log10(A)",                 "log  Десятичный логарифм (A)" },
{ "ln   Natural log(A)",           "ln   Натуральный логарифм (A)" },
{ "abs  |A|",                      "abs  Модуль |A|" },
{ "!    Factorial(A)",             "!    Факториал (A)" },
{ "Division by zero",              "Деление на ноль" },
{ "sqrt of negative",              "Корень из отрицательного числа" },
{ "Modulo by zero",                "Остаток от деления на ноль" },
{ "log of non-positive",           "Логарифм неположительного числа" },
{ "ln of non-positive",            "Ln неположительного числа" },
{ "Factorial: integer 0..170 only", "Факториал: только целые 0..170" },

// ─── Converter ───
{ "UNIT CONVERTER",        "КОНВЕРТЕР ЕДИНИЦ" },
{ "Conversion:",           "Преобразование:" },
{ "Input value:",          "Исходное значение:" },
{ "Convert",               "Конвертировать" },
{ "Section: mm^2 -> AWG",  "Сечение: мм² -> AWG" },
{ "Section: AWG -> mm^2",  "Сечение: AWG -> мм²" },
{ "Power: Watt -> HP",     "Мощность: Вт -> л.с." },
{ "Power: HP -> Watt",     "Мощность: л.с. -> Вт" },
{ "Temp: C -> F",          "Температура: °C -> °F" },
{ "Temp: F -> C",          "Температура: °F -> °C" },
{ "Length: m -> ft",       "Длина: м -> фут" },
{ "Length: ft -> m",       "Длина: фут -> м" },
{ "Mass: kg -> lb",        "Масса: кг -> фунт" },
{ "Mass: lb -> kg",        "Масса: фунт -> кг" },
{ "Energy: kW -> kcal/h",  "Энергия: кВт -> ккал/ч" },
{ "Energy: kcal/h -> kW",  "Энергия: ккал/ч -> кВт" },
{ "Time: sec -> min",      "Время: с -> мин" },
{ "Time: min -> sec",      "Время: мин -> с" },
{ "Time: hours -> sec",    "Время: ч -> с" },
{ "Time: sec -> hours",    "Время: с -> ч" },

// ─── Formulas ───
{ "PHYSICS & MATH SOLVERS", "ФИЗИКА И МАТЕМАТИКА" },
{ "Electricity / Current",  "Электричество / ток" },
{ "Algebra",                "Алгебра" },
{ "Geometry",               "Геометрия" },
{ "Charge q (C):",          "Заряд q (Кл):" },
{ "Time t (s):",            "Время t (с):" },
{ "Voltage U (V):",         "Напряжение U (В):" },
{ "Resistance R (Ohm):",    "Сопротивление R (Ом):" },
{ "Power P (W):",           "Мощность P (Вт):" },
{ "Current I (A):",         "Ток I (А):" },
{ "Quadratic",              "Квадратное уравнение" },
{ "No real roots",          "Нет действительных корней" },
{ "Pythagorean: c = sqrt(a^2 + b^2)", "Теорема Пифагора: c = sqrt(a^2 + b^2)" },
{ "Leg a:",                 "Катет a:" },
{ "Leg b:",                 "Катет b:" },
{ "Circle: C = 2*pi*R, S = pi*R^2", "Окружность: C = 2*pi*R, S = pi*R^2" },
{ "Radius R:",              "Радиус R:" },

// ─── Date ───
{ "DATE CALCULATOR", "КАЛЬКУЛЯТОР ДАТ" },
{ "Difference",      "Разница" },
{ "Add days",        "Прибавить дни" },
{ "Weekday",         "День недели" },
{ "Date 1",          "Дата 1" },
{ "Date 2",          "Дата 2" },
{ "Y:",              "Г:" },
{ "M:",              "М:" },
{ "D:",              "Д:" },
{ "Selected:",       "Выбрано:" },
{ "Today",           "Сегодня" },
{ "+1d",             "+1д" },
{ "+7d",             "+7д" },
{ "Add days:",       "Прибавить дней:" },
{ "Reset",           "Сброс" },
{ "days",            "дн." },
{ "years",           "г." },
{ "is",              "-" },
{ "Monday",          "понедельник" },
{ "Tuesday",         "вторник" },
{ "Wednesday",       "среда" },
{ "Thursday",        "четверг" },
{ "Friday",          "пятница" },
{ "Saturday",        "суббота" },
{ "Sunday",          "воскресенье" },
// ─── History ───
        { "CALCULATION HISTORY", "ИСТОРИЯ РАСЧЁТОВ" },
        { "Clear History",       "Очистить историю" },
        { "Copy All",            "Копировать всё" },
        { "Export CSV",          "Экспорт в CSV" },
        { "History is empty. Results appear here after calculations.",
          "История пуста. Здесь появятся результаты после расчётов." },
        { "entries:",            "записей:" },
        { "Cable",               "Кабель" },
        { "Load",                "Нагрузка" },
        { "Ground",              "Заземление" },
        { "Math",                "Математика" },
        { "Date",                "Дата" },

        // ─── Window Ctrl: карточки и кнопки ───
        { "WINDOW LIST",             "СПИСОК ОКОН" },
        { "POSITION & SIZE",         "ПОЛОЖЕНИЕ И РАЗМЕР" },
        { "STATE & ACTIONS",         "СОСТОЯНИЕ И ДЕЙСТВИЯ" },
        { "Search by title...",      "Поиск по заголовку..." },
        { "Refresh",                 "Обновить" },
        { "Clear Target",            "Сбросить цель" },
        { "Thumbnails",              "Превью" },
        { "(preview not available)", "(превью недоступно)" },
        { "Target:",                 "Цель:" },
        { "No target selected",      "Цель не выбрана" },
        { "Position:",               "Позиция:" },
        { "Size:",                   "Размер:" },
        { "Apply Move",              "Переместить" },
        { "Apply Resize",            "Изменить размер" },
        { "Apply Both",              "Применить всё" },
        { "Read Current Values",     "Прочитать текущие" },
        { "Center on Screen",        "По центру экрана" },
        { "Minimize",                "Свернуть" },
        { "Maximize",                "Развернуть" },
        { "Restore",                 "Восстановить" },
        { "Hide",                    "Скрыть" },
        { "Show",                    "Показать" },
        { "Focus",                   "Фокус" },
        { "Start Drag",              "Перетащить" },
        { "Always on top",           "Поверх всех окон" },
        { "Close Window",            "Закрыть окно" },
        { "Force Kill",              "Завершить процесс" },

        // ─── Window Ctrl: статусы (из win_control::SetStatus) ───
        { "Window list refreshed",   "Список окон обновлён" },
        { "Target cleared",          "Цель сброшена" },
        { "Target selected",         "Цель выбрана" },
        { "Moved",                   "Перемещено" },
        { "Resized",                 "Размер изменён" },
        { "Moved & resized",         "Перемещено и изменено" },
        { "Read from target",        "Значения прочитаны" },
        { "Failed to read",          "Не удалось прочитать" },
        { "Centered",                "Отцентрировано" },
        { "Minimized",               "Свёрнуто" },
        { "Maximized",               "Развёрнуто" },
        { "Restored",                "Восстановлено" },
        { "Hidden",                  "Скрыто" },
        { "Shown",                   "Показано" },
        { "Focused",                 "Фокус установлен" },
        { "Topmost ON",              "Поверх всех: ВКЛ" },
        { "Topmost OFF",             "Поверх всех: ВЫКЛ" },
        { "Close request sent",      "Запрос на закрытие отправлен" },
        { "Refusing to close self",  "Нельзя закрыть само приложение" },
        { "Refusing to kill self",   "Нельзя завершить само приложение" },
        { "Refusing to drag self",   "Нельзя перетащить само приложение" },
        { "Failed to get PID",       "Не удалось получить PID" },
        { "OpenProcess failed",      "Ошибка OpenProcess" },
        { "Process terminated",      "Процесс завершён" },
        { "TerminateProcess failed", "Ошибка TerminateProcess" },
        { "No target to drag",       "Нет цели для перетаскивания" },
        { "Dragging...",             "Перетаскивание..." },
        // ─── Settings ───
{ "THEME COLORS",       "ЦВЕТА ТЕМЫ" },
{ "SCROLLBAR",          "ПОЛОСА ПРОКРУТКИ" },
{ "ANIMATION",          "АНИМАЦИЯ" },
{ "CONFIG",             "КОНФИГУРАЦИЯ" },
{ "Accent",             "Акцент" },
{ "Icon",               "Иконки" },
{ "Text Main",          "Основной текст" },
{ "Text Dim",           "Второстепенный текст" },
{ "Window BG",          "Фон окна" },
{ "Cards BG",           "Фон карточек" },
{ "Card Border",        "Рамка карточек" },
{ "Switch OFF",         "Переключатель ВЫКЛ" },
{ "Switch ON",          "Переключатель ВКЛ" },
{ "Switch preview (OFF / ON):", "Предпросмотр переключателя (ВЫКЛ / ВКЛ):" },
{ "Card Rounding",      "Скругление карточек" },
{ "Idle",               "Обычная" },
{ "Hovered",            "При наведении" },
{ "Active",             "Активная" },
{ "Width",              "Ширина" },
{ "Intro animation on start",   "Анимация при запуске" },
{ "Duration (s)",               "Длительность (с)" },
{ "Start scale",                "Начальный масштаб" },
{ "Minimize/restore animation", "Анимация сворачивания" },
{ "Crumble to dust on close", "Рассыпание при закрытии" },
{ "CABLE MARK", "МАРКА КАБЕЛЯ" },
{ "MARK CHECK", "ПРОВЕРКА МАРКИ" },
{ "Wire type (2nd letter):", "Тип провода (2-я буква):" },
{ "Insulation (3rd letter):", "Изоляция (3-я буква):" },
{ "Sheath (for rubber insulation):", "Оболочка (для резиновой изоляции):" },
{ "Design (4th letter):", "Конструкция (4-я буква):" },
{ "Number of cores:", "Число жил:" },
{ "Rated voltage, kV:", "Номинальное напряжение, кВ:" },
{ "- (no letter)", "- (без буквы)" },
{ "nairit sheath", "найритовая оболочка" },
{ "PVC sheath", "оболочка из ПВХ" },
{ "Mark:", "Марка:" },
{ "Cable voltage:", "Напряжение кабеля:" },
{ "Cores:", "Жилы:" },
{ "Laying:", "Прокладка:" },
{ "Core metal mass:", "Масса металла жил:" },
{ "armour needed: Б or К", "нужна броня: Б или К" },
{ "round-wire armour needed: К", "нужна броня из проволоки: К" },
{ "need at least", "нужно не меньше" },
{ "PE 70C", "ПЭ 70C" },
{ "kg", "кг" },
{ "Voltage check:", "Проверка напряжения:" },
{ "The first letter follows the Material switch, the section comes from the calculation.", "Первая буква берётся из переключателя «Материал», сечение - из расчёта." },
{ "In earth a cable needs armour (Б or К), in water - round-wire armour (К). The cable voltage must be no less than the network voltage.", "В земле кабелю нужна броня (Б или К), в воде - броня из круглой проволоки (К). Напряжение кабеля должно быть не меньше напряжения сети." },
{ "PVC ng-LS 70C", "ПВХ нг-LS 70°C" },
{ "PVC ng-FRLS 70C", "ПВХ нг-FRLS 70°C" },
{ "Heat-resistant PVC 105C", "ПВХ теплостойкий 105°C" },
{ "Halogen-free (HF) 70C", "Безгалогенная (HF) 70°C" },
{ "Fire-resistant halogen-free (FRHF) 70C", "Безгалогенная огнестойкая (FRHF) 70°C" },
{ "Cross-linked polyolefin (XLPO) 90C", "Сшитый полиолефин (XLPO) 90°C" },
{ "Cross-linked EVA (XL-EVA) 110C", "Сшитый этиленвинилацетат (XL-EVA) 110°C" },
{ "Heat-resistant rubber 85C", "Резина нагревостойкая 85°C" },
{ "Ethylene-propylene rubber (EPR) 90C", "Этиленпропиленовая резина (ЭПР) 90°C" },
{ "EPDM rubber 90C", "Резина EPDM 90°C" },
{ "Silicone rubber 180C", "Кремнийорганическая резина 180°C" },
{ "Ceramic-forming rubber 90C", "Керамообразующая резина 90°C" },
{ "Nairit (neoprene) rubber 65C", "Наиритовая резина 65°C" },
{ "PTFE (F-4) 250C", "Фторопласт-4 (PTFE) 250°C" },
{ "FEP (F-4M) 200C", "Фторопласт-2М/4М (FEP) 200°C" },
{ "ETFE (F-40) 150C", "Фторопласт-40 (ETFE) 150°C" },
{ "Impregnated paper 80C", "Бумажная пропитанная (БПИ) 80°C" },
{ "Paper, non-draining compound 80C", "Бумажная, нестекающий состав 80°C" },
{ "Oil-filled, low pressure 85C", "Маслонаполненная, низкое давление 85°C" },
{ "Oil-filled, high pressure 85C", "Маслонаполненная, высокое давление 85°C" },
{ "Mica tape 400C", "Слюдяная (микалента) 400°C" },
{ "Fiberglass 180C", "Стекловолоконная 180°C" },
{ "Mineral (MgO) 250C", "Минеральная (оксид магния) 250°C" },
{ "steel tapes, PVC hose", "стальные ленты, шланг из ПВХ" },
{ "steel tapes, PE hose", "стальные ленты, шланг из полиэтилена" },
{ "flat steel wires", "плоские стальные проволоки" },
{ "aluminium tapes (single-core)", "алюминиевые ленты (одножильный)" },
{ "round aluminium wires (single-core)", "круглые алюминиевые проволоки (одножильный)" },
{ "corrugated steel tape", "гофрированная стальная лента" },
{ "steel braid (mail armour)", "стальная оплётка (панцирная броня)" },
{ "Armour:", "Броня:" },
{ "Ба and Ка are for single-core cables", "Ба и Ка - только для одножильных" },
{ "single-core: use Ба or Ка", "одножильный: нужна Ба или Ка" },
{ "armour needed for earth", "в земле нужна броня" },
{ "round-wire armour needed: К or Ка", "нужна броня из проволоки: К или Ка" },
{ "Steel armour on a single-core AC cable heats up, aluminium (Ба, Ка) is used instead.", "Стальная броня на одножильном кабеле переменного тока греется, поэтому берут алюминиевую (Ба, Ка)." },
{ "Cables laid together:", "Кабелей проложено вместе:" },
{ "Group factor:", "Коэф. групповой прокладки:" },
{ "Seasonal factor (climate zone):", "Сезонный коэффициент (климатическая зона):" },
{ "Not considered (1.0)", "Не учитывать (1.0)" },
{ "Zone I, cold (1.9)", "Зона I, холодная (1.9)" },
{ "Zone II (1.7)", "Зона II (1.7)" },
{ "Zone III (1.5)", "Зона III (1.5)" },
{ "Zone IV, warm (1.3)", "Зона IV, тёплая (1.3)" },
{ "Design soil resistivity:", "Расчётное сопротивление грунта:" },
{ "Start time, s:", "Время пуска, с:" },
{ "Relay trip class:", "Класс расцепления реле:" },
{ "special protection needed", "нужна особая защита" },
{ "Target cos φ:", "Желаемый cos φ:" },
{ "Capacitor power:", "Мощность конденсаторов:" },
{ "Capacitance per phase (delta):", "Ёмкость на фазу (треугольник):" },
{ "Capacitance:", "Ёмкость:" },
{ "Standard capacitor unit:", "Стандартная установка:" },
{ "not needed", "не требуется" },
{ "Demand factor Kc:", "Коэффициент спроса Кс:" },
{ "Simultaneity factor Ko:", "Коэффициент одновременности Ко:" },
{ "Non-linear load (PCs, UPS, LED)", "Нелинейная нагрузка (ПК, ИБП, LED)" },
{ "3rd harmonic, % of phase current:", "3-я гармоника, % от фазного тока:" },
{ "Design power:", "Расчётная мощность:" },
{ "Neutral current:", "Ток в нулевом проводе:" },
{ "Current for cable sizing:", "Ток для выбора кабеля:" },
{ "Up to 15%: neutral equals phase. 15-33%: cable derated by 0.86. Above 33%: the cable is sized by the neutral current.", "До 15%: ноль равен фазе. 15-33%: ток кабеля снижают на 0.86. Выше 33%: кабель выбирают по току нуля." },
{ "Temperature in panel, C:", "Температура в щите, C:" },
{ "Short-circuit current at panel, kA:", "Ток КЗ в месте установки, кА:" },
{ "Real rating at this temperature:", "Реальный номинал при этой температуре:" },
{ "Breaking capacity needed:", "Нужная отключающая способность:" },
{ "Breakers are calibrated at +30 C, about 0.5% per degree.", "Автоматы калибруют при +30 C, поправка около 0.5% на градус." },
{ "ECONOMICS", "ЭКОНОМИКА" },
{ "Hours of maximum load per year:", "Часов максимума нагрузки в год:" },
{ "Price per kWh:", "Цена за кВт*ч:" },
{ "Economic current density:", "Экономическая плотность тока:" },
{ "Economic section:", "Экономическое сечение:" },
{ "Power loss in the line:", "Потери мощности в линии:" },
{ "Energy loss per year:", "Потери энергии за год:" },
{ "Loss cost per year:", "Стоимость потерь за год:" },
{ "Loss cost with economic section:", "Стоимость потерь при эконом. сечении:" },
{ "Saving per year:", "Экономия за год:" },
{ "rub", "руб" },
{ "Economic density per PUE table 1.3.36. PUE does not require this check for networks up to 1 kV with less than 4000-5000 hours of maximum load.", "Экономическая плотность по ПУЭ табл. 1.3.36. Для сетей до 1 кВ при числе часов максимума меньше 4000-5000 ПУЭ эту проверку не требует." },
{ "Reduced neutral (3+1)", "Уменьшенный ноль (3+1)" },
{ "Earthing type:", "Тип заземлителя:" },
{ "Vertical rods", "Вертикальные электроды" },
{ "Horizontal strip 40x4", "Горизонтальная полоса 40x4" },
{ "Strip length, m:", "Длина полосы, м:" },
{ "Depth, m:", "Глубина прокладки, м:" },
{ "Strip length needed for norm:", "Длина полосы для нормы:" },
{ "Zone I, cold", "Зона I, холодная" },
{ "Zone II", "Зона II" },
{ "Zone III", "Зона III" },
{ "Zone IV, warm", "Зона IV, тёплая" },
{ "LIGHTNING PROTECTION", "МОЛНИЕЗАЩИТА" },
{ "Rod height, m:", "Высота молниеотвода, м:" },
{ "Object height, m:", "Высота объекта, м:" },
{ "Distance to the farthest corner, m:", "Расстояние до дальнего угла объекта, м:" },
{ "Protection reliability:", "Надёжность защиты:" },
{ "Zone cone height h0:", "Высота конуса зоны h0:" },
{ "Zone radius at ground r0:", "Радиус зоны на земле r0:" },
{ "Radius at object height rx:", "Радиус на высоте объекта rx:" },
{ "Object is protected:", "Объект защищён:" },
{ "Yes", "Да" },
{ "No", "Нет" },
{ "Minimum rod height:", "Минимальная высота молниеотвода:" },
{ "Single rod, cone zone per SO 153-34.21.122-2003. The object must fit inside the radius rx at its height.", "Одиночный стержневой молниеотвод, зона-конус по СО 153-34.21.122-2003. Объект должен целиком помещаться в радиус rx на своей высоте." },
{ "USSR / old PUE (busbars)", "СССР / старые ПУЭ (шины)" },
{ "Phase A", "Фаза A" },
{ "Phase B", "Фаза B" },
{ "Phase C", "Фаза C" },
{ "Yellow", "Жёлтый" },
{ "Light blue", "Голубой" },
{ "Metal compatibility", "Совместимость металлов" },
{ "Copper + aluminium", "Медь + алюминий" },
{ "Copper + galvanized steel", "Медь + оцинкованная сталь" },
{ "Copper + tin, brass, bronze", "Медь + олово, латунь, бронза" },
{ "Copper + nickel, chrome", "Медь + никель, хром" },
{ "Aluminium + galvanized steel", "Алюминий + оцинкованная сталь" },
{ "Aluminium + brass, bronze", "Алюминий + латунь, бронза" },
{ "Steel + zinc", "Сталь + цинк" },
{ "no", "нельзя" },
{ "yes", "можно" },
{ "Copper to aluminium - only through a terminal block, tinned lug or steel washer.", "Медь с алюминием - только через клеммник, лужёный наконечник или стальную шайбу." },
{ "Conduit fill", "Заполнение труб и коробов" },
{ "1 cable - up to 53%, 2 cables - 31%, 3 and more - 40% of the cross-section.", "1 кабель - до 53%, 2 кабеля - 31%, 3 и больше - 40% сечения трубы." },
{ "Closed trunking - 35%, with a removable cover - 40% (PUE 2.1.61).", "Глухие короба - 35%, с открываемой крышкой - 40% (ПУЭ 2.1.61)." },
{ "Cable diameter, mm:", "Диаметр кабеля, мм:" },
{ "Number of cables:", "Число кабелей:" },
{ "Min. inner diameter:", "Мин. внутренний диаметр:" },
{ "Altitude correction", "Поправка на высоту над уровнем моря" },
{ "Altitude", "Высота" },
{ "Current", "Ток" },
{ "Voltage", "Напряжение" },
{ "Typical values for moulded-case breakers. Check the manufacturer's data.", "Типовые значения для автоматов в литом корпусе. Сверяйте с данными производителя." },
{ "Altitude above sea level, m:", "Высота над уровнем моря, м:" },
{ "Altitude factor:", "Коэф. высоты:" },
{ "up to 1000 m", "до 1000 м" },
{ "Allowable current", "Допустимый ток" },
{ "dry rooms only", "только в сухих помещениях" },
{ "Copper + stainless steel", "Медь + нержавеющая сталь" },
{ "Copper + carbon steel, lead", "Медь + чёрная сталь, свинец" },
{ "Aluminium + zinc, cadmium-plated steel", "Алюминий + цинк, кадмированная сталь" },
{ "Aluminium + stainless steel", "Алюминий + нержавеющая сталь" },
{ "Galvanized steel + carbon steel", "Оцинкованная сталь + чёрная сталь" },
{ "Galvanized steel + stainless steel", "Оцинкованная + нержавеющая сталь" },
{ "Per GOST 9.005-72. Copper to aluminium - only through Al-Cu lugs, transition plates or tinned terminals.", "По ГОСТ 9.005-72. Медь с алюминием - только через алюмомедные наконечники, переходные пластины или лужёные клеммы." },
{ "1 cable - up to 40%, 2 cables - 25%, 3 and more - 35% of the cross-section (by outer cable diameter).", "1 кабель - до 40%, 2 кабеля - 25%, 3 и больше - 35% сечения трубы (по наружному диаметру кабелей)." },
{ "Above 1000 m the air is thinner and cools worse, so the allowable current is reduced (GOST 15150-69).", "Выше 1000 м воздух разрежен и хуже охлаждает, поэтому допустимый ток снижают (ГОСТ 15150-69)." },
{ "Conduit inner diameter, mm:", "Внутренний диаметр трубы, мм:" },
{ "Fill:", "Заполнение:" },
{ "Cables will jam when pulled. Take a larger conduit.", "Кабели заклинит при протяжке. Возьмите трубу большего диаметра." },
{ "limit", "лимит" },
{ "Cable and wire marking", "Маркировка кабеля и провода" },
{ "Letters: metal, type, insulation, design. Digits: cores x section - voltage.", "Буквы: металл, тип, изоляция, конструкция. Цифры: число жил x сечение - напряжение." },
{ "1st letter - core metal", "1-я буква - металл жилы" },
{ "2nd letter - wire type", "2-я буква - тип провода" },
{ "3rd letter - insulation", "3-я буква - изоляция" },
{ "4th letter - design features", "4-я буква - конструкция" },
{ "Digits after the letters", "Цифры после букв" },
{ "Rubber-insulated wires also have a sheath: Н - nairit, П - PVC. These letters go after the insulation letter.", "Провода с резиновой изоляцией дополнительно защищены оболочкой: Н - найритовой, П - ПВХ. Эти буквы стоят после буквы изоляции жилы." },
{ "aluminium core", "алюминий" },
{ "no letter: copper core", "без буквы - медь" },
{ "control wire", "контрольный" },
{ "mounting wire", "монтажный" },
{ "mounting, flexible cores", "монтажный с гибкими жилами" },
{ "flat wire", "плоский" },
{ "installation wire", "установочный" },
{ "PVC insulation", "поливинилхлоридная" },
{ "with a flexible core", "с гибкой жилой" },
{ "kapron (nylon)", "капроновая" },
{ "lacquered", "лакированная" },
{ "enamelled", "эмалированная" },
{ "nairit, non-flammable rubber", "найритовая, из негорючей резины" },
{ "polyamide silk", "полиамидный шёлк" },
{ "polyethylene", "полиэтиленовая" },
{ "fiberglass", "из стекловолокна" },
{ "with a carrier cable", "с несущим тросом" },
{ "seamed (folded) sheath", "фальцованная" },
{ "screened", "экранированная" },
{ "asphalt-coated", "асфальтированная" },
{ "armoured with steel tapes", "бронированная стальными лентами" },
{ "bare, no protective cover", "без защитного покрова (голый) или гибкий провод" },
{ "armoured with round wire", "бронированная круглой проволокой" },
{ "in a protective braid", "в защитной оплётке" },
{ "for laying inside pipes", "для прокладки в трубах" },
{ "number of cores; if absent - single core", "число жил; если не указано - одна жила" },
{ "core cross-section, mm2", "сечение жилы, мм2" },
{ "rated voltage of the conductor", "номинальное напряжение проводника" },
        { "Animated background",        "Анимированный фон" },
{ "Settings file: settings.ini (next to .exe)", "Файл настроек: settings.ini (рядом с .exe)" },
{ "Save Settings",           "Сохранить настройки" },
{ "Load Settings",           "Загрузить настройки" },
{ "Reset Theme to Defaults", "Сбросить тему" },
// ─── Reference: колонки и секции ───
{ "WIRE COLORS",            "ЦВЕТА ПРОВОДОВ" },
{ "IP & CATEGORIES",        "IP И КАТЕГОРИИ" },
{ "MOTOR & AMPACITY",       "ДВИГАТЕЛЬ И ТОКИ" },
{ "Wire color codes",       "Цветовая маркировка" },
{ "IP rating",              "Степень защиты IP" },
{ "Overvoltage cat.",       "Категории перенапряжения" },
{ "ANSI / IEC symbols",     "Обозначения ANSI / IEC" },
{ "Motor: Star vs Delta",   "Двигатель: звезда и треугольник" },
{ "Ampacity copper (PVC)",  "Допустимый ток, медь (ПВХ)" },
{ "AWG <-> mm^2",           "AWG <-> мм²" },

// ─── Reference: цвета проводов ───
{ "IEC 60446 (Europe)",     "IEC 60446 (Европа)" },
{ "Old UK",                 "Старый стандарт UK" },
{ "DC / US (NEC)",          "DC / США (NEC)" },
{ "Brown / Black / Grey",   "Коричн. / Чёрный / Серый" },
{ "Neutral (N)",            "Нейтраль (N)" },
{ "Blue",                   "Синий" },
{ "Protective",             "Защитный (PE)" },
{ "Yellow-Green",           "Жёлто-зелёный" },
{ "Phase (L)",              "Фаза (L)" },
{ "Red",                    "Красный" },
{ "Black",                  "Чёрный" },
{ "Green",                  "Зелёный" },
{ "Positive (+)",           "Плюс (+)" },
{ "Negative (-)",           "Минус (-)" },
{ "Green / bare",           "Зелёный / голый" },

// ─── Reference: IP ───
{ "1st digit - solids",             "1-я цифра - твёрдые тела" },
{ "0 none  1 >50mm  2 >12.5mm",     "0 нет  1 >50мм  2 >12.5мм" },
{ "3 >2.5mm  4 >1mm",               "3 >2.5мм  4 >1мм" },
{ "5 dust protected  6 dust tight", "5 пылезащищ.  6 пыленепрониц." },
{ "2nd digit - water",              "2-я цифра - вода" },
{ "0 none  1 drip  2 drip 15deg",   "0 нет  1 капли  2 капли 15°" },
{ "3 spray  4 splash  5 jets",      "3 дождь  4 брызги  5 струи" },
{ "6 power jets  7 immersion 1m",   "6 мощн. струи  7 погруж. 1м" },
{ "8 continuous immersion",         "8 длительное погружение" },
{ "Common",                         "Типовые" },
{ "IP20 indoor   IP44 bath",        "IP20 помещения   IP44 ванная" },
{ "IP54 industrial   IP65 panel",   "IP54 производство   IP65 щит" },
{ "IP67 submerged   IP68 underwater", "IP67 погружение   IP68 под водой" },

// ─── Reference: категории перенапряжения ───
{ "Electronics, 1500 V",     "Электроника, 1500 В" },
{ "Appliances, 2500 V",      "Бытовые приборы, 2500 В" },
{ "Fixed install, 4000 V",   "Стационарные установки, 4000 В" },
{ "Origin, 6000 V",          "Ввод, 6000 В" },

// ─── Reference: ANSI / IEC ───
{ "Description",             "Описание" },
{ "Circuit breaker",         "Автомат. выключатель" },
{ "Fuse",                    "Предохранитель" },
{ "Contactor",               "Контактор" },
{ "Relay aux",               "Промежуточное реле" },
{ "Thermal overload",        "Тепловое реле" },
{ "MCCB / motor breaker",    "Автомат защиты двигателя" },
{ "Protective earth",        "Защитное заземление" },
{ "ANSI - US (NEC), IEC - Europe.", "ANSI - США (NEC), IEC - Европа." },

// ─── Reference: двигатель и токи ───
{ "Start I/T ~ 3x less",     "Пусковой ток/момент ~ в 3 раза меньше" },
{ "U2=V2=W2 tied",           "U2=V2=W2 соединены" },
{ "Full torque/current",     "Полный момент/ток" },
{ "Star-Delta needs 6 terminals.", "Для звезды-треугольника нужны 6 выводов." },
{ "mm2",                     "мм²" },
{ "1ph",                     "1ф" },
{ "3ph",                     "3ф" },
{ "Approx. Verify vs local code.", "Ориентировочно. Сверяйте с ПУЭ." },
{ "AWG is a logarithmic scale.",   "AWG - логарифмическая шкала." },
// ─── Help: таб, карточка, секции ───
        { "Help",                  "Справка" },
        { "HELP & RESOURCES",      "СПРАВКА И МАТЕРИАЛЫ" },
        { "How to use",            "Как пользоваться" },
        { "Lectures & references", "Лекции и материалы" },
        { "Click a link to open in browser:",          "Нажми на ссылку, откроется в браузере:" },
        { "Links lead to Russian-language materials.", "Ссылки ведут на русскоязычные материалы." },

        // ─── Help: общий порядок ───
        { "General workflow", "Общий порядок работы" },
        { "1. Choose a calculator in the left sidebar.", "1. Выберите калькулятор в панели слева." },
        { "2. Enter parameters in the left card.",       "2. Введите параметры в левой карточке." },
        { "3. The result appears on the right (with Auto-Recalculate on).",
          "3. Результат появится справа (если включён автопересчёт)." },
        { "4. Press 'Save to History' or 'Calculate' to store the result.",
          "4. Нажмите «Сохранить в историю» или «Рассчитать», чтобы сохранить результат." },
        { "5. Export saved results to CSV in the History tab.",
          "5. Во вкладке «История» результаты можно выгрузить в CSV." },
        { "6. Switch language with the EN | RU switch in the sidebar.",
          "6. Язык переключается кнопкой EN | RU в боковой панели." },

          // ─── Help: описание табов ───
        { "Scientific calculator: + - * /, power, root, sin/cos/tan, log, ln, factorial.",
          "Инженерный калькулятор: + - * /, степень, корень, sin/cos/tan, log, ln, факториал." },
        { "Cable section by power, voltage, cos φ and length; material, installation, insulation, ambient temp.",
          "Сечение кабеля по мощности, напряжению, cos φ и длине; материал, прокладка, изоляция, темп. среды." },
        { "Manual section: fix a specific section and check it against the required one.",
          "Ручное сечение: задайте конкретное сечение и сравните его с требуемым." },
        { "Ik check (short-circuit current) and a max-length table for 5% voltage drop.",
          "Проверка Ik (ток КЗ) и таблица максимальной длины при падении 5%." },
        { "Total load: current and daily energy consumption, kWh.",
          "Суммарная нагрузка: ток и потребление энергии за сутки, кВт·ч." },
        { "Breaker rating by load current with a safety margin.",
          "Подбор номинала автомата по току нагрузки с коэффициентом запаса." },
        { "Curves B / C / D with recommendations for the load type.",
          "Характеристики B / C / D с рекомендациями по типу нагрузки." },
        { "Earthing resistance by soil resistivity, rod length and number of rods.",
          "Сопротивление заземления по удельному сопротивлению грунта, длине и числу электродов." },
        { "Motor current: Forward (power -> current) and Reverse (current -> power).",
          "Ток двигателя: прямой расчёт (мощность -> ток) и обратный (ток -> мощность)." },
        { "Start methods, breaker / contactor / thermal relay selection, Star / Delta diagram.",
          "Способы пуска, подбор автомата / контактора / теплового реле, схема звезда / треугольник." },
        { "Physics and math: Ohm's law, power, Joule's law, quadratic equation, geometry.",
          "Физика и математика: закон Ома, мощность, закон Джоуля-Ленца, квадратное уравнение, геометрия." },
        { "Difference between dates, adding days, day of the week.",
          "Разница между датами, прибавление дней, день недели." },
        { "Unit conversion: AWG <-> mm2, HP <-> W, C <-> F, m <-> ft, kg <-> lb, time.",
          "Перевод единиц: AWG <-> мм², л.с. <-> Вт, °C <-> °F, м <-> фут, кг <-> фунт, время." },
        { "Control other windows: move, resize, minimize, hide, close, always on top.",
          "Управление другими окнами: перемещение, размер, сворачивание, скрытие, закрытие, поверх всех." },
        { "Select a window in the list - a preview is shown under it.",
          "Выберите окно в списке - под ним появится превью." },
        { "Wire colors, IP codes, overvoltage categories, ANSI/IEC symbols, AWG, ampacity.",
          "Цвета проводов, коды IP, категории перенапряжения, обозначения ANSI/IEC, AWG, допустимые токи." },
        { "Log of all saved calculations; copy to clipboard or export to CSV.",
          "Журнал сохранённых расчётов; копирование в буфер и экспорт в CSV." },
        { "Theme colors, scrollbar, animations, config file, auto-recalculate, language.",
          "Цвета темы, полоса прокрутки, анимации, файл настроек, автопересчёт, язык." },

          // ─── Help: ссылки ───
        { "Ohm's law - Wikipedia",            "Закон Ома - Википедия" },
        { "Electric power - Wikipedia",       "Электрическая мощность - Википедия" },
        { "Alternating current - Wikipedia",  "Переменный ток - Википедия" },
        { "Circuit breaker - Wikipedia",      "Автоматический выключатель - Википедия" },
        { "Electric motor - Wikipedia",       "Электродвигатель - Википедия" },
        { "Grounding - Wikipedia",            "Заземление - Википедия" },
        { "PUE, ch. 1.1: General part (docs.cntd.ru)",
          "ПУЭ, гл. 1.1: Общая часть (docs.cntd.ru)" },
        { "PUE, ch. 1.7: Earthing and protective measures (docs.cntd.ru)",
          "ПУЭ, гл. 1.7: Заземление и защитные меры (docs.cntd.ru)" },
        { "GOST R 50571.1-2009 (IEC 60364-1) (docs.cntd.ru)",
          "ГОСТ Р 50571.1-2009 (МЭК 60364-1) (docs.cntd.ru)" },
          // ─── Help: файлы лекций ───
      { "Lecture files (offline)", "Лекции (без интернета)" },
      { "Files from the 'lectures' folder next to the program:",
        "Файлы из папки «lectures» рядом с программой:" },
      { "Open folder",               "Открыть папку" },
      { "No files found. Put .pdf / .docx into the 'lectures' folder.",
        "Файлы не найдены. Положите .pdf / .docx в папку «lectures»." },
      { "No app for this file type", "Нет программы для этого типа файлов" },
      { "Failed to open file",       "Не удалось открыть файл" },
      // ─── Settings: HSB и радиокнопки ───
      { "Radio / checkbox dot",      "Точка кружка / галочка" },
      { "Radio / checkbox hover",    "Кружок при наведении" },
      { "Fields & radio background", "Фон полей и кружков" },
      { "Preview:",                  "Предпросмотр:" },
      { "Hue",                       "Оттенок" },
      { "Saturation",                "Насыщенность" },
      { "Brightness",                "Яркость" },
      { "Opacity",                   "Прозрачность" },

      // ─── Settings: кнопки ───
      { "BUTTONS",                   "КНОПКИ" },
      { "Danger (delete, reset)",    "Опасные (удалить, сбросить)" },
      { "Success (save, export)",    "Успех (сохранить, экспорт)" },
      { "Warning (careful actions)", "Осторожно (скрыть, сбросить цель)" },
        { "Option 1",                  "Вариант 1" },
        { "Option 2",                  "Вариант 2" },
        { "Checkbox",                  "Флажок" },
        // ─── Расчёты по ПУЭ ───
        { "Supply transformer (Y/Yn):", "Трансформатор ТП (Y/Yн):" },
        { "Not considered",            "Не учитывать" },
        { "kVA",                       "кВА" },
        { "Section (PUE):",            "Сечение по ПУЭ:" },
        { "Chosen:",                   "Выбрано:" },
        { "by heating",                "по нагреву" },
        { "by breaker protection",     "по защите автоматом" },
        { "by voltage drop",           "по падению напряжения" },
        { "by short-circuit current",  "по току КЗ" },
        { "no suitable section in PUE tables", "нет подходящего сечения в таблицах ПУЭ" },
        { "Allowable current:",        "Допустимый ток:" },
        { "Manual section:",           "Ручное сечение:" },
        { "Transformer Zt/3:",         "Трансформатор Zт/3:" },
        { "Line loop Z:",              "Петля линии Z:" },
        { "Per PUE tables 1.3.4-1.3.7, temperature per table 1.3.3. Contacts 0.03 Ohm included.",
          "По таблицам ПУЭ 1.3.4-1.3.7, температура по табл. 1.3.3. Учтены контакты 0,03 Ом." },
        { "Distance between rods, m:", "Расстояние между электродами, м:" },
        { "Depth of rod top, m:",      "Глубина верха электрода, м:" },
        { "Electrode:",                "Электрод:" },
        { "Round bar d16",             "Круг d16" },
        { "Angle 50x50",               "Уголок 50x50" },
        { "Required resistance:",      "Требуемое сопротивление:" },
        { "4 Ohm (TP neutral)",        "4 Ом (нейтраль ТП)" },
        { "10 Ohm",                    "10 Ом" },
        { "30 Ohm (repeated)",         "30 Ом (повторное)" },
        { "One rod:",                  "Один электрод:" },
        { "Utilization factor:",       "Коэф. использования:" },
        { "Norm check:",               "Проверка нормы:" },
        { "Rods needed for norm:",     "Нужно электродов для нормы:" },
        { "Vertical rods in a row, without the connecting strip (with margin). Utilization factors are approximate table values.",
          "Вертикальные электроды в ряд, без учёта соединительной полосы (с запасом). Коэффициенты использования - ориентировочные табличные." },
        { "Soft starter current limit, x In:", "Ограничение тока УПП, x Iн:" },
        // ─── Формулы: новые секции ───
        { "Power and current (Cable, Load)", "Мощность и ток (Кабель, Нагрузка)" },
        { "Power P, kW:",               "Мощность P, кВт:" },
        { "Cable: temperature and voltage drop", "Кабель: температура и падение напряжения" },
        { "t_max of core, C:",          "t_max жилы, °C:" },
        { "t ambient, C:",              "t среды, °C:" },
        { "t of table, C:",             "t таблицы, °C:" },
        { "t_max: PVC 65, XLPE 90, rubber 60. t of table: 25 in air, 15 in ground. Allowable current = table current * k_t.",
          "t_max: ПВХ 65, СПЭ 90, резина 60. t таблицы: 25 в воздухе, 15 в земле. Допустимый ток = ток по таблице ПУЭ * k_t." },
        { "Voltage U is taken from the 'Power and current' section above.",
          "Напряжение U берётся из секции «Мощность и ток» выше." },
        { "Short circuit: phase-zero loop", "Ток КЗ: петля фаза-ноль" },
        { "Phase voltage U_ph, V:",     "Фазное напряжение Uф, В:" },
        { "Breaker In, A:",             "Номинал автомата In, А:" },
        { "Transformer Zt/3, Ohm:",     "Трансформатор Zт/3, Ом:" },
        { "Contacts, Ohm:",             "Контакты, Ом:" },
        { "Zt/3 for Y/Yn transformers: 100 kVA 0.26; 160 kVA 0.162; 250 kVA 0.104; 400 kVA 0.065; 630 kVA 0.043; 1000 kVA 0.027 Ohm.",
          "Zт/3 трансформаторов Y/Yн: 100 кВА 0,26; 160 кВА 0,162; 250 кВА 0,104; 400 кВА 0,065; 630 кВА 0,043; 1000 кВА 0,027 Ом." },
        { "Grounding: vertical rods",   "Заземление: вертикальные электроды" },
        { "Rod diameter d, mm:",        "Диаметр электрода d, мм:" },
        { "Angle 50x50: d = 0.95 * 50 = 47.5 mm. Utilization factor: 0.5-0.95, see the Grounding tab or lecture 8.",
          "Уголок 50x50: d = 0,95 * 50 = 47,5 мм. Коэффициент использования 0,5-0,95 - см. вкладку «Заземление» или лекцию 8." },
        { "Energy and units",           "Энергия и единицы" },
        { "Core diameter d, mm:",       "Диаметр жилы d, мм:" },
        { "AWG number:",                "Номер AWG:" },
        { "Temperature, C:",            "Температура, °C:" },
        // ─── Формулы: подразделы ───
        { "Ohm's law and charge",       "Закон Ома и заряд" },
        { "Power",                      "Мощность" },
        { "Joule-Lenz law",             "Закон Джоуля-Ленца" },
        { "Wire resistance",            "Сопротивление провода" },
        { "Core temperature, C:",       "Температура жилы, °C:" },
        { "Series and parallel connection", "Последовательное и параллельное соединение" },
        { "Series R =",                 "Последовательно R =" },
        { "Parallel R =",               "Параллельно R =" },
        { "Alternating current: reactance and impedance", "Переменный ток: реактивное и полное сопротивление" },
        { "Frequency f, Hz:",           "Частота f, Гц:" },
        { "Inductance L, mH:",          "Индуктивность L, мГн:" },
        { "Capacitance C, uF:",         "Ёмкость C, мкФ:" },
        { "Capacitor and RC circuit",   "Конденсатор и RC-цепь" },
        { "Resistance R, kOhm:",        "Сопротивление R, кОм:" },
        { "Current density",            "Плотность тока" },
        { "Linear equation",            "Линейное уравнение" },
        { "a must not be 0",            "a не должно быть 0" },
        { "System of two equations (Cramer's rule)", "Система двух уравнений (метод Крамера)" },
        { "D = 0: no single solution",  "D = 0: нет единственного решения" },
        { "Percentages",                "Проценты" },
        { "x% of N =",                  "x% от N =" },
        { "Change =",                   "Изменение =" },
        { "Proportion",                 "Пропорция" },
        { "Powers, roots, logarithms",  "Степени, корни, логарифмы" },
        { "base b:",                    "основание b:" },
        { "root =",                     "корень =" },
        { "Progressions",               "Прогрессии" },
        { "Right triangle (Pythagoras)", "Прямоугольный треугольник (Пифагор)" },
        { "angle A =",                  "угол A =" },
        { "angle B =",                  "угол B =" },
        { "Any triangle",               "Произвольный треугольник" },
        { "base:",                      "основание:" },
        { "height h:",                  "высота h:" },
        { "Perimeter =",                "Периметр =" },
        { "Such a triangle does not exist", "Такого треугольника не существует" },
        { "Law of cosines and law of sines", "Теоремы косинусов и синусов" },
        { "angle C, deg:",              "угол C, град:" },
        { "angle A, deg:",              "угол A, град:" },
        { "angle B, deg:",              "угол B, град:" },
        { "Circle and sector",          "Окружность и сектор" },
        { "angle alpha, deg:",          "угол alpha, град:" },
        { "arc =",                      "дуга =" },
        { "S sector =",                 "S сектора =" },
        { "Rectangle and trapezoid",    "Прямоугольник и трапеция" },
        { "width a:",                   "ширина a:" },
        { "height b:",                  "высота b:" },
        { "diagonal =",                 "диагональ =" },
        { "base a:",                    "основание a:" },
        { "base b:",                    "основание b:" },
        { "S trapezoid =",              "S трапеции =" },
        { "Solids: cylinder, cone, sphere", "Тела: цилиндр, конус, шар" },
        { "radius r:",                  "радиус r:" },
        { "V cylinder =",               "V цилиндра =" },
        { "S cylinder =",               "S цилиндра =" },
        { "V cone =",                   "V конуса =" },
        { "V sphere =",                 "V шара =" },
        { "S sphere =",                 "S шара =" },
        { "Angles",                     "Углы" },
        { "angle, deg:",                "угол, град:" },
        // ─── Settings: цвета результатов ───
        { "RESULT COLORS",              "ЦВЕТА РЕЗУЛЬТАТОВ" },
        { "Results / OK",               "Результаты / норма" },
        { "Errors / FAIL",              "Ошибки / не проходит" },
        { "Intermediate values, notes", "Промежуточные значения, примечания" },
        { "Info values",                "Пояснения" },
      { "Glow on hover",             "Свечение при наведении" },
      { "Danger",                    "Опасно" },
      { "Success",                   "Успех" },
      { "Warning",                   "Внимание" },

      // ─── Settings: готовые темы ───
      { "THEME PRESETS",             "ГОТОВЫЕ ТЕМЫ" },
      { "Pick a theme, then fine-tune colors below if you like.",
        "Выберите тему, а ниже при желании подправьте цвета." },
      { "Ocean",                     "Океан" },
      { "Emerald",                   "Изумруд" },
      { "Amethyst",                  "Аметист" },
      { "Crimson",                   "Рубин" },
      { "Amber",                     "Янтарь" },
      { "Arctic",                    "Арктика" },
      { "Sakura",                    "Сакура" },
      { "Graphite",                  "Графит" },
    };

    // Перевод строки. Нет перевода -> возвращается ключ (английский).
    inline const char* T(const char* key) {
        if (g_lang == LANG_EN || key == nullptr) return key;

        // Кэш: хеш содержимого строки -> индекс в словаре (-1 = нет перевода)
        static std::map<ImGuiID, int> s_cache;
        const ImGuiID h = ImHashStr(key);
        int idx = -1;
        auto it = s_cache.find(h);
        if (it != s_cache.end()) {
            idx = it->second;
        }
        else {
            for (int i = 0; i < IM_ARRAYSIZE(g_dict); ++i) {
                if (strcmp(g_dict[i].en, key) == 0) { idx = i; break; }
            }
            s_cache[h] = idx;
        }
        if (idx >= 0 && strcmp(g_dict[idx].en, key) == 0) return g_dict[idx].ru;
        return key;
    }

    // Для стандартных виджетов ImGui: "Перевод###id".
    // Видно перевод, ID стабильный и не зависит от языка.
    // id == nullptr -> в качестве ID берётся сам ключ.
    inline const char* L(const char* key, const char* id = nullptr) {
        static char s_ring[32][256];
        static int  s_pos = 0;
        char* buf = s_ring[s_pos];
        s_pos = (s_pos + 1) % 32;
        snprintf(buf, sizeof(s_ring[0]), "%s###%s", T(key), id ? id : key);
        return buf;
    }

}   // namespace i18n



// ======================= WINDOW VISUALS =======================
namespace win_visuals {

    struct CachedTex {
        ID3D11ShaderResourceView* srv = nullptr;
        int w = 0, h = 0;
    };
    static std::map<HWND, CachedTex> g_icon_cache;
    static std::map<HWND, CachedTex> g_thumb_cache;
    static std::map<HWND, float>      g_thumb_timer;

    inline void ClearAll() {
        for (auto& kv : g_icon_cache)  if (kv.second.srv) kv.second.srv->Release();
        for (auto& kv : g_thumb_cache) if (kv.second.srv) kv.second.srv->Release();
        g_icon_cache.clear();
        g_thumb_cache.clear();
        g_thumb_timer.clear();
    }

    inline ID3D11ShaderResourceView* MakeSRV(unsigned char* rgba, int w, int h) {
        if (!g_pd3dDevice || !rgba || w <= 0 || h <= 0) return nullptr;

        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = w; desc.Height = h;
        desc.MipLevels = 1; desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        D3D11_SUBRESOURCE_DATA sub = {};
        sub.pSysMem = rgba;
        sub.SysMemPitch = w * 4;

        ID3D11Texture2D* tex = nullptr;
        if (FAILED(g_pd3dDevice->CreateTexture2D(&desc, &sub, &tex))) return nullptr;

        ID3D11ShaderResourceView* srv = nullptr;
        D3D11_SHADER_RESOURCE_VIEW_DESC srvd = {};
        srvd.Format = desc.Format;
        srvd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvd.Texture2D.MipLevels = 1;
        g_pd3dDevice->CreateShaderResourceView(tex, &srvd, &srv);
        tex->Release();
        return srv;
    }

    inline bool ExtractIcon(HWND hwnd, ID3D11ShaderResourceView** out_srv, int* out_w, int* out_h) {
        *out_srv = nullptr; *out_w = *out_h = 0;

        HICON hicon = nullptr;

        hicon = (HICON)::SendMessageW(hwnd, WM_GETICON, ICON_BIG, 0);
        if (!hicon) hicon = (HICON)::SendMessageW(hwnd, WM_GETICON, ICON_SMALL, 0);
        if (!hicon) hicon = (HICON)::GetClassLongPtrW(hwnd, GCLP_HICON);
        if (!hicon) hicon = (HICON)::GetClassLongPtrW(hwnd, GCLP_HICONSM);

        if (!hicon) {
            DWORD pid = 0;
            ::GetWindowThreadProcessId(hwnd, &pid);
            if (pid) {
                HANDLE hProc = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
                if (hProc) {
                    wchar_t exe_path[MAX_PATH] = {};
                    DWORD size = MAX_PATH;
                    if (::QueryFullProcessImageNameW(hProc, 0, exe_path, &size)) {
                        HICON big_icon = nullptr;
                        UINT extracted = ::ExtractIconExW(exe_path, 0, &big_icon, nullptr, 1);
                        if (extracted > 0 && big_icon) {
                            hicon = big_icon;
                        }
                    }
                    ::CloseHandle(hProc);
                }
            }
        }

        if (!hicon) return false;

        const int W = 32, H = 32;
        std::vector<unsigned char> pixels(W * H * 4, 0);

        BITMAPINFO bmi = {};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = W;
        bmi.bmiHeader.biHeight = -H;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        HDC hdc = ::GetDC(nullptr);
        HDC memDC = ::CreateCompatibleDC(hdc);
        void* bits = nullptr;
        HBITMAP bmp = ::CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
        HGDIOBJ old = ::SelectObject(memDC, bmp);

        memset(bits, 0, W * H * 4);
        ::DrawIconEx(memDC, 0, 0, hicon, W, H, 0, nullptr, DI_NORMAL);

        if (bits) memcpy(pixels.data(), bits, W * H * 4);

        ::SelectObject(memDC, old);
        ::DeleteObject(bmp);
        ::DeleteDC(memDC);
        ::ReleaseDC(nullptr, hdc);

        bool any = false;
        for (int i = 0; i < W * H; ++i)
            if (pixels[i * 4] || pixels[i * 4 + 1] || pixels[i * 4 + 2]) { any = true; break; }
        if (!any) return false;

        *out_srv = MakeSRV(pixels.data(), W, H);
        *out_w = W; *out_h = H;
        return *out_srv != nullptr;
    }

    inline bool CaptureThumb(HWND hwnd, ID3D11ShaderResourceView** out_srv, int* out_w, int* out_h) {
        *out_srv = nullptr; *out_w = *out_h = 0;
        if (!::IsWindow(hwnd)) return false;

        RECT rc;
        if (!::GetClientRect(hwnd, &rc)) return false;
        int w = rc.right - rc.left;
        int h = rc.bottom - rc.top;
        if (w <= 0 || h <= 0) return false;

        const int MAX_DIM = 320;
        if (w > MAX_DIM || h > MAX_DIM) {
            float scale = (float)MAX_DIM / (float)(w > h ? w : h);   // уже с приведением — ок
            w = (int)(w * scale);
            h = (int)(h * scale);
        }

        HDC hdc_screen = ::GetDC(nullptr);
        HDC hdc_mem = ::CreateCompatibleDC(hdc_screen);
        HBITMAP hbm = ::CreateCompatibleBitmap(hdc_screen, w, h);
        HGDIOBJ old = ::SelectObject(hdc_mem, hbm);

        RECT full = { 0, 0, w, h };
        ::FillRect(hdc_mem, &full, (HBRUSH)::GetStockObject(BLACK_BRUSH));

        BOOL ok = ::PrintWindow(hwnd, hdc_mem, PW_RENDERFULLCONTENT);
        if (!ok) ok = ::PrintWindow(hwnd, hdc_mem, 0);
        if (!ok) {
            ::SelectObject(hdc_mem, old);
            ::DeleteObject(hbm);
            ::DeleteDC(hdc_mem);
            ::ReleaseDC(nullptr, hdc_screen);
            return false;
        }

        BITMAPINFO bmi = {};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = w;
        bmi.bmiHeader.biHeight = -h;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        std::vector<unsigned char> pixels(w * h * 4, 0);
        ::GetDIBits(hdc_mem, hbm, 0, h, pixels.data(), &bmi, DIB_RGB_COLORS);

        for (int i = 0; i < w * h; ++i) pixels[i * 4 + 3] = 255;

        for (int i = 0; i < w * h; ++i) pixels[i * 4 + 3] = 255;

        ::SelectObject(hdc_mem, old);
        ::DeleteObject(hbm);
        ::DeleteDC(hdc_mem);
        ::ReleaseDC(nullptr, hdc_screen);

        // ==== Проверка: не пустое ли превью (всё чёрное) ====
        int nonblack = 0;
        for (int i = 0; i < w * h; ++i) {
            const int r = pixels[i * 4], g = pixels[i * 4 + 1], b = pixels[i * 4 + 2];
            if (r + g + b > 30) nonblack++;
        }
        // Если меньше 1% пикселей не-чёрных — превью пустое
        float ratio = (float)nonblack / (float)(w * h);
        if (ratio < 0.01f) {
            return false;   // ← отдаём "не удалось"
        }

        *out_srv = MakeSRV(pixels.data(), w, h);
        *out_w = w; *out_h = h;
        return *out_srv != nullptr;
    }

    inline ID3D11ShaderResourceView* GetIcon(HWND hwnd, int* w, int* h) {
        auto it = g_icon_cache.find(hwnd);
        if (it != g_icon_cache.end()) {
            *w = it->second.w; *h = it->second.h;
            return it->second.srv;
        }
        CachedTex ci;
        if (ExtractIcon(hwnd, &ci.srv, &ci.w, &ci.h)) {
            g_icon_cache[hwnd] = ci;
            *w = ci.w; *h = ci.h;
            return ci.srv;
        }
        g_icon_cache[hwnd] = CachedTex{};
        *w = *h = 0;
        return nullptr;
    }

    inline ID3D11ShaderResourceView* GetThumb(HWND hwnd, int* w, int* h, float refresh_sec = 2.0f) {
        float now = (float)ImGui::GetTime();
        auto it = g_thumb_cache.find(hwnd);
        float& timer = g_thumb_timer[hwnd];
        bool need = (it == g_thumb_cache.end()) || (now - timer > refresh_sec);

        if (!need && it != g_thumb_cache.end()) {
            *w = it->second.w; *h = it->second.h;
            return it->second.srv;
        }

        if (it != g_thumb_cache.end() && it->second.srv) it->second.srv->Release();

        CachedTex ci;
        if (CaptureThumb(hwnd, &ci.srv, &ci.w, &ci.h)) {
            g_thumb_cache[hwnd] = ci;
            timer = now;
            *w = ci.w; *h = ci.h;
            return ci.srv;
        }
        g_thumb_cache[hwnd] = CachedTex{};
        timer = now;
        *w = *h = 0;
        return nullptr;
    }

}   // namespace win_visuals
// ======================= GUI STATE =======================
// Объявления (сами функции ниже по файлу)
inline float AnimateTo(ImGuiID id, bool target, float speed);
inline void DrawGlow(ImDrawList* dl, const ImVec2& mn, const ImVec2& mx,
    const ImVec4& c, float t, float rounding);

// === NEW: заголовок карточки - полоска акцента + текст + линия, тающая вправо ===
inline void CardTitle(const char* title, const ImVec4& accent) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float th = ImGui::GetTextLineHeight();
    dl->AddRectFilled(ImVec2(p.x, p.y + 2.0f), ImVec2(p.x + 3.0f, p.y + th - 2.0f),
        ImGui::ColorConvertFloat4ToU32(accent), 2.0f);
    ImGui::SetCursorScreenPos(ImVec2(p.x + 11.0f, p.y));
    ImGui::TextColored(accent, "%s", title);

    const ImVec2 lp = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x;
    dl->AddRectFilledMultiColor(ImVec2(p.x, lp.y), ImVec2(p.x + w, lp.y + 1.0f),
        ImGui::ColorConvertFloat4ToU32(ImVec4(accent.x, accent.y, accent.z, 0.55f)),
        ImGui::ColorConvertFloat4ToU32(ImVec4(accent.x, accent.y, accent.z, 0.0f)),
        ImGui::ColorConvertFloat4ToU32(ImVec4(accent.x, accent.y, accent.z, 0.0f)),
        ImGui::ColorConvertFloat4ToU32(ImVec4(accent.x, accent.y, accent.z, 0.55f)));
    ImGui::SetCursorScreenPos(ImVec2(p.x, lp.y));
    ImGui::Dummy(ImVec2(0.0f, 1.0f));
}

struct GuiState {
    ImVec4 accent_color, icon_color, text, text_disabled, border, frame_active, group_box_bg;
    int m_tab = 0;
    float m_anim = 1.0f;

    void group_box(const char* title, ImVec2 size) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, g_theme.card_bg);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, g_theme.card_rounding);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18, 14));
        ImGui::BeginChild(title, size, true,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        CardTitle(title, accent_color);
        ImGui::Spacing();
    }
    void group_box_scroll(const char* title, ImVec2 size) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, g_theme.card_bg);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, g_theme.card_rounding);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18, 14));
        ImGui::BeginChild(title, size, true, 0);   // 0 = разрешить скролл
        CardTitle(title, accent_color);
        ImGui::Spacing();
    }
    void end_group_box() {
        ImGui::EndChild();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();
    }
    void group_title(const char* title) {
        ImGui::PushStyleColor(ImGuiCol_Text, text_disabled);
        ImGui::TextUnformatted(title);
        ImGui::PopStyleColor();
        ImGui::Spacing();
    }
    bool tab(const char* icon, const char* label, bool active) {
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        if (window->SkipItems) return false;
        ImGuiID id = window->GetID(label);
        ImVec2 pos = window->DC.CursorPos;
        ImVec2 sz(150.0f, 32.0f);
        ImRect bb(pos, ImVec2(pos.x + sz.x, pos.y + sz.y));
        ImGui::ItemSize(bb, 0);
        if (!ImGui::ItemAdd(bb, id)) return false;
        bool hovered, held;
        bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held, ImGuiButtonFlags_MouseButtonLeft);

        // === NEW: вкладка в стиле кнопок с обводкой, плавно ===
        const ImGuiID aid = window->GetID(icon);   // не зависит от языка
        const float t_act = AnimateTo(aid ^ 0x1A2Bu, active, 14.0f);
        const float t_hov = AnimateTo(aid ^ 0x3C4Du, hovered && !active, 14.0f);
        const ImVec4& a = accent_color;
        ImDrawList* dl = window->DrawList;

        const float fill_a = 0.14f * t_act + 0.06f * t_hov;
        if (fill_a > 0.005f)
            dl->AddRectFilled(bb.Min, bb.Max, ImGui::ColorConvertFloat4ToU32(ImVec4(a.x, a.y, a.z, fill_a)), 7.0f);
        const float brd_a = 0.80f * t_act + 0.22f * t_hov;
        if (brd_a > 0.005f)
            dl->AddRect(bb.Min, bb.Max, ImGui::ColorConvertFloat4ToU32(ImVec4(a.x, a.y, a.z, brd_a)), 7.0f, 0, 1.3f);
        if (t_act > 0.01f) {
            // полоска-индикатор слева
            const float bh = 14.0f * t_act;
            const float cy = bb.GetCenter().y;
            dl->AddRectFilled(ImVec2(bb.Min.x + 5.0f, cy - bh * 0.5f), ImVec2(bb.Min.x + 8.0f, cy + bh * 0.5f),
                ImGui::ColorConvertFloat4ToU32(ImVec4(a.x, a.y, a.z, t_act)), 2.0f);
        }

        const ImVec4 icon_col = ImLerp(ImVec4(icon_color.x, icon_color.y, icon_color.z, 0.70f), a, (std::max)(t_act, t_hov * 0.6f));
        window->DrawList->AddText(ImVec2(bb.Min.x + 16, bb.GetCenter().y - ImGui::CalcTextSize(icon).y * 0.5f),
            ImGui::ColorConvertFloat4ToU32(icon_col), icon);

        const ImVec4 txt_col = ImLerp(ImVec4(text_disabled.x, text_disabled.y, text_disabled.z, 0.85f),
            ImVec4(text.x, text.y, text.z, 1.0f), (std::max)(t_act, t_hov * 0.7f));
        window->DrawList->AddText(ImVec2(bb.Min.x + 44, bb.GetCenter().y - ImGui::CalcTextSize(label).y * 0.5f),
            ImGui::ColorConvertFloat4ToU32(txt_col), label);
        return pressed;
    }
} gui;

// ======================= CALC DATA =======================
namespace calc_data {
    int   cable_material = 0;
    int   cable_install = 0;
    float load_power_kw = 3.5f;
    float voltage = 220.0f;
    int   phases = 1;
    float cos_phi = 0.95f;
    float cable_length_m = 20.0f;
    float result_current = 0.0f;
    float result_section = 0.0f;
    float result_drop_v = 0.0f;
    float result_drop_pct = 0.0f;
    // === NEW: short-circuit (Ik) check ===
    float result_ik = 0.0f;      // 0 = n/a
    float result_ik_min = 0.0f;  // In * k(кривая)
    // === NEW: manual section ===
    bool  use_manual_section = false;      // включён ручной выбор
    float manual_section = 2.5f;           // выбранное вручную сечение
    float result_required_section = 0.0f;  // требуемое сечение (без manual)
    float result_k_install = 1.0f;
    const char* result_install_name = "Air";

    // === NEW: temperature derating ===
    int   insulation_type = 0;   // 0=PVC 70C, 1=XLPE 90C, 2=Rubber 60C, 3=PE 70C (из марки)

    // === NEW: марка кабеля (индексы в списках MK_*) ===
    int   mark_type = 4;         // 2-я буква: П
    int   mark_ins = 0;          // 3-я буква: В
    int   mark_sheath = 0;       // оболочка: нет
    int   mark_design = 0;       // 4-я буква: нет
    int   mark_cores = 2;        // число жил - 1 (то есть 3 жилы)
    int   mark_u = 1;            // напряжение: 0,66 кВ
    float ambient_temp = 30.0f;  // °C
    float result_k_temp = 1.0f;  // итоговый температурный коэффициент
    int   cable_group = 1;       // сколько кабелей лежит вместе (пучок, лоток, траншея)
    float cable_altitude = 0.0f; // высота площадки над уровнем моря, м
    float result_k_alt = 1.0f;   // коэффициент высоты
    float motor_cos_target = 0.95f;
    float load_kc = 1.0f;
    float load_ko = 1.0f;
    bool  load_nonlinear = false;
    float load_h3 = 30.0f;
    float breaker_ik_ka = 3.0f;
    float breaker_temp = 30.0f;
    float cable_tmax = 4000.0f;
    float cable_price = 6.0f;
    bool  mark_reduced = false;
    int   ground_type = 0;
    float ground_strip_len = 20.0f;
    float lp_h = 10.0f;
    float lp_hx = 5.0f;
    float lp_need = 5.0f;
    int   lp_rel = 0;
    float result_load_kw = 0.0f;         // расчётная мощность с коэффициентами
    float result_ground_len_need = 0.0f; // длина полосы для нормы, м (0 = не хватает 1000 м)
    float result_k_group = 1.0f; // коэффициент групповой прокладки
    int   ground_season = 0;     // сезонный коэффициент: 0 = не учитывать, 1..4 = климатическая зона
    float motor_start_time = 5.0f;   // время пуска двигателя, с

    float total_power_kw = 0.0f;
    float total_current_a = 0.0f;
    float total_energy_kwh = 0.0f;
    float hours_per_day = 8.0f;

    int   breaker_rating = 0;
    float breaker_margin = 1.25f;
    int   breaker_curve = 1;   // 0=B, 1=C, 2=D
    float result_breaker_curve_ok = 1.0f;  // проверка пускового

    float soil_resistivity = 100.0f;
    float ground_rod_len = 3.0f;
    int   ground_rods = 1;
    float result_ground = 0.0f;

    int    conv_type = 0;
    double conv_input = 0.0;
    double conv_output = 0.0;

    bool  use_auto_calc = true;

    int   date_mode = 0;
    int   date_y1 = 2025, date_m1 = 1, date_d1 = 1;
    int   date_y2 = 2025, date_m2 = 12, date_d2 = 31;
    int   date_add_days = 30;
    char  date_result[256] = "";

    double el_q = 10.0, el_t = 2.0;
    double el_U = 220.0, el_R = 10.0;
    double el_P = 1000.0;
    double jl_I = 5.0, jl_R = 10.0, jl_t = 60.0;
    double cd_j = 5.0, cd_S = 2.5;
    double qd_a = 1.0, qd_b = -3.0, qd_c = 2.0;
    double ap_a1 = 1.0, ap_d = 2.0, ap_n = 5.0;
    double gp_b1 = 1.0, gp_q = 2.0, gp_n = 5.0;
    double tri_a = 3.0, tri_b = 4.0;
    double sl_a = 5.0, sl_A = 30.0, sl_B = 45.0;
    double cl_a = 3.0, cl_b = 4.0, cl_C = 60.0;
    double geo_a = 3.0, geo_b = 4.0, geo_c = 5.0;
    double geo_R = 5.0;

    // === NEW: motor calc ===
    float motor_power_kw = 5.5f;      // мощность на валу
    float motor_voltage = 380.0f;     // линейное напряжение
    int   motor_phases = 3;           // 1 или 3
    float motor_cos_phi = 0.85f;
    float motor_efficiency = 0.90f;   // 0.85...0.95
    float motor_start_ratio = 6.5f;   // пусковой / номинальный
    int   motor_start_type = 0;       // 0=DOL, 1=Star-Delta, 2=Soft, 3=VFD
    float result_motor_flc = 0.0f;    // номинальный ток
    float result_motor_start = 0.0f;  // пусковой ток
    float result_motor_input_kw = 0.0f;  // потребляемая мощность
    // === NEW: reverse mode ===
    int   motor_mode = 0;                // 0=Forward, 1=Reverse
    float motor_flc_input = 10.0f;       // вводимый ток (для reverse)
    float result_motor_shaft_kw = 0.0f;  // вычисляемая мощность на валу (для reverse)

    // === NEW: расчёты по ПУЭ ===
    // Кабель
    int   trafo_index = 0;               // трансформатор ТП: 0 = не учитывать
    float result_i_allow = 0.0f;         // допустимый ток сечения с учётом температуры, А
    int   result_criterion = 0;          // 0 нагрев, 1 защита, 2 падение, 3 ток КЗ, 4 нет сечения
    float result_z_trafo = 0.0f;         // Zт/3, Ом
    float result_z_loop = 0.0f;          // сопротивление петли линии, Ом
    int   result_breaker_in = 0;         // номинал автомата, по которому проверяли
    // Заземление
    float ground_spacing = 3.0f;         // расстояние между электродами, м
    float ground_depth = 0.7f;           // глубина верха электрода, м
    int   ground_electrode = 0;          // 0 = круг d16, 1 = уголок 50x50
    int   ground_norm = 0;               // 0 = 4 Ом, 1 = 10 Ом, 2 = 30 Ом
    float result_ground_single = 0.0f;   // сопротивление одного электрода, Ом
    float result_ground_eta = 1.0f;      // коэффициент использования
    int   result_ground_need = 0;        // сколько электродов нужно для нормы (0 = не хватит 100)
    // Двигатель
    float motor_soft_limit = 3.0f;       // ограничение тока УПП, x Iн
    int   result_motor_breaker = 0;      // номинал автомата
    int   result_motor_curve = 1;        // 1 = C, 2 = D
    int   result_motor_contactor = 0;    // контактор AC-3, А

    // === NEW: вкладка "Формулы" - все формулы калькуляторов ===
    double f_P = 5.0, f_U = 230.0, f_cos = 0.95;  int f_ph = 1;          // мощность и ток
    double f_tmax = 65.0, f_tamb = 35.0, f_tref = 25.0;                  // поправка на температуру
    double f_I = 16.0, f_L = 30.0, f_S = 2.5;     int f_mat = 0;         // падение напряжения
    double f_Ukz = 230.0, f_Lkz = 50.0, f_Skz = 2.5, f_Zt = 0.104, f_Rk = 0.03, f_In = 16.0;
    int    f_matkz = 0, f_curve = 1;                                      // ток КЗ
    double f_rho = 100.0, f_Lg = 2.5, f_dg = 16.0, f_tg = 0.7, f_ng = 3.0, f_eta = 0.78; // заземление
    double f_P2 = 5.5, f_eff = 0.87, f_cosm = 0.85, f_Um = 400.0, f_km = 7.0; // двигатель
    double f_Pw = 2.0, f_hours = 4.0, f_d = 1.78, f_awg = 12.0, f_tc = 25.0;  // энергия и единицы

    // Электричество
    double e_pU = 230.0, e_pI = 10.0, e_pR = 23.0;                       // мощность
    double e_wL = 20.0, e_wS = 2.5, e_wt = 70.0;  int e_wmat = 0;        // сопротивление провода
    double e_r1 = 10.0, e_r2 = 20.0, e_r3 = 30.0;                        // соединения резисторов
    double e_f = 50.0, e_R = 10.0, e_Lmh = 50.0, e_Cuf = 100.0;          // переменный ток
    double e_cU = 230.0, e_cRk = 10.0, e_cUf = 100.0;                    // конденсатор, RC
    double e_jI = 25.0, e_jS = 4.0;                                      // плотность тока
    // Алгебра
    double a_la = 2.0, a_lb = -6.0;                                      // линейное уравнение
    double a_a1 = 2.0, a_b1 = 1.0, a_c1 = 5.0, a_a2 = 1.0, a_b2 = -1.0, a_c2 = 1.0; // система 2x2
    double a_px = 15.0, a_pn = 200.0, a_pa = 80.0, a_pb = 100.0;        // проценты
    double a_ra = 2.0, a_rb = 5.0, a_rc = 8.0;                           // пропорция
    double a_base = 2.0, a_exp = 10.0, a_lx = 1000.0, a_lb2 = 10.0;     // степени, корни, логарифмы
    // Геометрия
    double g_secA = 90.0;                                                // сектор
    double g_ta = 3.0, g_tb = 4.0, g_tc = 5.0, g_tbase = 6.0, g_th = 4.0; // треугольник
    double g_rw = 4.0, g_rh = 3.0, g_za = 6.0, g_zb = 4.0, g_zh = 3.0;  // прямоугольник, трапеция
    double g_cr = 0.5, g_ch = 2.0;                                       // цилиндр, конус, шар
    double g_deg = 30.0;                                                 // угол
}

// ======================= CONFIG (.ini) =======================
namespace config {
    static const char* FILE_NAME = "settings.ini";

    inline void WriteColor(FILE* f, const char* key, const ImVec4& c) {
        fprintf(f, "%s=%.4f,%.4f,%.4f,%.4f\n", key, c.x, c.y, c.z, c.w);
    }
    inline bool ReadColorValue(const char* v, ImVec4& out) {
        float x, y, z, w;
        if (sscanf_s(v, "%f,%f,%f,%f", &x, &y, &z, &w) == 4) {
            out = ImVec4(x, y, z, w);
            return true;
        }
        return false;
    }

    inline void Save() {
        FILE* f = nullptr;
        fopen_s(&f, FILE_NAME, "w");
        if (!f) return;

        fprintf(f, "; ElectroGuiCalc by iknlm - settings\n[theme]\n");
        WriteColor(f, "accent", g_theme.accent);
        WriteColor(f, "icon_color", g_theme.icon_color);
        WriteColor(f, "window_bg", g_theme.window_bg);
        WriteColor(f, "card_bg", g_theme.card_bg);
        WriteColor(f, "card_border", g_theme.card_border);
        WriteColor(f, "switch_off", g_theme.switch_off);
        WriteColor(f, "switch_on", g_theme.switch_on);
        WriteColor(f, "scrollbar_idle", g_theme.scrollbar_idle);
        WriteColor(f, "scrollbar_hovered", g_theme.scrollbar_hovered);
        WriteColor(f, "scrollbar_active", g_theme.scrollbar_active);
        WriteColor(f, "text_main", g_theme.text_main);
        WriteColor(f, "text_dim", g_theme.text_dim);
        WriteColor(f, "radio_mark", g_theme.radio_mark);
        WriteColor(f, "radio_hover", g_theme.radio_hover);
        WriteColor(f, "frame_bg", g_theme.frame_bg);
        WriteColor(f, "btn_danger", g_theme.btn_danger);
        WriteColor(f, "btn_success", g_theme.btn_success);
        WriteColor(f, "btn_warning", g_theme.btn_warning);
        fprintf(f, "button_glow=%d\n", g_theme.button_glow ? 1 : 0);
        fprintf(f, "bg_animated=%d\n", g_theme.bg_animated ? 1 : 0);
        WriteColor(f, "res_good", g_theme.res_good);
        WriteColor(f, "res_bad", g_theme.res_bad);
        WriteColor(f, "res_warn", g_theme.res_warn);
        WriteColor(f, "res_info", g_theme.res_info);
        fprintf(f, "card_rounding=%.2f\n", g_theme.card_rounding);
        fprintf(f, "scrollbar_width=%.2f\n", g_theme.scrollbar_width);
        fprintf(f, "show_clock=%d\n", g_theme.show_clock ? 1 : 0);
        fprintf(f, "intro_animation=%d\n", g_theme.intro_animation ? 1 : 0);
        fprintf(f, "intro_duration=%.2f\n", g_theme.intro_duration);
        fprintf(f, "intro_scale_min=%.2f\n", g_theme.intro_scale_min);
        fprintf(f, "minimize_animation=%d\n", g_theme.minimize_animation ? 1 : 0);
        fprintf(f, "close_animation=%d\n", g_theme.close_animation ? 1 : 0);

        fprintf(f, "\n[calc]\n");
        fprintf(f, "cable_material=%d\n", calc_data::cable_material);
        fprintf(f, "cable_install=%d\n", calc_data::cable_install);
        fprintf(f, "cable_group=%d\n", calc_data::cable_group);
        fprintf(f, "cable_altitude=%.1f\n", calc_data::cable_altitude);
        fprintf(f, "motor_cos_target=%.4f\n", calc_data::motor_cos_target);
        fprintf(f, "load_kc=%.4f\n", calc_data::load_kc);
        fprintf(f, "load_ko=%.4f\n", calc_data::load_ko);
        fprintf(f, "load_nonlinear=%d\n", calc_data::load_nonlinear ? 1 : 0);
        fprintf(f, "load_h3=%.4f\n", calc_data::load_h3);
        fprintf(f, "breaker_ik_ka=%.4f\n", calc_data::breaker_ik_ka);
        fprintf(f, "breaker_temp=%.4f\n", calc_data::breaker_temp);
        fprintf(f, "cable_tmax=%.4f\n", calc_data::cable_tmax);
        fprintf(f, "cable_price=%.4f\n", calc_data::cable_price);
        fprintf(f, "mark_reduced=%d\n", calc_data::mark_reduced ? 1 : 0);
        fprintf(f, "ground_type=%d\n", calc_data::ground_type);
        fprintf(f, "ground_strip_len=%.4f\n", calc_data::ground_strip_len);
        fprintf(f, "lp_h=%.4f\n", calc_data::lp_h);
        fprintf(f, "lp_hx=%.4f\n", calc_data::lp_hx);
        fprintf(f, "lp_need=%.4f\n", calc_data::lp_need);
        fprintf(f, "lp_rel=%d\n", calc_data::lp_rel);
        fprintf(f, "ground_season=%d\n", calc_data::ground_season);
        fprintf(f, "motor_start_time=%.2f\n", calc_data::motor_start_time);
        fprintf(f, "mark_type=%d\n", calc_data::mark_type);
        fprintf(f, "mark_ins=%d\n", calc_data::mark_ins);
        fprintf(f, "mark_sheath=%d\n", calc_data::mark_sheath);
        fprintf(f, "mark_design=%d\n", calc_data::mark_design);
        fprintf(f, "mark_cores=%d\n", calc_data::mark_cores);
        fprintf(f, "mark_u=%d\n", calc_data::mark_u);
        fprintf(f, "load_power_kw=%.4f\n", calc_data::load_power_kw);
        fprintf(f, "voltage=%.4f\n", calc_data::voltage);
        fprintf(f, "phases=%d\n", calc_data::phases);
        fprintf(f, "cos_phi=%.4f\n", calc_data::cos_phi);
        fprintf(f, "cable_length_m=%.4f\n", calc_data::cable_length_m);
        fprintf(f, "total_power_kw=%.4f\n", calc_data::total_power_kw);
        fprintf(f, "hours_per_day=%.4f\n", calc_data::hours_per_day);
        fprintf(f, "breaker_margin=%.4f\n", calc_data::breaker_margin);
        fprintf(f, "soil_resistivity=%.4f\n", calc_data::soil_resistivity);
        fprintf(f, "ground_rod_len=%.4f\n", calc_data::ground_rod_len);
        fprintf(f, "ground_rods=%d\n", calc_data::ground_rods);
        fprintf(f, "insulation_type=%d\n", calc_data::insulation_type);
        fprintf(f, "breaker_curve=%d\n", calc_data::breaker_curve);
        fprintf(f, "ambient_temp=%.4f\n", calc_data::ambient_temp);
        fprintf(f, "motor_power_kw=%.4f\n", calc_data::motor_power_kw);
        fprintf(f, "motor_voltage=%.4f\n", calc_data::motor_voltage);
        fprintf(f, "motor_phases=%d\n", calc_data::motor_phases);
        fprintf(f, "motor_cos_phi=%.4f\n", calc_data::motor_cos_phi);
        fprintf(f, "motor_efficiency=%.4f\n", calc_data::motor_efficiency);
        fprintf(f, "motor_start_ratio=%.4f\n", calc_data::motor_start_ratio);
        fprintf(f, "motor_start_type=%d\n", calc_data::motor_start_type);
        fprintf(f, "use_auto_calc=%d\n", calc_data::use_auto_calc ? 1 : 0);
        fprintf(f, "language=%d\n", (int)i18n::g_lang);

        fclose(f);
    }

    inline void Load() {
        FILE* f = nullptr;
        fopen_s(&f, FILE_NAME, "r");
        if (!f) return;

        char line[512];
        while (fgets(line, sizeof(line), f)) {
            std::string s(line);
            while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
            while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.erase(s.begin());
            if (s.empty() || s[0] == ';' || s[0] == '[') continue;

            size_t eq = s.find('=');
            if (eq == std::string::npos) continue;

            std::string key = s.substr(0, eq);
            std::string val = s.substr(eq + 1);
            while (!key.empty() && key.back() == ' ') key.pop_back();
            while (!val.empty() && val.front() == ' ') val.erase(val.begin());

            if (key == "accent")             ReadColorValue(val.c_str(), g_theme.accent);
            else if (key == "icon_color")         ReadColorValue(val.c_str(), g_theme.icon_color);
            else if (key == "window_bg")          ReadColorValue(val.c_str(), g_theme.window_bg);
            else if (key == "card_bg")            ReadColorValue(val.c_str(), g_theme.card_bg);
            else if (key == "card_border")        ReadColorValue(val.c_str(), g_theme.card_border);
            else if (key == "switch_off")         ReadColorValue(val.c_str(), g_theme.switch_off);
            else if (key == "switch_on")          ReadColorValue(val.c_str(), g_theme.switch_on);
            else if (key == "scrollbar_idle")     ReadColorValue(val.c_str(), g_theme.scrollbar_idle);
            else if (key == "scrollbar_hovered")  ReadColorValue(val.c_str(), g_theme.scrollbar_hovered);
            else if (key == "scrollbar_active")   ReadColorValue(val.c_str(), g_theme.scrollbar_active);
            else if (key == "text_main")          ReadColorValue(val.c_str(), g_theme.text_main);
            else if (key == "text_dim")           ReadColorValue(val.c_str(), g_theme.text_dim);
            else if (key == "radio_mark")         ReadColorValue(val.c_str(), g_theme.radio_mark);
            else if (key == "radio_hover")        ReadColorValue(val.c_str(), g_theme.radio_hover);
            else if (key == "frame_bg")           ReadColorValue(val.c_str(), g_theme.frame_bg);
            else if (key == "btn_danger")         ReadColorValue(val.c_str(), g_theme.btn_danger);
            else if (key == "btn_success")        ReadColorValue(val.c_str(), g_theme.btn_success);
            else if (key == "btn_warning")        ReadColorValue(val.c_str(), g_theme.btn_warning);
            else if (key == "button_glow")        g_theme.button_glow = (atoi(val.c_str()) != 0);
            else if (key == "bg_animated")        g_theme.bg_animated = (atoi(val.c_str()) != 0);
            else if (key == "res_good")           ReadColorValue(val.c_str(), g_theme.res_good);
            else if (key == "res_bad")            ReadColorValue(val.c_str(), g_theme.res_bad);
            else if (key == "res_warn")           ReadColorValue(val.c_str(), g_theme.res_warn);
            else if (key == "res_info")           ReadColorValue(val.c_str(), g_theme.res_info);
            else if (key == "card_rounding")      g_theme.card_rounding = (float)atof(val.c_str());
            else if (key == "scrollbar_width")    g_theme.scrollbar_width = (float)atof(val.c_str());
            else if (key == "show_clock")         g_theme.show_clock = atoi(val.c_str()) != 0;
            else if (key == "intro_animation")    g_theme.intro_animation = atoi(val.c_str()) != 0;
            else if (key == "intro_duration")     g_theme.intro_duration = (float)atof(val.c_str());
            else if (key == "intro_scale_min")    g_theme.intro_scale_min = (float)atof(val.c_str());
            else if (key == "minimize_animation") g_theme.minimize_animation = atoi(val.c_str()) != 0;
            else if (key == "close_animation") g_theme.close_animation = atoi(val.c_str()) != 0;
            else if (key == "cable_material")     calc_data::cable_material = atoi(val.c_str());
            else if (key == "cable_install")      calc_data::cable_install = atoi(val.c_str());
            else if (key == "cable_group")        calc_data::cable_group = atoi(val.c_str());
            else if (key == "cable_altitude")     calc_data::cable_altitude = (float)atof(val.c_str());
            else if (key == "motor_cos_target") calc_data::motor_cos_target = (float)atof(val.c_str());
            else if (key == "load_kc") calc_data::load_kc = (float)atof(val.c_str());
            else if (key == "load_ko") calc_data::load_ko = (float)atof(val.c_str());
            else if (key == "load_nonlinear") calc_data::load_nonlinear = atoi(val.c_str()) != 0;
            else if (key == "load_h3") calc_data::load_h3 = (float)atof(val.c_str());
            else if (key == "breaker_ik_ka") calc_data::breaker_ik_ka = (float)atof(val.c_str());
            else if (key == "breaker_temp") calc_data::breaker_temp = (float)atof(val.c_str());
            else if (key == "cable_tmax") calc_data::cable_tmax = (float)atof(val.c_str());
            else if (key == "cable_price") calc_data::cable_price = (float)atof(val.c_str());
            else if (key == "mark_reduced") calc_data::mark_reduced = atoi(val.c_str()) != 0;
            else if (key == "ground_type") calc_data::ground_type = atoi(val.c_str());
            else if (key == "ground_strip_len") calc_data::ground_strip_len = (float)atof(val.c_str());
            else if (key == "lp_h") calc_data::lp_h = (float)atof(val.c_str());
            else if (key == "lp_hx") calc_data::lp_hx = (float)atof(val.c_str());
            else if (key == "lp_need") calc_data::lp_need = (float)atof(val.c_str());
            else if (key == "lp_rel") calc_data::lp_rel = atoi(val.c_str());
            else if (key == "ground_season")      calc_data::ground_season = atoi(val.c_str());
            else if (key == "motor_start_time")   calc_data::motor_start_time = (float)atof(val.c_str());
            else if (key == "mark_type")          calc_data::mark_type = atoi(val.c_str());
            else if (key == "mark_ins")           calc_data::mark_ins = atoi(val.c_str());
            else if (key == "mark_sheath")        calc_data::mark_sheath = atoi(val.c_str());
            else if (key == "mark_design")        calc_data::mark_design = atoi(val.c_str());
            else if (key == "mark_cores")         calc_data::mark_cores = atoi(val.c_str());
            else if (key == "mark_u")             calc_data::mark_u = atoi(val.c_str());
            else if (key == "load_power_kw")      calc_data::load_power_kw = (float)atof(val.c_str());
            else if (key == "voltage")            calc_data::voltage = (float)atof(val.c_str());
            else if (key == "phases")             calc_data::phases = atoi(val.c_str());
            else if (key == "cos_phi")            calc_data::cos_phi = (float)atof(val.c_str());
            else if (key == "cable_length_m")     calc_data::cable_length_m = (float)atof(val.c_str());
            else if (key == "total_power_kw")     calc_data::total_power_kw = (float)atof(val.c_str());
            else if (key == "hours_per_day")      calc_data::hours_per_day = (float)atof(val.c_str());
            else if (key == "breaker_margin")     calc_data::breaker_margin = (float)atof(val.c_str());
            else if (key == "soil_resistivity")   calc_data::soil_resistivity = (float)atof(val.c_str());
            else if (key == "ground_rod_len")     calc_data::ground_rod_len = (float)atof(val.c_str());
            else if (key == "ground_rods")        calc_data::ground_rods = atoi(val.c_str());
            else if (key == "insulation_type")    calc_data::insulation_type = atoi(val.c_str());
            else if (key == "breaker_curve")      calc_data::breaker_curve = atoi(val.c_str());
            else if (key == "ambient_temp")       calc_data::ambient_temp = (float)atof(val.c_str());
            else if (key == "motor_power_kw")     calc_data::motor_power_kw = (float)atof(val.c_str());
            else if (key == "motor_voltage")      calc_data::motor_voltage = (float)atof(val.c_str());
            else if (key == "motor_phases")       calc_data::motor_phases = atoi(val.c_str());
            else if (key == "motor_cos_phi")      calc_data::motor_cos_phi = (float)atof(val.c_str());
            else if (key == "motor_efficiency")   calc_data::motor_efficiency = (float)atof(val.c_str());
            else if (key == "motor_start_ratio")  calc_data::motor_start_ratio = (float)atof(val.c_str());
            else if (key == "motor_start_type")   calc_data::motor_start_type = atoi(val.c_str());
            else if (key == "use_auto_calc")      calc_data::use_auto_calc = atoi(val.c_str()) != 0;
            else if (key == "language") {
                const int v = atoi(val.c_str());
                i18n::g_lang = (v == 1) ? i18n::LANG_RU : i18n::LANG_EN;
            }
        }
        fclose(f);
    }
}

// ======================= WINDOW CONTROL =======================
namespace win_control {
    struct WindowEntry { HWND hwnd; std::string title, cls; std::string exe_name; };
    static std::vector<WindowEntry> g_windows;
    static int  g_selected = -1;
    static char g_search[128] = "";
    static HWND g_target = nullptr;
    static char  g_status[128] = "";
    static float g_status_timer = 0.0f;
    static int pos_x = 100, pos_y = 100, size_w = 800, size_h = 600;
    static bool topmost = false;
    static bool show_thumbnails = true;

    inline void SetStatus(const char* msg) {
        strncpy_s(g_status, msg, sizeof(g_status)); g_status_timer = 3.0f;
    }
    inline std::string ToUtf8(const wchar_t* w) {
        if (!w || !*w) return "";
        int len = ::WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
        if (len <= 0) return "";
        std::string out(len - 1, '\0');
        ::WideCharToMultiByte(CP_UTF8, 0, w, -1, out.data(), len, nullptr, nullptr);
        return out;
    }
    static BOOL CALLBACK EnumProc(HWND hwnd, LPARAM lparam) {
        auto* list = reinterpret_cast<std::vector<WindowEntry>*>(lparam);
        if (!IsWindowVisible(hwnd)) return TRUE;
        if (GetWindow(hwnd, GW_OWNER) != nullptr) return TRUE;
        if (hwnd == g_hwnd) return TRUE;

        wchar_t wt[256] = {}, wc[256] = {};
        GetWindowTextW(hwnd, wt, 256);
        GetClassNameW(hwnd, wc, 256);
        if (wt[0] == L'\0') return TRUE;

        // ==== Скрываем служебные окна Windows ====
        std::wstring cls(wc);
        if (cls == L"Progman")                          return TRUE;  // рабочий стол
        if (cls == L"Shell_TrayWnd")                    return TRUE;  // панель задач
        if (cls == L"Shell_SecondaryTrayWnd")           return TRUE;  // панель задач (2-й монитор)
        if (cls == L"WorkerW")                          return TRUE;  // обои
        if (cls == L"Windows.UI.Core.CoreWindow")       return TRUE;  // UWP-контейнер
        if (cls == L"ApplicationFrameWindow")           return TRUE;  // UWP-обёртка
        if (cls == L"CEF-OSC-WIDGET")                   return TRUE;  // NVIDIA Overlay
        if (cls == L"WindowsForms10.Window.8.app.0.141b42a_r6_ad1") return TRUE;  // Extreme Injector

        // ==== Имя процесса ====
        std::string exe_name = "?";
        DWORD pid = 0;
        ::GetWindowThreadProcessId(hwnd, &pid);
        if (pid) {
            HANDLE hProc = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
            if (hProc) {
                wchar_t path[MAX_PATH] = {};
                DWORD sz = MAX_PATH;
                if (::QueryFullProcessImageNameW(hProc, 0, path, &sz)) {
                    std::wstring wp(path);
                    size_t slash = wp.find_last_of(L"\\/");
                    if (slash != std::wstring::npos) wp = wp.substr(slash + 1);
                    exe_name = ToUtf8(wp.c_str());
                }
                ::CloseHandle(hProc);
            }
        }

        // ==== Пропускаем пустые UWP, которые пробились ====
        if (exe_name == "ApplicationFrameHost.exe" && cls == L"ApplicationFrameWindow")
            return TRUE;

        list->push_back({ hwnd, ToUtf8(wt), ToUtf8(wc), exe_name });
        return TRUE;
    }
    inline void RefreshWindowList() {
        g_windows.clear();
        EnumWindows(EnumProc, reinterpret_cast<LPARAM>(&g_windows));
        std::sort(g_windows.begin(), g_windows.end(),
            [](const WindowEntry& a, const WindowEntry& b) { return a.title < b.title; });
        SetStatus("Window list refreshed");
    }
    inline void MoveTo(HWND h, int x, int y) { if (h) ::SetWindowPos(h, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE); }
    inline void ResizeW(HWND h, int w, int ht) { if (h) ::SetWindowPos(h, nullptr, 0, 0, w, ht, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE); }
    inline void MoveResize(HWND h, int x, int y, int w, int ht) { if (h) ::SetWindowPos(h, nullptr, x, y, w, ht, SWP_NOZORDER | SWP_NOACTIVATE); }
    inline void Minimize(HWND h) { if (h) ::ShowWindow(h, SW_MINIMIZE); }
    inline void Maximize(HWND h) { if (h) ::ShowWindow(h, SW_MAXIMIZE); }
    inline void Restore(HWND h) { if (h) ::ShowWindow(h, SW_RESTORE); }
    inline void HideW(HWND h) { if (h) ::ShowWindow(h, SW_HIDE); }
    inline void ShowW(HWND h) { if (h) ::ShowWindow(h, SW_SHOW); }
    inline void SetTopmost(HWND h, bool on) { if (h) ::SetWindowPos(h, on ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE); }
    inline bool GetRectW(HWND h, int& x, int& y, int& w, int& ht) {
        if (!h) return false; RECT r; if (!::GetWindowRect(h, &r)) return false;
        x = r.left; y = r.top; w = r.right - r.left; ht = r.bottom - r.top; return true;
    }
    inline bool RequestClose(HWND h) {
        if (!h) return false;
        if (h == g_hwnd) { SetStatus("Refusing to close self"); return false; }
        return ::PostMessageW(h, WM_CLOSE, 0, 0) != 0;
    }
    inline bool ForceKill(HWND h) {
        if (!h) return false;
        if (h == g_hwnd) { SetStatus("Refusing to kill self"); return false; }
        DWORD pid = 0; ::GetWindowThreadProcessId(h, &pid);
        if (!pid) { SetStatus("Failed to get PID"); return false; }
        HANDLE p = ::OpenProcess(PROCESS_TERMINATE, FALSE, pid);
        if (!p) { SetStatus("OpenProcess failed"); return false; }
        BOOL ok = ::TerminateProcess(p, 0); ::CloseHandle(p);
        SetStatus(ok ? "Process terminated" : "TerminateProcess failed");
        return ok != FALSE;
    }
    inline void FocusWindow(HWND h) { if (!h) return; if (::IsIconic(h)) ::ShowWindow(h, SW_RESTORE); ::SetForegroundWindow(h); }
    inline void StartDrag(HWND h) {
        if (!h) { SetStatus("No target to drag"); return; }
        if (h == g_hwnd) { SetStatus("Refusing to drag self"); return; }
        ::ReleaseCapture(); ::SendMessageW(h, WM_NCLBUTTONDOWN, HTCAPTION, 0);
        SetStatus("Dragging...");
    }
}

// ======================= THEME APPLY =======================
// === NEW: ограничение яркости фоновых цветов (защита от "глазовыжигателя") ===
// Оттенок и насыщенность остаются как выбрал пользователь, режется только яркость.
inline void ClampBrightness(ImVec4& c, float max_v) {
    float h = 0.0f, s = 0.0f, v = 0.0f;
    ImGui::ColorConvertRGBtoHSV(c.x, c.y, c.z, h, s, v);
    if (v > max_v) ImGui::ColorConvertHSVtoRGB(h, s, max_v, c.x, c.y, c.z);
}

// Пределы яркости для фонов (0..1)
constexpr float MAX_B_WINDOW = 0.16f;   // фон окна
constexpr float MAX_B_CARD = 0.22f;     // фон карточек
constexpr float MAX_B_FIELD = 0.30f;    // фон полей и кружков
constexpr float MAX_B_SWOFF = 0.35f;    // выключенный переключатель

inline void ApplyThemeStyle() {
    // Фоны всегда тёмные - даже если в settings.ini записан яркий цвет
    ClampBrightness(g_theme.window_bg, MAX_B_WINDOW);
    ClampBrightness(g_theme.card_bg, MAX_B_CARD);
    ClampBrightness(g_theme.frame_bg, MAX_B_FIELD);
    ClampBrightness(g_theme.switch_off, MAX_B_SWOFF);

    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = 0.0f;
    s.ChildRounding = g_theme.card_rounding;
    s.FrameRounding = 6.0f;
    s.GrabRounding = 6.0f;
    s.WindowBorderSize = 0.0f;
    s.ChildBorderSize = 1.0f;
    s.FramePadding = ImVec2(10, 6);
    s.ItemSpacing = ImVec2(12, 10);
    s.WindowPadding = ImVec2(0, 0);
    s.ScrollbarSize = g_theme.scrollbar_width;

    s.Colors[ImGuiCol_WindowBg] = ImVec4(0, 0, 0, 0);
    s.Colors[ImGuiCol_ChildBg] = g_theme.card_bg;
    s.Colors[ImGuiCol_PopupBg] = ImVec4(g_theme.card_bg.x, g_theme.card_bg.y, g_theme.card_bg.z, 0.98f);
    s.Colors[ImGuiCol_Text] = g_theme.text_main;
    s.Colors[ImGuiCol_TextDisabled] = g_theme.text_dim;
    s.Colors[ImGuiCol_FrameBg] = g_theme.frame_bg;
    s.Colors[ImGuiCol_FrameBgHovered] = g_theme.radio_hover;
    s.Colors[ImGuiCol_FrameBgActive] = ImVec4(
        (std::min)(g_theme.radio_hover.x * 1.3f, 1.0f),
        (std::min)(g_theme.radio_hover.y * 1.3f, 1.0f),
        (std::min)(g_theme.radio_hover.z * 1.3f, 1.0f),
        (std::min)(g_theme.radio_hover.w + 0.2f, 1.0f));
    s.Colors[ImGuiCol_CheckMark] = g_theme.radio_mark;
    s.Colors[ImGuiCol_SliderGrab] = g_theme.accent;
    s.Colors[ImGuiCol_SliderGrabActive] = ImVec4(g_theme.accent.x * 1.2f, g_theme.accent.y * 1.2f, g_theme.accent.z * 1.2f, 1);
    s.Colors[ImGuiCol_Header] = ImVec4(g_theme.accent.x, g_theme.accent.y, g_theme.accent.z, 0.20f);
    s.Colors[ImGuiCol_HeaderHovered] = ImVec4(g_theme.accent.x, g_theme.accent.y, g_theme.accent.z, 0.35f);
    s.Colors[ImGuiCol_HeaderActive] = ImVec4(g_theme.accent.x, g_theme.accent.y, g_theme.accent.z, 0.50f);
    s.Colors[ImGuiCol_Border] = g_theme.card_border;
    s.Colors[ImGuiCol_Separator] = ImVec4(1, 1, 1, 0.06f);

    s.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    s.Colors[ImGuiCol_ScrollbarGrab] = g_theme.scrollbar_idle;
    s.Colors[ImGuiCol_ScrollbarGrabHovered] = g_theme.scrollbar_hovered;
    s.Colors[ImGuiCol_ScrollbarGrabActive] = g_theme.scrollbar_active;

    gui.accent_color = g_theme.accent;
    gui.icon_color = g_theme.icon_color;
    gui.text = g_theme.text_main;
    gui.text_disabled = g_theme.text_dim;
    gui.border = g_theme.card_border;
    gui.frame_active = g_theme.accent;
    gui.group_box_bg = g_theme.card_bg;
}


// ======================= UTILS =======================
static ImGuiStorage g_anim_storage;
static float tab_switch_time = 0.0f;

inline float AnimateTo(ImGuiID id, bool target, float speed = 12.0f) {
    float* t = g_anim_storage.GetFloatRef(id, target ? 1.0f : 0.0f);
    float goal = target ? 1.0f : 0.0f;
    float dt = ImGui::GetIO().DeltaTime;
    *t += (goal - *t) * (std::min)(dt * speed, 1.0f);
    return *t;
}

void BeginTabFade() {
    float elapsed = (float)ImGui::GetTime() - tab_switch_time;
    float t = ImClamp(elapsed / 0.15f, 0.0f, 1.0f);
    float eased = 1.0f - (1.0f - t) * (1.0f - t);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * eased);
}
void EndTabFade() { ImGui::PopStyleVar(); }

inline void FormatNumber(char* buf, size_t sz, double v) {
    if (std::isnan(v)) { snprintf(buf, sz, "nan"); return; }
    if (std::isinf(v)) { snprintf(buf, sz, v > 0 ? "+inf" : "-inf"); return; }
    snprintf(buf, sz, "%.10g", v);
}

// === NEW: единица измерения для поля ввода (по его id) ===
inline const char* InputUnit(const char* id) {
    struct U { const char* id; const char* en; const char* ru; };
    static const U units[] = {
        { "power", "kW", "кВт" },        { "total_power", "kW", "кВт" },  { "motor_p", "kW", "кВт" },
        { "volt", "V", "В" },            { "load_volt", "V", "В" },       { "motor_u", "V", "В" },
        { "##el_U", "V", "В" },          { "##el_U2", "V", "В" },
        { "length", "m", "м" },          { "rodlen", "m", "м" },
        { "ambient", "°C", "°C" },       { "hours", "h/day", "ч/сут" },   { "rods", "pcs", "шт" },
        { "soil", "Ohm·m", "Ом·м" },     { "mansec", "mm²", "мм²" },      { "motor_flc_in", "A", "А" },
        { "motor_sr", "×In", "×Iн" },    { "motor_eff", "eff", "КПД" },
        { "##f_P", "kW", "кВт" },       { "##f_U", "V", "В" },            { "##f_I", "A", "А" },
        { "##f_L", "m", "м" },          { "##f_S", "mm²", "мм²" },        { "##f_Ukz", "V", "В" },
        { "##f_Lkz", "m", "м" },        { "##f_Skz", "mm²", "мм²" },      { "##f_In", "A", "А" },
        { "##f_Zt", "Ohm", "Ом" },      { "##f_Rk", "Ohm", "Ом" },        { "##f_rho", "Ohm·m", "Ом·м" },
        { "##f_Lg", "m", "м" },         { "##f_dg", "mm", "мм" },         { "##f_tg", "m", "м" },
        { "##f_P2", "kW", "кВт" },      { "##f_Um", "V", "В" },           { "##f_Pw", "kW", "кВт" },
        { "##f_hours", "h", "ч" },      { "##f_d", "mm", "мм" },          { "##f_tc", "°C", "°C" },
        { "##f_tmax", "°C", "°C" },     { "##f_tamb", "°C", "°C" },       { "##f_tref", "°C", "°C" },
        { "rodspace", "m", "м" },         { "roddepth", "m", "м" },        { "motor_soft", "×In", "×Iн" },
        { "cosphi", "cos", "cos" },      { "load_cosphi", "cos", "cos" }, { "motor_cos", "cos", "cos" },
        { "##el_q", "C", "Кл" },         { "##el_t", "s", "с" },          { "##jl_t", "s", "с" },
        { "##el_R", "Ohm", "Ом" },       { "##jl_R", "Ohm", "Ом" },       { "##el_P", "W", "Вт" },
        { "##jl_I", "A", "А" },
    };
    for (const U& u : units)
        if (strcmp(u.id, id) == 0) return (i18n::g_lang == i18n::LANG_RU) ? u.ru : u.en;
    return nullptr;
}

// === NEW: удобный ввод чисел ===
// - "0" + цифра -> цифра (не "08", а "8")
// - "." в начале -> "0." (набираешь ".1" - получается "0.1")
// - запятая с русской раскладки превращается в точку
static int NumberInputCallback(ImGuiInputTextCallbackData* d) {
    if (d->EventFlag == ImGuiInputTextFlags_CallbackCharFilter) {
        const ImWchar c = d->EventChar;
        if (c == ',') { d->EventChar = '.'; return 0; }
        if ((c >= '0' && c <= '9') || c == '.' || c == '-' || c == '+' || c == 'e' || c == 'E') return 0;
        return 1;   // остальные символы не пропускаем
    }
    if (d->EventFlag == ImGuiInputTextFlags_CallbackEdit) {
        const int sign = (d->BufTextLen > 0 && (d->Buf[0] == '-' || d->Buf[0] == '+')) ? 1 : 0;
        // лишние нули в начале: "08" -> "8", "-007" -> "-7"
        while (d->BufTextLen > sign + 1 && d->Buf[sign] == '0' &&
            d->Buf[sign + 1] >= '0' && d->Buf[sign + 1] <= '9')
            d->DeleteChars(sign, 1);
        // точка в начале: ".5" -> "0.5"
        if (d->BufTextLen > sign && d->Buf[sign] == '.')
            d->InsertChars(sign, "0");
    }
    return 0;
}

inline bool TextInputDouble(const char* id, double* value, float width = -1.0f) {
    char buf[64];
    snprintf(buf, sizeof(buf), "%g", *value);
    ImGui::PushID(id);
    // Поле на всю ширину, служебное имя (id) не показываем - подпись и так стоит над полем
    ImGui::PushItemWidth(width > 0 ? width : -FLT_MIN);
    char label[96];
    snprintf(label, sizeof(label), "##%s", (id[0] == '#' && id[1] == '#') ? id + 2 : id);
    bool changed = ImGui::InputText(label, buf, sizeof(buf),
        ImGuiInputTextFlags_CallbackCharFilter | ImGuiInputTextFlags_CallbackEdit, NumberInputCallback);
    if (ImGui::IsItemDeactivatedAfterEdit() || ImGui::IsItemEdited()) {
        char* end = nullptr;
        double v = strtod(buf, &end);
        if (end != buf && *value != v) { *value = v; changed = true; }
    }

    // === NEW: рамка цветом акцента при наведении и вводе ===
    {
        const ImGuiID fid = ImGui::GetItemID();
        const float t_hov = AnimateTo(fid ^ 0x5F17u, ImGui::IsItemHovered(), 14.0f);
        const float t_act = AnimateTo(fid ^ 0x6E29u, ImGui::IsItemActive(), 14.0f);
        const float a = (std::max)(0.35f * t_hov, t_act);
        if (a > 0.01f) {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImVec2 mn = ImGui::GetItemRectMin();
            const ImVec2 mx = ImGui::GetItemRectMax();
            const ImVec4& c = g_theme.accent;
            const float rnd = ImGui::GetStyle().FrameRounding;
            dl->AddRect(mn, mx, ImGui::ColorConvertFloat4ToU32(ImVec4(c.x, c.y, c.z, a)), rnd, 0, 1.5f);
            if (g_theme.button_glow && t_act > 0.01f) {
                for (int i = 1; i <= 3; ++i) {
                    const float g = (float)i * 1.6f;
                    const float ga = 0.14f * t_act * (1.0f - (float)i / 4.0f);
                    dl->AddRect(ImVec2(mn.x - g, mn.y - g), ImVec2(mx.x + g, mx.y + g),
                        ImGui::ColorConvertFloat4ToU32(ImVec4(c.x, c.y, c.z, ga)), rnd + g, 0, 2.0f);
                }
            }
        }
    }

    // === NEW: плашка с единицей измерения справа внутри поля ===
    if (const char* unit = InputUnit(id)) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 mn = ImGui::GetItemRectMin();
        const ImVec2 mx = ImGui::GetItemRectMax();
        const ImVec2 ts = ImGui::CalcTextSize(unit);
        const float pad_x = 8.0f;
        const ImVec2 b_min(mx.x - ts.x - pad_x * 2.0f - 5.0f, mn.y + 4.0f);
        const ImVec2 b_max(mx.x - 5.0f, mx.y - 4.0f);
        const ImVec4& a = g_theme.accent;
        dl->AddRectFilled(b_min, b_max, ImGui::ColorConvertFloat4ToU32(ImVec4(a.x, a.y, a.z, 0.12f)), 5.0f);
        dl->AddRect(b_min, b_max, ImGui::ColorConvertFloat4ToU32(ImVec4(a.x, a.y, a.z, 0.30f)), 5.0f);
        dl->AddText(ImVec2(b_min.x + pad_x, (mn.y + mx.y) * 0.5f - ts.y * 0.5f),
            ImGui::ColorConvertFloat4ToU32(ImVec4(a.x, a.y, a.z, 0.95f)), unit);
    }

    ImGui::PopItemWidth();
    ImGui::PopID();
    return changed;
}
inline bool TextInputFloat(const char* id, float* value, float width = -1.0f) {
    double d = (double)*value;
    bool c = TextInputDouble(id, &d, width);
    if (c) *value = (float)d;
    return c;
}

// === NEW: кнопка "с обводкой" - текст и рамка цветом, плавная подсветка, свечение ===
namespace btn_col {
    // Ссылки на цвета темы: меняешь в настройках - меняются все кнопки
    static const ImVec4& danger = g_theme.btn_danger;
    static const ImVec4& success = g_theme.btn_success;
    static const ImVec4& warning = g_theme.btn_warning;
}

inline void DrawGlow(ImDrawList* dl, const ImVec2& mn, const ImVec2& mx,
    const ImVec4& c, float t, float rounding) {
    if (!g_theme.button_glow || t < 0.01f) return;
    for (int i = 1; i <= 4; ++i) {
        const float g = (float)i * 1.6f;
        const float a = 0.16f * t * (1.0f - (float)i / 5.0f);
        dl->AddRect(ImVec2(mn.x - g, mn.y - g), ImVec2(mx.x + g, mx.y + g),
            ImGui::ColorConvertFloat4ToU32(ImVec4(c.x, c.y, c.z, a)), rounding + g, 0, 2.0f);
    }
}

inline bool OutlineButton(const char* label, const ImVec2& size, const ImVec4& c) {
    const ImGuiID id = ImGui::GetID(label);
    const bool hovered_prev = (GImGui->HoveredIdPreviousFrame == id);
    const bool active = (ImGui::GetActiveID() == id);
    const float t_hov = AnimateTo(id ^ 0x3C1Du, hovered_prev || active, 12.0f);  // плавное наведение
    const float t_prs = AnimateTo(id ^ 0x7E2Bu, active, 20.0f);                  // нажатие

    const ImVec4 bg(c.x, c.y, c.z, 0.07f + 0.13f * t_hov + 0.12f * t_prs);
    const ImVec4 txt(c.x + (1.0f - c.x) * 0.25f * t_hov,
        c.y + (1.0f - c.y) * 0.25f * t_hov,
        c.z + (1.0f - c.z) * 0.25f * t_hov, 1.0f);   // текст чуть светлеет при наведении

    ImGui::PushStyleColor(ImGuiCol_Button, bg);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, bg);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, bg);
    ImGui::PushStyleColor(ImGuiCol_Text, txt);
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(c.x, c.y, c.z, 0.75f + 0.25f * t_hov));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.5f);
    const bool pressed = ImGui::ButtonEx(label, size);
    if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    DrawGlow(ImGui::GetWindowDrawList(), ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
        c, t_hov, ImGui::GetStyle().FrameRounding);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(5);
    return pressed;
}

// Без цвета - цвет акцента темы
inline bool OutlineButton(const char* label, const ImVec2& size = ImVec2(0, 0)) {
    return OutlineButton(label, size, g_theme.accent);
}

// === NEW: рамка цветом акцента у поля ввода (наведение / ввод) ===
inline void DrawFieldFocus() {
    const ImGuiID fid = ImGui::GetItemID();
    const float t_hov = AnimateTo(fid ^ 0x5F17u, ImGui::IsItemHovered(), 14.0f);
    const float t_act = AnimateTo(fid ^ 0x6E29u, ImGui::IsItemActive(), 14.0f);
    const float a = (std::max)(0.35f * t_hov, t_act);
    if (a < 0.01f) return;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 mn = ImGui::GetItemRectMin();
    const ImVec2 mx = ImGui::GetItemRectMax();
    const ImVec4& c = g_theme.accent;
    const float rnd = ImGui::GetStyle().FrameRounding;
    dl->AddRect(mn, mx, ImGui::ColorConvertFloat4ToU32(ImVec4(c.x, c.y, c.z, a)), rnd, 0, 1.5f);
    DrawGlow(dl, mn, mx, c, t_act * 0.8f, rnd);
}

// === NEW: квадратная кнопка "-" / "+" в стиле чекбокса (зажми - повторяется) ===
inline bool StepButton(const char* id, bool plus, float sz) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;
    const ImGuiID bid = window->GetID(id);
    const ImVec2 pos = window->DC.CursorPos;
    const ImRect bb(pos, ImVec2(pos.x + sz, pos.y + sz));
    ImGui::ItemSize(bb, 0.0f);
    ImGui::PushItemFlag(ImGuiItemFlags_ButtonRepeat, true);
    const bool added = ImGui::ItemAdd(bb, bid);
    ImGui::PopItemFlag();
    if (!added) return false;

    bool hovered = false, held = false;
    const bool pressed = ImGui::ButtonBehavior(bb, bid, &hovered, &held);
    if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

    const float t = AnimateTo(bid, hovered || held, 14.0f);
    const float tp = AnimateTo(bid ^ 0x2D2Du, held, 20.0f);
    const ImVec4& a = g_theme.accent;
    const ImVec4& fb = g_theme.frame_bg;
    ImDrawList* dl = window->DrawList;

    const ImVec4 bg(fb.x + (a.x - fb.x) * (0.10f * t + 0.15f * tp),
        fb.y + (a.y - fb.y) * (0.10f * t + 0.15f * tp),
        fb.z + (a.z - fb.z) * (0.10f * t + 0.15f * tp), 1.0f);
    dl->AddRectFilled(bb.Min, bb.Max, ImGui::ColorConvertFloat4ToU32(bg), 5.0f);
    dl->AddRect(bb.Min, bb.Max, ImGui::ColorConvertFloat4ToU32(ImVec4(a.x, a.y, a.z, 0.30f + 0.55f * t)), 5.0f, 0, 1.3f);
    DrawGlow(dl, bb.Min, bb.Max, a, t * 0.6f, 5.0f);

    // Значок линиями, точно по центру
    const float cx = ImFloor(bb.GetCenter().x) + 0.5f;
    const float cy = ImFloor(bb.GetCenter().y) + 0.5f;
    const float r = sz * 0.20f;
    const ImVec4 ic4(a.x + (1.0f - a.x) * 0.35f * t, a.y + (1.0f - a.y) * 0.35f * t, a.z + (1.0f - a.z) * 0.35f * t, 1.0f);
    const ImU32 ic = ImGui::ColorConvertFloat4ToU32(ic4);
    dl->AddLine(ImVec2(cx - r, cy), ImVec2(cx + r, cy), ic, 1.8f);
    if (plus) dl->AddLine(ImVec2(cx, cy - r), ImVec2(cx, cy + r), ic, 1.8f);
    return pressed;
}

// === NEW: замена ImGui::InputInt - поле + кнопки -/+ в новом стиле, подпись справа ===
inline bool NiceInputInt(const char* label, int* v, int step = 1, int step_fast = 100,
    ImGuiInputTextFlags flags = 0) {
    (void)step_fast;
    const ImGuiStyle& style = ImGui::GetStyle();
    ImGui::PushID(label);
    const float fh = ImGui::GetFrameHeight();
    const float sp = 4.0f;
    const float total = ImGui::CalcItemWidth();
    const float field_w = (step > 0) ? (std::max)(total - (fh + sp) * 2.0f, 30.0f) : total;

    ImGui::SetNextItemWidth(field_w);
    bool changed = ImGui::InputScalar("##v", ImGuiDataType_S32, v, nullptr, nullptr, "%d", flags);
    DrawFieldFocus();

    if (step > 0) {
        ImGui::SameLine(0.0f, sp);
        if (StepButton("##minus", false, fh)) { *v -= step; changed = true; }
        ImGui::SameLine(0.0f, sp);
        if (StepButton("##plus", true, fh)) { *v += step; changed = true; }
    }

    // Видимая часть подписи (до "##"), например "X"
    const char* end = strstr(label, "##");
    if (end != label) {
        ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(g_theme.text_dim, "%.*s", end ? (int)(end - label) : (int)strlen(label), label);
    }
    ImGui::PopID();
    return changed;
}

// === NEW: замена ImGui::SmallButton - "-##.."/"+##.." рисуются как StepButton,
// остальные - маленькие кнопки с обводкой ===
inline bool SmallOutlineButton(const char* label) {
    if ((label[0] == '-' || label[0] == '+') && label[1] == '#' && label[2] == '#')
        return StepButton(label, label[0] == '+', ImGui::GetFrameHeight());
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 3.0f));
    const bool pressed = OutlineButton(label);
    ImGui::PopStyleVar();
    return pressed;
}

// === NEW: красивый чекбокс - скруглённый квадрат, галочка "прорисовывается" анимацией ===
inline bool NiceCheckbox(const char* label, bool* v) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;
    const ImGuiStyle& style = ImGui::GetStyle();
    const ImGuiID id = window->GetID(label);
    const ImVec2 label_size = ImGui::CalcTextSize(label, nullptr, true);

    const float box = 18.0f;
    const float h = ImGui::GetFrameHeight();
    const ImVec2 pos = window->DC.CursorPos;
    const float w = box + (label_size.x > 0.0f ? style.ItemInnerSpacing.x + label_size.x : 0.0f);
    const ImRect bb(pos, ImVec2(pos.x + w, pos.y + h));
    ImGui::ItemSize(bb, style.FramePadding.y);
    if (!ImGui::ItemAdd(bb, id)) return false;

    bool hovered = false, held = false;
    const bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);
    if (pressed) { *v = !*v; ImGui::MarkItemEdited(id); }
    if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

    const float t = AnimateTo(id, *v, 16.0f);               // 0 = выкл, 1 = вкл
    const float th = AnimateTo(id ^ 0x55AAu, hovered, 14.0f); // наведение
    const ImVec4& a = g_theme.accent;
    const ImVec4& fb = g_theme.frame_bg;
    ImDrawList* dl = window->DrawList;

    const ImVec2 bmin(pos.x, pos.y + (h - box) * 0.5f);
    const ImVec2 bmax(bmin.x + box, bmin.y + box);

    // Фон: из цвета полей в полупрозрачный акцент
    const ImVec4 bg(fb.x + (a.x - fb.x) * 0.22f * t, fb.y + (a.y - fb.y) * 0.22f * t,
        fb.z + (a.z - fb.z) * 0.22f * t, 1.0f);
    dl->AddRectFilled(bmin, bmax, ImGui::ColorConvertFloat4ToU32(bg), 5.0f);
    // Рамка: еле заметная -> цвет акцента
    const float ba = 0.25f + 0.25f * th + 0.50f * t;
    dl->AddRect(bmin, bmax, ImGui::ColorConvertFloat4ToU32(ImVec4(a.x, a.y, a.z, ba)), 5.0f, 0, 1.3f);
    DrawGlow(dl, bmin, bmax, a, t * 0.6f, 5.0f);

    // Галочка: тонкая, рисуется по мере t (сначала короткий штрих, потом длинный)
    if (t > 0.01f) {
        const ImVec2 p0(bmin.x + box * 0.24f, bmin.y + box * 0.53f);
        const ImVec2 p1(bmin.x + box * 0.43f, bmin.y + box * 0.71f);
        const ImVec2 p2(bmin.x + box * 0.77f, bmin.y + box * 0.31f);
        const ImU32 cc = ImGui::ColorConvertFloat4ToU32(ImVec4(a.x, a.y, a.z, 1.0f));
        const float k1 = ImClamp(t / 0.4f, 0.0f, 1.0f);            // первый штрих
        const float k2 = ImClamp((t - 0.4f) / 0.6f, 0.0f, 1.0f);   // второй штрих
        const ImVec2 e1(p0.x + (p1.x - p0.x) * k1, p0.y + (p1.y - p0.y) * k1);
        dl->PathLineTo(p0);
        dl->PathLineTo(e1);
        if (k2 > 0.0f) dl->PathLineTo(ImVec2(p1.x + (p2.x - p1.x) * k2, p1.y + (p2.y - p1.y) * k2));
        dl->PathStroke(cc, 0, 2.0f);
    }

    // Подпись
    if (label_size.x > 0.0f) {
        const ImVec4& tm = g_theme.text_main;
        const ImVec4& td = g_theme.text_dim;
        const float k = (std::max)(t, th * 0.5f);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(td.x + (tm.x - td.x) * k, td.y + (tm.y - td.y) * k,
            td.z + (tm.z - td.z) * k, 1.0f));
        ImGui::RenderText(ImVec2(bmax.x + style.ItemInnerSpacing.x, pos.y + style.FramePadding.y), label);
        ImGui::PopStyleColor();
    }
    return pressed;
}

inline bool ToggleSwitch(const char* label, bool* value, float width = 46.0f, float height = 26.0f) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;
    const ImGuiID id = window->GetID(label);
    const ImVec2 label_pos = window->DC.CursorPos;
    ImVec2 row_size(window->WorkRect.Max.x - label_pos.x, height);
    ImRect row_bb(label_pos, ImVec2(label_pos.x + row_size.x, label_pos.y + row_size.y));
    ImGui::ItemSize(row_size, 6.0f);
    if (!ImGui::ItemAdd(row_bb, id)) return false;

    // Один элемент с одним ID (раньше поверх рисовалась ещё InvisibleButton с тем же ID)
    bool hovered = false, held = false;
    const bool clicked = ImGui::ButtonBehavior(row_bb, id, &hovered, &held, ImGuiButtonFlags_MouseButtonLeft);
    if (clicked) *value = !*value;
    if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

    float t_hover = AnimateTo(id ^ 0xAA, hovered, 14.0f);
    float t_active = AnimateTo(id, *value, 16.0f);

    ImU32 row_bg = ImGui::ColorConvertFloat4ToU32(ImVec4(
        g_theme.card_bg.x + 0.03f, g_theme.card_bg.y + 0.03f,
        g_theme.card_bg.z + 0.04f, 0.6f + t_hover * 0.15f));
    window->DrawList->AddRectFilled(row_bb.Min, row_bb.Max, row_bg, 6.0f);

    window->DrawList->AddText(
        ImVec2(label_pos.x + 8.0f, label_pos.y + (height - ImGui::CalcTextSize(label).y) * 0.5f),
        ImGui::ColorConvertFloat4ToU32(g_theme.text_main), label);

    float pill_w = 46.0f, pill_h = 24.0f;
    ImVec2 pill_pos(row_bb.Max.x - pill_w - 8.0f, label_pos.y + (height - pill_h) * 0.5f);

    ImVec4 bg_lerp = ImLerp(g_theme.switch_off, g_theme.switch_on, t_active);
    ImU32 bg_col = ImGui::ColorConvertFloat4ToU32(bg_lerp);
    window->DrawList->AddRectFilled(pill_pos,
        ImVec2(pill_pos.x + pill_w, pill_pos.y + pill_h), bg_col, pill_h * 0.5f);

    float knob_r = pill_h * 0.5f - 2.0f;
    float knob_x = ImLerp(pill_pos.x + pill_h * 0.5f, pill_pos.x + pill_w - pill_h * 0.5f, t_active);
    ImVec2 knob_c(knob_x, pill_pos.y + pill_h * 0.5f);
    window->DrawList->AddCircleFilled(knob_c, knob_r,
        ImGui::ColorConvertFloat4ToU32(ImVec4(1, 1, 1, 1)), 24);

    return clicked;
}

// Ползунок + поле для ручного ввода. Введённое число автоматически
// ограничивается диапазоном: больше максимума -> максимум, меньше минимума -> минимум.
inline bool LabeledSlider(const char* label, float* value, float min, float max,
    const char* fmt = "%.1f", float width = 140.0f) {
    const float input_w = 72.0f;
    const float gap = 8.0f;
    ImGui::PushID(label);
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(g_theme.text_main, "%s", label);
    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x - width - gap - input_w - 10.0f);

    ImGui::PushItemWidth(width);
    bool changed = ImGui::SliderFloat("##slider", value, min, max, fmt, ImGuiSliderFlags_AlwaysClamp);
    ImGui::PopItemWidth();

    // Поле ручного ввода
    ImGui::SameLine(0.0f, gap);
    ImGui::PushItemWidth(input_w);
    float typed = *value;
    if (ImGui::InputFloat("##input", &typed, 0.0f, 0.0f, fmt)) {
        *value = typed;
        changed = true;
    }
    DrawFieldFocus();
    ImGui::PopItemWidth();

    // Ограничение диапазоном
    if (*value > max) { *value = max; changed = true; }
    if (*value < min) { *value = min; changed = true; }

    ImGui::PopID();
    return changed;
}
inline void ResultRow(const char* label, const char* value, ImVec4 color) {
    ImGui::TextColored(g_theme.text_dim, "%s", label);
    ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - ImGui::CalcTextSize(value).x - 10.0f);
    ImGui::TextColored(color, "%s", value);
}
// === NEW: мягкое свечение ВНУТРЬ прямоугольника (несколько тонких рамок к центру) ===
inline void DrawInnerGlow(ImDrawList* dl, const ImVec2& mn, const ImVec2& mx,
    const ImVec4& c, float t, float rounding) {
    if (t < 0.01f) return;
    for (int i = 1; i <= 5; ++i) {
        const float g = (float)i * 1.4f;
        const float a = 0.16f * t * (1.0f - (float)i / 6.0f);
        dl->AddRect(ImVec2(mn.x + g, mn.y + g), ImVec2(mx.x - g, mx.y - g),
            ImGui::ColorConvertFloat4ToU32(ImVec4(c.x, c.y, c.z, a)),
            (std::max)(rounding - g, 0.0f), 0, 1.6f);
    }
}

// === NEW: выпадающий список - скругления, свечение внутрь, стрелка ===
inline bool CustomCombo(const char* id, int* current, const char* const items[], int items_count) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;
    const ImGuiID gid = window->GetID(id);
    const ImVec2 pos = window->DC.CursorPos;
    const ImVec2 sz = ImVec2(ImGui::GetContentRegionAvail().x, 34.0f);
    const ImRect bb(pos, ImVec2(pos.x + sz.x, pos.y + sz.y));
    ImGui::ItemSize(bb, 0);
    if (!ImGui::ItemAdd(bb, gid)) return false;
    bool hovered = false, held = false;
    const bool pressed = ImGui::ButtonBehavior(bb, gid, &hovered, &held);
    if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

    const bool is_open = ImGui::IsPopupOpen(id);
    const float t_hov = AnimateTo(gid ^ 0x11C0u, hovered, 14.0f);
    const float t_open = AnimateTo(gid ^ 0x22C0u, is_open, 14.0f);
    const float t = (std::max)(t_hov, t_open);

    const ImVec4& a = g_theme.accent;
    const ImVec4& fb = g_theme.frame_bg;
    const float rnd = 9.0f;
    ImDrawList* dl = window->DrawList;

    const ImVec4 bg(fb.x + (a.x - fb.x) * 0.08f * t, fb.y + (a.y - fb.y) * 0.08f * t,
        fb.z + (a.z - fb.z) * 0.08f * t, 1.0f);
    dl->AddRectFilled(bb.Min, bb.Max, ImGui::ColorConvertFloat4ToU32(bg), rnd);
    DrawInnerGlow(dl, bb.Min, bb.Max, a, 0.35f + 0.65f * t, rnd);
    dl->AddRect(bb.Min, bb.Max, ImGui::ColorConvertFloat4ToU32(ImVec4(a.x, a.y, a.z, 0.30f + 0.60f * t)), rnd, 0, 1.3f);

    const char* preview = (*current >= 0 && *current < items_count) ? items[*current] : "?";
    const ImVec2 tsz = ImGui::CalcTextSize(preview);
    dl->AddText(ImVec2(bb.Min.x + 14.0f, bb.GetCenter().y - tsz.y * 0.5f),
        ImGui::ColorConvertFloat4ToU32(g_theme.text_main), preview);

    // Стрелка справа, переворачивается при открытии
    {
        const float cx = bb.Max.x - 20.0f, cy = bb.GetCenter().y;
        const float w = 5.0f, h = 3.0f * (1.0f - 2.0f * t_open);   // вниз -> вверх
        const ImU32 ac = ImGui::ColorConvertFloat4ToU32(ImVec4(a.x, a.y, a.z, 0.65f + 0.35f * t));
        dl->PathLineTo(ImVec2(cx - w, cy - h));
        dl->PathLineTo(ImVec2(cx, cy + h));
        dl->PathLineTo(ImVec2(cx + w, cy - h));
        dl->PathStroke(ac, 0, 1.8f);
    }

    if (pressed) ImGui::OpenPopup(id);
    bool changed = false;
    ImGui::SetNextWindowPos(ImVec2(bb.Min.x, bb.Max.y + 4.0f));
    ImGui::SetNextWindowSize(ImVec2(bb.GetWidth(), 0));

    // Оформление самого списка
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 10.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0f, 6.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 3.0f));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(g_theme.card_bg.x, g_theme.card_bg.y, g_theme.card_bg.z, 0.98f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(a.x, a.y, a.z, 0.40f));
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0, 0, 0, 0));

    ImGui::SetNextWindowSizeConstraints(ImVec2(0.0f, 0.0f), ImVec2(FLT_MAX, 420.0f));   // длинные списки прокручиваются
    if (ImGui::BeginPopup(id)) {
        ImDrawList* pdl = ImGui::GetWindowDrawList();
        for (int i = 0; i < items_count; ++i) {
            const bool selected = (i == *current);
            ImGui::PushID(i);
            if (ImGui::Selectable("##item", selected, 0, ImVec2(0, 28))) {
                *current = i; changed = true; ImGui::CloseCurrentPopup();
            }
            const bool ih = ImGui::IsItemHovered();
            const ImVec2 mn = ImGui::GetItemRectMin();
            const ImVec2 mx = ImGui::GetItemRectMax();
            const float th = AnimateTo(ImGui::GetItemID(), ih, 16.0f);
            const float ts = selected ? 1.0f : 0.0f;

            if (ts > 0.0f || th > 0.01f) {
                const float fill = 0.16f * ts + 0.08f * th;
                pdl->AddRectFilled(mn, mx, ImGui::ColorConvertFloat4ToU32(ImVec4(a.x, a.y, a.z, fill)), 7.0f);
                DrawInnerGlow(pdl, mn, mx, a, 0.8f * ts + 0.4f * th, 7.0f);
                pdl->AddRect(mn, mx, ImGui::ColorConvertFloat4ToU32(
                    ImVec4(a.x, a.y, a.z, 0.65f * ts + 0.25f * th)), 7.0f, 0, 1.2f);
            }
            if (selected)   // полоска-индикатор слева
                pdl->AddRectFilled(ImVec2(mn.x + 5.0f, mn.y + 8.0f), ImVec2(mn.x + 8.0f, mx.y - 8.0f),
                    ImGui::ColorConvertFloat4ToU32(a), 2.0f);

            const ImVec4& tm = g_theme.text_main;
            const ImVec4& td = g_theme.text_dim;
            const float k = (std::max)(ts, th);
            const ImVec4 tc = ImLerp(td, selected ? ImVec4(a.x + (1 - a.x) * 0.3f, a.y + (1 - a.y) * 0.3f, a.z + (1 - a.z) * 0.3f, 1.0f) : tm, k);
            const ImVec2 isz = ImGui::CalcTextSize(items[i]);
            pdl->AddText(ImVec2(mn.x + 16.0f, (mn.y + mx.y) * 0.5f - isz.y * 0.5f),
                ImGui::ColorConvertFloat4ToU32(tc), items[i]);
            ImGui::PopID();
        }
        ImGui::EndPopup();
    }
    ImGui::PopStyleColor(5);
    ImGui::PopStyleVar(4);
    return changed;
}

inline bool SectionHeader(const char* label, bool* open_state) {
    ImGui::PushID(label);
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) { ImGui::PopID(); return false; }

    ImGuiID id = window->GetID("##section");
    ImVec2 pos = window->DC.CursorPos;
    ImVec2 sz = ImVec2(ImGui::GetContentRegionAvail().x, 34.0f);
    ImRect bb(pos, ImVec2(pos.x + sz.x, pos.y + sz.y));

    ImGui::ItemSize(bb, 4.0f);
    if (!ImGui::ItemAdd(bb, id)) { ImGui::PopID(); return *open_state; }   // заголовок за экраном - секция остаётся открытой

    bool hovered, held;
    bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);
    if (pressed) *open_state = !*open_state;
    if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

    // === NEW: в стиле остального меню - плавная подсветка, свечение внутрь ===
    const float t_hov = AnimateTo(id ^ 0x5EC1u, hovered, 14.0f);
    const float t_open = AnimateTo(id ^ 0x5EC2u, *open_state, 12.0f);
    const ImVec4& a = g_theme.accent;
    const ImVec4& cb = g_theme.card_bg;
    const float rnd = 8.0f;
    ImDrawList* dl = window->DrawList;

    const float mix = 0.05f + 0.07f * t_hov + 0.04f * t_open;
    const ImVec4 bg(cb.x + 0.03f + (a.x - cb.x) * mix, cb.y + 0.03f + (a.y - cb.y) * mix,
        cb.z + 0.04f + (a.z - cb.z) * mix, 1.0f);
    dl->AddRectFilled(bb.Min, bb.Max, ImGui::ColorConvertFloat4ToU32(bg), rnd);
    DrawInnerGlow(dl, bb.Min, bb.Max, a, 0.25f * t_open + 0.75f * t_hov, rnd);
    dl->AddRect(bb.Min, bb.Max, ImGui::ColorConvertFloat4ToU32(
        ImVec4(a.x, a.y, a.z, 0.22f + 0.30f * t_open + 0.40f * t_hov)), rnd, 0, 1.3f);

    // Полоска-индикатор слева у открытой секции
    if (t_open > 0.01f) {
        const float bh = 16.0f * t_open, cy = bb.GetCenter().y;
        dl->AddRectFilled(ImVec2(bb.Min.x + 6.0f, cy - bh * 0.5f), ImVec2(bb.Min.x + 9.0f, cy + bh * 0.5f),
            ImGui::ColorConvertFloat4ToU32(ImVec4(a.x, a.y, a.z, t_open)), 2.0f);
    }

    // Значок "+" -> "-": вертикальная черта плавно исчезает при открытии
    {
        const float cx = ImFloor(bb.Min.x + 24.0f) + 0.5f;
        const float cy = ImFloor(bb.GetCenter().y) + 0.5f;
        const float r = 5.0f;
        const ImU32 ic = ImGui::ColorConvertFloat4ToU32(ImVec4(a.x, a.y, a.z, 0.75f + 0.25f * t_hov));
        dl->AddLine(ImVec2(cx - r, cy), ImVec2(cx + r, cy), ic, 1.8f);
        const float vr = r * (1.0f - t_open);
        if (vr > 0.3f) dl->AddLine(ImVec2(cx, cy - vr), ImVec2(cx, cy + vr), ic, 1.8f);
    }

    const ImVec4& tm = g_theme.text_main;
    const ImVec4 tc(tm.x + (a.x - tm.x) * 0.25f * t_hov, tm.y + (a.y - tm.y) * 0.25f * t_hov,
        tm.z + (a.z - tm.z) * 0.25f * t_hov, 1.0f);
    ImVec2 text_size = ImGui::CalcTextSize(label);
    ImVec2 text_pos(bb.Min.x + 42, bb.GetCenter().y - text_size.y * 0.5f);
    dl->AddText(text_pos, ImGui::ColorConvertFloat4ToU32(tc), label);

    ImGui::PopID();
    return *open_state;
}

inline void GetTimeString(char* buf, size_t sz) {
    time_t t = time(nullptr);
    struct tm lt;
    localtime_s(&lt, &t);
    snprintf(buf, sz, "%02d:%02d:%02d", lt.tm_hour, lt.tm_min, lt.tm_sec);
}

// === NEW: анимированный фон - сетка точек ===
// Точки еле видны; под двумя медленно плывущими "огоньками" и под курсором
// они плавно разгораются цветом акцента. Фон остаётся непрозрачным и тёмным.
inline void DrawDotGridBackground(ImDrawList* dl, const ImVec2& pos, const ImVec2& size, float fade = 1.0f) {
    const float step = 24.0f;                          // расстояние между точками
    const ImVec4& a = g_theme.accent;
    const ImVec4& d = g_theme.text_dim;
    const float t = g_theme.bg_animated ? (float)ImGui::GetTime() : 0.0f;

    // Два огонька медленно плавают по окну
    const ImVec2 c1(pos.x + size.x * (0.50f + 0.38f * sinf(t * 0.13f)),
        pos.y + size.y * (0.50f + 0.34f * cosf(t * 0.17f)));
    const ImVec2 c2(pos.x + size.x * (0.50f + 0.40f * cosf(t * 0.11f + 1.3f)),
        pos.y + size.y * (0.50f + 0.36f * sinf(t * 0.09f + 2.1f)));
    const float r_spot = (std::min)(size.x, size.y) * 0.42f;

    // Подсветка под курсором
    const ImVec2 m = ImGui::GetIO().MousePos;
    const bool mouse_in = g_theme.bg_animated && m.x >= pos.x && m.y >= pos.y &&
        m.x <= pos.x + size.x && m.y <= pos.y + size.y;
    const float r_mouse = 150.0f;

    dl->PushClipRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), true);
    const float ox = pos.x + fmodf(size.x, step) * 0.5f;
    const float oy = pos.y + fmodf(size.y, step) * 0.5f;
    for (float y = oy; y < pos.y + size.y; y += step) {
        for (float x = ox; x < pos.x + size.x; x += step) {
            float k = 0.0f;
            if (g_theme.bg_animated) {
                const float d1 = sqrtf((x - c1.x) * (x - c1.x) + (y - c1.y) * (y - c1.y));
                const float d2 = sqrtf((x - c2.x) * (x - c2.x) + (y - c2.y) * (y - c2.y));
                const float s1 = (std::max)(0.0f, 1.0f - d1 / r_spot);
                const float s2 = (std::max)(0.0f, 1.0f - d2 / r_spot);
                k = s1 * s1 * (3.0f - 2.0f * s1) * 0.75f + s2 * s2 * (3.0f - 2.0f * s2) * 0.65f;
                // лёгкая волна по диагонали
                k += 0.12f * (0.5f + 0.5f * sinf((x + y) * 0.012f - t * 0.9f));
                if (mouse_in) {
                    const float dm = sqrtf((x - m.x) * (x - m.x) + (y - m.y) * (y - m.y));
                    const float sm = (std::max)(0.0f, 1.0f - dm / r_mouse);
                    k += sm * sm * 0.9f;
                }
                if (k > 1.0f) k = 1.0f;
            }
            const ImVec4 col(d.x + (a.x - d.x) * k, d.y + (a.y - d.y) * k, d.z + (a.z - d.z) * k,
                (0.16f + 0.70f * k) * fade);
            dl->AddCircleFilled(ImVec2(x, y), 1.2f + 1.2f * k, ImGui::ColorConvertFloat4ToU32(col), 8);
        }
    }
    dl->PopClipRect();
}

inline bool WinButton(const char* id, const char* symbol, ImVec2 sz,
    ImVec4 base_col, ImVec4 hover_col, float symbol_scale) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;

    ImGuiID bid = window->GetID(id);
    ImVec2 pos = window->DC.CursorPos;
    ImRect bb(pos, ImVec2(pos.x + sz.x, pos.y + sz.y));
    ImGui::ItemSize(bb, 0);
    if (!ImGui::ItemAdd(bb, bid)) return false;

    bool hovered, held;
    bool pressed = ImGui::ButtonBehavior(bb, bid, &hovered, &held,
        ImGuiButtonFlags_MouseButtonLeft);

    // === NEW: в стиле кнопок с обводкой. base_col не используется, hover_col = цвет кнопки ===
    (void)base_col;
    const float t = AnimateTo(bid, hovered, 14.0f);
    const ImVec4& tc = hover_col;
    ImDrawList* dl = window->DrawList;
    dl->AddRectFilled(bb.Min, bb.Max, ImGui::ColorConvertFloat4ToU32(ImVec4(tc.x, tc.y, tc.z, 0.07f + 0.18f * t)), 8.0f);
    dl->AddRect(bb.Min, bb.Max, ImGui::ColorConvertFloat4ToU32(ImVec4(tc.x, tc.y, tc.z, 0.45f + 0.45f * t)), 8.0f, 0, 1.3f);
    DrawGlow(dl, bb.Min, bb.Max, tc, t, 8.0f);

    // Значок рисуем линиями точно по центру (буквы шрифта сидят криво)
    const ImU32 ic = ImGui::ColorConvertFloat4ToU32(ImLerp(tc, ImVec4(1.0f, 1.0f, 1.0f, 1.0f), 0.35f * t));
    const float cx = ImFloor(bb.GetCenter().x) + 0.5f;
    const float cy = ImFloor(bb.GetCenter().y) + 0.5f;
    const float r = 3.4f * symbol_scale;    // половина размера значка (~5.5 px)
    const float th = 1.6f;                  // толщина линий

    if (symbol[0] == '-' && symbol[1] == '\0') {
        dl->AddLine(ImVec2(cx - r, cy), ImVec2(cx + r, cy), ic, th);
    }
    else if (symbol[0] == '+' && symbol[1] == '\0') {
        dl->AddLine(ImVec2(cx - r, cy), ImVec2(cx + r, cy), ic, th);
        dl->AddLine(ImVec2(cx, cy - r), ImVec2(cx, cy + r), ic, th);
    }
    else if ((symbol[0] == 'x' || symbol[0] == 'X') && symbol[1] == '\0') {
        const float d = r * 0.9f;
        dl->AddLine(ImVec2(cx - d, cy - d), ImVec2(cx + d, cy + d), ic, th);
        dl->AddLine(ImVec2(cx - d, cy + d), ImVec2(cx + d, cy - d), ic, th);
    }
    else {
        // Любой другой символ - как раньше, текстом
        ImFont* font = ImGui::GetFont();
        const float font_size = ImGui::GetFontSize() * symbol_scale;
        const ImVec2 ts = font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, symbol);
        dl->AddText(font, font_size, ImVec2(bb.GetCenter().x - ts.x * 0.5f,
            bb.GetCenter().y - ts.y * 0.5f), ic, symbol);
    }

    return pressed;
}

inline void ResetImGuiInputState() {
    if (!g_imgui_ready) return;
    if (ImGui::GetCurrentContext() == nullptr) return;
    ImGuiIO& io = ImGui::GetIO();
    io.MouseDown[0] = false;
    io.MouseDown[1] = false;
    io.MouseDown[2] = false;
    io.MousePos = ImVec2(-FLT_MAX, -FLT_MAX);
    ImGui::ClearActiveID();
}

inline void CopyToClipboard(const char* text) {
    if (!::OpenClipboard(g_hwnd)) return;
    ::EmptyClipboard();
    size_t len = strlen(text);
    HGLOBAL hMem = ::GlobalAlloc(GMEM_MOVEABLE, len + 1);
    if (hMem) {
        char* p = (char*)::GlobalLock(hMem);
        memcpy(p, text, len + 1);
        ::GlobalUnlock(hMem);
        ::SetClipboardData(CF_TEXT, hMem);
    }
    ::CloseClipboard();
}

inline bool ExportHistoryToCSV() {
    // Открываем диалог "Сохранить как"
    wchar_t filename[MAX_PATH] = L"electrocalc_history.csv";

    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_hwnd;
    ofn.lpstrFilter = L"CSV files (*.csv)\0*.csv\0All files (*.*)\0*.*\0";
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = L"csv";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    ofn.lpstrTitle = L"Export history to CSV";

    if (!::GetSaveFileNameW(&ofn))
        return false;   // пользователь отменил

    // Открываем файл (UTF-8 с BOM, чтобы Excel не портил русские буквы)
    FILE* f = nullptr;
    _wfopen_s(&f, filename, L"wb");
    if (!f) return false;

    // BOM UTF-8
    const unsigned char bom[3] = { 0xEF, 0xBB, 0xBF };
    fwrite(bom, 1, 3, f);

    // Заголовок
    fprintf(f, "timestamp,category,summary\n");

    // Строки (в хронологическом порядке — от старых к новым)
    for (auto& e : history::g_entries) {
        char ts[32];
        struct tm lt;
        localtime_s(&lt, &e.timestamp);
        snprintf(ts, sizeof(ts), "%04d-%02d-%02d %02d:%02d:%02d",
            1900 + lt.tm_year, lt.tm_mon + 1, lt.tm_mday,
            lt.tm_hour, lt.tm_min, lt.tm_sec);

        // Экранируем кавычки и запятые в summary по правилам CSV
        auto EscapeCSV = [](const std::string& src) -> std::string {
            bool need_quotes = src.find_first_of(",\"\n") != std::string::npos;
            if (!need_quotes) return src;
            std::string out = "\"";
            for (char c : src) {
                if (c == '"') out += "\"\"";  // удвоение кавычек
                else          out += c;
            }
            out += "\"";
            return out;
            };

        fprintf(f, "%s,%s,%s\n",
            ts,
            EscapeCSV(e.category).c_str(),
            EscapeCSV(e.summary).c_str());
    }

    fclose(f);
    return true;
}

// ======================= MATH & LOGIC =======================
namespace menu {

    // === NEW: температурный коэффициент по типу изоляции и ambient ===
    // 
        // forward declarations
    void RecalcMotor();
    void RecalcCable();
    void RecalcLoad();
    void RecalcBreaker();
    void RecalcGround();
    inline const char* MotorStartName(int t);

    // === motor connection diagram: всё считается от width/height ===
    inline void DrawMotorDiagram(int side, float width, float height) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float W = width;
        const float H = (height > 240.0f) ? 240.0f : height;
        const ImVec2 p_max(p.x + W, p.y + H);

        const ImU32 bg_col = ImGui::ColorConvertFloat4ToU32(ImVec4(0.02f, 0.03f, 0.05f, 1.0f));
        const ImU32 brd_col = ImGui::ColorConvertFloat4ToU32(g_theme.card_border);
        const ImU32 wire_col = ImGui::ColorConvertFloat4ToU32(ImVec4(0.75f, 0.80f, 0.90f, 1.0f));
        const ImU32 node_col = ImGui::ColorConvertFloat4ToU32(g_theme.accent);
        const ImU32 term_col = ImGui::ColorConvertFloat4ToU32(g_theme.res_warn);
        const ImU32 dim_col = ImGui::ColorConvertFloat4ToU32(g_theme.text_dim);

        dl->AddRectFilled(p, p_max, bg_col, 6.0f);
        dl->AddRect(p, p_max, brd_col, 6.0f);
        dl->PushClipRect(p, p_max, true);   // гарантия: ничего не вылезет за canvas

        const float line_w = 2.0f;
        const float node_r = 5.0f;
        const float term_r = 5.0f;
        const float pad = 10.0f;
        const float th = ImGui::GetTextLineHeight();

        // Широкий canvas (карточка внизу) -> подписи справа от схемы.
        // Узкий canvas (карточка в ряд)   -> подписи под схемой.
        const bool wide = (W >= H * 2.2f);
        float fig_w = W;
        if (wide) fig_w = (W * 0.45f < 280.0f) ? W * 0.45f : 280.0f;
        const float cx = p.x + fig_w * 0.5f;

        float span = fig_w * 0.25f;
        if (span > 80.0f) span = 80.0f;
        const float xs[3] = { cx - span, cx, cx + span };

        // Вертикальная раскладка — только от p.y и H
        const float y_label = p.y + 6.0f;
        const float y_term = y_label + th + 8.0f;
        const float fig_top = y_term + 10.0f;

        float fig_bot = p_max.y - pad;
        float cap_x = cx;
        float cap_y1 = 0.0f;
        float cap_y2 = 0.0f;
        if (wide) {
            cap_x = (p.x + fig_w + p_max.x) * 0.5f;
            cap_y1 = p.y + H * 0.5f - th - 2.0f;
            cap_y2 = p.y + H * 0.5f + 2.0f;
            dl->AddLine(ImVec2(p.x + fig_w, p.y + pad),
                ImVec2(p.x + fig_w, p_max.y - pad), brd_col, 1.0f);
        }
        else {
            cap_y2 = p_max.y - pad - th;
            cap_y1 = cap_y2 - th - 4.0f;
            fig_bot = cap_y1 - 10.0f;
        }
        float fig_h = fig_bot - fig_top;
        if (fig_h < 20.0f) fig_h = 20.0f;

        auto CenterText = [&](const char* s, float x, float y, ImU32 col) {
            const ImVec2 sz = ImGui::CalcTextSize(s);
            dl->AddText(ImVec2(x - sz.x * 0.5f, y), col, s);
            };

        // Обмотка: линия + прямоугольник по центру (символ IEC)
        auto Winding = [&](ImVec2 a, ImVec2 b) {
            dl->AddLine(a, b, wire_col, line_w);
            const float dx = b.x - a.x, dy = b.y - a.y;
            const float len = sqrtf(dx * dx + dy * dy);
            if (len < 1.0f) return;
            const float ux = dx / len, uy = dy / len;
            const float nx = -uy, ny = ux;
            float hl = len * 0.18f;
            if (hl > 14.0f) hl = 14.0f;
            const float hw = 4.5f;
            const ImVec2 m((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
            const ImVec2 q1(m.x - ux * hl - nx * hw, m.y - uy * hl - ny * hw);
            const ImVec2 q2(m.x + ux * hl - nx * hw, m.y + uy * hl - ny * hw);
            const ImVec2 q3(m.x + ux * hl + nx * hw, m.y + uy * hl + ny * hw);
            const ImVec2 q4(m.x - ux * hl + nx * hw, m.y - uy * hl + ny * hw);
            dl->AddQuadFilled(q1, q2, q3, q4, bg_col);
            dl->AddQuad(q1, q2, q3, q4, node_col, 1.5f);
            };

        // Терминалы U1/V1/W1 сверху + вводы питания
        const char* labels[3] = { "U1", "V1", "W1" };
        for (int i = 0; i < 3; ++i) {
            CenterText(labels[i], xs[i], y_label, term_col);
            dl->AddLine(ImVec2(xs[i], y_label + th + 1.0f),
                ImVec2(xs[i], y_term), wire_col, line_w);
        }

        const char* cap1 = "";
        const char* cap2 = "";

        if (side == 0) {
            // ==================== STAR ====================
            const ImVec2 c(cx, fig_top + fig_h * 0.62f);
            for (int i = 0; i < 3; ++i)
                Winding(ImVec2(xs[i], y_term), c);

            dl->AddCircleFilled(c, node_r + 1.0f, node_col);
            dl->AddCircle(c, node_r + 6.0f, node_col, 0, 1.5f);

            cap1 = i18n::T("STAR (Y)");
            cap2 = "U2 = V2 = W2";
        }
        else {
            // ==================== DELTA ====================
            float side_len = span * 2.0f;
            const float max_len = fig_h / 0.866f;
            if (side_len > max_len) side_len = max_len;
            const float tri_h = side_len * 0.866f;
            const float y_top = fig_top + (fig_h - tri_h) * 0.5f;
            const float y_bot = y_top + tri_h;

            const ImVec2 v_top(cx, y_top);
            const ImVec2 v_bl(cx - side_len * 0.5f, y_bot);
            const ImVec2 v_br(cx + side_len * 0.5f, y_bot);

            // U1 -> левая нижняя, V1 -> верхняя, W1 -> правая нижняя
            dl->AddLine(ImVec2(xs[0], y_term), ImVec2(xs[0], y_bot), wire_col, line_w);
            dl->AddLine(ImVec2(xs[0], y_bot), v_bl, wire_col, line_w);
            dl->AddLine(ImVec2(xs[1], y_term), v_top, wire_col, line_w);
            dl->AddLine(ImVec2(xs[2], y_term), ImVec2(xs[2], y_bot), wire_col, line_w);
            dl->AddLine(ImVec2(xs[2], y_bot), v_br, wire_col, line_w);

            Winding(v_bl, v_top);
            Winding(v_top, v_br);
            Winding(v_br, v_bl);

            dl->AddCircleFilled(v_top, node_r, node_col);
            dl->AddCircleFilled(v_bl, node_r, node_col);
            dl->AddCircleFilled(v_br, node_r, node_col);

            cap1 = i18n::T("DELTA");
            cap2 = "U1-W2, V1-U2, W1-V2";
        }

        CenterText(cap1, cap_x, cap_y1, node_col);
        CenterText(cap2, cap_x, cap_y2, dim_col);

        // Точки терминалов — поверх проводов
        for (int i = 0; i < 3; ++i)
            dl->AddCircleFilled(ImVec2(xs[i], y_term), term_r, term_col);

        dl->PopClipRect();
        ImGui::Dummy(ImVec2(W, H));
    }

    // === NEW: виды изоляции и допустимая температура жилы для расчёта нагрева ===
    // ПВХ считаем по 65 °C - на эту температуру составлены таблицы ПУЭ.
    struct InsInfo { const char* key; float t_max; };
    static const InsInfo INS_TABLE[] = {
        { "PVC 70C", 65.0f },
        { "XLPE 90C", 90.0f },
        { "Rubber 60C", 60.0f },
        { "PE 70C", 70.0f },
        { "PVC ng-LS 70C", 65.0f },
        { "PVC ng-FRLS 70C", 65.0f },
        { "Heat-resistant PVC 105C", 105.0f },
        { "Halogen-free (HF) 70C", 70.0f },
        { "Fire-resistant halogen-free (FRHF) 70C", 70.0f },
        { "Cross-linked polyolefin (XLPO) 90C", 90.0f },
        { "Cross-linked EVA (XL-EVA) 110C", 110.0f },
        { "Heat-resistant rubber 85C", 85.0f },
        { "Ethylene-propylene rubber (EPR) 90C", 90.0f },
        { "EPDM rubber 90C", 90.0f },
        { "Silicone rubber 180C", 180.0f },
        { "Ceramic-forming rubber 90C", 90.0f },
        { "Nairit (neoprene) rubber 65C", 65.0f },
        { "PTFE (F-4) 250C", 250.0f },
        { "FEP (F-4M) 200C", 200.0f },
        { "ETFE (F-40) 150C", 150.0f },
        { "Impregnated paper 80C", 80.0f },
        { "Paper, non-draining compound 80C", 80.0f },
        { "Oil-filled, low pressure 85C", 85.0f },
        { "Oil-filled, high pressure 85C", 85.0f },
        { "Mica tape 400C", 400.0f },
        { "Fiberglass 180C", 180.0f },
        { "Mineral (MgO) 250C", 250.0f },
    };
    constexpr int INS_N = 27;
    // порядок в выпадающем списке: пластмассы, резины, фторопласты, бумага, особые
    static const int INS_ORDER[INS_N] = { 0, 4, 5, 6, 1, 3, 7, 8, 9, 10, 2, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26 };

    inline int InsClamp(int t) { return (t < 0 || t >= INS_N) ? 0 : t; }
    inline const char* InsulationName(int t) { return INS_TABLE[InsClamp(t)].key; }

    constexpr float CARD_W_HALF = 440.0f;
    constexpr float CARD_W_FULL = 900.0f;
    constexpr double PI = 3.14159265358979323846;

    inline const char* InstallName(int mode) {
        switch (mode) {
        case 0: return "Air";
        case 1: return "Pipe";
        case 2: return "Ground";
        case 3: return "Water";
        default: return "Air";
        }
    }

    // =====================================================================
    // === NEW: выбор сечения по ПУЭ (7-е изд.) ===
    // Допустимые длительные токи, А. 0 = такого сечения в таблице нет.
    // Табл. 1.3.4 (медь) и 1.3.5 (алюминий): провода с резиновой и ПВХ изоляцией,
    // Табл. 1.3.6 (медь) и 1.3.7 (алюминий): кабели в земле.
    // Таблицы даны для жилы +65 °C, воздуха +25 °C, земли +15 °C.
    static const float PUE_S[] = { 1.5f, 2.5f, 4.0f, 6.0f, 10.0f, 16.0f, 25.0f, 35.0f, 50.0f, 70.0f, 95.0f, 120.0f, 150.0f };
    constexpr int PUE_N = 13;
    static const float CU_OPEN[PUE_N]   = { 23, 30, 41, 50, 80, 100, 140, 170, 215, 270, 330, 385, 440 };
    static const float CU_PIPE2[PUE_N]  = { 19, 27, 38, 46, 70,  85, 115, 135, 185, 225, 275, 315, 360 };
    static const float CU_PIPE3[PUE_N]  = { 17, 25, 35, 42, 60,  80, 100, 125, 170, 210, 255, 290, 330 };
    static const float CU_EARTH2[PUE_N] = { 33, 44, 55, 70, 105, 135, 175, 210, 265, 320, 385, 445, 505 };
    static const float CU_EARTH3[PUE_N] = { 27, 38, 49, 60, 90, 115, 150, 180, 225, 275, 330, 385, 435 };
    static const float AL_OPEN[PUE_N]   = { 0, 24, 32, 39, 60, 75, 105, 130, 165, 210, 255, 295, 340 };
    static const float AL_PIPE2[PUE_N]  = { 0, 20, 28, 36, 50, 60,  85, 100, 140, 175, 215, 245, 275 };
    static const float AL_PIPE3[PUE_N]  = { 0, 19, 28, 32, 47, 60,  80,  95, 130, 165, 200, 220, 255 };
    static const float AL_EARTH2[PUE_N] = { 0, 34, 42, 55, 80, 105, 135, 160, 205, 245, 295, 340, 390 };
    static const float AL_EARTH3[PUE_N] = { 0, 29, 38, 46, 70,  90, 115, 140, 175, 210, 255, 295, 335 };

    // Трансформаторы ТП (схема Y/Yн-0): полное сопротивление при однофазном КЗ, Zт/3, Ом
    static const int   TRAFO_KVA[] = { 0, 100, 160, 250, 400, 630, 1000 };
    static const float TRAFO_Z3[]  = { 0.0f, 0.26f, 0.162f, 0.104f, 0.065f, 0.043f, 0.027f };
    constexpr int TRAFO_N = 7;

    constexpr float R_CONTACTS = 0.03f;   // переходные сопротивления контактов и аппаратов, Ом
    constexpr float X0_LINE = 0.00008f;   // индуктивное сопротивление жилы, Ом/м (0,08 Ом/км)

    // Допустимый ток по таблице для индекса сечения (0 = нет в таблице)
    inline float PueTableCurrent(int i) {
        const bool cu = (calc_data::cable_material == 0);
        const bool three = (calc_data::phases == 3);
        switch (calc_data::cable_install) {
        case 0:  return cu ? CU_OPEN[i] : AL_OPEN[i];                                   // открыто (воздух)
        case 1:  return cu ? (three ? CU_PIPE3[i] : CU_PIPE2[i]) : (three ? AL_PIPE3[i] : AL_PIPE2[i]); // в трубе
        case 2:                                                                          // в земле
        case 3:  return cu ? (three ? CU_EARTH3[i] : CU_EARTH2[i]) : (three ? AL_EARTH3[i] : AL_EARTH2[i]); // вода: как в земле (с запасом)
        default: return cu ? CU_OPEN[i] : AL_OPEN[i];
        }
    }

    // Поправка на температуру среды (ПУЭ табл. 1.3.3): k = sqrt((t_жилы - t_среды) / (65 - t_табл))
    inline float PueTempFactor() {
        const float t_max = INS_TABLE[InsClamp(calc_data::insulation_type)].t_max;
        const float t_ref = (calc_data::cable_install >= 2) ? 15.0f : 25.0f;  // земля/вода : воздух
        const float num = t_max - calc_data::ambient_temp;
        if (num <= 0.0f) return 0.0f;
        return sqrtf(num / (65.0f - t_ref));
    }

    // Снижение допустимого тока, когда несколько кабелей лежат вместе
    // (ГОСТ Р 50571.5.52, табл. B.52.17, пучок или один слой вплотную)
    inline float PueGroupFactor() {
        static const float K[9] = { 1.00f, 0.80f, 0.70f, 0.65f, 0.60f, 0.57f, 0.54f, 0.52f, 0.50f };
        int n = calc_data::cable_group;
        if (n < 1) n = 1;
        if (n > 9) n = 9;
        return K[n - 1];
    }

    // Выше 1000 м воздух разрежен и хуже охлаждает: допустимый ток снижают (ГОСТ 15150-69)
    inline float PueAltitudeFactor() {
        const float h = calc_data::cable_altitude;
        if (h <= 1000.0f) return 1.0f;
        if (h <= 2000.0f) return 0.90f;
        if (h <= 3000.0f) return 0.80f;
        return 0.72f;
    }

    // Удельное сопротивление при рабочей температуре жилы (+65 °C), Ом*мм2/м
    inline float RhoHot() {
        const float rho20 = (calc_data::cable_material == 0) ? 0.0175f : 0.028f;
        return rho20 * (1.0f + 0.004f * (65.0f - 20.0f));
    }

    // Номинал автомата: выбранный во вкладке "Автомат" или ближайший стандартный не меньше тока
    inline int PueBreakerIn() {
        if (calc_data::breaker_rating > 0) return calc_data::breaker_rating;
        const int ratings[] = { 6, 10, 16, 20, 25, 32, 40, 50, 63, 80, 100, 125, 160, 200, 250 };
        const float target = calc_data::result_current;   // ПУЭ: In >= Iрасч (запас из вкладки "Автомат" тут не нужен)
        for (int r : ratings) if ((float)r >= target) return r;
        return 250;
    }

    // Падение напряжения, В: dU = k * I * L * (r*cos + x*sin), k = 2 (1 фаза) или sqrt(3) (3 фазы)
    inline float PueVoltageDrop(float S) {
        const float k = (calc_data::phases == 3) ? 1.732f : 2.0f;
        float c = calc_data::cos_phi;
        if (c > 1.0f) c = 1.0f;
        if (c < 0.1f) c = 0.1f;
        const float sn = sqrtf(1.0f - c * c);
        return k * calc_data::result_current * calc_data::cable_length_m * (RhoHot() / S * c + X0_LINE * sn);
    }

    // Сечение нулевой жилы: у кабелей 3+1 оно меньше фазного (3x120+1x70)
    inline float MarkNeutral(float S) {
        if (!calc_data::mark_reduced || calc_data::phases != 3) return S;
        static const float PH[] = { 25.0f, 35.0f, 50.0f, 70.0f, 95.0f, 120.0f, 150.0f };
        static const float NE[] = { 16.0f, 16.0f, 25.0f, 35.0f, 50.0f, 70.0f, 70.0f };
        for (int i = 0; i < 7; ++i)
            if (fabsf(S - PH[i]) < 0.01f) return NE[i];
        return S;   // до 16 мм2 ноль равен фазе
    }

    // Ток однофазного КЗ в конце линии, А: Ik = Uф / (Zт/3 + Zпетли + Rконт)
    inline float PueIk(float S, float* z_loop_out) {
        const float L = calc_data::cable_length_m;
        const float r = L * RhoHot() * (1.0f / S + 1.0f / MarkNeutral(S));   // фаза + ноль
        const float x = 2.0f * L * X0_LINE;
        const float zl = sqrtf(r * r + x * x);
        if (z_loop_out) *z_loop_out = zl;
        int ti = calc_data::trafo_index;
        if (ti < 0 || ti >= TRAFO_N) ti = 0;
        const float z = TRAFO_Z3[ti] + zl + R_CONTACTS;
        const float u_ph = (calc_data::phases == 3) ? calc_data::voltage / 1.732f : calc_data::voltage;
        return (z > 1e-6f) ? u_ph / z : 0.0f;
    }

    inline float BreakerCurveK() {
        switch (calc_data::breaker_curve) {
        case 0:  return 5.0f;    // B: верхняя граница 5 In
        case 2:  return 20.0f;   // D: 20 In
        default: return 10.0f;   // C: 10 In
        }
    }

    void RecalcCable() {
        const float P = calc_data::load_power_kw * 1000.0f;
        const float U = calc_data::voltage;
        const float cosf = calc_data::cos_phi;
        if (calc_data::phases == 1) calc_data::result_current = P / (U * cosf);
        else calc_data::result_current = P / (1.732f * U * cosf);
        const float I = calc_data::result_current;

        calc_data::result_k_install = 1.0f;   // прокладка теперь учтена выбором колонки таблицы ПУЭ
        calc_data::result_install_name = InstallName(calc_data::cable_install);

        const float kt = PueTempFactor();
        calc_data::result_k_temp = kt;
        calc_data::result_k_group = PueGroupFactor();
        calc_data::result_k_alt = PueAltitudeFactor();
        const float kg = calc_data::result_k_group * calc_data::result_k_alt;   // группа и высота вместе

        const int in_rating = PueBreakerIn();
        calc_data::result_breaker_in = in_rating;
        calc_data::result_ik_min = (float)in_rating * BreakerCurveK() * 1.1f;   // запас 1,1 по ПУЭ

        int ti = calc_data::trafo_index;
        if (ti < 0 || ti >= TRAFO_N) ti = 0;
        calc_data::result_z_trafo = TRAFO_Z3[ti];

        // Наименьшее сечение по каждому критерию (индекс в PUE_S, -1 = не хватает таблицы)
        int need[4] = { -1, -1, -1, -1 };   // 0 нагрев, 1 защита, 2 падение, 3 КЗ
        for (int i = 0; i < PUE_N; ++i) {
            const float it = PueTableCurrent(i);
            if (it <= 0.0f) continue;                        // сечения нет (алюминий 1,5)
            const float ia = it * kt * kg;
            const float S = PUE_S[i];
            if (need[0] < 0 && ia >= I) need[0] = i;
            if (need[1] < 0 && ia >= (float)in_rating) need[1] = i;
            if (need[2] < 0 && (U > 0.0f) && PueVoltageDrop(S) / U * 100.0f <= 5.0f) need[2] = i;
            if (need[3] < 0 && (calc_data::cable_length_m <= 0.01f || PueIk(S, nullptr) >= calc_data::result_ik_min)) need[3] = i;
        }
        int idx = 0, crit = 0;
        bool ok = true;
        for (int c = 0; c < 4; ++c) {
            if (need[c] < 0) { ok = false; crit = c; break; }
            if (need[c] > idx) { idx = need[c]; crit = c; }
        }
        // Минимум из таблицы - первое существующее сечение
        if (ok && crit == 0 && need[0] >= 0) idx = (std::max)(idx, need[0]);

        if (ok) {
            calc_data::result_required_section = PUE_S[idx];
            calc_data::result_criterion = crit;
        }
        else {
            idx = PUE_N - 1;
            calc_data::result_required_section = 0.0f;       // 0 = больше 150 мм2
            calc_data::result_criterion = 4;
        }

        // Итоговое сечение для расчётов: ручное или выбранное
        float S = PUE_S[idx];
        int s_idx = idx;
        if (calc_data::use_manual_section && calc_data::manual_section > 0.01f) {
            S = calc_data::manual_section;
            // допустимый ток ручного сечения - по ближайшему меньшему табличному (с запасом)
            s_idx = -1;
            for (int i = 0; i < PUE_N; ++i)
                if (PUE_S[i] <= S + 0.001f && PueTableCurrent(i) > 0.0f) s_idx = i;
        }
        calc_data::result_section = S;
        calc_data::result_i_allow = (s_idx >= 0) ? PueTableCurrent(s_idx) * kt * kg : 0.0f;

        calc_data::result_drop_v = PueVoltageDrop(S);
        calc_data::result_drop_pct = (U > 0.0f) ? calc_data::result_drop_v / U * 100.0f : 0.0f;

        float zl = 0.0f;
        calc_data::result_ik = (calc_data::cable_length_m > 0.01f) ? PueIk(S, &zl) : 0.0f;
        calc_data::result_z_loop = zl;
    }
    void RecalcLoad() {
        // расчётная мощность: не всё включено сразу и не на полную
        float kc = calc_data::load_kc, ko = calc_data::load_ko;
        if (kc <= 0.0f || kc > 1.0f) kc = 1.0f;
        if (ko <= 0.0f || ko > 1.0f) ko = 1.0f;
        calc_data::result_load_kw = calc_data::total_power_kw * kc * ko;
        calc_data::total_current_a = calc_data::result_load_kw * 1000.0f
            / (calc_data::phases == 1 ? calc_data::voltage : 1.732f * calc_data::voltage)
            / calc_data::cos_phi;
        calc_data::total_energy_kwh = calc_data::result_load_kw * calc_data::hours_per_day;
    }
    void RenderMotorTab() {
        using i18n::T;
        using i18n::L;

        const float diag_w = 360.0f;
        float side_w = (ImGui::GetContentRegionAvail().x - diag_w - 30.0f) * 0.5f;
        if (side_w < 340.0f) side_w = 340.0f;

        gui.group_box(T("MOTOR PARAMETERS"), ImVec2(side_w, 930)); {
            ImGui::TextColored(g_theme.text_dim, "%s", T("Mode:"));
            ImGui::RadioButton(L("Forward"), &calc_data::motor_mode, 0); ImGui::SameLine();
            ImGui::RadioButton(L("Reverse"), &calc_data::motor_mode, 1);
            ImGui::Spacing();

            if (calc_data::motor_mode == 0) {
                ImGui::TextColored(g_theme.text_dim, "%s", T("Shaft power, kW:"));
                TextInputFloat("motor_p", &calc_data::motor_power_kw);
            }
            else {
                ImGui::TextColored(g_theme.text_dim, "%s", T("Nominal current, A:"));
                TextInputFloat("motor_flc_in", &calc_data::motor_flc_input);
            }

            ImGui::TextColored(g_theme.text_dim, "%s", T("Voltage, V:"));
            TextInputFloat("motor_u", &calc_data::motor_voltage);

            ImGui::Spacing();
            ImGui::TextColored(g_theme.text_dim, "%s", T("Phases:"));
            ImGui::RadioButton("1", &calc_data::motor_phases, 1); ImGui::SameLine();
            ImGui::RadioButton("3", &calc_data::motor_phases, 3);

            ImGui::Spacing();
            ImGui::TextColored(g_theme.text_dim, "%s", T("cos φ:"));
            TextInputFloat("motor_cos", &calc_data::motor_cos_phi);

            ImGui::TextColored(g_theme.text_dim, "%s", T("Efficiency (0.5-1.0):"));
            TextInputFloat("motor_eff", &calc_data::motor_efficiency);

            ImGui::TextColored(g_theme.text_dim, "%s", T("Target cos φ:"));
            TextInputFloat("motor_cos2", &calc_data::motor_cos_target);

            ImGui::TextColored(g_theme.text_dim, "%s", T("Start time, s:"));
            TextInputFloat("motor_tstart", &calc_data::motor_start_time);

            ImGui::Spacing();
            ImGui::TextColored(g_theme.text_dim, "%s", T("Start method:"));
            // Две строки по сетке: по-русски в одну строку не влезает
            constexpr float X_COL2 = 170.0f;
            ImGui::RadioButton(L("DOL"), &calc_data::motor_start_type, 0);
            ImGui::SameLine(X_COL2);
            ImGui::RadioButton(L("Star-Delta"), &calc_data::motor_start_type, 1);
            ImGui::RadioButton(L("Soft"), &calc_data::motor_start_type, 2);
            ImGui::SameLine(X_COL2);
            ImGui::RadioButton(L("VFD"), &calc_data::motor_start_type, 3);

            ImGui::Spacing();
            ImGui::TextColored(g_theme.text_dim, "%s", T("Start ratio (Ist/In):"));
            TextInputFloat("motor_sr", &calc_data::motor_start_ratio);

            // === NEW: ограничение тока устройства плавного пуска ===
            if (calc_data::motor_start_type == 2) {
                ImGui::TextColored(g_theme.text_dim, "%s", T("Soft starter current limit, x In:"));
                TextInputFloat("motor_soft", &calc_data::motor_soft_limit);
            }

            if (calc_data::use_auto_calc) RecalcMotor();
        } gui.end_group_box();

        ImGui::SameLine(0.0f, 15.0f);

        gui.group_box(T("RESULT"), ImVec2(side_w, 580)); {
            RecalcMotor();

            const ImVec4 col_ok = g_theme.res_good;
            char buf[64];

            snprintf(buf, sizeof(buf), "%.2f kW", calc_data::result_motor_input_kw);
            ResultRow(T("Input power:"), buf, g_theme.res_warn);

            if (calc_data::motor_mode == 0) {
                snprintf(buf, sizeof(buf), "%.2f A", calc_data::result_motor_flc);
                ResultRow(T("Nominal current:"), buf, g_theme.accent);
            }
            else {
                snprintf(buf, sizeof(buf), "%.2f kW", calc_data::result_motor_shaft_kw);
                ResultRow(T("Shaft power:"), buf, g_theme.accent);
            }

            snprintf(buf, sizeof(buf), "%.2f A", calc_data::result_motor_start);
            ResultRow(T("Starting current:"), buf, g_theme.res_bad);

            ResultRow(T("Start method:"), T(MotorStartName(calc_data::motor_start_type)),
                g_theme.res_info);

            ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

            // === NEW: автомат - не должен срабатывать от пускового тока ===
            // In >= 1,25 Iн, и 1,2*Iпуск ниже нижней границы мгновенного срабатывания:
            // C - 5 In, D - 10 In. Сначала пробуем C, потом D, потом номинал больше.
            {
                const float In_m = calc_data::result_motor_flc;
                const float Ist = calc_data::result_motor_start;
                const int ratings[] = { 6,10,16,20,25,32,40,50,63,80,100,125,160,200,250 };
                int best = 0, curve = 1;
                for (int r : ratings) {
                    if ((float)r < In_m * 1.25f) continue;
                    if (1.2f * Ist <= 5.0f * (float)r) { best = r; curve = 1; break; }
                    if (1.2f * Ist <= 10.0f * (float)r) { best = r; curve = 2; break; }
                }
                calc_data::result_motor_breaker = best;
                calc_data::result_motor_curve = curve;
                if (best > 0) snprintf(buf, sizeof(buf), "%c%d", curve == 2 ? 'D' : 'C', best);
                else snprintf(buf, sizeof(buf), "> 250 A");
                ResultRow(T("Recommended breaker:"), buf, col_ok);

                // Контактор: номинальный ток в категории AC-3 не меньше Iн
                const int contactor[] = { 9,12,18,25,32,40,50,65,80,95,115,150,185,225,265,330 };
                int c_best = 0;
                for (int c : contactor) if ((float)c >= In_m) { c_best = c; break; }
                calc_data::result_motor_contactor = c_best;
                if (c_best > 0) snprintf(buf, sizeof(buf), "%d A (AC-3)", c_best);
                else snprintf(buf, sizeof(buf), "> 330 A");
                ResultRow(T("Recommended contactor:"), buf, col_ok);
            }

            snprintf(buf, sizeof(buf), "%.2f - %.2f A",
                calc_data::result_motor_flc * 0.9f,
                calc_data::result_motor_flc * 1.1f);
            ResultRow(T("Thermal relay range:"), buf, col_ok);

            // Класс расцепления по времени пуска (ГОСТ IEC 60947-4-1): реле не должно сработать за время разгона
            {
                const float ts = calc_data::motor_start_time;
                if (ts <= 10.0f) ResultRow(T("Relay trip class:"), "Class 10", col_ok);
                else if (ts <= 20.0f) ResultRow(T("Relay trip class:"), "Class 20", g_theme.res_warn);
                else if (ts <= 30.0f) ResultRow(T("Relay trip class:"), "Class 30", g_theme.res_warn);
                else ResultRow(T("Relay trip class:"), T("special protection needed"), g_theme.res_bad);
            }

            // Компенсация реактивной мощности: Q = P * (tg(phi1) - tg(phi2))
            ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
            {
                float c1 = calc_data::motor_cos_phi, c2 = calc_data::motor_cos_target;
                if (c1 < 0.1f) c1 = 0.1f;
                if (c1 > 1.0f) c1 = 1.0f;
                if (c2 < 0.1f) c2 = 0.1f;
                if (c2 > 1.0f) c2 = 1.0f;
                const float q_kvar = calc_data::result_motor_input_kw *
                    (sqrtf(1.0f - c1 * c1) / c1 - sqrtf(1.0f - c2 * c2) / c2);
                const float U = calc_data::motor_voltage;
                if (q_kvar > 0.001f && U > 1.0f) {
                    snprintf(buf, sizeof(buf), "%.2f kvar", q_kvar);
                    ResultRow(T("Capacitor power:"), buf, g_theme.accent);
                    const float w = 2.0f * 3.14159265f * 50.0f;
                    if (calc_data::motor_phases == 3) {
                        snprintf(buf, sizeof(buf), "%.1f uF", q_kvar * 1000.0f / (3.0f * w * U * U) * 1e6f);
                        ResultRow(T("Capacitance per phase (delta):"), buf, g_theme.text_main);
                    }
                    else {
                        snprintf(buf, sizeof(buf), "%.1f uF", q_kvar * 1000.0f / (w * U * U) * 1e6f);
                        ResultRow(T("Capacitance:"), buf, g_theme.text_main);
                    }
                    // ближайшая стандартная ступень не больше расчётной, чтобы не перекомпенсировать
                    static const float STD_Q[] = { 0.5f, 1.0f, 1.5f, 2.0f, 2.5f, 3.0f, 4.0f, 5.0f, 7.5f, 10.0f, 12.5f, 15.0f, 20.0f, 25.0f, 30.0f, 40.0f, 50.0f };
                    float q_std = 0.0f;
                    for (float v : STD_Q) if (v <= q_kvar + 0.001f) q_std = v;
                    if (q_std > 0.0f) {
                        snprintf(buf, sizeof(buf), "%g kvar", (double)q_std);
                        ResultRow(T("Standard capacitor unit:"), buf, col_ok);
                    }
                }
                else {
                    ResultRow(T("Capacitor power:"), T("not needed"), g_theme.text_dim);
                }
            }

            ImGui::Spacing();
            if (OutlineButton(L("Save to History"), ImVec2(-1, 36), btn_col::success)) {
                RecalcMotor();
                char hbuf[160];
                if (calc_data::motor_mode == 0) {
                    snprintf(hbuf, sizeof(hbuf),
                        "Motor %.1f kW: In=%.2f A, Ist=%.2f A (%s)",
                        calc_data::motor_power_kw,
                        calc_data::result_motor_flc,
                        calc_data::result_motor_start,
                        MotorStartName(calc_data::motor_start_type));
                }
                else {
                    snprintf(hbuf, sizeof(hbuf),
                        "Motor In=%.2f A: P=%.1f kW, Ist=%.2f A (%s)",
                        calc_data::motor_flc_input,
                        calc_data::result_motor_shaft_kw,
                        calc_data::result_motor_start,
                        MotorStartName(calc_data::motor_start_type));
                }
                history::Add("Motor", hbuf);
            }
        } gui.end_group_box();

        // ==================== CONNECTION DIAGRAM ====================
        ImGui::SameLine(0.0f, 15.0f);

        gui.group_box(T("CONNECTION DIAGRAM"), ImVec2(diag_w, 580)); {
            static int diagram_side = 0; // 0 = STAR, 1 = DELTA
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(g_theme.text_dim, "%s", T("Show:")); ImGui::SameLine();
            ImGui::RadioButton(L("Star"), &diagram_side, 0); ImGui::SameLine();
            ImGui::RadioButton(L("Delta"), &diagram_side, 1);
            ImGui::Spacing();

            DrawMotorDiagram(diagram_side, ImGui::GetContentRegionAvail().x, 240.0f);
        } gui.end_group_box();
    }
    void RenderReferenceTab() {
        using i18n::T;

        static bool s_colors = true, s_ip = true, s_cat = true, s_motor = true,
            s_sections = true, s_awg = true, s_symbols = true, s_mark = true,
            s_metal = true, s_pipe = true, s_alt = true;

        constexpr float COL_W = 380.0f;
        constexpr float COL_H = 1700.0f;
        constexpr float GAP = 12.0f;
        const ImVec4 note_col = g_theme.res_warn;

        // ==================== КОЛОНКА 1: WIRE COLORS ====================
        gui.group_box(T("WIRE COLORS"), ImVec2(COL_W, COL_H)); {
            if (SectionHeader(T("Wire color codes"), &s_colors)) {
                auto Row = [](const char* role, const char* color, ImVec4 col) {
                    ImGui::TextColored(g_theme.text_dim, "%s", T(role));
                    ImGui::SameLine(150.0f);
                    ImGui::TextColored(col, "%s", T(color));
                    };

                ImGui::TextColored(g_theme.accent, "%s", T("IEC 60446 (Europe)"));
                ImGui::Spacing();
                Row("L1/L2/L3", "Brown / Black / Grey", ImVec4(0.8f, 0.5f, 0.3f, 1.0f));
                Row("Neutral (N)", "Blue", ImVec4(0.4f, 0.7f, 1.0f, 1.0f));
                Row("Protective", "Yellow-Green", ImVec4(0.6f, 1.0f, 0.4f, 1.0f));

                ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

                ImGui::TextColored(g_theme.accent, "%s", T("USSR / old PUE (busbars)"));
                ImGui::Spacing();
                Row("Phase A", "Yellow", ImVec4(1.0f, 0.9f, 0.3f, 1.0f));
                Row("Phase B", "Green", ImVec4(0.4f, 1.0f, 0.4f, 1.0f));
                Row("Phase C", "Red", ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
                Row("Neutral (N)", "Light blue", ImVec4(0.5f, 0.8f, 1.0f, 1.0f));
                Row("Positive (+)", "Red", ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
                Row("Negative (-)", "Blue", ImVec4(0.4f, 0.6f, 1.0f, 1.0f));

                ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

                ImGui::TextColored(g_theme.accent, "%s", T("Old UK"));
                ImGui::Spacing();
                Row("Phase (L)", "Red", ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
                Row("Neutral (N)", "Black", ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
                Row("Protective", "Green", ImVec4(0.4f, 1.0f, 0.4f, 1.0f));

                ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

                ImGui::TextColored(g_theme.accent, "%s", T("DC / US (NEC)"));
                ImGui::Spacing();
                Row("Positive (+)", "Red", ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
                Row("Negative (-)", "Black", ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
                Row("Ground", "Green / bare", ImVec4(0.4f, 1.0f, 0.4f, 1.0f));
            }

            // === NEW: расшифровка буквенной маркировки кабелей и проводов ===
            if (SectionHeader(T("Cable and wire marking"), &s_mark)) {
                auto Head = [](const char* key) {
                    ImGui::Spacing();
                    ImGui::TextColored(g_theme.accent, "%s", T(key));
                    };
                auto MRow = [](const char* letters, const char* key) {
                    ImGui::TextColored(g_theme.text_main, "%s", letters);
                    ImGui::SameLine(78.0f);
                    ImGui::PushTextWrapPos(0.0f);
                    ImGui::TextColored(g_theme.text_dim, "%s", T(key));
                    ImGui::PopTextWrapPos();
                    };

                ImGui::TextColored(g_theme.text_main, "%s", "А МГ К Б  3 x 2.5 - 0.66");
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextColored(g_theme.text_dim, "%s", T("Letters: metal, type, insulation, design. Digits: cores x section - voltage."));
                ImGui::PopTextWrapPos();

                Head("1st letter - core metal");
                MRow("А", "aluminium core");
                MRow("-", "no letter: copper core");

                Head("2nd letter - wire type");
                MRow("К", "control wire");
                MRow("М", "mounting wire");
                MRow("МГ", "mounting, flexible cores");
                MRow("П", "flat wire");
                MRow("П(У), Ш", "installation wire");

                Head("3rd letter - insulation");
                MRow("В, ВР", "PVC insulation");
                MRow("Г", "with a flexible core");
                MRow("К", "kapron (nylon)");
                MRow("Л", "lacquered");
                MRow("МЭ", "enamelled");
                MRow("Н, НР", "nairit, non-flammable rubber");
                MRow("О", "polyamide silk");
                MRow("П", "polyethylene");
                MRow("С", "fiberglass");
                MRow("Т", "with a carrier cable");
                MRow("Ф", "seamed (folded) sheath");
                MRow("Э", "screened");
                ImGui::Spacing();
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextColored(note_col, "%s", T("Rubber-insulated wires also have a sheath: Н - nairit, П - PVC. These letters go after the insulation letter."));
                ImGui::PopTextWrapPos();

                Head("4th letter - design features");
                MRow("А", "asphalt-coated");
                MRow("Б", "armoured with steel tapes");
                MRow("Г", "bare, no protective cover");
                MRow("К", "armoured with round wire");
                MRow("О", "in a protective braid");
                MRow("Т", "for laying inside pipes");

                Head("Digits after the letters");
                MRow("1", "number of cores; if absent - single core");
                MRow("2", "core cross-section, mm2");
                MRow("3", "rated voltage of the conductor");
                ImGui::Spacing();
            }
        } gui.end_group_box();

        ImGui::SameLine(0.0f, GAP);

        // ==================== КОЛОНКА 2: IP + CATEGORIES ====================
        gui.group_box(T("IP & CATEGORIES"), ImVec2(COL_W, COL_H)); {
            if (SectionHeader(T("IP rating"), &s_ip)) {
                ImGui::TextColored(g_theme.accent, "%s", T("1st digit - solids"));
                ImGui::TextColored(g_theme.text_dim, "%s", T("0 none  1 >50mm  2 >12.5mm"));
                ImGui::TextColored(g_theme.text_dim, "%s", T("3 >2.5mm  4 >1mm"));
                ImGui::TextColored(g_theme.text_dim, "%s", T("5 dust protected  6 dust tight"));

                ImGui::Spacing();
                ImGui::TextColored(g_theme.accent, "%s", T("2nd digit - water"));
                ImGui::TextColored(g_theme.text_dim, "%s", T("0 none  1 drip  2 drip 15deg"));
                ImGui::TextColored(g_theme.text_dim, "%s", T("3 spray  4 splash  5 jets"));
                ImGui::TextColored(g_theme.text_dim, "%s", T("6 power jets  7 immersion 1m"));
                ImGui::TextColored(g_theme.text_dim, "%s", T("8 continuous immersion"));

                ImGui::Spacing();
                ImGui::TextColored(g_theme.accent, "%s", T("Common"));
                ImGui::TextColored(g_theme.text_dim, "%s", T("IP20 indoor   IP44 bath"));
                ImGui::TextColored(g_theme.text_dim, "%s", T("IP54 industrial   IP65 panel"));
                ImGui::TextColored(g_theme.text_dim, "%s", T("IP67 submerged   IP68 underwater"));
            }

            if (SectionHeader(T("Overvoltage cat."), &s_cat)) {
                ImGui::TextColored(g_theme.accent, "CAT I");
                ImGui::TextColored(g_theme.text_dim, "  %s", T("Electronics, 1500 V"));
                ImGui::Spacing();
                ImGui::TextColored(g_theme.accent, "CAT II");
                ImGui::TextColored(g_theme.text_dim, "  %s", T("Appliances, 2500 V"));
                ImGui::Spacing();
                ImGui::TextColored(g_theme.accent, "CAT III");
                ImGui::TextColored(g_theme.text_dim, "  %s", T("Fixed install, 4000 V"));
                ImGui::Spacing();
                ImGui::TextColored(g_theme.accent, "CAT IV");
                ImGui::TextColored(g_theme.text_dim, "  %s", T("Origin, 6000 V"));
            }

            if (SectionHeader(T("ANSI / IEC symbols"), &s_symbols)) {
                constexpr float X_IEC = 58.0f;
                constexpr float X_DESC = 100.0f;

                ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(12.0f, 4.0f));

                ImGui::TextColored(g_theme.accent, "ANSI");
                ImGui::SameLine(X_IEC);
                ImGui::TextColored(g_theme.accent, "IEC");
                ImGui::SameLine(X_DESC);
                ImGui::TextColored(g_theme.accent, "%s", T("Description"));
                ImGui::Separator();

                auto SymRow = [](const char* ansi, const char* iec, const char* desc) {
                    ImGui::TextColored(g_theme.text_main, "%s", ansi);
                    ImGui::SameLine(58.0f);
                    ImGui::TextColored(g_theme.text_main, "%s", iec);
                    ImGui::SameLine(100.0f);
                    ImGui::PushTextWrapPos(0.0f);
                    ImGui::TextColored(g_theme.text_dim, "%s", T(desc));
                    ImGui::PopTextWrapPos();
                    };

                SymRow("QS", "QF", "Circuit breaker");
                SymRow("FU", "FU", "Fuse");
                SymRow("KM", "KM", "Contactor");
                SymRow("KA", "KA", "Relay aux");
                SymRow("KK", "KA", "Thermal overload");
                SymRow("M", "M", "Motor");
                SymRow("QF", "QM", "MCCB / motor breaker");
                SymRow("PE", "PE", "Protective earth");

                ImGui::PopStyleVar();

                ImGui::Spacing();
                ImGui::TextColored(note_col, "%s", T("ANSI - US (NEC), IEC - Europe."));
            }
            // === NEW: какие металлы можно соединять напрямую ===
            if (SectionHeader(T("Metal compatibility"), &s_metal)) {
                // 0 = нельзя, 1 = только в сухих помещениях, 2 = можно (ГОСТ 9.005-72)
                auto Pair = [](const char* pair, int level) {
                    ImGui::TextColored(g_theme.text_dim, "%s", T(pair));
                    const char* text = T(level == 2 ? "yes" : (level == 1 ? "dry rooms only" : "no"));
                    const ImVec4 col = (level == 2) ? g_theme.res_good : (level == 1 ? g_theme.res_warn : g_theme.res_bad);
                    ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - ImGui::CalcTextSize(text).x - 10.0f);
                    ImGui::TextColored(col, "%s", text);
                    };
                Pair("Copper + tin, brass, bronze", 2);
                Pair("Copper + stainless steel", 2);
                Pair("Copper + nickel, chrome", 2);
                Pair("Copper + carbon steel, lead", 1);
                Pair("Copper + aluminium", 0);
                Pair("Copper + galvanized steel", 0);
                ImGui::Spacing();
                Pair("Aluminium + zinc, cadmium-plated steel", 2);
                Pair("Aluminium + galvanized steel", 2);
                Pair("Aluminium + stainless steel", 1);
                Pair("Aluminium + brass, bronze", 0);
                ImGui::Spacing();
                Pair("Galvanized steel + carbon steel", 2);
                Pair("Galvanized steel + stainless steel", 1);
                ImGui::Spacing();
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextColored(note_col, "%s",
                    T("Per GOST 9.005-72. Copper to aluminium - only through Al-Cu lugs, transition plates or tinned terminals."));
                ImGui::PopTextWrapPos();
                ImGui::Spacing();
            }
        } gui.end_group_box();

        ImGui::SameLine(0.0f, GAP);

        // ==================== КОЛОНКА 3: MOTOR + AMPACITY ====================
        gui.group_box(T("MOTOR & AMPACITY"), ImVec2(COL_W, COL_H)); {
            if (SectionHeader(T("Motor: Star vs Delta"), &s_motor)) {
                ImGui::TextColored(g_theme.accent, "%s", T("STAR (Y)"));
                ImGui::TextColored(g_theme.text_dim, "U_ph = U_line / sqrt(3)");
                ImGui::TextColored(g_theme.text_dim, "%s", T("Start I/T ~ 3x less"));
                ImGui::TextColored(g_theme.text_dim, "%s", T("U2=V2=W2 tied"));
                ImGui::Spacing();
                ImGui::TextColored(g_theme.accent, "%s", T("DELTA"));
                ImGui::TextColored(g_theme.text_dim, "U_ph = U_line");
                ImGui::TextColored(g_theme.text_dim, "%s", T("Full torque/current"));
                ImGui::TextColored(g_theme.text_dim, "U1-W2, V1-U2, W1-V2");
                ImGui::Spacing();
                ImGui::TextColored(note_col, "%s", T("Star-Delta needs 6 terminals."));
                ImGui::Spacing();
            }

            if (SectionHeader(T("Ampacity copper (PVC)"), &s_sections)) {
                constexpr float X_1PH = 90.0f;
                constexpr float X_3PH = 160.0f;

                ImGui::TextColored(g_theme.text_dim, "%s", T("mm2"));
                ImGui::SameLine(X_1PH);
                ImGui::TextColored(g_theme.text_dim, "%s", T("1ph"));
                ImGui::SameLine(X_3PH);
                ImGui::TextColored(g_theme.text_dim, "%s", T("3ph"));

                auto Row2 = [](const char* s, const char* a1, const char* a3) {
                    ImGui::TextColored(g_theme.text_main, "%s", s);
                    ImGui::SameLine(90.0f);
                    ImGui::TextColored(g_theme.text_main, "%s", a1);
                    ImGui::SameLine(160.0f);
                    ImGui::TextColored(g_theme.text_main, "%s", a3);
                    };
                Row2("1.5", "16", "14");
                Row2("2.5", "20", "18");
                Row2("4", "27", "24");
                Row2("6", "34", "31");
                Row2("10", "46", "42");
                Row2("16", "62", "56");
                Row2("25", "80", "73");
                Row2("35", "99", "89");
                Row2("50", "118", "108");
                Row2("70", "149", "136");
                ImGui::Spacing();
                ImGui::TextColored(note_col, "%s", T("Approx. Verify vs local code."));
            }

            if (SectionHeader(T("AWG <-> mm^2"), &s_awg)) {
                constexpr float X_MM = 90.0f;

                ImGui::TextColored(g_theme.text_dim, "AWG");
                ImGui::SameLine(X_MM);
                ImGui::TextColored(g_theme.text_dim, "%s", T("mm2"));

                auto Row3 = [](const char* a, const char* m) {
                    ImGui::TextColored(g_theme.text_main, "%s", a);
                    ImGui::SameLine(90.0f);
                    ImGui::TextColored(g_theme.text_main, "%s", m);
                    };
                Row3("0000", "107.2");
                Row3("000", "85.0");
                Row3("00", "67.4");
                Row3("0", "53.5");
                Row3("1", "42.4");
                Row3("2", "33.6");
                Row3("4", "21.2");
                Row3("6", "13.3");
                Row3("8", "8.37");
                Row3("10", "5.26");
                Row3("12", "3.31");
                Row3("14", "2.08");
                Row3("16", "1.31");
                Row3("18", "0.823");
                Row3("20", "0.518");
                Row3("22", "0.326");
                Row3("24", "0.205");
                Row3("26", "0.129");
                ImGui::Spacing();
                ImGui::TextColored(note_col, "%s", T("AWG is a logarithmic scale."));
            }
            // === NEW: заполнение труб и коробов + мини-расчёт диаметра ===
            if (SectionHeader(T("Conduit fill"), &s_pipe)) {
                static double pf_d = 10.0, pf_n = 3.0;
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextColored(g_theme.text_dim, "%s", T("1 cable - up to 40%, 2 cables - 25%, 3 and more - 35% of the cross-section (by outer cable diameter)."));
                ImGui::TextColored(g_theme.text_dim, "%s", T("Closed trunking - 35%, with a removable cover - 40% (PUE 2.1.61)."));
                ImGui::PopTextWrapPos();
                ImGui::Spacing();
                ImGui::TextColored(g_theme.text_dim, "%s", T("Cable diameter, mm:"));
                TextInputDouble("pf_d", &pf_d);
                ImGui::TextColored(g_theme.text_dim, "%s", T("Number of cables:"));
                TextInputDouble("pf_n", &pf_n);
                const double n_c = (pf_n < 1.0) ? 1.0 : floor(pf_n);
                const double fill = (n_c < 1.5) ? 0.40 : (n_c < 2.5 ? 0.25 : 0.35);
                const double d_min = (pf_d > 0.0) ? pf_d * sqrt(n_c / fill) : 0.0;   // n*d^2 <= fill*D^2
                char pbuf[64];
                snprintf(pbuf, sizeof(pbuf), "%.1f mm", d_min);
                ResultRow(T("Min. inner diameter:"), pbuf, g_theme.accent);

                // проверка конкретной трубы: площадь кабелей / площадь трубы
                static double pf_pipe = 25.0;
                ImGui::TextColored(g_theme.text_dim, "%s", T("Conduit inner diameter, mm:"));
                TextInputDouble("pf_pipe", &pf_pipe);
                if (pf_pipe > 0.0 && pf_d > 0.0) {
                    const double used = n_c * pf_d * pf_d / (pf_pipe * pf_pipe);
                    const bool fits = (used <= fill + 1e-9);
                    snprintf(pbuf, sizeof(pbuf), "%.0f %% (%s %.0f %%)", used * 100.0, T("limit"), fill * 100.0);
                    ResultRow(T("Fill:"), pbuf, fits ? g_theme.res_good : g_theme.res_bad);
                    if (!fits) {
                        ImGui::PushTextWrapPos(0.0f);
                        ImGui::TextColored(g_theme.res_bad, "%s", T("Cables will jam when pulled. Take a larger conduit."));
                        ImGui::PopTextWrapPos();
                    }
                }
                ImGui::Spacing();
            }

            // === NEW: поправка на высоту над уровнем моря ===
            if (SectionHeader(T("Altitude correction"), &s_alt)) {
                auto ARow = [](const char* alt, const char* cur, ImVec4 col) {
                    ImGui::TextColored(col, "%s", alt);
                    ImGui::SameLine(160.0f);
                    ImGui::TextColored(col, "%s", cur);
                    };
                ARow(T("Altitude"), T("Allowable current"), g_theme.text_dim);
                ARow(T("up to 1000 m"), "100 %", g_theme.text_main);
                ARow("1001 - 2000 m", "90 %", g_theme.text_main);
                ARow("2001 - 3000 m", "80 %", g_theme.text_main);
                ARow("3001 - 4000 m", "72 %", g_theme.text_main);
                ImGui::Spacing();
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextColored(note_col, "%s", T("Above 1000 m the air is thinner and cools worse, so the allowable current is reduced (GOST 15150-69)."));
                ImGui::PopTextWrapPos();
            }
        } gui.end_group_box();
    }
    // Автоматы калибруют при +30 °C: в жарком щите тепловой расцепитель срабатывает раньше
    inline float BreakerTempK() {
        float k = 1.0f - 0.005f * (calc_data::breaker_temp - 30.0f);
        if (k < 0.7f) k = 0.7f;
        if (k > 1.1f) k = 1.1f;
        return k;
    }

    void RecalcBreaker() {
        float I = calc_data::total_current_a > 0 ? calc_data::total_current_a : calc_data::result_current;
        if (I <= 0.0f) { RecalcCable(); I = calc_data::result_current; }
        if (I <= 0.0f) { RecalcLoad(); I = calc_data::total_current_a; }
        if (I <= 0.0f) { calc_data::load_power_kw = 3.5f; RecalcCable(); I = calc_data::result_current; }

        float target = I * calc_data::breaker_margin;
        const int ratings[] = { 6,10,16,20,25,32,40,50,63,80,100,125 };
        calc_data::breaker_rating = 0;
        for (int r : ratings) {
            if ((float)r * BreakerTempK() >= target) { calc_data::breaker_rating = r; break; }
        }
        if (calc_data::breaker_rating == 0) calc_data::breaker_rating = 125;
    }
    inline const char* BreakerCurveName(int c) {
        switch (c) {
        case 0: return "B (3-5xIn)";
        case 1: return "C (5-10xIn)";
        case 2: return "D (10-20xIn)";
        default: return "C (5-10xIn)";
        }
    }
    inline const char* BreakerCurveRecommend(int c) {
        switch (c) {
        case 0: return "Lighting, resistive loads";
        case 1: return "Sockets, mixed household";
        case 2: return "Motors, transformers, high inrush";
        default: return "";
        }
    }
    // === NEW: motor calculations ===
    inline const char* MotorStartName(int t) {
        switch (t) {
        case 0: return "DOL (direct on line)";
        case 1: return "Star-Delta";
        case 2: return "Soft starter";
        case 3: return "VFD (frequency drive)";
        default: return "DOL";
        }
    }

    // Реальный пусковой ток с учётом способа пуска
    inline float MotorStartMultiplier(int type, float dol_ratio) {
        switch (type) {
        case 0: return dol_ratio;         // напрямую — полный пусковой
        case 1: return dol_ratio / 3.0f;  // звезда-треугольник ~ в 3 раза меньше
        case 2: return (std::min)(dol_ratio, calc_data::motor_soft_limit);  // УПП: ток ограничен настройкой (обычно 2-4 Iн)
        case 3: return (std::min)(dol_ratio, 1.5f);                         // ЧП: пуск током до ~1,5 Iн
        default: return dol_ratio;
        }
    }

    void RecalcMotor() {
        float U = calc_data::motor_voltage;
        float cosf = calc_data::motor_cos_phi;
        float eff = calc_data::motor_efficiency;
        if (eff < 0.01f) eff = 0.01f;

        if (calc_data::motor_mode == 0) {
            // ==================== FORWARD ====================
            // Ввод: мощность на валу -> результат: ток
            float P_out = calc_data::motor_power_kw;
            float P_in = P_out / eff;
            calc_data::result_motor_input_kw = P_in;

            if (calc_data::motor_phases == 1)
                calc_data::result_motor_flc = (P_in * 1000.0f) / (U * cosf);
            else
                calc_data::result_motor_flc = (P_in * 1000.0f) / (1.732f * U * cosf);

            calc_data::result_motor_shaft_kw = P_out;
        }
        else {
            // ==================== REVERSE ====================
            // Ввод: ток с шильдика -> результат: мощность на валу
            float I = calc_data::motor_flc_input;
            calc_data::result_motor_flc = I;

            float P_in = 0.0f;
            if (calc_data::motor_phases == 1)
                P_in = U * I * cosf / 1000.0f;                // кВт
            else
                P_in = 1.732f * U * I * cosf / 1000.0f;       // кВт

            calc_data::result_motor_input_kw = P_in;
            calc_data::result_motor_shaft_kw = P_in * eff;
        }

        // Пусковой ток считается всегда — от result_motor_flc
        float k = MotorStartMultiplier(calc_data::motor_start_type,
            calc_data::motor_start_ratio);
        calc_data::result_motor_start = calc_data::result_motor_flc * k;
    }
    // === NEW: заземление по методике для вертикальных электродов ===
    // Коэффициент использования вертикальных электродов, расположенных в ряд
    // (строки: a/L = 1, 2, 3; столбцы: n = 1, 2, 3, 5, 10, 15, 20). Табличные значения, ориентировочно.
    inline float GroundEta(int n, float a_over_l) {
        static const float N_PTS[7] = { 1, 2, 3, 5, 10, 15, 20 };
        static const float ETA[3][7] = {
            { 1.0f, 0.85f, 0.78f, 0.70f, 0.59f, 0.54f, 0.49f },   // a/L = 1
            { 1.0f, 0.91f, 0.86f, 0.81f, 0.74f, 0.70f, 0.68f },   // a/L = 2
            { 1.0f, 0.94f, 0.91f, 0.87f, 0.81f, 0.78f, 0.77f },   // a/L = 3
        };
        if (n <= 1) return 1.0f;
        float fn = (float)n;
        if (fn > 20.0f) fn = 20.0f;
        auto row = [&](int r) {
            for (int j = 0; j < 6; ++j) {
                if (fn <= N_PTS[j + 1]) {
                    const float t = (fn - N_PTS[j]) / (N_PTS[j + 1] - N_PTS[j]);
                    return ETA[r][j] + (ETA[r][j + 1] - ETA[r][j]) * t;
                }
            }
            return ETA[r][6];
            };
        float q = a_over_l;
        if (q < 1.0f) q = 1.0f;
        if (q > 3.0f) q = 3.0f;
        const int r0 = (q < 2.0f) ? 0 : 1;
        const float t = q - (float)(r0 + 1);
        return row(r0) + (row(r0 + 1) - row(r0)) * t;
    }

    inline float GroundNorm() {
        switch (calc_data::ground_norm) {
        case 1:  return 10.0f;
        case 2:  return 30.0f;
        default: return 4.0f;
        }
    }

    // Сезонный коэффициент для вертикальных электродов: зимой грунт промерзает, летом сохнет
    inline float GroundSeasonK() {
        // полоса лежит неглубоко и сильнее зависит от промерзания, чем вертикальный электрод
        const bool strip = (calc_data::ground_type == 1);
        switch (calc_data::ground_season) {
        // берём верхнюю границу диапазона для зоны - расчёт на худшее время года
        case 1:  return strip ? 6.0f : 1.9f;    // зона I
        case 2:  return strip ? 3.5f : 1.5f;    // зона II
        case 3:  return strip ? 2.0f : 1.3f;    // зона III
        case 4:  return strip ? 1.4f : 1.15f;   // зона IV
        default: return 1.0f;
        }
    }

    void RecalcGround() {
        const float rho = calc_data::soil_resistivity * GroundSeasonK();

        // Горизонтальная полоса 40x4: R = rho / (2*pi*L) * ln(2*L^2 / (b*t))
        if (calc_data::ground_type == 1) {
            const float b = 0.04f;
            const float depth = (calc_data::ground_depth > 0.1f) ? calc_data::ground_depth : 0.1f;
            auto strip_r = [&](float len) {
                return rho / (2.0f * 3.14159265f * len) * logf(2.0f * len * len / (b * depth));
                };
            const float len = (calc_data::ground_strip_len > 1.0f) ? calc_data::ground_strip_len : 1.0f;
            calc_data::result_ground = strip_r(len);
            calc_data::result_ground_single = calc_data::result_ground;
            calc_data::result_ground_eta = 1.0f;
            calc_data::result_ground_need = 0;
            calc_data::result_ground_len_need = 0.0f;
            const float norm_h = GroundNorm();
            for (int k = 1; k <= 1000; ++k) {
                if (strip_r((float)k) <= norm_h) { calc_data::result_ground_len_need = (float)k; break; }
            }
            return;
        }
        const float L = (calc_data::ground_rod_len > 0.1f) ? calc_data::ground_rod_len : 0.1f;
        const float d = (calc_data::ground_electrode == 1) ? 0.0475f : 0.016f;  // уголок 50x50: d = 0,95*b
        const float t = (calc_data::ground_depth > 0.0f) ? calc_data::ground_depth : 0.0f;
        const float T = t + L * 0.5f;   // глубина середины электрода

        // Одиночный вертикальный электрод, заглублённый в землю:
        // R1 = rho / (2*pi*L) * ( ln(2L/d) + 0.5 * ln((4T + L) / (4T - L)) )
        const float R1 = rho / (2.0f * 3.14159265f * L) *
            (logf(2.0f * L / d) + 0.5f * logf((4.0f * T + L) / (4.0f * T - L)));
        calc_data::result_ground_single = R1;

        int n = calc_data::ground_rods;
        if (n < 1) n = 1;
        const float a_l = calc_data::ground_spacing / L;
        const float eta = GroundEta(n, a_l);
        calc_data::result_ground_eta = eta;
        calc_data::result_ground = R1 / ((float)n * eta);

        // Сколько электродов нужно для выбранной нормы
        const float norm = GroundNorm();
        calc_data::result_ground_need = 0;
        for (int k = 1; k <= 100; ++k) {
            if (R1 / ((float)k * GroundEta(k, a_l)) <= norm) { calc_data::result_ground_need = k; break; }
        }
    }

    inline bool DateIsLeap(int y) { return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0); }
    inline int DateDaysInMonth(int y, int m) {
        static const int d[] = { 31,28,31,30,31,30,31,31,30,31,30,31 };
        if (m == 2 && DateIsLeap(y)) return 29;
        if (m >= 1 && m <= 12) return d[m - 1];
        return 30;
    }
    inline long long DateDaysSinceEpoch(int y, int m, int d) {
        long long days = 0;
        for (int yy = 1970; yy < y; ++yy) days += DateIsLeap(yy) ? 366 : 365;
        for (int mm = 1; mm < m; ++mm) days += DateDaysInMonth(y, mm);
        days += (d - 1);
        return days;
    }
    inline void DateAddDays(int y, int m, int d, int add, int& oy, int& om, int& od) {
        long long rem = DateDaysSinceEpoch(y, m, d) + add;
        int yy = 1970;
        while (true) {
            int yd = DateIsLeap(yy) ? 366 : 365;
            if (rem < yd) break;
            rem -= yd; ++yy;
        }
        int mm = 1;
        while (true) {
            int md = DateDaysInMonth(yy, mm);
            if (rem < md) break;
            rem -= md; ++mm;
        }
        oy = yy; om = mm; od = (int)rem + 1;
    }
    inline const char* DateWeekdayName(int y, int m, int d) {
        long long days = DateDaysSinceEpoch(y, m, d);
        static const char* names[] = { "Thursday","Friday","Saturday","Sunday","Monday","Tuesday","Wednesday" };
        return names[days % 7];
    }

    // =====================================================================
    // === NEW: марка кабеля по буквенной схеме (металл, тип, изоляция, конструкция) ===
    struct MarkLetter { const char* letter; const char* key; };
    static const MarkLetter MK_TYPE[] = {
        { "", "- (no letter)" }, { "К", "control wire" }, { "М", "mounting wire" },
        { "МГ", "mounting, flexible cores" }, { "П", "flat wire" },
        { "ПУ", "installation wire" }, { "Ш", "installation wire" } };
    static const MarkLetter MK_INS[] = {
        { "В", "PVC insulation" }, { "ВР", "PVC insulation" }, { "Г", "with a flexible core" },
        { "К", "kapron (nylon)" }, { "Л", "lacquered" }, { "МЭ", "enamelled" },
        { "Н", "nairit, non-flammable rubber" }, { "НР", "nairit, non-flammable rubber" },
        { "О", "polyamide silk" }, { "П", "polyethylene" }, { "С", "fiberglass" },
        { "Т", "with a carrier cable" }, { "Ф", "seamed (folded) sheath" }, { "Э", "screened" } };
    static const MarkLetter MK_SHEATH[] = {
        { "", "- (no letter)" }, { "Н", "nairit sheath" }, { "П", "PVC sheath" } };
    static const MarkLetter MK_DESIGN[] = {
        { "", "- (no letter)" }, { "А", "asphalt-coated" }, { "Б", "armoured with steel tapes" },
        { "Г", "bare, no protective cover" }, { "К", "armoured with round wire" },
        { "О", "in a protective braid" }, { "Т", "for laying inside pipes" },
        { "БШв", "steel tapes, PVC hose" },
        { "БШп", "steel tapes, PE hose" },
        { "П", "flat steel wires" },
        { "Ба", "aluminium tapes (single-core)" },
        { "Ка", "round aluminium wires (single-core)" },
        { "", "corrugated steel tape" },
        { "", "steel braid (mail armour)" },
    };
    constexpr int MK_TYPE_N = 7, MK_INS_N = 14, MK_SHEATH_N = 3, MK_DESIGN_N = 14, MK_CORES_N = 5, MK_U_N = 6;
    static const float MK_U_KV[MK_U_N] = { 0.38f, 0.66f, 1.0f, 3.0f, 6.0f, 10.0f };

    inline int MarkClamp(int v, int n) { return (v < 0 || v >= n) ? 0 : v; }

    // Полная марка: буквы + число жил x сечение - напряжение, например "АПВ 3x2.5-0.66"
    inline void BuildCableMark(char* out, size_t out_size) {
        const int cores = MarkClamp(calc_data::mark_cores, MK_CORES_N) + 1;
        const float S = calc_data::result_section;
        const float Sn = MarkNeutral(S);
        char size_buf[40];
        if (Sn < S - 0.01f) snprintf(size_buf, sizeof(size_buf), "3x%g+1x%g", (double)S, (double)Sn);
        else if (cores > 1) snprintf(size_buf, sizeof(size_buf), "%dx%g", cores, (double)S);
        else snprintf(size_buf, sizeof(size_buf), "%g", (double)S);
        snprintf(out, out_size, "%s%s%s%s%s %s-%g",
            calc_data::cable_material == 1 ? "А" : "",
            MK_TYPE[MarkClamp(calc_data::mark_type, MK_TYPE_N)].letter,
            MK_INS[MarkClamp(calc_data::mark_ins, MK_INS_N)].letter,
            MK_SHEATH[MarkClamp(calc_data::mark_sheath, MK_SHEATH_N)].letter,
            MK_DESIGN[MarkClamp(calc_data::mark_design, MK_DESIGN_N)].letter,
            size_buf,
            (double)MK_U_KV[MarkClamp(calc_data::mark_u, MK_U_N)]);
    }

    // Выпадающий список букв: "Б - бронированная стальными лентами"
    inline void MarkCombo(const char* id, int* value, const MarkLetter* list, int n) {
        static char bufs[16][160];
        const char* items[16];
        if (n > 16) n = 16;
        for (int i = 0; i < n; ++i) {
            if (list[i].letter[0] == 0) snprintf(bufs[i], sizeof(bufs[i]), "%s", i18n::T(list[i].key));
            else snprintf(bufs[i], sizeof(bufs[i]), "%s - %s", list[i].letter, i18n::T(list[i].key));
            items[i] = bufs[i];
        }
        *value = MarkClamp(*value, n);
        CustomCombo(id, value, items, n);
    }

    void RenderCableTab() {
        using i18n::T;
        using i18n::L;

        gui.group_box(T("PARAMETERS"), ImVec2(CARD_W_HALF, 1105)); {
            ImGui::TextColored(g_theme.text_dim, "%s", T("Material:"));
            ImGui::RadioButton(L("Copper"), &calc_data::cable_material, 0); ImGui::SameLine();
            ImGui::RadioButton(L("Aluminum"), &calc_data::cable_material, 1);

            ImGui::Spacing();
            ImGui::TextColored(g_theme.text_dim, "%s", T("Phases:"));
            ImGui::RadioButton(L("1 Phase"), &calc_data::phases, 1); ImGui::SameLine();
            ImGui::RadioButton(L("3 Phases"), &calc_data::phases, 3);

            ImGui::Spacing();
            ImGui::TextColored(g_theme.text_dim, "%s", T("Installation:"));
            ImGui::RadioButton(L("Air"), &calc_data::cable_install, 0); ImGui::SameLine();
            ImGui::RadioButton(L("Pipe"), &calc_data::cable_install, 1); ImGui::SameLine();
            ImGui::RadioButton(L("Earth"), &calc_data::cable_install, 2); ImGui::SameLine();
            ImGui::RadioButton(L("Water"), &calc_data::cable_install, 3);

            ImGui::Spacing();
            ImGui::TextColored(g_theme.text_dim, "%s", T("Insulation:"));
            {
                const char* ins_items[INS_N];
                int pos = 0;
                const int cur = InsClamp(calc_data::insulation_type);
                for (int i = 0; i < INS_N; ++i) {
                    ins_items[i] = T(INS_TABLE[INS_ORDER[i]].key);
                    if (INS_ORDER[i] == cur) pos = i;
                }
                const int before = pos;
                CustomCombo("##ins_type", &pos, ins_items, INS_N);
                if (pos != before) {
                    calc_data::insulation_type = INS_ORDER[pos];
                    // изоляция меняет и букву в марке кабеля
                    switch (calc_data::insulation_type) {
                    case 0: case 4: case 5: case 6: calc_data::mark_ins = 0; break;    // ПВХ - В
                    case 1: case 3: case 9:         calc_data::mark_ins = 9; break;    // полиэтилен - П
                    case 2: case 16:                calc_data::mark_ins = 6; break;    // резина, найрит - Н
                    case 25:                        calc_data::mark_ins = 10; break;   // стекловолокно - С
                    default: break;
                    }
                }
            }

            ImGui::Spacing();
            ImGui::TextColored(g_theme.text_dim, "%s", T("Ambient temp, C:"));
            TextInputFloat("ambient", &calc_data::ambient_temp);

            ImGui::TextColored(g_theme.text_dim, "%s", T("Cables laid together:"));
            {
                double group_d = (double)calc_data::cable_group;
                if (TextInputDouble("cgroup", &group_d))
                    calc_data::cable_group = (group_d < 1.0) ? 1 : (group_d > 50.0 ? 50 : (int)group_d);
            }

            ImGui::TextColored(g_theme.text_dim, "%s", T("Altitude above sea level, m:"));
            TextInputFloat("caltitude", &calc_data::cable_altitude);

            ImGui::Spacing();
            ImGui::TextColored(g_theme.text_dim, "%s", T("Power, kW:"));
            TextInputFloat("power", &calc_data::load_power_kw);
            ImGui::TextColored(g_theme.text_dim, "%s", T("Voltage, V:"));
            TextInputFloat("volt", &calc_data::voltage);
            ImGui::TextColored(g_theme.text_dim, "%s", T("cos φ:"));
            TextInputFloat("cosphi", &calc_data::cos_phi);
            ImGui::TextColored(g_theme.text_dim, "%s", T("Length, m:"));
            TextInputFloat("length", &calc_data::cable_length_m);

            // === NEW: трансформатор ТП - для расчёта тока КЗ ===
            ImGui::Spacing();
            ImGui::TextColored(g_theme.text_dim, "%s", T("Supply transformer (Y/Yn):"));
            {
                static char trafo_buf[TRAFO_N][48];
                const char* trafo_items[TRAFO_N];
                for (int i = 0; i < TRAFO_N; ++i) {
                    if (TRAFO_KVA[i] == 0) snprintf(trafo_buf[i], sizeof(trafo_buf[i]), "%s", T("Not considered"));
                    else snprintf(trafo_buf[i], sizeof(trafo_buf[i]), "%d %s", TRAFO_KVA[i], T("kVA"));
                    trafo_items[i] = trafo_buf[i];
                }
                CustomCombo("##trafo", &calc_data::trafo_index, trafo_items, TRAFO_N);
            }

            ImGui::Spacing();
            ToggleSwitch(T("Manual section"), &calc_data::use_manual_section);

            if (calc_data::use_manual_section) {
                ImGui::Spacing();
                ImGui::TextColored(g_theme.text_dim, "%s", T("Section, mm^2:"));
                TextInputFloat("mansec", &calc_data::manual_section);
            }

            if (calc_data::use_auto_calc) RecalcCable();
        } gui.end_group_box();

        ImGui::SameLine(0.0f, 15.0f);

        // ==================== RESULT ====================
        gui.group_box(T("RESULT"), ImVec2(CARD_W_HALF, 1105)); {
            const ImVec4 col_ok = g_theme.res_good;
            const ImVec4 col_fail = g_theme.res_bad;
            char buf[64];

            snprintf(buf, sizeof(buf), "%.2f A", calc_data::result_current);
            ResultRow(T("Current:"), buf, g_theme.accent);

            // === NEW: сечение по ПУЭ и по какому критерию выбрано ===
            if (calc_data::result_criterion == 4)
                snprintf(buf, sizeof(buf), "> 150 mm^2");
            else
                snprintf(buf, sizeof(buf), "%.1f mm^2", calc_data::result_required_section);
            ResultRow(T("Section (PUE):"), buf, calc_data::result_criterion == 4 ? col_fail : col_ok);
            {
                static const char* crit_names[] = {
                    "by heating", "by breaker protection", "by voltage drop", "by short-circuit current",
                    "no suitable section in PUE tables" };
                int c = calc_data::result_criterion;
                if (c < 0 || c > 4) c = 0;
                ResultRow(T("Chosen:"), T(crit_names[c]), c == 4 ? col_fail : g_theme.res_info);
            }
            snprintf(buf, sizeof(buf), "%.0f A", calc_data::result_i_allow);
            ResultRow(T("Allowable current:"), buf,
                calc_data::result_i_allow >= calc_data::result_current ? col_ok : col_fail);

            if (calc_data::use_manual_section) {
                snprintf(buf, sizeof(buf), "%.1f mm^2", calc_data::manual_section);
                ResultRow(T("Manual section:"), buf, g_theme.res_warn);
                const bool sec_ok = (calc_data::result_criterion != 4) &&
                    (calc_data::manual_section >= calc_data::result_required_section - 0.001f);
                ResultRow(T("Section check:"),
                    sec_ok ? T("OK") : T("OVERLOAD"),
                    sec_ok ? col_ok : col_fail);
            }

            ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

            snprintf(buf, sizeof(buf), "%.2f", calc_data::result_k_temp);
            ResultRow(T("Temp factor:"), buf, g_theme.res_warn);
            snprintf(buf, sizeof(buf), "%.2f", calc_data::result_k_group);
            ResultRow(T("Group factor:"), buf, g_theme.res_warn);
            snprintf(buf, sizeof(buf), "%.2f", calc_data::result_k_alt);
            ResultRow(T("Altitude factor:"), buf, g_theme.res_warn);
            ResultRow(T("Insulation:"), T(InsulationName(calc_data::insulation_type)),
                g_theme.res_warn);

            ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

            snprintf(buf, sizeof(buf), "%.2f V", calc_data::result_drop_v);
            ResultRow(T("Voltage drop:"), buf, g_theme.res_warn);
            snprintf(buf, sizeof(buf), "%.2f %%", calc_data::result_drop_pct);
            ResultRow(T("Drop percent:"), buf,
                calc_data::result_drop_pct <= 5.0f ? col_ok : col_fail);

            // === Ik: однофазное КЗ в конце линии ===
            ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
            {
                char min_buf[64];
                const char pref = (calc_data::breaker_curve == 0) ? 'B' : (calc_data::breaker_curve == 2 ? 'D' : 'C');
                if (calc_data::breaker_rating <= 0)
                    snprintf(min_buf, sizeof(min_buf), "%.0f A (%c%d %s)", calc_data::result_ik_min, pref,
                        calc_data::result_breaker_in, T("(auto)"));
                else
                    snprintf(min_buf, sizeof(min_buf), "%.0f A (%c%d)", calc_data::result_ik_min, pref,
                        calc_data::result_breaker_in);

                snprintf(buf, sizeof(buf), "%.3f Ohm", calc_data::result_z_trafo);
                ResultRow(T("Transformer Zt/3:"), buf, g_theme.text_main);
                snprintf(buf, sizeof(buf), "%.3f Ohm", calc_data::result_z_loop);
                ResultRow(T("Line loop Z:"), buf, g_theme.text_main);

                if (calc_data::result_ik > 0.0f) {
                    const bool ik_ok = (calc_data::result_ik >= calc_data::result_ik_min);
                    snprintf(buf, sizeof(buf), "%.0f A", calc_data::result_ik);
                    ResultRow(T("Ik:"), buf, ik_ok ? col_ok : col_fail);
                    ResultRow(T("Min Ik for breaker:"), min_buf, g_theme.text_main);
                    ResultRow(T("Ik check:"), ik_ok ? T("OK") : T("FAIL"), ik_ok ? col_ok : col_fail);
                }
                else {
                    ResultRow(T("Ik:"), T("n/a"), g_theme.text_dim);
                    ResultRow(T("Min Ik for breaker:"), min_buf, g_theme.text_main);
                    ResultRow(T("Ik check:"), T("n/a"), g_theme.text_dim);
                }
            }

            ImGui::Spacing();
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextColored(g_theme.text_dim, "%s",
                T("Per PUE tables 1.3.4-1.3.7, temperature per table 1.3.3. Contacts 0.03 Ohm included."));
            ImGui::PopTextWrapPos();

            ImGui::Spacing();
            if (OutlineButton(L("Calculate"), ImVec2(-1, 36))) {
                RecalcCable();
                char hbuf[128];
                char mark_buf[64];
                BuildCableMark(mark_buf, sizeof(mark_buf));
                snprintf(hbuf, sizeof(hbuf), "I=%.2f A, S=%.2f mm2, %s, %s",
                    calc_data::result_current, calc_data::result_section,
                    calc_data::result_install_name, mark_buf);
                history::Add("Cable", hbuf);
            }
        } gui.end_group_box();

        ImGui::Spacing();

        // ==================== МАРКА КАБЕЛЯ ====================
        gui.group_box(T("CABLE MARK"), ImVec2(CARD_W_HALF, 640)); {
            ImGui::TextColored(g_theme.text_dim, "%s", T("Wire type (2nd letter):"));
            MarkCombo("##mk_type", &calc_data::mark_type, MK_TYPE, MK_TYPE_N);

            ImGui::TextColored(g_theme.text_dim, "%s", T("Insulation (3rd letter):"));
            {
                const int before = calc_data::mark_ins;
                MarkCombo("##mk_ins", &calc_data::mark_ins, MK_INS, MK_INS_N);
                if (calc_data::mark_ins != before) {
                    // буква изоляции задаёт допустимую температуру жилы в расчёте
                    switch (calc_data::mark_ins) {
                    case 0: case 1: calc_data::insulation_type = 0; break;   // В, ВР - ПВХ
                    case 6: case 7: calc_data::insulation_type = 2; break;   // Н, НР - резина
                    case 9:         calc_data::insulation_type = 3; break;   // П - полиэтилен
                    case 10:        calc_data::insulation_type = 25; break;  // С - стекловолокно
                    default: break;                                          // остальные на нагрев не влияют
                    }
                }
            }

            ImGui::TextColored(g_theme.text_dim, "%s", T("Sheath (for rubber insulation):"));
            MarkCombo("##mk_sheath", &calc_data::mark_sheath, MK_SHEATH, MK_SHEATH_N);

            ImGui::TextColored(g_theme.text_dim, "%s", T("Design (4th letter):"));
            MarkCombo("##mk_design", &calc_data::mark_design, MK_DESIGN, MK_DESIGN_N);

            ImGui::TextColored(g_theme.text_dim, "%s", T("Number of cores:"));
            {
                static const char* const core_items[MK_CORES_N] = { "1", "2", "3", "4", "5" };
                calc_data::mark_cores = MarkClamp(calc_data::mark_cores, MK_CORES_N);
                CustomCombo("##mk_cores", &calc_data::mark_cores, core_items, MK_CORES_N);
            }

            ToggleSwitch(T("Reduced neutral (3+1)"), &calc_data::mark_reduced);

            ImGui::TextColored(g_theme.text_dim, "%s", T("Rated voltage, kV:"));
            {
                static const char* const u_items[MK_U_N] = { "0.38", "0.66", "1", "3", "6", "10" };
                calc_data::mark_u = MarkClamp(calc_data::mark_u, MK_U_N);
                CustomCombo("##mk_u", &calc_data::mark_u, u_items, MK_U_N);
            }

            ImGui::Spacing();
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextColored(g_theme.text_dim, "%s",
                T("The first letter follows the Material switch, the section comes from the calculation."));
            ImGui::PopTextWrapPos();
        } gui.end_group_box();

        ImGui::SameLine(0.0f, 15.0f);

        gui.group_box(T("MARK CHECK"), ImVec2(CARD_W_HALF, 640)); {
            const ImVec4 col_ok = g_theme.res_good;
            const ImVec4 col_fail = g_theme.res_bad;
            char buf[96];

            BuildCableMark(buf, sizeof(buf));
            ResultRow(T("Mark:"), buf, g_theme.accent);

            ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

            // напряжение кабеля не ниже напряжения сети
            const float u_kv = MK_U_KV[MarkClamp(calc_data::mark_u, MK_U_N)];
            const bool u_ok = (u_kv * 1000.0f >= calc_data::voltage - 0.5f);
            snprintf(buf, sizeof(buf), "%g kV / %.0f V", (double)u_kv, calc_data::voltage);
            ResultRow(T("Cable voltage:"), buf, g_theme.text_main);
            ResultRow(T("Voltage check:"), u_ok ? T("OK") : T("FAIL"), u_ok ? col_ok : col_fail);

            ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

            // жил должно хватать на сеть; одножильные провода кладут по одному на каждый проводник
            const float s_neutral = MarkNeutral(calc_data::result_section);
            const bool reduced = (s_neutral < calc_data::result_section - 0.01f);   // кабель 3+1
            const int cores = reduced ? 4 : MarkClamp(calc_data::mark_cores, MK_CORES_N) + 1;
            const int need = (calc_data::phases == 3) ? 3 : 2;
            const bool cores_ok = (cores == 1) || (cores >= need);
            if (cores_ok) snprintf(buf, sizeof(buf), "%d - %s", cores, T("OK"));
            else snprintf(buf, sizeof(buf), "%d - %s %d", cores, T("need at least"), need);
            ResultRow(T("Cores:"), buf, cores_ok ? col_ok : col_fail);

            // конструкция должна подходить к способу прокладки
            const int des = MarkClamp(calc_data::mark_design, MK_DESIGN_N);
            const char* lay_text = T("OK");
            bool lay_ok = true;
            const bool arm_al = (des == 10 || des == 11);                                    // Ба, Ка
            const bool arm_steel = (des == 2 || des == 4 || des == 7 || des == 8 || des == 9 || des == 12 || des == 13);
            if (calc_data::cable_install == 2 && !(arm_al || (arm_steel && des != 13))) {     // земля: нужна броня
                lay_ok = false;
                lay_text = T("armour needed for earth");
            }
            else if (calc_data::cable_install == 3 && des != 4 && des != 11) {                // вода: броня из проволоки
                lay_ok = false;
                lay_text = T("round-wire armour needed: К or Ка");
            }
            ResultRow(T("Laying:"), lay_text, lay_ok ? col_ok : col_fail);

            // стальная броня на одножильном кабеле греется от переменного тока
            const char* arm_text = T("OK");
            bool arm_ok = true;
            if (arm_al && cores > 1) { arm_ok = false; arm_text = T("Ба and Ка are for single-core cables"); }
            else if (arm_steel && cores == 1) { arm_ok = false; arm_text = T("single-core: use Ба or Ка"); }
            ResultRow(T("Armour:"), arm_text, arm_ok ? col_ok : col_fail);

            ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

            // масса металла жил: m = n * S * L * плотность
            const float density = (calc_data::cable_material == 0) ? 8.9f : 2.7f;   // г/см3
            const float metal_mm2 = reduced ? (3.0f * calc_data::result_section + s_neutral)
                : (float)cores * calc_data::result_section;
            const float mass_kg = metal_mm2 * calc_data::cable_length_m * density / 1000.0f;
            snprintf(buf, sizeof(buf), "%.1f mm^2", calc_data::result_section);
            ResultRow(T("Section, mm^2:"), buf, g_theme.text_main);
            snprintf(buf, sizeof(buf), "%.2f %s", mass_kg, T("kg"));
            ResultRow(T("Core metal mass:"), buf, g_theme.res_info);

            ImGui::Spacing();
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextColored(g_theme.text_dim, "%s",
                T("In earth a cable needs armour (Б or К), in water - round-wire armour (К). The cable voltage must be no less than the network voltage."));
            ImGui::TextColored(g_theme.text_dim, "%s",
                T("Steel armour on a single-core AC cable heats up, aluminium (Ба, Ка) is used instead."));
            ImGui::PopTextWrapPos();
        } gui.end_group_box();

        ImGui::Spacing();

        // ==================== ЭКОНОМИКА ====================
        gui.group_box(T("ECONOMICS"), ImVec2(CARD_W_FULL, 500)); {
            ImGui::TextColored(g_theme.text_dim, "%s", T("Hours of maximum load per year:"));
            TextInputFloat("eco_tmax", &calc_data::cable_tmax);
            ImGui::TextColored(g_theme.text_dim, "%s", T("Price per kWh:"));
            TextInputFloat("eco_price", &calc_data::cable_price);

            ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

            char buf[96];
            const float I = calc_data::result_current;
            const float S = (calc_data::result_section > 0.01f) ? calc_data::result_section : 1.5f;
            float tmax = calc_data::cable_tmax;
            if (tmax < 0.0f) tmax = 0.0f;
            if (tmax > 8760.0f) tmax = 8760.0f;

            // ПУЭ табл. 1.3.36, А/мм2. Строка зависит от изоляции:
            // кабели с резиновой и пластмассовой изоляцией или с бумажной
            const bool cu = (calc_data::cable_material == 0);
            const bool paper = (calc_data::insulation_type >= 20 && calc_data::insulation_type <= 23);
            float j = 0.0f;
            if (paper) {
                j = cu ? 3.0f : 1.6f;
                if (tmax > 5000.0f) j = cu ? 2.0f : 1.2f;
                else if (tmax > 3000.0f) j = cu ? 2.5f : 1.4f;
            }
            else {
                j = cu ? 3.5f : 1.9f;
                if (tmax > 5000.0f) j = cu ? 2.7f : 1.6f;
                else if (tmax > 3000.0f) j = cu ? 3.1f : 1.7f;
            }
            snprintf(buf, sizeof(buf), "%.1f A/mm^2", j);
            ResultRow(T("Economic current density:"), buf, g_theme.text_main);

            // экономическое сечение округляем до ближайшего стандартного
            const float s_calc = I / j;
            float s_eco = PUE_S[0];
            for (int i = 0; i < PUE_N; ++i)
                if (fabsf(PUE_S[i] - s_calc) < fabsf(s_eco - s_calc)) s_eco = PUE_S[i];
            snprintf(buf, sizeof(buf), "%.1f -> %.1f mm^2", s_calc, s_eco);
            ResultRow(T("Economic section:"), buf, g_theme.accent);

            // потери: dP = n * I^2 * rho * L / S; за год - через время наибольших потерь
            const float n_wires = (calc_data::phases == 3) ? 3.0f : 2.0f;
            const float tau = (0.124f + tmax / 10000.0f) * (0.124f + tmax / 10000.0f) * 8760.0f;
            auto loss_w = [&](float sx) { return n_wires * I * I * RhoHot() * calc_data::cable_length_m / sx; };
            const float p_loss = loss_w(S);
            const float cost = p_loss / 1000.0f * tau * calc_data::cable_price;
            snprintf(buf, sizeof(buf), "%.1f W", p_loss);
            ResultRow(T("Power loss in the line:"), buf, g_theme.res_warn);
            snprintf(buf, sizeof(buf), "%.0f kWh", p_loss / 1000.0f * tau);
            ResultRow(T("Energy loss per year:"), buf, g_theme.res_warn);
            snprintf(buf, sizeof(buf), "%.0f %s", cost, T("rub"));
            ResultRow(T("Loss cost per year:"), buf, g_theme.res_bad);

            if (s_eco > S + 0.01f) {
                const float cost_eco = loss_w(s_eco) / 1000.0f * tau * calc_data::cable_price;
                snprintf(buf, sizeof(buf), "%.0f %s", cost_eco, T("rub"));
                ResultRow(T("Loss cost with economic section:"), buf, g_theme.res_info);
                snprintf(buf, sizeof(buf), "%.0f %s", cost - cost_eco, T("rub"));
                ResultRow(T("Saving per year:"), buf, g_theme.res_good);
            }

            ImGui::Spacing();
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextColored(g_theme.text_dim, "%s",
                T("Economic density per PUE table 1.3.36. PUE does not require this check for networks up to 1 kV with less than 4000-5000 hours of maximum load."));
            ImGui::PopTextWrapPos();
        } gui.end_group_box();

        ImGui::Spacing();

        // ==================== MAX LENGTH ====================
        gui.group_box(T("MAX LENGTH FOR 5% VOLTAGE DROP"), ImVec2(CARD_W_FULL, 320)); {
            const float U = calc_data::voltage;
            const float I = (calc_data::result_current > 0.01f) ? calc_data::result_current : 1.0f;
            const float rho = RhoHot();                                   // при рабочей температуре жилы
            const float k = (calc_data::phases == 3) ? 1.732f : 2.0f;
            float cphi = calc_data::cos_phi;
            if (cphi > 1.0f) cphi = 1.0f;
            if (cphi < 0.1f) cphi = 0.1f;
            const float sphi = sqrtf(1.0f - cphi * cphi);
            constexpr float X_COL2 = 140.0f;   // вторая колонка (шрифт пропорциональный)

            ImGui::TextColored(g_theme.text_dim, "%s", T("Section"));
            ImGui::SameLine(X_COL2);
            ImGui::TextColored(g_theme.text_dim, "%s", T("Max length, m"));
            ImGui::Separator();

            const float sections[] = { 1.5f, 2.5f, 4.0f, 6.0f, 10.0f, 16.0f, 25.0f };
            for (float S : sections) {
                // dU = k * I * L * (rho/S * cos + x0 * sin) = 5% U
                const float len_max = (U * 0.05f) / (k * I * (rho / S * cphi + X0_LINE * sphi));
                ImGui::TextColored(g_theme.text_main, "%.1f", S);
                ImGui::SameLine(X_COL2);
                ImGui::TextColored(g_theme.text_main, "%.0f", len_max);
            }

            ImGui::Spacing();
            ImGui::TextColored(g_theme.res_warn, "%s, %s %.2f A, U = %.0f V",
                T(calc_data::cable_material == 0 ? "Copper" : "Aluminum"),
                T("at current"), calc_data::result_current, U);
        } gui.end_group_box();
    }

    void RenderLoadTab() {
        using i18n::T;
        using i18n::L;

        gui.group_box(T("TOTAL LOAD CALCULATOR"), ImVec2(CARD_W_FULL, 860)); {
            ImGui::TextColored(g_theme.text_dim, "%s", T("Total Power, kW:"));
            TextInputFloat("total_power", &calc_data::total_power_kw);
            ImGui::TextColored(g_theme.text_dim, "%s", T("Voltage, V:"));
            TextInputFloat("load_volt", &calc_data::voltage);
            ImGui::TextColored(g_theme.text_dim, "%s", T("cos φ:"));
            TextInputFloat("load_cosphi", &calc_data::cos_phi);
            ImGui::TextColored(g_theme.text_dim, "%s", T("Hours per day:"));
            TextInputFloat("hours", &calc_data::hours_per_day);

            ImGui::TextColored(g_theme.text_dim, "%s", T("Demand factor Kc:"));
            TextInputFloat("load_kc", &calc_data::load_kc);
            ImGui::TextColored(g_theme.text_dim, "%s", T("Simultaneity factor Ko:"));
            TextInputFloat("load_ko", &calc_data::load_ko);

            ImGui::Spacing();
            ToggleSwitch(T("Non-linear load (PCs, UPS, LED)"), &calc_data::load_nonlinear);
            if (calc_data::load_nonlinear) {
                ImGui::TextColored(g_theme.text_dim, "%s", T("3rd harmonic, % of phase current:"));
                TextInputFloat("load_h3", &calc_data::load_h3);
            }

            ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

            RecalcLoad();
            char buf[64];
            snprintf(buf, sizeof(buf), "%.2f kW", calc_data::result_load_kw);
            ResultRow(T("Design power:"), buf, g_theme.res_warn);

            // 3-я гармоника трёх фаз складывается в нуле: In = 3 * Iф * h3
            if (calc_data::load_nonlinear && calc_data::phases == 3) {
                float h3 = calc_data::load_h3;
                if (h3 < 0.0f) h3 = 0.0f;
                if (h3 > 100.0f) h3 = 100.0f;
                const float i_ph = calc_data::total_current_a;
                const float i_n = 3.0f * i_ph * h3 / 100.0f;
                float i_size = i_ph;                         // до 15% - как обычно
                if (h3 > 45.0f) i_size = i_n;                // кабель по току нуля
                else if (h3 > 33.0f) i_size = i_n / 0.86f;
                else if (h3 > 15.0f) i_size = i_ph / 0.86f;
                snprintf(buf, sizeof(buf), "%.2f A", i_n);
                ResultRow(T("Neutral current:"), buf, i_n > i_ph ? g_theme.res_bad : g_theme.res_warn);
                snprintf(buf, sizeof(buf), "%.2f A", i_size);
                ResultRow(T("Current for cable sizing:"), buf, g_theme.accent);
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextColored(g_theme.text_dim, "%s",
                    T("Up to 15%: neutral equals phase. 15-33%: cable derated by 0.86. Above 33%: the cable is sized by the neutral current."));
                ImGui::PopTextWrapPos();
                ImGui::Spacing();
            }
            snprintf(buf, sizeof(buf), "%.2f A", calc_data::total_current_a);
            ImGui::TextColored(g_theme.text_main, "%s", T("Total Current Load:"));
            ImGui::SameLine();
            ImGui::TextColored(g_theme.accent, "%s", buf);

            snprintf(buf, sizeof(buf), "%.2f kWh/day", calc_data::total_energy_kwh);
            ImGui::TextColored(g_theme.text_main, "%s", T("Daily energy:"));
            ImGui::SameLine();
            ImGui::TextColored(g_theme.res_good, "%s", buf);

            ImGui::Spacing();
            if (OutlineButton(L("Recalculate Load"), ImVec2(-1, 36))) {
                RecalcLoad();
                char hbuf[128];
                snprintf(hbuf, sizeof(hbuf), "Load: %.2f A, %.2f kWh/day",
                    calc_data::total_current_a, calc_data::total_energy_kwh);
                history::Add("Load", hbuf);
            }
        } gui.end_group_box();
    }

    void RenderGroundTab() {
        using i18n::T;
        using i18n::L;

        gui.group_box(T("EARTHING RESISTANCE"), ImVec2(CARD_W_FULL, 1050)); {
            ImGui::TextColored(g_theme.text_dim, "%s", T("Soil Resistivity, Ohm*m:"));
            TextInputFloat("soil", &calc_data::soil_resistivity);
            ImGui::TextColored(g_theme.text_dim, "%s", T("Earthing type:"));
            ImGui::RadioButton(L("Vertical rods"), &calc_data::ground_type, 0); ImGui::SameLine();
            ImGui::RadioButton(L("Horizontal strip 40x4"), &calc_data::ground_type, 1);
            const bool g_vertical = (calc_data::ground_type != 1);

            if (g_vertical) {
                ImGui::TextColored(g_theme.text_dim, "%s", T("Rod Length, m:"));
                TextInputFloat("rodlen", &calc_data::ground_rod_len);

                double rods_d = (double)calc_data::ground_rods;
                ImGui::TextColored(g_theme.text_dim, "%s", T("Number of Rods:"));
                if (TextInputDouble("rods", &rods_d))
                    calc_data::ground_rods = (rods_d < 1.0) ? 1 : (int)rods_d;

                ImGui::TextColored(g_theme.text_dim, "%s", T("Distance between rods, m:"));
                TextInputFloat("rodspace", &calc_data::ground_spacing);
                ImGui::TextColored(g_theme.text_dim, "%s", T("Depth of rod top, m:"));
                TextInputFloat("roddepth", &calc_data::ground_depth);
            }
            else {
                ImGui::TextColored(g_theme.text_dim, "%s", T("Strip length, m:"));
                TextInputFloat("striplen", &calc_data::ground_strip_len);
                ImGui::TextColored(g_theme.text_dim, "%s", T("Depth, m:"));
                TextInputFloat("stripdepth", &calc_data::ground_depth);
            }

            ImGui::TextColored(g_theme.text_dim, "%s", T("Seasonal factor (climate zone):"));
            {
                const char* season_items[5] = { T("Not considered"), T("Zone I, cold"),
                    T("Zone II"), T("Zone III"), T("Zone IV, warm") };
                if (calc_data::ground_season < 0 || calc_data::ground_season > 4) calc_data::ground_season = 0;
                CustomCombo("##gseason", &calc_data::ground_season, season_items, 5);
            }

            if (g_vertical) {
                ImGui::Spacing();
                ImGui::TextColored(g_theme.text_dim, "%s", T("Electrode:"));
                ImGui::RadioButton(L("Round bar d16"), &calc_data::ground_electrode, 0); ImGui::SameLine();
                ImGui::RadioButton(L("Angle 50x50"), &calc_data::ground_electrode, 1);
            }

            ImGui::Spacing();
            ImGui::TextColored(g_theme.text_dim, "%s", T("Required resistance:"));
            ImGui::RadioButton(L("4 Ohm (TP neutral)"), &calc_data::ground_norm, 0); ImGui::SameLine();
            ImGui::RadioButton(L("10 Ohm"), &calc_data::ground_norm, 1); ImGui::SameLine();
            ImGui::RadioButton(L("30 Ohm (repeated)"), &calc_data::ground_norm, 2);

            ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

            RecalcGround();
            char buf[64];
            const ImVec4 col_ok = g_theme.res_good;
            const ImVec4 col_fail = g_theme.res_bad;
            const float norm = GroundNorm();

            snprintf(buf, sizeof(buf), "%.0f Ohm*m", calc_data::soil_resistivity * GroundSeasonK());
            ResultRow(T("Design soil resistivity:"), buf, g_theme.res_warn);
            if (g_vertical) {
                snprintf(buf, sizeof(buf), "%.2f Ohm", calc_data::result_ground_single);
                ResultRow(T("One rod:"), buf, g_theme.text_main);
                snprintf(buf, sizeof(buf), "%.2f", calc_data::result_ground_eta);
                ResultRow(T("Utilization factor:"), buf, g_theme.res_warn);
            }
            snprintf(buf, sizeof(buf), "%.2f Ohm", calc_data::result_ground);
            ResultRow(T("Resistance:"), buf, calc_data::result_ground <= norm ? col_ok : col_fail);

            if (calc_data::result_ground <= norm)
                snprintf(buf, sizeof(buf), "%s (<= %.0f Ohm)", T("OK"), norm);
            else
                snprintf(buf, sizeof(buf), "%s (> %.0f Ohm)", T("FAIL"), norm);
            ResultRow(T("Norm check:"), buf, calc_data::result_ground <= norm ? col_ok : col_fail);

            if (g_vertical) {
                if (calc_data::result_ground_need > 0)
                    snprintf(buf, sizeof(buf), "%d", calc_data::result_ground_need);
                else
                    snprintf(buf, sizeof(buf), "> 100");
                ResultRow(T("Rods needed for norm:"), buf, g_theme.accent);
            }
            else {
                if (calc_data::result_ground_len_need > 0.0f)
                    snprintf(buf, sizeof(buf), "%.0f m", calc_data::result_ground_len_need);
                else
                    snprintf(buf, sizeof(buf), "> 1000 m");
                ResultRow(T("Strip length needed for norm:"), buf, g_theme.accent);
            }

            ImGui::Spacing();
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextColored(g_theme.text_dim, "%s",
                T("Vertical rods in a row, without the connecting strip (with margin). Utilization factors are approximate table values."));
            ImGui::PopTextWrapPos();

            ImGui::Spacing();
            if (OutlineButton(L("Save to History"), ImVec2(-1, 32), btn_col::success)) {
                char hbuf[128];
                snprintf(hbuf, sizeof(hbuf), "Ground: %.2f Ohm (rods=%d)",
                    calc_data::result_ground, calc_data::ground_rods);
                history::Add("Ground", hbuf);
            }
        } gui.end_group_box();

        ImGui::Spacing();

        // ==================== МОЛНИЕЗАЩИТА ====================
        // Одиночный стержневой молниеотвод, зона защиты - конус (СО 153-34.21.122-2003, табл. 3.4)
        gui.group_box(T("LIGHTNING PROTECTION"), ImVec2(CARD_W_FULL, 600)); {
            ImGui::TextColored(g_theme.text_dim, "%s", T("Rod height, m:"));
            TextInputFloat("lp_h", &calc_data::lp_h);
            ImGui::TextColored(g_theme.text_dim, "%s", T("Object height, m:"));
            TextInputFloat("lp_hx", &calc_data::lp_hx);
            ImGui::TextColored(g_theme.text_dim, "%s", T("Distance to the farthest corner, m:"));
            TextInputFloat("lp_need", &calc_data::lp_need);

            ImGui::Spacing();
            ImGui::TextColored(g_theme.text_dim, "%s", T("Protection reliability:"));
            ImGui::RadioButton("0.9##lp", &calc_data::lp_rel, 0); ImGui::SameLine();
            ImGui::RadioButton("0.99##lp", &calc_data::lp_rel, 1); ImGui::SameLine();
            ImGui::RadioButton("0.999##lp", &calc_data::lp_rel, 2);

            ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

            // радиус зоны на высоте hx для молниеотвода высотой h
            auto zone = [](float h, float hx, float* h0_out, float* r0_out) {
                float h0 = 0.85f * h, r0 = 1.2f * h;                       // надёжность 0,9
                if (calc_data::lp_rel == 0) {
                    if (h > 100.0f) r0 = (1.2f - 0.001f * (h - 100.0f)) * h;
                }
                else if (calc_data::lp_rel == 1) {                         // 0,99
                    h0 = 0.8f * h;
                    r0 = (h <= 30.0f) ? 0.8f * h : (0.8f - 0.00143f * (h - 30.0f)) * h;
                }
                else {                                                     // 0,999
                    h0 = (h <= 30.0f) ? 0.7f * h : (0.7f - 0.000714f * (h - 30.0f)) * h;
                    r0 = (h <= 30.0f) ? 0.6f * h : (0.6f - 0.00143f * (h - 30.0f)) * h;
                }
                if (h0_out) *h0_out = h0;
                if (r0_out) *r0_out = r0;
                return (hx < h0 && h0 > 0.0f) ? r0 * (h0 - hx) / h0 : 0.0f;
                };

            const float h_max = (calc_data::lp_rel == 0) ? 150.0f : 100.0f;   // границы применимости методики
            float h = calc_data::lp_h;
            if (h < 0.1f) h = 0.1f;
            if (h > h_max) h = h_max;
            const float hx = (calc_data::lp_hx > 0.0f) ? calc_data::lp_hx : 0.0f;

            float h0 = 0.0f, r0 = 0.0f;
            const float rx = zone(h, hx, &h0, &r0);
            const bool lp_ok = (rx >= calc_data::lp_need) && (rx > 0.0f);

            char buf[64];
            snprintf(buf, sizeof(buf), "%.2f m", h0);
            ResultRow(T("Zone cone height h0:"), buf, g_theme.text_main);
            snprintf(buf, sizeof(buf), "%.2f m", r0);
            ResultRow(T("Zone radius at ground r0:"), buf, g_theme.text_main);
            snprintf(buf, sizeof(buf), "%.2f m", rx);
            ResultRow(T("Radius at object height rx:"), buf, g_theme.accent);
            ResultRow(T("Object is protected:"), lp_ok ? T("Yes") : T("No"), lp_ok ? g_theme.res_good : g_theme.res_bad);

            // наименьшая высота, при которой объект помещается в зону
            float h_min = 0.0f;
            for (float hh = (hx > 0.5f ? hx : 0.5f); hh <= h_max; hh += 0.1f) {
                if (zone(hh, hx, nullptr, nullptr) >= calc_data::lp_need) { h_min = hh; break; }
            }
            if (h_min > 0.0f) snprintf(buf, sizeof(buf), "%.1f m", h_min);
            else snprintf(buf, sizeof(buf), "> %.0f m", h_max);
            ResultRow(T("Minimum rod height:"), buf, h_min > 0.0f ? g_theme.res_info : g_theme.res_bad);

            ImGui::Spacing();
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextColored(g_theme.text_dim, "%s",
                T("Single rod, cone zone per SO 153-34.21.122-2003. The object must fit inside the radius rx at its height."));
            ImGui::PopTextWrapPos();

            ImGui::Spacing();
            if (OutlineButton(L("Save to History", "lp_save"), ImVec2(-1, 32), btn_col::success)) {
                char hbuf[128];
                snprintf(hbuf, sizeof(hbuf), "Lightning rod: h=%.1f m, rx=%.2f m at %.1f m", h, rx, hx);
                history::Add("Lightning", hbuf);
            }
        } gui.end_group_box();
    }

    void RenderBreakerTab() {
        using i18n::T;
        using i18n::L;

        gui.group_box(T("CIRCUIT BREAKER SELECTOR"), ImVec2(CARD_W_FULL, 720)); {
            const float display_I = calc_data::total_current_a > 0
                ? calc_data::total_current_a : calc_data::result_current;

            char ibuf[64];
            snprintf(ibuf, sizeof(ibuf), "%.2f A", display_I);
            ResultRow(T("Current load:"), ibuf, g_theme.accent);

            snprintf(ibuf, sizeof(ibuf), "%.2f", calc_data::breaker_margin);
            ResultRow(T("Safety margin:"), ibuf, ImVec4(0.8f, 0.8f, 0.8f, 1.0f));

            const float target = display_I * calc_data::breaker_margin;
            snprintf(ibuf, sizeof(ibuf), "%.2f A", target);
            ResultRow(T("Target current:"), ibuf, g_theme.res_warn);

            ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
            ImGui::TextColored(g_theme.text_dim, "%s", T("Curve type:"));
            ImGui::RadioButton("B", &calc_data::breaker_curve, 0); ImGui::SameLine();
            ImGui::RadioButton("C", &calc_data::breaker_curve, 1); ImGui::SameLine();
            ImGui::RadioButton("D", &calc_data::breaker_curve, 2);

            ImGui::TextColored(g_theme.text_dim, "  -> %s",
                T(BreakerCurveRecommend(calc_data::breaker_curve)));
            ImGui::Spacing();
            LabeledSlider(T("Safety Margin, x"), &calc_data::breaker_margin, 1.0f, 2.0f, "%.2f");

            ImGui::Spacing();
            ImGui::TextColored(g_theme.text_dim, "%s", T("Temperature in panel, C:"));
            TextInputFloat("brk_temp", &calc_data::breaker_temp);
            ImGui::TextColored(g_theme.text_dim, "%s", T("Short-circuit current at panel, kA:"));
            TextInputFloat("brk_ik", &calc_data::breaker_ik_ka);

            ImGui::Spacing();
            if (OutlineButton(L("Select Breaker Rating"), ImVec2(-1, 36))) {
                RecalcBreaker();
                if (calc_data::breaker_rating > 0) {
                    char hbuf[64];
                    const char* prefix = "C";
                    if (calc_data::breaker_curve == 0) prefix = "B";
                    else if (calc_data::breaker_curve == 2) prefix = "D";
                    snprintf(hbuf, sizeof(hbuf), "Breaker: %s%d", prefix, calc_data::breaker_rating);
                    history::Add("Breaker", hbuf);
                }
            }

            ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

            if (calc_data::breaker_rating > 0) {
                char buf[32];
                const char* prefix = "C";
                if (calc_data::breaker_curve == 0) prefix = "B";
                else if (calc_data::breaker_curve == 2) prefix = "D";
                snprintf(buf, sizeof(buf), "%s%d", prefix, calc_data::breaker_rating);
                ImGui::TextColored(g_theme.text_main, "%s", T("Recommended Breaker:"));
                ImGui::SameLine();
                ImGui::TextColored(g_theme.res_good, "%s", buf);

                char rbuf[64];
                snprintf(rbuf, sizeof(rbuf), "%.1f A", (float)calc_data::breaker_rating * BreakerTempK());
                ResultRow(T("Real rating at this temperature:"), rbuf, g_theme.res_warn);
            }
            else {
                ImGui::TextColored(g_theme.text_dim, "%s", T("Click button to select breaker rating"));
            }

            // отключающая способность должна быть не меньше тока КЗ в месте установки
            {
                static const float ICU[] = { 4.5f, 6.0f, 10.0f, 15.0f, 25.0f, 36.0f, 50.0f };
                float icu = 0.0f;
                for (float v : ICU) if (v >= calc_data::breaker_ik_ka) { icu = v; break; }
                char cbuf[64];
                if (icu > 0.0f) snprintf(cbuf, sizeof(cbuf), "%g kA", (double)icu);
                else snprintf(cbuf, sizeof(cbuf), "> 50 kA");
                ResultRow(T("Breaking capacity needed:"), cbuf, icu > 0.0f ? g_theme.res_good : g_theme.res_bad);
            }
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextColored(g_theme.text_dim, "%s", T("Breakers are calibrated at +30 C, about 0.5% per degree."));
            ImGui::PopTextWrapPos();

            ImGui::Spacing();
            ImGui::TextColored(g_theme.text_dim, "%s 6, 10, 16, 20, 25, 32, 40, 50, 63, 80, 100, 125",
                T("Standard Ratings:"));
        } gui.end_group_box();
    }

    void RenderMathTab() {
        using i18n::T;
        using i18n::L;

        static double a = 0.0, b = 0.0, result = 0.0;
        static int op = 0;
        static bool isDegrees = false;
        static const char* err = "";      // английский ключ, переводится при выводе
        static bool hasResult = false;

        static const char* ops[] = {
            "+  Add", "-  Subtract", "*  Multiply", "/  Divide",
            "^  Power (A^B)", "sqrt  Square Root (of A)", "%  Modulo (A%B)",
            "sin  Sin(A)", "cos  Cos(A)", "tan  Tan(A)",
            "asin  Asin(A)", "acos  Acos(A)", "atan  Atan(A)",
            "log  Log10(A)", "ln   Natural log(A)", "exp  e^A",
            "abs  |A|", "!    Factorial(A)",
        };
        const char* ops_tr[IM_ARRAYSIZE(ops)];
        for (int i = 0; i < IM_ARRAYSIZE(ops); ++i) ops_tr[i] = T(ops[i]);

        gui.group_box(T("SCIENTIFIC CALCULATOR"), ImVec2(CARD_W_FULL, 550)); {
            ImGui::TextColored(g_theme.text_dim, "%s", T("Input A:"));
            TextInputDouble("##A", &a);

            ImGui::Spacing();
            ImGui::TextColored(g_theme.text_dim, "%s", T("Operation:"));
            CustomCombo("##op", &op, ops_tr, IM_ARRAYSIZE(ops));

            const bool needsB = (op >= 0 && op <= 4) || (op == 6);
            if (needsB) {
                ImGui::Spacing();
                ImGui::TextColored(g_theme.text_dim, "%s", T("Input B:"));
                TextInputDouble("##B", &b);
            }

            ImGui::Spacing();
            NiceCheckbox(L("Degrees mode (for sin/cos/tan)"), &isDegrees);

            ImGui::Spacing();
            if (OutlineButton(L("Calculate"), ImVec2(-1, 36))) {
                err = ""; hasResult = true;
                double A = a, B = b;
                if (isDegrees && (op == 7 || op == 8 || op == 9)) {
                    A = A * PI / 180.0;
                    B = B * PI / 180.0;
                }
                if (op == 0) result = A + B;
                else if (op == 1) result = A - B;
                else if (op == 2) result = A * B;
                else if (op == 3) { if (B != 0.0) result = A / B; else err = "Division by zero"; }
                else if (op == 4) result = pow(A, B);
                else if (op == 5) { if (A >= 0) result = sqrt(A); else err = "sqrt of negative"; }
                else if (op == 6) { if ((int)B != 0) result = (double)((int)A % (int)B); else err = "Modulo by zero"; }
                else if (op == 7) result = sin(A);
                else if (op == 8) result = cos(A);
                else if (op == 9) result = tan(A);
                else if (op == 10) result = asin(A);
                else if (op == 11) result = acos(A);
                else if (op == 12) result = atan(A);
                else if (op == 13) { if (A > 0) result = log10(A); else err = "log of non-positive"; }
                else if (op == 14) { if (A > 0) result = log(A); else err = "ln of non-positive"; }
                else if (op == 15) result = exp(A);
                else if (op == 16) result = fabs(A);
                else if (op == 17) {
                    if (A >= 0 && A <= 170 && A == (int)A) {
                        result = 1.0;
                        for (int i = 2; i <= (int)A; ++i) result *= (double)i;
                    }
                    else err = "Factorial: integer 0..170 only";
                }
                if (strlen(err) == 0) {
                    char hbuf[128];
                    snprintf(hbuf, sizeof(hbuf), "Result: %.6g", result);
                    history::Add("Math", hbuf);
                }
            }

            ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

            if (err && strlen(err) > 0) {
                ImGui::TextColored(g_theme.res_bad, "%s %s", T("Error:"), T(err));
            }
            else if (hasResult) {
                char nb[64];
                FormatNumber(nb, sizeof(nb), result);
                ImGui::TextColored(g_theme.text_main, "%s", T("Result:"));
                ImGui::SameLine();
                ImGui::TextColored(g_theme.accent, "%s", nb);
                ImGui::SameLine(0, 30.0f);
                if (SmallOutlineButton(L("Copy", "copy_math"))) CopyToClipboard(nb);
            }

            ImGui::Spacing();
            ImGui::TextColored(g_theme.text_dim, "%s", T("Constants:"));
            ImGui::SameLine();
            if (OutlineButton("pi = 3.14159...", ImVec2(180, 0))) a = PI;
            ImGui::SameLine();
            if (OutlineButton("e = 2.71828...", ImVec2(180, 0))) a = 2.71828182845904523536;
        } gui.end_group_box();
    }

    void RenderConverterTab() {
        using i18n::T;
        using i18n::L;

        static const char* conv_names[] = {
            "Section: mm^2 -> AWG",   "Section: AWG -> mm^2",
            "Power: Watt -> HP",      "Power: HP -> Watt",
            "Temp: C -> F",           "Temp: F -> C",
            "Length: m -> ft",        "Length: ft -> m",
            "Mass: kg -> lb",         "Mass: lb -> kg",
            "Energy: kW -> kcal/h",   "Energy: kcal/h -> kW",
            "Time: sec -> min",       "Time: min -> sec",
            "Time: hours -> sec",     "Time: sec -> hours",
        };
        const char* conv_tr[IM_ARRAYSIZE(conv_names)];
        for (int i = 0; i < IM_ARRAYSIZE(conv_names); ++i) conv_tr[i] = T(conv_names[i]);

        gui.group_box(T("UNIT CONVERTER"), ImVec2(CARD_W_FULL, 380)); {
            ImGui::TextColored(g_theme.text_dim, "%s", T("Conversion:"));
            CustomCombo("##conv", &calc_data::conv_type, conv_tr, IM_ARRAYSIZE(conv_names));

            ImGui::Spacing();
            ImGui::TextColored(g_theme.text_dim, "%s", T("Input value:"));
            TextInputDouble("##conv_in", &calc_data::conv_input);

            ImGui::Spacing();
            if (OutlineButton(L("Convert"), ImVec2(-1, 36))) {
                const double v = calc_data::conv_input;
                double out = 0.0;
                switch (calc_data::conv_type) {
                case 0:  out = (v > 0.0) ? 36.0 - 19.5 * log(v / 0.012668) / log(92.0) : 0.0; break;
                case 1:  out = 0.012668 * pow(92.0, (36.0 - v) / 19.5); break;
                case 2:  out = v / 735.49875; break;
                case 3:  out = v * 735.49875; break;
                case 4:  out = v * 9.0 / 5.0 + 32.0; break;
                case 5:  out = (v - 32.0) * 5.0 / 9.0; break;
                case 6:  out = v * 3.28084; break;
                case 7:  out = v / 3.28084; break;
                case 8:  out = v * 2.20462; break;
                case 9:  out = v / 2.20462; break;
                case 10: out = v * 859.845; break;
                case 11: out = v / 859.845; break;
                case 12: out = v / 60.0; break;
                case 13: out = v * 60.0; break;
                case 14: out = v * 3600.0; break;
                case 15: out = v / 3600.0; break;
                default: out = 0.0; break;
                }
                calc_data::conv_output = out;
            }

            ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

            char nb[64];
            FormatNumber(nb, sizeof(nb), calc_data::conv_output);
            ImGui::TextColored(g_theme.text_main, "%s", T("Result:"));
            ImGui::SameLine();
            ImGui::TextColored(g_theme.accent, "%s", nb);
            ImGui::SameLine(0, 30.0f);
            if (SmallOutlineButton(L("Copy", "copy_conv"))) CopyToClipboard(nb);
        } gui.end_group_box();
    }

    void RenderFormulasTab() {
        using i18n::T;

        static bool s_el = true, s_al = true, s_ge = true;
        static bool s_pw = false, s_vd = false, s_kz = false, s_gr = false, s_mo = false, s_en = false;

        auto ShowResult = [](const char* label, double value, const char* unit) {
            char buf[64];
            FormatNumber(buf, sizeof(buf), value);
            ImGui::TextColored(g_theme.text_dim, "%s", label);
            ImGui::SameLine();
            ImGui::TextColored(g_theme.res_good, "%s %s", buf, unit);
            };
        auto ShowResult2 = [](const char* l1, double v1, const char* u1,
            const char* l2, double v2, const char* u2) {
                char b1[64], b2[64];
                FormatNumber(b1, sizeof(b1), v1);
                FormatNumber(b2, sizeof(b2), v2);
                ImGui::TextColored(g_theme.text_dim, "%s", l1);
                ImGui::SameLine();
                ImGui::TextColored(g_theme.res_good, "%s %s", b1, u1);
                ImGui::SameLine(0.0f, 30.0f);
                ImGui::TextColored(g_theme.text_dim, "%s", l2);
                ImGui::SameLine();
                ImGui::TextColored(g_theme.res_good, "%s %s", b2, u2);
            };

        static float s_formulas_h = 1680.0f;   // высота по содержимому (секции сворачиваются)
        gui.group_box(T("PHYSICS & MATH SOLVERS"), ImVec2(CARD_W_FULL, s_formulas_h)); {

            // ---------- Помощники: поля в ряд, формулы, подзаголовки ----------
            auto Field = [](const char* label, const char* id, double* v) {
                ImGui::BeginGroup();
                ImGui::TextColored(g_theme.text_dim, "%s", label);
                TextInputDouble(id, v, 200.0f);
                ImGui::EndGroup();
                };
            auto Gap = []() { ImGui::SameLine(0.0f, 14.0f); };
            auto Formula = [](const char* f) { ImGui::TextColored(g_theme.accent, "%s", f); };
            auto Note = [](const char* t) {
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextColored(g_theme.text_dim, "%s", t);
                ImGui::PopTextWrapPos();
                };
            auto Sub = [](const char* title) {
                ImGui::Spacing();
                ImGui::TextColored(g_theme.text_main, "%s", title);
                ImGui::Separator();
                };
            using namespace calc_data;
            const double SQ3 = 1.7320508;
            const double PI_D = 3.14159265358979;

            // ================= ЭЛЕКТРИЧЕСТВО =================
            if (SectionHeader(T("Electricity / Current"), &s_el)) {
                Sub(T("Ohm's law and charge"));
                Formula("I = U / R        U = I * R        R = U / I        I = q / t");
                Field(T("Voltage U (V):"), "##el_U", &el_U); Gap();
                Field(T("Resistance R (Ohm):"), "##el_R", &el_R); Gap();
                Field(T("Charge q (C):"), "##el_q", &el_q); Gap();
                Field(T("Time t (s):"), "##el_t", &el_t);
                ShowResult2("I = U/R =", el_R != 0.0 ? el_U / el_R : 0.0, "A",
                    "I = q/t =", el_t != 0.0 ? el_q / el_t : 0.0, "A");

                Sub(T("Power"));
                Formula("P = U * I        P = I^2 * R        P = U^2 / R");
                Field(T("Voltage U (V):"), "##e_pU", &e_pU); Gap();
                Field(T("Current I (A):"), "##e_pI", &e_pI); Gap();
                Field(T("Resistance R (Ohm):"), "##e_pR", &e_pR);
                ShowResult2("U*I =", e_pU * e_pI, "W", "I^2*R =", e_pI * e_pI * e_pR, "W");
                ShowResult("U^2/R =", e_pR != 0.0 ? e_pU * e_pU / e_pR : 0.0, "W");

                Sub(T("Joule-Lenz law"));
                Formula("Q = I^2 * R * t");
                Field(T("Current I (A):"), "##jl_I", &jl_I); Gap();
                Field(T("Resistance R (Ohm):"), "##jl_R", &jl_R); Gap();
                Field(T("Time t (s):"), "##jl_t", &jl_t);
                ShowResult2("Q =", jl_I * jl_I * jl_R * jl_t, "J", "=", jl_I * jl_I * jl_R * jl_t / 3.6e6, "kWh");

                Sub(T("Wire resistance"));
                Formula("R = rho * L / S        R(t) = R20 * (1 + 0.004 * (t - 20))");
                ImGui::RadioButton(i18n::L("Copper", "e_wcu"), &e_wmat, 0); ImGui::SameLine();
                ImGui::RadioButton(i18n::L("Aluminum", "e_wal"), &e_wmat, 1);
                Field(T("Length, m:"), "##e_wL", &e_wL); Gap();
                Field(T("Section, mm^2:"), "##e_wS", &e_wS); Gap();
                Field(T("Core temperature, C:"), "##e_wt", &e_wt);
                {
                    const double rho = (e_wmat == 0) ? 0.0175 : 0.028;
                    const double r20 = (e_wS > 0.0) ? rho * e_wL / e_wS : 0.0;
                    ShowResult2("R20 =", r20, "Ohm", "R(t) =", r20 * (1.0 + 0.004 * (e_wt - 20.0)), "Ohm");
                }

                Sub(T("Series and parallel connection"));
                Formula("Series: R = R1 + R2 + R3        Parallel: 1/R = 1/R1 + 1/R2 + 1/R3");
                Field("R1, Ohm:", "##e_r1", &e_r1); Gap();
                Field("R2, Ohm:", "##e_r2", &e_r2); Gap();
                Field("R3, Ohm (0 = none):", "##e_r3", &e_r3);
                {
                    double inv = 0.0;
                    if (e_r1 > 0.0) inv += 1.0 / e_r1;
                    if (e_r2 > 0.0) inv += 1.0 / e_r2;
                    if (e_r3 > 0.0) inv += 1.0 / e_r3;
                    ShowResult2(T("Series R ="), e_r1 + e_r2 + e_r3, "Ohm", T("Parallel R ="), inv > 0.0 ? 1.0 / inv : 0.0, "Ohm");
                }

                Sub(T("Alternating current: reactance and impedance"));
                Formula("X_L = 2*pi*f*L        X_C = 1 / (2*pi*f*C)        Z = sqrt(R^2 + (X_L - X_C)^2)");
                Formula("cos φ = R / Z        f0 = 1 / (2*pi*sqrt(L*C))        T = 1 / f");
                Field(T("Frequency f, Hz:"), "##e_f", &e_f); Gap();
                Field(T("Resistance R (Ohm):"), "##e_R", &e_R); Gap();
                Field(T("Inductance L, mH:"), "##e_Lmh", &e_Lmh); Gap();
                Field(T("Capacitance C, uF:"), "##e_Cuf", &e_Cuf);
                {
                    const double Lh = e_Lmh / 1000.0, Cf = e_Cuf / 1e6;
                    const double xl = 2.0 * PI_D * e_f * Lh;
                    const double xc = (e_f > 0.0 && Cf > 0.0) ? 1.0 / (2.0 * PI_D * e_f * Cf) : 0.0;
                    const double z = sqrt(e_R * e_R + (xl - xc) * (xl - xc));
                    ShowResult2("X_L =", xl, "Ohm", "X_C =", xc, "Ohm");
                    ShowResult2("Z =", z, "Ohm", "cos φ =", z > 0.0 ? e_R / z : 0.0, "");
                    ShowResult2("f0 =", (Lh > 0.0 && Cf > 0.0) ? 1.0 / (2.0 * PI_D * sqrt(Lh * Cf)) : 0.0, "Hz",
                        "T =", e_f > 0.0 ? 1000.0 / e_f : 0.0, "ms");
                }

                Sub(T("Capacitor and RC circuit"));
                Formula("W = C * U^2 / 2        tau = R * C        charge to 95% ~ 3 * tau");
                Field(T("Voltage U (V):"), "##e_cU", &e_cU); Gap();
                Field(T("Resistance R, kOhm:"), "##e_cRk", &e_cRk); Gap();
                Field(T("Capacitance C, uF:"), "##e_cUf", &e_cUf);
                {
                    const double Cf = e_cUf / 1e6, R = e_cRk * 1000.0;
                    ShowResult2("W =", Cf * e_cU * e_cU / 2.0, "J", "tau =", R * Cf, "s");
                }

                Sub(T("Current density"));
                Formula("j = I / S");
                Field(T("Current I (A):"), "##e_jI", &e_jI); Gap();
                Field(T("Section, mm^2:"), "##e_jS", &e_jS);
                ShowResult("j =", e_jS > 0.0 ? e_jI / e_jS : 0.0, "A/mm^2");
                ImGui::Spacing();
            }

            // ---------- Мощность и ток ----------
            if (SectionHeader(T("Power and current (Cable, Load)"), &s_pw)) {
                Formula("1 ph:  I = P / (U * cos)        3 ph:  I = P / (sqrt(3) * U * cos)");
                Formula("S = P / cos        Q = S * sin = sqrt(S^2 - P^2)");
                ImGui::RadioButton(i18n::L("1 Phase", "f_ph1"), &f_ph, 1); ImGui::SameLine();
                ImGui::RadioButton(i18n::L("3 Phases", "f_ph3"), &f_ph, 3);
                Field(T("Power P, kW:"), "##f_P", &f_P); Gap();
                Field(T("Voltage, V:"), "##f_U", &f_U); Gap();
                Field(T("cos φ:"), "##f_cos", &f_cos);
                const double c = (f_cos > 0.0 && f_cos <= 1.0) ? f_cos : 1.0;
                const double den = (f_ph == 3 ? SQ3 : 1.0) * f_U * c;
                const double S_kva = f_P / c;
                ShowResult2("I =", den > 0.0 ? f_P * 1000.0 / den : 0.0, "A",
                    "S =", S_kva, "kVA");
                ShowResult("Q =", S_kva * sqrt((std::max)(0.0, 1.0 - c * c)), "kvar");
                ImGui::Spacing();
            }

            // ---------- Сечение, падение напряжения ----------
            if (SectionHeader(T("Cable: temperature and voltage drop"), &s_vd)) {
                Formula("k_t = sqrt( (t_max - t_amb) / (65 - t_ref) )      (PUE table 1.3.3)");
                Field(T("t_max of core, C:"), "##f_tmax", &f_tmax); Gap();
                Field(T("t ambient, C:"), "##f_tamb", &f_tamb); Gap();
                Field(T("t of table, C:"), "##f_tref", &f_tref);
                {
                    const double num = f_tmax - f_tamb, dn = 65.0 - f_tref;
                    ShowResult("k_t =", (num > 0.0 && dn > 0.0) ? sqrt(num / dn) : 0.0, "");
                }
                Note(T("t_max: PVC 65, XLPE 90, rubber 60. t of table: 25 in air, 15 in ground. Allowable current = table current * k_t."));
                ImGui::Separator();

                Formula("dU = k * I * L * (rho/S * cos + x0 * sin),   k = 2 (1 ph) or sqrt(3) (3 ph)");
                Formula("rho = rho20 * (1 + 0.004 * (65 - 20)),   x0 = 0.08 Ohm/km");
                Formula("L_max (5%) = 0.05 * U / (k * I * (rho/S * cos + x0 * sin))");
                ImGui::RadioButton(i18n::L("Copper", "f_cu"), &f_mat, 0); ImGui::SameLine();
                ImGui::RadioButton(i18n::L("Aluminum", "f_al"), &f_mat, 1); ImGui::SameLine(0.0f, 30.0f);
                ImGui::RadioButton(i18n::L("1 Phase", "f_vph1"), &f_ph, 1); ImGui::SameLine();
                ImGui::RadioButton(i18n::L("3 Phases", "f_vph3"), &f_ph, 3);
                Field(T("Current I (A):"), "##f_I", &f_I); Gap();
                Field(T("Length, m:"), "##f_L", &f_L); Gap();
                Field(T("Section, mm^2:"), "##f_S", &f_S); Gap();
                Field(T("cos φ:"), "##f_cos2", &f_cos);
                {
                    const double rho = (f_mat == 0 ? 0.0175 : 0.028) * 1.18;
                    const double c = (f_cos > 0.0 && f_cos <= 1.0) ? f_cos : 1.0;
                    const double sn = sqrt(1.0 - c * c);
                    const double k = (f_ph == 3) ? SQ3 : 2.0;
                    const double per_m = (f_S > 0.0) ? (rho / f_S * c + 0.00008 * sn) : 0.0;
                    const double du = k * f_I * f_L * per_m;
                    ShowResult2("dU =", du, "V", "dU% =", f_U > 0.0 ? du / f_U * 100.0 : 0.0, "%");
                    ShowResult("L_max =", (f_I > 0.0 && per_m > 0.0) ? 0.05 * f_U / (k * f_I * per_m) : 0.0, "m");
                }
                Note(T("Voltage U is taken from the 'Power and current' section above."));
                ImGui::Spacing();
            }

            // ---------- Ток КЗ ----------
            if (SectionHeader(T("Short circuit: phase-zero loop"), &s_kz)) {
                Formula("Ik = U_ph / (Zt/3 + Z_loop + R_contacts),   U_ph = U / sqrt(3) for 3 ph");
                Formula("Z_loop = sqrt( (2*L*rho/S)^2 + (2*L*x0)^2 )");
                Formula("Check: Ik >= k * In,   k = 5 (B), 10 (C), 20 (D)");
                ImGui::RadioButton(i18n::L("Copper", "f_kcu"), &f_matkz, 0); ImGui::SameLine();
                ImGui::RadioButton(i18n::L("Aluminum", "f_kal"), &f_matkz, 1); ImGui::SameLine(0.0f, 30.0f);
                ImGui::RadioButton("B##f_cb", &f_curve, 0); ImGui::SameLine();
                ImGui::RadioButton("C##f_cc", &f_curve, 1); ImGui::SameLine();
                ImGui::RadioButton("D##f_cd", &f_curve, 2);
                Field(T("Phase voltage U_ph, V:"), "##f_Ukz", &f_Ukz); Gap();
                Field(T("Length, m:"), "##f_Lkz", &f_Lkz); Gap();
                Field(T("Section, mm^2:"), "##f_Skz", &f_Skz); Gap();
                Field(T("Breaker In, A:"), "##f_In", &f_In);
                Field(T("Transformer Zt/3, Ohm:"), "##f_Zt", &f_Zt); Gap();
                Field(T("Contacts, Ohm:"), "##f_Rk", &f_Rk);
                {
                    const double rho = (f_matkz == 0 ? 0.0175 : 0.028) * 1.18;
                    const double r = (f_Skz > 0.0) ? 2.0 * f_Lkz * rho / f_Skz : 0.0;
                    const double x = 2.0 * f_Lkz * 0.00008;
                    const double zl = sqrt(r * r + x * x);
                    const double z = f_Zt + zl + f_Rk;
                    const double ik = (z > 0.0) ? f_Ukz / z : 0.0;
                    const double kc = (f_curve == 0) ? 5.0 : (f_curve == 2 ? 20.0 : 10.0);
                    ShowResult2("Z_loop =", zl, "Ohm", "Ik =", ik, "A");
                    const bool ok = ik >= kc * f_In;
                    ImGui::TextColored(g_theme.text_dim, "%s", T("Ik check:"));
                    ImGui::SameLine();
                    ImGui::TextColored(ok ? g_theme.res_good : g_theme.res_bad,
                        "%s (%.0f A >= %.0f A)", ok ? T("OK") : T("FAIL"), ik, kc * f_In);
                }
                Note(T("Zt/3 for Y/Yn transformers: 100 kVA 0.26; 160 kVA 0.162; 250 kVA 0.104; 400 kVA 0.065; 630 kVA 0.043; 1000 kVA 0.027 Ohm."));
                ImGui::Spacing();
            }

            // ---------- Заземление ----------
            if (SectionHeader(T("Grounding: vertical rods"), &s_gr)) {
                Formula("R1 = rho / (2*pi*L) * ( ln(2L/d) + 0.5 * ln((4T + L) / (4T - L)) ),   T = t + L/2");
                Formula("R = R1 / (n * eta)");
                Field(T("Soil Resistivity, Ohm*m:"), "##f_rho", &f_rho); Gap();
                Field(T("Rod Length, m:"), "##f_Lg", &f_Lg); Gap();
                Field(T("Rod diameter d, mm:"), "##f_dg", &f_dg); Gap();
                Field(T("Depth of rod top, m:"), "##f_tg", &f_tg);
                Field(T("Number of Rods:"), "##f_ng", &f_ng); Gap();
                Field(T("Utilization factor:"), "##f_eta", &f_eta);
                {
                    const double L = (f_Lg > 0.01) ? f_Lg : 0.01;
                    const double d = (f_dg > 0.1 ? f_dg : 0.1) / 1000.0;
                    const double Tm = f_tg + L / 2.0;
                    const double r1 = f_rho / (2.0 * 3.14159265358979 * L) *
                        (log(2.0 * L / d) + 0.5 * log((4.0 * Tm + L) / (4.0 * Tm - L)));
                    const double n = (f_ng >= 1.0) ? f_ng : 1.0;
                    const double eta = (f_eta > 0.0 && f_eta <= 1.0) ? f_eta : 1.0;
                    ShowResult2("R1 =", r1, "Ohm", "R =", r1 / (n * eta), "Ohm");
                }
                Note(T("Angle 50x50: d = 0.95 * 50 = 47.5 mm. Utilization factor: 0.5-0.95, see the Grounding tab or lecture 8."));
                ImGui::Spacing();
            }

            // ---------- Двигатель ----------
            if (SectionHeader(T("Motor"), &s_mo)) {
                Formula("P1 = P2 / eff        I = P1 / (sqrt(3) * U * cos)        P2 = sqrt(3) * U * I * cos * eff");
                Formula("Ist = k * I     Star-Delta: Ist / 3     Breaker: In >= 1.25 * I,  1.2 * Ist <= 5 In (C) / 10 In (D)");
                Field(T("Shaft power, kW:"), "##f_P2", &f_P2); Gap();
                Field(T("Efficiency (0.5-1.0):"), "##f_eff", &f_eff); Gap();
                Field(T("cos φ:"), "##f_cosm", &f_cosm); Gap();
                Field(T("Voltage, V:"), "##f_Um", &f_Um);
                Field(T("Start ratio (Ist/In):"), "##f_km", &f_km);
                {
                    const double eff = (f_eff > 0.01) ? f_eff : 0.01;
                    const double p1 = f_P2 / eff;
                    const double den = SQ3 * f_Um * f_cosm;
                    const double i = (den > 0.0) ? p1 * 1000.0 / den : 0.0;
                    ShowResult2("P1 =", p1, "kW", "I =", i, "A");
                    ShowResult2("Ist =", f_km * i, "A", "Ist (Y/D) =", f_km * i / 3.0, "A");
                }
                ImGui::Spacing();
            }

            // ---------- Энергия и единицы ----------
            if (SectionHeader(T("Energy and units"), &s_en)) {
                Formula("W = P * t        S = pi * d^2 / 4        S_AWG = 0.012668 * 92^((36 - n) / 19.5)        F = C * 9/5 + 32");
                Field(T("Power P, kW:"), "##f_Pw", &f_Pw); Gap();
                Field(T("Hours per day:"), "##f_hours", &f_hours); Gap();
                Field(T("Core diameter d, mm:"), "##f_d", &f_d); Gap();
                Field(T("AWG number:"), "##f_awg", &f_awg);
                Field(T("Temperature, C:"), "##f_tc", &f_tc);
                ShowResult2("W =", f_Pw * f_hours, "kWh", "S(d) =", 3.14159265358979 * f_d * f_d / 4.0, "mm^2");
                ShowResult2("S(AWG) =", 0.012668 * pow(92.0, (36.0 - f_awg) / 19.5), "mm^2", "F =", f_tc * 9.0 / 5.0 + 32.0, "F");
                ImGui::Spacing();
            }

            // ================= АЛГЕБРА =================
            if (SectionHeader(T("Algebra"), &s_al)) {
                Sub(T("Quadratic"));
                Formula("a*x^2 + b*x + c = 0        D = b^2 - 4ac        x = (-b +- sqrt(D)) / 2a");
                Field("a:", "##qd_a", &qd_a); Gap();
                Field("b:", "##qd_b", &qd_b); Gap();
                Field("c:", "##qd_c", &qd_c);
                {
                    const double qa = qd_a, qb = qd_b, qc = qd_c;
                    const double D = qb * qb - 4.0 * qa * qc;
                    ShowResult("D =", D, "");
                    if (qa == 0.0) {
                        if (qb != 0.0) ShowResult("x =", -qc / qb, "");
                    }
                    else if (D < 0.0) {
                        ImGui::TextColored(g_theme.res_bad, "%s", T("No real roots"));
                    }
                    else if (D == 0.0) {
                        ShowResult("x =", -qb / (2.0 * qa), "");
                    }
                    else {
                        ShowResult2("x1 =", (-qb + sqrt(D)) / (2.0 * qa), "",
                            "x2 =", (-qb - sqrt(D)) / (2.0 * qa), "");
                    }
                }

                Sub(T("Linear equation"));
                Formula("a*x + b = 0        x = -b / a");
                Field("a:", "##a_la", &a_la); Gap();
                Field("b:", "##a_lb", &a_lb);
                if (a_la != 0.0) ShowResult("x =", -a_lb / a_la, "");
                else ImGui::TextColored(g_theme.res_bad, "%s", T("a must not be 0"));

                Sub(T("System of two equations (Cramer's rule)"));
                Formula("a1*x + b1*y = c1,  a2*x + b2*y = c2        D = a1*b2 - a2*b1,  x = Dx/D,  y = Dy/D");
                Field("a1:", "##a_a1", &a_a1); Gap();
                Field("b1:", "##a_b1", &a_b1); Gap();
                Field("c1:", "##a_c1", &a_c1);
                Field("a2:", "##a_a2", &a_a2); Gap();
                Field("b2:", "##a_b2", &a_b2); Gap();
                Field("c2:", "##a_c2", &a_c2);
                {
                    const double D = a_a1 * a_b2 - a_a2 * a_b1;
                    if (D != 0.0)
                        ShowResult2("x =", (a_c1 * a_b2 - a_c2 * a_b1) / D, "", "y =", (a_a1 * a_c2 - a_a2 * a_c1) / D, "");
                    else
                        ImGui::TextColored(g_theme.res_bad, "%s", T("D = 0: no single solution"));
                }

                Sub(T("Percentages"));
                Formula("x% of N = N * x / 100        change A -> B = (B - A) / A * 100%");
                Field("x, %:", "##a_px", &a_px); Gap();
                Field("N:", "##a_pn", &a_pn); Gap();
                Field("A:", "##a_pa", &a_pa); Gap();
                Field("B:", "##a_pb", &a_pb);
                ShowResult2(T("x% of N ="), a_pn * a_px / 100.0, "", T("Change ="), a_pa != 0.0 ? (a_pb - a_pa) / a_pa * 100.0 : 0.0, "%");

                Sub(T("Proportion"));
                Formula("a / b = c / x        x = b * c / a");
                Field("a:", "##a_ra", &a_ra); Gap();
                Field("b:", "##a_rb", &a_rb); Gap();
                Field("c:", "##a_rc", &a_rc);
                ShowResult("x =", a_ra != 0.0 ? a_rb * a_rc / a_ra : 0.0, "");

                Sub(T("Powers, roots, logarithms"));
                Formula("a^n        n-th root of a = a^(1/n)        log_b(x) = ln(x) / ln(b)");
                Field("a:", "##a_base", &a_base); Gap();
                Field("n:", "##a_exp", &a_exp); Gap();
                Field("x:", "##a_lx", &a_lx); Gap();
                Field(T("base b:"), "##a_lb2", &a_lb2);
                ShowResult2("a^n =", pow(a_base, a_exp), "", T("root ="), (a_exp != 0.0 && a_base >= 0.0) ? pow(a_base, 1.0 / a_exp) : 0.0, "");
                ShowResult2("log_b(x) =", (a_lx > 0.0 && a_lb2 > 0.0 && a_lb2 != 1.0) ? log(a_lx) / log(a_lb2) : 0.0, "",
                    "ln(x) =", a_lx > 0.0 ? log(a_lx) : 0.0, "");

                Sub(T("Progressions"));
                Formula("Arithmetic: a_n = a1 + (n-1)*d,  S_n = (a1 + a_n) * n / 2");
                Formula("Geometric: b_n = b1 * q^(n-1),  S_n = b1 * (q^n - 1) / (q - 1)");
                Field("a1:", "##ap_a1", &ap_a1); Gap();
                Field("d:", "##ap_d", &ap_d); Gap();
                Field("n:", "##ap_n", &ap_n);
                {
                    const double an = ap_a1 + (ap_n - 1.0) * ap_d;
                    ShowResult2("a_n =", an, "", "S_n =", (ap_a1 + an) * ap_n / 2.0, "");
                }
                Field("b1:", "##gp_b1", &gp_b1); Gap();
                Field("q:", "##gp_q", &gp_q); Gap();
                Field("n:", "##gp_n", &gp_n);
                {
                    const double bn = gp_b1 * pow(gp_q, gp_n - 1.0);
                    const double sn = (gp_q != 1.0) ? gp_b1 * (pow(gp_q, gp_n) - 1.0) / (gp_q - 1.0) : gp_b1 * gp_n;
                    ShowResult2("b_n =", bn, "", "S_n =", sn, "");
                }
                ImGui::Spacing();
            }

            // ================= ГЕОМЕТРИЯ =================
            if (SectionHeader(T("Geometry"), &s_ge)) {
                Sub(T("Right triangle (Pythagoras)"));
                Formula("c = sqrt(a^2 + b^2)        S = a * b / 2        sin A = a / c");
                Field(T("Leg a:"), "##py_a", &geo_a); Gap();
                Field(T("Leg b:"), "##py_b", &geo_b);
                {
                    const double c = sqrt(geo_a * geo_a + geo_b * geo_b);
                    ShowResult2("c =", c, "", "S =", geo_a * geo_b / 2.0, "");
                    ShowResult2(T("angle A ="), c > 0.0 ? asin(geo_a / c) * 180.0 / PI_D : 0.0, "deg",
                        T("angle B ="), c > 0.0 ? asin(geo_b / c) * 180.0 / PI_D : 0.0, "deg");
                }

                Sub(T("Any triangle"));
                Formula("S = base * h / 2        Heron: p = (a+b+c)/2,  S = sqrt(p(p-a)(p-b)(p-c))");
                Field(T("base:"), "##g_tbase", &g_tbase); Gap();
                Field(T("height h:"), "##g_th", &g_th);
                ShowResult("S =", g_tbase * g_th / 2.0, "");
                Field("a:", "##g_ta", &g_ta); Gap();
                Field("b:", "##g_tb", &g_tb); Gap();
                Field("c:", "##g_tc", &g_tc);
                {
                    const double pp = (g_ta + g_tb + g_tc) / 2.0;
                    const double h2 = pp * (pp - g_ta) * (pp - g_tb) * (pp - g_tc);
                    if (h2 > 0.0) ShowResult2(T("Perimeter ="), 2.0 * pp, "", "S =", sqrt(h2), "");
                    else ImGui::TextColored(g_theme.res_bad, "%s", T("Such a triangle does not exist"));
                }

                Sub(T("Law of cosines and law of sines"));
                Formula("c^2 = a^2 + b^2 - 2ab*cos C        a / sin A = b / sin B");
                Field("a:", "##cl_a", &cl_a); Gap();
                Field("b:", "##cl_b", &cl_b); Gap();
                Field(T("angle C, deg:"), "##cl_C", &cl_C);
                ShowResult("c =", sqrt((std::max)(0.0, cl_a * cl_a + cl_b * cl_b - 2.0 * cl_a * cl_b * cos(cl_C * PI_D / 180.0))), "");
                Field("a:", "##sl_a", &sl_a); Gap();
                Field(T("angle A, deg:"), "##sl_A", &sl_A); Gap();
                Field(T("angle B, deg:"), "##sl_B", &sl_B);
                {
                    const double sa = sin(sl_A * PI_D / 180.0);
                    ShowResult("b =", sa != 0.0 ? sl_a * sin(sl_B * PI_D / 180.0) / sa : 0.0, "");
                }

                Sub(T("Circle and sector"));
                Formula("C = 2*pi*R        S = pi*R^2        arc = pi*R*alpha/180        S_sector = pi*R^2*alpha/360");
                Field(T("Radius R:"), "##ci_R", &geo_R); Gap();
                Field(T("angle alpha, deg:"), "##g_secA", &g_secA);
                ShowResult2("C =", 2.0 * PI_D * geo_R, "", "S =", PI_D * geo_R * geo_R, "");
                ShowResult2(T("arc ="), PI_D * geo_R * g_secA / 180.0, "", T("S sector ="), PI_D * geo_R * geo_R * g_secA / 360.0, "");

                Sub(T("Rectangle and trapezoid"));
                Formula("S = a*b        P = 2(a+b)        d = sqrt(a^2 + b^2)        trapezoid: S = (a+b)/2 * h");
                Field(T("width a:"), "##g_rw", &g_rw); Gap();
                Field(T("height b:"), "##g_rh", &g_rh);
                ShowResult2("S =", g_rw * g_rh, "", "P =", 2.0 * (g_rw + g_rh), "");
                ShowResult(T("diagonal ="), sqrt(g_rw * g_rw + g_rh * g_rh), "");
                Field(T("base a:"), "##g_za", &g_za); Gap();
                Field(T("base b:"), "##g_zb", &g_zb); Gap();
                Field(T("height h:"), "##g_zh", &g_zh);
                ShowResult(T("S trapezoid ="), (g_za + g_zb) / 2.0 * g_zh, "");

                Sub(T("Solids: cylinder, cone, sphere"));
                Formula("Cylinder: V = pi*r^2*h,  S = 2*pi*r*(r+h)        Cone: V = pi*r^2*h/3        Sphere: V = 4/3*pi*r^3,  S = 4*pi*r^2");
                Field(T("radius r:"), "##g_cr", &g_cr); Gap();
                Field(T("height h:"), "##g_ch", &g_ch);
                ShowResult2(T("V cylinder ="), PI_D * g_cr * g_cr * g_ch, "", T("S cylinder ="), 2.0 * PI_D * g_cr * (g_cr + g_ch), "");
                ShowResult2(T("V cone ="), PI_D * g_cr * g_cr * g_ch / 3.0, "", T("V sphere ="), 4.0 / 3.0 * PI_D * g_cr * g_cr * g_cr, "");
                ShowResult(T("S sphere ="), 4.0 * PI_D * g_cr * g_cr, "");

                Sub(T("Angles"));
                Formula("rad = deg * pi / 180        sin, cos, tan");
                Field(T("angle, deg:"), "##g_deg", &g_deg);
                {
                    const double r = g_deg * PI_D / 180.0;
                    ShowResult2("rad =", r, "", "sin =", sin(r), "");
                    ShowResult2("cos =", cos(r), "", "tan =", fabs(cos(r)) > 1e-12 ? tan(r) : 0.0, "");
                }
                ImGui::Spacing();
            }

            // Пересчитываем высоту, только когда карточка видна
            if (!ImGui::GetCurrentWindow()->SkipItems) {
                const float h = ImGui::GetCursorPosY() + 14.0f;
                if (fabsf(h - s_formulas_h) > 2.0f) s_formulas_h = h;
            }
        } gui.end_group_box();
    }

    void RenderDateTab() {
        using i18n::T;
        using i18n::L;

        gui.group_box(T("DATE CALCULATOR"), ImVec2(CARD_W_FULL, 560)); {
            auto GetToday = [](int& y, int& m, int& d) {
                time_t t = time(nullptr);
                struct tm lt;
                localtime_s(&lt, &t);
                y = 1900 + lt.tm_year;
                m = lt.tm_mon + 1;
                d = lt.tm_mday;
                };

            auto DateRow = [&](const char* id, const char* label, int& y, int& m, int& d) {
                ImGui::PushID(id);
                ImGui::TextColored(g_theme.accent, "%s", T(label));

                ImGui::TextColored(g_theme.text_dim, "%s", T("Y:"));
                ImGui::SameLine();
                ImGui::PushItemWidth(90);
                NiceInputInt("##Y", &y, 0, 0);
                ImGui::PopItemWidth();
                ImGui::SameLine(0, 6);
                if (SmallOutlineButton("-##Y-")) y--;
                ImGui::SameLine(0, 2);
                if (SmallOutlineButton("+##Y+")) y++;

                ImGui::SameLine(0, 20);
                ImGui::TextColored(g_theme.text_dim, "%s", T("M:"));
                ImGui::SameLine();
                ImGui::PushItemWidth(60);
                NiceInputInt("##M", &m, 0, 0);
                ImGui::PopItemWidth();
                ImGui::SameLine(0, 6);
                if (SmallOutlineButton("-##M-")) m--;
                ImGui::SameLine(0, 2);
                if (SmallOutlineButton("+##M+")) m++;

                ImGui::SameLine(0, 20);
                ImGui::TextColored(g_theme.text_dim, "%s", T("D:"));
                ImGui::SameLine();
                ImGui::PushItemWidth(60);
                NiceInputInt("##D", &d, 0, 0);
                ImGui::PopItemWidth();
                ImGui::SameLine(0, 6);
                if (SmallOutlineButton("-##D-")) d--;
                ImGui::SameLine(0, 2);
                if (SmallOutlineButton("+##D+")) d++;

                if (m < 1) m = 12;
                if (m > 12) m = 1;
                if (y < 1900) y = 1900;
                if (y > 2200) y = 2200;
                const int dim = DateDaysInMonth(y, m);
                if (d < 1) d = dim;
                if (d > dim) d = 1;

                ImGui::TextColored(g_theme.text_dim, "%s", T("Selected:"));
                ImGui::SameLine();
                char date_str[32];
                snprintf(date_str, sizeof(date_str), "%04d-%02d-%02d", y, m, d);
                ImGui::TextColored(g_theme.text_main, "%s", date_str);

                ImGui::SameLine(0, 20);
                if (SmallOutlineButton(L("Today"))) GetToday(y, m, d);
                ImGui::SameLine(0, 4);
                if (SmallOutlineButton(L("+1d"))) {
                    int oy, om, od;
                    DateAddDays(y, m, d, 1, oy, om, od);
                    y = oy; m = om; d = od;
                }
                ImGui::SameLine(0, 4);
                if (SmallOutlineButton(L("+7d"))) {
                    int oy, om, od;
                    DateAddDays(y, m, d, 7, oy, om, od);
                    y = oy; m = om; d = od;
                }
                ImGui::PopID();
                };

            ImGui::TextColored(g_theme.text_dim, "%s", T("Mode:"));
            ImGui::RadioButton(L("Difference"), &calc_data::date_mode, 0); ImGui::SameLine();
            ImGui::RadioButton(L("Add days"), &calc_data::date_mode, 1); ImGui::SameLine();
            ImGui::RadioButton(L("Weekday"), &calc_data::date_mode, 2);

            ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

            if (calc_data::date_mode == 0 || calc_data::date_mode == 1) {
                DateRow("##d1", "Date 1", calc_data::date_y1, calc_data::date_m1, calc_data::date_d1);
            }

            if (calc_data::date_mode == 0) {
                ImGui::Spacing();
                DateRow("##d2", "Date 2", calc_data::date_y2, calc_data::date_m2, calc_data::date_d2);
            }

            if (calc_data::date_mode == 1) {
                ImGui::Spacing();
                ImGui::TextColored(g_theme.accent, "%s", T("Add days:"));
                ImGui::SameLine();
                ImGui::PushItemWidth(100);
                NiceInputInt("##add", &calc_data::date_add_days);
                ImGui::PopItemWidth();
                ImGui::SameLine(0, 20);
                if (SmallOutlineButton("+1"))    calc_data::date_add_days += 1;
                ImGui::SameLine();
                if (SmallOutlineButton("+7"))    calc_data::date_add_days += 7;
                ImGui::SameLine();
                if (SmallOutlineButton("+30"))   calc_data::date_add_days += 30;
                ImGui::SameLine();
                if (SmallOutlineButton(L("Reset"))) calc_data::date_add_days = 0;
            }

            ImGui::Spacing();
            if (OutlineButton(L("Calculate"), ImVec2(-1, 40))) {
                if (calc_data::date_mode == 0) {
                    const long long d1 = DateDaysSinceEpoch(calc_data::date_y1, calc_data::date_m1, calc_data::date_d1);
                    const long long d2 = DateDaysSinceEpoch(calc_data::date_y2, calc_data::date_m2, calc_data::date_d2);
                    const long long diff = d2 - d1;
                    const long long abs_diff = diff < 0 ? -diff : diff;
                    snprintf(calc_data::date_result, sizeof(calc_data::date_result),
                        "%lld %s (%.2f %s)", abs_diff, T("days"),
                        (double)abs_diff / 365.25, T("years"));
                }
                else if (calc_data::date_mode == 1) {
                    int oy, om, od;
                    DateAddDays(calc_data::date_y1, calc_data::date_m1, calc_data::date_d1,
                        calc_data::date_add_days, oy, om, od);
                    snprintf(calc_data::date_result, sizeof(calc_data::date_result),
                        "%04d-%02d-%02d", oy, om, od);
                }
                else {
                    const char* wd = DateWeekdayName(calc_data::date_y1, calc_data::date_m1, calc_data::date_d1);
                    snprintf(calc_data::date_result, sizeof(calc_data::date_result),
                        "%04d-%02d-%02d %s %s",
                        calc_data::date_y1, calc_data::date_m1, calc_data::date_d1,
                        T("is"), T(wd));
                }
                history::Add("Date", calc_data::date_result);
            }

            ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

            ImGui::TextColored(g_theme.text_main, "%s", T("Result:"));
            ImGui::SameLine();
            ImGui::TextColored(g_theme.accent, "%s", calc_data::date_result);
            ImGui::SameLine(0, 30);
            if (SmallOutlineButton(L("Copy", "copy_date"))) CopyToClipboard(calc_data::date_result);
        } gui.end_group_box();
    }

    void RenderHistoryTab() {
        using i18n::T;
        using i18n::L;

        gui.group_box(T("CALCULATION HISTORY"), ImVec2(CARD_W_FULL, 620)); {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.20f, 0.20f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.75f, 0.28f, 0.28f, 1.0f));
            if (OutlineButton(L("Clear History"), ImVec2(160, 32), btn_col::danger)) history::Clear();
            ImGui::PopStyleColor(2);
            ImGui::SameLine();
            if (OutlineButton(L("Copy All"), ImVec2(160, 32))) {
                std::string all;
                for (auto& e : history::g_entries) {
                    char ts[32];
                    struct tm lt;
                    localtime_s(&lt, &e.timestamp);
                    snprintf(ts, sizeof(ts), "%02d:%02d:%02d", lt.tm_hour, lt.tm_min, lt.tm_sec);
                    all += "[" + std::string(ts) + "] " + T(e.category.c_str()) + ": " + e.summary + "\n";
                }
                CopyToClipboard(all.c_str());
            }
            ImGui::SameLine();
            if (OutlineButton(L("Export CSV"), ImVec2(160, 32), btn_col::success)) {
                ExportHistoryToCSV();
            }
            ImGui::SameLine();
            ImGui::TextColored(g_theme.text_dim, "(%s %d)", T("entries:"), (int)history::g_entries.size());

            ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

            // Список занимает всё оставшееся место в карточке - нижняя рамка не обрезается
            ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 10.0f));
            ImGui::BeginChild("##hist_list", ImVec2(0, ImGui::GetContentRegionAvail().y - 2.0f), true);
            if (history::g_entries.empty())
                ImGui::TextColored(g_theme.text_dim, "%s", T("History is empty. Results appear here after calculations."));
            for (int i = (int)history::g_entries.size() - 1; i >= 0; --i) {
                const auto& e = history::g_entries[i];
                char ts[16];
                struct tm lt;
                localtime_s(&lt, &e.timestamp);
                snprintf(ts, sizeof(ts), "%02d:%02d:%02d", lt.tm_hour, lt.tm_min, lt.tm_sec);

                ImGui::TextColored(g_theme.text_dim, "[%s]", ts);
                ImGui::SameLine();
                ImGui::TextColored(g_theme.accent, "%s:", T(e.category.c_str()));
                ImGui::SameLine();
                ImGui::TextColored(g_theme.text_main, "%s", e.summary.c_str());
            }
            ImGui::EndChild();
            ImGui::PopStyleVar(2);
        } gui.end_group_box();
    }

    void RenderWindowControlTab() {
        using namespace win_control;
        using i18n::T;
        using i18n::L;

        gui.group_box(T("WINDOW LIST"), ImVec2(CARD_W_FULL, 460)); {
            ImGui::PushItemWidth(300);
            ImGui::InputTextWithHint("##search", T("Search by title..."), g_search, sizeof(g_search));
            ImGui::PopItemWidth();
            ImGui::SameLine();
            if (OutlineButton(L("Refresh"), ImVec2(110, 0))) RefreshWindowList();
            ImGui::SameLine();
            if (OutlineButton(L("Clear Target"), ImVec2(140, 0), btn_col::warning)) {
                g_target = nullptr; g_selected = -1;
                SetStatus("Target cleared");
            }
            ImGui::SameLine();
            NiceCheckbox(L("Thumbnails"), &show_thumbnails);

            ImGui::Spacing();
            ImGui::BeginChild("##winlist", ImVec2(0, 340), true);

            for (int i = 0; i < (int)g_windows.size(); ++i) {
                auto& w = g_windows[i];

                // ==== ПОИСК ====
                if (g_search[0] != '\0') {
                    std::string lt = w.title;
                    std::string le = w.exe_name;
                    std::string ls = g_search;
                    std::transform(lt.begin(), lt.end(), lt.begin(), ::tolower);
                    std::transform(le.begin(), le.end(), le.begin(), ::tolower);
                    std::transform(ls.begin(), ls.end(), ls.begin(), ::tolower);
                    const bool match = (lt.find(ls) != std::string::npos) ||
                        (le.find(ls) != std::string::npos);
                    if (!match) continue;
                }

                ImGui::PushID(i);

                // ==== СТРОКА 1: иконка + selectable ====
                const ImVec2 row_start = ImGui::GetCursorPos();
                const float row_height = 26.0f;

                int iw = 0, ih = 0;
                ID3D11ShaderResourceView* icon_srv = win_visuals::GetIcon(w.hwnd, &iw, &ih);

                if (icon_srv && iw > 0 && ih > 0) {
                    ImGui::SetCursorPos(ImVec2(row_start.x, row_start.y + 3));
                    ImGui::Image((ImTextureID)icon_srv, ImVec2(20, 20));
                }

                ImGui::SetCursorPos(ImVec2(row_start.x + 28, row_start.y));

                const bool sel = (g_selected == i);
                if (sel) ImGui::PushStyleColor(ImGuiCol_Text, g_theme.accent);

                char label[512];
                snprintf(label, sizeof(label), "%s  [%s]##win%d",
                    w.title.c_str(), w.cls.c_str(), i);

                if (ImGui::Selectable(label, sel, 0, ImVec2(0, row_height))) {
                    g_selected = i; g_target = w.hwnd;
                    SetStatus("Target selected");
                }
                if (sel) ImGui::PopStyleColor();

                // ==== СТРОКА 2: имя процесса ====
                ImGui::SetCursorPos(ImVec2(row_start.x + 28, row_start.y + row_height));
                ImGui::TextColored(g_theme.text_dim, "%s", w.exe_name.c_str());

                // ==== ПРЕВЬЮ ====
                if (show_thumbnails && sel) {
                    ImGui::SetCursorPos(ImVec2(row_start.x + 28, row_start.y + row_height + 22));

                    int tw = 0, th = 0;
                    ID3D11ShaderResourceView* thumb =
                        win_visuals::GetThumb(w.hwnd, &tw, &th, 2.0f);

                    if (thumb && tw > 0 && th > 0) {
                        float scale = 1.0f;
                        if (th > 180) scale = 180.0f / (float)th;
                        const ImVec2 draw_size((float)tw * scale, (float)th * scale);
                        ImGui::Image((ImTextureID)thumb, draw_size);
                    }
                    else {
                        ImGui::TextColored(g_theme.text_dim, "%s", T("(preview not available)"));
                    }
                }

                // ==== Сдвигаем курсор на следующую строку ====
                {
                    float used_h = row_height + 22.0f;
                    if (show_thumbnails && sel) used_h += 200.0f;
                    ImGui::SetCursorPos(ImVec2(row_start.x, row_start.y + used_h));
                }

                ImGui::Separator();
                ImGui::Spacing();

                ImGui::PopID();
            }
            ImGui::EndChild();

            ImGui::Spacing();
            if (g_target) ImGui::TextColored(g_theme.res_good,
                "%s HWND 0x%p", T("Target:"), (void*)g_target);
            else ImGui::TextColored(g_theme.text_dim, "%s", T("No target selected"));

            if (g_status_timer > 0.0f) {
                g_status_timer -= ImGui::GetIO().DeltaTime;
                ImGui::SameLine();
                ImGui::TextColored(g_theme.accent, "| %s", T(g_status));
            }
        } gui.end_group_box();

        ImGui::Spacing();

        gui.group_box(T("POSITION & SIZE"), ImVec2(CARD_W_FULL, 345)); {
            ImGui::TextColored(g_theme.text_dim, "%s", T("Position:"));
            ImGui::PushItemWidth(140);
            NiceInputInt("X##pos", &pos_x); ImGui::SameLine();
            NiceInputInt("Y##pos", &pos_y);
            ImGui::PopItemWidth();

            ImGui::Spacing();
            ImGui::TextColored(g_theme.text_dim, "%s", T("Size:"));
            ImGui::PushItemWidth(140);
            NiceInputInt("W##size", &size_w); ImGui::SameLine();
            NiceInputInt("H##size", &size_h);
            ImGui::PopItemWidth();

            ImGui::Spacing();
            if (OutlineButton(L("Apply Move"), ImVec2(170, 32))) { MoveTo(g_target, pos_x, pos_y); SetStatus("Moved"); }
            ImGui::SameLine();
            if (OutlineButton(L("Apply Resize"), ImVec2(170, 32))) { ResizeW(g_target, size_w, size_h); SetStatus("Resized"); }
            ImGui::SameLine();
            if (OutlineButton(L("Apply Both"), ImVec2(170, 32))) { MoveResize(g_target, pos_x, pos_y, size_w, size_h); SetStatus("Moved & resized"); }

            ImGui::Spacing();
            if (OutlineButton(L("Read Current Values"), ImVec2(200, 32))) {
                if (GetRectW(g_target, pos_x, pos_y, size_w, size_h)) SetStatus("Read from target");
                else SetStatus("Failed to read");
            }
            ImGui::SameLine();
            if (OutlineButton(L("Center on Screen"), ImVec2(200, 32))) {
                const int sw = ::GetSystemMetrics(SM_CXSCREEN);
                const int sh = ::GetSystemMetrics(SM_CYSCREEN);
                pos_x = (sw - size_w) / 2; pos_y = (sh - size_h) / 2;
                MoveTo(g_target, pos_x, pos_y);
                SetStatus("Centered");
            }
        } gui.end_group_box();

        ImGui::Spacing();

        gui.group_box(T("STATE & ACTIONS"), ImVec2(CARD_W_FULL, 300)); {
            if (OutlineButton(L("Minimize"), ImVec2(160, 32))) { Minimize(g_target); SetStatus("Minimized"); }
            ImGui::SameLine();
            if (OutlineButton(L("Maximize"), ImVec2(160, 32))) { Maximize(g_target); SetStatus("Maximized"); }
            ImGui::SameLine();
            if (OutlineButton(L("Restore"), ImVec2(160, 32))) { Restore(g_target);  SetStatus("Restored"); }

            ImGui::Spacing();
            if (OutlineButton(L("Hide"), ImVec2(160, 32), btn_col::warning)) { HideW(g_target); SetStatus("Hidden"); }
            ImGui::SameLine();
            if (OutlineButton(L("Show"), ImVec2(160, 32))) { ShowW(g_target); SetStatus("Shown"); }
            ImGui::SameLine();
            if (OutlineButton(L("Focus"), ImVec2(160, 32))) { FocusWindow(g_target); SetStatus("Focused"); }

            ImGui::Spacing();
            if (OutlineButton(L("Start Drag"), ImVec2(200, 32), btn_col::warning)) StartDrag(g_target);
            ImGui::SameLine();
            NiceCheckbox(L("Always on top"), &topmost);
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                SetTopmost(g_target, topmost);
                SetStatus(topmost ? "Topmost ON" : "Topmost OFF");
            }

            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.20f, 0.20f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.75f, 0.28f, 0.28f, 1.0f));
            if (OutlineButton(L("Close Window"), ImVec2(200, 32), btn_col::danger)) {
                if (RequestClose(g_target)) SetStatus("Close request sent");
            }
            ImGui::PopStyleColor(2);
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.75f, 0.10f, 0.10f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.90f, 0.15f, 0.15f, 1.0f));
            if (OutlineButton(L("Force Kill"), ImVec2(200, 32), btn_col::danger)) ForceKill(g_target);
            ImGui::PopStyleColor(2);
        } gui.end_group_box();
    }

    // === NEW: готовые темы - вся палитра считается от одного цвета акцента ===
    // Фон всегда тёмный и слегка окрашен в оттенок акцента - глаза не устают.
    inline ImVec4 HSV4(float h, float s, float v, float a = 1.0f) {
        ImVec4 c(0.0f, 0.0f, 0.0f, a);
        ImGui::ColorConvertHSVtoRGB(h, s, v, c.x, c.y, c.z);
        return c;
    }

    inline void ApplyThemePreset(const ImVec4& accent, float bg_sat) {
        float h = 0.0f, s = 0.0f, v = 0.0f;
        ImGui::ColorConvertRGBtoHSV(accent.x, accent.y, accent.z, h, s, v);

        g_theme.accent = accent;
        g_theme.icon_color = accent;
        g_theme.switch_on = accent;
        g_theme.radio_mark = accent;
        g_theme.radio_hover = ImVec4(accent.x * 0.6f, accent.y * 0.6f, accent.z * 0.6f, 0.60f);

        g_theme.window_bg = HSV4(h, bg_sat, 0.040f);
        g_theme.card_bg = HSV4(h, bg_sat * 0.90f, 0.090f);
        g_theme.frame_bg = HSV4(h, bg_sat * 0.75f, 0.145f);
        g_theme.switch_off = HSV4(h, bg_sat * 0.35f, 0.200f);
        g_theme.card_border = HSV4(h, (std::min)(s, 0.60f), 0.45f, 0.45f);

        g_theme.text_main = HSV4(h, 0.04f, 0.97f);
        g_theme.text_dim = HSV4(h, bg_sat * 0.20f, 0.58f);

        g_theme.scrollbar_idle = ImVec4(1.00f, 1.00f, 1.00f, 0.05f);
        g_theme.scrollbar_hovered = ImVec4(accent.x, accent.y, accent.z, 0.45f);
        g_theme.scrollbar_active = ImVec4(accent.x, accent.y, accent.z, 0.75f);
        ApplyThemeStyle();
    }

    // === NEW: выбор цвета в HSB (оттенок / насыщенность / яркость) ===
    inline bool ColorEditHSB(const char* key, ImVec4* col, bool alpha = false, float max_b = 1.0f) {
        using i18n::T;
        using i18n::L;
        static std::map<ImGuiID, float> s_hue;   // оттенок помним, даже когда S или B = 0
        bool changed = false;

        ImGui::PushID(key);
        const ImGuiID hid = ImGui::GetID("##hue");

        float h = 0.0f, s = 0.0f, v = 0.0f;
        ImGui::ColorConvertRGBtoHSV(col->x, col->y, col->z, h, s, v);
        if (s > 0.001f && v > 0.001f) s_hue[hid] = h;
        else {
            auto it = s_hue.find(hid);
            if (it != s_hue.end()) h = it->second;
        }

        // Образец цвета + название + значения H/S/B
        // Круглый образец цвета (чуть меньше кружка радиокнопки)
        {
            const float fh = ImGui::GetFrameHeight();
            const ImVec2 p = ImGui::GetCursorScreenPos();
            if (ImGui::InvisibleButton("##swatch", ImVec2(fh, fh)))
                ImGui::OpenPopup("##hsb_popup");
            const bool hov = ImGui::IsItemHovered();
            if (hov) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImVec2 c(p.x + fh * 0.5f, p.y + fh * 0.5f);
            const float r = 8.5f;
            const float t = AnimateTo(ImGui::GetItemID(), hov, 14.0f);

            if (alpha && col->w < 0.999f) {
                // Подложка для прозрачности: светлый круг, левая половина тёмная
                dl->AddCircleFilled(c, r, IM_COL32(200, 200, 200, 255), 32);
                dl->PathArcTo(c, r, IM_PI * 0.5f, IM_PI * 1.5f, 16);
                dl->PathFillConvex(IM_COL32(110, 110, 110, 255));
            }
            dl->AddCircleFilled(c, r, ImGui::ColorConvertFloat4ToU32(
                ImVec4(col->x, col->y, col->z, alpha ? col->w : 1.0f)), 32);
            dl->AddCircle(c, r, IM_COL32(255, 255, 255, 40), 32, 1.0f);   // тонкая обводка
            if (t > 0.01f) {
                const ImVec4& a = g_theme.accent;
                dl->AddCircle(c, r + 3.0f, ImGui::ColorConvertFloat4ToU32(
                    ImVec4(a.x, a.y, a.z, 0.8f * t)), 32, 1.5f);          // кольцо при наведении
            }
        }
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(T(key));
        ImGui::SameLine(280.0f);
        ImGui::TextColored(g_theme.text_dim, "H %.0f   S %.0f%%   B %.0f%%",
            h * 360.0f, s * 100.0f, v * 100.0f);
        if (alpha) {
            ImGui::SameLine();
            ImGui::TextColored(g_theme.text_dim, "  A %.0f%%", col->w * 100.0f);
        }

        // Всплывающая палитра
        if (ImGui::BeginPopup("##hsb_popup")) {
            ImGuiColorEditFlags pf = ImGuiColorEditFlags_PickerHueBar |
                ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoSidePreview;
            pf |= alpha ? ImGuiColorEditFlags_AlphaBar : ImGuiColorEditFlags_NoAlpha;
            ImGui::SetNextItemWidth(240.0f);
            if (ImGui::ColorPicker4("##picker", (float*)col, pf)) {
                changed = true;
                if (max_b < 1.0f) ClampBrightness(*col, max_b);   // фон не ярче предела
            }

            ImGui::Separator();

            // Пересчитываем HSB после палитры
            float ph = 0.0f, ps = 0.0f, pv = 0.0f;
            ImGui::ColorConvertRGBtoHSV(col->x, col->y, col->z, ph, ps, pv);
            if (ps > 0.001f && pv > 0.001f) h = ph;
            s = ps; v = pv;

            float H = h * 360.0f, S = s * 100.0f, B = v * 100.0f;
            bool e = false;
            ImGui::SetNextItemWidth(200.0f);
            e |= ImGui::SliderFloat(L("Hue", "hsb_h"), &H, 0.0f, 360.0f, "%.0f");
            ImGui::SetNextItemWidth(200.0f);
            e |= ImGui::SliderFloat(L("Saturation", "hsb_s"), &S, 0.0f, 100.0f, "%.0f%%");
            ImGui::SetNextItemWidth(200.0f);
            e |= ImGui::SliderFloat(L("Brightness", "hsb_b"), &B, 0.0f, max_b * 100.0f, "%.0f%%");
            if (B > max_b * 100.0f) B = max_b * 100.0f;
            if (e) {
                if (H >= 360.0f) H = 359.9f;
                ImGui::ColorConvertHSVtoRGB(H / 360.0f, S / 100.0f, B / 100.0f, col->x, col->y, col->z);
                s_hue[hid] = H / 360.0f;
                changed = true;
            }

            if (alpha) {
                float A = col->w * 100.0f;
                ImGui::SetNextItemWidth(200.0f);
                if (ImGui::SliderFloat(L("Opacity", "hsb_a"), &A, 0.0f, 100.0f, "%.0f%%")) {
                    col->w = A / 100.0f;
                    changed = true;
                }
            }
            ImGui::EndPopup();
        }

        ImGui::PopID();
        return changed;
    }

    void RenderSettingsTab() {
        using i18n::T;
        using i18n::L;

        gui.group_box(T("THEME PRESETS"), ImVec2(CARD_W_FULL, 190)); {
            ImGui::TextColored(g_theme.text_dim, "%s",
                T("Pick a theme, then fine-tune colors below if you like."));
            ImGui::Spacing();

            struct Preset { const char* name; ImVec4 accent; float bg_sat; };
            static const Preset presets[] = {
                { "Ocean",    ImVec4(0.30f, 0.56f, 1.00f, 1.0f), 0.62f },
                { "Emerald",  ImVec4(0.25f, 0.85f, 0.55f, 1.0f), 0.50f },
                { "Amethyst", ImVec4(0.64f, 0.46f, 1.00f, 1.0f), 0.55f },
                { "Crimson",  ImVec4(1.00f, 0.38f, 0.45f, 1.0f), 0.50f },
                { "Amber",    ImVec4(1.00f, 0.70f, 0.28f, 1.0f), 0.45f },
                { "Arctic",   ImVec4(0.35f, 0.85f, 0.95f, 1.0f), 0.55f },
                { "Sakura",   ImVec4(1.00f, 0.55f, 0.76f, 1.0f), 0.40f },
                { "Graphite", ImVec4(0.80f, 0.82f, 0.86f, 1.0f), 0.10f },
            };
            const float bw = (ImGui::GetContentRegionAvail().x - 3.0f * 12.0f) / 4.0f;
            for (int i = 0; i < IM_ARRAYSIZE(presets); ++i) {
                if (i % 4 != 0) ImGui::SameLine(0.0f, 12.0f);
                const Preset& p = presets[i];
                char lbl[96];
                snprintf(lbl, sizeof(lbl), "%s###preset_%d", T(p.name), i);
                if (OutlineButton(lbl, ImVec2(bw, 36.0f), p.accent))
                    ApplyThemePreset(p.accent, p.bg_sat);
            }
        } gui.end_group_box();

        ImGui::Spacing();

        static float s_colors_h = 700.0f;   // высота по содержимому
        gui.group_box(T("THEME COLORS"), ImVec2(CARD_W_FULL, s_colors_h)); {
            ColorEditHSB("Accent", &g_theme.accent);
            ColorEditHSB("Icon", &g_theme.icon_color);
            ColorEditHSB("Text Main", &g_theme.text_main);
            ColorEditHSB("Text Dim", &g_theme.text_dim);
            ColorEditHSB("Window BG", &g_theme.window_bg, false, MAX_B_WINDOW);
            ColorEditHSB("Cards BG", &g_theme.card_bg, false, MAX_B_CARD);
            ColorEditHSB("Card Border", &g_theme.card_border, true);
            ColorEditHSB("Switch OFF", &g_theme.switch_off, false, MAX_B_SWOFF);
            ColorEditHSB("Switch ON", &g_theme.switch_on);

            ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

            // === Радиокнопки / чекбоксы ===
            ColorEditHSB("Radio / checkbox dot", &g_theme.radio_mark);
            ColorEditHSB("Radio / checkbox hover", &g_theme.radio_hover, true);
            ColorEditHSB("Fields & radio background", &g_theme.frame_bg, false, MAX_B_FIELD);

            ImGui::Spacing();
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(g_theme.text_dim, "%s", T("Preview:"));
            ImGui::SameLine();
            {
                static int  s_demo_radio = 0;
                static bool s_demo_check = true;
                ImGui::RadioButton(L("Option 1", "demo_r0"), &s_demo_radio, 0); ImGui::SameLine();
                ImGui::RadioButton(L("Option 2", "demo_r1"), &s_demo_radio, 1); ImGui::SameLine();
                NiceCheckbox(L("Checkbox", "demo_c"), &s_demo_check);
            }

            ImGui::Spacing();
            ImGui::TextColored(g_theme.text_dim, "%s", T("Switch preview (OFF / ON):"));
            {
                ImDrawList* dl = ImGui::GetWindowDrawList();
                const ImVec2 p = ImGui::GetCursorScreenPos();
                dl->AddRectFilled(p, ImVec2(p.x + 46, p.y + 24),
                    ImGui::ColorConvertFloat4ToU32(g_theme.switch_off), 12);
                dl->AddRectFilled(ImVec2(p.x + 56, p.y), ImVec2(p.x + 102, p.y + 24),
                    ImGui::ColorConvertFloat4ToU32(g_theme.switch_on), 12);
                ImGui::Dummy(ImVec2(110, 24));
            }

            ImGui::Spacing();
            LabeledSlider(T("Card Rounding"), &g_theme.card_rounding, 0.0f, 16.0f, "%.0f");

            // Пересчитываем высоту, только когда карточка видна
            // (скрытая за краем карточка не рисует содержимое и "сжимается")
            if (!ImGui::GetCurrentWindow()->SkipItems) {
                const float h = ImGui::GetCursorPosY() + 14.0f;
                if (fabsf(h - s_colors_h) > 2.0f) s_colors_h = h;
            }
        } gui.end_group_box();

        ImGui::Spacing();

        gui.group_box(T("SCROLLBAR"), ImVec2(CARD_W_FULL, 240)); {
            ColorEditHSB("Idle", &g_theme.scrollbar_idle, true);
            ColorEditHSB("Hovered", &g_theme.scrollbar_hovered, true);
            ColorEditHSB("Active", &g_theme.scrollbar_active, true);
            LabeledSlider(T("Width"), &g_theme.scrollbar_width, 4.0f, 20.0f, "%.0f");
        } gui.end_group_box();

        ImGui::Spacing();

        gui.group_box(T("RESULT COLORS"), ImVec2(CARD_W_FULL, 300)); {
            ColorEditHSB("Results / OK", &g_theme.res_good);
            ColorEditHSB("Errors / FAIL", &g_theme.res_bad);
            ColorEditHSB("Intermediate values, notes", &g_theme.res_warn);
            ColorEditHSB("Info values", &g_theme.res_info);

            ImGui::Spacing();
            ImGui::TextColored(g_theme.text_dim, "%s", T("Preview:"));
            ImGui::TextColored(g_theme.text_dim, "I ="); ImGui::SameLine();
            ImGui::TextColored(g_theme.res_good, "15000 J"); ImGui::SameLine(0.0f, 30.0f);
            ImGui::TextColored(g_theme.res_good, "%s", T("OK")); ImGui::SameLine(0.0f, 30.0f);
            ImGui::TextColored(g_theme.res_bad, "%s", T("FAIL")); ImGui::SameLine(0.0f, 30.0f);
            ImGui::TextColored(g_theme.res_warn, "2.35 %%"); ImGui::SameLine(0.0f, 30.0f);
            ImGui::TextColored(g_theme.res_info, "%s", T("by heating"));
        } gui.end_group_box();

        ImGui::Spacing();

        gui.group_box(T("BUTTONS"), ImVec2(CARD_W_FULL, 320)); {
            ColorEditHSB("Danger (delete, reset)", &g_theme.btn_danger);
            ColorEditHSB("Success (save, export)", &g_theme.btn_success);
            ColorEditHSB("Warning (careful actions)", &g_theme.btn_warning);
            ToggleSwitch(T("Glow on hover"), &g_theme.button_glow);

            ImGui::Spacing();
            ImGui::TextColored(g_theme.text_dim, "%s", T("Preview:"));
            OutlineButton(L("Accent", "demo_b0"), ImVec2(150, 32));
            ImGui::SameLine();
            OutlineButton(L("Danger", "demo_b1"), ImVec2(150, 32), btn_col::danger);
            ImGui::SameLine();
            OutlineButton(L("Success", "demo_b2"), ImVec2(150, 32), btn_col::success);
            ImGui::SameLine();
            OutlineButton(L("Warning", "demo_b3"), ImVec2(150, 32), btn_col::warning);
        } gui.end_group_box();

        ImGui::Spacing();

        gui.group_box(T("ANIMATION"), ImVec2(CARD_W_FULL, 326)); {
            ToggleSwitch(T("Intro animation on start"), &g_theme.intro_animation);
            LabeledSlider(T("Duration (s)"), &g_theme.intro_duration, 0.1f, 1.0f, "%.2f");
            LabeledSlider(T("Start scale"), &g_theme.intro_scale_min, 0.5f, 1.0f, "%.2f");
            ImGui::Spacing();
            ToggleSwitch(T("Minimize/restore animation"), &g_theme.minimize_animation);
            ToggleSwitch(T("Crumble to dust on close"), &g_theme.close_animation);
            ToggleSwitch(T("Animated background"), &g_theme.bg_animated);
        } gui.end_group_box();

        ImGui::Spacing();

        gui.group_box(T("CONFIG"), ImVec2(CARD_W_FULL, 180)); {
            ImGui::TextColored(g_theme.text_dim, "%s", T("Settings file: settings.ini (next to .exe)"));
            ImGui::Spacing();

            if (OutlineButton(L("Save Settings"), ImVec2(200, 36), btn_col::success)) {
                config::Save();
            }
            ImGui::SameLine();
            if (OutlineButton(L("Load Settings"), ImVec2(200, 36))) {
                config::Load();
                ApplyThemeStyle();
            }
            ImGui::SameLine();
            if (OutlineButton(L("Reset Theme to Defaults"), ImVec2(220, 36), btn_col::danger)) {
                g_theme.accent = ImVec4(0.30f, 0.49f, 1.00f, 1.00f);
                g_theme.icon_color = ImVec4(0.30f, 0.49f, 1.00f, 1.00f);
                g_theme.text_main = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
                g_theme.text_dim = ImVec4(0.51f, 0.52f, 0.56f, 1.00f);
                g_theme.window_bg = ImVec4(0.012f, 0.020f, 0.038f, 1.00f);
                g_theme.card_bg = ImVec4(0.035f, 0.055f, 0.095f, 1.00f);
                g_theme.card_border = ImVec4(0.10f, 0.15f, 0.25f, 0.60f);
                g_theme.switch_off = ImVec4(0.15f, 0.15f, 0.18f, 1.00f);
                g_theme.switch_on = ImVec4(0.30f, 0.49f, 1.00f, 1.00f);
                g_theme.radio_mark = ImVec4(0.30f, 0.49f, 1.00f, 1.00f);
                g_theme.radio_hover = ImVec4(0.18f, 0.29f, 0.60f, 0.60f);
                g_theme.frame_bg = ImVec4(0.080f, 0.100f, 0.150f, 1.00f);
                g_theme.btn_danger = ImVec4(1.00f, 0.36f, 0.36f, 1.00f);
                g_theme.btn_success = ImVec4(0.35f, 0.85f, 0.50f, 1.00f);
                g_theme.btn_warning = ImVec4(1.00f, 0.72f, 0.30f, 1.00f);
                g_theme.button_glow = true;
                g_theme.scrollbar_idle = ImVec4(1.00f, 1.00f, 1.00f, 0.05f);
                g_theme.scrollbar_hovered = ImVec4(0.50f, 0.50f, 0.50f, 0.65f);
                g_theme.scrollbar_active = ImVec4(0.70f, 0.70f, 0.70f, 0.90f);
                g_theme.scrollbar_width = 8.0f;
                g_theme.card_rounding = 10.0f;
                g_theme.intro_animation = true;
                g_theme.intro_duration = 0.30f;
                g_theme.intro_scale_min = 0.92f;
                g_theme.minimize_animation = true;
                g_theme.close_animation = true;
                g_theme.bg_animated = true;
                g_theme.res_good = ImVec4(0.40f, 1.00f, 0.40f, 1.00f);
                g_theme.res_bad = ImVec4(1.00f, 0.40f, 0.40f, 1.00f);
                g_theme.res_warn = ImVec4(1.00f, 0.80f, 0.40f, 1.00f);
                g_theme.res_info = ImVec4(0.60f, 0.85f, 1.00f, 1.00f);
                ApplyThemeStyle();
            }
        } gui.end_group_box();

        ImGui::Spacing();

        gui.group_box(T("OPTIONS"), ImVec2(CARD_W_FULL, 210)); {
            ToggleSwitch(T("Auto-Recalculate Values"), &calc_data::use_auto_calc);
            ToggleSwitch(T("Show clock in sidebar"), &g_theme.show_clock);

            ImGui::Spacing();
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(g_theme.text_dim, "%s", T("Language:"));
            ImGui::SameLine();
            int lang_int = (int)i18n::g_lang;
            if (ImGui::RadioButton("English###lang_en", &lang_int, 0)) i18n::g_lang = i18n::LANG_EN;
            ImGui::SameLine();
            if (ImGui::RadioButton("Русский###lang_ru", &lang_int, 1)) i18n::g_lang = i18n::LANG_RU;
        } gui.end_group_box();
    }

    // === NEW: файлы лекций из папки "lectures" рядом с .exe ===
    namespace lectures {
        static std::vector<std::wstring> g_files;   // имена файлов без пути
        static bool g_scanned = false;
        static const char* g_status = "";           // английский ключ, переводится при выводе

        inline std::wstring Dir() {
            wchar_t path[MAX_PATH] = {};
            ::GetModuleFileNameW(nullptr, path, MAX_PATH);
            std::wstring p(path);
            const size_t slash = p.find_last_of(L"\\/");
            if (slash != std::wstring::npos) p.resize(slash + 1);
            return p + L"lectures\\";
        }

        inline bool HasExt(const std::wstring& name, const wchar_t* ext) {
            const size_t n = wcslen(ext);
            if (name.size() < n) return false;
            return _wcsicmp(name.c_str() + name.size() - n, ext) == 0;
        }

        inline void Scan() {
            g_files.clear();
            g_scanned = true;
            WIN32_FIND_DATAW fd = {};
            HANDLE h = ::FindFirstFileW((Dir() + L"*").c_str(), &fd);
            if (h == INVALID_HANDLE_VALUE) return;
            do {
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
                const std::wstring name(fd.cFileName);
                if (name.size() > 2 && name[0] == L'~' && name[1] == L'$') continue;  // временные файлы Word
                if (HasExt(name, L".pdf") || HasExt(name, L".docx") ||
                    HasExt(name, L".doc") || HasExt(name, L".odt"))
                    g_files.push_back(name);
            } while (::FindNextFileW(h, &fd));
            ::FindClose(h);
            std::sort(g_files.begin(), g_files.end());
        }

        inline void Open(const std::wstring& full_path) {
            const INT_PTR r = (INT_PTR)::ShellExecuteW(nullptr, L"open", full_path.c_str(),
                nullptr, nullptr, SW_SHOWNORMAL);
            if (r == SE_ERR_NOASSOC) {
                // Нет программы для файла -> системное окно "Открыть с помощью"
                ::ShellExecuteW(nullptr, L"openas", full_path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                g_status = "No app for this file type";
            }
            else if (r <= 32) {
                g_status = "Failed to open file";
            }
            else {
                g_status = "";
            }
        }
    }   // namespace lectures

    // === NEW: Help — инструкция + ссылки ===
    void RenderHelpTab() {
        using i18n::T;
        using i18n::L;

        static bool s_howto = true;
        static bool s_links = true;
        static bool s_files = true;

        if (!lectures::g_scanned) lectures::Scan();
        // Высота = реальная высота содержимого с прошлого кадра (без пустоты внизу)
        static float s_help_h = 2600.0f;
        gui.group_box(T("HELP & RESOURCES"), ImVec2(CARD_W_FULL, s_help_h)); {

            // ==================== КАК ПОЛЬЗОВАТЬСЯ ====================
            if (SectionHeader(T("How to use"), &s_howto)) {
                ImGui::TextColored(g_theme.accent, "%s", T("General workflow"));
                ImGui::TextColored(g_theme.text_dim, "%s", T("1. Choose a calculator in the left sidebar."));
                ImGui::TextColored(g_theme.text_dim, "%s", T("2. Enter parameters in the left card."));
                ImGui::TextColored(g_theme.text_dim, "%s", T("3. The result appears on the right (with Auto-Recalculate on)."));
                ImGui::TextColored(g_theme.text_dim, "%s", T("4. Press 'Save to History' or 'Calculate' to store the result."));
                ImGui::TextColored(g_theme.text_dim, "%s", T("5. Export saved results to CSV in the History tab."));
                ImGui::TextColored(g_theme.text_dim, "%s", T("6. Switch language with the EN | RU switch in the sidebar."));
                ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

                // Заголовок таба (акцент) + 1..3 строки описания (dim)
                auto TabHelp = [](const char* tab, const char* l1, const char* l2, const char* l3) {
                    ImGui::TextColored(g_theme.accent, "%s", T(tab));
                    ImGui::PushTextWrapPos(0.0f);
                    ImGui::TextColored(g_theme.text_dim, "%s", T(l1));
                    if (l2) ImGui::TextColored(g_theme.text_dim, "%s", T(l2));
                    if (l3) ImGui::TextColored(g_theme.text_dim, "%s", T(l3));
                    ImGui::PopTextWrapPos();
                    ImGui::Spacing();
                    };

                TabHelp("Math Calc",
                    "Scientific calculator: + - * /, power, root, sin/cos/tan, log, ln, factorial.",
                    nullptr, nullptr);
                TabHelp("Cable Size",
                    "Cable section by power, voltage, cos φ and length; material, installation, insulation, ambient temp.",
                    "Manual section: fix a specific section and check it against the required one.",
                    "Ik check (short-circuit current) and a max-length table for 5% voltage drop.");
                TabHelp("Grid Load",
                    "Total load: current and daily energy consumption, kWh.",
                    nullptr, nullptr);
                TabHelp("Breaker",
                    "Breaker rating by load current with a safety margin.",
                    "Curves B / C / D with recommendations for the load type.",
                    nullptr);
                TabHelp("Grounding",
                    "Earthing resistance by soil resistivity, rod length and number of rods.",
                    nullptr, nullptr);
                TabHelp("Motor",
                    "Motor current: Forward (power -> current) and Reverse (current -> power).",
                    "Start methods, breaker / contactor / thermal relay selection, Star / Delta diagram.",
                    nullptr);
                TabHelp("Formulas",
                    "Physics and math: Ohm's law, power, Joule's law, quadratic equation, geometry.",
                    nullptr, nullptr);
                TabHelp("Date Calc",
                    "Difference between dates, adding days, day of the week.",
                    nullptr, nullptr);
                TabHelp("Converter",
                    "Unit conversion: AWG <-> mm2, HP <-> W, C <-> F, m <-> ft, kg <-> lb, time.",
                    nullptr, nullptr);
                TabHelp("Window Ctrl",
                    "Control other windows: move, resize, minimize, hide, close, always on top.",
                    "Select a window in the list - a preview is shown under it.",
                    nullptr);
                TabHelp("Reference",
                    "Wire colors, IP codes, overvoltage categories, ANSI/IEC symbols, AWG, ampacity.",
                    nullptr, nullptr);
                TabHelp("History",
                    "Log of all saved calculations; copy to clipboard or export to CSV.",
                    nullptr, nullptr);
                TabHelp("Settings",
                    "Theme colors, scrollbar, animations, config file, auto-recalculate, language.",
                    nullptr, nullptr);
            }

            ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

            // ==================== ЛЕКЦИИ И МАТЕРИАЛЫ ====================
            if (SectionHeader(T("Lectures & references"), &s_links)) {
                ImGui::TextColored(g_theme.text_dim, "%s", T("Click a link to open in browser:"));
                ImGui::TextColored(g_theme.text_dim, "%s", T("Links lead to Russian-language materials."));
                ImGui::Spacing();

                // key — английский ключ словаря; он же стабильный ID кнопки
                auto LinkBtn = [](const char* key, const wchar_t* url) {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.25f, 0.45f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.40f, 0.70f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.30f, 0.50f, 0.85f, 1.0f));
                    if (OutlineButton(L(key), ImVec2(-1, 32))) {
                        ::ShellExecuteW(nullptr, L"open", url, nullptr, nullptr, SW_SHOWNORMAL);
                    }
                    if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                    ImGui::PopStyleColor(3);
                    ImGui::Spacing();
                    };

                LinkBtn("Ohm's law - Wikipedia",
                    L"https://ru.wikipedia.org/wiki/%D0%97%D0%B0%D0%BA%D0%BE%D0%BD_%D0%9E%D0%BC%D0%B0");
                LinkBtn("Electric power - Wikipedia",
                    L"https://ru.wikipedia.org/wiki/%D0%AD%D0%BB%D0%B5%D0%BA%D1%82%D1%80%D0%B8%D1%87%D0%B5%D1%81%D0%BA%D0%B0%D1%8F_%D0%BC%D0%BE%D1%89%D0%BD%D0%BE%D1%81%D1%82%D1%8C");
                LinkBtn("Alternating current - Wikipedia",
                    L"https://ru.wikipedia.org/wiki/%D0%9F%D0%B5%D1%80%D0%B5%D0%BC%D0%B5%D0%BD%D0%BD%D1%8B%D0%B9_%D1%82%D0%BE%D0%BA");
                LinkBtn("Circuit breaker - Wikipedia",
                    L"https://ru.wikipedia.org/wiki/%D0%90%D0%B2%D1%82%D0%BE%D0%BC%D0%B0%D1%82%D0%B8%D1%87%D0%B5%D1%81%D0%BA%D0%B8%D0%B9_%D0%B2%D1%8B%D0%BA%D0%BB%D1%8E%D1%87%D0%B0%D1%82%D0%B5%D0%BB%D1%8C");
                LinkBtn("Electric motor - Wikipedia",
                    L"https://ru.wikipedia.org/wiki/%D0%AD%D0%BB%D0%B5%D0%BA%D1%82%D1%80%D0%BE%D0%B4%D0%B2%D0%B8%D0%B3%D0%B0%D1%82%D0%B5%D0%BB%D1%8C");
                LinkBtn("Grounding - Wikipedia",
                    L"https://ru.wikipedia.org/wiki/%D0%97%D0%B0%D0%B7%D0%B5%D0%BC%D0%BB%D0%B5%D0%BD%D0%B8%D0%B5");
                LinkBtn("PUE, ch. 1.1: General part (docs.cntd.ru)",
                    L"https://docs.cntd.ru/document/1200030216");
                LinkBtn("PUE, ch. 1.7: Earthing and protective measures (docs.cntd.ru)",
                    L"https://docs.cntd.ru/document/1200030218");
                LinkBtn("GOST R 50571.1-2009 (IEC 60364-1) (docs.cntd.ru)",
                    L"https://docs.cntd.ru/document/1200073895");
            }

            ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

            // ==================== ЛЕКЦИИ (БЕЗ ИНТЕРНЕТА) ====================
            if (SectionHeader(T("Lecture files (offline)"), &s_files)) {
                ImGui::TextColored(g_theme.text_dim, "%s",
                    T("Files from the 'lectures' folder next to the program:"));
                ImGui::Spacing();

                if (OutlineButton(L("Open folder"), ImVec2(220, 32), btn_col::success)) {
                    const std::wstring dir = lectures::Dir();
                    ::CreateDirectoryW(dir.c_str(), nullptr);   // нет папки - создаём
                    ::ShellExecuteW(nullptr, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                }
                ImGui::SameLine();
                if (OutlineButton(L("Refresh", "lec_refresh"), ImVec2(220, 32))) lectures::Scan();

                ImGui::Spacing();
                if (lectures::g_files.empty()) {
                    ImGui::TextColored(g_theme.text_dim, "%s",
                        T("No files found. Put .pdf / .docx into the 'lectures' folder."));
                }
                else {
                    for (int i = 0; i < (int)lectures::g_files.size(); ++i) {
                        const std::string name = win_control::ToUtf8(lectures::g_files[i].c_str());
                        char label[320];
                        snprintf(label, sizeof(label), ICON_FA_BOOK "  %s##lecfile", name.c_str());

                        ImGui::PushID(i);
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.30f, 0.22f, 1.0f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.18f, 0.45f, 0.32f, 1.0f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.22f, 0.55f, 0.40f, 1.0f));
                        if (OutlineButton(label, ImVec2(-1, 32), btn_col::success))
                            lectures::Open(lectures::Dir() + lectures::g_files[i]);
                        if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                        ImGui::PopStyleColor(3);
                        ImGui::PopID();
                        ImGui::Spacing();
                    }
                }

                if (lectures::g_status[0] != '\0')
                    ImGui::TextColored(g_theme.res_bad, "%s", T(lectures::g_status));
            }

            // Запоминаем, где закончилось содержимое (+ нижний отступ карточки)
            // Пересчитываем высоту, только когда карточка видна
            if (!ImGui::GetCurrentWindow()->SkipItems) {
                const float h = ImGui::GetCursorPosY() + 14.0f;
                if (fabsf(h - s_help_h) > 2.0f) s_help_h = h;   // не дёргаем из-за долей пикселя
            }
        } gui.end_group_box();
    }

    // === NEW: компактный переключатель языка EN | RU для сайдбара ===
    inline void LangSwitch() {
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        if (window->SkipItems) return;

        const ImVec2 pos = window->DC.CursorPos;
        const ImVec2 sz(150.0f, 28.0f);
        const ImRect bb(pos, ImVec2(pos.x + sz.x, pos.y + sz.y));
        const ImGuiID id = window->GetID("##lang_switch");
        ImGui::ItemSize(bb, 0.0f);
        if (!ImGui::ItemAdd(bb, id)) return;

        bool hovered = false, held = false;
        const bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held,
            ImGuiButtonFlags_MouseButtonLeft);
        if (pressed) {
            // Клик по левой половине -> EN, по правой -> RU
            const bool right_half = ImGui::GetIO().MousePos.x >= bb.GetCenter().x;
            i18n::g_lang = right_half ? i18n::LANG_RU : i18n::LANG_EN;
        }

        const bool is_ru = (i18n::g_lang == i18n::LANG_RU);
        const float t = AnimateTo(id, is_ru, 14.0f);   // 0 = EN, 1 = RU

        ImDrawList* dl = window->DrawList;
        const float rounding = sz.y * 0.5f;

        // Фон
        const float lift = hovered ? 0.03f : 0.0f;
        const ImU32 bg_col = ImGui::ColorConvertFloat4ToU32(ImVec4(
            g_theme.card_bg.x + 0.045f + lift,
            g_theme.card_bg.y + 0.045f + lift,
            g_theme.card_bg.z + 0.055f + lift, 1.0f));
        dl->AddRectFilled(bb.Min, bb.Max, bg_col, rounding);
        dl->AddRect(bb.Min, bb.Max,
            ImGui::ColorConvertFloat4ToU32(g_theme.card_border), rounding);

        // Подсветка активной половины (плавно едет)
        const float pad = 3.0f;
        const float half_w = sz.x * 0.5f;
        const float knob_x0 = bb.Min.x + pad + t * (half_w - pad);
        const ImVec2 k_min(knob_x0, bb.Min.y + pad);
        const ImVec2 k_max(knob_x0 + half_w - pad, bb.Max.y - pad);
        const ImVec4& a = g_theme.accent;
        dl->AddRectFilled(k_min, k_max,
            ImGui::ColorConvertFloat4ToU32(ImVec4(a.x, a.y, a.z, 0.18f)), rounding - pad);
        dl->AddRect(k_min, k_max,
            ImGui::ColorConvertFloat4ToU32(ImVec4(a.x, a.y, a.z, 0.85f)), rounding - pad, 0, 1.3f);

        // Подписи
        const ImU32 on_col = ImGui::ColorConvertFloat4ToU32(a);
        const ImU32 off_col = ImGui::ColorConvertFloat4ToU32(g_theme.text_dim);

        const char* en = "EN";
        const char* ru = "RU";
        const ImVec2 en_sz = ImGui::CalcTextSize(en);
        const ImVec2 ru_sz = ImGui::CalcTextSize(ru);
        const float cy = bb.GetCenter().y;
        const float left_cx = bb.Min.x + half_w * 0.5f;
        const float right_cx = bb.Min.x + half_w * 1.5f;

        dl->AddText(ImVec2(left_cx - en_sz.x * 0.5f, cy - en_sz.y * 0.5f),
            is_ru ? off_col : on_col, en);
        dl->AddText(ImVec2(right_cx - ru_sz.x * 0.5f, cy - ru_sz.y * 0.5f),
            is_ru ? on_col : off_col, ru);

        if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }

    void render() noexcept {
        if (!globals::menu_opened) return;
        if (!g_imgui_ready) return;
        if (ImGui::GetCurrentContext() == nullptr) return;

        ApplyThemeStyle();

        ImGuiIO& io = ImGui::GetIO();

        if (g_win_anim != WinAnim::None) {
            g_win_anim_t += io.DeltaTime / WIN_ANIM_DUR;
            if (g_win_anim == WinAnim::Minimizing && g_win_anim_t >= 1.0f) {
                g_win_anim_t = 1.0f;
                ::ShowWindow(g_hwnd, SW_MINIMIZE);
                g_win_anim = WinAnim::None;
                g_win_anim_t = 0.0f;
                return;
            }
            if (g_win_anim == WinAnim::Restoring && g_win_anim_t >= 1.0f) {
                g_win_anim_t = 1.0f;
                g_win_anim = WinAnim::None;
                g_win_anim_t = 0.0f;
            }
        }

        float win_anim_alpha = 1.0f;
        if (g_win_anim == WinAnim::Minimizing)
            win_anim_alpha = 1.0f - ImClamp(g_win_anim_t, 0.0f, 1.0f);
        else if (g_win_anim == WinAnim::Restoring)
            win_anim_alpha = ImClamp(g_win_anim_t, 0.0f, 1.0f);

        float intro_t = 1.0f;
        if (g_theme.intro_animation) {
            if (g_intro_anim_time < 0.0f) g_intro_anim_time = 0.0f;
            g_intro_anim_time += io.DeltaTime;
            intro_t = ImClamp(g_intro_anim_time / g_theme.intro_duration, 0.0f, 1.0f);
        }
        float intro_eased = 1.0f - powf(1.0f - intro_t, 3.0f);
        float intro_alpha = intro_eased;
        float intro_scale = g_theme.intro_scale_min + (1.0f - g_theme.intro_scale_min) * intro_eased;

        float total_alpha = intro_alpha * win_anim_alpha;
        float total_scale = intro_scale;

        ImVec2 full_size = io.DisplaySize;
        ImVec2 win_size = ImVec2(full_size.x * total_scale, full_size.y * total_scale);
        ImVec2 win_pos = ImVec2((full_size.x - win_size.x) * 0.5f,
            (full_size.y - win_size.y) * 0.5f);

        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * total_alpha);
        ImGui::SetNextWindowPos(win_pos);
        ImGui::SetNextWindowSize(win_size);

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse
            | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize
            | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus
            | ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoScrollbar
            | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings
            | ImGuiWindowFlags_NoBackground;

        if (ImGui::Begin("ElectroCalcRoot", nullptr, flags)) {
            ImVec2 size = ImGui::GetWindowSize();
            ImVec2 base_pos = ImGui::GetWindowPos();

            {
                ImDrawList* bg = ImGui::GetBackgroundDrawList();
                // fade: 0 = окно погашено (чёрное), 1 = горит полностью.
                // Фон затухает вместе с содержимым при сворачивании и разгорается при открытии.
                const float fade = total_alpha;
                const ImVec4& wb = g_theme.window_bg;
                ImU32 top = ImGui::ColorConvertFloat4ToU32(ImVec4(wb.x, wb.y, wb.z, fade));
                ImU32 bot = ImGui::ColorConvertFloat4ToU32(ImVec4(
                    wb.x * 0.6f, wb.y * 0.6f, wb.z * 0.6f, fade));
                bg->AddRectFilledMultiColor(base_pos, ImVec2(base_pos.x + size.x, base_pos.y + size.y),
                    top, top, bot, bot);

                // === NEW: фон - сетка точек с плавно плывущими "огоньками" ===
                DrawDotGridBackground(bg, base_pos, size, fade);

                // Полоса заголовка - сплошная, без точек, чтобы название читалось
                bg->AddRectFilled(base_pos, ImVec2(base_pos.x + size.x, base_pos.y + 44.0f),
                    ImGui::ColorConvertFloat4ToU32(ImVec4(wb.x, wb.y, wb.z, fade)));

                const ImVec4& a = g_theme.accent;
                // Линия под заголовком: ярче в центре, тает к краям
                const float ly = base_pos.y + 44.0f;
                const float cx = base_pos.x + size.x * 0.5f;
                const ImU32 c_mid = ImGui::ColorConvertFloat4ToU32(ImVec4(a.x, a.y, a.z, 0.45f * fade));
                const ImU32 c_end = ImGui::ColorConvertFloat4ToU32(ImVec4(a.x, a.y, a.z, 0.0f));
                bg->AddRectFilledMultiColor(ImVec2(base_pos.x + 20.0f, ly), ImVec2(cx, ly + 1.0f), c_end, c_mid, c_mid, c_end);
                bg->AddRectFilledMultiColor(ImVec2(cx, ly), ImVec2(base_pos.x + size.x - 20.0f, ly + 1.0f), c_mid, c_end, c_end, c_mid);
            }

            {
                const float header_h = 60.0f;
                const float buttons_w = 150.0f;

                ImGui::SetCursorPos(ImVec2(0, 0));
                ImGui::InvisibleButton("##titlebar", ImVec2(size.x - buttons_w, header_h));
                if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);

                if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                    // Само перетаскивание запускается после кадра (см. главный цикл),
                    // чтобы во время перетаскивания окно продолжало перерисовываться.
                    if (!g_in_move) g_pending_drag = true;
                }

                // Название: иконка + "ElectroGuiCalc" цветом акцента + "by iknlm" приглушённо
                {
                    const char* icon = ICON_FA_BOLT;
                    const char* name = "ElectroGuiCalc";
                    const char* author = "by iknlm";
                    const float gap = 7.0f;
                    const ImVec2 si = ImGui::CalcTextSize(icon);
                    const ImVec2 sn = ImGui::CalcTextSize(name);
                    const ImVec2 sa = ImGui::CalcTextSize(author);
                    const float total = si.x + gap + sn.x + gap + sa.x;
                    float x = base_pos.x + size.x * 0.5f - total * 0.5f;
                    const float y = base_pos.y + 22.0f - sn.y * 0.5f;   // по центру полосы заголовка
                    ImDrawList* tdl = ImGui::GetWindowDrawList();
                    tdl->AddText(ImVec2(x, y), ImGui::ColorConvertFloat4ToU32(gui.accent_color), icon);
                    x += si.x + gap;
                    tdl->AddText(ImVec2(x, y), ImGui::ColorConvertFloat4ToU32(g_theme.text_main), name);
                    x += sn.x + gap;
                    tdl->AddText(ImVec2(x, y), ImGui::ColorConvertFloat4ToU32(g_theme.text_dim), author);
                }

                float bx = size.x - 150.0f;
                ImGui::SetCursorPos(ImVec2(bx, 6));

                // Кнопки окна: обводка цветом акцента, закрыть - цветом "опасных" кнопок
                ImVec4 btn_bg = g_theme.accent;
                ImVec4 btn_hov = g_theme.accent;
                ImVec4 red_bg = g_theme.btn_danger;
                ImVec4 red_hov = g_theme.btn_danger;

                if (WinButton("##btn_min", "-", ImVec2(44, 28), btn_bg, btn_hov, 1.6f)) {
                    if (g_theme.minimize_animation && g_win_anim == WinAnim::None) {
                        g_win_anim = WinAnim::Minimizing;
                        g_win_anim_t = 0.0f;
                    }
                    else {
                        ::ShowWindow(g_hwnd, SW_MINIMIZE);
                    }
                }
                ImGui::SameLine(0, 6);

                if (WinButton("##btn_max", "+", ImVec2(44, 28), btn_bg, btn_hov, 1.6f)) {
                    ::ShowWindow(g_hwnd, g_maximized ? SW_RESTORE : SW_MAXIMIZE);
                    g_maximized = !g_maximized;
                }
                ImGui::SameLine(0, 6);

                if (WinButton("##btn_close", "x", ImVec2(44, 28), red_bg, red_hov, 1.6f)) {
                    config::Save();
                    ::PostMessageW(g_hwnd, WM_CLOSE, 0, 0);
                }
            }

            if (g_theme.show_clock) {
                {
                    time_t t = time(nullptr);
                    struct tm lt;
                    localtime_s(&lt, &t);
                    char date_buf[16];
                    snprintf(date_buf, sizeof(date_buf), "%04d-%02d-%02d",
                        1900 + lt.tm_year, lt.tm_mon + 1, lt.tm_mday);
                    ImGui::SetCursorPos(ImVec2(20, 52));
                    ImGui::TextColored(g_theme.text_dim, ICON_FA_CALENDAR "  %s", date_buf);
                }
                {
                    char time_buf[16];
                    GetTimeString(time_buf, sizeof(time_buf));
                    ImGui::SetCursorPos(ImVec2(20, 76));
                    ImGui::TextColored(g_theme.accent, ICON_FA_CLOCK "  %s", time_buf);
                }
            }

            ImGui::SetCursorPos(ImVec2(12, g_theme.show_clock ? 110 : 60));
            ImGui::BeginChild("##tabs", ImVec2(170, size.y - (g_theme.show_clock ? 115 : 65)), false,
                ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
            {
                gui.group_title(i18n::T("CALCULATORS"));
                if (gui.tab(ICON_FA_CALCULATOR, i18n::T("Math Calc"), gui.m_tab == 0) && gui.m_tab != 0) { gui.m_tab = 0; tab_switch_time = (float)ImGui::GetTime(); }
                if (gui.tab(ICON_FA_BOLT, i18n::T("Cable Size"), gui.m_tab == 1) && gui.m_tab != 1) { gui.m_tab = 1; tab_switch_time = (float)ImGui::GetTime(); }
                if (gui.tab(ICON_FA_PLUG, i18n::T("Grid Load"), gui.m_tab == 2) && gui.m_tab != 2) { gui.m_tab = 2; tab_switch_time = (float)ImGui::GetTime(); }
                if (gui.tab(ICON_FA_SHIELD_HALVED, i18n::T("Breaker"), gui.m_tab == 3) && gui.m_tab != 3) { gui.m_tab = 3; tab_switch_time = (float)ImGui::GetTime(); }
                if (gui.tab(ICON_FA_LEAF, i18n::T("Grounding"), gui.m_tab == 4) && gui.m_tab != 4) { gui.m_tab = 4; tab_switch_time = (float)ImGui::GetTime(); }
                if (gui.tab(ICON_FA_INDUSTRY, i18n::T("Motor"), gui.m_tab == 11) && gui.m_tab != 11) { gui.m_tab = 11; tab_switch_time = (float)ImGui::GetTime(); }
                if (gui.tab(ICON_FA_SQUARE_ROOT_VARIABLE, i18n::T("Formulas"), gui.m_tab == 8) && gui.m_tab != 8) { gui.m_tab = 8; tab_switch_time = (float)ImGui::GetTime(); }
                if (gui.tab(ICON_FA_CALENDAR, i18n::T("Date Calc"), gui.m_tab == 9) && gui.m_tab != 9) { gui.m_tab = 9; tab_switch_time = (float)ImGui::GetTime(); }

                ImGui::Spacing(); ImGui::Spacing();
                gui.group_title(i18n::T("TOOLS"));
                if (gui.tab(ICON_FA_RIGHT_LEFT, i18n::T("Converter"), gui.m_tab == 7) && gui.m_tab != 7) { gui.m_tab = 7; tab_switch_time = (float)ImGui::GetTime(); }
                if (gui.tab(ICON_FA_WINDOW_RESTORE, i18n::T("Window Ctrl"), gui.m_tab == 5) && gui.m_tab != 5) { gui.m_tab = 5; tab_switch_time = (float)ImGui::GetTime(); }
                if (gui.tab(ICON_FA_BOOK, i18n::T("Reference"), gui.m_tab == 12) && gui.m_tab != 12) { gui.m_tab = 12; tab_switch_time = (float)ImGui::GetTime(); }
                if (gui.tab(ICON_FA_LIST, i18n::T("History"), gui.m_tab == 10) && gui.m_tab != 10) { gui.m_tab = 10; tab_switch_time = (float)ImGui::GetTime(); }

                ImGui::Spacing(); ImGui::Spacing();
                gui.group_title(i18n::T("MISC"));
                if (gui.tab(ICON_FA_GEAR, i18n::T("Settings"), gui.m_tab == 6) && gui.m_tab != 6) { gui.m_tab = 6; tab_switch_time = (float)ImGui::GetTime(); }
                if (gui.tab(ICON_FA_CIRCLE_QUESTION, i18n::T("Help"), gui.m_tab == 13) && gui.m_tab != 13) { gui.m_tab = 13; tab_switch_time = (float)ImGui::GetTime(); }
                ImGui::Spacing();
                LangSwitch();
            }
            ImGui::EndChild();
            ImGui::SetCursorPos(ImVec2(200, 60));
            ImGui::BeginChild("##content", ImVec2(size.x - 220, size.y - 80), false,
                ImGuiWindowFlags_NoScrollWithMouse);
            {
                // === Плавная прокрутка колесом мыши ===
                {
                    static float s_target = 0.0f;   // куда едем
                    static float s_pos = 0.0f;      // где сейчас (дробное)
                    static int   s_last_tab = -1;
                    ImGuiWindow* cw = ImGui::GetCurrentWindow();
                    const float max_y = ImGui::GetScrollMaxY();
                    const float real_y = ImGui::GetScrollY();

                    // Новая вкладка всегда открывается сверху
                    if (s_last_tab != gui.m_tab) {
                        s_last_tab = gui.m_tab;
                        s_target = s_pos = 0.0f;
                        ImGui::SetScrollY(0.0f);
                    }
                    // Прокрутку сдвинул кто-то другой (страница стала короче,
                    // потянули полосу) - берём реальную позицию, без прыжков
                    else if (fabsf(real_y - s_pos) > 2.0f ||
                        ImGui::GetActiveID() == ImGui::GetWindowScrollbarID(cw, ImGuiAxis_Y)) {
                        s_pos = s_target = real_y;
                    }

                    // Колесо не трогаем, если курсор над списком со своим скроллом
                    ImGuiWindow* hw = GImGui->HoveredWindow;
                    const bool inner_scroll = hw && hw != cw && hw->ScrollMax.y > 0.0f &&
                        !(hw->Flags & ImGuiWindowFlags_NoScrollWithMouse);

                    const float wheel = ImGui::GetIO().MouseWheel;
                    if (wheel != 0.0f && !inner_scroll &&
                        ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows)) {
                        s_target -= wheel * 90.0f;
                        if (s_target < 0.0f)  s_target = 0.0f;
                        if (s_target > max_y) s_target = max_y;
                    }

                    if (s_pos != s_target) {
                        s_pos += (s_target - s_pos) * (std::min)(ImGui::GetIO().DeltaTime * 14.0f, 1.0f);
                        if (fabsf(s_target - s_pos) < 0.5f) s_pos = s_target;
                        ImGui::SetScrollY(s_pos);
                    }
                }

                BeginTabFade();
                switch (gui.m_tab) {
                case 0:  RenderMathTab();          break;
                case 1:  RenderCableTab();         break;
                case 2:  RenderLoadTab();          break;
                case 3:  RenderBreakerTab();       break;
                case 4:  RenderGroundTab();        break;
                case 5:  RenderWindowControlTab(); break;
                case 6:  RenderSettingsTab();      break;
                case 7:  RenderConverterTab();     break;
                case 8:  RenderFormulasTab();      break;
                case 9:  RenderDateTab();          break;
                case 10: RenderHistoryTab();       break;
                case 11: RenderMotorTab();         break;
                case 12: RenderReferenceTab();     break;
                case 13: RenderHelpTab();          break;
                }
                EndTabFade();
            }
            ImGui::EndChild();
        }
        ImGui::End();

        ImGui::PopStyleVar();
    }
}  // namespace menu

// ======================= ЗАКРЫТИЕ: РАССЫПАНИЕ В ПЫЛЬ =======================
// Как удаление сообщения в Telegram. По WM_CLOSE снимаем последний кадр окна,
// прячем окно и показываем картинку в прозрачном слое поверх рабочего стола.
// Картинка волной слева направо рассыпается на песчинки, они улетают вверх и гаснут.
namespace dust_fx {
    static bool                  s_request = false;   // снять кадр в ближайшем RenderFrame
    static bool                  s_ready = false;     // кадр снят, можно рассыпать
    static bool                  s_played = false;    // уже рассыпались - дальше закрываемся по-настоящему
    static std::vector<uint32_t> s_pixels;            // BGRA, как в DIB
    static int                   s_w = 0;
    static int                   s_h = 0;

    // Копия заднего буфера. Вызывать ДО Present - после него содержимое не определено.
    static void CaptureBackBuffer() {
        s_request = false;
        ID3D11Texture2D* back = nullptr;
        if (FAILED(g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&back))) || !back) { s_ready = true; return; }
        D3D11_TEXTURE2D_DESC desc;
        back->GetDesc(&desc);
        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        desc.MiscFlags = 0;
        ID3D11Texture2D* staging = nullptr;
        if (SUCCEEDED(g_pd3dDevice->CreateTexture2D(&desc, nullptr, &staging)) && staging) {
            g_pd3dDeviceContext->CopyResource(staging, back);
            D3D11_MAPPED_SUBRESOURCE m;
            if (SUCCEEDED(g_pd3dDeviceContext->Map(staging, 0, D3D11_MAP_READ, 0, &m))) {
                s_w = (int)desc.Width;
                s_h = (int)desc.Height;
                s_pixels.resize((size_t)s_w * (size_t)s_h);
                for (int y = 0; y < s_h; ++y) {
                    const uint8_t* src = (const uint8_t*)m.pData + (size_t)y * m.RowPitch;
                    uint32_t* dst = &s_pixels[(size_t)y * (size_t)s_w];
                    for (int x = 0; x < s_w; ++x) {
                        const uint8_t* px = src + (size_t)x * 4;   // RGBA -> BGRA
                        dst[x] = 0xFF000000u | ((uint32_t)px[0] << 16) | ((uint32_t)px[1] << 8) | (uint32_t)px[2];
                    }
                }
                g_pd3dDeviceContext->Unmap(staging, 0);
            }
            staging->Release();
        }
        back->Release();
        s_ready = true;   // даже если снять не вышло - просто закроемся без эффекта
    }

    struct Grain {
        float x, y;        // исходное место в окне
        float vx, vy;      // скорость, px/s
        float ax;          // боковой снос
        float delay;       // когда оторвётся
        float life;        // сколько летит
        uint32_t col;
    };

    static uint32_t s_rng = 0x9E3779B9u;
    static float Rand01() {
        s_rng ^= s_rng << 13; s_rng ^= s_rng >> 17; s_rng ^= s_rng << 5;
        return (float)(s_rng & 0xFFFFFFu) / 16777216.0f;
    }

    // точка внутри скруглённого прямоугольника (углы окна на Win10/11)
    static bool InsideRounded(int x, int y, int w, int h, int r) {
        if (r <= 0) return true;
        const int cx = x < r ? r : (x >= w - r ? w - r - 1 : x);
        const int cy = y < r ? r : (y >= h - r ? h - r - 1 : y);
        const int dx = x - cx, dy = y - cy;
        return dx * dx + dy * dy <= r * r;
    }

    static void Finish(HWND main) {
        s_pixels.clear();
        s_pixels.shrink_to_fit();
        s_played = true;
        ::PostMessageW(main, WM_CLOSE, 0, 0);
    }

    static void Play(HWND main) {
        s_ready = false;
        const int W = s_w, H = s_h;
        if (s_pixels.empty() || W <= 0 || H <= 0) { Finish(main); return; }

        RECT wr;
        ::GetWindowRect(main, &wr);

        // размер песчинки: чтобы их было не больше ~350 тысяч (иначе тормозит на 4K)
        const int cell = ImMax(2, (int)std::ceil(std::sqrt((double)W * (double)H / 350000.0)));
        const int corner = ::IsZoomed(main) ? 0 : 8;

        std::vector<Grain> grains;
        grains.reserve((size_t)(W / cell + 1) * (size_t)(H / cell + 1));
        float total = 0.0f;
        for (int y = 0; y < H; y += cell) {
            for (int x = 0; x < W; x += cell) {
                const int px = ImMin(x + cell / 2, W - 1);
                const int py = ImMin(y + cell / 2, H - 1);
                if (!InsideRounded(px, py, W, H, corner)) continue;
                Grain q;
                q.x = (float)x;
                q.y = (float)y;
                const float nx = (float)x / (float)W;
                const float ny = (float)y / (float)H;
                q.delay = nx * 0.45f + (1.0f - ny) * 0.08f + Rand01() * 0.15f;   // волна слева направо
                q.life = 0.45f + Rand01() * 0.40f;
                q.vx = -80.0f + Rand01() * 260.0f;
                q.vy = -(60.0f + Rand01() * 200.0f);
                q.ax = -80.0f + Rand01() * 160.0f;
                q.col = s_pixels[(size_t)py * (size_t)W + (size_t)px];
                grains.push_back(q);
                total = ImMax(total, q.delay + q.life);
            }
        }

        // слой больше окна - песчинкам есть куда лететь
        const int MS = 260, MT = 340, MB = 60;
        const int DW = W + MS * 2, DH = H + MT + MB;

        BITMAPINFO bi = {};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = DW;
        bi.bmiHeader.biHeight = -DH;   // сверху вниз
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;

        HDC screen = ::GetDC(nullptr);
        HDC mem = ::CreateCompatibleDC(screen);
        void* bits = nullptr;
        HBITMAP dib = ::CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (!dib || !bits) {
            if (dib) ::DeleteObject(dib);
            ::DeleteDC(mem);
            ::ReleaseDC(nullptr, screen);
            Finish(main);
            return;
        }
        HGDIOBJ old_bmp = ::SelectObject(mem, dib);

        HINSTANCE inst = ::GetModuleHandleW(nullptr);
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = ::DefWindowProcW;
        wc.hInstance = inst;
        wc.lpszClassName = L"ElectroCalcDust";
        ::RegisterClassExW(&wc);   // повторная регистрация просто вернёт ошибку - не страшно

        HWND dust = ::CreateWindowExW(
            WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
            wc.lpszClassName, L"", WS_POPUP,
            wr.left - MS, wr.top - MT, DW, DH, nullptr, nullptr, inst, nullptr);

        uint32_t* buf = (uint32_t*)bits;
        auto draw = [&](float t) {
            memset(buf, 0, (size_t)DW * (size_t)DH * 4);
            for (const Grain& q : grains) {
                const float lt = t - q.delay;
                float fx = q.x, fy = q.y;
                int s = cell;
                uint32_t a = 255;
                if (lt > 0.0f) {
                    if (lt >= q.life) continue;
                    const float k = lt / q.life;
                    fx += q.vx * lt + 0.5f * q.ax * lt * lt;
                    fy += q.vy * lt - 90.0f * lt * lt;   // ускоряются вверх
                    a = (uint32_t)(255.0f * (1.0f - k * k));
                    s = ImMax(1, (int)((float)cell * (1.0f - 0.5f * k) + 0.5f));
                    if (a == 0) continue;
                }
                const int ix = (int)fx + MS;
                const int iy = (int)fy + MT;
                if (ix < 0 || iy < 0 || ix + s > DW || iy + s > DH) continue;
                const uint32_t c = q.col;
                uint32_t src;
                if (a == 255) src = c;
                else {
                    const uint32_t r = ((c >> 16) & 0xFF) * a / 255;
                    const uint32_t g = ((c >> 8) & 0xFF) * a / 255;
                    const uint32_t b = (c & 0xFF) * a / 255;
                    src = (a << 24) | (r << 16) | (g << 8) | b;
                }
                const uint32_t inv = 255 - a;
                for (int yy = 0; yy < s; ++yy) {
                    uint32_t* row = buf + (size_t)(iy + yy) * (size_t)DW + (size_t)ix;
                    for (int xx = 0; xx < s; ++xx) {
                        if (inv == 0 || row[xx] == 0) { row[xx] = src; continue; }
                        const uint32_t d = row[xx];   // смешивание premultiplied: src + dst*(1-a)
                        const uint32_t da = ((d >> 24) & 0xFF) * inv / 255;
                        const uint32_t dr = ((d >> 16) & 0xFF) * inv / 255;
                        const uint32_t dg = ((d >> 8) & 0xFF) * inv / 255;
                        const uint32_t db = (d & 0xFF) * inv / 255;
                        row[xx] = src + ((da << 24) | (dr << 16) | (dg << 8) | db);
                    }
                }
            }
        };

        POINT pt_dst = { wr.left - MS, wr.top - MT };
        SIZE  sz = { DW, DH };
        POINT pt_src = { 0, 0 };
        BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
        auto present = [&]() {
            ::UpdateLayeredWindow(dust, screen, &pt_dst, &sz, mem, &pt_src, 0, &bf, ULW_ALPHA);
        };

        if (dust) {
            draw(0.0f);
            present();
            // слой ставим прямо над окном, чтобы не вылезти поверх чужих окон
            HWND above = ::GetWindow(main, GW_HWNDPREV);
            ::SetWindowPos(dust, above ? above : HWND_TOP, 0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
            ::ShowWindow(main, SW_HIDE);

            BOOL dwm_on = FALSE;
            ::DwmIsCompositionEnabled(&dwm_on);
            LARGE_INTEGER freq, t0, now;
            ::QueryPerformanceFrequency(&freq);
            ::QueryPerformanceCounter(&t0);
            for (;;) {
                MSG msg;
                while (::PeekMessageW(&msg, dust, 0, 0, PM_REMOVE)) {
                    ::TranslateMessage(&msg);
                    ::DispatchMessageW(&msg);
                }
                ::QueryPerformanceCounter(&now);
                const float t = (float)((double)(now.QuadPart - t0.QuadPart) / (double)freq.QuadPart);
                if (t >= total) break;
                draw(t);
                present();
                if (dwm_on) ::DwmFlush(); else ::Sleep(10);
            }
            ::DestroyWindow(dust);
        }

        ::SelectObject(mem, old_bmp);
        ::DeleteObject(dib);
        ::DeleteDC(mem);
        ::ReleaseDC(nullptr, screen);
        Finish(main);
    }
}  // namespace dust_fx

// ======================= WINMAIN =======================
// === NEW: один кадр целиком. Вызывается из главного цикла и по таймеру
// во время перетаскивания окна (Windows в это время крутит свой цикл сообщений).
static void RenderFrame() {
    if (!g_imgui_ready || g_in_frame) return;
    g_in_frame = true;

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    menu::render();

    ImGui::Render();
    const float cca[4] = { 0.0f, 0.0f, 0.0f, 1.0f };   // окно гаснет в чёрный
    g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
    g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, cca);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    if (dust_fx::s_request) dust_fx::CaptureBackBuffer();   // кадр для рассыпания - до Present
    g_pSwapChain->Present(1, 0);

    g_in_frame = false;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = WndProc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = hInstance;
    wc.hIcon = ::LoadIcon(nullptr, IDI_APPLICATION);
    wc.hCursor = ::LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)::GetStockObject(BLACK_BRUSH);   // было тёмно-синее - отсюда синие пятна
    wc.lpszMenuName = nullptr;
    wc.lpszClassName = L"ElectroCalcClass";
    wc.hIconSm = ::LoadIcon(nullptr, IDI_APPLICATION);

    if (!::RegisterClassExW(&wc)) {
        ::MessageBoxW(nullptr, L"RegisterClassExW failed", L"Error", MB_ICONERROR);
        return 1;
    }

    HWND hwnd = ::CreateWindowExW(
        0L, wc.lpszClassName, L"ElectroGuiCalc by iknlm",
        // WS_MINIMIZEBOX | WS_SYSMENU - чтобы клик по значку на панели задач сворачивал окно,
        // работали Win+Down и меню по правому клику на панели задач. Рамки при этом не появляется.
        WS_POPUP | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU, 100, 100, 1400, 900,
        nullptr, nullptr, hInstance, nullptr);

    if (!hwnd) {
        ::MessageBoxW(nullptr, L"CreateWindowExW failed", L"Error", MB_ICONERROR);
        ::UnregisterClassW(wc.lpszClassName, hInstance);
        return 1;
    }
    g_hwnd = hwnd;
    ApplyWindowCorners(hwnd);

    if (!CreateDeviceD3D(hwnd)) {
        CleanupDeviceD3D();
        ::DestroyWindow(hwnd);
        ::UnregisterClassW(wc.lpszClassName, hInstance);
        return 1;
    }

    ::ShowWindow(hwnd, nCmdShow);
    ::UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;

    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;

    g_imgui_ready = true;

    // латиница, греческие буквы (cos φ), кириллица
    static const ImWchar font_ranges[] = { 0x0020, 0x00FF, 0x0370, 0x03FF, 0x0400, 0x052F,
        0x2DE0, 0x2DFF, 0xA640, 0xA69F, 0 };
    io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 16.0f,
        nullptr, font_ranges);

    static const ImWchar icons_ranges[] = { ICON_MIN_FA, ICON_MAX_FA, 0 };
    ImFontConfig icons_config;
    icons_config.MergeMode = true;
    icons_config.PixelSnapH = true;
    icons_config.GlyphMinAdvanceX = 16.0f;

    io.Fonts->AddFontFromFileTTF(
        "fonts\\fa-solid-900.ttf",
        15.0f, &icons_config, icons_ranges
    );

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    config::Load();
    ApplyThemeStyle();

    win_control::RefreshWindowList();

    bool done = false;
    while (!done) {
        MSG msg;
        while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
            if (msg.message == WM_QUIT) done = true;
        }
        if (done) break;

        RenderFrame();

        if (dust_fx::s_ready) dust_fx::Play(g_hwnd);   // крутится ~1.5 с, потом снова WM_CLOSE

        // Перетаскивание окна за заголовок - строго вне кадра ImGui
        if (g_pending_drag) {
            g_pending_drag = false;
            ::ReleaseCapture();
            ::SendMessageW(g_hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);   // вернётся, когда отпустят мышь
            ResetImGuiInputState();
            g_pending_drag = false;   // кадры во время перетаскивания могли снова поставить флаг
        }
    }

    config::Save();
    win_visuals::ClearAll();

    g_imgui_ready = false;
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    CleanupDeviceD3D();
    ::DestroyWindow(hwnd);
    ::UnregisterClassW(wc.lpszClassName, hInstance);
    return 0;
}

// ======================= DIRECTX 11 HELPERS =======================
bool CreateDeviceD3D(HWND hWnd)
{
    DXGI_SWAP_CHAIN_DESC sd;
    ZeroMemory(&sd, sizeof(sd));
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT createDeviceFlags = 0;
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };

    HRESULT res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain,
        &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);

    if (res == DXGI_ERROR_UNSUPPORTED)
        res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
            createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain,
            &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);

    if (res != S_OK) return false;

    CreateRenderTarget();
    return true;
}

void CleanupDeviceD3D()
{
    CleanupRenderTarget();
    if (g_pSwapChain) { g_pSwapChain->Release();        g_pSwapChain = nullptr; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release();        g_pd3dDevice = nullptr; }
}

void CreateRenderTarget()
{
    ID3D11Texture2D* pBackBuffer = nullptr;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    if (pBackBuffer) {
        g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
        pBackBuffer->Release();
    }
}

void CleanupRenderTarget()
{
    if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (g_imgui_ready) {
        if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
            return true;
    }

    switch (msg) {
    case WM_SIZE:
        if (g_pd3dDevice != nullptr && wParam != SIZE_MINIMIZED) {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, (UINT)LOWORD(lParam), (UINT)HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
            CreateRenderTarget();
        }
        if (wParam != SIZE_MINIMIZED) ApplyWindowCorners(hWnd);   // обновить скругление под новый размер
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xFFF0) == SC_KEYMENU) return 0;
        // Сворачивание с панели задач / Win+Down - через нашу анимацию затухания
        if ((wParam & 0xFFF0) == SC_MINIMIZE && g_imgui_ready && g_theme.minimize_animation
            && !::IsIconic(hWnd)) {
            if (g_win_anim != WinAnim::Minimizing) {
                g_win_anim = WinAnim::Minimizing;
                g_win_anim_t = 0.0f;
            }
            return 0;   // окно свернётся само в конце анимации
        }
        break;
        // === NEW: пока окно тащат, Windows не отдаёт управление главному циклу -
        // рисуем кадры по таймеру, чтобы окно не "замерзало" и не заливалось цветом
    case WM_ENTERSIZEMOVE:
        g_in_move = true;
        ::SetTimer(hWnd, 1, 16, nullptr);
        return 0;
    case WM_EXITSIZEMOVE:
        g_in_move = false;
        ::KillTimer(hWnd, 1);
        return 0;
    case WM_TIMER:
        if (wParam == 1) { RenderFrame(); return 0; }
        break;
    case WM_ERASEBKGND:
        return 1;   // фон рисует DirectX, системная заливка не нужна
    case WM_ACTIVATE:
        if (g_imgui_ready && LOWORD(wParam) != WA_INACTIVE && g_theme.minimize_animation) {
            g_win_anim = WinAnim::Restoring;
            g_win_anim_t = 0.0f;
            ImGuiIO& io = ImGui::GetIO();
            io.MouseDown[0] = false;
            io.MouseDown[1] = false;
            io.MouseDown[2] = false;
            ImGui::ClearActiveID();
        }
        break;
    case WM_CLOSE:
        // Сначала рассыпаемся, закрываемся потом (dust_fx сам пришлёт WM_CLOSE ещё раз)
        if (g_imgui_ready && g_theme.close_animation && !dust_fx::s_played
            && ::IsWindowVisible(hWnd) && !::IsIconic(hWnd)) {
            dust_fx::s_request = true;
            return 0;
        }
        break;
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}