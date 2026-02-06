<!-- SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project. -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Addressability: URI Format

An MXL flow or group of flows can be addressed using this RFC 3986 compatible format:

```
mxl://<authority>/<domain-path>?id=<flow1>&id=<flow2>
```

## Authority (optional)

```
<authority> = <host> [ ":" <port> ]
```

Where:
```
<host> = hostname | IPv4 | "[" IPv6 "]"
<port> = decimal integer (optional)
```

**Examples:**

```
mxl://host1.local/domain/path?id=<flowId1>&id=<flowId2>
mxl://10.1.2.3:5000/domain/path?id=<flowId1>&id=<flowId2>
mxl://[2001:db8::2]/domain/path?id=<flowId1>&id=<flowId2>
```

If authority is absent, the syntax becomes:
```
mxl:///domain/path?id=<flowId1>&id=<flowId2>
```

**Interpretation:**

When authority is present, the URI refers to a remote MXL domain accessed over network protocols (future feature). When authority is absent, the URI refers to a local filesystem path.

## Domain Path (Required)

```
<domain-path> = "/" <segment> *( "/" <segment> )
```

The `domain-path` part of the URI is required and refers to the file system path **in the context of the media function** of the MXL domain. The domain path may need to be translated by media functions or orchestration layer(s). For example, an MXL domain may live on `/dev/shm/domain1` on a host but is mapped inside a container to `/dev/shm/mxl`. This translation is outside the scope of the MXL project.

## Query Component (Optional)

The query component of an MXL URI is optional. When present, it is used exclusively to specify one or more flow identifiers through the id parameter.

The general form is:

```
?<id-parameters>
```

If the query component is absent, no flow selection is implied and the URI simply refers to the root of the MXL domain.

[Back to Addressability overview](./Addressability.md)
