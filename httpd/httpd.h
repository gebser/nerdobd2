#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

// for file operations
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "../common/common.h"
#include "../common/config.h"

#include "../common/json.h"

int open_db(void);

const char *json_generate(unsigned long int, unsigned long int, unsigned long int, unsigned long int);