#!/bin/sh

# WINEPREFIX=$HOME/.wine_steam wine reg.exe ADD "HKEY_CURRENT_USER\Software\Wine\DllOverrides" /v "gameoverlayrenderer" /t "REG_SZ" /d "" /f
# WINEPREFIX=$HOME/.wine_steam wine reg.exe ADD "HKEY_CURRENT_USER\Software\Wine\DllOverrides" /v "gameoverlayrenderer64" /t "REG_SZ" /d "" /f
# WINEPREFIX=$HOME/.wine_steam wine reg.exe ADD "HKEY_CURRENT_USER\Software\Wine\AppDefaults\steamwebhelper.exe\DllOverrides" /v "d3d11" /t "REG_SZ" /d "builtin" /f
# WINEPREFIX=$HOME/.wine_steam wine reg.exe ADD "HKEY_CURRENT_USER\Software\Wine\AppDefaults\steamwebhelper.exe\DllOverrides" /v "dxgi" /t "REG_SZ" /d "builtin" /f
# WINEPREFIX=$HOME/.wine_steam winetricks -q nocrashdialog

DXVK_HUD=full WINEESYNC=1 WINE_LARGE_ADDRESS_AWARE=0 WINEDEBUG=-all WINEPREFIX=$HOME/.wine_steam /usr/local/bin/wine "$HOME/.wine_steam/drive_c/Program Files (x86)/Steam/steam.exe" -cef-disable-sandbox -cef-in-process-gpu -cef-disable-gpu-compositing -cef-single-process
