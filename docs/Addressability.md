<!-- SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project. -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# MXL Addressability

The following addressing scheme allows applications to refer to one or multiple flows hosted in an MXL domain on a local host or on a remote server.  

## URI 

An MXL flow or group of flows can be addressed using this RFC 3986 compatible format:  

```
mxl://<authority>/<domain-path>?id=<flow1>&<flow2>
```


### Authority (optional) 


```
<authority> = <host> [ ":" <port> ]
```

Where:
```
<host> = hostname | IPv4 | "[" IPv6 "]"
<port> = decimal integer (optional)
```

Examples:
```
mxl://host1.local/domain/path?id=<flowId1>&id=<flowId2>
mxl://10.1.2.3:5000/domain/path?id=<flowId1>&id=<flowId2>
mxl://[2001:db8::2]/domain/path?id=<flowId1>&id=<flowId2>
```

If authority is absent, the syntax becomes:
```
mxl:///domain/path?id=<flowId1>&id=<flowId2>
```

### Domain Path (Required)

```
<domain-path> = "/" <segment> *( "/" <segment> )
```

The ```domain-path``` part of the URI is required and refers to that on disk *in the context of the media function* of the MXL domain.  The domain path may need to be translated by media functions or orchestration layer(s). For example, an MXL domain may live on ```/dev/shm/domain1``` on a host but is mapped inside a container to ```/dev/shm/mxl```.  This translation is outside the scope of the MXL project.  



### Query Component (Optional)

The query component of an MXL URI is optional. When present, it is used exclusively to specify one or more flow identifiers through the id parameter.

The general form is:

```
?<id-parameters>
```


If the query component is absent, no flow selection is implied and the URI simply refers to the root of the MXL domain


## Structure

```
id-param      = "id=" uuid
id-parameters = id-param *( "&" id-param )
```
The id name is mandatory for each parameter. 


Examples:

```
?id=550e8400-e29b-41d4-a716-446655440000
?id=11111111-2222-3333-4444-555555555555&id=aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee
```

## UUID Format

A UUID is defined according to the canonical textual representation in lowercase or uppercase hexadecimal:

```
uuid = 8HEXDIG "-" 4HEXDIG "-" 4HEXDIG "-" 4HEXDIG "-" 12HEXDIG
```

Where:

```
HEXDIG = 0–9 or A–F or a–f
```

Hyphens (-) appear only in the fixed canonical positions.  No surrounding {} braces are allowed.  No URN (urn:uuid:) form is allowed.