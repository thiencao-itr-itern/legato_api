/** @file secStoreServer.c
 *
 * Secure Storage Daemon.  This daemon controls application and user access to secure storage.
 *
 * Each application and user is given a separate area in secure storage that they can access.
 * Applications and users can only access their area of secure storage.  Each application and user
 * has a limit to the amount of space they can use in secure storage.  For applications this limit
 * is defined in the application's adef file.  For non-app users the secure storage limit is a
 * default value.
 *
 * This daemon controls access to applications and user areas of secure storage by automatically
 * pre-pending the app name or user name to the access paths.  For example, when application "foo"
 * writes item "bar" the item will be stored as "/app/foo/bar".  Also, if a non-app user "foo"
 * writes item "bar" the item will be stored as "/foo/bar".
 *
 * <hr>
 *
 * Copyright (C) Sierra Wireless Inc.
 */

#include "legato.h"
#include "interfaces.h"

#include "limit.h"
#include "pa_secStore.h"
#include "watchdogChain.h"
#include "user.h"

#if !MK_CONFIG_SECSTORE_DISABLE_LIMIT
#   include "appCfg.h"
#endif

//--------------------------------------------------------------------------------------------------
/**
 * Number of bytes in secure storage path buffer.
 */
//--------------------------------------------------------------------------------------------------
#ifdef SECSTOREADMIN_MAX_PATH_BYTES
#   define SECSTORE_MAX_PATH_BYTES SECSTOREADMIN_MAX_PATH_BYTES
#else
#   define SECSTORE_MAX_PATH_BYTES 512
#endif

//--------------------------------------------------------------------------------------------------
/**
 * Number bytes in a md5 string.
 */
//--------------------------------------------------------------------------------------------------
#define MD5_STR_BYTES       LE_LIMIT_MD5_STR_LEN + 1

//--------------------------------------------------------------------------------------------------
/**
 * Path in secure storage to store data for non-app users.
 */
//--------------------------------------------------------------------------------------------------
#define USERS_PATH          "/user"


//--------------------------------------------------------------------------------------------------
/**
 * Path in secure storage to store data for systems.
 */
//--------------------------------------------------------------------------------------------------
#define SYS_PATH            "/sys"


//--------------------------------------------------------------------------------------------------
/**
 * Path in secure storage to store global data.
 */
//--------------------------------------------------------------------------------------------------
#define GLOBAL_PATH         "/global"

//--------------------------------------------------------------------------------------------------
/**
 * The timer interval to kick the watchdog chain.
 */
//--------------------------------------------------------------------------------------------------
#define MS_WDOG_INTERVAL 8

#if LE_CONFIG_LINUX

//--------------------------------------------------------------------------------------------------
/**
 * Current system path.
 */
//--------------------------------------------------------------------------------------------------
static char CurrSysPath[SECSTORE_MAX_PATH_BYTES] = "";
#define CURR_SYS_PATH CurrSysPath


//--------------------------------------------------------------------------------------------------
/**
 * Flag that indicates that there is a valid current system path.
 */
//--------------------------------------------------------------------------------------------------
static bool IsCurrSysPathValid = false;


//--------------------------------------------------------------------------------------------------
/**
 * List of system indices in secure storage.
 */
//--------------------------------------------------------------------------------------------------
static le_sls_List_t SecStoreSystems = LE_SLS_LIST_INIT;

//--------------------------------------------------------------------------------------------------
/**
 * Pool of system indices.
 */
//--------------------------------------------------------------------------------------------------
static le_mem_PoolRef_t SystemIndexPool = NULL;

//--------------------------------------------------------------------------------------------------
/**
 * Index of last good legato system.
 */
//--------------------------------------------------------------------------------------------------
static int LastGoodSystemIndex = -1;

//--------------------------------------------------------------------------------------------------
/**
 * A systems index object.
 */
//--------------------------------------------------------------------------------------------------
typedef struct
{
    int index;                              ///< System index value.
    le_sls_Link_t link;                     ///< Link in systems list.
}
SystemsIndex_t;

#else /* not LE_CONFIG_LINUX */

// No concept of system versions without SOTA, so use an empty current system path.
#define CURR_SYS_PATH ""

#endif /* end LE_CONFIG_LINUX */

#if !MK_CONFIG_SECSTORE_DISABLE_ADMIN

//--------------------------------------------------------------------------------------------------
/**
 * Entries iterator object.
 */
//--------------------------------------------------------------------------------------------------
typedef struct
{
    le_sls_List_t entryList;        ///< List of entries for this iterator.
    le_sls_Link_t* currEntryPtr;    ///< Current entry for the interator.
    le_msg_SessionRef_t sessionRef; ///< Session reference for this iterator.
}
EntryIter_t;


//--------------------------------------------------------------------------------------------------
/**
 * Pool of entry iterators.
 */
//--------------------------------------------------------------------------------------------------
static le_mem_PoolRef_t EntryIterPool = NULL;


//--------------------------------------------------------------------------------------------------
/**
 * Safe reference map of entry iterators to help validate external accesses to this API.
 */
//--------------------------------------------------------------------------------------------------
static le_ref_MapRef_t EntryIterMap = NULL;


//--------------------------------------------------------------------------------------------------
/**
 * An entry object.
 */
//--------------------------------------------------------------------------------------------------
typedef struct
{
    char path[SECSTORE_MAX_PATH_BYTES]; ///< Entry name.
    bool isDir;                         ///< true if the entry is a directory, otherwise the entry
                                        ///< is a file.
    le_sls_Link_t link;                 ///< Link in entries iterator.
}
Entry_t;


//--------------------------------------------------------------------------------------------------
/**
 * Pool of entry objects.
 */
//--------------------------------------------------------------------------------------------------
static le_mem_PoolRef_t EntryPool = NULL;

#endif /* end !MK_CONFIG_SECSTORE_DISABLE_ADMIN */

#if LE_CONFIG_LINUX

//--------------------------------------------------------------------------------------------------
/**
 * Checks if the specified system index is in the list.
 *
 * @return
 *      true if the index exists.
 *      false otherwise.
 */
//--------------------------------------------------------------------------------------------------
static bool IsSystemInList
(
    int index,                      ///< [IN] Index to check.
    le_sls_List_t* listPtr          ///< [IN] List to check.
)
{
    le_sls_Link_t* linkPtr = le_sls_Peek(listPtr);

    while (linkPtr != NULL)
    {
        SystemsIndex_t* indexPtr = CONTAINER_OF(linkPtr, SystemsIndex_t, link);

        if (indexPtr->index == index)
        {
            return true;
        }

        linkPtr = le_sls_PeekNext(listPtr, linkPtr);
    }

    return false;
}

//--------------------------------------------------------------------------------------------------
/**
 * Adds a system index into the list of secure storage system indices.
 */
//--------------------------------------------------------------------------------------------------
static void AddSystemToList
(
    const char* entryPathPtr,       ///< [IN] Entry path.
    const char* indexStr,           ///< [IN] System index as a string.
    bool isDir,                     ///< [IN] true if the entry is a directory, otherwise entry is a
                                    ///       file.
    void* contextPtr                ///< [IN] Contains the list to add the index too.
)
{
    LE_UNUSED(entryPathPtr);
    le_sls_List_t* listPtr = (le_sls_List_t*)contextPtr;

    int index;

    if (le_utf8_ParseInt(&index, indexStr) == LE_OK)
    {
        // Do not add duplicates.
        if (!IsSystemInList(index, listPtr))
        {
            SystemsIndex_t* sysIndexPtr = le_mem_ForceAlloc(SystemIndexPool);

            sysIndexPtr->index = index;
            sysIndexPtr->link = LE_SLS_LINK_INIT;

            le_sls_Queue(listPtr, &(sysIndexPtr->link));
        }
    }
    else
    {
        LE_ERROR("Unexpected system index '%s' in secure storage.", indexStr);
    }
}


//--------------------------------------------------------------------------------------------------
/**
 * Clear system index list.
 */
//--------------------------------------------------------------------------------------------------
static void ClearSystemList
(
    le_sls_List_t* listPtr          ///< [IN] List to clear.
)
{
    le_sls_Link_t* linkPtr = le_sls_Pop(listPtr);

    while (linkPtr != NULL)
    {
        SystemsIndex_t* indexPtr = CONTAINER_OF(linkPtr, SystemsIndex_t, link);

        le_mem_Release(indexPtr);

        linkPtr = le_sls_Pop(listPtr);
    }
}


