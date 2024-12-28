# Illinix 391: Operating System Development

## Overview
Illinix 391 is a custom-built operating system designed from scratch with a focus on modularity, performance, and scalability. This project demonstrates expertise in low-level systems programming, process management, memory virtualization, and driver development.

## Key Features & Achievements

### Core OS Capabilities
- **Virtual Memory Management**: Implemented RISC-V Sv39 paging for efficient address translation and memory protection.
- **Process Management**: Built a process abstraction layer, enabling task isolation, process execution, and memory allocation.
- **Preemptive Multitasking**: Designed a scheduling system for concurrent execution of multiple processes using timer-based context switching.

### Filesystem and Device Drivers
- **Filesystem Abstraction**: Developed a kernel-level filesystem supporting read, write, open, and close operations on virtual storage.
- **Virtio Block Driver**: Enabled robust interaction with virtualized block devices through device driver implementation.

### System Call Interface
- Created an extensible syscall layer enabling user programs to interact with kernel services, supporting I/O, process control, and inter-process communication.

### Resource Synchronization
- Implemented locking mechanisms to ensure safe access to shared kernel resources in a multi-process environment.

### Advanced Features
- **Process Duplication**: Added `fork()` functionality to enable process cloning.

---
