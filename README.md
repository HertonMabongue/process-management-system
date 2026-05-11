# Process Management System

Process scheduling and resource-management simulation written in C. The program loads or generates processes, schedules them with one of three algorithms, and logs state transitions, resource requests/releases, and message passing.

## What This Simulates

- Process control blocks (PCBs) with states: NEW, READY, RUNNING, WAITING, TERMINATED
- Resource requests and releases, with waiting when resources are unavailable
- Optional message send/receive instructions between processes
- Single-core or multi-core scheduling using OpenMP threads

## Scheduling Algorithms

- Priority scheduling with preemption (higher priority runs first)
- Round Robin (configurable time quantum)
- First-Come-First-Served (FCFS)

## Build

From [proj1](proj1), run:

```bash
make
```

The binary is named `schedule_processes`.

## Run

From [proj1](proj1), use:

```bash
./schedule_processes <threads> <process_file|generate> <scheduler> <quantum>
```

Parameters:

- `threads`: number of OpenMP threads (cores) to simulate
- `process_file|generate`: path to a process list file or the literal `generate` to create random processes
- `scheduler`: `0` = Priority, `1` = Round Robin, `2` = FCFS
- `quantum`: time quantum for Round Robin (ignored by others)

Examples:

```bash
./schedule_processes 1 data/process1.list 2 2
./schedule_processes 2 data/process2.list 1 3
./schedule_processes 4 generate 0 2
```

## Inputs

Process list files live under [proj1/data](proj1/data). The parser loads:

- Process names and priorities
- Number of initially ready processes
- Resources and mailboxes
- Per-process instruction lists (request, release, send, receive)

## Outputs

Each thread/process writes logs to `thr<N>.log` and `thr<N>.out` in [proj1](proj1). Logs include:

- State transitions and running processes
- Resource acquisition or waiting
- Release results and allocation snapshots
- Send/receive message events

## Requirements

- GCC with OpenMP support
- Make
- macOS or Linux environment

## Future Improvements

- Implement deadlock detection

## Author

HERTON CABRAL MABONGUE

contactherton@gmail.com