//--------------------------------------------------------------------------------------------------
/**
 * Find the specified system index's ancestor.  This is the largest index value that is smaller than
 * or equal to the specified index.
 *
 * @return
 *      Index of the ancestor.
 *      -1 if no ancestor can be found.
 */
//--------------------------------------------------------------------------------------------------
static int FindAncestorSys
(
    int index,                      ///< [IN] Index to check.
    le_sls_List_t* listPtr          ///< [IN] List to check.
)
{
    int ancestorIndex = -1;

    le_sls_Link_t* linkPtr = le_sls_Peek(listPtr);

    while (linkPtr != NULL)
    {
        SystemsIndex_t* indexPtr = CONTAINER_OF(linkPtr, SystemsIndex_t, link);

        if ( (indexPtr->index <= index) && (indexPtr->index > ancestorIndex) )
        {
            ancestorIndex = indexPtr->index;
        }

        linkPtr = le_sls_PeekNext(listPtr, linkPtr);
    }

    return ancestorIndex;
}


//--------------------------------------------------------------------------------------------------
/**
 * Sets the current system for secure storage.
 *
 * @return
 *      LE_OK if successful.
 *      LE_UNAVAILABLE if the secure storage is currently unavailable.
 *      LE_FAULT if there was some other error.
 */
//--------------------------------------------------------------------------------------------------
static le_result_t SetCurrSystem
(
    int currIndex                   ///< [IN] Index to use as the current system.
)
{
    // Set the current system's secure store path.
    LE_FATAL_IF(snprintf(CurrSysPath,
                         sizeof(CurrSysPath),
                         "%s/%d/apps",
                         SYS_PATH,
                         currIndex) >= sizeof(CurrSysPath),
                "Secure storage path '%s...' is too long.", CurrSysPath);

    // Get the path to the current system's secure storage hash.
    char secSysHashPath[SECSTORE_MAX_PATH_BYTES] = "";
    LE_FATAL_IF(snprintf(secSysHashPath,
                         sizeof(secSysHashPath),
                         "%s/%d/hash",
                         SYS_PATH,
                         currIndex) >= sizeof(secSysHashPath),
                "Secure storage path '%s...' is too long.", secSysHashPath);

    // Get the current system's hash.
    char currHash[MD5_STR_BYTES];
    le_result_t result = le_update_GetSystemHash(currIndex, currHash, sizeof(currHash));

    LE_FATAL_IF(result != LE_OK,
                "Could not get the current system's hash.  %s.",
                LE_RESULT_TXT(result));

    if (IsSystemInList(currIndex, &SecStoreSystems))
    {
        // Get the secure storage system's hash.
        char secSysHash[MD5_STR_BYTES] = {0};
        size_t hashLen = sizeof(secSysHash);
        bool isReadOnly = (0 == access("/legato/systems/current/read-only", R_OK));

        result = pa_secStore_Read(secSysHashPath, (uint8_t*)secSysHash, &hashLen);

        LE_FATAL_IF(result == LE_OVERFLOW, "Hash value from '%s' is too long.", secSysHashPath);

        if (result == LE_OK)
        {
            // Compare the hashes.
            if (strncmp(currHash, secSysHash, sizeof(currHash)) == 0)
            {
                return LE_OK;
            }
            LE_WARN("Hash values of '%s' mismatch, deleting its content.", secSysHashPath);
            LE_WARN("Current hash is '%s', secStore hash is '%s'", currHash, secSysHash);
        }
        else
        {
            LE_WARN("Hash '%s' is unreadable, deleting the content", secSysHashPath);
        }

        // This system is invalid and needs to be deleted.
        result = pa_secStore_Delete(CurrSysPath);
        if ((!isReadOnly) && (LE_NOT_FOUND == result))
        {
            LE_ERROR("Could not find entry '%s'.", CurrSysPath);
        }
    }

    // Find the ancestor index to create the system from.
    int ancestorIndex = FindAncestorSys(currIndex, &SecStoreSystems);

    // In some cases the ancestor might have the same index as the current system:
    // no need to create the system directory in this case.
    if ((ancestorIndex != -1) && (ancestorIndex != currIndex))
    {
        // Copy all the files from the ancestor to our current system.
        char ancestorPath[SECSTORE_MAX_PATH_BYTES];

        LE_FATAL_IF(snprintf(ancestorPath,
                             sizeof(ancestorPath),
                             "%s/%d/apps",
                             SYS_PATH,
                             ancestorIndex) >= sizeof(ancestorPath),
                    "Secure storage path '%s...' is too long.",
                    ancestorPath);

        if (-1 == LastGoodSystemIndex)
        {
            // If there is only one system then we can just do a move instead of a copy.
            LE_INFO("Creating current system from system index %d.", ancestorIndex);
            result = pa_secStore_Move(CurrSysPath, ancestorPath);
        }
        else
        {
            LE_INFO("Copying system index %d to current system.", ancestorIndex);
            result = pa_secStore_Copy(CurrSysPath, ancestorPath);
        }
    }
    else
    {
        // No ancestor so start fresh.
        result = LE_OK;
    }

    // Store the hash value for this system.
    pa_secStore_Write(secSysHashPath, (uint8_t*)currHash, strlen(currHash) + 1);

    // Current index path is created, so add it to the link if it was not there.
    if (!IsSystemInList(currIndex, &SecStoreSystems))
    {
        SystemsIndex_t* sysIndexPtr = le_mem_ForceAlloc(SystemIndexPool);
        sysIndexPtr->index = currIndex;
        sysIndexPtr->link = LE_SLS_LINK_INIT;
        le_sls_Queue(&SecStoreSystems, &(sysIndexPtr->link));
    }

    return result;
}


//--------------------------------------------------------------------------------------------------
/**
 * Removes old, unused systems from secure storage.
 */
//--------------------------------------------------------------------------------------------------
static void RemoveOldSystems
(
    int currIndex                   ///< [IN] Index of the current system.
)
{
    int ancestorIndex = -1;

    if (-1 != LastGoodSystemIndex)
    {
        //We need to keep the ancestor of last good system ,otherwise a rollback may leave us
        //with nothing to go back to.
        ancestorIndex = FindAncestorSys(LastGoodSystemIndex, &SecStoreSystems);
        LE_INFO("Retaining system index %d for later use.", ancestorIndex);
    }

    // Delete all secure storage systems not in the systems list.
    le_sls_Link_t* secLinkPtr = le_sls_Peek(&SecStoreSystems);

    while (secLinkPtr != NULL)
    {
        SystemsIndex_t* secIndexPtr = CONTAINER_OF(secLinkPtr, SystemsIndex_t, link);

        if ((secIndexPtr->index != currIndex) &&
             (secIndexPtr->index != ancestorIndex))
        {
            // Delete this system from sec store.
            char path[SECSTORE_MAX_PATH_BYTES];

            LE_FATAL_IF(snprintf(path, sizeof(path), "%s/%d",
                        SYS_PATH, secIndexPtr->index) >= sizeof(path),
                        "Secure storage path '%s...' is too long.", path);

            le_result_t result = pa_secStore_Delete(path);

            if (result != LE_OK)
            {
                LE_ERROR("Could not delete old systems.");
            }
        }

        secLinkPtr = le_sls_PeekNext(&SecStoreSystems, secLinkPtr);
    }
}


//--------------------------------------------------------------------------------------------------
/**
 * Initialize the secure storage to use the current system of apps.
 *
 * @return
 *      LE_OK if successful.
 *      LE_UNAVAILABLE if the secure storage is currently unavailable.
 *      LE_FAULT if there was some other error.
 */
//--------------------------------------------------------------------------------------------------
static le_result_t InitSystems
(
    void
)
{
    // Get the current system index.
    int currIndex = le_update_GetCurrentSysIndex();

    // Get last good system index.
    LastGoodSystemIndex = le_update_GetPreviousSystemIndex(currIndex);

    LE_INFO("current system index=%d,  last good system index=%d", currIndex, LastGoodSystemIndex);

    // Get a list of all the systems in secure storage right now.
    le_result_t result = pa_secStore_GetEntries(SYS_PATH, AddSystemToList, &SecStoreSystems);

    if (result != LE_OK)
    {
        return result;
    }

    // Set the current system.
    result = SetCurrSystem(currIndex);

    if (result != LE_OK)
    {
        return result;
    }

    // Removed old systems.
    RemoveOldSystems(currIndex);

    // Delete the list of systems.
    ClearSystemList(&SecStoreSystems);

    IsCurrSysPathValid = true;

    return LE_OK;
}

