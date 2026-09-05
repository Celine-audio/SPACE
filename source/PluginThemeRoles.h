#pragma once

/*
    SPACE's own themeable colours, added to the house list in `ui/ThemeRoles.h`.

    There are none. Everything this window draws already has a name in the shared kit --
    the response and the EQ curve both wear the accent, and the grounds behind them are
    the house's. The macro is declared all the same, because `ui/ThemeRoles.h` expects
    every plugin to answer the question even when the answer is "nothing".

    See ui/ThemeRoles.h for the shape of an entry if that changes.
*/
#define CELINE_PLUGIN_THEME_ROLES(X)
