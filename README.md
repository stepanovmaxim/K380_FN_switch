K380 FN Switch ⌨️

A lightweight, minimalist Windows utility to toggle the default behavior of the function keys (F1-F12) on Logitech K380 and K380s (Pebble Keys 2) keyboards.

💡 What it does
By default, Logitech K380 keyboards prioritize media keys (Play, Pause, Volume, etc.) over standard F-keys. To use standard F-keys, you either have to hold Fn or install Logitech's official software to toggle the mode.

K380 FN Switch sends a direct HID command to the keyboard to lock the F-keys as the primary function. It runs quietly in the system tray and automatically re-applies the setting if the keyboard wakes up from sleep or reconnects.

🚀 Why it's better than Logi Options
Zero Bloatware: The official Logi Options / Logi Options+ software is heavy, installs multiple background services, and consumes significant RAM.

Extremely Lightweight: Written in pure C++ (Win32 API). The executable is just a few kilobytes.

No Dependencies: Does not require any third-party libraries or runtimes.

Set and Forget: Includes a simple tray menu to toggle the F-keys and enable Windows startup.

⚙️ How to use
Download the executable from the Releases page (or compile it yourself).

Run K380_FN_switch.exe.

An icon will appear in your system tray. Right-click the icon to:

Toggle the F-keys on or off.

Add the program to Windows startup.

Exit the application.

🛠 Supported Devices & Building from Source
The default code is configured for the Logitech K380 (PID: 0xB342).

If you have a K380s (Pebble Keys 2):
You will need to change the Product ID (PID) in the source code before compiling.

Find your keyboard's PID in Windows Device Manager (under Device Properties -> Details -> Hardware Ids).

Open main.cpp and find this line:

C++
if (HidD_GetAttributes(hFile, &attr) && attr.VendorID == 0x046d && attr.ProductID == 0xb342)
Replace 0xb342 with your specific PID (e.g., 0xb36b).

Compilation:
Compile using MSVC from the Developer Command Prompt:

DOS
rc app.rc
cl K380_FN_switch.cpp app.res /O2 /link /SUBSYSTEM:WINDOWS

Русская версия 🇷🇺
Легкая и минималистичная утилита для Windows, которая переключает режим работы функциональных клавиш (F1-F12) на клавиатурах Logitech K380 и K380s (Pebble Keys 2).

💡 Что делает программа
По умолчанию на клавиатурах Logitech K380 приоритет отдан мультимедийным клавишам (Громкость, Пауза и т.д.). Чтобы использовать стандартные F-клавиши, нужно либо удерживать Fn, либо устанавливать официальное приложение от Logitech.

K380 FN Switch отправляет прямую HID-команду на клавиатуру для фиксации F-клавиш. Программа работает в фоновом режиме из системного трея и автоматически применяет настройки, если клавиатура выходит из спящего режима или переподключается.

🚀 Чем это лучше Logi Options
Никакого мусора: Официальные приложения Logi Options и Options+ громоздкие, устанавливают множество фоновых служб и потребляют много оперативной памяти.

Минимальный вес: Утилита написана на чистом C++ (Win32 API) и весит всего несколько килобайт.

Никаких зависимостей: Не требует установки сторонних библиотек.

Настроил и забыл: Простое меню в трее позволяет в один клик переключать режим клавиш и добавлять программу в автозагрузку Windows.

⚙️ Как пользоваться
Скачайте программу из раздела Releases (или скомпилируйте самостоятельно).

Запустите K380_FN_switch.exe.

В системном трее появится иконка клавиатуры. Нажмите на нее правой кнопкой мыши, чтобы:

Включить/выключить режим F-клавиш.

Добавить программу в автозагрузку Windows.

Закрыть приложение.

🛠 Поддерживаемые устройства и компиляция
По умолчанию исходный код настроен на Logitech K380 (PID: 0xB342).

Если у вас K380s (Pebble Keys 2):
Вам нужно будет изменить Product ID (PID) в исходном коде перед компиляцией.

Узнайте PID вашей клавиатуры в Диспетчере устройств Windows (Свойства -> Сведения -> ИД оборудования).

Откройте main.cpp и найдите эту строку:

C++
if (HidD_GetAttributes(hFile, &attr) && attr.VendorID == 0x046d && attr.ProductID == 0xb342)
Замените 0xb342 на ваш PID (например, 0xb36b).

Сборка:
Скомпилируйте через MSVC (Developer Command Prompt):

DOS
rc app.rc
cl K380_FN_switch.cpp app.res /O2 /link /SUBSYSTEM:WINDOWS
