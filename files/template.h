#ifndef CLAD_H
#define CLAD_H

typedef void (*CladProc)(void);
typedef CladProc (CladProcAddrLoader)(const char *);

int clad_init_gl(CladProcAddrLoader load_proc);

%TYPES%

%ENUMS%

%COMMAND_DECLARATIONS%

#endif
