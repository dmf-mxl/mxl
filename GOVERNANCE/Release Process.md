<!-- SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project. -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->


# SDK Release Process

This document describes the process and versioning scheme employed by the Media eXchange Layer project for its SDK
releases.


## Versioning Scheme

The MXL SDK is versioned in accordance with the [Semantic Versioning 2.0.0](https://semver.org/) specification, in
short that means that

* version numbers consist of three components `MAJOR`.`MINOR`.`PATCH`, where
* `MAJOR` is incremented whenever a release contains an API or ABI breaking change,
* `MINOR` is incremented for each feature release that retains compatibility with previous releases with the same
    `MAJOR` version, and 
* `PATCH` is incremented for each bugfix release to a previous release with the same `MAJOR`.`MINOR` version.

Whenever `MAJOR` version is increased, `MINOR` version is reset to `0`, and each SDK release irrespective of whether it
is a major or a minor release starts with `PATCH` set to `0`.

Non-release branches of the SDK such as `main` and feature branches always carry the pre-release suffix `dev`, release
branches that are still in their stabilization phase always carry the pre-release suffix `rc`.


## Release Process

Once RC and TSC agree that a release is feature complete a release branch for this release is created from `main`, whose
name adheres to the naming scheme `release/vMAJOR.MINOR`. A corresponding release candidate is published to indicate the
upcoming release to the wider public and encourage users to try out the release and report potential integration issues
with downstream projects.

Immediately after the branch point a new commit is introduced to main that increments the `MINOR` version component by
one. The first time an API or ABI braking change be introduced during the development of the next release a commit is
introduced to main that increments the `MAJOR` version component by one and resets the `MINOR` version component to `0`.

TSC may decide to publish further release candidates if a substantial amount of pre-releases issues has been identified
and addressed to gain additional feedback.

When -- after sufficient time for testing and fixes has been afforded -- TSC deems the release to be of satisfying
quality an official release is made and a commit that increments the `PATCH` component of the branch version by one.

## Backports of features and fixes
Bug fixes and improvements that are contributed to `main` can be ported back to one or more release branches. This is done through an automated process on GitHub. A maintainer has to attach a label of the form: `backport/${version}` to the pull request for the feature or bugfix. A workflow will run when the original pull request is merged and has this label attached, that will create a new branch from the current tip of the specified release branch of `${version}`, cherry-pick all commits from the original pull request branch onto this branch and open a new pull request to merge this branch into the release branch of ${version}. Multiple releases can be targeted for the backport by adding multiple labels to the pull request.
Backports will also be initiated when a label is added to a pull request that is closed and merged.

## Maintenances Window

Any issues identified are addressed on the `main` branch and selectively backported by *cherry-picking* to eligible
release branches.

Assuming the issue applies to them the following release branches are eligible to receive fixes:

* The last released branch independent of the current `MAJOR` and `MINOR` version components, and, if the issue has
    security implications or is deemed serious enough by TSC
* the last released `MINOR` version branch of the previous `MAJOR` version release.


### Example

1. If `main` is currently on version `3.24.0` then regular fixes will be made available on `3.23.x` and fixes for
   security relevant and major issues will also be made available on `2.YY.z`, where `YY` was the last `MINOR` version,
   before version `3.0.0`.

2. If `main` is currently on version `4.0.0` then regular fixes, as well as fixes for major and security relevant issues
   will be made available on `3.XX.y`, where `XX` was the last `MINOR` version release, before main switched to version
   `4.0.0`.


