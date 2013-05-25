/*-
 * Copyright (c) 2012 Ryan Kwolek 
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification, are
 * permitted provided that the following conditions are met:
 *  1. Redistributions of source code must retain the above copyright notice, this list of
 *     conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright notice, this list
 *     of conditions and the following disclaimer in the documentation and/or other materials
 *     provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* 
 * kldldr.c - 
 *    Creates, queries the status of, or deletes a service to load or unload a kernel module.
 */

#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdlib.h>
#include <stdio.h>

#define HELP_STR \
	"kldldr load [-imv] <filename> [service name] - load a driver using the " \
	"specified service name (uses filename if absent)\n" \
	"kldldr unload [-sv] <service name> - stops the specified driver and deletes " \
	"the service\n" \
	"kldldr query [-v] [service name] - queries the status of the specified " \
	"service (enumerates all if absent)\n" \
	"kldldr - this help message\n" \
	"Flags:\n" \
	"  -i: copy driver into system driver folder before loading\n" \
	"  -m: write registry entry for Eventlog to recognize this service while loading\n" \
	"  -s: stop service instead of deleting when unloading\n" \
	"  -v: increases verbosity\n"

#define EVLOG_REGISTRY_KEY "SYSTEM\\CurrentControlSet\\Services\\Eventlog\\System\\"

#define KLDLDR_LOAD   1
#define KLDLDR_UNLOAD 2
#define KLDLDR_QUERY  3

const char *str_actions[] = {
	"load",
	"unload",
	"query"
};

void ParseCmdLine(int argc, char *argv[]);
int DriverLoad(const char *servicename, const char *modulepath);
int DriverUnload(const char *servicename);
int DriverQuery(const char *servicename);
const char *GetSvcTypeStr(DWORD svctype);
const char *GetSvcStateStr(DWORD svcstate);
int CreateEventlogRegEntry(const char *evfilename, DWORD types_supported);

int verbose;
int do_copy_into_dir;
int do_create_elog_entry;
int do_stop_only;
int action;
char servicename[256];
char filename[MAX_PATH];


///////////////////////////////////////////////////////////////////////////////


int main(int argc, char *argv[]) {
	char drvpath[MAX_PATH];
	const char *filepath = filename;
	int status = 0;

	ParseCmdLine(argc, argv);

	switch (action) {
		case KLDLDR_LOAD:
			if (do_copy_into_dir) {
				char *s;

				if (!GetEnvironmentVariable("SYSTEMROOT", drvpath, sizeof(drvpath)))
					strcpy(drvpath, "C:\\WINDOWS");

				strncat(drvpath, "\\system32\\drivers\\", sizeof(drvpath));
				drvpath[sizeof(drvpath) - 1] = 0;

				s = strrchr(filename, '\\');
				strncat(drvpath, s ? s + 1 : filename, sizeof(drvpath));
				drvpath[sizeof(drvpath) - 1] = 0;

				if (!CopyFile(filename, drvpath, 0)) {
					fprintf(stderr, "error (%d): failed to copy %s to %s\n",
						filename, drvpath);
					return 1;
				}

				filepath = drvpath;
			}

			status = DriverLoad(servicename, filepath);

			if (do_create_elog_entry)
				status = status && CreateEventlogRegEntry(filepath, 0x07);
			break;
		case KLDLDR_UNLOAD:
			status = DriverUnload(servicename);
			break;
		case KLDLDR_QUERY:
			status = DriverQuery(servicename);
	}

	status = !status;
	if (status)
		fprintf(stderr, "failed to perform %s.\n", str_actions[action - 1]);
	return status;
}


