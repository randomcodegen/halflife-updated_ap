#pragma once

extern int AP_Printf(const char *f, ...);
extern int AP_Debugf(const char *f, ...);
extern int AP_Errorf(const char *f, ...);

extern int AP_Printf(std::string f, ...);
extern int AP_Debugf(std::string f, ...);
extern int AP_Errorf(std::string f, ...);

extern int AP_ErrorMsgBoxf(const char* format, ...);

extern void InitializeConsole();