#endif /* end LE_CONFIG_LINUX */

#if !MK_CONFIG_SECSTORE_DISABLE_ADMIN

//--------------------------------------------------------------------------------------------------
/**
 * Given an iterator safe reference, find the original object pointer.  If this cannot be done
 * attempt to kill the client.
 */
//--------------------------------------------------------------------------------------------------
#if LE_CONFIG_ENABLE_SECSTORE_ADMIN
static EntryIter_t* GetEntryIterPtr
(
    secStoreAdmin_IterRef_t iterRef  ///< [IN] The ref to translate to a pointer.
)
{
    EntryIter_t* iterPtr = le_ref_Lookup(EntryIterMap, iterRef);

    if (NULL == iterPtr)
    {
        LE_KILL_CLIENT("Iterator reference, <%p> is invalid.", iterRef);
        return NULL;
    }

    // Ensure that the reference indeed belongs to this client.
    if (iterPtr->sessionRef != secStoreAdmin_GetClientSessionRef())
    {
        LE_KILL_CLIENT("Iterator reference, <%p> does not belong to this client.", iterRef);
    }

    return iterPtr;
}
#endif

#endif /* end !MK_CONFIG_SECSTORE_DISABLE_ADMIN */

//--------------------------------------------------------------------------------------------------
/**
 * Gets the name of the currently connected client.  If the client process is part of a Legato app
 * then the name will be the name of the app.  If the client process is not part of a Legato app
 * then the name will be the process's effective user name.
 *
 * This function must be called within an IPC message handler from the client.
 *
 * @note
 *      If the caller supplied buffer is too small to fit the name of the client this function will
 *      kill the calling process.
 *
 * @return
 *      LE_OK if successful.
 *      LE_OVERFLOW if the buffer is too small to hold the client name.
 *      LE_FAULT if there was an error.
 */
//--------------------------------------------------------------------------------------------------
LE_SHARED le_result_t secStoreServer_GetClientName
(
    le_msg_SessionRef_t sessionRef, ///< [IN] Reference to the session.
    char* bufPtr,                   ///< [OUT] Buffer to store the client name.
    size_t bufSize,                 ///< [IN] Size of the buffer.
    bool* isAppPtr                  ///< [OUT] Set to true if the client is an app. May be NULL.
)
{
    // Get the client's credentials.
    pid_t pid;
    uid_t uid;

    if (le_msg_GetClientUserCreds(sessionRef, &uid, &pid) != LE_OK)
    {
        LE_CRIT("Could not get credentials for the client.");
        return LE_FAULT;
    }

    // Look up the process's application name.
    le_result_t result = le_appInfo_GetName(pid, bufPtr, bufSize);

    if (result == LE_OVERFLOW)
    {
        LE_ERROR("Buffer too small to contain the application name.");
        return result;
    }
    else if (result == LE_OK)
    {
        if (isAppPtr)
        {
            *isAppPtr = true;
        }
        return LE_OK;
    }

    // The process was not an app.  Get the user name for the process.
    if (isAppPtr)
    {
        *isAppPtr = false;
    }
    result = user_GetName(uid, bufPtr, bufSize);
    if (result == LE_OK)
    {
        return LE_OK;
    }

    // Could not get the user name.
    LE_ERROR("Could not get user name for pid %d (uid %d).", pid, uid);

    return LE_FAULT;
}


//--------------------------------------------------------------------------------------------------
/**
 * Gets the path to the client's area in secure storage.  If the client is an application the path
 * will be "/app/clientName".  If the client is not an application the path will be "/clientName".
 *
 * @note
 *      If the caller buffer is too small this function will kill the calling process.
 */
//--------------------------------------------------------------------------------------------------
static void GetClientPath
(
    const char* clientNamePtr,              ///< [IN] Name of the client.
    bool isApp,                             ///< [IN] true if the client is an app.
    char* bufPtr,                           ///< [OUT] Buffer to contain the path.
    size_t bufSize                          ///< [IN] Size of the buffer.
)
{
    le_result_t result;

    bufPtr[0] = '\0';
    result = le_path_Concat("/", bufPtr, bufSize,
                (isApp ? CURR_SYS_PATH : USERS_PATH), clientNamePtr, (void *) NULL);
    LE_FATAL_IF(result != LE_OK, "Buffer too small for secure storage path for %s.", clientNamePtr);
}

#if !MK_CONFIG_SECSTORE_DISABLE_LIMIT
#define MAX_CLIENT_LIMIT_NUM 64

typedef struct
{
    char key[SECSTORE_MAX_PATH_BYTES];
    int value;
} MapContext_t;

//--------------------------------------------------------------------------------------------------
/**
 * Hash map of client limit
 */
//--------------------------------------------------------------------------------------------------
static le_hashmap_Ref_t clientLimitMap = NULL;

//--------------------------------------------------------------------------------------------------
/**
 * Pool of Map Data
 */
//--------------------------------------------------------------------------------------------------
static le_mem_PoolRef_t mapDataPool = NULL;

//--------------------------------------------------------------------------------------------------
/**
 * Checks if there is enough space in the client's area of secure storage for the client to write
 * the item.
 *
 * @return
 *      LE_OK if the item would fit in the client's area of secure storage.
 *      LE_NO_MEMORY if there is not enough memory to store the item.
 *      LE_UNAVAILABLE if the secure storage is currently unavailable.
 *      LE_FAULT if there was an error.
 */
//--------------------------------------------------------------------------------------------------
static le_result_t CheckClientLimit
(
    const char* clientNamePtr,              ///< [IN] Name of the client.
    const char* clientPathPtr,              ///< [IN] Path to the client's area in secure storage.
    const char* itemNamePtr,                ///< [IN] Name of the item.
    size_t itemSize                         ///< [IN] Size, in bytes, of the item.
)
{
    // Get the secure storage limit for the client.
    appCfg_Iter_t iter = appCfg_FindApp(clientNamePtr);
    if (!iter)
    {
       LE_ERROR("iter is NULL");
       return LE_FAULT;
    }
    size_t secStoreLimit = appCfg_GetSecStoreLimit(iter);
    appCfg_DeleteIter(iter);

    // Get the current amount of space used by the client.
    le_result_t result;
    size_t usedSpace = 0;

    // Add map to decrease the pa_secStore_GetSize() calling times
    MapContext_t* map = (MapContext_t *)le_hashmap_Get(clientLimitMap, clientPathPtr);
    if(NULL == map)
    {
        result = pa_secStore_GetSize(clientPathPtr, &usedSpace);

        if ( (result != LE_OK) && (result != LE_NOT_FOUND) )
        {
            return result;
        }

        map = (MapContext_t *) le_mem_TryAlloc(mapDataPool);
        if(map)
        {
            strcpy(map->key, clientPathPtr);
            map->value = usedSpace;
            le_hashmap_Put(clientLimitMap, map->key, map);
        }
    }
    else
    {
        usedSpace = map->value;
    }

    // Get the size of the item in the secure storage if it already exists.
    char itemPath[SECSTORE_MAX_PATH_BYTES] = "";

    LE_FATAL_IF(le_path_Concat("/", itemPath, sizeof(itemPath),
        clientPathPtr, itemNamePtr, (void *) NULL) != LE_OK,
            "Client %s's path for item %s is too long.", clientNamePtr, itemNamePtr);

    size_t origItemSize = 0;
    result = pa_secStore_GetSize(itemPath, &origItemSize);

    if ( (result != LE_OK) && (result != LE_NOT_FOUND) )
    {
        return result;
    }

    // Calculate if replacing the item would fit within the limit.
    if (((ssize_t)(secStoreLimit - usedSpace + origItemSize - itemSize)) >= 0)
    {
        MapContext_t* map = (MapContext_t *)le_hashmap_Get(clientLimitMap, clientPathPtr);
        if(map)
        {
            map->value += itemSize;
            map->value -= origItemSize;
        }
        return LE_OK;
    }

    return LE_NO_MEMORY;
}

