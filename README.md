# DynC

A dynamically typed experimental programming language implemented in C.

![Language](https://img.shields.io/badge/language-C-blue)
![Status](https://img.shields.io/badge/status-experimental-orange)
![Build](https://img.shields.io/badge/build-gcc-success)
![License](https://img.shields.io/badge/license-MIT-green)

---

# Overview

DynC is a lightweight scripting language runtime implemented as an **AST interpreter**.

The project helped me learn how the core components of a programming language work:

- lexical analysis
- parsing
- abstract syntax trees
- runtime environments
- dynamic values
- interpreter evaluation

DynC intentionally mixes design traits from several languages in order to explore their behavior when combined.

---

# Example

### DynC program

```dyn
function fact(n) {
    if (n == 0) {
        return 1;
    }

    return n * fact(n - 1);
}

printf("Fact: %d\n", fact(5));
```

### Output

```
Fact: 120
```

---

# Features

### Language Features

- dynamic typing
- prototype-based objects
- first-class functions
- closures
- recursion
- implicit variable creation
- dynamic property access
- loose equality and coercion rules

---

# Example Programs

### Objects and Prototypes

```dyn
animal = make_object()
animal.sound = "roar"

dog = make_object()
set_prototype(dog, animal)

printf("Dog sound: %s\n", dog.sound)
```

---

### Control Flow

```dyn
i = 0

while(i < 5){
    printf("%d\n", i)
    i = i + 1
}
```

---

### Functions

```dyn
add(a,b){
    return a + b
}

printf("%d\n", add(3,4))
```

---

# Design Philosophy

DynC intentionally integrates features inspired by several programming languages.

| Feature | Inspired by |
|------|------|
| Prototype inheritance | JavaScript |
| Dynamic typing | Python |
| Loose equality | JavaScript |
| Truthiness rules | JavaScript |
| Functions as objects | JavaScript |
| Reference counting | C |
| Unified object/table model | Lua |

---

# Project Structure

```
Dyn/
├── DynC.c
├── README.md
├── docs/
│   └── dync_interpreter.tex
├── examples/
│   ├── recursion.dyn
│   ├── fibonacci_fix.dyn
│   ├── bubble_sort.dyn
│   ├── linear_search.dyn
│   └── test1.dyn
```

---

# Building

Compile the interpreter using GCC.

```bash
gcc DynC.c -o DynC
```

---

# Running Programs

Run a DynC script by passing a `.dyn` file to the interpreter.

```
./DynC program.dyn
```

Example:

```
./DynC recursion.dyn
```

---

# Language Overview

### Variables

Variables are created implicitly.

```dyn
x = 10
```

---

### Objects

Objects store dynamic properties.

```dyn
obj = make_object()

obj.name = "Dyn"
obj["version"] = 1
```

---

### Prototype Inheritance

```dyn
proto = make_object()
proto.x = 10

obj = make_object()
set_prototype(obj, proto)

printf("%d\n", obj.x)
```

---

# Documentation

Detailed technical documentation is available in the `docs` directory.

```
docs/dyn_interpreter.tex
```

---

# Why This Project Exists

DynC was created as an educational project to learn:

- how interpreters work internally
- how programming languages are implemented
- how runtime environments manage values
- how language design decisions affect behavior
