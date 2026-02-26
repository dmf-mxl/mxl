#! /bin/bash
# SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
# SPDX-License-Identifier: Apache-2.0

if [[ -z "${1}" ]]; then
    echo "Usage: ${0} <OUTPUT-FILE.tar.gz>"
    exit 1
fi

docker image ls -q --format 'table {{ .Repository }}' | grep '^mxl-example-.*$' | xargs docker save | gzip >"${1}"
