# Your first Kremen program

A program is a set of instructions for a computer. You write those instructions in a text file. The Kremen compiler checks the file and turns it into a program your computer can run.

This guide starts with an unpacked Kremen source archive on Linux. You do not need to know another language. If you have not unpacked or installed Kremen yet, [Installation](install.md) covers the required tools and archive commands.

## 1. Open a terminal in the project directory

A terminal accepts commands as text. A command such as `make` runs a tool. A path such as `examples/tutorial/01_hello.kr` tells that tool where to find a file.

The commands below assume your current directory is the unpacked `kremen` directory. You can check it with `pwd`, and see its files with `ls`. You should see `Makefile`, `README.md`, and `install.sh`. In a command, `.` means the current directory; `..` means its parent.

Type this and press Enter:

```sh
make
```

This builds the compiler itself. After it finishes, the executable is `build/kremen`. You only need to repeat this when its source or build settings change. If the terminal says a tool is missing, follow [the requirements](install.md#requirements).

Now run the supplied first example:

```sh
./build/kremen run examples/tutorial/01_hello.kr
```

The result should be:

```text
Hello, Kremen!
```

`./build/kremen` runs the compiler you just built. `run` asks it to check, build, and execute the following source file. The initial `./` tells your shell to use a program in the current directory tree.

## 2. Create your own file

Open a plain text editor and create `hello.kr` in the `kremen` directory. Copy this whole program into it, then save the file:

```go
package main

func main() {
    println("Hello, Kremen!")
}
```

The `.kr` extension identifies a Kremen source file. Make sure your editor has not silently named it `hello.kr.txt`. Use an ordinary text file, not a word processor document.

Run your file:

```sh
./build/kremen run hello.kr
```

You have now written and run a program. Change `Hello, Kremen!` to your own message, save, and run the command again. An unsaved editor change does not change the file the compiler reads.

## 3. Understand the five lines

`package main` gives this program its package name. Keep it at the top while learning.

`func main()` declares a **function** called `main`. A function groups instructions under a name. `main` is special: the program starts there. The empty parentheses mean it takes no input arguments.

`{` starts the function's body; `}` ends it. The instructions between them belong to the function. A pair of braces also defines a **block**, a region in which local names exist.

`println("Hello, Kremen!")` calls a function. The text between quotes is a **string**: a value containing text. Here it is the call's **argument**, the information passed into the function. `println` writes it to the terminal and ends the output line. A built-in is available without importing a library.

You usually put one instruction on each line. The compiler supplies the usual statement separators at line endings; you do not need to add a semicolon after each line. Text after `//` is a comment for a human reader:

```go
package main

func main() {
    // The compiler ignores this comment.
    println("Hello, Kremen!")
}
```

## 4. Run, check, and build are different jobs

During practice, `run` is convenient. It creates a temporary executable and removes it after execution.

To check the source without running it:

```sh
./build/kremen check hello.kr
```

A valid file produces `hello.kr: OK`. This checks Kremen syntax and types. It does not execute your program or check whether its answer is what you intended.

To keep an executable:

```sh
./build/kremen build hello.kr -o hello
./hello
```

`-o hello` chooses the output filename. `hello.kr` is the editable source; `hello` is the executable. Edit the source and build again when you want a new executable. Merely running `./hello` does not recompile it.

Built programs contain the Kremen runtime and do not need the Kremen source tree to run. They still depend on the host platform and its system C library. Building programs requires the C compiler used by the Kremen backend.

## 5. Let the formatter handle spacing

```sh
./build/kremen fmt -w hello.kr
```

This rewrites the file using Kremen's standard layout. It does not deliberately change what the program means. Use `fmt hello.kr` to print the formatted text without changing the file, or `fmt --check hello.kr` to check its layout.

## 6. Try a mistake, then fix it

Change the call to `println(message)` without declaring `message`, save, and run `check` again. You should get a message ending in something like:

```text
hello.kr:4:13: error[E107]: unknown variable 'message'
```

The numbers mean line 4, column 13. The compiler also shows that line and marks `message`. It cannot use a name until you have given the name a value. Put the quoted text back to fix this example.

An error is feedback about a specific piece of code. Start with the first reported error, read the marked line, and check again after your change. Kremen stops at the first frontend error so that later messages are not based on an already broken program. [Understanding errors](diagnostics.md) has more examples.

## Next: values and variables

You can already create, check, format, build, and run a program. Continue with [the tutorial](tutorial.md). Each lesson gives you a working file, expected output, and a small change to try.
