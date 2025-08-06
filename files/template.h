#ifndef CLAD_H
#define CLAD_H

typedef void (*CladProc)(void);
typedef CladProc (CladProcAddrLoader)(const char *);

%TYPES%

%ENUMS%

%COMMAND_DECLARATIONS%

#endif
