/** @file

  A plugin to remap to a hash based host. Hash made from host and path.

  @section license License

  Licensed to the Apache Software Foundation (ASF) under one
  or more contributor license agreements.  See the NOTICE file
  distributed with this work for additional information
  regarding copyright ownership.  The ASF licenses this file
  to you under the Apache License, Version 2.0 (the
  "License"); you may not use this file except in compliance
  with the License.  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
*/

/*

Compile with: $tsxs -c hash_remap.c -o hash_remap.so

Put something like this in your remap.config file:

regex_map http://(.*)/ http://{something unique}.$0/ @plugin=hash_remap.so @pparam={a domain name here}

Example for the use with The Last Mile Cache at an ISP:s customer, located in Karlskrona, Sweden:
regex_map http://(.*)/ http://kaa.k.se.$0/ @plugin=hash_remap.so @pparam=tlmc.isp.example

A request for http://www.example/ will result an request from this TS of 
http://{hash}.{a domain name here}/ 

which in our example would be:

http://24d4dc434ba8a1da.tlmc.isp.example/

If the plugin fail to set the new host it will fail back to:
http://kaa.k.se.www.example/


To test the hash function; Take the source from their homepage, compile and run:

./fnv164 -s www.example
0x24d4dc434ba8a1da

The path is appended without any '/' so

http://www.example/hello/world 

is hashed as:

./fnv164 -s www.examplehello/world

*/

#include <ts/ts.h>
#include <ts/remap.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>

#define PLUGIN_NAME "hash_remap"

/* function prototypes */
uint64_t hash_fnv64(char *buf, size_t len);
void hash_fnv64_continue(char *buf, size_t len, uint64_t *hval);

typedef struct _hash_remap_struct {
    char *isp_name;
    int isp_name_len;
    char *hash;
    size_t hash_len;
} hash_remap_struct;


int TSRemapInit(TSRemapInterface *api_info, char *errbuf, int errbuf_size) {
    /* Called at TS startup. Nothing needed for this plugin */

    TSDebug(PLUGIN_NAME , "remap plugin initialized");

    return 0;
}


int TSRemapNewInstance(int argc, char *argv[], void **ih, char *errbuf, int errbuf_size) {
  /* Called for each remap rule using this plugin. The parameters are parsed here */

    if (argc < 2 || argv[2] == NULL) {
        TSError("Missing parameters for " PLUGIN_NAME);
        return -1;
    }

    TSDebug(PLUGIN_NAME, "new instance fromURL: %s toURL: %s", argv[0], argv[1]);
//    fprintf(stderr, "new instance fromURL: '%s' toURL: '%s' argv[2]: '%s'\n", argv[0], argv[1], argv[2]);

    hash_remap_struct *hash = (hash_remap_struct*) TSmalloc(sizeof(hash_remap_struct));
    hash->isp_name = TSstrdup(argv[2]);
    hash->isp_name_len = strlen(hash->isp_name);

    hash->hash_len = (sizeof(uint64_t) * 1) * 2 + hash->isp_name_len + 1;
    hash->hash = TSmalloc(hash->hash_len); //length of one hash value and a dot and isp_name...

    *ih = (void*) hash;
    TSDebug(PLUGIN_NAME, "created instance %p", *ih);

    return 0;
}

void TSRemapDeleteInstance(void* ih) {
    /* Release instance memory allocated in TSRemapNewInstance */

    TSDebug(PLUGIN_NAME, "deleting instance %p", ih);

    if (ih != NULL) {
        hash_remap_struct *hash = (hash_remap_struct*) ih;

        if (hash->isp_name != NULL)
          TSfree(hash->isp_name);

        if (hash->hash != NULL)
          TSfree(hash->hash);

        TSfree(hash);
    }
}


TSRemapStatus TSRemapDoRemap(void* ih, TSHttpTxn rh, TSRemapRequestInfo *rri) {
    hash_remap_struct *hash = (hash_remap_struct*) ih;

    if (rri == NULL || ih == NULL) {
        TSError(PLUGIN_NAME "NULL pointer for rri or ih");
        return TSREMAP_NO_REMAP;
    }

    int req_host_len;
    int req_path_len;

    const char* req_host = TSUrlHostGet(rri->requestBufp, rri->requestUrl, &req_host_len);
    const char* req_path = TSUrlPathGet(rri->requestBufp, rri->requestUrl, &req_path_len);

    uint64_t hash_value = hash_fnv64((char *) req_host, req_host_len);
    hash_fnv64_continue((char *) req_path, req_path_len, &hash_value);

    memset(hash->hash, 0, hash->hash_len);
    int len = sprintf(hash->hash, "%lx.%.*s", hash_value, hash->isp_name_len, hash->isp_name);

    if (len > hash->hash_len) {
        TSError(PLUGIN_NAME "Memory probably corrupt :whistle:");
        return TSREMAP_NO_REMAP;
    }

    if (TSUrlHostSet(rri->requestBufp, rri->requestUrl, hash->hash, len) != TS_SUCCESS) {
        /* the request was not modified, TS will use the toURL from the remap rule */
        TSError(PLUGIN_NAME "Failed to modify the Host in request URL");
        return TSREMAP_NO_REMAP;
    }

    TSDebug(PLUGIN_NAME, "host changed from [%.*s] to [%.*s]", req_host_len, req_host, len, hash->hash);
//    fprintf(stderr, "host changed from [%.*s] to [%.*s/%.*s]\n", req_host_len, req_host, len, hash->hash, req_path_len, req_path);
    return TSREMAP_DID_REMAP; /* host has been modified */
}


/* FNV (Fowler/Noll/Vo) hash
   (description: http://www.isthe.com/chongo/tech/comp/fnv/index.html) */
uint64_t hash_fnv64(char *buf, size_t len) {
    uint64_t hval = (uint64_t)0xcbf29ce484222325ULL; /* FNV1_64_INIT */

    for (; len > 0; --len) {
        hval *= (uint64_t)0x100000001b3ULL;  /* FNV_64_PRIME */
        hval ^= (uint64_t)*buf++;
    }

    return hval;
}

void hash_fnv64_continue(char *buf, size_t len, uint64_t *hval) {
    for (; len > 0; --len) {
        *hval *= (uint64_t)0x100000001b3ULL;  /* FNV_64_PRIME */
        *hval ^= (uint64_t)*buf++;
    }
}
