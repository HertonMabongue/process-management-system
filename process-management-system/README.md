# Process Management System

A multi-core process management simulation that implements various scheduling algorithms.

## Overview

This project simulates an operating system's process scheduler and resource manager. It implements three scheduling algorithms (FCFS, Round Robin, and Priority-based) and handles resource allocation and release. The system can run in single-core mode or simulate multiple CPU cores using OpenMP.

## Features

- Process management with PCBs and state transitions
- Resource allocation and management
- Multiple scheduling algorithms:
  - First-Come-First-Served (FCFS)
  - Round Robin with configurable time quantum
  - Priority-based with preemption
- Multi-core scheduling  using OpenMP
- Logging 

## Requirements

- GCC compiler with OpenMP support
- Make build system
- Linux/Unix environment

## Author
HERTON CABRAL MABONGUE

contactherton@gmail.com


## Future Improvements
-Implement a deadlock detection algorithm.

## References
### ChatGPT
- Used ChatGPT to fix and understand technical issues regarding setting up gcc properly on MacOS.
- Used ChatGPT to get an overview of what the project was about and framework on where I should start from.

### w3schools.com
- Visited the website to revisit C syntax
