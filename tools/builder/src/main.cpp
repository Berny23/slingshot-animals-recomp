        qputenv("QT_QPA_PLATFORMTHEME", "xdgdesktopportal");
#endif
#ifdef SLINGSHOT_BREEZE
    // Breeze unless a style is asked for (-style, QT_STYLE_OVERRIDE).
    bool own_style = qEnvironmentVariableIsSet("QT_STYLE_OVERRIDE");
    for (int i = 1; i < argc; ++i)
        own_style |= !std::strcmp(argv[i], "-style") || !std::strncmp(argv[i], "-style=", 7) ||
                     !std::strcmp(argv[i], "--style") || !std::strncmp(argv[i], "--style=", 8);
