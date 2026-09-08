// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <string>

struct KairosOptions
{
    std::string domain;
    std::string flow;
    std::string flowOptionsFile;
    std::string node;
    std::string service;
    std::string provider;
    std::string targetInfo;
    bool runAsInitiator = false;
};

class mxl_KAIROS
{
public:
    explicit mxl_KAIROS(KairosOptions options);

    static void requestExit();
    int run();

private:
    KairosOptions _options;
};
