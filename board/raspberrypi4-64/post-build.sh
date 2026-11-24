#!/bin/sh

set -eu

# Crear directorios de la consola.
mkdir -p "${TARGET_DIR}/home/pi/emulators/gba"
mkdir -p "${TARGET_DIR}/home/pi/emulators/nes"
mkdir -p "${TARGET_DIR}/home/pi/emulators/snes"

mkdir -p "${TARGET_DIR}/home/pi/roms/gba"
mkdir -p "${TARGET_DIR}/home/pi/roms/nes"
mkdir -p "${TARGET_DIR}/home/pi/roms/snes"

mkdir -p "${TARGET_DIR}/home/pi/saves/gba"
mkdir -p "${TARGET_DIR}/home/pi/saves/nes"
mkdir -p "${TARGET_DIR}/home/pi/saves/snes"

# Eliminar el diagnóstico KMS antiguo y duplicado.
rm -f "${TARGET_DIR}/etc/init.d/S01zzkmslog"

# Limpiar residuos de la antigua expansión de la partición data.
INITTAB="${TARGET_DIR}/etc/inittab"
TEMP_FILE="${INITTAB}.tmp"

awk '
    /^sed -i .*mount -a/ { exit }
    { print }
' "${INITTAB}" > "${TEMP_FILE}"

mv "${TEMP_FILE}" "${INITTAB}"

rm -f "${TARGET_DIR}/usr/sbin/expand-data.sh"


# Ejecutar la interfaz directamente en HDMI y conservar la consola serie.
INITTAB="${TARGET_DIR}/etc/inittab"
sed -i 's|^tty1::respawn:.*$|tty1::respawn:/usr/bin/consola-ui|' "${INITTAB}"
