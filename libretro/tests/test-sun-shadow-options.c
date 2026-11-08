/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "ui/libretro_core_options.h"

static unsigned api, language, registered, found, registrations;
static const char key[]="xemu_doaxvb_sun_shadows";
static void v2(const struct retro_core_option_v2_definition *defs)
{
    for(;defs->key;defs++,registered++)if(!strcmp(defs->key,key)) {
        found++;assert(!strcmp(defs->default_value,"disabled"));
        assert(!strcmp(defs->category_key,"video"));
        assert(!strcmp(defs->values[0].value,"disabled") && !strcmp(defs->values[1].value,"enabled"));
    }
}
static void v1(const struct retro_core_option_definition *defs)
{
    for(;defs->key;defs++,registered++)if(!strcmp(defs->key,key)) {
        found++;assert(!strcmp(defs->default_value,"disabled"));
    }
}
static bool environment(unsigned cmd,void *data)
{
    switch(cmd) {
    case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION: *(unsigned*)data=api;return true;
    case RETRO_ENVIRONMENT_GET_LANGUAGE: *(unsigned*)data=language;return true;
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2: registrations++;v2(((struct retro_core_options_v2*)data)->definitions);return true;
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2_INTL: registrations++;v2(((struct retro_core_options_v2_intl*)data)->us->definitions);return true;
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS: registrations++;v1(data);return true;
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_INTL: registrations++;v1(((struct retro_core_options_intl*)data)->us);return true;
    case RETRO_ENVIRONMENT_SET_VARIABLES: {
        const struct retro_variable *vars=data;registrations++;
        for(;vars->key;vars++,registered++)if(!strcmp(vars->key,key)) {
            found++;assert(strstr(vars->value,"; disabled|enabled"));
        }
        return true;
    }
    default: assert(!"Option registration must not rely on frontend hiding support");return false;
    }
}
int main(void)
{
    unsigned cases=0;
    for(api=0;api<=2;api++)for(unsigned lang=0;lang<2;lang++) {
        language=lang?RETRO_LANGUAGE_JAPANESE:RETRO_LANGUAGE_ENGLISH;
        unsigned baseline=0;
        for(unsigned phase=0;phase<5;phase++) {
            bool supported=phase==1 || phase==3,categories=false;
            libretro_set_sun_option_definition(supported);
            registered=found=registrations=0;libretro_set_core_options(environment,&categories);
            assert(registrations==1 && found==supported);
            if(!phase)baseline=registered;
            assert(registered==baseline+(supported?1:0));cases++;
        }
    }
    printf("Sun option: %u API/language/availability transitions, absent unsupported key and Off default passed\n",cases);
    return 0;
}
