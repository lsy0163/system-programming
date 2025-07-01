# system-programming
[ CSE4100 System Programming Prj1~4 ] Sogang Univ. Spring 2025

This repository contains the source code and documentation for four System Programming projects assigned in CSE4100 at Sogang University. Each project is organized into its own directory with source files, a Makefile, and a document.

### Prj1: MyLib (Bitmap, Hash, List)
Implement basic data structures - bitmap, hash table, and doubly linked list - along with an interactive test program.

### Prj2: MyShell
Build a custom Unix-style shell supporting built-in commands, piping, redirection, and a job control.
Phases:
- phase 1: Basic command execution(cd, ls, mkdir, touch, exit)
- phase 2: Support for pipes(|) and I/O redirection
- phase 3: Background execution and job control(&, jobs, bg, fg, kill)

### Prj3: Concurrent Stock Server
Develop a concurrent network server and client for stock quote requests, and analyze performance under different concurrency models.
Tasks:
- task 1: Event-driven server using select
- task 2: Thread-pool server using POSIX threads
- task 3: Performance comparison and benchmarking

### Prj4: Mallocator
Implement and optimize a dynamic memory allocator(malloc, free, frealloc), and evaluate its performance and space utilization.