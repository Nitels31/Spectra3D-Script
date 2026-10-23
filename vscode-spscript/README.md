# Spectra3D Script for VS Code

Плагін для мови **Spectra3D Script** (розширення `.sps`):

* підсвітка синтаксису: ключові слова, типи, рядки, f-рядки, багаторядкові
  рядки `"""..."""`, коментарі, числа, вбудовані функції та методи;
* **помилки синтаксису** в панелі Problems (з рядком і колонкою);
* автопідказки ключових слів, типів і вбудованих функцій;
* запуск файлу у вбудованому терміналі (`F5` / `Ctrl+F5`);
* компіляція без запуску (`Ctrl+B`);
* перевірка файлу без запуску (`SPScript: Check File`).

## Встановлення (без npm)

1. Скопіюйте цю папку у каталог розширень:

   ```
   %USERPROFILE%\.vscode\extensions\spscript-vscode-0.1.0
   ```

   або скористайтеся `install_vscode_extension.bat` у корені проєкту.

2. Перезапустіть VS Code (або `Developer: Reload Window`).

> У PowerShell змінна розгортається як `$env:USERPROFILE`, а не `%USERPROFILE%`.

## Як працюють помилки

Плагін запускає компілятор у режимі перевірки:

```text
spsc --check файл.sps
```

Компілятор друкує помилки в стандартному форматі, який плагін розбирає:

```text
файл.sps:12:5: error: unexpected token ';'
```

Помилки з'являються в панелі **Problems** (Ctrl+Shift+M) і підсвічуються
у редакторі. Перевірка також виконується автоматично під час збереження файлу.

## Налаштування

| Параметр                  | Тип    | Опис                                                     |
|---------------------------|--------|----------------------------------------------------------|
| `spscript.compilerPath`   | string | Шлях до `spsc`. Якщо порожньо — шукається `bin/spsc.exe` у робочій теці, потім `spsc` у `PATH` |
| `spscript.outputDirectory`| string | Тека для `.spc` (за замовчуванням `out`)                 |
| `spscript.runArguments`   | string | Додаткові аргументи компілятора при запуску               |

Приклад `settings.json`:

```json
{
  "spscript.compilerPath": "C:\\__Spectra3D_Scripts\\bin\\spsc.exe",
  "spscript.outputDirectory": "out"
}
```

## Команди

| Команда                              | Дія                                    |
|--------------------------------------|----------------------------------------|
| `SPScript: Run File`                 | компілює та виконує поточний файл      |
| `SPScript: Run File With Arguments…` | те саме, але з додатковими аргументами|
| `SPScript: Compile File (no run)`    | лише компілює у `.spc`                 |
| `SPScript: Check File`               | показує помилки у Problems             |

## Формат команди в терміналі

PowerShell (типовий термінал у VS Code на Windows) — з **оператором виклику** `&`,
бо рядок у лапках на початку команди PowerShell сприймає за помилку розбору:

```text
& "C:\path\to\spsc.exe" "C:\path\to\script.sps"
echo "exit code: $LASTEXITCODE"
```

cmd.exe:

```text
"C:\path\to\spsc.exe" "C:\path\to\script.sps"
echo exit code: %errorlevel%
```

Код виходу компілятора виводиться окремим рядком у кінці — зручно для CI та
скриптів. Тип оболонки плагін визначає автоматично (`vscode.env.shell`).
