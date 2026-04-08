#!/bin/sh

# WINEPREFIX=$HOME/.wine_steam wine reg.exe ADD "HKEY_CURRENT_USER\Software\Wine\DllOverrides" /v "gameoverlayrenderer" /t "REG_SZ" /d "" /f
# WINEPREFIX=$HOME/.wine_steam wine reg.exe ADD "HKEY_CURRENT_USER\Software\Wine\DllOverrides" /v "gameoverlayrenderer64" /t "REG_SZ" /d "" /f
# WINEPREFIX=$HOME/.wine_steam wine reg.exe ADD "HKEY_CURRENT_USER\Software\Wine\AppDefaults\steamwebhelper.exe\DllOverrides" /v "d3d11" /t "REG_SZ" /d "builtin" /f
# WINEPREFIX=$HOME/.wine_steam wine reg.exe ADD "HKEY_CURRENT_USER\Software\Wine\AppDefaults\steamwebhelper.exe\DllOverrides" /v "dxgi" /t "REG_SZ" /d "builtin" /f
# WINEPREFIX=$HOME/.wine_steam winetricks -q corefonts nocrashdialog

export WINEDEBUG="-all"
export PULSE_LATENCY_MSEC=60
export WINE_LARGE_ADDRESS_AWARE=1
export WINEPREFIX=$HOME/.wine_steam
export WINE_D3D_CONFIG="renderer=vulkan"

/usr/local/bin/wine "$HOME/.wine_steam/drive_c/Program Files (x86)/Steam/steam.exe" -cef-disable-sandbox -cef-disable-seccomp-sandbox -cef-disable-hang-timeouts -cef-disable-gpu -console
