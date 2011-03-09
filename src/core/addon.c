/***************************************************************************
 *   Copyright (C) 2010~2010 by CSSlayer                                   *
 *   wengxt@gmail.com                                                      *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/

#include <limits.h>
#include <dirent.h>
#include <sys/stat.h>
#include <libintl.h>

#include "core/fcitx.h"
#include "addon.h"
#include "utils/utils.h"
#include "fcitx-config/xdg.h"

CONFIG_BINDING_BEGIN(FcitxAddon);
CONFIG_BINDING_REGISTER("Addon", "Name", name);
CONFIG_BINDING_REGISTER("Addon", "Category", category);
CONFIG_BINDING_REGISTER("Addon", "Enabled", bEnabled);
CONFIG_BINDING_REGISTER("Addon", "Library", library);
CONFIG_BINDING_REGISTER("Addon", "Type", type);
CONFIG_BINDING_REGISTER("Addon", "Dependency", depend);
CONFIG_BINDING_REGISTER("Addon", "Priority", priority);
CONFIG_BINDING_END()

static ConfigFileDesc *addonConfigDesc = NULL;
static ConfigFileDesc* GetAddonConfigDesc();
static int AddonPriorityCmp(const void* a, const void* b)
{
    FcitxAddon *aa = (FcitxAddon*)a, *ab = (FcitxAddon*)b;
    return aa->priority - ab->priority;
}

UT_array* GetFcitxAddons()
{
    const UT_icd addon_icd = {sizeof(FcitxAddon), NULL ,NULL, FreeAddon};
    static UT_array *addons = NULL;
    if (addons == NULL)
        utarray_new(addons, &addon_icd);
    return addons;
}

/** 
 * @brief Load Addon Info
 */
void LoadAddonInfo(void)
{
    char **addonPath;
    size_t len;
    char pathBuf[PATH_MAX];
    int i = 0;
    DIR *dir;
    struct dirent *drt;
    struct stat fileStat;
    UT_array* addons = GetFcitxAddons();
    utarray_done(addons);

    StringHashSet* sset = NULL;

    addonPath = GetXDGPath(&len, "XDG_CONFIG_HOME", ".config", "fcitx/addon" , DATADIR, "fcitx/data/addon" );

    for(i = 0; i< len; i++)
    {
        snprintf(pathBuf, sizeof(pathBuf), "%s", addonPath[i]);
        pathBuf[sizeof(pathBuf) - 1] = '\0';

        dir = opendir(pathBuf);
        if (dir == NULL)
            continue;

        /* collect all *.conf files */
        while((drt = readdir(dir)) != NULL)
        {
            size_t nameLen = strlen(drt->d_name);
            if (nameLen <= strlen(".conf") )
                continue;
            memset(pathBuf,0,sizeof(pathBuf));

            if (strcmp(drt->d_name + nameLen -strlen(".conf"), ".conf") != 0)
                continue;
            snprintf(pathBuf, sizeof(pathBuf), "%s/%s", addonPath[i], drt->d_name );

            if (stat(pathBuf, &fileStat) == -1)
                continue;

            if (fileStat.st_mode & S_IFREG)
            {
                StringHashSet *string;
                HASH_FIND_STR(sset, drt->d_name, string);
                if (!string)
                {
                    char *bStr = strdup(drt->d_name);
                    string = malloc(sizeof(StringHashSet));
                    memset(string, 0, sizeof(StringHashSet));
                    string->name = bStr;
                    HASH_ADD_KEYPTR(hh, sset, string->name, strlen(string->name), string);
                }
            }
        }

        closedir(dir);
    }

    char **paths = malloc(sizeof(char*) *len);
    for (i = 0;i < len ;i ++)
        paths[i] = malloc(sizeof(char) *PATH_MAX);
    StringHashSet* string;
    for (string = sset;
         string != NULL;
         string = (StringHashSet*)string->hh.next)
    {
        int i = 0;
        for (i = len -1; i >= 0; i--)
        {
            snprintf(paths[i], PATH_MAX, "%s/%s", addonPath[len - i - 1], string->name);
            FcitxLog(DEBUG, "Load Addon Config File:%s", paths[i]);
        }
        FcitxLog(INFO, _("Load Addon Config File:%s"), string->name);
        ConfigFile* cfile = ParseMultiConfigFile(paths, len, GetAddonConfigDesc());
        if (cfile)
        {
            FcitxAddon addon;
            memset(&addon, 0, sizeof(FcitxAddon));
            utarray_push_back(addons, &addon);
            FcitxAddon *a = (FcitxAddon*) utarray_back(addons);
            FcitxAddonConfigBind(a, cfile, GetAddonConfigDesc());
            ConfigBindSync((GenericConfig*)a);
            FcitxLog(DEBUG, _("Addon Config %s is %s"),string->name, (a->bEnabled)?"Enabled":"Disabled");
            if (!a->bEnabled)
                utarray_pop_back(addons);
        }
    }

    for (i = 0;i < len ;i ++)
        free(paths[i]);
    free(paths);

    FreeXDGPath(addonPath);

    StringHashSet *curStr;
    while(sset)
    {
        curStr = sset;
        HASH_DEL(sset, curStr);
        free(curStr->name);
        free(curStr);
    }
    
    utarray_sort(addons, AddonPriorityCmp);
}

