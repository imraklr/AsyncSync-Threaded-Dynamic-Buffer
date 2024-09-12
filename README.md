# AsyncSync-Threaded-Dynamic-Buffer

This is a dynamic buffer designed to focus on creating a self-learning, self-resizable and efficient buffer for different type of operations like **File I/O**, **I/O**, **Socket Messages**, etc.

## Specific Instructions to Compile for Microsoft Windows

### Compilation

Start The **x86 Native Tools Command Prompt for VS 2022** and type in the following:

```ps1
cl /EHsc *.cpp
```

OR use **g++ (GCC)** to compile with version **23** (Please note that I have compiled this on **g++ (GCC) version 12.4.0**):

```ps1
g++ -std=c++23 *.cpp
```

> [!TIP]
> **Optimization flags can be used to compile the code in both the cases, i.e., when compiling in MSVC environment or when compiling with g++. Several test cases will run to show you the time taken by the process to complete set of tasks.**

### Usage

Use the following command to run the executable from the root folder (When compiled using **x86 Native Tools Command Prompt for VS 2022**):

```ps1
main.exe
```

OR when compiled using g++ in PowerShell, run:

```ps1
./a.exe
```

## Linux Distro Specific Instructions

### Compilation

### Usage
