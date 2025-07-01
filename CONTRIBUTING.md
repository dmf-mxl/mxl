# Contributing to MXL
Thank you for your interest in contributing to MXL. This document explains our contribution process and procedures.
For a description of the roles and responsibilities of the various members of the MXL community, see the GOVERNANCE file, and for further details, see the project's Technical
Charter. Briefly, Contributors submit content to the project, Maintainers review and approve these submissions, and the Technical Steering Committee (TSC) oversees the project as a whole.
## Getting Information
There are several ways to connect with the MXL project:
- The MXL [GitHub discussions](https://github.com/dmf-mxl/mxl/discussions).
- The email distribution list?
- GitHub [Issues](https://github.com/dmf-mxl/mxl/issues): GitHub (used to track both bugs and feature requests).
- (?) Slack channel
- The regular TSC meetings are free to attend for all interested parties
## How to Ask for Help
If you have questions about implementing, using, or extending MXL, please use the MXL [GitHub discussions](https://github.com/dmf-mxl/mxl/discussions).
## How to Report a Bug
MXL use GitHub's issue tracking system for bugs and enhancements: https://github.com/dmf-mxl/mxl/issues.
If submitting a bug report, include:
- Which plug-in(s) and host products are involved
- OS and compilers used
- Any special build flags or environmental issues
- A detailed, reproducible description:
  - What you tried
  - What happened
  - What you expected to happen
- If possible, a minimalistic sample reproducing the issue
## How to Report a Security Vulnerability
If you think you've found a potential vulnerability in MXL, please refer to [SECURITY.md](SECURITY.md) to responsibly disclose it.
## How to Contribute a Bug Fix or Change
Before contributing code, please review the [GOVERNANCE](GOVERNANCE/GOVERNANCE.md) file to understand the roles involved.
You'll need:
- A good knowledge of git.
- A fork of the GitHub repo.
- An understanding of the project's development workflow.
## Legal Requirements
MXL follows the Linux Foundation’s best practices for open-source contributions.
## License
MXL is licensed under the [Apache-2.0](LICENSE.txt) license. Contributions must comply with this license.
## Contributor License Agreements
We do not require a CLA at this time.
## Commit Sign-Off
Every commit must be signed off.  That is, every commit log message must include a “`Signed-off-by`” line (generated, for example, with “`git commit --signoff`”), indicating that the Contributor wrote the code and has the right to release it under the [Apache-2.0](LICENSE.txt) license.
# Development Workflow
## Git Basics
Working with MXL requires understanding a significant amount of Git and GitHub-based terminology. If you’re unfamiliar with these tools or their lingo, please look at the [GitHub Glossary](https://docs.github.com/en/get-started/learning-about-github/github-glossary) or browse [GitHub Help](https://help.github.com/).
To contribute, you need a GitHub account. This is needed in order to push changes to the upstream repository via a pull request.
You will also need Git installed on your local development machine. If you need setup assistance, please see the official [Git Documentation](https://git-scm.com/doc).
## Repository Structure and Commit Policy
The main branch represents the stable release-ready branch and is protected. Development is typically done in feature branches that are merged into main via pull requests.
## Fork & Clone
1. Fork the repository on GitHub.
2. Clone your fork locally.
3. Add the upstream MXL repository as a remote.
## Pull Requests
Contributions are submitted via pull requests. Follow this workflow:
1. Create a topic branch named feature/<your-feature> or bugfix/<your-fix> from the latest main.
2. Make changes, test thoroughly, and match existing code style.
3. Push commits to your fork.
4. Open a pull request against the main branch.
5. PRs are reviewed by Maintainers and Contributors.
Note: The main branch is protected. All changes must go through reviewed pull requests.
## Code Review and Required Approvals
To comply with the MXL project’s rules:
- All PRs to the main branch must receive at least 2 Maintainer approvals.
- The PR must be up-to-date with the ``** branch** before it can be merged.
- The following status checks must pass:
  - DCO
  - Build on Ubuntu 24.04 - arm64 - Linux-Clang-Release
  - Build on Ubuntu 24.04 - arm64 - Linux-GCC-Release
  - Build on Ubuntu 24.04 - x86_64 - Linux-Clang-Release
  - Build on Ubuntu 24.04 - x86_64 - Linux-GCC-Release
Only Maintainers who are not authors of the PR may approve the merge.
Disagreements or blocked changes may be escalated to the TSC for resolution. Maintainers may assign the tsc-review label to request such escalation.
## Test Policy
This project uses [Catch2](https://github.com/catchorg/Catch2) automated testing framework. This framework is a modern, lightweight, and compatible with CMake/CTest and nicely integrated with VSCode [C++ Test Mate](https://marketplace.visualstudio.com/items?itemName=matepek.vscode-catch2-test-adapter) extension.
## Copyright Notices
All new source files should begin with a copyright and license stating:
 	//
	// SPDX-License-Identifier: Apache-2.0
	// Copyright (c) Contributors to the MXL Project.
	//
## Third-party libraries
The use of third-party libraries is allowed. If these libraries don't match the Apache 2 license, they need to be approved by the TSC. Dependencies of the library itself should be on small-sized libraries, not frameworks, as the goal is to keep the code lightweight.
## Comments and Doxygen
Documentation is automatically generated by Doxygen. A documentation target is available in the cmake generate build files.

`ninja doc`

## Logging

This library uses [spdlog](https://github.com/gabime/spdlog) for internal logging. Logging is disabled by default but can be enabled by setting the MXL_LOG_LEVEL environment variable to one of : off, critical, error, warn, info, debug, trace.
At the moment the logs are going to stdout.

_Note: the debug and trace log statements are statically excluded from the library at compilation time in release mode._
