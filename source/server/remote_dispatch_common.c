#include "metsrv.h"

#ifdef _WIN32
// see ReflectiveLoader.c...
extern HINSTANCE hAppInstance;
#else
#include <sys/types.h>
#include <dirent.h>
#include <sys/utsname.h>
#define MAX_PATH 256
#endif

PLIST gExtensionList = NULL;

DWORD request_core_enumextcmd(Remote* pRemote, Packet* pPacket);
DWORD request_core_machine_id(Remote* pRemote, Packet* pPacket);

// Dispatch table
Command customCommands[] = 
{
	COMMAND_REQ("core_loadlib", request_core_loadlib),
	COMMAND_REQ("core_enumextcmd", request_core_enumextcmd),
	COMMAND_REQ("core_machine_id", request_core_machine_id),
	COMMAND_TERMINATOR
};

typedef struct _EnumExtensions
{
	Packet* pResponse;
	char* lpExtensionName;
} EnumExtensions, * PEnumExtensions;

BOOL ext_cmd_callback(LPVOID pState, LPVOID pData)
{
	PEnumExtensions pEnum = (PEnumExtensions)pState;
	Command* command = NULL;

	if (pEnum != NULL && pEnum->pResponse != NULL && pData != NULL)
	{
		PEXTENSION pExt = (PEXTENSION)pData;
		if (pExt->name[0] != '\0' && pEnum->lpExtensionName != NULL && strcmp(pExt->name, pEnum->lpExtensionName) == 0)
		{
			dprintf("[LISTEXT] Found extension: %s", pExt->name);
			for (command = pExt->start; command != pExt->end; command = command->next)
			{
				packet_add_tlv_string(pEnum->pResponse, TLV_TYPE_STRING, command->method);
			}

			return TRUE;
		}
	}
	return FALSE;
}

DWORD request_core_machine_id(Remote* pRemote, Packet* pPacket)
{
	DWORD res = ERROR_SUCCESS;
	Packet* pResponse = packet_create_response(pPacket);

	if (pResponse)
	{
#ifdef _WIN32
		wchar_t buffer[MAX_PATH];
		if (GetSystemDirectory(buffer, MAX_PATH) != 0)
		{
			wchar_t computerName[MAX_PATH];
			DWORD computerNameSize = MAX_PATH;
			DWORD serialNumber;
			wchar_t* backslash = wcschr(buffer, L'\\');
			*(backslash + 1) = L'\0';

			GetVolumeInformation(buffer, NULL, 0, &serialNumber, NULL, 0, NULL, 0);

			GetComputerName(computerName, &computerNameSize);

			_snwprintf_s(buffer, MAX_PATH, MAX_PATH - 1, L"%04x-%04x:%s", HIWORD(serialNumber), LOWORD(serialNumber), computerName);
			packet_add_tlv_wstring(pResponse, TLV_TYPE_MACHINE_ID, buffer);
		}
#else
		char buffer[MAX_PATH];
    struct dirent *data;
    struct utsname utsbuf;
    char* folder = "/dev/disk/by-id/";
    DIR *ctx = opendir(folder);

    if (uname(&utsbuf) == -1)
    {
      res = GetLastError();
      goto out;
    }

    if (ctx == NULL)
    {
      res = GetLastError();
      goto out;
    }

    while (data = readdir(ctx))
    {
      // TODO: make sure that looking for drives prefixed with "ata" is a good
      // idea. We might need to search for a bunch of prefixes.
      if (strncmp(data->d_name, "ata-", 4) == 0)
      {
        snprintf(buffer, MAX_PATH - 1, "%s:%s", data->d_name + 4, utsbuf.nodename);
        packet_add_tlv_string(pResponse, TLV_TYPE_MACHINE_ID, buffer);
        break;
      }
    }
    closedir(ctx);

out:

#endif

		packet_transmit_response(res, pRemote, pResponse);
	}

	return ERROR_SUCCESS;
}

DWORD request_core_enumextcmd(Remote* pRemote, Packet* pPacket)
{
	BOOL bResult = FALSE;
	Packet* pResponse = packet_create_response(pPacket);

	if (pResponse != NULL)
	{
		EnumExtensions enumExt;
		enumExt.pResponse = pResponse;
		enumExt.lpExtensionName = packet_get_tlv_value_string(pPacket, TLV_TYPE_STRING);

		dprintf("[LISTEXTCMD] Listing extension commands for %s ...", enumExt.lpExtensionName);
		// Start by enumerating the names of the extensions
		bResult = list_enumerate(gExtensionList, ext_cmd_callback, &enumExt);

		packet_add_tlv_uint(pResponse, TLV_TYPE_RESULT, ERROR_SUCCESS);

		packet_transmit(pRemote, pResponse, NULL);
	}

	return ERROR_SUCCESS;
}

/*
 * Registers custom command handlers
 */
VOID register_dispatch_routines()
{
	gExtensionList = list_create();

	command_register_all(customCommands);
}

/*
 * Deregisters previously registered custom commands and loaded extensions.
 */
VOID deregister_dispatch_routines(Remote * remote)
{
	while (TRUE)
	{
		PEXTENSION extension = list_pop(gExtensionList);
		if (!extension)
		{
			break;
		}

		if (extension->deinit)
		{
			extension->deinit(remote);
		}

		free(extension);
	}

	command_deregister_all(customCommands);

	list_destroy(gExtensionList);
}
