# Midnight Launcher

C++-лаунчер Minecraft: Win32 + Direct2D/DirectWrite, один exe.

Возможности:

- профили сборок (Vanilla / Fabric / Quilt / Forge / NeoForge) и их запуск;
- каталог Modrinth: моды, ресурспаки, шейдеры, датапаки, дерево зависимостей;
- авторизация Microsoft (device code) и Ely.by, смена скина и плаща;
- настройки памяти, Java, аргументов запуска, темы и акцентов;
- языки: русский (по умолчанию), английский, французский, немецкий, китайский,
  японский. Переключение — в «Настройках» → «Язык», выбор сохраняется в
  `%APPDATA%\.minecraft\nexus\settings.json` (`language`).

## Сборка

Нужны Windows 10/11, MinGW-w64 (UCRT, C++20), CMake 3.20+ и Ninja.
`cpp/build.ps1` ищет `cmake`, `ninja` и `g++` в PATH. Если CMake и Ninja
установлены как pip-пакеты, скрипт также умеет использовать `py`/`python`.

```powershell
.\cpp\build.ps1
```

Результат: `cpp\build\MidnightL.exe`. Рядом копируются `resources\fonts`,
`midnight.ico` и `midnight.png`.

Подробное описание этапов и проверок — в [`cpp/README.md`](cpp/README.md).

## Структура

```
.
├─ cpp/                     C++-лаунчер
│  ├─ src/main.cpp          интерфейс, каталог, запуск, авторизация
│  ├─ src/i18n.h            таблица переводов и функции локализации
│  ├─ third_party/json.hpp  nlohmann/json (MIT)
│  ├─ CMakeLists.txt
│  └─ build.ps1
├─ fonts/                   Inter Regular/Bold (SIL OFL 1.1)
├─ midnight.ico
└─ midnight.png
```

## Сторонние компоненты

- [nlohmann/json](https://github.com/nlohmann/json) 3.11.3 — лицензия MIT.
- [Inter](https://rsms.me/inter/) — лицензия SIL Open Font License 1.1.
