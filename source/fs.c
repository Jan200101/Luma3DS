/*
*   This file is part of Luma3DS
*   Copyright (C) 2016-2017 Aurora Wright, TuxSH
*
*   This program is free software: you can redistribute it and/or modify
*   it under the terms of the GNU General Public License as published by
*   the Free Software Foundation, either version 3 of the License, or
*   (at your option) any later version.
*
*   This program is distributed in the hope that it will be useful,
*   but WITHOUT ANY WARRANTY; without even the implied warranty of
*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*   GNU General Public License for more details.
*
*   You should have received a copy of the GNU General Public License
*   along with this program.  If not, see <http://www.gnu.org/licenses/>.
*
*   Additional Terms 7.b and 7.c of GPLv3 apply to this file:
*       * Requiring preservation of specified reasonable legal notices or
*         author attributions in that material or in the Appropriate Legal
*         Notices displayed by works containing it.
*       * Prohibiting misrepresentation of the origin of that material,
*         or requiring that modified versions of such material be marked in
*         reasonable ways as different from the original version.
*/

#include "fs.h"
#include "memory.h"
#include "strings.h"
#include "fmt.h"
#include "crypto.h"
#include "cache.h"
#include "screen.h"
#include "draw.h"
#include "utils.h"
#include "fatfs/ff.h"
#include "buttons.h"
#include "firm.h"
#include "crypto.h"
#include "../build/bundled.h"

static FATFS sdFs,
             nandFs;

static bool switchToMainDir(bool isSd)
{
    const char *mainDir = isSd ? "/luma" : "/rw/luma";

    switch(f_chdir(mainDir))
    {
        case FR_OK:
            return true;
        case FR_NO_PATH:
            f_mkdir(mainDir);
            return switchToMainDir(isSd);
        default:
            return false;
    }
}

bool mountFs(bool isSd, bool switchToCtrNand)
{
    return isSd ? f_mount(&sdFs, "0:", 1) == FR_OK && switchToMainDir(true) :
                  f_mount(&nandFs, "1:", 1) == FR_OK && (!switchToCtrNand || (f_chdrive("1:") == FR_OK && switchToMainDir(false)));
}

u32 fileRead(void *dest, const char *path, u32 maxSize)
{
    FIL file;
    u32 ret = 0;

    if(f_open(&file, path, FA_READ) != FR_OK) return ret;

    u32 size = f_size(&file);
    if(dest == NULL) ret = size;
    else if(size <= maxSize)
        f_read(&file, dest, size, (unsigned int *)&ret);
    f_close(&file);

    return ret;
}

u32 getFileSize(const char *path)
{
    return fileRead(NULL, path, 0);
}

bool fileWrite(const void *buffer, const char *path, u32 size)
{
    FIL file;

    switch(f_open(&file, path, FA_WRITE | FA_OPEN_ALWAYS))
    {
        case FR_OK:
        {
            unsigned int written;
            f_write(&file, buffer, size, &written);
            f_truncate(&file);
            f_close(&file);

            return (u32)written == size;
        }
        case FR_NO_PATH:
            for(u32 i = 1; path[i] != 0; i++)
                if(path[i] == '/')
                {
                    char folder[i + 1];
                    memcpy(folder, path, i);
                    folder[i] = 0;
                    f_mkdir(folder);
                }

            return fileWrite(buffer, path, size);
        default:
            return false;
    }
}

void fileDelete(const char *path)
{
    f_unlink(path);
}

bool findPayload()
{
    return false;
}

bool payloadMenu()
{
    DIR dir;

    if(f_opendir(&dir, "payloads") != FR_OK) return false;

    FILINFO info;
    u32 payloadNum = 0;
    char payloadList[20][49];

    while(f_readdir(&dir, &info) == FR_OK && info.fname[0] != 0 && payloadNum < 20)
    {
        if(info.fname[0] == '.') continue;

        u32 nameLength = strlen(info.fname);

        if(nameLength < 6 || nameLength > 52) continue;

        nameLength -= 5;

        if(memcmp(info.fname + nameLength, ".firm", 5) != 0) continue;

        memcpy(payloadList[payloadNum], info.fname, nameLength);
        payloadList[payloadNum][nameLength] = 0;
        payloadNum++;
    }

    f_closedir(&dir);

    if(!payloadNum) return false;

    return false;
}

u32 firmRead(void *dest, u32 firmType)
{
    const char *firmFolders[][2] = {{"00000002", "20000002"},
                                    {"00000102", "20000102"},
                                    {"00000202", "20000202"},
                                    {"00000003", "20000003"},
                                    {"00000001", "20000001"}};

    char folderPath[35],
         path[48];

    sprintf(folderPath, "1:/title/00040138/%s/content", firmFolders[firmType][ISN3DS ? 1 : 0]);

    DIR dir;
    u32 firmVersion = 0xFFFFFFFF;

    if(f_opendir(&dir, folderPath) != FR_OK) goto exit;

    FILINFO info;

    //Parse the target directory
    while(f_readdir(&dir, &info) == FR_OK && info.fname[0] != 0)
    {
        //Not a cxi
        if(info.fname[9] != 'a' || strlen(info.fname) != 12) continue;

        u32 tempVersion = hexAtoi(info.altname, 8);

        //Found an older cxi
        if(tempVersion < firmVersion) firmVersion = tempVersion;
    }

    f_closedir(&dir);

    if(firmVersion == 0xFFFFFFFF) goto exit;

    //Complete the string with the .app name
    sprintf(path, "%s/%08x.app", folderPath, firmVersion);

    if(fileRead(dest, path, 0x400000 + sizeof(Cxi) + 0x200) <= sizeof(Cxi) + 0x400) firmVersion = 0xFFFFFFFF;

exit:
    return firmVersion;
}

void findDumpFile(const char *folderPath, char *fileName)
{
    DIR dir;
    FRESULT result;

    for(u32 n = 0; n <= 99999999; n++)
    {
        FILINFO info;

        sprintf(fileName, "crash_dump_%08u.dmp", n);
        result = f_findfirst(&dir, &info, folderPath, fileName);

        if(result != FR_OK || !info.fname[0]) break;
    }

    if(result == FR_OK) f_closedir(&dir);
}