void AddonResolveDependency()
{
    UT_array* addons = GetFcitxAddons();
    boolean remove = true;
    while(remove)
    {
        remove = false;
        FcitxAddon *addon;
        for ( addon = (FcitxAddon *) utarray_front(addons);
            addon != NULL;
            addon = (FcitxAddon *) utarray_next(addons, addon))
        {
            if (!addon->bEnabled)
                continue;
            UT_array* dependlist = SplitString(addon->depend);
            boolean valid = true;
            char **depend = NULL;
            for (depend = (char **) utarray_front(dependlist);
                 depend != NULL;
                 depend = (char **) utarray_next(dependlist, depend))
            {
                if (!AddonIsAvailable(*depend))
                {
                    valid = false;
                    break;
                }
            }
            
            utarray_free(dependlist);
            if (!valid)
            {
                FcitxLog(WARNING, _("Disable addon %s, dependency %s can not be satisfied."), addon->name, addon->depend);
                addon->bEnabled = false;
            }
        }
    }
}

boolean AddonIsAvailable(const char* name)
{
    UT_array* addons = GetFcitxAddons();
    FcitxAddon *addon;
    for ( addon = (FcitxAddon *) utarray_front(addons);
          addon != NULL;
          addon = (FcitxAddon *) utarray_next(addons, addon))
    {
        if (addon->bEnabled && strcmp(name, addon->name) == 0)
            return true;
    }
    return false;
}

FcitxAddon* GetAddonByName(const char* name)
{
    UT_array* addons = GetFcitxAddons();
    FcitxAddon *addon;
    for ( addon = (FcitxAddon *) utarray_front(addons);
          addon != NULL;
          addon = (FcitxAddon *) utarray_next(addons, addon))
    {
        if (addon->bEnabled && strcmp(name, addon->name) == 0)
            return addon;
    }
    return NULL;
}

/** 
 * @brief Load addon.desc file
 * 
 * @return the description of addon configure.
 */
ConfigFileDesc* GetAddonConfigDesc()
{
    if (!addonConfigDesc)
    {
        FILE *tmpfp;
        tmpfp = GetXDGFileData("addon.desc", "r", NULL);
        addonConfigDesc = ParseConfigFileDescFp(tmpfp);
        fclose(tmpfp);
    }

    return addonConfigDesc;
}

/** 
 * @brief Free one addon info
 * 
 * @param v addon info
 */
void FreeAddon(void *v)
{
    FcitxAddon *addon = (FcitxAddon*) v;
    if (!addon)
        return ;
    FreeConfigFile(addon->config.configFile);
    free(addon->name);
    free(addon->library);
}
