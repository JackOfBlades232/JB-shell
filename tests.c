/* Toy-Shell/tests.c */
#include "src/lookup.h"

#include <stdio.h>

int main(int argc, char **argv)
{
    if (argc < 2)
        return 1;
    perform_path_lookup(argv[1]);
    return 0;
}
