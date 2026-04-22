<!-- SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project. -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Security Model

The MXL SDK leverages the security features provided by UNIX filesystems and process isolation to ensure robust and flexible security for media exchange.

## UNIX Permissions

The MXL SDK uses UNIX files and directories, allowing it to leverage operating system file permissions (users, groups, etc.) not only at the MXL domain level but also at the individual flow level. This means you can control which users and groups have access to specific flows and domains using standard filesystem permissions.

## IPC and Process Namespaces

The memory mapping model used by MXL does not require a shared IPC or process namespace, making it suitable for safe use in containerized environments. Each process can operate within its own namespace, reducing the risk of unintended interactions between processes.

## Memory Mapping

An `mxlFlowReader` will only `mmap` flow resources in readonly mode (`PROT_READ`), allowing readers to access flows stored in a readonly volume or filesystem. In order to support this use case, synchronization between readers and writers is performed using futexes and not using POSIX mutexes, which would require write access to the mutex stored in shared memory.

This approach ensures that readers can safely access shared resources without requiring elevated permissions, and writers can maintain control over the integrity of the data.

!!! success
    By leveraging these operating system features, MXL provides a secure and flexible foundation for media exchange in both traditional and containerized environments.
