#include <clad/gl.h>
#include <stddef.h>

typedef struct {
    CladProc proc;
    const char *name;
} Proc;

static Proc lookup[] = {
%COMMAND_LOOKUP%
};

int clad_init_gl(CladProcAddrLoader load_proc) {
    for (size_t i = 0; i < sizeof(lookup) / sizeof(*lookup); i++) {
        lookup[i].proc = load_proc(lookup[i].name);
        if (lookup[i].proc == NULL) {
            return 0;
        }
    }
    return 1;
}

%COMMAND_WRAPPERS%