#endif /* end !MK_CONFIG_SECSTORE_DISABLE_LIMIT */

//--------------------------------------------------------------------------------------------------
/**
 * Check that item names are valid.
 *
 * @return
 *      true if the item name is valid.
 *      false otherwise.
 */
//--------------------------------------------------------------------------------------------------
static bool IsValidName
(
    const char* namePtr             ///< [IN] Name of the secure storage item.
)
{
    if (namePtr == NULL)
    {
        return false;
    }

    int nameLen = strlen(namePtr);

    if (nameLen == 0)
    {
        LE_ERROR("Name cannot be empty.");
        return false;
    }

    if (nameLen > LE_SECSTORE_MAX_NAME_SIZE)
    {
        LE_ERROR("Name is too long.");
        return false;
    }

    if (namePtr[nameLen-1] == '/')
    {
        LE_ERROR("Name cannot end with a separator '/'.");
        return false;
    }

    return true;
}

//--------------------------------------------------------------------------------------------------
/**
 * Prepare for a read or write operation, including constructing the path and initializing the
 * system if necessary.
 *
 * @return
 *      LE_OK if successful.
 *      LE_NO_MEMORY if there is not enough memory to store the item.
 *      LE_UNAVAILABLE if the secure storage is currently unavailable.
 *      LE_FAULT if there was some other error.
 */
//--------------------------------------------------------------------------------------------------
static le_result_t PrepareOp
(
    bool             isGlobal,              ///< [IN]  Is this an operation is the global domain?
    bool             isSupportSpecialName,  ///< [IN]  Is the special name "*" supported?
    const char      *name,                  ///< [IN]  Name of the secure storage item.
    size_t           bufNumElements,        ///< [IN]  Size of buffer.
    bool             checkLimit,            ///< [IN]  Check buffer size against client's secure
                                            ///<       storage limit?
    char            *path                   ///< [OUT] Buffer to write constructed path into. Must be
                                            ///<       SECSTORE_MAX_PATH_BYTES in size.
)
{
    le_result_t result = LE_OK;

    // Check parameters.
    if (!IsValidName(name))
    {
        LE_KILL_CLIENT("Item name is invalid.");
        return LE_FAULT;
    }

    // Supported only for deleting secure storage contents.
    // If the operation is not in global domain, "*" is a special item name indicating all the
    // contents for an application. Replace "*" with empty name which will be later translated to
    // application name as path while passing to the platform adaptor.
    if (strcmp(name, "*") == 0)
    {
        if ((isGlobal == false) && (isSupportSpecialName == true))
        {
            name = "";
        }
        else
        {
            LE_ERROR("Special name '*' is not supported with this secure storage operation");
            return LE_FAULT;
        }
    }
#if LE_CONFIG_LINUX
    // Make sure systems are initialized.
    if (!IsCurrSysPathValid)
    {
        result = InitSystems();

        if (result != LE_OK)
        {
            return result;
        }
    }
#endif /* end LE_CONFIG_LINUX */

#if MK_CONFIG_SECSTORE_DISABLE_GLOBAL_ACCESS
    LE_UNUSED(isGlobal);
#else /* !MK_CONFIG_SECSTORE_DISABLE_GLOBAL_ACCESS */
    if (isGlobal)
    {
        // Build global path based on prefix and item name.
        LE_FATAL_IF(le_path_Concat("/", path, SECSTORE_MAX_PATH_BYTES, GLOBAL_PATH, name, NULL)
            != LE_OK, "Global path for item %s is too long.", name);
    }
    else
#endif /* end !MK_CONFIG_SECSTORE_DISABLE_GLOBAL_ACCESS */
    {
        // Get the client's name and see if it is an app.
        bool isApp;
        char clientName[LIMIT_MAX_USER_NAME_BYTES];

        if (secStoreServer_GetClientName(le_secStore_GetClientSessionRef(), clientName,
                                         sizeof(clientName), &isApp) != LE_OK)
        {
            LE_KILL_CLIENT("Could not get the client's name.");
            return LE_FAULT;
        }

        // Get the path to the client's secure storage area.
        GetClientPath(clientName, isApp, path, SECSTORE_MAX_PATH_BYTES);

#if MK_CONFIG_SECSTORE_DISABLE_LIMIT
        LE_UNUSED(checkLimit);
#else /* !MK_CONFIG_SECSTORE_DISABLE_LIMIT */
        // need to clean map at delete flow
        if(0 == bufNumElements)
        {
            MapContext_t* map = (MapContext_t *)le_hashmap_Get(clientLimitMap, path);
            if(map)
            {
                le_hashmap_Remove(clientLimitMap, map->key);
                le_mem_Release(map);
            }
        }
        if (checkLimit)
        {
            // Check the available limit for the client.
            result = CheckClientLimit(clientName, path, name, bufNumElements);
            if (result != LE_OK)
            {
                return result;
            }
        }
#endif /* end !MK_CONFIG_SECSTORE_DISABLE_LIMIT */

        // Append item name to client path.
        LE_FATAL_IF(le_path_Concat("/", path, SECSTORE_MAX_PATH_BYTES, name, (void *) NULL)
            != LE_OK, "Client %s's path for item %s is too long.", clientName, name);
    }

    return result;
}

//--------------------------------------------------------------------------------------------------
/**
 * Writes an item to secure storage.  If the item already exists then it will be overwritten with
 * the new value.  If the item does not already exist then it will be created.  Specifying 0 for
 * buffer size means emptying an existing file or creating a 0-byte file.
 *
 * @return
 *      LE_OK if successful.
 *      LE_NO_MEMORY if there is not enough memory to store the item.
 *      LE_UNAVAILABLE if the secure storage is currently unavailable.
 *      LE_FAULT if there was some other error.
 */
//--------------------------------------------------------------------------------------------------
static le_result_t Write
(
    bool isGlobal,                  ///< [IN] Is this an operation is the global domain?
    const char* name,               ///< [IN] Name of the secure storage item.
    const uint8_t* bufPtr,          ///< [IN] Buffer contain the data to store.
    size_t bufNumElements           ///< [IN] Size of buffer.
)
{
    char        path[SECSTORE_MAX_PATH_BYTES] = {0};
    le_result_t result;

    LE_ASSERT(bufPtr != NULL);

    result  = PrepareOp(isGlobal, false, name, bufNumElements, true, path);
    if (result != LE_OK)
    {
        return result;
    }

    // Write the item to the secure storage.
    result = pa_secStore_Write(path, bufPtr, bufNumElements);

    if (result == LE_BAD_PARAMETER)
    {
        return LE_FAULT;
    }
    return result;
}

//--------------------------------------------------------------------------------------------------
/**
 * Writes an item to secure storage.  If the item already exists then it will be overwritten with
 * the new value.  If the item does not already exist then it will be created.  Specifying 0 for
 * buffer size means emptying an existing file or creating a 0-byte file.
 *
 * @return
 *      LE_OK if successful.
 *      LE_NO_MEMORY if there is not enough memory to store the item.
 *      LE_UNAVAILABLE if the secure storage is currently unavailable.
 *      LE_FAULT if there was some other error.
 */
//--------------------------------------------------------------------------------------------------
le_result_t le_secStore_Write
(
    const char* name,               ///< [IN] Name of the secure storage item.
    const uint8_t* bufPtr,          ///< [IN] Buffer contain the data to store.
    size_t bufNumElements           ///< [IN] Size of buffer.
)
{
    return Write(false, name, bufPtr, bufNumElements);
}

#if !MK_CONFIG_SECSTORE_DISABLE_GLOBAL_ACCESS

//--------------------------------------------------------------------------------------------------
/**
 * Writes an item to secure storage.  If the item already exists then it will be overwritten with
 * the new value.  If the item does not already exist then it will be created.  Specifying 0 for
 * buffer size means emptying an existing file or creating a 0-byte file.
 *
 * @return
 *      LE_OK if successful.
 *      LE_NO_MEMORY if there is not enough memory to store the item.
 *      LE_UNAVAILABLE if the secure storage is currently unavailable.
 *      LE_FAULT if there was some other error.
 */
//--------------------------------------------------------------------------------------------------
le_result_t secStoreGlobal_Write
(
    const char* name,               ///< [IN] Name of the secure storage item.
    const uint8_t* bufPtr,          ///< [IN] Buffer contain the data to store.
    size_t bufNumElements           ///< [IN] Size of buffer.
)
{
    return Write(true, name, bufPtr, bufNumElements);
}

