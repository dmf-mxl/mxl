# MXL : Media Exchange Layer

[![Build Pipeline](https://github.com/dmf-mxl/mxl/actions/workflows/build.yml/badge.svg)](https://github.com/dmf-mxl/mxl/actions/workflows/build.yml)
[![GitHub release](https://img.shields.io/github/v/release/dmf-mxl/mxl)](https://github.com/dmf-mxl/mxl/releases)
[![License](https://img.shields.io/badge/License-Apache_2.0-blue.svg)](LICENSE.txt)
[![Issues](https://img.shields.io/github/issues/dmf-mxl/mxl)](https://github.com/dmf-mxl/mxl/issues)

[Go straight to documentation](#governance)

Rapid advances in computing power and network infrastructure are transforming the landscape of live media production. The professional broadcast industry is gradually moving away from hardware-centric systems and towards software-defined solutions, promising far greater flexibility, scalability, and operational agility. However, this shift into a more virtualised, “dematerialised” environment also introduces substantial interoperability challenges. Multiple vendors, proprietary frameworks, and disparate technology stacks can prevent diverse systems from integrating seamlessly across distributed networks, potentially inhibiting innovation and restricting the efficiency of modern broadcast workflows.

To address these challenges, the [EBU Dynamic Media Facility (DMF)](https://tech.ebu.ch/groups/dmf) initiative proposes a standardised architecture inspired by the cloud-hyperscaler model. In this architecture, discrete “media functions”, the modular building blocks responsible for ingesting, processing, and delivering content, are deployed onto a common container-based platform. These functions can be provisioned and scaled on-demand, and strategically placed wherever compute, storage, and bandwidth are most readily available, whether on-premises, at the network edge, or in public or private clouds.

At the heart of the [DMF architecture](https://tech.ebu.ch/publications/white-paper-2024-09-03) lies the Media Exchange Layer (MXL), a high-performance data plane designed to simplify and accelerate communication between these distributed media functions. The MXL enables entirely new production paradigms, including asynchronous “faster-than-live” workflows, allowing teams to produce content more flexibly and quickly than traditional linear models permit. Moreover, its extensible design supports evolving transport mechanisms and new media formats as they emerge, ensuring that the architecture is well equipped to evolve alongside technological progress.

![docs/Media eXchange Layer.png](https://github.com/dmf-mxl/mxl/blob/53e889c888b2daceb4bf550943f3a194f559f182/docs/Media%20eXchange%20Layer.png "MXL Layer Diagram")

In order to encourage broad industry adoption, the European Broadcasting Union (EBU), the North American Broadcasters Association (NABA), and the Linux Foundation are pursuing an “implement-first” strategy. This practical, hands-on approach involves close collaboration with broadcasters and technology suppliers to produce an open-source software development kit that promotes interoperability and showcases real-world use cases for the MXL. The first alpha version of this SDK was released in June 2025. Ultimately, the DMF initiative aspires to establish a new baseline for open, interoperable software-based live production, a foundation that is robust, future-proof, and capable of sustaining innovation across the entire media ecosystem.

# Learn more about:
## Governance
- The project governance principles: [GOVERNANCE.md](GOVERNANCE/GOVERNANCE.md) and [Technical Charter](GOVERNANCE/CHARTER.pdf) 
- How to contribute: [CONTRIBUTING.md](CONTRIBUTING.md)
- How to report a vulnerability: [SECURITY.md](SECURITY.md)
- [Code of conduct](CODE_OF_CONDUCT.md)

## Technical documentation
- [MXL Architecture](docs/architecture.md)
- [SDK Usage](docs/usage.md)
- [Building options](docs/Building.md)
- [Tools](docs/Tools.md)

## License

This code is covered by the [Apache v2 license](./LICENSE.txt)
