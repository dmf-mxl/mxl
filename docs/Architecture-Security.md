<!-- SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project. -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Architecture: Security Model

## UNIX permissions

The MXL SDK uses UNIX files and directories, allowing it to leverage operating system file permissions (users, groups, etc) not only at the mxl domain level but also at the individual flow level.

## IPC and Process namespaces

The memory mapping model used by MXL does not require a shared IPC or process namespace, making it suitable for safe use in containerized environments.

Key namespace considerations:

1. **Volume mounts**: The MXL domain must be mounted as a volume into each container that needs access. Use bind mounts or tmpfs volumes.
2. **Namespace isolation**: MXL does not use System V IPC (shared memory segments, semaphores, message queues) or POSIX named resources that would require `--ipc=host`. Each container operates in its own IPC namespace.
3. **PID namespace**: MXL does not rely on process IDs for synchronization. Containers can run with isolated PID namespaces.
4. **Network namespace**: Local MXL communication requires no network access. Network namespaces can be fully isolated unless using remote MXL features.

## Memory mapping

An _mxlFlowReader_ will only _mmap_ flow resources in readonly mode (PROT_READ), allowing readers to access flows stored in a readonly volume or filesystem. In order to support this use case, synchronization between readers and writers is performed using futexes and not using POSIX mutexes, which would require write access to the mutex stored in shared memory.

Technical implications:

- Readers can be safely run against read-only filesystems.
- Futex operations that require write access (FUTEX_WAKE) are only performed by writers.
- Readers use FUTEX_WAIT on read-only mapped memory, which is permitted by the kernel.
- Advisory locks are held by writers only. Readers attempt to touch the `access` file if writable but do not fail if they cannot.

---

[Back to Architecture overview](./Architecture.md)
