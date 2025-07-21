#include "ap_lib.h"
#include <iostream>
#include <string>
#include <sstream>
#include <cstdarg>
#include <stdio.h>
#include <windows.h>

// Prefix strings
constexpr const char* AP_OSDTEXT_NORMAL = "[INFO] ";
constexpr const char* AP_OSDTEXT_DEBUG = "[DEBUG] ";
constexpr const char* AP_OSDTEXT_ERROR = "[ERROR] ";

std::string format_prefixed_str(const char* format, const char* prefix, va_list args)
{
	constexpr size_t BUF_SIZE = 1024;
	char buffer[BUF_SIZE];
	vsnprintf(buffer, BUF_SIZE, format, args);

	std::ostringstream oss;
	//oss << prefix << buffer << '\n';
	oss << prefix << buffer;
	return oss.str();
}

void InitializeConsole()
{
	static bool initialized = false;
	if (initialized)
		return;

	if (!GetConsoleWindow())
	{
		if (!AttachConsole(ATTACH_PARENT_PROCESS))
		{
			AllocConsole();
			printf("Created new console!\n");
		}
		else
		{
			printf("Attached to parent console!\n");
		}
	}

	freopen("CONOUT$", "w", stdout);
	freopen("CONOUT$", "w", stderr);
	initialized = true;
}

int AP_Printf(const char* format, ...)
{

	InitializeConsole();

	va_list args;
	va_start(args, format);
	std::string formatted = format_prefixed_str(format, AP_OSDTEXT_NORMAL, args);
	va_end(args);

	std::cout << formatted;
	return static_cast<int>(strlen(format));
}

int AP_Printf(std::string format, ...)
{
	InitializeConsole();

	va_list args;
	va_start(args, format);
	std::string formatted = format_prefixed_str(format.c_str(), AP_OSDTEXT_NORMAL, args);
	va_end(args);

	std::cout << formatted;
	return static_cast<int>(format.length());
}

int AP_Debugf(const char* format, ...)
{
#ifdef AP_DEBUG_ON
	InitializeConsole();

	va_list args;
	va_start(args, format);
	std::string formatted = format_prefixed_str(format, AP_OSDTEXT_DEBUG, args);
	va_end(args);

	std::cout << formatted;
	return static_cast<int>(strlen(format));
#else
	return 0;
#endif
}

int AP_Debugf(std::string format, ...)
{
#ifdef AP_DEBUG_ON
	InitializeConsole();

	va_list args;
	va_start(args, format);
	std::string formatted = format_prefixed_str(format.c_str(), AP_OSDTEXT_DEBUG, args);
	va_end(args);

	std::cout << formatted;
	return static_cast<int>(format.length());
#else
	return 0;
#endif
}

int AP_Errorf(const char* format, ...)
{
	InitializeConsole();

	va_list args;
	va_start(args, format);
	std::string formatted = format_prefixed_str(format, AP_OSDTEXT_ERROR, args);
	va_end(args);

	std::cerr << formatted;
	return static_cast<int>(strlen(format));
}

int AP_Errorf(std::string format, ...)
{
	InitializeConsole();

	va_list args;
	va_start(args, format);
	std::string formatted = format_prefixed_str(format.c_str(), AP_OSDTEXT_ERROR, args);
	va_end(args);

	std::cerr << formatted;
	return static_cast<int>(format.length());
}

int AP_ErrorMsgBoxf(const char* format, ...)
{
	// Format the string using va_list
	char buffer[1024];

	va_list args;
	va_start(args, format);
	vsnprintf(buffer, sizeof(buffer), format, args);
	va_end(args);

	MessageBoxA(NULL, buffer, "Error", MB_OK | MB_ICONERROR);

	//std::cerr << "Error: " << buffer << std::endl;

	return static_cast<int>(strlen(format));
}