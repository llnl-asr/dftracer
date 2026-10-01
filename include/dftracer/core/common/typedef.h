//
// Created by haridev on 10/7/23.
//

#ifndef DFTRACER_TYPEDEF_H
#define DFTRACER_TYPEDEF_H
#include <stdint.h>
typedef unsigned long long int TimeResolution;
typedef unsigned long int ThreadID;
typedef int ProcessID;
typedef char* EventNameType;
typedef const char* ConstEventNameType;
typedef char* HashType;
// Entities (see core/common/entity.h). Ids are 64-bit and rendered as 16 hex
// characters only when written; strings are bounded by the DFT_ENTITY_*_LEN
// capacities and sanitized to a JSON-safe character set.
typedef uint64_t EntityID;
typedef const char* ConstEntityTypeName;     // <= DFT_ENTITY_TYPE_LEN
typedef const char* ConstEntityKey;          // hashed into EntityID, not stored
typedef const char* ConstEntityURI;          // <= DFT_ENTITY_URI_LEN
typedef const char* ConstEntityDescription;  // <= DFT_ENTITY_DESC_LEN
#endif                                       // DFTRACER_TYPEDEF_H