#endif /* end !MK_CONFIG_SECSTORE_ENABLE_GLOBAL_ACCESS */

//--------------------------------------------------------------------------------------------------
/**
 * Reads an item from secure storage.
 *
 * @return
 *      LE_OK if successful.
 *      LE_OVERFLOW if the buffer is too small to hold the entire item.  No data will be written to
 *                  the buffer in this case.
 *      LE_NOT_FOUND if the item does not exist.
 *      LE_UNAVAILABLE if the secure storage is currently unavailable.
 *      LE_FAULT if there was some other error.
 */
//--------------------------------------------------------------------------------------------------
static le_result_t Read
(
    bool isGlobal,                  ///< [IN] Is this an operation is the global domain?
    const char* name,               ///< [IN] Name of the secure storage item.
    uint8_t* bufPtr,                ///< [OUT] Buffer to store the data in.
    size_t* bufNumElementsPtr       ///< [INOUT] Size of buffer.
)
{
    char        path[SECSTORE_MAX_PATH_BYTES] = {0};
    le_result_t result;

    LE_ASSERT(bufPtr != NULL);
    LE_ASSERT(bufNumElementsPtr != NULL);

    result = PrepareOp(isGlobal, false, name, *bufNumElementsPtr, false, path);
    if (result != LE_OK)
    {
        return result;
    }

    // Read the item from the secure storage.
    result = pa_secStore_Read(path, bufPtr, bufNumElementsPtr);

    // If there is an error, make sure that the buffer is empty.
    if ( (LE_OK != result) && (bufNumElementsPtr > 0) )
    {
        bufPtr[0] = 0;
        *bufNumElementsPtr = 0;
    }

    return result;
}

//--------------------------------------------------------------------------------------------------
/**
 * Reads an item from secure storage.
 *
 * @return
 *      LE_OK if successful.
 *      LE_OVERFLOW if the buffer is too small to hold the entire item.  No data will be written to
 *                  the buffer in this case.
 *      LE_NOT_FOUND if the item does not exist.
 *      LE_UNAVAILABLE if the secure storage is currently unavailable.
 *      LE_FAULT if there was some other error.
 */
//--------------------------------------------------------------------------------------------------
le_result_t le_secStore_Read
(
    const char* name,               ///< [IN] Name of the secure storage item.
    uint8_t* bufPtr,                ///< [OUT] Buffer to store the data in.
    size_t* bufNumElementsPtr       ///< [INOUT] Size of buffer.
)
{
    return Read(false, name, bufPtr, bufNumElementsPtr);
}

#if !MK_CONFIG_SECSTORE_DISABLE_GLOBAL_ACCESS

//--------------------------------------------------------------------------------------------------
/**
 * Reads an item from secure storage.
 *
 * @return
 *      LE_OK if successful.
 *      LE_OVERFLOW if the buffer is too small to hold the entire item.  No data will be written to
 *                  the buffer in this case.
 *      LE_NOT_FOUND if the item does not exist.
 *      LE_UNAVAILABLE if the secure storage is currently unavailable.
 *      LE_FAULT if there was some other error.
 */
//--------------------------------------------------------------------------------------------------
le_result_t secStoreGlobal_Read
(
    const char* name,               ///< [IN] Name of the secure storage item.
    uint8_t* bufPtr,                ///< [OUT] Buffer to store the data in.
    size_t* bufNumElementsPtr       ///< [INOUT] Size of buffer.
)
{
    return Read(true, name, bufPtr, bufNumElementsPtr);
}

#endif /* end !MK_CONFIG_SECSTORE_DISABLE_GLOBAL_ACCESS */

//--------------------------------------------------------------------------------------------------
/**
 * Deletes an item from secure storage.
 *
 * @return
 *      LE_OK if successful.
 *      LE_NOT_FOUND if the item does not exist.
 *      LE_UNAVAILABLE if the secure storage is currently unavailable.
 *      LE_FAULT if there was some other error.
 */
//--------------------------------------------------------------------------------------------------
le_result_t Delete
(
    bool isGlobal,      ///< [IN] Is this an operation is the global domain?
    const char* name    ///< [IN] Name of the secure storage item.
)
{
    char path[SECSTORE_MAX_PATH_BYTES] = {0};
    le_result_t result = PrepareOp(isGlobal, true, name, 0, false, path);
    if (result != LE_OK)
    {
        return result;
    }

    // Delete the item from the secure storage.
    return pa_secStore_Delete(path);
}

//--------------------------------------------------------------------------------------------------
/**
 * Deletes an item from secure storage.
 *
 * @return
 *      LE_OK if successful.
 *      LE_NOT_FOUND if the item does not exist.
 *      LE_UNAVAILABLE if the secure storage is currently unavailable.
 *      LE_FAULT if there was some other error.
 */
//--------------------------------------------------------------------------------------------------
le_result_t le_secStore_Delete
(
    const char* name    ///< [IN] Name of the secure storage item.
)
{
    return Delete(false, name);
}

#if !MK_CONFIG_SECSTORE_DISABLE_GLOBAL_ACCESS

//--------------------------------------------------------------------------------------------------
/**
 * Deletes an item from secure storage.
 *
 * @return
 *      LE_OK if successful.
 *      LE_NOT_FOUND if the item does not exist.
 *      LE_UNAVAILABLE if the secure storage is currently unavailable.
 *      LE_FAULT if there was some other error.
 */
//--------------------------------------------------------------------------------------------------
le_result_t secStoreGlobal_Delete
(
    const char* name    ///< [IN] Name of the secure storage item.
)
{
    return Delete(true, name);
}

#endif /* end !MK_CONFIG_SECSTORE_DISABLE_GLOBAL_ACCESS */

//--------------------------------------------------------------------------------------------------
/**
 * Gets the size of the buffer required to read an item from the secure storage.
 * It can be actual size of the data, or some slightly greater number.
 *
 * @return
 *      LE_OK if successful.
 *      LE_NOT_FOUND if the path doesn't exist.
 *      LE_UNAVAILABLE if the secure storage is currently unavailable.
 *      LE_FAULT if there was some other error.
 */
static le_result_t GetMinimumBufferSize
(
    bool isGlobal,      ///< [IN] Is this an operation is the global domain?
    const char* name,   ///< [IN] Name of the secure storage item.
    uint32_t* sizePtr   ///< [OUT] Number that is equal to or greater than
                        ///<   the size of the item, in bytes.
)
{

    char path[SECSTORE_MAX_PATH_BYTES] = {0};
    le_result_t result = PrepareOp(isGlobal, true, name, 0, false, path);
    if (result != LE_OK)
    {
        return result;
    }

    size_t size = 0;

    // TODO: replace with more efficient call
    // (to avoid decryption of the data and/or iteration through descendent nodes)
    result = pa_secStore_GetSize(path, &size);

    *sizePtr = (uint32_t) size;

    return result;
}

//--------------------------------------------------------------------------------------------------
/**
 * Gets the size of the buffer required to read an item from the secure storage.
 * It can be actual size of the data, or some slightly greater number.
 *
 * @return
 *      LE_OK if successful.
 *      LE_NOT_FOUND if the path doesn't exist.
 *      LE_UNAVAILABLE if the secure storage is currently unavailable.
 *      LE_FAULT if there was some other error.
 */
//--------------------------------------------------------------------------------------------------
le_result_t le_secStore_GetMinimumBufferSize
(
    const char* name,   ///< [IN] Name of the secure storage item.
    uint32_t* sizePtr   ///< [OUT] Number that is equal to or greater than
                        ///<   the size of the item, in bytes.
)
{
    return GetMinimumBufferSize(false, name, sizePtr);
}

#if !MK_CONFIG_SECSTORE_DISABLE_GLOBAL_ACCESS

//--------------------------------------------------------------------------------------------------
/**
 * Gets the size of the buffer required to read an item from the secure storage.
 * It can be actual size of the data, or some slightly greater number.
 *
 * @return
 *      LE_OK if successful.
 *      LE_NOT_FOUND if the path doesn't exist.
 *      LE_UNAVAILABLE if the secure storage is currently unavailable.
 *      LE_FAULT if there was some other error.
 */
