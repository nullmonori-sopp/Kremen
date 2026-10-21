# Кремень / Kremen 0.4.0

Кремень — компилируемый язык для системного программирования с синтаксисом в духе Go. Основной компилятор написан на C11; backend создаёт C11 и вызывает GCC/Clang. Для копирования больших буферов есть небольшой Asm-модуль x86-64 System V и переносимая реализация на C. В пакете нет Python и зависимости от него.

В версии 0.4 появился рабочий драйвер bootstrap на Kremen: `check`, `build`, `run`, `emit-c` и самопересборка. Bootstrap сам проверяет типы своего подмножества и воспроизводит C-код собственных компилятора и CLI. Команда `kremen selfhost` подключает его к основной установке.

Полный язык сохраняет возможности 0.3: `defer`, `catch` для `!T`, `arenablock`, файловые операции, алгебраические типы `enum`, исчерпывающий `match`, кортежи, структуры и методы, ссылки с проверкой заимствований, указатели в `unsafe`, массивы, срезы, локальные импорты и форматтер.

## Установка из архива

Нужны Linux/POSIX, C11-компилятор (`cc`, GCC), `make` и стандартные shell-утилиты. Проверенная платформа выпуска — Linux x86-64 с GCC. C-компилятор нужен также для `kremen build` и `kremen run`.

```sh
unzip kremen-v0.4.0.zip
cd kremen
sh install.sh
export PATH="$HOME/.local/bin:$PATH"
kremen --version
kremen run examples/hello.kr
```

По умолчанию установка идёт в `$HOME/.local`. Другой каталог и удаление:

```sh
sh install.sh --prefix /absolute/path/to/kremen
sh install.sh --prefix /absolute/path/to/kremen --uninstall
```

Инсталлятор собирает основной компилятор и bootstrap с драйвером, устанавливает runtime, исходники для самопересборки, документацию и примеры. Он не скачивает зависимости, не запускает `sudo` и не меняет настройки shell. Установку можно переносить целиком: ресурсы ищутся относительно исполняемого файла. Подробности: [docs/install.md](docs/install.md).

## Работа из исходников

```sh
make
./build/kremen check examples/resources.kr
./build/kremen run examples/resources.kr
./build/kremen build examples/algebra.kr -o build/algebra
./build/kremen emit-c examples/algebra.kr -o build/algebra.c
./build/kremen fmt -w examples/algebra.kr
./build/kremen fmt --check examples/algebra.kr
make selfhost
./build/kremen selfhost check selfhost/example.kr
./build/kremen selfhost run selfhost/example.kr -- /tmp/kremen-example.txt
./build/kremen selfhost bootstrap -o build/kremen-selfhost-next
make test
make test-sanitize
make test-install
make examples
make dist
```

По умолчанию и для компилятора, и для создаваемых программ включены `-std=c11 -Wall -Wextra -Wpedantic -Werror`; оптимизация — `-O3`. Проверки безопасности сохраняются при оптимизации. Для программы можно выбрать `-O0`…`-O3`, `--cc /path/to/cc` и `--root /project`. `make USE_ASM=0` собирает полностью C-вариант компилятора.

Основной `check` не вызывает C-компилятор; `emit-c` требует только поставляемый runtime. У отдельного `selfhost check` после собственной типизации есть дополнительная проверка C backend. Форматтер читает stdin или один файл и пишет stdout; `-w` меняет файлы, `--check` проверяет каноническое оформление. Вводить несколько файлов можно с `-w` либо `--check`.

## Пример

```go
package main

type Answer enum {
    Empty
    Number(value i64)
}

func readAnswer(path string) !string {
    var handle = fsOpen(path, "rb")?
    defer fileClose(handle)
    return fsReadAll(handle)
}

func main() {
    defer println("завершено")
    var answer = Answer.Number(42)
    match answer {
        Empty => { println("пусто") }
        Number(value) => { println(value) }
    }
    var (number, label) = (7, "кортеж")
    println(number, label)
    println(readAnswer("input.txt") catch code => errorMessage(code))
    arenablock memory {
        unsafe {
            var values = arenaAlloc[i64](memory, 4)
            values[0] = 9
            println(values[0])
        }
    }
}
```

`defer` захватывает аргументы при регистрации и исполняется в обратном порядке при выходе из **текущего блока**, включая `return`, `break`, `continue`, `?` и `panic`. Это явная очистка ресурсов; автоматических пользовательских деструкторов типов нет. `catch` вычисляет запасное значение только при ошибке. `panic` выполняет отложенную очистку и завершает процесс; `recover` не реализован.

## Документация и границы

- [Справочник языка](docs/language.md): синтаксис, типы, память и файловый API.
- [Модель безопасности](docs/safety.md): гарантии и ограничения.
- [Изменения 0.4](docs/v0.4.md), [возможности 0.3](docs/v0.3.md) и [история версий](CHANGELOG.md).
- [Проверки выпуска](docs/validation.md) и [измерения](bench/README.md).
- [Bootstrap на Kremen](selfhost/README.md): отдельное самокомпилируемое подмножество.
- [Драйвер self-hosting и системное направление](docs/selfhosting.md).

Основной компилятор полного языка пока остаётся на C. В `selfhost/` проверяются seed → stage1 → stage2 → stage3 и самопересборка компилятора вместе с драйвером; полный перенос всех возможностей языка ещё предстоит. C-toolchain и libc остаются зависимостями. Freestanding/ядра, потоки, GC, интерфейсы, наследование и произвольный C FFI пока не поддерживаются. Версия 0.4 — развивающийся прототип; совместимость ABI между выпусками не обещана.
