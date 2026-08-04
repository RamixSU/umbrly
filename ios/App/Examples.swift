import Foundation

/// Scripts shipped inside the app.
///
/// `hello` and `guess` are examples/hello.umb and examples/ugadayka.umb copied
/// verbatim, so what runs here is what runs on the CLI. `winapi` is
/// examples/winapi_demo.umb adapted: the original ends with RUN("cmd /c exit 3"),
/// which cannot work in an iOS sandbox.
enum Examples {
    static let all: [Script] = [hello, guess, winapi]

    static let hello = Script(
        name: "Hello",
        subtitle: "Переменные, ввод, ветвление",
        symbol: "sparkles",
        source: #"""
        # Знакомство с Umbrly 2.0
        PRINT: "Hello World"

        INT_CYFRA = 67
        PRINT: "[INT_CYFRA] + [INT_CYFRA] = [INT_CYFRA + INT_CYFRA]"

        STR_IMYA = INPUT: "Как тебя зовут? "
        PRINT: "Привет, [STR_IMYA]!"

        INT_CHISLO = INPUT: "Введите любое число: "
        PRINT: "Квадрат числа [INT_CHISLO] = [INT_CHISLO * INT_CHISLO]"

        BOOL_CHETNOE = (INT_CHISLO % 2 == 0)
        IF: BOOL_CHETNOE
            PRINT: "Число чётное"
        ELSE:
            PRINT: "Число нечётное"
        END
        """#
    )

    static let guess = Script(
        name: "Угадайка",
        subtitle: "WHILE, BREAK, COLOR и BEEP",
        symbol: "gamecontroller",
        source: #"""
        # Игра "Угадай число" — WHILE, BREAK/CONTINUE, WinAPI (BEEP, COLOR)
        PRINT: "Я загадал число от 1 до 10. Попробуй угадать!"

        INT_SECRET = 7
        INT_SCHET = 0

        WHILE: TRUE
            INT_POPYTKA = INPUT: "Твой вариант: "
            INT_SCHET = INT_SCHET + 1

            IF: INT_POPYTKA == INT_SECRET
                BREAK
            END
            IF: INT_POPYTKA < INT_SECRET
                PRINT: "Моё число больше!"
            ELSE:
                PRINT: "Моё число меньше!"
            END
        END

        COLOR(10, 0)
        PRINT: "Угадал с [INT_SCHET] попытки! Это было число [INT_SECRET]."
        COLOR_RESET()
        BEEP(1000, 200)
        """#
    )

    static let winapi = Script(
        name: "WinAPI на iOS",
        subtitle: "Что переведено, а что недоступно",
        symbol: "arrow.triangle.swap",
        source: #"""
        # Демонстрация функций-обёрток над WinAPI, версия для iOS
        PRINT: "Устройство: [COMPUTER_NAME()]"
        PRINT: "Экран: [SCREEN_WIDTH()]x[SCREEN_HEIGHT()] пунктов"
        PRINT: "Текущее время: [TIME_NOW()]"
        PRINT: "Ядер процессора: [CPU_COUNT()]"

        CONSOLE_TITLE("Umbrly на iOS")

        COLOR(11, 0)
        PRINT: "Этот текст голубой — COLOR() переведён на цвета консоли"
        COLOR_RESET()

        FILE_WRITE("demo_output.txt", "Строка из Umbrly")
        PRINT: "Записан файл, содержимое: [FILE_READ(\"demo_output.txt\")]"
        FILE_DELETE("demo_output.txt")

        BEEP(600, 150)
        SLEEP(100)
        BEEP(900, 150)

        BOOL_YES = CONFIRM("Показать окно с сообщением?", "Umbrly")
        IF: BOOL_YES
            MSGBOX("Привет из Umbrly! Это системный алерт iOS.", "Umbrly")
        END

        PRINT: "Готово."
        """#
    )
}
