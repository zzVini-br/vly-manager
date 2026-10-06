#include <stdio.h>
#include <string.h>

#include "vly/version.h"

int main(void)
{
    const char *version = vly_version();

    if (version == NULL || strlen(version) == 0) {
        fprintf(stderr, "vly_version() returned an empty value\n");
        return 1;
    }

    return 0;
}