void ParseCmdLine(int argc, char *argv[]) {
	int i, j, nparams;

	if (argc < 2) {
		puts(HELP_STR);
		exit(0);
	}

	for (i = 2; i < argc; i++) {
		if (argv[i][0] != '-')
			break;

		for (j = 1; argv[i][j]; j++) {
			switch (argv[i][j]) {
				case 'i':
					do_copy_into_dir = 1;
					break;
				case 'm':
					do_create_elog_entry = 1;
					break;
				case 's':
					do_stop_only = 1;
					break;
				case 'v':
					verbose++;
					break;
				default:
					fprintf(stderr, "error: unhandled flag '%c', ignoring\n", argv[i][j]);
			}
		}
	}
	nparams = argc - i;

	switch (argv[1][0]) {
		case 'l':
			action = KLDLDR_LOAD;
			if (nparams < 1) {
				fprintf(stderr, "error: insufficient number of arguments\n");
				exit(0);
			}

			strncpy(filename, argv[i], sizeof(filename));
			filename[sizeof(filename) - 1] = 0;
			if (nparams == 1) {
				char *s = strrchr(filename, '\\');
				strncpy(servicename, s ? s + 1 : filename, sizeof(servicename));
				servicename[sizeof(servicename) - 1] = 0;
				s = strrchr(servicename, '.');
				if (s)
					*s = 0;
			} else {
				strncpy(servicename, argv[i + 1], sizeof(servicename));
				servicename[sizeof(servicename) - 1] = 0;
			}
			break;
		case 'u':
			action = KLDLDR_UNLOAD;
			if (nparams < 1) {
				fprintf(stderr, "error: insufficient number of arguments\n");
				exit(0);
			}
			strncpy(servicename, argv[i], sizeof(servicename));
			servicename[sizeof(servicename) - 1] = 0;
			break;
		case 'q':
			action = KLDLDR_QUERY;
			if (nparams >= 1) {
				strncpy(servicename, argv[i], sizeof(servicename));
				servicename[sizeof(servicename) - 1] = 0;
			}
			break;
		default:
			fprintf(stderr, "error: unknown action '%s'\n", argv[2]);
			exit(0);
	}
}


int DriverLoad(const char *servicename, const char *modulepath) {
	SC_HANDLE hSCManager, hService;
	unsigned long err;
	int success;

	hSCManager = OpenSCManager(0, 0, SC_MANAGER_ALL_ACCESS);
	if (!hSCManager) {
		fprintf(stderr, "error (%d): failed to open service control manager\n",
			GetLastError());
		return 0;
	}

	hService = CreateService(hSCManager, servicename, servicename, SERVICE_ALL_ACCESS,
		SERVICE_KERNEL_DRIVER, SERVICE_DEMAND_START, SERVICE_ERROR_NORMAL, modulepath,
		NULL, NULL, NULL, NULL, NULL);

	if (!hService) {
		if (GetLastError() == ERROR_SERVICE_EXISTS) {
			if (verbose)
				printf("service already exists, opening\n");

			hService = OpenService(hSCManager, servicename, SERVICE_ALL_ACCESS);
			if (!hService) {
				fprintf(stderr, "error (%d): failed to open service\n", GetLastError());
				CloseServiceHandle(hSCManager);
				return 0;
			}
		} else {
			fprintf(stderr, "error (%d): failed to create service\n", GetLastError());
			CloseServiceHandle(hSCManager);
			return 0;
		}
	}
	
	success = StartService(hService, 0, NULL);
	if (!success) {
		err = GetLastError();
		if (err == ERROR_SERVICE_ALREADY_RUNNING) {
			if (verbose)
				printf("service is already running\n");
			success = 1;
		} else {
			fprintf(stderr, "error (%d): failed to start service\n", err);
		}
	}

	CloseServiceHandle(hService);
	CloseServiceHandle(hSCManager);
	return success;
}


int DriverUnload(const char *servicename) {
	SC_HANDLE hSCManager, hService;
	SERVICE_STATUS servicestat;
	int success;

	hSCManager = OpenSCManager(0, 0, SC_MANAGER_ALL_ACCESS);
	if (!hSCManager) {
		fprintf(stderr, "error (%d): failed to open service control manager\n", GetLastError());
		return 0;
	}

	hService = OpenService(hSCManager, servicename, SERVICE_ALL_ACCESS);
	if (!hService) {
		fprintf(stderr, "error (%d): failed to open service\n", GetLastError());
		CloseServiceHandle(hSCManager);
		return 0;
	}
	
	if (!ControlService(hService, SERVICE_CONTROL_STOP, &servicestat)) {
		if (verbose)
			printf("warning (%d): failed to control service\n", GetLastError());
		success = 0;
	} else {
		success = 1;
	}

	if (!do_stop_only) {
		success = DeleteService(hService);
		if (!success)
			fprintf(stderr, "error (%d): failed to delete service\n", GetLastError());
	}

	CloseServiceHandle(hSCManager);
	CloseServiceHandle(hService);
	return success;
}


