<!-- SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project. -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Addressability: Parsing MXL URIs

## C example

```c
#include <mxl/mxl.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    char domain_path[256];
    char flow_ids[16][64];  // Up to 16 flow IDs
    int flow_count;
} MxlUri;

int parse_mxl_uri(const char* uri, MxlUri* parsed) {
    // Simple URI parser (production code should use a proper URI parser)

    // Check scheme
    if (strncmp(uri, "mxl://", 6) != 0) {
        fprintf(stderr, "Invalid MXL URI scheme\n");
        return -1;
    }

    // Find domain path (everything after /// and before ?)
    const char* path_start = strstr(uri, "///");
    if (!path_start) {
        fprintf(stderr, "Invalid MXL URI: missing domain path\n");
        return -1;
    }
    path_start += 3;  // Skip ///

    const char* query_start = strchr(path_start, '?');
    if (query_start) {
        size_t path_len = query_start - path_start;
        strncpy(parsed->domain_path, path_start, path_len);
        parsed->domain_path[path_len] = '\0';

        // Parse flow IDs from query string
        parsed->flow_count = 0;
        const char* id_start = query_start + 1;
        while (id_start && parsed->flow_count < 16) {
            if (strncmp(id_start, "id=", 3) == 0) {
                id_start += 3;
                const char* id_end = strchr(id_start, '&');
                size_t id_len = id_end ? (id_end - id_start) : strlen(id_start);
                strncpy(parsed->flow_ids[parsed->flow_count], id_start, id_len);
                parsed->flow_ids[parsed->flow_count][id_len] = '\0';
                parsed->flow_count++;
                id_start = id_end ? id_end + 1 : NULL;
            } else {
                break;
            }
        }
    } else {
        strcpy(parsed->domain_path, path_start);
        parsed->flow_count = 0;
    }

    return 0;
}

int main() {
    const char* uri = "mxl:///dev/shm/mxl?id=5fbec3b1-1b0f-417d-9059-8b94a47197ed&id=b3bb5be7-9fe9-4324-a5bb-4c70e1084449";

    MxlUri parsed;
    if (parse_mxl_uri(uri, &parsed) == 0) {
        printf("Domain: %s\n", parsed.domain_path);
        printf("Flow count: %d\n", parsed.flow_count);
        for (int i = 0; i < parsed.flow_count; i++) {
            printf("  Flow %d: %s\n", i, parsed.flow_ids[i]);
        }

        // Create MXL instance
        mxlInstance instance = mxlCreateInstance(parsed.domain_path, NULL);
        if (instance) {
            // Create readers for each flow
            for (int i = 0; i < parsed.flow_count; i++) {
                mxlFlowReader reader;
                if (mxlCreateFlowReader(instance, parsed.flow_ids[i], NULL, &reader) == MXL_STATUS_OK) {
                    printf("Created reader for flow %s\n", parsed.flow_ids[i]);
                    mxlReleaseFlowReader(instance, reader);
                }
            }
            mxlDestroyInstance(instance);
        }
    }

    return 0;
}
```

## Python example

```python
from urllib.parse import urlparse, parse_qs

def parse_mxl_uri(uri):
    parsed = urlparse(uri)

    if parsed.scheme != 'mxl':
        raise ValueError(f"Invalid scheme: {parsed.scheme}")

    # Domain path is the path component
    domain_path = parsed.path

    # Flow IDs are in query parameters
    query_params = parse_qs(parsed.query)
    flow_ids = query_params.get('id', [])

    return {
        'domain_path': domain_path,
        'flow_ids': flow_ids,
        'host': parsed.hostname,
        'port': parsed.port
    }

# Example usage
uri = "mxl:///dev/shm/mxl?id=5fbec3b1-1b0f-417d-9059-8b94a47197ed&id=b3bb5be7-9fe9-4324-a5bb-4c70e1084449"
parsed = parse_mxl_uri(uri)

print(f"Domain: {parsed['domain_path']}")
print(f"Flow IDs: {parsed['flow_ids']}")
```

[Back to Addressability overview](./Addressability.md)