//--------------------------------------------------------------------------------------------------
le_result_t secStoreGlobal_GetMinimumBufferSize
(
    const char* name,   ///< [IN] Name of the secure storage item.
    uint32_t* sizePtr   ///< [OUT] Number that is equal to or greater than
                        ///<   the size of the item, in bytes.
)
{
    return GetMinimumBufferSize(true, name, sizePtr);
}

#endif /* end !MK_CONFIG_SECSTORE_DISABLE_GLOBAL_ACCESS */

//--------------------------------------------------------------------------------------------------
/**
 * Start the "batch write" that aggregates multiple write/delete operation into a single batch
 * with the purpose of improving the performance.
 *
 * The performance is optimized by postponing the data serialization (triggered by write/delete
 * API calls by this particular client) until the function EndBatchWrite is called.
 *
 * @return
 *      LE_OK if successful.
 *      LE_FAULT if there was error.
 */
//--------------------------------------------------------------------------------------------------
le_result_t le_secStore_StartBatchWrite
(
    void
)
{
    return LE_OK;
}

#if !MK_CONFIG_SECSTORE_DISABLE_GLOBAL_ACCESS

//--------------------------------------------------------------------------------------------------
/**
 * Start the "batch write" that aggregates multiple write/delete operation into a single batch
 * with the purpose of improving the performance.
 *
 * The performance is optimized by postponing the data serialization (triggered by write/delete
 * API calls by this particular client) until the function EndBatchWrite is called.
 *
 * @return
 *      LE_OK if successful.
 *      LE_FAULT if there was error.
 */
//--------------------------------------------------------------------------------------------------
le_result_t secStoreGlobal_StartBatchWrite
(
    void
)
{
    return LE_OK;
}

#endif /* end !MK_CONFIG_SECSTORE_DISABLE_GLOBAL_ACCESS */

//--------------------------------------------------------------------------------------------------
/**
 * Ends the "batch write" operation and serializes the data to the persistent storage.
 *
 * @note  - Failure to finish the (previously started) batch write may result in data loss.
 *        - This is not a transactional mechanism, i.e. the possibility to roll back the changes
 *          is not provided.
 *
 * @return
 *      LE_OK if successful.
 *      LE_FAULT if there was error.
 */
//--------------------------------------------------------------------------------------------------
le_result_t le_secStore_EndBatchWrite
(
    void
)
{
    return LE_OK;
}

#if !MK_CONFIG_SECSTORE_DISABLE_GLOBAL_ACCESS

//--------------------------------------------------------------------------------------------------
/**
 * Ends the "batch write" operation and serializes the data to the persistent storage.
 *
 * @note  - Failure to finish the (previously started) batch write may result in data loss.
 *        - This is not a transactional mechanism, i.e. the possibility to roll back the changes
 *          is not provided.
 *
 * @return
 *      LE_OK if successful.
 *      LE_FAULT if there was error.
 */
//--------------------------------------------------------------------------------------------------
le_result_t secStoreGlobal_EndBatchWrite
(
    void
)
{
    return LE_OK;
}

#endif /* end !MK_CONFIG_SECSTORE_DISABLE_GLOBAL_ACCESS */

#if !MK_CONFIG_SECSTORE_DISABLE_ADMIN

//--------------------------------------------------------------------------------------------------
/**
 * Checks whether an entry is already in an entry list.
 *
 * @return
 *      true if the entry is already in the the list.
 *      false otherwise.
 */
//--------------------------------------------------------------------------------------------------
#if LE_CONFIG_ENABLE_SECSTORE_ADMIN
static bool IsInEntryList
(
    const char* entry,                  ///< [IN] Entry.
    le_sls_List_t* entryListPtr         ///< [IN] Entry list.
)
{
    le_sls_Link_t* linkPtr = le_sls_Peek(entryListPtr);

    while (linkPtr != NULL)
    {
        Entry_t* entryPtr = CONTAINER_OF(linkPtr, Entry_t, link);

        if (strncmp(entryPtr->path, entry, SECSTORE_MAX_PATH_BYTES) == 0)
        {
            return true;
        }

        linkPtr = le_sls_PeekNext(entryListPtr, linkPtr);
    }

    return false;
}
#endif

//--------------------------------------------------------------------------------------------------
/**
 * Check a secure storage path is valid.
 *
 * @return
 *      true if the item name is valid.
 *      false otherwise.
 */
//--------------------------------------------------------------------------------------------------
static bool IsValidPath
(
    const char* pathPtr,            ///< [IN] Path in secure storage.
    bool mustBeFile                 ///< [IN] If true the path must not end with a separator, else
                                    ///       the path may end with a separator.
)
{
    if (pathPtr == NULL)
    {
        return false;
    }

    int pathLen = strlen(pathPtr);

    if (pathLen == 0)
    {
        LE_ERROR("Path cannot be empty.");
        return false;
    }

    if (pathLen > SECSTOREADMIN_MAX_PATH_SIZE)
    {
        LE_ERROR("Path is too long.");
        return false;
    }

    if (pathPtr[0] != '/')
    {
        LE_ERROR("Path is not absolute.");
        return false;
    }

    if ( (mustBeFile) && (pathPtr[pathLen-1] == '/') )
    {
        LE_ERROR("Path cannot end with a separator '/'.");
        return false;
    }

    return true;
}


//--------------------------------------------------------------------------------------------------
/**
 * Deletes an iterator.
 */
//--------------------------------------------------------------------------------------------------
static void DeleteIter
(
    EntryIter_t* iterPtr            ///< [IN] Iterator to delete.
)
{
    // Free each entry in the list.
    le_sls_Link_t* entryLinkPtr = le_sls_Pop(&(iterPtr->entryList));

    while (entryLinkPtr != NULL)
    {
        Entry_t* entryPtr = CONTAINER_OF(entryLinkPtr, Entry_t, link);

        le_mem_Release(entryPtr);

        entryLinkPtr = le_sls_Pop(&(iterPtr->entryList));
    }

    // Free the list.
    le_mem_Release(iterPtr);
}


//--------------------------------------------------------------------------------------------------
/**
 * Cleans up all of the iterator data for a specific session.
 */
//--------------------------------------------------------------------------------------------------
static void CleanupClientIterators
(
    le_msg_SessionRef_t sessionRef,
    void*               contextPtr
)
{
    // Iterate over the safe references and delete all iterator objects for this session reference.
    le_ref_IterRef_t safeRefIter = le_ref_GetIterator(EntryIterMap);

    le_result_t result;

    while ((result = le_ref_NextNode(safeRefIter)) == LE_OK)
    {
        EntryIter_t* entryListPtr = (EntryIter_t*)le_ref_GetValue(safeRefIter);

        if (entryListPtr->sessionRef == sessionRef)
        {
            // Delete the safe reference.
            le_ref_DeleteRef(EntryIterMap, (void*)le_ref_GetSafeRef(safeRefIter));

            DeleteIter(entryListPtr);
        }
    }

    LE_FATAL_IF(result == LE_FAULT, "Error iterating over safe reference.");
}

//--------------------------------------------------------------------------------------------------
/**
 * Create an iterator for listing entries in secure storage under the specified path.
 *
 * @return
 *      An iterator reference if successful.
 *      NULL if there is an error.
 */
//--------------------------------------------------------------------------------------------------
#if LE_CONFIG_ENABLE_SECSTORE_ADMIN
static void StoreEntry
(
    const char* entryPathPtr,       ///< [IN] Entry path.
    const char* entryNamePtr,       ///< [IN] Entry name.
    bool isDir,                     ///< [IN] true if the entry is a directory, otherwise entry is a
                                    ///       file.
    void* contextPtr                ///< [IN] Pointer to the context supplied to pa_secStore_GetEntries()
)
{
    LE_UNUSED(entryPathPtr);
    EntryIter_t* iterPtr = contextPtr;

    // Do not add duplicates.
    if (!IsInEntryList(entryNamePtr, &(iterPtr->entryList)))
    {
        // Add the entry to the iterator.
        Entry_t* entryPtr = le_mem_ForceAlloc(EntryPool);
        entryPtr->link = LE_SLS_LINK_INIT;

        LE_ASSERT(le_utf8_Copy(entryPtr->path,
                               entryNamePtr,
                               SECSTORE_MAX_PATH_BYTES,
                               NULL) == LE_OK);
        entryPtr->isDir = isDir;

        le_sls_Queue(&(iterPtr->entryList), &(entryPtr->link));
    }
}
#endif

