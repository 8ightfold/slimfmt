# slimfmt

This is a small formatting library for C++17, made with the intent of
reducing compile times. It has a similar API to [fmt](https://github.com/fmtlib/fmt),
but has no compile time checking. This doesn't mean it is unsafe, as it still does
type checking, but these checks have been moved to runtime.

## API

*Note:* Template arguments have been removed, and are formatted like functions.
``N`` is a ``std::size_t``, and ``TT`` is a variadic template.

```cpp
std::string format(const char(&Str)[N], TT&&...Args);
void null(const char(&Str)[N], TT&&...Args);

void print(std::FILE* File, const char(&Str)[N], TT&&...Args);
void print(std::ostream& Stream, const char(&Str)[N], TT&&...Args);
void print(const char(&Str)[N], TT&&...Args);

void println(std::FILE* File, const char(&Str)[N], TT&&...Args);
void println(std::ostream& Stream, const char(&Str)[N], TT&&...Args);
void println(const char(&Str)[N], TT&&...Args);

void out([...]);
void err([...]);
void outln([...]);
void errln([...]);

void flush(std::FILE* File);
void flush(std::ostream& Stream);
bool setColorMode(bool Value);
```

- ``null[s]``: Tests in debug, does nothing in release.
- ``out``/``print``: Formats the arguments, and prints to the passed stream/file (``stdout`` by default).
- ``outln``/``println``: Same as ``print``, but adds a newline.
- ``err[ln]``: Same as ``print[ln]``, but prints to ``stderr`` by default.
- ``format``: Formats the arguments and returns a string.
- ``flush``: Self explanatory...
- ``setColorMode``: Enables/disables colors, currently affects errors (if enabled) and ``err[ln]``.

Because the printers are actually objects, you can use them for simple optional printing.
For example:

```cpp
sfmt::Printer& getDbgErrorPrinter(bool IsDebug) {
  if (IsDebug)
    return sfmt::errln;
  else
    return sfmt::null;
}

void dbgTest(bool IsDebug) {
  auto& Dbg = getDbgErrorPrinter(IsDebug);
  Dbg("{}, {}, {}", 'x', 'y', 'z');
}

// Won't print anything.
dbgTest(false);

// Prints `x, y, z`.
dbgTest(true);
```

Keep in mind this is not the case for ``sfmt::format``.

## Format Strings

A format string will look something like:
``"{} {%x} {: =5} {:#-*%o}"`` (very clear, I know).

Format specifiers can be broken in two parts: alignment and options.
Alignment handles if, where, and how an argument should be aligned/padded.
Options are miscellaneous controls over how formatting is done.

### Grammar

*Note:* Anything in backticks is a regular expression.

```ebnf
replacement := "{" [alignment] [options] "}";
alignment := ":" character [align] width;
options := "%" [base] [extra];

width := (digit+) | dynamic_align;
align := `[< >]` | `[+=-]`;
base  := alpha_base | radix_base;
extra := `[pPcC]`;

dynamic_align := "*";
hex_base   := `[hHxX]`;
alpha_base := `[bBoOdD]` | hex_base;
radix_base := ("r" | "R") digit [digit];

digit := `[0-9]`;
character := `[ -~]`;
```

### Alignment

The alignment spec must begin with the character ``':'``.
It is followed by a printable character, which will be used for padding.
This can optionally be followed by an alignment:

- Left: ``'<'`` or ``'+'``
- Right: ``'>'`` or ``'-'``
- Center: ``' '`` or ``'='``

The argument will be left aligned by default.
Finally, you have the width, which can either be a decimal integer,
or the character ``'*'``, which specifies dynamic alignment.
When using dynamic align, you must pass a positive integer before
the argument you intend to format. For example:

```cpp
// Prints `123###`
sfmt::print("{:#*}", 6, "123");
```

### Options

Options modify the way values are printed.
The base is a single letter identifier, and when capitalized,
will print a number with uppercase digits.
Some of the default bases are:

- Binary: ``'b'`` or ``'B'``
- Octal: ``'o'`` or ``'O'``
- Decimal: ``'d'`` or ``'D'``
- Hex: ``'x'`` or ``'X'``, and alternatively ``'h'`` or ``'H'``
  
You may also print arbitrary radix bases with ``r[base]`` or ``R[base]``.
Only bases in the range ``[1, 32]`` are valid.

Some miscellaneous options are:

- Pointer: ``'p'`` or ``'P'``, will print strings as pointers.
- Character: ``'c'`` or ``'C'``, will only print the first character of strings.
  
These options must always follow an explicit base, as they are handled differently.
For example ``%xP`` is valid, but ``%Px`` is not.

## CMake

The CMake file also adds a few options. These are:

- ``SLIMFMT_FORCE_ASSERT``: Keep assertions enabled in release.
- ``SLIMFMT_STDERR_ASSERT``: Prints to ``stderr`` instead of aborting.

These are not made public, so do not check for them.

## Benchmarks

Coming soon...
