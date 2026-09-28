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
{ "cos phi:",         "Коэф. мощности (cos):" },
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
        { "Cable section by power, voltage, cos phi and length; material, installation, insulation, ambient temp.",
          "Сечение кабеля по мощности, напряжению, cos и длине; материал, прокладка, изоляция, темп. среды." },
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
        long long sum = 0;
        int nonblack = 0;
        for (int i = 0; i < w * h; ++i) {
            int r = pixels[i * 4], g = pixels[i * 4 + 1], b = pixels[i * 4 + 2];
            if (r + g + b > 30) nonblack++;
            sum += r + g + b;
        }
        // Если меньше 1% пикселей не-чёрных — превью пустое
        float ratio = (float)nonblack / (float)(w * h);
        if (ratio < 0.01f) {
            return false;   // ← отдаём "не удалось"
        }

        *out_srv = MakeSRV(pixels.data(), w, h);
        *out_w = w; *out_h = h;
        return *out_srv != nullptr;

        ::SelectObject(hdc_mem, old);
        ::DeleteObject(hbm);
        ::DeleteDC(hdc_mem);
        ::ReleaseDC(nullptr, hdc_screen);

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
    int   insulation_type = 0;   // 0=PVC 70C, 1=XLPE 90C, 2=Rubber 60C
    float ambient_temp = 30.0f;  // °C
    float result_k_temp = 1.0f;  // итоговый температурный коэффициент

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
        fprintf(f, "card_rounding=%.2f\n", g_theme.card_rounding);
        fprintf(f, "scrollbar_width=%.2f\n", g_theme.scrollbar_width);
        fprintf(f, "show_clock=%d\n", g_theme.show_clock ? 1 : 0);
        fprintf(f, "intro_animation=%d\n", g_theme.intro_animation ? 1 : 0);
        fprintf(f, "intro_duration=%.2f\n", g_theme.intro_duration);
        fprintf(f, "intro_scale_min=%.2f\n", g_theme.intro_scale_min);
        fprintf(f, "minimize_animation=%d\n", g_theme.minimize_animation ? 1 : 0);

        fprintf(f, "\n[calc]\n");
        fprintf(f, "cable_material=%d\n", calc_data::cable_material);
        fprintf(f, "cable_install=%d\n", calc_data::cable_install);
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
            else if (key == "card_rounding")      g_theme.card_rounding = (float)atof(val.c_str());
            else if (key == "scrollbar_width")    g_theme.scrollbar_width = (float)atof(val.c_str());
            else if (key == "show_clock")         g_theme.show_clock = atoi(val.c_str()) != 0;
            else if (key == "intro_animation")    g_theme.intro_animation = atoi(val.c_str()) != 0;
            else if (key == "intro_duration")     g_theme.intro_duration = (float)atof(val.c_str());
            else if (key == "intro_scale_min")    g_theme.intro_scale_min = (float)atof(val.c_str());
            else if (key == "minimize_animation") g_theme.minimize_animation = atoi(val.c_str()) != 0;
            else if (key == "cable_material")     calc_data::cable_material = atoi(val.c_str());
            else if (key == "cable_install")      calc_data::cable_install = atoi(val.c_str());
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
        { "cosphi", "cos", "cos" },      { "load_cosphi", "cos", "cos" }, { "motor_cos", "cos", "cos" },
        { "##el_q", "C", "Кл" },         { "##el_t", "s", "с" },          { "##jl_t", "s", "с" },
        { "##el_R", "Ohm", "Ом" },       { "##jl_R", "Ohm", "Ом" },       { "##el_P", "W", "Вт" },
        { "##jl_I", "A", "А" },
    };
    for (const U& u : units)
        if (strcmp(u.id, id) == 0) return (i18n::g_lang == i18n::LANG_RU) ? u.ru : u.en;
    return nullptr;
}

