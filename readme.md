# 📘 A Programming Language — Full Specification (v 2.3)

A is a lightweight, indentation-based scripting language that compiles directly into C.
This document specifies all features of the language based on the compiler source code.

## Table of Contents
- Lexical Structure
- Data Types
- Variables
- Control Flow
- Functions
- Lists and Dictionaries
- Expressions & Statements
- Printing
- Standard Library
- Indent-Based Scoping
- Compiler Errors
- Compilation Modes
- Summary

---

# 1. Lexical Structure

### Comments
```a
# This is a comment
```

Everything after `#` on a line is ignored.

---

# 2. Data Types

| A Type | C Equivalent | Notes |
|--------|--------------|--------|
| int | int | Default = 0 |
| bool | bool | Values: true, false |
| float | float | |
| string | char* | Raw C string pointer |
| list | List struct | Dynamic list of ints |
| const modifier | const | Works on standard types |

---

# 3. Variables

### Declaration Examples
```a
int x = 5
float y = 3.14
bool flag = true
string s = "hello"
list L
const int z = 9
```

### Default Initialization

| Type | Default Value |
|------|----------------|
| int | 0 |
| string | NULL |
| list | new_list() |
| bool, float | uninitialized (C default) |

---

# 4. Control Flow

### If
```a
if condition:
    ...
```

Compiles to:
```c
if (condition) {
```

### Elif
```a
elif condition:
```

Compiles to:
```c
} else if (condition) {
```

### Else
```a
else:
```

Compiles to:
```c
} else {
```

### While
```a
while condition:
    ...
```

Compiles to:
```c
while (condition) {
```

### For
```a
for i = A to B:
```

Compiles to:
```c
for (int i = A; i <= B; i++) {
```

### For loops with a step
```a
for i = A to(C) B:
```

Compiles to:
```C
for (int i = A; i <= B; i+=C) {
```
---

# 5. Functions

### Declaration
```a
func myfunc:
    print("hi")
```

Compiles to:
```c
void myfunc() {
    printf("%s\n", "hi");
}
```

### Key Rules
- No parameters  
- No return values  
- Functions end based on indentation unless in raw mode  
- `func main` is ignored because the compiler generates its own `main()`  

---

# 6. Lists and Dictionaries

### List Declaration
```a
list L
```

Internal representation:
```c
typedef struct { int* data; int size; int cap; } List;
```

### Append
```a
append(L, 5)
```

List indexing:
```a
print(L[0])
```

Becomes:
```c
L.data[0]
```

### Dictionaries
Provided via stdlib:

```
Dict { keys[], vals[], size }
```

Functions:
```
dset(d, k, v)
dget(d, k)
```

---

# 7. Expressions & Statements

Everything not recognized as a block/function/keyword becomes raw C:

```a
x = x + 1
```

Becomes:
```c
x = x + 1;
```

---

# 8. Printing

```a
print(expr)
```

| Type | Output C Code |
|------|----------------|
| string | `printf("%s\n", expr);` |
| bool | `printf("%s\n", (expr)?"true":"false");` |
| float | `printf("%f\n", expr);` |
| int/default | `printf("%d\n", (int)(expr));` |

Examples:
```a
print("hi")
print(flag)
print(x)
```

---

# 9. Standard Library

Automatically included in generated C:

- stdio  
- stdlib  
- string  
- math  
- time  
- setjmp  
- list/dict implementations  
- append, new_list, slice_arr  
- dset, dget  

### Time Replacement

| A Code | C Code |
|--------|--------|
| `time.now()` | `(int)time(NULL)` |
| `date.now()` | `(int)time(NULL)` |
| `clock.now()` | `((double)clock() / CLOCKS_PER_SEC)` |

---

# 10. Indent-Based Scoping

### Modes

| Mode | Behavior |
|------|-----------|
| optimized | auto-closes blocks from indentation |
| raw | requires manual `end` keyword |
| debug | shows what the compiler does but in a non-human readable way |
| debug_opt | shows what compiler does in optimized mode |
| debug_raw | shows what compiler does in raw mode |

### Auto-close example
```a
if x > 0:
    print("hi")
print("bye")
```

Compiles to:
```c
if (x > 0) {
    printf("%s\n", "hi");
}
printf("%s\n", "bye");
```

---

# 11. Compiler Errors

Only occur in **raw mode**.

Example:
```
[COMPILER ERROR] Syntax Error: Missing 'end'.
  -> Unclosed block started at line X
```

---

# 12. Compilation Modes

### Command-line
```
./compiler <file> [mode]
```

### Modes
- optimized (default)  
- raw
- debug
- debug_raw
- debug_opt  

### GCC Flags

| Mode | Flags | Characteristics |
|-------|--------|
| optimized | -Ofast -w |
| raw | -O1 |
| debug | -Ofast |
| debug_opt | -Ofast -w |
| debug_raw | -O2 |

Output binary: `program` and ran by `./program`

---

# Summary

A is a:

- Python-like indentation language  
- With C-like declarations  
- List & dict stdlib utilities  
- Auto-printing system  
- Lightweight transpiler to C
- Super-fast same/similar performance to C
- Optional strict/raw mode  
- Optional debug modes (raw, machine, optimized)

Its entire runtime maps directly onto raw C.

V2.3 now comes with an updated max vars/consts going from 512 to 1024, the version also includes various bug fixes like raw mode not logging and stuff, V2.3 was fully built by Claude Opus 4.5 Thinking at this [link](https://lmarena.ai) which is also free to use! Also something I found fascinating is it only took 4 prompts and 20 mins to build
