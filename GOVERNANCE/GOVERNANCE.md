<!-- SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project. -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# MXL Project Governance

MXL is a Linux Foundation project.

There are four primary project roles:

- Contributors submit code to the project.
- Maintainers approve code to be included into the project.
- A Technical Steering Committee (TSC) provides overall high-level project guidance.
- A Requirements Council complements the TSC by representing user requirements.

# Contributors

The MXL project grows and thrives from the assistance of Contributors. Contributors include anyone in the community who contributes code, documentation, or other technical artefacts that have been incorporated into the project's repository.
Anyone can be a Contributor. You need no formal approval from the project, beyond the legal forms (Developer Certificate of Origin).

## How to Become a Contributor

- Review the coding standards to ensure your contribution aligns with the project's coding and styling guidelines.
- Submit your code as a Pull Request with the appropriate DCO sign-off.

# Maintainers

Project Maintainers have merge access on the MXL GitHub repository and are responsible for approving submissions by Contributors.

## Maintainer Responsibilities

- Helping users and novice contributors.
- Ensuring responses to questions on the MXL-discussion mailing list.
- Contributing code and documentation changes that improve the project.
- Reviewing and commenting on issues and pull requests.
- Ensuring that changes and new code meet acceptable standards and align with the long-term interests of the project.
- Participation in working groups.
- Merging pull requests.

## How to Become a Maintainer

Any member of the MXL community (typically an existing Maintainer or TSC member) may nominate an individual making significant and valuable contributions to the MXL project to become a new Maintainer. Nominations may be submitted by opening an issue in the MXL repository, emailing the TSC mailing list, or raising the issue at a TSC meeting.
Maintainers may step down or be removed by a two-thirds vote of the entire TSC.

# Technical Steering Committee (TSC)

The Technical Steering Committee (TSC) is responsible for all technical oversight of the project.

## Composition and Evolution

During the Startup Period (first 18 months from project inception on 3.06.2025), TSC voting members are listed in this GOVERNANCE file. Before the conclusion of this period, the TSC will approve a 'steady state' composition model, to be documented herein. The [Requirements Council](#requirements-council) Chair is a voting TSC member.

## Leadership

The TSC may elect two Co-Chairs. One Co-Chair should be from an End-User and the other from an Implementor. Co-Chairs serve until resignation or replacement by the TSC.

## Responsibilities

- Coordinating the technical direction of the Project.
- Approving sub-project proposals, including incubation and deprecation.
- Organizing or removing sub-projects.
- Creating or disbanding working groups.
- Appointing representatives to work with other open source or open standards communities.
- Establishing community norms, workflows, release processes, and security policies.
- Defining and updating roles such as Contributor or Maintainer.
- Voting on exceptions to licenses (requires 2/3 vote).
- Coordinating any marketing, events, or communications regarding the project.

## Voting and Decision-Making

The TSC strives for consensus. If voting is needed:

- One vote per TSC member. If members are from the same company, they share one collective vote.
- Quorum is 50% of voting members.
- Votes at meetings require a majority of those present. Electronic votes require a majority of all TSC members.

## TSC Members

- [Vincent Trussart (Grass Valley)](https://github.com/vt-tv) - Implementor Co-Chair
- [Felix Poulin (CBC/Radio Canada)](https://github.com/felixpou) - User Co-Chair
- [Osmar Bento (AWS)](https://github.com/osmarbento-AWS)
- [Peter Brightwell (BBC)](https://github.com/peterbrightwell)
- [Kimon Hoffmann (Lawo)](https://github.com/KimonHoffmann)
- [Pavlo Kondratenko (EBU)](https://github.com/paulvko)
- [Jean-Philippe Lapointe (Riedel)](https://github.com/lapointejp)
- [Gareth Sylvester-Bradley (NVIDIA)](https://github.com/garethsb)
- [Willem Vermost (EBU)](https://github.com/wvermost)
- [Sithideth Viengkhou (Riedel)](https://github.com/sviengkhou)

# Requirements Council

The Requirements Council (RC) represents the voice of users and media professionals.
Members are selected by the TSC.
Expected composition: at least 50% End-Users and up to 50% Implementors.
Maintains a roadmap of user-relevant requirements in the Project's repository.
Elects a Chair who is also a voting TSC member.
Members serve one-year terms and may be reappointed or removed by the TSC.

## RC Members

- [Willem Vermost (EBU)](https://github.com/wvermost) - Chair
- [Phil Myers (Lawo)](https://github.com/philjmyers) - Vice Chair
- [Marcel Briesch (SWR)](https://github.com/mbriesch)
- [Peter Brightwell (BBC)](https://github.com/peterbrightwell)
- [Michael Cronk (TVU Networks)](https://github.com/michaelcronk01)
- [Thomas Edwards (AWS)](https://github.com/Thomas-video)
- [Zakarias Faust (SR)](https://github.com/zakariasfaust)
- Ian Fletcher (Grass Valley)
- [Pavlo Kondratenko (EBU)](https://github.com/paulvko)
- [John Mailhot (Imagine Communications)](https://github.com/jmailhot)
- [Mathieu Rochon (CBC/Radio Canada)](https://github.com/rochonma)
- [Ian Wagdin (Appear)](https://github.com/Ianwagdin)

# Meetings

TSC and RC meetings are public unless a justified need for privacy arises. Additional guests may attend in a non-voting capacity. Meetings are announced in advance and may be held virtually or in person. 

The agenda is maintained as a project board, with issues attached to this project board as agenda items: [TSC Board](https://github.com/orgs/dmf-mxl/projects/1), [RC Board](https://github.com/orgs/dmf-mxl/projects/5). Any community member can propose items by opening an issue and adding to the corresponding project board for consideration. (If you don't have the privilege to assign the issue to a project board, contact the group chair(s)). 

Items discussed in the meetings should include a comment indicating the date of the meeting, the general points of discussions, any decision or tentative decision taken. Immediate actions that can be executed in the next week to close the issue are indicated with a check mark and a mention to whom it is assigned. Substantive follow up action or further action require to create a new issue and can be linked in the note. Ex:
```
# TSC YYYY-MM-DD
- General points of discussion, keep as short as possible
- **Decision**: a decision taken
- **Tentative decision**: when we give another week for the participants to make verifications
- [] Action @assignee by [date]
- #Reference_to_a_follow_up_issue
```

Calls can be recorded and kept for a limited period of time as AI summarisation may be used for note taking purpose.


# Licensing

- All code contributions must be under the [Apache License 2.0](https://www.apache.org/licenses/LICENSE-2.0).
- All contributions must include DCO sign-off.
- Documentation contributions (excluding specifications) are licensed under [CC-BY 4.0](https://creativecommons.org/licenses/by/4.0/).
- SPDX license headers should be included.
- License exceptions must be approved by a 2/3 vote of the full TSC.

# Participation & Code of Conduct

Participation in the MXL Project is open to all, regardless of affiliation or competitive interest, as long as they comply with the [Technical Charter](CHARTER.pdf) and [LF Projects policies](https://lfprojects.org/policies/).
Unless superseded by a project-specific CoC, the [LF Projects Code of Conduct](https://lfprojects.org/policies/code-of-conduct/) applies.
The project operates in a transparent, collaborative, and open manner. All discussions, decisions, and contributions should be visible and accessible to the community.