inline bool TextInputDouble(const char* id, double* value, float width = -1.0f) {
    char buf[64];
    snprintf(buf, sizeof(buf), "%g", *value);
    ImGui::PushID(id);
    // Поле на всю ширину, служебное имя (id) не показываем - подпись и так стоит над полем
    ImGui::PushItemWidth(width > 0 ? width : -FLT_MIN);
    char label[96];
    snprintf(label, sizeof(label), "##%s", (id[0] == '#' && id[1] == '#') ? id + 2 : id);
    bool changed = ImGui::InputText(label, buf, sizeof(buf), ImGuiInputTextFlags_CharsScientific);
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

    ImGui::SetCursorScreenPos(row_bb.Min);
    ImGui::InvisibleButton(label, row_bb.GetSize(), ImGuiButtonFlags_MouseButtonLeft);

    bool hovered = ImGui::IsItemHovered();
    bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
    if (clicked) *value = !*value;

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
        const ImU32 term_col = ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f, 0.8f, 0.4f, 1.0f));
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

    inline float TempDeratingFactor(int insulation, float ambient) {
        // Базовые температуры: PVC=70, XLPE=90, Rubber=60
        float base = 70.0f;
        if (insulation == 1) base = 90.0f;
        else if (insulation == 2) base = 60.0f;

        // Если ambient ниже эталонных 30°C — коэффициент >1 (кабель "холоднее")
        // Если выше — коэффициент <1.
        // Простая линейная аппроксимация, близкая к таблицам ПУЭ/IEC 60364-5-52.
        if (ambient <= 30.0f) {
            // лёгкий бонус за холод, но не больше 1.15
            float bonus = 1.0f + (30.0f - ambient) * 0.005f;
            return (bonus > 1.15f) ? 1.15f : bonus;
        }

        // Выше 30°C — падение примерно на 1.5% на каждый градус (усреднённо)
        float k = 1.0f - (ambient - 30.0f) * 0.015f;
        if (k < 0.5f) k = 0.5f;
        return k;
    }

    inline const char* InsulationName(int t) {
        switch (t) {
        case 0: return "PVC 70C";
        case 1: return "XLPE 90C";
        case 2: return "Rubber 60C";
        default: return "PVC 70C";
        }
    }

    constexpr float CARD_W_HALF = 440.0f;
    constexpr float CARD_W_FULL = 900.0f;
    constexpr double PI = 3.14159265358979323846;

    inline float InstallFactor(int mode) {
        switch (mode) {
        case 0: return 1.00f;
        case 1: return 0.90f;
        case 2: return 0.85f;
        case 3: return 0.75f;
        default: return 1.00f;
        }
    }
    inline const char* InstallName(int mode) {
        switch (mode) {
        case 0: return "Air";
        case 1: return "Pipe";
        case 2: return "Ground";
        case 3: return "Water";
        default: return "Air";
        }
    }

    void RecalcCable() {
        float P = calc_data::load_power_kw * 1000.0f;
        float U = calc_data::voltage;
        float cosf = calc_data::cos_phi;
        if (calc_data::phases == 1) calc_data::result_current = P / (U * cosf);
        else calc_data::result_current = P / (1.732f * U * cosf);

        float k = InstallFactor(calc_data::cable_install);
        calc_data::result_k_install = k;
        calc_data::result_install_name = InstallName(calc_data::cable_install);

        // === NEW: температурный derating ===
        float k_temp = TempDeratingFactor(calc_data::insulation_type, calc_data::ambient_temp);
        calc_data::result_k_temp = k_temp;

        float j = (calc_data::cable_material == 0) ? 6.0f : 4.0f;
        j *= k;
        j *= k_temp;   // <-- применяем
        calc_data::result_required_section = calc_data::result_current / j;
        if (calc_data::use_manual_section && calc_data::manual_section > 0.01f)
            calc_data::result_section = calc_data::manual_section;
        else
            calc_data::result_section = calc_data::result_required_section;

        float rho = (calc_data::cable_material == 0) ? 0.0175f : 0.028f;
        float S = calc_data::result_section > 0.01f ? calc_data::result_section : 0.01f;
        calc_data::result_drop_v = (2.0f * calc_data::cable_length_m * calc_data::result_current * rho) / S;
        calc_data::result_drop_pct = (calc_data::result_drop_v / U) * 100.0f;
        // === NEW: Ik, петля фаза-ноль (упрощённо) ===
        {
            float in_rating = (float)calc_data::breaker_rating;
            if (calc_data::breaker_rating <= 0) {
                const int ratings[] = { 6,10,16,20,25,32,40,50,63,80,100,125 };
                const float target = calc_data::result_current * calc_data::breaker_margin;
                in_rating = 125.0f;
                for (int r : ratings) {
                    if ((float)r >= target) { in_rating = (float)r; break; }
                }
            }

            float k_curve = 10.0f;
            switch (calc_data::breaker_curve) {
            case 0:  k_curve = 5.0f;  break;   // B
            case 1:  k_curve = 10.0f; break;   // C
            case 2:  k_curve = 20.0f; break;   // D
            default: k_curve = 10.0f; break;
            }
            calc_data::result_ik_min = in_rating * k_curve;

            calc_data::result_ik = 0.0f;
            const float S_ik = calc_data::result_section;
            const float L_ik = calc_data::cable_length_m;
            if (S_ik >= 0.01f && L_ik > 0.01f) {
                const float z_loop = 2.0f * L_ik * rho / S_ik;
                if (z_loop > 1e-6f) {
                    const float u_ph = (calc_data::phases == 3) ? U / 1.732f : U;
                    calc_data::result_ik = u_ph / z_loop;
                }
            }
        }
    }
    void RecalcLoad() {
        calc_data::total_current_a = calc_data::total_power_kw * 1000.0f
            / (calc_data::phases == 1 ? calc_data::voltage : 1.732f * calc_data::voltage)
            / calc_data::cos_phi;
        calc_data::total_energy_kwh = calc_data::total_power_kw * calc_data::hours_per_day;
    }
    void RenderMotorTab() {
        using i18n::T;
        using i18n::L;

        const float diag_w = 360.0f;
        float side_w = (ImGui::GetContentRegionAvail().x - diag_w - 30.0f) * 0.5f;
        if (side_w < 340.0f) side_w = 340.0f;

        gui.group_box(T("MOTOR PARAMETERS"), ImVec2(side_w, 740)); {
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
            ImGui::TextColored(g_theme.text_dim, "%s", T("cos phi:"));
            TextInputFloat("motor_cos", &calc_data::motor_cos_phi);

            ImGui::TextColored(g_theme.text_dim, "%s", T("Efficiency (0.5-1.0):"));
            TextInputFloat("motor_eff", &calc_data::motor_efficiency);

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

            if (calc_data::use_auto_calc) RecalcMotor();
        } gui.end_group_box();

        ImGui::SameLine(0.0f, 15.0f);

        gui.group_box(T("RESULT"), ImVec2(side_w, 460)); {
            RecalcMotor();

            const ImVec4 col_ok(0.4f, 1.0f, 0.4f, 1.0f);
            char buf[64];

            snprintf(buf, sizeof(buf), "%.2f kW", calc_data::result_motor_input_kw);
            ResultRow(T("Input power:"), buf, ImVec4(1.0f, 0.8f, 0.4f, 1.0f));

            if (calc_data::motor_mode == 0) {
                snprintf(buf, sizeof(buf), "%.2f A", calc_data::result_motor_flc);
                ResultRow(T("Nominal current:"), buf, g_theme.accent);
            }
            else {
                snprintf(buf, sizeof(buf), "%.2f kW", calc_data::result_motor_shaft_kw);
                ResultRow(T("Shaft power:"), buf, g_theme.accent);
            }

            snprintf(buf, sizeof(buf), "%.2f A", calc_data::result_motor_start);
            ResultRow(T("Starting current:"), buf, ImVec4(1.0f, 0.5f, 0.5f, 1.0f));

            ResultRow(T("Start method:"), T(MotorStartName(calc_data::motor_start_type)),
                ImVec4(0.6f, 0.85f, 1.0f, 1.0f));

            ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

            const float target = calc_data::result_motor_flc * 1.25f;
            const int ratings[] = { 6,10,16,20,25,32,40,50,63,80,100,125,160,200 };
            int best = 0;
            for (int r : ratings) if ((float)r >= target) { best = r; break; }
            if (best == 0) best = 200;
            const char* prefix = (calc_data::motor_start_type == 0) ? "D" : "C";
            snprintf(buf, sizeof(buf), "%s%d", prefix, best);
            ResultRow(T("Recommended breaker:"), buf, col_ok);

            const int contactor[] = { 9,12,18,25,32,40,50,65,80,95,115,150,185,225 };
            int c_best = 0;
            for (int c : contactor) if ((float)c >= calc_data::result_motor_flc * 1.2f) { c_best = c; break; }
            if (c_best == 0) c_best = 225;
            snprintf(buf, sizeof(buf), "%d A", c_best);
            ResultRow(T("Recommended contactor:"), buf, col_ok);

            snprintf(buf, sizeof(buf), "%.2f - %.2f A",
                calc_data::result_motor_flc * 0.9f,
                calc_data::result_motor_flc * 1.1f);
            ResultRow(T("Thermal relay range:"), buf, col_ok);

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

        gui.group_box(T("CONNECTION DIAGRAM"), ImVec2(diag_w, 460)); {
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
            s_sections = true, s_awg = true, s_symbols = true;

        constexpr float COL_W = 380.0f;
        constexpr float COL_H = 1340.0f;
        constexpr float GAP = 12.0f;
        const ImVec4 note_col(1.0f, 0.8f, 0.4f, 1.0f);

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
        } gui.end_group_box();
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
            if ((float)r >= target) { calc_data::breaker_rating = r; break; }
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
        case 2: return dol_ratio * 0.5f;  // soft ~ половина
        case 3: return dol_ratio * 0.15f; // VFD — почти без броска
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
    void RecalcGround() {
        float R = (calc_data::soil_resistivity / (2.0f * 3.14159f * calc_data::ground_rod_len))
            * (logf(4.0f * calc_data::ground_rod_len / 0.02f) - 1.0f)
            / (float)calc_data::ground_rods;
        calc_data::result_ground = R;
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

    void RenderCableTab() {
        using i18n::T;
        using i18n::L;

        gui.group_box(T("PARAMETERS"), ImVec2(CARD_W_HALF, 880)); {
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
            ImGui::RadioButton(L("PVC 70C"), &calc_data::insulation_type, 0); ImGui::SameLine();
            ImGui::RadioButton(L("XLPE 90C"), &calc_data::insulation_type, 1); ImGui::SameLine();
            ImGui::RadioButton(L("Rubber 60C"), &calc_data::insulation_type, 2);

            ImGui::Spacing();
            ImGui::TextColored(g_theme.text_dim, "%s", T("Ambient temp, C:"));
            TextInputFloat("ambient", &calc_data::ambient_temp);

            ImGui::Spacing();
            ImGui::TextColored(g_theme.text_dim, "%s", T("Power, kW:"));
            TextInputFloat("power", &calc_data::load_power_kw);
            ImGui::TextColored(g_theme.text_dim, "%s", T("Voltage, V:"));
            TextInputFloat("volt", &calc_data::voltage);
            ImGui::TextColored(g_theme.text_dim, "%s", T("cos phi:"));
            TextInputFloat("cosphi", &calc_data::cos_phi);
            ImGui::TextColored(g_theme.text_dim, "%s", T("Length, m:"));
            TextInputFloat("length", &calc_data::cable_length_m);

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
        gui.group_box(T("RESULT"), ImVec2(CARD_W_HALF, 640)); {
            const ImVec4 col_ok(0.4f, 1.0f, 0.4f, 1.0f);
            const ImVec4 col_fail(1.0f, 0.4f, 0.4f, 1.0f);
            char buf[64];

            snprintf(buf, sizeof(buf), "%.2f A", calc_data::result_current);
            ResultRow(T("Current:"), buf, g_theme.accent);
            snprintf(buf, sizeof(buf), "%.2f mm^2", calc_data::result_section);
            ResultRow(T("Section:"), buf, col_ok);

            if (calc_data::use_manual_section) {
                snprintf(buf, sizeof(buf), "%.2f mm^2", calc_data::result_required_section);
                ResultRow(T("Required section:"), buf, ImVec4(1.0f, 0.8f, 0.4f, 1.0f));
                const bool sec_ok = (calc_data::manual_section >= calc_data::result_required_section);
                ResultRow(T("Section check:"),
                    sec_ok ? T("OK") : T("OVERLOAD"),
                    sec_ok ? col_ok : col_fail);
            }

            ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

            snprintf(buf, sizeof(buf), "%.2f", calc_data::result_k_temp);
            ResultRow(T("Temp factor:"), buf, ImVec4(1.0f, 0.7f, 0.4f, 1.0f));
            ResultRow(T("Insulation:"), T(InsulationName(calc_data::insulation_type)),
                ImVec4(1.0f, 0.7f, 0.4f, 1.0f));

            ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

            snprintf(buf, sizeof(buf), "%.2f V", calc_data::result_drop_v);
            ResultRow(T("Voltage drop:"), buf, ImVec4(1.0f, 0.8f, 0.4f, 1.0f));
            snprintf(buf, sizeof(buf), "%.2f %%", calc_data::result_drop_pct);
            ResultRow(T("Drop percent:"), buf,
                calc_data::result_drop_pct <= 5.0f ? col_ok : col_fail);

            ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

            ImGui::TextColored(g_theme.text_dim, "%s", T("Nearest standard section:"));
            const float std_sections[] = { 1.5f,2.5f,4,6,10,16,25,35,50,70,95,120 };
            float best = 1.5f;
            for (float s : std_sections) if (s >= calc_data::result_section) { best = s; break; }
            snprintf(buf, sizeof(buf), "%.1f mm^2", best);
            ImGui::TextColored(g_theme.accent, "%s", buf);

            // === Ik check ===
            ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
            {
                char min_buf[64];
                if (calc_data::breaker_rating <= 0)
                    snprintf(min_buf, sizeof(min_buf), "%.0f A %s", calc_data::result_ik_min, T("(auto)"));
                else
                    snprintf(min_buf, sizeof(min_buf), "%.0f A", calc_data::result_ik_min);

                if (calc_data::result_ik > 0.0f) {
                    const bool ik_ok = (calc_data::result_ik >= calc_data::result_ik_min);
                    snprintf(buf, sizeof(buf), "%.1f A", calc_data::result_ik);
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
            if (OutlineButton(L("Calculate"), ImVec2(-1, 36))) {
                RecalcCable();
                char hbuf[128];
                snprintf(hbuf, sizeof(hbuf), "I=%.2f A, S=%.2f mm2, %s",
                    calc_data::result_current, calc_data::result_section,
                    calc_data::result_install_name);
                history::Add("Cable", hbuf);
            }
        } gui.end_group_box();

        ImGui::Spacing();

        // ==================== MAX LENGTH ====================
        gui.group_box(T("MAX LENGTH FOR 5% VOLTAGE DROP"), ImVec2(CARD_W_FULL, 320)); {
            const float U = calc_data::voltage;
            const float I = (calc_data::result_current > 0.01f) ? calc_data::result_current : 1.0f;
            const float rho = (calc_data::cable_material == 0) ? 0.0175f : 0.028f;
            const float k = (calc_data::phases == 3) ? 1.732f : 2.0f;
            constexpr float X_COL2 = 140.0f;   // вторая колонка (шрифт пропорциональный)

            ImGui::TextColored(g_theme.text_dim, "%s", T("Section"));
            ImGui::SameLine(X_COL2);
            ImGui::TextColored(g_theme.text_dim, "%s", T("Max length, m"));
            ImGui::Separator();

            const float sections[] = { 1.5f, 2.5f, 4.0f, 6.0f, 10.0f, 16.0f, 25.0f };
            for (float S : sections) {
                const float len_max = (U * S * 0.05f) / (k * I * rho);
                ImGui::TextColored(g_theme.text_main, "%.1f", S);
                ImGui::SameLine(X_COL2);
                ImGui::TextColored(g_theme.text_main, "%.0f", len_max);
            }

            ImGui::Spacing();
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.4f, 1.0f), "%s, %s %.2f A, U = %.0f V",
                T(calc_data::cable_material == 0 ? "Copper" : "Aluminum"),
                T("at current"), calc_data::result_current, U);
        } gui.end_group_box();
    }

    void RenderLoadTab() {
        using i18n::T;
        using i18n::L;

        gui.group_box(T("TOTAL LOAD CALCULATOR"), ImVec2(CARD_W_FULL, 490)); {
            ImGui::TextColored(g_theme.text_dim, "%s", T("Total Power, kW:"));
            TextInputFloat("total_power", &calc_data::total_power_kw);
            ImGui::TextColored(g_theme.text_dim, "%s", T("Voltage, V:"));
            TextInputFloat("load_volt", &calc_data::voltage);
            ImGui::TextColored(g_theme.text_dim, "%s", T("cos phi:"));
            TextInputFloat("load_cosphi", &calc_data::cos_phi);
            ImGui::TextColored(g_theme.text_dim, "%s", T("Hours per day:"));
            TextInputFloat("hours", &calc_data::hours_per_day);

            ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

            RecalcLoad();
            char buf[64];
            snprintf(buf, sizeof(buf), "%.2f A", calc_data::total_current_a);
            ImGui::TextColored(g_theme.text_main, "%s", T("Total Current Load:"));
            ImGui::SameLine();
            ImGui::TextColored(g_theme.accent, "%s", buf);

            snprintf(buf, sizeof(buf), "%.2f kWh/day", calc_data::total_energy_kwh);
            ImGui::TextColored(g_theme.text_main, "%s", T("Daily energy:"));
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "%s", buf);

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

        gui.group_box(T("EARTHING RESISTANCE"), ImVec2(CARD_W_FULL, 430)); {
            ImGui::TextColored(g_theme.text_dim, "%s", T("Soil Resistivity, Ohm*m:"));
            TextInputFloat("soil", &calc_data::soil_resistivity);
            ImGui::TextColored(g_theme.text_dim, "%s", T("Rod Length, m:"));
            TextInputFloat("rodlen", &calc_data::ground_rod_len);

            double rods_d = (double)calc_data::ground_rods;
            ImGui::TextColored(g_theme.text_dim, "%s", T("Number of Rods:"));
            if (TextInputDouble("rods", &rods_d))
                calc_data::ground_rods = (int)rods_d;

            ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

            RecalcGround();
            char buf[64];
            snprintf(buf, sizeof(buf), "%.2f Ohm", calc_data::result_ground);
            ImGui::TextColored(g_theme.text_main, "%s", T("Resistance:"));
            ImGui::SameLine();
            ImGui::TextColored(g_theme.accent, "%s", buf);

            ImGui::Spacing();
            if (calc_data::result_ground <= 4.0f)
                ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "%s", T("Normal (<= 4 Ohm)"));
            else
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", T("Exceeds limit (> 4 Ohm)"));

            ImGui::Spacing();
            if (OutlineButton(L("Save to History"), ImVec2(-1, 32), btn_col::success)) {
                char hbuf[128];
                snprintf(hbuf, sizeof(hbuf), "Ground: %.2f Ohm (rods=%d)",
                    calc_data::result_ground, calc_data::ground_rods);
                history::Add("Ground", hbuf);
            }
        } gui.end_group_box();
    }

    void RenderBreakerTab() {
        using i18n::T;
        using i18n::L;

        gui.group_box(T("CIRCUIT BREAKER SELECTOR"), ImVec2(CARD_W_FULL, 470)); {
            const float display_I = calc_data::total_current_a > 0
                ? calc_data::total_current_a : calc_data::result_current;

            char ibuf[64];
            snprintf(ibuf, sizeof(ibuf), "%.2f A", display_I);
            ResultRow(T("Current load:"), ibuf, g_theme.accent);

            snprintf(ibuf, sizeof(ibuf), "%.2f", calc_data::breaker_margin);
            ResultRow(T("Safety margin:"), ibuf, ImVec4(0.8f, 0.8f, 0.8f, 1.0f));

            const float target = display_I * calc_data::breaker_margin;
            snprintf(ibuf, sizeof(ibuf), "%.2f A", target);
            ResultRow(T("Target current:"), ibuf, ImVec4(1.0f, 0.8f, 0.4f, 1.0f));

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
                ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "%s", buf);
            }
            else {
                ImGui::TextColored(g_theme.text_dim, "%s", T("Click button to select breaker rating"));
            }

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
                ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%s %s", T("Error:"), T(err));
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

        auto ShowResult = [](const char* label, double value, const char* unit) {
            char buf[64];
            FormatNumber(buf, sizeof(buf), value);
            ImGui::TextColored(g_theme.text_dim, "%s", label);
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "%s %s", buf, unit);
            };
        auto ShowResult2 = [](const char* l1, double v1, const char* u1,
            const char* l2, double v2, const char* u2) {
                char b1[64], b2[64];
                FormatNumber(b1, sizeof(b1), v1);
                FormatNumber(b2, sizeof(b2), v2);
                ImGui::TextColored(g_theme.text_dim, "%s", l1);
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "%s %s", b1, u1);
                ImGui::SameLine(0.0f, 30.0f);
                ImGui::TextColored(g_theme.text_dim, "%s", l2);
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "%s %s", b2, u2);
            };

        static float s_formulas_h = 1680.0f;   // высота по содержимому (секции сворачиваются)
        gui.group_box(T("PHYSICS & MATH SOLVERS"), ImVec2(CARD_W_FULL, s_formulas_h)); {

            if (SectionHeader(T("Electricity / Current"), &s_el)) {
                ImGui::TextColored(g_theme.accent, "I = q / t");
                ImGui::TextColored(g_theme.text_dim, "%s", T("Charge q (C):"));
                TextInputDouble("##el_q", &calc_data::el_q);
                ImGui::TextColored(g_theme.text_dim, "%s", T("Time t (s):"));
                TextInputDouble("##el_t", &calc_data::el_t);
                ShowResult("I =", calc_data::el_t != 0.0 ? calc_data::el_q / calc_data::el_t : 0.0, "A");
                ImGui::Separator();

                ImGui::TextColored(g_theme.accent, "I = U / R");
                ImGui::TextColored(g_theme.text_dim, "%s", T("Voltage U (V):"));
                TextInputDouble("##el_U", &calc_data::el_U);
                ImGui::TextColored(g_theme.text_dim, "%s", T("Resistance R (Ohm):"));
                TextInputDouble("##el_R", &calc_data::el_R);
                ShowResult("I =", calc_data::el_R != 0.0 ? calc_data::el_U / calc_data::el_R : 0.0, "A");
                ImGui::Separator();

                ImGui::TextColored(g_theme.accent, "I = P / U");
                ImGui::TextColored(g_theme.text_dim, "%s", T("Power P (W):"));
                TextInputDouble("##el_P", &calc_data::el_P);
                ImGui::TextColored(g_theme.text_dim, "%s", T("Voltage U (V):"));
                TextInputDouble("##el_U2", &calc_data::el_U);
                ShowResult("I =", calc_data::el_U != 0.0 ? calc_data::el_P / calc_data::el_U : 0.0, "A");
                ImGui::Separator();

                ImGui::TextColored(g_theme.accent, "Q = I^2 * R * t");
                ImGui::TextColored(g_theme.text_dim, "%s", T("Current I (A):"));
                TextInputDouble("##jl_I", &calc_data::jl_I);
                ImGui::TextColored(g_theme.text_dim, "%s", T("Resistance R (Ohm):"));
                TextInputDouble("##jl_R", &calc_data::jl_R);
                ImGui::TextColored(g_theme.text_dim, "%s", T("Time t (s):"));
                TextInputDouble("##jl_t", &calc_data::jl_t);
                ShowResult("Q =", calc_data::jl_I * calc_data::jl_I * calc_data::jl_R * calc_data::jl_t, "J");
                ImGui::Spacing();
            }

            if (SectionHeader(T("Algebra"), &s_al)) {
                ImGui::TextColored(g_theme.accent, "%s", T("Quadratic"));
                ImGui::TextColored(g_theme.text_dim, "a:");
                TextInputDouble("##qd_a", &calc_data::qd_a);
                ImGui::TextColored(g_theme.text_dim, "b:");
                TextInputDouble("##qd_b", &calc_data::qd_b);
                ImGui::TextColored(g_theme.text_dim, "c:");
                TextInputDouble("##qd_c", &calc_data::qd_c);
                {
                    const double a = calc_data::qd_a, b = calc_data::qd_b, c = calc_data::qd_c;
                    const double D = b * b - 4.0 * a * c;
                    ShowResult("D =", D, "");
                    if (a == 0.0) {
                        if (b != 0.0) ShowResult("x =", -c / b, "");
                    }
                    else if (D < 0.0) {
                        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "%s", T("No real roots"));
                    }
                    else if (D == 0.0) {
                        ShowResult("x =", -b / (2.0 * a), "");
                    }
                    else {
                        ShowResult2("x1 =", (-b + sqrt(D)) / (2.0 * a), "",
                            "x2 =", (-b - sqrt(D)) / (2.0 * a), "");
                    }
                }
                ImGui::Spacing();
            }

            if (SectionHeader(T("Geometry"), &s_ge)) {
                ImGui::TextColored(g_theme.accent, "%s", T("Pythagorean: c = sqrt(a^2 + b^2)"));
                ImGui::TextColored(g_theme.text_dim, "%s", T("Leg a:"));
                TextInputDouble("##py_a", &calc_data::geo_a);
                ImGui::TextColored(g_theme.text_dim, "%s", T("Leg b:"));
                TextInputDouble("##py_b", &calc_data::geo_b);
                ShowResult("c =", sqrt(calc_data::geo_a * calc_data::geo_a + calc_data::geo_b * calc_data::geo_b), "");
                ImGui::Separator();

                ImGui::TextColored(g_theme.accent, "%s", T("Circle: C = 2*pi*R, S = pi*R^2"));
                ImGui::TextColored(g_theme.text_dim, "%s", T("Radius R:"));
                TextInputDouble("##ci_R", &calc_data::geo_R);
                ShowResult2("C =", 2.0 * PI * calc_data::geo_R, "",
                    "S =", PI * calc_data::geo_R * calc_data::geo_R, "");
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
            if (g_target) ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f),
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

        gui.group_box(T("ANIMATION"), ImVec2(CARD_W_FULL, 290)); {
            ToggleSwitch(T("Intro animation on start"), &g_theme.intro_animation);
            LabeledSlider(T("Duration (s)"), &g_theme.intro_duration, 0.1f, 1.0f, "%.2f");
            LabeledSlider(T("Start scale"), &g_theme.intro_scale_min, 0.5f, 1.0f, "%.2f");
            ImGui::Spacing();
            ToggleSwitch(T("Minimize/restore animation"), &g_theme.minimize_animation);
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
                g_theme.bg_animated = true;
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
                    "Cable section by power, voltage, cos phi and length; material, installation, insulation, ambient temp.",
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
                    ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", T(lectures::g_status));
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

    io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 16.0f,
        nullptr, io.Fonts->GetGlyphRangesCyrillic());

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
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}