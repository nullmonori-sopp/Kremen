# Reading the Kremen documentation

You can learn Kremen without already knowing Go, C, or another programming language. The first two guides explain the vocabulary as they use it. Start small: run a program, change one line, and run it again.

## If this is your first language

1. [Your first program](getting-started.md) helps you build the compiler, create a file, and understand what each line does.
2. [The tutorial](tutorial.md) walks through ten complete programs. It covers variables, decisions, repetition, functions, data, input, errors, and memory.
3. [Understanding errors](diagnostics.md) explains the compiler's messages and common fixes. Keep it open while you practice.
4. [Safety and memory](safety.md) explains why some seemingly reasonable programs are rejected.

You can stop before the memory lesson and still write useful small programs. Raw pointers are an advanced tool; they are not needed for the early lessons.

## If you already write code

Read [Installation](install.md), then use the [language reference](language.md). Pay particular attention to lexical borrowing, value copies, block-scoped `defer`, and `!T` error results. Similar-looking syntax in another language may have different behavior.

For compiler work, read [Self-hosting and systems work](selfhosting.md), the [bootstrap reference](../selfhost/README.md), and [Validation](validation.md). The compiler for the full language and the self-hosted subset have different capabilities.

For concurrent work, read [Working with threads](threads.md). The first threading API runs named integer-to-integer worker functions. It deliberately excludes shared references and closures; the guide explains why and shows how to collect errors safely.

For the first additional learning requirements, read [Work and learn profiles](profiles.md). Ordinary commands default to work; learn is selected explicitly and keeps the same core safety rules.

## Releases and repository updates

The current release is [0.4.5](v0.4.5.md). Earlier notes cover [0.4.4](v0.4.4.md), [0.4.3](v0.4.3.md), [0.4.2](v0.4.2.md), [0.4.1](v0.4.1.md), [0.4.0](v0.4.md), [0.3.0](v0.3.md), and [migration from 0.2](v0.2.md). The [roadmap](roadmap.md) separates the planned learning tools from features available today.

[Repository update instructions](repository-update.md) distinguish newly added files from replacements, and explain which generated files belong in version control. [CHANGELOG.md](../CHANGELOG.md) gives the short history.