int DriverQuery(const char *servicename) {
	SC_HANDLE hSCManager = NULL, hService = NULL;
	LPENUM_SERVICE_STATUS pservices = NULL;
	int success = 0;

	hSCManager = OpenSCManager(0, 0, SC_MANAGER_ALL_ACCESS);
	if (!hSCManager) {
		fprintf(stderr, "error (%d): failed to open service control manager\n", GetLastError());
		return 0;
	}

	if (servicename[0]) {
		SERVICE_STATUS sstat;
		DWORD err;

		hService = OpenService(hSCManager, servicename, GENERIC_READ);

		err = GetLastError();
		if (err == ERROR_SERVICE_DOES_NOT_EXIST) {
			printf("service '%s' does not exist\n", servicename);
			success = 1;
			goto fail;
		}
		if (!hService) {
			fprintf(stderr, "error (%d): failed to open service\n", err);
			goto fail;
		}

		if (!QueryServiceStatus(hService, &sstat)) {
			fprintf(stderr, "error (%d): failed to query service status\n", GetLastError());
			goto fail;
		}

		success = 1;
		printf("%s\n\tType:   %s\n\tStatus: %s\n", servicename,
			GetSvcTypeStr(sstat.dwServiceType),
			GetSvcStateStr(sstat.dwCurrentState));
	
	} else {
		DWORD bytesneeded, nservices, err, i;

		EnumServicesStatus(hSCManager, SERVICE_DRIVER, SERVICE_STATE_ALL, NULL,
			0, &bytesneeded, &nservices, NULL);
		err = GetLastError();
		if (err != ERROR_INSUFFICIENT_BUFFER && err != ERROR_MORE_DATA) {
			fprintf(stderr, "error (%d): failed to query service status size\n",
				GetLastError());
			goto fail;
		}
		
		pservices = malloc(bytesneeded);
		
		if (!EnumServicesStatus(hSCManager, SERVICE_DRIVER, SERVICE_STATE_ALL,
			pservices, bytesneeded, &bytesneeded, &nservices, NULL)) {
			fprintf(stderr, "error (%d): failed to enumerate services\n",
				GetLastError());
			goto fail;
		}

		success = 1;
		for (i = 0; i != nservices; i++) {
			printf("%s - %s\n\tType:   %s\n\tStatus: %s\n",
				pservices[i].lpServiceName, pservices[i].lpDisplayName,
				GetSvcTypeStr(pservices[i].ServiceStatus.dwServiceType),
				GetSvcStateStr(pservices[i].ServiceStatus.dwCurrentState));
		}
	}
	
fail:
	free(pservices);
	if (hService)
		CloseServiceHandle(hService);
	if (hSCManager)
		CloseServiceHandle(hSCManager);
	return success;
}


const char *GetSvcTypeStr(DWORD svctype) {
	if (svctype & SERVICE_KERNEL_DRIVER)
		return "Kernel driver";

	if (svctype & SERVICE_FILE_SYSTEM_DRIVER)
		return "File system driver";
	
	if (svctype & SERVICE_WIN32_OWN_PROCESS) {
		return (svctype & SERVICE_INTERACTIVE_PROCESS) ?
			"Interactive individual process" :
			"Individual process";
	}

	if (svctype & SERVICE_WIN32_SHARE_PROCESS) {
			return (svctype & SERVICE_INTERACTIVE_PROCESS) ?
				"Interactive shared process" : 
				"Shared process";
	}

	return "Unknown type";
}


const char *GetSvcStateStr(DWORD svcstate) {
	switch (svcstate) {
		case SERVICE_STOPPED:
			return "Stopped";
		case SERVICE_START_PENDING:
			return "Pending start";
		case SERVICE_STOP_PENDING:
			return "Pending stop";
		case SERVICE_RUNNING:
			return "Running";
		case SERVICE_CONTINUE_PENDING:
			return "Pending continue";
		case SERVICE_PAUSE_PENDING:
			return "Pending pause";
		case SERVICE_PAUSED:
			return "Paused";
		default:
			return "Unknown state";
	}
}


int CreateEventlogRegEntry(const char *evfilename, DWORD types_supported) {
	char keyname[256];
	HKEY hKey;

	strcpy(keyname, EVLOG_REGISTRY_KEY);
	strncat(keyname, servicename, sizeof(keyname));
	keyname[sizeof(keyname) - 1] = 0;

	if (RegCreateKey(HKEY_LOCAL_MACHINE, keyname, &hKey) != ERROR_SUCCESS) {
		fprintf(stderr, "error (%d): failed to create registry key '%s'\n",
			GetLastError(), keyname);
		RegCloseKey(hKey);
		return 0;
	}

	if (RegSetValueEx(hKey, "EventMessageFile", 0, REG_EXPAND_SZ, evfilename,
		strlen(evfilename) + 1) != ERROR_SUCCESS ||
		RegSetValueEx(hKey, "TypesSupported", 0, REG_DWORD, (BYTE *)&types_supported,
		sizeof(types_supported)) != ERROR_SUCCESS) {
		
		fprintf(stderr, "error (%d): failed to set registry values\n",
			GetLastError());
		RegCloseKey(hKey);
		return 0;
	}

	RegCloseKey(hKey);
	return 1;
}
