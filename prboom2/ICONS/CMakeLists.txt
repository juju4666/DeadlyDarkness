if(UNIX AND NOT APPLE)
    install(FILES begotten.desktop DESTINATION share/applications)
    # placeholder icon (dsda's): replace ICONS/dsda-doom.svg/.ico with your own art
    install(FILES dsda-doom.svg DESTINATION share/icons/hicolor/scalable/apps RENAME begotten.svg)
    # appimagetool wants a PNG icon at the top of the AppDir as well (it makes .DirIcon from it); an SVG alone fails the AppImage build
    install(FILES begotten-256.png DESTINATION share/icons/hicolor/256x256/apps RENAME begotten.png)
endif()