//--------------------------------------------------------------------------------------------------
/**
 * Create an iterator for listing entries in secure storage under the specified path.
 *
 * @return
 *      An iterator reference if successful.
 *      NULL if there is an error.
 */
//--------------------------------------------------------------------------------------------------
secStoreAdmin_IterRef_t secStoreAdmin_CreateIter
(
    const char* path
        ///< [IN]
        ///< Path to iterate over.
)
{
#if LE_CONFIG_ENABLE_SECSTORE_ADMIN
    // Check parameters.
    if (!IsValidPath(path, false))
    {
        LE_KILL_CLIENT("Path is invalid.");
        return NULL;
    }

    // Create a snap shot of the entire list of entries for this path now so we don't need to worry
    // about concurrency issues.
    EntryIter_t* iterPtr = le_mem_ForceAlloc(EntryIterPool);
    iterPtr->entryList = LE_SLS_LIST_INIT;
    iterPtr->currEntryPtr = NULL;

    if (pa_secStore_GetEntries(path, StoreEntry, iterPtr) != LE_OK)
    {
        le_mem_Release(iterPtr);
        return NULL;
    }

    // Store the client's session reference.
    iterPtr->sessionRef = secStoreAdmin_GetClientSessionRef();

    // Create the saference for this iterator.
    return le_ref_CreateRef(EntryIterMap, iterPtr);
#else
    return NULL;
#endif
}


//--------------------------------------------------------------------------------------------------
/**
 * Deletes an iterator.
 */
//--------------------------------------------------------------------------------------------------
void secStoreAdmin_DeleteIter
(
    secStoreAdmin_IterRef_t iterRef
        ///< [IN]
        ///< Iterator reference.
)
{
#if LE_CONFIG_ENABLE_SECSTORE_ADMIN
    // Get the iterator from the safe reference.
    EntryIter_t* iterPtr = GetEntryIterPtr(iterRef);

    if (iterPtr == NULL)
    {
        // Already killed client, just need to return from this function.
        return;
    }

    // Delete the safe reference.
    le_ref_DeleteRef(EntryIterMap, iterRef);

    DeleteIter(iterPtr);
#endif
}


//--------------------------------------------------------------------------------------------------
/**
 * Go to the next entry in the iterator.  This should be called at least once before accessing the
 * entry.  After the first time this function is called successfully on an iterator the first entry
 * will be available.
 *
 * @return
 *      LE_OK if successful.
 *      LE_NOT_FOUND if there are no more entries available.
 */
//--------------------------------------------------------------------------------------------------
le_result_t secStoreAdmin_Next
(
    secStoreAdmin_IterRef_t iterRef
        ///< [IN]
        ///< Iterator reference.
)
{
#if LE_CONFIG_ENABLE_SECSTORE_ADMIN
    // Get the iterator from the safe reference.
    EntryIter_t* iterPtr = GetEntryIterPtr(iterRef);

    if (iterPtr == NULL)
    {
        // Already killed client, just need to return from this function.
        return LE_FAULT;
    }

    // Get the next entry.
    if (iterPtr->currEntryPtr == NULL)
    {
        iterPtr->currEntryPtr = le_sls_Peek(&(iterPtr->entryList));
    }
    else
    {
        iterPtr->currEntryPtr = le_sls_PeekNext(&(iterPtr->entryList), iterPtr->currEntryPtr);
    }

    if (iterPtr->currEntryPtr == NULL)
    {
        return LE_NOT_FOUND;
    }

    return LE_OK;
#else
    return LE_UNSUPPORTED;
#endif
}


//--------------------------------------------------------------------------------------------------
/**
 * Get the current entry's name.
 *
 * @return
 *      LE_OK if successful.
 *      LE_OVERFLOW if the buffer is too small to hold the entry name.
 *      LE_FAULT if there was some other error.
 */
//--------------------------------------------------------------------------------------------------
le_result_t secStoreAdmin_GetEntry
(
    secStoreAdmin_IterRef_t iterRef,
        ///< [IN]
        ///< Iterator reference.

    char* name,
        ///< [OUT]
        ///< Buffer to store the entry name.

    size_t nameNumElements,
        ///< [IN]

    bool* isDir
        ///< [OUT]
        ///< True if the entry is a directory, false otherwise.
)
{
#if LE_CONFIG_ENABLE_SECSTORE_ADMIN
    if (name == NULL)
    {
        LE_KILL_CLIENT("name buffer is NULL.");
        return LE_FAULT;
    }

    if (isDir == NULL)
    {
        LE_KILL_CLIENT("isDir is NULL.");
        return LE_FAULT;
    }

    // Get the iterator from the safe reference.
    EntryIter_t* iterPtr = GetEntryIterPtr(iterRef);

    if (NULL == iterPtr)
    {
        // Already killed client, just need to return from this function.
        return LE_FAULT;
    }

    // Check if there is a current entry.
    if (NULL == iterPtr->currEntryPtr)
    {
        LE_KILL_CLIENT("No current entry in iterator.");
        return LE_FAULT;
    }

    // Get the entry name.
    Entry_t* entryPtr = CONTAINER_OF(iterPtr->currEntryPtr, Entry_t, link);

    *isDir = entryPtr->isDir;
    return le_utf8_Copy(name, entryPtr->path, nameNumElements, NULL);
#else
    return LE_UNSUPPORTED;
#endif
}


//--------------------------------------------------------------------------------------------------
/**
 * Writes a buffer of data into the specified path in secure storage.  If the item already exists,
 * it'll be overwritten with the new value. If the item doesn't already exist, it'll be created.
 *
 * @note
 *      The specified path must be an absolute path.
 *
 * @return
 *      LE_OK if successful.
 *      LE_NO_MEMORY if there isn't enough memory to store the item.
 *      LE_UNAVAILABLE if the secure storage is currently unavailable.
 *      LE_BAD_PARAMETER if the path cannot be written to because it is a directory or ot would
 *                       result in an invalid path.
 *      LE_FAULT if there was some other error.
 */
//--------------------------------------------------------------------------------------------------
le_result_t secStoreAdmin_Write
(
    const char* path,
        ///< [IN]
        ///< Path of the secure storage item.

    const uint8_t* bufPtr,
        ///< [IN]
        ///< Buffer containing the data to store.

    size_t bufNumElements
        ///< [IN]
)
{
#if LE_CONFIG_ENABLE_SECSTORE_ADMIN
    // Check parameters.
    if (!IsValidPath(path, true))
    {
        LE_KILL_CLIENT("Path is invalid.");
        return LE_FAULT;
    }

    if (bufPtr == NULL)
    {
        LE_KILL_CLIENT("Client buffer should not be NULL.");
        return LE_FAULT;
    }

    // Write the item to the secure storage.
    return pa_secStore_Write(path, bufPtr, bufNumElements);
#else
    return LE_UNSUPPORTED;
#endif
}


//--------------------------------------------------------------------------------------------------
/**
 * Reads an item from secure storage.
 *
 * @note
 *      The specified path must be an absolute path.
 *
 * @return
 *      LE_OK if successful.
 *      LE_OVERFLOW if the buffer is too small to hold the entire item. No data will be written to
 *                  the buffer in this case.
 *      LE_NOT_FOUND if the item doesn't exist.
 *      LE_UNAVAILABLE if the secure storage is currently unavailable.
 *      LE_FAULT if there was some other error.
 */
//--------------------------------------------------------------------------------------------------
le_result_t secStoreAdmin_Read
(
    const char* path,
        ///< [IN]
        ///< Path of the secure storage item.

    uint8_t* bufPtr,
        ///< [OUT]
        ///< Buffer to store the data in.

    size_t* bufNumElementsPtr
        ///< [INOUT]
)
{
#if LE_CONFIG_ENABLE_SECSTORE_ADMIN
    // Check parameters.
    if (!IsValidPath(path, true))
    {
        LE_KILL_CLIENT("Path is invalid.");
        return LE_FAULT;
    }

    if (bufPtr == NULL)
    {
        LE_KILL_CLIENT("Client buffer should not be NULL.");
        return LE_FAULT;
    }

    // Read the item from the secure storage.
    return pa_secStore_Read(path, bufPtr, bufNumElementsPtr);
#else
    return LE_UNSUPPORTED;
#endif
}


