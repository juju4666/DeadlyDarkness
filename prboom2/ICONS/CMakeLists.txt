if(UNIX AND NOT APPLE)
    install(FILES begotten.desktop DESTINATION share/applications)
    # placeholder icon (dsda's): replace ICONS/dsda-doom.svg/.ico with your own art
    install(FILES dsda-doom.svg DESTINATION share/icons/hicolor/scalable/apps RENAME begotten.svg)
endif()
