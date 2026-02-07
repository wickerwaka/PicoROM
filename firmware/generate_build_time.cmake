# Generate build_time.h with current UTC timestamp in ISO8601 format
string(TIMESTAMP BUILD_TIMESTAMP "%Y-%m-%dT%H:%M:%SZ" UTC)
file(WRITE "${OUTPUT_FILE}" "#pragma once\n#define PICOROM_BUILD_TIME \"${BUILD_TIMESTAMP}\"\n")