//--------------------------------------------------------------------------------------------------
/**
 * Copy the meta file to the specified path.
 *
 * @return
 *      LE_OK if successful.
 *      LE_NOT_FOUND if the meta file does not exist.
 *      LE_UNAVAILABLE if the sfs is currently unavailable.
 *      LE_FAULT if there was some other error.
 */
//--------------------------------------------------------------------------------------------------
le_result_t secStoreAdmin_CopyMetaTo
(
    const char* path
        ///< [IN]
        ///< Destination path of meta file copy.
)
{
#if LE_CONFIG_ENABLE_SECSTORE_ADMIN
    return pa_secStore_CopyMetaTo(path);
#else
    return LE_UNSUPPORTED;
#endif
}


//--------------------------------------------------------------------------------------------------
/**
 * Recursively deletes all items under the specified path and the specified path from secure
 * storage.
 *
 * @note
 *      The specified path must be an absolute path.
 *
 * @return
 *      LE_OK if successful.
 *      LE_NOT_FOUND if the path doesn't exist.
 *      LE_UNAVAILABLE if the secure storage is currently unavailable.
 *      LE_FAULT if there was some other error.
 */
//--------------------------------------------------------------------------------------------------
le_result_t secStoreAdmin_Delete
(
    const char* path
        ///< [IN]
        ///< Path of the secure storage item.
)
{
#if LE_CONFIG_ENABLE_SECSTORE_ADMIN
    // Check parameters.
    if (!IsValidPath(path, false))
    {
        LE_KILL_CLIENT("Path is invalid.");
        return LE_FAULT;
    }

    // Delete the item from the secure storage.
    return pa_secStore_Delete(path);
#else
    return LE_UNSUPPORTED;
#endif
}


//--------------------------------------------------------------------------------------------------
/**
 * Gets the size, in bytes, of all items under the specified path.
 *
 * @note
 *      The specified path must be an absolute path.
 *
 * @return
 *      LE_OK if successful.
 *      LE_NOT_FOUND if the path doesn't exist.
 *      LE_UNAVAILABLE if the secure storage is currently unavailable.
 *      LE_FAULT if there was some other error.
 */
//--------------------------------------------------------------------------------------------------
le_result_t secStoreAdmin_GetSize
(
    const char* path,
        ///< [IN]
        ///< Path of the secure storage item.

    uint64_t* sizePtr
        ///< [OUT]
        ///< Size in bytes of all items in the path.
)
{
    // Check parameters.
    if (!IsValidPath(path, false))
    {
        LE_KILL_CLIENT("Path is invalid.");
        return LE_FAULT;
    }

    if (sizePtr == NULL)
    {
        LE_KILL_CLIENT("sizePtr is NULL.");
        return LE_FAULT;
    }

    // Delete the item from the secure storage.
    size_t size = 0;
    le_result_t result = pa_secStore_GetSize(path, &size);

    *sizePtr = size;

    return result;
}


//--------------------------------------------------------------------------------------------------
/**
 * Gets the total space and the available free space in secure storage.
 *
 * @return
 *      LE_OK if successful.
 *      LE_UNAVAILABLE if the secure storage is currently unavailable.
 *      LE_FAULT if there was some other error.
 */
//--------------------------------------------------------------------------------------------------
le_result_t secStoreAdmin_GetTotalSpace
(
    uint64_t* totalSizePtr,
        ///< [OUT]
        ///< Total size, in bytes, of secure storage.

    uint64_t* freeSizePtr
        ///< [OUT]
        ///< Free space, in bytes, in secure storage.
)
{
    if (totalSizePtr == NULL)
    {
        LE_KILL_CLIENT("totalSizePtr is NULL.");
        return LE_FAULT;
    }

    if (freeSizePtr == NULL)
    {
        LE_KILL_CLIENT("freeSizePtr is NULL.");
        return LE_FAULT;
    }

    size_t totalSize = 0, freeSize = 0;
    le_result_t result = pa_secStore_GetTotalSpace(&totalSize, &freeSize);

    *totalSizePtr = totalSize;
    *freeSizePtr = freeSize;

    return result;
}

#endif /* end !MK_CONFIG_SECSTORE_DISABLE_ADMIN */

#if LE_CONFIG_LINUX

//--------------------------------------------------------------------------------------------------
/**
 * Update secure storage
 * This must be called once NV restore is done or app installation/un-installation is done.
 */
//--------------------------------------------------------------------------------------------------
static void SecStoreUpdate
(
    void
)
{
    // First rebuild meta hash in PA level.
    pa_secStore_ReInitSecStorage();

    // Then re-initialize index based current secStore APP path.
    if (IsCurrSysPathValid)
    {
        IsCurrSysPathValid = 0;
    }
}

//--------------------------------------------------------------------------------------------------
/**
 * Event handler for NV restoration done.
 *
 */
//--------------------------------------------------------------------------------------------------
static void RestoreHandler
(
    pa_restore_status_t* statusPtr
)
{
    if (*statusPtr == PA_RESTORE_SUCCESS)
    {
        LE_INFO("Secure storage restore succeeded, rebuild legato secure storage ...");

        SecStoreUpdate();
    }
    else
    {
        LE_WARN("Secure storage restore failed.");
    }

    // The reportPtr is a reference counted object, so need to release it
    le_mem_Release(statusPtr);
}

//--------------------------------------------------------------------------------------------------
/**
 * Legato Event handler for app installation done
 */
//--------------------------------------------------------------------------------------------------
static void AppInstallHandler
(
    const char * installAppNamePtr,
    void * contextPtr
)
{
    LE_INFO("legato system is updated because APP '%s' installation is done !", installAppNamePtr);
    LE_INFO("rebuild legato secure storage ...");

    SecStoreUpdate();
}

//--------------------------------------------------------------------------------------------------
/**
 * Event handler for app uninstallation done
 */
//--------------------------------------------------------------------------------------------------
static void AppUninstallHandler
(
    const char * uninstallAppNamePtr,
    void * contextPtr
)
{
    LE_INFO("legato system is updated because APP '%s' un-installation is done !",
            uninstallAppNamePtr);
    LE_INFO("rebuild legato secure storage ...");

    SecStoreUpdate();
}

#endif /* end LE_CONFIG_LINUX */

//--------------------------------------------------------------------------------------------------
/**
 * The secure storage daemon's initialization function.
 */
//--------------------------------------------------------------------------------------------------
COMPONENT_INIT
{
#if !MK_CONFIG_SECSTORE_DISABLE_ADMIN
    EntryIterMap = le_ref_CreateMap("EntryIterMap", 1);

    EntryIterPool = le_mem_CreatePool("EntryIterPool", sizeof(EntryIter_t));
    EntryPool = le_mem_CreatePool("EntryPool", sizeof(Entry_t));

    // Register a handler that will clean up client specific data when clients disconnect.
    le_msg_AddServiceCloseHandler(secStoreAdmin_GetServiceRef(),
                                  CleanupClientIterators,
                                  NULL);
#endif /* end !MK_CONFIG_SECSTORE_DISABLE_ADMIN */

#if !MK_CONFIG_SECSTORE_DISABLE_LIMIT
    mapDataPool = le_mem_CreatePool("MapDataPool", sizeof(MapContext_t));

    le_mem_ExpandPool(mapDataPool, MAX_CLIENT_LIMIT_NUM);

    clientLimitMap = le_hashmap_Create  ( "ClientLimitMap", MAX_CLIENT_LIMIT_NUM,
                    le_hashmap_HashString, le_hashmap_EqualsString);
#endif

#if LE_CONFIG_LINUX
    SystemIndexPool = le_mem_CreatePool("SystemIndexPool", sizeof(SystemsIndex_t));

    // Register a handler function for secure storage restore indication.
    pa_secStore_SetRestoreHandler(RestoreHandler);

    // Register handlers for app installation/un-installiation events to update secure storage PATH.
    le_instStat_AddAppInstallEventHandler(AppInstallHandler, NULL);
    le_instStat_AddAppUninstallEventHandler(AppUninstallHandler, NULL);
#endif /* end LE_CONFIG_LINUX */

    // Try to kick a couple of times before each timeout.
    le_clk_Time_t watchdogInterval = { .sec = MS_WDOG_INTERVAL };
    le_wdogChain_Init(1);
    le_wdogChain_MonitorEventLoop(0, watchdogInterval);
}
