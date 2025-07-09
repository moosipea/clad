#include "xml.h"
#include <stdlib.h>

int main(int argc, char **argv) 
{
    if (argc < 2) 
    {
        fprintf(stderr, "Usage: %s <xml file>\n", argv[0]);
        return 1;
    }

    XML_Token root;
    char *src = XML_read_file(argv[1]);
    if (!src) 
    {
        return 1;
    }

    if (XML_parse_file(src, &root)) 
    {
        XML_debug_print(stdout, root);
        XML_free(root);
    }

    free(src);
    return 0;
}
