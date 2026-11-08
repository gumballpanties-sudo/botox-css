// backtrack chams cfg rows, spliced into config.cpp kEntries (old 10-31 keys, so old cfgs load)
    { "chamsBacktrack",          CfgEntry::STR,  g_chamsBacktrackCfg, sizeof(g_chamsBacktrackCfg) },
    { "chamsBacktrackXqz",       CfgEntry::BOOL, &g_chamsBacktrackXqz, 0 },
    { "chamsBacktrackType",      CfgEntry::INT,  &g_chamsBacktrackType, 0 },
    { "chamsBacktrackMaxGhosts", CfgEntry::INT,  &g_chamsBacktrackMaxGhosts, 0 },
    { "chamsBacktrackGradient",  CfgEntry::BOOL, &g_chamsBacktrackGradient, 0 },
