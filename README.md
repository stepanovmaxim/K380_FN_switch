# K380 FN Switch ⌨️

A lightweight, minimalist Windows utility to toggle the default behavior of the
function keys (F1–F12) on Logitech K380 and K380s (Pebble Keys 2) keyboards.

## 💡 What it does

By default, Logitech K380 keyboards prioritize media keys (Play, Pause, Volume,
etc.) over standard F-keys. To use standard F-keys you either have to hold `Fn`
or install Logitech's official software to toggle the mode.

K380 FN Switch sends a direct HID++ command to the keyboard to lock the F-keys
as the primary function. It runs quietly in the system tray and re-applies the
setting whenever the keyboard reconnects, the machine wakes from sleep, or the
session is unlocked.

## 🚀 Why it's better than Logi Options

- **Zero bloatware.** The official Logi Options / Logi Options+ software is
  heavy, installs multiple background services and consumes significant RAM.
- **Extremely lightweight.** Pure C++ (Win32 API), built without the C runtime:
  a ~30 KB executable using about 2 MB of private memory.
- **No dependencies.** Links only against system DLLs — no third-party
  libraries, no .NET, no Visual C++ redistributable.
- **Set and forget.** Tray menu, optional global hotkey, Windows startup entry.

## ⚙️ How to use

1. Download the executable from the [Releases](../../releases) page, or compile
   it yourself (see below).
2. Run `K380_FN_switch.exe`. An icon appears in the notification area.
   - The icon is **solid** while F1–F12 are primary and **greyed out** while the
     media keys are primary, so the current mode is visible at a glance.
   - The tooltip also reports whether the keyboard is currently connected.
3. **Left-click** the icon to toggle. **Right-click** it for the menu:

   | Item | Description |
   | --- | --- |
   | `F-keys act as F1-F12` | Toggle the Fn lock |
   | `Re-apply now` | Re-send the command (e.g. after a manual reconnect) |
   | `Hotkey: Ctrl+Alt+K` | Enable a global toggle hotkey (off by default) |
   | `Run at startup` | Add/remove the `HKCU\...\Run` entry |
   | `Exit` | Quit |

Preferences (mode, hotkey) are stored under
`HKCU\Software\K380FnSwitch` and restored on the next launch.

### Command line

Running the executable while an instance is already active forwards the request
to it and exits immediately — handy for binding to a launcher or another
hotkey tool:

```bat
K380_FN_switch.exe --toggle
K380_FN_switch.exe --on
K380_FN_switch.exe --off
K380_FN_switch.exe --exit
```

## 🛠 Supported devices

The default configuration targets the Logitech K380 (`VID 046D`, `PID B342`).

For a **K380s / Pebble Keys 2**, or any other Logitech keyboard speaking
HID++ 1.0, no recompilation is needed — pass the product ID on the command
line, or store it permanently:

```bat
K380_FN_switch.exe --pid=0xB37C
```

```
HKCU\Software\K380FnSwitch\ExtraProductIds  (REG_SZ) = "B37C,B36B"
```

Find your keyboard's PID in Device Manager → *Properties* → *Details* →
*Hardware Ids* (`HID\VID_046D&PID_XXXX`).

## 🔨 Building from source

Requires Visual Studio Build Tools with the *Desktop development with C++*
workload and a Windows SDK. Then simply:

```bat
build.cmd
```

The script locates the toolchain via `vswhere`, compiles the resources and
links without the C runtime (`/NODEFAULTLIB` plus a custom entry point), which
is what keeps the binary around 30 KB.

The plain, CRT-linked build also works if you prefer it:

```bat
rc app.rc
cl K380_FN_switch.cpp app.res /O2 /link /SUBSYSTEM:WINDOWS
```

To regenerate `icon.ico` (a multi-resolution icon built from
`tools/icon-master.ico`, so the tray gets a crisp 16/20/24 px frame instead of
a down-scaled 128 px one):

```bat
powershell -ExecutionPolicy Bypass -File tools\make-icon.ps1
```

## 📋 Technical notes

The Fn-inversion command is a HID++ 1.0 short report sent to the keyboard's
vendor-defined collection:

```
10 FF 0B 1E <00|01> 00 00
```

`00` makes F1–F12 primary, `01` restores the media keys. The utility identifies
the right HID collection by its vendor usage page and 7-byte output report
length, so the report is not blasted at unrelated collections of the same
device.

Re-application is event-driven — `WM_DEVICECHANGE` (filtered to Logitech VIDs),
`WM_POWERBROADCAST` resume and `WTS_SESSION_UNLOCK` — with a short retry loop,
because a Bluetooth keyboard is not writable the instant Windows announces it.
Nothing polls, so the keyboard's battery is not disturbed.

---

# Русская версия 🇷🇺

Лёгкая и минималистичная утилита для Windows, которая переключает режим работы
функциональных клавиш (F1–F12) на клавиатурах Logitech K380 и K380s
(Pebble Keys 2).

## 💡 Что делает программа

По умолчанию на клавиатурах Logitech K380 приоритет отдан мультимедийным
клавишам (громкость, пауза и т. д.). Чтобы пользоваться обычными F-клавишами,
нужно либо удерживать `Fn`, либо ставить официальное приложение Logitech.

K380 FN Switch отправляет клавиатуре HID++-команду напрямую. Программа живёт в
системном трее и заново применяет настройку при переподключении клавиатуры,
выходе компьютера из сна и разблокировке сессии.

## ⚙️ Как пользоваться

1. Скачайте исполняемый файл со страницы [Releases](../../releases) или
   соберите сами.
2. Запустите `K380_FN_switch.exe` — в трее появится иконка. Она **насыщенная**,
   когда активны F1–F12, и **приглушённая**, когда активны мультимедийные
   клавиши. Во всплывающей подсказке видно, подключена ли клавиатура.
3. **Левый клик** — переключить режим, **правый клик** — меню: переключение,
   «применить сейчас», глобальная горячая клавиша `Ctrl+Alt+K` (по умолчанию
   выключена), автозапуск, выход.

Настройки хранятся в `HKCU\Software\K380FnSwitch` и восстанавливаются при
следующем запуске.

### Аргументы командной строки

Если программа уже запущена, повторный запуск передаёт команду работающему
экземпляру и сразу завершается:

```bat
K380_FN_switch.exe --toggle
K380_FN_switch.exe --on
K380_FN_switch.exe --off
K380_FN_switch.exe --exit
```

## 🛠 Другие модели

По умолчанию используется K380 (`VID 046D`, `PID B342`). Для K380s / Pebble
Keys 2 перекомпиляция не нужна — укажите PID параметром `--pid=0xB37C` или
запишите его в реестр:

```
HKCU\Software\K380FnSwitch\ExtraProductIds  (REG_SZ) = "B37C,B36B"
```

PID своей клавиатуры можно посмотреть в Диспетчере устройств → *Свойства* →
*Сведения* → *ИД оборудования* (`HID\VID_046D&PID_XXXX`).

## 🔨 Сборка

Нужны Visual Studio Build Tools с рабочей нагрузкой «Разработка классических
приложений на C++» и Windows SDK. Дальше достаточно:

```bat
build.cmd
```

Скрипт сам находит компилятор через `vswhere` и собирает без C-рантайма, за
счёт чего размер исполняемого файла держится около 30 КБ.
